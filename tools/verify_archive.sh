#!/usr/bin/env sh
# verify_archive.sh -- the integrity gate for a downloaded .gz, on its own.
#
#   usage: tools/verify_archive.sh [--no-published-digest] <file.gz>
#                                  [expected_bytes] [expected_sha256]
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
#   6  no digest was given and --no-published-digest was not passed
#
# EXIT 6 EXISTS BECAUSE SILENCE IS THE WRONG DEFAULT. Most of this archive
# serves no checksum -- every .md5sum URL 404s -- so a digest is often
# genuinely unavailable, and an earlier version simply skipped the check and
# exited 0. A caller then could not tell "verified against the publisher" from
# "nothing was checked". The skip must now be asked for, and when it is, the
# script says the digest was RECORDED rather than VERIFIED.
set -eu

NO_PUBLISHED_DIGEST=0
if [ "${1:-}" = "--no-published-digest" ]; then
  NO_PUBLISHED_DIGEST=1
  shift
fi

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

# 3. SHA-256. Either a published digest is checked, or the absence of one is
#    declared and the digest computed for the record.
if [ -z "$EXPECT_SHA" ] && [ "$NO_PUBLISHED_DIGEST" -eq 0 ]; then
  echo "no expected sha256 was given for $FILE." >&2
  echo "This archive serves no checksum for most sessions, so that may be" >&2
  echo "correct -- but it has to be said rather than assumed. Re-run as:" >&2
  echo "  $0 --no-published-digest $FILE ${EXPECT_BYTES:-<bytes>}" >&2
  echo "and the digest will be RECORDED rather than verified." >&2
  exit 6
fi

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

if [ -z "$EXPECT_SHA" ]; then
  if command -v shasum > /dev/null 2>&1; then
    recorded=$(shasum -a 256 "$FILE" | awk '{print $1}')
  elif command -v sha256sum > /dev/null 2>&1; then
    recorded=$(sha256sum "$FILE" | awk '{print $1}')
  else
    recorded=""
  fi
  echo "sha256 RECORDED, NOT VERIFIED: ${recorded:-unavailable}"
  echo "  No published digest exists for this file, so this value establishes"
  echo "  that a later copy is the same bytes -- not that these bytes are the"
  echo "  ones the publisher served. Record it in docs/data.md."
fi

echo "verified $FILE ($actual bytes)"
