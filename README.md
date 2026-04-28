# Dynamic Graph Coloring (C++17 + ParlayLib)

This repository currently has the Foundation stage implemented.

- Shared infrastructure is in place (`types`, `batch`, `GraphStore`, validators, RNG).
- The current benchmark engine is `graph_store_only`.
- No coloring algorithms (SEQ-Exact, PAR-Relaxed, PAR-Exact) are implemented yet.

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
cmake --build build --target test_rng test_types_batch test_interfaces_compile test_graph_store test_validator
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

On validated Linux runs, all 6 tests pass:

- `foundation_placeholder`
- `rng`
- `types_batch`
- `interfaces_compile`
- `graph_store`
- `validator`

## Foundation Smoke Benchmark

Representative command:

```bash
./build/benchmarks/bench_smoke --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
```

Representative output shape:

```text
benchmark_name=foundation_smoke
engine_name=graph_store_only
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
```

Notes:

- Always run benchmarks with explicit deterministic `--seed` and record it.
- `graph_validated=1` indicates the final graph passed structural validation.
- In Foundation batch mode, conservative batch conflict semantics can reject many or all updates for random workloads at larger batch sizes (`4/8/16`). This is expected at this stage.

## Environment Notes

- Linux validation succeeded with:
  - `cmake -S . -B build`
  - `cmake --build build`
  - `ctest --test-dir build --output-on-failure`
- Some local macOS setups may fail with missing standard C++ headers. This is an SDK/toolchain environment issue, not a project logic issue.
