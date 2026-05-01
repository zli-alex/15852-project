#include "dgcolor/par_exact_engine.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>

#include "dgcolor/validator.hpp"

namespace dgcolor {

namespace {

double SecondsBetween(const std::chrono::steady_clock::time_point& start,
                      const std::chrono::steady_clock::time_point& end) {
  return std::chrono::duration<double>(end - start).count();
}

}  // namespace

ParExactEngine::ParExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                               std::uint32_t max_rounds)
    : graph_(num_vertices, delta_cap),
      colors_(num_vertices, kUncolored),
      levels_(num_vertices, 0),
      timestamps_(num_vertices, 0),
      seed_(seed),
      max_rounds_(max_rounds),
      active_stamp_(num_vertices, 0),
      active_index_(num_vertices, -1) {
  if (max_rounds_ == 0) {
    throw std::invalid_argument("ParExactEngine requires max_rounds >= 1");
  }
}

ParExactEngine::ParExactEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                               std::uint32_t max_rounds, const UpdateBatch& initial_updates)
    : ParExactEngine(num_vertices, delta_cap, seed, max_rounds) {
  if (initial_updates.empty()) {
    return;
  }
  const BatchApplyResult init_result = graph_.apply_batch(initial_updates);
  if (init_result.status != UpdateStatus::Ok) {
    throw std::invalid_argument("initial graph updates were rejected: " +
                                std::string(update_status_name(init_result.status)));
  }
}

std::string ParExactEngine::name() const {
  return "par_exact";
}

const GraphStore& ParExactEngine::graph() const {
  return graph_;
}

Color ParExactEngine::color_of(VertexId v) const {
  if (v >= graph_.num_vertices()) {
    return kUncolored;
  }
  return colors_[v];
}

parlay::sequence<Color> ParExactEngine::colors() const {
  return colors_;
}

void ParExactEngine::initialize_coloring() {
  logical_time_ = 0;
  diagnostics_.level_histogram_1 = 0;
  diagnostics_.level_histogram_2 = 0;
  diagnostics_.level_histogram_3 = 0;
  diagnostics_.level_histogram_4 = 0;
  diagnostics_.level_histogram_5_plus = 0;
  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    levels_[v] = deterministic_level_for_vertex(v);
    if (levels_[v] <= 1) {
      ++diagnostics_.level_histogram_1;
    } else if (levels_[v] == 2) {
      ++diagnostics_.level_histogram_2;
    } else if (levels_[v] == 3) {
      ++diagnostics_.level_histogram_3;
    } else if (levels_[v] == 4) {
      ++diagnostics_.level_histogram_4;
    } else {
      ++diagnostics_.level_histogram_5_plus;
    }
    timestamps_[v] = 0;
  }
  recolor_all_greedy_exact();
  rebuild_leu_all();
  initialized_ = true;
  validate_coloring_or_throw("initialize_coloring");
}

UpdateStats ParExactEngine::apply_update(const EdgeUpdate& update) {
  if (!initialized_) {
    throw std::logic_error("ParExactEngine::apply_update requires initialize_coloring() first");
  }

  UpdateBatch single;
  single.push_back(update);
  const BatchStats batch_stats = apply_batch_impl(single);
  return UpdateStats{batch_stats.applied, batch_stats.edges_changed, batch_stats.vertices_touched,
                     batch_stats.seconds};
}

BatchStats ParExactEngine::apply_batch(const UpdateBatch& batch) {
  if (!initialized_) {
    throw std::logic_error("ParExactEngine::apply_batch requires initialize_coloring() first");
  }
  return apply_batch_impl(batch);
}

