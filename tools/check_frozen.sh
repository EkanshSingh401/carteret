#!/usr/bin/env sh
# check_frozen.sh -- is the analysis code the code that was locked?
#
# TWO checks, both of which must pass. The first version was one check and it
# was wrong: research/heldout.lock lives under research/, so
#
#   git diff <frozen-base> HEAD -- research/
#
# can never be empty once the lock exists, because the lock commit necessarily
# follows the amendment it points at. The check could only pass in the instant
# before the lock was written, and its failure was read as a code change when
# it was nothing of the kind.
#
#   a) the frozen surface, EXCLUDING the lock, is unchanged since the base
#   b) the lock itself is unchanged since the commit that wrote it
#
# Excluding the lock from its own check is not a loosening. The lock is the
# statement of what is frozen; (b) is what stops it being edited afterwards.
#
#   usage: tools/check_frozen.sh [frozen-base] [lock-commit]
#          defaults are read from research/heldout.lock
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

LOCK=research/heldout.lock
BASE="${1:-$(grep -oE '^frozen[[:space:]]+[0-9a-f]{40}' "$LOCK" | awk '{print $2}')}"
LOCK_COMMIT="${2:-$(git log -1 --format=%H -- "$LOCK")}"

[ -n "$BASE" ] || { echo "no frozen base in $LOCK and none given" >&2; exit 2; }

fail=0

echo "a) frozen surface since $BASE, excluding the lock"
a=$(git diff "$BASE" HEAD -- research/ src/ include/ tools/ ':!research/heldout.lock')
if [ -n "$a" ]; then
  echo "   CHANGED:"
  git diff --stat "$BASE" HEAD -- research/ src/ include/ tools/ ':!research/heldout.lock' \
    | sed 's/^/     /'
  fail=1
else
  echo "   empty"
fi

echo "b) the lock itself since $LOCK_COMMIT"
b=$(git diff "$LOCK_COMMIT" HEAD -- "$LOCK")
if [ -n "$b" ]; then
  echo "   CHANGED:"
  git diff --stat "$LOCK_COMMIT" HEAD -- "$LOCK" | sed 's/^/     /'
  fail=1
else
  echo "   empty"
fi

# c) The working tree must match HEAD for those paths. Without this, (a) and
# (b) compare two committed states while the code that would actually RUN sits
# uncommitted beside them, and the check passes on a tree that is not the tree
# it certified. That is the same class of gap as a dry run that never reaches
# the command it would execute.
echo "c) working tree vs HEAD across the frozen surface"
c=$(git status --porcelain -- research/ src/ include/ tools/)
if [ -n "$c" ]; then
  echo "   UNCOMMITTED:"
  printf '%s\n' "$c" | sed 's/^/     /'
  fail=1
else
  echo "   clean"
fi

if [ "$fail" -ne 0 ]; then
  echo "FROZEN CHECK FAILED" >&2
  exit 1
fi
echo "FROZEN CHECK PASSED"
