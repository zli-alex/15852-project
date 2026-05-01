#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/real_datasets
timestamp="$(date +%Y%m%d_%H%M%S)"
run_log="logs/real_datasets/real_batch_probe_${timestamp}.log"
manifest_path="logs/real_datasets/real_batch_probe_manifest_${timestamp}.txt"
detailed_csv="logs/real_datasets/real_batch_probe_detailed_${timestamp}.csv"
summary_csv="logs/real_datasets/real_batch_probe_summary_${timestamp}.csv"

DATASETS="${DATASETS:-ca-GrQc ca-CondMat ca-AstroPh}"
SEEDS="${SEEDS:-1 2 3}"
BATCH_SIZES="${BATCH_SIZES:-512 1024 4096}"
THREAD_VALUES="${THREAD_VALUES:-8}"
PREPARED_OUTPUT_DIR="${PREPARED_OUTPUT_DIR:-data/prepared}"
DELTA_CAP="${DELTA_CAP:-64}"
C_VALUE="${C_VALUE:-4}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"
RUN_BUILD="${RUN_BUILD:-0}"
RUN_CTEST="${RUN_CTEST:-0}"
VALIDATE_FINAL_ONLY="${VALIDATE_FINAL_ONLY:-1}"

print_command() {
  printf "command="
  printf "%q " "$@"
  printf "\n"
}

metadata_value() {
  local metadata_path="$1"
  local key="$2"
  python3 - "${metadata_path}" "${key}" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as handle:
    value = json.load(handle)[sys.argv[2]]
print(value)
PY
}

run_bench() {
  local dataset="$1"
  local seed="$2"
  local batch_size="$3"
  local threads="$4"
  local engine="$5"
  local vertices="$6"
  local initial_file="$7"
  local updates_file="$8"

  local log="logs/real_datasets/${dataset}_seed${seed}_${engine}_b${batch_size}_t${threads}_${timestamp}.log"
  local cmd=(
    ./build/benchmarks/bench_smoke
    --engine "${engine}"
    --workload file_stream
    --seed "${seed}"
    --initial-file "${initial_file}"
    --updates-file "${updates_file}"
    --vertices "${vertices}"
    --delta-cap "${DELTA_CAP}"
    --batch-size "${batch_size}"
  )
  if [[ "${engine}" == "par_relaxed" ]]; then
    cmd+=(--c "${C_VALUE}" --max-rounds "${MAX_ROUNDS}")
  elif [[ "${engine}" == "par_exact" ]]; then
    cmd+=(--max-rounds "${MAX_ROUNDS}")
  fi
  if [[ "${VALIDATE_FINAL_ONLY}" == "1" ]]; then
    cmd+=(--validate-final-only)
  fi

  echo "===== RUN dataset=${dataset} seed=${seed} engine=${engine} batch_size=${batch_size} threads=${threads} ====="
  print_command "${cmd[@]}"
  PARLAY_NUM_THREADS="${threads}" "${cmd[@]}" | tee "${log}"
  echo "${log}" >> "${manifest_path}"
  echo
}

