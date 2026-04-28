# Foundation Stage Plan

## Goal

Create the shared C++17/ParlayLib project foundation for dynamic graph coloring:

- Build system and local project layout.
- Common graph, update, coloring, validation, RNG, test, and benchmark interfaces.
- Slow reference and validator paths for small graphs.
- Benchmark scaffolding that records deterministic seeds and core metrics.

This stage should make future SEQ-Exact, PAR-Relaxed, and PAR-Exact stages easier to implement without duplicating infrastructure.

## Non-Goals

- Do not implement any graph coloring algorithm logic.
- Do not implement SEQ-Exact, PAR-Relaxed, or PAR-Exact engines.
- Do not optimize parallel mutation or introduce algorithm-specific data structures.
- Do not add external dependencies beyond the C++17 standard library and ParlayLib unless explicitly approved.
- Do not support dynamic `delta_cap`; it is fixed for each graph instance.
- Do not accept loops, duplicate edges, or updates that would violate `delta_cap`.

## Minimal File Tree

```text
.
├── CMakeLists.txt
├── README.md
├── docs/
│   └── stages/
│       └── foundation.md
├── include/
│   └── dgcolor/
│       ├── batch.hpp
│       ├── coloring_engine.hpp
│       ├── graph_store.hpp
│       ├── rng.hpp
│       ├── types.hpp
│       └── validator.hpp
├── src/
│   ├── graph_store.cpp
│   ├── rng.cpp
│   └── validator.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_graph_store.cpp
│   ├── test_rng.cpp
│   └── test_validator.cpp
└── benchmarks/
    ├── CMakeLists.txt
    └── bench_smoke.cpp
```

## Files To Create Or Modify

- `CMakeLists.txt`: root CMake project, C++17 settings, warnings, ParlayLib include configuration, test and benchmark options.
- `README.md`: quick build/test/benchmark commands and current stage status.
- `docs/stages/foundation.md`: this stage plan.
- `include/dgcolor/types.hpp`: shared scalar types, constants, and lightweight helpers.
- `include/dgcolor/batch.hpp`: edge update and batch containers.
- `include/dgcolor/graph_store.hpp`: abstract graph interface and a simple adjacency-backed implementation declaration.
- `include/dgcolor/coloring_engine.hpp`: shared engine interface for later stages.
- `include/dgcolor/validator.hpp`: validation APIs for graph invariants and coloring correctness.
- `include/dgcolor/rng.hpp`: deterministic RNG wrapper and seed logging helpers.
- `src/graph_store.cpp`: graph implementation and update rejection rules.
- `src/validator.cpp`: slow reference validation logic.
- `src/rng.cpp`: RNG implementation.
- `tests/CMakeLists.txt`: small test executables registered with CTest.
- `tests/test_graph_store.cpp`: graph invariant and update application tests.
- `tests/test_validator.cpp`: correctness and fuzz validation tests.
- `tests/test_rng.cpp`: deterministic seed and reproducibility tests.
- `benchmarks/CMakeLists.txt`: benchmark smoke executable.
- `benchmarks/bench_smoke.cpp`: deterministic benchmark harness skeleton with metrics output.

## Proposed `ColoringEngine` Interface

The interface should define the contract shared by all later algorithm stages without committing to implementation details.

```cpp
class ColoringEngine {
public:
  virtual ~ColoringEngine() = default;

  virtual std::string name() const = 0;
  virtual const GraphStore& graph() const = 0;
  virtual Color color_of(VertexId v) const = 0;
  virtual parlay::sequence<Color> colors() const = 0;

  virtual void initialize_coloring() = 0;
  virtual UpdateStats apply_update(const EdgeUpdate& update) = 0;
  virtual BatchStats apply_batch(const UpdateBatch& batch) = 0;
};
```

Design notes:

- Exact engines must leave the graph properly colored after each completed update or batch.
- Relaxed engines may later expose stage-specific validity semantics, but the common interface should still allow validators and benchmarks to inspect the current colors.
- `UpdateStats` and `BatchStats` should include enough common timing/count fields for benchmarks without becoming algorithm-specific.
- No Foundation implementation should derive from this interface except possibly a tiny test-only stub used by validators.

## Proposed `GraphStore` Interface

The graph layer owns topology and enforces hard graph invariants before algorithms see updates.

```cpp
class GraphStore {
public:
  virtual ~GraphStore() = default;

  virtual VertexId num_vertices() const = 0;
  virtual std::size_t num_edges() const = 0;
  virtual Degree delta_cap() const = 0;
  virtual Degree degree(VertexId v) const = 0;
  virtual bool has_edge(VertexId u, VertexId v) const = 0;
  virtual parlay::sequence<VertexId> neighbors(VertexId v) const = 0;

  virtual UpdateResult can_apply(const EdgeUpdate& update) const = 0;
  virtual UpdateResult apply_update(const EdgeUpdate& update) = 0;
  virtual BatchApplyResult apply_batch(const UpdateBatch& batch) = 0;
};
```