BatchStats ParExactEngine::apply_batch_impl(const UpdateBatch& batch) {
  const auto start = std::chrono::steady_clock::now();
  const auto graph_apply_start = std::chrono::steady_clock::now();
  const BatchApplyResult result = graph_.apply_batch(batch);
  diagnostics_.graph_apply_seconds +=
      SecondsBetween(graph_apply_start, std::chrono::steady_clock::now());
  if (result.status != UpdateStatus::Ok) {
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    return BatchStats{false, batch.size(), 0, 0, seconds};
  }

  // Reflect structural changes in the LowerEqualUsed bitvectors.
  if (!leu_.empty()) {
    for (const EdgeUpdate& upd : batch) {
      if (upd.u < graph_.num_vertices() && upd.v < graph_.num_vertices()) {
        update_leu_for_edge(upd.u, upd.v, upd.kind == UpdateKind::Insert);
      }
    }
  }

  bool has_insert = false;
  for (const EdgeUpdate& update : batch) {
    if (update.kind == UpdateKind::Insert) {
      has_insert = true;
      break;
    }
  }

  std::size_t vertices_touched = 0;
  if (has_insert) {
    const auto active_build_start = std::chrono::steady_clock::now();
    const std::vector<VertexId> initial_active = collect_conflicted_vertices_from_inserted_edges(batch);
    diagnostics_.active_build_seconds +=
        SecondsBetween(active_build_start, std::chrono::steady_clock::now());
    if (!initial_active.empty()) {
      std::uint64_t rounds_attempted = 0;
      active_vertices_total_ += static_cast<std::uint64_t>(initial_active.size());
      const auto repair_start = std::chrono::steady_clock::now();
      const bool repaired = token_repair_enabled_
                                ? attempt_token_recolor_batch(initial_active, &vertices_touched,
                                                              &rounds_attempted)
                                : attempt_recolor_batch(initial_active, &vertices_touched,
                                                        &rounds_attempted);
      diagnostics_.repair_seconds += SecondsBetween(repair_start, std::chrono::steady_clock::now());
      repair_rounds_total_ += rounds_attempted;
      vertices_touched_total_ += static_cast<std::uint64_t>(vertices_touched);

      if (!repaired) {
        recolor_all_greedy_exact();
        ++fallback_count_;
        const std::size_t fallback_touched = static_cast<std::size_t>(graph_.num_vertices());
        vertices_touched += fallback_touched;
        vertices_touched_total_ += static_cast<std::uint64_t>(fallback_touched);
      }
    }
  }

  if (validate_after_apply_) {
    const auto validation_start = std::chrono::steady_clock::now();
    validate_coloring_or_throw("apply_batch");
    diagnostics_.internal_validation_seconds +=
        SecondsBetween(validation_start, std::chrono::steady_clock::now());
  }
  const auto end = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(end - start).count();
  return BatchStats{true, batch.size(), result.updates_applied, vertices_touched, seconds};
}

bool ParExactEngine::attempt_recolor_batch(const std::vector<VertexId>& initial_active,
                                           std::size_t* vertices_touched,
                                           std::uint64_t* rounds_attempted) {
  static constexpr std::size_t kSequentialThreshold = 512;
  std::vector<VertexId> active = deduplicate_and_sort_vertices(initial_active);
  if (active.empty()) {
    return true;
  }
  ++diagnostics_.repair_calls;

  // Mark all initial_active vertices as "logically uncolored" in leu_ so that
  // the proposal step can use leu_bit_[v] for O(palette) instead of O(deg+palette).
  const bool use_leu = !leu_.empty();
  if (use_leu) {
    for (VertexId v : active) {
      const Color old_c = colors_[v];
      if (old_c != kUncolored) update_leu_for_color_change(v, old_c, kUncolored);
    }
  }

  bool used_sequential = false;
  for (std::uint32_t round = 0; round < max_rounds_ && !active.empty(); ++round) {
    if (rounds_attempted != nullptr) {
      ++(*rounds_attempted);
    }
    if (vertices_touched != nullptr) {
      *vertices_touched += active.size();
    }

    prepare_active_membership(active);
    std::vector<Color> proposed(active.size(), kUncolored);
    std::vector<unsigned char> safe(active.size(), 0);
    std::vector<Color> colors_before(active.size(), kUncolored);
    for (std::size_t i = 0; i < active.size(); ++i) {
      colors_before[i] = colors_[active[i]];
    }
    proposal_count_ += static_cast<std::uint64_t>(active.size());
    diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(active.size());
    diagnostics_.active_size_round_total += static_cast<std::uint64_t>(active.size());
    record_level_diagnostics(active);
    if (active.size() > diagnostics_.max_active_size) {
      diagnostics_.max_active_size = static_cast<std::uint64_t>(active.size());
    }

    const bool sequential_path = active.size() <= kSequentialThreshold;
    if (sequential_path && !used_sequential) {
      ++sequential_fast_path_count_;
      used_sequential = true;
    }

    diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(active.size()) * 2U;
    if (sequential_path) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        const VertexId v = active[i];
        const Color offset = deterministic_color_offset(round, v);
        proposed[i] = first_level_available_color_with_offset(v, offset);
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        const VertexId v = active[i];
        const Color offset = deterministic_color_offset(round, v);
        proposed[i] = first_level_available_color_with_offset(v, offset);
      });
    }

    if (sequential_path) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        const VertexId v = active[i];
        const Color c = proposed[i];
        if (!color_in_palette_range(c)) {
          continue;
        }
        if (proposal_conflicts_non_active_neighbors(v, c)) {
          continue;
        }
        if (proposal_conflicts_active_neighbors(v, c, proposed)) {
          continue;
        }
        safe[i] = 1;
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        const VertexId v = active[i];
        const Color c = proposed[i];
        if (!color_in_palette_range(c)) {
          return;
        }
        if (proposal_conflicts_non_active_neighbors(v, c)) {
          return;
        }
        if (proposal_conflicts_active_neighbors(v, c, proposed)) {
          return;
        }
        safe[i] = 1;
      });
    }

    if (sequential_path) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        if (!safe[i]) {
          continue;
        }
        const VertexId v = active[i];
        colors_[v] = proposed[i];
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        if (!safe[i]) {
          return;
        }
        const VertexId v = active[i];
        const Color next = proposed[i];
        colors_[v] = next;
      });
    }
    for (std::size_t i = 0; i < active.size(); ++i) {
      if (!safe[i]) {
        continue;
      }
      if (colors_before[i] != proposed[i]) {
        ++logical_time_;
        timestamps_[active[i]] = logical_time_;
        ++commit_count_;
      }
    }

    // Re-add committed vertices' new colors to neighbors' leu_.
    // Must be sequential to avoid races on leu_cnt_ (proposal phase is done,
    // so reads of leu_bit_ in the NEXT round's proposal are safe).
    if (use_leu) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        if (!safe[i]) continue;
        const Color new_c = proposed[i];
        if (new_c != kUncolored) {
          update_leu_for_color_change(active[i], kUncolored, new_c);
        }
      }
    }

    active = collect_unresolved_frontier_from_candidates(active);
    unresolved_count_ += static_cast<std::uint64_t>(active.size());
  }

  // If any vertices failed to resolve within max_rounds_, restore their
  // colors in leu_ so the structure stays consistent for future batches.
  if (use_leu) {
    for (VertexId v : active) {
      const Color c = colors_[v];
      if (c != kUncolored) update_leu_for_color_change(v, kUncolored, c);
    }
  }

  return active.empty();
}

