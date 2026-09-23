#!/usr/bin/env sh
# verify_archive.sh -- the integrity gate for a downloaded .gz, on its own.
#
#   usage: tools/verify_archive.sh <file.gz> [expected_bytes] [expected_sha256]
#
# Extracted from fetch_data.sh so that it can be run against deliberately
# corrupted input. A gate that has only ever seen good data is not known to
# reject bad data; tests/integrity_negative.cmake feeds it a truncated stream,
# an appended stream, a short file and a wrong digest, and requires a nonzero
# exit from each.
#
# Exit codes are distinct so a caller, and a test, can tell which check failed:
#   0  every requested check passed
#   1  usage, or the file is missing
#   2  length does not match the expected size
#   3  the gzip stream is corrupt
#   4  the gzip stream carries trailing garbage
#   5  the SHA-256 does not match
set -eu

if [ $# -lt 1 ]; then
  echo "usage: $0 <file.gz> [expected_bytes] [expected_sha256]" >&2
  exit 1
fi

FILE=$1
EXPECT_BYTES=${2:-}
EXPECT_SHA=${3:-}

[ -f "$FILE" ] || { echo "no such file: $FILE" >&2; exit 1; }

# 1. Length. The archive drops connections mid-transfer, so a short file is
#    the most common failure and the cheapest to detect.
actual=$(wc -c < "$FILE" | tr -d ' ')
if [ -n "$EXPECT_BYTES" ] && [ "$actual" != "$EXPECT_BYTES" ]; then
  echo "length mismatch: have $actual bytes, expected $EXPECT_BYTES" >&2
  exit 2
fi

# 2. The gzip stream. A file of the right length can still be the wrong bytes.
#    gzip reports appended data inconsistently -- macOS exits 2 with "trailing
#    garbage ignored" on stderr, GNU exits 0 with the same warning -- so both
#    a nonzero exit and any output at all are treated as failure, and the two
#    are given different exit codes because they mean different things.
if ! gz_err=$(gzip -t "$FILE" 2>&1); then
  if [ -n "$gz_err" ] && ( echo "$gz_err" | grep -qi "trailing garbage" ); then
    echo "$gz_err" >&2
    echo "trailing garbage: bytes appended after the gzip stream" >&2
    exit 4
  fi
  echo "$gz_err" >&2
  echo "gzip stream is corrupt" >&2
  exit 3
fi
if [ -n "$gz_err" ]; then
  echo "$gz_err" >&2
  echo "trailing garbage: bytes appended after the gzip stream" >&2
  exit 4
fi

# 3. SHA-256, when one is known. docs/data.md records it per session.
if [ -n "$EXPECT_SHA" ]; then
  if command -v shasum > /dev/null 2>&1; then
    have=$(shasum -a 256 "$FILE" | awk '{print $1}')
  elif command -v sha256sum > /dev/null 2>&1; then
    have=$(sha256sum "$FILE" | awk '{print $1}')
  else
    echo "no shasum or sha256sum available; digest unverified" >&2
    have=""
  fi
  if [ -n "$have" ] && [ "$have" != "$EXPECT_SHA" ]; then
    echo "sha256 mismatch:" >&2
    echo "  have     $have" >&2
    echo "  expected $EXPECT_SHA" >&2
    exit 5
  fi
fi

echo "verified $FILE ($actual bytes)"
