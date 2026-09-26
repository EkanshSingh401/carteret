#!/usr/bin/env bash
# numbers_negative.sh -- proves the numbers gate can fail.
#
# tools/check_numbers.py is what makes a figure in README.md or the
# registration a claim about a committed file rather than a typed number. Seen
# only to pass, it could be checking nothing. This copies the checked documents
# and their sources into a scratch repository, requires the copy to pass, then
# breaks one thing at a time and requires each break to fail.
#
#   usage: tests/numbers_negative.sh [repository-root]
set -u

ROOT=$(cd "${1:-$(dirname "$0")/..}" && pwd)
CHECK="$ROOT/tools/check_numbers.py"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
failures=0

fresh() {
  rm -rf "$WORK/r"
  mkdir -p "$WORK/r"
  (cd "$ROOT" && git ls-files README.md docs) | while read -r f; do
    mkdir -p "$WORK/r/$(dirname "$f")"
    cp "$ROOT/$f" "$WORK/r/$f"
  done
  git -C "$WORK/r" init -q
  git -C "$WORK/r" add -A
}

expect() {  # expect <want-exit> <label>
  python3 "$CHECK" "$WORK/r" > "$WORK/out" 2>&1
  local got=$?
  if [ "$got" -eq "$1" ]; then
    echo "ok    $2"
  else
    echo "FAIL  $2: exit $got, wanted $1"
    sed 's/^/      /' "$WORK/out"
    failures=$((failures + 1))
  fi
}

fresh
expect 0 "(control) the committed documents pass"

fresh
sed -i.bak 's/\*\*167\.78 ns\*\* <!--src/**167.79 ns** <!--src/' "$WORK/r/README.md"
expect 1 "a README figure its source does not contain"

fresh
sed -i.bak 's/\*\*167\.78 ns\*\* <!--src/**67.78 ns** <!--src/' "$WORK/r/README.md"
expect 1 "a README figure that occurs only inside a longer number"

fresh
sed -i.bak 's|<!--src:docs/data.md-->|<!--src:docs/no-such-file.md-->|' "$WORK/r/README.md"
expect 1 "a src tag naming a file that does not exist"

fresh
printf '167.78\n' > "$WORK/r/docs/untracked.md"
sed -i.bak 's|\*\*167\.78 ns\*\* <!--src:docs/benchmarks.md-->|**167.78 ns** <!--src:docs/untracked.md-->|' "$WORK/r/README.md"
expect 1 "a src tag naming a file git does not track"

fresh
sed -i.bak 's/55\.16% <!--gen:accuracy_bar-->/55.17% <!--gen:accuracy_bar-->/' "$WORK/r/README.md"
expect 1 "a README figure disagreeing with the generated value"

fresh
sed -i.bak 's/<!--gen:accuracy_bar-->/<!--gen:no_such_key-->/' "$WORK/r/README.md"
expect 1 "a gen tag naming a key that was never generated"

if [ "$failures" -ne 0 ]; then
  echo "$failures case(s) failed"
  exit 1
fi
echo "all cases behaved"
