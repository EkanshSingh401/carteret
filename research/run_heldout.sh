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
#   usage: research/run_heldout.sh [--dry-run] [--rehearse]
#
# --dry-run    prints the exact commands the real run would execute and checks
#              them against the list recorded in
#              docs/heldout-harness-amendment.md. It fails if they differ, so
#              a runner wired to the wrong script cannot pass it. An earlier
#              --dry-run exited before it reached the commands at all, which
#              is why a runner pointing at a script that reported no verdict
#              passed every gate; see section 3 of that document.
#
# --rehearse   executes the IDENTICAL code path on the seven development
#              sessions. Only the session list and the output directory
#              differ. Lock validation is skipped, because development data
#              is not what the lock protects.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

LOCK="research/heldout.lock"
PREREG="docs/preregistration.md"
OUT="results/heldout"
DRY_RUN=0
REHEARSE=0
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    --rehearse) REHEARSE=1 ;;
    *) echo "unknown flag $arg" >&2; exit 2 ;;
  esac
done
[ "$REHEARSE" -eq 1 ] && OUT="results/rehearsal"
AMEND="docs/heldout-harness-amendment.md"

# The analysis entry point. Named once, here, so the dry run and the real run
# cannot disagree about which script executes.
STUDY="research/heldout_study.py"
LEGACY="research/signal_study.py"

fail() {
  echo "REFUSED: $1" >&2
  exit 1
}

if [ "$REHEARSE" -eq 1 ]; then
  echo "REHEARSAL: development sessions, identical code path, lock gates skipped."
  echo "  Development data is not what the lock protects. Everything below this"
  echo "  point is the same code the real run executes."
  echo
fi

if [ "$REHEARSE" -eq 0 ]; then
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

# --- 3b. the digest in the lock matches the registration at that commit -----
#
# Checks 2 and 3 are satisfied by editing the registration and amending the
# commit: the hash moves with it. A digest written into the lock BEFORE the
# held-out data existed is not, because anyone can recompute it. This is the
# check that makes the lock evidence rather than a note to self.
LOCK_SHA=$(grep -oE '^sha256[[:space:]]+[0-9a-f]{64}' "$LOCK" 2> /dev/null | awk '{print $2}' || true)
if [ -z "$LOCK_SHA" ]; then
  fail "$LOCK carries no sha256 of $PREREG.
  Add the digest of the registration AS COMMITTED:
      sha256 \$(git show <commit>:$PREREG | shasum -a 256 | awk '{print \$1}')
  A lock naming only a commit can be satisfied by amending that commit."
fi
if command -v shasum > /dev/null 2>&1; then
  HAVE_SHA=$(git show "$COMMIT:$PREREG" | shasum -a 256 | awk '{print $1}')
else
  HAVE_SHA=$(git show "$COMMIT:$PREREG" | sha256sum | awk '{print $1}')
fi
if [ "$LOCK_SHA" != "$HAVE_SHA" ]; then
  fail "$PREREG at $COMMIT does not match the digest in $LOCK.
  lock  $LOCK_SHA
  have  $HAVE_SHA
  The registration was changed after the lock was written. This study is not
  confirmatory; report it as exploratory."
fi

# --- 3c. the CI run named by the lock went green ----------------------------
#
# Section 10: the lock is written after CI is green on the pushed
# registration, so the run id is part of what is being locked.
LOCK_CI=$(grep -oE '^ci_run[[:space:]]+[0-9]+' "$LOCK" 2> /dev/null | awk '{print $2}' || true)
if [ -z "$LOCK_CI" ]; then
  fail "$LOCK names no ci_run.
  Add the id of the workflow run that went green on $COMMIT:
      ci_run <run id>"
fi
if command -v gh > /dev/null 2>&1; then
  CI_CONCLUSION=$(gh run view "$LOCK_CI" --json conclusion -q .conclusion 2> /dev/null || true)
  if [ "$CI_CONCLUSION" != "success" ]; then
    fail "CI run $LOCK_CI did not succeed (conclusion: ${CI_CONCLUSION:-unknown}).
  The registration must be green before the held-out sessions are fetched."
  fi
  echo "ci run $LOCK_CI: success"
else
  echo "gh not available; CI run $LOCK_CI recorded but not verified here" >&2
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

fi  # end of the lock gates

# --- the sessions under test ------------------------------------------------
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

# The development list, for --rehearse. Read from the same table, so the two
# lists cannot drift apart either.
DEV=()
while IFS= read -r line; do
  [ -n "$line" ] && DEV+=("$line")
