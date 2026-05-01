#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/scaling
timestamp="$(date +%Y%m%d_%H%M%S)"
repeat_log="logs/scaling/repeat_par_exact_1v8_neighbor_access_${timestamp}.log"
batch_log="logs/scaling/batch_granularity_par_exact_neighbor_access_${timestamp}.log"

SEED="${SEED:-1}"
VERTICES="${VERTICES:-10000}"
UPDATES="${UPDATES:-16384}"
DELTA_CAP="${DELTA_CAP:-32}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"
MAX_GENERATION_ATTEMPTS="${MAX_GENERATION_ATTEMPTS:-5000000}"
REPEAT_COUNT="${REPEAT_COUNT:-5}"
BATCH_VALUES="${BATCH_VALUES:-64 128 256 512 1024}"
THREAD_VALUES="${THREAD_VALUES:-1 2 4 8}"

run_par_exact() {
  local threads="$1"
  local batch_size="$2"

  echo "parlay_threads=${threads}"
  echo "batch_size_config=${batch_size}"
  PARLAY_NUM_THREADS="${threads}" ./build/benchmarks/bench_smoke \
    --engine par_exact \
    --workload conflict_heavy \
    --seed "${SEED}" \
    --vertices "${VERTICES}" \
    --updates "${UPDATES}" \
    --delta-cap "${DELTA_CAP}" \
    --batch-size "${batch_size}" \
    --max-rounds "${MAX_ROUNDS}" \
    --max-generation-attempts "${MAX_GENERATION_ATTEMPTS}" \
    --diagnostics 1 \
    --validate-final-only
  echo
}

echo "===== CONFIGURE / BUILD ====="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target test_par_exact test_par_exact_fuzz bench_smoke

echo "===== TARGETED CTEST ====="
ctest --test-dir build --output-on-failure -R par_exact

{
  echo "===== REPEAT PAR_EXACT 1V8 START ====="
  echo "run_name=repeat_par_exact_1v8_neighbor_access"
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "host=$(hostname)"
  echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  for rep in $(seq 1 "${REPEAT_COUNT}"); do
    echo "repeat=${rep}"
    run_par_exact 1 64
    run_par_exact 8 64
  done
  echo "run_complete=1"
} 2>&1 | tee "${repeat_log}"

{
  echo "===== BATCH GRANULARITY PAR_EXACT START ====="
  echo "run_name=batch_granularity_par_exact_neighbor_access"
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "host=$(hostname)"
  echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  for batch_size in ${BATCH_VALUES}; do
    for threads in ${THREAD_VALUES}; do
      run_par_exact "${threads}" "${batch_size}"
    done
  done
  echo "run_complete=1"
} 2>&1 | tee "${batch_log}"

echo "repeat_log=${repeat_log}"
echo "batch_log=${batch_log}"
