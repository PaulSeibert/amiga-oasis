#!/bin/bash
# Build the Oasis release in dist/ (runs ./build.sh first):
#   Oasis.lha     Oasis.info               drawer icon
#                 Oasis/Oasis              the program        Oasis/Oasis.info          its icon
#                 Oasis/Oasis.readme       Aminet readme      Oasis/Oasis.readme.info   opens it with More
#                 Oasis/Oasis.prefs        example prefs      Oasis/Oasis.prefs.info    opens it with Ed
#   Oasis.readme  (Aminet wants it next to the archive)
#   oasis.adf     a disk that boots straight into Oasis (only Oasis's own files)
set -euo pipefail
cd "$(dirname "$0")"
./build.sh >/dev/null
rm -rf dist
mkdir -p dist/Oasis dist/disk/s
cp build/oasis dist/Oasis/Oasis
cp build/oasis.info dist/Oasis/Oasis.info
cp Oasis.readme dist/Oasis/Oasis.readme
cp examples/Oasis.prefs dist/Oasis/Oasis.prefs
python3 tools/mkicon.py --drawer art/drawer.txt dist/Oasis.info
python3 tools/mkicon.py --project SYS:Utilities/More art/readme-icon.txt dist/Oasis/Oasis.readme.info
python3 tools/mkicon.py --project C:Ed art/prefs-icon.txt dist/Oasis/Oasis.prefs.info
printf 'oasis\n' > dist/disk/startup-sequence
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc '
  cd dist
  lha -aq Oasis.lha Oasis.info Oasis/Oasis Oasis/Oasis.info Oasis/Oasis.readme Oasis/Oasis.readme.info \
    Oasis/Oasis.prefs Oasis/Oasis.prefs.info
  lha -l Oasis.lha
  xdftool oasis.adf format Oasis ofs + boot install + write Oasis/Oasis oasis + write Oasis/Oasis.info oasis.info \
    + write Oasis/Oasis.readme Oasis.readme + makedir s + write disk/startup-sequence s/startup-sequence \
    + write ../examples/disk.prefs s/Oasis.prefs >/dev/null
  xdftool oasis.adf list'
rm -rf dist/Oasis dist/Oasis.info dist/disk
cp Oasis.readme dist/
SHARE=${OASIS_SHARE:-/mnt/j/amiga/share}
[ -d "$SHARE" ] && cp dist/Oasis.lha "$SHARE/"
ls -l dist
