/* Oasis - tools. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * The walks share one static path buffer and one FileInfoBlock, and read each drawer's names
 * in full (ops_list) before going into its sub-drawers, so a level of recursion costs only a
 * few dozen bytes of stack (1.3 CLI stacks are 4 KB). */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "ops.h"
#include "match.h"
#include "tools.h"

static char wp[PATHMAX];                /* the walk's current path */
static int wbase;                       /* where the part relative to the start begins */
static struct FileInfoBlock *wfib;
static ToolPoll wpoll;
static void *wctx;
static BOOL wstop;
static ULONG wbad;

/* what to do with each entry: size or search */
static struct SizeStats *wsize;
static const char *wpat;
static ToolHit whit;

static BOOL examine(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    BOOL ok = l && Examine(l, wfib);
    if (l)
        UnLock(l);
    return ok;
}

static void visit(void);

static void walk(void)
{
    struct Names nm;
    int len = slen(wp), i;
    if (wpoll && !wpoll(wctx, wp)) {
        wstop = TRUE;
        return;
    }
    if (ops_list(wp, &nm, TRUE)) {
        wbad++;
        return;
    }
    for (i = 0; i < nm.n && !wstop; i++) {
        if (len + slen(nm.v[i].name) + 2 > PATHMAX) {
            wbad++;
            continue;
        }
        path_add(wp, nm.v[i].name);
        if (examine(wp))
            visit();
        else
            wbad++;
        if (!wstop && nm.v[i].dir)
            walk();
        wp[len] = 0;
    }
    ops_freenames(&nm);
}

static void visit(void)
{
    if (wsize) {
        if (wfib->fib_DirEntryType > 0)
            wsize->dirs++;
        else {
            wsize->files++;
            wsize->bytes += wfib->fib_Size;
        }
        wsize->blocks += wfib->fib_NumBlocks + 1;     /* + the header block */
    } else if (match_search(wpat, (char *)wfib->fib_FileName)) {
        char rel[PATHMAX];
        int i, cut = wbase;
        /* the drawer part of wp below the start */
        for (i = wbase; wp[i]; i++)
            if (wp[i] == '/')
                cut = i;
        scopy(rel, wp + wbase, cut - wbase + 1);
        if (!whit(wctx, rel, wfib))
            wstop = TRUE;
    }
}

static LONG start(const char *path, ToolPoll poll, void *ctx)
{
    scopy(wp, path, PATHMAX);
    wbase = slen(wp);
    if (wbase && wp[wbase - 1] != ':' && wp[wbase - 1] != '/')
        wbase++;                       /* skip the "/" path_add puts in */
    wpoll = poll;
    wctx = ctx;
    wstop = FALSE;
    wbad = 0;
    if (!(wfib = AllocMem(sizeof(*wfib), MEMF_PUBLIC | MEMF_CLEAR)))
        return ERROR_NO_FREE_STORE;
    if (!examine(wp)) {
        LONG err = IoErr();
        FreeMem(wfib, sizeof(*wfib));
        wfib = NULL;
        return err ? err : ERROR_OBJECT_NOT_FOUND;
    }
    return 0;
}

static void finish(void)
{
    if (wfib)
        FreeMem(wfib, sizeof(*wfib));
    wfib = NULL;
    wsize = NULL;
    whit = NULL;
}

LONG tools_size(const char *path, struct SizeStats *st, ToolPoll poll, void *ctx)
{
    LONG err;
    dos_quiet(TRUE);
    if ((err = start(path, poll, ctx))) {
        dos_quiet(FALSE);
        return err;
    }
    wsize = st;
    if (wfib->fib_DirEntryType > 0)
        walk();
    else
        visit();
    st->unreadable += wbad;
    if (wstop)
        st->aborted = TRUE;
    finish();
    dos_quiet(FALSE);
    return 0;
}

LONG tools_search(const char *drawer, const char *pattern, ToolHit hit, ToolPoll poll, void *ctx,
                  ULONG *unreadable, BOOL *aborted)
{
    LONG err;
    dos_quiet(TRUE);
    if ((err = start(drawer, poll, ctx))) {
        dos_quiet(FALSE);
        return err;
    }
    if (wfib->fib_DirEntryType <= 0) {
        finish();
        dos_quiet(FALSE);
        return ERROR_OBJECT_WRONG_TYPE;
    }
    wpat = pattern;
    whit = hit;
    walk();
    *unreadable = wbad;
    *aborted = wstop;
    finish();
    dos_quiet(FALSE);
    return 0;
}

/* ---- protection ------------------------------------------------------------------------ */

static const char letters[] = "hsparwed";

BOOL tools_protbit(LONG prot, int i)
{
    int bit = 7 - i;
    return i < 4 ? (prot >> bit) & 1 : !((prot >> bit) & 1);   /* rwed are stored inverted */
}

LONG tools_setprotbit(LONG prot, int i, BOOL on)
{
    int bit = 7 - i;
    if (i >= 4)
        on = !on;
    return on ? prot | (1L << bit) : prot & ~(1L << bit);
}

BOOL tools_parseprot(const char *spec, LONG cur, LONG *out)
{
    int mode = 0, i;                   /* 0: exactly these, '+' add, '-' take away */
    LONG p = cur;
    if (*spec == '+' || *spec == '-')
        mode = *spec++;
    if (!mode)
        for (i = 0; i < 8; i++)
            p = tools_setprotbit(p, i, FALSE);
    for (; *spec; spec++) {
        int c = *spec | 0x20;
        for (i = 0; i < 8 && letters[i] != c; i++)
            ;
        if (i == 8)
            return FALSE;
        p = tools_setprotbit(p, i, mode != '-');
    }
    *out = p;
    return TRUE;
}

/* ---- compare --------------------------------------------------------------------------- */

static BOOL samefile(const struct FEntry *a, const struct FEntry *b)
{
    if (pane_isdir(a) != pane_isdir(b))
        return FALSE;
    if (pane_isdir(a))
        return TRUE;
    /* to the second: filesystems keep ticks differently (the WinUAE drive keeps none) */
    return a->size == b->size && a->date.ds_Days == b->date.ds_Days &&
           a->date.ds_Minute == b->date.ds_Minute && a->date.ds_Tick / 50 == b->date.ds_Tick / 50;
}

void tools_compare(struct Pane *a, struct Pane *b, struct CmpStats *st)
{
    int i, j;
    st->onlyA = st->onlyB = st->differ = st->same = 0;
    pane_selectall(a, FALSE);
    pane_selectall(b, FALSE);
    for (i = 0; i < a->count; i++) {
        for (j = 0; j < b->count && nocase_cmp(a->e[i].name, b->e[j].name); j++)
            ;
        if (j == b->count) {
            pane_select(a, i, TRUE);
            st->onlyA++;
        } else if (!samefile(&a->e[i], &b->e[j])) {
            pane_select(a, i, TRUE);
            pane_select(b, j, TRUE);
            st->differ++;
        } else
            st->same++;
    }
    for (j = 0; j < b->count; j++) {
        for (i = 0; i < a->count && nocase_cmp(a->e[i].name, b->e[j].name); i++)
            ;
        if (i == a->count) {
            pane_select(b, j, TRUE);
            st->onlyB++;
        }
    }
}
