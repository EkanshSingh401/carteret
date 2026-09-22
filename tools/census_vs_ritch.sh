#!/usr/bin/env bash
# Compares this project's per-type message census against RITCH::count_messages().
#
# Correctness layer 2. RITCH is an independently written reader of the same
# protocol, so an exact per-type match establishes that the framing loop, the
# length table and the dispatch switch walk a session the same way a second
# implementation does. It establishes nothing about field decode.
#
# Both sides are sorted under LC_ALL=C so the comparison does not depend on the
# caller's collation.
#
# RITCH's counter predates the 'O' Direct Listing With Capital Raise message
# and does not report it. It is therefore compared separately: this project's
# count must be zero for any session predating specification revision
# 2023-04-28, which is every session in docs/data.md.
#
#   usage: tools/census_vs_ritch.sh [session-file]
#
# With no argument, runs against the RITCH package's bundled 12,012-message
# fixture, which is the fast gate.
set -euo pipefail

CENSUS="${CENSUS:-./build/release/census}"
RSCRIPT="${RSCRIPT:-Rscript}"

if [ ! -x "$CENSUS" ]; then
  echo "census binary not found at $CENSUS" >&2
  echo "build it first:  cmake --preset release && cmake --build --preset release" >&2
  exit 2
fi

if ! command -v "$RSCRIPT" > /dev/null; then
  echo "Rscript not found. Install R (brew install r), then:" >&2
  echo "  Rscript -e 'install.packages(\"RITCH\", repos=\"https://cloud.r-project.org\")'" >&2
  exit 2
fi

SESSION="${1:-}"
if [ -z "$SESSION" ]; then
  SESSION=$("$RSCRIPT" --vanilla -e \
    'cat(system.file("extdata", "ex20101224.TEST_ITCH_50", package = "RITCH"))')
  if [ -z "$SESSION" ] || [ ! -f "$SESSION" ]; then
    echo "RITCH package not installed, or its bundled fixture is missing." >&2
    echo "  Rscript -e 'install.packages(\"RITCH\", repos=\"https://cloud.r-project.org\")'" >&2
    exit 2
  fi
  echo "no session given; using the RITCH bundled fixture"
fi

if [ ! -f "$SESSION" ]; then
  echo "no such session file: $SESSION" >&2
  exit 2
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "session   $SESSION"
echo "size      $(wc -c < "$SESSION" | tr -d ' ') bytes"
echo

"$CENSUS" --tsv "$SESSION" > "$WORK/carteret.raw"

"$RSCRIPT" --vanilla -e '
  suppressMessages(library(RITCH))
  args <- commandArgs(trailingOnly = TRUE)
  x <- count_messages(args[1], quiet = TRUE)
  write.table(data.frame(x$msg_type, x$count), stdout(),
              sep = "\t", quote = FALSE, row.names = FALSE, col.names = FALSE)
' "$SESSION" > "$WORK/ritch.raw"

# 'O' is absent from RITCH's type set; compare it separately.
LC_ALL=C grep -v '^O	' "$WORK/carteret.raw" | LC_ALL=C sort > "$WORK/carteret.tsv"
LC_ALL=C sort "$WORK/ritch.raw" > "$WORK/ritch.tsv"

carteret_o=$(LC_ALL=C awk -F'\t' '$1 == "O" { print $2 }' "$WORK/carteret.raw")

printf 'type  %12s  %12s\n' carteret RITCH
LC_ALL=C join -t $'\t' "$WORK/carteret.tsv" "$WORK/ritch.tsv" \
  | LC_ALL=C awk -F'\t' '{ printf "  %s   %12s  %12s  %s\n", $1, $2, $3, ($2 == $3 ? "" : "<-- MISMATCH") }'

echo
if LC_ALL=C diff -u "$WORK/ritch.tsv" "$WORK/carteret.tsv" > "$WORK/diff"; then
  echo "per-type counts: EXACT MATCH on $(LC_ALL=C wc -l < "$WORK/carteret.tsv" | tr -d ' ') types"
  status=0
else
  echo "per-type counts: MISMATCH"
  echo "  -- RITCH, ++ carteret"
  sed -n '3,$p' "$WORK/diff"
  status=1
fi

echo "totals:          carteret $(LC_ALL=C awk -F'\t' '{s+=$2} END {print s+0}' "$WORK/carteret.raw")" \
     "RITCH $(LC_ALL=C awk -F'\t' '{s+=$2} END {print s+0}' "$WORK/ritch.raw")"

if [ "${carteret_o:-0}" = "0" ]; then
  echo "'O' messages:    0 (RITCH does not report this type; zero is expected pre-2023)"
else
  echo "'O' messages:    ${carteret_o} -- unexpected for a session predating 2023-04-28" >&2
  status=1
fi

exit "$status"
