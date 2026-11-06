/* Oasis - IFF ILBM pictures: shown on a screen of their own (lores/hires, interlace,
 * up to 32 colours, Extra Halfbrite and HAM, as the original chipset can show them). */
#ifndef ILBM_H
#define ILBM_H
#include <exec/types.h>
#include <intuition/intuition.h>

struct IlbmInfo {
    UWORD w, h;
    UBYTE planes, masking, compression;
    ULONG camg;                  /* display mode bits from CAMG (0 if none) */
    BOOL  hascamg;
    int   ncolors;
    UBYTE cmap[3 * 64];
    const UBYTE *body;
    LONG  bodylen;
};

/* NULL, or why the picture can't be read */
const char *ilbm_parse(const UBYTE *buf, LONG len, struct IlbmInfo *ii);
/* decode the BODY into planes (bpr bytes a row, rows of them); extra planes and rows are
   dropped. FALSE if the data ran out early (what was there is still drawn). */
BOOL ilbm_decode(const struct IlbmInfo *ii, UBYTE **planes, int nplanes, int bpr, int rows);
/* the screen mode it will be shown in, and NULL or why it can't be shown on this machine */
const char *ilbm_mode(const struct IlbmInfo *ii, ULONG *modes, int *sw, int *sh, int *depth);
/* load and show; returns NULL, or a message for the user */
const char *ilbm_show(const char *path);

#endif
