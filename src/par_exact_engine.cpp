#include "dgcolor/par_exact_engine.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

#include <parlay/parallel.h>

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
      max_rounds_(max_rounds) {
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
  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    levels_[v] = deterministic_level_for_vertex(v);
    timestamps_[v] = 0;
  }
  recolor_all_greedy_exact();
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
      const bool repaired = attempt_recolor_batch(initial_active, &vertices_touched, &rounds_attempted);
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
  static constexpr std::size_t kSequentialThreshold = 128;
  std::vector<VertexId> active = deduplicate_and_sort_vertices(initial_active);
  if (active.empty()) {
    return true;
  }
  ++diagnostics_.repair_calls;

  bool used_sequential = false;
  for (std::uint32_t round = 0; round < max_rounds_ && !active.empty(); ++round) {
    if (rounds_attempted != nullptr) {
      ++(*rounds_attempted);
    }
    if (vertices_touched != nullptr) {
      *vertices_touched += active.size();
    }

    std::vector<unsigned char> active_mask = build_active_membership(active);
    std::vector<int> active_index(graph_.num_vertices(), -1);
    for (std::size_t i = 0; i < active.size(); ++i) {
      if (active[i] < graph_.num_vertices()) {
        active_index[active[i]] = static_cast<int>(i);
      }
    }
    std::vector<Color> proposed(active.size(), kUncolored);
    std::vector<unsigned char> safe(active.size(), 0);
    std::vector<Color> colors_before(active.size(), kUncolored);
    for (std::size_t i = 0; i < active.size(); ++i) {
      colors_before[i] = colors_[active[i]];
    }
    proposal_count_ += static_cast<std::uint64_t>(active.size());
    diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(active.size());
    diagnostics_.active_size_round_total += static_cast<std::uint64_t>(active.size());
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
        proposed[i] = first_available_color_with_offset(v, offset);
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        const VertexId v = active[i];
        const Color offset = deterministic_color_offset(round, v);
        proposed[i] = first_available_color_with_offset(v, offset);
      });
    }

    if (sequential_path) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        const VertexId v = active[i];
        const Color c = proposed[i];
        if (!color_in_palette_range(c)) {
          continue;
        }
        if (proposal_conflicts_non_active_neighbors(v, c, active_mask)) {
          continue;
        }
        if (proposal_conflicts_active_neighbors(v, c, active_mask, active_index, proposed)) {
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
        if (proposal_conflicts_non_active_neighbors(v, c, active_mask)) {
          return;
        }
        if (proposal_conflicts_active_neighbors(v, c, active_mask, active_index, proposed)) {
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

    std::vector<VertexId> frontier = active;
    diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(active.size());
    for (VertexId v : active) {
      for (VertexId u : direct_neighbors(v)) {
        frontier.push_back(u);
      }
    }
    frontier = deduplicate_and_sort_vertices(frontier);
    active = collect_unresolved_frontier_from_candidates(frontier);
    unresolved_count_ += static_cast<std::uint64_t>(active.size());
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
  const std::uint64_t mixed =
      mix_u64(seed_ ^ (static_cast<std::uint64_t>(v) + 0x9e3779b97f4a7c15ULL));
  const std::uint64_t modulo = static_cast<std::uint64_t>(graph_.delta_cap()) + 1ULL;
  return static_cast<Level>(mixed % modulo);
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
  std::vector<VertexId> out = vertices;
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<unsigned char> ParExactEngine::build_active_membership(
    const std::vector<VertexId>& active) const {
  std::vector<unsigned char> mask(graph_.num_vertices(), 0);
  for (VertexId v : active) {
    if (v < graph_.num_vertices()) {
      mask[v] = 1;
    }
  }
  return mask;
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
  std::vector<unsigned char> unavailable(palette, 0);
  for (VertexId u : direct_neighbors(v)) {
    const Color c = colors_[u];
    if (color_in_palette_range(c)) {
      unavailable[c] = 1;
    }
  }

  for (std::size_t i = 0; i < palette; ++i) {
    const Color candidate = static_cast<Color>((static_cast<std::size_t>(offset) + i) % palette);
    if (!unavailable[candidate]) {
      return candidate;
    }
  }
  return kUncolored;
}

bool ParExactEngine::proposal_conflicts_non_active_neighbors(
    VertexId v, Color proposed_color, const std::vector<unsigned char>& active_mask) const {
  if (!color_in_palette_range(proposed_color)) {
    return true;
  }

  for (VertexId u : direct_neighbors(v)) {
    if (u < active_mask.size() && active_mask[u]) {
      continue;
    }
    if (colors_[u] == proposed_color) {
      return true;
    }
  }
  return false;
}

bool ParExactEngine::proposal_conflicts_active_neighbors(
    VertexId v, Color proposed_color, const std::vector<unsigned char>& active_mask,
    const std::vector<int>& active_index, const std::vector<Color>& proposed_colors) const {
  if (!color_in_palette_range(proposed_color)) {
    return true;
  }

  for (VertexId u : direct_neighbors(v)) {
    if (u >= active_mask.size() || !active_mask[u] || u >= active_index.size()) {
      continue;
    }
    const int j = active_index[u];
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
  std::vector<VertexId> unresolved;
  unresolved.reserve(candidates.size());
  diagnostics_.direct_neighbor_scans += static_cast<std::uint64_t>(candidates.size());
  for (VertexId v : candidates) {
    if (v >= graph_.num_vertices()) {
      continue;
    }
    bool conflicted = false;
    for (VertexId u : direct_neighbors(v)) {
      if (colors_[u] == colors_[v]) {
        conflicted = true;
        break;
      }
    }
    if (conflicted) {
      unresolved.push_back(v);
    }
  }
  return deduplicate_and_sort_vertices(unresolved);
}

Color ParExactEngine::greedy_color_for_vertex(VertexId v) const {
  const std::size_t palette = palette_size();
  std::vector<unsigned char> unavailable(palette, 0);

  for (VertexId u : direct_neighbors(v)) {
    const Color neighbor_color = colors_[u];
    if (neighbor_color != kUncolored && color_in_palette_range(neighbor_color)) {
      unavailable[neighbor_color] = 1;
    }
  }

  for (Color c = 0; c <= graph_.delta_cap(); ++c) {
    if (!unavailable[c]) {
      return c;
    }
  }

  throw std::runtime_error("no available color in [0, delta_cap] during par_exact greedy coloring");
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
