#include <cassert>
#include <stdexcept>

#include "dgcolor/seq_exact_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::SeqExactEngine;
using dgcolor::UpdateStats;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::ValidationResult;
using dgcolor::VertexId;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

EdgeUpdate Delete(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Delete, u, v, 0};
}

void AssertStatsAreZero(const SeqExactEngine& engine) {
  assert(engine.recolor_calls() == 0);
  assert(engine.recolored_vertices_total() == 0);
  assert(engine.cascade_steps_total() == 0);
  assert(engine.full_fallback_count() == 0);
  assert(engine.level_conflict_choices() == 0);
}

void AssertColoringValid(const SeqExactEngine& engine) {
  const ValidationResult graph_validation = dgcolor::validate_graph_invariants(engine.graph());
  assert(graph_validation.ok);
  const ValidationResult coloring_validation =
      dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
  assert(coloring_validation.ok);

  const auto all_colors = engine.colors();
  for (Color c : all_colors) {
    assert(c < engine.palette_size());
  }
}

void AssertRejectedStats(const UpdateStats& stats) {
  assert(!stats.applied);
  assert(stats.edges_changed == 0);
  assert(stats.vertices_touched == 0);
  assert(stats.seconds >= 0.0);
}

void AssertAcceptedStats(const UpdateStats& stats) {
  assert(stats.applied);
  assert(stats.edges_changed == 1);
  assert(stats.seconds >= 0.0);
}

void TestInitializeEmptyGraph() {
  SeqExactEngine engine(0, 0, 1);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertStatsAreZero(engine);
  assert(engine.palette_size() == 1);
}

void TestInitializeNoEdges() {
  SeqExactEngine engine(6, 4, 7);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertStatsAreZero(engine);
}

void TestInitializePathGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  SeqExactEngine engine(5, 3, 11, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertStatsAreZero(engine);
}

void TestInitializeCycleGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  edges.push_back(Insert(4, 0));
  SeqExactEngine engine(5, 3, 13, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertStatsAreZero(engine);
}

void TestInitializeStarGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(0, 3));
  edges.push_back(Insert(0, 4));
  edges.push_back(Insert(0, 5));
  SeqExactEngine engine(6, 5, 17, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertStatsAreZero(engine);
}

void TestInitializeNearDeltaCapGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(0, 3));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(1, 3));
  edges.push_back(Insert(2, 3));
  SeqExactEngine engine(4, 3, 19, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertStatsAreZero(engine);
}

void TestApplyUpdateBeforeInitializeThrows() {
  SeqExactEngine engine(4, 3, 23);
  bool threw = false;
  try {
    (void)engine.apply_update(Insert(0, 1));
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw);
}

void TestDeleteBeforeInitializeThrows() {
  SeqExactEngine engine(4, 3, 23);
  bool threw = false;
  try {
    (void)engine.apply_update(Delete(0, 1));
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw);
}

void TestAcceptedInsertionNoConflict() {
  UpdateBatch initial;
  initial.push_back(Insert(1, 2));  // Produces colors [0,1,0,0] on 4 vertices.
  SeqExactEngine engine(4, 3, 29, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const std::uint64_t recolor_calls_before = engine.recolor_calls();
  const std::uint64_t recolored_vertices_before = engine.recolored_vertices_total();
  const std::uint64_t cascade_steps_before = engine.cascade_steps_total();
  const std::uint64_t fallback_before = engine.full_fallback_count();
  const std::uint64_t level_choices_before = engine.level_conflict_choices();

  const UpdateStats stats = engine.apply_update(Insert(0, 1));
  AssertAcceptedStats(stats);
  assert(stats.vertices_touched == 0);
  assert(engine.graph().num_edges() == edges_before + 1);
  AssertColoringValid(engine);
  assert(engine.recolor_calls() == recolor_calls_before);
  assert(engine.recolored_vertices_total() == recolored_vertices_before);
  assert(engine.cascade_steps_total() == cascade_steps_before);
  assert(engine.full_fallback_count() == fallback_before);
  assert(engine.level_conflict_choices() == level_choices_before);
  assert(colors_before[0] == engine.colors()[0]);
}

void TestAcceptedInsertionWithConflictRepairs() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(1, 2));
  SeqExactEngine engine(3, 2, 31, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const std::uint64_t recolor_calls_before = engine.recolor_calls();
  const std::uint64_t recolored_vertices_before = engine.recolored_vertices_total();
  const std::uint64_t cascade_steps_before = engine.cascade_steps_total();

  const UpdateStats stats = engine.apply_update(Insert(0, 2));
  AssertAcceptedStats(stats);
  assert(stats.vertices_touched >= 1);
  AssertColoringValid(engine);
  assert(engine.recolor_calls() == recolor_calls_before + 1);
  assert(engine.recolored_vertices_total() >= recolored_vertices_before + 1);
  assert(engine.cascade_steps_total() >= cascade_steps_before + 1);
  assert(engine.full_fallback_count() == 0);
  assert(engine.colors() != colors_before);
}

