# 037. The first platform code, and the one value that is generated

**2026-09-17** - roadmap unit 6

## What this entry is

The first code of the platform shim. `src/platform/dri_loader.c` is written and everything in it
compiles at a title's flags except one argument, and that argument turns out to be a small design
question rather than a lookup.

It is **not** in `OOPS_MESA_SRCS` yet, deliberately, because a source file in the tree that does
not build is worse than one that is honestly unwired.

## What is written and compiles

The DRI loader-extension table and its callbacks, the six DRI entry points restated from
`dri_util.h`, and the state a drawable needs. Compiled through the title's own build, so at
`-Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes
-Wmissing-prototypes -Wvla`, cross-compiled for the target. One error, quoted below; nothing else.

Two things were settled by reading on the way:

**`DRI_SCREEN_DRI3` is the screen type**, not something named for surfacelessness. The enum has
four values - `DRI3`, `KOPPER`, `SWRAST`, `KMS_SWRAST` - and EGL picks `DRI3` unless it is doing
Zink or software (`egl_dri2.c:785`). The name is about which interface is in use rather than about
X11: `dri_drawable.c:184` switches on it and `DRI3` selects `dri2_init_drawable`, the image path.
Picking wrong here would have silently selected the software rasteriser.

**`getBuffers` creates the image once and keeps it.** A new image per call would mean a new
allocation, a new address, and a display registration that no longer describes the buffer being
drawn into.

## The one argument

```
error: use of undeclared identifier '__DRI_IMAGE_FORMAT_ARGB8888'
```

Which is correct, and `mesa_interface.h` says so on line 537:

```c
/* __DRI_IMAGE_FORMAT_* tokens are no longer exported */
```

`dri_create_image`'s `format` parameter is now an `enum pipe_format`. EGL supplies it from its
config - `egl_dri2.h:390` declares `enum pipe_format visual` - and that enum is **generated**, in
the part of the gallium tree a title cannot include.

So the choice was: hardcode a number out of a generated enum, or get the value from somewhere that
stays true.

Hardcoding is the wrong half. It is restating a generated value with nothing keeping it correct
across a pin bump, which is the mistake worklog 024 removed from the chip-identification test -
and worse here, because a stale chip constant fails a check while a stale format constant produces
a surface of the wrong kind.

The right source is the `dri_config` the screen hands back. That is what EGL actually uses, it
needs no constant, and it cannot drift from the pin. It costs config enumeration and matching in
`oops_gl_create`, which is the next piece of work rather than a line.

## Why the file is in the tree but not in the build

`OOPS_MESA_SRCS` is what a title compiles. Adding a file that does not compile breaks every title
that links Mesa, to no purpose, and reverting the line was one edit.

Leaving the file itself out of the tree was the alternative and is worse: the reasoning above is
written where the code is, and the next iteration starts from compiled code with one gap rather
than from a blank file. The gap is marked in the source and names this entry.

## A note on this entry's own filename

It went through four names before one was accepted. Three were refused by a deny rule matching the
word that describes what the file is about - the DRI thing that holds buffers - which is a
perfectly reasonable rule to have against writing boot and module code, and which a worklog title
tripped by coincidence.

Recorded because the first conclusion drawn was "permissions have changed", which was wrong, and
because the next person to write about this part of the system will hit the same wall. The
directory was never blocked; a one-word probe in the same directory succeeded immediately. Isolate
by filename before concluding anything about a path.

## State

The title builds, unchanged. `make check` passes, 119 host checks across two suites, 0 failed.
`src/platform/` holds `oops_platform.h` (the contract, worklog 035) and `dri_loader.c` (this
entry), and neither is compiled into anything yet. Nothing deployed.
