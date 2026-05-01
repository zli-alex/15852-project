#include <cassert>
#include <stdexcept>

#include "dgcolor/seq_exact_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::SeqExactEngine;
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

void AssertInitializedValid(const SeqExactEngine& engine) {
  const ValidationResult graph_validation = dgcolor::validate_graph_invariants(engine.graph());
  assert(graph_validation.ok);
  const ValidationResult coloring_validation =
      dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
  assert(coloring_validation.ok);

  const auto all_colors = engine.colors();
  for (Color c : all_colors) {
    assert(c < engine.palette_size());
  }
  AssertStatsAreZero(engine);
}

void TestInitializeEmptyGraph() {
  SeqExactEngine engine(0, 0, 1);
  engine.initialize_coloring();
  AssertInitializedValid(engine);
  assert(engine.palette_size() == 1);
}

void TestInitializeNoEdges() {
  SeqExactEngine engine(6, 4, 7);
  engine.initialize_coloring();
  AssertInitializedValid(engine);
}

void TestInitializePathGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  SeqExactEngine engine(5, 3, 11, edges);
  engine.initialize_coloring();
  AssertInitializedValid(engine);
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
  AssertInitializedValid(engine);
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
  AssertInitializedValid(engine);
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
  AssertInitializedValid(engine);
}

void TestDeterministicSameSeedSameColorsAndStats() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 0));

  SeqExactEngine lhs(4, 3, 12345, edges);
  SeqExactEngine rhs(4, 3, 12345, edges);
  lhs.initialize_coloring();
  rhs.initialize_coloring();

  assert(lhs.colors() == rhs.colors());
  assert(lhs.palette_size() == rhs.palette_size());
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

void TestApplyUpdateStubThrows() {
  SeqExactEngine engine(4, 3, 23);
  engine.initialize_coloring();

  bool threw = false;
  try {
    (void)engine.apply_update(Insert(0, 1));
  } catch (const std::logic_error&) {
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
  TestDeterministicSameSeedSameColorsAndStats();
  TestInvalidInitialBatchThrows();
  TestApplyUpdateStubThrows();
  TestApplyBatchStubThrows();
  return 0;
}