std::uint64_t ParExactEngine::deterministic_hash(std::uint64_t seed, std::uint64_t round_index,
                                                 VertexId v, std::uint64_t salt) {
  std::uint64_t x = seed ^ (round_index + 0x9e3779b97f4a7c15ULL) ^
                    (static_cast<std::uint64_t>(v) * 0xbf58476d1ce4e5b9ULL) ^ salt;
  x ^= x >> 33U;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33U;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33U;
  return x;
}

Color ParExactEngine::deterministic_color_offset(std::uint64_t round_index, VertexId v,
                                                 std::uint64_t salt) const {
  const std::uint64_t palette = static_cast<std::uint64_t>(palette_size());
  if (palette == 0) {
    return 0;
  }
  return static_cast<Color>(deterministic_hash(seed_, round_index, v, salt) % palette);
}

std::size_t ParExactEngine::palette_size() const {
  return static_cast<std::size_t>(graph_.delta_cap()) + 1U;
}

std::uint32_t ParExactEngine::max_rounds() const {
  return max_rounds_;
}

std::uint64_t ParExactEngine::active_vertices_total() const {
  return active_vertices_total_;
}

std::uint64_t ParExactEngine::repair_rounds_total() const {
  return repair_rounds_total_;
}

std::uint64_t ParExactEngine::fallback_count() const {
  return fallback_count_;
}

std::uint64_t ParExactEngine::proposal_count() const {
  return proposal_count_;
}

std::uint64_t ParExactEngine::commit_count() const {
  return commit_count_;
}

std::uint64_t ParExactEngine::unresolved_count() const {
  return unresolved_count_;
}

std::uint64_t ParExactEngine::sequential_fast_path_count() const {
  return sequential_fast_path_count_;
}

std::uint64_t ParExactEngine::vertices_touched_total() const {
  return vertices_touched_total_;
}

void ParExactEngine::set_validate_after_apply(bool enabled) {
  validate_after_apply_ = enabled;
}

bool ParExactEngine::validate_after_apply() const {
  return validate_after_apply_;
}

void ParExactEngine::set_diagnostics_enabled(bool enabled) {
  diagnostics_enabled_ = enabled;
}

