#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "dgcolor/graph_store.hpp"
#include "dgcolor/rng.hpp"
#include "dgcolor/seq_exact_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::AdjacencyGraphStore;
using dgcolor::BatchStats;
using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::GraphStore;
using dgcolor::Rng;
using dgcolor::SeqExactEngine;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateStats;
using dgcolor::VertexId;

struct SeqExactStatsSnapshot {
  std::uint64_t recolor_calls;
  std::uint64_t recolored_vertices_total;
  std::uint64_t cascade_steps_total;
  std::uint64_t full_fallback_count;
  std::uint64_t level_conflict_choices;
};

void Require(bool condition, const std::string& context) {
  if (condition) {
    return;
  }
  std::cerr << "fuzz_failure=" << context << "\n";
  assert(false);
}

std::string OpKindName(UpdateKind kind) {
  return kind == UpdateKind::Insert ? "insert" : "delete";
}

SeqExactStatsSnapshot SnapshotStats(const SeqExactEngine& engine) {
  return SeqExactStatsSnapshot{
      engine.recolor_calls(),       engine.recolored_vertices_total(),
      engine.cascade_steps_total(), engine.full_fallback_count(),
      engine.level_conflict_choices()};
}

bool SameStats(const SeqExactStatsSnapshot& lhs, const SeqExactStatsSnapshot& rhs) {
  return lhs.recolor_calls == rhs.recolor_calls &&
         lhs.recolored_vertices_total == rhs.recolored_vertices_total &&
         lhs.cascade_steps_total == rhs.cascade_steps_total &&
         lhs.full_fallback_count == rhs.full_fallback_count &&
         lhs.level_conflict_choices == rhs.level_conflict_choices;
}

