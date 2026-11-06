/* Oasis - LhA archives. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * Header levels 0, 1 and 2 are read (LhA on the Amiga writes 0 and 1, other systems 1 and 2).
 * Each header is read on its own and the packed data is skipped with Seek, so archives of
 * any size list in little memory. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "lha.h"

#define HMAX 4096                /* largest header read (level 2 headers can carry extras) */

static UWORD le16(const UBYTE *p) { return (UWORD)(p[0] | (p[1] << 8)); }
static ULONG le32(const UBYTE *p) { return p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24); }

/* ---- a growing text buffer ---------------------------------------------------------------- */

static char *tb;
static LONG tlen, tcap;

static BOOL tput(const char *s)
{
    LONG n = slen(s);
    if (tlen + n + 1 > tcap) {
        LONG nc = tcap + 4096 + n;
        char *t = AllocMem(nc, MEMF_PUBLIC);
        if (!t)
            return FALSE;
        if (tb) {
            CopyMem(tb, t, tlen);
            FreeMem(tb, tcap);
        }
        tb = t;
        tcap = nc;
    }
    CopyMem((APTR)s, tb + tlen, n + 1);
    tlen += n;
    return TRUE;
}

/* ---- dates ---------------------------------------------------------------------------------- */

/* days since 1 Jan 1978 (the AmigaDOS epoch) */
static LONG days(int y, int m, int d)
{
    static const short before[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    LONG n;
    int k;
    if (m < 1 || m > 12)
        m = 1;
    n = (LONG)(y - 1978) * 365 + before[m - 1] + d - 1;
    for (k = 1978; k < y; k++)
        if ((k % 4 == 0 && k % 100) || k % 400 == 0)
            n++;
    if (m > 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0))
        n++;
    return n;
}

static void msdos_date(ULONG t, struct DateStamp *ds)
{
    UWORD date = t >> 16, time = t & 0xFFFF;
    ds->ds_Days = days(1980 + (date >> 9), (date >> 5) & 15, date & 31);
    ds->ds_Minute = (time >> 11) * 60 + ((time >> 5) & 63);
    ds->ds_Tick = (time & 31) * 2 * 50;
}

static void unix_date(ULONG t, struct DateStamp *ds)
{
    const ULONG off = 252460800UL;              /* 1 Jan 1978 in Unix time */
    t = t > off ? t - off : 0;
    ds->ds_Days = t / 86400;
    ds->ds_Minute = (t % 86400) / 60;
    ds->ds_Tick = (t % 60) * 50;
}

/* ---- listing --------------------------------------------------------------------------------- */

static void cleanname(char *s)
{
    for (; *s; s++)
        if (*s == '\\' || (UBYTE)*s == 0xFF)
            *s = '/';
}

