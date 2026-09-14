# 011. The thread shim, and the descriptor that cannot be duplicated

**2026-09-14** - roadmap unit 4's thread half, and the third hunk of patch 001

## What changed

Two pieces, both of which were unblocked while `GB_ADDR_CONFIG` sits on the obSCEne bus.

**`src/runtime/threads.c`** is the table from
`docs/hardware/mesa-thread-surface-fw1240.md` turned into code: all 26 functions Mesa's C11
threads layer calls, mapped onto the vendor's. Mesa still builds; the file compiles for the
target with warnings on.

**Patch 001 gained a third hunk.** libdrm duplicates the device descriptor with
`fcntl(F_DUPFD_CLOEXEC)` and keeps the copy. The shim's descriptor is a token naming the one
device it serves, not a kernel object, so there is nothing to duplicate. It keeps the same token,
which leaves `dev->fd` equal to `dev->flink_fd` - exactly what libdrm's own release path already
expects, so it closes once and that close is a no-op here.

## Why the mapping is nearly one to one

Every FreeBSD thread type is a pointer: thread, mutex, condition variable and mutex attribute are
all pointers to opaque structures, and the vendor calls take pointer-sized handles. So handles
pass straight through and nothing here allocates, copies or wraps. Two exceptions:

- **The key type is an `int`**, not a pointer, which the vendor call takes directly anyway.
- **`pthread_once_t` is a structure**, so unlike everything else it cannot be handed to the
  vendor call: the two layouts are different objects and passing one as the other would corrupt
  whatever follows it. That one call is implemented here over a mutex rather than delegated.

## The two conventions that do not line up

**Arity.** Three vendor twins take a trailing name the POSIX form has no place for: create, mutex
init and cond init. Delegating by a rule would read a register the caller never set. orbistoun
hit that and recorded it; each name here is written out and oops-sdk's declarations already carry
the right shape.

**Failure.** POSIX answers zero or an errno, the vendor answers `0x8002_0000 | errno`. That
scheme is measured, not assumed - orbistoun D398 across five families and seven provoked
failures, and obSCEne's `posixerr` section relies on it - so failures are translated rather than
flattened. It matters most for `pthread_cond_timedwait`, because Mesa's C11 layer turns
`ETIMEDOUT` into a timeout and anything else into an error, so a timeout reported generically
would look like a broken condition variable.

The deadline conversion goes with it: POSIX takes an absolute deadline, the vendor takes a
relative microsecond timeout, and a deadline already past becomes a zero wait rather than a
negative one that would turn into an enormous unsigned timeout.

## How this file is verified, which is not by a test

The host suite cannot run it: on the build machine our definitions of `pthread_create` and the
rest collide with the host C library's, so there is nothing to link. What stands in its place is
that the file compiles against the platform's own `pthread.h` with warnings on, which means all
26 signatures match the declarations the platform publishes.

That is only worth something if a wrong signature would actually be caught, so it was checked
rather than assumed: changing one argument of `pthread_join` produces
`error: conflicting types for 'pthread_join'` against the header's declaration. The clean compile
is therefore a real check on all 26 and not an absence of evidence.

## What is still assumed here

Ten of the 26 vendor twins have never been called from this collection: they are named `present`
in obSCEne's census or in orbistoun's inventory, and their arity follows the POSIX form they
mirror. That is written at the declaration rather than left implicit.
`REQ-20260914T1443Z-3ea7` would settle the whole group, including whether the portable names
bind directly and make this file unnecessary.

## Next

The startup path is now complete except for `GB_ADDR_CONFIG`, and nothing in this repository can
produce that. The remaining unblocked work is the rest of the runtime surface - what Mesa asks of
a C library beyond threads - which unit 3's build will name as soon as anything links.
