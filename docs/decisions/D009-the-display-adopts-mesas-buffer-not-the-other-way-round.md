# D009 - The display scans out Mesa's colour buffer

**Status:** decided
**Date:** 2026-09-26

The platform shim creates radeonsi's colour images before the display opens, with
`__DRI_IMAGE_USE_SCANOUT | __DRI_IMAGE_USE_FRONT_RENDERING`, and hands them to
`oops_display_open_adopting`. Present flips the image radeonsi drew; the CPU touches no
pixels. An image is adopted only when its allocation equals a plain `64KB_R_X` surface
(`1920 * 1152 * 4` at 1080p); otherwise present reads back and copies.

**Why:** the memory is the same kind (direct memory, mapped through the same calls), and
radeonsi's `64KB_R_X` target is byte-for-byte the layout the display tiler produces
(`tools/tiling-compare`). The readback path costs 37 ms a frame and direct scanout holds
60 fps ([the present path record](../hardware/the-present-path-measured-fw1240.md)).
VideoOut registration is fixed once the display opens, so the buffer is named first. The
front-rendering flag makes radeonsi disable DCC
(`mesa/src/gallium/drivers/radeonsi/si_texture.c:236-242`); the display is registered with
`dcc_control = 0` and would scan compressed colour as raw pixels. Any size above plain
`64KB_R_X` is metadata the display was not told about, hence the size gate.

**Rejected:**
- Importing a display-owned buffer into Mesa: libdrm refuses KMS handle import, and the
  other handle types need cross-process sharing the platform lacks.
- Copying into pre-registered scanout buffers every frame: a frame-sized copy per frame.
- Adding a buffer to an already registered set: VideoOut refuses it.