std::vector<std::uint64_t> SnapshotEdgeKeys(const GraphStore& graph) {
  std::vector<std::uint64_t> keys;
  const VertexId n = graph.num_vertices();
  for (VertexId u = 0; u < n; ++u) {
    for (VertexId v = static_cast<VertexId>(u + 1); v < n; ++v) {
      if (graph.has_edge(u, v)) {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(u) << 32U) | static_cast<std::uint64_t>(v);
        keys.push_back(key);
      }
    }
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

void RequireGraphTopologyEqual(const GraphStore& lhs, const GraphStore& rhs,
                               const std::string& context) {
  Require(lhs.num_vertices() == rhs.num_vertices(), context + " num_vertices");
  Require(lhs.delta_cap() == rhs.delta_cap(), context + " delta_cap");
  Require(lhs.num_edges() == rhs.num_edges(), context + " num_edges");
  const VertexId n = lhs.num_vertices();
  for (VertexId v = 0; v < n; ++v) {
    Require(lhs.degree(v) == rhs.degree(v), context + " degree mismatch");
  }
  Require(SnapshotEdgeKeys(lhs) == SnapshotEdgeKeys(rhs), context + " edge set mismatch");
}

void RequireColorsInRange(const SeqExactEngine& engine, const std::string& context) {
  for (Color c : engine.colors()) {
    Require(c < engine.palette_size(), context + " color out of range");
  }
}

EdgeUpdate RandomUpdate(Rng* rng, VertexId n, std::size_t step) {
  const VertexId u = rng->uniform_vertex(n);
  VertexId v = rng->uniform_vertex(n);
  if (n > 1 && u == v && rng->bernoulli(0.8)) {
    v = static_cast<VertexId>((v + 1) % n);
  }
  const UpdateKind kind = rng->bernoulli(0.5) ? UpdateKind::Insert : UpdateKind::Delete;
  return EdgeUpdate{kind, u, v, static_cast<std::uint64_t>(step)};
}

std::vector<dgcolor::Degree> CapsForN(VertexId n) {
  std::vector<dgcolor::Degree> caps = {1, 2, 3};
  const dgcolor::Degree max_cap =
      static_cast<dgcolor::Degree>(std::min<VertexId>(8, (n > 0) ? (n - 1) : 0));
  caps.push_back(max_cap);
  std::sort(caps.begin(), caps.end());
  caps.erase(std::unique(caps.begin(), caps.end()), caps.end());
  return caps;
}

void RunSingleUpdateScenario(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap,
                             std::size_t steps) {
  Rng rng(seed);
  SeqExactEngine engine(n, delta_cap, seed);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  for (std::size_t step = 0; step < steps; ++step) {
    const EdgeUpdate update = RandomUpdate(&rng, n, step);
    const std::string tag = "single seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                            " delta_cap=" + std::to_string(delta_cap) +
                            " step=" + std::to_string(step) + " kind=" + OpKindName(update.kind);

    const auto colors_before = engine.colors();
    const auto edges_before = SnapshotEdgeKeys(engine.graph());
    const SeqExactStatsSnapshot stats_before = SnapshotStats(engine);

    const auto mirror_result = mirror.apply_update(update);
    const UpdateStats engine_stats = engine.apply_update(update);
    const bool accepted = engine_stats.applied;
    const bool mirror_accepted = (mirror_result.status == dgcolor::UpdateStatus::Ok);
    Require(accepted == mirror_accepted, tag + " accepted mismatch");

    if (accepted) {
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology accepted");
      const auto graph_valid = dgcolor::validate_graph_invariants(engine.graph());
      Require(graph_valid.ok, tag + " graph invalid " + graph_valid.message);
      const auto color_valid = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
      Require(color_valid.ok, tag + " coloring invalid " + color_valid.message);
      RequireColorsInRange(engine, tag);
    } else {
      const auto edges_after = SnapshotEdgeKeys(engine.graph());
      Require(edges_after == edges_before, tag + " rejected changed topology");
      Require(engine.colors() == colors_before, tag + " rejected changed colors");
      Require(SameStats(stats_before, SnapshotStats(engine)), tag + " rejected changed stats");
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology rejected");
    }
  }
}

void RunBatchScenario(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap, std::size_t steps,
                      std::size_t batch_size) {
  Rng rng(seed);
  SeqExactEngine engine(n, delta_cap, seed);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  for (std::size_t step = 0; step < steps; ++step) {
    UpdateBatch batch;
    batch.reserve(batch_size);
    for (std::size_t i = 0; i < batch_size; ++i) {
      batch.push_back(RandomUpdate(&rng, n, step * batch_size + i));
    }

    const std::string tag = "batch seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                            " delta_cap=" + std::to_string(delta_cap) +
                            " step=" + std::to_string(step) +
                            " batch_size=" + std::to_string(batch_size);

    const auto colors_before = engine.colors();
    const auto edges_before = SnapshotEdgeKeys(engine.graph());
    const SeqExactStatsSnapshot stats_before = SnapshotStats(engine);

    const auto mirror_result = mirror.apply_batch(batch);
    const BatchStats engine_stats = engine.apply_batch(batch);
    const bool accepted = engine_stats.applied;
    const bool mirror_accepted = (mirror_result.status == dgcolor::UpdateStatus::Ok);
    Require(accepted == mirror_accepted, tag + " accepted mismatch");

    if (accepted) {
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology accepted");
      const auto graph_valid = dgcolor::validate_graph_invariants(engine.graph());
      Require(graph_valid.ok, tag + " graph invalid " + graph_valid.message);
      const auto color_valid = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
      Require(color_valid.ok, tag + " coloring invalid " + color_valid.message);
      RequireColorsInRange(engine, tag);
    } else {
      const auto edges_after = SnapshotEdgeKeys(engine.graph());
      Require(edges_after == edges_before, tag + " rejected changed topology");
      Require(engine.colors() == colors_before, tag + " rejected changed colors");
      Require(SameStats(stats_before, SnapshotStats(engine)), tag + " rejected changed stats");
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology rejected");
    }
  }
}

void RunDeterminismScenario() {
  const std::uint64_t seed = 12345;
  const VertexId n = 16;
  const dgcolor::Degree delta_cap = 4;
  const std::size_t single_steps = 30;
  const std::size_t batch_steps = 15;
  const std::size_t batch_size = 4;

  Rng rng(seed);
  SeqExactEngine lhs(n, delta_cap, seed);
  SeqExactEngine rhs(n, delta_cap, seed);
  lhs.initialize_coloring();
  rhs.initialize_coloring();

  for (std::size_t step = 0; step < single_steps; ++step) {
    const EdgeUpdate update = RandomUpdate(&rng, n, step);
    const UpdateStats lhs_stats = lhs.apply_update(update);
    const UpdateStats rhs_stats = rhs.apply_update(update);
    Require(lhs_stats.applied == rhs_stats.applied, "determinism single applied");
    Require(lhs_stats.edges_changed == rhs_stats.edges_changed, "determinism single edges");
    Require(lhs_stats.vertices_touched == rhs_stats.vertices_touched, "determinism single touched");
  }

  for (std::size_t step = 0; step < batch_steps; ++step) {
    UpdateBatch batch;
    batch.reserve(batch_size);
    for (std::size_t i = 0; i < batch_size; ++i) {
      batch.push_back(RandomUpdate(&rng, n, single_steps + step * batch_size + i));
    }
    const BatchStats lhs_stats = lhs.apply_batch(batch);
    const BatchStats rhs_stats = rhs.apply_batch(batch);
    Require(lhs_stats.applied == rhs_stats.applied, "determinism batch applied");
    Require(lhs_stats.updates == rhs_stats.updates, "determinism batch updates");
    Require(lhs_stats.edges_changed == rhs_stats.edges_changed, "determinism batch edges");
    Require(lhs_stats.vertices_touched == rhs_stats.vertices_touched, "determinism batch touched");
  }

  Require(lhs.colors() == rhs.colors(), "determinism final colors");
  Require(SnapshotEdgeKeys(lhs.graph()) == SnapshotEdgeKeys(rhs.graph()), "determinism topology");
  Require(SameStats(SnapshotStats(lhs), SnapshotStats(rhs)), "determinism stats");
}

}  // namespace

int main() {
  const std::vector<std::uint64_t> seeds = {1, 2, 3, 12345};
  const std::vector<VertexId> sizes = {2, 4, 8, 16, 32};
  const std::vector<std::size_t> batch_sizes = {2, 4};

  for (std::uint64_t seed : seeds) {
    for (VertexId n : sizes) {
      for (dgcolor::Degree delta_cap : CapsForN(n)) {
        RunSingleUpdateScenario(seed, n, delta_cap, 50);
        for (std::size_t batch_size : batch_sizes) {
          RunBatchScenario(seed + static_cast<std::uint64_t>(1000U * batch_size), n, delta_cap, 25,
                           batch_size);
        }
      }
    }
  }

  RunDeterminismScenario();
  return 0;
}
