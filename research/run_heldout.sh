#!/usr/bin/env bash
# Runs the pre-registered study against the held-out sessions. Exactly once.
#
# This script exists to make the pre-registration enforceable rather than
# aspirational. It refuses to run unless:
#
#   1. research/heldout.lock names a commit,
#   2. that commit is an ancestor of HEAD,
#   3. docs/preregistration.md is byte-identical to its content at that commit,
#   4. docs/preregistration.md has no uncommitted changes,
#   5. no held-out result file already exists.
#
# Checks 2 and 3 together are the claim: the registration was committed, it is
# in this branch's history, and it has not been edited since. A hash written by
# the same process that runs the study would establish nothing, which is why
# the author writes it by hand in a commit of his own.
#
# Check 5 is the one-run rule. A second run needs the existing results moved
# aside deliberately, and the writeup must say a second run happened and why.
#
#   usage: research/run_heldout.sh [--dry-run]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

LOCK="research/heldout.lock"
PREREG="docs/preregistration.md"
OUT="results/heldout"
DRY_RUN=0
[ "${1:-}" = "--dry-run" ] && DRY_RUN=1

fail() {
  echo "REFUSED: $1" >&2
  exit 1
}

# --- 1. the lock names a commit ---------------------------------------------

[ -f "$LOCK" ] || fail "$LOCK does not exist"
COMMIT=$(grep -oE '^commit[[:space:]]+[0-9a-f]{40}' "$LOCK" 2> /dev/null | awk '{print $2}' || true)
if [ -z "$COMMIT" ]; then
  fail "$LOCK names no commit.
  The author completes $PREREG, commits it, and writes
      commit <that commit's 40-hex sha>
  into $LOCK in a separate commit. Until then the held-out sessions must not
  be downloaded."
fi

# --- 2. it is an ancestor of HEAD -------------------------------------------

git rev-parse --verify --quiet "$COMMIT^{commit}" > /dev/null \
  || fail "$COMMIT is not a commit in this repository"
git merge-base --is-ancestor "$COMMIT" HEAD \
  || fail "$COMMIT is not an ancestor of HEAD.
  The registration must be in this branch's history, or it registers nothing."

# --- 3. the registration has not changed since ------------------------------

if ! git diff --quiet "$COMMIT" -- "$PREREG"; then
  fail "$PREREG has changed since $COMMIT.
  Differences:
$(git diff --stat "$COMMIT" -- "$PREREG")
  Either restore it, or register again with a new commit and a new lock."
fi

# --- 4. nothing uncommitted in the registration -----------------------------

if ! git diff --quiet -- "$PREREG" || ! git diff --cached --quiet -- "$PREREG"; then
  fail "$PREREG has uncommitted changes. Commit or discard them first."
fi

# --- 5. no result already exists --------------------------------------------

if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT" 2> /dev/null)" ]; then
  fail "$OUT already contains results.
  The held-out set is run once. To run again, move the existing results aside
  deliberately, and say in the writeup that a second run happened and why."
fi

# --- the registered sessions ------------------------------------------------
#
# Read from docs/data.md so the list cannot drift from the registration. A
# session is held out if its row says so.

# `mapfile` needs bash 4; macOS ships 3.2, and this script has to run on the
# development host as well as the benchmark host.
HELDOUT=()
while IFS= read -r line; do
  [ -n "$line" ] && HELDOUT+=("$line")
# Strips the backticks and trims, but NOT interior spaces: the venue
# directory is literally "Nasdaq ITCH".
done < <(awk -F'|' '/\*\*HELD OUT/ {
           gsub(/`/, "", $3); gsub(/^[ \t]+|[ \t]+$/, "", $3); print $3
         }' docs/data.md)

if [ "${#HELDOUT[@]}" -eq 0 ]; then
  fail "no held-out sessions found in docs/data.md"
fi

echo "registration commit  $COMMIT"
echo "registration subject $(git log -1 --format=%s "$COMMIT")"
echo "registration date    $(git log -1 --format=%ad --date=iso "$COMMIT")"
echo "$PREREG unchanged since that commit"
echo
echo "held-out sessions (${#HELDOUT[@]}):"
for f in "${HELDOUT[@]}"; do echo "  $f"; done
echo

if [ "$DRY_RUN" -eq 1 ]; then
  echo "dry run: every gate passed. Re-run without --dry-run to fetch and run."
  exit 0
fi

mkdir -p "$OUT"
git rev-parse HEAD > "$OUT/head.txt"
cp "$LOCK" "$OUT/heldout.lock"
cp "$PREREG" "$OUT/preregistration.md"

for path in "${HELDOUT[@]}"; do
  file="$(basename "$path")"
  dir="$(dirname "$path")"
  echo "=== $file ==="
  if [ ! -f "data/${file%.gz}" ]; then
    tools/fetch_data.sh "$file" "$dir"
  fi
  # Correctness first. A held-out session that fails a correctness layer makes
  # the study inconclusive rather than negative; see the pre-registration's
  # failure criteria.
  ./build/release/census "data/${file%.gz}" | tee "$OUT/census-${file%.gz}.txt"
  ./build/release/determinism "data/${file%.gz}" | tee "$OUT/determinism-${file%.gz}.txt"
  ./build/release/replay "data/${file%.gz}" | tee "$OUT/replay-${file%.gz}.txt"
  python3 research/signal_study.py --heldout "data/${file%.gz}" --out "$OUT" \
    | tee "$OUT/study-${file%.gz}.txt"
done

echo
echo "wrote $OUT"
echo "Report strictly per the decision rules in $PREREG section 8:"
echo "  signal holds / fails / inconclusive, and separately"
echo "  strategy profitable / unprofitable / inconclusive at the base tier,"
echo "  with the top-tier sensitivity alongside."
