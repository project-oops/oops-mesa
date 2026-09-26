# Pins. Everything this repository builds against, by exact revision, so a tree and its
# hardware evidence name the same inputs. A pin bump reruns the patches and the hardware
# tests as one unit (CLAUDE.md, principle 1).

# Upstream Mesa, as a submodule at mesa/. The tag is what a person reads; the commit is what
# the check compares.
MESA_TAG := mesa-26.2.2
MESA_PIN := 3281a69a8bfd9f997e91c15ed0e6290cae12dd32

# The C-library headers the runtime shim compiles against (D004): the same FreeBSD checkout
# orbistoun harvests its constants from, so the two projects agree about this platform.
# Located by OOPS_MESA_FREEBSD_SRC, defaulting to a sibling of the collection; the headers are
# copied into toolchain/sysroot/ (ignored) and verified against this revision. Nothing is
# downloaded.
#
# The pin is what Mesa compiles against, not a claim about the platform's library, whose
# generation is older and undisclosed. Where a structure differs, a named shim function
# converts it, and SYSROOT_STAT_LAYOUT picks the layout for the two that changed shape.
SYSROOT_SOURCE := freebsd-src
SYSROOT_PIN := ee81cd1d8f5596a6ab4c8eb29009405572cc162b
SYSROOT_STAT_LAYOUT ?= freebsd11

# The container image the Mesa build runs in, built from toolchain/Dockerfile.
TOOLCHAIN_IMAGE := oops-mesa-toolchain:dev
