#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "dgcolor/graph_store.hpp"
#include "dgcolor/rng.hpp"
#include "dgcolor/seq_baseline_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::AdjacencyGraphStore;
using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::GraphStore;
using dgcolor::Rng;
using dgcolor::SeqBaselineEngine;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::UpdateStats;
using dgcolor::VertexId;

void Require(bool condition, const std::string& context) {
  if (condition) {
    return;
  }
  std::cerr << "fuzz_failure=" << context << "\n";
  assert(false);
}

EdgeUpdate RandomUpdate(Rng* rng, VertexId n, std::size_t step) {
  const VertexId u = rng->uniform_vertex(n);
  VertexId v = rng->uniform_vertex(n);
  if (n > 1 && u == v && rng->bernoulli(0.8)) {
    v = static_cast<VertexId>((v + 1) % n);
  }
  return EdgeUpdate{rng->bernoulli(0.5) ? UpdateKind::Insert : UpdateKind::Delete, u, v,
                    static_cast<std::uint64_t>(step)};
}

void RequireGraphTopologyEqual(const GraphStore& lhs, const GraphStore& rhs,
                               const std::string& context) {
  Require(lhs.num_vertices() == rhs.num_vertices(), context + " num_vertices");
  Require(lhs.delta_cap() == rhs.delta_cap(), context + " delta_cap");
  Require(lhs.num_edges() == rhs.num_edges(), context + " num_edges");
  for (VertexId v = 0; v < lhs.num_vertices(); ++v) {
    Require(lhs.degree(v) == rhs.degree(v), context + " degree mismatch");
    Require(lhs.neighbors(v) == rhs.neighbors(v), context + " neighbors mismatch");
  }
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
  std::cerr << "scenario=single_update seed=" << seed << " n=" << n
            << " delta_cap=" << delta_cap << " steps=" << steps << "\n";

  Rng rng(seed);
  SeqBaselineEngine engine(n, delta_cap);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  for (std::size_t step = 0; step < steps; ++step) {
    const EdgeUpdate update = RandomUpdate(&rng, n, step);

    const auto colors_before = engine.colors();
    const std::size_t edges_before = engine.graph().num_edges();

    const dgcolor::UpdateResult mirror_result = mirror.apply_update(update);
    const UpdateStats engine_stats = engine.apply_update(update);

    Require((engine_stats.applied == (mirror_result.status == dgcolor::UpdateStatus::Ok)),
            "single status mismatch seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                " delta_cap=" + std::to_string(delta_cap) + " step=" + std::to_string(step));

    if (engine_stats.applied) {
      const auto graph_valid = dgcolor::validate_graph_invariants(engine.graph());
      Require(graph_valid.ok,
              "single graph invalid seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                  " delta_cap=" + std::to_string(delta_cap) +
                  " step=" + std::to_string(step) + " msg=" + graph_valid.message);

      const auto color_valid = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
      Require(color_valid.ok,
              "single coloring invalid seed=" + std::to_string(seed) +
                  " n=" + std::to_string(n) + " delta_cap=" + std::to_string(delta_cap) +
                  " step=" + std::to_string(step) + " msg=" + color_valid.message);
    } else {
      Require(engine.graph().num_edges() == edges_before,
              "single rejected changed edges seed=" + std::to_string(seed) +
                  " n=" + std::to_string(n) + " delta_cap=" + std::to_string(delta_cap) +
                  " step=" + std::to_string(step));
      Require(engine.colors() == colors_before,
              "single rejected changed colors seed=" + std::to_string(seed) +
                  " n=" + std::to_string(n) + " delta_cap=" + std::to_string(delta_cap) +
                  " step=" + std::to_string(step));
    }

    RequireGraphTopologyEqual(engine.graph(), mirror,
                              "single mirror mismatch seed=" + std::to_string(seed) +
                                  " n=" + std::to_string(n) +
                                  " delta_cap=" + std::to_string(delta_cap) +
                                  " step=" + std::to_string(step));
  }
}

void RunBatchScenario(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap, std::size_t steps,
                      std::size_t batch_size) {
  std::cerr << "scenario=batch seed=" << seed << " n=" << n << " delta_cap=" << delta_cap
            << " steps=" << steps << " batch_size=" << batch_size << "\n";

  Rng rng(seed);
  SeqBaselineEngine engine(n, delta_cap);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  std::size_t step = 0;
  while (step < steps) {
    UpdateBatch batch;
    const std::size_t target = std::min(batch_size, steps - step);
    batch.reserve(target);
    for (std::size_t i = 0; i < target; ++i) {
      batch.push_back(RandomUpdate(&rng, n, step + i));
    }

    const auto colors_before = engine.colors();
    const std::size_t edges_before = engine.graph().num_edges();

    const auto mirror_result = mirror.apply_batch(batch);
    const auto engine_stats = engine.apply_batch(batch);

    Require((engine_stats.applied == (mirror_result.status == dgcolor::UpdateStatus::Ok)),
            "batch status mismatch seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                " delta_cap=" + std::to_string(delta_cap) + " step=" + std::to_string(step) +
                " batch_size=" + std::to_string(batch.size()));

    if (engine_stats.applied) {
      const auto graph_valid = dgcolor::validate_graph_invariants(engine.graph());
      Require(graph_valid.ok,
              "batch graph invalid seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
                  " delta_cap=" + std::to_string(delta_cap) + " step=" + std::to_string(step) +
                  " msg=" + graph_valid.message);

      const auto color_valid = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
      Require(color_valid.ok,
              "batch coloring invalid seed=" + std::to_string(seed) +
                  " n=" + std::to_string(n) + " delta_cap=" + std::to_string(delta_cap) +
                  " step=" + std::to_string(step) + " msg=" + color_valid.message);
    } else {
      Require(engine.graph().num_edges() == edges_before,
              "batch rejected changed edges seed=" + std::to_string(seed) +
                  " n=" + std::to_string(n) + " delta_cap=" + std::to_string(delta_cap) +
                  " step=" + std::to_string(step));
      Require(engine.colors() == colors_before,
              "batch rejected changed colors seed=" + std::to_string(seed) +
                  " n=" + std::to_string(n) + " delta_cap=" + std::to_string(delta_cap) +
                  " step=" + std::to_string(step));
    }

    RequireGraphTopologyEqual(engine.graph(), mirror,
                              "batch mirror mismatch seed=" + std::to_string(seed) +
                                  " n=" + std::to_string(n) +
                                  " delta_cap=" + std::to_string(delta_cap) +
                                  " step=" + std::to_string(step));
    step += target;
  }
}

}  // namespace

int main() {
  const std::vector<std::uint64_t> seeds = {1, 2, 3, 12345, 99991};
  const std::vector<VertexId> sizes = {2, 4, 8, 16, 32};
  const std::vector<std::size_t> batch_sizes = {2, 4, 8};

  for (std::uint64_t seed : seeds) {
    for (VertexId n : sizes) {
      const auto caps = CapsForN(n);
      for (dgcolor::Degree delta_cap : caps) {
        RunSingleUpdateScenario(seed, n, delta_cap, 120);
        for (std::size_t batch_size : batch_sizes) {
          RunBatchScenario(seed + 1000U * batch_size, n, delta_cap, 120, batch_size);
        }
      }
    }
  }
  return 0;
}
