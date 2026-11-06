/* Oasis - the text and hex viewer: a window of its own on the Workbench screen. */
#ifndef VIEW_H
#define VIEW_H
#include <exec/types.h>
#include <intuition/intuition.h>

/* show path as text (or hex); returns 0, or a DOS error if the file couldn't be read.
   Tab switches between text and hex, Esc closes. */
LONG view_file(struct Window *parent, struct TextAttr *ta, const char *path, BOOL hex);

/* show text that is already in memory (it stays the caller's) under the title name */
LONG view_text(struct Window *parent, struct TextAttr *ta, const char *name, char *text, LONG len);

/* command mode: how the text viewer splits the file into lines of cols characters
   (line count, and the first lines as the viewer shows them) */
LONG view_lines(const char *path, int cols, int show, void (*out)(const char *));

#endif
