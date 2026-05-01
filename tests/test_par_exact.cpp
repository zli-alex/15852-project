#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "dgcolor/par_exact_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::BatchStats;
using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::ParExactEngine;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateStats;
using dgcolor::ValidationResult;
using dgcolor::VertexId;

constexpr std::uint32_t kMaxRounds = 4;

struct DiagSnapshot {
  std::uint64_t active_vertices_total;
  std::uint64_t repair_rounds_total;
  std::uint64_t fallback_count;
  std::uint64_t proposal_count;
  std::uint64_t commit_count;
  std::uint64_t unresolved_count;
  std::uint64_t sequential_fast_path_count;
  std::uint64_t vertices_touched_total;
};

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

EdgeUpdate Delete(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Delete, u, v, 0};
}

DiagSnapshot SnapshotDiag(const ParExactEngine& engine) {
  return DiagSnapshot{engine.active_vertices_total(),
                      engine.repair_rounds_total(),
                      engine.fallback_count(),
                      engine.proposal_count(),
                      engine.commit_count(),
                      engine.unresolved_count(),
                      engine.sequential_fast_path_count(),
                      engine.vertices_touched_total()};
}

bool SameDiag(const DiagSnapshot& lhs, const DiagSnapshot& rhs) {
  return lhs.active_vertices_total == rhs.active_vertices_total &&
         lhs.repair_rounds_total == rhs.repair_rounds_total &&
         lhs.fallback_count == rhs.fallback_count && lhs.proposal_count == rhs.proposal_count &&
         lhs.commit_count == rhs.commit_count && lhs.unresolved_count == rhs.unresolved_count &&
         lhs.sequential_fast_path_count == rhs.sequential_fast_path_count &&
         lhs.vertices_touched_total == rhs.vertices_touched_total;
}

