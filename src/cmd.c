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
#include "tools.h"
#include "match.h"
#include "pane.h"
#include "prefs.h"
#include "ftype.h"
#include "view.h"
#include "ilbm.h"
#include "svx.h"
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

/* ---- tools ----------------------------------------------------------------------------- */

static BOOL c_poll(void *ctx, const char *drawer)
{
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
        out("***Break\n");
        return FALSE;
    }
    return TRUE;
}

static int size(char **args, int n)
{
    struct SizeStats st = { 0 };
    char line[160];
    int i, rc = 0;
    LONG err;
    for (i = 0; i < n && !st.aborted; i++)
        if ((err = tools_size(args[i], &st, c_poll, NULL)))
            rc = single(err, "cannot read", args[i]);
    fmt(line, "%ld files, %ld drawers, %ld bytes, %ld blocks.\n", st.files, st.dirs, st.bytes, st.blocks);
    out(line);
    if (st.unreadable) {
        fmt(line, "Could not read: %ld.\n", st.unreadable);
        out(line);
    }
    return rc || st.unreadable || st.aborted ? 10 : 0;
}

static ULONG nhits;

static BOOL c_hit(void *ctx, const char *reldir, const struct FileInfoBlock *fib)
{
    char line[PATHMAX + 48];
    fmt(line, "%s%s%s%s\n", reldir, reldir[0] ? "/" : "", fib->fib_FileName,
        fib->fib_DirEntryType > 0 ? " (dir)" : "");
    out(line);
    nhits++;
    return TRUE;
}

static int search(const char *drawer, const char *pat)
{
    char line[64];
    ULONG bad = 0;
    BOOL stopped = FALSE;
    LONG err;
    nhits = 0;
    if ((err = tools_search(drawer, pat, c_hit, c_poll, NULL, &bad, &stopped)))
        return single(err, "cannot search", drawer);
    fmt(line, "%ld found.\n", nhits);
    out(line);
    return bad || stopped ? 10 : 0;
}

static int protect(const char *path, const char *spec)
{
    struct FileInfoBlock *fib = AllocMem(sizeof(*fib), MEMF_PUBLIC | MEMF_CLEAR);
    BPTR l;
    LONG prot, err = 0;
    if (!fib)
        return 20;
    if ((l = Lock((STRPTR)path, ACCESS_READ)) && Examine(l, fib)) {
        UnLock(l);
        l = 0;
        if (!tools_parseprot(spec, fib->fib_Protection, &prot))
            err = ERROR_BAD_TEMPLATE;
        else if (!SetProtection((STRPTR)path, prot))
            err = IoErr();
    } else
        err = IoErr();
    if (l)
        UnLock(l);
    FreeMem(fib, sizeof(*fib));
    return single(err, "cannot protect", path);
}

static int compare(const char *a, const char *b)
{
    static struct Pane pa, pb;              /* static: they are big */
    struct CmpStats st;
    char line[96];
    int i, j, rc = 0;
    scopy(pa.path, a, PATHMAX);
    scopy(pb.path, b, PATHMAX);
    if (!pane_read(&pa, SORT_NAME, FALSE))
        rc = single(ERROR_OBJECT_NOT_FOUND, "cannot read", a);
    else if (!pane_read(&pb, SORT_NAME, FALSE))
        rc = single(ERROR_OBJECT_NOT_FOUND, "cannot read", b);
    else {
        tools_compare(&pa, &pb, &st);
        for (i = 0; i < pa.count; i++)
            if (pa.e[i].sel) {
                for (j = 0; j < pb.count && nocase_cmp(pa.e[i].name, pb.e[j].name); j++)
                    ;
                fmt(line, "%s %s\n", j < pb.count ? "differs:" : "only in 1:", pa.e[i].name);
                out(line);
            }
        for (i = 0; i < pb.count; i++)
            if (pb.e[i].sel) {
                for (j = 0; j < pa.count && nocase_cmp(pb.e[i].name, pa.e[j].name); j++)
                    ;
                if (j == pa.count) {
                    fmt(line, "only in 2: %s\n", pb.e[i].name);
                    out(line);
                }
            }
        fmt(line, "%ld only in 1, %ld only in 2, %ld differ, %ld the same.\n", (LONG)st.onlyA,
            (LONG)st.onlyB, (LONG)st.differ, (LONG)st.same);
        out(line);
    }
    pane_free(&pa);
    pane_free(&pb);
    return rc;
}

static struct Prefs P;                      /* static: too big for a 1.3 stack */

