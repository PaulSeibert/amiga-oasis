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
#include "ops.h"
#include "req.h"
#include "tools.h"
#include "prefs.h"
#include "ftype.h"
#include "view.h"
#include "ilbm.h"
#include "svx.h"
#include "lha.h"
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

enum { GID_PATH = 1, GID_PROP = 3, GID_QUICK = 10, GID_NAV = 20, GID_BANK = 30, GID_CUSTOM = 70 };
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

static struct Btn quick[NQUICK], nav[NNAV], bank[NBANK], custom[MAXBUTTONS];
static struct Prefs prefs;          /* S:Oasis.prefs */
static int crows;                   /* rows of custom buttons (0-2) */
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

static void fileop(int b);
static void movecursor(int i, int k);
static void viewentry(int i, int k, BOOL hex);
static void lhaview(const char *full, const char *name);
static void lhaop(void);
static void toolop(int b);
static void runbutton(int i);
static void reloadprefs(void);

/* layout */
static int X0, XR, yPath, yBar, listT, listB, rows, yStat, yInfo, yBank, yCustom;
static int paneX[2], paneW, cols;

/* ---- menus ------------------------------------------------------------------------------ */

enum { M_PROJECT, M_SELECT, M_VIEW };
#define N_PROJ 4
#define N_SEL  2
#define N_VIEW 5
static struct MenuItem projItem[N_PROJ], selItem[N_SEL], viewItem[N_VIEW];
static struct IntuiText projText[N_PROJ], selText[N_SEL], viewText[N_VIEW];
static struct Menu viewMenu = { NULL, 160, 0, 56, 0, MENUENABLED, (UBYTE *)"View", viewItem, 0, 0, 0, 0 };
static struct Menu selMenu = { &viewMenu, 84, 0, 68, 0, MENUENABLED, (UBYTE *)"Select", selItem, 0, 0, 0, 0 };
static struct Menu projMenu = { &selMenu, 0, 0, 80, 0, MENUENABLED, (UBYTE *)"Project", projItem, 0, 0, 0, 0 };

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
    static const char *const proj[] = { "About...", "Reload prefs", "Rescan", "Quit" };
    static const char *const sel[] = { "Select all", "Select none" };
    static const char *const view[] = { "Sort by name", "Sort by size", "Sort by date",
                                        "Hide .info files", "Device list" };
    static const UWORD vflags[] = { CHECKIT, CHECKIT, CHECKIT, CHECKIT | MENUTOGGLE, 0 };
    static const LONG vex[] = { 0x06, 0x05, 0x03, 0, 0 };
    int w = CHECKWIDTH + 8 * 16 + COMMWIDTH + 8;
    mkitems(projItem, projText, N_PROJ, proj, "  RQ", 8 * 13 + COMMWIDTH + 8, NULL, NULL);
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
    crows = (prefs.nbuttons + 7) / 8;
    yCustom = win->Height - win->BorderBottom - 3 - crows * (BTN_H + BGAP) + BGAP;
    yBank = (crows ? yCustom - BGAP : win->Height - win->BorderBottom - 3 + BGAP) - 2 * BTN_H - 2 * BGAP;
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
        BOOL on = i != B_ADF;                    /* ADF imaging: milestone 6 */
        initbtn(&bank[i], GID_BANK + i, bankLabel[i], X0 + c * (bw + BGAP),
                yBank + r * (BTN_H + BGAP), bw, TRUE, on);
        AddGadget(win, &bank[i].g, (UWORD)~0);
    }
    /* custom buttons from the prefs file: up to two more rows of eight */
    for (i = 0; i < MAXBUTTONS; i++) {
        custom[i].used = FALSE;
        if (i < prefs.nbuttons) {
            initbtn(&custom[i], GID_CUSTOM + i, prefs.b[i].label, X0 + (i % 8) * (bw + BGAP),
                    yCustom + (i / 8) * (BTN_H + BGAP), bw, TRUE, TRUE);
            AddGadget(win, &custom[i].g, (UWORD)~0);
        }
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

static void removegadgets(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        RemoveGadget(win, &strGad[i]);
        RemoveGadget(win, &propGad[i]);
    }
    for (i = 0; i < NQUICK; i++)
        if (quick[i].used)
            RemoveGadget(win, &quick[i].g);
    for (i = 0; i < NNAV; i++)
        RemoveGadget(win, &nav[i].g);
    for (i = 0; i < NBANK; i++)
        RemoveGadget(win, &bank[i].g);
    for (i = 0; i < MAXBUTTONS; i++)
        if (custom[i].used)
            RemoveGadget(win, &custom[i].g);
}

/* ---- panes ----------------------------------------------------------------------------------- */

