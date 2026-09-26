#!/usr/bin/env bash
# Write the import manifest a title linking Mesa needs, from obSCEne's mined corpus.
#
# A title's module names the library each imported symbol resolves from. oops-apps'
# hand-maintained `common/symbols.txt` covers freestanding titles; a title linking Mesa imports
# far more. This reads the linked binary's undefined symbols, places each from
# `data/mined-names.txt`, and reports any name the corpus cannot place rather than guessing.
# `obscene-tool mkmodule` reads the corpus relative to its working directory
# (`obscene/tool/src/main.rs`), which a title build is not in.
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

# The container has `llvm-nm` and the WSL title builder has GNU `nm`; either will do.
if command -v llvm-nm >/dev/null 2>&1; then
    nm_cmd=llvm-nm
elif command -v nm >/dev/null 2>&1; then
    nm_cmd=nm
else
    echo "oops-mesa: no nm on PATH; cannot read the binary's imports" >&2
    exit 1
fi
# Imports live in the dynamic symbol table, since a title is a shared object. Weak undefined
# symbols (`w`, or `v` for objects) are skipped: they resolve to zero when absent and the code
# checks, e.g. the Itanium thread-local initialiser `_ZTH23_mesa_glapi_tls_Context` that no
# library defines.
if ! "$nm_cmd" --dynamic --undefined-only "$elf" 2>"$work/nm.err" \
    | awk '$(NF-1) != "w" && $(NF-1) != "v" { print $NF }' | sort -u > "$work/undef.txt"; then
    echo "oops-mesa: $nm_cmd could not read $elf" >&2
    sed 's/^/    /' "$work/nm.err" >&2
    exit 1
fi

# An empty import list means the binary was not readable: a title linking Mesa imports at least
# `malloc`. `make title` rewrites `build/<app>.elf` in place into a module, which `nm` rejects
# with exit status 0, so the order is a fresh link, `make imports`, then `make title`.
if [ ! -s "$work/undef.txt" ]; then
    echo "oops-mesa: $elf has no undefined dynamic symbols, which is not possible for a title" >&2
    echo "  that links Mesa. Almost certainly this ran after 'make title' and the file is a" >&2
    echo "  fixed-up module rather than a linked ELF - $nm_cmd cannot read one." >&2
    if [ -s "$work/nm.err" ]; then
        echo "  $nm_cmd said:" >&2
        sed 's/^/    /' "$work/nm.err" >&2
    fi
    echo "  Relink first: rm -f the .elf, 'make elf', then 'make imports', then 'make title'." >&2
    rm -f "$out"
    exit 1
fi

# Names that must never be imports, checked before the corpus. These prefixes belong to Mesa
# and this project; an undefined one means an archive is missing from the link, and the corpus
# would place it in one of the platform's own GL libraries (e.g. `glGetString` in
# `libSceGLSlimServerVSH`), binding the title to the vendor's GL.
: > "$work/owned.txt"
grep -E '^(gl[A-Z]|_mesa_|dri[A-Z_]|radeonsi_|_eglInternal|ac_|aco_)' "$work/undef.txt" \
    > "$work/owned.txt" || true
if [ -s "$work/owned.txt" ]; then
    echo "oops-mesa: these are Mesa's own symbols and a title must never import them:" >&2
    sed 's/^/    /' "$work/owned.txt" >&2
    echo >&2
    echo "  An archive is missing from the link, not a library from the manifest. Placing one of" >&2
    echo "  these would bind a title to the platform's own GL - which the corpus will happily" >&2
    echo "  offer, and which is not the GL this project builds." >&2
    echo "  Check build/link-order.txt and whether the defining target was built at all;" >&2
    echo "  libglapi_bridge is not built by default upstream." >&2
    rm -f "$out"
    exit 1
fi

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

# Names the corpus lacks, each placed with its evidence. The corpus is mined from name lists, so
# a missing name means no list had it, not that the platform lacks it.
#
#   scePthreadAttrSetprio  oops-sdk binds and calls it (src/thread/thread.c) in payloads
#                          that run, so libkernel is measured.
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
    # A library column of `-` means the mining never learned the exporting module. That is not a
    # placement: a `- name` line makes the console kill the title with
    # PRX_NOT_RESOLVED_FUNCTION on the first call.
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

# Checked before the manifest is written: app.mk only asks whether the file exists, so a stale
# one would be packaged by the next `make title`.
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
