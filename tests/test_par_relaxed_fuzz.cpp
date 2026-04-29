#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "dgcolor/graph_store.hpp"
#include "dgcolor/par_relaxed_engine.hpp"
#include "dgcolor/rng.hpp"
#include "dgcolor/validator.hpp"

namespace {

using dgcolor::AdjacencyGraphStore;
using dgcolor::Color;
using dgcolor::EdgeUpdate;
using dgcolor::GraphStore;
using dgcolor::ParRelaxedEngine;
using dgcolor::Rng;
using dgcolor::UpdateBatch;
using dgcolor::UpdateKind;
using dgcolor::VertexId;

void Require(bool condition, const std::string& context) {
  if (condition) {
    return;
  }
  std::cerr << "fuzz_failure=" << context << "\n";
  assert(false);
}

std::string ScenarioTag(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap,
                        std::uint32_t palette_multiplier, std::uint32_t max_rounds,
                        std::size_t step, const std::string& op_type) {
  return "seed=" + std::to_string(seed) + " n=" + std::to_string(n) +
         " delta_cap=" + std::to_string(delta_cap) + " c=" + std::to_string(palette_multiplier) +
         " max_rounds=" + std::to_string(max_rounds) + " step=" + std::to_string(step) +
         " op=" + op_type;
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

void RequireColorsInRange(const ParRelaxedEngine& engine, const std::string& context) {
  for (Color c : engine.colors()) {
    Require(c < engine.palette_size(), context + " color out of relaxed range");
  }
}

std::vector<dgcolor::Degree> CapsForN(VertexId n) {
  std::vector<dgcolor::Degree> caps;
  const dgcolor::Degree max_cap =
      static_cast<dgcolor::Degree>(std::min<VertexId>(8, (n > 0) ? (n - 1) : 0));
  caps.push_back(1);
  caps.push_back(2);
  caps.push_back(3);
  caps.push_back(max_cap);
  std::sort(caps.begin(), caps.end());
  caps.erase(std::unique(caps.begin(), caps.end()), caps.end());
  return caps;
}

void RunSingleUpdateScenario(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap,
                             std::uint32_t palette_multiplier, std::uint32_t max_rounds,
                             std::size_t steps) {
  std::cerr << "scenario=par_relaxed_single_update "
            << ScenarioTag(seed, n, delta_cap, palette_multiplier, max_rounds, 0, "single_start")
            << " steps=" << steps << "\n";

  Rng rng(seed);
  ParRelaxedEngine engine(n, delta_cap, seed, palette_multiplier, max_rounds);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  for (std::size_t step = 0; step < steps; ++step) {
    const EdgeUpdate update = RandomUpdate(&rng, n, step);
    const std::string tag =
        ScenarioTag(seed, n, delta_cap, palette_multiplier, max_rounds, step, "single_update");

    const auto colors_before = engine.colors();
    const std::size_t edges_before = engine.graph().num_edges();

    const dgcolor::UpdateResult mirror_result = mirror.apply_update(update);
    const dgcolor::UpdateStats engine_stats = engine.apply_update(update);

    Require((engine_stats.applied == (mirror_result.status == dgcolor::UpdateStatus::Ok)),
            tag + " status mismatch");

    if (engine_stats.applied) {
      const auto graph_valid = dgcolor::validate_graph_invariants(engine.graph());
      Require(graph_valid.ok, tag + " graph invalid msg=" + graph_valid.message);
      const auto color_valid = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
      Require(color_valid.ok, tag + " coloring invalid msg=" + color_valid.message);
      RequireColorsInRange(engine, tag);
    } else {
      Require(engine.graph().num_edges() == edges_before, tag + " rejected changed edges");
      Require(engine.colors() == colors_before, tag + " rejected changed colors");
    }

    RequireGraphTopologyEqual(engine.graph(), mirror, tag + " mirror mismatch");
  }
}

void RunBatchScenario(std::uint64_t seed, VertexId n, dgcolor::Degree delta_cap,
                      std::uint32_t palette_multiplier, std::uint32_t max_rounds,
                      std::size_t steps, std::size_t batch_size) {
  std::cerr << "scenario=par_relaxed_batch "
            << ScenarioTag(seed, n, delta_cap, palette_multiplier, max_rounds, 0, "batch_start")
            << " steps=" << steps << " batch_size=" << batch_size << "\n";

  Rng rng(seed);
  ParRelaxedEngine engine(n, delta_cap, seed, palette_multiplier, max_rounds);
  AdjacencyGraphStore mirror(n, delta_cap);
  engine.initialize_coloring();

  std::size_t step = 0;
  while (step < steps) {
    const std::size_t target = std::min(batch_size, steps - step);
    UpdateBatch batch;
    batch.reserve(target);
    for (std::size_t i = 0; i < target; ++i) {
      batch.push_back(RandomUpdate(&rng, n, step + i));
    }

    const std::string tag =
        ScenarioTag(seed, n, delta_cap, palette_multiplier, max_rounds, step, "batch_update");

    const auto colors_before = engine.colors();
    const std::size_t edges_before = engine.graph().num_edges();

    const dgcolor::BatchApplyResult mirror_result = mirror.apply_batch(batch);
    const dgcolor::BatchStats engine_stats = engine.apply_batch(batch);

    Require((engine_stats.applied == (mirror_result.status == dgcolor::UpdateStatus::Ok)),
            tag + " status mismatch");

    if (engine_stats.applied) {
      const auto graph_valid = dgcolor::validate_graph_invariants(engine.graph());
      Require(graph_valid.ok, tag + " graph invalid msg=" + graph_valid.message);
      const auto color_valid = dgcolor::validate_exact_coloring(engine.graph(), engine.colors());
      Require(color_valid.ok, tag + " coloring invalid msg=" + color_valid.message);
      RequireColorsInRange(engine, tag);
    } else {
      Require(engine.graph().num_edges() == edges_before, tag + " rejected changed edges");
      Require(engine.colors() == colors_before, tag + " rejected changed colors");
    }

    RequireGraphTopologyEqual(engine.graph(), mirror, tag + " mirror mismatch");
    step += target;
  }
}

}  // namespace

int main() {
  const char* extended_env = std::getenv("DGCOLOR_EXTENDED_FUZZ");
  const bool extended = (extended_env != nullptr && std::string(extended_env) == "1");

  // Keep the default target small enough for normal CTest. The repair-round
  // path is intentionally heavier than the earlier full-recolor baseline.
  const std::size_t steps_per_scenario = extended ? 100 : 5;
  const std::vector<std::uint64_t> seeds =
      extended ? std::vector<std::uint64_t>{1, 2, 3, 12345, 99991}
               : std::vector<std::uint64_t>{1};
  const std::vector<VertexId> sizes =
      extended ? std::vector<VertexId>{2, 4, 8, 16, 32} : std::vector<VertexId>{8, 32};
  const std::vector<std::uint32_t> multipliers =
      extended ? std::vector<std::uint32_t>{2, 4, 8} : std::vector<std::uint32_t>{2, 8};
  const std::vector<std::uint32_t> max_rounds_values = {1, 4};
  const std::vector<std::size_t> batch_sizes =
      extended ? std::vector<std::size_t>{2, 4, 8} : std::vector<std::size_t>{2};

  std::cerr << "par_relaxed_fuzz_mode=" << (extended ? "extended" : "default")
            << " steps_per_scenario=" << steps_per_scenario << "\n";

  for (std::uint64_t seed : seeds) {
    for (VertexId n : sizes) {
      const auto caps = CapsForN(n);
      for (dgcolor::Degree delta_cap : caps) {
        for (std::uint32_t palette_multiplier : multipliers) {
          for (std::uint32_t max_rounds : max_rounds_values) {
            RunSingleUpdateScenario(seed, n, delta_cap, palette_multiplier, max_rounds,
                                    steps_per_scenario);
            for (std::size_t batch_size : batch_sizes) {
              RunBatchScenario(seed + static_cast<std::uint64_t>(1000U * batch_size), n, delta_cap,
                               palette_multiplier, max_rounds, steps_per_scenario, batch_size);
            }
          }
        }
      }
    }
  }
  return 0;
}
