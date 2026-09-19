# 042. The screen has a citation, and the arithmetic has a library rather than a set of opinions

**2026-09-17** - roadmap units 5 and 7

## What this entry is

Two things, and the second is much larger than it looked.

The milestone worklog 040 was working towards is **reached and now citable**: radeonsi creates a
screen on the hardware. And the question behind the crash that follows it - which of the names a
Mesa title imports will actually resolve - was asked of the whole import table at once instead of
one name at a time, which turned 181 undefined symbols into 122 and left **no measured trap risk
at all**.

## The milestone, and why it needed a file written for it

`radeonsi created a screen: the startup path is complete` is `mesa_probe_main.c:206`, reached only
when `radeonsi_screen_create` returns non-null. That is exactly unit 5's stated milestone: a
non-null `pipe_screen` on firmware 12.40.

It had already happened. What it did not have was a citation, because the run was interactive and
the log was read off the console - there is no obSCEne report to point at, and principle 3 does
not have an exception for a fact that arrived in a conversation. So the lines are now in
[`docs/hardware/screen-created-fw1240.md`](../hardware/screen-created-fw1240.md), unedited, and
the ROADMAP points at them.

Reading that capture properly - rather than for the one line I was looking for - also turned up
two things nobody had noticed:

- **`sysconf(57)` and `sysconf(58)` were asked and refused.** They are the processor counts, and
  `u_cpu_detect.c:852` turns the refusal into `nr_cpus = 1`. Mesa has been sizing its shader
  compilation pool for a single-core machine. Filed as REQ-20260917T1845Z-6b3e, because the fix
  needs a measured number and inventing one is worse than the `-1`.
- **`__xuname` is reached in ordinary start-up**, not only from a debug dump. The stub is loud
  precisely so this would show, and it did.

The fault that follows is strictly after the probe's own `done`: `rip` zero on return from the
entry point, which is obscene REQ-20260917T1450Z-2e71. *(That request resolved later the same day,
after this entry was written, and the answer changed two things here - see worklog 044.)*

## The import table, asked once instead of sixty times

Every previous entry in `libc_absent.c` was found the same expensive way: a title died on the
console, the log named one symbol, the symbol got a definition. That loop is fine for six names
and absurd for sixty.

So the question was asked properly. `--error-limit=0` gives the complete undefined set (**181**),
obSCEne swept all of it for export on firmware 12.40 (REQ-20260917T1640Z-5b28, controls 3 of 3 on
both legs), and 68 came back absent. Crossing those two lists is the thing that had never been
done, and it splits cleanly in a way that decides what to do with each half:

| | count | what it is | action |
|---|---|---|---|
| absent in the sweep **and** in this collection's export census | 35 | genuinely not there | define locally |
| absent in the sweep, **present** in the census | 33 | two of our own measurements disagreeing | filed, not guessed |

The 33 are REQ-20260917T1818Z-9f41. The conflict is not a broken run - the sweep's controls
passed - and `-5b28`'s own `sysctlbyname` note contains the likely shape of the answer: a name can
sit in an export table as an internal stub that a dynamic lookup cannot resolve. If that holds,
census presence and bindability are different properties, which matters far beyond this project,
because `data/mined-names.txt` places imports for every OOPS title.

Of the 35, one was already handled, and **two were not symbols at all**: `operator` and `std`, the
truncated remains of `operator new(unsigned long)` and a `std::` name, produced by the extractor I
built the original request with. obSCEne measured exactly what I asked for. The ask was malformed,
which is worth writing down because the same extractor produced the numbers in this table.

## Fourteen of them were arithmetic, and that is a library, not a shim

`sin`, `cos`, `floor`, `ceil`, `log`, `log10`, `atan2`, `powf`, `round`, `trunc` and their float
twins are absent from this platform, in both measurements. Nine more the sweep never covered -
`acosf`, `expf`, `logf`, `log2`, `lround` and their kin - were undefined in the link as well.

Writing those by hand was never a real option. `pow` in this file already was one: repeated
multiplication, exact over the two whole-number exponents its callers actually use, loud about
refusing a fractional one. That is an honest shim and it was the right thing while nothing better
existed. Twenty-three of them, including `sin` and `log`, is a different proposition - a
correctly-rounded transcendental is a project, and an almost-correct one is the plausible-looking
answer principle 4 exists to refuse.

