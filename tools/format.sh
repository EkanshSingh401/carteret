#!/usr/bin/env sh
# format.sh -- run the pinned clang-format over the tree.
#
# The formatting gate is version-dependent: Ubuntu ships clang-format 18,
# which disagrees with 20-and-later about `struct stat st{}`, and CI failed on
# that alone while every build and test job passed. The version in
# .clang-format-version is the one the tree is written against, and it is
# installed from PyPI so the same binary runs on macOS, on Ubuntu and in CI.
#
#   usage: tools/format.sh [--check]
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WANT=$(cat "$ROOT/.clang-format-version")

have=""
if command -v clang-format > /dev/null 2>&1; then
  have=$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
fi
if [ "$have" != "$WANT" ]; then
  echo "clang-format ${WANT} is required; found ${have:-none}." >&2
  echo "  pip install 'clang-format==${WANT}'" >&2
  exit 1
fi

FILES=$(find "$ROOT/include" "$ROOT/src" "$ROOT/tests" "$ROOT/tools" "$ROOT/bench" \
  -type f \( -name '*.hpp' -o -name '*.cpp' \))

if [ "${1:-}" = "--check" ]; then
  echo "$FILES" | xargs clang-format --dry-run --Werror
  echo "formatting clean under clang-format ${WANT}"
else
  echo "$FILES" | xargs clang-format -i
  echo "formatted with clang-format ${WANT}"
fi
