#!/usr/bin/env bash
# What the built Mesa still needs that nothing in this tree provides.
#
# Takes every symbol the archives reference, subtracts every symbol they define, subtracts what
# the shims and the staged libraries define, and sorts what is left by **how well each name is
# evidenced**, which is the part this file originally got wrong:
#
#   exports          a native sweep resolved it to an address. A title binds it.
#   probably         only the GEN=4 census knows it - the PS4 backward-compatibility container,
#                    whose Orbis libc exported far more than native Prospero. ADVISORY.
#   measured absent  a native sweep looked and found nothing, and the build imports it anyway.
#                    This is the worst pile: it loads, then dies on the first call.
#   missing          nobody provides it and no sweep has an opinion.
#
# The middle two are the point. Until 2026-09-17 this tool had one "measured" pile, drawn entirely
# from `data/hardware/ps5-imports.txt`, and every row in that file turns out to have been captured
# under GEN=4 (REQ-20260917T2014Z-c8a7). So names were being reported as exported - and therefore
# moved out of the work list - on evidence from a different container. A tool that under-reports
# the work list is worse than no tool, because it is the thing a reader trusts to say what is
# left.
#
# The question is asked of the build rather than of a person, so it stays right as the build
# changes. Run it after `./bin/oops-mesa build`.
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
#
# **oops-sdk's freestanding helpers count too**, and leaving them out was a real defect. A title
# links `src/system/freestd.c` and its neighbours along with oops-mesa's shims, so a name defined
# there is already answered - but this tool only compiled oops-mesa's own sources and reported
# `memcmp` as unprovided when `freestd.c` had defined it all along. Acting on that produced a
# duplicate symbol at link, which is the harmless direction for this mistake to go, unlike the
# census defect above. Both are the same class: a pile built from a subset of what a title
# actually links.
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

# What the platform exports - and **this file used to get that wrong in the dangerous direction.**
#
# # Why the census is no longer the oracle
#
# `data/hardware/ps5-imports.txt` was the only source here, and its `present` rows were reported
# as "the platform exports". obSCEne has since established that **every one of its 17,645 rows
# was captured under GEN=4** - the PS4 backward-compatibility container, running Orbis userland,
# where `libSceLibcInternal` did export the C runtime and the maths. There are zero GEN=5 rows in
# it (REQ-20260917T2014Z-c8a7, and the root cause first surfaced in `-9f41`).
#
# Native Prospero ships a stripped PRX without those exports. So a `present` row in that file is
# evidence about a different container, and a symbol it lists can still be unbindable in a title -
# which is not a link error but a module that loads and dies on first call with
# `PRX_NOT_RESOLVED_FUNCTION`. Reporting those names as "the platform exports" moved them out of
# the work list, which is the one place a reader looks to find out what is left.
#
# # What replaced it
#
# The two native sweeps `-c8a7` names as authoritative. Their rows are unambiguous - each swept
# symbol gets an `addr` row, and `0x0` means the name did not resolve:
#
#     OBS|measure|<check>|snprintf|addr|0x8000a8790|hex     resolves
#     OBS|measure|<check>|sin|addr|0x0|hex                  does not
#
# The census is still read, because most of what a title imports was never swept natively and an
# Orbis-era answer is better than no answer - but it is reported as **advisory** and counted
# separately, so nothing here can present it as measurement again.
# Globbed rather than listed, and date-scoped rather than open.
#
# The glob is because the file naming is not stable between runs - `-payload.obs.log` for two of
# these and `-payload.obs.obs.log` for the third - and a hardcoded list silently loses a sweep
# when the next one lands under whichever spelling. Files with no `addr` rows contribute nothing,
# so a wider net costs only the read.
#
# The date is because native `GEN=5` title execution did not exist before 2026-09-02 (obscene
# worklog 150), and everything earlier is the backward-compatibility container - which is the
# entire point of not reading the census. `20260917-*` is the set obSCEne named as authoritative
# in `-c8a7`, plus whatever it adds to that day.
NATIVE_SWEEPS=$(ls "$OBSCENE"/reports/hardware/20260917-*-payload.obs*.log 2>/dev/null || true)
: > "$WORK/native-present.txt"
: > "$WORK/native-absent.txt"
for sweep in $NATIVE_SWEEPS; do
    [ -f "$sweep" ] || continue
    # `>>` and not `>`. awk truncates a redirection target on first use *per invocation*, and this
    # runs once per sweep - so `>` made the second sweep discard the first one's rows, leaving 39
    # of 151 and reporting a dozen names as unprovided that the platform plainly exports. The
    # files are emptied once before the loop instead.
    awk -F'|' '$1=="OBS" && $2=="measure" && $5=="addr" {
        if ($6 == "0x0") print $4 >> absent; else print $4 >> present;
    }' present="$WORK/native-present.txt" absent="$WORK/native-absent.txt" "$sweep"
done
sort -u -o "$WORK/native-present.txt" "$WORK/native-present.txt"
sort -u -o "$WORK/native-absent.txt" "$WORK/native-absent.txt"
# A name measured both ways across two sweeps resolves somewhere, so presence wins.
comm -23 "$WORK/native-absent.txt" "$WORK/native-present.txt" > "$WORK/na.tmp"
mv "$WORK/na.tmp" "$WORK/native-absent.txt"

# The GEN=4 census, kept as advisory only. Two kinds of row count.
{
    grep -oE "^OBS\|sym\|[A-Za-z][A-Za-z_0-9]*\|[A-Za-z_][A-Za-z_0-9]*\|present" "$CENSUS" \
        | awk -F'|' '{print $4}'
    grep -oE "^OBS\|try\|[^|]*\|[A-Za-z][A-Za-z_0-9]*\|[A-Za-z_][A-Za-z_0-9]*$" "$CENSUS" \
        | awk -F'|' '{print $5}'
} | sort -u > "$WORK/advisory.txt"

comm -23 "$WORK/referenced.txt" "$WORK/defined.txt" > "$WORK/unresolved.txt"
comm -23 "$WORK/unresolved.txt" "$WORK/shimmed.txt" > "$WORK/after-shims.txt"
comm -23 "$WORK/after-shims.txt" "$WORK/staged.txt" > "$WORK/after-staged.txt"

# Measured natively, in both directions. The absent pile is the one that matters: those names are
# imported by this build and are known not to resolve, so each is a crash with a caller's name on
# it rather than something to go looking for.
comm -12 "$WORK/after-staged.txt" "$WORK/native-present.txt" > "$WORK/from-platform.txt"
comm -12 "$WORK/after-staged.txt" "$WORK/native-absent.txt"  > "$WORK/must-be-local.txt"

# Everything not settled by a native sweep, then split by whether the Orbis-era census knows it.
comm -23 "$WORK/after-staged.txt" "$WORK/native-present.txt" > "$WORK/unswept.tmp"
comm -23 "$WORK/unswept.tmp" "$WORK/native-absent.txt" > "$WORK/unswept.txt"
rm -f "$WORK/unswept.tmp"
comm -12 "$WORK/unswept.txt" "$WORK/advisory.txt" > "$WORK/advisory-only.txt"
comm -23 "$WORK/unswept.txt" "$WORK/advisory.txt" > "$WORK/missing.txt"

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

# Ordered worst first: a name measured absent and still imported is a crash with a caller, not a
# thing to go looking for.
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
    echo "  (obSCEne REQ-20260917T2014Z-c8a7). A name in that pile may still fail to bind in a"
    echo "  title. To settle one, ask for a native sweep of it rather than trusting this line."
fi
