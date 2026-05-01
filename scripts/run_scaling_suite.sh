#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/scaling

timestamp="$(date +%Y%m%d_%H%M%S)"
log_path="logs/scaling/scaling_suite_${timestamp}.log"

SMALL_VERTICES="${SMALL_VERTICES:-1000}"
SMALL_UPDATES="${SMALL_UPDATES:-1000}"
SMALL_BATCH_UPDATES="${SMALL_BATCH_UPDATES:-1024}"
SMALL_DELTA_CAP="${SMALL_DELTA_CAP:-16}"
LARGE_VERTICES="${LARGE_VERTICES:-10000}"
LARGE_UPDATES="${LARGE_UPDATES:-10000}"
LARGE_BATCH_UPDATES="${LARGE_BATCH_UPDATES:-16384}"
LARGE_DELTA_CAP="${LARGE_DELTA_CAP:-32}"
SEEDS="${SEEDS:-1 2 3}"
C_VALUES="${C_VALUES:-2 4 8 16}"
THREAD_VALUES="${THREAD_VALUES:-1 2 4 8}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"
RELAXED_C_DEFAULT="${RELAXED_C_DEFAULT:-4}"
BATCH_SIZE_SMALL="${BATCH_SIZE_SMALL:-16}"
BATCH_SIZE_LARGE="${BATCH_SIZE_LARGE:-64}"
MAX_GENERATION_ATTEMPTS="${MAX_GENERATION_ATTEMPTS:-5000000}"
RUN_ENGINE_COMPARISON="${RUN_ENGINE_COMPARISON:-1}"
RUN_C_SCALING="${RUN_C_SCALING:-1}"
RUN_THREAD_SCALING="${RUN_THREAD_SCALING:-1}"

print_command() {
  local parlay_threads="$1"
  shift

  printf "command="
  if [[ -n "${parlay_threads}" ]]; then
    printf "PARLAY_NUM_THREADS=%q " "${parlay_threads}"
  fi
  printf "%q " "$@"
  printf "\n"
}

run_bench() {
  local engine="$1"
  local workload="$2"
  local seed="$3"
  local vertices="$4"
  local updates="$5"
  local delta_cap="$6"
  local batch_size="$7"
  local c_value="${8:-}"
  local max_rounds="${9:-}"
  local parlay_threads="${10:-}"

  echo "===== RUN engine=${engine} workload=${workload} seed=${seed} vertices=${vertices} updates=${updates} delta_cap=${delta_cap} batch_size=${batch_size} ====="
  echo "parlay_threads=${parlay_threads:-default}"

  local cmd=(
    ./build/benchmarks/bench_smoke
    --engine "${engine}"
    --workload "${workload}"
    --seed "${seed}"
    --vertices "${vertices}"
    --updates "${updates}"
    --delta-cap "${delta_cap}"
    --batch-size "${batch_size}"
    --max-generation-attempts "${MAX_GENERATION_ATTEMPTS}"
    --diagnostics 1
  )

  if [[ "${engine}" == "par_relaxed" ]]; then
    if [[ -z "${c_value}" || -z "${max_rounds}" ]]; then
      echo "error=par_relaxed requires c_value and max_rounds" >&2
      return 1
    fi
    cmd+=(--c "${c_value}" --max-rounds "${max_rounds}")
    echo "engine_options=--c ${c_value} --max-rounds ${max_rounds}"
  elif [[ "${engine}" == "par_exact" ]]; then
    if [[ -z "${max_rounds}" ]]; then
      echo "error=par_exact requires max_rounds" >&2
      return 1
    fi
    cmd+=(--max-rounds "${max_rounds}")
    echo "engine_options=--max-rounds ${max_rounds}"
  else
    echo "engine_options="
  fi

  print_command "${parlay_threads}" "${cmd[@]}"
  if [[ -n "${parlay_threads}" ]]; then
    PARLAY_NUM_THREADS="${parlay_threads}" "${cmd[@]}"
  else
    "${cmd[@]}"
  fi
  echo
}

run_engine_comparison_group() {
  echo "===== EXPERIMENT GROUP 1: ENGINE COMPARISON ====="
  echo "group_purpose=report_ready_four_engine_comparison"

  for seed in ${SEEDS}; do
    for engine in seq_baseline seq_exact par_relaxed par_exact; do
      local c_value=""
      local rounds_value=""
      if [[ "${engine}" == "par_relaxed" ]]; then
        c_value="${RELAXED_C_DEFAULT}"
        rounds_value="${MAX_ROUNDS}"
      elif [[ "${engine}" == "par_exact" ]]; then
        rounds_value="${MAX_ROUNDS}"
      fi

      run_bench "${engine}" valid_insertions "${seed}" "${SMALL_VERTICES}" "${SMALL_UPDATES}" "${SMALL_DELTA_CAP}" 1 "${c_value}" "${rounds_value}" ""
      run_bench "${engine}" mixed_valid "${seed}" "${SMALL_VERTICES}" "${SMALL_UPDATES}" "${SMALL_DELTA_CAP}" 1 "${c_value}" "${rounds_value}" ""
      run_bench "${engine}" batch_valid "${seed}" "${SMALL_VERTICES}" "${SMALL_BATCH_UPDATES}" "${SMALL_DELTA_CAP}" "${BATCH_SIZE_SMALL}" "${c_value}" "${rounds_value}" ""
      run_bench "${engine}" conflict_heavy "${seed}" "${SMALL_VERTICES}" "${SMALL_UPDATES}" "${SMALL_DELTA_CAP}" 1 "${c_value}" "${rounds_value}" ""
      run_bench "${engine}" conflict_heavy "${seed}" "${SMALL_VERTICES}" "${SMALL_BATCH_UPDATES}" "${SMALL_DELTA_CAP}" "${BATCH_SIZE_SMALL}" "${c_value}" "${rounds_value}" ""
    done
  done
}

