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


# libm, built for the target from FreeBSD's own msun, out of the same pinned checkout.
#
# # Why the arithmetic has to be in the title
#
# The platform's C library does not export it. obSCEne swept 139 candidate imports on firmware
# 12.40 and the maths came back absent twice over - absent from the export census and absent from
# a dynamic lookup - for `sin`, `cos`, `floor`, `ceil`, `log`, `log10`, `atan2`, `powf`, `round`,
# `trunc` and the float twins (REQ-20260917T1640Z-5b28). That failure is not a link error: the
# import is placed from a mined corpus, the module loads, and the console kills the title on the
# first call. So these are compiled in, and a name that is genuinely exported is still imported
# the ordinary way.
#
# # Double and float only, on purpose
#
# Long double is not built: no `ld80`, and every `*l.c` is skipped. Mesa's GL and GLSL paths are
# `float` and `double` throughout, so the 63 long-double sources would be dead weight carrying
# their own header tree. The consequence is chosen rather than accepted - a reference to `sinl`
# becomes an undefined symbol at link time here, which is a diagnostic at this desk, instead of
# a plausible answer computed at the wrong precision on the console.
#
# The `.S` files under `amd64/` are deliberately not used either. They are optimisations of
# functions that all have C implementations in `src/`, and FreeBSD's own build makes them
# conditional on MK_MACHDEP_OPTIMIZATIONS for exactly that reason. C everywhere is one fewer
# assembler in the loop for arithmetic that is not the bottleneck.
#
# The include path and the two flags are msun's own, from `lib/msun/Makefile` and
# `lib/msun/amd64/Makefile.inc`, not chosen here.
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
    # `fenv.c` is architecture-specific and has no copy in src/, which is why amd64/ is on the
    # source list as well as the include path.
    #
    # `bsdsrc` contributes exactly one object and is named file by file rather than globbed:
    # `b_tgamma.c` **#includes** `b_log.c` and `b_exp.c` as source, so compiling that directory
    # wholesale builds the two of them a second time, standalone, without the definitions the
    # including file provides. That fails on `copysign`, `ldexp` and `isfinite` being undeclared,
    # which is what the first run of this did.
    for c in "$LIBM_SRC"/msun/src/*.c "$LIBM_SRC"/msun/bsdsrc/b_tgamma.c \
             "$LIBM_SRC"/msun/amd64/fenv.c; do
        [ -f "$c" ] || continue
        # Long double, not built - see above. The test is not the filename ending in `l.c` on its
        # own, because three of these end that way for a different reason: the *function* is
        # called `ceil`, `creal` or `isnormal`. A long-double source is always the twin of a
        # double one beside it - `s_ceill.c` next to `s_ceil.c` - so the twin has to exist for
        # the exclusion to hold. Naming alone dropped `ceil` and the whole `__isfinite` /
        # `__isnormal` family on the first run of this, which mattered: those five are among the
        # names the export census and the sweep disagree about, and having them locally settles
        # the question instead of betting on it.
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


# The C locale's character tables, built for the target from the same pinned checkout.
#
# One object, and it answers two symbols this platform will not bind: `_DefaultRuneLocale` and
# `_CurrentRuneLocale`. See `stage-sources.sh` for why they are needed at all - `ctype.h` inlines
# the table lookup, so `tolower` in Mesa reaches a global here rather than a call into libc, and
# `REQ-20260917T1818Z-9f41` established that the census listing them was a GEN=4 capture and they
# are not bindable natively.
#
# It also carries `__runes_for_locale`, which nothing in this link calls - measured, zero
# references across every Mesa archive and every staged one. Its two libc-private dependencies
# are therefore dead, and `libc_absent.c` defines them as the placeholders they are, with a note
# saying so. Compiling the file whole rather than carving the function out keeps it identical to
# upstream's, which is the property that makes staging it worth anything.
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

# A changed cross file needs the build directory gone, not just reconfigured.
#
# meson reads `[built-in options]` - which is where `c_args` lives - when it first configures a
# build directory, and keeps them in its own coredata afterwards. A later edit to the cross file
# is then *silently ignored*: ninja regenerates, every target rebuilds, the build succeeds, and
# the compile lines still carry the old flags. That is what happened when `-DNO_REGEX` was added
# on 2026-09-17 - 1,171 targets recompiled and `build.ninja` contained no mention of it, while
# `-DOOPS_MESA_WINSYS` from the same line was present because it had been there at first setup.
#
# A build that ignores its own configuration and reports success is worse than one that fails, so
# this starts over whenever the cross file's *contents* differ from what the build directory was
# configured against.
#
# Contents rather than timestamps: `build.ninja` is regenerated on every build, so it is almost
# always newer than the cross file and a `-nt` test never fires. That was tried first and is why
# this comment names the trap.
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

# Record what this build directory was configured against, so the check above can tell.
printf '%s\n' "$cross_sha" > "$CROSS_STAMP"

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

# Make libdrm's private symbols actually private.
#
# Mesa's `libgallium.a` and libdrm's `libdrm_amdgpu.a` both define `handle_table_remove` and its
# family, and a title linking both meets a duplicate symbol. Upstream does not have this problem
# because libdrm is normally a shared library: its own headers mark these `drm_private`, which is
# `visibility("hidden")`, and a shared library never exports them. A static archive keeps them
# global at link time, so the intent is stated and not enforced.
#
# `--localize-hidden` enforces it: every symbol already marked hidden becomes local. That is
# upstream's own declaration applied, not a renaming or an override, and it needs no patch. A
# symbol libdrm meant to publish is unaffected, because it was never marked hidden.
# It has to be a partial link first, and that is not a detail.
#
# Running `--localize-hidden` over the archive in place was tried and is wrong: it localizes
# per object, so libdrm's own cross-object references break. `amdgpu_bo.c` calls
# `handle_table_insert` in `handle_table.c` and `amdgpu_cs_calculate_timeout` in `amdgpu_cs.c`,
# both hidden, and making them local to their own objects leaves those calls undefined.
#
# So the objects are combined into one relocatable object first. References between them resolve
# inside it, and only then are the hidden symbols localized - which is now safe, because nothing
# outside is entitled to them. That is what partial linking is for.
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

# Prove the collision is gone, by linking rather than by counting.
#
# Counting symbols defined in more than one archive was tried and is wrong: that is normal and
# harmless, because the linker pulls at most one definition out of a group. radeonsi alone
# defines its tracepoints in eleven per-generation archives. A duplicate is only an error when
# two objects that are both pulled in define the same name, and the only thing that knows which
# objects get pulled in is the linker.
#
# So this attempts Mesa's own DRI link, which pulls in everything a driver needs, and looks at
# the errors. Undefined symbols are expected and fine: they are the platform's C library, which
# a title resolves at load and which `tools/what-is-still-needed.sh` accounts for. A duplicate
# symbol is not fine, and fails here.
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

# The C++ half of the shim, as an archive.
#
# The other shim sources compile with the title, so they bind to whichever oops-sdk that title
# was built against and a stale one cannot be linked by accident. This file is different: it
# touches no oops-sdk header, only libc++'s, which belong to this repository. Building it here
# also keeps it away from the title's `-std=c11`, which would refuse it.
#
# It carries two things: this repository's own definitions, and the upstream libc++ sources that
# do compile with this compiler. `stdexcept.cpp` is one of them, and it provides the exception
# constructors properly - with libc++'s own reference-counted string behind them - where the
# first version of this file had hand-written stubs that could not construct that member.
#
# Most of libc++ still does not compile here, for the reason D006 gives: its headers are LLVM 21
# and the compiler is clang 18. Taking the files that do compile is not a change to that
# decision, it is the same decision applied more carefully. Building each separately and keeping
# what succeeds means a compiler upgrade later simply yields more of them.
LIBCXX_SRC="$HERE/libcxx-src"
CXXSHIM_A="$HERE/sysroot/usr/lib/liboopsmesa_cxx.a"
if [ -f "$ROOT/src/runtime/cxx_support.cpp" ]; then
    work="$ROOT/build/cxxshim"
    rm -rf "$work"; mkdir -p "$work"
    cxxflags="-target x86_64-unknown-freebsd --sysroot=$HERE/sysroot -stdlib=libc++ -fPIC -O2"

    # `-fno-exceptions` on ours only, and it is a correctness fix rather than a size one.
    #
    # `cxx_support.cpp` already states in its own comments that it does not throw, because Mesa is
    # built with exceptions off - 139 times over in its own ninja file. But it was being compiled
    # *with* them, so clang emitted the cleanup machinery anyway and the object carried undefined
    # references to `__cxa_begin_catch` and `__gxx_personality_v0`. Those two reached the title's
    # import table, where the corpus placed them in `libSceLibcInternal` on the strength of an
    # export census row that obSCEne's own sweep contradicts (REQ-20260917T1818Z-9f41). So a file
    # that does not throw was importing the exception ABI from a library that may not have it.
    #
    # Compiling it the way it says it is written removes both references outright, which is a
    # better answer than either side of that conflict. Measured: the object's exception-ABI
    # undefineds go from two to none and nothing else about it changes.
    #
    # Upstream's `stdexcept.cpp` below keeps exceptions, because throwing is precisely what it is
    # for. Nothing in this link references it, so it costs a title nothing.
    clang++ $cxxflags -std=c++17 -fno-exceptions -c "$ROOT/src/runtime/cxx_support.cpp" \
            -o "$work/cxx_support.o"

    # Upstream's own, for the parts it can still build. `-I src` is not optional: its sources
    # include their implementation details by a path relative to that directory, and without it
    # `stdexcept.cpp` compiles to the `what()` accessors and silently omits every constructor.
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

# The public GL entry points, which this configuration does not build by default.
#
# # What was missing
#
# `libglapi.a` is built and is in the link, and it does **not** define `glGetString` or any other
# `gl*` name. It is compiled `-DMAPI_MODE_SHARED_GLAPI` with hidden visibility, so what it carries
# is the dispatch machinery and 1,648 `_dispatch_stub_*` symbols - the inside of GL, not its API.
#
# The public names live in `libglapi_bridge`, which upstream marks `build_by_default : false`
# because the only thing that normally wants it is libGL, and libGL is a GLX target this build has
# no reason to produce. So nothing asked for it, ninja did not build it, and a title calling
# `glGetString` got an undefined symbol.
#
# **That was not a link error, which is what made it worth finding.** A title links with
# `--unresolved-symbols=ignore-all`, so the name became an import, and the mined corpus placed it
# confidently: `libSceGLSlimServerVSH`, one of the platform's own PS4-era GL libraries. A title
# would have called *the vendor's* OpenGL, or trapped - and neither outcome would have looked like
# a build problem. It was found by `dri-probe` being written to make the first GL call at all.
#
# It is a `static_library`, so unlike EGL (D010) there is nothing to work around: it only had to
# be asked for.
echo "oops-mesa: building the public GL entry points (libglapi_bridge)"
ninja -C "$BUILD" src/mesa/glapi/glapi/libglapi_bridge.a >/dev/null 2>&1 || {
    echo "oops-mesa: libglapi_bridge would not build; a title cannot call GL without it" >&2
    exit 1
}

# The archives in the order Mesa links them, for a title to consume.
#
# These are static archives, so the linker resolves left to right and the order is load-bearing:
# a wrong one is an undefined symbol rather than a warning. Taking it from Mesa's own link line
# means `oops-mesa.mk` never has to arrange it by hand and cannot drift from what upstream does.
# Written relative to this repository's root, not absolute. The build runs in a container where
# the repository is mounted at /w, and a consumer does not, so an absolute path here is a path
# that exists nowhere the title is built. `oops-mesa.mk` prefixes its own directory.
# Objects first, then archives.
#
# The line carries both, and taking only the archives was wrong: the DRI target's own object is
# where every `*_driver_descriptor` lives. `drm_helper.h` defines the real one for whichever
# `GALLIUM_<DRIVER>` the target was compiled with and a stub for all the others, so that single
# object answers all twenty-four at once - `radeonsi` among them, which is why `radeonsi` itself
# appeared unresolved while radeonsi was plainly built (worklog 039 called that the thread to
# pull, and it was).
#
# Objects lead because they are always pulled in whole, while an archive member is only taken to
# satisfy something already pending. `dri_target.c.o` *references* `radeonsi_screen_create`, so it
# has to be seen before `libradeonsi.a` rather than after it.
{
    ninja -C "$BUILD" -t commands "$dri_so" 2>/dev/null | tail -1 \
        | tr ' ' '\n' | sed -n 's/^\(.*\.o\)$/build\/mesa\/\1/p'
    ninja -C "$BUILD" -t commands "$dri_so" 2>/dev/null | tail -1 \
        | tr ' ' '\n' | sed -n 's/^\(.*\.a\)$/build\/mesa\/\1/p'
} > "$ROOT/build/link-order.txt"

echo "oops-mesa: built ${#archives[@]} archives for x86_64-unknown-freebsd"
echo "oops-mesa: $localized libdrm archives had their private symbols localized; no duplicates remain"
echo "oops-mesa: link order for a title written to build/link-order.txt ($(wc -l < "$ROOT/build/link-order.txt" | tr -d ' ') archives)"