**The matching implementation was already on disk.** D004 pins a FreeBSD checkout for the headers
Mesa compiles against, and `lib/msun` in that same checkout at that same revision is the libm
those headers describe. `stage-sources.sh` already takes subtrees from it for libelf and libc++,
so libm is a third entry of an established shape rather than a new dependency: `git archive` out
of the pinned object store, compiled for the target in the container, `libm.a` into the sysroot,
`-lm` on the title's link line.

Scoped to **double and float only**. No `ld80`, no long double: Mesa's GL and GLSL paths do not
use it, and the consequence is chosen rather than accepted - a reference to `sinl` is now an
undefined symbol at this desk instead of a number computed at the wrong precision on the console.
The `.S` files under `amd64/` are skipped too; they are optimisations of functions that all have C
implementations, and FreeBSD's own build makes them conditional for that reason.

`pow` came out of `libc_absent.c` as a consequence, and not by choice: the linker reported it
defined twice and stopped. That is the better failure, and msun's `e_pow.c` is correct over the
whole domain rather than the two shapes that happened to be called.

### Two bugs in my own recipe, both quiet

Neither would have failed loudly, which is the only reason they are worth a section.

**`bsdsrc/*.c` compiled three files where FreeBSD compiles one.** `b_tgamma.c` `#include`s
`b_log.c` and `b_exp.c` as source. Globbing the directory built those two a second time,
standalone, without the definitions the including file provides, and they failed on `copysign` and
`ldexp` being undeclared. This one did at least stop the build.

**The long-double exclusion caught three double-precision functions.** Skipping `*l.c` is the
obvious way to drop long double and it is wrong, because three of these files end that way for a
completely different reason: the functions are called `ceil`, `creal` and `isnormal`. The first
build silently shipped a `libm.a` with no `ceil` in it - and `s_isnormal.c` is where `__isfinite`,
`__isnormal` and their float twins live, which are **five of the 33 disputed names**. Excluding it
would have left five symbols riding on an unresolved conflict for no reason at all.

The test is now that a long-double source is the twin of a double one beside it: `s_ceill.c` next
to `s_ceil.c`, so the twin has to exist for the exclusion to hold. `s_ceil.c` has no `s_cei.c`
beside it, so it is built. 198 objects became 201, and the archive went from 255 defined symbols
to 260.

### Six of the disputed names stopped being disputed

There is a rule in this, and it is worth stating because it will come up again: **a symbol whose
value is fully specified can be defined locally without taking a side in the conflict**, because
the local definition is correct whichever measurement turns out to be right. The IEEE predicates
are exactly that - `__isfinite`, `__isinff` and their relatives are bit tests with one right
answer - so five come from msun and `__isinff` is written out in `libc_absent.c`, because FreeBSD
keeps the `isinf` family in libc rather than msun.

The rest of the 33 stay imports on purpose. `_CurrentRuneLocale` is the platform's own locale
table and a local one would be a different table, not a copy. The three C++ ABI entries need a
real runtime. Defining either kind would replace an open question with a wrong answer that links.

## The other eighteen

Four are real implementations because their paths are live: `strcpy` and `strcat`; `usleep` as a
unit conversion onto `nanosleep`, which is exported and measured; and `strtod` delegating to
`sscanf`, which is also exported and measured. That last one is the interesting choice - Mesa
parses GLSL float literals through `strtod`, so correct rounding decides a shader's constants, and
the platform's own `sscanf` is the same library's conversion rather than an approximation of it.
`%n` supplies the `endptr` the contract needs.

`exit` does not return, and traps. Nothing in oops-sdk or oops-apps' entry path calls it, so this
does not stand between a title and its ordinary completion - and there is no exit to delegate to,
because both ways a big-app container can try to finish are measured and both crash (`-2e71`).
*(The trap was the wrong remedy for a correct observation. `-2e71` resolved hours after this was
written and named the conforming ending; `exit` now parks instead. Worklog 044.)*

Twelve are loud stubs, each checked for its caller first rather than written off for looking
obscure: the cache and shader-dump paths, the debug dumps, the disassembler's `popen` hand-off,
`openlog` beside an exported `syslog`, and the two generic escape hatches. `syscall` deliberately
does not forward - a raw call by number needs a numbering this platform declines to name
(`kern.osrelease` reads `"0.0-prototype"`, D004) and a wrong number is not a failed call, it is a
different call.

