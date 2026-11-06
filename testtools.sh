#!/bin/bash
# Oasis tools test (milestone 3). Builds Oasis, makes fixtures in Work:tt ($OASIS_SHARE/tt),
# and a boot disk (build/tt.adf) whose startup-sequence runs "oasis CMD" SIZE / SEARCH /
# MATCH / PROTECT / COMMENT / COMPARE / PREFS / BUTTON steps, writing Work:tt-*.txt.
# Then start WinUAE with $OASIS_PLAY/oasis-tt-ks13.uae (or -ks31) and run tools/checktools.py
# when Work:tt-done.txt appears.
#
# The boot disk borrows Echo, FailAt, Run and L:Ram-Handler from the Workbench 1.3 disk
# ($WB13_ADF): it is a local test disk only and must never be distributed.
set -euo pipefail
cd "$(dirname "$0")"
SHARE=${OASIS_SHARE:-/mnt/j/amiga/share}
WB13_ADF=${WB13_ADF:-"/mnt/j/amiga/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf"}
./build.sh >/dev/null
cc -Wall -Wextra -I src -o build/testmatch tools/testmatch.c src/match.c && build/testmatch

# ---- fixtures ----
rm -rf "$SHARE/tt" "$SHARE"/tt-*.txt
python3 - "$SHARE/tt" <<'PY'
import os, sys, random
root = sys.argv[1]
r = random.Random(1986)
def put(rel, data):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'wb').write(data)
put('src/readme.txt', b'Oasis tools fixture.\n' * 95)
put('src/notes.TXT', b'0123456789')
put('src/big.bin', bytes(r.getrandbits(8) for _ in range(300 * 1024 + 123)))
put('src/empty.dat', b'')
put('src/tool', b'\x00\x00\x03\xf3' + bytes(5000))
put('src/tool.info', open('build/oasis.info', 'rb').read())
put('src/sub.info', open('build/oasis.info', 'rb').read())
put('src/name with space.txt', b'spaces\n')
put('src/sub/a.txt', b'level one\n' * 50)
put('src/sub/deeper/b.txt', b'level two\n' * 20)
put('src/sub/deeper/c.bin', bytes(range(256)) * 40)
# two drawers to compare
for side, extra in (('a', b''), ('b', b'!')):
    put(f'{side}/same1', b'1' * 100)
    put(f'{side}/same2', b'2' * 200)
    put(f'{side}/size.diff', b's' * 100 + extra)
    put(f'{side}/date.diff', b'd' * 50)
    put(f'{side}/only{side.upper()}.txt', b'only here')
    os.makedirs(os.path.join(root, side, 'both'), exist_ok=True)
put('a/clash', b'a file here')
os.makedirs(os.path.join(root, 'b', 'clash'), exist_ok=True)
# a prefs file written on a PC (CRLF), with two lines Oasis must not accept
prefs = '\r\n'.join([
    '; Oasis test prefs',
    'LEFT "Work:tt/a"',
    'RIGHT ""',
    'SORT date',
    'HIDEICONS yes',
    'BUTTON "Args" DF0:oasis CMD MATCH #?.txt %s',
    'BUTTON "Copy to" "DF0:oasis CMD COPY %s TO %d"',
    'BUTTON "Quiet" QUIET Echo >Work:tt-quiet.txt "quiet ran"',
    'BUTTON "Pct" DF0:oasis CMD MATCH 100%% %p',
    'BUTTON "Here" DF0:oasis CMD LIST ""',
    'FROB nonsense',
    'BUTTON "" nothing',
]) + '\r\n'
open(os.path.join(root, 'test.prefs'), 'w', newline='').write(prefs)
os.makedirs(os.path.join(root, 'dst'), exist_ok=True)
PY
find "$SHARE/tt" -exec touch -h -d '2002-03-04 05:06:07' {} +
touch -d '2003-01-01 12:00:00' "$SHARE/tt/b/date.diff"

