# 008. The buffer commands finish, and the host grows a stand-in for the platform

**2026-09-14** - roadmap unit 5, `GEM_WAIT_IDLE` and `GEM_OP`

## What changed

Ten of the winsys's 22 commands now do real work. Eleven still refuse, and most of them should
keep refusing until something asks: sharing, metadata, user queues, scheduling.

- **`GEM_WAIT_IDLE`** answers that the buffer is idle, and that is correct rather than
  convenient. Submission waits for its own fence before returning, so every stream handed to the
  driver has retired by the time anything can ask. There is no window in which a buffer is busy.
  It says so, and names the other place that would have to change if submission ever became
  asynchronous, so neither is left quietly answering from an assumption that has expired.
- **`GEM_OP`** answers how a buffer was created, from the table, which now remembers the
  alignment and domain flags it was asked for. Moving a buffer between domains is refused: the
  domain chose the cache policy when the pages were mapped, and honouring a change would mean
  reallocating behind a caller still holding the old pointer.

## The host suite can now build a buffer

The suite went from 31 checks to 47, and the reason is a file rather than more assertions.

oops-sdk binds the platform's memory calls as weak symbols, so off the console they resolve to
nothing and every allocation refuses. That is right for the SDK, and it made most of the winsys
untestable: a buffer could never exist, so nothing taking a live buffer was ever reached. Half
the commands written in the last two entries had no test that got past their first line.

`tests/platform_double.c` supplies those symbols with host memory behind them. What is tested is
still the winsys's own bookkeeping, and the stand-in does the least it can while staying honest:
allocation really allocates, a mapping really returns the pages of the allocation it names, a
release really invalidates. Its one deliberate pretence is mapping at a chosen address, which a
host cannot imitate, so it reports the pages mapped without moving anything and every test that
touches it asserts on bookkeeping rather than on memory contents.

The new test takes one buffer through its whole life: create it at a size that is not a page
multiple, ask how it was created, refuse to move it, map it, give it an address, refuse a partial
mapping, ask whether the GPU has finished with it, name it in a buffer list, close it, and then
find that every one of those refuses afterwards.

## Surprises

- **The stand-in immediately made an older test obsolete, and that was the point.** A test
  asserting "buffer creation refuses when there is no allocator" had been passing for two
  entries. It was testing the absence of a platform, not the presence of a contract. With real
  buffers available it failed, and what replaced it is worth fifteen times as much.
- **Physical offset zero is a real answer, and the stand-in hands it out first on purpose.** The
  buffer table had already been caught reading zero as "free" (worklog 007), so the double
  allocates index zero to the first caller rather than avoiding it. A regression there fails on
  the first buffer of the first test rather than on some unlucky later one.

## Next

`AMDGPU_INFO`'s remaining queries - memory sizes, firmware versions, hardware IP blocks - are the
largest group left, and they are all the same measurement: `REQ-20260914T1558Z-7d41`. The rest
can wait for a real run to ask for them.
