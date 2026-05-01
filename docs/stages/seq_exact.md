# SEQ-Exact Stage Plan

## Implementation status (complete)

The **SEQ-Exact** stage is implemented end-to-end: engine, unit tests, deterministic fuzz, and **`bench_smoke --engine seq_exact`** benchmark integration.

### Implemented behavior

- **Initialization:** deterministic per-vertex levels (hash mixing from `seed` and vertex id) and timestamps; greedy exact coloring over vertices `0..n-1` with palette `[0, delta_cap]`.
- **Insertions:** on accepted insert, if endpoints share a color, choose one endpoint by **higher level**, then **newer/larger timestamp**, then **larger vertex id**; `level_conflict_choices` increments only when level comparison decides; bounded local recolor cascade; **full greedy exact recolor** on failure (`full_fallback_count`).
- **Deletions:** accepted deletions leave **colors unchanged**; stats unchanged for delete-only paths.
- **Batches:** `GraphStore::apply_batch` first; after topology mutation, repair from **inserted edges** whose endpoints conflict; same endpoint policy and local repair + greedy fallback as single updates.
- **Fuzz:** `test_seq_exact_fuzz` exercises `apply_update` / `apply_batch` with an `AdjacencyGraphStore` mirror, validates after every accepted op, and checks rejection stability (topology, colors, stats).
- **Benchmarks:** `bench_smoke` accepts `--engine seq_exact`, initializes coloring before workloads, supports `random_attempts`, `valid_insertions`, `mixed_valid`, `batch_valid`, and `conflict_heavy` (same coloring-engine path as `seq_baseline` / `par_relaxed`). Prints `palette_size`, `recolor_calls`, `recolored_vertices_total`, `cascade_steps_total`, `full_fallback_count`, `level_conflict_choices`, plus `graph_validated=1` and `coloring_validated=1`.

### Conservative simplifications (still true)

- **Deletion recoloring** is omitted (no optional/probabilistic deletion recolor yet).
- **Levels** guide **which endpoint recolors first** only; they are not used for paper-style palette sampling.
- **Color choice** in local repair is **deterministic first-available greedy** in `[0, delta_cap]`, not randomized palette sampling from the paper.
- **Full greedy fallback** guarantees correctness when local repair hits its cap or fails.
- **Batch repair** is correctness-first (sequential, may repair multiple conflicted inserts in batch order); it is **not** PAR-Exact or parallel exact.

### Linux validation

- **CTest:** on validated Linux runs, full suite passes: **`100% tests passed, 0 tests failed out of 12`** (includes `seq_exact`, `seq_exact_fuzz`, and existing tests).
- **`bench_smoke` / `seq_exact`:** paste representative command lines and key=value output lines here when available (e.g. `valid_insertions`, `mixed_valid`, `batch_valid`, `conflict_heavy` with `batch_size=1` and `batch_size=16`).

### Current limitations

- Does **not** achieve the paper’s theoretical **expected O(1)** update time; implementation is correctness- and determinism-first.
- **PAR-Exact** is not implemented.
- **Batch repair** may run redundant local work or fall back to full greedy more often than a tuned algorithm; performance is secondary to correctness in this stage.

---

## 1. Goal and non-goals

### Goal

Implement a correctness-first `SEQ-Exact` engine for exact dynamic graph coloring in C++17.

The engine should use the existing `GraphStore` and `ColoringEngine` contracts, preserve a proper coloring after every accepted update or batch, and keep every color in the exact palette `[0, delta_cap]`. The first implementation should model the paper-inspired sequential data model: fixed degree cap, exact palette size, per-vertex levels, timestamps, insertion conflict resolution, optional deletion recoloring policy hooks, and local recoloring cascades.

### Non-goals

- Do not implement `PAR-Exact` in this stage.
- Do not change `GraphStore` semantics, rejection behavior, or batch atomicity.
- Do not change existing engines.
- Do not target the full theoretical expected `O(1)` update bound in the first implementation.
- Do not introduce nondeterministic update results for a fixed seed and input stream.
- Do not weaken exact coloring validation.

## 2. Relationship to SEQ-Baseline and PAR-Relaxed

