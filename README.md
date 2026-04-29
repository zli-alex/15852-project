# Dynamic Graph Coloring (C++17 + ParlayLib)

This repository currently has Foundation and SEQ-Baseline stages implemented.

- Shared infrastructure is in place (`types`, `batch`, `GraphStore`, validators, RNG).
- `SeqBaselineEngine` is implemented as a correctness-first sequential exact baseline.
- Benchmarks support both `graph_store_only` and `seq_baseline` engines.
- Paper-specific algorithms (SEQ-Exact, PAR-Relaxed, PAR-Exact) are not implemented yet.

## Requirements

- CMake 3.16+
- A C++17 compiler
- ParlayLib available via one of:
  - `external/parlaylib` git submodule (recommended)
  - `-DPARLAYLIB_DIR=/path/to/parlaylib` (or its `include` directory)

No external dependencies are downloaded automatically.

## ParlayLib Setup (Submodule)

Clone with submodules:

```bash
git clone --recurse-submodules <repo-url>
```

If already cloned:

```bash
git submodule update --init --recursive
```

## Configure

```bash
cmake -S . -B build
```

With explicit ParlayLib location:

```bash
cmake -S . -B build -DPARLAYLIB_DIR=/absolute/path/to/parlaylib
```

Optional feature toggles:

```bash
cmake -S . -B build -DDGCOLOR_BUILD_TESTS=ON -DDGCOLOR_BUILD_BENCHMARKS=ON
```

## Build

Build all enabled targets:

```bash
cmake --build build
```

Build only smoke benchmark:

```bash
cmake --build build --target bench_smoke
```

Build only selected tests:

```bash
cmake --build build --target test_rng test_types_batch test_interfaces_compile test_graph_store test_validator test_seq_baseline test_seq_baseline_fuzz
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

On validated Linux runs, all 8 tests pass:

- `foundation_placeholder`
- `rng`
- `types_batch`
- `interfaces_compile`
- `graph_store`
- `validator`
- `seq_baseline`
- `seq_baseline_fuzz`

## Smoke Benchmarks

Engine selection:

```bash
./build/benchmarks/bench_smoke --engine graph_store_only|seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
```

Representative commands:

```bash
./build/benchmarks/bench_smoke --engine graph_store_only --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4
```

Representative `seq_baseline` output shape:

```text
benchmark_name=foundation_smoke
engine_name=seq_baseline
seed=1
num_vertices=32
delta_cap=8
updates_requested=100
updates_applied=49
updates_rejected=51
batch_size=1
initial_edges=0
final_edges=45
build_seconds=<seconds>
update_seconds=<seconds>
validate_seconds=<seconds>
throughput_updates_per_second=<value>
max_degree_observed=8
graph_validated=1
coloring_validated=1
vertices_touched_total=<value>
```

Notes:

- Always run benchmarks with explicit deterministic `--seed` and record it.
- `graph_store_only` remains the default mode for backward compatibility.
- `seq_baseline` is correctness-first and uses full greedy recoloring after accepted insertions and accepted batches.
- `seq_baseline` is not the paper-specific SEQ-Exact algorithm.
- `graph_validated=1` indicates final structural validation success; `coloring_validated=1` indicates final exact-coloring validation success.
- Conservative batch behavior is expected: repeated same undirected edge inside a batch may reject the entire batch.

Linux smoke script:

```bash
./scripts/run_seq_baseline_smoke.sh
```

The script runs configure/build, graph-store-only and seq-baseline smoke benchmarks, seed sensitivity checks, full build, and full CTest, and logs to `logs/`.

## Environment Notes

- Linux validation succeeded with:
  - `cmake -S . -B build`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
- Linux validation for SEQ-Baseline also succeeded:
  - benchmark script completed with `run_complete=1`
  - full CTest passed: `100% tests passed, 0 tests failed out of 8`
- Some local macOS setups may fail with missing standard C++ headers. This is an SDK/toolchain environment issue, not a project logic issue. Linux cluster results are the source of truth.
