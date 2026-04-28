#ifndef DGCOLOR_GRAPH_STORE_HPP_
#define DGCOLOR_GRAPH_STORE_HPP_

#include <cstddef>
#include <unordered_set>
#include <vector>

#include "dgcolor/batch.hpp"
#include "dgcolor/types.hpp"

namespace dgcolor {

class GraphStore {
 public:
  virtual ~GraphStore() = default;

  virtual VertexId num_vertices() const = 0;
  virtual std::size_t num_edges() const = 0;
  virtual Degree delta_cap() const = 0;
  virtual Degree degree(VertexId v) const = 0;
  virtual bool has_edge(VertexId u, VertexId v) const = 0;
  virtual parlay::sequence<VertexId> neighbors(VertexId v) const = 0;

  virtual UpdateResult can_apply(const EdgeUpdate& update) const = 0;
  virtual UpdateResult apply_update(const EdgeUpdate& update) = 0;
  virtual BatchApplyResult apply_batch(const UpdateBatch& batch) = 0;
};

// Simple adjacency-backed GraphStore declaration for future implementation.
class AdjacencyGraphStore final : public GraphStore {
 public:
  AdjacencyGraphStore(VertexId num_vertices, Degree delta_cap);
  ~AdjacencyGraphStore() override = default;

  VertexId num_vertices() const override;
  std::size_t num_edges() const override;
  Degree delta_cap() const override;
  Degree degree(VertexId v) const override;
  bool has_edge(VertexId u, VertexId v) const override;
  parlay::sequence<VertexId> neighbors(VertexId v) const override;

  UpdateResult can_apply(const EdgeUpdate& update) const override;
  UpdateResult apply_update(const EdgeUpdate& update) override;
  BatchApplyResult apply_batch(const UpdateBatch& batch) override;

 private:
  VertexId num_vertices_{0};
  Degree delta_cap_{0};
  std::size_t num_edges_{0};
  std::vector<std::unordered_set<VertexId>> adjacency_;
};

}  // namespace dgcolor

#endif  // DGCOLOR_GRAPH_STORE_HPP_
