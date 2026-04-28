#include "dgcolor/validator.hpp"

#include <cstdint>
#include <unordered_set>

#include "dgcolor/batch.hpp"

namespace dgcolor {

namespace {

ValidationResult Ok() {
  return ValidationResult{true, ""};
}

ValidationResult Fail(const std::string& message) {
  return ValidationResult{false, message};
}

std::uint64_t EdgeKey(VertexId u, VertexId v) {
  const Edge e = normalize_edge(u, v);
  return (static_cast<std::uint64_t>(e.u) << 32U) | static_cast<std::uint64_t>(e.v);
}

}  // namespace

ValidationResult validate_graph_invariants(const GraphStore& graph) {
  const VertexId n = graph.num_vertices();
  const Degree cap = graph.delta_cap();

  std::size_t degree_sum = 0;
  std::unordered_set<std::uint64_t> undirected_edges;

  for (VertexId v = 0; v < n; ++v) {
    const Degree d = graph.degree(v);
    if (d > cap) {
      return Fail("degree exceeds delta_cap at vertex " + std::to_string(v));
    }
    if (graph.has_edge(v, v)) {
      return Fail("loop detected at vertex " + std::to_string(v));
    }

    const parlay::sequence<VertexId> nbrs = graph.neighbors(v);
    std::unordered_set<VertexId> seen_neighbors;
    seen_neighbors.reserve(nbrs.size());
    if (nbrs.size() != static_cast<std::size_t>(d)) {
      return Fail("degree/neighbors mismatch at vertex " + std::to_string(v));
    }

    for (VertexId u : nbrs) {
      if (u >= n) {
        return Fail("neighbor out of range at edge (" + std::to_string(v) + "," +
                    std::to_string(u) + ")");
      }
      if (u == v) {
        return Fail("loop detected at edge (" + std::to_string(v) + "," +
                    std::to_string(u) + ")");
      }
      if (seen_neighbors.find(u) != seen_neighbors.end()) {
        return Fail("duplicate neighbor in adjacency list at edge (" + std::to_string(v) +
                    "," + std::to_string(u) + ")");
      }
      seen_neighbors.insert(u);

      if (!graph.has_edge(v, u) || !graph.has_edge(u, v)) {
        return Fail("asymmetric edge relation at edge (" + std::to_string(v) + "," +
                    std::to_string(u) + ")");
      }
      undirected_edges.insert(EdgeKey(v, u));
    }

    degree_sum += d;
  }

  if ((degree_sum % 2U) != 0U) {
    return Fail("sum of degrees is odd");
  }

  const std::size_t counted_edges = undirected_edges.size();
  if (counted_edges != graph.num_edges()) {
    return Fail("num_edges mismatch: expected " + std::to_string(counted_edges) +
                ", got " + std::to_string(graph.num_edges()));
  }
  if ((degree_sum / 2U) != graph.num_edges()) {
    return Fail("degree sum mismatch with num_edges");
  }

  return Ok();
}

ValidationResult validate_exact_coloring(const GraphStore& graph,
                                         const parlay::sequence<Color>& colors) {
  const VertexId n = graph.num_vertices();
  if (colors.size() != static_cast<std::size_t>(n)) {
    return Fail("coloring length mismatch: expected " + std::to_string(n) +
                ", got " + std::to_string(colors.size()));
  }

  for (VertexId v = 0; v < n; ++v) {
    if (colors[v] == kUncolored) {
      return Fail("uncolored vertex at " + std::to_string(v));
    }
    const parlay::sequence<VertexId> nbrs = graph.neighbors(v);
    for (VertexId u : nbrs) {
      if (u >= n) {
        return Fail("neighbor out of range at edge (" + std::to_string(v) + "," +
                    std::to_string(u) + ")");
      }
      if (colors[u] == colors[v]) {
        const Edge e = normalize_edge(v, u);
        return Fail("adjacent vertices share color at edge (" + std::to_string(e.u) + "," +
                    std::to_string(e.v) + ")");
      }
    }
  }

  return Ok();
}

}  // namespace dgcolor
