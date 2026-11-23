#!/usr/bin/env bash
# Configures, builds, and tests duplicate-file-analyzer from a clean clone:
#   - Debug build + CTest (45 GoogleTest cases)
#   - AddressSanitizer build + CTest
#   - UndefinedBehaviorSanitizer build + CTest
#   - ThreadSanitizer build + CTest (this project has a real worker pool)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found on PATH; bootstrapping a project-local copy via pip (no sudo, no global install)..."
  python3 -m venv "$ROOT_DIR/.venv"
  "$ROOT_DIR/.venv/bin/pip" install --quiet --upgrade pip
  "$ROOT_DIR/.venv/bin/pip" install --quiet cmake ninja
  export PATH="$ROOT_DIR/.venv/bin:$PATH"
fi

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  GENERATOR="Ninja"
fi

build_and_test() {
  local dir="$1"
  shift
  echo "=== Configuring $dir ($*) ==="
  cmake -S . -B "$dir" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Debug "$@"
  echo "=== Building $dir ==="
  cmake --build "$dir"
  echo "=== Testing $dir ==="
  ctest --test-dir "$dir" --output-on-failure
}

build_and_test build
build_and_test build-asan -DENABLE_ASAN=ON
build_and_test build-ubsan -DENABLE_UBSAN=ON
build_and_test build-tsan -DENABLE_TSAN=ON

echo
echo "All builds and tests passed. Try the CLI:"
echo "  ./build/duplicate-file-analyzer . --exclude '*.git/*'"
echo
echo "Run benchmarks (writes CSVs to results/, which is gitignored — see README):"
echo "  ./benchmarks/run_benchmarks.sh"
