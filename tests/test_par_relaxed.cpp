#include <cassert>
#include <stdexcept>
#include <vector>

#include "dgcolor/par_relaxed_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::Color;
using dgcolor::BatchStats;
using dgcolor::EdgeUpdate;
using dgcolor::ParRelaxedEngine;
using dgcolor::UpdateStats;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::VertexId;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

EdgeUpdate Delete(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Delete, u, v, 0};
}

void AssertColorRange(const ParRelaxedEngine& engine) {
  const auto all_colors = engine.colors();
  const Color limit = engine.palette_size();
  for (Color c : all_colors) {
    assert(c < limit);
  }
}

void AssertInitializedColoringValid(const ParRelaxedEngine& engine) {
  const auto graph_validation = dgcolor::validate_graph_invariants(engine.graph());
  assert(graph_validation.ok);
  const auto validation = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
  assert(validation.ok);
  AssertColorRange(engine);
}

void AssertStatsRejected(const UpdateStats& stats) {
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(stats.seconds >= 0.0);
}

void AssertStatsApplied(const UpdateStats& stats, std::size_t expected_vertices_touched) {
  assert(stats.applied);
  assert(stats.edges_changed == 1);
  assert(stats.vertices_touched == expected_vertices_touched);
  assert(stats.seconds >= 0.0);
}

void AssertBatchStatsRejected(const BatchStats& stats, std::size_t expected_updates) {
  assert(!stats.applied);
  assert(stats.updates == expected_updates);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(stats.seconds >= 0.0);
}

void AssertBatchStatsApplied(const BatchStats& stats, std::size_t expected_updates,
                             std::size_t expected_edges_changed,
                             std::size_t expected_vertices_touched) {
  assert(stats.applied);
  assert(stats.updates == expected_updates);
  assert(stats.edges_changed == expected_edges_changed);
  assert(stats.vertices_touched == expected_vertices_touched);
  assert(stats.seconds >= 0.0);
}

UpdateBatch PathEdges() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  return edges;
}

void TestInitializeEmptyGraph() {
  ParRelaxedEngine engine(0, 0, 1, 2, 8);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
  assert(engine.palette_multiplier() == 2);
  assert(engine.palette_size() == 1);
  assert(engine.max_rounds() == 8);
  assert(engine.total_rounds() == 0);
  assert(engine.fallback_count() == 0);
  assert(engine.vertices_touched_total() == 0);
}

void TestInitializeNoEdges() {
  ParRelaxedEngine engine(6, 4, 7, 2, 8);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
  assert(engine.palette_size() == 9);
  assert(engine.total_rounds() == 0);
  assert(engine.fallback_count() == 0);
  assert(engine.vertices_touched_total() == 6);
}

void TestInitializePathGraph() {
  ParRelaxedEngine engine(5, 3, 11, 2, 8, PathEdges());
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
}

void TestInitializeCycleGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  edges.push_back(Insert(4, 0));

  ParRelaxedEngine engine(5, 3, 13, 4, 16, edges);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
}

void TestInitializeStarGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(0, 3));
  edges.push_back(Insert(0, 4));
  edges.push_back(Insert(0, 5));

  ParRelaxedEngine engine(6, 5, 17, 2, 8, edges);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
}

void TestInitializeNearDeltaCapGraph() {
  // K4 where each vertex has degree 3, matching delta_cap.
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(0, 3));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(1, 3));
  edges.push_back(Insert(2, 3));

  ParRelaxedEngine engine(4, 3, 23, 2, 8, edges);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
}

void TestInvalidConstructorArgs() {
  bool threw_c = false;
  try {
    ParRelaxedEngine engine(4, 3, 1, 0, 8);
    (void)engine;
  } catch (const std::invalid_argument&) {
    threw_c = true;
  }
  assert(threw_c);

  bool threw_rounds = false;
  try {
    ParRelaxedEngine engine(4, 3, 1, 2, 0);
    (void)engine;
  } catch (const std::invalid_argument&) {
    threw_rounds = true;
  }
  assert(threw_rounds);
}

void TestApplyUpdateBeforeInitializeThrows() {
  ParRelaxedEngine engine(4, 3, 1, 2, 8);

  bool update_threw = false;
  try {
    (void)engine.apply_update(Insert(0, 1));
  } catch (const std::logic_error&) {
    update_threw = true;
  }
  assert(update_threw);
}

