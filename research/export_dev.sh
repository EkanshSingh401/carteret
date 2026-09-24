#!/usr/bin/env sh
# export_dev.sh -- feature CSVs for the seven development sessions.
#
# The gated computation reads these and nothing else. Kept as a script rather
# than a command in a README so that the inputs to a reported result are fixed
# by a commit, which is the property research/README.md asks for.
#
# Window and symbol universe are the registered ones: N = 50 book updates,
# and the 50 symbols with the most book messages in each session, selected per
# session from that session's own activity (docs/preregistration.md section 5
# and "Symbol universe").
#
# THE HELD-OUT SESSIONS ARE NOT IN THIS LIST AND MUST NOT BE ADDED. 2019-10-30
# and 2020-01-30 are not exported until section 10's sequence has completed.
# research/gated.py refuses a file whose name carries either date, but the
# first line of defence is not generating it.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

OUT=results/features
mkdir -p "$OUT"

for s in 01302019.NASDAQ_ITCH50 \
         03272019.NASDAQ_ITCH50 \
         05302019.NASDAQ_ITCH50 \
         07302019.NASDAQ_ITCH50 \
         08302019.NASDAQ_ITCH50 \
         S101819-v50.txt \
         12302019.NASDAQ_ITCH50; do
  case "$s" in
    *10302019*|*01302020*)
      echo "REFUSING $s: held out" >&2; exit 1 ;;
  esac
  dst="$OUT/dev_${s}.csv"
  if [ -f "$dst" ]; then
    echo "already present: $dst"
    continue
  fi
  echo "=== $s  $(date '+%H:%M:%S')"
  ./build/release/export_features --window 50 --symbols 50 --out "$dst" "data/$s"
  wc -l < "$dst" | tr -d ' ' | sed 's/^/  rows /'
done

echo
echo "development feature files:"
ls -l "$OUT"/dev_*.csv | awk '{print "  "$9"  "$5" bytes"}'
