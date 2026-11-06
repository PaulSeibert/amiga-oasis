/* Oasis - the prefs file and custom buttons. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "pane.h"
#include "prefs.h"

#define MAXFILE 8192

static const char *skipsp(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* one word or "quoted text" into d; returns the rest of the line */
static const char *word(const char *s, char *d, int size)
{
    int n = 0;
    s = skipsp(s);
    if (*s == '"') {
        for (s++; *s && *s != '"'; s++)
            if (n < size - 1)
                d[n++] = *s;
        if (*s == '"')
            s++;
    } else
        for (; *s && *s != ' ' && *s != '\t'; s++)
            if (n < size - 1)
                d[n++] = *s;
    d[n] = 0;
    return s;
}

static BOOL is(const char *a, const char *b)
{
    return !nocase_cmp(a, b);
}

static BOOL yes(const char *w, BOOL *ok)
{
    if (is(w, "YES") || is(w, "ON") || is(w, "1"))
        return TRUE;
    if (!(is(w, "NO") || is(w, "OFF") || is(w, "0")))
        *ok = FALSE;
    return FALSE;
}

static BOOL line(struct Prefs *p, const char *s)
{
    char key[16], w[PATHMAX];
    BOOL ok = TRUE;
    s = skipsp(s);
    if (!*s || *s == ';' || *s == '#')
        return TRUE;
    s = word(s, key, sizeof(key));
    if (is(key, "LEFT") || is(key, "RIGHT")) {
        word(s, w, sizeof(w));
        if (is(key, "LEFT")) {
            scopy(p->left, w, PATHMAX);
            p->hasleft = TRUE;
        } else {
            scopy(p->right, w, PATHMAX);
            p->hasright = TRUE;
        }
    } else if (is(key, "SORT")) {
        word(s, w, sizeof(w));
        p->sort = is(w, "SIZE") ? SORT_SIZE : is(w, "DATE") ? SORT_DATE : SORT_NAME;
        p->hassort = is(w, "NAME") || p->sort != SORT_NAME;
        ok = p->hassort;
    } else if (is(key, "HIDEICONS")) {
        word(s, w, sizeof(w));
        p->hideinfo = yes(w, &ok);
        p->hashide = ok;
    } else if (is(key, "BUTTON")) {
        struct PrefButton *b;
        const char *c;
        int n;
        if (p->nbuttons == MAXBUTTONS)
            return FALSE;
        b = &p->b[p->nbuttons];
        s = word(s, b->label, BLABEL);
        c = word(s, w, sizeof(w));
        b->quiet = is(w, "QUIET");
        if (b->quiet)
            s = c;
        s = skipsp(s);
        n = slen(s);
        while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
            n--;
        /* the whole command in quotes, as in "Run Player %s": take off the quotes */
        if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
            s++;
            n -= 2;
        }
        if (!b->label[0] || !n || n >= BCMD)
            return FALSE;
        scopy(b->cmd, s, n + 1);
        p->nbuttons++;
    } else
        ok = FALSE;
    return ok;
}

BOOL prefs_load(const char *file, struct Prefs *p)
{
    BPTR fh;
    char *buf, *s, *e;
    LONG n;
    int lineno = 0;
    char *c = (char *)p;
    for (n = 0; n < (LONG)sizeof(*p); n++)      /* (no struct copy: it is too big for 1.3 stacks) */
        c[n] = 0;
    if (!(buf = AllocMem(MAXFILE + 1, MEMF_PUBLIC)))
        return FALSE;
    dos_quiet(TRUE);
    fh = Open((STRPTR)file, MODE_OLDFILE);
    dos_quiet(FALSE);
    if (!fh) {
        FreeMem(buf, MAXFILE + 1);
        return FALSE;
    }
    n = Read(fh, buf, MAXFILE);
    Close(fh);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    for (s = buf; *s; s = e) {
        for (e = s; *e && *e != '\n'; e++)
            ;
        if (*e)
            *e++ = 0;
        if ((n = slen(s)) && s[n - 1] == '\r')     /* files written on a PC */
            s[n - 1] = 0;
        lineno++;
        if (!line(p, s)) {
            if (!p->badline)
                p->badline = lineno;
            p->nbad++;
        }
    }
    FreeMem(buf, MAXFILE + 1);
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

int prefs_expand(const char *tmpl, const char *src, const char *dst,
                 char *const *files, int n, char *out, int size)
{
    int k = 0, i;
    const char *t;
    char one[2] = { 0, 0 };
    for (t = tmpl; *t; t++)
        if (*t == '%' && (t[1] == 's' || t[1] == 'f') && !n)
            return 1;
    out[0] = 0;
    for (t = tmpl; *t; t++) {
        BOOL ok = TRUE;
        if (*t == '%' && t[1]) {
            switch (*++t) {
            case 's':
                for (i = 0; i < n && ok; i++)
                    ok = (i == 0 || put(out, size, &k, " ")) && quoted(out, size, &k, files[i]);
                break;
            case 'f': ok = quoted(out, size, &k, files[0]); break;
            case 'p': ok = quoted(out, size, &k, src); break;
            case 'd': ok = quoted(out, size, &k, dst); break;
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
    /* AmigaDOS command lines end with a newline: without one, some programs' argument
       parsers run past the end (a trailing "" hung libnix programs) */
    return put(out, size, &k, "\n") ? 0 : 2;
}
