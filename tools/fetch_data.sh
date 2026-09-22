#!/usr/bin/env bash
# Downloads a NASDAQ TotalView-ITCH 5.0 sample session into data/.
#
# The sample sessions are free to download but carry redistribution
# restrictions, so data/ is gitignored and every session is fetched rather than
# committed. A NASDAQ day is roughly 3.5 GB gzipped and 5-13 GB unpacked;
# several sessions need on the order of 100 GB of disk.
#
# A BX session runs the same protocol at roughly 54M messages against a NASDAQ
# day's ~270M, which makes it the cheaper target for correctness iteration.
#
#   usage: tools/fetch_data.sh [filename]
#   e.g.   tools/fetch_data.sh 01302020.NASDAQ_ITCH50.gz
set -euo pipefail

BASE="https://emi.nasdaq.com/ITCH"
FILE="${1:-20170130.BX_ITCH_50.gz}"
mkdir -p data

echo "fetching ${FILE} ..."
curl -fL --progress-bar "${BASE}/${FILE}" -o "data/${FILE}"
echo "unpacking ..."
gunzip -k "data/${FILE}"
ls -lh data/
echo
echo "census:     ./build/release/census data/${FILE%.gz}"
echo "comparison: tools/census_vs_ritch.sh data/${FILE%.gz}"