`write` is the one that is not in this file. It went into `stderr_to_klog.c`, where the line buffer
it needs is in scope, because it is the bottom of that file's stream capture rather than a
C-library gap. The measurement decides its shape: on the eboot leg `write(1)` and `write(2)`
**succeed** and nothing surfaces, which is the worst available shape for a diagnostic, so the
bytes now go where every other stream in that file already goes.

## What it measures out to

| | before | after |
|---|---|---|
| undefined symbols in the linked title | 181 | **116** |
| absent-in-both names still imported | 32 | **0** (`operator` and `std` are not symbols) |
| disputed names still imported | 33 | **11** |
| module | 28,209,368 bytes | 28,251,944 |

Sixty-five names left the import table. The 42 KB the module grew is the arithmetic the platform
does not have.

The eleven disputed names still imported are the ones where a local definition would be a
different answer rather than the same one: `_CurrentRuneLocale` and `__mb_sb_limit` are the
platform's locale data, `opendir`/`readdir`/`closedir` describe a filesystem, `time`,
`getprogname`, `devname_r` and `system` describe the platform, and `sigdelset`/`sigfillset`
manipulate a `sigset_t` the kernel also reads - which D004's generation mismatch makes exactly
the wrong thing to reimplement from a header. Those wait for `-9f41`, and none of them is on a
path this stack executes.

The title relinks, places all 480 imports with **0 unknown**, and packages. `./bin/oops-mesa
check` passes, pin `mesa-26.2.2`, 2 patches, 46 entries in `link-order.txt`.

## What has not happened, and what is next

None of this has run on hardware. The console still carries the 17:15 module, and the right next
run is the first one that exercises any of it - but nothing here is a reason to launch on its own,
and stacking two changes into one run is the mistake this project has already paid for twice.

## The C++ ABI turned out not to need a library at all

That was going to be the next unit, and the plan was wrong. `__cxa_begin_catch`,
`__cxa_pure_virtual` and `__gxx_personality_v0` were unresolved imports sitting on the `-9f41`
conflict, libcxxrt was already staged, and building it looked like the same move that settled the
arithmetic. Asking which archive actually references each one is what made that unnecessary:

- **Mesa is built `-fno-exceptions`**, 139 times over in its own ninja file, and **no Mesa archive
  references `__cxa_begin_catch` or `__gxx_personality_v0` at all.** Both came from
  `src/runtime/cxx_support.cpp` - ours - which states in its own comments that it does not throw
  and was nonetheless being compiled *with* exceptions, so clang emitted the cleanup machinery
  anyway. Compiling it the way it is written removes both outright. Measured: two exception-ABI
  undefineds become none, and nothing else about the object changes.
- **`__cxa_pure_virtual` is genuinely referenced**, by four objects across `libglsl.a` and
  `libaddrlib.a` - and from their *vtables*, not from any call site. It is now defined in
  `cxx_support.cpp` and does not return, because its behaviour was never the platform's to decide:
  reaching it means a vtable slot was still the pure one when it was called, which is a lifetime
  bug in the caller. There is no platform-specific right answer to import.

So the exception ABI is not needed, not stubbed, and not imported - which is a better outcome than
building an unwinder for a stack that never throws. `upstream_stdexcept.o` still needs the full
ABI and still keeps exceptions, and costs a title nothing because nothing references it.

Three more of the disputed names went the same way as the IEEE predicates, by the same rule:
`bcmp`, `bzero` and `strnlen` each have exactly one correct implementation, and unlike their
neighbours they are on paths Mesa reaches constantly. Leaving a hot path resting on an unresolved
measurement is the trade this file exists to avoid.

## What is actually next

Nothing here has run on hardware. The console still carries the 17:15 module, and the right next
run is the first one that exercises any of this - but no single change here is a reason to launch,
and stacking two changes into one run is the mistake this project has already paid for twice.

Unit 6 is where the work is: something has to call `oops_gl_create`, which nothing does yet.
`sceVideoOutRegisterBuffers2` remains the open unknown underneath `oops_gl_present`, and it cannot
be asked until a surface exists to ask about.
