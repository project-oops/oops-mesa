# 045. The census was answering about a different machine, and Mesa was compiling on one thread

**2026-09-17** - roadmap units 4 and 7

## What this entry is

Both requests filed this evening resolved at 20:45Z, and between them they close the last of the
C-runtime surface and one real performance defect.

The interesting half is *why* the census and the sweep disagreed, because the reason is not a
mistake in either.

## The 33 names: dynamic resolution is authoritative, and the census described GEN=4

`REQ-20260917T1818Z-9f41` asked which of two of this collection's own measurements decides whether
a title's import binds. The answer:

**String and NID dynamic resolution is authoritative. All 33 are unbindable.** An import table
referencing any of them fails to link dynamically or dies on the first call with
`PRX_NOT_RESOLVED_FUNCTION`.

And the reconciliation is one nobody had proposed. The census rows in
`obscene/data/hardware/ps5-imports.txt` were captured under **GEN=4** - the PS4
backward-compatibility container, running Orbis userland - where `libSceLibcInternal` genuinely
did export the C runtime and the maths. Native Prospero replaced it with a stripped PRX that
dropped them from the dynamic export table. Both measurements were correct about the machine they
were taken on; only one of them was about the machine a title runs on.

**Census presence never implied bindability.** That is worth more than the 33 names: the same
census places imports for every OOPS title, not just Mesa ones, and the request said so when it
was filed. The generalisation is now measured rather than suspected.

### The rule that was guessed at held, which is the useful part

Worklog 042 defined six of the 33 while the conflict was still open, under a rule invented for
the occasion:

> a symbol whose value is fully specified can be defined locally without prejudicing an open
> measurement; a symbol that describes the platform cannot, and stays an import.

It picked exactly the safe subset. The six were IEEE predicates - bit tests with one right answer
- and they were correct to define early whichever way the conflict went. The eleven it refused to
cover turned out to be precisely the ones that needed the answer: a locale table, a filesystem, a
clock, a program name, two signal-set operations.

That is a rule worth keeping for the next disagreement, and it is now written into D011 rather
than left in a worklog.

## The eleven, and the one that was load-bearing

`_CurrentRuneLocale` is the one that mattered. On this platform `ctype.h` does **not** call into
the C library for `tolower` and its kin - it inlines `__getCurrentRuneLocale()`, which reads a
global `_RuneLocale` table. So a title calling `tolower` references that global directly, and
Mesa does: four objects reach it, `ac_gpu_info.c` among them, which is on the startup path. This
is the same `ctype` path that produced the original crash in `si_init_renderer_string` back in
worklog 038.

The table is upstream's own. `lib/libc/locale/table.c` from the checkout D004 pins, staged and
compiled for the target exactly as msun is (D011's precedent), producing `librune.a` with
`_DefaultRuneLocale` and `_CurrentRuneLocale`. Writing a 256-entry rune table by hand was the
alternative and it is not one: the whole value of staging is that the table is identical to the
one the headers describe.

It carries one passenger. `__runes_for_locale` comes in the same object and references two
libc-private locale structures, and carving it out would mean editing a file whose point is being
unedited. Nothing in this link calls it - measured, zero references across every Mesa archive and
every staged library - so `libc_absent.c` defines the two as placeholders that say plainly they
are placeholders, and say that if either is ever read the locale API has become live and they are
wrong.

The other ten:

| | |
|---|---|
| `__mb_sb_limit` | 256, the C locale's value. Read by the inlined `ctype` functions themselves before they index the table (`_ctype.h:106`, `:139`) |
| `time` | over `clock_gettime`, which **is** exported and measured at `0x800001210` - a unit conversion onto a real call, like `usleep` |
| `sigfillset`, `sigdelset` | bit manipulation on `__uint32_t __bits[_SIG_WORDS]`, fully specified by the header Mesa already compiles against |
| `getprogname` | the title's own identifier, which the build knows. Not a stub: it is the true answer |
| `opendir`, `readdir`, `closedir`, `devname_r`, `system` | loud stubs - the cache paths, the descriptor walk and the disassembler hand-off, none of which a title can have |

**Result: no symbol this platform cannot bind is imported by a title any more.** 181 undefined
symbols at the start of the day are 106, and of the 68 obSCEne found absent, the only two still in
the list are `operator` and `std` - which are not symbols, but demangled C++ names truncated by
the extractor that built the original request.

## Mesa had been compiling shaders on one thread

`REQ-20260917T1845Z-6b3e` came back with numbers: **16** hardware threads on this part (8 Zen 2
cores, two-way SMT), of which a big-app container gets **14**, affinity mask `0x3fff` - one
physical core held back for system services.

The cost of not knowing was specific. `u_cpu_detect.c:852` reads
`nr_cpus = MAX2(1, available_cpus)`, so the `-1` this shim had been returning told Mesa the
machine had one processor, and `nr_cpus` is what sizes its thread pools. Shader compilation had
been single-threaded on an eight-core part, silently, since the first run.

**The available count is queried, not stated.** The resolution is explicit that it varies - 12 and
mask `0x0fff` under some system workloads - and a constant that is right today and wrong under
load is exactly what this project should not compile in. `cpuset_getaffinity` is the call, and its
placement is evidenced twice: obSCEne puts it in `libkernel` at `+0x20f0`, and the mined corpus
independently agrees, which is the manifest line the build then produced. The measured 14 is the
fallback when the mask cannot be read, and it says so in the log when it is used.

`sysctlbyname("hw.ncpu")` is what the resolution suggests first, and it is not available: that
name is one of the 35 absent in both measurements, so a title cannot bind it. The two resolutions
have to be read together, which is an argument for reading a whole bus rather than one's own
requests - the lesson of worklog 044, arriving again the same evening.

`_SC_NPROCESSORS_CONF` is stated as 16, because it is a property of the part rather than the
container and the call that would report it is the one that cannot be bound.

## State

`mesa-probe` and `dri-probe` relink, place 470 imports with 0 unknown, and package;
`dri-probe`'s module is 28,499,272 bytes. `./bin/oops-mesa check` passes, pin `mesa-26.2.2`,
2 patches, 46 entries in `link-order.txt`. Identity guard clean on oops-mesa and oops-apps.

Nothing has run on hardware. The console still carries the 17:15 module, which predates libm, the
runtime names, the C++ ABI change, the GL entry points, the parking ending, the locale table and
the thread count.
