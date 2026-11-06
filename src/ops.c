/* Oasis - file operations engine (copy, move, delete, rename, make drawer) for Kickstart 1.3.
 * Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * Trees are walked recursively with two shared path buffers that each level extends and
 * cuts back, and every drawer's names are read in full before anything is changed in it
 * (changing a drawer while ExNext walks it is not safe on every filesystem). The stack per
 * level is small: 1.3 CLI stacks are only 4 KB. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "ops.h"

static struct OpsHooks *H;
static struct OpsStats *S;
static UBYTE *buf;
static ULONG bufsize;
static char sp[PATHMAX], dp[PATHMAX];        /* current source / destination object */
static int depth;                            /* 0 = the object the user picked */
static struct MsgPort *nodate[4];            /* handlers that refused ACTION_SET_DATE */

/* ---- names --------------------------------------------------------------------------- */

void ops_freenames(struct Names *nm)
{
    if (nm->v)
        FreeMem(nm->v, nm->cap * sizeof(struct NameEnt));
    nm->v = NULL;
    nm->n = nm->cap = 0;
}

LONG ops_list(const char *path, struct Names *nm, BOOL sorted)
{
    struct FileInfoBlock *fib = AllocMem(sizeof(*fib), MEMF_PUBLIC | MEMF_CLEAR);
    BPTR lock = 0;
    BOOL ok = FALSE;
    LONG err = 0;
    nm->v = NULL;
    nm->n = nm->cap = 0;
    if (!fib)
        return ERROR_NO_FREE_STORE;
    if ((lock = Lock((STRPTR)path, ACCESS_READ)) && Examine(lock, fib)) {
        ok = TRUE;
        while (ExNext(lock, fib)) {
            if (nm->n == nm->cap) {
                int nc = nm->cap + 32;
                struct NameEnt *v = AllocMem(nc * sizeof(*v), MEMF_PUBLIC);
                if (!v) {
                    ok = FALSE;
                    err = ERROR_NO_FREE_STORE;
                    break;
                }
                if (nm->v) {
                    CopyMem(nm->v, v, nm->n * sizeof(*v));
                    FreeMem(nm->v, nm->cap * sizeof(*v));
                }
                nm->v = v;
                nm->cap = nc;
            }
            scopy(nm->v[nm->n].name, (char *)fib->fib_FileName, 31);
            nm->v[nm->n].dir = fib->fib_DirEntryType > 0;
            nm->n++;
        }
        if (ok && IoErr() != ERROR_NO_MORE_ENTRIES) {
            ok = FALSE;
            err = IoErr();
        }
    } else
        err = IoErr();
    if (lock)
        UnLock(lock);
    FreeMem(fib, sizeof(*fib));
    if (!ok) {
        ops_freenames(nm);
        return err ? err : ERROR_OBJECT_NOT_FOUND;
    }
    if (sorted) {
        int i, j;
        for (i = 1; i < nm->n; i++) {
            struct NameEnt t = nm->v[i];
            for (j = i; j > 0 && nocase_cmp(t.name, nm->v[j - 1].name) < 0; j--)
                nm->v[j] = nm->v[j - 1];
            nm->v[j] = t;
        }
    }
    return 0;
}

/* ---- helpers ------------------------------------------------------------------------- */

static BOOL samelock(BPTR a, BPTR b)
{
    struct FileLock *la = BADDR(a), *lb = BADDR(b);
    return la->fl_Key == lb->fl_Key && la->fl_Volume == lb->fl_Volume && la->fl_Task == lb->fl_Task;
}

BOOL ops_samedir(const char *a, const char *b)
{
    BPTR la, lb;
    BOOL same = FALSE;
    dos_quiet(TRUE);
    la = Lock((STRPTR)a, ACCESS_READ);
    lb = Lock((STRPTR)b, ACCESS_READ);
    if (la && lb)
        same = samelock(la, lb);
    if (la)
        UnLock(la);
    if (lb)
        UnLock(lb);
    dos_quiet(FALSE);
    return same;
}

BOOL ops_inside(const char *inner, const char *outer)
{
    BPTR lo, l;
    BOOL in = FALSE;
    dos_quiet(TRUE);
    lo = Lock((STRPTR)outer, ACCESS_READ);
    l = lo ? Lock((STRPTR)inner, ACCESS_READ) : 0;
    while (l && !in) {
        BPTR p;
        in = samelock(l, lo);
        p = ParentDir(l);
        UnLock(l);
        l = p;
    }
    if (l)
        UnLock(l);
    if (lo)
        UnLock(lo);
    dos_quiet(FALSE);
    return in;
}

