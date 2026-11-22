#!/bin/sh
# Builds ANNA.dll with llvm-mingw (clang). Usage: sh build.sh [output.dll]
# Install: mods\Forza Horizon 3\ANNA\plugins\ANNA.dll next to azimuth.exe (ANNA.ini is written beside plugins\).
set -e
cd "$(dirname "$0")"
OUT=${1:-ANNA.dll}
clang --target=x86_64-w64-mingw32 -std=c11 -shared -O2 -Wall -Wextra -Wno-missing-field-initializers \
    -DUNICODE -D_UNICODE -I . -I src \
    src/anna.c src/menu.c src/drive.c src/car.c src/text.c src/support.c \
    -static -o "$OUT"