LONG lha_list(const char *path, char **text, LONG *len, LONG *alloc, struct LhaStats *st)
{
    static char name[PATHMAX], dirn[PATHMAX], comment[96], line[PATHMAX + 160];
    UBYTE *h;
    BPTR fh;
    LONG pos = 0, err = 0, n, total;
    st->files = st->dirs = st->packed = st->original = 0;
    tb = NULL;
    tlen = tcap = 0;
    *text = NULL;
    if (!(h = AllocMem(HMAX, MEMF_PUBLIC)))
        return ERROR_NO_FREE_STORE;
    dos_quiet(TRUE);
    fh = Open((STRPTR)path, MODE_OLDFILE);
    dos_quiet(FALSE);
    if (!fh) {
        FreeMem(h, HMAX);
        return IoErr();
    }
    Seek(fh, 0, OFFSET_END);
    total = Seek(fh, 0, OFFSET_BEGINNING);
    fmt(line, "Archive %s\n\n  Original    Packed Ratio Method Date      Time   Name\n"
              " --------- --------- ----- ------ --------- -----  ----------------\n", path);
    tput(line);
    for (;;) {
        LONG hsize, packed, orig, next, i;
        int level;
        struct DateStamp ds;
        char date[20], method[6], ratio[8], a[12], b[12];
        BOOL dir;
        if (Seek(fh, pos, OFFSET_BEGINNING) < 0 || (n = Read(fh, h, HMAX)) < 1 || h[0] == 0)
            break;                                     /* a 0 size byte ends the archive */
        if (n < 22 || h[2] != '-' || h[6] != '-') {
            err = pos ? 0 : ERROR_OBJECT_WRONG_TYPE;   /* not LhA, or junk after the end */
            break;
        }
        level = h[20];
        packed = (LONG)le32(h + 7);
        orig = (LONG)le32(h + 11);
        CopyMem(h + 2, method, 5);
        method[5] = 0;
        name[0] = comment[0] = 0;
        if (level == 0 || level == 1) {
            int nl = h[21];
            hsize = h[0] + 2;
            if (22 + nl > n)
                break;
            /* the name; Amiga LhA puts the file comment after a NUL in it */
            for (i = 0; i < nl && h[22 + i] && i < PATHMAX - 1; i++)
                name[i] = h[22 + i];
            name[i] = 0;
            if (i < nl && !h[22 + i]) {
                int k = 0;
                for (i++; i < nl && k < (int)sizeof(comment) - 1; i++)
                    comment[k++] = h[22 + i];
                comment[k] = 0;
            }
            msdos_date(le32(h + 15), &ds);
            next = pos + hsize + packed;               /* level 1: packed counts the extras */
            if (level == 1) {                          /* extended headers: maybe a drawer name */
                LONG e = pos + hsize, esz = le16(h + hsize - 2);
                dirn[0] = 0;
                while (esz >= 3 && e - pos + esz <= n) {
                    const UBYTE *x = h + (e - pos);
                    if (x[0] == 2) {
                        int k;
                        for (k = 0; k < esz - 3 && k < PATHMAX - 2; k++)
                            dirn[k] = x[1 + k];
                        dirn[k] = 0;
                    }
                    packed -= esz;
                    e += esz;
                    esz = le16(x + esz - 2);
                }
                if (dirn[0]) {
                    cleanname(dirn);
                    if (dirn[slen(dirn) - 1] != '/')
                        scat(dirn, "/", PATHMAX);
                    scat(dirn, name, PATHMAX);
                    scopy(name, dirn, PATHMAX);
                }
            }
        } else if (level == 2) {
            LONG e = 26;                               /* the first extended header */
            UWORD esz = le16(h + 24);
            hsize = le16(h);
            unix_date(le32(h + 15), &ds);
            dirn[0] = 0;
            while (esz >= 3 && e + esz <= n && e + esz <= hsize) {
                const UBYTE *x = h + e;
                int k;
                if (x[0] == 1) {                        /* file name */
                    for (k = 0; k < esz - 3 && k < PATHMAX - 1; k++)
                        name[k] = x[1 + k];
                    name[k] = 0;
                } else if (x[0] == 2) {                 /* drawer */
                    for (k = 0; k < esz - 3 && k < PATHMAX - 2; k++)
                        dirn[k] = x[1 + k];
                    dirn[k] = 0;
                } else if (x[0] == 0x71 || x[0] == 0x3F) {   /* comment */
                    for (k = 0; k < esz - 3 && k < (int)sizeof(comment) - 1; k++)
                        comment[k] = x[1 + k];
                    comment[k] = 0;
                }
                e += esz;
                esz = le16(x + esz - 2);
            }
            if (dirn[0]) {
                cleanname(dirn);
                if (dirn[slen(dirn) - 1] != '/')
                    scat(dirn, "/", PATHMAX);
                scat(dirn, name, PATHMAX);
                scopy(name, dirn, PATHMAX);
            }
            next = pos + hsize + packed;
        } else {
            err = pos ? 0 : ERROR_OBJECT_WRONG_TYPE;
            break;
        }
        cleanname(name);
        dir = !nocase_cmp(method, "-lhd-");
        if (dir)
            st->dirs++;
        else {
            st->files++;
            st->packed += packed;
            st->original += orig;
        }
        dos_datestr(date, &ds);
        if (dir) {
            a[0] = 0;
            scopy(b, "", sizeof(b));
            scopy(ratio, "", sizeof(ratio));
        } else {
            fmt(a, "%ld", orig);
            fmt(b, "%ld", packed);
            /* how much smaller it got (without overflowing 32 bits on big files) */
            if (orig <= 0)
                scopy(ratio, "", sizeof(ratio));
            else
                fmt(ratio, "%ld%%", 100 - (orig > 20000000 ? packed / (orig / 100) : packed * 100 / orig));
        }
        fmt(line, "%10s %9s %5s %s  %s  %s%s%s%s\n", dir ? "(drawer)" : a, b, ratio, method, date,
            name, dir && name[0] && name[slen(name) - 1] != '/' ? "/" : "", comment[0] ? "  : " : "",
            comment);
        if (!tput(line)) {
            err = ERROR_NO_FREE_STORE;
            break;
        }
        if (next <= pos || next > total)
            break;                                     /* cut off: stop at what can be read */
        pos = next;
    }
    Close(fh);
    FreeMem(h, HMAX);
    if (!err) {
        char x[16], y[16];
        fmt(x, "%ld", st->original);
        fmt(y, "%ld", st->packed);
        fmt(line, " --------- ---------\n%10s %9s       %ld file%s, %ld drawer%s\n", x, y, st->files,
            st->files == 1 ? "" : "s", st->dirs, st->dirs == 1 ? "" : "s");
        if (!tput(line))
            err = ERROR_NO_FREE_STORE;
    }
    if (err) {
        if (tb)
            FreeMem(tb, tcap);
        return err;
    }
    *text = tb;
    *len = tlen;
    *alloc = tcap;
    return 0;
}