Foundation implementation:

- Use a simple undirected adjacency representation optimized for clarity, not final performance.
- Store each undirected edge exactly once logically and mirror it in both adjacency lists.
- Reject invalid vertex ids, loops, duplicate insertions, missing-edge deletions, and insertions that exceed `delta_cap`.
- For batches, validate the complete batch against a temporary degree/edge view before mutating the graph.
- Keep mutation phase-separated: validate first, mutate second.

## Common Types

Define these in `include/dgcolor/types.hpp` and `include/dgcolor/batch.hpp`.

- `using VertexId = std::uint32_t;`
- `using Color = std::uint32_t;`
- `using Level = std::uint32_t;`
- `using Timestamp = std::uint64_t;`
- `using Degree = std::uint32_t;`
- `constexpr Color kUncolored = std::numeric_limits<Color>::max();`
- `struct Edge { VertexId u; VertexId v; };`
- `enum class UpdateKind { Insert, Delete };`
- `struct EdgeUpdate { UpdateKind kind; VertexId u; VertexId v; Timestamp time; };`
- `using UpdateBatch = parlay::sequence<EdgeUpdate>;`
- `struct UpdateStats { bool applied; std::size_t edges_changed; std::size_t vertices_touched; double seconds; };`
- `struct BatchStats { bool applied; std::size_t updates; std::size_t edges_changed; std::size_t vertices_touched; double seconds; };`
- `enum class UpdateStatus { Ok, InvalidVertex, Loop, DuplicateEdge, MissingEdge, DegreeCapExceeded, BatchConflict };`
- `struct UpdateResult { UpdateStatus status; std::string message; };`

Conventions:

- Normalize undirected edge endpoints before comparison or storage.
- Treat timestamps as metadata supplied by generators or benchmarks, not wall-clock time.
- Keep all IDs unsigned and small enough for compact graph storage.

## Validator Responsibilities

Validators should be slow, explicit, and suitable for debugging small graphs.

- Check vertex ids are in range.
- Check no loops exist.
- Check no duplicate undirected edges exist.
- Check adjacency symmetry for undirected storage.
- Check every degree is at most fixed `delta_cap`.
- Check reported `num_edges()` matches adjacency contents.
- Check coloring vector size equals `num_vertices()`.
- For exact algorithms, check every vertex has a valid color and every edge has differently colored endpoints.
- Provide concise failure messages that include the offending vertex or edge.
- Provide batch preflight validation helpers so tests can confirm rejected updates leave graph state unchanged.

## Test Responsibilities

Foundation tests should prove the shared infrastructure rejects bad states and supports future algorithm stages.

- Build and run through CTest.
- Avoid external test frameworks unless explicitly approved; use small assertion-based executables.
- Keep each test deterministic and print the RNG seed when fuzzing.
- Verify graph construction, update validation, batch atomicity, and validator failures.
- Verify RNG reproducibility from fixed seeds.
- Verify benchmark smoke binaries build and run quickly.

## Benchmark Hooks And Metrics

Add a minimal benchmark harness that future stages can reuse.

Benchmark hooks:

- Deterministic graph/update generation using the shared RNG wrapper.
- Command-line options for seed, vertex count, update count, `delta_cap`, batch size, and output format.
- A simple timer helper based on `std::chrono::steady_clock`.
- Structured metric emission as line-oriented text, preferably key-value pairs for easy parsing.

Metrics to record:

- `engine_name`
- `seed`
- `num_vertices`
- `initial_edges`
- `final_edges`
- `delta_cap`
- `updates_requested`
- `updates_applied`
- `updates_rejected`
- `batch_size`
- `build_seconds`
- `update_seconds`
- `validate_seconds`
- `throughput_updates_per_second`
- `max_degree_observed`
- `proper_coloring_validated` when an exact engine is benchmarked later

Foundation smoke benchmark:

- Use only graph/update generation and graph validation.
- Do not call coloring logic.
- Confirm deterministic seed logging and basic metric output.

## Invariants Before And After The Stage

Before the stage:

- Repository may be empty.
- No build, test, graph, update, validation, RNG, or benchmark infrastructure exists.

After the stage:

- Project builds with C++17.
- ParlayLib is configured without adding new external dependencies.
- `delta_cap` is fixed per `GraphStore` instance and cannot be changed after construction.
- Graphs reject loops.
- Graphs reject duplicate undirected edges.
- Graphs reject updates that would violate `delta_cap`.
- Invalid updates and invalid batches do not partially mutate graph state.
- Validators can detect graph invariant violations and improper exact colorings.
- RNG output is reproducible from a logged deterministic seed.
- Tests and benchmark smoke targets are available from the beginning.
- No coloring algorithm logic has been implemented.

## Exact Implementation Steps

