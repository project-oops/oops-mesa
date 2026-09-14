# D005 - The winsys shims the kernel interface, and libdrm is carried rather than replaced


**decided** · 2026-09-14 (measured before deciding; the numbers are below)

Unit 3 ended at `libdrm`, which radeonsi requires and this platform has no copy of. There were
two ways to answer it and the choice is worth a page because it sets the size of unit 5.

## The choice

**libdrm is a dependency, pinned and built like Mesa, and the shim sits underneath it.** What
this repository writes is the thing libdrm calls: a synthetic device descriptor, an `ioctl`
that dispatches the AMD command set onto the vendor graphics driver oops-sdk already binds, and
`mmap` for buffer access. libdrm's own bookkeeping - virtual address ranges, buffer objects,
command-stream chunk assembly, fences - is upstream's and stays upstream's.

The rejected alternative was to implement libdrm_amdgpu's own API against the vendor driver, so
that Mesa linked ours instead of libdrm's.

## Why, in one table

Both routes were measured rather than estimated, against the pinned Mesa's own libdrm
(`2.4.133`, the version Mesa pins by hash in `subprojects/libdrm.wrap`):

| | surface this repository would own |
|---|---|
| implement libdrm_amdgpu's API | 91 public functions, plus the bookkeeping behind them |
| shim the kernel interface | one `ioctl` with 22 command cases, plus `open`, `close`, `mmap` |

The second is roughly a quarter of the first and it is the shape D001 already committed to: a
shim under upstream, not a driver of our own. It also cannot drift, because everything libdrm's
AMD layer does reaches the kernel through three thin wrappers - `drmCommandWriteRead`,
`drmCommandWrite` and `drmIoctl`, 44 calls between them - and all three are `ioctl` underneath.
A command we have not implemented fails loudly at one place instead of being quietly absent from
an API we transcribed by hand.

**libdrm already supports this operating system.** Its `xf86drm.c` carries ten `__FreeBSD__`
conditionals, so the portability work this route depends on is not speculative and is not ours.

**The protocol is already in the tree.** Mesa bundles the kernel interface definitions it needs
at `include/drm-uapi`, so the structures and command codes the shim must honour are present,
readable and MIT-licensed. Nothing about the interface has to be inferred.

## The 22 commands

From `libdrm/amdgpu/*.c` at the pinned version, the full set the shim must eventually answer:

```text
GEM_CREATE  GEM_MMAP  GEM_VA  GEM_OP  GEM_METADATA  GEM_USERPTR  GEM_WAIT_IDLE
CS  BO_LIST  CTX  SCHED  INFO  FENCE_TO_HANDLE  VM
WAIT_CS  WAIT_FENCES  USERQ  USERQ_SIGNAL  USERQ_WAIT
GEM_FLINK  GEM_OPEN  GET_CLIENT
```

Not all are needed to draw a triangle. `INFO`, `GEM_CREATE`, `GEM_MMAP`, `GEM_VA`, `CS` and
`WAIT_CS` are the path to a first frame; the sharing and user-queue commands can fail cleanly
until something asks for them, which is what principle 4 requires of them anyway.

`INFO` is the one with nothing behind it today: it is where radeonsi asks what the device is,
and `REQ-20260914T1558Z-7d41` on the obSCEne bus asks the console the same question so the
answers are measured rather than borrowed from a PC part of the same family.

## How libdrm is pinned

Through Mesa's own wrap, which names the version and a SHA-256 and is fetched into a directory
Mesa already ignores. The pin therefore moves with the Mesa pin and the two cannot disagree
about which libdrm this is - the same argument D004 made for sharing orbistoun's FreeBSD
checkout. `dependencies.mk` records the value for a reader; it does not set it.

## What would overturn it

libdrm needing something from a Linux kernel that is not an `ioctl` on a descriptor - an
`epoll`, a netlink socket, a sysfs read - on a path radeonsi actually takes. Its FreeBSD support
makes that unlikely, and unit 5 will find out at the point where the shim is real enough to run.
