#!/usr/bin/env bash
# Configure and build Mesa for Prospero-generation hardware, inside the image from Dockerfile.
#
# `./bin/oops-mesa build` stages the sysroot, starts the container and calls this. The Mesa
# options, and why:
#
#   gallium-drivers=radeonsi   the one driver for this GPU (D001).
#   amd-use-llvm=false         the shader backend is ACO; no LLVM is installed or linked.
#   llvm=disabled              nothing else may pull it in either.
#   platforms=[]               no windowing system; presentation goes through the platform shim.
#   egl-native-platform=surfaceless   the one EGL platform that asks the system for nothing.
#   shader-cache=disabled      the cache is the largest file-system demand Mesa makes.
#   build-tests=false          the test suites run on the target, not in this build.
#   allow-fallback-for=libdrm  libdrm is carried, with the shim under its ioctls (D005).
#   --wrap-mode=nodownload     no other dependency arrives by fallback.
#
# A configure failure prints the tail of its log and exits with configure's status.
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

# libelf, built for the target from the pinned checkout's sources. radeonsi will not configure
# without it and its shader-disassembly path calls into it (D005). Three sources are generated
# from m4 templates, as FreeBSD's lib/libelf/Makefile generates them.
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


# libm, built for the target from FreeBSD's msun in the same pinned checkout. The platform's C
# library does not export `sin`, `floor`, `log` and their kin (D011); an import of one loads and
# is killed on the first call, so the arithmetic is compiled into the title.
#
# Double and float only: Mesa's GL and GLSL paths use no long double, and a reference to `sinl`
# is a link-time undefined symbol rather than a wrong-precision answer on the console. The
# `amd64/*.S` files are optional optimisations of C sources in `src/` and are not used. The
# include path and the two flags are msun's own, from `lib/msun/Makefile` and
# `lib/msun/amd64/Makefile.inc`.
LIBM_SRC="$HERE/msun-src"
LIBM_A="$HERE/sysroot/usr/lib/libm.a"
if [ ! -f "$LIBM_A" ]; then
    if [ ! -d "$LIBM_SRC/msun/src" ]; then
        echo "oops-mesa: libm sources not staged; run toolchain/stage-sources.sh first" >&2
        exit 1
    fi
    echo "oops-mesa: building libm for the target"
    work="$ROOT/build/libm"
    rm -rf "$work"; mkdir -p "$work"
    # `fenv.c` is architecture-specific and has no copy in src/. From `bsdsrc` only
    # `b_tgamma.c` is compiled: it includes `b_log.c` and `b_exp.c` as source, which do not
    # compile standalone.
    for c in "$LIBM_SRC"/msun/src/*.c "$LIBM_SRC"/msun/bsdsrc/b_tgamma.c \
             "$LIBM_SRC"/msun/amd64/fenv.c; do
        [ -f "$c" ] || continue
        # A long-double source is the `l.c` twin of a double one beside it (`s_ceill.c` next to
        # `s_ceil.c`). The twin test keeps `s_ceil.c`, `s_creal.c` and `s_isnormal.c`, whose
        # names end in `l.c` for another reason.
        name=$(basename "$c")
        case "$name" in
            *l.c)
                twin="${name%l.c}.c"
                [ -f "$(dirname "$c")/$twin" ] && continue
                ;;
        esac
        clang -target x86_64-unknown-freebsd --sysroot="$HERE/sysroot" \
              -I"$LIBM_SRC/msun/src" -I"$LIBM_SRC/msun/x86" -I"$LIBM_SRC/msun/amd64" \
              -I"$LIBM_SRC/libc/include" -I"$LIBM_SRC/libc/amd64" \
              -fno-math-errno -ffp-exception-behavior=maytrap \
              -fPIC -O2 -w -c "$c" -o "$work/$(basename "${c%.c}").o"
    done
    mkdir -p "$HERE/sysroot/usr/lib"
    llvm-ar rcs "$LIBM_A" "$work"/*.o
    echo "oops-mesa: libm.a built ($(llvm-ar t "$LIBM_A" | wc -l | tr -d ' ') objects)"
fi


# The C locale's character tables, built for the target from the same pinned checkout. One
# object answering `_DefaultRuneLocale` and `_CurrentRuneLocale`, which `ctype.h` reads inline
# and the platform does not bind natively. It also carries `__runes_for_locale`, which nothing
# calls; `libc_absent.c` defines its two libc-private dependencies as placeholders so the file
# compiles unmodified from upstream.
RUNE_SRC="$HERE/locale-src"
RUNE_A="$HERE/sysroot/usr/lib/librune.a"
if [ ! -f "$RUNE_A" ]; then
    if [ ! -f "$RUNE_SRC/locale/table.c" ]; then
        echo "oops-mesa: locale sources not staged; run toolchain/stage-sources.sh first" >&2
        exit 1
    fi
    echo "oops-mesa: building the C locale tables for the target"
    work="$ROOT/build/rune"
    rm -rf "$work"; mkdir -p "$work"
    clang -target x86_64-unknown-freebsd --sysroot="$HERE/sysroot" \
          -I"$RUNE_SRC/locale" -fPIC -O2 -w \
          -c "$RUNE_SRC/locale/table.c" -o "$work/table.o"
    mkdir -p "$HERE/sysroot/usr/lib"
    llvm-ar rcs "$RUNE_A" "$work/table.o"
    echo "oops-mesa: librune.a built ($(llvm-nm --defined-only "$RUNE_A" | grep -cE ' [TDRB] ') symbols)"
fi


# The compiler's own processor-feature object. AddressLib asks `__builtin_cpu_supports("avx2")`
# to pick a swizzle path, and that builtin reads `__cpu_model` from the compiler runtime. The
# object defines `__cpu_model`, `__cpu_features2` and `__cpu_indicator_init` with no undefined
# symbols - CPUID only - so the Linux build carries to this target; the check below enforces it.
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

# The probe archive, so meson's link probes answer honestly: an entry point plus one empty
# definition per symbol obSCEne's import census records present (stage-platform-stubs.sh).
# Compiler builtins link, measured platform functions link, and anything else reports absent,
# which sends Mesa to its portable fallback.
STUB="$HERE/sysroot/usr/lib/libplatform.a"
if [ ! -f "$STUB" ]; then
    [ -f "$HERE/platform-stubs.c" ] || { echo "oops-mesa: run toolchain/stage-platform-stubs.sh first" >&2; exit 1; }
    mkdir -p "$HERE/sysroot/usr/lib"
    clang -target x86_64-unknown-freebsd --sysroot="$HERE/sysroot" -fPIC -O0 -w -c \
        "$HERE/platform-stubs.c" -o "$ROOT/build/platform-stubs.o"
    llvm-ar rcs "$STUB" "$ROOT/build/platform-stubs.o"
    echo "oops-mesa: probe archive built from the import census"
fi

# libdrm, and nothing else, is fetched by name from the wrap Mesa pins by hash (D005).
# Configure then runs with downloads forbidden, so no second dependency arrives by fallback.
if [ ! -d "$MESA/subprojects/libdrm-2.4.133" ]; then
    echo "oops-mesa: fetching the pinned libdrm subproject"
    ( cd "$MESA" && meson subprojects download libdrm )
fi

# Patches apply after the subproject exists, because some are to it; `git -C mesa checkout .`
# reverts them. Plain `git apply`, not `--3way`: subproject files are ignored by Mesa's
# repository, so there is no index entry to merge from. An already-applied patch is skipped.
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

# A changed cross file needs the build directory gone, not just reconfigured: meson keeps
# `[built-in options]` (where `c_args` lives) from the first configure and silently ignores
# later edits. The stamp compares contents, because `build.ninja` is regenerated on every build
# and is nearly always newer than the cross file.
CROSS_STAMP="$BUILD/.cross-prospero.sha256"
cross_sha=$(sha256sum "$HERE/cross-prospero.ini" | awk '{print $1}')
if [ -f "$BUILD/build.ninja" ] && [ "$(cat "$CROSS_STAMP" 2>/dev/null)" != "$cross_sha" ]; then
    echo "oops-mesa: cross-prospero.ini does not match what this build directory was configured"
    echo "           against; meson would keep its cached options, so it is being reconfigured"
    echo "           from scratch."
    rm -rf "$BUILD"
fi

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

printf '%s\n' "$cross_sha" > "$CROSS_STAMP"

# The archives to build, asked of ninja rather than listed by hand: the inputs of Mesa's own DRI
# module link, the one target that gathers everything a driver needs. `all` is not used, since
# it also links the DRI shared object, which nothing here loads; every archive in the build is
# not used either, since that includes Google Test.
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

# Make libdrm's private symbols private. Mesa's `libgallium.a` and `libdrm_amdgpu.a` both define
# `handle_table_remove` and its family; libdrm marks its copies `drm_private` (hidden), which a
# static archive does not enforce. Each libdrm archive is partially linked into one relocatable
# object, so its cross-object references (`handle_table_insert`, `amdgpu_cs_calculate_timeout`)
# resolve inside it, and then `--localize-hidden` makes the hidden symbols local.
localized=0
for a in "${archives[@]}"; do
    case "$a" in
        *libdrm*) ;;
        *) continue ;;
    esac
    combined="$ROOT/build/$(basename "${a%.a}")-combined.o"
    ld.lld -r --whole-archive "$BUILD/$a" -o "$combined"
    llvm-objcopy --localize-hidden "$combined"
    rm -f "$BUILD/$a"
    llvm-ar rcs "$BUILD/$a" "$combined"
    localized=$((localized + 1))
done

# Prove no duplicate symbol remains by attempting Mesa's DRI link: only the linker knows which
# members get pulled, and a name defined in several archives is harmless unless two pulled
# objects define it. Undefined symbols are the platform C library's, resolved at load and
# accounted for by `tools/what-is-still-needed.sh`.
echo "oops-mesa: checking a full link for duplicate symbols"
linklog="$ROOT/build/link-check.log"
ninja -C "$BUILD" "$dri_so" > "$linklog" 2>&1 || true
if grep -q "duplicate symbol" "$linklog"; then
    echo "oops-mesa: a title linking these archives would meet a duplicate symbol:" >&2
    grep -A3 "duplicate symbol" "$linklog" | head -12 >&2
    exit 1
fi
undef=$(grep -c "undefined symbol" "$linklog" || true)
echo "oops-mesa: no duplicate symbols; $undef undefined, which is the platform's to answer"

# The C++ half of the shim, as an archive. It touches no oops-sdk header, only libc++'s, and a
# title's `-std=c11` would refuse it (D006). It holds `cxx_support.cpp` plus upstream libc++
# sources, each compiled separately and kept if it builds; `stdexcept.cpp` supplies the
# exception constructors with libc++'s own reference-counted string.
LIBCXX_SRC="$HERE/libcxx-src"
CXXSHIM_A="$HERE/sysroot/usr/lib/liboopsmesa_cxx.a"
if [ -f "$ROOT/src/runtime/cxx_support.cpp" ]; then
    work="$ROOT/build/cxxshim"
    rm -rf "$work"; mkdir -p "$work"
    cxxflags="-target x86_64-unknown-freebsd --sysroot=$HERE/sysroot -stdlib=libc++ -fPIC -O2"

    # `-fno-exceptions` on ours, as Mesa itself is built: with exceptions on, the object imports
    # `__cxa_begin_catch` and `__gxx_personality_v0` from a platform library that may not export
    # them. Upstream's `stdexcept.cpp` keeps exceptions, since throwing is its purpose.
    clang++ $cxxflags -std=c++17 -fno-exceptions -c "$ROOT/src/runtime/cxx_support.cpp" \
            -o "$work/cxx_support.o"

    # `-I src` is required: without it `stdexcept.cpp` compiles to the `what()` accessors and
    # silently omits every constructor.
    upstream_ok=0
    for c in "$LIBCXX_SRC"/src/stdexcept.cpp; do
        [ -f "$c" ] || continue
        if clang++ $cxxflags -std=c++23 -w -D_LIBCPP_BUILDING_LIBRARY \
                   -I"$LIBCXX_SRC/src" -I"$LIBCXX_SRC/rt" \
                   -c "$c" -o "$work/upstream_$(basename "${c%.cpp}").o" 2>/dev/null; then
            upstream_ok=$((upstream_ok + 1))
        fi
    done

    mkdir -p "$HERE/sysroot/usr/lib"
    rm -f "$CXXSHIM_A"
    llvm-ar rcs "$CXXSHIM_A" "$work"/*.o
    echo "oops-mesa: C++ support archive built (ours plus $upstream_ok upstream source)"
fi

# The public GL entry points. `libglapi.a` carries only the hidden dispatch machinery; the `gl*`
# names live in `libglapi_bridge`, which upstream builds only for libGL (`build_by_default :
# false`). A title links with unresolved symbols ignored, so without this archive `glGetString`
# becomes an import that resolves to one of the platform's own GL libraries.
echo "oops-mesa: building the public GL entry points (libglapi_bridge)"
ninja -C "$BUILD" src/mesa/glapi/glapi/libglapi_bridge.a >/dev/null 2>&1 || {
    echo "oops-mesa: libglapi_bridge would not build; a title cannot call GL without it" >&2
    exit 1
}

# The link order for a title, taken from Mesa's DRI link line: objects first, then archives.
# The DRI target's object defines every `*_driver_descriptor` (`drm_helper.h`) and references
# `radeonsi_screen_create`, so it must precede `libradeonsi.a`. Paths are relative to this
# repository, because the container mounts it at /w; `oops-mesa.mk` prefixes its own directory.
{
    ninja -C "$BUILD" -t commands "$dri_so" 2>/dev/null | tail -1 \
        | tr ' ' '\n' | sed -n 's/^\(.*\.o\)$/build\/mesa\/\1/p'
    ninja -C "$BUILD" -t commands "$dri_so" 2>/dev/null | tail -1 \
        | tr ' ' '\n' | sed -n 's/^\(.*\.a\)$/build\/mesa\/\1/p'
} > "$ROOT/build/link-order.txt"

echo "oops-mesa: built ${#archives[@]} archives for x86_64-unknown-freebsd"
echo "oops-mesa: $localized libdrm archives had their private symbols localized; no duplicates remain"
echo "oops-mesa: link order for a title written to build/link-order.txt ($(wc -l < "$ROOT/build/link-order.txt" | tr -d ' ') archives)"