/* ---- the LhA program ------------------------------------------------------------------------ */

BOOL lha_program(const char *prefsprog, char *out, int size)
{
    const char *p = prefsprog && prefsprog[0] ? prefsprog : "C:LhA";
    BPTR l;
    dos_quiet(TRUE);
    l = Lock((STRPTR)p, ACCESS_READ);
    dos_quiet(FALSE);
    if (!l)
        return FALSE;
    UnLock(l);
    scopy(out, p, size);
    return TRUE;
}

static BOOL put(char *out, int size, int *k, const char *s)
{
    while (*s) {
        if (*k >= size - 1)
            return FALSE;
        out[(*k)++] = *s++;
    }
    out[*k] = 0;
    return TRUE;
}

static BOOL quoted(char *out, int size, int *k, const char *s)
{
    return put(out, size, k, "\"") && put(out, size, k, s) && put(out, size, k, "\"");
}

int lha_command(const char *tmpl, const char *prog, const char *archive, const char *dest,
                char *const *names, int n, char *out, int size)
{
    int k = 0, i;
    char one[2] = { 0, 0 }, d[PATHMAX + 2];
    const char *t;
    scopy(d, dest ? dest : "", PATHMAX);
    if (d[0] && d[slen(d) - 1] != '/' && d[slen(d) - 1] != ':')
        scat(d, "/", sizeof(d));
    out[0] = 0;
    for (t = tmpl; *t; t++) {
        BOOL ok = TRUE;
        if (*t == '%' && t[1]) {
            switch (*++t) {
            case 'l': ok = put(out, size, &k, prog); break;
            case 'a': ok = quoted(out, size, &k, archive); break;
            case 'd': ok = quoted(out, size, &k, d); break;
            case 'n':
                for (i = 0; i < n && ok; i++)
                    ok = (i == 0 || put(out, size, &k, " ")) && quoted(out, size, &k, names[i]);
                break;
            case '%': ok = put(out, size, &k, "%"); break;
            default:
                one[0] = '%';
                ok = put(out, size, &k, one);
                one[0] = *t;
                ok = ok && put(out, size, &k, one);
            }
        } else {
            one[0] = *t;
            ok = put(out, size, &k, one);
        }
        if (!ok)
            return 2;
    }
    return put(out, size, &k, "\n") ? 0 : 2;
}

int lha_packcmds(const char *tmpl, const char *prog, const char *archive, char *const *names,
                 int n, char (*cmds)[LHA_CMDMAX], int max, int maxline)
{
    int done = 0, count = 0;
    while (done < n) {
        int take = 0;
        if (count == max)
            return -1;
        /* as many names as fit: try one more until it doesn't */
        while (done + take < n &&
               lha_command(tmpl, prog, archive, "", names + done, take + 1, cmds[count], LHA_CMDMAX) == 0 &&
               slen(cmds[count]) <= maxline)
            take++;
        if (!take)
            return -1;
        lha_command(tmpl, prog, archive, "", names + done, take, cmds[count], LHA_CMDMAX);
        done += take;
        count++;
    }
    return count;
}
