# The oops-mesa build image: meson, ninja, clang with lld, and the Python packages Mesa's
# generators import, in a container rather than on the machine.
#
#   docker build -t oops-mesa-toolchain:dev toolchain/
#   docker run --rm -v "$PWD:/w" -w /w oops-mesa-toolchain:dev ./toolchain/build-mesa.sh
#
# This tag is the collection's authoritative toolchain pin (D013): oops-sdk, oops-apps and
# obSCEne build with the same clang, and OOPS/tools/check-toolchain.sh fails when any
# repository's `toolchain.mk` diverges from it. The staged libc++ (`_LIBCPP_VERSION 210108`)
# needs a compiler at least as new as itself (D006).
FROM silkeh/clang:21

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ninja-build pkg-config \
      python3 python3-pip python3-mako python3-yaml python3-packaging \
      bison flex \
      git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# meson comes from pip, pinned: Debian bookworm ships 1.0.1 and the pinned Mesa requires
# `meson_version >= 1.4.0`.
RUN pip3 install --no-cache-dir --break-system-packages meson==1.9.1

# No LLVM package: the AMD shader backend here is ACO, which needs none.
WORKDIR /w
