#!/bin/sh
# Builds ANNAExample.dll with llvm-mingw (clang). Usage: sh build.sh [output.dll]
# Install: <Azimuth folder>\mods\Forza Horizon 3\ANNAExample\plugins\ANNAExample.dll
set -e
cd "$(dirname "$0")"
OUT=${1:-ANNAExample.dll}
clang --target=x86_64-w64-mingw32 -std=c11 -shared -O2 -Wall -Wextra -Wno-missing-field-initializers \
    -DUNICODE -D_UNICODE -I ../../include -I ../../sdk \
    anna_example.c -static -o "$OUT"
