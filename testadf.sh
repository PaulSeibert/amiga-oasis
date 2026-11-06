#!/bin/bash
# Oasis ADF imaging test (milestone 6). Builds Oasis, makes disk images in Work:ad
# ($OASIS_SHARE/ad) and a boot disk (build/ad.adf) that runs "oasis CMD" ADFINFO / ADFWRITE /
# ADFREAD against the emulator's other drives, writing Work:ad-*.txt:
#   DF1: a writable disk (J:\amiga\play\oasis-ad-df1.adf, starts as "OldDisk")
#   DF2: a write-protected disk (oasis-ad-df2.adf, read-only on the PC: WinUAE protects it)
#   DF3: a drive with no disk in it
# Then start WinUAE with $OASIS_PLAY/oasis-ad-ks13.uae (or -ks31) and run tools/checkadf.py
# when Work:ad-done.txt appears. Borrows Echo, FailAt and Wait from the Workbench 1.3 disk.
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${OASIS_SHARE:-/mnt/j/amiga/share}
PLAY=${OASIS_PLAY:-/mnt/j/amiga/play}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
IMG=${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}
./build.sh >/dev/null
rm -rf "$SHARE/ad" "$SHARE"/ad-*.txt
mkdir -p "$SHARE/ad" build/ad
python3 - build/ad <<'PY'
import os, sys, random
d = sys.argv[1]
r = random.Random(1987)
open(os.path.join(d, 'readme'), 'wb').write(b'This disk was written by Oasis.\n' * 40)
open(os.path.join(d, 'big.bin'), 'wb').write(bytes(r.getrandbits(8) for _ in range(150000)))
open(os.path.join(d, 'a.txt'), 'wb').write(b'in a drawer\n' * 10)
open(os.path.join(d, 'old.txt'), 'wb').write(b'the old disk\n')
open(os.path.join(d, 'locked.txt'), 'wb').write(b'a write-protected disk\n')
PY
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src "$IMG" sh -euc "
  cd build/ad
  rm -f test.adf old.adf prot.adf
  xdftool test.adf format Imaged ofs + write readme readme + write big.bin big.bin + makedir sub + write a.txt sub/a.txt >/dev/null
  xdftool old.adf format OldDisk ofs + write old.txt old.txt >/dev/null
  xdftool prot.adf format Locked ffs + write locked.txt locked.txt >/dev/null
  cd /src
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo Wait; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  cat > build/ad-startup <<'EOS'
failat 21
oasis >Work:ad-mem1.txt CMD MEM
oasis >Work:ad-i0.txt CMD ADFINFO
oasis >Work:ad-i1.txt CMD ADFINFO DF1:
oasis >Work:ad-i2.txt CMD ADFINFO DF2:
oasis >Work:ad-i3.txt CMD ADFINFO DF3:
oasis >Work:ad-l1.txt CMD LIST DF1:
oasis >Work:ad-w1.txt CMD ADFWRITE Work:ad/test.adf DF1: VERIFY
wait 3
oasis >Work:ad-l2.txt CMD LIST DF1:
oasis >Work:ad-i4.txt CMD ADFINFO DF1:
oasis >Work:ad-r1.txt CMD ADFREAD DF1: Work:ad/back.adf
oasis >Work:ad-r2.txt CMD ADFREAD DF2: Work:ad/prot-back.adf
oasis >Work:ad-w2.txt CMD ADFWRITE Work:ad/test.adf DF2:
oasis >Work:ad-w3.txt CMD ADFWRITE Work:ad/test.adf DF3:
oasis >Work:ad-r3.txt CMD ADFREAD DF3: Work:ad/none.adf
oasis >Work:ad-w4.txt CMD ADFWRITE Work:ad/small.bin DF1:
oasis >Work:ad-r4.txt CMD ADFREAD DF1: DF1:self.adf
oasis >Work:ad-w5.txt CMD ADFWRITE Work:ad/old.adf DF1:
wait 3
oasis >Work:ad-l3.txt CMD LIST DF1:
oasis >Work:ad-r5.txt CMD ADFREAD DF1: Work:ad/back2.adf
oasis >Work:ad-t1.txt CMD TYPE Work:ad/back.adf Work:ad/small.bin
oasis >Work:ad-mem2.txt CMD MEM
oasis >Work:ad-w6.txt CMD ADFWRITE Work:ad/test.adf DF1: VERIFY
wait 3
oasis >Work:ad-r6.txt CMD ADFREAD DF1: Work:ad/back3.adf
oasis >Work:ad-w7.txt CMD ADFWRITE Work:ad/old.adf DF1:
wait 3
oasis >Work:ad-r7.txt CMD ADFREAD DF2: Work:ad/prot-back2.adf
oasis >Work:ad-i5.txt CMD ADFINFO DF1:
oasis >Work:ad-mem3.txt CMD MEM
echo >Work:ad-done.txt \"done\"
EOS
  rm -f build/ad.adf
  xdftool build/ad.adf format OasisTest ofs + boot install + write build/oasis oasis \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo + write build/c/Wait c/Wait \
    + makedir s + write build/ad-startup s/startup-sequence >/dev/null"
cp build/ad/test.adf build/ad/old.adf build/ad/prot.adf "$SHARE/ad/"
head -c 100000 build/ad/test.adf > "$SHARE/ad/small.bin"
# the drives' disks: DF1 starts as the old disk; DF2 is read-only on the PC (write-protected)
cp build/ad/old.adf "$PLAY/oasis-ad-df1.adf"
chmod u+w "$PLAY/oasis-ad-df2.adf" 2>/dev/null || true
cp build/ad/prot.adf "$PLAY/oasis-ad-df2.adf"
chmod a-w "$PLAY/oasis-ad-df2.adf"
cp build/ad.adf "$PLAY/oasis-ad.adf"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\oasis-ad.adf#' \
      -e 's#^nr_floppies=.*#nr_floppies=4#' \
      -e 's#^config_description=.*#config_description=Oasis ADF test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/oasis-ad-$k.uae"
  printf '%s\n' 'floppy1=J:\amiga\play\oasis-ad-df1.adf' 'floppy1type=0' \
    'floppy2=J:\amiga\play\oasis-ad-df2.adf' 'floppy2type=0' 'floppy3=' 'floppy3type=0' >> "$PLAY/oasis-ad-$k.uae"
done
echo "ready: $PLAY/oasis-ad-ks13.uae, $PLAY/oasis-ad-ks31.uae"
