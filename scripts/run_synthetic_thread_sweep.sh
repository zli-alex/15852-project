#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/scaling
timestamp="$(date +%Y%m%d_%H%M%S)"
log_path="logs/scaling/synthetic_thread_sweep_${timestamp}.log"

main() {
run_date="$(date +"%Y-%m-%dT%H:%M:%S%z")"
host_name="$(hostname)"
git_commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

echo "run_name=synthetic_thread_sweep"
echo "run_start=${timestamp}"
echo "date=${run_date}"
echo "host=${host_name}"
echo "git_commit=${git_commit}"
echo "root_dir=${ROOT_DIR}"
echo "log_path=${log_path}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

# Defaults follow the Benchmark Workloads scaling plan. Override these
# environment variables for quick smoke checks, e.g. VERTICES=1000 UPDATES=1000.
VERTICES="${VERTICES:-10000}"
UPDATES="${UPDATES:-10000}"
DELTA_CAP="${DELTA_CAP:-32}"
BATCH_SIZE="${BATCH_SIZE:-16}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"
PALETTE_MULTIPLIER="${PALETTE_MULTIPLIER:-4}"
THREAD_VALUES="${THREAD_VALUES:-1 2 4 8 16}"
SEED="${SEED:-1}"

# Thread scaling may be masked if sequential_repair_rounds / repair_rounds is
# near 1, because tiny repair sets take the sequential fast path by design.
for threads in ${THREAD_VALUES}; do
  export PARLAY_NUM_THREADS="${threads}"
  echo "parlay_threads=${PARLAY_NUM_THREADS}"
  echo "command=bench_smoke threads=${threads} seed=${SEED}"
  ./build/benchmarks/bench_smoke \
    --engine par_relaxed \
    --workload batch_valid \
    --seed "${SEED}" \
    --vertices "${VERTICES}" \
    --updates "${UPDATES}" \
    --delta-cap "${DELTA_CAP}" \
    --batch-size "${BATCH_SIZE}" \
    --c "${PALETTE_MULTIPLIER}" \
    --max-rounds "${MAX_ROUNDS}" \
    --diagnostics 1
done

echo "run_complete=1"
}

main 2>&1 | tee -a "${log_path}"
exit "${PIPESTATUS[0]}"
