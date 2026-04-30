# Conflict Heavy Batch Mode Plan

## 1. Goal and non-goals

### Goal

Design `conflict_heavy` for `batch_size > 1` so each emitted batch contains multiple valid insertion updates that preferentially connect same-color endpoints, while preserving `GraphStore` batch semantics and benchmark determinism.

### Non-goals

- Do not change engine logic (`seq_baseline`, `par_relaxed`, or `graph_store_only` internals).
- Do not weaken batch atomicity.
- Do not change `GraphStore` invariants.
- Do not implement mixed insert/delete conflict-heavy batches in this stage.
- Do not add stream sharing for `graph_store_only` in this stage.

## 2. Why single-update conflict_heavy is insufficient for c/thread scaling

- Single-update `conflict_heavy` generates one local same-color insertion at a time.
- Repair work per update remains small and often stays in sequential fast path.
- Observed behavior (`sequential_repair_rounds == repair_rounds`) masks thread scaling.
- c-sweep effect is muted because each update induces only a tiny local conflict and quickly resolves.
- Batch mode is needed to aggregate multiple same-color insertions in one apply, increasing concurrent repair work.

## 3. Batch generation semantics

- `conflict_heavy` batch mode should construct insertion-only `UpdateBatch` objects.
- Batch construction happens against a pre-batch snapshot of:
  - engine colors,
  - shadow edge set,
  - shadow degrees.
- Prefer same-color insertion candidates first.
- If same-color candidates are insufficient to fill the target batch size, allow fallback valid insertions.
- Emit the batch only when it has at least one valid update.
- Apply through existing engine batch API (`apply_batch`) to preserve current semantics.

## 4. Shadow topology requirements

Generator-side shadow state must include:

- existing undirected edge set (for O(1)-ish duplicate checks),
- degree vector,
- optional edge list/vector for bookkeeping if needed later.

Update policy:

- On accepted batch: update shadow topology with all inserted edges.
- On rejected batch: do not mutate shadow topology.
- Shadow topology must mirror successful engine-side topology changes only.

## 5. Same-color candidate selection

- Inspect engine colors before constructing each batch.
- Candidate insertion `(u, v)` is preferred when:
  - `u != v`,
  - `colors[u] == colors[v]`,
  - edge does not exist in shadow topology,
  - projected post-batch degrees stay within `delta_cap`.
- Use deterministic RNG with existing seed to sample candidate pairs.
- Use bounded local sampling budget per batch to avoid unbounded generation.

## 6. Intra-batch validity rules

For every insertion in a candidate batch:

- no loops (`u != v`),
- no existing edges in pre-batch shadow topology,
- no duplicate undirected edge within the same batch,
- degree cap respected after pending insertions:
  - `degree[u] + pending_degree[u] < delta_cap`,
  - `degree[v] + pending_degree[v] < delta_cap`.

These checks must be enforced before emitting each insertion into the batch.

## 7. Interaction with current colors

- Colors are read from the engine before constructing a batch.
- Same-color preference is based on pre-batch colors only.
- Do not update colors during generation; color changes occur only through engine apply.
- This keeps generator behavior deterministic and avoids coupling generation to mid-batch repair outcomes.

## 8. Generator metrics

Keep existing metrics and add/extend:

- `generator_same_color_attempts`
- `generator_same_color_chosen`
- `generator_same_color_fallbacks`
- `batches_generated`
- `batches_applied`
- `batch_accepted_ratio` (`batches_applied / batches_generated` when `batches_generated > 0`)

Reporting rules:

- Do not hide fallback usage; report it explicitly.
- Keep `updates_generated`, `updates_applied`, `updates_rejected`, and `accepted_ratio` consistent with emitted/applied updates.

## 9. Expected effects on c-scaling

- Larger `c` should make same-color matches rarer for a fixed graph state, potentially increasing:
  - `generator_same_color_attempts`,
  - `generator_same_color_fallbacks`.
- Batch conflict-heavy should increase per-batch local conflict density relative to single-update mode.
- This should expose more visible c-dependent repair behavior than current single-update conflict-heavy.

## 10. Expected effects on thread-scaling

- Multiple same-color insertions per batch should enlarge active/conflicted sets per repair call.
- This should reduce the fraction of rounds that fall into sequential fast path on larger cases.
- Thread scaling should improve only when `sequential_repair_rounds / repair_rounds` decreases meaningfully below 1.
- If ratio remains near 1, thread scaling is still expected to be limited.

## 11. Implementation steps

1. Extend workload parser/guards:
   - allow `conflict_heavy` with `batch_size > 1` for coloring engines only,
   - keep `graph_store_only` unsupported with clear argument error.

2. Add batch generation helper(s):
   - construct insertion-only conflict-heavy batch using pre-batch colors and shadow topology,
   - enforce intra-batch validity rules.

3. Add fallback fill logic:
   - if same-color insertions cannot fill batch, use valid insertion fallback until batch full or attempts exhausted.

4. Apply batch and update counters:
   - increment update and batch counters,
   - update shadow topology only on accepted batch,
   - preserve truthful rejection accounting.

5. Add output metrics for batch generation quality:
   - same-color metrics plus `batches_generated`, `batches_applied`, `batch_accepted_ratio`.

6. Keep single-update conflict-heavy behavior unchanged.

## 12. Tests / validation commands

Build and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bench_smoke
cmake --build build
ctest --test-dir build --output-on-failure
```

Conflict-heavy batch validation:

```bash
./build/benchmarks/bench_smoke --engine seq_baseline --workload conflict_heavy --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size 4
./build/benchmarks/bench_smoke --engine par_relaxed --workload conflict_heavy --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size 4 --c 4 --max-rounds 4 --diagnostics 1
./build/benchmarks/bench_smoke --engine par_relaxed --workload conflict_heavy --seed 1 --vertices 1000 --updates 1024 --delta-cap 16 --batch-size 16 --c 4 --max-rounds 4 --diagnostics 1
```

Unsupported engine behavior check:

```bash
./build/benchmarks/bench_smoke --engine graph_store_only --workload conflict_heavy --seed 1 --vertices 1000 --updates 1000 --delta-cap 16 --batch-size 4
```

## 13. Risks and fallback simplifications

Risks:

- Generator batch checks may still miss a `GraphStore` batch rejection edge case.
- Same-color search may become expensive at high `c` or sparse matching states.
- Batch still may not induce enough parallel repair pressure if fallback dominates.

Fallback simplifications:

- Start insertion-only; defer mixed insert/delete conflict-heavy batches.
- Use bounded local candidate attempts per batch and stop early on saturation.
- Allow partial batch emission (non-empty) rather than forcing full batch if generation is constrained.
- Keep `graph_store_only` unsupported until stream-sharing is explicitly implemented.

## 14. Acceptance criteria

- `conflict_heavy` supports `batch_size=4` and `batch_size=16` for `seq_baseline` and `par_relaxed`.
- `graph_store_only` remains clearly unsupported for `conflict_heavy`.
- Insertion-only conflict-heavy batches satisfy batch validity rules before apply.
- `updates_applied` is nontrivial and `accepted_ratio` remains high on representative Linux runs.
- `generator_same_color_chosen` is nonzero on representative runs.
- `generator_same_color_fallbacks` is reported and nonzero when same-color fill is hard.
- `batches_generated`, `batches_applied`, and `batch_accepted_ratio` are reported.
- Existing workloads (`random_attempts`, `valid_insertions`, `mixed_valid`, `batch_valid`) remain behaviorally unchanged.
- Full CTest remains passing on Linux.
