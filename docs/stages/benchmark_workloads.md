# Benchmark Workloads Stage Plan

## Current implementation status

Implemented workloads:

- `random_attempts`: default rejection-stress workload; preserves the original random attempt behavior.
- `valid_insertions`: accepted insertion streams for `batch_size=1`.
- `mixed_valid`: single-update insert/delete streams using `--insert-ratio` as a preference, not a hard constraint. If the preferred operation cannot be generated, the generator tries the other operation.
- `batch_valid`: accepted insertion-only batches. This makes `batch_size=4` and `batch_size=16` meaningful for accepted batch benchmarking.
- `conflict_heavy`: implemented for coloring engines; supports `batch_size=1` and insertion-only conflict-heavy batches for `batch_size>1`, with same-color preference and fallback valid insertion fill.

Current limitations:

- `batch_valid` does not yet implement mixed insert/delete batches.
- `conflict_heavy` remains insertion-only and keeps `graph_store_only` unsupported.
- `sparse_stream`, `dense_near_delta`, and `batch_conflict` remain future/stretch workloads.

Current Linux validation:

- `batch_valid batch_size=4` applies `1000/1000` updates with `accepted_ratio=1`.
- `batch_valid batch_size=16` applies `1024/1024` updates with `accepted_ratio=1` for `par_relaxed`.
- conflict-heavy batch c-sweep (`vertices=10000`, `updates=6800`, `delta_cap=32`, `batch_size=16`, `c in {2,4,8,16}`, seeds `1,2,3`, `PARLAY_NUM_THREADS=1`, `max_generation_attempts=5000000`) validated with:
  - `updates_generated=6800`
  - `accepted_ratio=1`
  - `batch_accepted_ratio=1`
  - `graph_validated=1`
  - `coloring_validated=1`
  - `fallback_count=0`
- Full CTest passes: `100% tests passed, 0 tests failed out of 10`.
- Parlay external-header warnings may appear during build but are not currently blocking.

## 1. Goal and non-goals

### Goal

Design benchmark workload generation for dynamic graph coloring experiments that supports meaningful comparisons across `graph_store_only`, `seq_baseline`, and `par_relaxed`.

The stage should preserve the current smoke benchmark path while adding deterministic workload modes with controllable or measurable accepted-update ratios, especially for batched updates.

### Non-goals

- Do not change `GraphStore` invariants.
- Do not weaken batch atomicity or rejection semantics.
- Do not implement `SEQ-Exact` or `PAR-Exact`.
- Do not add external dependencies.
- Do not optimize coloring engines in this stage.
- Do not replace the existing rejection-heavy random workload; keep it for rejection-stress comparisons.

## 2. Current benchmark limitations

The current `bench_smoke` workload generates random edge attempts independently:

- For each requested update, choose random endpoints.
- Avoid self-loops by shifting one endpoint when possible.
- Choose insert/delete with probability `0.5`.
- Group generated updates into batches when `batch_size > 1`.

Limitations:

- `batch_size=4` can apply very few updates.
- `batch_size=16` can reject all batches.
- Rejection-heavy workloads measure validation, graph rejection paths, and batch atomicity more than coloring repair behavior.
- Final performance comparisons need workloads where batched engines apply nontrivial numbers of updates.
- There is no way to request an accepted-update target.
- There is no workload name in output, so logs are hard to compare across experiments.
- Current random attempts remain useful, but only as one workload family.

## 3. Workload families to support

### `random_attempts`

Current behavior. Generate random insert/delete attempts without trying to ensure validity.

Purpose:

- Rejection-stress baseline.
- Backward-compatible smoke behavior.
- Useful for testing batch atomicity under invalid inputs.

### `valid_insertions`

Generate insertions that are currently valid under `GraphStore` constraints until `updates_generated` or `target_accepted` is reached, or until saturation.

Current implementation supports `batch_size=1`.

Purpose:

- Isolate accepted insertion cost.
- Exercise recoloring/repair paths without rejection noise.

