# oops-mesa build integration.
#
# A title that wants OpenGL includes this after oops-sdk's own fragment:
#
#   OOPS_SDK  ?= $(abspath ../../../oops-sdk)
#   OOPS_MESA ?= $(abspath ../../../oops-mesa)
#   include $(OOPS_SDK)/oops-sdk.mk
#   include $(OOPS_MESA)/oops-mesa.mk
#
# and links `$(OOPS_MESA_LIBS)` with `$(OOPS_MESA_SRCS)` added to its own sources.
#
# # A title that links this is hosted, and that is not a detail
#
# oops-sdk links no C library. A title that links oops-mesa does: Mesa calls `malloc`, `snprintf`
# and their kin by their published names and the platform's own C library answers them at load.
# That is a stated divergence from the collection's freestanding rule (D002), confined to titles
# that include this file, and `OOPS_MESA_HOSTED` is defined so a title can say so about itself
# and so packaging can tell the two kinds apart (CLAUDE.md, principle 5).

OOPS_MESA_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OOPS_MESA_BUILD ?= $(OOPS_MESA_DIR)/build/mesa

# Mesa's public headers, and the shim's own. Nothing here exposes Mesa's internals to a title:
# it sees GL and EGL and nothing else.
# The sysroot is not optional for a hosted title.
#
# A freestanding title needs no headers beyond oops-sdk's, which is why the rest of oops-apps
# builds without one. Dropping `-ffreestanding` to make Mesa work sends the compiler looking for
# a standard library, and without a sysroot it finds the build machine's Linux one and fails on
# the first glibc-internal header. So a title that links Mesa compiles against exactly the
# headers Mesa compiled against: the staged FreeBSD set, pinned by D004.
OOPS_MESA_SYSROOT := $(OOPS_MESA_DIR)/toolchain/sysroot

# `mesa/include` carries the public headers. `mesa/src` is here too, for one thing: a title that
# calls `radeonsi_screen_create` has to build the option cache that call reaches, and that type is
# `util/xmlconfig.h`, declared through `util/driconf.h`. Those two compile on their own.
#
# Gallium's own include tree is deliberately *not* here. `pipe/p_screen.h` reaches the util tree
# and generated headers that only exist after a build, and none of it survives the warning flags a
# title compiles at. A title that needs `pipe_screen_config` restates those three fields with the
# pin named beside them, which is what mesa-probe does.
# `mesa/src/gallium/include` is here for one header: `mesa_interface.h`, Mesa's loader ABI. It is
# the seam D010 puts the platform shim on, and unlike the rest of the gallium tree it is safe to
# expose - it includes `<stdbool.h>` and `<stdint.h>` and nothing else, deliberately, because
# loaders outside Mesa include it. It compiles at this title's full `-Wconversion
# -Wsign-conversion -Werror` setting with no suppression, which `util/driconf.h` next door does
# not (worklog 035).
OOPS_MESA_INCLUDE := \
    --sysroot=$(OOPS_MESA_SYSROOT) \
    -I$(OOPS_MESA_DIR)/mesa/include \
    -I$(OOPS_MESA_DIR)/mesa/src \
    -I$(OOPS_MESA_DIR)/mesa/src/gallium/include \
    -I$(OOPS_MESA_DIR)/src/winsys \
    -I$(OOPS_MESA_DIR)/src/platform \
    -DOOPS_MESA_HOSTED=1

# The shims this repository owns. They compile with the title rather than shipping as an archive,
# so that a title built against a different oops-sdk cannot silently link a stale one.
OOPS_MESA_SRCS := \
    $(OOPS_MESA_DIR)/src/winsys/drm_device.c \
    $(OOPS_MESA_DIR)/src/winsys/device_info.c \
    $(OOPS_MESA_DIR)/src/winsys/buffers.c \
    $(OOPS_MESA_DIR)/src/winsys/submit.c \
    $(OOPS_MESA_DIR)/src/winsys/context.c \
    $(OOPS_MESA_DIR)/src/winsys/log.c \
    $(OOPS_MESA_DIR)/src/winsys/syncobj.c \
    $(OOPS_MESA_DIR)/src/runtime/threads.c \
    $(OOPS_MESA_DIR)/src/runtime/abi.c \
    $(OOPS_MESA_DIR)/src/runtime/libc_absent.c \
    $(OOPS_MESA_DIR)/src/runtime/stderr_to_klog.c \
    $(OOPS_MESA_DIR)/src/platform/dri_loader.c

# `src/platform/dri_loader.c` joined the build on 2026-09-17, after two separate reasons for
# keeping it out were each dealt with.
#
# It was out first because it did not compile: `dri_create_image`'s format argument had no source
# a title could reach (worklog 037). That went away when the format turned out not to need
# naming - it is carried opaquely out of the chosen `dri_config` (worklog 039).
#
# It was out second because linking it pulls in the whole Gallium DRI frontend, and the fixup then
# could not place 55 imported symbols. Those are now accounted for: 24 driver descriptors come
# from the target object the build already produced and now puts in `link-order.txt`, four System V
# shared-memory calls are stubs in `libc_absent.c`, the corpus placed the rest once that object was
# in the link, and the last one was a weak undefined symbol that `obscene-tool` should never have
# asked about (obSCEne REQ-20260917T1755Z-4a91, fixed there).