BOOL ops_isicon(const char *name, char *base, int size)
{
    int n = slen(name);
    if (n <= 5 || !ends_with_nocase(name, ".info"))
        return FALSE;
    scopy(base, name, size);
    if (n - 5 < size)
        base[n - 5] = 0;
    return TRUE;
}

static BOOL extend(char *path, const char *name)
{
    if (slen(path) + slen(name) + 2 > PATHMAX)
        return FALSE;
    path_add(path, name);
    return TRUE;
}

static int ask(const char *what, const char *path, LONG err)
{
    int a = H->error ? H->error(H->ctx, what, path, err) : ANS_SKIP;
    if (a == ANS_ABORT)
        S->aborted = TRUE;
    if (a == ANS_SKIP || a == ANS_ABORT)
        S->skipped++;
    return a;
}

static BOOL progress(int op, const char *name, ULONG done, ULONG total)
{
    if (S->aborted)
        return FALSE;
    if (H->progress && !H->progress(H->ctx, op, name, done, total))
        S->aborted = TRUE;
    return !S->aborted;
}

static const char *leaf(const char *path)
{
    const char *l = path;
    for (; *path; path++)
        if (*path == ':' || *path == '/')
            l = path + 1;
    return l;
}

/* Examine a path: 1 = drawer, 0 = file, -1 = missing (or error, in *err) */
static int kind(const char *path, struct FileInfoBlock *fib, LONG *err)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    int k = -1;
    *err = 0;
    if (!l) {
        *err = IoErr();
        if (*err == ERROR_OBJECT_NOT_FOUND)
            *err = 0;
        return -1;
    }
    if (Examine(l, fib))
        k = fib->fib_DirEntryType > 0;
    else
        *err = IoErr();
    UnLock(l);
    return k;
}

static void keepmeta(const char *path, const struct FileInfoBlock *fib)
{
    struct FileLock *fl;
    BPTR l;
    LONG err;
    int i;
    /* date first: a write-protected copy might refuse it after SetProtection */
    BOOL known = TRUE;
    if ((l = Lock((STRPTR)path, ACCESS_READ))) {
        fl = BADDR(l);
        for (i = 0; i < 4; i++)
            if (nodate[i] == fl->fl_Task)
                known = FALSE;
        UnLock(l);
        if (known) {
            err = dos_setdate(path, &fib->fib_Date);
            if (err == 0)
                S->datesSet++;
            else {
                S->datesFailed++;
                if (err == ERROR_ACTION_NOT_KNOWN && (l = Lock((STRPTR)path, ACCESS_READ))) {
                    fl = BADDR(l);
                    for (i = 0; i < 4 && nodate[i]; i++)
                        ;
                    if (i < 4)
                        nodate[i] = fl->fl_Task;   /* don't ask this handler again */
                    UnLock(l);
                }
            }
        } else
            S->datesFailed++;
    }
    if (fib->fib_Comment[0])
        SetComment((STRPTR)path, fib->fib_Comment);
    SetProtection((STRPTR)path, fib->fib_Protection);
}

/* ---- copy ------------------------------------------------------------------------------- */

/* copy file sp -> dp; fib describes sp. Returns 0, or the DOS error and *what. */
static LONG copyfile(const struct FileInfoBlock *fib, const char **what, const char **where)
{
    BPTR in, out;
    LONG n, err = 0;
    ULONG done = 0, total = fib->fib_Size;
    const char *name = leaf(sp);

    *where = sp;
    if (!buf) {
        *what = "Not enough memory to copy";
        return ERROR_NO_FREE_STORE;
    }
    if (!(in = Open((STRPTR)sp, MODE_OLDFILE))) {
        *what = "Cannot read";
        return IoErr();
    }
    if (!(out = Open((STRPTR)dp, MODE_NEWFILE))) {
        err = IoErr();
        Close(in);
        *what = "Cannot create";
        *where = dp;
        return err;
    }
    progress(OP_COPY, name, 0, total);
    while (!S->aborted) {
        n = Read(in, buf, bufsize);
        if (n < 0) {
            err = IoErr();
            *what = "Error reading";
            break;
        }
        if (n == 0)
            break;
        if (Write(out, buf, n) != n) {
            err = IoErr();
            *what = "Error writing";
            *where = dp;
            break;
        }
        done += n;
        S->bytes += n;
        progress(OP_COPY, name, done, total);
    }
    Close(in);
    Close(out);
    if (err || S->aborted) {
        DeleteFile((STRPTR)dp);              /* never leave half a file behind */
        return err;
    }
    keepmeta(dp, fib);
    S->files++;
    return 0;
}

