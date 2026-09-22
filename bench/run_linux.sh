#!/usr/bin/env bash
# Runs the full benchmark suite on the x86_64 Linux benchmark host.
#
# This is the only script whose output may be quoted as a latency measurement.
# It refuses to run anywhere else, because a number from the wrong host is
# worse than no number: it looks like a measurement.
#
#   usage: bench/run_linux.sh <session-file-or-name> [output-dir]
#
# The argument may be a path to an unpacked session, or the name of one in the
# NASDAQ archive -- the benchmark host is not the development host and will not
# already have the data, so the script fetches and verifies it through the same
# path everything else uses. A benchmark against a truncated session would be a
# measurement of a shorter file rather than an obviously wrong number.
#
# Writes a docs/benchmarks.md-ready block to <output-dir>/entry.md, with the
# machine check and the session verification embedded, and the raw run output
# beside it.
#
# The machine check runs first and its exit status is binding: the script
# refuses to run at all unless the host is clean, and BENCH_REQUIRE_CLEAN=0 is
# the only way past that, which marks the entry unpublishable.
#
# Before running, and recorded in the entry either way:
#   sudo cpupower frequency-set -g performance
#   echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
#   GRUB: isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
#   offline the SMT sibling of the bench core
set -euo pipefail

SESSION_ARG="${1:?usage: bench/run_linux.sh <session-file-or-name> [output-dir]}"
OUT="${2:-results/$(date -u +%Y%m%dT%H%M%SZ)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${BENCH_CORE:-2}"
RUNS="${BENCH_RUNS:-5}"
WARMUP="${BENCH_WARMUP:-2000000}"

cd "$ROOT"

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "This script runs only on x86_64 Linux. On any other host the harness is" >&2
  echo "a smoke test: build the bench preset and run bench_book directly, and do" >&2
  echo "not record its output in docs/benchmarks.md." >&2
  exit 2
fi

mkdir -p "$OUT"

# --- 1. the machine, first, and its verdict is binding ---------------------
#
# A latency figure is only interpretable alongside the host that produced it,
# so the machine state is captured before anything else and embedded in the
# entry. The check's exit status decides whether this run may be labelled
# publishable: 0 clean, 1 x86_64 Linux but not isolated, 2 not a benchmark
# host.
set +e
"$ROOT/tools/machine_check.sh" > "$OUT/machine.txt" 2>&1
MACHINE_STATUS=$?
set -e
cat "$OUT/machine.txt"
echo
if [ "$MACHINE_STATUS" -eq 0 ]; then
  PUBLISHABLE=1
  echo "machine check: CLEAN. Results from this run may be published."
else
  PUBLISHABLE=0
  echo "machine check: NOT CLEAN (exit ${MACHINE_STATUS})." >&2
  echo "Any result would be marked unpublishable and no percentile from it" >&2
  echo "could be quoted." >&2
  if [ "${BENCH_REQUIRE_CLEAN:-1}" = "1" ]; then
    echo >&2
    echo "Refusing to run. Fix the conditions above, or set BENCH_REQUIRE_CLEAN=0" >&2
    echo "to take an explicitly unpublishable measurement." >&2
    exit 1
  fi
fi
echo

# --- 2. the data, fetched and verified by the same path as everything else --
#
# The benchmark host is not the development host, so it will not already have
# the session. Fetching through tools/fetch_data.sh gives it the same length
# check, gzip check and End of Messages check as any other use. Benchmarking a
# truncated session would produce a measurement of a shorter file rather than
# an obviously wrong number, which is much harder to notice.
if [ -f "$SESSION_ARG" ]; then
  SESSION="$SESSION_ARG"
else
  echo "session not present locally; fetching ${SESSION_ARG} ..."
  "$ROOT/tools/fetch_data.sh" "$SESSION_ARG"
  SESSION="data/${SESSION_ARG%.gz}"
fi

if [ ! -f "$SESSION" ]; then
  echo "no such session file: $SESSION" >&2
  exit 2
fi

echo "verifying $SESSION ..."
cmake -S "$ROOT" -B "$ROOT/build/release" -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null
cmake --build "$ROOT/build/release" --target census -j > /dev/null
if ! "$ROOT/build/release/census" --sha256 "$SESSION" > "$OUT/session.txt" 2>&1; then
  cat "$OUT/session.txt" >&2
  echo "session failed verification; not benchmarking against it" >&2
  exit 1
