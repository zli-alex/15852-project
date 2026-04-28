#include <cassert>

#include "dgcolor/graph_store.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::AdjacencyGraphStore;
using dgcolor::BatchApplyResult;
using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::GraphStore;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateResult;
using dgcolor::UpdateStatus;
using dgcolor::VertexId;
using dgcolor::kUncolored;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

void BuildPath(AdjacencyGraphStore& g, VertexId n) {
  for (VertexId i = 0; i + 1 < n; ++i) {
    assert(g.apply_update(Insert(i, i + 1)).status == UpdateStatus::Ok);
  }
}

void TestValidEmptyAndSmallGraphs() {
  AdjacencyGraphStore empty(0, 0);
  auto r0 = dgcolor::validate_graph_invariants(empty);
  assert(r0.ok);

  AdjacencyGraphStore single(1, 0);
  auto r1 = dgcolor::validate_graph_invariants(single);
  assert(r1.ok);
}

void TestValidPathAndCycleGraphs() {
  AdjacencyGraphStore path(5, 3);
  BuildPath(path, 5);
  auto path_result = dgcolor::validate_graph_invariants(path);
  assert(path_result.ok);

  AdjacencyGraphStore cycle(4, 3);
  assert(cycle.apply_update(Insert(0, 1)).status == UpdateStatus::Ok);
  assert(cycle.apply_update(Insert(1, 2)).status == UpdateStatus::Ok);
  assert(cycle.apply_update(Insert(2, 3)).status == UpdateStatus::Ok);
  assert(cycle.apply_update(Insert(3, 0)).status == UpdateStatus::Ok);
  auto cycle_result = dgcolor::validate_graph_invariants(cycle);
  assert(cycle_result.ok);
}

void TestProperColoringAccepted() {
  AdjacencyGraphStore path(5, 3);
  BuildPath(path, 5);
  parlay::sequence<Color> colors = {0, 1, 0, 1, 0};
  auto result = dgcolor::validate_exact_coloring(path, colors);
  assert(result.ok);
}

void TestImproperColoringRejected() {
  AdjacencyGraphStore path(4, 3);
  BuildPath(path, 4);
  parlay::sequence<Color> colors = {0, 0, 1, 1};
  auto result = dgcolor::validate_exact_coloring(path, colors);
  assert(!result.ok);
}

void TestWrongLengthColoringRejected() {
  AdjacencyGraphStore path(3, 2);
  BuildPath(path, 3);
  parlay::sequence<Color> colors = {0, 1};
  auto result = dgcolor::validate_exact_coloring(path, colors);
  assert(!result.ok);
}

void TestUncoloredRejected() {
  AdjacencyGraphStore path(3, 2);
  BuildPath(path, 3);
  parlay::sequence<Color> colors = {0, kUncolored, 1};
  auto result = dgcolor::validate_exact_coloring(path, colors);
  assert(!result.ok);
}

class LoopGraphStore final : public GraphStore {
 public:
  VertexId num_vertices() const override { return 2; }
  std::size_t num_edges() const override { return 1; }
  dgcolor::Degree delta_cap() const override { return 1; }
  dgcolor::Degree degree(VertexId v) const override { return (v == 0) ? 1 : 0; }
  bool has_edge(VertexId u, VertexId v) const override { return u == 0 && v == 0; }
  parlay::sequence<VertexId> neighbors(VertexId v) const override {
    if (v == 0) {
      return parlay::sequence<VertexId>{0};
    }
    return {};
  }
  UpdateResult can_apply(const EdgeUpdate&) const override { return {UpdateStatus::Ok, ""}; }
  UpdateResult apply_update(const EdgeUpdate&) override { return {UpdateStatus::Ok, ""}; }
  BatchApplyResult apply_batch(const UpdateBatch&) override { return {UpdateStatus::Ok, "", 0}; }
};

void TestGraphValidatorCatchesObservableInvalidStates() {
  LoopGraphStore bad;
  auto result = dgcolor::validate_graph_invariants(bad);
  assert(!result.ok);
}

}  // namespace

int main() {
  TestValidEmptyAndSmallGraphs();
  TestValidPathAndCycleGraphs();
  TestProperColoringAccepted();
  TestImproperColoringRejected();
  TestWrongLengthColoringRejected();
  TestUncoloredRejected();
  TestGraphValidatorCatchesObservableInvalidStates();
  return 0;
}