std::vector<std::uint64_t> SnapshotEdgeKeys(const dgcolor::GraphStore& graph) {
  std::vector<std::uint64_t> keys;
  const VertexId n = graph.num_vertices();
  for (VertexId u = 0; u < n; ++u) {
    for (VertexId v = static_cast<VertexId>(u + 1); v < n; ++v) {
      if (graph.has_edge(u, v)) {
        keys.push_back((static_cast<std::uint64_t>(u) << 32U) | static_cast<std::uint64_t>(v));
      }
    }
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

void AssertColoringValid(const ParExactEngine& engine) {
  const ValidationResult graph_validation = dgcolor::validate_graph_invariants(engine.graph());
  assert(graph_validation.ok);
  const ValidationResult coloring_validation =
      dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
  assert(coloring_validation.ok);
  for (Color c : engine.colors()) {
    assert(c < engine.palette_size());
  }
}

void TestMaxRoundsZeroThrows() {
  bool threw = false;
  try {
    (void)ParExactEngine(4, 3, 1, 0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

void TestApplyUpdateBeforeInitializeThrows() {
  ParExactEngine engine(4, 3, 1, kMaxRounds);
  bool threw = false;
  try {
    (void)engine.apply_update(Insert(0, 1));
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw);
}

void TestApplyBatchBeforeInitializeThrows() {
  ParExactEngine engine(4, 3, 1, kMaxRounds);
  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  bool threw = false;
  try {
    (void)engine.apply_batch(batch);
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw);
}

void TestAcceptedNoConflictInsertionUpdateDoesNotFallback() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));  // deterministic init gives color(0)=0, color(1)=1
  ParExactEngine engine(4, 3, 5, kMaxRounds, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  const UpdateStats stats = engine.apply_update(Insert(2, 1));
  assert(stats.applied);
  assert(stats.edges_changed == 1);
  assert(stats.vertices_touched == 0);
  assert(engine.graph().num_edges() == 2);
  AssertColoringValid(engine);
  assert(engine.colors() == colors_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestAcceptedConflictInsertionUpdateValidatesAndTracksRepair() {
  ParExactEngine engine(4, 3, 6, kMaxRounds);
  engine.initialize_coloring();
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  // Initially all vertices are color 0 in empty graph; this insert creates a direct conflict.
  const UpdateStats stats = engine.apply_update(Insert(0, 1));
  assert(stats.applied);
  assert(stats.edges_changed == 1);
  AssertColoringValid(engine);
  assert(engine.active_vertices_total() >= diag_before.active_vertices_total + 1);
  assert(engine.repair_rounds_total() >= diag_before.repair_rounds_total + 1);
  assert(engine.proposal_count() >= diag_before.proposal_count + 1);
  assert(engine.vertices_touched_total() >= diag_before.vertices_touched_total + 1);
  assert(engine.fallback_count() == diag_before.fallback_count ||
         engine.fallback_count() == diag_before.fallback_count + 1);
}

void TestAcceptedDeletionUpdateKeepsColorsAndDiagnostics() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  ParExactEngine engine(4, 3, 7, kMaxRounds, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const DiagSnapshot diag_before = SnapshotDiag(engine);
  const std::size_t edges_before = engine.graph().num_edges();

  const UpdateStats stats = engine.apply_update(Delete(0, 1));
  assert(stats.applied);
  assert(stats.edges_changed == 1);
  assert(stats.vertices_touched == 0);
  assert(engine.graph().num_edges() == edges_before - 1);
  assert(engine.colors() == colors_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
  AssertColoringValid(engine);
}

void TestRejectedLoopInsertionPreservesState() {
  ParExactEngine engine(4, 3, 9, kMaxRounds);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const auto edges_before = SnapshotEdgeKeys(engine.graph());
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  const UpdateStats stats = engine.apply_update(Insert(0, 0));
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(engine.colors() == colors_before);
  assert(SnapshotEdgeKeys(engine.graph()) == edges_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestRejectedDuplicateInsertionPreservesState() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  ParExactEngine engine(4, 3, 10, kMaxRounds, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const auto edges_before = SnapshotEdgeKeys(engine.graph());
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  const UpdateStats stats = engine.apply_update(Insert(0, 1));
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(engine.colors() == colors_before);
  assert(SnapshotEdgeKeys(engine.graph()) == edges_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestRejectedMissingDeletionPreservesState() {
  ParExactEngine engine(4, 3, 11, kMaxRounds);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const auto edges_before = SnapshotEdgeKeys(engine.graph());
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  const UpdateStats stats = engine.apply_update(Delete(0, 1));
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(engine.colors() == colors_before);
  assert(SnapshotEdgeKeys(engine.graph()) == edges_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestAcceptedBatchValidStyleInsertionBatchNoConflictSkipsRepair() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(2, 3));
  initial.push_back(Insert(4, 5));
  ParExactEngine engine(6, 5, 13, kMaxRounds, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  UpdateBatch batch;
  // Both inserts connect opposite-color endpoints, so no recolor is needed.
  batch.push_back(Insert(0, 3));
  batch.push_back(Insert(2, 5));
  const BatchStats stats = engine.apply_batch(batch);
  assert(stats.applied);
  assert(stats.updates == batch.size());
  assert(stats.edges_changed == batch.size());
  assert(stats.vertices_touched == 0);
  AssertColoringValid(engine);
  assert(engine.colors() == colors_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestAcceptedConflictHeavyStyleInsertionBatchTracksRepair() {
  ParExactEngine engine(6, 5, 13, kMaxRounds);
  engine.initialize_coloring();
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  UpdateBatch batch;
  // Conflict-heavy style: endpoints start with equal color in empty graph.
  batch.push_back(Insert(0, 1));
  batch.push_back(Insert(2, 3));
  const BatchStats stats = engine.apply_batch(batch);
  assert(stats.applied);
  assert(stats.updates == batch.size());
  assert(stats.edges_changed == batch.size());
  AssertColoringValid(engine);
  assert(engine.active_vertices_total() >= diag_before.active_vertices_total + 1);
  assert(engine.repair_rounds_total() >= diag_before.repair_rounds_total + 1);
  assert(engine.proposal_count() >= diag_before.proposal_count + 1);
  assert(engine.vertices_touched_total() >= diag_before.vertices_touched_total + 1);
  assert(engine.fallback_count() == diag_before.fallback_count ||
         engine.fallback_count() == diag_before.fallback_count + 1);
}

void TestAcceptedMixedBatchValidatesAndMayFallback() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(2, 3));
  ParExactEngine engine(6, 5, 14, kMaxRounds, initial);
  engine.initialize_coloring();
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  UpdateBatch batch;
  batch.push_back(Delete(0, 1));
  batch.push_back(Insert(4, 5));
  const BatchStats stats = engine.apply_batch(batch);
  assert(stats.applied);
  assert(stats.edges_changed == batch.size());
  AssertColoringValid(engine);
  assert(engine.fallback_count() == diag_before.fallback_count ||
         engine.fallback_count() == diag_before.fallback_count + 1);
  assert(engine.vertices_touched_total() >= diag_before.vertices_touched_total);
}

void TestAcceptedDeletionOnlyBatchKeepsColorsAndDiagnostics() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(1, 2));
  ParExactEngine engine(5, 4, 15, kMaxRounds, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  UpdateBatch batch;
  batch.push_back(Delete(0, 1));
  batch.push_back(Delete(1, 2));
  const BatchStats stats = engine.apply_batch(batch);
  assert(stats.applied);
  assert(stats.edges_changed == batch.size());
  assert(stats.vertices_touched == 0);
  assert(engine.colors() == colors_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
  AssertColoringValid(engine);
}

void TestRejectedDuplicateSameBatchEdgePreservesState() {
  ParExactEngine engine(5, 4, 16, kMaxRounds);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const auto edges_before = SnapshotEdgeKeys(engine.graph());
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  batch.push_back(Insert(1, 0));
  const BatchStats stats = engine.apply_batch(batch);
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(engine.colors() == colors_before);
  assert(SnapshotEdgeKeys(engine.graph()) == edges_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestRejectedLoopBatchPreservesState() {
  ParExactEngine engine(5, 4, 17, kMaxRounds);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const auto edges_before = SnapshotEdgeKeys(engine.graph());
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  UpdateBatch batch;
  batch.push_back(Insert(2, 2));
  const BatchStats stats = engine.apply_batch(batch);
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(engine.colors() == colors_before);
  assert(SnapshotEdgeKeys(engine.graph()) == edges_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestRejectedDegreeCapBatchPreservesState() {
  // delta_cap=1 cannot accept two incident inserts on the same vertex in one batch.
  ParExactEngine engine(3, 1, 18, kMaxRounds);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const auto edges_before = SnapshotEdgeKeys(engine.graph());
  const DiagSnapshot diag_before = SnapshotDiag(engine);

  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  batch.push_back(Insert(0, 2));
  const BatchStats stats = engine.apply_batch(batch);
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(engine.colors() == colors_before);
  assert(SnapshotEdgeKeys(engine.graph()) == edges_before);
  assert(SameDiag(diag_before, SnapshotDiag(engine)));
}

void TestDeterministicSameSeedSequence() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(2, 3));

  ParExactEngine lhs(6, 5, 21, kMaxRounds, initial);
  ParExactEngine rhs(6, 5, 21, kMaxRounds, initial);
  lhs.initialize_coloring();
  rhs.initialize_coloring();

  const std::vector<EdgeUpdate> updates = {
      Insert(4, 5),
      Delete(0, 1),
      Insert(0, 2),
      Delete(2, 3),
  };
  for (const EdgeUpdate& update : updates) {
    const UpdateStats l = lhs.apply_update(update);
    const UpdateStats r = rhs.apply_update(update);
    assert(l.applied == r.applied);
    assert(l.edges_changed == r.edges_changed);
    assert(l.vertices_touched == r.vertices_touched);
  }

  UpdateBatch batch;
  batch.push_back(Insert(1, 3));
  batch.push_back(Delete(4, 5));
  const BatchStats lb = lhs.apply_batch(batch);
  const BatchStats rb = rhs.apply_batch(batch);
  assert(lb.applied == rb.applied);
  assert(lb.edges_changed == rb.edges_changed);
  assert(lb.vertices_touched == rb.vertices_touched);

  assert(lhs.colors() == rhs.colors());
  assert(SnapshotEdgeKeys(lhs.graph()) == SnapshotEdgeKeys(rhs.graph()));
  assert(SameDiag(SnapshotDiag(lhs), SnapshotDiag(rhs)));
}

void TestInitialBatchInvalidThrows() {
  UpdateBatch bad;
  bad.push_back(Insert(0, 0));
  bool threw = false;
  try {
    (void)ParExactEngine(4, 3, 1, kMaxRounds, bad);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

int main() {
  TestMaxRoundsZeroThrows();
  TestApplyUpdateBeforeInitializeThrows();
  TestApplyBatchBeforeInitializeThrows();
  TestAcceptedNoConflictInsertionUpdateDoesNotFallback();
  TestAcceptedConflictInsertionUpdateValidatesAndTracksRepair();
  TestAcceptedDeletionUpdateKeepsColorsAndDiagnostics();
  TestRejectedLoopInsertionPreservesState();
  TestRejectedDuplicateInsertionPreservesState();
  TestRejectedMissingDeletionPreservesState();
  TestAcceptedBatchValidStyleInsertionBatchNoConflictSkipsRepair();
  TestAcceptedConflictHeavyStyleInsertionBatchTracksRepair();
  TestAcceptedMixedBatchValidatesAndMayFallback();
  TestAcceptedDeletionOnlyBatchKeepsColorsAndDiagnostics();
  TestRejectedDuplicateSameBatchEdgePreservesState();
  TestRejectedLoopBatchPreservesState();
  TestRejectedDegreeCapBatchPreservesState();
  TestDeterministicSameSeedSequence();
  TestInitialBatchInvalidThrows();
  return 0;
}
