#!/usr/bin/env bash
# Builds and runs the TapTap host tests with the system C++ compiler.
set -euo pipefail
cd "$(dirname "$0")"
SRC=../../examples/companion_radio
OUT="${TMPDIR:-/tmp}/taptap_host_test"
g++ -std=c++17 -Wall -Wextra -DTAPTAP_FEATURES -I. -I"$SRC" -I"$SRC/ui-taptap" \
  test_taptap.cpp "$SRC/TapTap.cpp" "$SRC/ui-taptap/TapPlayer.cpp" "$SRC/ui-taptap/Button.cpp" -o "$OUT"
"$OUT"
