/* Oasis - file types. Written by Paul Seibert, (c) 2026 Desert Palms Software. */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "ftype.h"

#define HEAD 512

static const char *const names[FT_COUNT] = {
    "", "Empty", "Text", "Data", "Program", "Object code", "Link library",
    "ILBM picture", "8SVX sound", "SMUS music", "ANIM animation", "IFF file", "LhA archive",
    "LZX archive", "Zip archive", "DMS disk image", "ADF disk image", "Icon", "Font"
};

ULONG be32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

UWORD be16(const UBYTE *p)
{
    return (UWORD)((p[0] << 8) | p[1]);
}

static BOOL is4(const UBYTE *p, const char *id)
{
    return p[0] == id[0] && p[1] == id[1] && p[2] == id[2] && p[3] == id[3];
}

const char *ft_name(int type)
{
    return type > 0 && type < FT_COUNT ? names[type] : "";
}

/* text: no NULs and few control characters other than tab, LF, CR, FF, ESC (ANSI colours) */
static BOOL looks_text(const UBYTE *p, LONG n)
{
    LONG i, odd = 0;
    for (i = 0; i < n; i++) {
        UBYTE c = p[i];
        if (c == 0)
            return FALSE;
        if (c < 32 && c != 9 && c != 10 && c != 13 && c != 12 && c != 27)
            odd++;
        else if (c >= 0x80 && c < 0xA0 && c != 0x9B)       /* 0x9B: the Amiga's CSI */
            odd++;
    }
    return odd * 50 <= n;
}

int ft_detect(const UBYTE *h, LONG n, LONG size)
{
    if (size == 0 || n == 0)
        return FT_EMPTY;
    if (n >= 4) {
        ULONG w = be32(h);
        if (w == 0x3F3)
            return FT_PROGRAM;
        if (w == 0x3E7)
            return FT_OBJECT;
        if (w == 0x3FA)
            return FT_LIBRARY;
        if (n >= 12 && (is4(h, "FORM") || is4(h, "CAT ") || is4(h, "LIST"))) {
            if (is4(h + 8, "ILBM") || is4(h + 8, "PBM "))
                return FT_ILBM;
            if (is4(h + 8, "8SVX"))
                return FT_8SVX;
            if (is4(h + 8, "SMUS"))
                return FT_SMUS;
            if (is4(h + 8, "ANIM"))
                return FT_ANIM;
            return FT_IFF;
        }
        if (is4(h, "DMS!"))
            return FT_DMS;
        if (is4(h, "PK\003\004"))
            return FT_ZIP;
        if (h[0] == 'L' && h[1] == 'Z' && h[2] == 'X')
            return FT_LZX;
        if (h[0] == 0xE3 && h[1] == 0x10)
            return FT_ICON;
    }
    if (n >= 7 && h[2] == '-' && h[3] == 'l' && (h[4] == 'h' || h[4] == 'z') && h[6] == '-')
        return FT_LHA;
    if (size == 901120 && n >= 3 && h[0] == 'D' && h[1] == 'O' && h[2] == 'S')
        return FT_ADF;
    if (n >= 2 && h[0] == 0x0F && h[1] == 0x00)        /* FontContents header (FCH_ID) */
        return FT_FONT;
    return looks_text(h, n) ? FT_TEXT : FT_DATA;
}

int ft_file(const char *path)
{
    UBYTE *h;
    BPTR fh;
    LONG n = 0, size;
    int t = FT_UNKNOWN;
    if (!(h = AllocMem(HEAD, MEMF_PUBLIC)))
        return FT_UNKNOWN;
    dos_quiet(TRUE);
    if ((fh = Open((STRPTR)path, MODE_OLDFILE))) {
        n = Read(fh, h, HEAD);
        Seek(fh, 0, OFFSET_END);
        size = Seek(fh, 0, OFFSET_BEGINNING);
        if (n >= 0)
            t = ft_detect(h, n, size);
        Close(fh);
    }
    dos_quiet(FALSE);
    FreeMem(h, HEAD);
    return t;
}

UBYTE *ft_load(const char *path, LONG max, ULONG memtype, LONG *len, LONG *size, LONG *alloc, LONG *err)
{
    BPTR fh;
    UBYTE *buf;
    LONG want;
    *err = 0;
    if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) {
        *err = IoErr();
        return NULL;
    }
    Seek(fh, 0, OFFSET_END);
    *size = Seek(fh, 0, OFFSET_BEGINNING);
    want = *size < max ? *size : max;
    *alloc = want + 2;                           /* room for a closing NUL; never 0 */
    if (!(buf = AllocMem(*alloc, memtype | MEMF_PUBLIC))) {
        Close(fh);
        *err = ERROR_NO_FREE_STORE;
        return NULL;
    }
    *len = Read(fh, buf, want);
    if (*len < 0) {
        *err = IoErr();
        FreeMem(buf, *alloc);
        Close(fh);
        return NULL;
    }
    buf[*len] = 0;
    Close(fh);
    return buf;
}

BOOL iff_chunk(const UBYTE *form, LONG len, const char *id, const UBYTE **data, LONG *size)
{
    LONG pos = 12, end;
    if (len < 12 || !is4(form, "FORM"))
        return FALSE;
    end = 8 + (LONG)be32(form + 4);
    if (end > len)
        end = len;                               /* a cut-off file: use what is there */
    while (pos + 8 <= end) {
        LONG n = (LONG)be32(form + pos + 4);
        if (n < 0)
            return FALSE;
        if (is4(form + pos, id)) {
            *data = form + pos + 8;
            *size = pos + 8 + n <= end ? n : end - pos - 8;
            return TRUE;
        }
        pos += 8 + n + (n & 1);
    }
    return FALSE;
}