main() {
  : > "${manifest_path}"

  {
    echo "===== REAL DATASET LARGE-BATCH PROBE START ====="
    echo "run_start=${timestamp}"
    echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
    echo "host=$(hostname)"
    echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "datasets=${DATASETS}"
    echo "seeds=${SEEDS}"
    echo "batch_sizes=${BATCH_SIZES}"
    echo "thread_values=${THREAD_VALUES}"
    echo "delta_cap=${DELTA_CAP}"
    echo "c_value=${C_VALUE}"
    echo "max_rounds=${MAX_ROUNDS}"
    echo "validate_final_only=${VALIDATE_FINAL_ONLY}"
    echo

    if [[ "${RUN_BUILD}" == "1" ]]; then
      echo "===== CONFIGURE / BUILD ====="
      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
      cmake --build build --target bench_smoke
      echo
    fi
    if [[ "${RUN_CTEST}" == "1" ]]; then
      echo "===== FULL CTEST ====="
      ctest --test-dir build --output-on-failure
      echo
    fi

    for dataset in ${DATASETS}; do
      for seed in ${SEEDS}; do
        prepared_dir="${PREPARED_OUTPUT_DIR}/${dataset}/seed_${seed}"
        metadata_path="${prepared_dir}/metadata.json"
        initial_file="${prepared_dir}/initial_edges.txt"
        updates_file="${prepared_dir}/updates.txt"
        if [[ ! -f "${metadata_path}" || ! -f "${initial_file}" || ! -f "${updates_file}" ]]; then
          echo "skip_reason=missing prepared files dataset=${dataset} seed=${seed} dir=${prepared_dir}"
          continue
        fi
        vertices="$(metadata_value "${metadata_path}" remapped_num_vertices)"
        if [[ "${vertices}" == "0" ]]; then
          echo "skip_reason=zero vertices dataset=${dataset} seed=${seed}"
          continue
        fi
        for threads in ${THREAD_VALUES}; do
          for batch_size in ${BATCH_SIZES}; do
            run_bench "${dataset}" "${seed}" "${batch_size}" "${threads}" "par_relaxed" \
              "${vertices}" "${initial_file}" "${updates_file}"
            run_bench "${dataset}" "${seed}" "${batch_size}" "${threads}" "par_exact" \
              "${vertices}" "${initial_file}" "${updates_file}"
          done
        done
      done
    done

    echo "===== REAL DATASET LARGE-BATCH PROBE END ====="
    echo "run_complete=1"
  } 2>&1 | tee "${run_log}"

  python3 - "${manifest_path}" "${detailed_csv}" "${summary_csv}" <<'PY'
import csv
import statistics
import sys
from pathlib import Path

manifest_path = Path(sys.argv[1])
detailed_csv = Path(sys.argv[2])
summary_csv = Path(sys.argv[3])


def as_float(value):
    try:
        return float(value)
    except Exception:
        return None


def as_int(value):
    try:
        return int(float(value))
    except Exception:
        return None


rows = []
for log_path in [Path(line.strip()) for line in manifest_path.read_text(encoding="utf-8").splitlines() if line.strip()]:
    kv = {}
    for line in log_path.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        kv[key.strip()] = value.strip()
    name = log_path.name
    # Format: <dataset>_seed<seed>_<engine>_b<batch>_t<threads>_<timestamp>.log
    parts = name.split("_")
    dataset = parts[0]
    seed = kv.get("seed", "")
    engine = kv.get("engine_name", "")
    batch_size = as_int(kv.get("batch_size", ""))
    batches_applied = as_int(kv.get("batches_applied", ""))
    max_active_size = as_int(kv.get("max_active_size", ""))
    seq_fast = as_int(kv.get("sequential_fast_path_count", ""))
    graph_apply = as_float(kv.get("graph_apply_seconds", ""))
    repair_seconds = as_float(kv.get("repair_seconds", ""))
    throughput = as_float(kv.get("throughput_updates_per_second", ""))
    graph_valid = as_int(kv.get("graph_validated", ""))
    color_valid = kv.get("coloring_validated", "")

    fast_path_ratio = ""
    if seq_fast is not None and batches_applied not in (None, 0):
        fast_path_ratio = seq_fast / batches_applied

    low_parallel_work_flag = ""
    if max_active_size is not None:
        low_parallel_work_flag = 1 if max_active_size < 128 else 0

    rows.append(
        {
            "source_log": str(log_path),
            "dataset": dataset,
            "seed": seed,
            "engine_name": engine,
            "batch_size": batch_size,
            "batches_applied": batches_applied,
            "max_active_size": max_active_size,
            "sequential_fast_path_count": seq_fast,
            "sequential_fast_path_ratio": fast_path_ratio,
            "graph_apply_seconds": graph_apply,
            "repair_seconds": repair_seconds,
            "throughput_updates_per_second": throughput,
            "graph_validated": graph_valid,
            "coloring_validated": color_valid,
            "low_parallel_work_flag_max_active_lt_128": low_parallel_work_flag,
        }
    )

fieldnames = [
    "source_log",
    "dataset",
    "seed",
    "engine_name",
    "batch_size",
    "batches_applied",
    "max_active_size",
    "sequential_fast_path_count",
    "sequential_fast_path_ratio",
    "graph_apply_seconds",
    "repair_seconds",
    "throughput_updates_per_second",
    "graph_validated",
    "coloring_validated",
    "low_parallel_work_flag_max_active_lt_128",
]
with detailed_csv.open("w", encoding="utf-8", newline="") as out:
    writer = csv.DictWriter(out, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

groups = {}
for row in rows:
    key = (row["dataset"], row["engine_name"], row["batch_size"])
    groups.setdefault(key, []).append(row)

summary_rows = []
for (dataset, engine, batch_size), vals in sorted(groups.items()):
    def collect_float(name):
        out = []
        for row in vals:
            v = row.get(name)
            if isinstance(v, (int, float)):
                out.append(float(v))
        return out

    max_active_vals = collect_float("max_active_size")
    fast_ratio_vals = collect_float("sequential_fast_path_ratio")
    graph_apply_vals = collect_float("graph_apply_seconds")
    repair_vals = collect_float("repair_seconds")
    throughput_vals = collect_float("throughput_updates_per_second")

    max_active_median = statistics.median(max_active_vals) if max_active_vals else ""
    low_parallel_flag = ""
    if max_active_vals:
        low_parallel_flag = 1 if max(max_active_vals) < 128 else 0

    summary_rows.append(
        {
            "dataset": dataset,
            "engine_name": engine,
            "batch_size": batch_size,
            "n": len(vals),
            "max_active_size_median": max_active_median,
            "max_active_size_max": max(max_active_vals) if max_active_vals else "",
            "sequential_fast_path_ratio_median": statistics.median(fast_ratio_vals) if fast_ratio_vals else "",
            "graph_apply_seconds_median": statistics.median(graph_apply_vals) if graph_apply_vals else "",
            "repair_seconds_median": statistics.median(repair_vals) if repair_vals else "",
            "throughput_median": statistics.median(throughput_vals) if throughput_vals else "",
            "all_graph_validated": 1 if all((r.get("graph_validated") == 1) for r in vals) else 0,
            "max_active_size_max_lt_128_flag": low_parallel_flag,
        }
    )

summary_fields = [
    "dataset",
    "engine_name",
    "batch_size",
    "n",
    "max_active_size_median",
    "max_active_size_max",
    "sequential_fast_path_ratio_median",
    "graph_apply_seconds_median",
    "repair_seconds_median",
    "throughput_median",
    "all_graph_validated",
    "max_active_size_max_lt_128_flag",
]
with summary_csv.open("w", encoding="utf-8", newline="") as out:
    writer = csv.DictWriter(out, fieldnames=summary_fields)
    writer.writeheader()
    writer.writerows(summary_rows)

print(f"wrote_detailed_csv={detailed_csv}")
print(f"wrote_summary_csv={summary_csv}")
PY

  echo "run_log=${run_log}"
  echo "manifest_path=${manifest_path}"
  echo "detailed_csv=${detailed_csv}"
  echo "summary_csv=${summary_csv}"
}

main
