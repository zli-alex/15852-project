#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

mkdir -p logs
timestamp="$(date +%Y%m%d_%H%M%S)"
log_path="logs/seq_baseline_smoke_${timestamp}.log"

exec > >(tee -a "${log_path}") 2>&1

echo "run_start=${timestamp}"
echo "root_dir=${ROOT_DIR}"
echo "log_path=${log_path}"

cmake -S . -B build
cmake --build build --target bench_smoke

./build/benchmarks/bench_smoke --engine graph_store_only --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4

for seed in 1 2 3 12345 99991; do
  ./build/benchmarks/bench_smoke --engine seq_baseline --seed "${seed}" --vertices 32 --updates 120 --delta-cap 8 --batch-size 4
done

cmake --build build
ctest --test-dir build --output-on-failure

echo "run_complete=1"
