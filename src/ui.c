/* Oasis user interface: a two-pane file manager window on the Workbench screen, built by
 * hand from V33 Intuition (Kickstart 1.3 has no GadTools).
 * Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * Buttons are gadgets without imagery that Oasis draws itself (so it can show disabled and
 * active states cleanly on 1.3); the path fields are real string gadgets and each list has a
 * real proportional scroller. */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "pane.h"
#include "about.h"
#include "ui.h"

extern struct GfxBase *GfxBase;
extern struct IntuitionBase *IntuitionBase;

#define ROW_H   9
#define PROP_W  16
#define BTN_H   12
#define BGAP    4
#define MIN_W   640
#define MIN_H   150
#define NQUICK  6
#define NNAV    4
#define NBANK   16

/* pens: the four Workbench colours (1.3: blue white black orange; 2.0+: grey black white blue) */
enum { BG, FG, DARK, HI };

enum { GID_PATH = 1, GID_PROP = 3, GID_QUICK = 10, GID_NAV = 20, GID_BANK = 30 };
enum { NAV_DEVS, NAV_PARENT, NAV_ROOT, NAV_RESCAN };
enum { B_COPY, B_MOVE, B_RENAME, B_DELETE, B_MAKEDIR, B_PROTECT, B_COMMENT, B_SIZE,
       B_VIEW, B_HEX, B_SEARCH, B_COMPARE, B_LHA, B_ADF, B_ALL, B_NONE };

struct Btn {
    struct Gadget g;
    const char *label;
    int x, y, w;             /* where it is drawn (window coordinates) */
    BOOL on;                 /* enabled */
    BOOL used;               /* present in this layout */
};

static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
static struct TextFont *font;
static struct Window *win;
static struct RastPort *rp;
static UWORD *ghost;         /* disabled-button pattern, in chip RAM (the blitter reads it) */

static struct Pane pane[2];
static int active;
static int sortmode = SORT_NAME;
static BOOL hideinfo;

static struct Btn quick[NQUICK], nav[NNAV], bank[NBANK];
static char quickName[NQUICK][DEVNAME + 1];
static int nquick;
static const char *const navLabel[NNAV] = { "Devs", "Parent", "Root", "Rescan" };
static const char *const bankLabel[NBANK] = {
    "Copy", "Move", "Rename", "Delete", "MakeDir", "Protect", "Comment", "Size",
    "View", "Hex", "Search", "Compare", "LhA", "ADF", "All", "None"
};

static struct Gadget strGad[2], propGad[2];
static struct StringInfo strInfo[2];
static UBYTE pathBuf[2][PATHMAX], undoBuf[PATHMAX];
static struct PropInfo propInfo[2];
static struct Image knob[2];
static UWORD lastPot[2] = { 0xFFFF, 0xFFFF }, lastBody[2];
static int dragging = -1;

static char info[96];        /* the info line: details of the cursor entry, or a message */
static BOOL infoIsMsg;

/* double-click tracking */
static ULONG clickSecs, clickMicros;
static int clickPane = -1, clickRow = -1;

/* layout */
static int X0, XR, yPath, yBar, listT, listB, rows, yStat, yInfo, yBank;
static int paneX[2], paneW, cols;

/* ---- menus ------------------------------------------------------------------------------ */

enum { M_PROJECT, M_SELECT, M_VIEW };
#define N_PROJ 3
#define N_SEL  2
#define N_VIEW 5
static struct MenuItem projItem[N_PROJ], selItem[N_SEL], viewItem[N_VIEW];
static struct IntuiText projText[N_PROJ], selText[N_SEL], viewText[N_VIEW];
static struct Menu viewMenu = { NULL, 140, 0, 50, 0, MENUENABLED, (UBYTE *)"View", viewItem, 0, 0, 0, 0 };
static struct Menu selMenu = { &viewMenu, 70, 0, 60, 0, MENUENABLED, (UBYTE *)"Select", selItem, 0, 0, 0, 0 };
static struct Menu projMenu = { &selMenu, 0, 0, 70, 0, MENUENABLED, (UBYTE *)"Project", projItem, 0, 0, 0, 0 };

