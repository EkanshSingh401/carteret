#!/usr/bin/env bash
# Reports whether the host can produce a defensible latency measurement.
#
# Run before every benchmark run; its output is embedded at the top of the
# corresponding docs/benchmarks.md entry, because a latency figure is only
# interpretable alongside the machine state that produced it.
#
# Exit status, so a script can act on it rather than a human reading prose:
#   0  clean: measurements from this host are interpretable
#   1  x86_64 Linux, but the isolation or timing conditions are not met
#   2  not a benchmark host at all
#
#   usage: tools/machine_check.sh
#
# Environment:
#   BENCH_CORE           the core the benchmark will run on (default: the
#                        lowest isolated CPU). Its whole L3 domain is checked,
#                        because on Zen 3 the L3 belongs to the CCD and a
#                        neighbour on the same CCD evicts the benchmark's lines.
#   MACHINE_CHECK_SYS    root of the sysfs tree to read (default /sys)
#   MACHINE_CHECK_PROC   root of the procfs tree to read (default /proc)
#   MACHINE_CHECK_PROBE  a file of probe output to use instead of compiling
#                        and running the hardware probe
#
# The three MACHINE_CHECK_ variables exist so that tests/machine_check_negative.sh
# can drive every verdict below from a synthetic tree. A check that has only
# ever been seen to pass is not known to check anything.

SYS="${MACHINE_CHECK_SYS:-/sys}"
PROC="${MACHINE_CHECK_PROC:-/proc}"
SYNTHETIC=0
[ -n "${MACHINE_CHECK_SYS:-}${MACHINE_CHECK_PROC:-}" ] && SYNTHETIC=1
CPU="$SYS/devices/system/cpu"

if [ "$SYNTHETIC" -eq 0 ] && [ "$(uname -s)" = "Darwin" ]; then
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
  exit 2
fi

fail=0
ok()   { printf '  \033[32mOK\033[0m    %s\n' "$1"; }
warn() { printf '  \033[33mWARN\033[0m  %s\n' "$1"; fail=1; }
bad()  { printf '  \033[31mBAD\033[0m   %s\n' "$1"; fail=1; }

# Expands a kernel CPU list ("8-15", "0,2,8-11") to one number per line.
expand_cpus() {
  echo "$1" | tr ',' '\n' | while IFS=- read -r lo hi; do
    [ -n "$lo" ] || continue
    seq "$lo" "${hi:-$lo}"
  done
}

# True if every CPU in list $1 appears in list $2.
covers() {
  local want have
  want=$(expand_cpus "$1")
  have=$(expand_cpus "$2")
  for c in $want; do
    echo "$have" | grep -qx "$c" || return 1
  done
  return 0
}

readf() { cat "$1" 2>/dev/null; }

cmdline=$(readf "$PROC/cmdline")
cmdarg() { echo "$cmdline" | tr ' ' '\n' | sed -n "s/^$1=//p" | tail -1; }
vendor=$(grep -m1 vendor_id "$PROC/cpuinfo" | cut -d: -f2- | tr -d ' ')
flags=$(grep -m1 '^flags' "$PROC/cpuinfo" | cut -d: -f2-)
has_flag() { echo " $flags " | grep -q " $1 "; }

isolated=$(readf "$CPU/isolated")
core="${BENCH_CORE:-$(expand_cpus "$isolated" | head -1)}"
core="${core:-0}"

# The bench core's L3 domain, read from the cache hierarchy rather than
# assumed: index3 is conventional but not guaranteed to be the L3.
domain=""
for idx in "$CPU/cpu$core"/cache/index*; do
  [ "$(readf "$idx/level")" = "3" ] && domain=$(readf "$idx/shared_cpu_list")
done
# Membership as a lookup, because the task and interrupt scans below test it
# once per thread and per irq, and a subshell per test took twelve seconds.
declare -A indomain=()
for c in $(expand_cpus "$domain"); do indomain[$c]=1; done

# --- the hardware probe ------------------------------------------------------
#
# Three things sysfs cannot answer, read from the CPU itself on the bench core:
#
#   cpuid_cpb     CPUID 8000_0007 EDX[9]. Firmware clears it when Core
#                 Performance Boost is disabled in the BIOS.
#   lfence        CPUID 8000_0021 EAX[2], LFENCE always dispatch-serializing.
#                 The timer's `lfence; rdtsc` fence means nothing if lfence
#                 does not hold dispatch. On AMD parts without this bit it
#                 depends on DE_CFG[1], which the kernel sets at boot but which
#                 is not readable without root.
#   clock ratio   core cycles against TSC ticks over a quarter-second spin.
#                 The TSC runs at the nominal P0 frequency, so a ratio above 1
#                 is boost actually happening, whatever the knobs say.
#   rdpmc         whether a self-monitoring event grants user-mode rdpmc, which
#                 the per-message miss attribution depends on.
probe_out=""
if [ -n "${MACHINE_CHECK_PROBE:-}" ]; then
  probe_out=$(readf "$MACHINE_CHECK_PROBE")
