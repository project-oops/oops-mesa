# Asset files read from /app0/data/

Firmware 12.40, retail unit. Title `MDEM00001`, `mesa-demos` `textures`, 2026-09-23.

`textures` opens, reads and decodes four files:

```
Loaded /app0/data/arch.rgb
Loaded /app0/data/reflect.rgb
Loaded /app0/data/tree2.rgba
Loaded /app0/data/tile.rgb
```

30 presents, zero GPU faults, zero GL errors.

## Staging

`make stage-data` copies all 13 assets (2.1 MB) into the packaged title, `meson.build` excluded;
`pros restore` sends 14 files at 27.5 MiB. `DEMOS_DATA_DIR` is `/app0/data/`, and the demos
concatenate it directly, so the trailing slash in the Makefile is required. The staged title
carries the assets of every data-using demo: `dissolve`, `fbo_firecube`, `fire`, `geartrain`,
`ipers`, `lodbias`, `reflect`, `teapot`, `terrain`, `tunnel`, `tunnel2`.

## Facts from the run

| | |
|---|---|
| `/app0/` resolves from a hosted title | the path of a packaged title's own files |
| `fopen`/`fread` reach it | through oops-sdk's `fs.c` |
| SGI RGB decode is correct | `readtex.c` unmodified; RLE and channel order right |
| the alpha channel survives | `tree2.rgba`'s foliage is alpha-tested, with cut-out leaf edges |

The hues are correct: the arch photograph shows sandstone-orange rock, blue sky and green foliage.
`textures` rotates its quads in three dimensions, so a capture can show the arch inverted; a
capture facing the camera shows it upright, so SGI row order is preserved.
