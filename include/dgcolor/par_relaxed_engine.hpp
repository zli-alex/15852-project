#ifndef DGCOLOR_PAR_RELAXED_ENGINE_HPP_
#define DGCOLOR_PAR_RELAXED_ENGINE_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "dgcolor/coloring_engine.hpp"

namespace dgcolor {

struct ParRelaxedDiagnostics {
  std::uint64_t repair_calls{0};
  std::uint64_t repair_rounds{0};
  std::uint64_t active_vertices_initial_total{0};
  std::uint64_t active_vertices_expanded_total{0};
  std::uint64_t conflicted_vertices_initial_total{0};
  std::uint64_t neighbor_scans{0};
  std::uint64_t commits_total{0};
  double repair_seconds{0.0};
  double active_build_seconds{0.0};
};

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
  void set_diagnostics_enabled(bool enabled);
  bool diagnostics_enabled() const;
  ParRelaxedDiagnostics diagnostics() const;

 private:
  Color greedy_color_for_vertex(VertexId v) const;
  void recolor_all_greedy_relaxed();
  static Color compute_palette_size(Degree delta_cap, std::uint32_t palette_multiplier);
  bool color_in_palette_range(Color c) const;
  std::uint64_t deterministic_hash(std::uint64_t round_index, VertexId v,
                                   std::uint64_t salt = 0) const;
  Color deterministic_color_offset(std::uint64_t round_index, VertexId v,
                                   std::uint64_t salt = 0) const;
  std::vector<VertexId> expand_with_neighbors(const std::vector<VertexId>& seeds) const;
  std::vector<VertexId> collect_conflicted_vertices_from_candidates(
      const std::vector<VertexId>& candidates) const;
  bool attempt_parallel_repair(const std::vector<VertexId>& initial_active,
                               std::size_t* vertices_touched, std::uint64_t* rounds_attempted);
  void validate_coloring_or_throw(const char* context) const;

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
  bool diagnostics_enabled_{false};
  mutable ParRelaxedDiagnostics diagnostics_;
};

}  // namespace dgcolor

#endif  // DGCOLOR_PAR_RELAXED_ENGINE_HPP_