run_c_scaling_group() {
  echo "===== EXPERIMENT GROUP 2: PAR-RELAXED C-SCALING ====="
  echo "group_purpose=measure_palette_multiplier_effects"
  # For conflict_heavy, larger c makes same-color endpoints rarer, so
  # generation_attempts and generator_same_color_attempts are part of the
  # result, not just algorithm runtime.

  for seed in ${SEEDS}; do
    for c_value in ${C_VALUES}; do
      run_bench par_relaxed valid_insertions "${seed}" "${LARGE_VERTICES}" "${LARGE_UPDATES}" "${LARGE_DELTA_CAP}" 1 "${c_value}" "${MAX_ROUNDS}" ""
      run_bench par_relaxed batch_valid "${seed}" "${LARGE_VERTICES}" "${LARGE_BATCH_UPDATES}" "${LARGE_DELTA_CAP}" "${BATCH_SIZE_LARGE}" "${c_value}" "${MAX_ROUNDS}" ""
      run_bench par_relaxed conflict_heavy "${seed}" "${LARGE_VERTICES}" "${LARGE_UPDATES}" "${LARGE_DELTA_CAP}" 1 "${c_value}" "${MAX_ROUNDS}" ""
      run_bench par_relaxed conflict_heavy "${seed}" "${LARGE_VERTICES}" "${LARGE_BATCH_UPDATES}" "${LARGE_DELTA_CAP}" "${BATCH_SIZE_LARGE}" "${c_value}" "${MAX_ROUNDS}" ""
    done
  done
}

run_thread_scaling_group() {
  echo "===== EXPERIMENT GROUP 3: PARALLEL THREAD SCALING ====="
  echo "group_purpose=measure_parlay_thread_count_effects"
  echo "expected_caveat=if sequential_fast_path_count is close to repair rounds or calls, speedup may be limited because active repair sets are small"

  for seed in ${SEEDS}; do
    for threads in ${THREAD_VALUES}; do
      for engine in par_relaxed par_exact; do
        local c_value=""
        if [[ "${engine}" == "par_relaxed" ]]; then
          c_value="${RELAXED_C_DEFAULT}"
        fi

        run_bench "${engine}" batch_valid "${seed}" "${LARGE_VERTICES}" "${LARGE_BATCH_UPDATES}" "${LARGE_DELTA_CAP}" "${BATCH_SIZE_LARGE}" "${c_value}" "${MAX_ROUNDS}" "${threads}"
        run_bench "${engine}" conflict_heavy "${seed}" "${LARGE_VERTICES}" "${LARGE_BATCH_UPDATES}" "${LARGE_DELTA_CAP}" "${BATCH_SIZE_LARGE}" "${c_value}" "${MAX_ROUNDS}" "${threads}"
      done
    done
  done
}

main() {
  echo "===== SCALING SUITE START ====="
  echo "run_name=scaling_suite"
  echo "run_start=${timestamp}"
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "host=$(hostname)"
  echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "root_dir=${ROOT_DIR}"
  echo "log_path=${log_path}"
  echo

  echo "===== CONFIG ====="
  echo "small_vertices=${SMALL_VERTICES}"
  echo "small_updates=${SMALL_UPDATES}"
  echo "small_batch_updates=${SMALL_BATCH_UPDATES}"
  echo "small_delta_cap=${SMALL_DELTA_CAP}"
  echo "large_vertices=${LARGE_VERTICES}"
  echo "large_updates=${LARGE_UPDATES}"
  echo "large_batch_updates=${LARGE_BATCH_UPDATES}"
  echo "large_delta_cap=${LARGE_DELTA_CAP}"
  echo "seeds=${SEEDS}"
  echo "c_values=${C_VALUES}"
  echo "thread_values=${THREAD_VALUES}"
  echo "max_rounds=${MAX_ROUNDS}"
  echo "relaxed_c_default=${RELAXED_C_DEFAULT}"
  echo "batch_size_small=${BATCH_SIZE_SMALL}"
  echo "batch_size_large=${BATCH_SIZE_LARGE}"
  echo "max_generation_attempts=${MAX_GENERATION_ATTEMPTS}"
  echo "run_engine_comparison=${RUN_ENGINE_COMPARISON}"
  echo "run_c_scaling=${RUN_C_SCALING}"
  echo "run_thread_scaling=${RUN_THREAD_SCALING}"
  echo

  echo "===== CONFIGURE / BUILD ====="
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  echo

  echo "===== FULL CTEST ====="
  ctest --test-dir build --output-on-failure
  echo

  if [[ "${RUN_ENGINE_COMPARISON}" == "1" ]]; then
    run_engine_comparison_group
  else
    echo "===== EXPERIMENT GROUP 1: ENGINE COMPARISON SKIPPED ====="
  fi

  if [[ "${RUN_C_SCALING}" == "1" ]]; then
    run_c_scaling_group
  else
    echo "===== EXPERIMENT GROUP 2: PAR-RELAXED C-SCALING SKIPPED ====="
  fi

  if [[ "${RUN_THREAD_SCALING}" == "1" ]]; then
    run_thread_scaling_group
  else
    echo "===== EXPERIMENT GROUP 3: PARALLEL THREAD SCALING SKIPPED ====="
  fi

  echo "===== SCALING SUITE END ====="
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "run_complete=1"
}

main 2>&1 | tee "${log_path}"
exit "${PIPESTATUS[0]}"
