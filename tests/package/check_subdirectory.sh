#!/usr/bin/env bash
# Packaging check: a C++20 project that pulls cortex in with add_subdirectory()
# (what FetchContent/CPM do) must configure, build and run, and must not
# inherit cortex's tests, apps or GoogleTest.
#
# Usage: tests/package/check_subdirectory.sh [build-dir]
# Extra CMake arguments (e.g. CPM_*_SOURCE overrides) can be passed through
# the CORTEX_PACKAGE_CMAKE_ARGS environment variable.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build/package-subdir}"

rm -rf "$BUILD"
# shellcheck disable=SC2086
cmake -S "$ROOT/tests/package/subdir" -B "$BUILD" -G Ninja \
    -DCORTEX_SOURCE_DIR="$ROOT" ${CORTEX_PACKAGE_CMAKE_ARGS:-}
cmake --build "$BUILD" --target consumer

targets="$(cmake --build "$BUILD" --target help)"
for leaked in video_editor gtest cortex_tiny_fiber_test; do
    if grep -q "$leaked" <<<"$targets"; then
        echo "FAIL: consumer build contains cortex-internal target '$leaked'" >&2
        exit 1
    fi
done

output="$("$BUILD/consumer")"
if [ "$output" != "42" ]; then
    echo "FAIL: consumer printed '$output', expected 42" >&2
    exit 1
fi
echo "PASS: add_subdirectory consumer builds and runs"
