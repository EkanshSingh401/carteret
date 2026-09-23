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

# Sessions run to several gigabytes and this archive drops connections
# mid-transfer, so a fetch has to survive a reset. The obvious answer --
# continuing a partial file with curl's --continue-at -- was tried twice and
# abandoned, because this server's range handling is unreliable in two ways
# that both end in a file that looks finished and is not:
#
#   * It answers HEAD carrying a Range header with 416 and "content-range:
#     bytes */0", while answering GET carrying a Range with a correct 206. A
#     probe built on HEAD concludes, wrongly, that ranges are unsupported.
#   * More seriously, a continued transfer that hit curl's slow-transfer
#     timeout and retried came back with the WHOLE body rather than the
#     requested range, and curl appended it to the partial file. The result
#     was 115% of the advertised length and still growing, with valid gzip at
#     the front and garbage from the restart offset on. A size check catches
#     that, but only after the bandwidth is spent.
#
# So: no partial continuation. Every attempt fetches the whole file to a
# temporary path, and the file is moved into place only once its length
# matches what the server advertised. A failed attempt therefore leaves no
# partial file that a later run could mistake for a good one.
EXPECTED=$(curl -sI --max-time 30 "$URL" | tr -d '\r' \
           | awk 'tolower($1) == "content-length:" { print $2 }' | tail -1)

if [ -f "data/${FILE}" ] && [ -n "$EXPECTED" ] \
   && [ "$(wc -c < "data/${FILE}" | tr -d ' ')" = "$EXPECTED" ]; then
  echo "already have ${FILE} at the advertised ${EXPECTED} bytes"
else
  TMP="data/.${FILE}.partial"
  rm -f "$TMP"
  ATTEMPTS="${FETCH_ATTEMPTS:-4}"
  ok=0
  for attempt in $(seq 1 "$ATTEMPTS"); do
    echo "fetching ${DIR}/${FILE}${EXPECTED:+ (${EXPECTED} bytes)}, attempt ${attempt}/${ATTEMPTS} ..."
    rm -f "$TMP"
    if curl -fL --progress-bar --path-as-is \
         --speed-time 120 --speed-limit 10240 \
         "$URL" -o "$TMP"; then
      actual=$(wc -c < "$TMP" | tr -d ' ')
      if [ -z "$EXPECTED" ]; then
        echo "server advertised no content-length; size unverified"
        ok=1
        break
      elif [ "$actual" = "$EXPECTED" ]; then
        echo "size ok: ${actual} bytes"
        ok=1
        break
      else
        echo "short or over-long: got ${actual}, expected ${EXPECTED}; retrying" >&2
      fi
    else
      echo "transfer failed; retrying" >&2
    fi
  done
  if [ "$ok" -ne 1 ]; then
    rm -f "$TMP"
    echo "gave up after ${ATTEMPTS} attempts; no partial file left behind" >&2
    exit 1
  fi
  mv "$TMP" "data/${FILE}"
fi

# A file of the right length can still be the wrong bytes, and gunzip on a
# truncated stream produces a plausible prefix of a session rather than
# failing at the start.
#
# Appended bytes are the shape of corruption a bad continued transfer produced
# on this archive, and gzip reports them inconsistently: macOS gzip exits 2
# with "trailing garbage ignored" on stderr, GNU gzip exits 0 with the same
# warning. Both are checked -- a nonzero exit, and any output at all -- so the
# result does not depend on which gzip is installed.
echo "verifying the gzip stream ..."
# The length and gzip checks live in tools/verify_archive.sh so that they can
# be run against deliberately corrupted input; tests/integrity_negative.cmake
# does exactly that on every build. Keeping them here would have meant a gate
# that only ever saw good data.
if ! sh "$(dirname "$0")/verify_archive.sh" "data/${FILE}" "${EXPECTED:-}"; then
  echo "delete data/${FILE} and re-run" >&2
  exit 1
fi
echo "gzip ok, no trailing garbage"

# Recorded in docs/data.md. The archive serves no checksum of its own (every
# .md5sum URL 404s), so this is the only handle a later reader has on whether
# they have the same bytes.
if command -v shasum > /dev/null; then
  echo "sha256 (.gz)      $(shasum -a 256 "data/${FILE}" | awk '{print $1}')"
elif command -v sha256sum > /dev/null; then
  echo "sha256 (.gz)      $(sha256sum "data/${FILE}" | awk '{print $1}')"
fi

echo "unpacking ..."
gunzip -k "data/${FILE}"
ls -lh data/
echo
SESSION="data/${FILE%.gz}"

# The final integrity check is a content check, not a byte check. Since NASDAQ
# writes no zero-length terminator, a truncated session is a well-formed
# prefix of a valid one; the only thing that distinguishes it is that its last
# message is not System Event 'C', End of Messages. The census decides that
# and exits nonzero if it fails, so a bad file cannot get past this script.
if [ -x ./build/release/census ]; then
  echo
  echo "verifying the session ..."
  if ./build/release/census --sha256 "$SESSION" | sed -n '1,14p'; then
    echo "session verified"
  else
    echo "SESSION FAILED VERIFICATION; do not use it" >&2
    exit 1
  fi
else
  echo
  echo "census not built, so the session content is unverified. Build it and run:" >&2
  echo "  ./build/release/census --sha256 $SESSION" >&2
fi

echo
echo "comparison: tools/census_vs_ritch.sh $SESSION"
echo "record the sha256 above in docs/data.md"
