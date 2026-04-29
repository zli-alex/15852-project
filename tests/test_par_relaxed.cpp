#include <cassert>
#include <stdexcept>

#include "dgcolor/par_relaxed_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::ParRelaxedEngine;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::VertexId;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

void AssertColorRange(const ParRelaxedEngine& engine) {
  const auto all_colors = engine.colors();
  const Color limit = engine.palette_size();
  for (Color c : all_colors) {
    assert(c < limit);
  }
}

void AssertInitializedColoringValid(const ParRelaxedEngine& engine) {
  const auto validation = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
  assert(validation.ok);
  AssertColorRange(engine);
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

void TestUpdateAndBatchStubsThrow() {
  ParRelaxedEngine engine(4, 3, 1, 2, 8);
  engine.initialize_coloring();

  bool update_threw = false;
  try {
    (void)engine.apply_update(Insert(0, 1));
  } catch (const std::logic_error&) {
    update_threw = true;
  }
  assert(update_threw);

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

}  // namespace

int main() {
  TestInitializeEmptyGraph();
  TestInitializeNoEdges();
  TestInitializePathGraph();
  TestInitializeCycleGraph();
  TestInitializeStarGraph();
  TestInitializeNearDeltaCapGraph();
  TestInvalidConstructorArgs();
  TestUpdateAndBatchStubsThrow();
  return 0;
}
