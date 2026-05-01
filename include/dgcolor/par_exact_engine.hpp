#ifndef DGCOLOR_PAR_EXACT_ENGINE_HPP_
#define DGCOLOR_PAR_EXACT_ENGINE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

#include "dgcolor/coloring_engine.hpp"

namespace dgcolor {

class ParExactEngine final : public ColoringEngine {
 public:
  ParExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                 std::uint32_t max_rounds);
  ParExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                 std::uint32_t max_rounds, const UpdateBatch& initial_updates);
  ~ParExactEngine() override = default;

  std::string name() const override;
  const GraphStore& graph() const override;
  Color color_of(VertexId v) const override;
  parlay::sequence<Color> colors() const override;

  void initialize_coloring() override;
  UpdateStats apply_update(const EdgeUpdate& update) override;
  BatchStats apply_batch(const UpdateBatch& batch) override;

  std::size_t palette_size() const;
  std::uint32_t max_rounds() const;
  std::uint64_t active_vertices_total() const;
  std::uint64_t repair_rounds_total() const;
  std::uint64_t fallback_count() const;
  std::uint64_t proposal_count() const;
  std::uint64_t commit_count() const;
  std::uint64_t unresolved_count() const;
  std::uint64_t sequential_fast_path_count() const;
  std::uint64_t vertices_touched_total() const;

 private:
  BatchStats apply_batch_impl(const UpdateBatch& batch);
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
  std::uint32_t max_rounds_{0};
  Timestamp logical_time_{0};
  bool initialized_{false};

  std::uint64_t active_vertices_total_{0};
  std::uint64_t repair_rounds_total_{0};
  std::uint64_t fallback_count_{0};
  std::uint64_t proposal_count_{0};
  std::uint64_t commit_count_{0};
  std::uint64_t unresolved_count_{0};
  std::uint64_t sequential_fast_path_count_{0};
  std::uint64_t vertices_touched_total_{0};
};

}  // namespace dgcolor

#endif  // DGCOLOR_PAR_EXACT_ENGINE_HPP_
