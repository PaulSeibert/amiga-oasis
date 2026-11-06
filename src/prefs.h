/* Oasis - S:Oasis.prefs (or Oasis.prefs in the current directory): start drawers, sorting,
 * and custom buttons. A plain text file:
 *
 *   ; comment
 *   LEFT  "Work:"                 drawer for the left pane ("" = the device list)
 *   RIGHT "RAM:"
 *   SORT  NAME | SIZE | DATE
 *   HIDEICONS YES | NO
 *   BUTTON "Label" [QUIET] command line...
 *   LHA "C:LhA"                   the LhA program (C:LhA when not set)
 *   LHAEXTRACT %l x %a %d         how to extract (see lha.h for %l %a %d %n)
 *   LHAPACK %l -r a %a %n         how to pack
 *
 * In a button's command:  %s  the selected entries as quoted full paths (the cursor entry
 * when nothing is selected),  %f  the first of them,  %p  the source drawer,  %d  the
 * destination drawer (the other pane),  %%  a percent sign. The command runs with the
 * source drawer as current directory; its output goes to a window that waits for Return,
 * or nowhere with QUIET. */
#ifndef PREFS_H
#define PREFS_H
#include <exec/types.h>
#include "dos13.h"

#define PREFS_FILE  "S:Oasis.prefs"
#define PREFS_LOCAL "Oasis.prefs"       /* tried next, in the current directory */
#define MAXBUTTONS  12
#define BLABEL      16
#define BCMD        200

struct PrefButton {
    char  label[BLABEL];
    char  cmd[BCMD];
    UBYTE quiet;
};

struct Prefs {
    BOOL hasleft, hasright, hassort, hashide;
    char left[PATHMAX], right[PATHMAX];
    int  sort;                          /* SORT_... */
    BOOL hideinfo;
    int  nbuttons;
    struct PrefButton b[MAXBUTTONS];
    char lha[PATHMAX];                  /* "" = C:LhA */
    char lhaextract[BCMD], lhapack[BCMD];   /* "" = the built-in templates */
    int  badline;                      /* first line Oasis didn't understand, 0 if none */
    int  nbad;
};

/* FALSE if the file can't be read (then p holds the defaults: no settings, no buttons) */
BOOL prefs_load(const char *file, struct Prefs *p);

/* expand a button's command, ending it with a newline as AmigaDOS command lines do.
   files[] are full paths. Returns 0, 1 if the command needs files (%s or %f) and n == 0,
   or 2 if the result doesn't fit in size. */
int prefs_expand(const char *tmpl, const char *src, const char *dst,
                 char *const *files, int n, char *out, int size);

#endif
