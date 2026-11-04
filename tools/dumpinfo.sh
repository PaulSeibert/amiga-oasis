#!/bin/bash
# Hex-dump a .info file from one of the user's Workbench 1.3 disks (reference for icon formats).
# Usage: dumpinfo.sh PATH.info [wb|extras]     dumpinfo.sh -l DIR [wb|extras]  lists a directory
set -euo pipefail
WB='/disks/Workbench v1.3.3 rev 34.34 (1990)(Commodore)(A500-A2000)(Disk 1 of 2)(Workbench).adf'
EX='/disks/amiga-os-134-extras.adf'
if [ "$1" = -l ]; then
  D=$WB; [ "${3:-wb}" = extras ] && D=$EX
  docker run --rm -v /mnt/j/amiga/disks:/disks:ro amigadev/crosstools:m68k-amigaos xdftool "$D" list "$2"
  exit
fi
D=$WB; [ "${2:-wb}" = extras ] && D=$EX
docker run --rm -v /mnt/j/amiga/disks:/disks:ro amigadev/crosstools:m68k-amigaos sh -euc "
  xdftool '$D' read '$1' /tmp/x.info && od -A d -t x1 /tmp/x.info | head -6 && echo ... && od -A d -c /tmp/x.info | tail -4 && wc -c /tmp/x.info"
