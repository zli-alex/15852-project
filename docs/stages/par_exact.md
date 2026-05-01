# PAR-Exact Stage Status

## 1. Implementation status

PAR-Exact is implemented through benchmark integration.

Implemented files:

- `include/dgcolor/par_exact_engine.hpp`
- `src/par_exact_engine.cpp`
- `tests/test_par_exact.cpp`
- `tests/test_par_exact_fuzz.cpp`

Benchmark integration:

- `bench_smoke` supports `--engine par_exact`.

## 2. Implemented behavior

- Exact palette size is `delta_cap + 1`.
- Deterministic level/timestamp metadata is initialized from seed and vertex id.
- Initialization uses deterministic greedy exact coloring.
- `apply_update` forwards through a one-element batch path.
- `apply_batch` uses `GraphStore::apply_batch` first, preserving atomic graph mutation semantics.
- Rejected updates/batches preserve graph, colors, and diagnostics.
- Accepted deletion-only batches leave colors unchanged.
- Accepted batches containing insertions collect conflicted inserted-edge endpoints.
- Bounded exact repair rounds are attempted with phase-separated proposal/safety/commit/frontier structure.
- Full greedy exact fallback remains available when bounded repair does not converge.
- Exact coloring is validated after accepted operations/batches.

## 3. Diagnostics printed by benchmark

For `--engine par_exact`, `bench_smoke` prints:

- `palette_size`
- `max_rounds`
- `active_vertices_total`
- `repair_rounds_total`
- `fallback_count`
- `proposal_count`
- `commit_count`
- `unresolved_count`
- `sequential_fast_path_count`
- `vertices_touched_total`

## 4. Validation summary

Current Linux validation:

- Full CTest passed:
  - `14/14 tests passed`
  - `0 failed`
- `par_exact` benchmark smoke ran on:
  - `valid_insertions`
  - `mixed_valid`
  - `batch_valid`
  - `conflict_heavy` (`batch_size=1`)
  - `conflict_heavy` (`batch_size=16`)
- All shown `par_exact` benchmark runs had:
  - `accepted_ratio=1`
  - `graph_validated=1`
  - `coloring_validated=1`
  - `fallback_count=0`

## 5. Representative benchmark observations

- `valid_insertions`: `1000/1000` updates applied, `repair_rounds_total=500`.
- `mixed_valid`: `1000/1000` updates applied, `repair_rounds_total=346`.
- `batch_valid` (`b=16`): `1024/1024` updates applied, `repair_rounds_total=64`.
- `conflict_heavy` (`b=1`): `1000/1000` updates applied, `repair_rounds_total=1000`.
- `conflict_heavy` (`b=16`): `1024/1024` updates applied, `repair_rounds_total=66`, `unresolved_count=4`, `fallback_count=0`.

## 6. Paper-faithful pieces

- Exact `Delta+1` palette.
- Batch active set derived from insertion conflicts.
- Repeated bounded repair rounds.
- Phase-separated proposal/safety/commit/frontier flow.
- Levels/timestamps retained as deterministic priority metadata.

## 7. Conservative simplifications

- No claim of theoretical expected `O(1)` update work.
- Deletion recoloring is omitted.
- Color proposals are deterministic rather than fully randomized paper sampling.
- Full greedy fallback remains a correctness backstop.
- Small-active sequential fast path likely dominates many runs.
- Distributed algorithm is not implemented.

## 8. Current limitations / future work

- Batch counters are not yet uniformly reported for all workloads (for example, some `batch_valid` runs may show zero batch counters).
- PAR-Exact scaling/performance sweeps still need to be run comprehensively.
- Repair policy is conservative and fallback-capable, not theoretically optimized.
- Next useful comparison set: `seq_baseline`, `seq_exact`, `par_relaxed`, and `par_exact` on the same workload matrix.
