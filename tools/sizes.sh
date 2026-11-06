#!/bin/bash
# Code size of each Oasis source file (to keep the program small). Run ./build.sh first.
set -euo pipefail
cd "$(dirname "$0")/.."
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src \
  "${AGK_TOOLCHAIN_IMAGE:-amigadev/crosstools:m68k-amigaos}" sh -euc '
  mkdir -p build/obj
  for f in src/*.c build/logo.c; do
    m68k-amigaos-gcc -mcrt=nix13 -m68000 -Os -Isrc -c "$f" -o "build/obj/$(basename "$f" .c).o"
  done
  m68k-amigaos-size build/obj/*.o'