static int prefs(const char *file)
{
    static const char *const sortname[] = { "NAME", "SIZE", "DATE" };
    char line[BCMD + 64];
    int i;
    if (!prefs_load(file, &P))
        return single(ERROR_OBJECT_NOT_FOUND, "cannot read", file);
    if (P.hasleft) { fmt(line, "LEFT \"%s\"\n", P.left); out(line); }
    if (P.hasright) { fmt(line, "RIGHT \"%s\"\n", P.right); out(line); }
    if (P.hassort) { fmt(line, "SORT %s\n", sortname[P.sort]); out(line); }
    if (P.hashide) { fmt(line, "HIDEICONS %s\n", P.hideinfo ? "YES" : "NO"); out(line); }
    for (i = 0; i < P.nbuttons; i++) {
        fmt(line, "BUTTON %ld \"%s\"%s [%s]\n", (LONG)i + 1, P.b[i].label, P.b[i].quiet ? " QUIET" : "",
            P.b[i].cmd);
        out(line);
    }
    if (P.nbad) {
        fmt(line, "Not understood: %ld line%s, the first is line %ld.\n", (LONG)P.nbad,
            P.nbad == 1 ? "" : "s", (LONG)P.badline);
        out(line);
    }
    return P.nbad ? 5 : 0;
}

/* BUTTON <prefsfile> <n> <srcdrawer> <dstdrawer> [<file>...]: run custom button n as the
   window would (files: full paths), with its output here */
static int button(char **argv, int argc)
{
    static char cmd[1024];
    int n = 0, r;
    const char *c;
    BPTR dir, old;
    for (c = argv[4]; *c >= '0' && *c <= '9'; c++)
        n = n * 10 + *c - '0';
    if (!prefs_load(argv[3], &P))
        return single(ERROR_OBJECT_NOT_FOUND, "cannot read", argv[3]);
    if (n < 1 || n > P.nbuttons)
        return single(ERROR_OBJECT_NOT_FOUND, "no such button", argv[4]);
    r = prefs_expand(P.b[n - 1].cmd, argv[5], argv[6], argv + 7, argc - 7, cmd, sizeof(cmd));
    if (r) {
        out(r == 1 ? "oasis: this button needs files\n" : "oasis: the command line is too long\n");
        return 10;
    }
    out("> ");
    out(cmd);
    if (!(dir = Lock((STRPTR)argv[5], ACCESS_READ)))
        return single(IoErr(), "cannot open", argv[5]);
    old = CurrentDir(dir);
    r = Execute((STRPTR)cmd, 0, Output()) ? 0 : 10;
    CurrentDir(old);
    UnLock(dir);
    if (r)
        out("oasis: could not run the command (Execute failed)\n");
    return r;
}

/* ---- viewers ----------------------------------------------------------------------------- */

static int types(char **args, int n)
{
    char line[PATHMAX + 32];
    int i;
    for (i = 0; i < n; i++) {
        int t = ft_file(args[i]);
        fmt(line, "%s: %s\n", args[i], t == FT_UNKNOWN ? "(cannot read)" : ft_name(t));
        out(line);
    }
    return 0;
}

static int lines(const char *path, const char *cols, const char *show)
{
    int c = 0, s = 0;
    for (; *cols >= '0' && *cols <= '9'; cols++)
        c = c * 10 + *cols - '0';
    for (; *show >= '0' && *show <= '9'; show++)
        s = s * 10 + *show - '0';
    return single(view_lines(path, c < 8 ? 8 : c, s, out), "cannot read", path);
}

/* ILBM <file>: what Oasis makes of a picture, and a checksum of its decoded planes */
static int ilbm(const char *path)
{
    static struct IlbmInfo ii;
    UBYTE *buf, *planes[8], *mem = NULL;
    LONG len, size, alloc, err, psize = 0;
    const char *msg;
    char line[160];
    ULONG modes = 0, h = 0;
    int sw = 0, sh = 0, depth = 0, rb, p, i;
    if (!(buf = ft_load(path, 0x7FFFFFFF, 0, &len, &size, &alloc, &err)))
        return single(err, "cannot read", path);
    if ((msg = ilbm_parse(buf, len, &ii))) {
        fmt(line, "oasis: %s\n", msg);
        out(line);
        FreeMem(buf, alloc);
        return 10;
    }
    msg = ilbm_mode(&ii, &modes, &sw, &sh, &depth);
    fmt(line, "%ld x %ld, %ld planes, masking %ld, compression %ld, CAMG $%lx%s, %ld colours\n",
        (LONG)ii.w, (LONG)ii.h, (LONG)ii.planes, (LONG)ii.masking, (LONG)ii.compression,
        ii.camg, ii.hascamg ? "" : " (none)", (LONG)ii.ncolors);
    out(line);
    if (msg)
        fmt(line, "screen: cannot show (%s)\n", msg);
    else
        fmt(line, "screen: %ld x %ld x %ld, modes $%lx\n", (LONG)sw, (LONG)sh, (LONG)depth, modes);
    out(line);
    rb = ((ii.w + 15) >> 4) * 2;
    psize = (LONG)rb * ii.h;
    if (ii.planes <= 8 && (mem = AllocMem(psize * ii.planes, MEMF_PUBLIC | MEMF_CLEAR))) {
        BOOL whole;
        for (p = 0; p < ii.planes; p++)
            planes[p] = mem + p * psize;
        whole = ilbm_decode(&ii, planes, ii.planes, rb, ii.h);
        for (i = 0; i < psize * ii.planes; i++)
            h = h * 31 + mem[i];
        fmt(line, "planes checksum %lx%s\n", h, whole ? "" : " (the data ran out)");
        out(line);
        FreeMem(mem, psize * ii.planes);
    } else
        out("oasis: not enough memory to decode the picture\n");
    FreeMem(buf, alloc);
    return 0;
}

