#!/bin/zsh
# Builds the 32-bit proxy with MinGW and runs the sync tests natively.
set -euo pipefail
cd "${0:A:h}"
mkdir -p dist
i686-w64-mingw32-gcc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -shared -static-libgcc -s \
  -o dist/winmm.dll mod.c sync.c thunks.S winmm.def -lws2_32
test_bin=$(mktemp)
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -o "$test_bin" test_sync.c sync.c
"$test_bin"
rm -f "$test_bin"
