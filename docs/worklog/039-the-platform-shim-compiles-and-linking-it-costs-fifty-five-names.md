# 039. The platform shim compiles, and linking it costs fifty-five names

**2026-09-17** - roadmap unit 6

## What this entry is

`src/platform/dri_loader.c` now compiles complete, with `oops_gl_create`, `oops_gl_present` and
`oops_gl_destroy` written. The one argument worklog 037 left open is answered. It is still not in
`OOPS_MESA_SRCS`, for a different and much more interesting reason: linking it pulls in the whole
Gallium DRI frontend, and the module fixup then refuses **55 imported symbols it cannot place**.

That list is unit 6's work list, and it is the first measured statement of what the unit costs.

## The format argument dissolved rather than being decided

037 framed it as a choice: hardcode a number out of a generated enum, or find a source that stays
true. It is neither, because **this shim never has to know what the value means**.

`dri_create_image` wants an `enum pipe_format`. Mesa produces that value and Mesa consumes it. The
only job here is to carry it from the chosen config to the call, unexamined - and a word that is
never interpreted cannot go stale across a pin bump, because nothing depends on what it stands
for.

Reading it needs one fact, and it is the fact EGL already relies on:
`dri2_image_format_for_pbuffer_config` (`egl_dri2.c:283`) casts a `dri_config` to
`struct gl_config` and reads `color_format`, which is the **first** member
(`mesa/src/mesa/main/glconfig.h:12`). So the restatement is one member long, which is as small as
a layout dependency gets.

The config itself is chosen by attributes rather than by format: eight bits per channel with
alpha, double buffered, deepest depth buffer offered. Those attributes are in `mesa_interface.h`,
which a title may include; naming a `pipe_format` would have dragged in the generated enum this
file exists to avoid.

## What linking it costs

Twelve DRI entry points, all confirmed defined in archives a title links (`nm`, not assumption).
The file compiles at `-Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion
-Wstrict-prototypes -Wmissing-prototypes -Wvla`, cross-compiled, with no suppression.

Then the module grows from 20 MB to 27.6 MB and the fixup stops:

```
error: 55 imported symbol(s) have no library, so a module cannot say where to resolve them.
```

The 55 are not one problem. They are five, and only one of them is large:

**Other drivers' descriptors, about twenty of them.** `asahi`, `crocus`, `etnaviv`, `i915`,
`iris`, `kgsl`, `kmsro`, `lima`, `msm`, `nouveau`, `panfrost`, `panthor`, `r300`, `r600`,
`rocket`, `tegra`, `v3d`, `vc4`, `virtio_gpu`, `vmwgfx`, `zink`, `ethosu`. The static pipe-loader
names every driver's descriptor, and this build configures radeonsi alone. This is a build
configuration question, not a platform gap, and it is the bulk of the list.

**`radeonsi_driver_descriptor` is in that list too**, which is the odd one and worth chasing
first: radeonsi *is* built. That points at a target object that is not in the link order rather
than at anything missing from the platform.

**Dynamic loading** - `dlopen`, `dlsym`, `dlerror`. The frontend's route to a driver it would
otherwise load at run time. A statically linked driver does not need them, so these are stubs, but
they have to be stubs that say so.

**The C++ runtime** - `__cxa_atexit`, `__cxa_guard_acquire`, `__cxa_guard_release`, and
`_ZTH23_mesa_glapi_tls_Context`, which is a thread-local initialiser and therefore lands in the
same question the title is currently stuck on.

**More of the C library** - `__isthreaded`, `__srget`, `__stdinp`, `clearerr`, `ferror`,
`fileno`, `getc`, `isatty`, `setvbuf`; `setjmp`, `longjmp`, `sigaction`, `stpcpy`; the System V
shared-memory four; and seven more math functions (`acosf`, `asinf`, `atanf`, `atan2f`, `atof`,
`llround`, `lround`). None of these has been swept, so none is known present or absent.

## Why the file stays out of the build

The same reason as 037 and a different cause. A source in `OOPS_MESA_SRCS` is compiled by every
title that links Mesa, so adding this line before the 55 are dealt with breaks all of them.

Leaving the file in the tree and out of the build is still the better half of the trade: the
reasoning is written where the code is, the next iteration starts from compiled code, and the gap
is now a list rather than a question.

## State

`./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches. `mesa-probe` builds and packages
unchanged at 20,003,120 bytes. Nothing in this entry has run on hardware: `MESA00001` still stops
earlier, in `si_init_renderer_string`, on the cross-module thread-local described in worklog 038.