# The C++ half of the shim ships as an archive rather than as a source, for two reasons: it
# touches no oops-sdk header so it has nothing to bind to, and a title compiles its own sources
# as C11, which would refuse it. The container build produces it (D006).

# The archives, in the order Mesa's own DRI module links them. The order matters: these are
# static archives and the linker resolves left to right, so a wrong order is an undefined symbol
# rather than a warning. It is taken from that link line rather than arranged by hand.
# The file holds paths relative to this repository, because the build that writes it runs in a
# container where the repository is mounted somewhere else entirely.
OOPS_MESA_LIBS := $(addprefix $(OOPS_MESA_DIR)/,$(shell \
    if [ -f $(OOPS_MESA_DIR)/build/link-order.txt ]; then \
        cat $(OOPS_MESA_DIR)/build/link-order.txt; \
    fi))

# The public GL entry points, taken whole, and both halves of that are deliberate.
#
# # Why it is a separate line rather than an entry in link-order.txt
#
# `link-order.txt` is derived from Mesa's own DRI link line, and this archive is not on it -
# upstream builds it only for libGL, which this configuration does not produce. The container
# build asks for it explicitly; see the note there for how a title calling `glGetString` ended up
# importing it from one of the platform's own GL libraries instead.
#
# # Why --whole-archive, which is not the usual answer
#
# A title's own sources are placed *after* the archives on the link line (`app.mk`), and a static
# archive member is only pulled to satisfy a reference the linker has already seen. So a symbol
# referenced only by the title and defined only in an archive is never pulled in - which is why
# `radeonsi_screen_create` resolves today only because `dri_target.c.o` happens to reference it
# first, not because the title does.
#
# For GL that fallback does not exist: nothing inside Mesa references `glGetString` on a title's
# behalf. Taking the archive whole is the honest fix, and it is also the right shape - a title's
# GL entry points are its API surface, and which of them it calls is not something the build
# knows. 1,300 thunks into the dispatch table is what that costs.
#
# The alternative was to reorder `app.mk` so every title's objects precede the archives. That is
# the deeper fix and it is not made here, because it changes the link of every title in oops-apps
# to solve a problem one of them has.
# It leads the list rather than joining the end, because the thunks reference
# `_mesa_glapi_tls_Dispatch` and `libglapi.a` - which defines it - is further down. Taken whole at
# the end, the reference would have nothing after it to resolve against.
#
# `app.mk` filters flags out of the dependency list, so the archive is still a prerequisite of the
# link and the two `-Wl,` entries are not mistaken for files.
OOPS_MESA_GLAPI_BRIDGE := $(OOPS_MESA_DIR)/build/mesa/src/mesa/glapi/glapi/libglapi_bridge.a
ifneq ($(wildcard $(OOPS_MESA_GLAPI_BRIDGE)),)
OOPS_MESA_LIBS := -Wl,--whole-archive $(OOPS_MESA_GLAPI_BRIDGE) -Wl,--no-whole-archive \
                  $(OOPS_MESA_LIBS)
else
$(warning oops-mesa: no libglapi_bridge.a; a title will import GL names instead of calling Mesa)
endif

# libelf, libm and the compiler's processor-feature object, built for the target by the container
# build. radeonsi will not start without the first; AddressLib asks the last whether the CPU
# has AVX2.
#
# `-lm` is not the usual no-op it is on a hosted system. The platform's C library does not export
# the arithmetic Mesa calls - measured absent twice over on firmware 12.40, from the export
# census and from a dynamic lookup both (obscene REQ-20260917T1640Z-5b28) - so this archive is
# where `sin`, `floor`, `log` and their kin actually come from. Without it they are placed as
# imports from a mined corpus, the title loads, and the console kills it on the first call.
#
# It goes last because it answers and asks for nothing: Mesa's archives reference it, and it
# references only what the staged sysroot already has.
# `-lrune` is the C locale's character tables. `ctype.h` inlines the table lookup rather than
# calling into the C library, so `tolower` in Mesa reads `_CurrentRuneLocale` directly - and that
# symbol is not bindable on the native leg, because the census listing it was captured under the
# PS4 backward-compatibility container (obscene REQ-20260917T1818Z-9f41). Without this a `ctype`
# call on the startup path reads through an unresolved pointer.
OOPS_MESA_SYSLIBS := \
    -L$(OOPS_MESA_DIR)/toolchain/sysroot/usr/lib -loopsmesa_cxx -lelf -lcpu_model -lm -lrune

# Mesa is C++ where it matters, so a title linking it needs the C++ headers this build compiles
# against. The platform's own C++ library cannot serve them (D006).
OOPS_MESA_CXXFLAGS := -stdlib=libc++ -std=c++17

ifeq ($(wildcard $(OOPS_MESA_BUILD)),)
$(warning oops-mesa: no build at $(OOPS_MESA_BUILD); run ./bin/oops-mesa build first)
endif
