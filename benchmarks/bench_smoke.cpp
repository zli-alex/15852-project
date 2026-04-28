#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "dgcolor/batch.hpp"
#include "dgcolor/graph_store.hpp"
#include "dgcolor/rng.hpp"
#include "dgcolor/validator.hpp"

namespace {

struct BenchConfig {
  std::uint64_t seed{1};
  dgcolor::VertexId vertices{32};
  std::size_t updates{100};
  dgcolor::Degree delta_cap{8};
  std::size_t batch_size{16};
};

bool ParseU64(const std::string& text, std::uint64_t* out) {
  try {
    *out = std::stoull(text);
    return true;
  } catch (...) {
    return false;
  }
}

void ParseArgs(int argc, char** argv, BenchConfig* cfg) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--seed" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v)) {
        throw std::invalid_argument("invalid --seed");
      }
      cfg->seed = v;
    } else if (arg == "--vertices" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v) || v == 0 || v > UINT32_MAX) {
        throw std::invalid_argument("invalid --vertices");
      }
      cfg->vertices = static_cast<dgcolor::VertexId>(v);
    } else if (arg == "--updates" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v)) {
        throw std::invalid_argument("invalid --updates");
      }
      cfg->updates = static_cast<std::size_t>(v);
    } else if (arg == "--delta-cap" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v) || v > UINT32_MAX) {
        throw std::invalid_argument("invalid --delta-cap");
      }
      cfg->delta_cap = static_cast<dgcolor::Degree>(v);
    } else if (arg == "--batch-size" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v) || v == 0) {
        throw std::invalid_argument("invalid --batch-size");
      }
      cfg->batch_size = static_cast<std::size_t>(v);
    } else {
      throw std::invalid_argument("unknown or incomplete argument: " + arg);
    }
  }
}

double SecondsSince(const std::chrono::steady_clock::time_point& start) {
  const auto end = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(end - start).count();
}

dgcolor::EdgeUpdate RandomAttempt(dgcolor::Rng* rng, dgcolor::VertexId n) {
  const dgcolor::VertexId u = rng->uniform_vertex(n);
  dgcolor::VertexId v = rng->uniform_vertex(n);
  if (n > 1 && u == v) {
    v = static_cast<dgcolor::VertexId>((v + 1) % n);
  }
  const bool do_insert = rng->bernoulli(0.5);
  return dgcolor::EdgeUpdate{
      do_insert ? dgcolor::UpdateKind::Insert : dgcolor::UpdateKind::Delete, u, v, 0};
}

void PrintMetric(const std::string& key, const std::string& value) {
  std::cout << key << "=" << value << "\n";
}

template <typename T>
void PrintMetric(const std::string& key, T value) {
  std::cout << key << "=" << value << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  BenchConfig cfg;
  try {
    ParseArgs(argc, argv, &cfg);
  } catch (const std::exception& e) {
    std::cerr << "argument_error=" << e.what() << "\n";
    return 2;
  }

  const auto build_start = std::chrono::steady_clock::now();
  dgcolor::Rng rng(cfg.seed);
  dgcolor::AdjacencyGraphStore graph(cfg.vertices, cfg.delta_cap);
  const std::size_t initial_edges = graph.num_edges();
  const double build_seconds = SecondsSince(build_start);

  std::size_t applied = 0;
  std::size_t rejected = 0;

  const auto update_start = std::chrono::steady_clock::now();
  if (cfg.batch_size <= 1) {
    for (std::size_t i = 0; i < cfg.updates; ++i) {
      const dgcolor::EdgeUpdate update = RandomAttempt(&rng, cfg.vertices);
      const dgcolor::UpdateResult result = graph.apply_update(update);
      if (result.status == dgcolor::UpdateStatus::Ok) {
        ++applied;
      } else {
        ++rejected;
      }
    }
  } else {
    dgcolor::UpdateBatch batch;
    batch.reserve(cfg.batch_size);
    for (std::size_t i = 0; i < cfg.updates; ++i) {
      batch.push_back(RandomAttempt(&rng, cfg.vertices));
      const bool batch_full = batch.size() >= cfg.batch_size;
      const bool last_update = (i + 1 == cfg.updates);
      if (!batch_full && !last_update) {
        continue;
      }

      const dgcolor::BatchApplyResult result = graph.apply_batch(batch);
      if (result.status == dgcolor::UpdateStatus::Ok) {
        applied += result.updates_applied;
      } else {
        rejected += batch.size();
      }
      batch.clear();
    }
  }
  const double update_seconds = SecondsSince(update_start);

  const auto validate_start = std::chrono::steady_clock::now();
  const dgcolor::ValidationResult validation = dgcolor::validate_graph_invariants(graph);
  const double validate_seconds = SecondsSince(validate_start);

  dgcolor::Degree max_degree = 0;
  for (dgcolor::VertexId v = 0; v < graph.num_vertices(); ++v) {
    if (graph.degree(v) > max_degree) {
      max_degree = graph.degree(v);
    }
  }

  const double throughput =
      (update_seconds > 0.0) ? (static_cast<double>(cfg.updates) / update_seconds) : 0.0;

  PrintMetric("benchmark_name", "foundation_smoke");
  PrintMetric("engine_name", "graph_store_only");
  PrintMetric("seed", cfg.seed);
  PrintMetric("num_vertices", cfg.vertices);
  PrintMetric("delta_cap", cfg.delta_cap);
  PrintMetric("updates_requested", cfg.updates);
  PrintMetric("updates_applied", applied);
  PrintMetric("updates_rejected", rejected);
  PrintMetric("batch_size", cfg.batch_size);
  PrintMetric("initial_edges", initial_edges);
  PrintMetric("final_edges", graph.num_edges());
  PrintMetric("build_seconds", build_seconds);
  PrintMetric("update_seconds", update_seconds);
  PrintMetric("validate_seconds", validate_seconds);
  PrintMetric("throughput_updates_per_second", throughput);
  PrintMetric("max_degree_observed", max_degree);
  PrintMetric("graph_validated", validation.ok ? 1 : 0);

  if (!validation.ok) {
    PrintMetric("validation_message", validation.message);
    return 1;
  }
  return 0;
}
