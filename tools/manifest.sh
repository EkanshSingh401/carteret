#!/usr/bin/env sh
# manifest.sh -- record what was on disk, under a label, in a committed file.
#
# WHY THIS EXISTS. `data/` and `results/` are gitignored, because market data
# is not redistributable and derived CSVs are made from it. That is correct,
# and it has a consequence that was overlooked once and is worth stating: a
# claim about what those directories contained at some commit is NOT checkable
# from the repository. Git records nothing about an untracked path. "It was
# verified at the time" is a claim about the author, which is the kind of claim
# this project's gates exist to avoid needing.
#
# A manifest is committed instead. It carries no market data -- a relative
# path, a byte count and a SHA-256 -- so it may be tracked, and it fixes in the
# history what a later reader would otherwise have to take on trust.
#
# Committed at three points (docs/preregistration.md section 10):
#   - with the gated computation's outputs, listing the inputs it read
#   - with the pre-registration commit, showing no held-out session present
#   - with the heldout.lock commit, showing the same
#
#   usage: tools/manifest.sh <label>
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

LABEL="${1:-}"
[ -n "$LABEL" ] || { echo "usage: $0 <label>" >&2; exit 1; }
case "$LABEL" in
  */*|"") echo "label must not contain a path separator" >&2; exit 1 ;;
esac

OUT="docs/manifests/${LABEL}.txt"
mkdir -p docs/manifests

if command -v shasum > /dev/null 2>&1; then
  sha() { shasum -a 256 "$1" | awk '{print $1}'; }
else
  sha() { sha256sum "$1" | awk '{print $1}'; }
fi

TMP="docs/manifests/.${LABEL}.tmp"
: > "$TMP"

# Sorted by path so the file is a stable diff between labels. Partial and
# in-flight files are listed too: a manifest that hides them would not describe
# the disk.
for dir in data results; do
  [ -d "$dir" ] || continue
  find "$dir" -type f | LC_ALL=C sort | while read -r f; do
    printf '%s  %s  %s\n' "$(sha "$f")" "$(wc -c < "$f" | tr -d ' ')" "$f" >> "$TMP"
  done
done

# The held-out check is the reason two of the three commits carry a manifest,
# so it is asserted here rather than left for a reader to grep for.
HELD=$(LC_ALL=C grep -cE '10302019|01302020' "$TMP" || true)

{
  echo "# Manifest: $LABEL"
  echo "# Written $(date '+%Y-%m-%d %H:%M:%S %z') by tools/manifest.sh"
  echo "#"
  echo "# Every regular file under data/ and results/, both gitignored."
  echo "# Columns: SHA-256, bytes, path. Sorted by path."
  echo "#"
  if [ "$HELD" -eq 0 ]; then
    echo "# HELD-OUT SESSIONS PRESENT: none (0 paths match 10302019 or 01302020)"
  else
    echo "# HELD-OUT SESSIONS PRESENT: $HELD  <-- see docs/preregistration.md section 10"
  fi
  echo "# Files: $(wc -l < "$TMP" | tr -d ' ')"
  echo
  cat "$TMP"
} > "$OUT"

rm -f "$TMP"
echo "wrote $OUT"
if [ "$HELD" -eq 0 ]; then
  echo "held-out sessions present: none"
else
  echo "held-out sessions present: $HELD" >&2
fi
