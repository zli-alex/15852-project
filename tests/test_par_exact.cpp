#include <cassert>
#include <stdexcept>
#include <string>

#include "dgcolor/par_exact_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::BatchStats;
using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::ParExactEngine;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::ValidationResult;
using dgcolor::VertexId;

constexpr std::uint32_t kMaxRounds = 4;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

void AssertDiagnosticsZero(const ParExactEngine& engine) {
  assert(engine.active_vertices_total() == 0);
  assert(engine.repair_rounds_total() == 0);
  assert(engine.fallback_count() == 0);
  assert(engine.proposal_count() == 0);
  assert(engine.commit_count() == 0);
  assert(engine.unresolved_count() == 0);
  assert(engine.sequential_fast_path_count() == 0);
  assert(engine.vertices_touched_total() == 0);
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

void TestInitializeEmptyGraph() {
  ParExactEngine engine(0, 0, 1, kMaxRounds);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertDiagnosticsZero(engine);
  assert(engine.palette_size() == 1);
  assert(engine.name() == "par_exact");
}

void TestInitializeNoEdges() {
  ParExactEngine engine(6, 4, 7, kMaxRounds);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertDiagnosticsZero(engine);
}

void TestInitializePathGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  ParExactEngine engine(5, 3, 11, kMaxRounds, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertDiagnosticsZero(engine);
}

void TestInitializeCycleGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(2, 3));
  edges.push_back(Insert(3, 4));
  edges.push_back(Insert(4, 0));
  ParExactEngine engine(5, 3, 13, kMaxRounds, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertDiagnosticsZero(engine);
}

void TestInitializeStarGraph() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(0, 3));
  edges.push_back(Insert(0, 4));
  edges.push_back(Insert(0, 5));
  ParExactEngine engine(6, 5, 17, kMaxRounds, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertDiagnosticsZero(engine);
}

void TestInitializeK4Delta3() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(0, 2));
  edges.push_back(Insert(0, 3));
  edges.push_back(Insert(1, 2));
  edges.push_back(Insert(1, 3));
  edges.push_back(Insert(2, 3));
  ParExactEngine engine(4, 3, 19, kMaxRounds, edges);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertDiagnosticsZero(engine);
}

void TestDeterministicInitialization() {
  UpdateBatch edges;
  edges.push_back(Insert(0, 1));
  edges.push_back(Insert(1, 2));
  ParExactEngine lhs(4, 3, 42, kMaxRounds, edges);
  ParExactEngine rhs(4, 3, 42, kMaxRounds, edges);
  lhs.initialize_coloring();
  rhs.initialize_coloring();
  assert(lhs.colors() == rhs.colors());
  assert(lhs.graph().num_edges() == rhs.graph().num_edges());
  AssertDiagnosticsZero(lhs);
  AssertDiagnosticsZero(rhs);
}

void TestInitialBatchEmptyOk() {
  UpdateBatch empty;
  ParExactEngine engine(3, 2, 99, kMaxRounds, empty);
  engine.initialize_coloring();
  AssertColoringValid(engine);
  AssertDiagnosticsZero(engine);
}

void TestInitialBatchInvalidThrows() {
  UpdateBatch bad;
  bad.push_back(Insert(0, 0));  // loop: rejected by GraphStore
  bool threw = false;
  try {
    (void)ParExactEngine(4, 3, 1, kMaxRounds, bad);
  } catch (const std::invalid_argument& e) {
    threw = true;
    assert(std::string(e.what()).find("rejected") != std::string::npos);
  }
  assert(threw);
}

void TestApplyUpdateStubThrows() {
  ParExactEngine engine(4, 3, 1, kMaxRounds);
  engine.initialize_coloring();
  bool threw = false;
  try {
    (void)engine.apply_update(Insert(0, 1));
  } catch (const std::logic_error& e) {
    threw = true;
    assert(std::string(e.what()).find("Step 1") != std::string::npos);
  }
  assert(threw);
}

void TestApplyBatchStubThrows() {
  ParExactEngine engine(4, 3, 1, kMaxRounds);
  engine.initialize_coloring();
  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  bool threw = false;
  try {
    (void)engine.apply_batch(batch);
  } catch (const std::logic_error& e) {
    threw = true;
    assert(std::string(e.what()).find("Step 1") != std::string::npos);
  }
  assert(threw);
}

void TestColorOfInvalidVertex() {
  ParExactEngine engine(3, 2, 1, kMaxRounds);
  engine.initialize_coloring();
  assert(engine.color_of(3) == dgcolor::kUncolored);
}

}  // namespace

int main() {
  TestMaxRoundsZeroThrows();
  TestInitializeEmptyGraph();
  TestInitializeNoEdges();
  TestInitializePathGraph();
  TestInitializeCycleGraph();
  TestInitializeStarGraph();
  TestInitializeK4Delta3();
  TestDeterministicInitialization();
  TestInitialBatchEmptyOk();
  TestInitialBatchInvalidThrows();
  TestApplyUpdateStubThrows();
  TestApplyBatchStubThrows();
  TestColorOfInvalidVertex();
  return 0;
}
