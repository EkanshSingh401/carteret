#!/usr/bin/env bash
# Refuses to let you publish a latency number from a machine that cannot
# produce one.
#
# Run this BEFORE every benchmark run and paste its output at the top of the
# docs/benchmarks.md entry. A number without the machine that produced it is not a
# number, and this script is how you stop yourself forgetting.
#
#   usage: tools/machine_check.sh

if [ "$(uname -s)" = "Darwin" ]; then
  echo "=== macOS detected: $(sysctl -n machdep.cpu.brand_string 2>/dev/null) ($(uname -m)) ==="
  echo
  echo "This machine is fine for ALL correctness work: parser, census, fixtures,"
  echo "reference book, fast book, differential tests, sanitizers, the queue"
  echo "simulator and the study."
  echo
  echo "It CANNOT produce a publishable latency number. macOS has no isolcpus,"
  echo "no hard thread pinning (affinity is only a hint), no perf, and on Apple"
  echo "Silicon no rdtsc -- the generic ARM timer ticks at 24 MHz, ~42 ns per"
  echo "tick, which is coarser than the thing you are trying to measure."
  echo
  echo "Plan: build and verify everything here. Benchmark later on rented x86"
  echo "Linux bare metal, as a half-day, once the book is provably correct."
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
# rdtscp is only usable as a clock if the TSC is invariant. Without these two
# flags the counter changes rate with frequency or stops in deep C-states, and
# every number you take from it is noise.
grep -q constant_tsc /proc/cpuinfo && ok "constant_tsc" || bad "constant_tsc MISSING - rdtsc is not a clock here"
grep -q nonstop_tsc  /proc/cpuinfo && ok "nonstop_tsc"  || bad "nonstop_tsc MISSING - TSC stops in deep C-states"
grep -q rdtscp       /proc/cpuinfo && ok "rdtscp"       || warn "no rdtscp - you need an lfence around rdtsc"

echo
echo "=== isolation ==="
if grep -q isolcpus /proc/cmdline; then ok "isolcpus set: $(sed 's/.*\(isolcpus=[^ ]*\).*/\1/' /proc/cmdline)"
else warn "no isolcpus - the scheduler will put other work on your core"; fi
grep -q nohz_full  /proc/cmdline && ok "nohz_full"  || warn "no nohz_full - timer ticks will show in your tail"
grep -q rcu_nocbs  /proc/cmdline && ok "rcu_nocbs"  || warn "no rcu_nocbs - RCU callbacks will show in your tail"

echo
echo "=== frequency ==="
gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)
[ "$gov" = "performance" ] && ok "governor=performance" || warn "governor=$gov (want performance)"
if [ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
  [ "$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)" = "1" ] \
    && ok "turbo disabled (stability over peak)" \
    || warn "turbo ENABLED - run-to-run variance will swamp small deltas"
fi

echo
echo "=== memory ==="
thp=$(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo unknown)
echo "  THP        $thp"
echo "  hugepages  $(cat /proc/sys/vm/nr_hugepages 2>/dev/null || echo n/a)"

echo
if [ "$fail" -eq 0 ]; then
  echo "Clean. Numbers from this machine are defensible."
else
  echo "NOT clean. You can still run, but say so in docs/benchmarks.md and do not"
  echo "put a p99.9 from this machine on a resume. Fix the WARNs first:"
  echo "  sudo cpupower frequency-set -g performance"
  echo "  echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo"
  echo "  add to GRUB: isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3"
  echo "  offline the HT sibling of your bench core"
fi
exit 0
