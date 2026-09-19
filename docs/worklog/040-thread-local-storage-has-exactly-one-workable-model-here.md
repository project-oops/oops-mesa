# 040. Thread-local storage has exactly one workable model here

**2026-09-17** - roadmap unit 5

## What this entry is

`MESA00001` died three times in `si_init_renderer_string`, on the same source line, for three
different reasons. Each death named the next one. The end of it is a compiler flag, and the route
there is worth keeping because two of the three answers were measurements that could not have been
guessed.

The line is `tolower(name[i])`. On this platform that reaches thread-local storage in another
module, and only one of the three TLS models can be made to work.

## The three deaths

**First: an unresolved call.** `PRX_RUNTIME_ERROR 0xa0020103`, `rip` inside libkernel, no symbol
named anywhere. Worklog 038 has how it was found - a link map, then the instruction at
`si_init_renderer_string+0x9c`, which carries the `data16 data16 rex.W` prefixes that mark a
General Dynamic call to `__tls_get_addr`.

**Second: a missing variable.** obSCEne measured both halves (REQ-20260917T1640Z-5b28, verified
against the rows rather than the summary): `libkernel` **does** export `__tls_get_addr`
(`libkernel-vaddrs.txt:67`, `+0x3b960`), and `libSceLibcInternal` **does not** export
`_ThreadRuneLocale` - 3,016 of that library's symbols are captured and the only Rune names among
them are `_CurrentRuneLocale` and `_DefaultRuneLocale`.

That is a coherent platform rather than a broken one. A C library with no per-thread locale has
nothing to put in a per-thread locale override. The sysroot header says what the absence means:

```c
extern _Thread_local const _RuneLocale *_ThreadRuneLocale;
static __inline const _RuneLocale *__getCurrentRuneLocale(void)
{
    if (_ThreadRuneLocale)
        return _ThreadRuneLocale;
    return _CurrentRuneLocale;
}
```

So null is not a stand-in for the value, it **is** the value: "this thread has no override", which
is true, and the fall-through returns `_CurrentRuneLocale`, which the platform does export.
Defining it in the title's own TLS block (`src/runtime/libc_absent.c`) states that rather than
substituting for it.

**Third: the model itself.** With the variable defined, the failure changed kind, and the change
is the finding:

| | before | after |
|---|---|---|
| kind | `PRX_RUNTIME_ERROR` | `SIGSEGV`, read of `0x8` |
| `rip` | `0x80003ad99` | `0x80003bac9` |
| frames | 5 | 6 - a new innermost one in libkernel |

`rip` is libkernel `+0x3bac9`, which is `0x169` bytes into `__tls_get_addr` at `+0x3b960`. The
call resolved, the resolver was entered, and it dereferenced null. **The platform exports the
resolver and cannot answer with it**: the per-thread bookkeeping a General Dynamic lookup walks is
not set up for a title's own thread-local block.

Fixing the variable was necessary and was never going to fix the mechanism.

## Which model is left

Mesa compiles `-fPIC`, so every `__thread` access defaults to General Dynamic - the call above.
There are two other models and only one of them links.

**Local Exec does not.** It was tried first:

```
ld.lld: error: relocation R_X86_64_TPOFF32 against _ThreadRuneLocale
        cannot be used with -shared
```

A title is linked `-shared` (`oops-apps/common/app.mk`), and Local Exec bakes an absolute
thread-pointer offset that is only meaningful in an image that is not shared. That is the end of
the road rather than a preference, and it is recorded in the cross file so it is not tried again.

**Initial Exec is the strongest model a shared object may use**, and it is what the build now asks
for: `-ftls-model=initial-exec` in both `c_args` and `cpp_args`
(`toolchain/cross-prospero.ini`). The thread-pointer offset lives in a GOT entry the loader fills
once, and each access is a `%fs`-relative load through it - no call, nothing walked per access.

It is legitimate here rather than a trick. Initial Exec is for thread-locals in an image present
from the start, and after the change above every thread-local Mesa touches is in the title's own
image: its own (`_mesa_glapi_tls_Context`, ACO's instruction buffer) and now `_ThreadRuneLocale`.

## What was checked rather than assumed

- **No archive references `__tls_get_addr`** after the rebuild (`nm --undefined-only` across all
  45). That is the whole point of the flag and it is the only proof it took.
- The thread-local is still allocated in the title's own `.tbss`, at offset `0x1108`, 8 bytes, and
  the module still carries a `TLS` program header with `FileSiz 0` - all `.tbss`, which is what a
  null pointer should be.
- The module shrank from 20,003,048 to 20,002,960 bytes, which is the right direction: an Initial
  Exec sequence is shorter than a General Dynamic one.

`imports.txt` still lists both names, and that is not a signal: it is the union of the 400-entry
shared list and the corpus placements, so a name in `common/symbols.txt` appears whether or not
the module needs it. It listed `_ThreadRuneLocale` while the symbol was locally defined too.

## What this costs a title, and who owes what

**A title linking Mesa needs a `PT_TLS` segment.** That is SELFish REQ-20260915T0001Z-8d72, whose
original ask was right and whose two "it is refused" updates were wrong and are retracted.
`mesa-probe` carries its own `local_tls.ld`; until `native_eboot.ld` grows the segment, every
title linking Mesa needs the same script.

## State

`./bin/oops-mesa check` passes, pin `mesa-26.2.2`, 2 patches. 45 archives rebuilt. `MESA00001`
builds, packages and deploys at 20,002,960 bytes. Whether `si_init_renderer_string` completes has
not been observed yet: the build is on the console and has not been launched.
