#include <cassert>
#include <stdexcept>

#include "dgcolor/seq_baseline_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::SeqBaselineEngine;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateStats;
using dgcolor::UpdateStatus;
using dgcolor::VertexId;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

EdgeUpdate Delete(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Delete, u, v, 0};
}

void AssertColorRange(const SeqBaselineEngine& engine) {
  const auto all_colors = engine.colors();
  const auto cap = engine.graph().delta_cap();
  for (Color c : all_colors) {
    assert(c <= cap);
  }
}

void AssertInitializedColoringValid(const SeqBaselineEngine& engine) {
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

UpdateBatch PathEdges() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  return edges;
}

void TestInitializeEmptyGraph() {
  SeqBaselineEngine engine(0, 0);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
}

void TestInitializeNoEdges() {
  SeqBaselineEngine engine(6, 4);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
}

void TestInitializePathGraph() {
  SeqBaselineEngine engine(5, 3, PathEdges());
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

  SeqBaselineEngine engine(5, 3, edges);
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

  SeqBaselineEngine engine(6, 5, edges);
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

  SeqBaselineEngine engine(4, 3, edges);
  engine.initialize_coloring();
  AssertInitializedColoringValid(engine);
}

void TestApplyUpdateBeforeInitializeThrows() {
  SeqBaselineEngine engine(3, 2);
  bool threw = false;
  try {
    (void)engine.apply_update(Insert(0, 1));
  } catch (const std::logic_error&) {
    threw = true;
  }
  assert(threw);
}

void TestInsertionWithoutConflict() {
  SeqBaselineEngine engine(4, 3);
  engine.initialize_coloring();

  const UpdateStats stats = engine.apply_update(Insert(0, 1));
  AssertStatsApplied(stats, 4);
  AssertInitializedColoringValid(engine);
}

void TestInsertionWithConflictRequiresRecolor() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  SeqBaselineEngine engine(3, 2, edges);
  engine.initialize_coloring();
  const auto before = engine.colors();

  const UpdateStats stats = engine.apply_update(Insert(0, 2));
  AssertStatsApplied(stats, 3);
  AssertInitializedColoringValid(engine);

  const auto after = engine.colors();
  assert(before != after);
}

void TestDeletionPreservesValidityAndColors() {
  SeqBaselineEngine engine(5, 3, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();

  const UpdateStats stats = engine.apply_update(Delete(2, 3));
  AssertStatsApplied(stats, 0);
  AssertInitializedColoringValid(engine);
  assert(engine.colors() == before);
}

void TestRejectedLoopInsertionStable() {
  SeqBaselineEngine engine(5, 3, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();

  const UpdateStats stats = engine.apply_update(Insert(1, 1));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
}

void TestRejectedDuplicateInsertionStable() {
  SeqBaselineEngine engine(5, 3, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();

  const UpdateStats stats = engine.apply_update(Insert(1, 2));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
}

void TestRejectedMissingDeletionStable() {
  SeqBaselineEngine engine(5, 3, PathEdges());
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();

  const UpdateStats stats = engine.apply_update(Delete(0, 4));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
}

void TestRejectedDegreeCapInsertionStable() {
  // Vertex 0 and 1 are at degree cap already.
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(1, 3));
  SeqBaselineEngine engine(4, 2, edges);
  engine.initialize_coloring();
  const auto before = engine.colors();
  const std::size_t edges_before = engine.graph().num_edges();

  const UpdateStats stats = engine.apply_update(Insert(0, 3));
  AssertStatsRejected(stats);
  assert(engine.graph().num_edges() == edges_before);
  assert(engine.colors() == before);
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
  TestInsertionWithoutConflict();
  TestInsertionWithConflictRequiresRecolor();
  TestDeletionPreservesValidityAndColors();
  TestRejectedLoopInsertionStable();
  TestRejectedDuplicateInsertionStable();
  TestRejectedMissingDeletionStable();
  TestRejectedDegreeCapInsertionStable();
  return 0;
}