static BOOL copyobj(void);

/* is the destination drawer the source drawer or inside it? (only checked at the top) */
static BOOL intoitself(void)
{
    char t[PATHMAX];
    if (depth)
        return FALSE;
    scopy(t, dp, PATHMAX);
    if (!path_parent(t))
        return FALSE;
    return ops_inside(t, sp);
}

static BOOL copytree(const struct FileInfoBlock *fib)
{
    struct Names nm;
    int sl = slen(sp), dl = slen(dp), i;
    BOOL clean = TRUE;
    LONG err;
    BPTR l;
    struct FileInfoBlock *dfib = AllocMem(sizeof(*dfib), MEMF_PUBLIC);
    int k;

    if (!dfib) {
        ask("Out of memory copying", sp, ERROR_NO_FREE_STORE);
        return FALSE;
    }
    /* the destination drawer: create it, or merge into an existing one */
    for (;;) {
        k = kind(dp, dfib, &err);
        if (k == 1)
            break;
        if (k == 0) {
            err = ERROR_OBJECT_EXISTS;
            if (ask("A file is in the way of", dp, err) != ANS_RETRY)
                goto fail;
            continue;
        }
        if (!err && (l = CreateDir((STRPTR)dp))) {
            UnLock(l);
            break;
        }
        if (!err)
            err = IoErr();
        if (ask("Cannot create drawer", dp, err) != ANS_RETRY)
            goto fail;
    }
    FreeMem(dfib, sizeof(*dfib));
    dfib = NULL;

    while ((err = ops_list(sp, &nm, FALSE)))
        if (ask("Cannot read drawer", sp, err) != ANS_RETRY)
            return FALSE;
    depth++;
    for (i = 0; i < nm.n && !S->aborted; i++) {
        if (!extend(sp, nm.v[i].name) || !extend(dp, nm.v[i].name)) {
            ask("Path too long", sp, ERROR_INVALID_COMPONENT_NAME);
            clean = FALSE;
        } else if (!copyobj())
            clean = FALSE;
        sp[sl] = 0;
        dp[dl] = 0;
    }
    depth--;
    ops_freenames(&nm);
    if (S->aborted)
        return FALSE;
    keepmeta(dp, fib);
    S->dirs++;
    return clean;

fail:
    if (dfib)
        FreeMem(dfib, sizeof(*dfib));
    return FALSE;
}

/* copy sp -> dp (file or whole drawer) */
static BOOL copyobj(void)
{
    struct FileInfoBlock *fib = AllocMem(sizeof(*fib), MEMF_PUBLIC);
    const char *what = "", *where = sp;
    LONG err;
    int k;
    BOOL ok = FALSE;

    if (!fib) {
        ask("Out of memory copying", sp, ERROR_NO_FREE_STORE);
        return FALSE;
    }
    for (;;) {
        k = kind(sp, fib, &err);
        if (k >= 0)
            break;
        if (ask("Cannot find", sp, err ? err : ERROR_OBJECT_NOT_FOUND) != ANS_RETRY)
            goto out;
    }
    if (k == 1) {
        if (intoitself()) {
            ask("Cannot copy a drawer into itself:", sp, ERROR_OBJECT_IN_USE);
            goto out;
        }
        ok = copytree(fib);
        goto out;
    }
    /* a file: what's at the destination? */
    {
        struct FileInfoBlock *dfib = AllocMem(sizeof(*dfib), MEMF_PUBLIC);
        int dk = dfib ? kind(dp, dfib, &err) : -1;
        if (dfib)
            FreeMem(dfib, sizeof(*dfib));
        if (dk >= 0) {
            int a;
            if (ops_samedir(sp, dp)) {
                ask("Cannot copy a file onto itself:", sp, ERROR_OBJECT_IN_USE);
                goto out;
            }
            if (dk == 1) {
                ask("A drawer is in the way of", dp, ERROR_OBJECT_EXISTS);
                goto out;
            }
            a = H->exists ? H->exists(H->ctx, dp) : ANS_REPLACE;
            if (a != ANS_REPLACE) {
                if (a == ANS_ABORT)
                    S->aborted = TRUE;
                S->skipped++;
                goto out;
            }
        }
    }
    for (;;) {
        if (!(err = copyfile(fib, &what, &where))) {
            ok = !S->aborted;
            break;
        }
        if (S->aborted || ask(what, where, err) != ANS_RETRY)
            break;
    }
out:
    FreeMem(fib, sizeof(*fib));
    return ok;
}

