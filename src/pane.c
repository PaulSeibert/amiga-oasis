/* Oasis - directory panes. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "pane.h"

#define GROW 64

void pane_free(struct Pane *p)
{
    if (p->e)
        FreeMem(p->e, p->cap * sizeof(struct FEntry));
    p->e = NULL;
    p->count = p->cap = 0;
    if (p->pool)
        FreeMem(p->pool, p->poolcap);
    p->pool = NULL;
    p->poolcap = p->poolused = 0;
}

static struct FEntry *newentry(struct Pane *p)
{
    if (p->count == p->cap) {
        int ncap = p->cap + GROW;
        struct FEntry *ne = AllocMem(ncap * sizeof(struct FEntry), MEMF_PUBLIC);
        if (!ne)
            return NULL;
        if (p->e) {
            CopyMem(p->e, ne, p->count * sizeof(struct FEntry));
            FreeMem(p->e, p->cap * sizeof(struct FEntry));
        }
        p->e = ne;
        p->cap = ncap;
    }
    return &p->e[p->count++];
}

static void reset(struct Pane *p)
{
    p->count = 0;            /* keep the allocation for the next read */
    p->top = 0;
    p->cur = -1;
    p->nfiles = p->ndirs = p->nsel = 0;
    p->bytes = p->selbytes = p->freek = 0;
    p->vol[0] = p->err[0] = 0;
    p->found = FALSE;
    p->poolused = 0;
}

BOOL pane_isdir(const struct FEntry *e)
{
    return e->kind != EK_FILE;
}

/* ---- sorting: drawers first, then by the chosen key (shell sort: no recursion, no copies of
 * the whole array, fine for thousands of entries on a 68000) ------------------------------- */

static int compare(const struct FEntry *a, const struct FEntry *b, int sort)
{
    BOOL da = pane_isdir(a), db = pane_isdir(b);
    if (da != db)
        return da ? -1 : 1;
    if (a->kind != b->kind)
        return a->kind - b->kind;
    if (!da && sort == SORT_SIZE && a->size != b->size)
        return a->size > b->size ? -1 : 1;                  /* biggest first */
    if (sort == SORT_DATE) {
        if (a->date.ds_Days != b->date.ds_Days)
            return a->date.ds_Days > b->date.ds_Days ? -1 : 1;   /* newest first */
        if (a->date.ds_Minute != b->date.ds_Minute)
            return a->date.ds_Minute > b->date.ds_Minute ? -1 : 1;
        if (a->date.ds_Tick != b->date.ds_Tick)
            return a->date.ds_Tick > b->date.ds_Tick ? -1 : 1;
    }
    return nocase_cmp(a->name, b->name);
}

void pane_sort(struct Pane *p, int sort)
{
    int gap, i, j;
    for (gap = p->count / 2; gap > 0; gap /= 2)
        for (i = gap; i < p->count; i++) {
            struct FEntry t = p->e[i];
            for (j = i; j >= gap && compare(&t, &p->e[j - gap], sort) < 0; j -= gap)
                p->e[j] = p->e[j - gap];
            p->e[j] = t;
        }
}

/* ---- reading ----------------------------------------------------------------------------- */

BOOL pane_read(struct Pane *p, int sort, BOOL hideinfo)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    BOOL ok = FALSE;

    reset(p);
    p->devlist = FALSE;
    if (!(fib = AllocMem(sizeof(struct FileInfoBlock), MEMF_PUBLIC | MEMF_CLEAR))) {
        scopy(p->err, "Not enough memory", sizeof(p->err));
        return FALSE;
    }
    dos_quiet(TRUE);                 /* an empty drive is reported in the pane, not a requester */
    if (!(lock = Lock((STRPTR)p->path, ACCESS_READ)))
        scopy(p->err, dos_errstr(IoErr()), sizeof(p->err));
    else if (!Examine(lock, fib))
        scopy(p->err, dos_errstr(IoErr()), sizeof(p->err));
    else if (fib->fib_DirEntryType <= 0)
        scopy(p->err, "Not a drawer", sizeof(p->err));
    else {
        dos_space(lock, p->path, &p->freek, p->vol);
        while (ExNext(lock, fib)) {
            struct FEntry *e;
            if (hideinfo && ends_with_nocase((char *)fib->fib_FileName, ".info"))
                continue;
            if (!(e = newentry(p)))
                break;
            scopy(e->name, (char *)fib->fib_FileName, FNAME);
            e->date = fib->fib_Date;
            e->prot = fib->fib_Protection;
            e->sel = 0;
            e->sized = 0;
            e->aux = -1;
            if (fib->fib_DirEntryType > 0) {
                e->kind = EK_DIR;
                e->size = 0;
                p->ndirs++;
            } else {
                e->kind = EK_FILE;
                e->size = fib->fib_Size;
                p->nfiles++;
                p->bytes += fib->fib_Size;
            }
        }
        ok = (IoErr() == ERROR_NO_MORE_ENTRIES);
        if (!ok)
            scopy(p->err, dos_errstr(IoErr()), sizeof(p->err));
    }
    if (lock)
        UnLock(lock);
    dos_quiet(FALSE);
    FreeMem(fib, sizeof(struct FileInfoBlock));
    pane_sort(p, sort);
    if (p->count)
        p->cur = 0;
    return ok;
}

