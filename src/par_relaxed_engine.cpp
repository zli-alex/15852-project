#include "dgcolor/par_relaxed_engine.hpp"

#include <algorithm>
#include <chrono>
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
      max_rounds_(max_rounds),
      active_stamp_(num_vertices, 0),
      active_index_(num_vertices, -1) {
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
  const auto graph_apply_start = std::chrono::steady_clock::now();
  const UpdateResult result = graph_.apply_update(update);
  diagnostics_.graph_apply_seconds +=
      SecondsBetween(graph_apply_start, std::chrono::steady_clock::now());
  if (result.status != UpdateStatus::Ok) {
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    return UpdateStats{false, 0, 0, seconds};
  }

  std::size_t vertices_touched = 0;
  if (update.kind == UpdateKind::Insert) {
    if (colors_[update.u] == colors_[update.v]) {
      std::chrono::steady_clock::time_point active_build_start;
      if (diagnostics_enabled_) {
        active_build_start = std::chrono::steady_clock::now();
      }
      parlay::sequence<VertexId> active = {update.u, update.v};
      if (diagnostics_enabled_) {
        diagnostics_.active_vertices_initial_total += static_cast<std::uint64_t>(active.size());
      }
      active = expand_with_neighbors(active);
      if (diagnostics_enabled_) {
        diagnostics_.active_vertices_expanded_total += static_cast<std::uint64_t>(active.size());
        diagnostics_.active_build_seconds +=
            SecondsBetween(active_build_start, std::chrono::steady_clock::now());
      }

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

  if (validate_after_apply_) {
    const auto validation_start = std::chrono::steady_clock::now();
    validate_coloring_or_throw("apply_update");
    diagnostics_.internal_validation_seconds +=
        SecondsBetween(validation_start, std::chrono::steady_clock::now());
  }

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
  const auto graph_apply_start = std::chrono::steady_clock::now();
  const BatchApplyResult result = graph_.apply_batch(batch);
  diagnostics_.graph_apply_seconds +=
      SecondsBetween(graph_apply_start, std::chrono::steady_clock::now());
  if (result.status != UpdateStatus::Ok) {
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    return BatchStats{false, batch.size(), 0, 0, seconds};
  }

  parlay::sequence<VertexId> active;
  std::chrono::steady_clock::time_point active_build_start;
  if (diagnostics_enabled_) {
    active_build_start = std::chrono::steady_clock::now();
  }
  active.reserve(batch.size() * 2);
  for (const EdgeUpdate& update : batch) {
    if (update.kind == UpdateKind::Insert) {
      active.push_back(update.u);
      active.push_back(update.v);
    }
  }
  if (diagnostics_enabled_) {
    diagnostics_.active_vertices_initial_total += static_cast<std::uint64_t>(active.size());
  }
  active = expand_with_neighbors(active);
  if (diagnostics_enabled_) {
    diagnostics_.active_vertices_expanded_total += static_cast<std::uint64_t>(active.size());
    diagnostics_.active_build_seconds +=
        SecondsBetween(active_build_start, std::chrono::steady_clock::now());
  }

  std::size_t vertices_touched = 0;
  if (!active.empty()) {
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

void ParRelaxedEngine::set_diagnostics_enabled(bool enabled) {
  diagnostics_enabled_ = enabled;
}

bool ParRelaxedEngine::diagnostics_enabled() const {
  return diagnostics_enabled_;
}

void ParRelaxedEngine::set_validate_after_apply(bool enabled) {
  validate_after_apply_ = enabled;
}

bool ParRelaxedEngine::validate_after_apply() const {
  return validate_after_apply_;
}

ParRelaxedDiagnostics ParRelaxedEngine::diagnostics() const {
  return diagnostics_;
}

Color ParRelaxedEngine::greedy_color_for_vertex(VertexId v) const {
  if (diagnostics_enabled_) {
    ++diagnostics_.neighbor_scans;
  }
  thread_local std::vector<unsigned char> tl_unavail;
  thread_local std::vector<Color> tl_touched;
  if (tl_unavail.size() < palette_size_) tl_unavail.assign(palette_size_, 0);
  tl_touched.clear();

  for (VertexId u : graph_.adjacency_set(v)) {
    const Color c = colors_[u];
    if (c != kUncolored && c < palette_size_ && !tl_unavail[c]) {
      tl_unavail[c] = 1;
      tl_touched.push_back(c);
    }
  }

  Color result = kUncolored;
  for (Color c = 0; c < palette_size_; ++c) {
    if (!tl_unavail[c]) {
      result = c;
      break;
    }
  }
  for (Color c : tl_touched) tl_unavail[c] = 0;

  if (result == kUncolored) {
    throw std::runtime_error("no available color in relaxed palette during initialization");
  }
  return result;
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

parlay::sequence<VertexId> ParRelaxedEngine::expand_with_neighbors(
    const parlay::sequence<VertexId>& seeds) const {
  if (seeds.empty()) return {};
  const VertexId n = graph_.num_vertices();

  // Build each seed's 1-hop neighbourhood (including the seed itself) in
  // parallel, then flatten, integer-sort and deduplicate.
  if (diagnostics_enabled_) {
    diagnostics_.neighbor_scans += static_cast<std::uint64_t>(seeds.size());
  }
  auto per_seed = parlay::tabulate(seeds.size(), [&](std::size_t i) {
    const VertexId v = seeds[i];
    if (v >= n) return parlay::sequence<VertexId>{};
    const auto& adj = graph_.adjacency_set(v);
    parlay::sequence<VertexId> result;
    result.reserve(adj.size() + 1);
    result.push_back(v);
    for (VertexId u : adj) result.push_back(u);
    return result;
  });

  auto flat = parlay::flatten(std::move(per_seed));
  // integer_sort is O(n) for uint32_t keys and runs in parallel.
  auto sorted = parlay::integer_sort(flat);
  return parlay::unique(sorted);
}

parlay::sequence<VertexId> ParRelaxedEngine::collect_conflicted_vertices_from_candidates(
    const parlay::sequence<VertexId>& candidates) const {
  const VertexId n = graph_.num_vertices();
  if (diagnostics_enabled_) {
    diagnostics_.neighbor_scans += static_cast<std::uint64_t>(candidates.size());
  }
  // parlay::filter runs the predicate in parallel.
  return parlay::filter(candidates, [&](VertexId v) -> bool {
    if (v >= n) return false;
    const Color cv = colors_[v];
    for (VertexId u : graph_.adjacency_set(v)) {
      if (colors_[u] == cv) return true;
    }
    return false;
  });
}

void ParRelaxedEngine::prepare_active_membership(const parlay::sequence<VertexId>& active) {
  active_membership_sparse_current_ = active.size() <= kSparseMembershipThreshold;
  if (active_membership_sparse_current_) {
    // Keep a sorted copy for binary-search lookup.
    active_lookup_.assign(active.begin(), active.end());
    // active is already sorted (output of integer_sort + unique).
    return;
  }
  active_lookup_.clear();

  // Dense stamp-array mode: increment the generation counter and tag each
  // active vertex.  If the counter wraps, clear the whole array first.
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

bool ParRelaxedEngine::is_active_vertex(VertexId v) const {
  if (active_membership_sparse_current_) {
    return std::binary_search(active_lookup_.begin(), active_lookup_.end(), v);
  }
  return v < active_stamp_.size() && active_stamp_[v] == current_active_stamp_;
}

int ParRelaxedEngine::active_vertex_index(VertexId v) const {
  if (active_membership_sparse_current_) {
    const auto it = std::lower_bound(active_lookup_.begin(), active_lookup_.end(), v);
    if (it == active_lookup_.end() || *it != v) return -1;
    return static_cast<int>(it - active_lookup_.begin());
  }
  if (v >= active_stamp_.size() || active_stamp_[v] != current_active_stamp_) return -1;
  return active_index_[v];
}

bool ParRelaxedEngine::attempt_parallel_repair(const parlay::sequence<VertexId>& initial_active,
                                               std::size_t* vertices_touched,
                                               std::uint64_t* rounds_attempted) {
  const bool diagnostics_enabled = diagnostics_enabled_;
  if (diagnostics_enabled) {
    ++diagnostics_.repair_calls;
  }
  std::chrono::steady_clock::time_point repair_start;
  if (diagnostics_enabled) {
    repair_start = std::chrono::steady_clock::now();
  }

  std::chrono::steady_clock::time_point active_build_start;
  if (diagnostics_enabled) {
    active_build_start = std::chrono::steady_clock::now();
  }
  // initial_active is already sorted & deduped (output of expand_with_neighbors).
  parlay::sequence<VertexId> active =
      collect_conflicted_vertices_from_candidates(initial_active);
  if (diagnostics_enabled) {
    diagnostics_.conflicted_vertices_initial_total += static_cast<std::uint64_t>(active.size());
    diagnostics_.active_build_seconds +=
        SecondsBetween(active_build_start, std::chrono::steady_clock::now());
  }

  if (active.empty()) {
    if (diagnostics_enabled) {
      diagnostics_.repair_seconds += SecondsBetween(repair_start, std::chrono::steady_clock::now());
    }
    return true;
  }

  bool used_sequential_repair = false;
  for (std::uint64_t round = 0; round < max_rounds_ && !active.empty(); ++round) {
    if (rounds_attempted != nullptr) {
      ++(*rounds_attempted);
    }
    if (diagnostics_enabled) {
      ++diagnostics_.repair_rounds;
    }
    if (vertices_touched != nullptr) {
      *vertices_touched += active.size();
    }
    diagnostics_.active_size_round_total += static_cast<std::uint64_t>(active.size());
    if (active.size() > diagnostics_.max_active_size) {
      diagnostics_.max_active_size = static_cast<std::uint64_t>(active.size());
    }

    // Build the active-index map once per round using the stamp-based scheme
    // (avoids an O(n) vector alloc + fill on every round).
    prepare_active_membership(active);

    parlay::sequence<Color> proposed(active.size(), kUncolored);
    parlay::sequence<unsigned char> safe(active.size(), 0);
    std::vector<std::uint64_t> proposal_neighbor_scans(
        diagnostics_enabled ? active.size() : 0, 0);
    std::vector<std::uint64_t> safety_neighbor_scans(
        diagnostics_enabled ? active.size() : 0, 0);

    const bool sequential_round = active.size() <= kSequentialRepairThreshold;
    if (sequential_round) {
      if (diagnostics_enabled) {
        if (!used_sequential_repair) {
          ++diagnostics_.sequential_repair_calls;
        }
        ++diagnostics_.sequential_repair_rounds;
      }
      used_sequential_repair = true;

      // Thread-local scratch avoids a heap allocation per vertex and converts
      // the O(deg × palette) nested loop to O(deg + palette).
      thread_local std::vector<unsigned char> tl_unavail;
      thread_local std::vector<Color> tl_touched;
      for (std::size_t i = 0; i < active.size(); ++i) {
        const VertexId v = active[i];
        const Color offset = deterministic_color_offset(round, v);
        const auto& adj = graph_.adjacency_set(v);
        if (diagnostics_enabled) {
          ++proposal_neighbor_scans[i];
        }
        if (tl_unavail.size() < palette_size_) tl_unavail.assign(palette_size_, 0);
        tl_touched.clear();
        for (VertexId u : adj) {
          const Color c = colors_[u];
          if (color_in_palette_range(c) && !tl_unavail[c]) {
            tl_unavail[c] = 1;
            tl_touched.push_back(c);
          }
        }
        for (Color attempt = 0; attempt < palette_size_; ++attempt) {
          const Color candidate = static_cast<Color>((offset + attempt) % palette_size_);
          if (!tl_unavail[candidate]) {
            proposed[i] = candidate;
            break;
          }
        }
        for (Color c : tl_touched) tl_unavail[c] = 0;
      }

      for (std::size_t i = 0; i < active.size(); ++i) {
        const VertexId v = active[i];
        const Color candidate = proposed[i];
        if (!color_in_palette_range(candidate)) {
          continue;
        }

        bool ok = true;
        if (diagnostics_enabled) {
          ++safety_neighbor_scans[i];
        }
        for (VertexId u : graph_.adjacency_set(v)) {
          if (colors_[u] == candidate) {
            ok = false;
            break;
          }
          const int j = active_vertex_index(u);
          if (j >= 0 && proposed[static_cast<std::size_t>(j)] == candidate && u < v) {
            // Tie-break: adjacent active vertices proposing the same color —
            // only the lower-id vertex may commit this round.
            ok = false;
            break;
          }
        }
        safe[i] = ok ? 1 : 0;
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        const VertexId v = active[i];
        const Color offset = deterministic_color_offset(round, v);
        const auto& adj = graph_.adjacency_set(v);
        if (diagnostics_enabled) {
          ++proposal_neighbor_scans[i];
        }
        // Per-thread scratch: O(deg + palette) instead of O(deg × palette).
        thread_local std::vector<unsigned char> tl_unavail;
        thread_local std::vector<Color> tl_touched;
        if (tl_unavail.size() < palette_size_) tl_unavail.assign(palette_size_, 0);
        tl_touched.clear();
        for (VertexId u : adj) {
          const Color c = colors_[u];
          if (color_in_palette_range(c) && !tl_unavail[c]) {
            tl_unavail[c] = 1;
            tl_touched.push_back(c);
          }
        }
        for (Color attempt = 0; attempt < palette_size_; ++attempt) {
          const Color candidate = static_cast<Color>((offset + attempt) % palette_size_);
          if (!tl_unavail[candidate]) {
            proposed[i] = candidate;
            break;
          }
        }
        for (Color c : tl_touched) tl_unavail[c] = 0;
      });

      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        const VertexId v = active[i];
        const Color candidate = proposed[i];
        if (!color_in_palette_range(candidate)) {
          return;
        }

        bool ok = true;
        if (diagnostics_enabled) {
          ++safety_neighbor_scans[i];
        }
        for (VertexId u : graph_.adjacency_set(v)) {
          if (colors_[u] == candidate) {
            ok = false;
            break;
          }
          const int j = active_vertex_index(u);
          if (j >= 0 && proposed[static_cast<std::size_t>(j)] == candidate && u < v) {
            ok = false;
            break;
          }
        }
        safe[i] = ok ? 1 : 0;
      });
    }

    if (diagnostics_enabled) {
      for (std::uint64_t scans : proposal_neighbor_scans) {
        diagnostics_.neighbor_scans += scans;
      }
      for (std::uint64_t scans : safety_neighbor_scans) {
        diagnostics_.neighbor_scans += scans;
      }
      for (std::size_t i = 0; i < safe.size(); ++i) {
        if (safe[i]) {
          ++diagnostics_.commits_total;
        }
      }
    }

    if (sequential_round) {
      for (std::size_t i = 0; i < active.size(); ++i) {
        if (safe[i]) {
          colors_[active[i]] = proposed[i];
        }
      }
    } else {
      parlay::parallel_for(0, active.size(), [&](std::size_t i) {
        if (safe[i]) {
          colors_[active[i]] = proposed[i];
        }
      });
    }

    const parlay::sequence<VertexId> expanded = expand_with_neighbors(active);
    active = collect_conflicted_vertices_from_candidates(expanded);
  }

  const bool repaired = active.empty();
  if (diagnostics_enabled) {
    diagnostics_.repair_seconds += SecondsBetween(repair_start, std::chrono::steady_clock::now());
  }
  return repaired;
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