/* run one step on "name" and then on "name.info" if it exists and isn't the subject itself */
static BOOL withicon(BOOL (*fn)(void), const char *srcdir, const char *name, const char *dstdir)
{
    char icon[40];
    BOOL ok = FALSE;
    int pass;
    if (slen(name) > 30) {
        ask("Name too long:", name, ERROR_INVALID_COMPONENT_NAME);
        return FALSE;
    }
    for (pass = 0; pass < 2 && !S->aborted; pass++) {
        const char *n = name;
        if (pass == 1) {
            BPTR l;
            if (ends_with_nocase(name, ".info") || slen(name) > 25)
                break;
            fmt(icon, "%s.info", name);
            scopy(sp, srcdir, PATHMAX);
            path_add(sp, icon);
            dos_quiet(TRUE);
            l = Lock((STRPTR)sp, ACCESS_READ);
            dos_quiet(FALSE);
            if (!l)
                break;
            UnLock(l);
            n = icon;
        }
        scopy(sp, srcdir, PATHMAX);
        path_add(sp, n);
        if (dstdir) {
            scopy(dp, dstdir, PATHMAX);
            path_add(dp, n);
        }
        ok = fn();
        if (pass == 0 && !ok)
            return FALSE;
    }
    return !S->aborted;
}

BOOL ops_copy(const char *srcdir, const char *name, const char *dstdir)
{
    return withicon(copyobj, srcdir, name, dstdir);
}

/* ---- delete ------------------------------------------------------------------------------- */

static BOOL delobj(void)
{
    struct FileInfoBlock *fib = AllocMem(sizeof(*fib), MEMF_PUBLIC);
    LONG err;
    int k, i, sl = slen(sp);
    BOOL clean = TRUE, forced;

    if (!fib) {
        ask("Out of memory deleting", sp, ERROR_NO_FREE_STORE);
        return FALSE;
    }
    k = kind(sp, fib, &err);
    FreeMem(fib, sizeof(*fib));
    if (k < 0) {
        if (!err)
            return TRUE;                     /* already gone */
        return ask("Cannot delete", sp, err) == ANS_RETRY ? delobj() : FALSE;
    }
    if (k == 1) {
        struct Names nm;
        while ((err = ops_list(sp, &nm, FALSE)))
            if (ask("Cannot read drawer", sp, err) != ANS_RETRY)
                return FALSE;
        for (i = 0; i < nm.n && !S->aborted; i++) {
            if (!extend(sp, nm.v[i].name)) {
                ask("Path too long", sp, ERROR_INVALID_COMPONENT_NAME);
                clean = FALSE;
            } else if (!delobj())
                clean = FALSE;
            sp[sl] = 0;
        }
        ops_freenames(&nm);
        if (!clean || S->aborted)
            return FALSE;                    /* something inside stayed: keep the drawer */
    }
    if (!progress(OP_DELETE, leaf(sp), 0, 0))
        return FALSE;
    for (forced = FALSE;;) {
        int a;
        if (DeleteFile((STRPTR)sp)) {
            if (k == 1)
                S->dirs++;
            else
                S->files++;
            return TRUE;
        }
        err = IoErr();
        if (forced) {                        /* unprotected and still refused */
            ask("Cannot delete", sp, err);
            return FALSE;
        }
        a = ask("Cannot delete", sp, err);
        if (a == ANS_FORCE) {
            SetProtection((STRPTR)sp, 0);    /* rwed allowed: the bits are inverted */
            forced = TRUE;
            continue;
        }
        if (a != ANS_RETRY)
            return FALSE;
    }
}

BOOL ops_delete(const char *dir, const char *name)
{
    return withicon(delobj, dir, name, NULL);
}

/* ---- move ------------------------------------------------------------------------------------ */

