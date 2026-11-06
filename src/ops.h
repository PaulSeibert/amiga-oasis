/* Oasis - the file operations engine: copy, move, delete, rename, make drawer.
 * Used by both the window and command mode. Icons travel with their files: copying,
 * moving, deleting or renaming "foo" also handles "foo.info" when it exists. */
#ifndef OPS_H
#define OPS_H
#include <exec/types.h>
#include <dos/dos.h>

/* answers from the hooks */
enum { ANS_RETRY, ANS_SKIP, ANS_ABORT, ANS_FORCE, ANS_REPLACE };

enum { OP_COPY, OP_MOVE, OP_DELETE };

struct OpsHooks {
    void *ctx;
    /* the current file and how far it has got; FALSE = stop (checked between buffers) */
    BOOL (*progress)(void *ctx, int op, const char *name, ULONG done, ULONG total);
    /* something failed: ANS_RETRY, ANS_SKIP, ANS_ABORT, or ANS_FORCE (only offered for
       ERROR_DELETE_PROTECTED: clear the protection and try again) */
    int  (*error)(void *ctx, const char *what, const char *path, LONG err);
    /* the destination exists: ANS_REPLACE, ANS_SKIP or ANS_ABORT */
    int  (*exists)(void *ctx, const char *path);
};

struct OpsStats {
    ULONG files, dirs, bytes;        /* done */
    ULONG skipped;                   /* left out after an error or a "skip" answer */
    ULONG datesSet, datesFailed;     /* copies whose date could / could not be kept */
    BOOL  aborted;
};

/* a drawer's entries (names only), for walking trees */
struct NameEnt { char name[31]; UBYTE dir; };
struct Names { struct NameEnt *v; int n, cap; };
LONG ops_list(const char *path, struct Names *out, BOOL sorted);   /* 0 or a DOS error */
void ops_freenames(struct Names *names);

/* One batch = ops_begin, any number of operations, ops_end. */
void ops_begin(struct OpsHooks *hooks, struct OpsStats *stats);
void ops_end(void);
BOOL ops_copy(const char *srcdir, const char *name, const char *dstdir);
BOOL ops_move(const char *srcdir, const char *name, const char *dstdir);
BOOL ops_delete(const char *dir, const char *name);

/* single actions: 0 or a DOS error code */
LONG ops_rename(const char *dir, const char *from, const char *to);
LONG ops_makedir(const char *dir, const char *name);

/* TRUE if the two paths are the same drawer / if inner is outer or inside it */
BOOL ops_samedir(const char *a, const char *b);
BOOL ops_inside(const char *inner, const char *outer);

/* "foo.info" is selected together with "foo": handled with foo, so leave it out */
BOOL ops_isicon(const char *name, char *base, int size);

#endif
