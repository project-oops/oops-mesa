# 023. The second null in the function that already killed this title

**2026-09-17** - roadmap unit 5

## What this entry is

Worklog 022 said the next hardware run's whole value is the diagnostics it produces, and filed
`-5c9d` about whether they surface. Reading `mesa-probe` with that in mind turned up something
better than a diagnostics question: a crash, in the same function that killed this title on
2026-09-16, one platform call further along.

It is fixed, through upstream's own seam, without a patch. A second instance of the same defect is
found and **not** fixed, because it is currently unreachable and the reason it is unreachable is
itself worth recording.

## `driParseConfigFiles` wants an executable name, and this platform may not have one

Worklog 019's crash was `driParseConfigFiles` reaching `getenv` through `os_get_option`. The fix
was to define `getenv` and have it answer null honestly. That is correct, and it moves execution
three lines down the same function:

```c
if (!execname) execname = os_get_option("MESA_DRICONF_EXECUTABLE_OVERRIDE");
if (!execname) execname = util_get_process_name();
```

With no environment, the first yields null and the second runs. On a FreeBSD target that is:

```c
const char *program_name = getprogname();
if (program_name) return strdup(program_name);
return NULL;                                    /* mesa/src/util/u_process.c */
```

That null propagates to `data->execName`, and `parseStaticConfig` then walks the entire built-in
driconf database - every application entry Mesa ships - calling `parseAppAttr`, which does:

```c
if (exec && strcmp(exec, data->execName)) {     /* mesa/src/util/xmlconfig.c */
```

`exec` is checked. `data->execName` is not. Hundreds of entries carry an `executable` attribute, so
the first one reached dereferences null.

Whether that happens depends entirely on what `getprogname()` returns in a title started by the
system loader rather than by a shell. The symbol resolves - it is in the import manifest, placed in
`libSceLibcInternal` - so the title loads either way. An empty string is harmless; a null is a
segfault. Nothing in the collection has measured which.

## Fixed by telling Mesa the answer instead of asking whether it can find it

Mesa exports a setter for exactly this, and uses it in its own tests:

```c
static const char *execname;
void driInjectExecName(const char *exec) { execname = exec; }
```

`mesa-probe` now calls `driInjectExecName(OOPS_APP_NAME)` before building the option cache. With
`execname` already set, neither the environment lookup nor `util_get_process_name` runs, and
`data->execName` cannot be null.

This is the better fix and not only the safer one. The title *knows* what it is called -
`OOPS_APP_NAME` is its own build identity, already on the compiler command line - so injecting it
states a fact rather than working around a question. It needs no patch, because it uses the seam
upstream put there. And it removes the dependency on `getprogname`'s behaviour rather than
gambling on it, which matters because the gamble would have been resolved by a launch.

`-5c9d` is still worth answering, and is unaffected: it asks whether failure messages surface, not
whether this particular failure happens.

## The second instance, and why it is not fixed

`util_get_process_name()` has another unguarded caller, in radeonsi itself:

```c
static bool is_process_name_param(const char *name, const char *param)
{
   if (!strstr(util_get_process_name(), name))    /* si_state.c:4825 */
```

`strstr(NULL, ...)` faults the same way. It is reached from `si_init_graphics_preamble_state`,
which is **context** creation rather than screen creation - beyond the milestone worklog 022
defines, and squarely in unit 6.

It is also, right now, unreachable for a second reason, and this is the part worth writing down:

```c
.gfx10.cache_db_gl2 = sctx->gfx_level >= GFX10 && sscreen->options.cache_db_gl2 &&
                      !is_process_name_param("GpuTest", "fur"),
```

`&&` short-circuits, so the call happens only if `cache_db_gl2` is true. Upstream's default for it
*is* true - `si_debug_options.h:23`. But `mesa-probe` declares one option, `radeonsi_zerovram`, and
`driQueryOptionb` on an undeclared name returns the zeroed value of an empty hash entry, which is
false. The asserts that would have caught this are compiled out.

So the null dereference is masked by the option cache being incomplete, and **completing the option
cache is what would expose it**. That is an unusually unpleasant shape: making the configuration
more correct activates a latent crash, and the change that does it looks unrelated to the crash it
causes. Recorded here so that whoever widens `mesa_probe_options` - which is the obvious next
improvement to that file - knows what they are turning on.

No fix is written for it today. It needs one of: a patch adding the null guard upstream wants
anyway, a `MESA_PROCESS_NAME` answer (impossible, there is no environment), or a runtime shim that
makes `util_get_process_name` answerable. Which of those is right depends on how many more of these
there are, and that is a question for when unit 6 starts rather than a guess now.

## Two smaller things in the same file

**The stale prediction.** `mesa_probe_main.c`'s header comment said "where this title stops next is
not predicted anywhere in the collection". Worklogs 021 and 022 predict it in detail. The comment
now says what is actually true: the stopping point *is* predicted, and the reason to run is that
nothing in the prediction has met the hardware and every answer the winsys gives is a value this
collection decided rather than read off the silicon.

**The duplicated parse, kept on purpose.** `radeonsi_screen_create` calls `driParseConfigFiles` on
the same cache the title already parsed into, and `initOptionCache` mallocs unconditionally, so the
title's call leaks its allocation once. That is now stated in the file rather than left to be
discovered, along with why it stays: reaching the line after it proves the option path survived,
which separates "died building the option cache" from "died inside radeonsi" without needing either
to say so - and given `-5c9d`, not needing Mesa to say so is worth a few hundred bytes.

**The failure message no longer overclaims.** It used to say the winsys lines above name the command
radeonsi stopped on. That is true only when radeonsi stopped on something the winsys *refused*. Most
of worklog 021's table is answers the winsys gives successfully and Mesa may then reject, and in that
case the winsys logs nothing because from its side nothing went wrong. The message now says both
cases and says that an absence of winsys lines is itself a fact about where the failure was.

## State

`mesa-probe` builds; import manifest unchanged at 504 placed, 0 unknown. oops-mesa's host suite is
untouched at 90 of 90 - none of this is winsys code. Nothing deployed.
