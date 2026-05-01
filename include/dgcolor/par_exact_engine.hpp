#ifndef DGCOLOR_PAR_EXACT_ENGINE_HPP_
#define DGCOLOR_PAR_EXACT_ENGINE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "dgcolor/coloring_engine.hpp"

namespace dgcolor {

struct ParExactDiagnostics {
  std::uint64_t repair_calls{0};
  std::uint64_t max_active_size{0};
  std::uint64_t active_size_round_total{0};
  double graph_apply_seconds{0.0};
  double repair_seconds{0.0};
  double active_build_seconds{0.0};
  double internal_validation_seconds{0.0};
  std::uint64_t neighbor_materializations{0};
  std::uint64_t direct_neighbor_scans{0};
  std::uint64_t level_ge_neighbor_scans{0};
  std::uint64_t level_le_neighbor_scans{0};
  std::uint64_t level_palette_candidates_total{0};
  std::uint64_t level_diagnostic_vertices{0};
  std::uint64_t active_dense_rebuilds{0};
  std::uint64_t token_repair_calls{0};
  std::uint64_t token_safe_commits{0};
  std::uint64_t token_active_conflict_rejections{0};
  std::uint64_t token_lower_equal_conflict_rejections{0};
  std::uint64_t token_unique_higher_moves{0};
  std::uint64_t token_multi_higher_conflicts{0};
  std::uint64_t level_histogram_1{0};
  std::uint64_t level_histogram_2{0};
  std::uint64_t level_histogram_3{0};
  std::uint64_t level_histogram_4{0};
  std::uint64_t level_histogram_5_plus{0};
};

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
  void set_validate_after_apply(bool enabled);
  bool validate_after_apply() const;
  void set_diagnostics_enabled(bool enabled);
  bool diagnostics_enabled() const;
  void set_token_repair_enabled(bool enabled);
  bool token_repair_enabled() const;
  ParExactDiagnostics diagnostics() const;

 private:
  BatchStats apply_batch_impl(const UpdateBatch& batch);
  bool attempt_recolor_batch(const std::vector<VertexId>& initial_active,
                             std::size_t* vertices_touched, std::uint64_t* rounds_attempted);
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
  [[maybe_unused]] void prepare_active_membership(const std::vector<VertexId>& active);
  bool is_active_vertex(VertexId v) const;
  int active_vertex_index(VertexId v) const;
  void record_level_diagnostics(const std::vector<VertexId>& active);
  [[maybe_unused]] std::vector<VertexId> collect_conflicted_vertices_from_inserted_edges(
      const UpdateBatch& batch) const;
  [[maybe_unused]] Color first_available_color_with_offset(VertexId v, Color offset) const;
  [[maybe_unused]] Color first_level_available_color_with_offset(VertexId v, Color offset) const;
  [[maybe_unused]] bool proposal_conflicts_non_active_neighbors(
      VertexId v, Color proposed_color) const;
  [[maybe_unused]] bool proposal_conflicts_active_neighbors(
      VertexId v, Color proposed_color, const std::vector<Color>& proposed_colors) const;
  [[maybe_unused]] std::vector<VertexId> collect_unresolved_frontier_from_candidates(
      const std::vector<VertexId>& candidates) const;
  bool attempt_token_recolor_batch(const std::vector<VertexId>& initial_active,
                                   std::size_t* vertices_touched,
                                   std::uint64_t* rounds_attempted);
  Color sampled_level_aware_color(VertexId v, std::uint64_t round_index) const;
  Color greedy_color_for_vertex(VertexId v) const;
  const std::unordered_set<VertexId>& direct_neighbors(VertexId v) const;
  void recolor_all_greedy_exact();
  bool color_in_palette_range(Color c) const;
  void validate_coloring_or_throw(const char* context) const;

  // Initialise / rebuild all leu_ entries from scratch (called at initialize_coloring()).
  void rebuild_leu_all();
  // Update leu_ for an inserted or deleted edge.
  void update_leu_for_edge(VertexId u, VertexId v, bool inserted);
  // Update leu_ of all equal/higher-level neighbours when v's color changes.
  void update_leu_for_color_change(VertexId v, Color old_color, Color new_color);

  AdjacencyGraphStore graph_;
  parlay::sequence<Color> colors_;
  parlay::sequence<Level> levels_;
  parlay::sequence<Timestamp> timestamps_;
  std::uint64_t seed_{0};
  std::uint32_t max_rounds_{0};
  Timestamp logical_time_{0};
  bool initialized_{false};
  bool validate_after_apply_{true};
  bool diagnostics_enabled_{false};
  bool token_repair_enabled_{false};

  // LowerEqualUsed data structure (paper Section 3.1).
  // leu_[v][c] = number of *non-active* lower-or-equal-level neighbours of v
  //              that currently hold color c.
  // leu_bit_[v][c] = 1 iff leu_[v][c] > 0 (fast O(1) availability check).
  // Both arrays have logical size palette_size() per vertex.
  parlay::sequence<std::vector<std::uint16_t>> leu_;      // count
  parlay::sequence<std::vector<unsigned char>> leu_bit_;  // bitvector
  std::uint64_t current_active_stamp_{1};
  bool active_membership_sparse_current_{false};
  std::vector<VertexId> active_lookup_;
  std::vector<std::uint64_t> active_stamp_;
  std::vector<int> active_index_;

  std::uint64_t active_vertices_total_{0};
  std::uint64_t repair_rounds_total_{0};
  std::uint64_t fallback_count_{0};
  std::uint64_t proposal_count_{0};
  std::uint64_t commit_count_{0};
  std::uint64_t unresolved_count_{0};
  std::uint64_t sequential_fast_path_count_{0};
  std::uint64_t vertices_touched_total_{0};
  mutable ParExactDiagnostics diagnostics_;
};

}  // namespace dgcolor

#endif  // DGCOLOR_PAR_EXACT_ENGINE_HPP_
