#ifndef UI_H
#define UI_H
#include <exec/types.h>

BOOL  ui_open(const char *left, const char *right);   /* start paths; NULL = device list */
void  ui_close(void);
ULONG ui_sigmask(void);
BOOL  ui_handle(void);       /* drain the window port; FALSE = quit */

#endif
