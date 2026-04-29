#include "dgcolor/par_relaxed_engine.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

#include <parlay/parallel.h>

#include "dgcolor/validator.hpp"

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

UpdateStats ParRelaxedEngine::apply_update(const EdgeUpdate& update) {
  if (!initialized_) {
    throw std::logic_error(
        "ParRelaxedEngine::apply_update requires initialize_coloring() first");
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
    if (colors_[update.u] == colors_[update.v]) {
      std::vector<VertexId> active = {update.u, update.v};
      active = expand_with_neighbors(active);

      std::uint64_t rounds_attempted = 0;
      const bool repaired = attempt_parallel_repair(active, &vertices_touched, &rounds_attempted);
      total_rounds_ += rounds_attempted;
      vertices_touched_total_ += static_cast<std::uint64_t>(vertices_touched);

      if (!repaired) {
        ++fallback_count_;
        recolor_all_greedy_relaxed();
        vertices_touched += static_cast<std::size_t>(graph_.num_vertices());
      }
    }
  }

  validate_coloring_or_throw("apply_update");

  const auto end = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(end - start).count();
  return UpdateStats{true, 1, vertices_touched, seconds};
}

BatchStats ParRelaxedEngine::apply_batch(const UpdateBatch& batch) {
  if (!initialized_) {
    throw std::logic_error(
        "ParRelaxedEngine::apply_batch requires initialize_coloring() first");
  }

  const auto start = std::chrono::steady_clock::now();
  const BatchApplyResult result = graph_.apply_batch(batch);
  if (result.status != UpdateStatus::Ok) {
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    return BatchStats{false, batch.size(), 0, 0, seconds};
  }

  recolor_all_greedy_relaxed();
  validate_coloring_or_throw("apply_batch");

  const auto end = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(end - start).count();
  return BatchStats{true, batch.size(), result.updates_applied,
                    static_cast<std::size_t>(graph_.num_vertices()), seconds};
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

bool ParRelaxedEngine::color_in_palette_range(Color c) const {
  return c != kUncolored && c < palette_size_;
}

std::uint64_t ParRelaxedEngine::deterministic_hash(std::uint64_t round_index, VertexId v,
                                                   std::uint64_t salt) const {
  std::uint64_t x = seed_ ^ (round_index + 0x9e3779b97f4a7c15ULL) ^
                    (static_cast<std::uint64_t>(v) * 0xbf58476d1ce4e5b9ULL) ^ salt;
  x ^= x >> 33U;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33U;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33U;
  return x;
}

Color ParRelaxedEngine::deterministic_color_offset(std::uint64_t round_index, VertexId v,
                                                   std::uint64_t salt) const {
  return static_cast<Color>(deterministic_hash(round_index, v, salt) %
                            static_cast<std::uint64_t>(palette_size_));
}

std::vector<VertexId> ParRelaxedEngine::expand_with_neighbors(const std::vector<VertexId>& seeds) const {
  const VertexId n = graph_.num_vertices();
  std::vector<unsigned char> mark(n, 0);
  for (VertexId v : seeds) {
    if (v >= n) {
      continue;
    }
    mark[v] = 1;
    const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
    for (VertexId u : nbrs) {
      mark[u] = 1;
    }
  }
  std::vector<VertexId> out;
  out.reserve(seeds.size() * 2 + 1);
  for (VertexId v = 0; v < n; ++v) {
    if (mark[v]) {
      out.push_back(v);
    }
  }
  return out;
}

std::vector<VertexId> ParRelaxedEngine::collect_conflicted_vertices_from_candidates(
    const std::vector<VertexId>& candidates) const {
  const VertexId n = graph_.num_vertices();
  std::vector<unsigned char> visited(n, 0);
  std::vector<VertexId> conflicted;
  conflicted.reserve(candidates.size());
  for (VertexId v : candidates) {
    if (v >= n || visited[v]) {
      continue;
    }
    visited[v] = 1;
    const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
    bool conflict = false;
    for (VertexId u : nbrs) {
      if (colors_[u] == colors_[v]) {
        conflict = true;
        break;
      }
    }
    if (conflict) {
      conflicted.push_back(v);
    }
  }
  return conflicted;
}

bool ParRelaxedEngine::attempt_parallel_repair(const std::vector<VertexId>& initial_active,
                                               std::size_t* vertices_touched,
                                               std::uint64_t* rounds_attempted) {
  std::vector<VertexId> active = collect_conflicted_vertices_from_candidates(initial_active);
  if (active.empty()) {
    return true;
  }

  for (std::uint64_t round = 0; round < max_rounds_ && !active.empty(); ++round) {
    if (rounds_attempted != nullptr) {
      ++(*rounds_attempted);
    }
    if (vertices_touched != nullptr) {
      *vertices_touched += active.size();
    }

    const VertexId n = graph_.num_vertices();
    std::vector<int> active_index(n, -1);
    for (std::size_t i = 0; i < active.size(); ++i) {
      active_index[active[i]] = static_cast<int>(i);
    }

    std::vector<Color> proposed(active.size(), kUncolored);
    std::vector<unsigned char> safe(active.size(), 0);

    parlay::parallel_for(0, active.size(), [&](std::size_t i) {
      const VertexId v = active[i];
      const Color offset = deterministic_color_offset(round, v);
      for (Color attempt = 0; attempt < palette_size_; ++attempt) {
        const Color candidate = static_cast<Color>((offset + attempt) % palette_size_);
        bool available = true;
        const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
        for (VertexId u : nbrs) {
          if (colors_[u] == candidate) {
            available = false;
            break;
          }
        }
        if (available) {
          proposed[i] = candidate;
          return;
        }
      }
    });

    parlay::parallel_for(0, active.size(), [&](std::size_t i) {
      const VertexId v = active[i];
      const Color candidate = proposed[i];
      if (!color_in_palette_range(candidate)) {
        return;
      }

      bool ok = true;
      const parlay::sequence<VertexId> nbrs = graph_.neighbors(v);
      for (VertexId u : nbrs) {
        if (colors_[u] == candidate) {
          ok = false;
          break;
        }

        const int j = active_index[u];
        if (j >= 0 && proposed[static_cast<std::size_t>(j)] == candidate && u < v) {
          // Conservative deterministic tie-break: for adjacent active vertices
          // proposing the same color, only the lower vertex id may commit.
          ok = false;
          break;
        }
      }
      safe[i] = ok ? 1 : 0;
    });

    parlay::parallel_for(0, active.size(), [&](std::size_t i) {
      if (safe[i]) {
        colors_[active[i]] = proposed[i];
      }
    });

    const std::vector<VertexId> expanded = expand_with_neighbors(active);
    active = collect_conflicted_vertices_from_candidates(expanded);
  }

  return active.empty();
}

void ParRelaxedEngine::validate_coloring_or_throw(const char* context) const {
  const ValidationResult coloring_result = validate_exact_coloring(graph_, colors_);
  if (!coloring_result.ok) {
    throw std::runtime_error(std::string("par_relaxed produced invalid coloring after ") + context +
                             ": " + coloring_result.message);
  }
  for (Color c : colors_) {
    if (!color_in_palette_range(c)) {
      throw std::runtime_error(std::string("par_relaxed produced out-of-range color after ") +
                               context);
    }
  }
}

}  // namespace dgcolor
