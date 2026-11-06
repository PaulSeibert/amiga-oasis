/* A stand-in for the LhA program, for Oasis's tests only (the real LhA is shareware and not
 * included). It appends what it was asked to do to Work:la-log.txt:
 *   LhA [arg] [arg] ...                    every argument as it arrived
 *   has <name> / missing <name>            for "a": whether each name exists in the current
 *                                          drawer (Oasis must run LhA in the source drawer)
 * and for "x" writes <dest>stub-extracted.txt; for "a" appends the names to the archive file. */
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

static BPTR logf;

static int len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void put(BPTR f, const char *s)
{
    Write(f, (APTR)s, len(s));
}

static BPTR append(const char *path)
{
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    if (f)
        Seek(f, 0, OFFSET_END);
    else
        f = Open((STRPTR)path, MODE_NEWFILE);
    return f;
}

int main(int argc, char **argv)
{
    int i, cmd = 0;
    if (!(logf = append("Work:la-log.txt")))
        return 20;
    put(logf, "LhA");
    for (i = 1; i < argc; i++) {
        put(logf, " [");
        put(logf, argv[i]);
        put(logf, "]");
    }
    put(logf, "\n");
    for (i = 1; i < argc && argv[i][0] == '-'; i++)
        ;
    cmd = i;                                   /* the command letter: x or a */
    if (cmd + 1 < argc && argv[cmd][0] == 'x') {
        char path[300];
        int k = 0, j;
        const char *d = argc > cmd + 2 ? argv[argc - 1] : "";
        BPTR f;
        for (j = 0; d[j] && k < 250; j++)
            path[k++] = d[j];
        for (j = 0; "stub-extracted.txt"[j]; j++)
            path[k++] = "stub-extracted.txt"[j];
        path[k] = 0;
        if ((f = Open((STRPTR)path, MODE_NEWFILE))) {
            put(f, "extracted from ");
            put(f, argv[cmd + 1]);
            put(f, "\n");
            Close(f);
        } else
            put(logf, "cannot write the extracted file\n");
    } else if (cmd + 1 < argc && argv[cmd][0] == 'a') {
        BPTR arc = append(argv[cmd + 1]);
        for (i = cmd + 2; i < argc; i++) {
            BPTR l = Lock((STRPTR)argv[i], ACCESS_READ);
            put(logf, l ? "has " : "missing ");
            put(logf, argv[i]);
            put(logf, "\n");
            if (l)
                UnLock(l);
            if (arc) {
                put(arc, argv[i]);
                put(arc, "\n");
            }
        }
        if (arc)
            Close(arc);
    }
    Close(logf);
    return 0;
}
