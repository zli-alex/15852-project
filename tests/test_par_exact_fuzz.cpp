#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "dgcolor/graph_store.hpp"
#include "dgcolor/par_exact_engine.hpp"
#include "dgcolor/rng.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::AdjacencyGraphStore;
using dgcolor::BatchStats;
using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::GraphStore;
using dgcolor::ParExactEngine;
using dgcolor::Rng;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateStats;
using dgcolor::VertexId;

struct ParExactDiagSnapshot {
  std::uint64_t active_vertices_total;
  std::uint64_t repair_rounds_total;
  std::uint64_t fallback_count;
  std::uint64_t proposal_count;
  std::uint64_t commit_count;
  std::uint64_t unresolved_count;
  std::uint64_t sequential_fast_path_count;
  std::uint64_t vertices_touched_total;
};

void Require(bool condition, const std::string& context) {
  if (condition) {
    return;
  }
  std::cerr << "fuzz_failure=" << context << "\n";
  assert(false);
}

ParExactDiagSnapshot SnapshotDiag(const ParExactEngine& engine) {
  return ParExactDiagSnapshot{
      engine.active_vertices_total(),    engine.repair_rounds_total(),  engine.fallback_count(),
      engine.proposal_count(),           engine.commit_count(),          engine.unresolved_count(),
      engine.sequential_fast_path_count(), engine.vertices_touched_total()};
}

bool SameDiag(const ParExactDiagSnapshot& lhs, const ParExactDiagSnapshot& rhs) {
  return lhs.active_vertices_total == rhs.active_vertices_total &&
         lhs.repair_rounds_total == rhs.repair_rounds_total &&
         lhs.fallback_count == rhs.fallback_count && lhs.proposal_count == rhs.proposal_count &&
         lhs.commit_count == rhs.commit_count && lhs.unresolved_count == rhs.unresolved_count &&
         lhs.sequential_fast_path_count == rhs.sequential_fast_path_count &&
         lhs.vertices_touched_total == rhs.vertices_touched_total;
}

