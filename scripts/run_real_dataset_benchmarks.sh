#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/real_datasets

timestamp="$(date +%Y%m%d_%H%M%S)"
log_path="logs/real_datasets/real_dataset_benchmarks_${timestamp}.log"

: "${DATASET_NAME:?DATASET_NAME must be set}"
: "${DATASET_PATH:?DATASET_PATH must be set}"

DELTA_CAP="${DELTA_CAP:-64}"
INITIAL_EDGES="${INITIAL_EDGES:-0}"
UPDATES="${UPDATES:-10000}"
BATCH_SIZE="${BATCH_SIZE:-64}"
SEEDS="${SEEDS:-1 2 3}"
C_VALUE="${C_VALUE:-4}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"
ENGINES="${ENGINES:-seq_baseline seq_exact par_relaxed par_exact}"
MODE="${MODE:-insertion_only}"
PREPARED_OUTPUT_DIR="${PREPARED_OUTPUT_DIR:-data/prepared}"
SKIP_PREPARE="${SKIP_PREPARE:-0}"

print_command() {
  printf "command="
  printf "%q " "$@"
  printf "\n"
}

bench_supports_file_stream() {
  local source="benchmarks/bench_smoke.cpp"
  [[ -f "${source}" ]] || return 1
  rg -q "file_stream" "${source}" &&
    rg -q -- "--initial-file" "${source}" &&
    rg -q -- "--updates-file" "${source}"
}

metadata_value() {
  local metadata_path="$1"
  local key="$2"
  if [[ ! -f "${metadata_path}" ]]; then
    echo "error: metadata file does not exist: ${metadata_path}" >&2
    return 1
  fi

  local line
  line="$(rg -m1 "\"${key}\"[[:space:]]*:[[:space:]]*([0-9]+|\"[^\"]*\"|true|false|null)" "${metadata_path}" || true)"
  if [[ -z "${line}" ]]; then
    echo "error: key '${key}' not found in ${metadata_path}" >&2
    return 1
  fi

  line="${line#*:}"
  line="${line#,}"
  line="${line%"${line##*[![:space:]]}"}"
  line="${line#"${line%%[![:space:]]*}"}"
  line="${line%,}"
  if [[ "${line}" == \"*\" ]]; then
    line="${line#\"}"
    line="${line%\"}"
  fi
  echo "${line}"
}

run_prepare() {
  local seed="$1"

  local cmd=(
    python3 scripts/prepare_real_dataset.py
    --input "${DATASET_PATH}"
    --output-dir "${PREPARED_OUTPUT_DIR}"
    --name "${DATASET_NAME}"
    --delta-cap "${DELTA_CAP}"
    --initial-edges "${INITIAL_EDGES}"
    --updates "${UPDATES}"
    --batch-size "${BATCH_SIZE}"
    --seed "${seed}"
    --mode "${MODE}"
  )

  echo "===== PREPARE dataset=${DATASET_NAME} seed=${seed} ====="
  print_command "${cmd[@]}"
  "${cmd[@]}"
  echo
}

run_bench() {
  local engine="$1"
  local seed="$2"
  local prepared_dir="${PREPARED_OUTPUT_DIR}/${DATASET_NAME}/seed_${seed}"
  local metadata_path="${prepared_dir}/metadata.json"
  local initial_file="${prepared_dir}/initial_edges.txt"
  local updates_file="${prepared_dir}/updates.txt"
  local vertices

  vertices="$(metadata_value "${metadata_path}" remapped_num_vertices)"
  if [[ "${vertices}" == "0" ]]; then
    vertices="1"
  fi

  echo "===== RUN dataset=${DATASET_NAME} engine=${engine} seed=${seed} workload=file_stream ====="

  local cmd=(
    ./build/benchmarks/bench_smoke
    --engine "${engine}"
    --workload file_stream
    --seed "${seed}"
    --vertices "${vertices}"
    --updates "${UPDATES}"
    --delta-cap "${DELTA_CAP}"
    --batch-size "${BATCH_SIZE}"
    --initial-file "${initial_file}"
    --updates-file "${updates_file}"
  )

  if [[ "${engine}" == "par_relaxed" ]]; then
    cmd+=(--c "${C_VALUE}" --max-rounds "${MAX_ROUNDS}")
  elif [[ "${engine}" == "par_exact" ]]; then
    cmd+=(--max-rounds "${MAX_ROUNDS}")
  fi

  print_command "${cmd[@]}"
  "${cmd[@]}"
  echo
}

main() {
  echo "===== REAL DATASET BENCHMARKS START ====="
  echo "run_name=real_dataset_benchmarks"
  echo "run_start=${timestamp}"
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "host=$(hostname)"
  echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "root_dir=${ROOT_DIR}"
  echo "log_path=${log_path}"
  echo

  echo "===== CONFIG ====="
  echo "dataset_name=${DATASET_NAME}"
  echo "dataset_path=${DATASET_PATH}"
  echo "delta_cap=${DELTA_CAP}"
  echo "initial_edges=${INITIAL_EDGES}"
  echo "updates=${UPDATES}"
  echo "batch_size=${BATCH_SIZE}"
  echo "seeds=${SEEDS}"
  echo "c_value=${C_VALUE}"
  echo "max_rounds=${MAX_ROUNDS}"
  echo "engines=${ENGINES}"
  echo "mode=${MODE}"
  echo "prepared_output_dir=${PREPARED_OUTPUT_DIR}"
  echo "skip_prepare=${SKIP_PREPARE}"
  echo

  echo "===== CONFIGURE / BUILD ====="
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  echo

  echo "===== FULL CTEST ====="
  ctest --test-dir build --output-on-failure
  echo

  local file_stream_supported=0
  if bench_supports_file_stream; then
    file_stream_supported=1
  fi
  echo "file_stream_supported=${file_stream_supported}"
  if [[ "${file_stream_supported}" == "0" ]]; then
    echo "skip_reason=bench_smoke does not yet advertise --workload file_stream with --initial-file/--updates-file"
    echo "next_step=add file_stream workload support to benchmarks/bench_smoke.cpp"
    echo
  fi

  for seed in ${SEEDS}; do
    if [[ "${SKIP_PREPARE}" == "1" ]]; then
      echo "===== PREPARE SKIPPED dataset=${DATASET_NAME} seed=${seed} ====="
      echo "reason=SKIP_PREPARE=1"
      echo
    else
      run_prepare "${seed}"
    fi
    if [[ "${file_stream_supported}" == "1" ]]; then
      for engine in ${ENGINES}; do
        run_bench "${engine}" "${seed}"
      done
    else
      for engine in ${ENGINES}; do
        echo "===== SKIP dataset=${DATASET_NAME} engine=${engine} seed=${seed} workload=file_stream ====="
        echo "reason=file_stream workload is not implemented in bench_smoke yet"
        echo
      done
    fi
  done

  echo "===== REAL DATASET BENCHMARKS END ====="
  echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
  echo "run_complete=1"
}

main 2>&1 | tee "${log_path}"
exit "${PIPESTATUS[0]}"