elif [ "$SYNTHETIC" -eq 0 ] && [ "$(uname -m)" = "x86_64" ]; then
  pdir=$(mktemp -d)
  trap 'rm -rf "$pdir"' EXIT
  cat > "$pdir/probe.c" <<'EOF'
#define _GNU_SOURCE
#include <cpuid.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
int main(int argc, char** argv) {
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(atoi(argv[1]), &s);
    if (sched_setaffinity(0, sizeof s, &s) != 0) return 1;
    unsigned a, b, c, d, maxext;
    __cpuid(0x80000000, maxext, b, c, d);
    __cpuid(0x80000007, a, b, c, d);
    printf("cpuid_cpb %u\n", (d >> 9) & 1);
    if (maxext >= 0x80000021) {
        __cpuid(0x80000021, a, b, c, d);
        printf("lfence_always_serializing %u\n", (a >> 2) & 1);
    } else {
        printf("lfence_always_serializing unknown\n");
    }
    struct perf_event_attr pa;
    memset(&pa, 0, sizeof pa);
    pa.size = sizeof pa;
    pa.type = PERF_TYPE_HARDWARE;
    pa.config = PERF_COUNT_HW_CPU_CYCLES;
    pa.exclude_kernel = 1;
    pa.exclude_hv = 1;
    int fd = (int)syscall(SYS_perf_event_open, &pa, 0, -1, -1, 0);
    if (fd < 0) {
        printf("clock_ratio unknown\nrdpmc_user unknown\n");
        return 0;
    }
    struct perf_event_mmap_page* pg = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    printf("rdpmc_user %d\n", pg != MAP_FAILED && pg->cap_user_rdpmc ? 1 : 0);
    uint64_t c0 = 0, c1 = 0;
    const double w0 = now();
    const uint64_t t0 = __rdtsc();
    if (read(fd, &c0, 8) != 8) return 0;
    volatile uint64_t sink = 0;
    while (now() - w0 < 0.25)
        for (int i = 0; i < 1000; ++i) sink += (uint64_t)i;
    if (read(fd, &c1, 8) != 8) return 0;
    const uint64_t t1 = __rdtsc();
    const double w1 = now();
    printf("tsc_mhz %.1f\ncore_mhz %.1f\nclock_ratio %.4f\n",
           (double)(t1 - t0) / (w1 - w0) / 1e6, (double)(c1 - c0) / (w1 - w0) / 1e6,
           (double)(c1 - c0) / (double)(t1 - t0));
    return 0;
}
EOF
  if "${CC:-cc}" -O1 -o "$pdir/probe" "$pdir/probe.c" 2> "$pdir/cc.log"; then
    probe_out=$("$pdir/probe" "$core" 2>/dev/null)
  fi
fi
probe() { echo "$probe_out" | awk -v k="$1" '$1 == k {print $2}'; }

echo "=== machine ==="
echo "  cpu        $(grep -m1 'model name' "$PROC/cpuinfo" | cut -d: -f2- | sed 's/^ *//')"
echo "  microcode  $(grep -m1 microcode "$PROC/cpuinfo" | cut -d: -f2- | sed 's/^ *//')"
echo "  kernel     $(uname -r)"
echo "  compiler   $(${CXX:-g++} --version 2>/dev/null | head -1)"
echo "  cmdline    $cmdline"
echo "  bench core $core"
echo "  L3 domain  ${domain:-unknown}"

echo
echo "=== timing ==="
# rdtscp is usable as a clock only if the TSC is invariant. Without both flags
# the counter changes rate with core frequency or stops in deep C-states, which
# makes every derived interval meaningless.
has_flag constant_tsc && ok "constant_tsc" || bad "constant_tsc missing; rdtsc is not a clock here"
has_flag nonstop_tsc  && ok "nonstop_tsc"  || bad "nonstop_tsc missing; the TSC stops in deep C-states"
has_flag rdtscp       && ok "rdtscp"       || warn "no rdtscp; rdtsc requires a surrounding lfence"
lf=$(probe lfence_always_serializing)
if [ "$vendor" = "GenuineIntel" ]; then
  ok "lfence serializing (architectural on Intel)"
elif [ "$lf" = "1" ]; then
  ok "lfence always dispatch-serializing (CPUID 8000_0021 EAX[2])"