### `valid_deletions`

Delete existing edges from an initialized graph.

Purpose:

- Measure deletion cost.
- Confirm deletion-heavy workloads avoid unnecessary repair in `par_relaxed`.

### `mixed_valid`

Maintain existing-edge and non-existing-edge pools to produce a mostly valid insert/delete mix according to `--insert-ratio`.

Current implementation supports `batch_size=1`. `--insert-ratio` is a preference rather than a hard guarantee because the generator falls back when the preferred operation is unavailable.

Purpose:

- Representative dynamic stream.
- Configurable accepted ratio.
- Better final-report baseline than raw random attempts.

### `conflict_heavy`

For coloring engines, prefer valid insertions between same-colored vertices when possible. If no such edge can be found within generation limits, fall back to valid insertions.

Purpose:

- Stress `par_relaxed` repair rounds.
- Create meaningful coloring work while preserving graph validity.
- For `graph_store_only`, use the same generated edge stream produced from a color-aware run or a topology-only approximation documented in logs.

Current implementation:

- supports coloring engines only (`seq_baseline`, `par_relaxed`);
- keeps `graph_store_only` unsupported;
- supports `batch_size=1` and insertion-only batches for `batch_size>1`;
- for batch mode, prefers same-color edges and then uses fallback valid insertions when needed.

### `sparse_stream`

Keep graph degree low relative to `delta_cap`.

Purpose:

- Model sparse evolving graphs.
- Compare low-conflict, low-degree behavior.

### `dense_near_delta`

Initialize and maintain graphs near `delta_cap` without violating degree caps.

Purpose:

- Stress degree-bound checks and coloring in near-saturated graphs.
- Reveal behavior as valid insertions become harder to find.

### `batch_valid`

Construct batches that are valid under `GraphStore` batch semantics, avoiding duplicate undirected edges, self-loops, degree-cap violations, and conflicting intra-batch updates.

Current implementation is insertion-only. Mixed insert/delete batches are not implemented yet.

Purpose:

- Ensure `batch_size=4` and `batch_size=16` apply nontrivial numbers of updates.
- Measure batch processing cost without rejection dominating.

### `batch_conflict`

Intentionally construct invalid batches, such as duplicate same-batch edges or degree-cap violations.

Purpose:

- Rejection stress.
- Verify batch atomicity remains intact.
- Keep separate from performance claims about accepted updates.

## 4. Accepted-update-ratio control

Benchmark modes should either control or explicitly report accepted ratio.

Definitions:

- `updates_requested`: user-requested update count.
- `updates_generated`: updates successfully emitted by the workload generator.
- `generation_attempts`: candidate attempts considered while building the stream.
- `updates_applied`: updates accepted by the engine/graph.
- `updates_rejected`: generated updates rejected by the engine/graph.
- `accepted_ratio = updates_applied / updates_generated` when `updates_generated > 0`.

Control mechanisms:

- `--target-accepted <count>`: keep generating until the benchmark applies the target count or generation limit is reached.
- `--max-generation-attempts <count>`: bound generator work to avoid infinite loops near saturation.
- `--insert-ratio <0..1>`: guide mixed workloads.
- Workload-specific generation should prefer known-valid candidates where the workload name promises valid behavior.

Recommended policy:

- `random_attempts` measures accepted ratio but does not control it.
- `valid_insertions`, `valid_deletions`, `mixed_valid`, and `batch_valid` should aim for high accepted ratios.
- `batch_conflict` intentionally targets low accepted ratios.
- If a generator saturates before target, print the achieved counts and a saturation indicator if added later.

## 5. Single-update vs batch-update benchmark design

### Single-update mode

When `batch_size <= 1`:

- Generate one update at a time.
- Apply immediately.
- Update generator state after each accepted operation.
- Use this mode for precise accepted-update control and simpler debugging.

### Batch-update mode

When `batch_size > 1`:

