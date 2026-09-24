#!/usr/bin/env sh
# verify_session.sh -- the five arrival checks, in order, on one session.
#
# docs/data.md fixes these and requires all five before a session counts as
# local and verified. Running them by hand invites running four of them, so
# they are one command:
#
#   1  archive integrity: advertised length, gzip stream, no trailing garbage
#   2  unpack, and record the SHA-256 of both the archive and the session
#   3  census: clean framing, and the last message is System Event 'C'
#   4  venue profile: the session must be NASDAQ, decided from content
#   5  differential replay: RESULT: identical
#
# Exits nonzero on the first failure and says which check failed. A session
# that fails any of them is not analysed and is not quietly replaced.
#
#   usage: tools/verify_session.sh <name-without-.gz> [advertised_bytes]
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME="${1:-}"
[ -n "$NAME" ] || { echo "usage: $0 <name-without-.gz> [advertised_bytes]" >&2; exit 1; }
BYTES="${2:-}"
GZ="data/${NAME}.gz"
SESSION="data/${NAME}"

cd "$ROOT"
[ -f "$GZ" ] || { echo "no archive: $GZ" >&2; exit 1; }

echo "=== $NAME ==="

echo "[1/5] archive integrity"
sh tools/verify_archive.sh --no-published-digest "$GZ" ${BYTES:+"$BYTES"} \
  || { echo "FAILED check 1: archive integrity" >&2; exit 1; }

echo "[2/5] unpack and digest"
[ -f "$SESSION" ] || gunzip -k "$GZ"
if command -v shasum > /dev/null 2>&1; then
  GZ_SHA=$(shasum -a 256 "$GZ" | awk '{print $1}')
  SE_SHA=$(shasum -a 256 "$SESSION" | awk '{print $1}')
else
  GZ_SHA=$(sha256sum "$GZ" | awk '{print $1}')
  SE_SHA=$(sha256sum "$SESSION" | awk '{print $1}')
fi
echo "  sha256 .gz      $GZ_SHA"
echo "  sha256 session  $SE_SHA"

echo "[3/5] census"
./build/release/census --sha256 "$SESSION" > "data/.${NAME}.census" 2>&1 \
  || { tail -5 "data/.${NAME}.census" >&2; echo "FAILED check 3: census" >&2; exit 1; }
grep -E "^messages|final message|complete session" "data/.${NAME}.census" | sed 's/^/  /'

echo "[4/5] venue profile"
./build/release/venue_profile "$SESSION" | tail -1 | sed 's/^/  /'
VERDICT=$(./build/release/venue_profile "$SESSION" | tail -1 | sed 's/.*  //')
if [ "$VERDICT" != "NASDAQ" ]; then
  echo "FAILED check 4: venue is '$VERDICT', not NASDAQ." >&2
  echo "  The registration's session list is amended on provenance grounds" >&2
  echo "  BEFORE any gated computation. See docs/data.md." >&2
  exit 4
fi

echo "[5/5] differential replay"
# The market code is taken from check 4's verdict, not from the file name.
# replay infers it from the name and REFUSES when the name is unrecognised --
# `S101819-v50.txt` follows neither convention and so exits 2 before replaying
# anything. Check 4 has already established the venue from content by this
# point, and NASDAQ's market code is 'Q', so passing it is using the verdict
# rather than guessing. A session that reaches here and is not NASDAQ has
# already exited above.
./build/release/replay --market Q "$SESSION" > "data/.${NAME}.replay" 2>&1 || {
  # Show what actually happened. An earlier version grepped for RESULT and
  # unexplained, which match nothing when replay refuses before it starts, so
  # a refusal was reported as a bare "FAILED check 5" with no cause.
  tail -20 "data/.${NAME}.replay" >&2
  echo "FAILED check 5: differential replay" >&2
  exit 5
}
grep -E "^messages compared|^RESULT" "data/.${NAME}.replay" | sed 's/^/  /'

echo "VERIFIED $NAME"
