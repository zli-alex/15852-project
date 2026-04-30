# Dynamic Graph Coloring (C++17 + ParlayLib)

This repository currently has Foundation, SEQ-Baseline, and PAR-Relaxed (through Step 6 phase-separated repair rounds) implemented.

- Shared infrastructure is in place (`types`, `batch`, `GraphStore`, validators, RNG).
- `SeqBaselineEngine` is implemented as a correctness-first sequential exact baseline.
- Benchmarks support `graph_store_only`, `seq_baseline`, and `par_relaxed`.
- `ParRelaxedEngine` uses deterministic phase-separated repair rounds for accepted insertions and accepted batches, with full relaxed greedy recolor fallback for correctness.
- Accepted deletions do not trigger repair; rejected updates/batches preserve graph, colors, and stats.
- Paper-specific exact algorithms (SEQ-Exact, PAR-Exact) are not implemented yet.

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
./build/benchmarks/bench_smoke --engine graph_store_only|seq_baseline|par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
```

Representative commands:

```bash
./build/benchmarks/bench_smoke --engine graph_store_only --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4
./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1 --palette-multiplier 2 --max-rounds 4
./build/benchmarks/bench_smoke --engine par_relaxed --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4 --c 4 --max-rounds 4
```

Representative `par_relaxed` output shape:

```text
benchmark_name=foundation_smoke
engine_name=par_relaxed
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
palette_multiplier=4
palette_size=33
max_rounds=4
total_rounds=<rounds>
fallback_count=<fallbacks>
vertices_touched_total=<value>
```

Notes:

- Always run benchmarks with explicit deterministic `--seed` and record it.
- `graph_store_only` remains the default mode for backward compatibility.
- `seq_baseline` is correctness-first and uses full greedy recoloring after accepted insertions and accepted batches.
- `par_relaxed` is benchmark-selectable via `--engine par_relaxed`.
- Benchmark workloads currently include:
  - `random_attempts` (default rejection-stress workload),
  - `valid_insertions` (`batch_size=1` accepted insertion streams),
  - `mixed_valid` (`batch_size=1` insert/delete streams where `--insert-ratio` is a preference),
  - `batch_valid` (accepted insertion-only batches for meaningful `batch_size=4` and `batch_size=16` runs).
- Future/stretch workloads include `conflict_heavy`, `sparse_stream`, `dense_near_delta`, and `batch_conflict`.
- `par_relaxed` uses bounded phase-separated repair rounds for accepted insertions/batches and falls back to full relaxed greedy recolor if conflicts remain.
- `total_rounds`, `fallback_count`, and `vertices_touched_total` are meaningful PAR-Relaxed benchmark metrics.
- Optional PAR-Relaxed diagnostics are available with `--diagnostics 1`, including repair counters/timers and small-active-set fast-path counters.
- `par_relaxed` CLI options: `--palette-multiplier`, `--c`, `--max-rounds`.
- `seq_baseline` is not the paper-specific SEQ-Exact algorithm.
- `graph_validated=1` indicates final structural validation success; `coloring_validated=1` indicates final exact-coloring validation success.
- Conservative batch behavior is expected: repeated same undirected edge inside a batch may reject the entire batch.
- Earlier `par_relaxed batch_size=4` runs showed high `repair_seconds` despite tiny active sets. Diagnostics identified Parlay tiny-active-set overhead; this is addressed by a thresholded sequential repair fast path for active sets of size `<= 128`.

Extended PAR-Relaxed fuzz:

```bash
DGCOLOR_EXTENDED_FUZZ=1 ctest --test-dir build -R '^par_relaxed_fuzz$' --output-on-failure
```

Linux smoke script:

```bash
./scripts/run_seq_baseline_smoke.sh
./scripts/run_par_relaxed_smoke.sh
```

`run_par_relaxed_smoke.sh` runs configure/build, `graph_store_only`, `seq_baseline`, and `par_relaxed` smoke benchmarks (`c in {2,4,8}` with batch sizes `1` and `4`), then full build + CTest, and logs to `logs/`.

## Environment Notes

- Linux validation succeeded with:
  - `cmake -S . -B build`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
- Linux validation for SEQ-Baseline also succeeded:
  - benchmark script completed with `run_complete=1`
  - full CTest passed: `100% tests passed, 0 tests failed out of 8`
- Linux validation for PAR-Relaxed Step 6 smoke also succeeded:
  - benchmark script completed with `run_complete=1`
  - full CTest passed: `100% tests passed, 0 tests failed out of 10`
  - total test time around 6 seconds
- Linux validation after the PAR-Relaxed small-active-set repair fast path succeeded:
  - full CTest passed: `100% tests passed, 0 tests failed out of 10`
  - total test time around 2.78 seconds
  - medium `batch_size=4` problem case dropped to about `0.0043s update_seconds`
- Linux validation after benchmark workload additions succeeded:
  - `batch_valid batch_size=4` applied `1000/1000` updates with `accepted_ratio=1`
  - `batch_valid batch_size=16` applied `1024/1024` updates with `accepted_ratio=1` for `par_relaxed`
  - full CTest passed: `100% tests passed, 0 tests failed out of 10`
  - Parlay external-header warnings may appear during build but are not currently blocking
- Some local macOS setups may fail with missing standard C++ headers. This is an SDK/toolchain environment issue, not a project logic issue. Linux cluster results are the source of truth.