fi
grep -E "file|bytes|sha256|messages|final message|complete session" "$OUT/session.txt"
echo

# Pinning is a hint on some systems and a guarantee on none, so the entry
# records what was asked for rather than asserting it was honoured.
PIN=""
if command -v taskset > /dev/null; then PIN="taskset -c $CORE"; fi
if command -v chrt > /dev/null; then PIN="$PIN chrt -f 80"; fi

build_and_run() {
  local label="$1"; shift
  local cmake_args="$1"; shift
  local bin_dir="$ROOT/build/bench-$label"

  # shellcheck disable=SC2086
  cmake -S "$ROOT" -B "$bin_dir" -DCMAKE_BUILD_TYPE=Release -DCARTERET_BENCH=ON $cmake_args \
    > "$OUT/configure-$label.log" 2>&1
  cmake --build "$bin_dir" -j > "$OUT/build-$label.log" 2>&1

  echo "=== $label ==="
  # shellcheck disable=SC2086
  $PIN "$bin_dir/bench_book" --runs "$RUNS" --warmup "$WARMUP" "$@" "$SESSION" \
    | tee "$OUT/run-$label.txt"
  echo
}

# The experiment matrix. Each row is one variable against a fixed baseline, so
# a difference is attributable; a matrix that varied two things at once would
# produce numbers nobody can interpret.
build_and_run "baseline"        ""                                --hash multiply-shift
build_and_run "hash-identity"   ""                                --hash identity
build_and_run "hash-std"        ""                                --hash std
build_and_run "order-32"        "-DCARTERET_ORDER_BYTES=32"       --hash multiply-shift
build_and_run "window-1024"     "-DCARTERET_WINDOW_TICKS=1024"    --hash multiply-shift
build_and_run "window-2048"     "-DCARTERET_WINDOW_TICKS=2048"    --hash multiply-shift

# perf counters for the baseline, per message. Reported alongside wall time so
# a change can be attributed to cycles, instructions or misses rather than
# guessed at.
if command -v perf > /dev/null; then
  echo "=== perf stat, baseline ==="
  # shellcheck disable=SC2086
  $PIN perf stat -e cycles,instructions,LLC-load-misses,branch-misses,dTLB-load-misses \
    "$ROOT/build/bench-baseline/bench_book" --runs 1 --warmup "$WARMUP" --mode batch \
    "$SESSION" > "$OUT/perf-baseline.txt" 2>&1 || true
  cat "$OUT/perf-baseline.txt"
else
  echo "perf not available; the entry records that the counters are missing" \
    > "$OUT/perf-baseline.txt"
fi

{
  echo "### $(date -u +%Y-%m-%d) — <one-line description of what changed>"
  echo
  echo "- Prediction, recorded before this run: *(fill in from the record that"
  echo "  proposed the change; if none was recorded, say so)*"
  echo "- Session: \`$(basename "$SESSION")\`"
  echo "- Runs: $RUNS, warmup $WARMUP messages, pinned with: \`$PIN\`"
  if [ "$PUBLISHABLE" -eq 1 ]; then
    echo "- Machine check: **clean**. These numbers are publishable."
  else
    echo "- Machine check: **NOT CLEAN** (exit $MACHINE_STATUS). These numbers are"
    echo "  **not publishable**; no percentile from them may be quoted."
  fi
  echo
  echo "<details><summary>session</summary>"
  echo
  echo '```'
  cat "$OUT/session.txt"
  echo '```'
  echo
  echo "</details>"
  echo "- Compiler: \`$(${CXX:-g++} --version | head -1)\`"
  echo
  echo '<details><summary>machine</summary>'
  echo
  echo '```'
  cat "$OUT/machine.txt"
  echo '```'
  echo
  echo '</details>'
  echo
  for f in "$OUT"/run-*.txt; do
    echo "#### $(basename "$f" .txt | sed 's/^run-//')"
    echo
    echo '```'
    cat "$f"
    echo '```'
    echo
  done
  echo "#### perf stat"
  echo
  echo '```'
  cat "$OUT/perf-baseline.txt"
  echo '```'
  echo
  echo "**Interpretation.** *(Lead with anything that contradicted the"
  echo "prediction. State what the numbers do not show.)*"
} > "$OUT/entry.md"

echo
echo "wrote $OUT/entry.md"
echo "Paste it into docs/benchmarks.md, fill in the prediction and the"
echo "interpretation, and keep the entry even if the change made things worse."
