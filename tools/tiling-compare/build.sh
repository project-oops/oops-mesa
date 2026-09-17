#!/usr/bin/env bash
# Build tools/tiling-compare against the pinned Mesa's AddressLib and oops-sdk's tiler, run it,
# and write tiling_gfx1013.txt next to this script.
#
# The tracked output is the point. It records whether a radeonsi 64KB_R_X surface lands in the
# same bytes as the layout the display scans out, across four blocks rather than one - which is
# the extrapolation worklog 030 caught. `make check` regenerates into build/ and compares, so a
# pin bump or a tiler edit that changes the answer is visible in the diff rather than discovered
# during bring-up.
#
# Needs clang++ on PATH; the collection's WSL builder has it. No meson, no Mesa build, no LLVM.
set -eu
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
MESA=$ROOT/mesa
ADDR=$MESA/src/amd/addrlib
OUT=${OUT:-$ROOT/build/tiling-compare}

# oops-sdk is a sibling checkout, named by a variable rather than assumed - the same shape the
# host suite and the FreeBSD checkout use (D004).
OOPS_SDK=${OOPS_SDK:-$ROOT/../oops-sdk}

mkdir -p "$OUT"

if [ ! -f "$ADDR/src/addrinterface.cpp" ]; then
    echo "tiling-compare: mesa/ is not checked out (git submodule update --init)" >&2
    exit 1
fi
if [ ! -f "$OOPS_SDK/src/agc/agc_tiler.c" ]; then
    echo "tiling-compare: no oops-sdk at $OOPS_SDK; set OOPS_SDK" >&2
    exit 1
fi

# The source list and the flags are AddressLib's own, from src/amd/addrlib/meson.build. The r800
# and gfx9/11/12 backends are compiled too: addrinterface.cpp dispatches to all of them and the
# link needs every one, even though only the gfx10 path is exercised.
SRCS="
$ADDR/src/addrinterface.cpp
$ADDR/src/core/addrelemlib.cpp
$ADDR/src/core/addrlib.cpp
$ADDR/src/core/addrlib1.cpp
$ADDR/src/core/addrlib2.cpp
$ADDR/src/core/addrlib3.cpp
$ADDR/src/core/addrobject.cpp
$ADDR/src/core/addrswizzler.cpp
$ADDR/src/core/coord.cpp
$ADDR/src/gfx9/gfx9addrlib.cpp
$ADDR/src/gfx10/gfx10addrlib.cpp
$ADDR/src/gfx11/gfx11addrlib.cpp
$ADDR/src/gfx12/gfx12addrlib.cpp
$ADDR/src/r800/ciaddrlib.cpp
$ADDR/src/r800/egbaddrlib.cpp
$ADDR/src/r800/siaddrlib.cpp
"

INCS="
-I$ADDR/inc
-I$ADDR/src
-I$ADDR/src/core
-I$ADDR/src/chip/gfx9
-I$ADDR/src/chip/gfx10
-I$ADDR/src/chip/gfx11
-I$ADDR/src/chip/gfx12
-I$ADDR/src/chip/r800
-I$MESA/src/amd/common
-I$MESA/src
-I$MESA/include
-I$OOPS_SDK/include
"

# -DADDR_FASTCALL= and the endian/SIMD/DEBUG defines are what meson.build sets. DEBUG=0 matches
# a release build, which is what the target build produces and therefore what a title runs.
DEFS="-DADDR_FASTCALL= -DLITTLEENDIAN_CPU -DADDR_ALLOW_SIMD=1 -DDEBUG=0"
WARN="-Wno-unused-variable -Wno-unused-local-typedefs -Wno-unused-but-set-variable
      -Wno-self-assign -Wno-uninitialized -Wno-unused-private-field -Wno-missing-braces"

# shellcheck disable=SC2086
clang -std=c11 -O1 -c $INCS "$OOPS_SDK/src/agc/agc_tiler.c" -o "$OUT/agc_tiler.o"
# shellcheck disable=SC2086
clang++ -std=c++17 -O1 $DEFS $WARN $INCS \
    "$HERE/tiling_compare.cpp" $SRCS "$OUT/agc_tiler.o" \
    -o "$OUT/tiling-compare"

"$OUT/tiling-compare" > "$OUT/tiling_gfx1013.txt"
if [ "${1:-}" = "--check" ]; then
    cmp -s "$OUT/tiling_gfx1013.txt" "$HERE/tiling_gfx1013.txt" ||
        { echo "tiling-compare: tracked output differs from a fresh run; run $0" >&2; exit 1; }
    echo "tiling-compare: tracked output matches the pinned Mesa and oops-sdk's tiler"
else
    cp "$OUT/tiling_gfx1013.txt" "$HERE/"
    echo "tiling-compare: wrote $HERE/tiling_gfx1013.txt"
    cat "$HERE/tiling_gfx1013.txt"
fi
