# The asset path works: files read from `/app0/data/` and decoded

**2026-09-23** · firmware 12.40 · the same retail unit as the earlier records · `mesa-demos`
`textures`, title `MDEM00001`

Every demo run here so far has been self-contained — geometry and procedural textures, nothing
read from disk. This is the first that opens files, and it is a test of the title's *filesystem*
rather than of GL.

## What it did

```
Loaded /app0/data/arch.rgb
Loaded /app0/data/reflect.rgb
Loaded /app0/data/tree2.rgba
Loaded /app0/data/tile.rgb
```

Four files opened, read and decoded. 30 presents, zero GPU faults, zero GL errors.

`make stage-data` copies all 13 assets (2.1 MB) into the packaged title, `meson.build` excluded,
and `pros restore` sent 14 files at 27.5 MiB. `DEMOS_DATA_DIR` is `/app0/data/`, the title's own
directory on the console, and the demos concatenate it directly — so the trailing slash in the
Makefile is load-bearing and now measured to be right.

## What that establishes, beyond "it drew"

| | |
|---|---|
| `/app0/` resolves from a hosted title | the path a packaged title's own files live at |
| `fopen`/`fread` reach it | through oops-sdk's `fs.c`, from a title that carries a C runtime |
| SGI RGB decode is correct | `readtex.c` unmodified, RLE and channel order both right |
| the alpha channel survives | `tree2.rgba`'s foliage is alpha-tested, and the cut-out leaf edges are that working |

**Correct hues are the part worth naming.** A file that opened and decoded to the wrong channel
order would still print `Loaded`, still draw, and still look like a success in the log. The arch
photograph is recognisably itself, the rock is sandstone-orange, the sky is blue and the foliage
is green — so the decode is right, not merely complete.

## A misreading worth recording

Two of the three captures show the arch photograph upside down, and the first reading of them was
that the SGI row order had been inverted somewhere between `readtex` and `glTexImage2D` — a real
and plausible bug, since SGI files store rows bottom-to-top and so does OpenGL.

It is not. `textures` rotates its quads in three dimensions, and those two frames caught the same
quad from angles that present it inverted. The third capture shows it facing the camera: sky at
the top, rock at the bottom, arch upright. **Two frames agreeing is not a measurement when the
subject is spinning**, and the third is what settled it.

## What this unblocks

`stage-data` copies every asset regardless of which demo is built, so the staged title already
carries what the other eleven data-using demos need: `dissolve`, `fbo_firecube`, `fire`,
`geartrain`, `ipers`, `lodbias`, `reflect`, `teapot`, `terrain`, `tunnel`, `tunnel2`. Each is now
a rebuild and a restore with no further asset work.