static int play(const char *path)
{
    struct SvxInfo si = { 0 };
    LONG ticks;
    char line[160];
    const char *msg = svx_play(path, NULL, NULL, 0, &si, &ticks);
    if (msg && !si.rate) {
        fmt(line, "oasis: %s\n", msg);
        out(line);
        return 10;
    }
    fmt(line, "%ld Hz, %ld samples%s, compression %ld, volume %ld, octaves %ld\n", (LONG)si.rate,
        si.samples, si.stereo ? " (stereo)" : "", (LONG)si.compression, si.volume, (LONG)si.octaves);
    out(line);
    if (msg) {
        fmt(line, "oasis: %s\n", msg);
        out(line);
        return 10;
    }
    fmt(line, "played in %ld ticks\n", ticks);
    out(line);
    return 0;
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
    if (argc >= 4 && is(argv[2], "SIZE"))
        return size(argv + 3, argc - 3);
    if (argc == 5 && is(argv[2], "SEARCH"))
        return search(argv[3], argv[4]);
    if (argc == 5 && is(argv[2], "PROTECT"))
        return protect(argv[3], argv[4]);
    if (argc == 5 && is(argv[2], "COMMENT"))
        return single(SetComment((STRPTR)argv[3], (STRPTR)argv[4]) ? 0 : IoErr(), "cannot comment", argv[3]);
    if (argc == 5 && is(argv[2], "COMPARE"))
        return compare(argv[3], argv[4]);
    if (argc >= 5 && is(argv[2], "MATCH")) {
        for (i = 4; i < argc; i++) {
            char line[96];
            fmt(line, "%s %s\n", match_search(argv[3], argv[i]) ? "yes" : "no ", argv[i]);
            out(line);
        }
        return 0;
    }
    if (argc == 4 && is(argv[2], "PREFS"))
        return prefs(argv[3]);
    if (argc >= 7 && is(argv[2], "BUTTON"))
        return button(argv, argc);
    if (argc >= 4 && is(argv[2], "TYPE"))
        return types(argv + 3, argc - 3);
    if (argc == 6 && is(argv[2], "LINES"))
        return lines(argv[3], argv[4], argv[5]);
    if (argc == 4 && is(argv[2], "ILBM"))
        return ilbm(argv[3]);
    if (argc == 4 && is(argv[2], "PLAY"))
        return play(argv[3]);
    out("Oasis command mode. Usage:\n"
        "  oasis CMD LIST <drawer>             list a drawer (name, size, protection, date, comment)\n"
        "  oasis CMD TREE <drawer>             list everything below a drawer, sorted\n"
        "  oasis CMD DEVS                      list devices, volumes and assigns\n"
        "  oasis CMD COPY <from>... TO <drawer>   copy files and drawers (icons go along)\n"
        "  oasis CMD MOVE <from>... TO <drawer>   move them\n"
        "  oasis CMD DELETE <path>... [FORCE]  delete (FORCE: protected files too)\n"
        "  oasis CMD MAKEDIR <path>            make a drawer\n"
        "  oasis CMD RENAME <path> <newname>   rename (its icon too)\n"
        "  oasis CMD SIZE <path>...            add up files and drawers\n"
        "  oasis CMD SEARCH <drawer> <pattern> find names (#? ? * wildcards, or part of a name)\n"
        "  oasis CMD PROTECT <path> <bits>     set the hsparwed bits (rwed, +s, -d)\n"
        "  oasis CMD COMMENT <path> <text>     set the file comment\n"
        "  oasis CMD COMPARE <drawer> <drawer> what is only on one side, or differs\n"
        "  oasis CMD MATCH <pattern> <name>... try a pattern\n"
        "  oasis CMD PREFS <file>              check a prefs file\n"
        "  oasis CMD BUTTON <prefs> <n> <src> <dst> [file...]   run custom button n\n"
        "  oasis CMD TYPE <file>...            what each file is\n"
        "  oasis CMD LINES <file> <cols> <n>   how the text viewer shows a file\n"
        "  oasis CMD ILBM <file>               check a picture\n"
        "  oasis CMD PLAY <file>               play an 8SVX sound\n"
        "  oasis CMD MEM                       free memory\n");
    return 5;
}
