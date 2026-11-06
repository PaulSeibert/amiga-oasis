/* Oasis - ILBM viewer. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "ftype.h"
#include "ilbm.h"

extern struct GfxBase *GfxBase;

#define ROWMAX 1024              /* bytes in one plane row: pictures up to 8192 pixels wide */
#define CAMG_HIRES 0x8000
#define CAMG_LACE  0x0004
#define CAMG_HAM   0x0800
#define CAMG_EHB   0x0080

const char *ilbm_parse(const UBYTE *buf, LONG len, struct IlbmInfo *ii)
{
    const UBYTE *d;
    LONG n;
    int i;
    if (len < 12 || be32(buf) != 0x464F524D)                    /* "FORM" */
        return "This is not an IFF file.";
    if (be32(buf + 8) == 0x50424D20)                            /* "PBM " */
        return "PC Deluxe Paint (PBM) pictures can't be shown.";
    if (be32(buf + 8) != 0x494C424D)                            /* "ILBM" */
        return "This IFF file is not an ILBM picture.";
    if (!iff_chunk(buf, len, "BMHD", &d, &n) || n < 20)
        return "The picture has no BMHD header.";
    ii->w = be16(d);
    ii->h = be16(d + 2);
    ii->planes = d[8];
    ii->masking = d[9];
    ii->compression = d[10];
    if (!ii->w || !ii->h || !ii->planes)
        return "The picture is empty.";
    if (ii->compression > 1)
        return "The picture uses a packing method Oasis doesn't know.";
    if (((ii->w + 15) >> 4) * 2 > ROWMAX)
        return "The picture is too wide.";
    ii->ncolors = 0;
    if (iff_chunk(buf, len, "CMAP", &d, &n)) {
        ii->ncolors = n / 3 > 64 ? 64 : n / 3;
        CopyMem((APTR)d, ii->cmap, ii->ncolors * 3);
    }
    if (!ii->ncolors) {                                         /* no palette: greys */
        ii->ncolors = 1 << (ii->planes > 5 ? 5 : ii->planes);
        for (i = 0; i < ii->ncolors; i++)
            ii->cmap[i * 3] = ii->cmap[i * 3 + 1] = ii->cmap[i * 3 + 2] = i * 255 / (ii->ncolors - 1);
    }
    ii->hascamg = iff_chunk(buf, len, "CAMG", &d, &n) && n >= 4;
    ii->camg = ii->hascamg ? be32(d) : 0;
    if (!iff_chunk(buf, len, "BODY", &ii->body, &ii->bodylen))
        return "The picture has no BODY (no image data).";
    return NULL;
}

/* ByteRun1: n >= 0: n+1 bytes as they are; -127..-1: the next byte -n+1 times; -128: nothing */
static const UBYTE *unpack(const UBYTE *s, const UBYTE *end, UBYTE *d, int n)
{
    while (n > 0 && s < end) {
        int c = (BYTE)*s++;
        if (c >= 0) {
            for (c++; c > 0 && s < end; c--, s++)
                if (n > 0)
                    *d++ = *s, n--;
        } else if (c != -128) {
            UBYTE v = s < end ? *s++ : 0;
            for (c = -c + 1; c > 0 && n > 0; c--, n--)
                *d++ = v;
        }
    }
    while (n-- > 0)                        /* ran out: the rest of the row stays empty */
        *d++ = 0;
    return s;
}

BOOL ilbm_decode(const struct IlbmInfo *ii, UBYTE **planes, int nplanes, int bpr, int rows)
{
    static UBYTE row[ROWMAX];
    const UBYTE *s = ii->body, *end = ii->body + ii->bodylen;
    int rb = ((ii->w + 15) >> 4) * 2, n = rb < bpr ? rb : bpr, y, p;
    int all = ii->planes + (ii->masking == 1);      /* a mask plane follows the colour planes */
    for (y = 0; y < ii->h; y++)
        for (p = 0; p < all; p++) {
            if (s >= end)
                return FALSE;
            if (ii->compression)
                s = unpack(s, end, row, rb);
            else {
                LONG k = end - s < rb ? end - s : rb;
                CopyMem((APTR)s, row, k);
                s += rb;
            }
            if (p < nplanes && p < ii->planes && y < rows)
                CopyMem(row, planes[p] + (LONG)y * bpr, n);
        }
    return TRUE;
}

static BOOL ispal(void)
{
    return (GfxBase->DisplayFlags & PAL) != 0;
}

