#include "dgcolor/seq_baseline_engine.hpp"

#include <stdexcept>
#include <vector>

namespace dgcolor {

SeqBaselineEngine::SeqBaselineEngine(VertexId num_vertices, Degree delta_cap)
    : graph_(num_vertices, delta_cap), colors_(num_vertices, kUncolored) {}

SeqBaselineEngine::SeqBaselineEngine(VertexId num_vertices, Degree delta_cap,
                                     const UpdateBatch& initial_updates)
    : graph_(num_vertices, delta_cap), colors_(num_vertices, kUncolored) {
  if (initial_updates.empty()) {
    return;
  }
  const BatchApplyResult init_result = graph_.apply_batch(initial_updates);
  if (init_result.status != UpdateStatus::Ok) {
    throw std::invalid_argument("initial graph updates were rejected: " +
                                std::string(update_status_name(init_result.status)));
  }
}

std::string SeqBaselineEngine::name() const {
  return "seq_baseline";
}

const GraphStore& SeqBaselineEngine::graph() const {
  return graph_;
}

Color SeqBaselineEngine::color_of(VertexId v) const {
  if (v >= graph_.num_vertices()) {
    return kUncolored;
  }
  return colors_[v];
}

parlay::sequence<Color> SeqBaselineEngine::colors() const {
  return colors_;
}

void SeqBaselineEngine::initialize_coloring() {
  recolor_all_greedy();
}

UpdateStats SeqBaselineEngine::apply_update(const EdgeUpdate&) {
  throw std::logic_error("SeqBaselineEngine::apply_update is not implemented in Step 1");
}

BatchStats SeqBaselineEngine::apply_batch(const UpdateBatch&) {
  throw std::logic_error("SeqBaselineEngine::apply_batch is not implemented in Step 1");
}

Color SeqBaselineEngine::greedy_color_for_vertex(VertexId v) const {
  const std::size_t palette_size = static_cast<std::size_t>(graph_.delta_cap()) + 1;
  std::vector<bool> unavailable(palette_size, false);

  const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
  for (VertexId u : nbrs) {
    const Color neighbor_color = colors_[u];
    if (neighbor_color != kUncolored && neighbor_color <= graph_.delta_cap()) {
      unavailable[neighbor_color] = true;
    }
  }

  for (Color c = 0; c <= graph_.delta_cap(); ++c) {
    if (!unavailable[c]) {
      return c;
    }
  }

  throw std::runtime_error("no available color in [0, delta_cap] during greedy coloring");
}

void SeqBaselineEngine::recolor_all_greedy() {
  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    colors_[v] = kUncolored;
  }

  for (VertexId v = 0; v < graph_.num_vertices(); ++v) {
    colors_[v] = greedy_color_for_vertex(v);
  }
}

}  // namespace dgcolor
