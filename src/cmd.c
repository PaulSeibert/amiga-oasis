/* Oasis command mode: "oasis CMD <command> ..." runs Oasis's engine without the window.
 * Used for scripting and for Oasis's own automated tests.
 * Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "ops.h"
#include "cmd.h"

static void out(const char *s)
{
    Write(Output(), (APTR)s, slen(s));
}

static int fail(const char *what, const char *arg)
{
    char line[PATHMAX + 64];
    fmt(line, "oasis: %s %s: %s\n", what, arg, dos_errstr(IoErr()));
    out(line);
    return 10;
}

static BOOL is(const char *a, const char *b)
{
    return !nocase_cmp(a, b);
}

/* LIST <path>: one line per entry: name, size or "(dir)", protection, date, comment */
static int list(const char *path)
{
    struct FileInfoBlock *fib = AllocMem(sizeof(*fib), MEMF_PUBLIC | MEMF_CLEAR);
    char line[PATHMAX + 128], date[20], prot[10], sz[16];
    BPTR lock;
    int rc = 0;
    if (!fib)
        return 20;
    if (!(lock = Lock((STRPTR)path, ACCESS_READ))) {
        FreeMem(fib, sizeof(*fib));
        return fail("cannot open", path);
    }
    if (Examine(lock, fib) && fib->fib_DirEntryType > 0) {
        while (ExNext(lock, fib)) {
            dos_datestr(date, &fib->fib_Date);
            dos_protstr(prot, fib->fib_Protection);
            if (fib->fib_DirEntryType > 0)
                scopy(sz, "(dir)", sizeof(sz));
            else
                fmt(sz, "%ld", fib->fib_Size);
            fmt(line, "%-30s %9s %s %s %s\n", fib->fib_FileName, sz, prot, date, fib->fib_Comment);
            out(line);
        }
        if (IoErr() != ERROR_NO_MORE_ENTRIES)
            rc = fail("error reading", path);
    } else
        rc = fail("not a drawer:", path);
    UnLock(lock);
    FreeMem(fib, sizeof(*fib));
    return rc;
}

/* TREE <path>: every entry below path, sorted, with its path relative to the start,
   size, protection, exact date (days/minutes/ticks) and comment: for comparing trees */
static char treepath[PATHMAX];
static int treebase;

static int tree(void)
{
    /* recursive: keep this frame small (1.3 CLI stacks are 4 KB and DOS calls use plenty) */
    static char line[PATHMAX + 128], prot[10], sz[16];
    struct Names nm;
    struct FileInfoBlock *fib;
    int i, len = slen(treepath), rc = 0;
    LONG err;
    if ((err = ops_list(treepath, &nm, TRUE))) {
        fmt(line, "oasis: cannot read %s: %s\n", treepath, dos_errstr(err));
        out(line);
        return 10;
    }
    if (!(fib = AllocMem(sizeof(*fib), MEMF_PUBLIC))) {
        ops_freenames(&nm);
        return 20;
    }
    for (i = 0; i < nm.n; i++) {
        BPTR l;
        path_add(treepath, nm.v[i].name);
        if ((l = Lock((STRPTR)treepath, ACCESS_READ)) && Examine(l, fib)) {
            dos_protstr(prot, fib->fib_Protection);
            if (fib->fib_DirEntryType > 0)
                scopy(sz, "(dir)", sizeof(sz));
            else
                fmt(sz, "%ld", fib->fib_Size);
            fmt(line, "%s|%s|%s|%ld/%ld/%ld|%s\n", treepath + treebase, sz, prot,
                fib->fib_Date.ds_Days, fib->fib_Date.ds_Minute, fib->fib_Date.ds_Tick,
                fib->fib_Comment);
            out(line);
        } else
            rc = fail("cannot examine", treepath);
        if (l)
            UnLock(l);
        if (nm.v[i].dir && tree())
            rc = 10;
        treepath[len] = 0;
    }
    FreeMem(fib, sizeof(*fib));
    ops_freenames(&nm);
    return rc;
}

/* DEVS: the device list as Oasis sees it */
static int devs(void)
{
    static struct DevEntry dev[48];
    static const char *const kind[] = { "device", "volume", "assign" };
    char line[64];
    int n = dos_devices(dev, 48), i;
    for (i = 0; i < n; i++) {
        fmt(line, "%-24s %s\n", dev[i].name, kind[dev[i].kind]);
        out(line);
    }
    return 0;
}

/* ---- file operations ------------------------------------------------------------------ */

static BOOL force;

static BOOL c_progress(void *ctx, int op, const char *name, ULONG done, ULONG total)
{
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
        out("***Break\n");
        return FALSE;
    }
    return TRUE;
}

static int c_error(void *ctx, const char *what, const char *path, LONG err)
{
    char line[PATHMAX + 96];
    if (force && err == ERROR_DELETE_PROTECTED)
        return ANS_FORCE;
    fmt(line, "oasis: %s %s: %s\n", what, path, dos_errstr(err));
    out(line);
    return ANS_SKIP;
}

static int c_exists(void *ctx, const char *path)
{
    return ANS_REPLACE;                       /* like AmigaDOS Copy */
}

/* split "Work:a/b" into dir "Work:a" and name "b"; FALSE for a bare volume like "DF0:" */
static BOOL splitpath(const char *path, char *dir, char *name)
{
    int i, cut = -1, n;
    char p[PATHMAX];
    scopy(p, path, PATHMAX);
    n = slen(p);
    while (n > 1 && p[n - 1] == '/')
        p[--n] = 0;
    for (i = 0; p[i]; i++)
        if (p[i] == ':' || p[i] == '/')
            cut = i;
    if (cut == n - 1)
        return FALSE;
    scopy(name, p + cut + 1, 32);
    scopy(dir, p, PATHMAX);
    dir[cut < 0 ? 0 : (p[cut] == ':' ? cut + 1 : cut)] = 0;
    return TRUE;
}

