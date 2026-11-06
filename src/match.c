/* Oasis - wildcard matching. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * Backtracking matcher with an explicit "what comes next" chain instead of copying the pattern:
 * the recursion only goes one level deeper per # or group in the pattern, so it stays small
 * on 1.3's 4 KB stacks. Plain C with no AmigaOS calls (it is also unit-tested on the host). */
#include "match.h"

struct K {                  /* the rest of the pattern to match after the current piece */
    const char *p, *e;
    const struct K *next;
    const char *from;       /* repeat of a group: it must have used up some of the name */
};

static int low(int c)
{
    c &= 0xFF;
    return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) ? c + 32 : c;
}

/* end of the piece starting at p (a character, ?, 'x or a whole (group)) */
static const char *piece(const char *p, const char *e)
{
    int depth = 0;
    if (p >= e)
        return e;
    if (*p == '\'')
        return p + 2 <= e ? p + 2 : e;
    if (*p == '#')                      /* ## : # of a # piece */
        return piece(p + 1, e);
    if (*p != '(')
        return p + 1;
    for (; p < e; p++) {
        if (*p == '\'') {
            p++;
            continue;
        }
        if (*p == '(')
            depth++;
        else if (*p == ')' && --depth == 0)
            return p + 1;
    }
    return e;                           /* no closing bracket: the group runs to the end */
}

static int m(const char *p, const char *e, const char *s, const struct K *k);

static int rest(const char *s, const struct K *k)
{
    if (!k)
        return *s == 0;
    if (k->from && s == k->from)        /* an empty repeat would loop forever */
        return 0;
    return m(k->p, k->e, s, k->next);
}

static int m(const char *p, const char *e, const char *s, const struct K *k)
{
    while (p < e) {
        int c = (unsigned char)*p;
        if (c == '?') {
            if (!*s)
                return 0;
            p++, s++;
        } else if (c == '*' || c == '#') {
            const char *ip, *ie, *after;
            if (c == '*') {
                ip = "?";
                ie = ip + 1;
                after = p + 1;
            } else {
                ip = p + 1;
                ie = piece(ip, e);
                after = ie;
            }
            if (ie - ip == 1 && *ip == '?') {          /* #? : the common case, no recursion per step */
                for (;; s++) {
                    if (m(after, e, s, k))
                        return 1;
                    if (!*s)
                        return 0;
                }
            }
            if (ie - ip == 1 || (ie - ip == 2 && *ip == '\'')) {   /* #x : a run of one character */
                int ch = low(ie[-1]);
                for (;; s++) {
                    if (m(after, e, s, k))
                        return 1;
                    if (!*s || low(*s) != ch)
                        return 0;
                }
            }
            {                                           /* #(group) or ##x: zero, or one then again */
                struct K again;
                if (m(after, e, s, k))
                    return 1;
                again.p = p;
                again.e = e;
                again.next = k;
                again.from = s;
                return m(ip, ie, s, &again);
            }
        } else if (c == '(') {
            const char *close = piece(p, e), *a = p + 1, *q;
            const char *ae = (close > p + 1 && close[-1] == ')') ? close - 1 : close;
            struct K after;
            int depth = 0;
            after.p = close;
            after.e = e;
            after.next = k;
            after.from = 0;
            for (q = a; q <= ae; q++) {                 /* each alternative, split at top-level | */
                if (q < ae && *q == '\'') {
                    q++;
                    continue;
                }
                if (q < ae && *q == '(')
                    depth++;
                else if (q < ae && *q == ')')
                    depth--;
                else if (q == ae || (*q == '|' && depth == 0)) {
                    if (m(a, q, s, &after))
                        return 1;
                    a = q + 1;
                }
            }
            return 0;
        } else {
            if (c == '\'' && p + 1 < e)
                c = (unsigned char)*++p;
            if (low(c) != low(*s) || !*s)
                return 0;
            p++, s++;
        }
    }
    return rest(s, k);
}

int match_haswild(const char *pat)
{
    for (; *pat; pat++)
        if (*pat == '?' || *pat == '#' || *pat == '*' || *pat == '(' || *pat == '|' || *pat == '\'')
            return 1;
    return 0;
}

int match(const char *pat, const char *name)
{
    const char *e = pat;
    while (*e)
        e++;
    return m(pat, e, name, 0);
}

int match_search(const char *pat, const char *name)
{
    const char *s, *a, *b;
    if (match_haswild(pat))
        return match(pat, name);
    for (s = name;; s++) {              /* plain text: contained anywhere */
        for (a = s, b = pat; *b && low(*a) == low(*b); a++, b++)
            ;
        if (!*b)
            return 1;
        if (!*s)
            return 0;
    }
}