- Generate a batch as a coherent unit.
- For valid batch workloads, enforce `GraphStore` batch semantics during generation.
- Apply batch atomically through existing engine APIs.
- Update generator state only after an accepted batch.
- If a batch is rejected unexpectedly in a valid workload, count it and keep the output truthful; do not weaken batch semantics to force acceptance.

Design target:

- Provide at least one workload where `batch_size=4` and `batch_size=16` apply nontrivial numbers of updates.
- Keep `random_attempts` and `batch_conflict` for rejected-batch behavior.

## 6. Graph initialization strategies

### Empty graph

Default current behavior.

Use for:

- Smoke tests.
- Insertion-heavy streams.
- Backward compatibility.

### Random valid initial graph

Use `--initial-edges <count>` to build a deterministic valid graph before timing update processing.

Requirements:

- Respect `delta_cap`.
- Avoid duplicate undirected edges and self-loops.
- Stop if the target edge count cannot be reached within generation attempts.
- Initialize coloring after the initial graph is built for coloring engines.

### Sparse initial graph

Build a graph with degree far below `delta_cap`.

Use for:

- `sparse_stream`.
- Mixed insert/delete streams with room for valid insertions.

### Dense near-delta initial graph

Build a graph near degree cap without violating constraints.

Use for:

- `dense_near_delta`.
- Rejection and saturation analysis.

## 7. Update stream generation strategies

### Candidate edge helpers

Add helper logic that can:

- Canonicalize undirected edges.
- Check self-loop rejection before generation.
- Track existing edges.
- Track non-existing candidate edges where practical.
- Estimate degree-cap feasibility before emitting insertions.

### Valid insertion generation

Generate candidate non-existing edges whose endpoints have remaining degree capacity.

Stop when:

- target generated/accepted count is reached,
- graph saturates,
- `--max-generation-attempts` is exhausted.

### Valid deletion generation

Sample from existing edges.

Stop when:

- target count is reached,
- no edges remain.

### Mixed valid generation

Use `--insert-ratio` to choose intended operation kind.

Fallback behavior:

- If insertion is requested but no valid insertion is available, try deletion.
- If deletion is requested but no edge exists, try insertion.
- If neither is available, stop generation.

### Conflict-heavy generation

For coloring engines:

- Inspect current colors.
- Prefer valid non-existing edges `(u, v)` where `color[u] == color[v]`.
- Respect degree caps and graph validity.
- Fall back to any valid insertion if no same-color candidate is found.

For `graph_store_only`:

- Prefer using the same pre-generated stream from a coloring engine run for direct comparison.
- If stream sharing is not implemented in the first pass, document that topology-only conflict-heavy behavior is approximate.

### Valid batch generation

Construct batches incrementally against a shadow topology:

- Avoid duplicate undirected edges inside the same batch.
- Avoid applying insert/delete contradictions inside the same batch.
- Respect degree caps after all batch insertions.
- For deletion entries, require the edge to exist in the pre-batch or shadow state according to intended semantics.

### Invalid batch generation

Construct batches that intentionally violate one batch rule:

- duplicate same undirected insertion,
- self-loop,
- degree-cap overflow,
- deletion of missing edge,
- conflicting insert/delete pair if `GraphStore` rejects it.

Keep this workload separate from accepted-update performance experiments.

## 8. Metrics to report

Existing metrics to preserve:

- `benchmark_name`
- `engine_name`
- `seed`
- `num_vertices`
- `delta_cap`
- `updates_requested`
- `updates_applied`
- `updates_rejected`
- `batch_size`
- `initial_edges`
- `final_edges`
- `build_seconds`
- `update_seconds`
- `validate_seconds`
- `throughput_updates_per_second`
- `max_degree_observed`
- `graph_validated`
- `coloring_validated` when applicable
- existing engine-specific metrics for `seq_baseline` and `par_relaxed`

New workload metrics:

