/* Oasis command mode: "oasis CMD <command> ..." runs Oasis's engine without the window.
 * Used for scripting and for Oasis's own automated tests.
 * Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
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

static BOOL is(const char *a, const char *b)
{
    return !nocase_cmp(a, b);
}

int cmd_run(int argc, char **argv)
{
    if (argc >= 4 && is(argv[2], "LIST"))
        return list(argv[3]);
    if (argc >= 3 && is(argv[2], "DEVS"))
        return devs();
    out("Oasis command mode. Usage:\n"
        "  oasis CMD LIST <drawer>     list a drawer (name, size, protection, date, comment)\n"
        "  oasis CMD DEVS              list devices, volumes and assigns\n");
    return 5;
}