- `SEQ-Baseline` remains the simplest exact reference. It recolors the whole graph after accepted insertions and accepted batches, and leaves colors unchanged after deletions.
- `SEQ-Exact` should be a new engine, not a modification of `SEQ-Baseline`.
- `SEQ-Exact` should reuse the same public engine shape as `SeqBaselineEngine`: `name()`, `graph()`, `color_of()`, `colors()`, `initialize_coloring()`, `apply_update()`, and `apply_batch()`.
- `PAR-Relaxed` is useful implementation reference for deterministic stats, diagnostics, conflict collection, bounded repair, and fallback counters, but its relaxed palette and parallel phase structure are not part of this stage.
- `SEQ-Exact` should become the sequential paper-inspired exact engine against which future `PAR-Exact` behavior can be compared.

## 3. Paper concepts to model

Model these concepts directly in the engine state, even if the first algorithm uses conservative versions of them:

- Fixed `delta_cap`: supplied at construction through `AdjacencyGraphStore`.
- Exact palette: `palette_size = delta_cap + 1`, with legal colors `[0, delta_cap]`.
- Vertex levels: `levels_[v]` is a stable per-vertex `Level` used to decide which conflicted endpoint should move first.
- Timestamps: `timestamps_[v]` tracks the last logical recolor/update touch for deterministic tie-breaking and diagnostics.
- Conflict resolution on insertions: an accepted insertion that joins equal-colored endpoints must repair the conflict immediately.
- Deletion recoloring policy: deletion never creates a color conflict, so the first implementation should omit deletion recoloring and leave a future probabilistic hook.
- Local recoloring cascade: when one vertex recolors, any remaining conflicts caused by the change should be handled through a bounded queue/stack of local repair work.

## 4. Simplifications for first implementation

- Omit deletion recoloring initially. This answers the first design question: deletion recoloring should be omitted in Step 3 because deletions cannot introduce conflicts, and correctness is already preserved by leaving colors unchanged.
- Use levels only to choose the first endpoint to recolor on an insertion conflict. This answers the second design question: do not fully integrate levels into palette sampling in the first implementation.
- Use deterministic color choice, not probabilistic palette sampling, in the first local recolor path.
- Keep the local cascade bounded by a configurable or fixed internal step cap.
- If local repair cannot restore invariants quickly, run a deterministic full greedy recolor over all vertices.
- Keep batch handling simple: apply the graph batch atomically through `GraphStore`, collect conflicted vertices from inserted edges, repair locally, and fall back to full greedy recolor when needed.
- If batch repair complexity blocks Step 4, it is acceptable to stage batch behavior as full greedy recolor after accepted batches before adding local batch repair.

## 5. Engine interface and file plan

Create:

- `include/dgcolor/seq_exact_engine.hpp`
- `src/seq_exact_engine.cpp`
- `tests/test_seq_exact.cpp`
- `tests/test_seq_exact_fuzz.cpp`

Modify when implementing:

- `CMakeLists.txt`
- `tests/CMakeLists.txt`
- `benchmarks/bench_smoke.cpp`
- `benchmarks/CMakeLists.txt` only if needed by target wiring
- `README.md` or stage status docs only after implementation checkpoints

Proposed engine constructor shape:

- `SeqExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed)`
- `SeqExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed, const UpdateBatch& initial_updates)`

Public engine-specific accessors should be non-virtual and lightweight:

- `Color palette_size() const`
- `std::uint64_t recolor_calls() const`
- `std::uint64_t recolored_vertices_total() const`
- `std::uint64_t cascade_steps_total() const`
- `std::uint64_t full_fallback_count() const`
- `std::uint64_t level_conflict_choices() const`

## 6. Internal state

Core state:

- `AdjacencyGraphStore graph_`
- `parlay::sequence<Color> colors_`
- `parlay::sequence<Level> levels_`
- `parlay::sequence<Timestamp> timestamps_`
- `std::uint64_t seed_`
- `Timestamp logical_time_`
- `bool initialized_`

Derived constants:

- `Color palette_size_ = delta_cap + 1`
- legal color range `[0, palette_size_ - 1]`

Stats:

- `std::uint64_t recolor_calls_`
- `std::uint64_t recolored_vertices_total_`
- `std::uint64_t cascade_steps_total_`
- `std::uint64_t full_fallback_count_`
- `std::uint64_t level_conflict_choices_`
- optional diagnostic counters for local repair failures and neighbor scans

## 7. Initialization strategy

Initialization should:

