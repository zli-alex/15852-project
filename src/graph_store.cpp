#include "dgcolor/graph_store.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dgcolor {

namespace {

UpdateResult OkResult() {
  return UpdateResult{UpdateStatus::Ok, ""};
}

UpdateResult ErrorResult(UpdateStatus status, const char* message) {
  return UpdateResult{status, message};
}

bool IsValidVertex(VertexId v, VertexId n) {
  return v < n;
}

}  // namespace

AdjacencyGraphStore::AdjacencyGraphStore(VertexId num_vertices, Degree delta_cap)
    : num_vertices_(num_vertices),
      delta_cap_(delta_cap),
      num_edges_(0),
      adjacency_(num_vertices) {}

VertexId AdjacencyGraphStore::num_vertices() const {
  return num_vertices_;
}

std::size_t AdjacencyGraphStore::num_edges() const {
  return num_edges_;
}

Degree AdjacencyGraphStore::delta_cap() const {
  return delta_cap_;
}

Degree AdjacencyGraphStore::degree(VertexId v) const {
  if (!IsValidVertex(v, num_vertices_)) {
    return 0;
  }
  return static_cast<Degree>(adjacency_[v].size());
}

bool AdjacencyGraphStore::has_edge(VertexId u, VertexId v) const {
  if (!IsValidVertex(u, num_vertices_) || !IsValidVertex(v, num_vertices_)) {
    return false;
  }
  if (u == v) {
    return false;
  }
  const Edge e = normalize_edge(u, v);
  return adjacency_[e.u].find(e.v) != adjacency_[e.u].end();
}

parlay::sequence<VertexId> AdjacencyGraphStore::neighbors(VertexId v) const {
  parlay::sequence<VertexId> result;
  if (!IsValidVertex(v, num_vertices_)) {
    return result;
  }
  result.reserve(adjacency_[v].size());
  for (VertexId u : adjacency_[v]) {
    result.push_back(u);
  }
  std::sort(result.begin(), result.end());
  return result;
}

const std::unordered_set<VertexId>& AdjacencyGraphStore::adjacency_set(VertexId v) const {
  static const std::unordered_set<VertexId> kEmpty;
  if (!IsValidVertex(v, num_vertices_)) {
    return kEmpty;
  }
  return adjacency_[v];
}

UpdateResult AdjacencyGraphStore::can_apply(const EdgeUpdate& update) const {
  if (!IsValidVertex(update.u, num_vertices_) || !IsValidVertex(update.v, num_vertices_)) {
    return ErrorResult(UpdateStatus::InvalidVertex, "vertex id out of range");
  }
  if (is_loop(update.u, update.v)) {
    return ErrorResult(UpdateStatus::Loop, "loops are not allowed");
  }

  const Edge e = normalize_edge(update.u, update.v);
  const bool edge_exists = adjacency_[e.u].find(e.v) != adjacency_[e.u].end();

  if (update.kind == UpdateKind::Insert) {
    if (edge_exists) {
      return ErrorResult(UpdateStatus::DuplicateEdge, "edge already exists");
    }
    if (adjacency_[e.u].size() >= delta_cap_ || adjacency_[e.v].size() >= delta_cap_) {
      return ErrorResult(UpdateStatus::DegreeCapExceeded, "degree cap exceeded");
    }
    return OkResult();
  }

  if (!edge_exists) {
    return ErrorResult(UpdateStatus::MissingEdge, "edge does not exist");
  }
  return OkResult();
}

UpdateResult AdjacencyGraphStore::apply_update(const EdgeUpdate& update) {
  const UpdateResult validation = can_apply(update);
  if (validation.status != UpdateStatus::Ok) {
    return validation;
  }

  const Edge e = normalize_edge(update.u, update.v);
  if (update.kind == UpdateKind::Insert) {
    adjacency_[e.u].insert(e.v);
    adjacency_[e.v].insert(e.u);
    ++num_edges_;
  } else {
    adjacency_[e.u].erase(e.v);
    adjacency_[e.v].erase(e.u);
    --num_edges_;
  }
  return OkResult();
}

BatchApplyResult AdjacencyGraphStore::apply_batch(const UpdateBatch& batch) {
  std::unordered_set<std::uint64_t> seen_edges;
  seen_edges.reserve(batch.size());
  std::vector<int> degree_delta(num_vertices_, 0);

  for (const EdgeUpdate& update : batch) {
    if (!IsValidVertex(update.u, num_vertices_) || !IsValidVertex(update.v, num_vertices_)) {
      return BatchApplyResult{UpdateStatus::InvalidVertex, "vertex id out of range", 0};
    }
    if (is_loop(update.u, update.v)) {
      return BatchApplyResult{UpdateStatus::Loop, "loops are not allowed", 0};
    }

    const Edge e = normalize_edge(update.u, update.v);
    const std::uint64_t edge_key =
        (static_cast<std::uint64_t>(e.u) << 32U) | static_cast<std::uint64_t>(e.v);
    if (seen_edges.find(edge_key) != seen_edges.end()) {
      return BatchApplyResult{UpdateStatus::BatchConflict, "duplicate edge in batch", 0};
    }
    seen_edges.insert(edge_key);

    const bool edge_exists = adjacency_[e.u].find(e.v) != adjacency_[e.u].end();
    if (update.kind == UpdateKind::Insert) {
      if (edge_exists) {
        return BatchApplyResult{UpdateStatus::DuplicateEdge, "edge already exists", 0};
      }
      const int next_degree_u = static_cast<int>(adjacency_[e.u].size()) + degree_delta[e.u] + 1;
      const int next_degree_v = static_cast<int>(adjacency_[e.v].size()) + degree_delta[e.v] + 1;
      if (next_degree_u > static_cast<int>(delta_cap_) ||
          next_degree_v > static_cast<int>(delta_cap_)) {
        return BatchApplyResult{UpdateStatus::DegreeCapExceeded, "degree cap exceeded", 0};
      }
      ++degree_delta[e.u];
      ++degree_delta[e.v];
    } else {
      if (!edge_exists) {
        return BatchApplyResult{UpdateStatus::MissingEdge, "edge does not exist", 0};
      }
      --degree_delta[e.u];
      --degree_delta[e.v];
    }
  }

  for (const EdgeUpdate& update : batch) {
    const Edge e = normalize_edge(update.u, update.v);
    if (update.kind == UpdateKind::Insert) {
      adjacency_[e.u].insert(e.v);
      adjacency_[e.v].insert(e.u);
      ++num_edges_;
    } else {
      adjacency_[e.u].erase(e.v);
      adjacency_[e.v].erase(e.u);
      --num_edges_;
    }
  }
  return BatchApplyResult{UpdateStatus::Ok, "", batch.size()};
}

}  // namespace dgcolor
