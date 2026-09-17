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
OOPS_MESA_INCLUDE := \
    --sysroot=$(OOPS_MESA_SYSROOT) \
    -I$(OOPS_MESA_DIR)/mesa/include \
    -I$(OOPS_MESA_DIR)/mesa/src \
    -I$(OOPS_MESA_DIR)/src/winsys \
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
    $(OOPS_MESA_DIR)/src/runtime/libc_absent.c

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

# libelf and the compiler's processor-feature object, built for the target by the container
# build. radeonsi will not start without the first; AddressLib asks the second whether the CPU
# has AVX2.
OOPS_MESA_SYSLIBS := \
    -L$(OOPS_MESA_DIR)/toolchain/sysroot/usr/lib -loopsmesa_cxx -lelf -lcpu_model

# Mesa is C++ where it matters, so a title linking it needs the C++ headers this build compiles
# against. The platform's own C++ library cannot serve them (D006).
OOPS_MESA_CXXFLAGS := -stdlib=libc++ -std=c++17

ifeq ($(wildcard $(OOPS_MESA_BUILD)),)
$(warning oops-mesa: no build at $(OOPS_MESA_BUILD); run ./bin/oops-mesa build first)
endif
