/* Oasis 1.0: a two-pane file manager for AmigaOS 1.3 (V33) and later.
 * Written by Paul Seibert. (c) 2026 Desert Palms Software. Freeware.
 *   oasis [left [right]]      open the window (also from Workbench)
 *   oasis CMD ...             command mode, without the window (see cmd.c) */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "cmd.h"
#include "ui.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static const char version[] __attribute__((used)) =
    "$VER: Oasis 1.0 (6.11.2026) (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.";

int main(int argc, char **argv)
{
    BOOL running = TRUE;
    int rc = 20;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 33);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 33);
    if (!IntuitionBase || !GfxBase)
        goto out;

    if (argc > 1 && !nocase_cmp(argv[1], "CMD")) {
        rc = cmd_run(argc, argv);
        goto out;
    }
    if (argc > 1 && (argv[1][0] == '?' || !nocase_cmp(argv[1], "HELP"))) {
        static const char usage[] =
            "Oasis 1.0 - (c) 2026 Desert Palms Software, by Paul Seibert. Freeware.\n"
            "Usage: oasis [leftdrawer [rightdrawer]]\n"
            "       oasis CMD ...      (oasis CMD alone lists the commands)\n";
        Write(Output(), (APTR)usage, sizeof(usage) - 1);
        rc = 0;
        goto out;
    }
    /* no drawers given: from S:Oasis.prefs, or SYS: on the left and the device list on the right */
    if (!ui_open(argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL))
        goto out;
    while (running) {
        ULONG sigs = Wait(ui_sigmask() | SIGBREAKF_CTRL_C);
        if (sigs & ui_sigmask())
            running = ui_handle();
        if (sigs & SIGBREAKF_CTRL_C)
            running = FALSE;
    }
    rc = 0;

out:
    ui_close();
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    return rc;
}
