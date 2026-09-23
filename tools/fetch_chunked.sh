#!/usr/bin/env sh
# fetch_chunked.sh -- resumable download by explicit byte range, validated.
#
# Whole-file downloads of this archive's larger sessions do not complete. The
# measured pattern on 01302019.NASDAQ_ITCH50.gz (4.76 GB) was 1.5%, then
# 67.6%, then 0.4%, then 0.4%, and on 03272019 (5.51 GB) 0.7% then three
# immediate failures -- progressive degradation rather than a fixed duration
# cap, which is the shape of server-side throttling. Whole-file transfer is
# therefore not a strategy for these files, and the server does honour ranges:
#
#   GET with Range: bytes=0-1023  ->  206, content-range: bytes 0-1023/4764426091
#
# THE DANGER THIS SCRIPT EXISTS TO AVOID. An earlier attempt used
# `curl --continue-at`, and on a retry the server returned the WHOLE body,
# which curl appended to the partial file. The result was 4,038,899,612 bytes
# against an advertised 3,524,013,057 -- 115% of the file, with a valid gzip
# stream at the front, so it looked finished. Nothing is appended here until
# the response has been proved to be the range that was asked for.
#
# Every chunk must satisfy all four:
#   - HTTP status exactly 206. A 200 is the whole file and is REJECTED.
#   - Content-Range exactly "bytes START-END/TOTAL" for the range requested.
#   - ETag identical to the first chunk's, so the file cannot change mid-fetch.
#   - Body length exactly END-START+1.
#
#   usage: tools/fetch_chunked.sh <file.gz> [directory] [chunk_bytes]
#          tools/fetch_chunked.sh --check-headers <hdrfile> <start> <end> <total> [etag]
#
# Exit codes:
#   0   complete and verified
#   1   usage, or a check outside the per-chunk validation failed
#   10  response was not 206
#   11  Content-Range did not match the request
#   12  ETag changed during the download
#   13  body length did not match the range
set -eu

BASE="https://emi.nasdaq.com/ITCH"

# --- per-chunk validation, callable on its own so it can be tested offline --
#
# Takes a file of response headers rather than making a request, which is what
# lets tests/chunk_negative.cmake feed it a 200, a mismatched Content-Range and
# a changed ETag without a network.
check_headers() {
  hdr=$1; start=$2; end=$3; total=$4; want_etag=${5:-}

  status=$(awk 'tolower($1) ~ /^http/ {print $2; exit}' "$hdr" | tr -d '\r')
  if [ "$status" != "206" ]; then
    echo "chunk rejected: status $status, expected 206" >&2
    [ "$status" = "200" ] && echo "  a 200 is the whole file, not the range asked for" >&2
    return 10
  fi

  cr=$(awk 'tolower($1) == "content-range:" {sub(/^[^:]*: */, ""); print; exit}' "$hdr" \
       | tr -d '\r')
  want="bytes ${start}-${end}/${total}"
  if [ "$cr" != "$want" ]; then
    echo "chunk rejected: content-range mismatch" >&2
    echo "  asked for $want" >&2
    echo "  got       ${cr:-<absent>}" >&2
    return 11
  fi

  if [ -n "$want_etag" ]; then
    etag=$(awk 'tolower($1) == "etag:" {sub(/^[^:]*: */, ""); print; exit}' "$hdr" | tr -d '\r')
    if [ "$etag" != "$want_etag" ]; then
      echo "chunk rejected: etag changed during the download" >&2
      echo "  started with $want_etag" >&2
      echo "  now          ${etag:-<absent>}" >&2
      return 12
    fi
  fi
  return 0
}

if [ "${1:-}" = "--check-headers" ]; then
  shift
  [ $# -ge 4 ] || { echo "usage: $0 --check-headers <hdr> <start> <end> <total> [etag]" >&2; exit 1; }
  check_headers "$@"
  exit $?
fi

FILE="${1:-}"
[ -n "$FILE" ] || { echo "usage: $0 <file.gz> [directory] [chunk_bytes]" >&2; exit 1; }
DIR="${2:-}"
CHUNK="${3:-67108864}"   # 64 MiB

if [ -z "$DIR" ]; then
  case "$FILE" in
    *BX_ITCH_50*|*-bx.*) DIR="Nasdaq BX ITCH" ;;
    *PSX_ITCH_50*)       DIR="Nasdaq PSX ITCH" ;;
    *)                   DIR="Nasdaq ITCH" ;;
  esac
