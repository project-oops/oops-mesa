#!/usr/bin/env bash
# Build tools/preamble-dump against the pinned Mesa and regenerate its two outputs.
#
# Runs Mesa's own header generators (register definitions, packet tables, format enums) into
# build/preamble-dump/gen, compiles the tool with the four Mesa sources it needs, runs it, and
# writes preamble_gfx1013.h (the streams as C data) and preamble_gfx1013.txt (the streams
# decoded with Mesa's register names) next to this script. Both are tracked; `make check`
# regenerates them into the build directory and fails if the tracked copies differ, so the pin
# in dependencies.mk and these two files move together.
#
# Needs python3 and clang on PATH; the collection's WSL builder has both. No Mesa build, no
# meson, no LLVM.
set -eu
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
MESA=$ROOT/mesa
OUT=${OUT:-$ROOT/build/preamble-dump}
GEN=$OUT/gen
mkdir -p "$GEN/util/format"

if [ ! -f "$MESA/src/amd/common/ac_cmdbuf.c" ]; then
    echo "preamble-dump: mesa/ is not checked out (git submodule update --init)" >&2
    exit 1
fi

# The same inputs, in the same order, as src/amd/common/meson.build and src/util/format.
REGS=$(for f in gfx6 gfx7 gfx8 gfx81 gfx9 gfx940 gfx10 gfx103 gfx11 gfx115 gfx12 \
                pkt3 gfx10-rsrc gfx11-rsrc gfx12-rsrc registers-manually-defined; do
           printf '%s ' "$MESA/src/amd/registers/$f.json"; done)
PKTS="$MESA/src/amd/packets/cp_pm4_table_data_gfx11.json $MESA/src/amd/packets/pm4_it_opcodes_gfx11.h \
$MESA/src/amd/packets/cp_pm4_table_data_gfx12.json $MESA/src/amd/packets/pm4_it_opcodes_gfx12.h"

# shellcheck disable=SC2086
python3 "$MESA/src/amd/registers/makeregheader.py" $REGS --sort address --guard AMDGFXREGS_H > "$GEN/amdgfxregs.h"
# shellcheck disable=SC2086
python3 "$MESA/src/amd/common/sid_tables.py" "$MESA/src/amd/common/sid.h" $REGS > "$GEN/sid_tables.h"
for gen in gfx11 gfx12; do
    # shellcheck disable=SC2086
    python3 "$MESA/src/amd/packets/parse_cp_pm4_table_data_json.py" $PKTS $gen packets_h > "$GEN/amd_cp_packets_$gen.h"
done
python3 "$MESA/src/util/format/u_format_table.py" "$MESA/src/util/format/u_format.yaml" --enums > "$GEN/util/format/u_format_gen.h"

AMD=$MESA/src/amd/common
clang -std=gnu11 -O1 -Wall -Wno-unused-function -Wno-unused-variable \
    -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0 \
    -I"$GEN" -I"$AMD" -I"$MESA/src/amd" -I"$MESA/src" -I"$MESA/include" -I"$MESA/src/util" \
    "$HERE/preamble_dump.c" "$HERE/stubs.c" \
    "$AMD/ac_cmdbuf.c" "$AMD/ac_pm4.c" "$AMD/ac_shader_util.c" "$AMD/ac_debug.c" \
    -o "$OUT/preamble-dump"

"$OUT/preamble-dump" > "$OUT/preamble_gfx1013.h" 2> "$OUT/preamble_gfx1013.txt"
if [ "${1:-}" = "--check" ]; then
    cmp -s "$OUT/preamble_gfx1013.h" "$HERE/preamble_gfx1013.h" &&
    cmp -s "$OUT/preamble_gfx1013.txt" "$HERE/preamble_gfx1013.txt" ||
        { echo "preamble-dump: tracked outputs differ from a fresh generation; run $0" >&2; exit 1; }
    echo "preamble-dump: tracked outputs match the pinned Mesa"
else
    cp "$OUT/preamble_gfx1013.h" "$OUT/preamble_gfx1013.txt" "$HERE/"
    echo "preamble-dump: wrote $HERE/preamble_gfx1013.h and .txt"
fi
