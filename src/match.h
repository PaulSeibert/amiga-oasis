/* Oasis - AmigaDOS-style wildcards (1.3 has no MatchPattern), always without case:
 *   ?        any one character          #x      any number of x (x: a character, ? or a group)
 *   *        any characters (= #?)      (a|b)   a or b
 *   'x       x itself (so '? is a question mark)
 * A pattern with no wildcard in it matches names that contain it ("txt" finds "readme.txt"). */
#ifndef MATCH_H
#define MATCH_H

int match_haswild(const char *pat);
int match(const char *pat, const char *name);          /* the whole name must match */
int match_search(const char *pat, const char *name);   /* as above, or "contains" for plain text */

#endif