# ---- boot disk ----
cat > build/tt-startup <<'EOS'
failat 21
oasis >Work:tt-ram0.txt CMD TREE RAM:
oasis >Work:tt-mem1.txt CMD MEM
oasis >Work:tt-t-src.txt CMD TREE Work:tt/src
oasis >Work:tt-size1.txt CMD SIZE Work:tt/src
oasis >Work:tt-size2.txt CMD SIZE Work:tt/src/big.bin Work:tt/src/sub
oasis >Work:tt-size3.txt CMD SIZE Work:tt/nothing
oasis >Work:tt-s1.txt CMD SEARCH Work:tt/src #?.txt
oasis >Work:tt-s2.txt CMD SEARCH Work:tt/src txt
oasis >Work:tt-s3.txt CMD SEARCH Work:tt/src #?.(bin|dat)
oasis >Work:tt-s4.txt CMD SEARCH Work:tt/src deep
oasis >Work:tt-s5.txt CMD SEARCH Work:tt/src ?ool
oasis >Work:tt-s6.txt CMD SEARCH Work:tt/src x#?y
oasis >Work:tt-s7.txt CMD SEARCH Work:tt/src/readme.txt #?
oasis >Work:tt-m1.txt CMD MATCH #?.info tool.info tool TOOL.INFO
oasis >Work:tt-m2.txt CMD MATCH (a|b)#c ac bccc c abc
oasis >Work:tt-p0.txt CMD PROTECT Work:tt/src/tool rwed
oasis >Work:tt-p1.txt CMD LIST Work:tt/src
oasis >Work:tt-p2.txt CMD PROTECT Work:tt/src/tool +sp
oasis >Work:tt-p3.txt CMD LIST Work:tt/src
oasis >Work:tt-p4.txt CMD PROTECT Work:tt/src/tool -wd
oasis >Work:tt-p5.txt CMD LIST Work:tt/src
oasis >Work:tt-p6.txt CMD PROTECT Work:tt/src/tool xyz
oasis >Work:tt-p7.txt CMD PROTECT Work:tt/src/tool rwed
oasis >Work:tt-r1.txt CMD COPY Work:tt/src/tool TO RAM:
oasis >Work:tt-r2.txt CMD PROTECT RAM:tool +a
oasis >Work:tt-r3.txt CMD PROTECT RAM:tool -e
oasis >Work:tt-r4.txt CMD LIST RAM:
oasis >Work:tt-c1.txt CMD COMMENT Work:tt/src/readme.txt "Hello from Oasis"
oasis >Work:tt-c2.txt CMD LIST Work:tt/src
oasis >Work:tt-c3.txt CMD COMMENT RAM:tool "On the RAM disk"
oasis >Work:tt-c4.txt CMD LIST RAM:
oasis >Work:tt-c5.txt CMD COMMENT Work:tt/src/readme.txt ""
oasis >Work:tt-c6.txt CMD LIST Work:tt/src
oasis >Work:tt-k1.txt CMD COMPARE Work:tt/a Work:tt/b
oasis >Work:tt-k2.txt CMD COPY Work:tt/a TO RAM:
oasis >Work:tt-k3.txt CMD COMPARE Work:tt/a RAM:a
oasis >Work:tt-k4.txt CMD COMPARE Work:tt/a Work:tt/nothing
oasis >Work:tt-memA.txt CMD MEM
oasis >Work:tt-f1.txt CMD PREFS Work:tt/test.prefs
oasis >Work:tt-f2.txt CMD PREFS Work:tt/none.prefs
oasis >Work:tt-b1.txt CMD BUTTON Work:tt/test.prefs 1 Work:tt/src Work:tt/dst Work:tt/src/readme.txt "Work:tt/src/name with space.txt"
oasis >Work:tt-b2.txt CMD BUTTON Work:tt/test.prefs 2 Work:tt/src Work:tt/dst Work:tt/src/readme.txt
oasis >Work:tt-b3.txt CMD BUTTON Work:tt/test.prefs 3 Work:tt/src Work:tt/dst
oasis >Work:tt-b4.txt CMD BUTTON Work:tt/test.prefs 4 Work:tt/src Work:tt/dst
oasis >Work:tt-b5.txt CMD BUTTON Work:tt/test.prefs 5 Work:tt/src/sub Work:tt/dst
oasis >Work:tt-b6.txt CMD BUTTON Work:tt/test.prefs 1 Work:tt/src Work:tt/dst
oasis >Work:tt-b7.txt CMD BUTTON Work:tt/test.prefs 9 Work:tt/src Work:tt/dst
oasis >Work:tt-memB.txt CMD MEM
oasis >Work:tt-t-dst.txt CMD TREE Work:tt/dst
oasis >Work:tt-x1.txt CMD DELETE RAM:a RAM:tool FORCE
oasis >Work:tt-mem2.txt CMD MEM
echo >Work:tt-done.txt "done"
EOS
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -v "$(dirname "$WB13_ADF"):/wb:ro" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc "
  wb='/wb/$(basename "$WB13_ADF")'
  mkdir -p build/c
  for c in FailAt Echo Run; do xdftool \"\$wb\" read c/\$c build/c/\$c; done
  xdftool \"\$wb\" read l/Ram-Handler build/c/Ram-Handler     # 1.3 loads RAM: from L:
  rm -f build/tt.adf
  xdftool build/tt.adf format OasisTest ofs + boot install + write build/oasis oasis \
    + makedir c + write build/c/FailAt c/FailAt + write build/c/Echo c/Echo + write build/c/Run c/Run \
    + makedir l + write build/c/Ram-Handler l/Ram-Handler \
    + makedir s + write build/tt-startup s/startup-sequence >/dev/null"
PLAY=${OASIS_PLAY:-/mnt/j/amiga/play}
cp build/tt.adf "$PLAY/oasis-tt.adf"
for k in ks13 ks31; do
  sed -e 's#^floppy0=.*#floppy0=J:\\amiga\\play\\oasis-tt.adf#' \
      -e 's#^config_description=.*#config_description=Oasis tools test#' \
      "/mnt/j/amiga/presets/oasis-$k.uae" > "$PLAY/oasis-tt-$k.uae"
done
echo "ready: $PLAY/oasis-tt-ks13.uae, $PLAY/oasis-tt-ks31.uae"
