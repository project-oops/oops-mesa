# oops-mesa build integration.
#
# A title that wants OpenGL includes this after oops-sdk's own fragment:
#
#   OOPS_SDK  ?= $(abspath ../../../oops-sdk)
#   OOPS_MESA ?= $(abspath ../../../oops-mesa)
#   include $(OOPS_SDK)/oops-sdk.mk
#   include $(OOPS_MESA)/oops-mesa.mk
#
# and links `$(OOPS_MESA_LIBS)` with `$(OOPS_MESA_SRCS)` added to its own sources. Such a title
# is hosted: Mesa calls `malloc`, `snprintf` and their kin, and the platform's C library answers
# them at load. `OOPS_MESA_HOSTED` marks it for the title and for packaging (D002).

OOPS_MESA_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OOPS_MESA_BUILD ?= $(OOPS_MESA_DIR)/build/mesa

# A hosted title compiles against exactly the headers Mesa compiled against, the staged FreeBSD
# set (D004). Without a sysroot the compiler finds the build machine's glibc headers instead.
OOPS_MESA_SYSROOT := $(OOPS_MESA_DIR)/toolchain/sysroot

# A title sees GL and EGL, not Mesa's internals. `mesa/src` is here for `util/xmlconfig.h` and
# `util/driconf.h`, the option cache `radeonsi_screen_create` needs; both compile on their own.
# Of the Gallium tree only `mesa/src/gallium/include` is exposed, for `mesa_interface.h`, the
# loader ABI the platform shim sits on (D010); it includes only `<stdbool.h>` and `<stdint.h>`
# and compiles clean at a title's `-Wconversion -Wsign-conversion -Werror`. `pipe/p_screen.h`
# needs generated headers, so a title restates `pipe_screen_config` as mesa-probe does.
OOPS_MESA_INCLUDE := \
    --sysroot=$(OOPS_MESA_SYSROOT) \
    -I$(OOPS_MESA_DIR)/mesa/include \
    -I$(OOPS_MESA_DIR)/mesa/src \
    -I$(OOPS_MESA_DIR)/mesa/src/gallium/include \
    -I$(OOPS_MESA_DIR)/src/winsys \
    -I$(OOPS_MESA_DIR)/src/platform \
    -DOOPS_MESA_HOSTED=1

# The shims compile with the title rather than shipping as an archive, so a title built against
# a different oops-sdk cannot link a stale one.
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
    $(OOPS_MESA_DIR)/src/platform/dri_loader.c \
    $(OOPS_MESA_DIR)/src/platform/gfx.c

# The C++ half of the shim ships as an archive (`-loopsmesa_cxx` below): it binds to no oops-sdk
# header, and a title compiles its own sources as C11 (D006).

# The archives, in the order Mesa's own DRI module links them, taken from that link line: static
# archives resolve left to right, so a wrong order is an undefined symbol. Paths are relative to
# this repository because the container build that writes the file mounts it elsewhere.
OOPS_MESA_LIBS := $(addprefix $(OOPS_MESA_DIR)/,$(shell \
    if [ -f $(OOPS_MESA_DIR)/build/link-order.txt ]; then \
        cat $(OOPS_MESA_DIR)/build/link-order.txt; \
    fi))

# The public GL entry points, taken whole and placed first. Upstream builds this archive only
# for libGL, so it is not on the DRI link line; the container build asks for it explicitly.
# A title's objects follow the archives on the link line (`app.mk`), and nothing inside Mesa
# references `glGetString` for a title, so without --whole-archive no GL thunk is pulled. It
# leads because the thunks reference `_mesa_glapi_tls_Dispatch`, defined in `libglapi.a`
# further down. `app.mk` filters the `-Wl,` flags out of the link's prerequisites.
OOPS_MESA_GLAPI_BRIDGE := $(OOPS_MESA_DIR)/build/mesa/src/mesa/glapi/glapi/libglapi_bridge.a
ifneq ($(wildcard $(OOPS_MESA_GLAPI_BRIDGE)),)
OOPS_MESA_LIBS := -Wl,--whole-archive $(OOPS_MESA_GLAPI_BRIDGE) -Wl,--no-whole-archive \
                  $(OOPS_MESA_LIBS)
else
$(warning oops-mesa: no libglapi_bridge.a; a title will import GL names instead of calling Mesa)
endif

# Target-built support archives. radeonsi needs libelf to start; AddressLib asks
# `-lcpu_model` whether the CPU has AVX2. `-lm` supplies `sin`, `floor`, `log` and their kin,
# which the platform's C library does not export (D011). `-lrune` supplies the C locale's
# character tables, which `ctype.h` reads through `_CurrentRuneLocale` and the platform does
# not bind on the native leg. Both go last: they reference only what the sysroot already has.
OOPS_MESA_SYSLIBS := \
    -L$(OOPS_MESA_DIR)/toolchain/sysroot/usr/lib -loopsmesa_cxx -lelf -lcpu_model -lm -lrune

# A title linking Mesa compiles against the libc++ headers this build uses; the platform's own
# C++ library cannot serve them (D006).
OOPS_MESA_CXXFLAGS := -stdlib=libc++ -std=c++17

ifeq ($(wildcard $(OOPS_MESA_BUILD)),)
$(warning oops-mesa: no build at $(OOPS_MESA_BUILD); run ./bin/oops-mesa build first)
endif
