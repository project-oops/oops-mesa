/*
 * The symbols a hosted binary gets from its start-up objects (crtbegin and its kin),
 * which this target does not link. A title is linked by oops-apps with its own entry
 * point, so what Mesa references from those objects is defined here.
 *
 * `__cpu_model` is not here: it belongs to the compiler and comes from its runtime
 * library, because its layout is the compiler's to define.
 */

/*
 * The handle passed to the exit-time destructor registrar. Nothing is unloaded, so the
 * value only has to be unique and stable, and its own address is both - as in any
 * static link.
 */
void *__dso_handle = &__dso_handle;

/*
 * The thread-local initialiser for `_mesa_glapi_tls_Context`, which has nothing to
 * initialise. clang guards C++ uses of that thread-local with a weak reference to this
 * function and calls it when the address is non-null. A module cannot carry a weak
 * undefined import (selfish#D101), so the address is made non-null and the call empty.
 * Defined under its mangled name so no C++ translation unit is needed for it.
 */
void _ZTH23_mesa_glapi_tls_Context(void);
void _ZTH23_mesa_glapi_tls_Context(void) {}

/*
 * Runs the `.preinit_array` and `.init_array` constructors that crt start-up files run
 * on an ordinary system. Mesa has dynamically initialised globals - ACO's
 * `aco::instr_info` opcode table, the GLSL builtin tables - that stay zero without
 * them. A title calls this once before it touches Mesa, rather than relying on the
 * platform loader's handling of `DT_INIT_ARRAY`.
 *
 * The bounds come from the title's link script (oops-apps `local_tls.ld`). They are
 * weak so a link without the section walks an empty range instead of failing.
 */
extern void (*__preinit_array_start[])(void) __attribute__((weak));
extern void (*__preinit_array_end[])(void) __attribute__((weak));
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));

void oops_mesa_run_init_array(void);
void oops_mesa_run_init_array(void) {
    static int done = 0;
    if (done)
        return;
    done = 1;

    for (void (**fn)(void) = __preinit_array_start; fn != __preinit_array_end; ++fn)
        if (*fn)
            (*fn)();
    for (void (**fn)(void) = __init_array_start; fn != __init_array_end; ++fn)
        if (*fn)
            (*fn)();
}