- `workload`
- `generation_attempts`
- `updates_generated`
- `accepted_ratio`
- `initial_edges_requested`
- `target_accepted` if configured
- `insert_ratio` if configured
- `max_generation_attempts`
- `validate_every`

Optional later metrics:

- `batches_generated`
- `batches_applied`
- `batches_rejected`
- `batch_accepted_ratio`
- `generation_saturated`

## 9. CLI design

Required additions:

```bash
--workload random_attempts|valid_insertions|valid_deletions|mixed_valid|conflict_heavy|batch_valid|batch_conflict
--initial-edges <count>
--insert-ratio <0..1>
--target-accepted <count>
--max-generation-attempts <count>
--validate-every <k>
--validate-final-only
```

Defaults:

- `--workload random_attempts` to preserve existing behavior.
- `--initial-edges 0`.
- `--insert-ratio 0.5`.
- `--target-accepted 0`, meaning use `--updates` as the generated/requested target.
- `--max-generation-attempts` default to a conservative multiple of `updates`, such as `100 * updates`.
- Final validation remains enabled by default.

Validation policy for CLI:

- Reject unknown workload names.
- Reject `--insert-ratio` outside `[0, 1]`.
- Reject contradictory validation flags.
- Keep `--engine`, `--seed`, `--vertices`, `--updates`, `--delta-cap`, `--batch-size`, `--c`, `--palette-multiplier`, `--max-rounds`, and `--diagnostics` working.

## 10. Output/logging design

Output remains deterministic `key=value` lines.

Rules:

- Always print `workload`.
- Always print `seed`.
- Always print generation and acceptance counts.
- Preserve existing keys for backward comparison.
- Add new keys after existing core benchmark identity keys where practical.
- Do not print per-update logs in normal benchmark output.
- Use external scripts or shell redirection for log files; benchmark binary should remain simple.

Example output additions:

```text
workload=mixed_valid
generation_attempts=1234
updates_generated=1000
accepted_ratio=0.94
initial_edges_requested=5000
target_accepted=1000
insert_ratio=0.5
max_generation_attempts=100000
validate_every=0
```

## 11. Correctness validation policy

Default policy:

- Always validate final graph invariants.
- Validate final coloring for coloring engines.
- Keep relaxed palette range validation for `par_relaxed`.

Optional policy:

- `--validate-final-only` validates only at the end.
- `--validate-every <k>` validates every `k` applied operations or batches.
- `--validate-every 0` may mean final-only if this is simpler than a separate boolean.

Constraints:

- Do not weaken correctness checks to improve benchmark numbers.
- Rejected updates/batches must preserve graph and coloring state according to existing engine contracts.
- Valid workloads should still report unexpected rejections rather than hiding them.

## 12. Experiment matrix for final report

Recommended final-report matrix:

- `n`: `1000`, `10000`, and `100000` if feasible.
- `delta_cap`: `16`, `32`.
- `batch_size`: `1`, `4`, `16`, `64`.
- `engines`: `graph_store_only`, `seq_baseline`, `par_relaxed`.
- `par_relaxed c`: `2`, `4`, `8`.
- `workloads`: `random_attempts`, `mixed_valid`, `conflict_heavy`, `batch_valid`.
- `seeds`: `1`, `2`, `3`.

Initial reduced matrix:

- `n`: `1000`, `10000`.
- `delta_cap`: `16`.
- `batch_size`: `1`, `4`, `16`.
- `workloads`: `random_attempts`, `mixed_valid`, `batch_valid`.
- `seeds`: `1`.

Interpretation rules:

- Use `random_attempts` as rejection-stress, not as the only performance result.
- Use `mixed_valid` and `batch_valid` for accepted-update throughput.
- Use `conflict_heavy` to compare repair behavior.
- Always report accepted ratios with throughput.

## 13. Staged Implementation Plan

### Step 1: CLI and output metrics only

