#include "dgcolor/par_exact_engine.hpp"

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

UpdateStats ParExactEngine::apply_update(const EdgeUpdate& /*update*/) {
  throw std::logic_error("ParExactEngine::apply_update is not implemented (Step 1 skeleton)");
}

BatchStats ParExactEngine::apply_batch(const UpdateBatch& /*batch*/) {
  throw std::logic_error("ParExactEngine::apply_batch is not implemented (Step 1 skeleton)");
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
