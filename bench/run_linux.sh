#!/usr/bin/env bash
# Runs the Stage 4 benchmark suite on the x86_64 Linux benchmark host.
#
# This is the only script whose output may be quoted as a latency measurement.
# It refuses to run anywhere else, because a number from the wrong host is
# worse than no number: it looks like a measurement.
#
#   usage: bench/run_linux.sh <session-file> [output-dir]
#
# The session must already be local and verified with tools/verify_session.sh
# (docs/data.md), and its SHA-256 must match the digest docs/data.md records.
# The script does not fetch: a benchmark run with a download in flight
# measures the download, so fetching is a separate step that must finish
# first, and this script checks that it has.
#
# Writes one block per experiment to <output-dir>/, each with the machine
# check that preceded it embedded, and a docs/benchmarks.md-ready summary in
# <output-dir>/entry.md.
#
# The machine check runs before every experiment and its exit status is
# binding: the script refuses to run unless the host is clean, and
# BENCH_REQUIRE_CLEAN=0 is the only way past that, which marks the entry
# unpublishable.
#
# Environment:
#   BENCH_CORE           the core the book runs on (default: lowest isolated)
#   BENCH_PRODUCER_CORE  the SPSC producer's core, on the same CCD (default:
#                        BENCH_CORE + 1)
#   BENCH_RUNS           runs per configuration, at least 5 (default 5)
#   BENCH_WARMUP         untimed messages at the start of each run
#   BENCH_ONLY           space-separated experiment labels to run (default all)
#   BENCH_REQUIRE_CLEAN  1 (default) refuses to run on a machine check failure
#   BENCH_REQUIRE_QUIET  1 (default) refuses to run with network traffic
set -euo pipefail

SESSION="${1:?usage: bench/run_linux.sh <session-file> [output-dir]}"
OUT="${2:-results/$(date -u +%Y%m%dT%H%M%SZ)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNS="${BENCH_RUNS:-5}"
WARMUP="${BENCH_WARMUP:-2000000}"

cd "$ROOT"

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "This script runs only on x86_64 Linux. On any other host the harness is" >&2
  echo "a smoke test: build the bench preset and run bench_book directly, and do" >&2
  echo "not record its output in docs/benchmarks.md." >&2
  exit 2
fi
[ "$RUNS" -ge 5 ] || { echo "BENCH_RUNS must be at least 5 (docs/benchmarks.md, method 2)" >&2; exit 2; }
[ -f "$SESSION" ] || { echo "no such session file: $SESSION" >&2; exit 2; }

# The first isolated CPU unless told otherwise. The machine check examines
# this core's whole L3 domain.
ISOLATED=$(cat /sys/devices/system/cpu/isolated 2>/dev/null)
CORE="${BENCH_CORE:-$(echo "$ISOLATED" | cut -d, -f1 | cut -d- -f1)}"
CORE="${CORE:-0}"
PRODUCER="${BENCH_PRODUCER_CORE:-$((CORE + 1))}"
export BENCH_CORE="$CORE"

# GCC 13 or later, named explicitly: the host's default g++ is 11.4, and a
# number from an unrecorded compiler is not reproducible.
if [ -z "${CXX:-}" ]; then
  for c in g++-14 g++-13; do command -v "$c" > /dev/null && { CXX=$c; break; }; done
fi
CXX="${CXX:-g++}"
CC="${CC:-${CXX/g++/gcc}}"
GCC_MAJOR=$("$CXX" -dumpversion | cut -d. -f1)
[ "$GCC_MAJOR" -ge 13 ] || { echo "need GCC 13 or later; $CXX is $("$CXX" -dumpversion)" >&2; exit 2; }
export CC CXX

mkdir -p "$OUT"

