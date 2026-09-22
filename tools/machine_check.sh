#!/usr/bin/env bash
# Reports whether the host can produce a defensible latency measurement.
#
# Run before every benchmark run; its output is embedded at the top of the
# corresponding docs/benchmarks.md entry, because a latency figure is only
# interpretable alongside the machine state that produced it.
#
#   usage: tools/machine_check.sh

if [ "$(uname -s)" = "Darwin" ]; then
  echo "=== macOS: $(sysctl -n machdep.cpu.brand_string 2>/dev/null) ($(uname -m)) ==="
  echo
  echo "Suitable for all correctness work: parser, census, fixtures, reference"
  echo "book, fast book, differential replay, sanitizers, queue simulator and"
  echo "the study."
  echo
  echo "Not suitable for published latency. macOS offers no isolcpus, no hard"
  echo "thread pinning (affinity is advisory), and no perf. On Apple Silicon"
  echo "there is no rdtsc; the generic ARM timer ticks at 24 MHz, about 42 ns"
  echo "per tick, which is coarser than a book update."
  echo
  echo "Latency figures come from the x86_64 Linux benchmark host only."
  exit 0
fi

fail=0
ok()   { printf '  \033[32mOK\033[0m    %s\n' "$1"; }
warn() { printf '  \033[33mWARN\033[0m  %s\n' "$1"; fail=1; }
bad()  { printf '  \033[31mBAD\033[0m   %s\n' "$1"; fail=1; }

echo "=== machine ==="
echo "  cpu        $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
echo "  microcode  $(grep -m1 microcode /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//')"
echo "  kernel     $(uname -r)"
echo "  compiler   $(${CXX:-g++} --version | head -1)"
echo "  cmdline    $(cat /proc/cmdline)"

echo
echo "=== timing ==="
# rdtscp is usable as a clock only if the TSC is invariant. Without both flags
# the counter changes rate with core frequency or stops in deep C-states, which
# makes every derived interval meaningless.
grep -q constant_tsc /proc/cpuinfo && ok "constant_tsc" || bad "constant_tsc missing; rdtsc is not a clock here"
grep -q nonstop_tsc  /proc/cpuinfo && ok "nonstop_tsc"  || bad "nonstop_tsc missing; the TSC stops in deep C-states"
grep -q rdtscp       /proc/cpuinfo && ok "rdtscp"       || warn "no rdtscp; rdtsc requires a surrounding lfence"

echo
echo "=== isolation ==="
if grep -q isolcpus /proc/cmdline; then ok "isolcpus set: $(sed 's/.*\(isolcpus=[^ ]*\).*/\1/' /proc/cmdline)"
else warn "no isolcpus; the scheduler will place other work on the bench core"; fi
grep -q nohz_full  /proc/cmdline && ok "nohz_full"  || warn "no nohz_full; timer ticks will appear in the tail"
grep -q rcu_nocbs  /proc/cmdline && ok "rcu_nocbs"  || warn "no rcu_nocbs; RCU callbacks will appear in the tail"

echo
echo "=== frequency ==="
gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)
[ "$gov" = "performance" ] && ok "governor=performance" || warn "governor=$gov (want performance)"
if [ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
  [ "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)" = "1" ] \
    && ok "turbo disabled (stability over peak)" \
    || warn "turbo enabled; run-to-run variance will swamp small deltas"
fi

echo
echo "=== memory ==="
thp=$(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo unknown)
echo "  THP        $thp"
echo "  hugepages  $(cat /proc/sys/vm/nr_hugepages 2>/dev/null || echo n/a)"

echo
if [ "$fail" -eq 0 ]; then
  echo "Clean. Measurements from this host are interpretable."
else
  echo "Not clean. A run is still possible, but the entry in docs/benchmarks.md"
  echo "must record the failing conditions and no tail percentile from this run"
  echo "is publishable. Remediation:"
  echo "  sudo cpupower frequency-set -g performance"
  echo "  echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo"
  echo "  add to GRUB: isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3"
  echo "  offline the SMT sibling of the bench core"
fi
exit 0