static BOOL moveobj(void)
{
    struct FileInfoBlock *fib = AllocMem(sizeof(*fib), MEMF_PUBLIC);
    LONG err;
    int k;
    ULONG skipped;
    if (!fib) {
        ask("Out of memory moving", sp, ERROR_NO_FREE_STORE);
        return FALSE;
    }
    k = kind(sp, fib, &err);
    FreeMem(fib, sizeof(*fib));
    if (k < 0)
        return ask("Cannot find", sp, err ? err : ERROR_OBJECT_NOT_FOUND) == ANS_RETRY ? moveobj() : FALSE;
    if (k == 1 && intoitself()) {
        ask("Cannot move a drawer into itself:", sp, ERROR_OBJECT_IN_USE);
        return FALSE;
    }
    if (ops_samedir(sp, dp))
        return TRUE;                         /* already there */
    if (!progress(OP_MOVE, leaf(sp), 0, 0))
        return FALSE;
    /* same volume: a rename moves it instantly */
    if (Rename((STRPTR)sp, (STRPTR)dp)) {
        if (k == 1)
            S->dirs++;
        else
            S->files++;
        return TRUE;
    }
    err = IoErr();
    if (err != ERROR_RENAME_ACROSS_DEVICES && err != ERROR_OBJECT_EXISTS)
        return ask("Cannot move", sp, err) == ANS_RETRY ? moveobj() : FALSE;
    /* another volume, or merging into an existing drawer: copy, then delete what was copied */
    skipped = S->skipped;
    {
        ULONG f = S->files, d = S->dirs;
        BOOL ok = copyobj();
        S->files = f;                        /* count moved objects, not copied + deleted */
        S->dirs = d;
        if (!ok || S->aborted || S->skipped != skipped)
            return FALSE;                    /* keep the source unless all of it arrived */
        ok = delobj();
        return ok;
    }
}

BOOL ops_move(const char *srcdir, const char *name, const char *dstdir)
{
    return withicon(moveobj, srcdir, name, dstdir);
}

/* ---- single actions ----------------------------------------------------------------------------- */

LONG ops_rename(const char *dir, const char *from, const char *to)
{
    char a[PATHMAX], b[PATHMAX], ia[40], ib[40];
    BPTR l;
    if (slen(to) > 30 || !to[0])
        return ERROR_INVALID_COMPONENT_NAME;
    scopy(a, dir, PATHMAX);
    path_add(a, from);
    scopy(b, dir, PATHMAX);
    path_add(b, to);
    if (!Rename((STRPTR)a, (STRPTR)b))
        return IoErr();
    /* the icon follows, if there is one and the new name leaves room for ".info" */
    if (!ends_with_nocase(from, ".info") && slen(from) <= 25 && slen(to) <= 25) {
        fmt(ia, "%s.info", from);
        fmt(ib, "%s.info", to);
        scopy(a, dir, PATHMAX);
        path_add(a, ia);
        scopy(b, dir, PATHMAX);
        path_add(b, ib);
        dos_quiet(TRUE);
        if ((l = Lock((STRPTR)a, ACCESS_READ))) {
            UnLock(l);
            Rename((STRPTR)a, (STRPTR)b);
        }
        dos_quiet(FALSE);
    }
    return 0;
}

LONG ops_makedir(const char *dir, const char *name)
{
    char p[PATHMAX];
    BPTR l;
    if (slen(name) > 30 || !name[0])
        return ERROR_INVALID_COMPONENT_NAME;
    scopy(p, dir, PATHMAX);
    path_add(p, name);
    if (!(l = CreateDir((STRPTR)p)))
        return IoErr();
    UnLock(l);
    return 0;
}

/* ---- batches ---------------------------------------------------------------------------------------- */

void ops_begin(struct OpsHooks *hooks, struct OpsStats *stats)
{
    struct OpsStats z = { 0 };
    ULONG largest = AvailMem(MEMF_PUBLIC | MEMF_LARGEST);
    int i;
    H = hooks;
    S = stats;
    *S = z;
    for (i = 0; i < 4; i++)
        nodate[i] = NULL;
    /* a quarter of the largest free block, 4-64 KB: big enough to be quick on floppies,
       small enough to leave a 512 KB machine room to breathe */
    bufsize = largest / 4;
    if (bufsize > 65536)
        bufsize = 65536;
    bufsize &= ~511UL;
    if (bufsize < 4096)
        bufsize = 4096;
    while (!(buf = AllocMem(bufsize, MEMF_PUBLIC)) && bufsize > 512)
        bufsize /= 2;
    dos_quiet(TRUE);         /* missing disks are reported through the error hook */
}

void ops_end(void)
{
    dos_quiet(FALSE);
    if (buf)
        FreeMem(buf, bufsize);
    buf = NULL;
}