# --- 1. the network, which must be quiet --------------------------------------
#
# A download in flight competes for memory bandwidth and interrupts on the
# housekeeping cores, and its page-cache writes evict what the benchmark just
# read. Refused if a fetch is running or the NIC moved more than 64 KB/s over
# five seconds; the measured rate is recorded either way.
net_bytes() { awk 'NR > 2 && $1 != "lo:" {s += $2 + $10} END {print s + 0}' /proc/net/dev; }
n0=$(net_bytes); sleep 5; n1=$(net_bytes)
NET_RATE=$(( (n1 - n0) / 5 ))
FETCHING=$(pgrep -fa 'fetch_chunked|fetch_data|curl .*emi.nasdaq' | grep -v pgrep || true)
echo "network: ${NET_RATE} bytes/s over 5 s${FETCHING:+; fetch in progress: $FETCHING}" | tee "$OUT/network.txt"
if [ -n "$FETCHING" ] || [ "$NET_RATE" -gt 65536 ]; then
  if [ "${BENCH_REQUIRE_QUIET:-1}" = "1" ]; then
    echo "Refusing to run: the network is not quiet. Finish every download first." >&2
    exit 1
  fi
fi

# --- 2. the session, verified against the recorded digest --------------------
#
# census --sha256 checks framing and End of Messages; the digest it prints
# must equal the one docs/data.md recorded when the session was verified, so
# the bytes benchmarked are the bytes the correctness work used.
echo "configuring and building ..."
cmake -S "$ROOT" -B "$ROOT/build/release" -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null
cmake --build "$ROOT/build/release" --target census draw_checksum -j > /dev/null
"$ROOT/build/release/census" --sha256 "$SESSION" > "$OUT/session.txt" 2>&1 || {
  cat "$OUT/session.txt" >&2
  echo "session failed verification; not benchmarking against it" >&2
  exit 1
}
GOT_SHA=$(awk '/sha256/ {print $NF; exit}' "$OUT/session.txt")
WANT_SHA=$(grep -F "\`$(basename "$SESSION")\`" docs/data.md | grep -oE '[0-9a-f]{64}' | head -1 || true)
if [ -z "$WANT_SHA" ] || [ "$GOT_SHA" != "$WANT_SHA" ]; then
  echo "session digest ${GOT_SHA:-<none>} does not match docs/data.md (${WANT_SHA:-<none>})" >&2
  exit 1
fi
echo "session: $(basename "$SESSION"), sha256 matches docs/data.md" | tee -a "$OUT/session.txt"

# --- 3. the floating-point comparison that only this host can make -----------
#
# std::log1p is not required to be correctly rounded, and the development host
# is arm64 with Apple's libm while this one is x86-64 with glibc. See
# docs/design.md record 038. A mismatch does NOT mean regenerate anything: it
# means a libm difference has been found and has to be diagnosed.
EXPECTED_DRAW_CHECKSUM="${EXPECTED_DRAW_CHECKSUM:-6095088912012545764}"
"$ROOT/build/release/draw_checksum" > "$OUT/draws.txt" 2>&1
GOT_DRAW_CHECKSUM=$(awk '/^draw checksum/ {print $3}' "$OUT/draws.txt")
if [ "$GOT_DRAW_CHECKSUM" = "$EXPECTED_DRAW_CHECKSUM" ]; then
  DRAW_STATUS="agrees with the development host ($GOT_DRAW_CHECKSUM)"
else
  DRAW_STATUS="DIFFERS (expected ${EXPECTED_DRAW_CHECKSUM}, got ${GOT_DRAW_CHECKSUM})"
  echo "*** DRAW CHECKSUM DIFFERS; see docs/design.md record 038 ***" >&2
fi
echo "draw checksum: $DRAW_STATUS"

# --- 4. the experiments --------------------------------------------------------
#
# Pinning is taskset alone. chrt needs real-time privilege this account does
# not have, and on an isolated nohz_full core the benchmark is the only
# runnable task, so SCHED_OTHER is not preempted by anything a priority would
# have kept off. The entry records what was used.
PIN="taskset -c $CORE"
if chrt -f 80 true 2> /dev/null; then PIN="$PIN chrt -f 80"; fi