elif [ "$lf" = "0" ]; then
  warn "lfence serialization rests on DE_CFG[1], set by the kernel and unreadable without root"
else
  warn "lfence serialization not verified (probe unavailable)"
fi

echo
echo "=== isolation ==="
# The whole L3 domain of the bench core, not only the core. The order
# structures are compared against ONE CCD's 32 MB, and a task anywhere on that
# CCD competes for it.
nohz=$(readf "$CPU/nohz_full")
nocbs=$(cmdarg rcu_nocbs)
if [ -z "$domain" ]; then
  bad "cannot read the L3 domain of cpu$core"
elif ! covers "$core" "$isolated"; then
  bad "bench core $core is not isolated (isolated: ${isolated:-none})"
else
  covers "$domain" "$isolated" && ok "isolcpus covers the whole L3 domain $domain" \
    || bad "isolcpus (${isolated:-none}) does not cover L3 domain $domain"
  covers "$domain" "$nohz" && ok "nohz_full covers $domain" \
    || bad "nohz_full (${nohz:-none}) does not cover $domain; timer ticks will appear in the tail"
  covers "$domain" "$nocbs" && ok "rcu_nocbs covers $domain" \
    || bad "rcu_nocbs (${nocbs:-none}) does not cover $domain; RCU callbacks will appear in the tail"
fi

# User tasks last run on the domain. Per-CPU kernel threads (children of
# kthreadd, pid 2) are bound there by construction and are not counted.
intruders=""
for stat in "$PROC"/[0-9]*/task/[0-9]*/stat; do
  [ -r "$stat" ] || continue
  s=$(readf "$stat") || continue
  rest="${s##*) }"
  # shellcheck disable=SC2086
  set -- $rest
  ppid=$2
  last=${37:-}
  [ "$ppid" = "2" ] || [ "$ppid" = "0" ] && continue
  [ -n "$last" ] && [ -n "${indomain[$last]:-}" ] \
    && intruders="$intruders $(readf "${stat%/stat}/comm")/cpu$last"
done
[ -z "$intruders" ] && ok "no user task has run on the L3 domain" \
  || bad "user tasks on the L3 domain:$(echo "$intruders" | cut -c1-200)"

# Device interrupts routed onto the domain. Managed per-queue interrupts
# (NVMe) are placed on every CPU and isolcpus does not move them; they fire only
# for I/O submitted from that CPU. Routed-but-silent is reported and passes;
# any delivered count fails. bench/run_linux.sh records the bench core's
# interrupt deltas across every run, which is the binding evidence.
routed=""; delivered=0
for irq in "$PROC"/irq/[0-9]*; do
  aff=$(readf "$irq/effective_affinity_list")
  [ -n "$aff" ] || continue
  for c in $(expand_cpus "$aff"); do
    if [ -n "${indomain[$c]:-}" ]; then
      n=$(basename "$irq")
      routed="$routed $n"
      col=$((c + 2))
      cnt=$(awk -v n="$n:" -v col="$col" '$1 == n {print $col}' "$PROC/interrupts" 2>/dev/null)
      delivered=$((delivered + ${cnt:-0}))
      break
    fi
  done
done
if [ -z "$routed" ]; then ok "no device interrupt routed to the L3 domain"
elif [ "$delivered" -eq 0 ]; then ok "device irqs routed to the domain but silent (0 delivered):$routed"
else bad "device irqs delivered $delivered interrupts on the domain:$routed"; fi

echo
echo "=== SMT ==="
# A sibling thread shares the core's L1, L2 and execution ports; its presence
# changes every number and its absence has to be shown, not assumed.
smt=$(readf "$CPU/smt/control")
case "$smt" in
  off|forceoff|notsupported|notimplemented) ok "SMT control: $smt" ;;
  on) bad "SMT is on; a sibling shares the bench core's L1, L2 and ports" ;;
  *) warn "SMT control unreadable (${smt:-absent})" ;;
esac
sib=$(readf "$CPU/cpu$core/topology/thread_siblings_list")
[ "$sib" = "$core" ] && ok "bench core $core has no sibling thread" \
  || bad "bench core $core shares a core with: ${sib:-unknown}"

echo
echo "=== frequency ==="
for c in $(expand_cpus "${domain:-$core}"); do
  gov=$(readf "$CPU/cpu$c/cpufreq/scaling_governor"); gov=${gov:-unknown}
  [ "$gov" = "performance" ] || { warn "cpu$c governor=$gov (want performance)"; govbad=1; }
