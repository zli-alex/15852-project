#include "dgcolor/par_relaxed_engine.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

namespace dgcolor {

Color ParRelaxedEngine::compute_palette_size(Degree delta_cap, std::uint32_t palette_multiplier) {
  if (palette_multiplier == 0) {
    throw std::invalid_argument("ParRelaxedEngine requires palette_multiplier >= 1");
  }

  const std::uint64_t product = static_cast<std::uint64_t>(palette_multiplier) *
                                static_cast<std::uint64_t>(delta_cap);
  const std::uint64_t palette_size_u64 = product + 1ULL;
  if (palette_size_u64 > static_cast<std::uint64_t>(std::numeric_limits<Color>::max())) {
    throw std::invalid_argument("ParRelaxedEngine palette_size overflows Color");
  }
  return static_cast<Color>(palette_size_u64);
}

ParRelaxedEngine::ParRelaxedEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                                   std::uint32_t palette_multiplier, std::uint32_t max_rounds)
    : graph_(num_vertices, delta_cap),
      colors_(num_vertices, kUncolored),
      seed_(seed),
      palette_multiplier_(palette_multiplier),
      palette_size_(compute_palette_size(delta_cap, palette_multiplier)),
      max_rounds_(max_rounds) {
  if (max_rounds_ == 0) {
    throw std::invalid_argument("ParRelaxedEngine requires max_rounds >= 1");
  }
}

ParRelaxedEngine::ParRelaxedEngine(VertexId num_vertices, Degree delta_cap, std::uint64_t seed,
                                   std::uint32_t palette_multiplier, std::uint32_t max_rounds,
                                   const UpdateBatch& initial_updates)
    : ParRelaxedEngine(num_vertices, delta_cap, seed, palette_multiplier, max_rounds) {
  if (initial_updates.empty()) {
    return;
  }
  const BatchApplyResult init_result = graph_.apply_batch(initial_updates);
  if (init_result.status != UpdateStatus::Ok) {
    throw std::invalid_argument("initial graph updates were rejected: " +
                                std::string(update_status_name(init_result.status)));
  }
}

std::string ParRelaxedEngine::name() const {
  return "par_relaxed";
}

const GraphStore& ParRelaxedEngine::graph() const {
  return graph_;
}

Color ParRelaxedEngine::color_of(VertexId v) const {
  if (v >= graph_.num_vertices()) {
    return kUncolored;
  }
  return colors_[v];
}

parlay::sequence<Color> ParRelaxedEngine::colors() const {
  return colors_;
}

void ParRelaxedEngine::initialize_coloring() {
  recolor_all_greedy_relaxed();
  initialized_ = true;
}

UpdateStats ParRelaxedEngine::apply_update(const EdgeUpdate&) {
  throw std::logic_error("ParRelaxedEngine::apply_update is not implemented in Step 1");
}

BatchStats ParRelaxedEngine::apply_batch(const UpdateBatch&) {
  throw std::logic_error("ParRelaxedEngine::apply_batch is not implemented in Step 1");
}

std::uint32_t ParRelaxedEngine::palette_multiplier() const {
  return palette_multiplier_;
}

Color ParRelaxedEngine::palette_size() const {
  return palette_size_;
}

std::uint32_t ParRelaxedEngine::max_rounds() const {
  return max_rounds_;
}

std::uint64_t ParRelaxedEngine::total_rounds() const {
  return total_rounds_;
}

std::uint64_t ParRelaxedEngine::fallback_count() const {
  return fallback_count_;
}

std::uint64_t ParRelaxedEngine::vertices_touched_total() const {
  return vertices_touched_total_;
}

Color ParRelaxedEngine::greedy_color_for_vertex(VertexId v) const {
  const std::size_t palette_size = static_cast<std::size_t>(palette_size_);
  std::vector<bool> unavailable(palette_size, false);

  const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
  for (VertexId u : nbrs) {
    const Color neighbor_color = colors_[u];
    if (neighbor_color != kUncolored && neighbor_color < palette_size_) {
      unavailable[neighbor_color] = true;
    }
  }

  for (Color c = 0; c < palette_size_; ++c) {
    if (!unavailable[c]) {
      return c;
    }
  }

  throw std::runtime_error("no available color in relaxed palette during initialization");
}

void ParRelaxedEngine::recolor_all_greedy_relaxed() {
  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    colors_[v] = kUncolored;
  }
  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    colors_[v] = greedy_color_for_vertex(v);
  }
  vertices_touched_total_ += static_cast<std::uint64_t>(graph_.num_vertices());
}

}  // namespace dgcolor
