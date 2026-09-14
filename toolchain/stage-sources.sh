#!/usr/bin/env bash
# Stage the upstream libraries this build compiles for the target, out of the checkout D004 pins.
#
# Two so far, and both are here rather than in a script each because they differ only in which
# subtree they take:
#
#   libelf   radeonsi will not configure without it, and Mesa builds its runtime linker only
#            when it is found, so it is a real dependency rather than a feature.
#   libc++   Mesa is not a C project - its ACO shader backend is C++ - and the platform's own
#            C++ standard library cannot serve it. See D006: the platform exports 666 C++
#            symbols and not one of them is in libc++'s ABI namespace, because its library is a
#            different implementation with a different ABI.
#
# Both read the pinned checkout's object store with `git archive`, so neither writes to it, nor
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
rm -rf "$HERE/libelf-src" "$HERE/libcxx-src"

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

# FreeBSD's own generated configuration for libc++, which its headers refuse to compile without
# and every value in which is a platform decision rather than a default worth guessing.
for f in __config_site __assertion_handler; do
    git -C "$SRC" show "$PIN:lib/libc++/$f" > "$HERE/libcxx-src/include/$f"
done

# Manual pages are most of elftoolchain's file count and none of its build.
find "$HERE/libelf-src" -name '*.[0-9]' -delete

printf 'oops-mesa: staged libelf (%s C sources) and libc++ (%s C++ sources) from %s @ %s\n' \
    "$(find "$HERE/libelf-src/libelf" -name '*.c' | wc -l | tr -d ' ')" \
    "$(find "$HERE/libcxx-src" -name '*.cpp' | wc -l | tr -d ' ')" \
    "$SRC_NAME" "${PIN:0:12}"
