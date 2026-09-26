/*
 * The two symbols a hosted binary gets from its start-up objects, which this target has
 * none of.
 *
 * On an ordinary system these come from the files the compiler links before and after
 * everything else - crtbegin and its kin. A title here is linked by oops-apps with its
 * own entry point and no such objects, so anything Mesa references from them has to
 * come from somewhere, and this is the somewhere.
 *
 * `__cpu_model` is deliberately not here. It is the compiler's own, taken from its
 * runtime library by the build, because guessing at a structure the compiler owns is
 * the kind of thing that works until it silently does not.
 */

/*
 * The handle a shared object passes to the exit-time destructor registrar, so that
 * destructors belonging to it can be run when it is unloaded. Nothing is unloaded here
 * - a title links archives and runs until it stops - so the value only has to be unique
 * and stable, and its own address is both. This is what a static link does on every
 * other platform.
 */
void *__dso_handle = &__dso_handle;

/*
 * The thread-local initialiser for `_mesa_glapi_tls_Context`, which has nothing to
 * initialise.
 *
 * # What references it
 *
 * `main_shader_query.cpp` and `main_uniform_query.cpp` reach that thread-local from
 * C++, and clang emits the Itanium ABI's guarded form for a `thread_local` that might
 * need dynamic initialisation:
 *
 *     if (&_ZTH23_mesa_glapi_tls_Context)
 *         _ZTH23_mesa_glapi_tls_Context();
 *
 * The reference is **weak**, because on an ordinary system the address comes out null
 * when the variable needs no such function - and `_mesa_glapi_tls_Context` needs none:
 * it is a C variable declared `extern __thread`, defined in `core.c`, with no dynamic
 * initialisation at all.
 *
 * # Why the null cannot be delivered here, so a definition is
 *
 * That pattern does not survive packaging on this platform, and SELFish says so on
 * purpose. A module it builds cannot carry a weak undefined import: the binding is
 * rewritten to `GLOBAL` because a loader is entitled to read `STB_WEAK, undefined` as
 * "do not bother resolving this", and one does - measured, a module whose 203 imports
 * were all weak had them bound only from the two libraries already resident in the
 * process (selfish `dynlib.rs`, obscene#D248). An import that nothing claims is a build
 * error rather than a silently null slot (D101).
 *
 * So "absent, therefore null, therefore skipped" is not available. The remaining honest
 * option is to make the address non-null and the call harmless, which is what this is:
 * initialisation that has nothing to do, done. The guard then calls this, it returns,
 * and the thread-local is used exactly as it would have been.
 *
 * Named in its mangled form deliberately. It is a C++ ABI symbol and a C definition of
 * it is the smaller thing than a C++ translation unit added to every title for one
 * empty function.
 */
void _ZTH23_mesa_glapi_tls_Context(void);
void _ZTH23_mesa_glapi_tls_Context(void) {}

/*
 * The C++ constructors a start-up object would normally run, run here instead.
 *
 * # What breaks without this
 *
 * Some of Mesa's namespace-scope globals are **dynamically** initialised: the compiler
 * cannot fold their initialiser to a constant, so it leaves the object in `.bss`
 * (zeroed) and emits a constructor - registered in `.init_array` - that fills it at
 * start-up. ACO's opcode table `aco::instr_info` is one:
 * `_GLOBAL__sub_I_aco_opcodes.cpp` copies the real per-generation opcode numbers into
 * it. GLSL's builtin-function and builtin-type tables are two more, and the compiler's
 * own `__cpu_indicator_init` (behind `__builtin_cpu_supports`) is a fourth.
 *
 * On an ordinary system the crt start-up files walk `.init_array` before `main`. A
 * title here is linked by oops-apps with its own entry point and no such files, so
 * nothing was calling them and every one of those globals stayed zero. The symptom that
 * led here: ACO assembled every instruction with opcode 0 - reading a zeroed
 * `instr_info.opcode_gfx10[...]` - so a GFX10 shader came out with GFX11 encodings and
 * the GPU faulted ILLEGAL_INST decoding it (oops-mesa worklog 062). The gfx_level was
 * correct end to end; the table it indexed was empty.
 *
 * # Why it is here and not in the loader
 *
 * The module's own dynamic relocations are applied (function pointers and the GOT work,
 * or nothing would run), so the `.init_array` entries are correctly relocated - they
 * were simply never called. Rather than depend on whatever the platform's `ld-elf.so.1`
 * does with `DT_INIT_ARRAY` for a module in this shape, a title runs this itself, once,
 * before it touches Mesa. That is the same crt-start-up job `__dso_handle` above stands
 * in for, kept in the same file for the same reason.
 *
 * The bounds are the linker's encapsulation symbols, placed by the title's link script
 * (oops-apps `local_tls.ld`). They are declared weak so that a link which somehow omits
 * the section yields a null pair and an empty walk rather than an undefined-symbol
 * error.
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