void TestInsertionWithoutConflict() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8, PathEdges());
  engine.initialize_coloring();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  const auto touched_before = engine.vertices_touched_total();

  const UpdateStats stats = engine.apply_update(Insert(0, 3));
  AssertStatsApplied(stats, 0);
  AssertInitializedColoringValid(engine);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
  assert(engine.vertices_touched_total() == touched_before);
}

void TestInsertionWithConflictRequiresRecolor() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  ParRelaxedEngine engine(3, 2, 1, 2, 8, edges);
  engine.initialize_coloring();
  const auto before = engine.colors();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  const auto touched_before = engine.vertices_touched_total();

  const UpdateStats stats = engine.apply_update(Insert(0, 2));
  assert(stats.applied);
  assert(stats.edges_changed == 1);
  assert(stats.vertices_touched >= 1);
  assert(stats.seconds >= 0.0);
  AssertInitializedColoringValid(engine);

  const auto after = engine.colors();
  assert(before != after);
  assert(engine.total_rounds() > rounds_before || engine.fallback_count() > fallbacks_before);
  assert(engine.vertices_touched_total() == touched_before + stats.vertices_touched);
}

void TestDeletionPreservesValidityAndColors() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  const auto touched_before = engine.vertices_touched_total();

  const UpdateStats stats = engine.apply_update(Delete(2, 3));
  AssertStatsApplied(stats, 0);
  AssertInitializedColoringValid(engine);
  assert(engine.colors() == before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
  assert(engine.vertices_touched_total() == touched_before);
}

void TestRejectedLoopInsertionStable() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  const auto touched_before = engine.vertices_touched_total();

  const UpdateStats stats = engine.apply_update(Insert(1, 1));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
  assert(engine.vertices_touched_total() == touched_before);
}

void TestRejectedDuplicateInsertionStable() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  const auto touched_before = engine.vertices_touched_total();

  const UpdateStats stats = engine.apply_update(Insert(1, 2));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
  assert(engine.vertices_touched_total() == touched_before);
}

void TestRejectedMissingDeletionStable() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  const auto touched_before = engine.vertices_touched_total();

  const UpdateStats stats = engine.apply_update(Delete(0, 4));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
  assert(engine.vertices_touched_total() == touched_before);
}

void TestRejectedDegreeCapInsertionStable() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(1, 2));
  ParRelaxedEngine engine(4, 2, 1, 2, 8, edges);
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  const auto touched_before = engine.vertices_touched_total();

  const UpdateStats stats = engine.apply_update(Insert(0, 3));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
  assert(engine.vertices_touched_total() == touched_before);
}

void TestApplyBatchBeforeInitializeThrows() {
  ParRelaxedEngine engine(4, 3, 1, 2, 8);
  bool batch_threw = false;
  try {
    UpdateBatch batch;
    batch.push_back(Insert(0, 1));
    (void)engine.apply_batch(batch);
  } catch (const std::logic_error&) {
    batch_threw = true;
  }
  assert(batch_threw);
}

void TestAcceptedInsertionBatch() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8);
  engine.initialize_coloring();
  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  batch.push_back(Insert(2, 3));

  const auto touched_before = engine.vertices_touched_total();
  const BatchStats stats = engine.apply_batch(batch);
  // Step 6C has not wired batch repair yet, so accepted batches still full-recolor.
  // Do not change this expectation back to 0 before Step 6D wires repair into apply_batch().
  AssertBatchStatsApplied(stats, 2, 2, 5);
  AssertInitializedColoringValid(engine);
  assert(engine.vertices_touched_total() == touched_before + 5);
}

void TestAcceptedMixedInsertDeleteBatch() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8, PathEdges());
  engine.initialize_coloring();
  UpdateBatch batch;
  batch.push_back(Delete(1, 2));
  batch.push_back(Insert(0, 4));

  const BatchStats stats = engine.apply_batch(batch);
  AssertBatchStatsApplied(stats, 2, 2, 5);
  AssertInitializedColoringValid(engine);
}

void TestAcceptedConflictBatchRequiresRecolor() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  ParRelaxedEngine engine(3, 2, 1, 2, 8, edges);
  engine.initialize_coloring();
  const auto before = engine.colors();

  UpdateBatch batch;
  batch.push_back(Insert(0, 2));
  const BatchStats stats = engine.apply_batch(batch);
  AssertBatchStatsApplied(stats, 1, 1, 3);
  AssertInitializedColoringValid(engine);
  assert(engine.colors() != before);
}