1. Assign deterministic initial levels and timestamps for every vertex.
2. Greedily color vertices in deterministic order using palette `[0, delta_cap]`.
3. Mark `initialized_ = true`.
4. Validate exact coloring and palette bounds.

Initial level policy:

- Use deterministic hash-style randomness from `(seed_, vertex_id)` or `Rng` in vertex-id order.
- Bound levels to a simple range, for example `[0, delta_cap]` or `[0, max(1, delta_cap)]`.
- Keep the exact distribution less important than determinism and stable tie-breaking in the first implementation.

Initial timestamp policy:

- Start `logical_time_ = 0`.
- Set `timestamps_[v]` to deterministic initialization metadata, such as `0` for all vertices or vertex-id order during greedy coloring.
- Increment `logical_time_` only for accepted update repair events and recolors.

## 8. Insert update behavior

For `apply_update(Insert)`:

1. Require prior initialization.
2. Apply the edge through `graph_.apply_update(update)`.
3. If rejected, return `applied=false` and leave graph and colors unchanged through `GraphStore` rejection semantics.
4. If accepted and `colors_[u] != colors_[v]`, validate and return with no recolor.
5. If accepted and `colors_[u] == colors_[v]`, choose one endpoint to recolor.
6. Run local recolor cascade.
7. If cascade fails, run full greedy recolor.
8. Validate exact coloring and palette bounds before returning.

Endpoint choice should be deterministic:

- Prefer the endpoint with higher level.
- If levels tie, prefer the endpoint with newer/larger timestamp.
- If timestamps tie, prefer the larger vertex id or another documented stable rule.
- Increment `level_conflict_choices_` when the level comparison determines the choice.

This is paper-inspired but conservative: levels influence who moves first, not the full probabilistic palette mechanism yet.

## 9. Delete update behavior

For `apply_update(Delete)`:

1. Require prior initialization.
2. Apply the edge through `graph_.apply_update(update)`.
3. If rejected, return `applied=false` and preserve graph/colors.
4. If accepted, leave colors unchanged.
5. Optionally update timestamps for endpoints only if diagnostics need a visible touch marker; prefer not to change timestamps in Step 3 unless needed.
6. Validate exact coloring and palette bounds.

Deletion recoloring should be omitted in the first implementation. Add a private placeholder or comment for a future probabilistic deletion recolor policy, but do not change behavior until correctness and deterministic fuzz coverage are stable.

## 10. Recolor procedure

Use a local repair function such as:

- `bool repair_from_conflict(VertexId start, std::size_t* vertices_touched)`
- `bool recolor_vertex_local(VertexId v)`
- `void recolor_all_greedy_exact()`

Local cascade policy:

1. Maintain a FIFO queue or vector stack of conflicted vertices.
2. Pop a vertex `v`.
3. Compute available colors from current neighbor colors.
4. If a color is available, assign a deterministic candidate and update `timestamps_[v]`.
5. Re-scan neighbors of `v` and enqueue any neighbors still conflicting with `v`.
6. Stop when the queue is empty.
7. Fail if the number of cascade steps exceeds a conservative cap.

Recommended cap:

- Start with `max(4 * num_vertices, 1)` or another simple function of graph size.
- The cap is not for theoretical performance; it prevents accidental infinite loops while fallback guarantees correctness.

Fallback guarantee:

- On local failure, call full greedy exact recolor over all vertices.
- This answers the fourth design question: the local-repair fallback should be deterministic full greedy recolor, followed by strict validation.

## 11. Palette / available-color computation

Palette rules:

- `palette_size_ = graph_.delta_cap() + 1`
- valid colors are `0 <= color <= graph_.delta_cap()`
- `kUncolored` may appear only during initialization or full recolor internals, never after public operations complete.

Available-color computation:

1. Allocate or reuse a `std::vector<unsigned char>` or `std::vector<bool>` of length `palette_size_`.
2. Scan `graph_.neighbors(v)`.
3. Mark neighbor colors that are in range.
4. Choose the first available color for deterministic greedy behavior.

Future refinement:

- Add deterministic hash offset `(seed_, logical_time_, vertex_id, attempt)` to scan colors cyclically.
- Keep this behind a clearly documented policy so exact test assertions do not rely on unstable color labels.

## 12. Level and timestamp policy

Levels:

- Initialize deterministically from seed and vertex id.
- Use levels first for insertion conflict endpoint choice.
- Consider updating a recolored vertex's level during local repair only after the first implementation is stable.
- Keep level range small and inspectable in tests.