static void mkitems(struct MenuItem *it, struct IntuiText *tx, int n, const char *const *label,
                    const char *keys, int width, const UWORD *flags, const LONG *excl)
{
    int i;
    for (i = 0; i < n; i++) {
        UWORD f = flags ? flags[i] : 0;
        struct IntuiText t = { 0, 1, JAM2, (f & CHECKIT) ? CHECKWIDTH : 2, 1, &topaz8,
                               (UBYTE *)label[i], NULL };
        struct MenuItem m = { i + 1 < n ? &it[i + 1] : NULL, 0, i * 10, width, 10,
                              ITEMTEXT | ITEMENABLED | HIGHCOMP | f | (keys && keys[i] != ' ' ? COMMSEQ : 0),
                              excl ? excl[i] : 0, &tx[i], NULL, keys ? keys[i] : 0, NULL, MENUNULL };
        tx[i] = t;
        it[i] = m;
    }
}

static void initmenus(void)
{
    static const char *const proj[] = { "About...", "Rescan", "Quit" };
    static const char *const sel[] = { "Select all", "Select none" };
    static const char *const view[] = { "Sort by name", "Sort by size", "Sort by date",
                                        "Hide .info files", "Device list" };
    static const UWORD vflags[] = { CHECKIT, CHECKIT, CHECKIT, CHECKIT | MENUTOGGLE, 0 };
    static const LONG vex[] = { 0x06, 0x05, 0x03, 0, 0 };
    int w = CHECKWIDTH + 8 * 16 + COMMWIDTH + 8;
    mkitems(projItem, projText, N_PROJ, proj, " RQ", 8 * 9 + COMMWIDTH + 8, NULL, NULL);
    mkitems(selItem, selText, N_SEL, sel, "AN", 8 * 11 + COMMWIDTH + 8, NULL, NULL);
    mkitems(viewItem, viewText, N_VIEW, view, "   ID", w, vflags, vex);
}

static void setcheck(struct MenuItem *it, BOOL on)
{
    if (on)
        it->Flags |= CHECKED;
    else
        it->Flags &= ~CHECKED;
}

static void syncmenus(void)
{
    int i;
    ClearMenuStrip(win);
    for (i = 0; i < 3; i++)
        setcheck(&viewItem[i], i == sortmode);
    setcheck(&viewItem[3], hideinfo);
    SetMenuStrip(win, &projMenu);
}

/* ---- drawing helpers ------------------------------------------------------------------- */

static void text(int x, int y, int fg, int bg, const char *s, int n)
{
    SetAPen(rp, fg);
    SetBPen(rp, bg);
    SetDrMd(rp, JAM2);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, n);
}

static void textpad(int x, int y, int fg, int bg, const char *s, int n)
{
    char buf[128];
    int i;
    if (n > (int)sizeof(buf))
        n = sizeof(buf);
    if (n <= 0)
        return;
    for (i = 0; i < n && s[i]; i++)
        buf[i] = s[i];
    for (; i < n; i++)
        buf[i] = ' ';
    text(x, y, fg, bg, buf, n);
}

static void fill(int x0, int y0, int x1, int y1, int pen)
{
    if (x1 < x0 || y1 < y0)
        return;
    SetAPen(rp, pen);
    RectFill(rp, x0, y0, x1, y1);
}

static void box(int x0, int y0, int x1, int y1, int pen)
{
    SetAPen(rp, pen);
    Move(rp, x0, y0);
    Draw(rp, x1, y0);
    Draw(rp, x1, y1);
    Draw(rp, x0, y1);
    Draw(rp, x0, y0);
}

/* ---- buttons ------------------------------------------------------------------------------ */

static void drawbtn(struct Btn *b)
{
    int n = slen(b->label);
    if (!b->used)
        return;
    if (b->g.Flags & GRELBOTTOM)        /* follows the window's bottom edge, like its gadget */
        b->y = win->Height + b->g.TopEdge;
    if (n > (b->w - 4) / 8)
        n = (b->w - 4) / 8;
    fill(b->x + 1, b->y + 1, b->x + b->w - 2, b->y + BTN_H - 2, BG);
    box(b->x, b->y, b->x + b->w - 1, b->y + BTN_H - 1, FG);
    text(b->x + (b->w - n * 8) / 2, b->y + 2, FG, BG, b->label, n);
    if (!b->on && ghost) {
        SetDrMd(rp, JAM1);
        SetAfPt(rp, ghost, 1);
        fill(b->x + 1, b->y + 1, b->x + b->w - 2, b->y + BTN_H - 2, BG);
        SetAfPt(rp, NULL, 0);
    }
}