# Column 3 must be a backticked .gz path and column 5 must say development.
# Matching "development" anywhere on the line picks up prose and the transfer
# table, which is how "data/development Mac" appeared in a command list.
done < <(awk -F'|' '$3 ~ /`.*\.gz`/ && $5 ~ /development/ && !/HELD OUT/ {
           gsub(/`/, "", $3); gsub(/^[ \t]+|[ \t]+$/, "", $3); print $3
         }' docs/data.md)

SESSIONS=()
if [ "$REHEARSE" -eq 1 ]; then
  LABEL="development"
  SESSIONS=("${DEV[@]}")
  [ "${#SESSIONS[@]}" -gt 0 ] || fail "no development sessions found in docs/data.md"
else
  LABEL="heldout"
  SESSIONS=("${HELDOUT[@]}")
  echo "registration commit  $COMMIT"
  echo "registration subject $(git log -1 --format=%s "$COMMIT")"
  echo "registration date    $(git log -1 --format=%ad --date=iso "$COMMIT")"
  echo "$PREREG unchanged since that commit"
  echo
fi

echo "sessions under test (${#SESSIONS[@]}), label $LABEL:"
for f in "${SESSIONS[@]}"; do echo "  $f"; done
echo

# --- the commands the run will execute -------------------------------------
#
# Built as a list FIRST, so --dry-run shows exactly what --dry-run is checking
# and the real run executes nothing else. The previous runner built no list:
# its dry run stopped before the commands existed, so it could not notice that
# the command it would have run pointed at a script which reported no verdict.

CMDS=()
FEATURES=()
RAW=()
for path in "${SESSIONS[@]}"; do
  file="$(basename "$path")"
  name="${file%.gz}"
  RAW+=("data/$name")
  FEATURES+=("$OUT/features_${name}.csv")
  CMDS+=("./build/release/census data/$name")
  CMDS+=("./build/release/determinism data/$name")
  CMDS+=("./build/release/replay --market Q data/$name")
  CMDS+=("./build/release/export_features --window 50 --symbols 50 --out $OUT/features_${name}.csv data/$name")
done
CMDS+=("python3 $STUDY --sessions ${FEATURES[*]} --out $OUT --primary C --label $LABEL --strategy --exploratory --strategy-sessions ${RAW[*]}")

if [ "$DRY_RUN" -eq 1 ]; then
  echo "commands this run would execute, in order:"
  printf '  %s\n' "${CMDS[@]}"
  echo

  # The expected list lives in the amendment document, between markers. A
  # runner wired to the legacy script produces a different last line and fails
  # here. This is the check the old dry run did not have.
  if [ "$REHEARSE" -eq 0 ]; then
    exp="$(awk '/<!-- expected-commands -->/{f=1;next} /<!-- \/expected-commands -->/{f=0} f' "$AMEND" \
           | sed -n 's/^    //p')"
    got="$(printf '%s\n' "${CMDS[@]}")"
    if [ "$exp" != "$got" ]; then
      echo "EXPECTED, from $AMEND:" >&2
      printf '%s\n' "$exp" >&2
      echo "GOT:" >&2
      printf '%s\n' "$got" >&2
      fail "the commands this runner would execute do not match the list
  recorded in $AMEND. Either the runner was rewired without updating the
  record, or the record was changed without rewiring the runner. Both are
  reasons to stop."
    fi
    echo "commands match the list recorded in $AMEND"
  fi
  echo "dry run: every gate passed. Re-run without --dry-run to execute."
  exit 0
fi

# The legacy path must not be reachable. It reported no verdict and returned
# zero, which is how it survived two amendments and two locks.
if grep -q "$LEGACY" <<< "${CMDS[*]}"; then
  fail "a command references $LEGACY, which cannot produce a registered verdict."
fi

mkdir -p "$OUT"
git rev-parse HEAD > "$OUT/head.txt"
cp "$LOCK" "$OUT/heldout.lock" 2>/dev/null || true
cp "$PREREG" "$OUT/preregistration.md"
printf '%s\n' "${CMDS[@]}" > "$OUT/commands.txt"

for path in "${SESSIONS[@]}"; do
  file="$(basename "$path")"
  name="${file%.gz}"
  dir="$(dirname "$path")"
  echo "=== $name ==="
  if [ ! -f "data/$name" ]; then
    if [ "$REHEARSE" -eq 1 ]; then
      fail "rehearsal expects data/$name to be present already"
    fi
    tools/fetch_data.sh "$file" "$dir"
  fi
  # Correctness first. A session that fails a correctness layer makes the
  # study inconclusive rather than negative; see the failure criteria.
  ./build/release/census "data/$name" | tee "$OUT/census-$name.txt"
  ./build/release/determinism "data/$name" | tee "$OUT/determinism-$name.txt"
  ./build/release/replay --market Q "data/$name" | tee "$OUT/replay-$name.txt"
  ./build/release/export_features --window 50 --symbols 50 \
    --out "$OUT/features_${name}.csv" "data/$name"
done

# ONE call, over every session together. The registered inference pools them
# and resamples WITHIN sessions; running the analysis per session would be the
# degenerate estimator section 4 rejected.
python3 "$STUDY" --sessions "${FEATURES[@]}" --out "$OUT" --primary C \
  --label "$LABEL" --strategy --exploratory --strategy-sessions "${RAW[@]}" \
  | tee "$OUT/study.txt"

echo
echo "wrote $OUT"
echo "Report strictly per the decision rules in $PREREG section 8:"
echo "  signal holds / fails / inconclusive, and separately"
echo "  strategy profitable / unprofitable / inconclusive at the base tier,"
echo "  with the top-tier sensitivity alongside."
