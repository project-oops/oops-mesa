# D013 - The collection compiles with clang 21, and the pin is enforced

**Status:** decided
**Date:** 2026-09-21

Every repository that compiles C or C++ uses clang 21, and `silkeh/clang:21` is the
authority. WSL `oops-builder` is a permitted runner while it reports the same major.
obscene and oops-apps carry a `toolchain.mk` that refuses another major; oops-mesa's pin is
the `FROM` line of `toolchain/Dockerfile`. `OOPS/tools/check-toolchain.sh` fails when the
repositories disagree or one declares no pin. Cite as `oops-mesa#D013`.

**Why:** an unenforced pin let the same source compile with clang 21 under WSL and clang
18 under Docker, with no build noticing. clang 21 compiles the libc++ the pinned checkout
carries (`_LIBCPP_VERSION 210108`); clang 18 fails on its type traits. An image tag
resolves to a digest, while WSL's clang moves with `apt upgrade`. `silkeh/clang:21` is the
same LLVM revision oops-apps pins libc++ to.

**Rejected:**
- Staying on clang 18: libc++ does not build with it.
- WSL as the authority: its compiler follows the distribution's package index.
- A `toolchain.mk` in oops-mesa beside the Dockerfile: a second place to be wrong.

orbistoun's reference LLVM 18 for shader fixtures is separate (orbistoun#D681). Mesa keeps
`-fno-exceptions -fno-rtti`.