/* enable/disable = switch highlighting; Remove/AddGadget around the change, as required */
static __attribute__((unused)) void enable(struct Btn *b, BOOL on)
{
    UWORD pos;
    if (!b->used || b->on == on)
        return;
    pos = RemoveGadget(win, &b->g);
    b->g.Flags = (b->g.Flags & ~GADGHIGHBITS) | (on ? GADGHCOMP : GADGHNONE);
    AddGadget(win, &b->g, pos);
    b->on = on;
    drawbtn(b);
}

static void initbtn(struct Btn *b, int id, const char *label, int x, int y, int w, BOOL relbottom, BOOL on)
{
    struct Gadget z = { 0 };
    b->g = z;
    b->g.LeftEdge = x;
    b->g.TopEdge = relbottom ? y - win->Height : y;
    b->g.Width = w;
    b->g.Height = BTN_H;
    b->g.Flags = (on ? GADGHCOMP : GADGHNONE) | (relbottom ? GRELBOTTOM : 0);
    b->g.Activation = RELVERIFY;
    b->g.GadgetType = BOOLGADGET;
    b->g.GadgetID = id;
    b->label = label;
    b->x = x;
    b->y = y;
    b->w = w;
    b->on = on;
    b->used = TRUE;
}

/* ---- layout and gadgets -------------------------------------------------------------------- */

static void layout(void)
{
    X0 = win->BorderLeft + 4;
    XR = win->Width - win->BorderRight - 5;
    paneW = (XR - X0 + 1 - 8) / 2;
    paneX[0] = X0;
    paneX[1] = XR - paneW + 1;
    cols = (paneW - PROP_W - 8) / 8;
    yPath = win->BorderTop + 4;
    yBar = yPath + 14;
    listT = yBar + BTN_H + 6;
    yBank = win->Height - win->BorderBottom - 3 - 2 * BTN_H - BGAP;
    yInfo = yBank - 12;
    yStat = yInfo - 11;
    listB = yStat - 4;
    rows = (listB - listT + 1) / ROW_H;
    if (rows < 1)
        rows = 1;
}

static int listL(int i)  { return paneX[i] + 3; }
static int propX(int i)  { return paneX[i] + paneW - PROP_W; }

static void scanquick(void)
{
    static struct DevEntry dev[24];
    int n = dos_devices(dev, 24), i;
    nquick = 0;
    for (i = 0; i < n && nquick < NQUICK; i++)
        if (dev[i].kind == DK_DEVICE) {
            fmt(quickName[nquick], "%s:", dev[i].name);
            nquick++;
        }
}

