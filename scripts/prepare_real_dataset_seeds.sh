#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

: "${DATASET_NAME:?DATASET_NAME must be set}"
: "${DATASET_PATH:?DATASET_PATH must be set}"

DELTA_CAP="${DELTA_CAP:-64}"
INITIAL_EDGES="${INITIAL_EDGES:-0}"
UPDATES="${UPDATES:-10000}"
BATCH_SIZE="${BATCH_SIZE:-64}"
SEEDS="${SEEDS:-1 2 3}"
MODE="${MODE:-insertion_only}"
PREPARED_OUTPUT_DIR="${PREPARED_OUTPUT_DIR:-data/prepared}"

print_command() {
  printf "command="
  printf "%q " "$@"
  printf "\n"
}

echo "===== PREPARE REAL DATASET (NO BUILD) ====="
echo "dataset_name=${DATASET_NAME}"
echo "dataset_path=${DATASET_PATH}"
echo "seeds=${SEEDS}"
echo "prepared_output_dir=${PREPARED_OUTPUT_DIR}"
echo

for seed in ${SEEDS}; do
  cmd=(
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
done

echo "===== PREPARE COMPLETE ====="
