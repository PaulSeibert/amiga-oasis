#!/bin/bash
# Build Oasis (AmigaOS 1.3+, 68000) into build/:
#   build/oasis       the program          build/oasis.info  its Workbench icon
#   build/oasis.adf   bootable test disk: writes Work:oasis-devs.txt and Work:oasis-list.txt
#                     (command mode), then opens the window on Work: and the device list
# Needs Docker (amigadev/crosstools:m68k-amigaos: bebbo GCC + xdftool) and Python 3.
# If $OASIS_SHARE (default /mnt/j/amiga/share, the emulator's Work: folder) exists,
# the results are copied there too.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
python3 tools/mkicon.py art/icon.txt build/oasis.info 8192
python3 tools/mkimage.py art/logo.txt logo build/logo.c
SRC="src/main.c src/ui.c src/pane.c src/dos13.c src/util.c src/cmd.c src/ops.c src/req.c src/tools.c src/match.c src/prefs.c src/about.c build/logo.c"
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  m68k-amigaos-gcc -mcrt=nix13 -m68000 -Os -Wall -Wextra -Wno-unused-parameter -s -Isrc \
    ${OASIS_CFLAGS:-} -o build/oasis $SRC
  # AmigaDOS 1.3: redirection must come right after the command name
  printf 'oasis >Work:oasis-devs.txt CMD DEVS\noasis >Work:oasis-list.txt CMD LIST Work:\noasis Work:\n' \
    > build/startup-sequence
  rm -f build/oasis.adf
  xdftool build/oasis.adf format Oasis ofs + boot install + write build/oasis oasis \
    + write build/oasis.info oasis.info + makedir s + write build/startup-sequence s/startup-sequence >/dev/null"
SHARE=${OASIS_SHARE:-/mnt/j/amiga/share}
[ -d "$SHARE" ] && cp build/oasis build/oasis.info build/oasis.adf "$SHARE/"
# the example prefs next to Oasis in Work: (never over one that has been edited)
if [ -d "$SHARE" ] && [ ! -e "$SHARE/Oasis.prefs" ]; then cp examples/Oasis.prefs "$SHARE/"; fi
ls -l build/oasis build/oasis.info build/oasis.adf
