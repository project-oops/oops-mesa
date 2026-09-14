#!/usr/bin/env bash
# Configure and build Mesa for Prospero-generation hardware, inside the image from Dockerfile.
#
# Run it through the verb rather than directly: `./bin/oops-mesa build` stages the sysroot,
# starts the container and calls this.
#
# What this asks Mesa for, and why each one:
#
#   gallium-drivers=radeonsi   the driver D003's measurement opened. Nothing else is built.
#   amd-use-llvm=false         the shader backend is ACO, so no LLVM is installed or linked.
#   llvm=disabled              nothing else may pull it in either.
#   platforms=[]               there is no windowing system. Presentation is oops-sdk's display,
#                              reached through this repository's platform shim.
#   egl-native-platform=surfaceless   the only EGL platform that asks the system for nothing.
#   default_library=static     a title links archives; there is no loader here for a .so.
#   shader-cache=disabled      the cache is the largest file-system demand Mesa makes. Off for
#                              now so unit 3's error list is about the C runtime rather than
#                              about a feature we have not decided to carry.
#   build-tests=false          the test suites need to run on the target, which is unit 8.
#
#   allow-fallback-for=libdrm  libdrm is carried rather than replaced (D005): the shim sits under
#                              it and answers its ioctls, so Mesa builds upstream libdrm from the
#                              wrap Mesa itself pins by hash.
#   --wrap-mode=nodownload     and nothing else may arrive that way. libdrm is fetched by name
#                              in a step above; a second dependency appearing by fallback would
#                              be a decision made by accident rather than written down.
#
# The point of this script for roadmap unit 3 is not a finished build. It is to reach Mesa's
# first real complaint against this sysroot and this shim, because that complaint is unit 4's
# work list (D002). It therefore keeps going past a configure failure only to report it.
set -eu

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
MESA="$ROOT/mesa"
BUILD="$ROOT/build/mesa"
LOG="$ROOT/build/mesa-configure.log"

if [ ! -f "$MESA/meson.build" ]; then
    echo "oops-mesa: mesa/ is empty; run: git submodule update --init --depth 1" >&2
    exit 1
fi
if [ ! -d "$HERE/sysroot/usr/include" ]; then
    echo "oops-mesa: no staged sysroot; run toolchain/stage-sysroot.sh first" >&2
    exit 1
fi

mkdir -p "$ROOT/build"

