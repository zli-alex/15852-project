#ifndef DGCOLOR_SEQ_EXACT_ENGINE_HPP_
#define DGCOLOR_SEQ_EXACT_ENGINE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

#include "dgcolor/coloring_engine.hpp"

namespace dgcolor {

class SeqExactEngine final : public ColoringEngine {
 public:
  SeqExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed);
  SeqExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                 const UpdateBatch& initial_updates);
  ~SeqExactEngine() override = default;

  std::string name() const override;
  const GraphStore& graph() const override;
  Color color_of(VertexId v) const override;
  parlay::sequence<Color> colors() const override;

  void initialize_coloring() override;
  UpdateStats apply_update(const EdgeUpdate& update) override;
  BatchStats apply_batch(const UpdateBatch& batch) override;

  std::size_t palette_size() const;
  std::uint64_t recolor_calls() const;
  std::uint64_t recolored_vertices_total() const;
  std::uint64_t cascade_steps_total() const;
  std::uint64_t full_fallback_count() const;
  std::uint64_t level_conflict_choices() const;

 private:
  static std::uint64_t mix_u64(std::uint64_t x);
  Level deterministic_level_for_vertex(VertexId v) const;
  Color greedy_color_for_vertex(VertexId v) const;
  void recolor_all_greedy_exact();
  bool color_in_palette_range(Color c) const;
  void validate_coloring_or_throw(const char* context) const;

  AdjacencyGraphStore graph_;
  parlay::sequence<Color> colors_;
  parlay::sequence<Level> levels_;
  parlay::sequence<Timestamp> timestamps_;
  std::uint64_t seed_{0};
  Timestamp logical_time_{0};
  bool initialized_{false};

  std::uint64_t recolor_calls_{0};
  std::uint64_t recolored_vertices_total_{0};
  std::uint64_t cascade_steps_total_{0};
  std::uint64_t full_fallback_count_{0};
  std::uint64_t level_conflict_choices_{0};
};

}  // namespace dgcolor

#endif  // DGCOLOR_SEQ_EXACT_ENGINE_HPP_
