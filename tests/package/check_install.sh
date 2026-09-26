#!/usr/bin/env bash
# Packaging check: build cortex against a system Boost, install it to a
# scratch prefix, then build and run a C++20 consumer that only uses
# find_package(cortex).
#
# Usage: tests/package/check_install.sh [work-dir]
# Extra CMake arguments for the cortex configure step (e.g. CPM_*_SOURCE
# overrides) can be passed via CORTEX_PACKAGE_CMAKE_ARGS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${1:-$ROOT/build/package-install}"

rm -rf "$WORK"
# shellcheck disable=SC2086
cmake -S "$ROOT" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCORTEX_USE_SYSTEM_BOOST=ON \
    -DCORTEX_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX="$WORK/prefix" ${CORTEX_PACKAGE_CMAKE_ARGS:-}
cmake --build "$WORK/build"
cmake --install "$WORK/build"

cmake -S "$ROOT/tests/package/installed" -B "$WORK/consumer" -G Ninja \
    -DCMAKE_PREFIX_PATH="$WORK/prefix"
cmake --build "$WORK/consumer"

output="$("$WORK/consumer/consumer")"
if [ "$output" != "42" ]; then
    echo "FAIL: consumer printed '$output', expected 42" >&2
    exit 1
fi
echo "PASS: installed package works with find_package(cortex)"