# Patches are applied to the submodule in place and reverted by `git -C mesa checkout .`;
# each one carries a header saying why it could not be a shim (CLAUDE.md, principle 1).
# libelf, built for the target from the sources stage-libelf.sh took out of the pinned checkout.
# radeonsi will not configure without it and its shader-disassembly path calls into it, so it is
# a real dependency rather than a feature (D005). Three of its sources are generated from m4
# templates, exactly as FreeBSD's own lib/libelf/Makefile generates them.
LIBELF_SRC="$HERE/libelf-src"
LIBELF_A="$HERE/sysroot/usr/lib/libelf.a"
if [ ! -f "$LIBELF_A" ]; then
    if [ ! -d "$LIBELF_SRC/libelf" ]; then
        echo "oops-mesa: libelf sources not staged; run toolchain/stage-libelf.sh first" >&2
        exit 1
    fi
    echo "oops-mesa: building libelf for the target"
    work="$ROOT/build/libelf"
    rm -rf "$work"; mkdir -p "$work"
    for gen in convert fsize msize; do
        m4 -D SRCDIR="$LIBELF_SRC/libelf" \
           "$LIBELF_SRC/libelf/elf_types.m4" "$LIBELF_SRC/libelf/libelf_$gen.m4" \
           > "$work/libelf_$gen.c"
    done
    # The library includes <sys/elf32.h> and friends, which are in the staged sysroot already.
    for c in "$LIBELF_SRC"/libelf/*.c "$work"/libelf_*.c; do
        clang -target x86_64-unknown-freebsd --sysroot="$HERE/sysroot" \
              -I"$LIBELF_SRC/libelf" -I"$LIBELF_SRC/common" -I"$work" \
              -fPIC -O2 -w -c "$c" -o "$work/$(basename "${c%.c}").o"
    done
    mkdir -p "$HERE/sysroot/usr/lib"
    llvm-ar rcs "$LIBELF_A" "$work"/*.o
    cp "$LIBELF_SRC/libelf/libelf.h" "$LIBELF_SRC/libelf/gelf.h" "$HERE/sysroot/usr/include/"
    echo "oops-mesa: libelf.a built ($(llvm-ar t "$LIBELF_A" | wc -l | tr -d ' ') objects)"
fi


# The compiler's own processor-feature object.
#
# AddressLib asks `__builtin_cpu_supports("avx2")` once, to pick between two swizzle paths that
# compute the same answers at different speeds. That builtin reads `__cpu_model`, which the
# compiler's runtime library defines and this target has no copy of.
#
# The three options were: answer false and lose the fast path, fake the structure and guess at a
# layout that belongs to the compiler, or use the compiler's own. The third is right and is safe
# to do across operating systems, which was checked rather than assumed: the object is 11 KB,
# defines exactly `__cpu_model`, `__cpu_features2` and `__cpu_indicator_init`, and has **no
# undefined symbols at all**. It is CPUID and nothing else, so there is no Linux in it to carry
# into a FreeBSD target.
CPUMODEL_A="$HERE/sysroot/usr/lib/libcpu_model.a"
if [ ! -f "$CPUMODEL_A" ]; then
    builtins=$(ls /usr/lib/llvm-*/lib/clang/*/lib/linux/libclang_rt.builtins-x86_64.a 2>/dev/null | head -1)
    if [ -z "$builtins" ]; then
        echo "oops-mesa: no compiler builtins archive in the image; cannot provide __cpu_model" >&2
        exit 1
    fi
    work="$ROOT/build/cpumodel"
    rm -rf "$work"; mkdir -p "$work"
    ( cd "$work" && llvm-ar x "$builtins" x86.c.o )
    if [ -n "$(llvm-nm --undefined-only "$work/x86.c.o" 2>/dev/null)" ]; then
        echo "oops-mesa: the processor-feature object gained a dependency; it is no longer" >&2
        echo "  safe to carry across operating systems. Re-check before shipping it." >&2
        exit 1
    fi
    mkdir -p "$HERE/sysroot/usr/lib"
    llvm-ar rcs "$CPUMODEL_A" "$work/x86.c.o"
    echo "oops-mesa: processor-feature object taken from the compiler's own runtime"
fi

# A one-symbol archive so link probes can answer honestly.
#
# meson answers "does this function exist" by linking a program that takes its address. With no
# libraries in the sysroot every such probe fails for the one reason that is not the question,
# and the first version of this build silenced that with `--unresolved-symbols=ignore-all`.
# That made every probe succeed instead, which is worse: Mesa concluded clang had GCC's
# `__builtin_add_overflow_p` and ARM's FPSCR builtins on an x86-64 target, and emitted code
# using them. A false yes is a compile error a thousand objects later; a false no is Mesa
# choosing its own portable fallback.
#
# So the linker gets an entry point and nothing else. A probe for something the compiler really
# has links, because those builtins are inlined; a probe for something absent does not resolve
# and is reported absent. Functions the platform provides at run time also report absent, which
# is the safe direction and is why nothing here claims to have measured them.
STUB="$HERE/sysroot/usr/lib/libplatform.a"
if [ ! -f "$STUB" ]; then
    [ -f "$HERE/platform-stubs.c" ] || { echo "oops-mesa: run toolchain/stage-platform-stubs.sh first" >&2; exit 1; }
    mkdir -p "$HERE/sysroot/usr/lib"
    clang -target x86_64-unknown-freebsd --sysroot="$HERE/sysroot" -fPIC -O0 -w -c \
        "$HERE/platform-stubs.c" -o "$ROOT/build/platform-stubs.o"
    llvm-ar rcs "$STUB" "$ROOT/build/platform-stubs.o"
    echo "oops-mesa: probe archive built from the import census"
fi

# libdrm, and nothing else, is fetched - by name, from the wrap Mesa itself pins by hash
# (D005). Configure then runs with downloads forbidden, so a second dependency cannot arrive by
# falling back: that would be a decision made by accident rather than written down.
if [ ! -d "$MESA/subprojects/libdrm-2.4.133" ]; then
    echo "oops-mesa: fetching the pinned libdrm subproject"
    ( cd "$MESA" && meson subprojects download libdrm )
fi

# Patches are applied after the subproject exists, because some of them are to it. Each carries a
# header saying what it changes and why it could not be a shim (CLAUDE.md, principle 1). Plain
# `git apply` rather than `--3way`: the files a patch touches inside a subproject are ignored by
# Mesa's own repository, so there is no index entry for a three-way merge to work from.
# Re-applying is a no-op rather than an error, so a rebuild on a tree that is already patched
# behaves the same as one on a fresh tree.
shopt -s nullglob
for p in "$ROOT"/patches/*.patch; do
    name=$(basename "$p")
    if git -C "$MESA" apply --check --reverse "$p" >/dev/null 2>&1; then
        echo "oops-mesa: $name is already applied"
    elif git -C "$MESA" apply "$p"; then
        echo "oops-mesa: applied $name"
    else
        echo "oops-mesa: $name does not apply to this tree" >&2
        exit 1
    fi
done
shopt -u nullglob

echo "oops-mesa: configuring Mesa for x86_64-unknown-freebsd (log: build/mesa-configure.log)"
set +e
meson setup "$BUILD" "$MESA" \
    --cross-file "$HERE/cross-prospero.ini" \
    --prefix /usr \
    --buildtype release \
    --wrap-mode=nodownload \
    -Dallow-fallback-for=libdrm \
    -Dlibdrm:tests=false \
    -Dlibdrm:man-pages=disabled \
    -Dgallium-drivers=radeonsi \
    -Dvulkan-drivers= \
    -Damd-use-llvm=false \
    -Dllvm=disabled \
    -Dplatforms= \
    -Degl=enabled \
    -Degl-native-platform=surfaceless \
    -Dglx=disabled \
    -Dgbm=disabled \
    -Dopengl=true \
    -Dgles1=disabled \
    -Dgles2=disabled \
    -Dshared-glapi=disabled \
    -Dshader-cache=disabled \
    -Dxmlconfig=disabled \
    -Dzlib=disabled \
    -Dzstd=disabled \
    -Dlibunwind=disabled \
    -Dvalgrind=disabled \
    -Dvideo-codecs= \
    -Dbuild-tests=false \
    > "$LOG" 2>&1
rc=$?
set -e

if [ "$rc" -ne 0 ]; then
    echo "oops-mesa: configure failed. The tail of the log, which is the finding:" >&2
    tail -40 "$LOG" >&2
    exit "$rc"
fi

echo "oops-mesa: configured. Building."

# Every static archive the configuration produces, asked of ninja rather than listed here.
#
# A hand-written list goes stale silently, and did: it omitted libdrm's core archive, Mesa's
# dispatch table, its C11 threads layer and its hash library, so the ioctl seam was never
# compiled and `tools/what-is-still-needed.sh` reported hundreds of symbols as missing that were
# merely never built. Asking the build what it can produce cannot drift from what the build
# produces.
#
# `all` is deliberately not used: it also links `libgallium-<version>.so`, Mesa's DRI module,
# which nothing here consumes - a title links archives, and there is no loader on this platform
# that would resolve a Mesa shared object. That link also fails today on a real collision worth
# recording rather than hiding: Mesa's `libgallium.a` and libdrm's `libdrm_amdgpu.a` both define
# `handle_table_remove`, and a whole-archive link sees both. Unit 7 has to settle it before it
# packages an SDK, because a title linking both archives meets the same collision.
# The list comes from the link line of Mesa's own DRI module. That target is the one thing in
# this configuration that pulls together everything a driver needs, so its inputs are the answer
# to "what does a title link" without anybody maintaining a list. Taking every archive in the
# build instead was tried and is wrong: it drags in Google Test, which this configuration does
# not build and which has no business in a title.
dri_so=$(ninja -C "$BUILD" -t targets all 2>/dev/null \
         | sed -n 's/^\(src\/gallium\/targets\/dri\/libgallium[^:]*\.so\): .*/\1/p' | head -1)
if [ -z "$dri_so" ]; then
    echo "oops-mesa: no DRI module in the build graph; cannot derive the archive list" >&2
    exit 1
fi
mapfile -t archives < <(ninja -C "$BUILD" -t commands "$dri_so" 2>/dev/null | tail -1 \
                        | tr ' ' '\n' | sed -n 's/^\(.*\.a\)$/\1/p' | sort -u)
if [ "${#archives[@]}" -eq 0 ]; then
    echo "oops-mesa: the DRI module links no archives, which cannot be right" >&2
    exit 1
fi
ninja -C "$BUILD" "${archives[@]}"

echo "oops-mesa: built ${#archives[@]} archives for x86_64-unknown-freebsd"
