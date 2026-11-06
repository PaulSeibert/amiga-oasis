/* Oasis - what a file is, from its first bytes; loading files; finding IFF chunks. */
#ifndef FTYPE_H
#define FTYPE_H
#include <exec/types.h>

enum { FT_UNKNOWN, FT_EMPTY, FT_TEXT, FT_DATA, FT_PROGRAM, FT_OBJECT, FT_LIBRARY,
       FT_ILBM, FT_8SVX, FT_SMUS, FT_ANIM, FT_IFF, FT_LHA, FT_LZX, FT_ZIP, FT_DMS, FT_ADF,
       FT_ICON, FT_FONT, FT_COUNT };

int  ft_detect(const UBYTE *head, LONG n, LONG size);   /* head = the first n bytes */
int  ft_file(const char *path);                         /* FT_... (FT_UNKNOWN if unreadable) */
const char *ft_name(int type);                           /* "ILBM picture" */

/* the whole file (or its first max bytes) in memory: free with FreeMem(buf, *alloc).
   *len = bytes read, *size = the file's size. NULL and *err = DOS error on failure. */
UBYTE *ft_load(const char *path, LONG max, ULONG memtype, LONG *len, LONG *size, LONG *alloc, LONG *err);

/* inside a FORM: the data of the first chunk called id (TRUE if found) */
BOOL iff_chunk(const UBYTE *form, LONG len, const char *id, const UBYTE **data, LONG *size);
ULONG be32(const UBYTE *p);
UWORD be16(const UBYTE *p);

#endif