static void addgadgets(void)
{
    int i, bw, x;
    /* path fields */
    for (i = 0; i < 2; i++) {
        struct Gadget *g = &strGad[i];
        struct StringInfo *si = &strInfo[i];
        struct Gadget z = { 0 };
        si->Buffer = pathBuf[i];
        si->UndoBuffer = undoBuf;
        si->MaxChars = PATHMAX;
        *g = z;
        g->LeftEdge = paneX[i] + 4;
        g->TopEdge = yPath;
        g->Width = paneW - 8;
        g->Height = 8;
        g->Flags = GADGHCOMP;
        g->Activation = RELVERIFY;
        g->GadgetType = STRGADGET;
        g->SpecialInfo = si;
        g->GadgetID = GID_PATH + i;
        AddGadget(win, g, (UWORD)~0);
    }
    /* drive bar: quick drives, then navigation, spread over the full width */
    bw = (XR - X0 + 1 - (NQUICK + NNAV - 1) * BGAP) / (NQUICK + NNAV);
    for (i = 0; i < NQUICK; i++) {
        quick[i].used = FALSE;
        if (i < nquick) {
            initbtn(&quick[i], GID_QUICK + i, quickName[i], X0 + i * (bw + BGAP), yBar, bw, FALSE, TRUE);
            AddGadget(win, &quick[i].g, (UWORD)~0);
        }
    }
    for (i = 0; i < NNAV; i++) {
        x = XR + 1 - (NNAV - i) * bw - (NNAV - 1 - i) * BGAP;
        initbtn(&nav[i], GID_NAV + i, navLabel[i], x, yBar, bw, FALSE, TRUE);
        AddGadget(win, &nav[i].g, (UWORD)~0);
    }
    /* function bank: two rows of eight, anchored to the bottom */
    bw = (XR - X0 + 1 - 7 * BGAP) / 8;
    for (i = 0; i < NBANK; i++) {
        int r = i / 8, c = i % 8;
        BOOL on = (i == B_ALL || i == B_NONE);      /* the rest arrive in later milestones */
        initbtn(&bank[i], GID_BANK + i, bankLabel[i], X0 + c * (bw + BGAP),
                yBank + r * (BTN_H + BGAP), bw, TRUE, on);
        AddGadget(win, &bank[i].g, (UWORD)~0);
    }
    /* list scrollers */
    for (i = 0; i < 2; i++) {
        struct Gadget *g = &propGad[i];
        struct Gadget z = { 0 };
        propInfo[i].Flags = AUTOKNOB | FREEVERT;
        propInfo[i].VertBody = propInfo[i].HorizBody = MAXBODY;
        *g = z;
        g->LeftEdge = propX(i);
        g->TopEdge = listT - 2;
        g->Width = PROP_W;
        g->Height = (listB + 2) - (listT - 2) + 1 - win->Height;
        g->Flags = GADGHNONE | GRELHEIGHT;
        g->Activation = RELVERIFY | GADGIMMEDIATE | FOLLOWMOUSE;
        g->GadgetType = PROPGADGET;
        g->GadgetRender = &knob[i];
        g->SpecialInfo = &propInfo[i];
        g->GadgetID = GID_PROP + i;
        AddGadget(win, g, (UWORD)~0);
    }
    RefreshGadgets(win->FirstGadget, win, NULL);
}

/* ---- panes ----------------------------------------------------------------------------------- */

static void entryrow(char *buf, const struct Pane *p, int i, BOOL cursor)
{
    const struct FEntry *e = &p->e[i];
    int nw = cols - 11, n, k;
    char sz[16];
    char *d = buf;
    *d++ = cursor ? '>' : ' ';
    for (k = 0; k < nw && e->name[k]; k++)
        *d++ = e->name[k];
    for (; k < nw; k++)
        *d++ = ' ';
    switch (e->kind) {
    case EK_FILE:   sizestr(sz, e->size); break;
    case EK_DIR:    scopy(sz, "Drawer", sizeof(sz)); break;
    case EK_DEVICE: scopy(sz, "Device", sizeof(sz)); break;
    case EK_VOLUME: scopy(sz, "Volume", sizeof(sz)); break;
    default:        scopy(sz, "Assign", sizeof(sz));
    }
    n = slen(sz);
    for (k = n; k < 10; k++)
        *d++ = ' ';
    for (k = 0; k < n && k < 10; k++)
        *d++ = sz[k];
    *d = 0;
}

static void drawrows(int i)
{
    struct Pane *p = &pane[i];
    char buf[128];
    int r, x = listL(i), w = cols * 8;
    for (r = 0; r < rows; r++) {
        int k = p->top + r, y = listT + r * ROW_H;
        if (k < p->count) {
            const struct FEntry *e = &p->e[k];
            BOOL cur = (i == active && k == p->cur);
            int fg = e->sel ? DARK : (pane_isdir(e) ? HI : FG), bg = e->sel ? HI : BG;
            entryrow(buf, p, k, cur);
            textpad(x, y, fg, bg, buf, cols);
            fill(x, y + 8, x + w - 1, y + ROW_H - 1, bg);
        } else
            fill(x, y, x + w - 1, y + ROW_H - 1, BG);
    }
    if (!p->count) {
        const char *m = p->err[0] ? p->err : p->devlist ? "No devices" : "(empty drawer)";
        textpad(x + 8, listT + ROW_H, p->err[0] ? HI : FG, BG, m, cols - 2);
    }
}

static void setprop(int i, BOOL force)
{
    struct Pane *p = &pane[i];
    int over = p->count - rows;
    UWORD body, pot;
    if (dragging == i)
        return;
    if (over <= 0) {
        body = MAXBODY;
        pot = 0;
    } else {
        body = (UWORD)((ULONG)rows * MAXBODY / p->count);
        pot = (UWORD)((ULONG)p->top * MAXPOT / over);
    }
    if (force || pot != lastPot[i] || body != lastBody[i]) {
        NewModifyProp(&propGad[i], win, NULL, AUTOKNOB | FREEVERT, 0, pot, MAXBODY, body, 1);
        lastPot[i] = pot;
        lastBody[i] = body;
    }
}