PUBLISHABLE=1
bench_build() {
  local label="$1"; shift
  local dir="$ROOT/build/bench-$label"
  # shellcheck disable=SC2068
  cmake -S "$ROOT" -B "$dir" -DCMAKE_BUILD_TYPE=Release -DCARTERET_BENCH=ON $@ \
    > "$OUT/configure-$label.log" 2>&1
  cmake --build "$dir" -j > "$OUT/build-$label.log" 2>&1
  if grep -q 'warning:' "$OUT/build-$label.log"; then
    echo "build $label produced warnings; see $OUT/build-$label.log" >&2
    exit 1
  fi
  echo "$dir"
}

# The bench core's row of /proc/interrupts, one "name count" pair per line.
irq_row() {
  awk -v col=$((CORE + 2)) 'NR > 1 {n = $1; sub(":", "", n); print n, $col}' /proc/interrupts
}

want() { [ -z "${BENCH_ONLY:-}" ] || echo " $BENCH_ONLY " | grep -q " $1 "; }

# run_experiment LABEL PIN-PREFIX COMMAND...
# The machine check runs first and is embedded; the interrupts delivered to
# the bench core while the command ran are recorded beside the output.
run_experiment() {
  local label="$1" pin="$2"; shift 2
  local f="$OUT/run-$label.txt"
  set +e
  "$ROOT/tools/machine_check.sh" > "$OUT/machine-$label.txt" 2>&1
  local ms=$?
  set -e
  if [ "$ms" -ne 0 ]; then
    PUBLISHABLE=0
    echo "machine check NOT CLEAN before $label (exit $ms)" >&2
    if [ "${BENCH_REQUIRE_CLEAN:-1}" = "1" ]; then
      cat "$OUT/machine-$label.txt" >&2
      echo "Refusing to run. BENCH_REQUIRE_CLEAN=0 takes an unpublishable measurement." >&2
      exit 1
    fi
  fi
  echo "=== $label ==="
  irq_row > "$OUT/irq0-$label.txt"
  # shellcheck disable=SC2086
  $pin "$@" > "$f" 2>&1 || { tail -20 "$f" >&2; echo "experiment $label FAILED; kept in $f" >&2; }
  irq_row > "$OUT/irq1-$label.txt"
  {
    echo
    echo "interrupts delivered to cpu$CORE during this experiment:"
    awk 'NR == FNR {before[$1] = $2; next}
         {d = $2 - before[$1]; if (d > 0) {printf "  %-6s %d\n", $1, d; any = 1}}
         END {if (!any) print "  none"}' "$OUT/irq0-$label.txt" "$OUT/irq1-$label.txt"
  } >> "$f"
  grep -E "RESULT|ns per message|^  all |p99.9 |major" "$f" | sed 's/^/  /' || true
}

COMMON="--runs $RUNS --warmup $WARMUP --verify"

BASE=$(bench_build baseline)
want baseline        && run_experiment baseline        "$PIN" "$BASE/bench_book" $COMMON --hash multiply-shift "$SESSION"
want hash-identity   && run_experiment hash-identity   "$PIN" "$BASE/bench_book" $COMMON --hash identity "$SESSION"
want hash-std        && run_experiment hash-std        "$PIN" "$BASE/bench_book" $COMMON --hash std "$SESSION"
want index-chained   && run_experiment index-chained   "$PIN" "$BASE/bench_book" $COMMON --hash chained "$SESSION"
if want order-32; then
  O32=$(bench_build order-32 -DCARTERET_ORDER_BYTES=32)
  run_experiment order-32 "$PIN" "$O32/bench_book" $COMMON --hash multiply-shift "$SESSION"
fi
for w in 512 1024 2048; do
  if want "window-$w"; then
    WD=$(bench_build "window-$w" -DCARTERET_WINDOW_TICKS=$w)
    run_experiment "window-$w" "$PIN" "$WD/bench_book" $COMMON --hash multiply-shift "$SESSION"
  fi
done
# SPSC needs a second core. It is on the same CCD, so the two share the L3 the
# experiment is about; this is the one configuration where a second core of
# the benchmark CCD is busy, and the entry says so.
want spsc && run_experiment spsc "taskset -c $CORE,$PRODUCER" "$BASE/bench_book" $COMMON \
  --mode spsc --core "$CORE" --producer-core "$PRODUCER" --hash multiply-shift "$SESSION"

