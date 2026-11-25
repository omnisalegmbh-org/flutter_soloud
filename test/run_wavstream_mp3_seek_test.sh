#!/bin/bash
# Build and run the standalone native test that MP3 seeks land on their target.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/wavstream_mp3_seek_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_NULL -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -o "$OUT" \
    test/wavstream_mp3_seek_test.cpp \
    test/dr_mp3_impl.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/soloud_wavstream.cpp \
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"