fi
URL="${BASE}/$(printf '%s' "$DIR" | sed 's/ /%20/g')/${FILE}"

mkdir -p data
PART="data/.${FILE}.chunked"
HDR="data/.${FILE}.hdr"

TOTAL=$(curl -sI --max-time 60 "$URL" | tr -d '\r' \
        | awk 'tolower($1)=="content-length:" {print $2}' | tail -1)
ETAG=$(curl -sI --max-time 60 "$URL" | tr -d '\r' \
       | awk 'tolower($1)=="etag:" {sub(/^[^:]*: */, ""); print}' | tail -1)
[ -n "$TOTAL" ] || { echo "server advertised no content-length; refusing" >&2; exit 1; }
echo "$FILE: $TOTAL bytes, etag ${ETAG:-<none>}, chunk $CHUNK"

have=0
[ -f "$PART" ] && have=$(wc -c < "$PART" | tr -d ' ')
if [ "$have" -gt "$TOTAL" ]; then
  echo "partial is larger than the file ($have > $TOTAL); deleting and restarting" >&2
  rm -f "$PART"; have=0
fi
echo "resuming at $have"

while [ "$have" -lt "$TOTAL" ]; do
  start=$have
  end=$((start + CHUNK - 1))
  [ "$end" -ge "$TOTAL" ] && end=$((TOTAL - 1))

  # Body and headers to separate temporaries. The body is NOT appended until
  # the headers have been validated against the request.
  #
  # A chunk is retried in place rather than failing the run. The throughput
  # this archive offers varies by more than an order of magnitude -- 11.6 MB/s
  # was measured on one session and 0.34-0.76 MB/s on the next -- and a
  # connection that stalls completely is common. --speed-limit abandons a
  # chunk that drops below 24 KB/s for 45 s, which turns a stall into a fast
  # retry instead of a fifteen-minute wait on a dead socket. Only the chunk is
  # lost, because nothing is appended until it is validated.
  tmp="data/.${FILE}.chunk"
  attempt=1
  while : ; do
    if curl -sS --fail-with-body --max-time 1800 \
          --speed-limit 24576 --speed-time 45 --retry 0 \
          -D "$HDR" -o "$tmp" -H "Range: bytes=${start}-${end}" "$URL"; then
      break
    fi
    rm -f "$tmp"
    if [ "$attempt" -ge 20 ]; then
      echo "" >&2
      echo "chunk ${start}-${end}: 20 attempts failed; re-run to resume from $have" >&2
      exit 1
    fi
    back=$((attempt * 5))
    [ "$back" -gt 60 ] && back=60
    printf '\r  chunk %s-%s stalled, retry %s in %ss   ' "$start" "$end" "$attempt" "$back" >&2
    sleep "$back"
    attempt=$((attempt + 1))
  done

  if ! check_headers "$HDR" "$start" "$end" "$TOTAL" "$ETAG"; then
    rc=$?
    rm -f "$tmp"
    exit $rc
  fi

  got=$(wc -c < "$tmp" | tr -d ' ')
  want=$((end - start + 1))
  if [ "$got" != "$want" ]; then
    echo "chunk rejected: body is $got bytes, range asked for $want" >&2
    rm -f "$tmp"; exit 13
  fi

  cat "$tmp" >> "$PART"
  rm -f "$tmp"
  have=$(wc -c < "$PART" | tr -d ' ')
  printf '\r  %s / %s bytes (%.1f%%)' "$have" "$TOTAL" \
    "$(awk "BEGIN{print $have*100/$TOTAL}")"
done
printf '\n'

# --- end-to-end verification before the file is promoted -------------------
#
# Nothing above proves the assembled bytes are right: each chunk was the range
# asked for, but a silently corrupted byte inside a chunk would pass every one
# of those checks. gzip's trailer settles it -- see docs/data.md.
echo "verifying the assembled file ..."
if ! sh "$(dirname "$0")/verify_archive.sh" --no-published-digest "$PART" "$TOTAL"; then
  echo "assembled file failed verification; NOT promoting it" >&2
  exit 1
fi

mv "$PART" "data/${FILE}"
rm -f "$HDR"
echo "promoted data/${FILE}"
echo "next: gunzip, then census --sha256, venue_profile and replay (docs/data.md)"