static void topfromprop(int i)
{
    struct Pane *p = &pane[i];
    int over = p->count - rows, t = 0;
    if (over > 0)
        t = (int)(((ULONG)propInfo[i].VertPot * over + MAXPOT / 2) / MAXPOT);
    if (t != p->top) {
        p->top = t;
        drawrows(i);
    }
}

static void drawstatus(int i)
{
    struct Pane *p = &pane[i];
    char line[64], a[16], b[16];
    if (p->devlist)
        fmt(line, "%ld devices, volumes and assigns", (LONG)p->count);
    else if (p->err[0])
        fmt(line, "%s", p->err);
    else if (p->nsel) {
        sizestr(a, p->selbytes);
        fmt(line, "%ld selected (%s bytes)", (LONG)p->nsel, a);
    } else {
        sizestr(a, p->bytes);
        kbstr(b, p->freek);
        fmt(line, "%ld files %ld drawers, %s free", (LONG)p->nfiles, (LONG)p->ndirs, b);
        (void)a;
    }
    textpad(paneX[i] + 3, yStat, i == active ? HI : FG, BG, line, (paneW - 6) / 8);
}

static void drawframe(int i)
{
    int pen = (i == active) ? HI : FG;
    box(paneX[i], listT - 3, paneX[i] + paneW - PROP_W - 2, listB + 3, pen);
    box(paneX[i], yPath - 2, paneX[i] + paneW - 1, yPath + 9, pen);
}

/* comment of the cursor entry: read on demand, comments are not kept in memory */
static void getcomment(const struct Pane *p, const struct FEntry *e, char *out, int size)
{
    char full[PATHMAX];
    struct FileInfoBlock *fib;
    BPTR lock;
    out[0] = 0;
    if (p->devlist || !(fib = AllocMem(sizeof(*fib), MEMF_PUBLIC | MEMF_CLEAR)))
        return;
    scopy(full, p->path, PATHMAX);
    path_add(full, e->name);
    dos_quiet(TRUE);
    if ((lock = Lock((STRPTR)full, ACCESS_READ))) {
        if (Examine(lock, fib))
            scopy(out, (char *)fib->fib_Comment, size);
        UnLock(lock);
    }
    dos_quiet(FALSE);
    FreeMem(fib, sizeof(*fib));
}

static void drawinfo(void)
{
    struct Pane *p = &pane[active];
    char line[128], date[20], prot[10], sz[16], comment[80];
    int n = (XR - X0 + 1) / 8;
    if (!infoIsMsg) {
        if (p->cur < 0 || p->cur >= p->count)
            fmt(line, "%s", p->devlist ? "Double-click a device, volume or assign to open it" : "");
        else {
            const struct FEntry *e = &p->e[p->cur];
            if (p->devlist)
                fmt(line, "%s:  (%s)", e->name, e->kind == EK_DEVICE ? "device"
                                       : e->kind == EK_VOLUME ? "volume" : "assign");
            else {
                dos_datestr(date, &e->date);
                dos_protstr(prot, e->prot);
                getcomment(p, e, comment, sizeof(comment));
                if (e->kind == EK_DIR)
                    fmt(line, "%s  drawer  %s  %s", e->name, date, prot);
                else {
                    fmt(sz, "%ld", e->size);
                    fmt(line, "%s  %s bytes  %s  %s", e->name, sz, date, prot);
                }
                if (comment[0]) {
                    scat(line, "  : ", sizeof(line));
                    scat(line, comment, sizeof(line));
                }
            }
        }
        scopy(info, line, sizeof(info));
    }
    textpad(X0, yInfo, infoIsMsg ? HI : FG, BG, info, n);
}

static void message(const char *m)
{
    scopy(info, m, sizeof(info));
    infoIsMsg = TRUE;
    drawinfo();
}

