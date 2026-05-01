#ifndef DGCOLOR_PAR_EXACT_ENGINE_HPP_
#define DGCOLOR_PAR_EXACT_ENGINE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
  [[maybe_unused]] static std::uint64_t deterministic_hash(std::uint64_t seed,
                                                           std::uint64_t round_index, VertexId v,
                                                           std::uint64_t salt = 0);
  [[maybe_unused]] Color deterministic_color_offset(std::uint64_t round_index, VertexId v,
                                                    std::uint64_t salt = 0) const;
  static std::uint64_t mix_u64(std::uint64_t x);
  Level deterministic_level_for_vertex(VertexId v) const;
  [[maybe_unused]] VertexId choose_conflict_endpoint(VertexId u, VertexId v) const;
  [[maybe_unused]] static std::vector<VertexId> deduplicate_and_sort_vertices(
      const std::vector<VertexId>& vertices);
  [[maybe_unused]] std::vector<unsigned char> build_active_membership(
      const std::vector<VertexId>& active) const;
  [[maybe_unused]] std::vector<VertexId> collect_conflicted_vertices_from_inserted_edges(
      const UpdateBatch& batch) const;
  [[maybe_unused]] Color first_available_color_with_offset(VertexId v, Color offset) const;
  [[maybe_unused]] bool proposal_conflicts_non_active_neighbors(
      VertexId v, Color proposed_color, const std::vector<unsigned char>& active_mask) const;
  [[maybe_unused]] bool proposal_conflicts_active_neighbors(
      VertexId v, Color proposed_color, const std::vector<unsigned char>& active_mask,
      const std::vector<VertexId>& active, const std::vector<Color>& proposed_colors) const;
  [[maybe_unused]] std::vector<VertexId> collect_unresolved_frontier_from_candidates(
      const std::vector<VertexId>& candidates) const;
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
