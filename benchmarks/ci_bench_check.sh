#!/usr/bin/env bash
# Benchmark regression check.
#
# Builds and runs the micro-benchmarks for the working tree and, when a
# baseline checkout is given, for the baseline on the same machine, then
# fails if any benchmark regressed beyond the noise tolerance.
#
# Usage: benchmarks/ci_bench_check.sh [path-to-baseline-checkout]
#
# Environment:
#   BENCH_MAX_REGRESSION  Failure threshold as a ratio (default 1.30,
#                         i.e. fail when a benchmark got >30% slower).

set -euo pipefail

cd "$(dirname "$0")/.."

BASE_DIR="${1:-}"
TOLERANCE="${BENCH_MAX_REGRESSION:-1.30}"
CPM_CACHE="${CPM_SOURCE_CACHE:-$PWD/build/cpm-cache}"

build_and_run() { # <source-dir> <build-dir> <output-csv>
    cmake -S "$1" -B "$2" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCORTEX_BUILD_TESTS=OFF \
        -DCORTEX_BUILD_BENCHMARKS=ON \
        -DCPM_SOURCE_CACHE="$CPM_CACHE" >/dev/null
    cmake --build "$2" --target cortex_bench -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" >/dev/null
    "$2/benchmarks/cortex_bench" --csv | tee "$3"
}

echo "=== Benchmarking working tree ==="
build_and_run . build/bench-head build/bench-head.csv

if [ -n "$BASE_DIR" ] && [ -f "$BASE_DIR/benchmarks/CMakeLists.txt" ]; then
    echo "=== Benchmarking baseline ($BASE_DIR) ==="
    build_and_run "$BASE_DIR" build/bench-base build/bench-base.csv

    echo "=== Comparing (tolerance ${TOLERANCE}x) ==="
    python3 benchmarks/compare.py build/bench-base.csv build/bench-head.csv \
        --max-regression "$TOLERANCE"
elif [ -n "$BASE_DIR" ]; then
    echo "Baseline checkout has no benchmarks yet; reporting head numbers only."
else
    echo "No baseline given; reporting head numbers only."
fi
