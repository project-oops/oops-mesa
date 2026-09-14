# Mesa's thread surface, and what the platform already answers

**2026-09-14** · firmware 12.40 · roadmap unit 4's work list, computed without a hardware run

Mesa does not call the platform's threads directly. It calls its own C11 threads layer, and that
layer is the only thing in Mesa that touches `pthread_*` (`mesa/src/c11/impl/threads_posix.c` at
the pin in `dependencies.mk`). So the runtime shim's thread surface is exactly that file's
requirements, and it is closed: **26 functions**, listed below, plus six other names.

Every one of the 26 has a vendor twin on this platform. **None is missing.** What varies is only
how well each is evidenced, which the last column states.

## The 26

| Mesa calls | vendor twin | evidence |
|---|---|---|
| `pthread_create` | `scePthreadCreate` | bound and working in oops-sdk; takes a trailing name |
| `pthread_join` | `scePthreadJoin` | bound and working in oops-sdk |
| `pthread_detach` | `scePthreadDetach` | bound and working in oops-sdk |
| `pthread_self` | `scePthreadSelf` | bound and working in oops-sdk |
| `pthread_equal` | `scePthreadEqual` | bound and working in oops-sdk |
| `pthread_mutex_init` | `scePthreadMutexInit` | bound and working in oops-sdk; takes a trailing name |
| `pthread_mutex_destroy` | `scePthreadMutexDestroy` | bound and working in oops-sdk |
| `pthread_mutex_lock` | `scePthreadMutexLock` | bound and working in oops-sdk |
| `pthread_mutex_trylock` | `scePthreadMutexTrylock` | bound and working in oops-sdk |
| `pthread_mutex_unlock` | `scePthreadMutexUnlock` | bound and working in oops-sdk |
| `pthread_cond_init` | `scePthreadCondInit` | bound and working in oops-sdk; takes a trailing name |
| `pthread_cond_destroy` | `scePthreadCondDestroy` | bound and working in oops-sdk |
| `pthread_cond_wait` | `scePthreadCondWait` | bound and working in oops-sdk |
| `pthread_cond_timedwait` | `scePthreadCondTimedwait` | bound and working in oops-sdk |
| `pthread_cond_signal` | `scePthreadCondSignal` | bound and working in oops-sdk |
| `pthread_cond_broadcast` | `scePthreadCondBroadcast` | bound and working in oops-sdk |
| `pthread_exit` | `scePthreadExit` | obSCEne census, `present` in libkernel |
| `pthread_once` | `scePthreadOnce` | obSCEne census, `present` in libkernel |
| `pthread_key_create` | `scePthreadKeyCreate` | obSCEne census, `present` in libkernel |
| `pthread_key_delete` | `scePthreadKeyDelete` | obSCEne census, `present` in libkernel |
| `pthread_getspecific` | `scePthreadGetspecific` | obSCEne census, `present` in libkernel |
| `pthread_setspecific` | `scePthreadSetspecific` | obSCEne census, `present` in libkernel |
| `pthread_mutex_timedlock` | `scePthreadMutexTimedlock` | obSCEne census, `present` in libkernel |
| `pthread_mutexattr_init` | `scePthreadMutexattrInit` | orbistoun's libkernel symbol inventory |
| `pthread_mutexattr_destroy` | `scePthreadMutexattrDestroy` | orbistoun's libkernel symbol inventory |
| `pthread_mutexattr_settype` | `scePthreadMutexattrSettype` | orbistoun's libkernel symbol inventory |

Sixteen are already bound by a payload that runs. Seven more are named `present` in obSCEne's
libkernel census of 2026-08-30 and only need a declaration. The last three appear in orbistoun's
libkernel symbol inventory but not in obSCEne's mined census, which is a gap in the census rather
than evidence against them; orbistoun's own suite exercises all three. They are the three to bind
first, because they are the three that could still surprise.

## The six other names the same file needs

`clock_gettime`, `timespec_get`, `nanosleep`, `sched_yield`, `malloc`, `free`. `malloc` and
`free` are measured bound from the platform's C library (obSCEne census, section `035-libc`).
`sched_yield` has `scePthreadYield`, already bound by oops-sdk. The other three are unconfirmed
and belong with unit 3's wider C-runtime list rather than here.

## The trap this table exists to carry

Three vendor twins take a trailing `const char *name` that the POSIX form has no place for:
create, mutex init and cond init. A shim that delegates by a rule rather than per name reads a
register the caller never set. orbistoun hit exactly this and recorded it (its D385 and D475),
and oops-sdk's own declarations in `src/thread/thread.c` already carry the correct arity. Unit 4
takes its arity from the vendor declaration, one name at a time, never from a pattern.

## What this does not settle

Whether the portable names bind directly, which would make the mapping table unnecessary. The
portable names exist and retail titles import them, but from the `libScePosix` library, which
obSCEne measured does not load in the app sandbox. `REQ-20260914T1443Z-3ea7` asks whether
libkernel serves them anyway under either spelling. That request is now an optimisation and not
a gate: this table is the answer if it comes back negative, and it is already complete enough to
build against.
