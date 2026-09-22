# 073. The bump is verified, and the oracle had a gap in it

**2026-09-22** · follows [072](072-the-clang-21-bump-and-what-it-did-not-buy.md) · closes the
hardware acceptance [067](067-unit-8-needs-a-cpp-standard-library-nobody-costed.md) set

Entry 072 ended by saying what it did not claim: the bump was reproduced on the build machine and
nothing had been near a console. That run has now happened, and the full record is in
[the hardware note](../hardware/the-clang-21-bump-verified-fw1240.md).

## What came back

`dri-probe` returns `0x5188ddb7` - the same 32-bit hash of the same 1920x1080 frame, with the same
373248 modified pixels and the same centre and corner values as the clang 18 oracle. `mesa-cube`
holds 59.94 fps with no CPU pixel work, no presentation fallbacks and no faults.

So: **verified**, and unit 8's toolchain prerequisite is met on hardware rather than only in the
container.

## Three things worth keeping

**The build log lies about its own compiler.** `build/mesa/meson-logs/meson-log.txt` says clang
18.1.8. The objects say 21.1.8. Meson wrote that log at configure time in an earlier session and
never rewrote it, and nothing about the file looks stale - it is dated the same evening as the
rebuild. `readelf -p .comment` on an object is the answer; the log is not. This nearly became a
finding of its own ("D013 claims a rebuild that did not happen") before the objects were checked.

**A hash is an oracle only for the path it covers.** `0x5188ddb7` reproduced bit-for-bit through a
change that simultaneously broke the other title on the same panel, in a way anyone looking at the
screen would see instantly. dri-probe does not sample a texture, so the texture unit is outside
what the oracle can speak about. Worth saying plainly, because the number is quoted in three
documents as though it certified the whole stack.

**The evidence that settles a question is not always in this repository.** `mesa-cube` drawing as a
black silhouette read as a compiler regression and was minutes from being filed as one. What ruled
it out was a gif in oops-apps, captured on hardware at 19:57 on 2026-09-21 - after the clang-21
Mesa rebuild - showing the cube correctly textured and with no overlay on it. The compiler was
already 21 when that frame was good. Checking the timestamp on somebody else's asset was faster
and more conclusive than any experiment that could have been run here.

## What was filed rather than fixed

Both halves of the black cube are other repositories', so both went to the bus and neither was
touched here:

- `REQ-20260922T0040Z-c93d` to oops-sdk. `oops_hud_create` ends with
  `glBindTexture(GL_TEXTURE_2D, 0)`, unbinding whatever the caller had on the active unit;
  `oops_hud_begin`/`end` save and restore that same state correctly a few lines later, so the
  contract the file states is the one being broken.
- `REQ-20260922T0045Z-a7e2` to oops-apps, as a heads-up: the overlay that triggers it is in their
  working tree and not committed, and `mesa-cube` would be robust against the next such library if
  it bound its texture inside the frame loop rather than once at setup.

Also filed earlier, still open: `REQ-20260921T2230Z-f1b3` to oops-sdk for the missing
`toolchain.mk`. `tools/check-toolchain.sh` exits 1 on that one repository. It cannot be wired by
an agent - `.claude/settings.json` denies read and write on `oops-sdk/Makefile` and `oops-sdk.mk`,
which are the two files it would go in - so it is the owner's to unblock, and the gate stays red
until then.

## What is next

Unit 8. The C++ standard library, the unwinder and the compiler are all in place and all measured;
what is not is a console that has run a `throw`. oops-apps#D005 owes exactly that
(`cxx-throw` reporting `passed=5 total=5` over klog), and until it comes back what is established
is that a title *can carry* a working unwinder, not that the platform lets one run. The CTS subset
sits behind that one result.