static void entryrow(char *buf, const struct Pane *p, int i, BOOL cursor)
{
    static char full[PATHMAX];
    const struct FEntry *e = &p->e[i];
    const char *nm = e->name;
    int nw = cols - 11, n, k;
    char sz[16];
    char *d = buf;
    *d++ = cursor ? '>' : ' ';
    if (p->found && pane_reldir(p, e)[0]) {       /* a search hit below the start: "dir/name" */
        scopy(full, pane_reldir(p, e), PATHMAX);
        path_add(full, e->name);
        nm = full;
        if ((n = slen(full)) > nw) {             /* too long: keep the end, it says the most */
            nm = full + n - nw + 2;
            *d++ = '.';
            *d++ = '.';
            nw -= 2;
        }
    }
    for (k = 0; k < nw && nm[k]; k++)
        *d++ = nm[k];
    for (; k < nw; k++)
        *d++ = ' ';
    switch (e->kind) {
    case EK_FILE:   sizestr(sz, e->size); break;
    case EK_DIR:
        if (e->sized)
            sizestr(sz, e->size);
        else
            scopy(sz, "Drawer", sizeof(sz));
        break;
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
    else if (p->found && !p->nsel)
        fmt(line, "%ld found: \"%s\"", (LONG)p->count, p->pattern);
    else if (p->err[0])
        fmt(line, "%s", p->err);
    else if (p->nsel) {
        sizestr(a, p->selbytes);
        fmt(line, "%ld selected (%s bytes)", (LONG)p->nsel, a);
    } else {
        sizestr(a, p->bytes);
        kbstr(b, p->freek);
        fmt(line, "%ld file%s %ld drawer%s, %s free", (LONG)p->nfiles, p->nfiles == 1 ? "" : "s",
            (LONG)p->ndirs, p->ndirs == 1 ? "" : "s", b);
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
    pane_entrydir(p, e, full);
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
                static char nm[PATHMAX];
                dos_datestr(date, &e->date);
                dos_protstr(prot, e->prot);
                getcomment(p, e, comment, sizeof(comment));
                scopy(nm, pane_reldir(p, e), PATHMAX);
                path_add(nm, e->name);
                if (e->kind == EK_DIR && e->sized)
                    fmt(line, "%s  drawer, %ld bytes  %s  %s", nm, e->size, date, prot);
                else if (e->kind == EK_DIR)
                    fmt(line, "%s  drawer  %s  %s", nm, date, prot);
                else {
                    struct FEntry *fe = (struct FEntry *)e;
                    if (!fe->spare) {               /* what it is: read once, then remembered */
                        static char full[PATHMAX];
                        pane_entrydir(p, e, full);
                        path_add(full, e->name);
                        fe->spare = (UBYTE)(ft_file(full) + 1);
                    }
                    fmt(sz, "%ld", e->size);
                    if (fe->spare - 1 > FT_EMPTY)
                        fmt(line, "%s  %s, %s bytes  %s  %s", nm, ft_name(fe->spare - 1), sz, date, prot);
                    else                            /* unreadable or empty: just the size */
                        fmt(line, "%s  %s bytes  %s  %s", nm, sz, date, prot);
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

static void dosearch(int i);

/* read what pane i shows: its drawer, the device list, or its search results again */
static void readpane(int i)
{
    struct Pane *p = &pane[i];
    if (p->found)
        dosearch(i);
    else if (p->devlist || !p->path[0])
        pane_devlist(p);
    else
        pane_read(p, sortmode, hideinfo);
}

/* (re)read pane i from its path, or its device list */
static void loadpane(int i)
{
    readpane(i);
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
    pane[i].found = FALSE;
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
    } else if (p->found) {
        /* a search hit: open its drawer (a drawer hit: open the drawer itself) */
        char name[FNAME];
        int j;
        scopy(name, e->name, FNAME);
        pane_entrydir(p, e, path);
        if (e->kind == EK_DIR) {
            path_add(path, name);
            go(i, path);
        } else {
            go(i, path);
            for (j = 0; j < p->count; j++)
                if (!nocase_cmp(p->e[j].name, name)) {
                    movecursor(i, j);
                    break;
                }
        }
    } else if (e->kind == EK_DIR) {
        scopy(path, p->path, PATHMAX);
        path_add(path, e->name);
        go(i, path);
    } else
        viewentry(i, k, FALSE);
}

static void parent(int i)
{
    char path[PATHMAX];
    if (pane[i].devlist)
        return;
    if (pane[i].found) {               /* out of the search results, back to the drawer */
        scopy(path, pane[i].path, PATHMAX);
        go(i, path);
        return;
    }
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
    for (i = 0; i < MAXBUTTONS; i++)
        drawbtn(&custom[i]);
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
    case 0x46: fileop(B_DELETE); break;                                       /* Del */
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
        if (!pane[i].devlist && !pane[i].found) {    /* search hits stay in tree order */
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
                reloadprefs();
            else if (n == 2)
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


/* ---- file operations ------------------------------------------------------------------------- */

static struct {
    int  op;
    BOOL replaceAll, skipAll, resized;
    char shown[32];
} job;

/* keep the user's place when a pane is read again */
static void reloadkeep(int i)
{
    struct Pane *p = &pane[i];
    char name[FNAME];
    int top = p->top, cur = p->cur, k;
    name[0] = 0;
    if (cur >= 0 && cur < p->count)
        scopy(name, p->e[cur].name, FNAME);
    readpane(i);
    for (k = 0; k < p->count; k++)
        if (name[0] && !nocase_cmp(p->e[k].name, name))
            break;
    p->cur = k < p->count ? k : (cur < p->count ? cur : p->count - 1);
    p->top = top;
    clamptop(p);
    showsel(p);
    setpathgad(i);
    drawpane(i);
}

/* answer what arrived while the window wasn't listening: clicks and keys made during an
   operation or a dialog are dropped, so they can't start something unexpected */
static void flushinput(BOOL *stop)
{
    struct IntuiMessage *im;
    while ((im = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG class = im->Class;
        UWORD code = im->Code;
        ReplyMsg((struct Message *)im);
        if (class == NEWSIZE)
            job.resized = TRUE;
        else if (class == REFRESHWINDOW) {
            BeginRefresh(win);
            EndRefresh(win, TRUE);
        } else if (stop && ((class == RAWKEY && code == 0x45) || class == CLOSEWINDOW))
            *stop = TRUE;                    /* Esc or the close gadget stops the operation */
    }
}

static void tail(char *d, const char *path, int max)
{
    int n = slen(path);
    if (n <= max)
        scopy(d, path, max + 1);
    else {
        scopy(d, "...", max + 1);
        scat(d, path + n - (max - 3), max + 1);
    }
}

static BOOL u_progress(void *ctx, int op, const char *name, ULONG done, ULONG total)
{
    static const char *const verb[] = { "Copying", "Moving", "Deleting" };
    BOOL stop = FALSE;
    int n = (XR - X0 + 1) / 8, barw = 160, bx = XR - barw + 1;
    if (job.resized)                         /* the layout is stale until the job ends */
        goto poll;
    if (nocase_cmp(job.shown, name)) {
        char line[96];
        scopy(job.shown, name, sizeof(job.shown));
        fmt(line, "%s %s   (Esc stops)", verb[op], name);
        textpad(X0, yInfo, HI, BG, line, n - (total ? barw / 8 + 1 : 0));
        if (!total)
            fill(bx, yInfo, XR, yInfo + 7, BG);
    }
    if (total) {
        int w = (int)((done >> 4) * (barw - 2) / ((total >> 4) ? (total >> 4) : 1));
        if (w > barw - 2)
            w = barw - 2;
        box(bx, yInfo, XR, yInfo + 7, FG);
        fill(bx + 1, yInfo + 1, bx + w, yInfo + 6, HI);
        fill(bx + 1 + w, yInfo + 1, XR - 1, yInfo + 6, BG);
    }
poll:
    flushinput(&stop);
    return !stop;
}

static int u_error(void *ctx, const char *what, const char *path, LONG err)
{
    char body[200], p[52];
    int a;
    if (job.skipAll)
        return ANS_SKIP;
    tail(p, path, 50);
    fmt(body, "%s\n%s\n\n%s.", what, p, dos_errstr(err));
    if (err == ERROR_DELETE_PROTECTED) {
        a = req_choose(win, &topaz8, "Oasis", body, "Unprotect|Skip|Skip all|Abort");
        if (a == 0)
            return ANS_FORCE;
    } else {
        a = req_choose(win, &topaz8, "Oasis", body, "Retry|Skip|Skip all|Abort");
        if (a == 0)
            return ANS_RETRY;
    }
    if (a == 2)
        job.skipAll = TRUE;
    return a == 3 ? ANS_ABORT : ANS_SKIP;
}

static int u_exists(void *ctx, const char *path)
{
    char body[200], p[52];
    int a;
    if (job.replaceAll)
        return ANS_REPLACE;
    tail(p, path, 50);
    fmt(body, "This file already exists:\n%s\n\nReplace it?", p);
    a = req_choose(win, &topaz8, "Oasis", body, "Replace|Replace all|Skip|Abort");
    if (a == 1)
        job.replaceAll = TRUE;
    return a <= 1 ? ANS_REPLACE : a == 2 ? ANS_SKIP : ANS_ABORT;
}

/* the entries an operation works on: the selection, or the cursor entry when nothing is
   selected; "foo.info" is left out when "foo" is in, because the icon travels with foo */
static BOOL picked(struct Pane *p, int k)
{
    char base[FNAME];
    int j;
    if (p->nsel ? !p->e[k].sel : k != p->cur)
        return FALSE;
    if (p->found && p->nsel && ops_isicon(p->e[k].name, base, FNAME)) {
        for (j = 0; j < p->count; j++)          /* the same drawer, too */
            if (p->e[j].sel && p->e[j].aux == p->e[k].aux && !nocase_cmp(p->e[j].name, base))
                return FALSE;
        return TRUE;
    }
    if (p->nsel && ops_isicon(p->e[k].name, base, FNAME))
        for (j = 0; j < p->count; j++)
            if (p->e[j].sel && !nocase_cmp(p->e[j].name, base))
                return FALSE;
    return TRUE;
}

static void opsummary(int op, const struct OpsStats *st)
{
    static const char *const verb[] = { "Copied", "Moved", "Deleted" };
    char line[128], b[16];
    sizestr(b, (LONG)st->bytes);
    if (op == OP_COPY)
        fmt(line, "%s %ld files, %ld drawers, %s bytes", verb[op], st->files, st->dirs, b);
    else
        fmt(line, "%s %ld files, %ld drawers", verb[op], st->files, st->dirs);
    if (st->skipped) {
        fmt(b, ", %ld skipped", st->skipped);
        scat(line, b, sizeof(line));
    }
    if (st->aborted)
        scat(line, " - stopped", sizeof(line));
    else if (op != OP_DELETE && st->datesFailed && !st->datesSet)
        scat(line, " (this disk can't keep dates)", sizeof(line));
    scat(line, ".", sizeof(line));
    message(line);
}

static void renameop(struct Pane *p)
{
    char name[FNAME], prompt[64], dir[PATHMAX];
    int k, done = 0;
    LONG err;
    for (k = 0; k < p->count; k++) {
        if (!picked(p, k))
            continue;
        scopy(name, p->e[k].name, FNAME);
        fmt(prompt, "New name for \"%s\":", p->e[k].name);
        if (!req_string(win, &topaz8, "Rename", prompt, name, 31))
            break;
        {
            const char *x = name, *y = p->e[k].name;
            while (*x && *x == *y)
                x++, y++;
            if (!name[0] || *x == *y)       /* empty or unchanged (a case change is a rename) */
                continue;
        }
        pane_entrydir(p, &p->e[k], dir);
        if ((err = ops_rename(dir, p->e[k].name, name))) {
            char body[160];
            fmt(body, "Cannot rename \"%s\":\n%s.", p->e[k].name, dos_errstr(err));
            req_choose(win, &topaz8, "Oasis", body, "OK");
        } else
            done++;
    }
    if (done) {
        fmt(prompt, "Renamed %ld.", (LONG)done);
        message(prompt);
    }
}

static void makedirop(struct Pane *p)
{
    char name[FNAME] = "", prompt[80], d[40];
    LONG err;
    int k;
    tail(d, p->path, 36);
    fmt(prompt, "New drawer in %s", d);
    if (!req_string(win, &topaz8, "Make drawer", prompt, name, 31) || !name[0])
        return;
    if ((err = ops_makedir(p->path, name))) {
        char body[160];
        fmt(body, "Cannot make drawer \"%s\":\n%s.", name, dos_errstr(err));
        req_choose(win, &topaz8, "Oasis", body, "OK");
        return;
    }
    flushinput(NULL);
    reloadkeep(active);
    for (k = 0; k < p->count; k++)             /* put the cursor on the new drawer */
        if (!nocase_cmp(p->e[k].name, name)) {
            movecursor(active, k);
            break;
        }
    message("Drawer made.");
}

static void fileop(int b)
{
    struct Pane *s = &pane[active], *d = &pane[1 - active];
    struct OpsHooks h = { NULL, u_progress, u_error, u_exists };
    struct OpsStats st;
    char body[200], where[52];
    int k, n = 0, files = 0, dirs = 0, op;

    if (s->devlist || s->err[0]) {
        message("Open a drawer first.");
        return;
    }
    if (b == B_MAKEDIR) {
        if (s->found) {
            message("This pane shows search results: open a drawer first.");
            return;
        }
        makedirop(s);
        goto done;
    }
    for (k = 0; k < s->count; k++)
        if (picked(s, k)) {
            n++;
            if (s->e[k].kind == EK_DIR)
                dirs++;
            else
                files++;
        }
    if (!n) {
        message("Select something first (click it, or Space).");
        return;
    }
    if (b == B_RENAME) {
        renameop(s);
        flushinput(NULL);
        reloadkeep(active);
        if (!d->devlist && ops_samedir(d->path, s->path))
            reloadkeep(1 - active);
        goto done;
    }
    op = b == B_COPY ? OP_COPY : b == B_MOVE ? OP_MOVE : OP_DELETE;
    if (op != OP_DELETE) {
        if (d->devlist || d->err[0] || d->found) {
            message("Open the destination drawer in the other pane first.");
            return;
        }
        if (!s->found && ops_samedir(s->path, d->path)) {
            message("Both panes show the same drawer: pick another destination.");
            return;
        }
        if (s->found)
            for (k = 0; k < s->count; k++) {
                char dir[PATHMAX];
                if (!picked(s, k))
                    continue;
                pane_entrydir(s, &s->e[k], dir);
                if (ops_samedir(dir, d->path)) {
                    fmt(body, "\"%s\" is already in the destination drawer.", s->e[k].name);
                    message(body);
                    return;
                }
            }
    } else {
        tail(where, s->path, 50);
        if (s->found)
            scat(where, " (search results)", sizeof(where));
        fmt(body, "Delete %ld file%s and %ld drawer%s from\n%s ?%s", (LONG)files,
            files == 1 ? "" : "s", (LONG)dirs, dirs == 1 ? "" : "s", where,
            dirs ? "\n\nDrawers are deleted with everything in them." : "");
        if (req_choose(win, &topaz8, "Delete", body, "Delete|Cancel") != 0) {
            flushinput(NULL);
            return;
        }
    }
    job.op = op;
    job.replaceAll = job.skipAll = FALSE;
    job.shown[0] = 0;
    ops_begin(&h, &st);
    for (k = 0; k < s->count && !st.aborted; k++) {
        char dir[PATHMAX];
        if (!picked(s, k))
            continue;
        pane_entrydir(s, &s->e[k], dir);
        if (op == OP_COPY)
            ops_copy(dir, s->e[k].name, d->path);
        else if (op == OP_MOVE)
            ops_move(dir, s->e[k].name, d->path);
        else
            ops_delete(dir, s->e[k].name);
    }
    ops_end();
    flushinput(NULL);
    if (job.resized) {
        job.resized = FALSE;
        resized();
    }
    if (op == OP_COPY) {
        pane_selectall(s, FALSE);
        drawpane(active);
    } else
        reloadkeep(active);
    reloadkeep(1 - active);
    opsummary(op, &st);
    return;
done:
    flushinput(NULL);
    if (job.resized) {
        job.resized = FALSE;
        resized();
    }
}


/* ---- tools ----------------------------------------------------------------------------------- */

static BOOL t_poll(void *ctx, const char *drawer)
{
    BOOL stop = FALSE;
    char line[96], t[52];
    int n = (XR - X0 + 1) / 8;
    if (!job.resized) {
        tail(t, drawer, 46);
        fmt(line, "%s %s   (Esc stops)", (const char *)ctx, t);
        textpad(X0, yInfo, HI, BG, line, n);
    }
    flushinput(&stop);
    return !stop;
}

static int nhits;
static BOOL searchStopped;
static struct Pane *hitpane;
#define MAXHITS 2000

static BOOL t_hit(void *ctx, const char *reldir, const struct FileInfoBlock *fib)
{
    struct Pane *p = hitpane;
    if (hideinfo && ends_with_nocase((char *)fib->fib_FileName, ".info"))
        return TRUE;
    if (!pane_addhit(p, reldir, fib) || ++nhits >= MAXHITS)
        return FALSE;
    return TRUE;
}

/* fill pane i with the entries below its path that match its pattern */
static void dosearch(int i)
{
    struct Pane *p = &pane[i];
    char pat[64];
    ULONG bad = 0;
    BOOL stopped = FALSE;
    LONG err;
    scopy(pat, p->pattern, sizeof(pat));
    pane_beginfound(p, pat);
    nhits = 0;
    hitpane = p;
    err = tools_search(p->path, pat, t_hit, t_poll, (void *)"Searching", &bad, &stopped);
    if (err) {
        scopy(p->err, dos_errstr(err), sizeof(p->err));
        p->found = FALSE;
    } else if (!p->count)
        scopy(p->err, stopped ? "Search stopped" : "Nothing found", sizeof(p->err));
    searchStopped = stopped && p->count < MAXHITS;
}

static void searchop(struct Pane *p)
{
    static char pat[64];
    char prompt[80], d[36];
    tail(d, p->path, 34);
    fmt(prompt, "Find in %s (#? ? * or part of a name):", d);
    if (!req_string(win, &topaz8, "Search", prompt, pat, sizeof(pat)) || !pat[0]) {
        flushinput(NULL);
        return;
    }
    scopy(p->pattern, pat, sizeof(p->pattern));
    p->found = TRUE;
    flushinput(NULL);
    loadpane(active);
    if (p->found && p->count) {
        char line[96];
        if (p->count >= MAXHITS)
            fmt(line, "More than %ld found: showing the first %ld.", (LONG)MAXHITS, (LONG)MAXHITS);
        else
            fmt(line, "%ld found%s. Double-click one to go there; Parent goes back.", (LONG)p->count,
                searchStopped ? " before you stopped" : "");
        message(line);
    }
}

static void sizeop(struct Pane *p)
{
    struct SizeStats total = { 0 }, st;
    char line[128], full[PATHMAX], b[16];
    int k, nd = 0;
    BOOL any = FALSE;
    for (k = 0; k < p->count && !total.aborted; k++) {
        struct FEntry *e = &p->e[k];
        struct SizeStats z = { 0 };
        if (p->nsel ? !e->sel : k != p->cur)
            continue;
        any = TRUE;
        st = z;
        pane_entrydir(p, e, full);
        path_add(full, e->name);
        if (tools_size(full, &st, t_poll, (void *)"Adding up") == 0 && e->kind == EK_DIR) {
            if (!st.aborted) {
                if (e->sel && e->sized)
                    p->selbytes -= e->size;
                e->size = st.bytes;
                e->sized = 1;
                if (e->sel)
                    p->selbytes += e->size;
            }
            nd++;
        }
        total.files += st.files;
        total.dirs += st.dirs;
        total.bytes += st.bytes;
        total.blocks += st.blocks;
        total.unreadable += st.unreadable;
        total.aborted |= st.aborted;
    }
    if (!any) {
        message("Select something first (click it, or Space).");
        return;
    }
    flushinput(NULL);
    drawpane(active);
    sizestr(b, (LONG)total.bytes);
    fmt(line, "%ld files in %ld drawers: %s bytes, %ld blocks%s%s", total.files, total.dirs + nd, b,
        total.blocks, total.unreadable ? " (some unreadable)" : "", total.aborted ? " - stopped" : ".");
    message(line);
}

static void protectop(struct Pane *p)
{
    BOOL on[8];
    char prompt[64], full[PATHMAX], body[160];
    int k, n = 0, first = -1, done = 0, i;
    LONG prot;
    for (k = 0; k < p->count; k++)
        if (p->nsel ? p->e[k].sel : k == p->cur) {
            if (first < 0)
                first = k;
            n++;
        }
    if (first < 0) {
        message("Select something first (click it, or Space).");
        return;
    }
    for (i = 0; i < 8; i++)
        on[i] = tools_protbit(p->e[first].prot, i);
    if (n == 1)
        fmt(prompt, "Protection of \"%s\":", p->e[first].name);
    else
        fmt(prompt, "Protection for %ld entries:", (LONG)n);
    if (!req_protect(win, &topaz8, "Protect", prompt, on)) {
        flushinput(NULL);
        return;
    }
    for (k = 0; k < p->count; k++) {
        if (!(p->nsel ? p->e[k].sel : k == p->cur))
            continue;
        prot = p->e[k].prot;
        for (i = 0; i < 8; i++)
            prot = tools_setprotbit(prot, i, on[i]);
        pane_entrydir(p, &p->e[k], full);
        path_add(full, p->e[k].name);
        if (SetProtection((STRPTR)full, prot)) {
            p->e[k].prot = prot;
            done++;
        } else {
            fmt(body, "Cannot set the protection of\n\"%s\":\n%s.", p->e[k].name, dos_errstr(IoErr()));
            if (req_choose(win, &topaz8, "Oasis", body, "Go on|Stop") == 1)
                break;
        }
    }
    flushinput(NULL);
    drawpane(active);
    fmt(body, "Protection set on %ld.", (LONG)done);
    message(body);
}

static void commentop(struct Pane *p)
{
    static char text[80];
    char prompt[64], full[PATHMAX], body[160];
    int k, n = 0, first = -1, done = 0;
    for (k = 0; k < p->count; k++)
        if (p->nsel ? p->e[k].sel : k == p->cur) {
            if (first < 0)
                first = k;
            n++;
        }
    if (first < 0) {
        message("Select something first (click it, or Space).");
        return;
    }
    getcomment(p, &p->e[first], text, sizeof(text));
    if (n == 1)
        fmt(prompt, "Comment for \"%s\":", p->e[first].name);
    else
        fmt(prompt, "Comment for %ld entries:", (LONG)n);
    if (!req_string(win, &topaz8, "Comment", prompt, text, 80)) {
        flushinput(NULL);
        return;
    }
    for (k = 0; k < p->count; k++) {
        if (!(p->nsel ? p->e[k].sel : k == p->cur))
            continue;
        pane_entrydir(p, &p->e[k], full);
        path_add(full, p->e[k].name);
        if (SetComment((STRPTR)full, (STRPTR)text))
            done++;
        else {
            fmt(body, "Cannot set the comment of\n\"%s\":\n%s.", p->e[k].name, dos_errstr(IoErr()));
            if (req_choose(win, &topaz8, "Oasis", body, "Go on|Stop") == 1)
                break;
        }
    }
    flushinput(NULL);
    fmt(body, text[0] ? "Comment set on %ld." : "Comment removed from %ld.", (LONG)done);
    infoIsMsg = FALSE;
    drawinfo();
    if (n > 1 || !done)
        message(body);
}

static void compareop(void)
{
    struct CmpStats st;
    char line[128];
    int i;
    for (i = 0; i < 2; i++)
        if (pane[i].devlist || pane[i].found || pane[i].err[0]) {
            message("Compare needs a drawer open in each pane.");
            return;
        }
    if (ops_samedir(pane[0].path, pane[1].path)) {
        message("Both panes show the same drawer.");
        return;
    }
    tools_compare(&pane[0], &pane[1], &st);
    drawpane(0);
    drawpane(1);
    if (!st.onlyA && !st.onlyB && !st.differ)
        fmt(line, "The drawers match: %ld the same (by name, size and date).", (LONG)st.same);
    else
        fmt(line, "Selected: %ld only on the left, %ld only on the right, %ld differ. %ld the same.",
            (LONG)st.onlyA, (LONG)st.onlyB, (LONG)st.differ, (LONG)st.same);
    message(line);
}

static void toolop(int b)
{
    struct Pane *s = &pane[active];
    if (b == B_COMPARE) {
        compareop();
        goto done;
    }
    if (s->devlist || (s->err[0] && !s->found)) {
        message("Open a drawer first.");
        return;
    }
    switch (b) {
    case B_PROTECT: protectop(s); break;
    case B_COMMENT: commentop(s); break;
    case B_SIZE:    sizeop(s); break;
    case B_SEARCH:  searchop(s); break;
    }
done:
    flushinput(NULL);
    if (job.resized) {
        job.resized = FALSE;
        resized();
    }
}

/* ---- viewers --------------------------------------------------------------------------------- */

static BOOL v_poll(void *ctx, LONG done, LONG total)
{
    BOOL stop = FALSE;
    int barw = 160, bx = XR - barw + 1, w;
    if (!job.resized) {
        w = (int)((done >> 4) * (barw - 2) / ((total >> 4) ? (total >> 4) : 1));
        if (w > barw - 2)
            w = barw - 2;
        box(bx, yInfo, XR, yInfo + 7, FG);
        fill(bx + 1, yInfo + 1, bx + w, yInfo + 6, HI);
        fill(bx + 1 + w, yInfo + 1, XR - 1, yInfo + 6, BG);
    }
    flushinput(&stop);
    return !stop;
}

/* look at the file at row k of pane i: a picture, a sound, text, or (hex, or anything else) hex */
static void viewentry(int i, int k, BOOL hex)
{
    static char full[PATHMAX], line[96];
    struct Pane *p = &pane[i];
    struct FEntry *e = &p->e[k];
    const char *msg = NULL;
    int t, n = (XR - X0 + 1) / 8;
    pane_entrydir(p, e, full);
    path_add(full, e->name);
    t = ft_file(full);
    e->spare = (UBYTE)(t + 1);
    if (!hex && t == FT_LHA) {
        lhaview(full, e->name);
        return;
    }
    if (!hex && t == FT_ILBM)
        msg = ilbm_show(full);
    else if (!hex && t == FT_8SVX) {
        struct SvxInfo si;
        LONG ticks;
        fmt(line, "Playing %s   (Esc stops)", e->name);
        textpad(X0, yInfo, HI, BG, line, n - 21);
        msg = svx_play(full, v_poll, NULL, ui_sigmask(), &si, &ticks);
        if (!msg) {
            LONG tenths = (si.samples * 10 + (LONG)si.rate / 2) / (LONG)si.rate;
            fmt(line, "Played %s: %ld.%ld seconds, %ld Hz%s.", e->name, tenths / 10, tenths % 10,
                (LONG)si.rate, si.stereo ? ", stereo" : "");
            msg = line;
        }
    } else {
        LONG err = view_file(win, &topaz8, full, hex || !(t == FT_TEXT || t == FT_EMPTY));
        if (err) {
            fmt(line, "Cannot show %s: %s.", e->name, dos_errstr(err));
            msg = line;
        }
    }
    flushinput(NULL);
    if (job.resized) {
        job.resized = FALSE;
        resized();
    }
    if (msg)
        message(msg);
    else {
        infoIsMsg = FALSE;
        drawinfo();
    }
}

/* ---- custom buttons (S:Oasis.prefs) ----------------------------------------------------------- */

/* run AmigaDOS command lines one after another, in curdir (NULL: Oasis's own), with their
   output in a window that waits for Return (or nowhere, quiet); then read the panes again.
   1 if they all ran, 0 if one couldn't be started, -1 if there was no output window. */
static int runcmds(char **cmds, int n, const char *title, BOOL quiet, const char *curdir)
{
    static char con[96];
    BPTR out, dir = 0, old = 0;
    int ok = 1, i, k;
    if (quiet)
        out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    else {
        struct Screen *scr = win->WScreen;
        fmt(con, "CON:0/%ld/%ld/%ld/Oasis: %s", (LONG)(scr->Height > 200 ? 40 : 11),
            (LONG)scr->Width, (LONG)(scr->Height > 200 ? 160 : 120), title);
        out = Open((STRPTR)con, MODE_NEWFILE);
    }
    if (!out) {
        message("Cannot open a window for the command's output.");
        return -1;
    }
    message("Running the command...");
    if (curdir && curdir[0] && (dir = Lock((STRPTR)curdir, ACCESS_READ)))
        old = CurrentDir(dir);
    for (i = 0; i < n && ok; i++) {
        if (!quiet) {
            Write(out, (APTR)"> ", 2);
            Write(out, cmds[i], slen(cmds[i]));
        }
        ok = Execute((STRPTR)cmds[i], 0, out) ? 1 : 0;
    }
    if (dir) {
        CurrentDir(old);
        UnLock(dir);
    }
    if (!quiet) {
        static const char bye[] = "\nPress Return to close this window. ";
        static const char nogo[] = "Could not run the command (1.3 needs C:Run for this).\n";
        char c;
        if (!ok)
            Write(out, (APTR)nogo, sizeof(nogo) - 1);
        Write(out, (APTR)bye, sizeof(bye) - 1);
        Read(out, &c, 1);
    }
    Close(out);
    flushinput(NULL);
    for (k = 0; k < 2; k++)            /* the commands may have changed files */
        if (!pane[k].devlist)
            reloadkeep(k);
    if (job.resized) {
        job.resized = FALSE;
        resized();
    }
    return ok;
}

static void runbutton(int i)
{
    static char cmd[1024], con[96];
    char *cmdp = cmd;
    struct PrefButton *pb = &prefs.b[i];
    struct Pane *s = &pane[active], *d = &pane[1 - active];
    char **files = NULL, *paths = NULL;
    int k, n = 0, r, ok;
    if (!s->devlist)
        for (k = 0; k < s->count; k++)
            if (s->nsel ? s->e[k].sel : k == s->cur)
                n++;
    if (n && (!(files = AllocMem(n * sizeof(char *), MEMF_PUBLIC)) ||
              !(paths = AllocMem(n * PATHMAX, MEMF_PUBLIC)))) {
        if (files)
            FreeMem(files, n * sizeof(char *));
        message("Not enough memory.");
        return;
    }
    for (k = 0, r = 0; r < n; k++)
        if (s->nsel ? s->e[k].sel : k == s->cur) {
            files[r] = paths + r * PATHMAX;
            pane_entrydir(s, &s->e[k], files[r]);
            path_add(files[r], s->e[k].name);
            r++;
        }
    r = prefs_expand(pb->cmd, s->devlist ? "" : s->path, d->devlist ? "" : d->path, files, n, cmd, sizeof(cmd));
    if (n) {
        FreeMem(files, n * sizeof(char *));
        FreeMem(paths, n * PATHMAX);
    }
    if (r) {
        fmt(con, r == 1 ? "Select the files for \"%s\" first." : "Too many files for \"%s\": select fewer.",
            pb->label);
        message(con);
        return;
    }
    ok = runcmds(&cmdp, 1, pb->label, pb->quiet, s->devlist ? NULL : s->path);
    if (ok >= 0) {
        fmt(con, ok ? "Done: %s." : "Could not run \"%s\".", pb->label);
        message(con);
    }
}

/* ---- LhA archives ------------------------------------------------------------------------------- */

#define MAXPACK 40               /* pack commands for one selection (each up to ~230 characters) */

static void lhaview(const char *full, const char *name)
{
    static char line[120];
    struct LhaStats st;
    char *text, a[16], b[16];
    LONG len, alloc, err;
    message("Reading the archive...");
    if ((err = lha_list(full, &text, &len, &alloc, &st))) {
        if (err == ERROR_OBJECT_WRONG_TYPE)
            fmt(line, "%s is not an LhA archive.", name);
        else
            fmt(line, "Cannot read %s: %s.", name, dos_errstr(err));
        message(line);
        return;
    }
    err = view_text(win, &topaz8, name, text, len);
    FreeMem(text, alloc);
    flushinput(NULL);
    if (job.resized) {
        job.resized = FALSE;
        resized();
    }
    sizestr(a, st.original);
    sizestr(b, st.packed);
    fmt(line, "%s: %ld files, %ld drawers, %s bytes packed into %s.", name, st.files, st.dirs, a, b);
    message(err ? "Not enough memory to show the list." : line);
}

/* the LhA program, or a friendly word about where to get it */
static BOOL needlha(char *prog)
{
    char body[300];
    if (lha_program(prefs.lha, prog, PATHMAX))
        return TRUE;
    if (prefs.lha[0])
        fmt(body, "Oasis can't find the LhA program named in the prefs:\n%s\n\n"
                  "Looking inside archives works without it.", prefs.lha);
    else
        scopy(body, "Extracting and packing need the LhA program (shareware,\n"
                    "not part of Oasis or Workbench). Copy it to C:LhA, or\n"
                    "name it in Oasis.prefs, as in:  LHA \"Work:Tools/LhA\"\n\n"
                    "Looking inside archives works without it.", sizeof(body));
    req_choose(win, &topaz8, "LhA", body, "OK");
    flushinput(NULL);
    return FALSE;
}

static BOOL lhapicked(struct Pane *p, int k)
{
    return p->nsel ? p->e[k].sel : k == p->cur;
}

static int etype(struct Pane *p, struct FEntry *e)
{
    if (e->kind != EK_FILE)
        return FT_UNKNOWN;
    if (!e->spare) {
        static char full[PATHMAX];
        pane_entrydir(p, e, full);
        path_add(full, e->name);
        e->spare = (UBYTE)(ft_file(full) + 1);
    }
    return e->spare - 1;
}

static void lhaextract(struct Pane *s, struct Pane *d, int n)
{
    static char prog[PATHMAX], full[PATHMAX], line[96];
    char (*cmds)[LHA_CMDMAX], *ptr[MAXPACK];
    int k, c = 0;
    if (!needlha(prog))
        return;
    if (n > MAXPACK)
        n = MAXPACK;
    if (!(cmds = AllocMem(n * LHA_CMDMAX, MEMF_PUBLIC))) {
        message("Not enough memory.");
        return;
    }
    for (k = 0; k < s->count && c < n; k++)
        if (lhapicked(s, k)) {
            pane_entrydir(s, &s->e[k], full);
            path_add(full, s->e[k].name);
            if (lha_command(prefs.lhaextract[0] ? prefs.lhaextract : LHA_EXTRACT, prog, full, d->path,
                            NULL, 0, cmds[c], LHA_CMDMAX) == 0) {
                ptr[c] = cmds[c];
                c++;
            }
        }
    if (runcmds(ptr, c, "LhA", FALSE, d->path) >= 0) {
        fmt(line, "Extracted %ld archive%s.", (LONG)c, c == 1 ? "" : "s");
        message(line);
    }
    FreeMem(cmds, n * LHA_CMDMAX);
}

static void lhapack(struct Pane *s, struct Pane *d, int n)
{
    static char prog[PATHMAX], archive[PATHMAX], name[FNAME + 4], prompt[80], where[40], probe[PATHMAX];
    char (*cmds)[LHA_CMDMAX] = NULL, **names = NULL, *pool = NULL, *ptr[MAXPACK];
    int k, m = 0, c, i, cap = n * 2;            /* room for an icon with every entry */
    BPTR l;
    /* a name for the archive: the entry's, or the drawer's */
    for (k = 0; k < s->count && !lhapicked(s, k); k++)
        ;
    if (n == 1)
        scopy(name, s->e[k].name, 27);
    else {
        const char *t = s->path, *q;
        for (q = s->path; *q; q++)
            if (*q == ':' || *q == '/')
                t = q + 1;
        scopy(name, *t ? t : "Archive", 27);
    }
    if (!ends_with_nocase(name, ".lha"))
        scat(name, ".lha", sizeof(name));
    tail(where, d->path, 36);
    fmt(prompt, "Pack into this archive, in %s:", where);
    if (!req_string(win, &topaz8, "LhA", prompt, name, 31) || !name[0]) {
        flushinput(NULL);
        return;
    }
    flushinput(NULL);
    if (!needlha(prog))
        return;
    scopy(archive, d->path, PATHMAX);
    path_add(archive, name);
    if (!(names = AllocMem(cap * sizeof(char *), MEMF_PUBLIC)) || !(pool = AllocMem(cap * PATHMAX, MEMF_PUBLIC)) ||
        !(cmds = AllocMem(MAXPACK * LHA_CMDMAX, MEMF_PUBLIC))) {
        message("Not enough memory.");
        goto out;
    }
    /* the names, relative to the source drawer (the commands run there); icons go along */
    for (k = 0; k < s->count && m < cap - 1; k++) {
        const char *rel;
        if (!lhapicked(s, k))
            continue;
        rel = pane_reldir(s, &s->e[k]);
        names[m] = pool + m * PATHMAX;
        scopy(names[m], rel, PATHMAX);
        path_add(names[m], s->e[k].name);
        m++;
        if (ends_with_nocase(s->e[k].name, ".info"))
            continue;
        scopy(probe, s->e[k].name, PATHMAX);
        scat(probe, ".info", PATHMAX);
        for (i = 0; i < s->count; i++)                /* is its icon picked itself? */
            if (lhapicked(s, i) && s->e[i].aux == s->e[k].aux && !nocase_cmp(s->e[i].name, probe))
                break;
        scopy(probe, s->path, PATHMAX);
        path_add(probe, names[m - 1]);
        scat(probe, ".info", PATHMAX);
        if (i == s->count) {
            dos_quiet(TRUE);
            l = Lock((STRPTR)probe, ACCESS_READ);
            dos_quiet(FALSE);
            if (l) {
                UnLock(l);
                names[m] = pool + m * PATHMAX;
                scopy(names[m], names[m - 1], PATHMAX);
                scat(names[m], ".info", PATHMAX);
                m++;
            }
        }
    }
    c = lha_packcmds(prefs.lhapack[0] ? prefs.lhapack : LHA_PACK, prog, archive, names, m, cmds,
                     MAXPACK, 230);
    if (c < 0) {
        message("Too many entries for one archive at a time: select fewer.");
        goto out;
    }
    for (i = 0; i < c; i++)
        ptr[i] = cmds[i];
    if (runcmds(ptr, c, "LhA", FALSE, s->path) >= 0) {
        for (k = 0; k < d->count; k++)                 /* show the new archive */
            if (!nocase_cmp(d->e[k].name, name)) {
                d->cur = k;
                showsel(d);
                drawpane(1 - active);
                break;
            }
        fmt(prompt, "Packed %ld entr%s into %s.", (LONG)m, m == 1 ? "y" : "ies", name);
        message(prompt);
    }
out:
    if (cmds)
        FreeMem(cmds, MAXPACK * LHA_CMDMAX);
    if (pool)
        FreeMem(pool, cap * PATHMAX);
    if (names)
        FreeMem(names, cap * sizeof(char *));
}

static void lhaop(void)
{
    struct Pane *s = &pane[active], *d = &pane[1 - active];
    static char body[200], where[40];
    int k, n = 0, narch = 0, first = -1, a;
    BOOL destok = !(d->devlist || d->found || d->err[0]);
    if (s->devlist || (s->err[0] && !s->found)) {
        message("Open a drawer first.");
        return;
    }
    for (k = 0; k < s->count; k++)
        if (lhapicked(s, k)) {
            if (first < 0)
                first = k;
            n++;
            if (etype(s, &s->e[k]) == FT_LHA)
                narch++;
        }
    if (!n) {
        message("Select something first (click it, or Space).");
        return;
    }
    tail(where, destok ? d->path : "(no drawer in the other pane)", 36);
    if (narch == n) {                                  /* archives: extract them, or look inside */
        if (n == 1)
            fmt(body, "\"%s\" is an LhA archive.\n\nExtract it into %s,\nor list what is in it?",
                s->e[first].name, where);
        else
            fmt(body, "Extract %ld archives into\n%s ?", (LONG)n, where);
        a = req_choose(win, &topaz8, "LhA", body, n == 1 ? "Extract|List|Cancel" : "Extract|Cancel");
        flushinput(NULL);
        if (n == 1 && a == 1) {
            static char full[PATHMAX];
            pane_entrydir(s, &s->e[first], full);
            path_add(full, s->e[first].name);
            lhaview(full, s->e[first].name);
        } else if (a == 0) {
            if (!destok)
                message("Open the drawer to extract into in the other pane first.");
            else
                lhaextract(s, d, n);
        }
    } else if (!destok)
        message("Open the drawer for the new archive in the other pane first.");
    else
        lhapack(s, d, n);
}

/* S:Oasis.prefs, or else Oasis.prefs in the current directory (Oasis's own drawer when it is
   started from Workbench): a copy of Oasis can carry its prefs with it */
static const char *prefsfile;

static BOOL loadprefs(void)
{
    static const char *const where[] = { PREFS_FILE, PREFS_LOCAL };
    int i;
    for (i = 0; i < 2; i++)
        if (prefs_load(where[i], &prefs)) {
            prefsfile = where[i];
            return TRUE;
        }
    prefsfile = PREFS_FILE;
    return FALSE;
}

static void reloadprefs(void)
{
    char line[96];
    BOOL there;
    removegadgets();
    there = loadprefs();
    if (prefs.hassort)
        sortmode = prefs.sort;
    if (prefs.hashide)
        hideinfo = prefs.hideinfo;
    layout();
    addgadgets();
    resized();                          /* clears the window, draws everything in its new place */
    loadpane(0);
    loadpane(1);
    syncmenus();
    if (!there)
        fmt(line, "No %s (or %s here): no custom buttons.", PREFS_FILE, PREFS_LOCAL);
    else if (prefs.nbad)
        fmt(line, "%s: line %ld not understood (%ld buttons).", prefsfile, (LONG)prefs.badline,
            (LONG)prefs.nbuttons);
    else
        fmt(line, "%s read: %ld custom button%s.", prefsfile, (LONG)prefs.nbuttons,
            prefs.nbuttons == 1 ? "" : "s");
    message(line);
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
    } else if (id >= GID_CUSTOM && id < GID_CUSTOM + MAXBUTTONS)
        runbutton(id - GID_CUSTOM);
    else if (id >= GID_BANK && id < GID_BANK + NBANK) {
        int b = id - GID_BANK;
        if (!bank[b].on)
            return;
        if (b <= B_MAKEDIR)
            fileop(b);
        else if (b <= B_SIZE || b == B_SEARCH || b == B_COMPARE)
            toolop(b);
        else if (b == B_LHA) {
            lhaop();
            flushinput(NULL);
            if (job.resized) {
                job.resized = FALSE;
                resized();
            }
        } else if (b == B_VIEW || b == B_HEX) {
            struct Pane *p = &pane[active];
            if (p->devlist || p->cur < 0 || p->cur >= p->count || p->e[p->cur].kind != EK_FILE)
                message("Put the cursor on a file to look at it.");
            else
                viewentry(active, p->cur, b == B_HEX);
        }
        else if (b == B_ALL)
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
    loadprefs();
    if (prefs.hassort)
        sortmode = prefs.sort;
    if (prefs.hashide)
        hideinfo = prefs.hideinfo;
    if (!left)
        left = prefs.hasleft ? prefs.left : "SYS:";
    if (!right)
        right = prefs.hasright ? prefs.right : "";
    initmenus();
    syncmenus();
    layout();
    scanquick();
    addgadgets();
    scopy(pane[0].path, left, PATHMAX);
    scopy(pane[1].path, right, PATHMAX);
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
#ifdef TEST_REQ          /* screenshot builds: open one of the dialogs straight away */
    {
        char nm[32] = "readme.txt";
        Delay(100);
        flushinput(NULL);
        if (job.resized)
            resized();
        if (TEST_REQ == 1)
            u_error(NULL, "Cannot delete", "Work:ot/src/locked.txt", ERROR_DELETE_PROTECTED);
        else if (TEST_REQ == 2)
            req_string(win, &topaz8, "Rename", "New name for \"readme.txt\":", nm, 31);
        else if (TEST_REQ == 4) {
            BOOL on[8] = { 0, 1, 0, 0, 1, 1, 0, 1 };
            req_protect(win, &topaz8, "Protect", "Protection of \"tool\":", on);
        } else if (TEST_REQ == 5) {      /* search results in the left pane */
            scopy(pane[0].pattern, "#?.txt", sizeof(pane[0].pattern));
            pane[0].found = TRUE;
            loadpane(0);
        } else if (TEST_REQ == 6)        /* compare the two panes */
            compareop();
        else if (TEST_REQ >= 7 && TEST_REQ <= 13) {   /* the viewers */
            static const char *const f[] = { "Work:vt/readme.txt", "Work:vt/readme.txt",
                "Work:vt/lores32.iff", "Work:vt/ham.iff", "Work:vt/ehb.iff", "Work:vt/hires16.iff",
                "Work:vt/masked.iff" };
            if (TEST_REQ <= 8)
                view_file(win, &topaz8, f[TEST_REQ - 7], TEST_REQ == 8);
            else
                ilbm_show(f[TEST_REQ - 7]);
        } else if (TEST_REQ >= 15 && TEST_REQ <= 18) {    /* LhA: list, dialog, no LhA, pack */
            static char prog[PATHMAX];
            int k;
            if (TEST_REQ == 15)
                lhaview("Work:la/h1.lha", "h1.lha");
            else if (TEST_REQ == 17)
                needlha(prog);
            else {
                for (k = 0; k < pane[0].count; k++)
                    if (!nocase_cmp(pane[0].e[k].name, TEST_REQ == 16 ? "h1.lha" : "readme.txt") ||
                        (TEST_REQ == 18 && !nocase_cmp(pane[0].e[k].name, "tool")))
                        pane_select(&pane[0], k, TRUE);
                drawpane(0);
                if (TEST_REQ == 16)
                    pane_selectall(&pane[0], FALSE), pane[0].cur = 0;
                for (k = 0; TEST_REQ == 16 && k < pane[0].count; k++)
                    if (!nocase_cmp(pane[0].e[k].name, "h1.lha"))
                        movecursor(0, k);
                lhaop();
            }
        } else if (TEST_REQ == 14) {     /* play a sound from the window, as a double-click does */
            int k;
            for (k = 0; k < pane[0].count; k++)
                if (!nocase_cmp(pane[0].e[k].name, "tone.8svx")) {
                    movecursor(0, k);
                    viewentry(0, k, FALSE);
                }
        } else
            req_choose(win, &topaz8, "Delete", "Delete 4 files and 1 drawer from\nWork:ot/src ?\n\n"
                       "Drawers are deleted with everything in them.", "Delete|Cancel");
    }
#endif
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
