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

# Sessions run to several gigabytes and the archive drops connections
# mid-transfer, so the fetch resumes from whatever is already on disk and
# retries rather than restarting. Without -C the download begins again from
# zero, which on a 5 GB session is an hour lost to a transient reset.
echo "fetching ${DIR}/${FILE} ..."
if [ -f "data/${FILE}" ]; then
  echo "resuming from $(wc -c < "data/${FILE}" | tr -d ' ') bytes already on disk"
fi
curl -fL --progress-bar --path-as-is \
  --continue-at - \
  --retry 10 --retry-delay 5 --retry-all-errors \
  --speed-time 60 --speed-limit 1024 \
  "${BASE}/${DIR// /%20}/${FILE}" -o "data/${FILE}"

# The archive publishes an .md5sum beside most sessions. Files are withdrawn
# and re-uploaded over time, so the checksum is the only evidence that a given
# result came from the bytes the author replayed. A missing .md5sum is
# reported, not treated as a pass.
if curl -fsL --path-as-is "${BASE}/${DIR// /%20}/${FILE}.md5sum" -o "data/${FILE}.md5sum" 2>/dev/null; then
  expected=$(tr -d '\r' < "data/${FILE}.md5sum" | awk '{print $1}')
  if command -v md5sum > /dev/null; then
    actual=$(md5sum "data/${FILE}" | awk '{print $1}')
  else
    actual=$(md5 -q "data/${FILE}")
  fi
  if [ "$expected" = "$actual" ]; then
    echo "md5 ok: ${actual}"
  else
    echo "md5 MISMATCH: expected ${expected}, got ${actual}" >&2
    exit 1
  fi
else
  echo "no .md5sum published for ${FILE}; integrity unverified" >&2
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
