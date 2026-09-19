# 036. The answer was no, and counting the sources was the wrong count

**2026-09-17** - roadmap unit 5

## What this entry is

`REQ-20260917T0233Z-5c9d` came back. The answer is the bad one, and acting on it turned up a
mistake in this repository's own reasoning that a test caught before it shipped.

## The measurement

Resolved 2026-09-17T09:20Z, sweeps `20260917-043235` (payload) and `20260917-095820` (eboot).
Both legs, four routes each:

| | payload leg | eboot leg |
|---|---|---|
| `write(1)` | 22 bytes, **surfaces** | 22 bytes, **does not appear** |
| `write(2)` | 22 bytes, **surfaces** | 22 bytes, **does not appear** |
| `dup2(1,2)` then `write(2)` | 22 bytes, **surfaces** | 22 bytes, **does not appear** |
| `sys_call(SYS_klog)` | - | **surfaces reliably** |

Every write returns the full byte count with `errno` zero. The descriptors are open in title space
and accept bytes into nothing: no error to notice, no output to read.

The controls are both there and both positive, which is what makes this usable. `SYS_klog`
surfacing in the same eboot sweep proves the capture was working, so "does not appear" is a fact
about the descriptor rather than about the log. And all three surfacing on the payload leg proves
the writes and the markers were well formed. This is a clean result and it settles the question the
request asked.

It also explains a detail nobody had explained: `oops_klog` writes `SYS_klog` *and* `write(1)`. On
the eboot leg the second of those has never done anything.

## Which makes Mesa silent, so the shim now intercepts

`src/runtime/stderr_to_klog.c` takes the stdio entry points Mesa reaches stderr and stdout
through, accumulates them into lines, and puts them on klog.

Both streams, not just stderr: the measurement found stdout equally dead, and a diagnostic is no
less lost for having gone to the other descriptor.

## The mistake, which is the reason to write this entry

The first version intercepted `fprintf` and `vfprintf`. That choice had a number behind it - a
count across `ac_*`, radeonsi, the amdgpu winsys, `src/util` and libdrm's amdgpu layer:

```
281  fprintf(stderr, ...)
  5  vfprintf(stderr, ...)
```

**That was a count of the sources, and the sources are not what the archives call.** clang rewrites
`fprintf(f, "literal")` into `fwrite`, `fprintf(f, "%s", s)` into `fputs`, a one-character format
into `fputc`, and `printf("...\n")` into `puts`. Mesa is built at `-O1` here, so the transformation
applies.

A host test caught it, and only because the test called `fprintf` itself and watched its own
interception get bypassed. The right count is `nm -u` against the built archives:

```
libamd_common.a     21 fprintf  16 fwrite   6 fputc  2 fputs  2 puts  2 vfprintf
libmesa_util.a      10 fprintf   8 fwrite   5 fputc  2 fputs  6 fflush
libradeonsi.a        8 fprintf   8 fwrite   6 fputc  1 fputs  4 puts  2 vfprintf  1 perror
libdrm_amdgpu.a      1 fprintf
libamdgpuwinsys.a    1 vfprintf
```

Seven entry points, not two. The two-function version would have carried roughly half the
diagnostics and dropped the rest without saying so - which is the exact failure mode the whole
request was filed to avoid, reproduced one level down.

The general lesson is worth keeping: **for a question about what a binary calls, ask the binary.**
The same mistake shape as worklog 024's transcribed constants and worklog 030's one-block
extrapolation, and the third time this week that checking beat reasoning.

## How far the lesson goes, which is not as far as it first looks

Having been caught once, the obvious worry is that every other source-level grep in this
repository's recent entries is suspect. Checked, and it is not - but the boundary is worth naming
so that nobody re-verifies everything.

Worklog 027 claimed `WAIT_FENCES` and `FENCE_TO_HANDLE` have no Mesa caller at all, from a grep of
the sources. Against the archives:

```
amdgpu_cs_wait_fences         [defined in libdrm_amdgpu.a, referenced by nothing]
amdgpu_cs_fence_to_handle     [defined in libdrm_amdgpu.a, referenced by nothing]
amdgpu_bo_import              referenced by libamd_common.a
amdgpu_create_bo_from_user_mem  referenced by libamd_common.a
```

Both claims hold, and the sharing entry points that worklog 027 said were present-but-refused are
indeed linked in.

The difference is which functions a compiler is allowed to rewrite. `fprintf`, `puts` and their
kin are *builtins*: clang knows their semantics and substitutes cheaper ones, so a source-level
count of them means nothing. `amdgpu_cs_wait_fences` is an ordinary library call the compiler has
no opinion about, and a grep for it answers the question.

So the rule is narrower than "never trust a grep": **counting builtins needs `nm`; counting
ordinary calls does not.** Worth knowing, because verifying everything against the archives would
cost more than it returns.

## What `fputc` forces

Line buffering. A message emitted a character at a time would otherwise become one klog line per
character, so writes accumulate and are emitted at each newline, when the buffer fills, or on
`fflush`. Long lines are split across klog writes rather than truncated, because a klog line is
dropped silently past about 128 bytes (orbistoun#539) and the tail of a diagnostic is usually the
useful half.

`putc` and `putchar` are deliberately absent: both are macros in this platform's `stdio.h`, so
functions by those names do not compile - and `nm -u` references none of `putc`, `putchar` or
`__sputc`, because clang's rewrite emits `fputc`.

A write to a stream that is neither stdout nor stderr says so once and drops the bytes. There is no
route to pass it through, because this file *is* `fwrite` and there is no second name for the
original - and nothing in this stack writes to a file, so an uncaptured stream arriving here is
something new rather than something to be quietly handled.

## Checked, and the check is in the repository

`tests/runtime_test.c`, fifteen checks against the real source with a fake `oops_klog`, calling the
way the archives do rather than the way the sources read: `fputs` and `fwrite` lines, thirty-four
`fputc` calls becoming one line rather than thirty-four, `puts` on stdout, three newlines in one
write becoming three lines in order, a 399-byte line spanning several writes with every byte
surviving, a partial line held until `fflush`, and a bare newline producing nothing.

It is **known to be able to fail**, which is the part worth saying: its first run is what caught
the two-function version, with six checks red because the interception was being bypassed. A test
that has never failed has not been shown to test anything.

Two things about how it is wired, both of which are the point rather than plumbing:

**It is a separate binary.** The subject defines `fprintf`, `fwrite`, `fputs`, `fputc`, `puts`,
`fflush` and `perror`. Linking it into the winsys suite would redirect *that* suite's reporting
into the log sink and it would appear to pass silently.

**It reports with `write(1, ...)`, not `printf`.** For the same reason the two-function version was
wrong: clang rewrites `printf("literal\n")` into `puts`, which the subject defines, so a reporter
written with `printf` would be swallowed by the code it is testing - intermittently, depending on
whether a given call had a format specifier. `write` cannot be rewritten into something
intercepted.

`make test` now runs both suites: 104 winsys checks and 15 runtime ones.

The title builds and its import manifest is 500 placed, 0 unknown - five fewer than before,
because these names are now satisfied locally rather than imported.

## State

`make check` passes. `make test` is 119 checks across two suites, 0 failed. The interception is
tested on the host and has never run on the console. Nothing deployed.

What changes for the next run: a failure inside Mesa now has somewhere to be read. That was the
whole point of asking.
