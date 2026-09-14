# Pins. Everything this repository builds against, by exact revision, so a tree and its
# hardware evidence name the same inputs. Bump a pin and the patches and the hardware tests
# run again as one unit (CLAUDE.md, principle 1).

# Upstream Mesa, as a submodule at mesa/. Tag and commit both, because the tag is what a
# person reads and the commit is what the check compares.
MESA_TAG := mesa-26.2.2
MESA_PIN := 3281a69a8bfd9f997e91c15ed0e6290cae12dd32

# The C-library headers the runtime shim compiles against (D002, pinned by D004). The same
# FreeBSD source checkout orbistoun harvests its constants from, so the two projects cannot
# disagree about this platform. Located by OOPS_MESA_FREEBSD_SRC, defaulting to a sibling of the
# collection; the headers are copied into toolchain/sysroot/ (ignored) and verified against the
# revision below. Nothing is downloaded.
#
# The pin is what Mesa is compiled against, not a claim that the platform's library matches it:
# the target's generation is older and the platform does not disclose which (kern.osrelease reads
# "0.0-prototype"). Where a structure differs, the conversion lives in a named shim function, and
# SYSROOT_STAT_LAYOUT chooses the layout for the two that changed shape (D004).
SYSROOT_SOURCE := freebsd-src
SYSROOT_PIN := ee81cd1d8f5596a6ab4c8eb29009405572cc162b
SYSROOT_STAT_LAYOUT ?= freebsd11

# The container image the Mesa build runs in, built from toolchain/Dockerfile. The tag is
# the digest-bearing name the build script prints; it is recorded here once the image exists.
TOOLCHAIN_IMAGE := oops-mesa-toolchain:dev
