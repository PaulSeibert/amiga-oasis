#!/bin/bash
# Oasis viewer test (milestone 4). Builds Oasis, writes test pictures, sounds and text to
# Work:vt ($OASIS_SHARE/vt, made by tools/mkiff.py) and a boot disk (build/vt.adf) that runs
# "oasis CMD" TYPE / LINES / ILBM / PLAY on them, writing Work:vt-*.txt. Then start WinUAE with
# $OASIS_PLAY/oasis-vt-ks13.uae (or -ks31) and run tools/checkview.py when Work:vt-done.txt appears.
# Borrows Echo and FailAt from the Workbench 1.3 disk ($WB13_ADF): a local test disk only.
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${OASIS_SHARE:-/mnt/j/amiga/share}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
./build.sh >/dev/null
rm -rf "$SHARE/vt" "$SHARE"/vt-*.txt
python3 tools/mkiff.py "$SHARE/vt" >/dev/null

cat > build/vt-startup <<'EOS'
failat 21
oasis >Work:vt-mem1.txt CMD MEM
oasis >Work:vt-type.txt CMD TYPE Work:vt/lores32.iff Work:vt/tone.8svx Work:vt/readme.txt Work:vt/empty.txt Work:vt/data.bin DF0:oasis Work:oasis.info Work:oasis.adf Work:vt/nothing
oasis >Work:vt-i1.txt CMD ILBM Work:vt/lores32.iff
oasis >Work:vt-i2.txt CMD ILBM Work:vt/hires16.iff
oasis >Work:vt-i3.txt CMD ILBM Work:vt/ham.iff
oasis >Work:vt-i4.txt CMD ILBM Work:vt/ehb.iff
oasis >Work:vt-i5.txt CMD ILBM Work:vt/masked.iff
oasis >Work:vt-i6.txt CMD ILBM Work:vt/aga256.iff
oasis >Work:vt-i7.txt CMD ILBM Work:vt/tone.8svx
oasis >Work:vt-l1.txt CMD LINES Work:vt/readme.txt 40 8
oasis >Work:vt-l2.txt CMD LINES Work:vt/readme.txt 76 3
oasis >Work:vt-l3.txt CMD LINES Work:vt/empty.txt 76 3
oasis >Work:vt-p1.txt CMD PLAY Work:vt/tone.8svx
oasis >Work:vt-p2.txt CMD PLAY Work:vt/tonefib.8svx
oasis >Work:vt-p3.txt CMD PLAY Work:vt/stereo.8svx
oasis >Work:vt-p4.txt CMD PLAY Work:vt/notsound.8svx
oasis >Work:vt-p5.txt CMD PLAY Work:vt/lores32.iff
oasis >Work:vt-mem2.txt CMD MEM
oasis >Work:vt-r1.txt CMD ILBM Work:vt/lores32.iff
oasis >Work:vt-r2.txt CMD PLAY Work:vt/stereo.8svx
oasis >Work:vt-r3.txt CMD PLAY Work:vt/tonefib.8svx
oasis >Work:vt-r4.txt CMD LINES Work:vt/readme.txt 40 8
oasis >Work:vt-r5.txt CMD TYPE Work:vt/lores32.iff Work:vt/nothing
oasis >Work:vt-mem3.txt CMD MEM
echo >Work:vt-done.txt "done"
EOS
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  rm -f build/vt.adf
  xdftool build/vt.adf format OasisTest ofs + boot install + write build/oasis oasis \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo \
    + makedir s + write build/vt-startup s/startup-sequence >/dev/null"
PLAY=${OASIS_PLAY:-/mnt/j/amiga/play}
cp build/vt.adf "$PLAY/oasis-vt.adf"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\oasis-vt.adf#' \
      -e 's#^config_description=.*#config_description=Oasis viewer test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/oasis-vt-$k.uae"
done
echo "ready: $PLAY/oasis-vt-ks13.uae, $PLAY/oasis-vt-ks31.uae"
