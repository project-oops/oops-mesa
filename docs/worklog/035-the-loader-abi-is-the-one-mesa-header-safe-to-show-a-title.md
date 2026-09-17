# 035. The loader ABI is the one Mesa header safe to show a title

**2026-09-17** - roadmap unit 6

## What this entry is

D010 put the platform shim on the Gallium DRI frontend's loader seam. Before writing against it,
the question is whether that seam can be reached from where the shim compiles - which is with the
title, at the title's flags, not inside Mesa's build.

It can, and the margin is narrower than it looks.

## The problem the check was for

The shim's sources compile as part of `OOPS_MESA_SRCS`, with a title's flags: `-std=c11 -Wall
-Wextra -Werror -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes
-Wvla`. Mesa does not compile at that setting and has no reason to.

`dri_util.h`, the obvious header for the five DRI calls, is not reachable from there at all:

```c
#include <GL/gl.h>
#include "mesa_interface.h"
#include "kopper_interface.h"
#include "main/formats.h"
#include "main/glconfig.h"
#include "main/menums.h"
#include "util/xmlconfig.h"
#include "pipe/p_defines.h"
```

`main/` is Mesa's GL core and `pipe/` is Gallium's; between them they pull in generated headers
that only exist inside a Mesa build directory. That is the same reason `oops-mesa.mk` already
keeps Gallium's include tree away from a title, and why `mesa-probe` restates `pipe_screen_config`
rather than including `p_screen.h`.

## What makes it work

`mesa_interface.h` - the header that actually defines `__DRIimageLoaderExtension`,
`__DRIextension` and `__DRIimageList` - includes exactly two things:

```c
#include <stdbool.h>
#include <stdint.h>
```

That is not an accident. It is the loader ABI, and loaders outside Mesa's tree include it, so it is
deliberately free of dependencies. It happens to be exactly the header this shim needs and nothing
more.

**Checked rather than assumed.** A one-file probe defining a `__DRIimageLoaderExtension` was
compiled with the title's flags verbatim - cross-compiled for `x86_64-unknown-freebsd`, against the
staged sysroot, at full `-Werror`:

```
OK: mesa_interface.h compiles at the title's flags
```

No suppression pragmas. `mesa-probe` needs three of them around `util/driconf.h` next door, so
"a Mesa header is fine at our warning level" was not safe to assume from the neighbours.

## What changed

`oops-mesa.mk` gains `mesa/src/gallium/include` on the include path, with a comment saying why that
one directory and not the rest of the tree, and `src/platform` so the shim's own header resolves.

`src/platform/oops_platform.h` is written: four functions and an opaque handle.

```c
struct oops_gl *oops_gl_create(uint32_t width, uint32_t height);
bool            oops_gl_present(struct oops_gl *gl);
void            oops_gl_extent(const struct oops_gl *gl, uint32_t *w, uint32_t *h);
void            oops_gl_destroy(struct oops_gl *gl);
```

Three choices in it are worth naming, because each could have gone the other way:

**The extent is the title's, not the display's.** A title may render below screen resolution and
let the compositor scale, which at 4K is the difference between a frame and a slideshow. Nothing
here reads the display's mode to decide for it.

**The format is not a parameter.** `B8G8R8A8_UNORM`, because that is what the display controller
reports for its own scanout, measured through `/dev/dce` (`REQ-20260909T1315Z-71dc`, sweep
`20260909-144348`, pixel format `0x80000000` with a stride of exactly `width * 4`). There is no
second format this collection has evidence for, so offering a choice would be offering a way to be
wrong.

**`present` returns false meaning the frame is not on screen**, rather than "may not be". That is
principle 4 written into a signature rather than into a comment - there is no path here that
returns true having fallen back to software, because there is no software path.

## What is not written

The implementation. That is the next unit of work and it is now against a known contract on both
sides: Mesa's loader ABI above, D009's buffer direction and worklog 033's format below.

## State

No behaviour changed; the header declares and nothing implements it yet. 104 host checks, `make
check` passes, the title still builds - the new include path is additive and nothing consumes the
header. Nothing deployed.
