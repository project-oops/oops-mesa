/*
 * The two symbols a hosted binary gets from its start-up objects, which this target has none of.
 *
 * On an ordinary system these come from the files the compiler links before and after everything
 * else - crtbegin and its kin. A title here is linked by oops-apps with its own entry point and
 * no such objects, so anything Mesa references from them has to come from somewhere, and this is
 * the somewhere.
 *
 * `__cpu_model` is deliberately not here. It is the compiler's own, taken from its runtime
 * library by the build, because guessing at a structure the compiler owns is the kind of thing
 * that works until it silently does not.
 */

/*
 * The handle a shared object passes to the exit-time destructor registrar, so that destructors
 * belonging to it can be run when it is unloaded. Nothing is unloaded here - a title links
 * archives and runs until it stops - so the value only has to be unique and stable, and its own
 * address is both. This is what a static link does on every other platform.
 */
void *__dso_handle = &__dso_handle;