void TestRejectedLoopInsertionStable() {
  SeqExactEngine engine(5, 3, 37);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const std::uint64_t recolor_calls_before = engine.recolor_calls();
  const std::uint64_t recolored_vertices_before = engine.recolored_vertices_total();
  const std::uint64_t cascade_steps_before = engine.cascade_steps_total();
  const std::uint64_t fallback_before = engine.full_fallback_count();
  const std::uint64_t level_choices_before = engine.level_conflict_choices();

  const UpdateStats stats = engine.apply_update(Insert(1, 1));
  AssertRejectedStats(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == colors_before);
  assert(engine.recolor_calls() == recolor_calls_before);
  assert(engine.recolored_vertices_total() == recolored_vertices_before);
  assert(engine.cascade_steps_total() == cascade_steps_before);
  assert(engine.full_fallback_count() == fallback_before);
  assert(engine.level_conflict_choices() == level_choices_before);
}

void TestRejectedDuplicateInsertionStable() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  SeqExactEngine engine(4, 3, 41, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const std::uint64_t recolor_calls_before = engine.recolor_calls();
  const std::uint64_t recolored_vertices_before = engine.recolored_vertices_total();
  const std::uint64_t cascade_steps_before = engine.cascade_steps_total();
  const std::uint64_t fallback_before = engine.full_fallback_count();
  const std::uint64_t level_choices_before = engine.level_conflict_choices();

  const UpdateStats stats = engine.apply_update(Insert(1, 0));
  AssertRejectedStats(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == colors_before);
  assert(engine.recolor_calls() == recolor_calls_before);
  assert(engine.recolored_vertices_total() == recolored_vertices_before);
  assert(engine.cascade_steps_total() == cascade_steps_before);
  assert(engine.full_fallback_count() == fallback_before);
  assert(engine.level_conflict_choices() == level_choices_before);
}

void TestRejectedDegreeCapInsertionStable() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(0, 2));
  initial.push_back(Insert(1, 3));
  SeqExactEngine engine(4, 2, 43, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const std::uint64_t recolor_calls_before = engine.recolor_calls();
  const std::uint64_t recolored_vertices_before = engine.recolored_vertices_total();
  const std::uint64_t cascade_steps_before = engine.cascade_steps_total();
  const std::uint64_t fallback_before = engine.full_fallback_count();
  const std::uint64_t level_choices_before = engine.level_conflict_choices();

  const UpdateStats stats = engine.apply_update(Insert(0, 3));
  AssertRejectedStats(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == colors_before);
  assert(engine.recolor_calls() == recolor_calls_before);
  assert(engine.recolored_vertices_total() == recolored_vertices_before);
  assert(engine.cascade_steps_total() == cascade_steps_before);
  assert(engine.full_fallback_count() == fallback_before);
  assert(engine.level_conflict_choices() == level_choices_before);
}

void TestAcceptedDeletionPreservesColorsAndStats() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(1, 2));
  initial.push_back(Insert(2, 3));
  SeqExactEngine engine(4, 3, 47, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const std::uint64_t recolor_calls_before = engine.recolor_calls();
  const std::uint64_t recolored_vertices_before = engine.recolored_vertices_total();
  const std::uint64_t cascade_steps_before = engine.cascade_steps_total();
  const std::uint64_t fallback_before = engine.full_fallback_count();
  const std::uint64_t level_choices_before = engine.level_conflict_choices();

  const UpdateStats stats = engine.apply_update(Delete(1, 2));
  AssertAcceptedStats(stats);
  assert(stats.vertices_touched == 0);
  assert(engine.graph().num_edges() + 1 == edges_before);
  AssertColoringValid(engine);
  assert(engine.colors() == colors_before);
  assert(engine.recolor_calls() == recolor_calls_before);
  assert(engine.recolored_vertices_total() == recolored_vertices_before);
  assert(engine.cascade_steps_total() == cascade_steps_before);
  assert(engine.full_fallback_count() == fallback_before);
  assert(engine.level_conflict_choices() == level_choices_before);
}

