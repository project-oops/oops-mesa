# 033. The format word is B8G8R8A8, and one half of that is measured

**2026-09-17** - roadmap unit 6

## What this entry is

D009 left presentation with one open item: "The display is handed `0x8000000000000000` with 32-bit
pixels and nothing in the collection says which Gallium format that is."

Something does, and it was filed a week before oops-mesa existed. This closes the question, and is
careful about which half of the answer is a measurement.

## The measurement

`REQ-20260909T1315Z-71dc`, sweep `20260909-144348`, payload leg, section
`085-videobuf/scanout`. It opened `/dev/dce` and read the display controller's own registers:

```
Width:        3840 (0xf00)
Height:       2160 (0x870)
Stride:       15360 bytes (0x3c00, exactly width * 4)
Pixel format: 0x80000000 (linear SDR B8G8R8A8_UNORM)
```

Stride is exactly `width * 4`, which confirms 32 bits per pixel independently of the format gloss.
That is the console's own scanout, read from the hardware rather than inferred.

## The corroboration, which is an argument rather than a measurement

oops-sdk writes pixels as `uint32_t` constants in `0xAARRGGBB` order - `0xff44ff88` for a green,
`0xffff5544` for a red, `0xff0d121f` for gl-cube's background. Stored little-endian those are the
bytes `B, G, R, A`, which is B8G8R8A8.

The reason this is worth stating is that the wrong reading is loud. Taking `0xffff5544` as `RGBA`
bytes gives R=0x44, G=0x55, B=0xff - blue where red was intended. Red and blue swapped across every
surface oops-gl has ever drawn is not a subtle artefact, and it would have been noticed.

That is an argument from absence, not a pixel anybody sampled and recorded. It agrees with the
measurement, which is why it is here; it would not be enough on its own.

## What Mesa needs, and that it can do it

`PIPE_FORMAT_B8G8R8A8_UNORM`. Mesa's own tables say the hardware can render to it:
`ac_get_cb_format` maps the four-channel eight-bit formats to `V_028C70_COLOR_8_8_8_8`, and the
colour-buffer support test a few lines below keys off exactly that not being `COLOR_INVALID`. So
the platform shim asks for a format radeonsi already knows how to write, on this generation, from
upstream's table rather than from this repository's opinion.

## Two things that are still not established

**That `0x8000000000000000` and `0x80000000` are the same encoding.** The controller register
obSCEne read is 32 bits and reads `0x80000000`. `agc_display.c` passes a 64-bit
`0x8000000000000000` to `sceVideoOutSetBufferAttribute2`. They differ by a 32-bit shift, which is
consistent with one field living in the high half of a wider argument - and "consistent with" is
all it is. Nothing has confirmed that the value a title hands the attribute call and the value the
controller reports are the same quantity.

It does not block anything. `agc_display.c` passes that value and the display works, so whatever it
means, it is the right value to keep passing. The shim inherits it rather than constructing one.

**Whether sRGB encoding is applied.** `B8G8R8A8_UNORM` and `B8G8R8A8_SRGB` differ in how shader
output is encoded, and the difference between them is a gamma curve - not a swapped channel. A
solid-colour test cannot tell them apart, and nothing in the collection has drawn a gradient and
measured it.

`UNORM` is the conservative choice and is what the measurement's gloss says, so that is what the
shim will ask for. Getting it wrong produces a picture that is too dark or too light and is
correct in every other respect, which is worth writing down now because it is exactly the kind of
result that gets blamed on the shader compiler.

## State

No code changed; this is a question answered from existing measurements rather than new work. D009
amended so its open list no longer carries the format. 104 host checks, `make check` passes.
Nothing deployed.

The bus question `-5c9d` was found missing this morning and has been re-filed - it had been lost
to a stale-copy overwrite of `worklog.md` some time after 02:33Z, along with no trace in the backup
rotation. Checking that it is still there is now the first thing each iteration does.
