#include <cassert>

#include "dgcolor/seq_baseline_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::SeqBaselineEngine;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateStatus;
using dgcolor::VertexId;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
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
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));

  SeqBaselineEngine engine(5, 3, edges);
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

}  // namespace

int main() {
  TestInitializeEmptyGraph();
  TestInitializeNoEdges();
  TestInitializePathGraph();
  TestInitializeCycleGraph();
  TestInitializeStarGraph();
  TestInitializeNearDeltaCapGraph();
  return 0;
}
