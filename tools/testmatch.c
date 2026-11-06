/* Host unit test for src/match.c:  cc -I src -o build/testmatch tools/testmatch.c src/match.c */
#include <stdio.h>
#include "match.h"

static int fails;

static void t(const char *pat, const char *name, int want)
{
    int got = match(pat, name);
    if (got != want) {
        printf("FAIL match(\"%s\", \"%s\") = %d, want %d\n", pat, name, got, want);
        fails++;
    }
}

static void ts(const char *pat, const char *name, int want)
{
    int got = match_search(pat, name);
    if (got != want) {
        printf("FAIL match_search(\"%s\", \"%s\") = %d, want %d\n", pat, name, got, want);
        fails++;
    }
}

int main(void)
{
    t("", "", 1);            t("", "a", 0);
    t("abc", "abc", 1);      t("abc", "ABC", 1);     t("abc", "abcd", 0);   t("abcd", "abc", 0);
    t("?", "a", 1);          t("?", "", 0);          t("??", "a", 0);       t("a?c", "abc", 1);
    t("#?", "", 1);          t("#?", "anything", 1); t("*", "x", 1);        t("*", "", 1);
    t("#?.info", "disk.info", 1);  t("#?.info", "disk.inf", 0);  t("#?.INFO", "Disk.Info", 1);
    t("*.txt", "readme.txt", 1);   t("*.txt", "readme.txt.bak", 0);
    t("#a", "", 1);          t("#a", "aaa", 1);      t("#a", "aab", 0);     t("b#ac", "baaac", 1);
    t("#ab", "aab", 1);      t("#ab", "b", 1);       t("#ab", "ab", 1);     t("#ab", "ba", 0);
    t("(a|b)", "a", 1);      t("(a|b)", "b", 1);     t("(a|b)", "c", 0);    t("(a|b)", "ab", 0);
    t("(a|b)#c", "ac", 1);   t("(a|b)#c", "bccc", 1); t("(a|b)#c", "c", 0); t("(a|b)#c", "abc", 0);
    t("(a|b)#c", "a", 1);    t("x(a|b)", "xb", 1);    t("(a|b)(c|d)", "bd", 1);
    t("#?.(txt|doc)", "x.doc", 1);  t("#?.(txt|doc)", "x.txt", 1);  t("#?.(txt|doc)", "x.dot", 0);
    t("#(ab)", "", 1);       t("#(ab)", "abab", 1);  t("#(ab)", "aba", 0);  t("#(ab)c", "ababc", 1);
    t("#(a|)", "aaa", 1);    t("#(a|)b", "b", 1);    t("(|x)y", "y", 1);    t("(|x)y", "xy", 1);
    t("((a|b)c|d)", "bc", 1); t("((a|b)c|d)", "d", 1); t("((a|b)c|d)", "c", 0);
    t("'?", "?", 1);         t("'?", "a", 0);        t("a'#b", "a#b", 1);   t("'(x')", "(x)", 1);
    t("#?a#?b#?c#?", "xxaxxbxxcxx", 1);  t("#?a#?b#?c#?", "xxaxxcxxbxx", 0);
    t("#?#?#?#?#?#?#?#?x", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaay", 0);
    t("\xC4#?", "\xE4pfel", 1);    /* Latin-1 case folding: A-umlaut */
    ts("txt", "readme.txt", 1);    ts("TXT", "readme.txt", 1);  ts("xt.", "readme.txt", 0);
    ts("", "x", 1);                ts("#?.txt", "readme.txt", 1); ts("read", "unread", 1);
    ts("*.c", "a.c", 1);           ts("*.c", "a.cc", 0);
    printf(fails ? "%d FAILED\n" : "match: all passed\n", fails);
    return fails != 0;
}
