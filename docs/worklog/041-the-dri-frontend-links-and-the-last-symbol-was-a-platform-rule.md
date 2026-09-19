# 041. The DRI frontend links, and the last symbol was a platform rule rather than a gap

**2026-09-17** - roadmap unit 6

## What this entry is

`src/platform/dri_loader.c` is in `OOPS_MESA_SRCS`. A title links the Gallium DRI frontend,
places every import, and packages: 28,209,368 bytes of module, 26,403,584 of `eboot.bin`, zero
unplaced symbols.

Worklog 039 measured the cost of getting here as 55 imported symbols with no library. All 55 are
accounted for, and only one of them turned out to be work.

## Twenty-four came from an object the build already had

039 flagged `radeonsi_driver_descriptor` as the thread to pull first - unresolved while radeonsi
was plainly built - and that was the right instinct for the wrong reason. It is not radeonsi's
descriptor that is special; it is that **none** of them are defined by any archive.

`target-helpers/drm_helper.h` defines every driver's descriptor in one place: the real one for
whichever `GALLIUM_<DRIVER>` the compiling target was given, and `DRM_DRIVER_DESCRIPTOR_STUB` for
all the others. So one object answers all twenty-four at once, and this build already produced it
- `src/gallium/targets/dri/libgallium-26.2.2.so.p/dri_target.c.o`, built as part of the DRI module
the link check builds anyway.

It never reached a title because `link-order.txt` is derived from that module's own link line with
a filter that keeps `*.a` and drops everything else. `build-mesa.sh` now emits objects too, and
puts them first: an object is pulled in whole, while an archive member is only taken to satisfy
something already pending, and `dri_target.c.o` *references* `radeonsi_screen_create`, so it has
to be seen before `libradeonsi.a`.

That one line took the list from 55 to 5. The eight symbols the object needs for itself were all
already in archives a title links, checked with `nm` before relying on it.

## Four were the software rasteriser

`shmget`, `shmat`, `shmdt`, `shmctl`, all from `gallium/winsys/sw/dri/dri_sw_winsys.c`, which puts
its display target in a shared segment for an X server to map. `libswdri.a` is in the link because
the driver table references it, not because anything selects it - this build creates its screen
through radeonsi and D003 forbids falling back to software.

Stubs in `libc_absent.c`, of the first kind that file describes: unreached paths where being
reached is the news. Each names the path, so a log line saying `shmget` ran means the software
winsys ran, which is a larger problem than the call failing.

## The last one was not a gap, and the request about it was aimed slightly wrong

`_ZTH23_mesa_glapi_tls_Context` is the Itanium ABI's thread-local initialiser, referenced
**weakly** by two C++ files that read `_mesa_glapi_tls_Context`. Clang emits the guarded form:

```c
if (&_ZTH23_mesa_glapi_tls_Context)
    _ZTH23_mesa_glapi_tls_Context();
```

On an ordinary system the address is null - the variable is C, `extern __thread`, with no dynamic
initialisation - and the guard skips. So it looked like a tool bug, and
REQ-20260917T1755Z-4a91 was filed asking obSCEne to stop treating weak undefined symbols as
imports. obSCEne made that change and it is a reasonable one; it moved the error and did not
clear it.

**The gate is SELFish's, and it is deliberate.** `selfish-elf/dynlib.rs` rewrites every import's
binding to `GLOBAL` and refuses any import nothing claims, because a loader is entitled to read
`STB_WEAK, undefined` as "do not bother resolving this" - and one does. Measured: a module whose
203 imports were all weak had them bound **only from the two libraries already resident in the
process**, while every other declared library was mapped and had nothing bound (obscene#D248).
D101 records the consequence in as many words: a deliberately-absent symbol used as a weak-symbol
control reads present once packaged.

So "absent, therefore null, therefore skipped" is not available to a packaged title, by design and
for a measured reason. The remaining honest option is to make the address non-null and the call
harmless, which is what `src/runtime/abi.c` now does: an empty function, in the file for symbols a
hosted binary would have got from start-up objects it does not have. Initialisation that has
nothing to do, done.

Named in its mangled form from C deliberately - smaller than adding a C++ translation unit to
every title for one empty function.

## What has not happened

None of this has run. `dri_loader.c` is linked but nothing calls `oops_gl_create` yet:
`mesa-probe` still creates its screen directly through `radeonsi_screen_create`.

The console is deliberately **not** carrying this build. It has the 20,002,960-byte module from
17:15, which is the clean test of the Initial Exec change in worklog 040 - the open question that
matters. Deploying an image eight megabytes larger, with a frontend linked that nothing calls,
would answer that question and a second one at the same time, which is the mistake this project
has already paid for twice today.

## State

`./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches, 46 entries in `link-order.txt` where
there were 45. Identity guard clean on both repositories.