bool ParExactEngine::diagnostics_enabled() const {
  return diagnostics_enabled_;
}

void ParExactEngine::set_token_repair_enabled(bool enabled) {
  token_repair_enabled_ = enabled;
}

bool ParExactEngine::token_repair_enabled() const {
  return token_repair_enabled_;
}

ParExactDiagnostics ParExactEngine::diagnostics() const {
  return diagnostics_;
}

std::uint64_t ParExactEngine::mix_u64(std::uint64_t x) {
  x ^= x >> 30U;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27U;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31U;
  return x;
}

Level ParExactEngine::deterministic_level_for_vertex(VertexId v) const {
  std::uint32_t max_level = 1;
  Degree cap = graph_.delta_cap();
  while (cap > 1) {
    cap = (cap + 1U) / 2U;
    ++max_level;
  }
  ++max_level;

  std::uint64_t x =
      mix_u64(seed_ ^ (static_cast<std::uint64_t>(v) + 0x9e3779b97f4a7c15ULL));
  Level level = 1;
  while (level < max_level && (x & 1ULL) == 0ULL) {
    ++level;
    x >>= 1U;
  }
  return level;
}

VertexId ParExactEngine::choose_conflict_endpoint(VertexId u, VertexId v) const {
  const Level lu = levels_[u];
  const Level lv = levels_[v];
  if (lu != lv) {
    return (lu > lv) ? u : v;
  }

  const Timestamp tu = timestamps_[u];
  const Timestamp tv = timestamps_[v];
  if (tu != tv) {
    return (tu > tv) ? u : v;
  }

  return (u > v) ? u : v;
}

std::vector<VertexId> ParExactEngine::deduplicate_and_sort_vertices(
    const std::vector<VertexId>& vertices) {
  if (vertices.empty()) return {};
  // parlay::integer_sort runs in parallel and is O(n) for integer keys.
  parlay::sequence<VertexId> seq(vertices.begin(), vertices.end());
  parlay::integer_sort_inplace(seq);
  auto end_it = std::unique(seq.begin(), seq.end());
  return std::vector<VertexId>(seq.begin(), end_it);
}

void ParExactEngine::prepare_active_membership(const std::vector<VertexId>& active) {
  static constexpr std::size_t kSparseMembershipThreshold = 4096;
  active_membership_sparse_current_ = active.size() <= kSparseMembershipThreshold;
  if (active_membership_sparse_current_) {
    active_lookup_ = active;
    return;
  }
  active_lookup_.clear();

  ++current_active_stamp_;
  if (current_active_stamp_ == 0) {
    std::fill(active_stamp_.begin(), active_stamp_.end(), 0);
    current_active_stamp_ = 1;
  }
  for (std::size_t i = 0; i < active.size(); ++i) {
    const VertexId v = active[i];
    if (v < active_stamp_.size()) {
      active_stamp_[v] = current_active_stamp_;
      active_index_[v] = static_cast<int>(i);
    }
  }
}

bool ParExactEngine::is_active_vertex(VertexId v) const {
  if (active_membership_sparse_current_) {
    return std::binary_search(active_lookup_.begin(), active_lookup_.end(), v);
  }
  return v < active_stamp_.size() && active_stamp_[v] == current_active_stamp_;
}

int ParExactEngine::active_vertex_index(VertexId v) const {
  if (active_membership_sparse_current_) {
    const auto it = std::lower_bound(active_lookup_.begin(), active_lookup_.end(), v);
    if (it == active_lookup_.end() || *it != v) {
      return -1;
    }
    return static_cast<int>(std::distance(active_lookup_.begin(), it));
  }
  if (v >= active_stamp_.size() || active_stamp_[v] != current_active_stamp_) {
    return -1;
  }
  return active_index_[v];
}

void ParExactEngine::record_level_diagnostics(const std::vector<VertexId>& active) {
  if (!diagnostics_enabled_) {
    return;
  }
  std::vector<unsigned char> unavailable(palette_size(), 0);
  for (VertexId v : active) {
    if (v >= graph_.num_vertices()) {
      continue;
    }
    std::fill(unavailable.begin(), unavailable.end(), 0);
    for (VertexId u : direct_neighbors(v)) {
      if (levels_[u] >= levels_[v]) {
        ++diagnostics_.level_ge_neighbor_scans;
      }
      if (levels_[u] <= levels_[v]) {
        ++diagnostics_.level_le_neighbor_scans;
        const Color c = colors_[u];
        if (!is_active_vertex(u) && color_in_palette_range(c)) {
          unavailable[c] = 1;
        }
      }
    }
    std::uint64_t available = 0;
    for (unsigned char used : unavailable) {
      if (!used) {
        ++available;
      }
    }
    diagnostics_.level_palette_candidates_total += available;
    ++diagnostics_.level_diagnostic_vertices;
  }
}

