# SEQ-Baseline Stage

## Goal

Deliver a simple, deterministic, correctness-first sequential dynamic coloring baseline on top of the Foundation infrastructure.

This stage provides a reliable exact-coloring reference engine before paper-specific SEQ-Exact or parallel algorithm stages.

## Implemented Scope

- `SeqBaselineEngine` implemented in:
  - `include/dgcolor/seq_baseline_engine.hpp`
  - `src/seq_baseline_engine.cpp`
- Initialization:
  - `initialize_coloring()` performs full greedy coloring over vertices `0..n-1`.
  - Palette is bounded to `[0, delta_cap]` (`delta_cap + 1` colors).
- Single updates:
  - `apply_update()` requires prior initialization.
  - Rejected updates return `applied=false` and preserve graph/coloring state.
  - Accepted insertions trigger full greedy recoloring.
  - Accepted deletions keep colors unchanged.
  - Accepted operations validate exact coloring.
- Batch updates:
  - `apply_batch()` requires prior initialization.
  - Uses `GraphStore::apply_batch()` for atomic topology mutation.
  - Rejected batches return `applied=false` and preserve graph/coloring state.
  - Accepted batches trigger full greedy recoloring and exact-coloring validation.
- Benchmarks:
  - `bench_smoke` now supports `--engine graph_store_only|seq_baseline`.
  - Default remains `graph_store_only` for backward compatibility.
  - `seq_baseline` mode reports exact-coloring validation and `vertices_touched_total`.

## Non-Goals (Still Not Implemented)

- No paper-specific SEQ-Exact level/timestamp cascade logic.
- No local repair optimization (current policy is full greedy recolor after accepted insertions/batches).
- No parallel algorithm logic.

## Behavior Notes

- This stage is intentionally correctness-first, not throughput-first.
- Conservative batch behavior from `GraphStore` remains expected:
  - repeated same undirected edge inside a batch is rejected as `BatchConflict`.
  - rejected batches are atomic and do not partially mutate topology.
- `SeqBaselineEngine` is therefore suitable as a reference baseline for future differential testing.

## Deterministic Test Coverage

Implemented tests include:

- `seq_baseline` deterministic unit/integration tests for:
  - initialization on empty/no-edge/path/cycle/star/near-delta-cap graphs
  - accepted/rejected single updates
  - accepted/rejected batches
  - rejected-operation state stability checks
- `seq_baseline_fuzz` deterministic small-graph fuzz tests with fixed seeds, small graph sizes, varied `delta_cap`, and both single/batch operations:
  - accepted ops: validate graph invariants + exact coloring
  - rejected ops: verify unchanged edge count + unchanged colors
  - topology cross-check against a mirror `AdjacencyGraphStore`

## Benchmark Support

Supported commands:

```bash
./build/benchmarks/bench_smoke --engine graph_store_only --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 1
./build/benchmarks/bench_smoke --engine seq_baseline --seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 4
```

Linux smoke script:

```bash
./scripts/run_seq_baseline_smoke.sh
```

## Completion Summary

- SEQ-Baseline stage implementation is complete for correctness-first baseline goals.
- Linux validation completed successfully:
  - benchmark smoke script finished with `run_complete=1`
  - full CTest passed: `100% tests passed, 0 tests failed out of 8`
  - passing tests:
    - `foundation_placeholder`
    - `rng`
    - `types_batch`
    - `interfaces_compile`
    - `graph_store`
    - `validator`
    - `seq_baseline`
    - `seq_baseline_fuzz`
- Representative `seq_baseline` single-update benchmark output includes:
  - `engine_name=seq_baseline`
  - `graph_validated=1`
  - `coloring_validated=1`
  - `vertices_touched_total=1504`
- Known local macOS standard-header failures are treated as environment-only SDK/toolchain issues; Linux cluster results are the source of truth.
