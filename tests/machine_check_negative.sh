#!/usr/bin/env bash
# machine_check_negative.sh -- proves the machine check can fail.
#
# tools/machine_check.sh decides whether a latency number may be published,
# and on the benchmark host every one of its checks passes. A check seen only
# to pass is not known to check anything, so this builds a synthetic sysfs and
# procfs tree shaped like the benchmark host -- a 5950X with the second CCD
# isolated, boost off, SMT off -- and requires it to pass, then breaks one
# condition at a time and requires each break to fail.
#
# The hardware probe cannot be synthesised by a tree, so its output is supplied
# as a file; the clock-ratio case below is the probe's contribution.
#
#   usage: tests/machine_check_negative.sh <path/to/machine_check.sh>
set -u

CHECK="${1:?usage: $0 <machine_check.sh>}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
failures=0

build_tree() {
  rm -rf "$WORK/t"
  local T="$WORK/t" c
  mkdir -p "$T/proc/sys/kernel" "$T/proc/sys/vm" "$T/sys/devices/system/cpu/smt" \
           "$T/sys/kernel/mm/transparent_hugepage"
  cat > "$T/proc/cpuinfo" <<'EOF'
vendor_id	: AuthenticAMD
model name	: AMD Ryzen 9 5950X 16-Core Processor
microcode	: 0xa201009
flags		: fpu tsc msr constant_tsc rdtscp nonstop_tsc hw_pstate
EOF
  echo "BOOT_IMAGE=/boot/vmlinuz ro isolcpus=8-15 nohz_full=8-15 rcu_nocbs=8-15" \
    > "$T/proc/cmdline"
  echo "           CPU0" > "$T/proc/interrupts"
  echo 1 > "$T/proc/sys/kernel/perf_event_paranoid"
  echo 0 > "$T/proc/sys/vm/nr_hugepages"
  echo "always [madvise] never" > "$T/sys/kernel/mm/transparent_hugepage/enabled"
  echo 8-15 > "$T/sys/devices/system/cpu/isolated"
  echo 8-15 > "$T/sys/devices/system/cpu/nohz_full"
  echo notsupported > "$T/sys/devices/system/cpu/smt/control"
  for c in $(seq 0 15); do
    local d="$T/sys/devices/system/cpu/cpu$c"
    mkdir -p "$d/cache/index3" "$d/topology" "$d/cpufreq"
    echo 3 > "$d/cache/index3/level"
    if [ "$c" -lt 8 ]; then echo 0-7 > "$d/cache/index3/shared_cpu_list"
    else echo 8-15 > "$d/cache/index3/shared_cpu_list"; fi
    echo "$c" > "$d/topology/thread_siblings_list"
    echo performance > "$d/cpufreq/scaling_governor"
    echo acpi-cpufreq > "$d/cpufreq/scaling_driver"
    echo 0 > "$d/cpufreq/cpb"
  done
  cat > "$WORK/probe" <<'EOF'
cpuid_cpb 0
lfence_always_serializing 1
rdpmc_user 1
tsc_mhz 3400.0
core_mhz 3400.0
clock_ratio 1.0000
EOF
}

run_check() {
  BENCH_CORE=8 MACHINE_CHECK_SYS="$WORK/t/sys" MACHINE_CHECK_PROC="$WORK/t/proc" \
    MACHINE_CHECK_PROBE="$WORK/probe" bash "$CHECK" > "$WORK/out" 2>&1
}

# expect LABEL EXIT PATTERN: the check exits EXIT and its output matches PATTERN.
expect() {
  run_check
  local got=$?
  if [ "$got" -ne "$2" ] || ! grep -qE "$3" "$WORK/out"; then
    echo "FAIL  $1: expected exit $2 and /$3/, got exit $got"
    sed 's/^/      /' "$WORK/out"
    failures=$((failures + 1))
  else
    echo "ok    $1 (exit $got)"
  fi
}

# Positive control first. If the clean tree fails, every negative below is
# meaningless: a check that rejects everything is not a gate either.
build_tree
expect "the benchmark host's shape passes" 0 "^Clean\."

# AMD boost, through each switch that can enable it.
build_tree
mkdir -p "$WORK/t/sys/devices/system/cpu/cpufreq"
echo 1 > "$WORK/t/sys/devices/system/cpu/cpufreq/boost"
expect "global cpufreq boost on fails" 1 "BAD.*AMD boost enabled.*cpufreq/boost=1"

build_tree; echo 1 > "$WORK/t/sys/devices/system/cpu/cpu12/cpufreq/cpb"
expect "legacy cpb on for a domain core fails" 1 "BAD.*AMD boost enabled.*cpu12/cpb=1"

build_tree
for c in $(seq 0 15); do rm "$WORK/t/sys/devices/system/cpu/cpu$c/cpufreq/cpb"; done
echo 1 > "$WORK/t/sys/devices/system/cpu/cpu8/cpufreq/boost"
mkdir -p "$WORK/t/sys/devices/system/cpu/amd_pstate"
echo active > "$WORK/t/sys/devices/system/cpu/amd_pstate/status"
expect "amd-pstate per-policy boost on fails" 1 "BAD.*AMD boost enabled.*cpu8/boost=1"

build_tree
for c in $(seq 0 15); do rm "$WORK/t/sys/devices/system/cpu/cpu$c/cpufreq/cpb"; done
sed -i.bak 's/hw_pstate/hw_pstate cpb/' "$WORK/t/proc/cpuinfo"
expect "CPB advertised with no control fails" 1 "BAD.*CPB advertised"

build_tree; sed -i.bak 's/^core_mhz.*/core_mhz 4950.0/; s/^clock_ratio.*/clock_ratio 1.4559/' "$WORK/probe"
expect "a core measured above TSC rate fails" 1 "BAD.*the core is boosting"

# SMT.
build_tree; echo on > "$WORK/t/sys/devices/system/cpu/smt/control"
echo 8,24 > "$WORK/t/sys/devices/system/cpu/cpu8/topology/thread_siblings_list"
expect "SMT on fails" 1 "BAD.*SMT is on"

# Isolation of the whole CCD, not only the bench core.
build_tree
echo 8-11 > "$WORK/t/sys/devices/system/cpu/isolated"
expect "isolating half the CCD fails" 1 "BAD.*isolcpus \(8-11\) does not cover L3 domain 8-15"

build_tree
sed -i.bak 's/rcu_nocbs=8-15/rcu_nocbs=8-14/' "$WORK/t/proc/cmdline"
expect "rcu_nocbs missing one domain core fails" 1 "BAD.*rcu_nocbs \(8-14\) does not cover"

build_tree
mkdir -p "$WORK/t/proc/4242/task/4242"
# Field 39 of stat is the CPU last run on; 13 is inside the domain, and ppid 1
# makes it a user task rather than a per-CPU kernel thread.
echo "4242 (stress) R 1 $(seq -s ' ' 5 38 | sed 's/[0-9]*/0/g') 13 0 0" \
  > "$WORK/t/proc/4242/task/4242/stat"
echo stress > "$WORK/t/proc/4242/task/4242/comm"
expect "a user task on the CCD fails" 1 "BAD.*user tasks on the L3 domain: stress/cpu13"

# lfence serialization unproven.
build_tree; sed -i.bak 's/^lfence_always_serializing.*/lfence_always_serializing 0/' "$WORK/probe"
expect "lfence serialization unproven is not clean" 1 "WARN.*DE_CFG"

if [ "$failures" -ne 0 ]; then
  echo "$failures case(s) failed"
  exit 1
fi
echo "all cases behaved"
