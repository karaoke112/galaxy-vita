#!/usr/bin/env bash
# The Mii library's big-endian files (RFL_DB.dat from the Mii folder, RFL_Res.dat from the disc) after
# platform/src/sdk/rfl_port.cpp, read through RFL's own structures on a little-endian host: every bit-field of 100 Miis,
# the database's CRC as RFL checks it, a damaged database (no Miis, not an error), hidden Miis, and a face resource's
# tables, texture headers and shapes. No game data.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$(mktemp -d)}
mkdir -p "$out"
nice "${CXX:-clang++}" -std=gnu++20 -O1 -g -Wall -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
  -DTARGET_PC=1 -DVERSION=0 -fshort-wchar -fsigned-char -fno-strict-aliasing -fwrapv -fms-extensions \
  -Wno-register -Wno-multichar -Wno-writable-strings -Wno-missing-declarations -Wno-unused-function \
  -I"$root/platform/include" -isystem "$root/decomp/include" -isystem "$root/decomp/libs/RVL_SDK/include" \
  -isystem "$root/decomp/libs/RVLFaceLib/include" \
  "$root/tests/rfl_port_layout.cpp" "$root/platform/src/sdk/rfl_port.cpp" -o "$out/rfl_port_layout"
"$out/rfl_port_layout"
echo "rfl_port: all passed"