void TestRejectedMissingDeletionStable() {
  UpdateBatch initial;
  initial.push_back(Insert(0, 1));
  initial.push_back(Insert(1, 2));
  SeqExactEngine engine(4, 3, 53, initial);
  engine.initialize_coloring();
  const auto colors_before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();
  const std::uint64_t recolor_calls_before = engine.recolor_calls();
  const std::uint64_t recolored_vertices_before = engine.recolored_vertices_total();
  const std::uint64_t cascade_steps_before = engine.cascade_steps_total();
  const std::uint64_t fallback_before = engine.full_fallback_count();
  const std::uint64_t level_choices_before = engine.level_conflict_choices();

  const UpdateStats stats = engine.apply_update(Delete(0, 3));
  AssertRejectedStats(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == colors_before);
  assert(engine.recolor_calls() == recolor_calls_before);
  assert(engine.recolored_vertices_total() == recolored_vertices_before);
  assert(engine.cascade_steps_total() == cascade_steps_before);
  assert(engine.full_fallback_count() == fallback_before);
  assert(engine.level_conflict_choices() == level_choices_before);
}

void TestDeterministicSameSeedInsertionSequence() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 0));

  SeqExactEngine lhs(4, 3, 12345, edges);
  SeqExactEngine rhs(4, 3, 12345, edges);
  lhs.initialize_coloring();
  rhs.initialize_coloring();

  UpdateBatch sequence;
  sequence.push_back(Insert(0, 2));
  sequence.push_back(Insert(0, 3));
  sequence.push_back(Insert(1, 3));
  sequence.push_back(Insert(1, 2));
  for (const EdgeUpdate& update : sequence) {
    const UpdateStats lhs_stats = lhs.apply_update(update);
    const UpdateStats rhs_stats = rhs.apply_update(update);
    assert(lhs_stats.applied == rhs_stats.applied);
    assert(lhs_stats.edges_changed == rhs_stats.edges_changed);
    assert(lhs_stats.vertices_touched == rhs_stats.vertices_touched);
  }

  AssertColoringValid(lhs);
  AssertColoringValid(rhs);
  assert(lhs.colors() == rhs.colors());
  assert(lhs.recolor_calls() == rhs.recolor_calls());
  assert(lhs.recolored_vertices_total() == rhs.recolored_vertices_total());
  assert(lhs.cascade_steps_total() == rhs.cascade_steps_total());
  assert(lhs.full_fallback_count() == rhs.full_fallback_count());
  assert(lhs.level_conflict_choices() == rhs.level_conflict_choices());
}

void TestInvalidInitialBatchThrows() {
  UpdateBatch invalid;
  invalid.push_back(Insert(0, 1));
  invalid.push_back(Insert(1, 0));  // Duplicate undirected edge in one batch.

  bool threw = false;
  try {
    SeqExactEngine engine(3, 2, 1, invalid);
    (void)engine;
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

void TestApplyBatchStubThrows() {
  SeqExactEngine engine(4, 3, 23);
  engine.initialize_coloring();

  UpdateBatch batch;
  batch.push_back(Delete(0, 1));

  bool threw = false;
  try {
    (void)engine.apply_batch(batch);
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

int main() {
  TestInitializeEmptyGraph();
  TestInitializeNoEdges();
  TestInitializePathGraph();
  TestInitializeCycleGraph();
  TestInitializeStarGraph();
  TestInitializeNearDeltaCapGraph();
  TestApplyUpdateBeforeInitializeThrows();
  TestDeleteBeforeInitializeThrows();
  TestAcceptedInsertionNoConflict();
  TestAcceptedInsertionWithConflictRepairs();
  TestRejectedLoopInsertionStable();
  TestRejectedDuplicateInsertionStable();
  TestRejectedDegreeCapInsertionStable();
  TestAcceptedDeletionPreservesColorsAndStats();
  TestRejectedMissingDeletionStable();
  TestDeterministicSameSeedInsertionSequence();
  TestInvalidInitialBatchThrows();
  TestApplyBatchStubThrows();
  return 0;
}