std::vector<VertexId> ParExactEngine::collect_conflicted_vertices_from_inserted_edges(
    const UpdateBatch& batch) const {
  std::vector<VertexId> starts;
  starts.reserve(batch.size());
  for (const EdgeUpdate& update : batch) {
    if (update.kind != UpdateKind::Insert) {
      continue;
    }
    if (update.u >= graph_.num_vertices() || update.v >= graph_.num_vertices()) {
      continue;
    }
    if (colors_[update.u] == colors_[update.v]) {
      starts.push_back(choose_conflict_endpoint(update.u, update.v));
    }
  }
  return deduplicate_and_sort_vertices(starts);
}

Color ParExactEngine::first_available_color_with_offset(VertexId v, Color offset) const {
  const std::size_t palette = palette_size();
  // Thread-local scratch avoids a heap allocation per call inside parallel_for.
  // We grow the buffer lazily and clear only the entries we touch (O(deg) reset).
  thread_local std::vector<unsigned char> unavail;
  thread_local std::vector<Color> touched;
  if (unavail.size() < palette) unavail.assign(palette, 0);
  touched.clear();

  for (VertexId u : direct_neighbors(v)) {
    const Color c = colors_[u];
    if (color_in_palette_range(c) && !unavail[c]) {
      unavail[c] = 1;
      touched.push_back(c);
    }
  }

  Color result = kUncolored;
  for (std::size_t i = 0; i < palette; ++i) {
    const Color candidate = static_cast<Color>((static_cast<std::size_t>(offset) + i) % palette);
    if (!unavail[candidate]) {
      result = candidate;
      break;
    }
  }
  for (Color c : touched) unavail[c] = 0;
  return result;
}

Color ParExactEngine::first_level_available_color_with_offset(VertexId v, Color offset) const {
  const std::size_t palette = palette_size();

  // Fast path: use the pre-maintained LowerEqualUsed bitvector when available.
  // leu_bit_[v] only contains colors from *non-active* lower/equal-level neighbours;
  // active vertices were removed from leu_ when they joined the active set.
  if (v < leu_bit_.size() && !leu_bit_[v].empty()) {
    const auto& unavail = leu_bit_[v];
    for (std::size_t i = 0; i < palette; ++i) {
      const Color candidate =
          static_cast<Color>((static_cast<std::size_t>(offset) + i) % palette);
      if (!unavail[candidate]) return candidate;
    }
    return kUncolored;
  }

  // Fallback: build the unavailability set on the fly (used before leu_ is built).
  thread_local std::vector<unsigned char> unavail;
  thread_local std::vector<Color> touched;
  if (unavail.size() < palette) unavail.assign(palette, 0);
  touched.clear();

  for (VertexId u : direct_neighbors(v)) {
    if (is_active_vertex(u) || levels_[u] > levels_[v]) continue;
    const Color c = colors_[u];
    if (color_in_palette_range(c) && !unavail[c]) {
      unavail[c] = 1;
      touched.push_back(c);
    }
  }

  Color result = kUncolored;
  for (std::size_t i = 0; i < palette; ++i) {
    const Color candidate = static_cast<Color>((static_cast<std::size_t>(offset) + i) % palette);
    if (!unavail[candidate]) {
      result = candidate;
      break;
    }
  }
  for (Color c : touched) unavail[c] = 0;
  return result;
}

bool ParExactEngine::proposal_conflicts_non_active_neighbors(
    VertexId v, Color proposed_color) const {
  if (!color_in_palette_range(proposed_color)) {
    return true;
  }

  for (VertexId u : direct_neighbors(v)) {
    if (is_active_vertex(u)) {
      continue;
    }
    if (colors_[u] == proposed_color) {
      return true;
    }
  }
  return false;
}

bool ParExactEngine::proposal_conflicts_active_neighbors(
    VertexId v, Color proposed_color, const std::vector<Color>& proposed_colors) const {
  if (!color_in_palette_range(proposed_color)) {
    return true;
  }

  for (VertexId u : direct_neighbors(v)) {
    const int j = active_vertex_index(u);
    if (j < 0) {
      continue;
    }
    if (proposed_colors[static_cast<std::size_t>(j)] == proposed_color && u < v) {
      return true;
    }
  }
  return false;
}

