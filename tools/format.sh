#!/usr/bin/env bash
# Runs clang-format from the pinned compiler image over first-party C and C++.
#
#   tools/format.sh           format in place
#   tools/format.sh --check   fail on any difference
#
# The image is the base of toolchain/Dockerfile (D013). Generated data and mesa/ are excluded.
# Files are rewritten by redirection, not `clang-format -i`, because -i renames and a Windows
# bind mount refuses the rename.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

image=$(sed -n 's/^FROM //p' toolchain/Dockerfile | head -1)
mapfile -t files < <(git ls-files '*.c' '*.h' '*.cpp' |
    grep -v -e '^mesa/' -e '^tools/preamble-dump/preamble_gfx1013\.h$')

command -v docker >/dev/null 2>&1 || {
    echo "oops-mesa: docker is not on PATH; clang-format runs in $image" >&2
    exit 1
}

# Git Bash rewrites Unix-looking arguments; `pwd -W` gives the path the Windows daemon mounts.
host_pwd=$(pwd -W 2>/dev/null || pwd)

if [ "${1:-}" = "--check" ]; then
    MSYS_NO_PATHCONV=1 docker run --rm -v "$host_pwd:/w" -w /w "$image" \
        clang-format --dry-run --Werror "${files[@]}"
    echo "oops-mesa: formatting clean (${#files[@]} files)"
else
    # shellcheck disable=SC2016
    MSYS_NO_PATHCONV=1 docker run --rm -v "$host_pwd:/w" -w /w "$image" sh -c \
        'for f; do clang-format "$f" > /tmp/fmt.out && cat /tmp/fmt.out > "$f"; done' \
        sh "${files[@]}"
    echo "oops-mesa: formatted ${#files[@]} files"
fi
