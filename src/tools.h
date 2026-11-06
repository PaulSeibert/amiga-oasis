/* Oasis - tools: size of a tree, search by name, protection bits, comparing two drawers.
 * Used by both the window and command mode. */
#ifndef TOOLS_H
#define TOOLS_H
#include <exec/types.h>
#include <dos/dos.h>
#include "pane.h"

/* called for each drawer a walk enters; FALSE = stop */
typedef BOOL (*ToolPoll)(void *ctx, const char *drawer);

struct SizeStats { ULONG files, dirs, bytes, blocks, unreadable; BOOL aborted; };
/* add up path (a file, or a drawer and everything in it; the drawer itself is not counted) */
LONG tools_size(const char *path, struct SizeStats *st, ToolPoll poll, void *ctx);

/* every entry below drawer whose name matches pattern (see match.h); hit() gets the drawer
   relative to the start ("" = the start itself) and the entry; FALSE from hit() stops.
   Returns 0 or the DOS error for the start drawer; *unreadable counts drawers that failed. */
typedef BOOL (*ToolHit)(void *ctx, const char *reldir, const struct FileInfoBlock *fib);
LONG tools_search(const char *drawer, const char *pattern, ToolHit hit, ToolPoll poll, void *ctx,
                  ULONG *unreadable, BOOL *aborted);

/* "rwed", "+s", "-wd" (the AmigaDOS Protect letters hsparwed): the new protection value
   from the current one; FALSE for letters that aren't protection bits */
BOOL tools_parseprot(const char *spec, LONG cur, LONG *out);
BOOL tools_protbit(LONG prot, int i);              /* letter i of "hsparwed" is shown on */
LONG tools_setprotbit(LONG prot, int i, BOOL on);

/* compare two drawer panes by name: marks (selects) in both what is only on one side or
   differs in kind, size or date (to the second; dirs are not compared inside) */
struct CmpStats { int onlyA, onlyB, differ, same; };
void tools_compare(struct Pane *a, struct Pane *b, struct CmpStats *st);

#endif
