#include "dgcolor/seq_baseline_engine.hpp"

#include <chrono>
#include <stdexcept>
#include <vector>

#include "dgcolor/validator.hpp"

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
  initialized_ = true;
}

UpdateStats SeqBaselineEngine::apply_update(const EdgeUpdate& update) {
  if (!initialized_) {
    throw std::logic_error(
        "SeqBaselineEngine::apply_update requires initialize_coloring() first");
  }

  const auto start = std::chrono::steady_clock::now();
  const UpdateResult result = graph_.apply_update(update);
  if (result.status != UpdateStatus::Ok) {
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    return UpdateStats{false, 0, 0, seconds};
  }

  std::size_t vertices_touched = 0;
  if (update.kind == UpdateKind::Insert) {
    recolor_all_greedy();
    vertices_touched = static_cast<std::size_t>(graph_.num_vertices());
  }
  const ValidationResult coloring_result = validate_exact_coloring(graph_, colors_);
  if (!coloring_result.ok) {
    throw std::runtime_error("seq_baseline produced invalid coloring after apply_update: " +
                             coloring_result.message);
  }

  const auto end = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(end - start).count();
  return UpdateStats{true, 1, vertices_touched, seconds};
}

BatchStats SeqBaselineEngine::apply_batch(const UpdateBatch& batch) {
  if (!initialized_) {
    throw std::logic_error(
        "SeqBaselineEngine::apply_batch requires initialize_coloring() first");
  }

  const auto start = std::chrono::steady_clock::now();
  const BatchApplyResult result = graph_.apply_batch(batch);
  if (result.status != UpdateStatus::Ok) {
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    return BatchStats{false, batch.size(), 0, 0, seconds};
  }

  recolor_all_greedy();
  const ValidationResult coloring_result = validate_exact_coloring(graph_, colors_);
  if (!coloring_result.ok) {
    throw std::runtime_error("seq_baseline produced invalid coloring after apply_batch: " +
                             coloring_result.message);
  }

  const auto end = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(end - start).count();
  return BatchStats{true, batch.size(), result.updates_applied,
                    static_cast<std::size_t>(graph_.num_vertices()), seconds};
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