static void setpathgad(int i)
{
    struct StringInfo *si = &strInfo[i];
    int len, vis = (paneW - 8) / 8;
    UWORD pos = RemoveGList(win, &strGad[i], 1);
    scopy((char *)pathBuf[i], pane[i].path, PATHMAX);
    len = slen((char *)pathBuf[i]);
    si->BufferPos = len;
    si->DispPos = len > vis ? len - vis : 0;   /* show the end of long paths */
    AddGList(win, &strGad[i], pos, 1, NULL);
    fill(paneX[i] + 1, yPath - 1, paneX[i] + paneW - 2, yPath + 8, BG);
    RefreshGList(&strGad[i], win, NULL, 1);
}

static void drawpane(int i)
{
    drawframe(i);
    drawrows(i);
    drawstatus(i);
    setprop(i, FALSE);
}

static void showsel(struct Pane *p)
{
    if (p->cur < 0)
        return;
    if (p->cur < p->top)
        p->top = p->cur;
    if (p->cur >= p->top + rows)
        p->top = p->cur - rows + 1;
}

static void clamptop(struct Pane *p)
{
    if (p->top > p->count - rows)
        p->top = p->count - rows;
    if (p->top < 0)
        p->top = 0;
}

/* (re)read pane i from its path, or its device list */
static void loadpane(int i)
{
    struct Pane *p = &pane[i];
    if (p->devlist || !p->path[0])
        pane_devlist(p);
    else
        pane_read(p, sortmode, hideinfo);
    setpathgad(i);
    infoIsMsg = FALSE;
    drawpane(i);
    if (i == active)
        drawinfo();
}

static void go(int i, const char *path)
{
    scopy(pane[i].path, path, PATHMAX);
    pane[i].devlist = !path[0];
    loadpane(i);
}

static void setactive(int i)
{
    int old = active;
    if (i == active)
        return;
    active = i;
    drawframe(old);
    drawrows(old);
    drawstatus(old);
    drawpane(i);
    infoIsMsg = FALSE;
    drawinfo();
}

static void open_entry(int i, int k)
{
    struct Pane *p = &pane[i];
    char path[PATHMAX];
    const struct FEntry *e;
    if (k < 0 || k >= p->count)
        return;
    e = &p->e[k];
    if (p->devlist) {
        fmt(path, "%s:", e->name);
        go(i, path);
    } else if (e->kind == EK_DIR) {
        scopy(path, p->path, PATHMAX);
        path_add(path, e->name);
        go(i, path);
    } else
        message("Viewers arrive in a later test version of Oasis.");
}

static void parent(int i)
{
    char path[PATHMAX];
    if (pane[i].devlist)
        return;
    scopy(path, pane[i].path, PATHMAX);
    if (path_parent(path))
        go(i, path);
    else
        go(i, "");                    /* above the root: the device list */
}

static void movecursor(int i, int k)
{
    struct Pane *p = &pane[i];
    if (!p->count)
        return;
    if (k < 0)
        k = 0;
    if (k >= p->count)
        k = p->count - 1;
    p->cur = k;
    showsel(p);
    drawrows(i);
    setprop(i, FALSE);
    infoIsMsg = FALSE;
    drawinfo();
}

static void redrawall(void)
{
    int i;
    for (i = 0; i < NQUICK; i++)
        drawbtn(&quick[i]);
    for (i = 0; i < NNAV; i++)
        drawbtn(&nav[i]);
    for (i = 0; i < NBANK; i++)
        drawbtn(&bank[i]);
    for (i = 0; i < 2; i++) {
        drawpane(i);
        setprop(i, TRUE);
    }
    drawinfo();
}

static void resized(void)
{
    int i;
    layout();
    fill(win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1,
         win->Height - win->BorderBottom - 1, BG);
    RefreshGadgets(win->FirstGadget, win, NULL);
    for (i = 0; i < 2; i++) {
        clamptop(&pane[i]);
        showsel(&pane[i]);
    }
    redrawall();
}

/* ---- events ---------------------------------------------------------------------------------- */

static void click(int mx, int my, ULONG secs, ULONG micros)
{
    int i, r, k;
    for (i = 0; i < 2; i++) {
        int x = listL(i);
        if (mx < x || mx >= x + cols * 8 || my < listT || my >= listT + rows * ROW_H)
            continue;
        setactive(i);
        r = (my - listT) / ROW_H;
        k = pane[i].top + r;
        if (k >= pane[i].count)
            return;
        if (clickPane == i && clickRow == k && DoubleClick(clickSecs, clickMicros, secs, micros)) {
            clickPane = -1;
            open_entry(i, k);
            return;
        }
        clickPane = i;
        clickRow = k;
        clickSecs = secs;
        clickMicros = micros;
        pane_select(&pane[i], k, !pane[i].e[k].sel);
        pane[i].cur = k;
        drawrows(i);
        drawstatus(i);
        infoIsMsg = FALSE;
        drawinfo();
        return;
    }
}

