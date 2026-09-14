# 009. libdrm now calls the shim, and the repository has its first patch

**2026-09-14** - roadmap unit 5, the integration

## What changed

Until now the winsys was a library nobody called. It is wired in: **libdrm's command path reaches
the shim instead of a kernel**, and Mesa rebuilds with it. The proof is in the built archive
rather than in the source:

```text
libdrm.a           U oops_winsys_ioctl
                   U oops_winsys_mmap_or_failed
libdrm_amdgpu.a    U oops_winsys_mmap_or_failed
```

`patches/001-libdrm-reaches-the-shim-instead-of-a-kernel.patch` is the first patch this
repository carries. It is two hunks, both guarded by a define so the patch is inert anywhere
else: `drmIoctl` calls the shim instead of `ioctl(2)`, and `drm_mmap` calls it instead of
`mmap(2)`. Nothing else in libdrm is touched.

**Why it could not be a shim**, which is the question CLAUDE.md requires a patch to answer:
libdrm calls libc's `ioctl` and `mmap` directly by name. The only way to intercept that without
editing the call sites is to define those symbols ourselves and let them win at link time, which
would capture every `ioctl` and `mmap` the title makes - its own file access, its own memory -
and route them into a graphics winsys. Two call sites is smaller and is confined to the library
that should be talking to a kernel and has none.

The patch reconciles the two error conventions, which is the part that would have been a quiet
bug: the shim returns a negative errno, `ioctl` returns -1 and sets `errno`, and libdrm reads
`errno` in places.

## What is deliberately not intercepted

Three `ioctl` calls remain in libdrm and all three are off the AMD path: the legacy DMA command,
the vblank wait, and FreeBSD's PCI enumeration, which libdrm uses to discover devices. The
amdgpu layer never calls any of them - checked, not assumed - and this project hands in its own
descriptor rather than discovering one. Saying so here is better than claiming the interception
is total when it is not.

## Surprises

- **`git apply --3way` cannot patch a subproject.** The files live inside Mesa's tree but are
  ignored by its repository, so there is no index entry for a three-way merge to work from.
  Plain `git apply` is correct here and the script now says why.
- **The patch step ran before the thing it patches existed.** Patches were applied at the top of
  the build and libdrm is fetched two thirds of the way down, so on a clean tree the first patch
  would have failed on a missing file. It only worked here because a previous run had left
  libdrm in place. Patching now happens after the fetch, and re-applying is a no-op rather than
  an error, so a rebuild on an already-patched tree behaves like one on a fresh tree.
- **The named-target list was hiding a gap.** Building only the archives a title consumes meant
  libdrm's core archive was never built, so the ioctl seam was never compiled and the first
  check for it found nothing. The seam was fine; the evidence was missing. It is in the list now.

## Next

Mesa's radeonsi can now be created against a descriptor this shim owns. The next step is the
thing that has no code yet on either side: something that opens the device, creates the screen
and asks for a context, which is where the remaining `AMDGPU_INFO` queries will be demanded for
real rather than in the abstract.
