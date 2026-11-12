#!/bin/sh
# Build, test and measure CarloBomb on this machine, and write one report.
#
#     sh benchmarks/measure_machine.sh [output-file]
#
# Meant for a machine other than the one the recorded results came from: a
# different processor family (AMD Zen, for instance), or one that exposes
# hardware performance counters.  Linux x86-64, natively or under WSL2.
# Needs cmake and a C compiler; nothing else.  Takes a few minutes.
#
# The report contains: the processor and its instruction sets, the test
# results, which kernel the "auto" engine selects for each node budget, and
# the three benchmarks.  Where the kernel allows perf_event_open on hardware
# events (bare-metal Linux usually does, with
# /proc/sys/kernel/perf_event_paranoid at 2 or lower; most virtual machines
# and WSL2 do not), the characterization also reports cycles, instructions,
# branches and branch misses per search step.
set -u
cd "$(dirname "$0")/.." || exit 1
out=${1:-machine-report-$(uname -n)-$(date +%Y%m%d-%H%M%S).txt}
build=build-measure

{
    echo "CarloBomb machine report"
    echo "date:      $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "revision:  $(git describe --always --dirty 2>/dev/null || echo unknown)"
    echo "kernel:    $(uname -srm)"
    grep -m1 'model name' /proc/cpuinfo 2>/dev/null
    grep -m1 'cpu family' /proc/cpuinfo 2>/dev/null
    grep -m1 '^model[^ ]' /proc/cpuinfo 2>/dev/null
    echo "flags of interest: $(grep -m1 '^flags' /proc/cpuinfo 2>/dev/null | tr ' ' '\n' |
        grep -E '^(bmi1|bmi2|avx2|avx512f|avx512bw|avx512vl|hypervisor)$' | tr '\n' ' ')"
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo n/a)"
    echo "compiler:  $(${CC:-cc} --version 2>/dev/null | head -1)"
    echo

    echo "== build"
    if cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release >"$build.log" 2>&1 &&
        cmake --build "$build" --parallel >>"$build.log" 2>&1; then
        echo "build: ok ($(grep -c 'warning' "$build.log") warnings)"
    else
        echo "build: FAILED"
        tail -30 "$build.log"
    fi
    echo

    echo "== tests"
    ctest --test-dir "$build" 2>&1 | tail -15
    echo

    if [ -x "$build/carlobomb" ] && "$build/carlobomb" engines >/dev/null 2>&1; then
        echo "== engines and instruction-set selection"
        "$build/carlobomb" engines
        echo
    fi

    if [ -x "$build/characterize" ]; then
        echo "== hardware counter plumbing (software events)"
        "$build/characterize" --perf-selftest
        echo
        echo "== workload characterization (completed vs cut off, short vs long; counters if available)"
        "$build/characterize" -r 11
        echo
        echo "== kernels by node budget and target length"
        "$build/repr_comparison"
        echo
        echo "== C vs reference assembly kernel, legacy workloads"
        "$build/asm_comparison"
        echo
        echo "== C vs auto, legacy workloads"
        "$build/asm_comparison" -k auto
    else
        echo "The assembly engines were not built on this machine (not Linux x86-64?)."
        echo "== C baseline"
        "$build/baseline"
    fi
} > "$out" 2>&1

echo "report written to $out"