const char *ilbm_mode(const struct IlbmInfo *ii, ULONG *modes, int *sw, int *sh, int *depth)
{
    int lines = ispal() ? 256 : 200, maxw;
    ULONG m = 0;
    if (ii->hascamg)
        m = ii->camg & (CAMG_HIRES | CAMG_LACE | CAMG_HAM | CAMG_EHB);
    else {                                           /* no CAMG: guess from the size */
        if (ii->w > 400)
            m |= CAMG_HIRES;
        if (ii->h > lines + 40)
            m |= CAMG_LACE;
        if (ii->planes == 6)
            m |= CAMG_HAM;
    }
    if (ii->planes == 6 && !(m & (CAMG_HAM | CAMG_EHB)))
        m |= CAMG_EHB;
    if (ii->planes > 6)
        return "Pictures with more than 64 colours need the AGA chipset.";
    if ((m & CAMG_HIRES) && ii->planes > 4)
        return "Hires pictures with more than 16 colours need the AGA chipset.";
    if ((m & (CAMG_HAM | CAMG_EHB)) && ii->planes != 6)
        m &= ~(CAMG_HAM | CAMG_EHB);
    maxw = (m & CAMG_HIRES) ? 736 : 368;
    *sw = ii->w < ((m & CAMG_HIRES) ? 640 : 320) ? ((m & CAMG_HIRES) ? 640 : 320) : ii->w;
    if (*sw > maxw)
        *sw = maxw;
    *sh = ii->h;
    if (*sh > ((m & CAMG_LACE) ? 2 : 1) * (lines + 32))
        *sh = ((m & CAMG_LACE) ? 2 : 1) * (lines + 32);
    *depth = ii->planes;
    *modes = m;
    return NULL;
}

const char *ilbm_show(const char *path)
{
    static struct IlbmInfo ii;
    static UWORD rgb[32];
    static char why[80];
    struct NewScreen ns = { 0, 0, 320, 200, 1, 0, 1, 0, CUSTOMSCREEN, NULL, NULL, NULL, NULL };
    struct NewWindow nw = { 0, 0, 320, 200, 0, 1, MOUSEBUTTONS | VANILLAKEY,
                            BACKDROP | BORDERLESS | ACTIVATE | RMBTRAP | SIMPLE_REFRESH | NOCAREREFRESH,
                            NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0, CUSTOMSCREEN };
    struct Screen *scr;
    struct Window *w;
    UWORD *blank;
    UBYTE *buf;
    LONG len, size, alloc, err;
    const char *msg;
    ULONG modes;
    int sw, sh, depth, i, n;
    BOOL done = FALSE;

    dos_quiet(TRUE);
    buf = ft_load(path, 0x7FFFFFFF, 0, &len, &size, &alloc, &err);
    dos_quiet(FALSE);
    if (!buf) {
        fmt(why, "Cannot read the picture: %s.", dos_errstr(err));
        return why;
    }
    if ((msg = ilbm_parse(buf, len, &ii)) || (msg = ilbm_mode(&ii, &modes, &sw, &sh, &depth))) {
        FreeMem(buf, alloc);
        return msg;
    }
    ns.Width = sw;
    ns.Height = sh;
    ns.Depth = depth;
    ns.ViewModes = (UWORD)modes;
    if (!(scr = OpenScreen(&ns))) {
        FreeMem(buf, alloc);
        return "Not enough chip memory to show this picture.";
    }
    n = ii.ncolors < 32 ? ii.ncolors : 32;
    for (i = 0; i < 32; i++)
        rgb[i] = 0;
    LoadRGB4(&scr->ViewPort, rgb, 1 << (depth > 5 ? 5 : depth));   /* black while it is drawn */
    ShowTitle(scr, FALSE);
    nw.Width = sw;
    nw.Height = sh;
    nw.Screen = scr;
    if (!(w = OpenWindow(&nw))) {
        CloseScreen(scr);
        FreeMem(buf, alloc);
        return "Not enough memory to show this picture.";
    }
    /* no pointer over the picture (its colours would come from the picture): a blank sprite */
    if ((blank = AllocMem(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR)))
        SetPointer(w, blank, 1, 16, 0, 0);
    ilbm_decode(&ii, (UBYTE **)scr->BitMap.Planes, scr->BitMap.Depth, scr->BitMap.BytesPerRow,
                scr->BitMap.Rows);
    FreeMem(buf, alloc);
    for (i = 0; i < n; i++)
        rgb[i] = ((ii.cmap[i * 3] >> 4) << 8) | ((ii.cmap[i * 3 + 1] >> 4) << 4) | (ii.cmap[i * 3 + 2] >> 4);
    LoadRGB4(&scr->ViewPort, rgb, n);
    while (!done) {
        struct IntuiMessage *im;
        WaitPort(w->UserPort);
        while ((im = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            if ((im->Class == MOUSEBUTTONS && (im->Code == SELECTDOWN || im->Code == MENUDOWN)) ||
                im->Class == VANILLAKEY)
                done = TRUE;               /* any key or click: back to Oasis */
            ReplyMsg((struct Message *)im);
        }
    }
    if (blank)
        ClearPointer(w);
    CloseWindow(w);
    if (blank)
        FreeMem(blank, 6 * sizeof(UWORD));
    CloseScreen(scr);
    return NULL;
}
