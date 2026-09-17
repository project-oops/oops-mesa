# 015. A title links all of Mesa

**2026-09-15** - roadmap unit 7, most of it

## What changed

`oops-apps/src/mesa-probe` links upstream Mesa into a PlayStation title. A 19.6 MB shared object
for FreeBSD on x86-64, two loadable segments, a dynamic segment, a thread-local storage segment,
and 284 undefined symbols the platform resolves at load. Every archive, both shims, the C++
support and oops-sdk, in one binary.

It does not render and is not meant to. `GB_ADDR_CONFIG` is still unmeasured, so radeonsi cannot
finish initialising. What this title is for is the link: everything else in oops-mesa is verified
by compiling, and compiling does not catch a wrong archive order, a missing runtime symbol or a
duplicate definition.

Also in this entry: `oops-mesa.mk`, the SDK fragment a title includes; `USE_MESA=1` in oops-apps'
`common/app.mk`; and the build now writing `build/link-order.txt` so nobody maintains that order
by hand.

## The duplicate symbol, and the wrong fix before the right one

Mesa's `libgallium.a` and libdrm's `libdrm_amdgpu.a` both define `handle_table_remove`. Upstream
never meets this because libdrm is normally a shared library: its headers mark those functions
`drm_private`, which is `visibility("hidden")`, and a shared library does not export them. A
static archive keeps them global, so the intent is stated and not enforced.

`llvm-objcopy --localize-hidden` over each archive enforces it, and **that was wrong**: it
localizes per object, so libdrm's own cross-object calls broke. `amdgpu_bo.c` calls
`handle_table_insert` in `handle_table.c` and `amdgpu_cs_calculate_timeout` in `amdgpu_cs.c`,
both hidden, and making them local to their own objects left those calls undefined.

The right form is a partial link first: combine the archive's objects into one relocatable
object, so references between them resolve inside it, and only then localize. That is what
partial linking is for.

The check changed too. Counting symbols defined in more than one archive was the first attempt
and is wrong - that is normal and harmless, and radeonsi alone defines its tracepoints in eleven
per-generation archives. A duplicate is only an error when two objects that are both pulled in
define the same name, and the only thing that knows which objects get pulled in is the linker.
So the build now attempts Mesa's own DRI link and fails on `duplicate symbol` while treating
`undefined symbol` as expected.

## Two flag distinctions that each cost a link

**`-ffreestanding` is compile-time and must go; `-nostdlib` is link-time and must stay.** Mesa
needs a hosted compiler. Nothing here links a C library, because the platform's own answers at
load. Dropping both made the linker hunt for `crtbeginS.o` and `libgcc` on the build machine.

**A hosted title needs the sysroot.** Removing `-ffreestanding` sends the compiler looking for a
standard library, and without `--sysroot` it finds the build machine's Linux headers and fails on
the first glibc-internal one. A title that links Mesa now compiles against exactly the headers
Mesa compiled against.

## The C++ constructors were mine and should not have been

`std::logic_error` holds libc++'s own reference-counted string. A constructor written here cannot
build that member without reimplementing the reference counting, and the hand-written one linked
to an undefined `__libcpp_refstring::__libcpp_refstring(char const*)` - the right answer to the
wrong approach.

Upstream's `src/stdexcept.cpp` provides them properly and is one of the libc++ sources that does
compile with this compiler. It is in the support archive now. It needed `-I src`, without which
it compiles to the `what()` accessors and **silently omits every constructor** - which is how the
file got dismissed the first time.

D006 is unchanged: 24 of 62 libc++ sources compile, and the LLVM 21 against clang 18 gap is
still why.

## Surprises

- **An hour was spent on a path.** Packaging reports 150 symbols as "not in the mined corpus".
  They are all in it, with their libraries; `mkmodule` reads `data/mined-names.txt` relative to
  the working directory, a title runs it from its own directory, and the empty result is
  indistinguishable from a real gap. `REQ-20260915T0025Z-1a95` has the line and the fix.
- **Nothing in this collection had ever used thread-local storage.** `__thread` appears nowhere
  in oops-sdk, so SELFish's link script emits no `PT_TLS` segment and every Mesa `__thread`
  symbol fails the link. A local script proves the rest of the stack links past it;
  `REQ-20260915T0001Z-8d72` asks SELFish for the real one, and asks the question this cannot
  answer - whether the target's loader honours a `PT_TLS` segment in a title's own image at all.
- **Turning exceptions off across Mesa removed seven symbols, not seventy.** ACO already builds
  with `-fno-exceptions`; applying it to the rest was worth doing and was not the lever it
  looked like.

## Next

Packaging, which needs the corpus path fixed or an import manifest generated. Rendering, which
needs the register. Both are filed and neither is here.

## Postscript, the same night: it packages

`dist/mesa-probe-title-prospero.zip` exists. The last step was the import manifest, and it went
from 150 unplaced symbols to none:

| | |
|---|---|
| placed from obSCEne's mined corpus | 140 |
| already in oops-apps' shared list | 373 |
| carried in a supplement, with evidence | 4 |
| unknown | 0 |

`tools/generate-imports.sh` reads the linked binary's imports and asks the corpus where each
lives, so nothing is decided here: 166,971 corpus lines answer 140 of them. It runs under either
`llvm-nm` or GNU `nm`, because the container has one and the builder that actually builds titles
has the other.

The four in the supplement are genuine corpus gaps and each carries its reason. The notable one
is `scePthreadAttrSetprio`: oops-sdk binds and calls it today and its payloads run, so a working
vendor call is missing from the corpus. That is recorded against
`REQ-20260915T0025Z-1a95` rather than left in a comment.

So unit 7 is done except for its last two words. A title in oops-apps builds with `make title`
and packages with SELFish. It deploys and renders when the hardware questions are answered, and
neither of those is this repository's to answer.
