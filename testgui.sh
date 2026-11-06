#!/bin/bash
# Oasis window test disk for screenshots: testgui.sh [N]
#   N = 0 (default) the window with custom buttons from S:Oasis.prefs, on Work:tt/a and Work:tt/b
#   N = 4 the Protect dialog, 5 search results ("#?.txt" below Work:tt), 6 Compare of the panes
#   N = 7 text viewer, 8 hex viewer, 9-13 pictures (lores 32 colours, HAM, EHB, hires, masked)
#   N = v the window on Work:vt (file types in the info line)
# Needs the Work:tt fixtures from testtools.sh (and Work:vt from testview.sh for 7-13 and v).
# Writes $OASIS_PLAY/oasis-gui.adf and the presets
# oasis-gui-ks13.uae / -ks31.uae. Afterwards ./build.sh restores the normal build.
# Borrows C:Run from the Workbench 1.3 disk ($WB13_ADF): a local test disk, never distribute it.
set -euo pipefail
cd "$(dirname "$0")"
N=${1:-0}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
if [ "$N" = 0 ] || [ "$N" = v ]; then OASIS_CFLAGS= ./build.sh >/dev/null
else OASIS_CFLAGS="-DTEST_REQ=$N" ./build.sh >/dev/null; fi
case "$N" in
  5) start='oasis Work:tt Work:tt/b' ;;
  v|7|8|9|1[0-4]) start='oasis Work:vt Work:tt/b' ;;
  15|16|17) start='oasis Work:la Work:la/out' ;;
  18) start='oasis Work:la/src Work:la/out' ;;
  19|20) start='oasis Work:ad Work:la/out' ;;
  *) start='oasis Work:tt/a Work:tt/b' ;;
esac
printf '%s\n' "$start" > build/gui-startup
cat > build/gui.prefs <<'EOP'
; Oasis prefs for the screenshot disk
SORT NAME
BUTTON "Type" Type %f
BUTTON "List" List %p
BUTTON "Info" Info %p
BUTTON "Run it" QUIET Run %f
BUTTON "Count" DF0:oasis CMD SIZE %s
EOP
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  xdftool \"\$wb\" read c/Run build/c/Run
  rm -f build/gui.adf
  xdftool build/gui.adf format OasisGUI ofs + boot install + write build/oasis oasis \
    + makedir c + write build/c/Run c/Run \
    + makedir s + write build/gui-startup s/startup-sequence + write build/gui.prefs s/Oasis.prefs >/dev/null"
PLAY=${OASIS_PLAY:-/mnt/j/amiga/play}
cp build/gui.adf "$PLAY/oasis-gui.adf"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\oasis-gui.adf#' \
      -e 's#^config_description=.*#config_description=Oasis window test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/oasis-gui-$k.uae"
  if [ "$N" = 19 ] || [ "$N" = 20 ]; then        # ADF dialogs: a second drive with a disk in it
    sed -i 's#^nr_floppies=.*#nr_floppies=2#' "$PLAY/oasis-gui-$k.uae"
    printf '%s\n' 'floppy1=J:\amiga\play\oasis-ad-df1.adf' 'floppy1type=0' >> "$PLAY/oasis-gui-$k.uae"
  fi
done
[ "$N" = 0 ] || ./build.sh >/dev/null       # back to the normal build in Work:
echo "ready: $PLAY/oasis-gui-ks13.uae (TEST_REQ=$N)"
