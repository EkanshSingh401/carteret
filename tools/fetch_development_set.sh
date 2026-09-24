#!/usr/bin/env sh
# fetch_development_set.sh -- fetch and verify the seven development sessions.
#
# One session at a time: fetch it, then run its five arrival checks, then the
# next. Sequential on purpose. This archive throttles per client and
# cumulatively -- 11.6 MB/s was measured before a large transfer and 0.3-0.8
# MB/s on FRESH connections after one (docs/data.md) -- so parallel streams
# make it worse rather than better: the limit does not reset with a new
# connection, and more connections are more of what provoked it.
#
# Run it under caffeinate so the machine cannot idle-sleep mid-transfer:
#
#   caffeinate -i -s sh tools/fetch_development_set.sh
#
# Safe to re-run. fetch_chunked.sh resumes from the bytes already on disk, and
# verify_session.sh is idempotent, so an interrupted run continues rather than
# starting over.
#
# THE HELD-OUT SESSIONS ARE NOT IN THIS LIST AND MUST NOT BE ADDED. 2019-10-30
# and 2020-01-30 are not fetched until the sequence in docs/preregistration.md
# section 10 has completed. The guard below refuses them by name, because the
# one thing worse than not having them is having them early.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

HELD_OUT="10302019 01302020"

# Development set, per docs/data.md. 05302019 is first because its venue
# verdict decides whether the registration's session list has to be amended,
# and that amendment must happen before any gated computation rather than
# after five more sessions have been fetched. It is also the one session filed
# under a directory that contradicts its name, so its directory is given
# explicitly -- inferring it from the file name would look in the wrong place.
set -- \
  "05302019.NASDAQ_ITCH50:Nasdaq PSX ITCH" \
  "03272019.NASDAQ_ITCH50:Nasdaq ITCH" \
  "07302019.NASDAQ_ITCH50:Nasdaq ITCH" \
  "08302019.NASDAQ_ITCH50:Nasdaq ITCH" \
  "S101819-v50.txt:Nasdaq ITCH"

failed=""
for entry in "$@"; do
  name=${entry%%:*}
  dir=${entry#*:}

  for h in $HELD_OUT; do
    case "$name" in
      *"$h"*)
        echo "REFUSING $name: it is a held-out session" >&2
        exit 1
        ;;
    esac
  done

  echo "########## $name  $(date '+%Y-%m-%d %H:%M:%S')"

  if [ -f "data/$name" ] && [ -f "data/$name.gz" ]; then
    echo "already present"
  elif ! sh tools/fetch_chunked.sh "$name.gz" "$dir"; then
    echo "FETCH FAILED $name" >&2
    failed="$failed $name"
    continue
  fi

  if ! sh tools/verify_session.sh "$name"; then
    echo "VERIFY FAILED $name" >&2
    failed="$failed $name"
    continue
  fi

  echo "########## DONE $name  $(date '+%Y-%m-%d %H:%M:%S')"
done

echo
if [ -n "$failed" ]; then
  echo "SESSIONS STILL OUTSTANDING:$failed"
  echo "Re-run to continue; nothing is restarted from zero."
  exit 1
fi
echo "ALL REQUESTED SESSIONS FETCHED AND VERIFIED  $(date '+%Y-%m-%d %H:%M:%S')"
