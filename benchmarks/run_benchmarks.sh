#!/usr/bin/env bash
# Benchmarks duplicate-file-analyzer with/without the partial-hash
# pre-filter and with/without the SQLite cache. Generates a synthetic
# file tree (not committed — regenerated each run) and writes raw CSV +
# metadata to results/ (gitignored; see README for why the numbers in
# the README are a snapshot of a real run, not re-derivable from a
# committed CSV).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Benchmarked against the Release build (-O3/-DNDEBUG), not the Debug
# build used for day-to-day development and the sanitizer runs: an
# unoptimized SHA-256 inner loop would make every scenario I/O-bound and
# hide the differences this benchmark exists to show.
BIN="$ROOT_DIR/build-release/duplicate-file-analyzer"
RESULTS_DIR="$ROOT_DIR/results"
DATASET_DIR="$(mktemp -d)"
trap 'rm -rf "$DATASET_DIR"' EXIT

if [ ! -x "$BIN" ]; then
  echo "Build the Release binary first:" >&2
  echo "  cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF && cmake --build build-release" >&2
  exit 1
fi

NPROC="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

mkdir -p "$RESULTS_DIR"
CSV="$RESULTS_DIR/benchmark.csv"
META="$RESULTS_DIR/metadata.txt"

echo "Generating synthetic dataset in $DATASET_DIR ..."
# 200 "large" files of 2 MiB each: 40 distinct contents, each duplicated
# 5x, so every file has 4 same-size+same-partial-hash duplicates forcing
# real full-hash work either way, but letting the partial-hash prefilter
# avoid full-hashing any *non*-duplicate same-size file (there are none
# here deliberately, so this dataset isolates the cache's effect; a
# second dataset below adds same-size non-duplicates to isolate the
# partial hash's effect).
python3 - "$DATASET_DIR" <<'PYEOF'
import os, sys, random
root = sys.argv[1]
random.seed(42)
dup_dir = os.path.join(root, "duplicates")
os.makedirs(dup_dir, exist_ok=True)
for content_id in range(40):
    block = bytes([content_id % 256]) * (2 * 1024 * 1024)
    for copy in range(5):
        with open(os.path.join(dup_dir, f"c{content_id}_copy{copy}.bin"), "wb") as f:
            f.write(block)

# Dataset 2: same-size, NON-duplicate files (to show the partial hash's
# benefit: it rules these out after reading only the first+last 64 KiB
# instead of a full 2 MiB read each).
nodup_dir = os.path.join(root, "same_size_unique")
os.makedirs(nodup_dir, exist_ok=True)
for i in range(60):
    # Same size (2 MiB), unique content throughout (not just a differing
    # header) so a full hash is genuinely required to tell them apart,
    # but they differ within the first 64 KiB so the partial hash alone
    # already disproves a match without reading the rest of the file.
    data = bytes([(i * 37 + j) % 256 for j in range(2 * 1024 * 1024)])
    with open(os.path.join(nodup_dir, f"u{i}.bin"), "wb") as f:
        f.write(data)
PYEOF

TOTAL_SIZE=$(du -sh "$DATASET_DIR" | cut -f1)
echo "Dataset size: $TOTAL_SIZE"

run_timed() {
  local label="$1"; shift
  local start end elapsed
  start=$(date +%s.%N)
  "$BIN" "$DATASET_DIR" "$@" --format json > /dev/null
  end=$(date +%s.%N)
  elapsed=$(echo "$end - $start" | bc)
  echo "$label,$elapsed"
}

# Warm the OS page cache first so every scenario below pays the same (low)
# disk-I/O cost and the numbers isolate CPU-side differences (hashing work,
# thread spawn/join overhead, cache lookups) instead of disk variance.
find "$DATASET_DIR" -type f -exec cat {} + > /dev/null

echo "label,seconds" > "$CSV"
echo "Running: full pipeline (partial hash + full hash, no cache), jobs=1..."
run_timed "with_partial_hash_no_cache_jobs1" --jobs 1 >> "$CSV"
echo "Running: full pipeline (partial hash + full hash, no cache), jobs=$NPROC..."
run_timed "with_partial_hash_no_cache_jobsN" --jobs "$NPROC" >> "$CSV"
echo "Running: no partial hash (straight to full hash, no cache), jobs=$NPROC..."
run_timed "without_partial_hash_no_cache_jobsN" --jobs "$NPROC" --no-partial-hash >> "$CSV"

rm -f "$RESULTS_DIR/bench_cache.sqlite"
echo "Running: with cache, cold (first pass populates it), jobs=$NPROC..."
run_timed "with_cache_cold_jobsN" --jobs "$NPROC" --cache "$RESULTS_DIR/bench_cache.sqlite" >> "$CSV"
echo "Running: with cache, warm (second pass, same dataset, cache hits), jobs=$NPROC..."
run_timed "with_cache_warm_jobsN" --jobs "$NPROC" --cache "$RESULTS_DIR/bench_cache.sqlite" >> "$CSV"

{
  echo "Date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "Machine: $(uname -mv)"
  echo "CPU: $(sysctl -n machdep.cpu.brand_string 2>/dev/null || grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2)"
  echo "Logical CPUs (NPROC used for jobsN scenarios): $NPROC"
  echo "Compiler: $(c++ --version | head -1)"
  echo "Build type: Release (-O3 -DNDEBUG, build-release/ ; sanitizer builds in build-asan/build-ubsan/build-tsan are Debug and are not used for timing)"
  echo "Dataset: 200 duplicate files across 40 content groups (2 MiB each) + 60 same-size unique files (2 MiB each), total $TOTAL_SIZE"
  echo "Page cache: warmed by reading every file once before timing (see find|cat above)"
} > "$META"

echo
echo "Results written to $CSV"
cat "$CSV"
echo
echo "Metadata written to $META"
