#!/usr/bin/env bash
# Write the import manifest a title linking Mesa needs, from obSCEne's mined corpus.
#
# # Why this exists
#
# A title's module must say which library each imported symbol resolves from. oops-apps keeps a
# shared list at `common/symbols.txt`, 376 lines, hand-maintained and enough for a freestanding
# title that imports a few dozen vendor calls. A title linking Mesa imports 284, and hand-writing
# those is not a thing to do once, let alone on every rebuild.
#
# obSCEne already knows where they live: `data/mined-names.txt` maps 166,971 names to the
# libraries that export them. This reads the linked binary, asks the corpus about each undefined
# symbol, and writes the manifest. Nothing here decides anything - every attribution comes from
# the corpus and a name the corpus does not know is reported rather than guessed.
#
# `obscene-tool mkmodule` has the same corpus and would do this itself, except that it reads it
# relative to the working directory and a title does not run from obSCEne's repository, so it
# sees an empty corpus and reports everything as unknown (REQ-20260915T0025Z-1a95). When that is
# fixed this may become unnecessary, which would be the better outcome.
set -eu

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

elf=${1:-}
out=${2:-}
if [ -z "$elf" ] || [ -z "$out" ]; then
    echo "usage: generate-imports.sh <linked.elf> <out.txt>" >&2
    exit 2
fi
[ -f "$elf" ] || { echo "oops-mesa: no binary at $elf" >&2; exit 1; }

OBSCENE="${OOPS_MESA_OBSCENE_SRC:-$ROOT/../obscene}"
CORPUS="$OBSCENE/data/mined-names.txt"
SHARED="${OOPS_APPS_SYMBOLS:-$ROOT/../oops-apps/common/symbols.txt}"
[ -f "$CORPUS" ] || { echo "oops-mesa: no mined corpus at $CORPUS" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Everything the binary still needs. `--dynamic` because a title is a shared object and its
# imports live in the dynamic symbol table.
#
# Either reader will do and both are wanted: the container has `llvm-nm` and the WSL builder that
# actually builds titles has GNU `nm`. Picking whichever is present means this runs where the
# title is built rather than only where Mesa is.
if command -v llvm-nm >/dev/null 2>&1; then
    nm_cmd=llvm-nm
elif command -v nm >/dev/null 2>&1; then
    nm_cmd=nm
else
    echo "oops-mesa: no nm on PATH; cannot read the binary's imports" >&2
    exit 1
fi
"$nm_cmd" --dynamic --undefined-only "$elf" | awk '{print $NF}' | sort -u > "$work/undef.txt"

# The shared list already places some of them; those lines are reused rather than regenerated,
# so a title agrees with the rest of oops-apps about which spelling of a library to use.
: > "$work/out.txt"
: > "$work/unknown.txt"
if [ -f "$SHARED" ]; then
    cat "$SHARED" >> "$work/out.txt"
    awk '{print $2}' "$SHARED" | sort -u > "$work/placed.txt"
else
    : > "$work/placed.txt"
fi

# Which library spellings the shared list already uses, so a corpus entry offering both
# `libKernel` and `libkernel` picks the one this collection writes.
awk '{print $1}' "$work/out.txt" | sort -u > "$work/spellings.txt"

# The supplement, and why each line is more than a guess.
#
# The corpus is a mine of other people's name lists, so a name missing from it means nobody's
# list had it, not that the platform lacks it. Four names a Mesa title imports are in that
# position. They go here with their evidence, and a request is filed to replace the argument
# with a corpus entry. Nothing enters this list for convenience.
#
#   scePthreadAttrSetprio  oops-sdk binds and calls it today (src/thread/thread.c) and its
#                          payloads run, so libkernel is measured rather than inferred.
#   _ThreadRuneLocale      FreeBSD's per-thread locale object, reached by the ctype macros. Its
#                          neighbours _CurrentRuneLocale and the ctype functions are all placed
#                          in libSceLibcInternal by the corpus.
#   open_memstream         a C library function; every other stdio name is in libSceLibcInternal.
#   openlog                the same, for syslog.
supplement() {
    cat <<'SUPP'
libkernel scePthreadAttrSetprio
libSceLibcInternal _ThreadRuneLocale
libSceLibcInternal open_memstream
libSceLibcInternal openlog
SUPP
}
supplement >> "$work/out.txt"
supplement | awk '{print $2}' >> "$work/placed.txt"
sort -u -o "$work/placed.txt" "$work/placed.txt"

found=0
unknown=0
while read -r sym; do
    grep -qxF "$sym" "$work/placed.txt" && continue
    line=$(grep -m1 -E "^$sym " "$CORPUS" || true)
    if [ -z "$line" ]; then
        printf '%s\n' "$sym" >> "$work/unknown.txt"
        unknown=$((unknown + 1))
        continue
    fi
    libs=$(printf '%s' "$line" | awk '{print $2}')
    # A corpus row exists for every name the mining saw, but the library column is `-` when the
    # mining never learned which module exports it. That is not a placement. Writing it through
    # produces a manifest line like `- getline`, mkmodule builds a module claiming those symbols
    # resolve from a library named `-`, and the console kills the title with
    # PRX_NOT_RESOLVED_FUNCTION the moment one is called - which is what happened on
    # 2026-09-16, after this script had reported "0 unknown".
    if [ -z "$libs" ] || [ "$libs" = "-" ]; then
        printf '%s\n' "$sym" >> "$work/unknown.txt"
        unknown=$((unknown + 1))
        continue
    fi
    pick=""
    for lib in ${libs//,/ }; do
        if grep -qxF "$lib" "$work/spellings.txt"; then pick="$lib"; break; fi
    done
    [ -n "$pick" ] || pick=${libs%%,*}
    printf '%s %s\n' "$pick" "$sym" >> "$work/out.txt"
    found=$((found + 1))
done < "$work/undef.txt"

sort -u -o "$work/out.txt" "$work/out.txt"

printf 'oops-mesa: %s imports placed from the corpus, %s already in the shared list, %s unknown\n' \
    "$found" "$(wc -l < "$work/placed.txt" | tr -d ' ')" "$unknown"

# Checked before the manifest is written, not after. A refusal that still leaves the file behind
# is not a refusal: app.mk only asks whether it exists, so the next `make title` would pick up the
# incomplete one and package it.
if [ "$unknown" -gt 0 ]; then
    echo "  the corpus does not know these, and they need a library by hand:"
    sed 's/^/    /' "$work/unknown.txt" | head -30
    echo
    echo "  No manifest written. A title built from an incomplete one loads and then dies on the"
    echo "  first call to any of them, which is a worse failure than this one because it happens"
    echo "  on the console instead of here."
    rm -f "$out"
    exit 1
fi

cp "$work/out.txt" "$out"
