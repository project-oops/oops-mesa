#!/usr/bin/env bash
# Stage the upstream library sources this build compiles for the target, out of the checkout
# D004 pins. Each differs only in the subtree taken:
#
#   libelf   radeonsi will not configure without it (D005).
#   libc++   ACO is C++, and the platform's C++ library has a different ABI (D006).
#   libm     the platform's C library does not export the arithmetic Mesa calls (D011); this is
#            the implementation matching the headers Mesa compiles against.
#   locale   the C locale's character tables (below).
#
# Everything is read with `git archive`, so the checkout, its sparse-checkout configuration and
# orbistoun's constant harvest are untouched. This only extracts; build-mesa.sh compiles in the
# container.
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

# Clear the staging roots outright, so a subtree that stops being taken does not linger.
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
# The C++ runtime under libc++ is libcxxrt, not libc++abi; FreeBSD builds the two into one
# library, and so does this build.
take contrib/libcxxrt                     libcxx-src/rt

# libm, and the libc header directories on msun's own include path (`lib/msun/Makefile`):
# `fpmath.h` and the architecture's `_fpmath.h`.
take lib/msun          msun-src/msun
take lib/libc/include  msun-src/libc/include
take lib/libc/amd64    msun-src/libc/amd64

# The C locale's character tables. `ctype.h` inlines `__getCurrentRuneLocale()`, so `tolower`
# in Mesa (`ac_gpu_info.c`, on the startup path) references `_CurrentRuneLocale` directly, and
# the native platform libc does not export it. `lib/libc/locale/table.c` supplies it at the
# revision the headers come from.
take lib/libc/locale   locale-src/locale

# FreeBSD's generated libc++ configuration, which the headers require; its values are platform
# decisions, not defaults.
for f in __config_site __assertion_handler; do
    git -C "$SRC" show "$PIN:lib/libc++/$f" > "$HERE/libcxx-src/include/$f"
done

# Manual pages are no part of the build.
find "$HERE/libelf-src" "$HERE/msun-src" -name '*.[0-9]' -delete

printf 'oops-mesa: staged libelf (%s C), libc++ (%s C++) and libm (%s C) from %s @ %s\n' \
    "$(find "$HERE/libelf-src/libelf" -name '*.c' | wc -l | tr -d ' ')" \
    "$(find "$HERE/libcxx-src" -name '*.cpp' | wc -l | tr -d ' ')" \
    "$(find "$HERE/msun-src/msun/src" -name '*.c' | wc -l | tr -d ' ')" \
    "$SRC_NAME" "${PIN:0:12}"
