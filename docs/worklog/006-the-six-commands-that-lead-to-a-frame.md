# 006. The six commands that lead to a frame

**2026-09-14** - roadmap unit 5, the path to a first frame implemented

## What changed

The six commands D005 named as the route to a triangle are implemented, compile for the target
with warnings on, and are covered by a host suite that now runs seventeen checks.

| command | what it does here |
|---|---|
| `DRM_VERSION` | names the driver `amdgpu`, which libdrm compares before it will talk to a device at all |
| `GEM_CREATE` | reserves physical memory and nothing else, as the kernel does |
| `GEM_MMAP` | hands back a token the shim's own `mmap` turns into a CPU pointer |
| `GEM_VA` | maps that memory at the address libdrm's manager chose, page by page |
| `CS` | submits every instruction buffer in the stream, then a fence of its own |
| `WAIT_CS` | answers, because submission is synchronous |

`src/winsys/` is now four files: the dispatcher, the device description, the buffer table and
address space, and submission.

## Two choices worth stating rather than burying

**Buffer creation and address mapping stay separate**, because libdrm expects the split and a
winsys that folds them together confuses it. A buffer with no mapping is a legitimate state on
Linux and it is one here.

**Submission is synchronous, and it is a stated choice.** The vendor submit call returns when
the work is queued, not when it has run, and nothing on this platform has established how
radeonsi's own end-of-pipe fence could be read back. So after handing over radeonsi's stream the
shim submits a second, tiny stream ending in an end-of-pipe event that writes a known word, and
waits for it. Two streams on one queue retire in order, so the word arriving means the first one
finished. That is oops-gl's proven sequence reused rather than reinvented: the packet and its
constants are from the oracle record for firmware 12.40, which is the only fence on this
hardware this collection has ever seen retire. The cost is that frames cannot overlap, and
`WAIT_CS` always answers signalled because by the time it is asked the work is done. Making it
asynchronous needs radeonsi's fence to have a home the shim can read, which is a later unit with
a measurement of its own.

## What this rests on, and the one thing it does not have

`GEM_VA` works because the platform's batch-map call takes a virtual address and a physical
offset, which is exactly the page-table operation a kernel performs. That it can be used this way
at all rests on something measured: oops-gl hands the pointer from an allocation straight to the
GPU as a command-buffer address and as a colour-target address, and the GPU reads both. A virtual
address means the same thing to both sides here.

What is missing is the ordering a real kernel provides. **libdrm picks an address first and asks
for the mapping second**, and nothing in oops-sdk reserves a range beforehand, so the address it
chose could already belong to something else. It works today because the ranges libdrm picks come
from what this shim itself advertised and nothing else has claimed them. That stops being true
for any title that allocates outside Mesa. `REQ-20260914T1657Z-9a3e` is filed on the oops-sdk
bus for the reservation binding, with the smaller observation that `oops_mem_map_direct` passes a
null address unconditionally and so can never honour a caller's choice.

## Surprises

- **The argument to a refused create is a union, and a caller who skips the return code sees a
  plausible handle.** The host suite caught it: a failed create left the low half of the
  requested size sitting where the handle goes, which is a perfectly good-looking handle. Every
  refusal now clears it. A real kernel does not bother, because it only copies back on success,
  but a caller that forgets to check deserves a zero rather than a number.
- **The host suite exercises the real SDK memory code**, not a stub of it, because oops-sdk's
  vendor calls are weak symbols that resolve to nothing off the console. So the host run tests
  the actual refusal path rather than a mock of it, and the log lines it prints are the ones the
  console would print.

## Next

Contexts and buffer lists, which radeonsi asks for before its first draw, and then whatever the
first real run says. `REQ-20260914T1558Z-7d41` is still the one that matters most: six of the
device description's field groups are assumed, and a frame rendered on assumptions is worth
less than the same frame rendered on measurements.
