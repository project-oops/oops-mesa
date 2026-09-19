#!/usr/bin/env bash
# Stage the upstream libraries this build compiles for the target, out of the checkout D004 pins.
#
# Three so far, and all are here rather than in a script each because they differ only in which
# subtree they take:
#
#   libelf   radeonsi will not configure without it, and Mesa builds its runtime linker only
#            when it is found, so it is a real dependency rather than a feature.
#   libc++   Mesa is not a C project - its ACO shader backend is C++ - and the platform's own
#            C++ standard library cannot serve it. See D006: the platform exports 666 C++
#            symbols and not one of them is in libc++'s ABI namespace, because its library is a
#            different implementation with a different ABI.
#   libm     the platform's C library does not export the arithmetic Mesa calls. obSCEne swept
#            139 candidate imports on firmware 12.40 and `sin`, `cos`, `floor`, `log`, `log10`,
#            `atan2`, `powf`, `round`, `trunc` and their float twins all came back absent, from
#            both the export census and a dynamic lookup. A title that imports one of those does
#            not fail to link - it loads, and the console kills it on the first call. So the
#            arithmetic has to be in the title, and this is the implementation that matches the
#            headers Mesa is already compiled against, at the revision they already come from,
#            which is the whole reason for taking it from here rather than writing it
#            (obscene REQ-20260917T1640Z-5b28).
#
# All three read the pinned checkout's object store with `git archive`, so none writes to it, nor
# changes its sparse-checkout configuration, nor can disturb orbistoun's constant harvest.
#
# This only extracts. The compiling happens in the container, from build-mesa.sh, because the
# cross compiler lives there and this checkout does not.
set -eu

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)

PIN=$(sed -n 's/^SYSROOT_PIN *:= *//p' "$ROOT/dependencies.mk")
SRC_NAME=$(sed -n 's/^SYSROOT_SOURCE *:= *//p' "$ROOT/dependencies.mk")
SRC="${OOPS_MESA_FREEBSD_SRC:-$ROOT/../../$SRC_NAME}"

if [ ! -d "$SRC/.git" ]; then
    echo "oops-mesa: no $SRC_NAME checkout at $SRC" >&2
    exit 1
fi
actual=$(git -C "$SRC" rev-parse HEAD)
if [ "$actual" != "$PIN" ]; then
    echo "oops-mesa: $SRC_NAME is at $actual, dependencies.mk pins $PIN" >&2
    exit 1
fi

# Clear the staging roots outright rather than per subtree, so a subtree that stops being taken
# does not linger. One did: an earlier run took libc++abi before it turned out this platform uses
# libcxxrt instead, and the empty directory survived the change.
rm -rf "$HERE/libelf-src" "$HERE/libcxx-src" "$HERE/msun-src" "$HERE/locale-src"

# take <tree path> <destination under toolchain/>
take() {
    local from="$1" to="$2" strip
    strip=$(printf '%s' "$from" | awk -F/ '{print NF}')
    rm -rf "$HERE/$to"
    mkdir -p "$HERE/$to"
    git -C "$SRC" archive "$PIN" "$from" | tar -x -C "$HERE/$to" --strip-components="$strip"
}

take contrib/elftoolchain/libelf  libelf-src/libelf
take contrib/elftoolchain/common  libelf-src/common
take contrib/llvm-project/libcxx/src      libcxx-src/src
take contrib/llvm-project/libcxx/include  libcxx-src/include
# The C++ runtime underneath libc++ - exception unwinding, dynamic casts, guard variables - is
# libcxxrt here, not libc++abi. FreeBSD builds the two into one library and so does this, which
# is why there is no separate abi archive below.
take contrib/libcxxrt                     libcxx-src/rt

# libm, and the two libc header directories msun's sources include: `lib/libc/include` carries
# `fpmath.h`, and `lib/libc/amd64` the `_fpmath.h` beside it that describes this architecture's
# floating-point layout. Both are on msun's own include path rather than chosen here
# (`lib/msun/Makefile`, the CFLAGS that add `${LIBC_SRCTOP}/include` and `${LIBC_ARCH}`).
take lib/msun          msun-src/msun
take lib/libc/include  msun-src/libc/include
take lib/libc/amd64    msun-src/libc/amd64

# The C locale's character tables, for the same reason and from the same place.
#
# `ctype.h` on this platform does not call into the C library for `tolower` and its kin. It
# inlines `__getCurrentRuneLocale()`, which reads a global `_RuneLocale` table - so a title that
# calls `tolower` references `_CurrentRuneLocale` directly, and Mesa does: `ac_gpu_info.c` is on
# the startup path and is one of four objects that reach it.
#
# That symbol is **not bindable** on the native leg. obSCEne settled it in
# REQ-20260917T1818Z-9f41: the export census that lists it was captured under GEN=4, the PS4
# backward-compatibility container, and Sony's native Prospero libc dropped those exports. So the
# table has to be in the title, and `lib/libc/locale/table.c` is the authentic one at the
# revision the headers already come from.
take lib/libc/locale   locale-src/locale

# FreeBSD's own generated configuration for libc++, which its headers refuse to compile without
# and every value in which is a platform decision rather than a default worth guessing.
for f in __config_site __assertion_handler; do
    git -C "$SRC" show "$PIN:lib/libc++/$f" > "$HERE/libcxx-src/include/$f"
done

# Manual pages are most of elftoolchain's file count and none of its build. msun's `man/` is the
# same shape and the same nuisance, so it goes the same way.
find "$HERE/libelf-src" "$HERE/msun-src" -name '*.[0-9]' -delete

printf 'oops-mesa: staged libelf (%s C), libc++ (%s C++) and libm (%s C) from %s @ %s\n' \
    "$(find "$HERE/libelf-src/libelf" -name '*.c' | wc -l | tr -d ' ')" \
    "$(find "$HERE/libcxx-src" -name '*.cpp' | wc -l | tr -d ' ')" \
    "$(find "$HERE/msun-src/msun/src" -name '*.c' | wc -l | tr -d ' ')" \
    "$SRC_NAME" "${PIN:0:12}"
