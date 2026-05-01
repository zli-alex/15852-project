#ifndef DGCOLOR_PAR_RELAXED_ENGINE_HPP_
#define DGCOLOR_PAR_RELAXED_ENGINE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <parlay/sequence.h>

#include "dgcolor/coloring_engine.hpp"

namespace dgcolor {

struct ParRelaxedDiagnostics {
  std::uint64_t repair_calls{0};
  std::uint64_t repair_rounds{0};
  std::uint64_t sequential_repair_calls{0};
  std::uint64_t sequential_repair_rounds{0};
  std::uint64_t active_vertices_initial_total{0};
  std::uint64_t active_vertices_expanded_total{0};
  std::uint64_t conflicted_vertices_initial_total{0};
  std::uint64_t neighbor_scans{0};
  std::uint64_t commits_total{0};
  std::uint64_t max_active_size{0};
  std::uint64_t active_size_round_total{0};
  double graph_apply_seconds{0.0};
  double repair_seconds{0.0};
  double active_build_seconds{0.0};
  double internal_validation_seconds{0.0};
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
  void set_validate_after_apply(bool enabled);
  bool validate_after_apply() const;
  ParRelaxedDiagnostics diagnostics() const;

 private:
  // Threshold above which parlay::parallel_for is used instead of a serial loop.
  // 512 is large enough to amortise fork-join overhead while still parallelising
  // typical batch workloads.
  static constexpr std::size_t kSequentialRepairThreshold = 512;

  // Threshold for switching between a sorted-lookup sparse table and the dense
  // stamp array for active-set membership tests.
  static constexpr std::size_t kSparseMembershipThreshold = 4096;

  Color greedy_color_for_vertex(VertexId v) const;
  void recolor_all_greedy_relaxed();
  static Color compute_palette_size(Degree delta_cap, std::uint32_t palette_multiplier);
  bool color_in_palette_range(Color c) const;
  std::uint64_t deterministic_hash(std::uint64_t round_index, VertexId v,
                                   std::uint64_t salt = 0) const;
  Color deterministic_color_offset(std::uint64_t round_index, VertexId v,
                                   std::uint64_t salt = 0) const;

  // Expand a seed set to include all 1-hop neighbors, deduplicated and sorted.
  parlay::sequence<VertexId> expand_with_neighbors(
      const parlay::sequence<VertexId>& seeds) const;

  // Filter a candidate set down to vertices that conflict with at least one neighbor.
  parlay::sequence<VertexId> collect_conflicted_vertices_from_candidates(
      const parlay::sequence<VertexId>& candidates) const;

  // Attempt to repair all conflicts within initial_active using parallel rounds.
  bool attempt_parallel_repair(const parlay::sequence<VertexId>& initial_active,
                               std::size_t* vertices_touched,
                               std::uint64_t* rounds_attempted);

  // Stamp-based O(1) active-set membership, reused across repair rounds to avoid
  // allocating an O(n) index vector per round.
  void prepare_active_membership(const parlay::sequence<VertexId>& active);
  bool is_active_vertex(VertexId v) const;
  int active_vertex_index(VertexId v) const;

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
  bool validate_after_apply_{true};
  mutable ParRelaxedDiagnostics diagnostics_;

  // Persistent membership structures (avoid per-round O(n) allocation).
  std::uint64_t current_active_stamp_{1};
  std::vector<std::uint64_t> active_stamp_;  // size == num_vertices
  std::vector<int> active_index_;            // size == num_vertices
  bool active_membership_sparse_current_{false};
  std::vector<VertexId> active_lookup_;  // sorted list used for sparse mode
};

}  // namespace dgcolor

#endif  // DGCOLOR_PAR_RELAXED_ENGINE_HPP_
