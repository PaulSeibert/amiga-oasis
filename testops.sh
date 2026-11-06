#!/bin/bash
# Oasis file-operation test. Builds Oasis, makes a fixture tree in Work:ot/src
# ($OASIS_SHARE/ot/src), and a boot disk (build/ot.adf) whose startup-sequence runs
# "oasis CMD" copy/move/rename/delete steps across Work: (WinUAE directory filesystem),
# RAM: and the boot floppy itself (OFS), writing results to Work:ot-*.txt.
# Then start WinUAE with $OASIS_SHARE/../play/oasis-ot-ks13.uae (or -ks31) and run
# tools/checkops.py when Work:ot-done.txt appears.
#
# The boot disk borrows FailAt, Echo, FileNote, Protect and L:Ram-Handler from the Workbench 1.3 disk
# ($WB13_ADF): it is a local test disk only and must never be distributed.
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${OASIS_SHARE:-/mnt/j/amiga/share}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
./build.sh >/dev/null

# ---- fixture (fixed dates, so kept and lost dates are easy to tell apart) ----
rm -rf "$SHARE/ot" "$SHARE"/ot-*.txt
python3 - "$SHARE/ot/src" <<'PY'
import os, sys, random
root = sys.argv[1]
def put(rel, data):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(data)
r = random.Random(1985)
put('readme.txt', b'Oasis test fixture.\n' * 100)
put('big.bin', bytes(r.getrandbits(8) for _ in range(300 * 1024 + 123)))
put('empty.dat', b'')
put('tool', b'\x00\x00\x03\xf3' + bytes(r.getrandbits(8) for _ in range(5000)))
put('tool.info', open('build/oasis.info', 'rb').read())
put('locked.txt', b'This file is protected from deletion.\n')
put('sub/a.txt', b'level one\n' * 50)
put('sub/deeper/b.txt', b'level two\n' * 20)
put('sub/deeper/c.bin', bytes(range(256)) * 40)
put('sub.info', open('build/oasis.info', 'rb').read())
PY
find "$SHARE/ot/src" -exec touch -h -d '2001-02-03 04:05:06' {} +

# ---- boot disk ----
cat > build/ot-startup <<'EOS'
failat 21
oasis >Work:ot-ram0.txt CMD TREE RAM:
oasis >Work:ot-mem1.txt CMD MEM
filenote Work:ot/src/readme.txt "Read me first"
filenote Work:ot/src/sub "A drawer comment"
protect Work:ot/src/locked.txt rw
protect Work:ot/src/tool rwed
oasis >Work:ot-t-src.txt CMD TREE Work:ot/src
oasis >Work:ot-s01.txt CMD COPY Work:ot/src TO RAM:
oasis >Work:ot-t-ram.txt CMD TREE RAM:src
oasis >Work:ot-s02.txt CMD MAKEDIR Work:ot/back
oasis >Work:ot-s03.txt CMD COPY RAM:src TO Work:ot/back
oasis >Work:ot-t-back.txt CMD TREE Work:ot/back/src
oasis >Work:ot-s04.txt CMD COPY Work:ot/src TO DF0:
oasis >Work:ot-t-df0.txt CMD TREE DF0:src
oasis >Work:ot-s05.txt CMD MAKEDIR Work:ot/backdf
oasis >Work:ot-s06.txt CMD COPY DF0:src TO Work:ot/backdf
oasis >Work:ot-t-backdf.txt CMD TREE Work:ot/backdf/src
oasis >Work:ot-s07.txt CMD MAKEDIR RAM:icons
oasis >Work:ot-s08.txt CMD COPY Work:ot/src/tool TO RAM:icons
oasis >Work:ot-t-icons.txt CMD TREE RAM:icons
oasis >Work:ot-s09.txt CMD COPY Work:ot/src TO Work:ot/src/sub
oasis >Work:ot-s10.txt CMD COPY Work:ot/src/readme.txt TO Work:ot/back/src
oasis >Work:ot-s11.txt CMD MAKEDIR Work:ot/moved
oasis >Work:ot-s12.txt CMD MOVE Work:ot/back/src TO Work:ot/moved
oasis >Work:ot-t-moved.txt CMD TREE Work:ot/moved
oasis >Work:ot-t-backafter.txt CMD TREE Work:ot/back
oasis >Work:ot-s13.txt CMD MAKEDIR Work:ot/moved2
oasis >Work:ot-s14.txt CMD MOVE RAM:src TO Work:ot/moved2
oasis >Work:ot-t-moved2.txt CMD TREE Work:ot/moved2
oasis >Work:ot-t-ramafter.txt CMD TREE RAM:
oasis >Work:ot-s15.txt CMD RENAME RAM:icons/tool hammer
oasis >Work:ot-t-renamed.txt CMD TREE RAM:icons
oasis >Work:ot-s16.txt CMD DELETE DF0:src
oasis >Work:ot-t-df0b.txt CMD TREE DF0:src
oasis >Work:ot-s17.txt CMD DELETE DF0:src FORCE
oasis >Work:ot-t-df0c.txt CMD LIST DF0:
oasis >Work:ot-s18.txt CMD DELETE Work:ot/moved Work:ot/back RAM:icons RAM:src FORCE
oasis >Work:ot-t-final.txt CMD TREE Work:ot
oasis >Work:ot-t-ramfinal.txt CMD TREE RAM:
oasis >Work:ot-mem2.txt CMD MEM
echo >Work:ot-done.txt "done"
EOS
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo FileNote Protect; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  xdftool \"\$wb\" read l/Ram-Handler build/c/Ram-Handler     # 1.3 loads RAM: from L:
  rm -f build/ot.adf
  xdftool build/ot.adf format OasisTest ofs + boot install + write build/oasis oasis \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo \
    + write build/c/FileNote c/FileNote + write build/c/Protect c/Protect \
    + makedir l + write build/c/Ram-Handler l/Ram-Handler \
    + makedir s + write build/ot-startup s/startup-sequence >/dev/null"
PLAY=${OASIS_PLAY:-/mnt/j/amiga/play}
cp build/ot.adf "$PLAY/oasis-ot.adf"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\oasis-ot.adf#' \
      -e 's#^config_description=.*#config_description=Oasis file-operation test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/oasis-ot-$k.uae"
done
echo "ready: $PLAY/oasis-ot-ks13.uae, $PLAY/oasis-ot-ks31.uae"
