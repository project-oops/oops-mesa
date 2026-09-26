#!/usr/bin/env bash
# What the built Mesa still needs that nothing in this tree provides.
#
# Takes every symbol the archives reference, subtracts what they, the shims and the staged
# libraries define, and sorts the rest by evidence:
#
#   exports          a native sweep resolved it to an address. A title binds it.
#   probably         only the census knows it, captured in the PS4 backward-compatibility
#                    container whose libc exports far more than native Prospero. ADVISORY.
#   measured absent  a native sweep found nothing, and the build imports it anyway. It loads,
#                    then dies on the first call.
#   missing          nobody provides it and no sweep has an opinion.
#
# Run it after `./bin/oops-mesa build`.
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

# What the shims define, compiled rather than parsed so a function that does not compile is not
# counted. oops-sdk's `src/system/freestd.c` counts too, because a title links it alongside.
: > "$WORK/shimmed.txt"
for c in "$ROOT"/src/runtime/*.c "$ROOT"/src/runtime/*.cpp "$ROOT"/src/winsys/*.c \
         "${OOPS_SDK:-$ROOT/../oops-sdk}"/src/system/freestd.c; do
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

# Libraries staged into the sysroot (libelf, libm, ...) are linked by a title, so they count.
: > "$WORK/staged.txt"
for a in "$ROOT"/toolchain/sysroot/usr/lib/*.a; do
    [ -f "$a" ] || continue
    case "$(basename "$a")" in
        libplatform.a) continue ;;   # the probe archive, never linked into anything that runs
    esac
    llvm-nm --defined-only "$a" 2>/dev/null | awk 'NF>=3 {print $3}' >> "$WORK/staged.txt"
done
sort -u -o "$WORK/staged.txt" "$WORK/staged.txt"

# What the platform exports, from obSCEne's native sweeps. The census
# (`data/hardware/ps5-imports.txt`) was captured in the PS4 backward-compatibility container,
# whose libc exports the C runtime and the maths that native Prospero does not, so it is read
# only as advisory. Each swept symbol has an `addr` row, and `0x0` means it did not resolve:
#
#     OBS|measure|<check>|snprintf|addr|0x8000a8790|hex     resolves
#     OBS|measure|<check>|sin|addr|0x0|hex                  does not
#
# The report names vary in spelling (`.obs.log`, `.obs.obs.log`), hence the glob; the date
# prefix selects the native-leg sweeps obSCEne names as authoritative.
NATIVE_SWEEPS=$(ls "$OBSCENE"/reports/hardware/20260917-*-payload.obs*.log 2>/dev/null || true)
: > "$WORK/native-present.txt"
: > "$WORK/native-absent.txt"
for sweep in $NATIVE_SWEEPS; do
    [ -f "$sweep" ] || continue
    # `>>`, not `>`: awk truncates on first use per invocation, which would discard earlier
    # sweeps. The files are emptied once before the loop.
    awk -F'|' '$1=="OBS" && $2=="measure" && $5=="addr" {
        if ($6 == "0x0") print $4 >> absent; else print $4 >> present;
    }' present="$WORK/native-present.txt" absent="$WORK/native-absent.txt" "$sweep"
done
sort -u -o "$WORK/native-present.txt" "$WORK/native-present.txt"
sort -u -o "$WORK/native-absent.txt" "$WORK/native-absent.txt"
# A name measured both ways across two sweeps resolves somewhere, so presence wins.
comm -23 "$WORK/native-absent.txt" "$WORK/native-present.txt" > "$WORK/na.tmp"
mv "$WORK/na.tmp" "$WORK/native-absent.txt"

# The census, advisory only: `sym ... present` and `try` rows both count.
{
    grep -oE "^OBS\|sym\|[A-Za-z][A-Za-z_0-9]*\|[A-Za-z_][A-Za-z_0-9]*\|present" "$CENSUS" \
        | awk -F'|' '{print $4}'
    grep -oE "^OBS\|try\|[^|]*\|[A-Za-z][A-Za-z_0-9]*\|[A-Za-z_][A-Za-z_0-9]*$" "$CENSUS" \
        | awk -F'|' '{print $5}'
} | sort -u > "$WORK/advisory.txt"

comm -23 "$WORK/referenced.txt" "$WORK/defined.txt" > "$WORK/unresolved.txt"
comm -23 "$WORK/unresolved.txt" "$WORK/shimmed.txt" > "$WORK/after-shims.txt"
comm -23 "$WORK/after-shims.txt" "$WORK/staged.txt" > "$WORK/after-staged.txt"

# Measured natively, in both directions. An absent name that is imported crashes on first call.
comm -12 "$WORK/after-staged.txt" "$WORK/native-present.txt" > "$WORK/from-platform.txt"
comm -12 "$WORK/after-staged.txt" "$WORK/native-absent.txt"  > "$WORK/must-be-local.txt"

# Everything not settled by a native sweep, then split by whether the Orbis-era census knows it.
comm -23 "$WORK/after-staged.txt" "$WORK/native-present.txt" > "$WORK/unswept.tmp"
comm -23 "$WORK/unswept.tmp" "$WORK/native-absent.txt" > "$WORK/unswept.txt"
rm -f "$WORK/unswept.tmp"
comm -12 "$WORK/unswept.txt" "$WORK/advisory.txt" > "$WORK/advisory-only.txt"
comm -23 "$WORK/unswept.txt" "$WORK/advisory.txt" > "$WORK/missing.txt"

# Set aside, not counted: other drivers' descriptors from the static pipe loader's table (only
# radeonsi is built), and `sw_screen_create` from the video-layer winsys, which a title does
# not link.
grep -E '_driver_descriptor$|^sw_screen_create' "$WORK/missing.txt" > "$WORK/other-drivers.txt" || true
grep -vE '_driver_descriptor$|^sw_screen_create' "$WORK/missing.txt" > "$WORK/missing-real.txt" || true
mv "$WORK/missing-real.txt" "$WORK/missing.txt"

printf '\n'
printf '  referenced by the archives   %6s\n' "$(wc -l < "$WORK/referenced.txt" | tr -d ' ')"
printf '  answered by the archives     %6s\n' "$(comm -12 "$WORK/referenced.txt" "$WORK/defined.txt" | wc -l | tr -d ' ')"
printf '  answered by the shims        %6s\n' "$(comm -12 "$WORK/unresolved.txt" "$WORK/shimmed.txt" | wc -l | tr -d ' ')"
printf '  answered by staged libraries %6s\n' "$(comm -12 "$WORK/after-shims.txt" "$WORK/staged.txt" | wc -l | tr -d ' ')"
printf '  the platform exports         %6s   (measured on the native leg)\n' \
    "$(wc -l < "$WORK/from-platform.txt" | tr -d ' ')"
printf '  probably exported            %6s   (GEN=4 census only - ADVISORY, see below)\n' \
    "$(wc -l < "$WORK/advisory-only.txt" | tr -d ' ')"
printf '  other drivers and rasteriser %6s   (not built on purpose)\n' \
    "$(wc -l < "$WORK/other-drivers.txt" | tr -d ' ')"
printf '  MEASURED ABSENT, STILL IMPORTED %3s   <- build/needed/must-be-local.txt\n' \
    "$(wc -l < "$WORK/must-be-local.txt" | tr -d ' ')"
printf '  NOBODY PROVIDES              %6s   <- build/needed/missing.txt\n\n' \
    "$(wc -l < "$WORK/missing.txt" | tr -d ' ')"

# Ordered worst first.
if [ -s "$WORK/must-be-local.txt" ]; then
    echo "  MEASURED ABSENT on the native leg and still imported. Each of these loads fine and"
    echo "  kills the title on the first call. They are the work list before anything else:"
    sed 's/^/    /' "$WORK/must-be-local.txt"
    echo
fi

if [ -s "$WORK/missing.txt" ]; then
    echo "  nobody provides these, and no sweep has an opinion either way:"
    head -40 "$WORK/missing.txt" | sed 's/^/    /'
    echo
fi

if [ -s "$WORK/advisory-only.txt" ]; then
    echo "  the 'probably exported' pile is ADVISORY and is not a measurement. Every row in"
    echo "  data/hardware/ps5-imports.txt was captured under GEN=4, the PS4 backward-compatibility"
    echo "  container, where Orbis' libc exported far more than native Prospero does"
    echo "  (obSCEne measurement). A name in that pile may still fail to bind in a"
    echo "  title. To settle one, ask for a native sweep of it rather than trusting this line."
fi