# Attribution: per-message fill counts by type, recenter and order age.
for v in multiply-shift identity chained; do
  want "attribute-$v" && run_experiment "attribute-$v" "$PIN" "$BASE/bench_book" \
    --warmup "$WARMUP" --mode attribute --hash "$v" "$SESSION"
done
want attribute-order-32 && [ -n "${O32:-}" ] && run_experiment attribute-order-32 "$PIN" \
  "$O32/bench_book" --warmup "$WARMUP" --mode attribute --hash multiply-shift "$SESSION"

# Structure attribution: perf mem samples loads through IBS, and each sampled
# address is resolved against the exact ranges the book allocated.
if want perf-mem && command -v perf > /dev/null; then
  run_experiment perf-mem "" perf mem record -o "$OUT/perf-mem.data" -- $PIN \
    "$BASE/bench_book" --mode structures --region-map "$OUT/regions.txt" --hash multiply-shift "$SESSION"
  perf script -i "$OUT/perf-mem.data" -F comm,addr,data_src,weight,ip 2> /dev/null \
    | "$ROOT/bench/perf_mem_attribute.py" "$OUT/regions.txt" > "$OUT/perf-mem-attribution.txt" \
    || echo "perf mem attribution FAILED" >> "$OUT/perf-mem-attribution.txt"
  cat "$OUT/perf-mem-attribution.txt"
fi

# perf stat for each index and layout variant, whole run, batch mode.
# LLC-load-misses is not supported on Zen 3 and would print nothing, so the
# last-level miss is the validated DRAM-fill event (record 040).
STAT_EVENTS="cycles,instructions,branch-misses,ls_l1_d_tlb_miss.all,ls_dmnd_fills_from_sys.mem_io_local,ls_dmnd_fills_from_sys.int_cache"
for v in multiply-shift identity std chained; do
  want "perf-stat-$v" && run_experiment "perf-stat-$v" "$PIN" perf stat -e "$STAT_EVENTS" \
    "$BASE/bench_book" --runs 1 --warmup "$WARMUP" --mode batch --hash "$v" "$SESSION"
done
want perf-stat-order-32 && [ -n "${O32:-}" ] && run_experiment perf-stat-order-32 "$PIN" \
  perf stat -e "$STAT_EVENTS" "$O32/bench_book" --runs 1 --warmup "$WARMUP" --mode batch "$SESSION"

# --- 5. the entry ---------------------------------------------------------------
{
  echo "### $(date -u +%Y-%m-%d) — Stage 4, $(basename "$SESSION")"
  echo
  echo "- Session: \`$(basename "$SESSION")\`, SHA-256 matches docs/data.md"
  echo "- Runs: $RUNS per configuration, warmup $WARMUP messages, pinned with \`$PIN\`"
  echo "- Compiler: \`$("$CXX" --version | head -1)\`"
  echo "- Network during setup: $NET_RATE bytes/s"
  echo "- Draw checksum: $DRAW_STATUS"
  if [ "$PUBLISHABLE" -eq 1 ]; then
    echo "- Machine check: **clean** before every experiment. These numbers are publishable."
  else
    echo "- Machine check: **NOT CLEAN** before at least one experiment. These numbers are"
    echo "  **not publishable**; no percentile from them may be quoted."
  fi
  echo
  for f in "$OUT"/run-*.txt; do
    label=$(basename "$f" .txt | sed 's/^run-//')
    echo "#### $label"
    echo
    echo "<details><summary>machine check</summary>"
    echo
    echo '```'
    sed 's/\x1b\[[0-9;]*m//g' "$OUT/machine-$label.txt"
    echo '```'
    echo
    echo "</details>"
    echo
    echo '```'
    cat "$f"
    echo '```'
    echo
  done
} > "$OUT/entry.md"

echo
echo "wrote $OUT/entry.md"
echo "Summarise it into docs/benchmarks.md beneath the predictions recorded before"
echo "the run, and keep every entry, including the experiments that lost."
