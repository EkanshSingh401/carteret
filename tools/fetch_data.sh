#!/usr/bin/env bash
# Downloads a NASDAQ TotalView-ITCH 5.0 sample session into data/.
#
# The sample sessions are free to download but carry redistribution
# restrictions, so data/ is gitignored and every session is fetched rather than
# committed. A NASDAQ day is roughly 3.5-5.6 GB gzipped and 5-13 GB unpacked;
# several sessions need on the order of 100 GB of disk.
#
# Sessions live under a per-venue subdirectory of /ITCH/, not at /ITCH/ itself,
# and the archive's contents change over time: sessions are withdrawn, and at
# least one NASDAQ session is filed under the PSX directory. docs/data.md
# records the index as of a stated date, including which sessions are gone.
#
# The default is BX 2019-01-30, this project's primary correctness session. BX
# runs the same protocol at roughly a fifth of a NASDAQ session's message
# count. BX is taker-maker and NASDAQ is maker-taker, so the two are never
# pooled in a cost-inclusive result; see docs/design.md record 022.
#
#   usage: tools/fetch_data.sh [filename] [venue-directory]
#   e.g.   tools/fetch_data.sh 01302020.NASDAQ_ITCH50.gz "Nasdaq ITCH"
set -euo pipefail

BASE="https://emi.nasdaq.com/ITCH"
FILE="${1:-20190130.BX_ITCH_50.gz}"
DIR="${2:-}"

# Infer the venue directory from the file name when it was not given.
if [ -z "$DIR" ]; then
  case "$FILE" in
    *BX_ITCH_50*|*-bx.*)  DIR="Nasdaq BX ITCH" ;;
    *PSX_ITCH_50*)        DIR="Nasdaq PSX ITCH" ;;
    *)                    DIR="Nasdaq ITCH" ;;
  esac
fi

mkdir -p data

URL="${BASE}/${DIR// /%20}/${FILE}"

# Sessions run to several gigabytes and the archive drops connections
# mid-transfer, so a fetch has to survive a reset. Resuming is the obvious way
# and is NOT safe here: the server advertises "accept-ranges: bytes" and then
# answers every range request with 416 and "content-range: bytes */0". curl
# reads that 416 as "the local file is already complete", prints 100%, and
# exits zero on a file that is a third of the session. Resuming therefore
# happens only if a probe shows the server actually honours a range.
RANGE_STATUS=$(curl -s -o /dev/null -w '%{http_code}' -r 0-0 --max-time 30 "$URL" || echo 000)
if [ "$RANGE_STATUS" = "206" ]; then
  RESUME=(--continue-at -)
  echo "server honours range requests; a partial file will be resumed"
else
  RESUME=()
  echo "server does not honour range requests (probe returned ${RANGE_STATUS}); fetching whole"
  rm -f "data/${FILE}"
fi

EXPECTED=$(curl -sI --max-time 30 "$URL" | tr -d '\r' \
           | awk 'tolower($1) == "content-length:" { print $2 }' | tail -1)

echo "fetching ${DIR}/${FILE}${EXPECTED:+ (${EXPECTED} bytes)} ..."
if [ -n "${RESUME[*]:-}" ] && [ -f "data/${FILE}" ]; then
  echo "resuming from $(wc -c < "data/${FILE}" | tr -d ' ') bytes already on disk"
fi
curl -fL --progress-bar --path-as-is \
  "${RESUME[@]}" \
  --retry 10 --retry-delay 5 --retry-all-errors \
  --speed-time 120 --speed-limit 1024 \
  "$URL" -o "data/${FILE}"

# curl exiting zero is not evidence the file is whole: see the 416 case above.
# The byte count is checked against what the server advertised.
if [ -n "$EXPECTED" ]; then
  ACTUAL=$(wc -c < "data/${FILE}" | tr -d ' ')
  if [ "$ACTUAL" != "$EXPECTED" ]; then
    echo "short download: got ${ACTUAL} bytes, expected ${EXPECTED}; re-run" >&2
    exit 1
  fi
  echo "size ok: ${ACTUAL} bytes"
else
  echo "server advertised no content-length; size unverified" >&2
fi

# A resumed transfer can still be short if the server closed cleanly at the
# wrong point, and gunzip on a truncated stream produces a plausible prefix of
# a session rather than an error at the start. Testing the stream first turns
# that into a failure here instead of a wrong message count later.
echo "verifying the gzip stream ..."
if ! gunzip -t "data/${FILE}"; then
  echo "gzip stream is incomplete or corrupt; re-run to resume" >&2
  exit 1
fi

echo "unpacking ..."
gunzip -k "data/${FILE}"
ls -lh data/
echo
echo "census:     ./build/release/census data/${FILE%.gz}"
echo "comparison: tools/census_vs_ritch.sh data/${FILE%.gz}"
