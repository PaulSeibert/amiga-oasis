#!/bin/bash
# Oasis LhA test (milestone 5). Builds Oasis and the LhA stand-in (tools/lhastub.c), makes
# fixtures and real archives in Work:la ($OASIS_SHARE/la, archives by LHa for UNIX with header
# levels 0, 1 and 2, packed and stored), and a boot disk (build/la.adf) that runs "oasis CMD"
# LHALIST / LHAX / LHAA, writing Work:la-*.txt. Then start WinUAE with
# $OASIS_PLAY/oasis-la-ks13.uae (or -ks31) and run tools/checklha.py when Work:la-done.txt appears.
# Borrows Echo, FailAt, Rename and Run from the Workbench 1.3 disk ($WB13_ADF): local only.
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${OASIS_SHARE:-/mnt/j/amiga/share}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
IMG=${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}
./build.sh >/dev/null
rm -rf "$SHARE/la" "$SHARE"/la-*.txt
python3 - "$SHARE/la/src" <<'PY'
import os, sys, random
root = sys.argv[1]
r = random.Random(7)
def put(rel, data):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(data)
put('readme.txt', b'LhA test fixture for Oasis.\n' * 200)
put('with space.txt', b'a name with a space\n')
put('tool', bytes(r.getrandbits(8) for _ in range(3000)))
put('tool.info', b'\xe3\x10' + bytes(200))
put('sub/a.txt', b'level one\n' * 300)
put('sub/deeper/b.bin', bytes(range(256)) * 20)
for i in range(1, 31):
    put(f'many/file-number-{i:02}.txt', f'file {i}\n'.encode())
PY
mkdir -p "$SHARE/la/out"
docker run --rm -u "$(id -u):$(id -g)" -v "$SHARE/la:/la" -w /la/src "$IMG" sh -euc '
  files="readme.txt tool tool.info sub/a.txt sub/deeper/b.bin"
  lha -a0q ../h0.lha $files
  lha -a1q ../h1.lha $files
  lha -a2q ../h2.lha $files
  lha -azq ../stored.lha readme.txt tool
  lha -a2q ../dirs.lha sub'
for i in $(seq -w 1 20); do echo "many/file-number-$i.txt"; done > "$SHARE/la/list.txt"
head -c 2000 "$SHARE/la/h1.lha" > "$SHARE/la/cut.lha"
printf 'This is not an archive.\n' > "$SHARE/la/notlha.lha"

cat > build/la-startup <<'EOS'
failat 21
oasis >Work:la-mem1.txt CMD MEM
oasis >Work:la-l0.txt CMD LHALIST Work:la/h0.lha
oasis >Work:la-l1.txt CMD LHALIST Work:la/h1.lha
oasis >Work:la-l2.txt CMD LHALIST Work:la/h2.lha
oasis >Work:la-l3.txt CMD LHALIST Work:la/stored.lha
oasis >Work:la-l4.txt CMD LHALIST Work:la/dirs.lha
oasis >Work:la-l5.txt CMD LHALIST Work:la/cut.lha
oasis >Work:la-l6.txt CMD LHALIST Work:la/notlha.lha
oasis >Work:la-l7.txt CMD TYPE Work:la/h0.lha Work:la/h1.lha Work:la/h2.lha Work:la/stored.lha
oasis >Work:la-x1.txt CMD LHAX Work:la/h1.lha Work:la/out
oasis >Work:la-x2.txt CMD LHAX Work:la/h2.lha Work:
oasis >Work:la-a1.txt CMD LHAA Work:la/new.lha Work:la/src readme.txt "with space.txt" sub tool tool.info
oasis >Work:la-a2.txt CMD LHAA Work:la/many.lha Work:la/src @Work:la/list.txt
rename C:LhA C:LhA-off
oasis >Work:la-n1.txt CMD LHAX Work:la/h1.lha Work:la/out
oasis >Work:la-n2.txt CMD LHALIST Work:la/h1.lha
rename C:LhA-off C:LhA
oasis >Work:la-mem2.txt CMD MEM
echo >Work:la-done.txt "done"
EOS
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src "$IMG" sh -euc "
  m68k-amigaos-gcc -mcrt=nix13 -m68000 -Os -s -o build/lhastub tools/lhastub.c
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo Rename Run; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  rm -f build/la.adf
  xdftool build/la.adf format OasisTest ofs + boot install + write build/oasis oasis \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo \
    + write build/c/Rename c/Rename + write build/c/Run c/Run + write build/lhastub c/LhA \
    + makedir s + write build/la-startup s/startup-sequence >/dev/null"
PLAY=${OASIS_PLAY:-/mnt/j/amiga/play}
cp build/la.adf "$PLAY/oasis-la.adf"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\oasis-la.adf#' \
      -e 's#^config_description=.*#config_description=Oasis LhA test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/oasis-la-$k.uae"
done
echo "ready: $PLAY/oasis-la-ks13.uae, $PLAY/oasis-la-ks31.uae"
