#!/usr/bin/env bash
# Downloads a NASDAQ TotalView-ITCH 5.0 sample session into data/.
#
# The data is free but large and redistribution-restricted, so it is gitignored
# and fetched, never committed. A NASDAQ day is roughly 3.5 GB gzipped and
# 5-13 GB unpacked; budget ~100 GB of disk if you want several.
#
#   usage: tools/fetch_data.sh [filename]
#   e.g.   tools/fetch_data.sh 01302020.NASDAQ_ITCH50.gz
#
# Start with a BX session: same protocol, ~54M messages instead of ~270M, so
# your edit-run loop is seconds rather than minutes. Move to a NASDAQ day only
# once the book is correct.
set -euo pipefail

BASE="https://emi.nasdaq.com/ITCH"
FILE="${1:-20170130.BX_ITCH_50.gz}"
mkdir -p data

echo "fetching ${FILE} ..."
curl -fL --progress-bar "${BASE}/${FILE}" -o "data/${FILE}"
echo "unpacking (this takes a while) ..."
gunzip -k "data/${FILE}"
ls -lh data/
echo
echo "Next: ./build/census data/${FILE%.gz}"
echo "Then check the per-type totals against an independent source before you"
echo "write a single line of order book code."
