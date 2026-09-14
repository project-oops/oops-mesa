#!/usr/bin/env bash
# What the built Mesa still needs that nothing in this tree provides.
#
# Takes every symbol the archives reference, subtracts every symbol they define, subtracts what
# the shims define, and sorts what is left into three piles:
#
#   measured   obSCEne's import census records the platform exporting it. A title will resolve
#              it at load, the way oops-sdk's payloads already resolve their vendor calls.
#   shimmed    this repository defines it, in src/runtime or src/winsys.
#   missing    nobody provides it. This is the work list, and it is the only pile that matters.
#
# The question is asked of the build rather than of a person, so it stays right as the build
# changes. Run it after `./bin/oops-mesa build`.
#
# It reads the census from the obscene checkout beside this one, the same source the probe
# archive is generated from, so the two can never disagree about what the platform has.
set -eu

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
BUILD="$ROOT/build/mesa"
WORK="$ROOT/build/needed"

OBSCENE="${OOPS_MESA_OBSCENE_SRC:-$ROOT/../obscene}"
CENSUS="$OBSCENE/data/hardware/ps5-imports.txt"

command -v llvm-nm >/dev/null 2>&1 || {
    echo "oops-mesa: llvm-nm is not on PATH; run this inside the toolchain container" >&2
    exit 1
}
[ -d "$BUILD" ] || { echo "oops-mesa: no build at build/mesa; run ./bin/oops-mesa build" >&2; exit 1; }
[ -f "$CENSUS" ] || { echo "oops-mesa: no obSCEne census at $CENSUS" >&2; exit 1; }

mkdir -p "$WORK"

# Everything the archives ask for, and everything they answer for themselves.
find "$BUILD" -name '*.a' -exec llvm-nm --undefined-only {} + 2>/dev/null \
    | awk '/^ +U /{print $2}' | sort -u > "$WORK/referenced.txt"
find "$BUILD" -name '*.a' -exec llvm-nm --defined-only {} + 2>/dev/null \
    | awk 'NF>=3 {print $3}' | sort -u > "$WORK/defined.txt"

# What the shims define. Compiled here rather than parsed out of the source, so a function that
# exists but does not compile is not counted as provided.
: > "$WORK/shimmed.txt"
for c in "$ROOT"/src/runtime/*.c "$ROOT"/src/runtime/*.cpp "$ROOT"/src/winsys/*.c; do
    [ -f "$c" ] || continue
    o="$WORK/$(basename "${c%.*}").o"
    case "$c" in
        *.cpp) cc=(clang++ -std=c++17 -stdlib=libc++) ;;
        *)     cc=(clang) ;;
    esac
    "${cc[@]}" -target x86_64-unknown-freebsd --sysroot="$ROOT/toolchain/sysroot" \
          -I"$ROOT/mesa/include" -I"$ROOT/src/winsys" -I"${OOPS_SDK:-$ROOT/../oops-sdk}/include" \
          -w -c "$c" -o "$o"
    llvm-nm --defined-only "$o" | awk 'NF>=3 {print $3}' >> "$WORK/shimmed.txt"
done
sort -u -o "$WORK/shimmed.txt" "$WORK/shimmed.txt"

# Libraries staged into the sysroot are provided too. libelf is built from the pinned checkout
# and a title links it; counting only Mesa's archives reported its fifteen symbols as missing.
: > "$WORK/staged.txt"
for a in "$ROOT"/toolchain/sysroot/usr/lib/*.a; do
    [ -f "$a" ] || continue
    case "$(basename "$a")" in
        libplatform.a) continue ;;   # the probe archive, never linked into anything that runs
    esac
    llvm-nm --defined-only "$a" 2>/dev/null | awk 'NF>=3 {print $3}' >> "$WORK/staged.txt"
done
sort -u -o "$WORK/staged.txt" "$WORK/staged.txt"

# What the platform exports, per measurement. Two kinds of row count, and taking only the first
# reported `malloc` and eighty others as missing when the census plainly has them:
#
#   OBS|sym|<lib>|<name>|present   the symbol mine: the name is in the library's export table.
#   OBS|try|<section>|<lib>|<name> a probe that called it, which is stronger evidence than the
#                                  mine, because something ran.
{
    grep -oE "^OBS\|sym\|[A-Za-z][A-Za-z_0-9]*\|[A-Za-z_][A-Za-z_0-9]*\|present" "$CENSUS" \
        | awk -F'|' '{print $4}'
    grep -oE "^OBS\|try\|[^|]*\|[A-Za-z][A-Za-z_0-9]*\|[A-Za-z_][A-Za-z_0-9]*$" "$CENSUS" \
        | awk -F'|' '{print $5}'
} | sort -u > "$WORK/measured.txt"

comm -23 "$WORK/referenced.txt" "$WORK/defined.txt" > "$WORK/unresolved.txt"
comm -23 "$WORK/unresolved.txt" "$WORK/shimmed.txt" > "$WORK/after-shims.txt"
comm -23 "$WORK/after-shims.txt" "$WORK/staged.txt" > "$WORK/after-staged.txt"
comm -12 "$WORK/after-staged.txt" "$WORK/measured.txt" > "$WORK/from-platform.txt"
comm -23 "$WORK/after-staged.txt" "$WORK/measured.txt" > "$WORK/missing.txt"

# Other drivers' entry points are not a requirement. Mesa's static pipe loader carries a table
# naming every gallium driver it knows, and only radeonsi is built, so the rest are references
# to things this configuration deliberately does not have. They are separated rather than
# counted, because a work list with twenty-five of them in it hides the real ones.
# The same goes for the software rasteriser's entry points, which come from the video-layer
# winsys. Video codecs are disabled in this configuration and a title does not link that archive,
# so a reference to it is a property of the build graph rather than a thing anybody has to write.
grep -E '_driver_descriptor$|^sw_screen_create' "$WORK/missing.txt" > "$WORK/other-drivers.txt" || true
grep -vE '_driver_descriptor$|^sw_screen_create' "$WORK/missing.txt" > "$WORK/missing-real.txt" || true
mv "$WORK/missing-real.txt" "$WORK/missing.txt"

printf '\n'
printf '  referenced by the archives   %6s\n' "$(wc -l < "$WORK/referenced.txt" | tr -d ' ')"
printf '  answered by the archives     %6s\n' "$(comm -12 "$WORK/referenced.txt" "$WORK/defined.txt" | wc -l | tr -d ' ')"
printf '  answered by the shims        %6s\n' "$(comm -12 "$WORK/unresolved.txt" "$WORK/shimmed.txt" | wc -l | tr -d ' ')"
printf '  answered by staged libraries %6s\n' "$(comm -12 "$WORK/after-shims.txt" "$WORK/staged.txt" | wc -l | tr -d ' ')"
printf '  the platform exports         %6s   (measured, obSCEne census)\n' \
    "$(wc -l < "$WORK/from-platform.txt" | tr -d ' ')"
printf '  other drivers and rasteriser %6s   (not built on purpose)\n' \
    "$(wc -l < "$WORK/other-drivers.txt" | tr -d ' ')"
printf '  NOBODY PROVIDES              %6s   <- build/needed/missing.txt\n\n' \
    "$(wc -l < "$WORK/missing.txt" | tr -d ' ')"

if [ -s "$WORK/missing.txt" ]; then
    echo "  the first forty, which is the work list:"
    head -40 "$WORK/missing.txt" | sed 's/^/    /'
fi
