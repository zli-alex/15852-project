# PAR-Relaxed Step 6 Plan: Phase-Separated Parallel Repair Rounds

## 1. Goal and non-goals

### Goal
Replace unconditional full relaxed greedy recolor after accepted insertions/batches with a cautious deterministic parallel repair attempt, while preserving guaranteed correctness through full greedy fallback.

### Non-goals
- Do not change public `ColoringEngine` interface.
- Do not change `GraphStore` semantics or rejection behavior.
- Do not change benchmark CLI shape.
- Do not implement `SEQ-Exact` or `PAR-Exact`.
- Do not remove full greedy fallback.

## 2. Current behavior to preserve

- Rejected update/batch keeps graph and colors unchanged.
- Accepted deletion keeps colors unchanged.
- Post-operation coloring remains proper and in relaxed range.
- Deterministic same-seed behavior for equivalent operation streams.
- Existing benchmark fields and test/fuzz invariants remain valid.

Primary implementation/test files to touch:
- [`include/dgcolor/par_relaxed_engine.hpp`](include/dgcolor/par_relaxed_engine.hpp)
- [`src/par_relaxed_engine.cpp`](src/par_relaxed_engine.cpp)
- [`tests/test_par_relaxed.cpp`](tests/test_par_relaxed.cpp)

## 3. Proposed new helper functions

Add internal (private) helpers in `ParRelaxedEngine` for clarity and phase separation:
- `validate_coloring_or_throw(const char* context)`
- `color_in_palette_range(Color c)` (or equivalent small predicate)
- `deterministic_hash(...)` and/or `deterministic_color_offset(...)`
- relaxed greedy color helper (reuse existing helper if already present)
- `collect_conflicted_vertices_from_candidates(...)`
- `expand_with_neighbors(...)`
- `initial_active_from_update(...)`
- `initial_active_from_batch(...)`
- `attempt_parallel_repair(...)`

These remain engine-private and do not alter external API.

## 4. Active-set definition

### Single insertion
- After topology acceptance, if inserted endpoints have different colors: active set empty, skip repair.
- If endpoints conflict: seed active set with both endpoints (conservative).
- Optionally expand to neighbors immediately for safety (`endpoints + neighbors`).

### Batch acceptance
- Seed with endpoints of inserted edges.
- Additionally include endpoint pairs currently conflicting after batch topology mutation.
- Conservatively expand with neighbors.
- Filter to currently conflicted vertices before entering rounds.

## 5. Conflict definition

A vertex is conflicted if any neighbor currently has the same color under the latest committed `colors_`.

A proposal is unsafe if committing it could create an equal-color edge under round commit rules.

## 6. Deterministic proposal generation

Per active vertex, propose a color by:
1. Building neighbor-color unavailable set from current committed `colors_` snapshot.
2. Computing deterministic hashed start offset from `(seed_, round_index, vertex_id[, attempt])`.
3. Scanning palette cyclically from that offset to choose first available color.

No shared mutable RNG is used inside parallel loops.

## 7. Phase-separated round algorithm

For up to `max_rounds_`:
1. **Collect active/conflicted vertices** (round input).
2. **Proposal phase (parallel)**: compute proposals without mutating `colors_`.
3. **Safety/conflict phase (parallel)**: compute `safe` flags without mutating `colors_`.
4. **Commit phase (parallel)**: apply only safe proposals.
5. **Recompute unresolved set** from affected region (`active + neighbors`), then iterate.

Stop early if unresolved set becomes empty.

## 8. Commit/tie-break rule

Use deterministic tie-break for adjacent active vertices with same proposed color:
- lower vertex id wins,
- higher vertex id is unsafe for this round.

Conservative safety preference: reject ambiguous commits rather than risking conflict.

## 9. Fallback behavior

If unresolved conflicts remain after `max_rounds_`:
- run `recolor_all_greedy_relaxed()` full fallback,
- increment `fallback_count_`,
- perform post-op validation (proper coloring + palette bound).

Fallback remains mandatory for correctness.