Timestamps:

- Treat `EdgeUpdate::time` as metadata supplied by generators, not wall-clock time.
- Maintain internal `logical_time_` for engine events.
- On every successful recolor, increment `logical_time_` and store it in `timestamps_[v]`.
- If `EdgeUpdate::time` is nonzero, the plan should not depend on it for correctness; at most use it later as an input to deterministic tie-breaking if needed.

Policy answer:

- Levels and timestamps should initially be tie-breaking metadata for choosing recolor order and recording local cascade behavior, not a complete implementation of the paper's sampling analysis.

## 13. Randomness and determinism

The engine must remain deterministic across runs with the same seed, graph size, `delta_cap`, initial updates, and update stream.

Recommended policy:

- Store `seed_`.
- Use `Rng` only in deterministic vertex-id order during initialization, or use a local deterministic hash helper.
- Avoid mutable random draws inside repair decisions whose count depends on traversal accidents.
- Prefer hash-style functions for levels and future color offsets:
  - inputs: `seed_`, `vertex_id`, `logical_time_`, optional salt
  - output: stable `std::uint64_t`
- Iterate vertices and active sets in sorted or deterministic insertion order.
- Use stable tie-breakers for all equal-priority cases.

This answers the third design question: determinism comes from fixed traversal order, deterministic hash/Rng usage, and documented tie-breaks.

## 14. Correctness invariants

After `initialize_coloring()`, every accepted `apply_update()`, and every accepted `apply_batch()`:

- `graph_` satisfies existing `GraphStore` invariants.
- `colors_.size() == graph_.num_vertices()`.
- No vertex remains `kUncolored`.
- Every color is in `[0, delta_cap]`.
- For every edge `(u, v)`, `colors_[u] != colors_[v]`.
- `levels_.size() == graph_.num_vertices()`.
- `timestamps_.size() == graph_.num_vertices()`.
- Rejected updates and rejected batches preserve graph topology and full color vector.

Validation implementation should call existing exact coloring validation and add an explicit palette upper-bound check if needed.

## 15. Stats and diagnostics

Expose and print these metrics:

- `recolor_calls`
- `recolored_vertices_total`
- `cascade_steps_total`
- `full_fallback_count`
- `level_conflict_choices`

Optional diagnostics:

- `local_repair_failures`
- `neighbor_scans`
- `conflicted_vertices_initial_total`
- `max_cascade_depth_seen`

Stats policy:

- Rejected updates/batches should not increment recolor/cascade/fallback counters.
- Accepted insertions with no color conflict should increment no recolor counters.
- Full fallback should increment both fallback count and touched/recolored totals in a documented way.

## 16. Tests and fuzz checks

Unit tests:

- Initialization on empty/no-edge/path/cycle/star/near-delta-cap graphs.
- Constructor with valid initial updates.
- Constructor rejects invalid initial updates.
- Accepted insertion with no conflict preserves coloring validity.
- Accepted insertion with conflict repairs exact coloring.
- Accepted deletion leaves colors valid and preferably unchanged.
- Rejected update preserves graph and full color vector.
- Accepted batch repairs conflicts.
- Rejected batch preserves graph and full color vector.
- Deterministic seed behavior: same seed and same stream produce same colors/stats.

Fuzz tests:

- Fixed seed matrix.
- Small graph sizes and varied `delta_cap`.
- Mix single updates and batches.
- Mirror topology with `AdjacencyGraphStore`.
- After every accepted operation/batch, validate graph and exact coloring.
- After every rejected operation/batch, verify edge count, topology mirror, and full color vector are unchanged.
- Include conflict-heavy insertion generation where possible to exercise local cascade.

Do not assert exact color labels broadly except in deterministic-specific tests; prefer invariant checks.

## 17. Benchmark integration

**Done.** `bench_smoke` accepts `--engine seq_exact`.

Supported workloads:

- `random_attempts`
- `valid_insertions`
- `mixed_valid`
- `batch_valid`
- `conflict_heavy`

Benchmark behavior:

- Construct `SeqExactEngine(vertices, delta_cap, seed)`.
- Initialize coloring before workload execution.
- Use `apply_update` when `batch_size <= 1` and `apply_batch` when `batch_size > 1` (consistent with other coloring engines).
- Keep existing graph and coloring validation output.
- Print:
  - `engine_name=seq_exact`
  - `palette_size`
  - `recolor_calls`
  - `recolored_vertices_total`
  - `cascade_steps_total`
  - `full_fallback_count`
  - `level_conflict_choices`
  - `graph_validated=1`, `coloring_validated=1` on success

Batch benchmark policy:

- Correctness-first: local repair from inserted-edge conflicts, then full greedy fallback if needed.
- No parallel exact behavior.

## 18. Staged implementation steps

### Step 1: engine skeleton and greedy exact initialization

- Add `SeqExactEngine`.
- Use palette size `delta_cap + 1`.
- Initialize levels and timestamps deterministically.
- Implement greedy exact coloring.
- Add initialization tests only.
- Dynamic update methods may be stubs that require initialization and return conservative unimplemented errors until Step 2, or may reject by documented temporary behavior if tests avoid them.

### Step 2: accepted insertion behavior

- Apply accepted insertions through `GraphStore`.
- If no color conflict, do nothing.
- If conflict, recolor one deterministic endpoint using level/timestamp tie-breaks.
- Add local recolor cascade with full greedy fallback.
- Validate exact coloring after every accepted insertion.

### Step 3: accepted deletion behavior

- Apply accepted deletions through `GraphStore`.
- Leave colors unchanged.
- Validate exact coloring.
- Keep deletion recoloring as a future optional/probabilistic extension.

### Step 4: batch behavior

- Apply graph batch atomically through `GraphStore`.
- For accepted batches, collect endpoints of inserted edges whose colors conflict after topology mutation.
- Repair locally from collected vertices.
- Use full greedy fallback if local repair fails.
- If local batch repair is risky, first implement accepted batch full greedy recolor and then refine.

### Step 5: deterministic fuzz tests

- Add mirror-graph fuzzing.
- Validate graph and exact coloring after every accepted operation/batch.
- Verify rejected updates/batches preserve state.
- Include deterministic reproducibility checks.

### Step 6: benchmark integration

- **Complete:** `--engine seq_exact`, required stats, all listed workloads including `conflict_heavy`.

## 19. Risks and fallback simplifications

Risks:

- Local cascade can oscillate if recolor order is poorly chosen.
- Deterministic tests can become brittle if they assert exact color labels too often.
- Level/timestamp policy can look paper-like without delivering theoretical guarantees.
- Batch repair can touch a large region and obscure local-repair behavior.

Fallback simplifications:

- Use full greedy recolor whenever local repair exceeds its step cap.
- For Step 4, use full greedy recolor for all accepted batches before local batch repair.
- Keep levels fixed after initialization if level updates complicate correctness.
- Keep deletion recoloring omitted until insertion and batch behavior are stable.
- Use first-available color instead of randomized color offsets until deterministic fuzz is passing.

## 20. Acceptance criteria

The `SEQ-Exact` stage is **met** when (all satisfied on Linux):

- `SeqExactEngine` is a separate engine; existing engines unchanged in behavior.
- Full CTest passes: **`100% tests passed, 0 tests failed out of 12`** (includes `seq_exact` and `seq_exact_fuzz`).
- `bench_smoke --engine seq_exact` runs on implemented workloads; benchmark log snippets can be archived under [Linux validation](#linux-validation).
- Every accepted update/batch leaves graph and exact coloring valid; colors in `[0, delta_cap]`; rejections preserve topology and colors.
- Benchmark output includes required `seq_exact` stats.
- Documentation records paper-faithful vs conservative simplifications (see [Implementation status](#implementation-status-complete) and [First-implementation simplifications](#first-implementation-simplifications)).

## Paper-faithful parts

- Fixed `delta_cap`.
- Exact palette size `delta_cap + 1`.
- Per-vertex levels.
- Per-vertex timestamps.
- Insertion-triggered conflict resolution.
- Local recoloring cascade.
- Optional deletion recoloring concept represented as a future policy, even though it is initially disabled.

## First-implementation simplifications

- Deletions do not recolor.
- Levels only choose which conflicted endpoint moves first.
- Timestamps are deterministic tie-break and diagnostic metadata.
- Color selection is first-available greedy (not paper palette sampling).
- Full greedy recolor is the correctness fallback.
- Batches are correctness-first: local repair from inserted-edge conflicts, with full greedy fallback; not PAR-Exact.
- No parallel exact behavior is included.
