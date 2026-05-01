#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/comparison

timestamp="$(date +%Y%m%d_%H%M%S)"
log_path="logs/comparison/engine_comparison_${timestamp}.log"

VERTICES="${VERTICES:-1000}"
UPDATES="${UPDATES:-1000}"
BATCH_UPDATES="${BATCH_UPDATES:-1024}"
DELTA_CAP="${DELTA_CAP:-16}"
SEEDS="${SEEDS:-1}"
C_VALUE="${C_VALUE:-4}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"

run_bench() {
  local engine="$1"
  local workload="$2"
  local batch_size="$3"
  local updates="$4"
  local seed="$5"

  echo "===== RUN engine=${engine} workload=${workload} batch_size=${batch_size} seed=${seed} ====="
  echo "command=./build/benchmarks/bench_smoke --engine ${engine} --workload ${workload} --seed ${seed} --vertices ${VERTICES} --updates ${updates} --delta-cap ${DELTA_CAP} --batch-size ${batch_size}"

  local cmd=(
    ./build/benchmarks/bench_smoke
    --engine "${engine}"
    --workload "${workload}"
    --seed "${seed}"
    --vertices "${VERTICES}"
    --updates "${updates}"
    --delta-cap "${DELTA_CAP}"
    --batch-size "${batch_size}"
  )

  if [[ "${engine}" == "par_relaxed" ]]; then
    echo "engine_options=--c ${C_VALUE} --max-rounds ${MAX_ROUNDS}"
    cmd+=(--c "${C_VALUE}" --max-rounds "${MAX_ROUNDS}")
  elif [[ "${engine}" == "par_exact" ]]; then
    echo "engine_options=--max-rounds ${MAX_ROUNDS}"
    cmd+=(--max-rounds "${MAX_ROUNDS}")
  else
    echo "engine_options="
  fi

  "${cmd[@]}"
  echo
}

main() {
  echo "===== ENGINE COMPARISON START ====="
  echo "run_name=engine_comparison"
  echo "run_start=${timestamp}"
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "host=$(hostname)"
  echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "root_dir=${ROOT_DIR}"
  echo "log_path=${log_path}"
  echo

  echo "===== CONFIG ====="
  echo "vertices=${VERTICES}"
  echo "updates=${UPDATES}"
  echo "batch_updates=${BATCH_UPDATES}"
  echo "delta_cap=${DELTA_CAP}"
  echo "seeds=${SEEDS}"
  echo "c_value=${C_VALUE}"
  echo "max_rounds=${MAX_ROUNDS}"
  echo

  echo "===== CONFIGURE / BUILD ====="
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  echo

  echo "===== FULL CTEST ====="
  ctest --test-dir build --output-on-failure
  echo

  echo "===== ENGINE COMPARISON RUNS ====="
  for seed in ${SEEDS}; do
    for engine in seq_baseline seq_exact par_relaxed par_exact; do
      run_bench "${engine}" valid_insertions 1 "${UPDATES}" "${seed}"
      run_bench "${engine}" mixed_valid 1 "${UPDATES}" "${seed}"
      run_bench "${engine}" batch_valid 16 "${BATCH_UPDATES}" "${seed}"
      run_bench "${engine}" conflict_heavy 1 "${UPDATES}" "${seed}"
      run_bench "${engine}" conflict_heavy 16 "${BATCH_UPDATES}" "${seed}"
    done
  done

  echo "===== ENGINE COMPARISON END ====="
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "run_complete=1"
}

main 2>&1 | tee "${log_path}"
exit "${PIPESTATUS[0]}"
