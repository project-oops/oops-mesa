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

/*
 * The thread-local initialiser for `_mesa_glapi_tls_Context`, which has nothing to initialise.
 *
 * # What references it
 *
 * `main_shader_query.cpp` and `main_uniform_query.cpp` reach that thread-local from C++, and
 * clang emits the Itanium ABI's guarded form for a `thread_local` that might need dynamic
 * initialisation:
 *
 *     if (&_ZTH23_mesa_glapi_tls_Context)
 *         _ZTH23_mesa_glapi_tls_Context();
 *
 * The reference is **weak**, because on an ordinary system the address comes out null when the
 * variable needs no such function - and `_mesa_glapi_tls_Context` needs none: it is a C variable
 * declared `extern __thread`, defined in `core.c`, with no dynamic initialisation at all.
 *
 * # Why the null cannot be delivered here, so a definition is
 *
 * That pattern does not survive packaging on this platform, and SELFish says so on purpose. A
 * module it builds cannot carry a weak undefined import: the binding is rewritten to `GLOBAL`
 * because a loader is entitled to read `STB_WEAK, undefined` as "do not bother resolving this",
 * and one does - measured, a module whose 203 imports were all weak had them bound only from the
 * two libraries already resident in the process (selfish `dynlib.rs`, obscene#D248). An import
 * that nothing claims is a build error rather than a silently null slot (D101).
 *
 * So "absent, therefore null, therefore skipped" is not available. The remaining honest option is
 * to make the address non-null and the call harmless, which is what this is: initialisation that
 * has nothing to do, done. The guard then calls this, it returns, and the thread-local is used
 * exactly as it would have been.
 *
 * Named in its mangled form deliberately. It is a C++ ABI symbol and a C definition of it is the
 * smaller thing than a C++ translation unit added to every title for one empty function.
 */
void _ZTH23_mesa_glapi_tls_Context(void);
void _ZTH23_mesa_glapi_tls_Context(void)
{
}
