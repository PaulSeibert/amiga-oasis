/* Oasis - one directory pane: its path, entries, selection and sort order. */
#ifndef PANE_H
#define PANE_H
#include <exec/types.h>
#include <dos/dos.h>
#include "dos13.h"

#define FNAME 32                     /* AmigaDOS names are at most 30 characters */

enum { EK_FILE, EK_DIR, EK_DEVICE, EK_VOLUME, EK_ASSIGN };
enum { SORT_NAME, SORT_SIZE, SORT_DATE };

struct FEntry {
    char  name[FNAME];
    LONG  size;
    struct DateStamp date;
    LONG  prot;
    UBYTE kind;                      /* EK_... */
    UBYTE sel;
    UBYTE sized;                     /* drawer whose size has been worked out (Size) */
    UBYTE spare;                     /* file type + 1 once looked at (0: not yet) */
    LONG  aux;                       /* search results: offset of its drawer in pool, -1: none */
};

struct Pane {
    char  path[PATHMAX];             /* "" when showing the device list */
    struct FEntry *e;
    int   count, cap;
    int   top, cur;                  /* first visible row, cursor row (-1 = none) */
    int   nfiles, ndirs, nsel;
    ULONG bytes, selbytes, freek;   /* freek: free space in KB */
    char  vol[DEVNAME];
    char  err[48];                   /* why the last read failed, "" if it worked */
    BOOL  devlist;
    BOOL  found;                     /* showing search results for pattern below path */
    char  pattern[64];
    char *pool;                      /* search results: their drawers, relative to path */
    LONG  poolcap, poolused;
};

BOOL pane_read(struct Pane *p, int sort, BOOL hideinfo);   /* reads p->path */
void pane_devlist(struct Pane *p);
void pane_sort(struct Pane *p, int sort);
void pane_free(struct Pane *p);
void pane_select(struct Pane *p, int i, BOOL on);
void pane_selectall(struct Pane *p, BOOL on);
BOOL pane_isdir(const struct FEntry *e);   /* something you can enter */

/* search results: start an empty list, add hits (reldir "" = path itself) */
void pane_beginfound(struct Pane *p, const char *pattern);
BOOL pane_addhit(struct Pane *p, const char *reldir, const struct FileInfoBlock *fib);
const char *pane_reldir(const struct Pane *p, const struct FEntry *e);
void pane_entrydir(const struct Pane *p, const struct FEntry *e, char *out);   /* PATHMAX */

#endif