- Add `--workload` with default `random_attempts`.
- Preserve current generation behavior exactly for `random_attempts`.
- Add output keys:
  - `workload`
  - `generation_attempts`
  - `updates_generated`
  - `accepted_ratio`
  - `initial_edges_requested`
  - `target_accepted`
  - `insert_ratio`
  - `max_generation_attempts`
- Do not add new workload generation yet.
- This is the first implementation task and should be coded alone before any generator refactor.

### Step 2: `valid_insertions`

- Generate valid insertions using rejection sampling and a generator-side shadow topology.
- Keep this single-update focused first.
- Validate deterministic behavior under fixed seeds.
- Stop cleanly when the graph saturates or `--max-generation-attempts` is reached.

### Step 3: `mixed_valid`

- Generate mostly valid insert/delete streams using `--insert-ratio`.
- Maintain generator-side topology.
- Focus on `batch_size=1` first.
- Extend to `batch_size > 1` only if straightforward after the single-update path is stable.

### Step 4: `batch_valid`

- Construct valid batches under `GraphStore` batch semantics.
- Goal: make `batch_size=4` and `batch_size=16` apply nontrivial updates.
- Start with insertion-only valid batches if mixed insert/delete batches are too complex.
- Preserve batch atomicity; if a supposedly valid batch is rejected, report it rather than weakening semantics.

### Step 5: `conflict_heavy`

- Use coloring information to prefer valid edges whose endpoints share a color.
- Preserve deterministic seed behavior.
- Document comparison limitations with `graph_store_only`, since that engine has no coloring state.

### Step 6: benchmark scripts and reduced experiment matrix

- Add scripts for the reduced final-report matrix.
- Keep large experiments manual.
- Keep the existing smoke script behavior available for quick validation.

Stretch workloads:

- `conflict_heavy`, `sparse_stream`, `dense_near_delta`, and `batch_conflict` are stretch workloads.
- They are useful for broader coverage, but they should not block the main Benchmark Workloads stage.
- Implement them only after `random_attempts`, `valid_insertions`, `mixed_valid`, `batch_valid`, and the reduced matrix are stable.

## 14. Risks and fallback simplifications

Risks:

- Valid batch generation can accidentally disagree with `GraphStore` batch semantics.
- Maintaining edge pools may consume memory for large `n`.
- Conflict-heavy generation depends on colors and is harder to compare with `graph_store_only`.
- Dense-near-delta generation can saturate before target counts.
- Accepted-ratio control can bias workloads if not clearly reported.
- Per-operation validation can dominate timing.

Fallback simplifications:

- Start with simple rejection sampling plus `--max-generation-attempts`.
- For large graphs, avoid materializing all non-edges; sample candidates instead.
- Implement `mixed_valid` before `conflict_heavy`.
- Implement `batch_valid` for insertions first, then add mixed insert/delete batches.
- If `target_accepted` is hard to enforce in the first pass, print measured accepted ratio and saturation status.
- Keep `sparse_stream`, `dense_near_delta`, and `batch_conflict` out of the main acceptance path if schedule is tight.

## 15. Acceptance criteria

This stage is complete when:

- `bench_smoke` still supports existing engines: `graph_store_only`, `seq_baseline`, and `par_relaxed`.
- Default behavior remains compatible with current `random_attempts` smoke runs.
- Benchmark output includes `workload`, `generation_attempts`, `updates_generated`, and `accepted_ratio`.
- These core workloads are implemented:
  - `random_attempts`,
  - `valid_insertions`,
  - `mixed_valid`,
  - `batch_valid`.
- At least one workload applies nontrivial updates for `batch_size=4` and `batch_size=16`.
- Deterministic seeds reproduce the same generated stream and results on Linux.
- Final graph validation and coloring validation still pass.
- Full CTest passes on Linux.
- A reduced experiment matrix runs successfully and logs accepted ratios for all runs.
- Stretch workloads (`conflict_heavy`, `sparse_stream`, `dense_near_delta`, `batch_conflict`) are documented but not required for the current core workload milestone.
