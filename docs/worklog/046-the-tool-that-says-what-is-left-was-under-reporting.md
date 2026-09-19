# 046. The tool that says what is left was under-reporting it, twice over

**2026-09-17** - roadmap unit 7

## What this entry is

`tools/what-is-still-needed.sh` exists to answer one question: what does the built Mesa still need
that nothing in this tree provides. It is the thing a reader trusts to say what is left, and it
had two defects pulling in the same direction - both of which made the work list look shorter than
it was.

The first was inherited from a source. The second was mine, introduced while fixing the first, and
it is the better story.

## The census it trusted describes a different container

The tool sorted unresolved symbols into piles, and one pile was "the platform exports", drawn
entirely from `obscene/data/hardware/ps5-imports.txt`. A name landing there left the work list.

`REQ-20260917T2014Z-c8a7` resolved that **every one of that file's 17,645 rows was captured under
GEN=4** - the PS4 backward-compatibility container, running Orbis userland, where
`libSceLibcInternal` exported the C runtime and the maths that native Prospero's stripped PRX does
not. There are zero GEN=5 rows in it. Its header says so in one line that nothing downstream ever
read: `Build: BUILD_ID=global1, GEN=4, ...`.

So the pile was not a measurement about the machine a title runs on, and a name in it could still
be unbindable - which is not a link error but a module that loads and dies on the first call.

The tool now sorts by **how well each name is evidenced** instead:

| pile | means |
|---|---|
| the platform exports | a native sweep resolved it to an address |
| probably exported | only the GEN=4 census knows it. **Advisory**, and labelled as such |
| measured absent, still imported | a native sweep looked and found nothing, and the build imports it anyway |
| nobody provides | no sweep has an opinion either way |

The third pile is new and is printed first when non-empty, because a name in it is a crash with a
caller's name on it rather than something to go looking for. The advisory pile prints a paragraph
saying what it is and that settling a name means asking for a native sweep of it.

The native sets come from the two sweeps `-c8a7` names as authoritative, whose rows are
unambiguous - every swept symbol gets an `addr` row and `0x0` means it did not resolve:

```text
OBS|measure|017-posix/mesa-candidate-imports|snprintf|addr|0x8000a8790|hex    resolves
OBS|measure|017-posix/mesa-candidate-imports|sin|addr|0x0|hex                 does not
```

## The bug I wrote while fixing it

The first run of the reworked tool reported:

```text
  the platform exports             18   (measured on the native leg)
  NOBODY PROVIDES                  23
```

and the 23 included `clock_gettime`, `nanosleep`, `mmap`, `getpid` and `strncmp` - all of which
`-5b28` measured **present**, with addresses, in the very sweep the tool had just read. That is
the same failure as the one being fixed, arrived at by a different route, and it was caught only
because those names are familiar enough to look wrong.

The cause is an awk detail worth knowing: **`print > "file"` truncates its target on first use per
awk invocation.** The extraction loops over two sweeps, one awk run each, so the second run
truncated the file the first had just filled. 39 rows survived out of 151. `>>` appends, and the
files are emptied once before the loop instead.

After the fix:

```text
  referenced by the archives     8887
  answered by the archives       8584
  answered by the shims           108
  answered by staged libraries     57
  the platform exports             80   (measured on the native leg)
  probably exported                29   (GEN=4 census only - ADVISORY)
  other drivers and rasteriser     24   (not built on purpose)
  MEASURED ABSENT, STILL IMPORTED   0
  NOBODY PROVIDES                   5
```

**The zero is the useful line.** It says, from a different code path and a different data source
than the `--error-limit=0` link measurement in worklog 045, that no symbol measured absent on the
native leg is still imported by this build. Two independent instruments agreeing is worth more
than either.

## What is actually left, and it is now a finite list

29 advisory plus 5 unknown is **34 names** whose native bindability has never been established.
That is the whole remainder: after those, no symbol a Mesa title imports has an unknown status.
Filed as `REQ-20260917T2045Z-a4f2`, with six of them marked as already answered by execution -
`memcpy`, `memset`, `memcmp`, `__cxa_atexit` and the guard pair cannot not be bound, because
`MESA00001` answered 35 ioctls and created a screen, and nothing gets that far without them.

The rest are on paths the probe never took: the `dl*` family, the stdio accessors,
`setjmp`/`longjmp`, `sigaction`, the file-locking calls. Those are the real question, and none of
them is on a path this build executes today, which is why this is a request rather than a blocker.

## One small thing

`getprogname` returned `OOPS_APP_NAME`, which `app.mk` defines when the shim is compiled with a
title - and this tool compiles the shims standalone to ask what they answer, so the shim failed to
compile and the tool reported a false gap. It is guarded now. A shim that only compiles inside a
title is a shim the instruments cannot see.

## State

`./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches. Identity guard clean.

The tool reads the build rather than changing it, so nothing here needed a rebuild except the one
shim edit above - and the three Mesa-family titles were rebuilt and repackaged anyway, 470 imports
with 0 unknown each. The guard compiles to the same bytes when `OOPS_APP_NAME` is defined, which
it is for a title, so this is about the artifacts matching the source rather than about behaviour.
Shipping a package built from source that no longer exists is how the stale-binary day started.