std::vector<VertexId> ParExactEngine::collect_unresolved_frontier_from_candidates(
    const std::vector<VertexId>& candidates) const {
  const VertexId n = graph_.num_vertices();
  diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(candidates.size());

  // parlay::filter runs the predicate in parallel; result is already compacted.
  parlay::sequence<VertexId> seq(candidates.begin(), candidates.end());
  auto unresolved = parlay::filter(seq, [&](VertexId v) -> bool {
    if (v >= n) return false;
    const Color cv = colors_[v];
    for (VertexId u : direct_neighbors(v)) {
      if (colors_[u] == cv) return true;
    }
    return false;
  });

  // The filtered sequence preserves the original order (candidates are already
  // sorted), so deduplicate_and_sort_vertices just removes any duplicates.
  return deduplicate_and_sort_vertices(std::vector<VertexId>(unresolved.begin(), unresolved.end()));
}

Color ParExactEngine::sampled_level_aware_color(VertexId v, std::uint64_t round_index) const {
  const std::size_t palette = palette_size();
  const Color offset = deterministic_color_offset(round_index, v, 0x7f4a7c15ULL);

  // Fast path: use the pre-maintained LowerEqualUsed bitvector when available.
  // In the token repair path, active vertices have colors_[v] = kUncolored, so
  // they were already removed from leu_ and are correctly invisible here.
  if (v < leu_bit_.size() && !leu_bit_[v].empty()) {
    const auto& unavail = leu_bit_[v];
    for (std::size_t i = 0; i < palette; ++i) {
      const Color candidate =
          static_cast<Color>((static_cast<std::size_t>(offset) + i) % palette);
      if (!unavail[candidate]) return candidate;
    }
    return kUncolored;
  }

  // Fallback: build the unavailability set on the fly.
  thread_local std::vector<unsigned char> unavail;
  thread_local std::vector<Color> touched;
  if (unavail.size() < palette) unavail.assign(palette, 0);
  touched.clear();

  for (VertexId u : direct_neighbors(v)) {
    if (levels_[u] > levels_[v]) continue;
    const Color c = colors_[u];
    if (color_in_palette_range(c) && !unavail[c]) {
      unavail[c] = 1;
      touched.push_back(c);
    }
  }

  Color result = kUncolored;
  for (std::size_t i = 0; i < palette; ++i) {
    const Color candidate = static_cast<Color>((static_cast<std::size_t>(offset) + i) % palette);
    if (!unavail[candidate]) {
      result = candidate;
      break;
    }
  }
  for (Color c : touched) unavail[c] = 0;
  return result;
}