## 10. Stats behavior

For accepted insertion/batch operations:
- `total_rounds_ += rounds_attempted`.
- `vertices_touched_total_ += active_vertices_touched_across_rounds`.
- if fallback used: add full-graph recolor touched vertices and increment `fallback_count_`.

Per-operation stats:
- `UpdateStats.vertices_touched` / `BatchStats.vertices_touched` include round touches + fallback full recolor touch count (if any).
- Rejected operations report zero edges changed and zero vertices touched.

## 11. Update path behavior

`apply_update()`:
- unchanged preconditions (`initialize_coloring()` required).
- apply topology via `graph_.apply_update(update)` first.
- if rejected: return unchanged stats/state.
- if accepted delete: no repair attempt, validate, return.
- if accepted insert:
  - build initial active set,
  - run bounded phase-separated repair,
  - fallback if unresolved,
  - validate and return stats.

## 12. Batch path behavior

`apply_batch()`:
- unchanged preconditions (`initialize_coloring()` required).
- apply topology atomically via `graph_.apply_batch(batch)`.
- if rejected: return unchanged stats/state.
- if accepted:
  - derive conservative initial active set from inserted/conflicting endpoints,
  - run bounded phase-separated repair,
  - fallback if unresolved,
  - validate and return stats.

## 13. Determinism strategy

- Round proposals use pure deterministic hash-based generation.
- No mutable RNG in parallel contexts.
- Tie-break based on vertex id only.
- Round order and commit rule deterministic under fixed operation stream.
- Add deterministic regression test comparing two same-seed engines on identical operation sequence for equal colors and stats.

## 14. Test plan

Extend [`tests/test_par_relaxed.cpp`](tests/test_par_relaxed.cpp):
- No-conflict insertion: no extra rounds/fallback expected.
- Conflict insertion: rounds attempted and/or fallback, final validity preserved.
- `max_rounds=1`: correctness preserved via fallback path when needed.
- Deletion: no round/fallback increments.
- Rejected update/batch: graph/colors stable and round/fallback stats unchanged.
- Same-seed deterministic scenario: identical final colors and stats.
- Keep assertions invariant-based (not exact color labels unless deterministic test explicitly checks equality across runs).

## 15. Fuzz-test impact

Keep [`tests/test_par_relaxed_fuzz.cpp`](tests/test_par_relaxed_fuzz.cpp) passing unchanged in structure:
- accepted op: invariants + exact coloring + palette bound,
- rejected op: unchanged colors/edges,
- mirror topology equivalence.

If needed, only adjust fuzz diagnostics, not core expectations.

## 16. Benchmark impact

No CLI/shape changes required in [`benchmarks/bench_smoke.cpp`](benchmarks/bench_smoke.cpp).

Expected behavior change only in values:
- `total_rounds` may become non-zero,
- `fallback_count` may increase when round cap is hit,
- `vertices_touched_total` reflects rounds + fallback work.

All required benchmark keys remain unchanged.

## 17. Risks and conservative fallbacks

Primary risks:
- Proposal computed from stale snapshot becoming unsafe at commit time.
- Active-set under-approximation missing conflict propagation.
- Over-aggressive commits creating new conflicts.

Conservative mitigations:
- Strict phase separation with no writes before commit phase.
- Conservative safety checks (reject borderline commits).
- Neighbor-expanded unresolved recomputation each round.
- Guaranteed full greedy fallback on unresolved conflicts.

## 18. Acceptance criteria

Step 6 is complete when:
- Insert/batch accepted paths attempt phase-separated repair rounds before fallback.
- Rejected operations remain no-op on graph/colors.
- Accepted deletions do not require repair.
- Final state after every accepted op remains proper and palette-bounded.
- Determinism test passes for same seed + same operation stream.
- Existing PAR-relaxed unit/fuzz and full suite pass on Linux.
- Bench output keys unchanged; `par_relaxed` remains selectable.

## Step 6A / 6B / 6C / 6D Implementation Breakdown

