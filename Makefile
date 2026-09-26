# oops-mesa - the mechanics behind `./bin/oops-mesa`. `check` is everything CI runs; the
# Mesa build itself is `./bin/oops-mesa build`, in the container.

include dependencies.mk

.PHONY: check check-pin check-patches check-docs check-format check-preamble check-tiling preamble tiling test clean

check: check-pin check-patches check-docs check-format check-preamble check-tiling
	@echo "oops-mesa: check passed (pin $(MESA_TAG), $(words $(wildcard patches/*.patch)) patches)"

# Mesa is at exactly the pinned commit: the patches and hardware tests ran against that one.
check-pin:
	@test -f mesa/meson.build || { echo "oops-mesa: mesa/ is empty; run: git submodule update --init --depth 1" >&2; exit 1; }
	@actual=$$(git -C mesa rev-parse HEAD); \
	 if [ "$$actual" != "$(MESA_PIN)" ]; then \
	   echo "oops-mesa: mesa/ is at $$actual, dependencies.mk pins $(MESA_PIN) ($(MESA_TAG))" >&2; exit 1; \
	 fi

# Every patch starts with a header saying what it changes and why a shim cannot carry it.
check-patches:
	@for p in $(wildcard patches/*.patch); do \
	   head -1 "$$p" | grep -q '^#' || { echo "oops-mesa: $$p has no header comment" >&2; exit 1; }; \
	 done

# The documents STYLE requires, and the generated decision index matching its files.
check-docs:
	@for f in README.md CLAUDE.md ACKNOWLEDGEMENTS.md docs/DECISIONS.md docs/ROADMAP.md docs/WORKLOG.md docs/decisions/_preamble.md; do \
	   test -f "$$f" || { echo "oops-mesa: missing $$f" >&2; exit 1; }; \
	 done
	@for d in docs/decisions/D*.md; do \
	   n=$$(basename "$$d" | cut -d- -f1); \
	   grep -q "| $$n |" docs/DECISIONS.md || { echo "oops-mesa: $$d is not in docs/DECISIONS.md; run tools/split-decisions.sh --index oops-mesa from the collection root" >&2; exit 1; }; \
	 done

# First-party C and C++ matches .clang-format, checked in the pinned compiler image.
check-format:
	@bash tools/format.sh --check

# The radeonsi preamble for this hardware, regenerated from the pinned Mesa and compared with
# the tracked copy. Skipped without clang and python3.
check-preamble:
	@if command -v clang >/dev/null && command -v python3 >/dev/null; then \
	   bash tools/preamble-dump/build.sh --check; \
	 else \
	   echo "oops-mesa: check-preamble skipped (needs clang and python3 on PATH)"; \
	 fi

preamble:
	bash tools/preamble-dump/build.sh

# Whether a radeonsi 64KB_R_X surface matches oops-sdk's scanout tiler byte for byte, with a
# control, compared with the tracked verdict. Skipped without clang++ and a sibling oops-sdk.
check-tiling:
	@if command -v clang++ >/dev/null && test -d $(OOPS_SDK)/src/agc; then \
	   OOPS_SDK=$(OOPS_SDK) bash tools/tiling-compare/build.sh --check; \
	 else \
	   echo "oops-mesa: check-tiling skipped (needs clang++ and an oops-sdk at $(OOPS_SDK))"; \
	 fi

tiling:
	OOPS_SDK=$(OOPS_SDK) bash tools/tiling-compare/build.sh

clean:
	rm -rf build dist

# The shims' host suite, built with OOPS_HOST_BUILD: the device description's values and that
# an unimplemented command refuses. oops-sdk is a sibling checkout.
OOPS_SDK ?= ../oops-sdk

test:
	@test -d $(OOPS_SDK)/include || { echo "oops-mesa: no oops-sdk at $(OOPS_SDK); set OOPS_SDK" >&2; exit 1; }
	@mkdir -p build/host
	@cc -std=c11 -Wall -Wextra -Werror -DOOPS_HOST_BUILD \
	    -Imesa/include -Imesa/src -Isrc/winsys -I$(OOPS_SDK)/include \
	    tests/winsys_test.c tests/platform_double.c src/winsys/drm_device.c src/winsys/device_info.c src/winsys/buffers.c src/winsys/submit.c src/winsys/context.c src/winsys/log.c src/winsys/syncobj.c \
	    $(OOPS_SDK)/src/memory/memory.c \
	    -o build/host/winsys_test
	@./build/host/winsys_test
	@# The runtime shim links alone: stderr_to_klog.c defines fprintf and kin, which would
	@# route the suite's own reporting into the log sink.
	@cc -std=c11 -Wall -Wextra -Werror -DOOPS_HOST_BUILD \
	    -Isrc/runtime \
	    tests/runtime_test.c \
	    -o build/host/runtime_test
	@./build/host/runtime_test