done
[ -z "${govbad:-}" ] && ok "governor=performance on ${domain:-cpu$core}"
echo "  driver     $(readf "$CPU/cpu$core/cpufreq/scaling_driver")"
if [ -r "$CPU/intel_pstate/no_turbo" ]; then
  [ "$(readf "$CPU/intel_pstate/no_turbo")" = "1" ] \
    && ok "turbo disabled (stability over peak)" \
    || bad "turbo enabled; run-to-run variance will swamp small deltas"
fi
if [ "$vendor" = "AuthenticAMD" ]; then
  # Boost on AMD has several independent switches, and any one reading 1 is
  # boost available to the core. cpuinfo_max_freq is NOT one of them: under
  # acpi-cpufreq it reports the CPPC highest-perf capability (5.08 GHz on the
  # benchmark host) whether boost is enabled or not.
  pst=$(readf "$CPU/amd_pstate/status")
  [ -n "$pst" ] && echo "  amd_pstate $pst"
  boost_on=""; boost_knob=0
  if [ -r "$CPU/cpufreq/boost" ]; then
    boost_knob=1
    [ "$(readf "$CPU/cpufreq/boost")" = "1" ] && boost_on="$boost_on cpufreq/boost=1"
  fi
  for c in $(expand_cpus "${domain:-$core}"); do
    for knob in boost cpb; do
      f="$CPU/cpu$c/cpufreq/$knob"
      [ -r "$f" ] || continue
      boost_knob=1
      [ "$(readf "$f")" = "1" ] && boost_on="$boost_on cpu$c/$knob=1"
    done
  done
  if [ -n "$boost_on" ]; then
    bad "AMD boost enabled:$(echo "$boost_on" | cut -c1-160)"
  elif [ "$boost_knob" -eq 1 ]; then
    ok "AMD boost disabled (every cpufreq boost/cpb control reads 0)"
  fi
  if has_flag cpb; then
    [ "$boost_knob" -eq 0 ] && bad "CPB advertised by firmware and no boost control found"
  else
    ok "CPB not advertised: Core Performance Boost is off in firmware"
  fi
  cb=$(probe cpuid_cpb)
  [ "$cb" = "1" ] && ! has_flag cpb && warn "CPUID reports CPB but the kernel does not"
fi
ratio=$(probe clock_ratio)
if [ -n "$ratio" ] && [ "$ratio" != "unknown" ]; then
  verdict=$(awk -v r="$ratio" 'BEGIN { print (r > 1.02) ? "high" : (r < 0.95) ? "low" : "ok" }')
  line="core $(probe core_mhz) MHz against TSC $(probe tsc_mhz) MHz on cpu$core (ratio $ratio)"
  case "$verdict" in
    ok) ok "$line" ;;
    high) bad "$line: the core is boosting" ;;
    low) warn "$line: the core is running below nominal" ;;
  esac
else
  warn "effective clock not measured (probe unavailable)"
fi

echo
echo "=== counters ==="
par=$(readf "$PROC/sys/kernel/perf_event_paranoid")
echo "  perf_event_paranoid ${par:-unknown}"
case "$(probe rdpmc_user)" in
  1) ok "user-mode rdpmc granted on a self-monitoring event" ;;
  0) warn "user-mode rdpmc refused; per-message miss attribution is unavailable" ;;
  *) warn "user-mode rdpmc not verified (probe unavailable)" ;;
esac

echo
echo "=== memory ==="
thp=$(readf "$SYS/kernel/mm/transparent_hugepage/enabled"); thp=${thp:-unknown}
echo "  THP        $thp"
echo "  hugepages  $(readf "$PROC/sys/vm/nr_hugepages" || echo n/a)"

echo
if [ "$SYNTHETIC" -eq 0 ] && [ "$(uname -m)" != "x86_64" ]; then
  echo "Not x86_64: there is no invariant TSC to read, so no measurement from"
  echo "this host is publishable."
  exit 2
fi

if [ "$fail" -eq 0 ]; then
  echo "Clean. Measurements from this host are interpretable."
  exit 0
else
  echo "Not clean. A run is still possible, but the entry in docs/benchmarks.md"
  echo "must record the failing conditions and no tail percentile from this run"
  echo "is publishable. Remediation:"
  echo "  sudo cpupower frequency-set -g performance"
  echo "  AMD: disable Core Performance Boost and PBO in firmware, or"
  echo "       echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost"
  echo "  Intel: echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo"
  echo "  GRUB: isolate the bench core's whole L3 domain, e.g. on a 5950X"
  echo "        isolcpus=8-15 nohz_full=8-15 rcu_nocbs=8-15"
  echo "  disable SMT in firmware, or offline the bench core's sibling"
  exit 1
fi
