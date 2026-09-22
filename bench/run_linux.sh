#!/usr/bin/env bash
# Runs the full benchmark suite on the x86_64 Linux benchmark host.
#
# This is the only script whose output may be quoted as a latency measurement.
# It refuses to run anywhere else, because a number from the wrong host is
# worse than no number: it looks like a measurement.
#
#   usage: bench/run_linux.sh <session-file> [output-dir]
#
# Writes a docs/benchmarks.md-ready block to <output-dir>/entry.md, with the
# machine check embedded, and the raw run output beside it.
#
# Before running, and recorded in the entry either way:
#   sudo cpupower frequency-set -g performance
#   echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
#   GRUB: isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
#   offline the SMT sibling of the bench core
set -euo pipefail

SESSION="${1:?usage: bench/run_linux.sh <session-file> [output-dir]}"
OUT="${2:-results/$(date -u +%Y%m%dT%H%M%SZ)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="${BENCH_CORE:-2}"
RUNS="${BENCH_RUNS:-5}"
WARMUP="${BENCH_WARMUP:-2000000}"

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "This script runs only on x86_64 Linux. On any other host the harness is" >&2
  echo "a smoke test: build the bench preset and run bench_book directly, and do" >&2
  echo "not record its output in docs/benchmarks.md." >&2
  exit 2
fi

if [ ! -f "$SESSION" ]; then
  echo "no such session file: $SESSION" >&2
  exit 2
fi

mkdir -p "$OUT"

# The machine state is captured first and embedded in the entry. A latency
# figure is only interpretable alongside the host that produced it.
"$ROOT/tools/machine_check.sh" > "$OUT/machine.txt" 2>&1 || true
cat "$OUT/machine.txt"

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