static void key(UWORD code, UWORD qual)
{
    struct Pane *p = &pane[active];
    BOOL shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    if (code & IECODE_UP_PREFIX)
        return;
    switch (code) {
    case 0x4C: movecursor(active, p->cur - (shift ? rows - 1 : 1)); break;    /* up */
    case 0x4D: movecursor(active, p->cur + (shift ? rows - 1 : 1)); break;    /* down */
    case 0x44: case 0x43: open_entry(active, p->cur); break;                  /* Return, Enter */
    case 0x41: parent(active); break;                                         /* Backspace */
    case 0x42: setactive(1 - active); break;                                  /* Tab */
    case 0x40:                                                                /* Space */
        if (p->cur >= 0 && p->cur < p->count) {
            pane_select(p, p->cur, !p->e[p->cur].sel);
            drawstatus(active);
            movecursor(active, p->cur + 1);
        }
        break;
    }
}

static void selectall(BOOL on)
{
    pane_selectall(&pane[active], on);
    drawrows(active);
    drawstatus(active);
}

static void resort(void)
{
    int i;
    for (i = 0; i < 2; i++)
        if (!pane[i].devlist) {
            pane_sort(&pane[i], sortmode);
            drawrows(i);
        }
}

static void menupick(UWORD code, BOOL *quit)
{
    while (code != MENUNULL) {
        struct MenuItem *it = ItemAddress(&projMenu, code);
        int m = MENUNUM(code), n = ITEMNUM(code);
        if (m == M_PROJECT) {
            if (n == 0)
                about_show(win, &topaz8);
            else if (n == 1)
                loadpane(active);
            else
                *quit = TRUE;
        } else if (m == M_SELECT)
            selectall(n == 0);
        else if (m == M_VIEW) {
            if (n < 3 && n != sortmode) {
                sortmode = n;
                resort();
            } else if (n == 3) {
                hideinfo = (viewItem[3].Flags & CHECKED) != 0;
                loadpane(0);
                loadpane(1);
            } else if (n == 4)
                go(active, "");
        }
        code = it ? it->NextSelect : MENUNULL;
    }
    syncmenus();
}

static void diskchange(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        struct Pane *p = &pane[i];
        /* re-read what a disk change can affect: the device list, failed panes, floppies */
        if (p->devlist || p->err[0] || (p->path[0] == 'D' || p->path[0] == 'd'))
            loadpane(i);
    }
}

static void gadgetup(struct Gadget *g)
{
    int id = g->GadgetID;
    if (id == GID_PATH || id == GID_PATH + 1) {
        int i = id - GID_PATH;
        setactive(i);
        go(i, (char *)pathBuf[i]);
    } else if (id == GID_PROP || id == GID_PROP + 1) {
        int i = id - GID_PROP;
        topfromprop(i);
        dragging = -1;
        setprop(i, TRUE);
    } else if (id >= GID_QUICK && id < GID_QUICK + NQUICK)
        go(active, quickName[id - GID_QUICK]);
    else if (id >= GID_NAV && id < GID_NAV + NNAV) {
        switch (id - GID_NAV) {
        case NAV_DEVS:   go(active, ""); break;
        case NAV_PARENT: parent(active); break;
        case NAV_ROOT:
            if (!pane[active].devlist) {
                char path[PATHMAX];
                scopy(path, pane[active].path, PATHMAX);
                path_root(path);
                go(active, path);
            }
            break;
        case NAV_RESCAN: loadpane(active); break;
        }
    } else if (id >= GID_BANK && id < GID_BANK + NBANK) {
        int b = id - GID_BANK;
        if (!bank[b].on)
            return;
        if (b == B_ALL)
            selectall(TRUE);
        else if (b == B_NONE)
            selectall(FALSE);
    }
}

