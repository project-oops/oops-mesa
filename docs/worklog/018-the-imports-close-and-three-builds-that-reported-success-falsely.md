# 018. The imports close, and three builds that reported success falsely

**2026-09-17** - roadmap unit 5

## What changed

A Mesa-linked title's import manifest is complete: 504 names placed, none unknown, none naming a
library that cannot resolve it. mesa-probe builds again. Nothing was deployed.

Getting there took removing three imports, defining six, and fixing three separate places where a
build reported success while doing nothing of the kind. The last of those is the part worth
reading.

## The imports, and why six of them are ours now

Worklog 017 left the title unable to build: `tools/generate-imports.sh` had been writing `-` as a
library for names the corpus knows without knowing where they live, counting them as placed, and
reporting `0 unknown`. mkmodule accepted `-`, built a module claiming those symbols resolve from a
library of that name, and the console killed the title on the first call with
`PRX_NOT_RESOLVED_FUNCTION`. The generator now refuses instead, before writing anything.

That left eleven names to answer. They fell into three groups.

**Three were reachable only from dead code.** `regcomp`, `regexec` and `regfree` are referenced by
`xmlconfig.c.o` alone, from `parseAppAttr` - an expat element handler. expat is not linked here:
that object has no `XML_*` reference and no `opendir`/`readdir`/`stat`, so `-Dxmlconfig=disabled`
is doing its job and the handler is never invoked. The calls were unreachable and the imports were
weight. `xmlconfig.c` carries its own stubs behind a **separate** guard, `NO_REGEX`, which is why
disabling xmlconfig had not already removed them; the cross file now sets it and they are gone.

**Two were already answered** by the generator's own supplement: `open_memstream` and `openlog`.

**Six had no answer anywhere, and obSCEne established that properly.** `REQ-20260916T2200Z-4d7e`
asked for the payload leg and for handle-status reported separately from symbol-resolution, because
the previous attempt had read an all-zero result on the eboot leg as a platform fact when the same
sweep's own controls also read zero. Sweep `20260916-235024` answered on payload: all eleven absent
from `libkernel`, `libSceLibcInternal` and `libScePosix`, with the leg independently validated -
`017-posix/libkernel-pthread-symbols` resolved 27 of 27 and `017-posix/clock-symbols` 11 and 11,
including `sceKernelClockGetres` on both libraries.

So there is nothing to place them in, and `src/runtime/libc_absent.c` defines them instead:
`__assert`, `__xuname`, `getline`, `localtime_r`, `mknod`, `mkstemps`. The linker satisfies them
locally and the module never asks the platform.

Every one is loud. None is on a path this stack executes - their call sites are Mesa's debug dumps,
its syslog wrapper, `uname`, temporary files and time formatting, established by reading which
archive member references each - and that is exactly why a quiet plausible return would be the
lying stub this collection refuses. If one is reached, the interesting fact is that it was reached,
and the log line says which. `__assert` does not return, because its contract is not to.

`__assert` is also not Mesa's: it comes from oops-sdk's `agc_display.c`, compiled into the title. A
title built `-DNDEBUG` would not reference it at all, and that is the better fix for whoever owns
the trade.

## Three builds that reported success falsely

This is the entry's real finding, because all three have the same shape and two of them were in
this repository.

**app.mk skipped mkmodule silently.** The stamp rule guarded the tool and its symbols file with one
condition and touched the stamp regardless, so a project naming a symbols file it had not generated
yet built, packaged and deployed an ELF mkmodule never touched - no `PT_SCE_DYNLIBDATA`, refused by
the loader with "found illegal segment header", and nothing before the console said a word. A
missing symbols file is now fatal; an absent *tool* is still tolerated, because a checkout without
obSCEne built is a real state.

**The generator wrote `-` and counted it placed.** Above. It now refuses *before* writing, and
removes any stale manifest, because a refusal that leaves the file behind is not one - app.mk only
asks whether it exists.

**meson ignored the cross file and rebuilt everything anyway.** `-DNO_REGEX` was added to
`c_args` in `toolchain/cross-prospero.ini`; 1,171 targets recompiled; `build.ninja` contained no
mention of it, while `-DOOPS_MESA_WINSYS` from the same line was present. meson reads
`[built-in options]` when it first configures a build directory and keeps them in coredata
afterwards, so a later edit is silently discarded. `build-mesa.sh` now hashes the cross file,
stores the hash beside the build and reconfigures from scratch on a mismatch.

A timestamp comparison was tried first and cannot work: `build.ninja` is regenerated on every
build, so the cross file is never the newer of the two. That is written in the script beside the
check, because it is the obvious first attempt.

## What is still not known

The title still crashes before the winsys is reached. The eleven were never the cause - all of them
sit in unreachable paths - so the symbol that failed to resolve is one the corpus *did* place and
which does not resolve on this firmware. The crash is bracketed: `winsys device opened` printed,
`screen options built` did not, so it is inside `driParseOptionInfo` or `driParseConfigFiles`. The
candidate set is that object's eighteen platform imports, all placed in `libSceLibcInternal`.

Naming it needs either the coredump, which the crash-report sequencer deletes, or a probe. It is a
static question and the next thing to answer.

## State

The host suite is 70 checks and passes. The import manifest is 504 lines with no unplaced name and
no `-`. `dist/mesa-probe-title-prospero.zip` builds. radeonsi has still never issued an ioctl to
the winsys, so every answer in `drm_device.c` - the derived `GB_ADDR_CONFIG`, DRM 3.54,
`HW_IP_INFO`, `FW_VERSION` - remains unexercised on hardware.
