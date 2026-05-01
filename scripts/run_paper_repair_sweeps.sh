#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs/scaling
timestamp="$(date +%Y%m%d_%H%M%S)"

SEED="${SEED:-1}"
VERTICES="${VERTICES:-10000}"
UPDATES="${UPDATES:-16384}"
DELTA_CAP="${DELTA_CAP:-32}"
MAX_ROUNDS="${MAX_ROUNDS:-4}"
MAX_GENERATION_ATTEMPTS="${MAX_GENERATION_ATTEMPTS:-5000000}"
DIAGNOSTICS="${DIAGNOSTICS:-0}"
REPEAT_COUNT="${REPEAT_COUNT:-1}"
BATCH_VALUES="${BATCH_VALUES:-64 128 256 512 1024}"
THREAD_VALUES="${THREAD_VALUES:-1 2 4 8}"
if [[ "${DIAGNOSTICS}" == "1" ]]; then
  run_kind="diagnostics"
else
  run_kind="timing"
fi
baseline_log="logs/scaling/batch_granularity_par_exact_paper_${run_kind}_baseline_${timestamp}.log"
token_log="logs/scaling/batch_granularity_par_exact_paper_${run_kind}_token_${timestamp}.log"
comparison_csv="logs/scaling/batch_granularity_par_exact_paper_${run_kind}_compare_${timestamp}.csv"
latest_csv="logs/scaling/batch_granularity_par_exact_paper_${run_kind}_compare_latest.csv"
median_csv="logs/scaling/batch_granularity_par_exact_paper_${run_kind}_medians_${timestamp}.csv"
latest_median_csv="logs/scaling/batch_granularity_par_exact_paper_${run_kind}_medians_latest.csv"

run_par_exact() {
  local threads="$1"
  local batch_size="$2"
  local token_flag="$3"

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
    --diagnostics "${DIAGNOSTICS}" \
    --validate-final-only \
    ${token_flag}
  echo
}

echo "===== CONFIGURE / BUILD ====="
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target test_par_exact test_par_exact_fuzz bench_smoke

echo "===== TARGETED CTEST ====="
ctest --test-dir build --output-on-failure -R par_exact

for mode in baseline token; do
  if [[ "${mode}" == "baseline" ]]; then
    log="${baseline_log}"
    token_flag=""
  else
    log="${token_log}"
    token_flag="--par-exact-token-repair"
  fi

  {
    echo "===== PAPER REPAIR ${mode} START ====="
    echo "run_name=batch_granularity_par_exact_paper_${run_kind}_${mode}"
    echo "date=$(date +"%Y-%m-%dT%H:%M:%S%z")"
    echo "host=$(hostname)"
    echo "git_commit=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "diagnostics_config=${DIAGNOSTICS}"
    for rep in $(seq 1 "${REPEAT_COUNT}"); do
      echo "repeat=${rep}"
      for batch_size in ${BATCH_VALUES}; do
        for threads in ${THREAD_VALUES}; do
          run_par_exact "${threads}" "${batch_size}" "${token_flag}"
        done
      done
    done
    echo "run_complete=1"
  } 2>&1 | tee "${log}"
done

awk -f scripts/parse_bench_kv.awk "${baseline_log}" "${token_log}" > "${comparison_csv}"
cp "${comparison_csv}" "${latest_csv}"
python3 scripts/summarize_paper_repair_medians.py "${comparison_csv}" --output "${median_csv}"
cp "${median_csv}" "${latest_median_csv}"

echo "baseline_log=${baseline_log}"
echo "token_log=${token_log}"
echo "comparison_csv=${comparison_csv}"
echo "latest_csv=${latest_csv}"
echo "median_csv=${median_csv}"
echo "latest_median_csv=${latest_median_csv}"
