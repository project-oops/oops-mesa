# The oops-mesa build image. Container first: Mesa's build needs meson, ninja, a clang with
# lld, and the Python packages Mesa's generators import. None of that is installed on the
# machine; it lives here and is thrown away.
#
#   docker build -t oops-mesa-toolchain:dev toolchain/
#   docker run --rm -v "$PWD:/w" -w /w oops-mesa-toolchain:dev ./toolchain/build-mesa.sh
#
# clang 21 matches the compiler oops-sdk, oops-apps and obSCEne build with, so the shims and
# the titles that link them see one compiler and one set of target flags.
#
# **This tag is the collection's authoritative toolchain pin** (D013). It was clang 18
# until 2026-09-21, and the bump is what makes a C++ standard library reachable: the libc++
# already staged here is `_LIBCPP_VERSION 210108`, and D006 measured 43 of its 71 sources
# failing against clang 18 on constructs in its own type traits. A compiler at least as new as
# the library it ships with removes that class of failure rather than working around it.
#
# The major version here and the one every other repository's `toolchain.mk` guards against are
# the same number by construction: `OOPS/tools/check-toolchain.sh` fails when they diverge.
FROM silkeh/clang:21

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ninja-build pkg-config \
      python3 python3-pip python3-mako python3-yaml python3-packaging \
      bison flex \
      git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# meson comes from pip, not apt: this base is Debian bookworm, whose meson is 1.0.1, and the
# pinned Mesa's own `meson_version` is `>= 1.4.0`. The version is pinned here for the same
# reason every other input is, and it moves with a deliberate change rather than with the
# distribution's package index.
RUN pip3 install --no-cache-dir --break-system-packages meson==1.9.1

# Mesa's build scripts run under python3; the target-side compile is a cross build driven by
# the meson cross file this directory grows in roadmap unit 3. No LLVM package is installed:
# the AMD driver's shader backend here is ACO, which needs none.
WORKDIR /w