bool ParExactEngine::attempt_token_recolor_batch(const std::vector<VertexId>& initial_active,
                                                 std::size_t* vertices_touched,
                                                 std::uint64_t* rounds_attempted) {
  static constexpr std::size_t kSequentialThreshold = 512;
  std::vector<VertexId> active = deduplicate_and_sort_vertices(initial_active);
  if (active.empty()) {
    return true;
  }
  ++diagnostics_.repair_calls;
  ++diagnostics_.token_repair_calls;

  const bool use_leu = !leu_.empty();

  bool used_sequential = false;
  for (std::uint32_t round = 0; round < max_rounds_ && !active.empty(); ++round) {
    if (rounds_attempted != nullptr) {
      ++(*rounds_attempted);
    }
    if (vertices_touched != nullptr) {
      *vertices_touched += active.size();
    }

    prepare_active_membership(active);
    for (VertexId v : active) {
      if (v < graph_.num_vertices()) {
        const Color old_c = colors_[v];
        colors_[v] = kUncolored;
        // Remove this vertex's old color from neighbors' leu_.
        if (use_leu && old_c != kUncolored) {
          update_leu_for_color_change(v, old_c, kUncolored);
        }
      }
    }

    std::vector<Color> proposed(active.size(), kUncolored);
    std::vector<unsigned char> safe(active.size(), 0);
    std::vector<unsigned char> reject_reason(active.size(), 0);
    std::vector<VertexId> next_active;
    proposal_count_ += static_cast<std::uint64_t>(active.size());
    diagnostics_.active_size_round_total += static_cast<std::uint64_t>(active.size());
    record_level_diagnostics(active);
    if (active.size() > diagnostics_.max_active_size) {
      diagnostics_.max_active_size = static_cast<std::uint64_t>(active.size());
    }

    const bool sequential_path = active.size() <= kSequentialThreshold;
    if (sequential_path && !used_sequential) {
      ++sequential_fast_path_count_;
      used_sequential = true;
    }
    diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(active.size());
    if (sequential_path) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        proposed[i] = sampled_level_aware_color(active[i], round);
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        proposed[i] = sampled_level_aware_color(active[i], round);
      });
    }

    if (sequential_path) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        const VertexId v = active[i];
        const Color c = proposed[i];
        if (!color_in_palette_range(c)) {
          continue;
        }
        bool conflict = false;
        for (VertexId u : direct_neighbors(v)) {
          const int j = active_vertex_index(u);
          if (j >= 0 && proposed[static_cast<std::size_t>(j)] == c && u < v) {
            conflict = true;
            reject_reason[i] = 1;
            break;
          }
          if (j < 0 && levels_[u] <= levels_[v] && colors_[u] == c) {
            conflict = true;
            reject_reason[i] = 2;
            break;
          }
        }
        safe[i] = conflict ? 0 : 1;
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        const VertexId v = active[i];
        const Color c = proposed[i];
        if (!color_in_palette_range(c)) {
          return;
        }
        bool active_conflict = false;
        bool lower_equal_conflict = false;
        for (VertexId u : direct_neighbors(v)) {
          const int j = active_vertex_index(u);
          if (j >= 0 && proposed[static_cast<std::size_t>(j)] == c && u < v) {
            active_conflict = true;
            break;
          }
          if (j < 0 && levels_[u] <= levels_[v] && colors_[u] == c) {
            lower_equal_conflict = true;
            break;
          }
        }
        if (active_conflict) {
          reject_reason[i] = 1;
          return;
        }
        if (lower_equal_conflict) {
          reject_reason[i] = 2;
          return;
        }
        safe[i] = 1;
      });
    }

    diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(active.size());
    for (unsigned char reason : reject_reason) {
      if (reason == 1) {
        ++diagnostics_.token_active_conflict_rejections;
      } else if (reason == 2) {
        ++diagnostics_.token_lower_equal_conflict_rejections;
      }
    }

    for (std::size_t i = 0; i < active.size(); ++i) {
      if (!safe[i]) {
        next_active.push_back(active[i]);
        continue;
      }
      const VertexId v = active[i];
      const Color c = proposed[i];
      colors_[v] = c;
      ++logical_time_;
      timestamps_[v] = logical_time_;
      ++commit_count_;
      ++diagnostics_.token_safe_commits;
      // Restore the new color in neighbors' leu_.
      if (use_leu && c != kUncolored) {
        update_leu_for_color_change(v, kUncolored, c);
      }

      VertexId unique_higher_conflict = graph_.num_vertices();
      bool multiple_higher_conflicts = false;
      ++diagnostics_.direct_neighbor_scans;
      for (VertexId u : direct_neighbors(v)) {
        if (levels_[u] <= levels_[v] || colors_[u] != c) {
          continue;
        }
        if (unique_higher_conflict == graph_.num_vertices()) {
          unique_higher_conflict = u;
        } else {
          multiple_higher_conflicts = true;
          break;
        }
      }
      if (multiple_higher_conflicts) {
        ++diagnostics_.token_multi_higher_conflicts;
        next_active.push_back(v);
      } else if (unique_higher_conflict != graph_.num_vertices()) {
        ++diagnostics_.token_unique_higher_moves;
        next_active.push_back(unique_higher_conflict);
      }
    }

    active = deduplicate_and_sort_vertices(next_active);
    unresolved_count_ += static_cast<std::uint64_t>(active.size());
  }

  return active.empty();
}

// ---------------------------------------------------------------------------
// LowerEqualUsed maintenance
// ---------------------------------------------------------------------------

void ParExactEngine::rebuild_leu_all() {
  const VertexId n = graph_.num_vertices();
  const std::size_t palette = palette_size();

  // Allocate / resize per-vertex arrays.
  leu_.resize(n);
  leu_bit_.resize(n);
  parlay::parallel_for(0, n, [&](std::size_t v) {
    leu_[v].assign(palette, 0);
    leu_bit_[v].assign(palette, 0);
  });

  // For each vertex v, count the colors of its lower/equal-level neighbours.
  parlay::parallel_for(0, n, [&](std::size_t v) {
    for (VertexId u : direct_neighbors(v)) {
      if (levels_[u] > levels_[v]) continue;
      const Color c = colors_[u];
      if (!color_in_palette_range(c)) continue;
      if (leu_[v][c] < std::numeric_limits<std::uint16_t>::max()) {
        ++leu_[v][c];
      }
      leu_bit_[v][c] = 1;
    }
  });
}

