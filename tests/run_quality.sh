#!/usr/bin/env bash
# Builds and runs the GPU-free test suite (see tests/CMakeLists.txt). Needs a C++20 compiler and glm; no GPU.
#   ./tests/run_quality.sh [build-dir]
# Pass -DSAKUGA_GLM_INCLUDE_DIR=/path/to/glm through CMAKE_FLAGS if glm is not installed system-wide.
set -euo pipefail
cd "$(dirname "$0")/.."
build="${1:-build-tests}"

echo "=== Sakuga: GPU-free test suite ==="
cmake -S tests -B "$build" ${CMAKE_FLAGS:-}
cmake --build "$build" -j "$(nproc 2>/dev/null || echo 2)"
ctest --test-dir "$build" --output-on-failure
