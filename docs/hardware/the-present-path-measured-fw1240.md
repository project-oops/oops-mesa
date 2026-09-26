# Present path costs

Firmware 12.40, retail unit, 2026-09-21. Titles `dri-probe` (`DRIP00001`) and `mesa-cube`
(`MCUB00001`, a textured, depth-tested cube animated through a matrix pipeline, presenting every
frame), deployed and followed with `pros probe`, which attaches to the log before launch.
`oops_gl_present` reports four disjoint parts that sum to the total.

## Readback path

`flush` is `dri_flush_drawable`, `read` is `glReadPixels`, `mirror` the CPU row swap from
bottom-up to top-down, `disp` is `oops_display_present` (CPU tile plus a flip submit of about
25 us). `mesa-cube`, averaged over 18 reports in one 40-second capture:

| part | cost | share |
|---|---|---|
| `flush` | 4195 us | 11% |
| `read` | 20442 us | 56% |
| `mirror` | 3761 us | 10% |
| `disp` | 8270 us | 23% |
| total | 36668 us | |

`dri-probe` calls `glFinish` before presenting, so its `flush` is near zero:

```text
present us: flush=19 read=23296 mirror=3260 disp=10654 total=37229
```

and on another run `read=22526 mirror=5119 disp=9561 total=37223`.

`mesa-cube` runs flips 600 through 1620 with zero faults and zero refusals:

```text
[MCUB00001:MESA-CUBE] frame  600: 36769 us a frame over the last 300
[MCUB00001:MESA-CUBE] frame  900: 36808 us a frame over the last 300
[MCUB00001:MESA-CUBE] frame 1200: 36729 us a frame over the last 300
[MCUB00001:MESA-CUBE] frame 1500: 36769 us a frame over the last 300
```

The panel shows the cube with its yellow and blue checkerboard, darkest at the equator per
`0.55 + 0.45 * abs(normalize(v_dir).y)`. The `dri-probe` frame hash is `0x5188ddb7`,
`mod-pixels 373248`, `centre 0xff404080`, `corner 0xff0d0d14`.

## Colour buffer layout

```text
colour buffer 1920x1080: stride 7680 (ok, linear would be 7680), modifier 0x0000000000000000 (unavailable)
```

`stride 7680` is `1920 * 4`, the same for linear and `64KB_R_X` at this width. The modifier is
unavailable: `dri2_query_image` returns false for `DRM_FORMAT_MOD_INVALID` (`dri2.c`, lines
1144-1153), which a surface created with `modifiers = NULL` has. The allocated size, from the GEM
handle (`__DRI_IMAGE_ATTRIB_HANDLE`) and the winsys:

```text
colour buffer gem 5: 8896512 bytes (linear 8294400, 64KB_R_X 8847360) -> tiled
```

8896512 is the plain `64KB_R_X` size 8847360 plus 49152 bytes of displayable DCC metadata
(`surf->u.gfx9.color.display_dcc_size`). The display registers buffers with `dcc_control = 0`.
Mesa's `CB_COLOR0_ATTRIB3 = 0x0dc6c000` (`SW_MODE 27`, `64KB_R_X`) matches the display tiler
bit-for-bit across a 1080p frame: all 135 blocks, 2073600 pixels, zero mismatches.
`dri_create_image` with `__DRI_IMAGE_USE_SCANOUT | __DRI_IMAGE_USE_FRONT_RENDERING` disables DCC
for a `DRM_FORMAT_MOD_INVALID` image (`si_texture.c`, lines 236-242).

## Display registration

VideoOut refuses a third buffer at index 2, with the display's own address as well as Mesa's:

```text
direct scanout: register VA 0x400600000 (8896512 bytes) at index 2 -> rc 0x80290001
direct scanout: same call, display's own VA 0x4000000000 -> rc 0x80290001 (so the INDEX was refused, not the address)
```

Re-registering the display's two buffers plus the foreign one as a set of three at index 0
returns `0x80290010`. The display adopts Mesa's buffer at open instead (D009).

## Direct scanout

```text
colour buffer 8847360 bytes, plain 64KB_R_X is 8847360: no compression, offering it for scanout
direct scanout: VA 0x400600000 (8847360 bytes) has flip index 2
present us: flush=22 read=0 mirror=0 disp=3653 total=3675
```

With two buffers and flip pacing, `mesa-cube` holds 16682 us a frame (59.94 fps, vsync-locked)
with a present of 5083 us, 4428 us of it the GPU drawing. The steady-state flip costs 5-11 us; the
3653 us above is a first flip.
