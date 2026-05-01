#!/usr/bin/env bash
set -euo pipefail

mkdir -p logs

MAX_GENERATION_ATTEMPTS="${MAX_GENERATION_ATTEMPTS:-100000}"
VERTICES="${VERTICES:-10000}"
UPDATES="${UPDATES:-10000}"
DELTA_CAP="${DELTA_CAP:-32}"
SEED="${SEED:-1}"
BATCH_SIZE="${BATCH_SIZE:-1}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"
THREADS="${THREADS:-1}"
C_VALUES="${C_VALUES:-2 4 8 16}"

RUN_ID="$(date +%Y%m%d_%H%M%S)"
LOG="logs/conflict_heavy_c_sweep_${RUN_ID}.log"

{
  echo "===== CONFLICT_HEAVY C-SWEEP START ====="
  date
  echo "run_id=${RUN_ID}"
  echo "root_dir=$(pwd)"
  echo "log_path=${LOG}"
  echo

  echo "===== CONFIG ====="
  echo "vertices=${VERTICES}"
  echo "updates=${UPDATES}"
  echo "delta_cap=${DELTA_CAP}"
  echo "seed=${SEED}"
  echo "batch_size=${BATCH_SIZE}"
  echo "max_rounds=${MAX_ROUNDS}"
  echo "parlay_threads=${THREADS}"
  echo "c_values=${C_VALUES}"
  echo

  echo "===== GIT / HOST INFO ====="
  git rev-parse --short HEAD 2>/dev/null | sed 's/^/git_commit=/'
  hostname | sed 's/^/host=/'
  echo

  echo "===== CONFIGURE / BUILD ====="
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target bench_smoke
  echo

  echo "===== FULL CTEST ====="
  cmake --build build
  ctest --test-dir build --output-on-failure
  echo

  echo "===== CONFLICT_HEAVY C-SWEEP ====="
  for c in ${C_VALUES}; do
    echo "----- c=${c} -----"
    PARLAY_NUM_THREADS="${THREADS}" ./build/benchmarks/bench_smoke \
      --engine par_relaxed \
      --workload conflict_heavy \
      --seed "${SEED}" \
      --vertices "${VERTICES}" \
      --updates "${UPDATES}" \
      --delta-cap "${DELTA_CAP}" \
      --batch-size "${BATCH_SIZE}" \
      --c "${c}" \
      --max-rounds "${MAX_ROUNDS}" \
      --diagnostics 1 \
      --max-generation-attempts "${MAX_GENERATION_ATTEMPTS}"
    echo
  done

  echo "===== CONFLICT_HEAVY C-SWEEP END ====="
  date
} 2>&1 | tee "${LOG}"

echo "Log written to: ${LOG}"
