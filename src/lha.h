/* Oasis - LhA archives. Listing is built in (it only reads the headers), so it works without
 * the LhA program; extracting and packing run the LhA command (shareware, not included). */
#ifndef LHA_H
#define LHA_H
#include <exec/types.h>

struct LhaStats { LONG files, dirs, packed, original; };

/* a listing of the archive as text (free with FreeMem(*text, *alloc)); 0 or a DOS error.
   ERROR_OBJECT_WRONG_TYPE if it isn't an LhA archive. */
LONG lha_list(const char *path, char **text, LONG *len, LONG *alloc, struct LhaStats *st);

/* the LhA program to use: prog from the prefs, or C:LhA. FALSE if it can't be found. */
BOOL lha_program(const char *prefsprog, char *out, int size);

/* build an LhA command line from a template:
     %l the LhA program   %a the archive (quoted)   %d the destination drawer (quoted, ending
     in / or :, as LhA wants it)   %n the names to pack, quoted, relative to the current drawer
     %% a percent sign
   Ends with a newline. 0, or 2 if it doesn't fit. */
int lha_command(const char *tmpl, const char *prog, const char *archive, const char *dest,
                char *const *names, int n, char *out, int size);

/* packing many names: as many commands as it takes to keep each one under maxline
   characters (1.3 command lines hold 255). Fills cmds[max][LHA_CMDMAX]; returns how many,
   or -1 if one name alone doesn't fit. */
#define LHA_CMDMAX 300
int lha_packcmds(const char *tmpl, const char *prog, const char *archive, char *const *names,
                 int n, char (*cmds)[LHA_CMDMAX], int max, int maxline);

#define LHA_EXTRACT "%l x %a %d"
#define LHA_PACK    "%l -r a %a %n"

#endif
