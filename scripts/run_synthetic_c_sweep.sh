#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/scaling
timestamp="$(date +%Y%m%d_%H%M%S)"
log_path="logs/scaling/synthetic_c_sweep_${timestamp}.log"

main() {
run_date="$(date +"%Y-%m-%dT%H:%M:%S%z")"
host_name="$(hostname)"
git_commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

echo "run_name=synthetic_c_sweep"
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
C_VALUES="${C_VALUES:-2 4 8 16}"
# Run three deterministic seeds by default; set SEEDS=1 if runtime is too high.
SEEDS="${SEEDS:-1 2 3}"

export PARLAY_NUM_THREADS=1
echo "parlay_threads=${PARLAY_NUM_THREADS}"

for seed in ${SEEDS}; do
  for c in ${C_VALUES}; do
    echo "command=bench_smoke c=${c} seed=${seed}"
    ./build/benchmarks/bench_smoke \
      --engine par_relaxed \
      --workload batch_valid \
      --seed "${seed}" \
      --vertices "${VERTICES}" \
      --updates "${UPDATES}" \
      --delta-cap "${DELTA_CAP}" \
      --batch-size "${BATCH_SIZE}" \
      --c "${c}" \
      --max-rounds "${MAX_ROUNDS}" \
      --diagnostics 1
  done
done

echo "run_complete=1"
}

main 2>&1 | tee -a "${log_path}"
exit "${PIPESTATUS[0]}"