void TestRejectedDuplicateSameBatchEdgeStable() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8);
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  batch.push_back(Insert(1, 0));

  const BatchStats stats = engine.apply_batch(batch);
  AssertBatchStatsRejected(stats, 2);
  assert(engine.colors() == before);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
}

void TestRejectedLoopBatchStable() {
  ParRelaxedEngine engine(5, 3, 1, 2, 8);
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  UpdateBatch batch;
  batch.push_back(Insert(2, 2));

  const BatchStats stats = engine.apply_batch(batch);
  AssertBatchStatsRejected(stats, 1);
  assert(engine.colors() == before);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
}

void TestRejectedDegreeCapBatchStable() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(1, 2));
  ParRelaxedEngine engine(4, 2, 1, 2, 8, edges);
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();
  UpdateBatch batch;
  batch.push_back(Insert(0, 3));

  const BatchStats stats = engine.apply_batch(batch);
  AssertBatchStatsRejected(stats, 1);
  assert(engine.colors() == before);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.total_rounds() == rounds_before);
  assert(engine.fallback_count() == fallbacks_before);
}

void TestBatchStep3StatsRemainFallbackOnly() {
  ParRelaxedEngine engine(4, 3, 1, 2, 8);
  engine.initialize_coloring();
  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  (void)engine.apply_batch(batch);
  assert(engine.total_rounds() == 0);
  assert(engine.fallback_count() == 0);
}

void TestMaxRoundsOneStillPreservesCorrectness() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  ParRelaxedEngine engine(3, 2, 1, 2, 1, edges);
  engine.initialize_coloring();
  const auto rounds_before = engine.total_rounds();
  const auto fallbacks_before = engine.fallback_count();

  const UpdateStats stats = engine.apply_update(Insert(0, 2));
  assert(stats.applied);
  assert(stats.edges_changed == 1);
  AssertInitializedColoringValid(engine);
  assert(engine.total_rounds() >= rounds_before + 1);
  assert(engine.fallback_count() >= fallbacks_before);
}

void TestRepeatedSameSeedUpdateDeterminism() {
  const UpdateBatch initial = PathEdges();
  ParRelaxedEngine a(8, 4, 12345, 4, 4, initial);
  ParRelaxedEngine b(8, 4, 12345, 4, 4, initial);
  a.initialize_coloring();
  b.initialize_coloring();

  const std::vector<EdgeUpdate> updates = {
      Insert(0, 4), Delete(1, 2), Insert(2, 6), Insert(3, 7), Delete(0, 1)};
  for (const EdgeUpdate& update : updates) {
    const UpdateStats stats_a = a.apply_update(update);
    const UpdateStats stats_b = b.apply_update(update);
    assert(stats_a.applied == stats_b.applied);
    assert(stats_a.edges_changed == stats_b.edges_changed);
    assert(stats_a.vertices_touched == stats_b.vertices_touched);
  }

  AssertInitializedColoringValid(a);
  AssertInitializedColoringValid(b);
  assert(a.colors() == b.colors());
  assert(a.total_rounds() == b.total_rounds());
  assert(a.fallback_count() == b.fallback_count());
  assert(a.vertices_touched_total() == b.vertices_touched_total());
}

}  // namespace

int main() {
  TestInitializeEmptyGraph();
  TestInitializeNoEdges();
  TestInitializePathGraph();
  TestInitializeCycleGraph();
  TestInitializeStarGraph();
  TestInitializeNearDeltaCapGraph();
  TestInvalidConstructorArgs();
  TestApplyUpdateBeforeInitializeThrows();
  TestInsertionWithoutConflict();
  TestInsertionWithConflictRequiresRecolor();
  TestDeletionPreservesValidityAndColors();
  TestRejectedLoopInsertionStable();
  TestRejectedDuplicateInsertionStable();
  TestRejectedMissingDeletionStable();
  TestRejectedDegreeCapInsertionStable();
  TestApplyBatchBeforeInitializeThrows();
  TestAcceptedInsertionBatch();
  TestAcceptedMixedInsertDeleteBatch();
  TestAcceptedConflictBatchRequiresRecolor();
  TestRejectedDuplicateSameBatchEdgeStable();
  TestRejectedLoopBatchStable();
  TestRejectedDegreeCapBatchStable();
  TestBatchStep3StatsRemainFallbackOnly();
  TestMaxRoundsOneStillPreservesCorrectness();
  TestRepeatedSameSeedUpdateDeterminism();
  return 0;
}