1. Create the minimal repository layout and root `CMakeLists.txt`.
2. Add C++17 compiler settings, warnings, and options for tests and benchmark smoke targets.
3. Add ParlayLib include discovery using the project’s expected local setup; if ParlayLib is not present, emit a clear CMake configuration message rather than adding a dependency.
4. Define common types in `include/dgcolor/types.hpp`.
5. Define `EdgeUpdate`, `UpdateBatch`, update statuses, and stats structs in `include/dgcolor/batch.hpp`.
6. Define the `GraphStore` interface and a simple adjacency-backed implementation declaration.
7. Implement graph construction, edge normalization, `has_edge`, `degree`, `neighbors`, and edge count reporting.
8. Implement single-update validation and mutation with loop, duplicate, missing-edge, invalid-vertex, and `delta_cap` checks.
9. Implement batch preflight validation against a temporary state, followed by phase-separated mutation only if the full batch is valid.
10. Define the `ColoringEngine` interface with initialization, single update, batch update, color inspection, and graph inspection methods.
11. Add deterministic RNG wrapper with explicit seed construction and seed-printing helper.
12. Implement graph validators and exact coloring validators.
13. Add assertion-based tests for graph storage and update rejection.
14. Add validator tests, including deliberately invalid graph/coloring cases where possible through test hooks or controlled fixtures.
15. Add RNG reproducibility tests.
16. Add benchmark smoke harness with deterministic update generation and key-value metric output.
17. Add README build, test, and benchmark commands.
18. Run the narrowest relevant tests first, then the full Foundation test set and benchmark smoke target.

## Small-Graph Correctness Tests And Fuzz Checks

Small deterministic tests:

- Construct an empty graph with `n = 0`, `n = 1`, and several small `n`.
- Insert a valid edge and verify symmetry, degrees, edge count, and validator success.
- Reject loop insertion.
- Reject duplicate insertion in both endpoint orders.
- Reject deletion of a missing edge.
- Delete an existing edge and verify both adjacency lists are updated.
- Reject insertion when either endpoint would exceed `delta_cap`.
- Reject invalid vertex ids.
- Reject a batch containing a loop without mutating earlier valid-looking updates.
- Reject a batch containing duplicate insertions.
- Reject a batch whose combined inserts would exceed `delta_cap`.
- Accept a valid mixed insert/delete batch and verify final graph invariants.
- Validate a proper coloring on a small path/cycle.
- Reject an improper coloring where adjacent vertices share a color.
- Reject coloring vectors with the wrong length or `kUncolored` for exact validation.

Fuzz checks:

- For seeds such as `1`, `2`, `3`, `12345`, and a documented default seed, generate small random update streams.
- Maintain a simple slow reference set of normalized edges and degrees inside the test.
- Compare `GraphStore` results against the reference after every accepted update.
- Confirm rejected updates leave `GraphStore` unchanged.
- Print the seed before each fuzz run.
- Keep fuzz sizes small enough for fast local execution, for example `n <= 32` and `updates <= 1000`.

## Benchmark Smoke Tests

- Build `bench_smoke`.
- Run with a tiny deterministic workload, for example `--seed 1 --vertices 32 --updates 100 --delta-cap 8 --batch-size 16`.
- Verify process exit code is zero.
- Verify output includes `seed`, `num_vertices`, `delta_cap`, `updates_requested`, `updates_applied`, `updates_rejected`, and timing fields.
- Verify benchmark invokes graph validation at least once.
- Keep runtime short enough for normal development iteration.

## Risks And Fallback Simplifications

- ParlayLib location may be unknown in an empty repository. Fallback: support `PARLAYLIB_DIR` or `CMAKE_PREFIX_PATH` and document the expected setup without vendoring ParlayLib.
- Batch validation can become complex if mixed inserts and deletes target the same edge. Fallback: reject ambiguous same-batch conflicts in Foundation and document that later stages may relax this with a clearer batch semantics model.
- A high-performance adjacency structure is unnecessary at this stage. Fallback: use sorted vectors or sets for clarity, then replace behind `GraphStore` later if benchmarks justify it.
- Validator access to deliberately invalid graph states may be hard through public APIs. Fallback: validate public invariants through rejected updates and use small controlled test fixtures only if needed.
- Command-line parsing can grow distracting. Fallback: implement a tiny local parser for `--key value` pairs and keep benchmark options minimal.

## Acceptance Criteria

Foundation is complete when:

- The repository has the planned build, include, source, test, benchmark, and docs structure.
- The project configures and builds in C++17 with ParlayLib include support.
- Common types, `GraphStore`, `ColoringEngine`, validators, RNG wrapper, and benchmark hooks exist.
- Graph updates enforce fixed `delta_cap`, no loops, no duplicates, and no partial mutation on rejected updates or batches.
- Small graph tests and deterministic fuzz checks pass.
- Benchmark smoke target builds, runs, logs its seed, and emits the required metrics.
- No coloring algorithm logic has been added.
- The post-stage summary lists touched files, tests run, and any remaining risks.