### Step 6A: Helper scaffolding only
- Add private helper declarations/definitions for:
  - `validate_coloring_or_throw`
  - `color_in_palette_range`
  - `deterministic_hash` / `deterministic_color_offset`
  - greedy relaxed color helper if not already present
  - `collect_conflicted_vertices_from_candidates`
  - `expand_with_neighbors`
- Add narrow tests only for behavior observable through public engine operations where practical.
- Do **not** wire repair rounds into `apply_update` / `apply_batch` yet.
- Existing behavior should remain unchanged: accepted insertions/batches still use full greedy recolor.

### Step 6B: Attempt repair loop as an internal helper
- Implement `attempt_parallel_repair(...)` as an internal helper.
- It may be tested indirectly through a controlled scenario if appropriate, but should not yet replace production update/batch behavior unless helper stability is confirmed.
- Keep full fallback path unchanged in public operations.

### Step 6C: Wire repair loop into `apply_update`
- Accepted insertion uses repair attempt first.
- Deletion remains no-repair.
- Rejected update remains no-op.
- Full greedy fallback remains mandatory if unresolved conflicts remain.

### Step 6D: Wire repair loop into `apply_batch`
- Accepted batch uses repair attempt first.
- Rejected batch remains no-op.
- Full greedy fallback remains mandatory if unresolved conflicts remain.

## Safety Rule for Proposal Commit

- All proposals are computed from the same pre-round color snapshot.
- A vertex proposal is safe only if:
  - proposed color is within palette,
  - proposed color differs from every non-active neighbor's current color,
  - for every active neighbor:
    - if the neighbor has a proposal with the same color, only the lower vertex id may commit;
    - if the neighbor does not safely commit, the proposal must still differ from that neighbor's current color unless that neighbor is the known conflict being repaired and will remain active.
- If this rule is difficult to reason about, use an even more conservative rule:
  - a proposal is safe only if it differs from all neighbors' current colors and all active neighbors' proposed colors, except it may win deterministic tie-break against same proposed color.
- Prefer fewer commits over risky commits.

## Deterministic Hash Design

- Do not use `std::hash` (not guaranteed stable across implementations).
- Use a small explicit integer mixing function over `uint64_t`.
- Inputs should include:
  - `seed_`
  - `round_index`
  - `vertex_id`
  - optional `attempt`/salt
- The function must be deterministic across platforms.

## Coloring Validation Helper

Step 6A should add a private engine helper:
- `validate_coloring_or_throw(context)`

It should check:
- `validate_exact_coloring(graph_, colors_)`
- every color `< palette_size_`

Do not change public validator API yet unless clearly beneficial.

## When Fallback Counts

- `fallback_count_` increments **only** when bounded repair leaves unresolved conflicts and full greedy recolor is used.
- Full greedy recolor during initialization does **not** count as fallback.
- Step 5's previous always-full-recolor behavior should not be retroactively counted as fallback.

## Testing by Substep

- Step 6A tests should not expect `total_rounds` or `fallback_count` to change.
- Step 6C tests may start checking `total_rounds` / `fallback_count` for insertions.
- Step 6D tests may start checking `total_rounds` / `fallback_count` for batches.
- Same-seed determinism tests should compare final colors and stats.

## Plain-English algorithm summary

After an accepted insertion or batch, the engine focuses on potentially conflicted vertices, runs a bounded number of deterministic parallel repair rounds where proposals are computed first, checked second, and committed last, and if any conflict remains it falls back to full relaxed greedy recoloring to guarantee correctness.

## Highest-risk part

The highest-risk part is commit safety under simultaneous proposals: proposals can look valid under the snapshot but interact badly once commits are applied. The plan addresses this with strict phase separation, conservative safety checks, deterministic tie-break, and mandatory fallback.

## Recommended implementation split

Yes—implement in the explicit conservative sequence:
1. Step 6A helper scaffolding only.
2. Step 6B internal repair loop helper only.
3. Step 6C update-path wiring.
4. Step 6D batch-path wiring.
5. Then broaden tests/fuzz validation on Linux.
