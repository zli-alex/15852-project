#ifndef DGCOLOR_SEQ_BASELINE_ENGINE_HPP_
#define DGCOLOR_SEQ_BASELINE_ENGINE_HPP_

#include <string>

#include "dgcolor/coloring_engine.hpp"

namespace dgcolor {

class SeqBaselineEngine final : public ColoringEngine {
 public:
  SeqBaselineEngine(VertexId num_vertices, Degree delta_cap);
  SeqBaselineEngine(VertexId num_vertices, Degree delta_cap, const UpdateBatch& initial_updates);
  ~SeqBaselineEngine() override = default;

  std::string name() const override;
  const GraphStore& graph() const override;
  Color color_of(VertexId v) const override;
  parlay::sequence<Color> colors() const override;

  void initialize_coloring() override;
  UpdateStats apply_update(const EdgeUpdate& update) override;
  BatchStats apply_batch(const UpdateBatch& batch) override;

 private:
  Color greedy_color_for_vertex(VertexId v) const;
  void recolor_all_greedy();

  AdjacencyGraphStore graph_;
  parlay::sequence<Color> colors_;
  bool initialized_{false};
};

}  // namespace dgcolor

#endif  // DGCOLOR_SEQ_BASELINE_ENGINE_HPP_