/* "x.info" is handled together with "x" when both are named */
static BOOL followsicon(const char *path, char **args, int n)
{
    char base[PATHMAX];
    int i;
    if (!ops_isicon(path, base, PATHMAX))
        return FALSE;
    for (i = 0; i < n; i++)
        if (is(args[i], base))
            return TRUE;
    return FALSE;
}

static void summary(int op, const struct OpsStats *st)
{
    static const char *const verb[] = { "Copied", "Moved", "Deleted" };
    char line[160];
    if (op == OP_COPY)
        fmt(line, "%s %ld files and %ld drawers (%ld bytes).\n", verb[op], st->files, st->dirs, st->bytes);
    else
        fmt(line, "%s %ld files and %ld drawers.\n", verb[op], st->files, st->dirs);
    out(line);
    if (op != OP_DELETE && (st->datesSet || st->datesFailed)) {
        fmt(line, "Dates kept: %ld, not kept: %ld.\n", st->datesSet, st->datesFailed);
        out(line);
    }
    if (st->skipped) {
        fmt(line, "Skipped: %ld.\n", st->skipped);
        out(line);
    }
    if (st->aborted)
        out("Stopped.\n");
}

static int fileop(int op, char **args, int n, const char *dst)
{
    struct OpsHooks h = { NULL, c_progress, c_error, c_exists };
    struct OpsStats st;
    char dir[PATHMAX], name[32];
    int i;
    ops_begin(&h, &st);
    for (i = 0; i < n && !st.aborted; i++) {
        if (followsicon(args[i], args, n))
            continue;
        if (!splitpath(args[i], dir, name)) {
            char line[PATHMAX + 64];
            fmt(line, "oasis: give a file or drawer, not a whole volume: %s\n", args[i]);
            out(line);
            st.skipped++;
            continue;
        }
        if (op == OP_COPY)
            ops_copy(dir, name, dst);
        else if (op == OP_MOVE)
            ops_move(dir, name, dst);
        else
            ops_delete(dir, name);
    }
    ops_end();
    summary(op, &st);
    return st.skipped || st.aborted ? 10 : 0;
}

static int single(LONG err, const char *what, const char *arg)
{
    char line[PATHMAX + 64];
    if (!err)
        return 0;
    fmt(line, "oasis: %s %s: %s\n", what, arg, dos_errstr(err));
    out(line);
    return 10;
}

int cmd_run(int argc, char **argv)
{
    int i;
    if (argc >= 4 && is(argv[2], "LIST"))
        return list(argv[3]);
    if (argc >= 4 && is(argv[2], "TREE")) {
        scopy(treepath, argv[3], PATHMAX);
        treebase = slen(treepath);
        if (treebase && treepath[treebase - 1] != ':' && treepath[treebase - 1] != '/')
            treebase++;                       /* also skip the "/" that path_add inserts */
        return tree();
    }
    if (argc >= 3 && is(argv[2], "DEVS"))
        return devs();
    if (argc >= 3 && is(argv[2], "MEM")) {
        char line[96];
        fmt(line, "chip %ld fast %ld total %ld\n", AvailMem(MEMF_CHIP), AvailMem(MEMF_FAST),
            AvailMem(0));
        out(line);
        return 0;
    }
    if (argc >= 6 && (is(argv[2], "COPY") || is(argv[2], "MOVE")) && is(argv[argc - 2], "TO"))
        return fileop(is(argv[2], "COPY") ? OP_COPY : OP_MOVE, argv + 3, argc - 5, argv[argc - 1]);
    if (argc >= 4 && is(argv[2], "DELETE")) {
        int n = argc - 3;
        force = is(argv[argc - 1], "FORCE");
        if (force)
            n--;
        return fileop(OP_DELETE, argv + 3, n, NULL);
    }
    if (argc == 4 && is(argv[2], "MAKEDIR")) {
        char dir[PATHMAX], name[32];
        if (!splitpath(argv[3], dir, name))
            return single(ERROR_OBJECT_EXISTS, "cannot make drawer", argv[3]);
        return single(ops_makedir(dir, name), "cannot make drawer", argv[3]);
    }
    if (argc == 5 && is(argv[2], "RENAME")) {
        char dir[PATHMAX], name[32], dir2[PATHMAX], name2[32];
        if (!splitpath(argv[3], dir, name) || !splitpath(argv[4], dir2, name2))
            return single(ERROR_INVALID_COMPONENT_NAME, "cannot rename", argv[3]);
        return single(ops_rename(dir, name, name2), "cannot rename", argv[3]);
    }
    (void)i;
    out("Oasis command mode. Usage:\n"
        "  oasis CMD LIST <drawer>             list a drawer (name, size, protection, date, comment)\n"
        "  oasis CMD TREE <drawer>             list everything below a drawer, sorted\n"
        "  oasis CMD DEVS                      list devices, volumes and assigns\n"
        "  oasis CMD COPY <from>... TO <drawer>   copy files and drawers (icons go along)\n"
        "  oasis CMD MOVE <from>... TO <drawer>   move them\n"
        "  oasis CMD DELETE <path>... [FORCE]  delete (FORCE: protected files too)\n"
        "  oasis CMD MAKEDIR <path>            make a drawer\n"
        "  oasis CMD RENAME <path> <newname>   rename (its icon too)\n"
        "  oasis CMD MEM                       free memory\n");
    return 5;
}
