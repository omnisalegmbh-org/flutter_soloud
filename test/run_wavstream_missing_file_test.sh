#!/bin/bash
# Build and run the standalone native test for a WavStream whose file is gone.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/wavstream_missing_file_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_NULL -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -o "$OUT" \
    test/wavstream_missing_file_test.cpp \
    test/dr_mp3_impl.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/soloud_wavstream.cpp \
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"
