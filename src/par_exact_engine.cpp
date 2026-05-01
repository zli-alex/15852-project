#include "dgcolor/par_exact_engine.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

#include "dgcolor/validator.hpp"

namespace dgcolor {

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
  const BatchApplyResult result = graph_.apply_batch(batch);
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
    recolor_all_greedy_exact();
    ++fallback_count_;
    vertices_touched = static_cast<std::size_t>(graph_.num_vertices());
    vertices_touched_total_ += static_cast<std::uint64_t>(vertices_touched);
  }

  validate_coloring_or_throw("apply_batch");
  const auto end = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(end - start).count();
  return BatchStats{true, batch.size(), result.updates_applied, vertices_touched, seconds};
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
  const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
  for (VertexId u : nbrs) {
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

  const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
  for (VertexId u : nbrs) {
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
    const std::vector<VertexId>& active, const std::vector<Color>& proposed_colors) const {
  if (!color_in_palette_range(proposed_color)) {
    return true;
  }
  if (active.size() != proposed_colors.size()) {
    return true;
  }

  std::vector<int> active_index(graph_.num_vertices(), -1);
  for (std::size_t i = 0; i < active.size(); ++i) {
    if (active[i] < graph_.num_vertices()) {
      active_index[active[i]] = static_cast<int>(i);
    }
  }

  const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
  for (VertexId u : nbrs) {
    if (u >= active_mask.size() || !active_mask[u]) {
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
  for (VertexId v : candidates) {
    if (v >= graph_.num_vertices()) {
      continue;
    }
    const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
    bool conflicted = false;
    for (VertexId u : nbrs) {
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

  const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
  for (VertexId u : nbrs) {
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
