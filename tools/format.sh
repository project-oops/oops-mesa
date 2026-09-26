#!/usr/bin/env bash
# Runs clang-format over first-party C and C++, at the major version the compiler image pins.
#
#   tools/format.sh           format in place
#   tools/format.sh --check   fail on any difference
#
# The image is the base of toolchain/Dockerfile (D013). A clang-format of the same major on
# PATH runs directly; otherwise the image runs it. Generated data and mesa/ are excluded.
# Files are rewritten by redirection, not `clang-format -i`, because -i renames and a Windows
# bind mount refuses the rename.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

image=$(sed -n 's/^FROM //p' toolchain/Dockerfile | head -1)
major=${image##*:}
mapfile -t files < <(git ls-files '*.c' '*.h' '*.cpp' |
    grep -v -e '^mesa/' -e '^tools/preamble-dump/preamble_gfx1013\.h$')

if clang-format --version 2>/dev/null | grep -q "version $major\."; then
    run=()
elif command -v docker >/dev/null 2>&1; then
    # Git Bash rewrites Unix-looking arguments; `pwd -W` gives the path the Windows daemon mounts.
    host_pwd=$(pwd -W 2>/dev/null || pwd)
    run=(env MSYS_NO_PATHCONV=1 docker run --rm -v "$host_pwd:/w" -w /w "$image")
else
    echo "oops-mesa: needs clang-format $major on PATH, or docker to run $image" >&2
    exit 1
fi

if [ "${1:-}" = "--check" ]; then
    "${run[@]}" clang-format --dry-run --Werror "${files[@]}"
    echo "oops-mesa: formatting clean (${#files[@]} files)"
else
    # shellcheck disable=SC2016
    "${run[@]}" sh -c \
        'for f; do clang-format "$f" > /tmp/fmt.out && cat /tmp/fmt.out > "$f"; done' \
        sh "${files[@]}"
    echo "oops-mesa: formatted ${#files[@]} files"
fi