std::vector<std::uint64_t> SnapshotEdgeKeys(const GraphStore& graph) {
  std::vector<std::uint64_t> keys;
  const VertexId n = graph.num_vertices();
  for (VertexId u = 0; u < n; ++u) {
    for (VertexId v = static_cast<VertexId>(u + 1); v < n; ++v) {
      if (graph.has_edge(u, v)) {
        keys.push_back((static_cast<std::uint64_t>(u) << 32U) | static_cast<std::uint64_t>(v));
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

void RequireColoringValidAndInRange(const ParExactEngine& engine, const std::string& context) {
  const auto graph_valid = dgcolor::validate_graph_invariants(engine.graph());
  Require(graph_valid.ok, context + " graph invalid " + graph_valid.message);
  const auto color_valid = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
  Require(color_valid.ok, context + " coloring invalid " + color_valid.message);
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
                             std::uint32_t max_rounds, std::size_t steps) {
  Rng rng(seed);
  ParExactEngine engine(n, delta_cap, seed, max_rounds);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  for (std::size_t step = 0; step < steps; ++step) {
    const EdgeUpdate update = RandomUpdate(&rng, n, step);
    const std::string tag = "single seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                            " delta=" + std::to_string(delta_cap) +
                            " rounds=" + std::to_string(max_rounds) +
                            " step=" + std::to_string(step);

    const auto colors_before = engine.colors();
    const auto edges_before = SnapshotEdgeKeys(engine.graph());
    const std::size_t edge_count_before = engine.graph().num_edges();
    const ParExactDiagSnapshot diag_before = SnapshotDiag(engine);

    const auto mirror_result = mirror.apply_update(update);
    const UpdateStats engine_stats = engine.apply_update(update);
    const bool accepted = engine_stats.applied;
    const bool mirror_accepted = (mirror_result.status == dgcolor::UpdateStatus::Ok);
    Require(accepted == mirror_accepted, tag + " accepted mismatch");

    if (accepted) {
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology accepted");
      RequireColoringValidAndInRange(engine, tag + " accepted");
    } else {
      Require(engine.graph().num_edges() == edge_count_before, tag + " rejected edge count changed");
      Require(SnapshotEdgeKeys(engine.graph()) == edges_before, tag + " rejected topology changed");
      Require(engine.colors() == colors_before, tag + " rejected colors changed");
      Require(SameDiag(diag_before, SnapshotDiag(engine)), tag + " rejected diagnostics changed");
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology rejected");
    }
  }
}

void RunBatchScenario(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap,
                      std::uint32_t max_rounds, std::size_t steps, std::size_t batch_size) {
  Rng rng(seed);
  ParExactEngine engine(n, delta_cap, seed, max_rounds);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  for (std::size_t step = 0; step < steps; ++step) {
    UpdateBatch batch;
    batch.reserve(batch_size);
    for (std::size_t i = 0; i < batch_size; ++i) {
      batch.push_back(RandomUpdate(&rng, n, step * batch_size + i));
    }

    const std::string tag = "batch seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                            " delta=" + std::to_string(delta_cap) +
                            " rounds=" + std::to_string(max_rounds) +
                            " step=" + std::to_string(step) +
                            " batch_size=" + std::to_string(batch_size);

    const auto colors_before = engine.colors();
    const auto edges_before = SnapshotEdgeKeys(engine.graph());
    const std::size_t edge_count_before = engine.graph().num_edges();
    const ParExactDiagSnapshot diag_before = SnapshotDiag(engine);

    const auto mirror_result = mirror.apply_batch(batch);
    const BatchStats engine_stats = engine.apply_batch(batch);
    const bool accepted = engine_stats.applied;
    const bool mirror_accepted = (mirror_result.status == dgcolor::UpdateStatus::Ok);
    Require(accepted == mirror_accepted, tag + " accepted mismatch");

    if (accepted) {
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology accepted");
      RequireColoringValidAndInRange(engine, tag + " accepted");
    } else {
      Require(engine.graph().num_edges() == edge_count_before, tag + " rejected edge count changed");
      Require(SnapshotEdgeKeys(engine.graph()) == edges_before, tag + " rejected topology changed");
      Require(engine.colors() == colors_before, tag + " rejected colors changed");
      Require(SameDiag(diag_before, SnapshotDiag(engine)), tag + " rejected diagnostics changed");
      RequireGraphTopologyEqual(engine.graph(), mirror, tag + " topology rejected");
    }
  }
}

void RunConflictHeavyTargetedScenario(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap,
                                      std::uint32_t max_rounds) {
  ParExactEngine engine(n, delta_cap, seed, max_rounds);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  bool found = false;
  VertexId chosen_u = 0;
  VertexId chosen_v = 0;
  const auto colors = engine.colors();
  for (VertexId u = 0; u < n && !found; ++u) {
    for (VertexId v = static_cast<VertexId>(u + 1); v < n; ++v) {
      if (!engine.graph().has_edge(u, v) && colors[u] == colors[v] &&
          engine.graph().degree(u) < delta_cap && engine.graph().degree(v) < delta_cap) {
        chosen_u = u;
        chosen_v = v;
        found = true;
        break;
      }
    }
  }
  if (!found) {
    return;
  }

  const auto diag_before = SnapshotDiag(engine);
  const EdgeUpdate update{UpdateKind::Insert, chosen_u, chosen_v, 0};
  const auto mirror_result = mirror.apply_update(update);
  const UpdateStats stats = engine.apply_update(update);
  Require(stats.applied == (mirror_result.status == dgcolor::UpdateStatus::Ok),
          "conflict_target accepted mismatch");
  if (!stats.applied) {
    return;
  }
  RequireGraphTopologyEqual(engine.graph(), mirror, "conflict_target topology");
  RequireColoringValidAndInRange(engine, "conflict_target coloring");
  const auto diag_after = SnapshotDiag(engine);
  Require(diag_after.active_vertices_total >= diag_before.active_vertices_total + 1,
          "conflict_target active not incremented");
}

void RunDeterminismScenario() {
  const std::uint64_t seed = 12345;
  const VertexId n = 16;
  const dgcolor::Degree delta_cap = 4;
  const std::uint32_t max_rounds = 4;
  const std::size_t single_steps = 20;
  const std::size_t batch_steps = 10;
  const std::size_t batch_size = 4;

  Rng rng(seed);
  ParExactEngine lhs(n, delta_cap, seed, max_rounds);
  ParExactEngine rhs(n, delta_cap, seed, max_rounds);
  lhs.initialize_coloring();
  rhs.initialize_coloring();

  for (std::size_t step = 0; step < single_steps; ++step) {
    const EdgeUpdate update = RandomUpdate(&rng, n, step);
    const UpdateStats ls = lhs.apply_update(update);
    const UpdateStats rs = rhs.apply_update(update);
    Require(ls.applied == rs.applied, "determinism single applied");
    Require(ls.edges_changed == rs.edges_changed, "determinism single edges");
    Require(ls.vertices_touched == rs.vertices_touched, "determinism single touched");
  }

  for (std::size_t step = 0; step < batch_steps; ++step) {
    UpdateBatch batch;
    batch.reserve(batch_size);
    for (std::size_t i = 0; i < batch_size; ++i) {
      batch.push_back(RandomUpdate(&rng, n, single_steps + step * batch_size + i));
    }
    const BatchStats lb = lhs.apply_batch(batch);
    const BatchStats rb = rhs.apply_batch(batch);
    Require(lb.applied == rb.applied, "determinism batch applied");
    Require(lb.edges_changed == rb.edges_changed, "determinism batch edges");
    Require(lb.vertices_touched == rb.vertices_touched, "determinism batch touched");
  }

  Require(lhs.colors() == rhs.colors(), "determinism colors");
  Require(SnapshotEdgeKeys(lhs.graph()) == SnapshotEdgeKeys(rhs.graph()), "determinism topology");
  Require(SameDiag(SnapshotDiag(lhs), SnapshotDiag(rhs)), "determinism diagnostics");
}

}  // namespace

int main() {
  const std::vector<std::uint64_t> seeds = {1, 2, 3, 12345};
  const std::vector<VertexId> sizes = {2, 4, 8, 16, 32};
  const std::vector<std::uint32_t> max_rounds_values = {1, 4};
  const std::vector<std::size_t> batch_sizes = {2, 4};
  const std::size_t single_steps = 40;
  const std::size_t batch_steps = 20;

  for (std::uint64_t seed : seeds) {
    for (VertexId n : sizes) {
      const auto caps = CapsForN(n);
      for (dgcolor::Degree delta_cap : caps) {
        for (std::uint32_t max_rounds : max_rounds_values) {
          RunSingleUpdateScenario(seed, n, delta_cap, max_rounds, single_steps);
          for (std::size_t batch_size : batch_sizes) {
            const std::uint64_t batch_seed = seed + static_cast<std::uint64_t>(1000U * batch_size);
            RunBatchScenario(batch_seed, n, delta_cap, max_rounds, batch_steps, batch_size);
          }
          RunConflictHeavyTargetedScenario(seed + 999U, n, delta_cap, max_rounds);
        }
      }
    }
  }

  RunDeterminismScenario();
  return 0;
}
