#ifndef DGCOLOR_PAR_RELAXED_ENGINE_HPP_
#define DGCOLOR_PAR_RELAXED_ENGINE_HPP_

#include <cstdint>
#include <string>

#include "dgcolor/coloring_engine.hpp"

namespace dgcolor {

class ParRelaxedEngine final : public ColoringEngine {
 public:
  ParRelaxedEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                   std::uint32_t palette_multiplier, std::uint32_t max_rounds);
  ParRelaxedEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                   std::uint32_t palette_multiplier, std::uint32_t max_rounds,
                   const UpdateBatch& initial_updates);
  ~ParRelaxedEngine() override = default;

  std::string name() const override;
  const GraphStore& graph() const override;
  Color color_of(VertexId v) const override;
  parlay::sequence<Color> colors() const override;

  void initialize_coloring() override;
  UpdateStats apply_update(const EdgeUpdate& update) override;
  BatchStats apply_batch(const UpdateBatch& batch) override;

  std::uint32_t palette_multiplier() const;
  Color palette_size() const;
  std::uint32_t max_rounds() const;
  std::uint64_t total_rounds() const;
  std::uint64_t fallback_count() const;
  std::uint64_t vertices_touched_total() const;

 private:
  Color greedy_color_for_vertex(VertexId v) const;
  void recolor_all_greedy_relaxed();
  static Color compute_palette_size(Degree delta_cap, std::uint32_t palette_multiplier);

  AdjacencyGraphStore graph_;
  parlay::sequence<Color> colors_;
  std::uint64_t seed_{0};
  std::uint32_t palette_multiplier_{0};
  Color palette_size_{0};
  std::uint32_t max_rounds_{0};
  bool initialized_{false};

  std::uint64_t total_rounds_{0};
  std::uint64_t fallback_count_{0};
  std::uint64_t vertices_touched_total_{0};
};

}  // namespace dgcolor

#endif  // DGCOLOR_PAR_RELAXED_ENGINE_HPP_
