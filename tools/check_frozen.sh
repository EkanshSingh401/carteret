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
# A CONSUMED LOCK IS CHECKED DIFFERENTLY. Once a run has happened the lock
# makes a claim about the PAST -- that the code which ran was the code that was
# frozen -- and the tree moves on afterwards. Checking a spent lock against
# HEAD would fail for ever, and the failure would say nothing about whether the
# run was sound. So when the lock carries `executed`, the comparison runs from
# the frozen base to the commit the RUNNER recorded, and the binary digests
# recorded before the run must match the ones committed with it. Both are facts
# about the past that no later commit can disturb.
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

EXECUTED=$(grep -oE '^executed[[:space:]]+[0-9a-f]{40}' "$LOCK" | awk '{print $2}' || true)
RESULTS=$(grep -oE '^results[[:space:]]+[0-9a-f]{40}' "$LOCK" | awk '{print $2}' || true)
PRERUN=docs/generated/heldout/prerun_checks.txt

fail=0

if [ -n "$EXECUTED" ]; then
  # --- consumed: verify the historical claim -------------------------------
  echo "lock is CONSUMED: a run executed at $EXECUTED"
  echo "a) frozen surface, $BASE -> $EXECUTED, excluding the lock"
  a=$(git diff "$BASE" "$EXECUTED" -- research/ src/ include/ tools/ \
        ':!research/heldout.lock')
  if [ -n "$a" ]; then
    echo "   CHANGED -- the code that RAN is not the code that was frozen:"
    git diff --stat "$BASE" "$EXECUTED" -- research/ src/ include/ tools/ \
      ':!research/heldout.lock' | sed 's/^/     /'
    fail=1
  else
    echo "   empty"
  fi

  echo "b) binary digests recorded in the lock against the run's own record"
  if [ ! -f "$PRERUN" ]; then
    echo "   $PRERUN is missing; the run recorded no digests" >&2
    fail=1
  else
    # No pipeline here: a `while read` on the right of a pipe runs in a
    # subshell, so a counter set inside it is lost when the loop ends and the
    # check would pass while reporting failures.
    missing=0
    for h in $(grep -E '^binary[[:space:]]+[0-9a-f]{64}' "$LOCK" | awk '{print $2}'); do
      n=$(grep -E "^binary[[:space:]]+$h" "$LOCK" | awk '{print $3}')
      if grep -q "$h" "$PRERUN"; then
        echo "   $n  present"
      else
        echo "   $n  MISSING from $PRERUN" >&2
        missing=1
      fi
    done
    [ "$missing" -ne 0 ] && fail=1
  fi

  if [ "$fail" -ne 0 ]; then
    echo "FROZEN CHECK FAILED" >&2
    exit 1
  fi
  echo "lock consumed by run at ${RESULTS:-unknown}; frozen at run time: verified"
  echo "A new held-out run requires a new lock."
  exit 0
fi

# --- not consumed: the lock still authorises a future run --------------------
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
