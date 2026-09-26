# Mesa's thread surface

Firmware 12.40. Computed from source, without a hardware run, 2026-09-14.

Mesa's only caller of `pthread_*` is its C11 threads layer, `mesa/src/c11/impl/threads_posix.c`
at the pin in `dependencies.mk`. That file needs 26 thread functions and six other names. Every
one of the 26 has a vendor twin on this platform; the last column gives the evidence.

## The thread functions

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

The obSCEne census is its libkernel census of 2026-08-30. The three `mutexattr` names are in
orbistoun's libkernel symbol inventory, exercised by its own suite, and absent from the mined
census.

Three vendor twins - create, mutex init and cond init - take a trailing `const char *name` that
the POSIX form lacks, so each binding takes its arity from the vendor declaration
(`oops-sdk/src/thread/thread.c`), one name at a time.

## Other names

`clock_gettime`, `timespec_get`, `nanosleep`, `sched_yield`, `malloc`, `free`. `malloc` and `free`
are measured bound from the platform's C library (obSCEne census, section `035-libc`).
`sched_yield` maps to `scePthreadYield`, bound by oops-sdk. The other three are unconfirmed here.

The portable `pthread_*` names exist and retail titles import them from `libScePosix`, which
obSCEne measures does not load in the app sandbox. Whether libkernel serves them under either
spelling is unmeasured.