void pane_devlist(struct Pane *p)
{
    static struct DevEntry dev[48];      /* static: 1.3 CLI stacks are only 4K */
    int n = dos_devices(dev, 48), i;
    static const UBYTE kind[] = { EK_DEVICE, EK_VOLUME, EK_ASSIGN };

    reset(p);
    p->devlist = TRUE;
    p->path[0] = 0;
    for (i = 0; i < n; i++) {
        struct FEntry *e = newentry(p);
        if (!e)
            break;
        scopy(e->name, dev[i].name, FNAME - 1);
        e->kind = kind[dev[i].kind];
        e->size = 0;
        e->prot = 0;
        e->sel = 0;
        e->sized = 0;
        e->aux = -1;
        e->date.ds_Days = e->date.ds_Minute = e->date.ds_Tick = 0;
    }
    if (p->count)
        p->cur = 0;
}

void pane_select(struct Pane *p, int i, BOOL on)
{
    struct FEntry *e = &p->e[i];
    if (p->devlist || e->sel == on)
        return;
    e->sel = on;
    p->nsel += on ? 1 : -1;
    if (e->kind == EK_FILE || e->sized)
        p->selbytes += on ? e->size : -e->size;
}

void pane_selectall(struct Pane *p, BOOL on)
{
    int i;
    for (i = 0; i < p->count; i++)
        pane_select(p, i, on);
}

/* ---- search results ------------------------------------------------------------------- */

void pane_beginfound(struct Pane *p, const char *pattern)
{
    reset(p);
    p->devlist = FALSE;
    p->found = TRUE;
    scopy(p->pattern, pattern, sizeof(p->pattern));
}

/* the drawer strings of the hits share one pool; a hit in the same drawer as the last one
   reuses its string */
static LONG pooladd(struct Pane *p, const char *s)
{
    int n = slen(s) + 1;
    LONG off;
    if (p->count > 1 && p->e[p->count - 2].aux >= 0 && !nocase_cmp(p->pool + p->e[p->count - 2].aux, s))
        return p->e[p->count - 2].aux;
    if (p->poolused + n > p->poolcap) {
        LONG nc = p->poolcap + 2048 + n;
        char *np = AllocMem(nc, MEMF_PUBLIC);
        if (!np)
            return -1;
        if (p->pool) {
            CopyMem(p->pool, np, p->poolused);
            FreeMem(p->pool, p->poolcap);
        }
        p->pool = np;
        p->poolcap = nc;
    }
    off = p->poolused;
    CopyMem((APTR)s, p->pool + off, n);
    p->poolused += n;
    return off;
}

BOOL pane_addhit(struct Pane *p, const char *reldir, const struct FileInfoBlock *fib)
{
    struct FEntry *e = newentry(p);
    if (!e)
        return FALSE;
    scopy(e->name, (char *)fib->fib_FileName, FNAME);
    e->date = fib->fib_Date;
    e->prot = fib->fib_Protection;
    e->sel = e->sized = 0;
    e->aux = -1;
    if (fib->fib_DirEntryType > 0) {
        e->kind = EK_DIR;
        e->size = 0;
        p->ndirs++;
    } else {
        e->kind = EK_FILE;
        e->size = fib->fib_Size;
        p->nfiles++;
        p->bytes += fib->fib_Size;
    }
    if (reldir[0] && (e->aux = pooladd(p, reldir)) < 0) {
        p->count--;
        return FALSE;
    }
    if (p->cur < 0)
        p->cur = 0;
    return TRUE;
}

const char *pane_reldir(const struct Pane *p, const struct FEntry *e)
{
    return (p->found && e->aux >= 0) ? p->pool + e->aux : "";
}

void pane_entrydir(const struct Pane *p, const struct FEntry *e, char *out)
{
    const char *r = pane_reldir(p, e);
    scopy(out, p->path, PATHMAX);
    if (r[0])
        path_add(out, r);
}
