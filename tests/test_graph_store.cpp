#include <algorithm>
#include <cassert>

#include "dgcolor/graph_store.hpp"

namespace {

using dgcolor::AdjacencyGraphStore;
using dgcolor::BatchApplyResult;
using dgcolor::EdgeUpdate;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateResult;
using dgcolor::UpdateStatus;
using dgcolor::VertexId;

EdgeUpdate Insert(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Insert, u, v, 0};
}

EdgeUpdate Delete(VertexId u, VertexId v) {
  return EdgeUpdate{UpdateKind::Delete, u, v, 0};
}

bool ContainsNeighbor(const parlay::sequence<VertexId>& neighbors, VertexId v) {
  return std::find(neighbors.begin(), neighbors.end(), v) != neighbors.end();
}

void TestConstruction() {
  AdjacencyGraphStore g(4, 2);
  assert(g.num_vertices() == 4);
  assert(g.num_edges() == 0);
  assert(g.delta_cap() == 2);
  assert(g.degree(0) == 0);
}

void TestValidInsertionAndSymmetry() {
  AdjacencyGraphStore g(4, 3);
  const UpdateResult result = g.apply_update(Insert(0, 1));
  assert(result.status == UpdateStatus::Ok);
  assert(g.num_edges() == 1);
  assert(g.has_edge(0, 1));
  assert(g.has_edge(1, 0));
  assert(g.degree(0) == 1);
  assert(g.degree(1) == 1);
  assert(ContainsNeighbor(g.neighbors(0), 1));
  assert(ContainsNeighbor(g.neighbors(1), 0));
}

void TestDuplicateInsertionRejectedBothOrders() {
  AdjacencyGraphStore g(4, 3);
  assert(g.apply_update(Insert(0, 1)).status == UpdateStatus::Ok);

  const std::size_t before_edges = g.num_edges();
  assert(g.apply_update(Insert(0, 1)).status == UpdateStatus::DuplicateEdge);
  assert(g.apply_update(Insert(1, 0)).status == UpdateStatus::DuplicateEdge);
  assert(g.num_edges() == before_edges);
}

void TestLoopRejection() {
  AdjacencyGraphStore g(3, 2);
  const UpdateResult result = g.apply_update(Insert(1, 1));
  assert(result.status == UpdateStatus::Loop);
  assert(g.num_edges() == 0);
}

void TestInvalidVertexRejection() {
  AdjacencyGraphStore g(3, 2);
  const UpdateResult result = g.apply_update(Insert(0, 9));
  assert(result.status == UpdateStatus::InvalidVertex);
  assert(g.num_edges() == 0);
}

void TestMissingDeletionRejection() {
  AdjacencyGraphStore g(3, 2);
  const UpdateResult result = g.apply_update(Delete(0, 1));
  assert(result.status == UpdateStatus::MissingEdge);
  assert(g.num_edges() == 0);
}

void TestDegreeCapRejection() {
  AdjacencyGraphStore g(4, 1);
  assert(g.apply_update(Insert(0, 1)).status == UpdateStatus::Ok);

  const std::size_t before_edges = g.num_edges();
  const UpdateResult result = g.apply_update(Insert(0, 2));
  assert(result.status == UpdateStatus::DegreeCapExceeded);
  assert(g.num_edges() == before_edges);
  assert(!g.has_edge(0, 2));
}

void TestValidDeletion() {
  AdjacencyGraphStore g(4, 3);
  assert(g.apply_update(Insert(0, 1)).status == UpdateStatus::Ok);
  assert(g.apply_update(Delete(1, 0)).status == UpdateStatus::Ok);
  assert(g.num_edges() == 0);
  assert(!g.has_edge(0, 1));
  assert(g.degree(0) == 0);
  assert(g.degree(1) == 0);
}

void TestValidBatchApplication() {
  AdjacencyGraphStore g(5, 3);
  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  batch.push_back(Insert(1, 2));
  batch.push_back(Insert(3, 4));

  const BatchApplyResult result = g.apply_batch(batch);
  assert(result.status == UpdateStatus::Ok);
  assert(result.updates_applied == 3);
  assert(g.num_edges() == 3);
  assert(g.has_edge(0, 1));
  assert(g.has_edge(1, 2));
  assert(g.has_edge(3, 4));
}

void TestBatchRejectionLeavesGraphUnchanged() {
  AdjacencyGraphStore g(4, 2);
  assert(g.apply_update(Insert(0, 1)).status == UpdateStatus::Ok);

  const std::size_t before_edges = g.num_edges();
  const auto before_neighbors_0 = g.neighbors(0);
  const auto before_neighbors_1 = g.neighbors(1);

  UpdateBatch batch;
  batch.push_back(Insert(2, 3));
  batch.push_back(Insert(2, 3));  // same edge appears twice in batch
  const BatchApplyResult result = g.apply_batch(batch);

  assert(result.status == UpdateStatus::BatchConflict);
  assert(g.num_edges() == before_edges);
  assert(g.neighbors(0) == before_neighbors_0);
  assert(g.neighbors(1) == before_neighbors_1);
  assert(!g.has_edge(2, 3));
}

void TestDuplicateEdgeInsideBatchRejected() {
  AdjacencyGraphStore g(5, 3);
  UpdateBatch batch;
  batch.push_back(Insert(0, 4));
  batch.push_back(Insert(4, 0));

  const BatchApplyResult result = g.apply_batch(batch);
  assert(result.status == UpdateStatus::BatchConflict);
  assert(g.num_edges() == 0);
}

void TestBatchDegreeCapViolationRejectedAtomically() {
  AdjacencyGraphStore g(4, 1);
  UpdateBatch batch;
  batch.push_back(Insert(0, 1));
  batch.push_back(Insert(0, 2));  // would exceed degree cap at vertex 0

  const BatchApplyResult result = g.apply_batch(batch);
  assert(result.status == UpdateStatus::DegreeCapExceeded);
  assert(g.num_edges() == 0);
  assert(!g.has_edge(0, 1));
  assert(!g.has_edge(0, 2));
}

}  // namespace

int main() {
  TestConstruction();
  TestValidInsertionAndSymmetry();
  TestDuplicateInsertionRejectedBothOrders();
  TestLoopRejection();
  TestInvalidVertexRejection();
  TestMissingDeletionRejection();
  TestDegreeCapRejection();
  TestValidDeletion();
  TestValidBatchApplication();
  TestBatchRejectionLeavesGraphUnchanged();
  TestDuplicateEdgeInsideBatchRejected();
  TestBatchDegreeCapViolationRejectedAtomically();
  return 0;
}