void ParExactEngine::update_leu_for_edge(VertexId u, VertexId v, bool inserted) {
  const std::size_t palette = palette_size();
  // Update leu_[v] based on u's color (if level[u] <= level[v]).
  auto update_one = [&](VertexId target, VertexId source) {
    if (levels_[source] > levels_[target]) return;
    const Color c = colors_[source];
    if (!color_in_palette_range(c)) return;
    if (c >= palette) return;
    if (inserted) {
      if (leu_[target][c] < std::numeric_limits<std::uint16_t>::max()) {
        ++leu_[target][c];
      }
      leu_bit_[target][c] = 1;
    } else {
      if (leu_[target][c] > 0) {
        --leu_[target][c];
        if (leu_[target][c] == 0) leu_bit_[target][c] = 0;
      }
    }
  };
  update_one(v, u);
  update_one(u, v);
}

void ParExactEngine::update_leu_for_color_change(VertexId v, Color old_color,
                                                  Color new_color) {
  const std::size_t palette = palette_size();
  // Propagate the color change of v to all equal/higher-level neighbours.
  for (VertexId u : direct_neighbors(v)) {
    if (levels_[v] > levels_[u]) continue;  // only update when level[v] <= level[u]
    if (old_color != kUncolored && old_color < palette) {
      if (leu_[u][old_color] > 0) {
        --leu_[u][old_color];
        if (leu_[u][old_color] == 0) leu_bit_[u][old_color] = 0;
      }
    }
    if (new_color != kUncolored && new_color < palette) {
      if (leu_[u][new_color] < std::numeric_limits<std::uint16_t>::max()) {
        ++leu_[u][new_color];
      }
      leu_bit_[u][new_color] = 1;
    }
  }
}

Color ParExactEngine::greedy_color_for_vertex(VertexId v) const {
  const std::size_t palette = palette_size();
  thread_local std::vector<unsigned char> unavail;
  thread_local std::vector<Color> touched;
  if (unavail.size() < palette) unavail.assign(palette, 0);
  touched.clear();

  for (VertexId u : direct_neighbors(v)) {
    const Color c = colors_[u];
    if (c != kUncolored && color_in_palette_range(c) && !unavail[c]) {
      unavail[c] = 1;
      touched.push_back(c);
    }
  }

  Color result = kUncolored;
  for (Color c = 0; c <= graph_.delta_cap(); ++c) {
    if (!unavail[c]) {
      result = c;
      break;
    }
  }
  for (Color c : touched) unavail[c] = 0;

  if (result == kUncolored) {
    throw std::runtime_error("no available color in [0, delta_cap] during par_exact greedy coloring");
  }
  return result;
}

const std::unordered_set<VertexId>& ParExactEngine::direct_neighbors(VertexId v) const {
  return graph_.adjacency_set(v);
}

void ParExactEngine::recolor_all_greedy_exact() {
  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    colors_[v] = kUncolored;
  }
  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    colors_[v] = greedy_color_for_vertex(v);
  }
  // Fallback recoloring changes all colors; cheapest to rebuild leu_ from scratch.
  if (!leu_.empty()) {
    rebuild_leu_all();
  }
}

bool ParExactEngine::color_in_palette_range(Color c) const {
  return c != kUncolored && c < palette_size();
}

void ParExactEngine::validate_coloring_or_throw(const char* context) const {
  const ValidationResult graph_result = validate_graph_invariants(graph_);
  if (!graph_result.ok) {
    throw std::runtime_error(std::string("par_exact graph invalid after ") + context + ": " +
                             graph_result.message);
  }

  const ValidationResult coloring_result = validate_exact_coloring(graph_, colors_);
  if (!coloring_result.ok) {
    throw std::runtime_error(std::string("par_exact coloring invalid after ") + context + ": " +
                             coloring_result.message);
  }

  if (palette_size() > static_cast<std::size_t>(std::numeric_limits<Color>::max())) {
    throw std::runtime_error("par_exact palette size exceeds Color range");
  }
  for (Color c : colors_) {
    if (!color_in_palette_range(c)) {
      throw std::runtime_error(std::string("par_exact produced out-of-range color after ") + context);
    }
  }
}

}  // namespace dgcolor