BOOL ui_handle(void)
{
    struct IntuiMessage *im;
    BOOL quit = FALSE;
    while ((im = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG class = im->Class, secs = im->Seconds, micros = im->Micros;
        UWORD code = im->Code, qual = im->Qualifier;
        WORD mx = im->MouseX, my = im->MouseY;
        struct Gadget *g = (struct Gadget *)im->IAddress;
        ReplyMsg((struct Message *)im);
        switch (class) {
        case CLOSEWINDOW:   quit = TRUE; break;
        case GADGETDOWN:
            if (g->GadgetID == GID_PROP || g->GadgetID == GID_PROP + 1) {
                dragging = g->GadgetID - GID_PROP;
                setactive(dragging);
                topfromprop(dragging);
            }
            break;
        case MOUSEMOVE:     if (dragging >= 0) topfromprop(dragging); break;
        case GADGETUP:      gadgetup(g); break;
        case MOUSEBUTTONS:  if (code == SELECTDOWN) click(mx, my, secs, micros); break;
        case RAWKEY:        key(code, qual); break;
        case MENUPICK:      menupick(code, &quit); break;
        case NEWSIZE:       resized(); break;
        case DISKINSERTED:
        case DISKREMOVED:   diskchange(); break;
        case REFRESHWINDOW: BeginRefresh(win); EndRefresh(win, TRUE); break;
        }
    }
    return !quit;
}

/* ---- open / close ------------------------------------------------------------------------------ */

BOOL ui_open(const char *left, const char *right)
{
    int h = GfxBase->NormalDisplayRows < 200 ? GfxBase->NormalDisplayRows : 200;
    struct NewWindow nw = {
        0, 0, MIN_W, 200, 0, 1,
        CLOSEWINDOW | GADGETUP | GADGETDOWN | MOUSEMOVE | MOUSEBUTTONS | MENUPICK | NEWSIZE |
            REFRESHWINDOW | RAWKEY | DISKINSERTED | DISKREMOVED,
        WINDOWCLOSE | WINDOWDRAG | WINDOWDEPTH | WINDOWSIZING | SIZEBRIGHT | SMART_REFRESH | ACTIVATE,
        NULL, NULL, (UBYTE *)"Oasis 1.0  -  Desert Palms Software", NULL, NULL,
        MIN_W, MIN_H, 0, 0, WBENCHSCREEN
    };
    nw.Height = h;
    nw.MaxWidth = 1024;
    nw.MaxHeight = 1024;
    if ((ghost = AllocMem(2 * sizeof(UWORD), MEMF_CHIP))) {
        ghost[0] = 0x8888;
        ghost[1] = 0x2222;
    }
    if (!(win = OpenWindow(&nw)))
        return FALSE;
    WindowLimits(win, MIN_W, MIN_H, win->WScreen->Width, win->WScreen->Height);
    rp = win->RPort;
    if ((font = OpenFont(&topaz8)))
        SetFont(rp, font);
    dos_setwindow(win);              /* "insert disk" requesters appear on our screen */
    initmenus();
    syncmenus();
    layout();
    scanquick();
    addgadgets();
    scopy(pane[0].path, left ? left : "", PATHMAX);
    scopy(pane[1].path, right ? right : "", PATHMAX);
    pane[0].devlist = !pane[0].path[0];
    pane[1].devlist = !pane[1].path[0];
    redrawall();
    loadpane(0);
    loadpane(1);
    /* 1.3 reports 200 display rows until the Workbench prefs are loaded: opened at the safe
       size, now grow to fill the real screen (Intuition then sends NEWSIZE: relayout) */
    {
        struct Screen *s = win->WScreen;
        int dw = s->Width - win->LeftEdge - win->Width, dh = s->Height - win->TopEdge - win->Height;
        if (dw > 0 || dh > 0)
            SizeWindow(win, dw > 0 ? dw : 0, dh > 0 ? dh : 0);
    }
    return TRUE;
}

void ui_close(void)
{
    int i;
    dos_setwindow(NULL);
    if (win) {
        ClearMenuStrip(win);
        CloseWindow(win);
        win = NULL;
    }
    for (i = 0; i < 2; i++)
        pane_free(&pane[i]);
    if (font)
        CloseFont(font);
    if (ghost)
        FreeMem(ghost, 2 * sizeof(UWORD));
    font = NULL;
    ghost = NULL;
}

ULONG ui_sigmask(void)
{
    return 1UL << win->UserPort->mp_SigBit;
}
