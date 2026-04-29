#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "dgcolor/batch.hpp"
#include "dgcolor/graph_store.hpp"
#include "dgcolor/par_relaxed_engine.hpp"
#include "dgcolor/rng.hpp"
#include "dgcolor/seq_baseline_engine.hpp"
#include "dgcolor/validator.hpp"

namespace {

struct BenchConfig {
  std::string engine{"graph_store_only"};
  std::string workload{"random_attempts"};
  std::uint64_t seed{1};
  dgcolor::VertexId vertices{32};
  std::size_t updates{100};
  dgcolor::Degree delta_cap{8};
  std::size_t batch_size{16};
  std::uint32_t palette_multiplier{2};
  std::uint32_t max_rounds{4};
  bool diagnostics{false};
  std::size_t initial_edges_requested{0};
  double insert_ratio{0.5};
  std::size_t target_accepted{0};
  std::size_t max_generation_attempts{0};
  std::size_t validate_every{0};
  bool validate_final_only{false};
};

bool ParseU64(const std::string& text, std::uint64_t* out) {
  try {
    *out = std::stoull(text);
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseDouble(const std::string& text, double* out) {
  try {
    std::size_t pos = 0;
    const double value = std::stod(text, &pos);
    if (pos != text.size()) {
      return false;
    }
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

void ParseArgs(int argc, char** argv, BenchConfig* cfg) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--engine" && i + 1 < argc) {
      cfg->engine = argv[++i];
      if (cfg->engine != "graph_store_only" && cfg->engine != "seq_baseline" &&
          cfg->engine != "par_relaxed") {
        throw std::invalid_argument(
            "invalid --engine (expected graph_store_only|seq_baseline|par_relaxed)");
      }
    } else if (arg == "--workload" && i + 1 < argc) {
      cfg->workload = argv[++i];
      if (cfg->workload != "random_attempts" && cfg->workload != "valid_insertions" &&
          cfg->workload != "mixed_valid" && cfg->workload != "batch_valid") {
        throw std::invalid_argument(
            "invalid --workload (expected random_attempts|valid_insertions|mixed_valid|batch_valid)");
      }
    } else if (arg == "--seed" && i + 1 < argc) {
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
    } else if ((arg == "--palette-multiplier" || arg == "--c") && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v) || v == 0 || v > UINT32_MAX) {
        throw std::invalid_argument("invalid --palette-multiplier/--c");
      }
      cfg->palette_multiplier = static_cast<std::uint32_t>(v);
    } else if (arg == "--max-rounds" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v) || v == 0 || v > UINT32_MAX) {
        throw std::invalid_argument("invalid --max-rounds");
      }
      cfg->max_rounds = static_cast<std::uint32_t>(v);
    } else if (arg == "--diagnostics" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v) || v > 1) {
        throw std::invalid_argument("invalid --diagnostics (expected 0|1)");
      }
      cfg->diagnostics = (v == 1);
    } else if (arg == "--initial-edges" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v)) {
        throw std::invalid_argument("invalid --initial-edges");
      }
      cfg->initial_edges_requested = static_cast<std::size_t>(v);
    } else if (arg == "--insert-ratio" && i + 1 < argc) {
      double v = 0.0;
      if (!ParseDouble(argv[++i], &v) || v < 0.0 || v > 1.0) {
        throw std::invalid_argument("invalid --insert-ratio (expected 0..1)");
      }
      cfg->insert_ratio = v;
    } else if (arg == "--target-accepted" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v)) {
        throw std::invalid_argument("invalid --target-accepted");
      }
      cfg->target_accepted = static_cast<std::size_t>(v);
    } else if (arg == "--max-generation-attempts" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v)) {
        throw std::invalid_argument("invalid --max-generation-attempts");
      }
      cfg->max_generation_attempts = static_cast<std::size_t>(v);
    } else if (arg == "--validate-every" && i + 1 < argc) {
      std::uint64_t v = 0;
      if (!ParseU64(argv[++i], &v)) {
        throw std::invalid_argument("invalid --validate-every");
      }
      cfg->validate_every = static_cast<std::size_t>(v);
    } else if (arg == "--validate-final-only") {
      cfg->validate_final_only = true;
    } else {
      throw std::invalid_argument("unknown or incomplete argument: " + arg);
    }
  }

  if (cfg->max_generation_attempts == 0) {
    cfg->max_generation_attempts = cfg->updates * 100;
  }
  if ((cfg->workload == "valid_insertions" || cfg->workload == "mixed_valid") &&
      cfg->batch_size > 1) {
    throw std::invalid_argument(cfg->workload + " currently supports batch_size=1 only");
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

std::uint64_t EdgeKey(dgcolor::VertexId a, dgcolor::VertexId b) {
  const dgcolor::VertexId u = (a < b) ? a : b;
  const dgcolor::VertexId v = (a < b) ? b : a;
  return (static_cast<std::uint64_t>(u) << 32U) | static_cast<std::uint64_t>(v);
}

dgcolor::VertexId EdgeKeyU(std::uint64_t key) {
  return static_cast<dgcolor::VertexId>(key >> 32U);
}

dgcolor::VertexId EdgeKeyV(std::uint64_t key) {
  return static_cast<dgcolor::VertexId>(key & 0xffffffffULL);
}

bool GenerateValidInsertion(dgcolor::Rng* rng, dgcolor::VertexId n, dgcolor::Degree delta_cap,
                            const std::vector<dgcolor::Degree>& degrees,
                            const std::unordered_set<std::uint64_t>& edges,
                            dgcolor::EdgeUpdate* update) {
  const dgcolor::VertexId u = rng->uniform_vertex(n);
  const dgcolor::VertexId v = rng->uniform_vertex(n);
  if (u == v) {
    return false;
  }
  if (degrees[u] >= delta_cap || degrees[v] >= delta_cap) {
    return false;
  }
  if (edges.find(EdgeKey(u, v)) != edges.end()) {
    return false;
  }
  *update = dgcolor::EdgeUpdate{dgcolor::UpdateKind::Insert, u, v, 0};
  return true;
}

bool GenerateValidBatchInsertion(dgcolor::Rng* rng, dgcolor::VertexId n,
                                 dgcolor::Degree delta_cap,
                                 const std::vector<dgcolor::Degree>& degrees,
                                 const std::unordered_set<std::uint64_t>& existing_edges,
                                 const std::vector<dgcolor::Degree>& pending_degrees,
                                 const std::unordered_set<std::uint64_t>& batch_edges,
                                 dgcolor::EdgeUpdate* update) {
  const dgcolor::VertexId u = rng->uniform_vertex(n);
  const dgcolor::VertexId v = rng->uniform_vertex(n);
  if (u == v) {
    return false;
  }

  const std::uint64_t key = EdgeKey(u, v);
  if (existing_edges.find(key) != existing_edges.end() ||
      batch_edges.find(key) != batch_edges.end()) {
    return false;
  }

  if (degrees[u] + pending_degrees[u] >= delta_cap ||
      degrees[v] + pending_degrees[v] >= delta_cap) {
    return false;
  }

  *update = dgcolor::EdgeUpdate{dgcolor::UpdateKind::Insert, u, v, 0};
  return true;
}

bool FindValidInsertion(dgcolor::Rng* rng, dgcolor::VertexId n, dgcolor::Degree delta_cap,
                        const std::vector<dgcolor::Degree>& degrees,
                        const std::unordered_set<std::uint64_t>& edges,
                        std::size_t max_generation_attempts, std::size_t* generation_attempts,
                        std::size_t max_local_attempts, dgcolor::EdgeUpdate* update) {
  std::size_t local_attempts = 0;
  while (*generation_attempts < max_generation_attempts && local_attempts < max_local_attempts) {
    ++(*generation_attempts);
    ++local_attempts;
    if (GenerateValidInsertion(rng, n, delta_cap, degrees, edges, update)) {
      return true;
    }
  }
  return false;
}

bool GenerateValidDeletion(dgcolor::Rng* rng, const std::vector<std::uint64_t>& edge_list,
                           dgcolor::EdgeUpdate* update) {
  if (edge_list.empty()) {
    return false;
  }
  const std::uint64_t key = edge_list[rng->uniform_index(edge_list.size())];
  *update = dgcolor::EdgeUpdate{dgcolor::UpdateKind::Delete, EdgeKeyU(key), EdgeKeyV(key), 0};
  return true;
}

void RecordShadowInsertion(const dgcolor::EdgeUpdate& update,
                           std::vector<dgcolor::Degree>* degrees,
                           std::unordered_set<std::uint64_t>* edges,
                           std::vector<std::uint64_t>* edge_list) {
  const std::uint64_t key = EdgeKey(update.u, update.v);
  edges->insert(key);
  edge_list->push_back(key);
  ++(*degrees)[update.u];
  ++(*degrees)[update.v];
}

void RecordShadowDeletion(const dgcolor::EdgeUpdate& update,
                          std::vector<dgcolor::Degree>* degrees,
                          std::unordered_set<std::uint64_t>* edges,
                          std::vector<std::uint64_t>* edge_list) {
  const std::uint64_t key = EdgeKey(update.u, update.v);
  edges->erase(key);
  --(*degrees)[update.u];
  --(*degrees)[update.v];
  for (std::size_t i = 0; i < edge_list->size(); ++i) {
    if ((*edge_list)[i] == key) {
      (*edge_list)[i] = edge_list->back();
      edge_list->pop_back();
      return;
    }
  }
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

  dgcolor::Rng rng(cfg.seed);
  std::size_t generation_attempts = 0;
  std::size_t updates_generated = 0;
  std::size_t initial_edges = 0;
  std::size_t final_edges = 0;
  std::size_t applied = 0;
  std::size_t rejected = 0;
  std::size_t vertices_touched_total = 0;
  std::uint32_t palette_multiplier_out = 0;
  dgcolor::Color palette_size_out = 0;
  std::uint32_t max_rounds_out = 0;
  std::uint64_t total_rounds_out = 0;
  std::uint64_t fallback_count_out = 0;
  dgcolor::Degree max_degree = 0;
  bool graph_validated = false;
  bool coloring_validated = false;
  std::string graph_validation_message;
  std::string coloring_validation_message;

  const auto build_start = std::chrono::steady_clock::now();
  std::unique_ptr<dgcolor::AdjacencyGraphStore> graph_only;
  std::unique_ptr<dgcolor::SeqBaselineEngine> seq_engine;
  std::unique_ptr<dgcolor::ParRelaxedEngine> par_relaxed_engine;
  if (cfg.engine == "graph_store_only") {
    graph_only = std::make_unique<dgcolor::AdjacencyGraphStore>(cfg.vertices, cfg.delta_cap);
    initial_edges = graph_only->num_edges();
  } else if (cfg.engine == "seq_baseline") {
    seq_engine = std::make_unique<dgcolor::SeqBaselineEngine>(cfg.vertices, cfg.delta_cap);
    seq_engine->initialize_coloring();
    initial_edges = seq_engine->graph().num_edges();
  } else {
    par_relaxed_engine = std::make_unique<dgcolor::ParRelaxedEngine>(
        cfg.vertices, cfg.delta_cap, cfg.seed, cfg.palette_multiplier, cfg.max_rounds);
    par_relaxed_engine->initialize_coloring();
    par_relaxed_engine->set_diagnostics_enabled(cfg.diagnostics);
    initial_edges = par_relaxed_engine->graph().num_edges();
    palette_multiplier_out = par_relaxed_engine->palette_multiplier();
    palette_size_out = par_relaxed_engine->palette_size();
    max_rounds_out = par_relaxed_engine->max_rounds();
  }
  const double build_seconds = SecondsSince(build_start);

  const auto update_start = std::chrono::steady_clock::now();
  if (cfg.workload == "valid_insertions") {
    std::vector<dgcolor::Degree> shadow_degrees(cfg.vertices, 0);
    std::unordered_set<std::uint64_t> shadow_edges;
    std::vector<std::uint64_t> shadow_edge_list;
    shadow_edges.reserve(cfg.updates * 2 + 1);
    shadow_edge_list.reserve(cfg.updates);

    while (updates_generated < cfg.updates && generation_attempts < cfg.max_generation_attempts) {
      ++generation_attempts;
      dgcolor::EdgeUpdate update{};
      if (!GenerateValidInsertion(&rng, cfg.vertices, cfg.delta_cap, shadow_degrees, shadow_edges,
                                  &update)) {
        continue;
      }

      ++updates_generated;
      bool update_applied = false;
      if (graph_only) {
        const dgcolor::UpdateResult result = graph_only->apply_update(update);
        update_applied = (result.status == dgcolor::UpdateStatus::Ok);
      } else if (seq_engine) {
        const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      }

      if (update_applied) {
        ++applied;
        RecordShadowInsertion(update, &shadow_degrees, &shadow_edges, &shadow_edge_list);
      } else {
        ++rejected;
      }
    }
  } else if (cfg.workload == "mixed_valid") {
    std::vector<dgcolor::Degree> shadow_degrees(cfg.vertices, 0);
    std::unordered_set<std::uint64_t> shadow_edges;
    std::vector<std::uint64_t> shadow_edge_list;
    shadow_edges.reserve(cfg.updates * 2 + 1);
    shadow_edge_list.reserve(cfg.updates);

    while (updates_generated < cfg.updates && generation_attempts < cfg.max_generation_attempts) {
      const std::size_t local_insert_attempts = static_cast<std::size_t>(cfg.vertices) * 4 + 16;
      const bool prefer_insert = rng.bernoulli(cfg.insert_ratio);
      dgcolor::EdgeUpdate update{};
      bool generated = false;

      if (prefer_insert) {
        generated = FindValidInsertion(&rng, cfg.vertices, cfg.delta_cap, shadow_degrees,
                                       shadow_edges, cfg.max_generation_attempts,
                                       &generation_attempts, local_insert_attempts, &update);
        if (!generated && generation_attempts < cfg.max_generation_attempts) {
          ++generation_attempts;
          generated = GenerateValidDeletion(&rng, shadow_edge_list, &update);
        }
      } else {
        ++generation_attempts;
        generated = GenerateValidDeletion(&rng, shadow_edge_list, &update);
        if (!generated) {
          generated = FindValidInsertion(&rng, cfg.vertices, cfg.delta_cap, shadow_degrees,
                                         shadow_edges, cfg.max_generation_attempts,
                                         &generation_attempts, local_insert_attempts, &update);
        }
      }

      if (!generated) {
        break;
      }

      ++updates_generated;
      bool update_applied = false;
      if (graph_only) {
        const dgcolor::UpdateResult result = graph_only->apply_update(update);
        update_applied = (result.status == dgcolor::UpdateStatus::Ok);
      } else if (seq_engine) {
        const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      }

      if (update_applied) {
        ++applied;
        if (update.kind == dgcolor::UpdateKind::Insert) {
          RecordShadowInsertion(update, &shadow_degrees, &shadow_edges, &shadow_edge_list);
        } else {
          RecordShadowDeletion(update, &shadow_degrees, &shadow_edges, &shadow_edge_list);
        }
      } else {
        ++rejected;
      }
    }
  } else if (cfg.workload == "batch_valid") {
    std::vector<dgcolor::Degree> shadow_degrees(cfg.vertices, 0);
    std::unordered_set<std::uint64_t> shadow_edges;
    std::vector<std::uint64_t> shadow_edge_list;
    shadow_edges.reserve(cfg.updates * 2 + 1);
    shadow_edge_list.reserve(cfg.updates);

    while (updates_generated < cfg.updates && generation_attempts < cfg.max_generation_attempts) {
      const std::size_t remaining_updates = cfg.updates - updates_generated;
      const std::size_t target_batch_size =
          (cfg.batch_size < remaining_updates) ? cfg.batch_size : remaining_updates;
      dgcolor::UpdateBatch batch;
      batch.reserve(target_batch_size);
      std::vector<dgcolor::Degree> pending_degrees(cfg.vertices, 0);
      std::unordered_set<std::uint64_t> batch_edges;
      batch_edges.reserve(target_batch_size * 2 + 1);

      while (batch.size() < target_batch_size &&
             generation_attempts < cfg.max_generation_attempts) {
        ++generation_attempts;
        dgcolor::EdgeUpdate update{};
        if (!GenerateValidBatchInsertion(&rng, cfg.vertices, cfg.delta_cap, shadow_degrees,
                                         shadow_edges, pending_degrees, batch_edges, &update)) {
          continue;
        }

        batch.push_back(update);
        batch_edges.insert(EdgeKey(update.u, update.v));
        ++pending_degrees[update.u];
        ++pending_degrees[update.v];
      }

      if (batch.empty()) {
        break;
      }

      updates_generated += batch.size();
      bool batch_applied = false;
      std::size_t edges_changed = 0;
      if (batch.size() == 1) {
        const dgcolor::EdgeUpdate update = batch[0];
        if (graph_only) {
          const dgcolor::UpdateResult result = graph_only->apply_update(update);
          batch_applied = (result.status == dgcolor::UpdateStatus::Ok);
          edges_changed = batch_applied ? 1 : 0;
        } else if (seq_engine) {
          const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
          batch_applied = stats.applied;
          edges_changed = stats.edges_changed;
          vertices_touched_total += stats.vertices_touched;
        } else {
          const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
          batch_applied = stats.applied;
          edges_changed = stats.edges_changed;
          vertices_touched_total += stats.vertices_touched;
        }
      } else if (graph_only) {
        const dgcolor::BatchApplyResult result = graph_only->apply_batch(batch);
        batch_applied = (result.status == dgcolor::UpdateStatus::Ok);
        edges_changed = batch_applied ? result.updates_applied : 0;
      } else if (seq_engine) {
        const dgcolor::BatchStats stats = seq_engine->apply_batch(batch);
        batch_applied = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::BatchStats stats = par_relaxed_engine->apply_batch(batch);
        batch_applied = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      }

      if (batch_applied) {
        applied += edges_changed;
        rejected += (batch.size() - edges_changed);
        for (const dgcolor::EdgeUpdate& update : batch) {
          RecordShadowInsertion(update, &shadow_degrees, &shadow_edges, &shadow_edge_list);
        }
      } else {
        rejected += batch.size();
      }
    }
  } else if (cfg.batch_size <= 1) {
    generation_attempts = cfg.updates;
    updates_generated = cfg.updates;
    for (std::size_t i = 0; i < cfg.updates; ++i) {
      const dgcolor::EdgeUpdate update = RandomAttempt(&rng, cfg.vertices);
      if (graph_only) {
        const dgcolor::UpdateResult result = graph_only->apply_update(update);
        if (result.status == dgcolor::UpdateStatus::Ok) {
          ++applied;
        } else {
          ++rejected;
        }
      } else if (seq_engine) {
        const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
        if (stats.applied) {
          ++applied;
        } else {
          ++rejected;
        }
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
        if (stats.applied) {
          ++applied;
        } else {
          ++rejected;
        }
        vertices_touched_total += stats.vertices_touched;
      }
    }
  } else {
    generation_attempts = cfg.updates;
    updates_generated = cfg.updates;
    dgcolor::UpdateBatch batch;
    batch.reserve(cfg.batch_size);
    for (std::size_t i = 0; i < cfg.updates; ++i) {
      batch.push_back(RandomAttempt(&rng, cfg.vertices));
      const bool batch_full = batch.size() >= cfg.batch_size;
      const bool last_update = (i + 1 == cfg.updates);
      if (!batch_full && !last_update) {
        continue;
      }

      if (graph_only) {
        const dgcolor::BatchApplyResult result = graph_only->apply_batch(batch);
        if (result.status == dgcolor::UpdateStatus::Ok) {
          applied += result.updates_applied;
        } else {
          rejected += batch.size();
        }
      } else if (seq_engine) {
        const dgcolor::BatchStats stats = seq_engine->apply_batch(batch);
        if (stats.applied) {
          applied += stats.edges_changed;
          rejected += (batch.size() - stats.edges_changed);
        } else {
          rejected += batch.size();
        }
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::BatchStats stats = par_relaxed_engine->apply_batch(batch);
        if (stats.applied) {
          applied += stats.edges_changed;
          rejected += (batch.size() - stats.edges_changed);
        } else {
          rejected += batch.size();
        }
        vertices_touched_total += stats.vertices_touched;
      }
      batch.clear();
    }
  }
  const double update_seconds = SecondsSince(update_start);

  const auto validate_start = std::chrono::steady_clock::now();
  if (graph_only) {
    const dgcolor::ValidationResult graph_validation =
        dgcolor::validate_graph_invariants(*graph_only);
    graph_validated = graph_validation.ok;
    graph_validation_message = graph_validation.message;
    final_edges = graph_only->num_edges();
    for (dgcolor::VertexId v = 0; v < graph_only->num_vertices(); ++v) {
      if (graph_only->degree(v) > max_degree) {
        max_degree = graph_only->degree(v);
      }
    }
  } else if (seq_engine) {
    const dgcolor::ValidationResult graph_validation =
        dgcolor::validate_graph_invariants(seq_engine->graph());
    graph_validated = graph_validation.ok;
    graph_validation_message = graph_validation.message;
    const dgcolor::ValidationResult color_validation =
        dgcolor::validate_exact_coloring(seq_engine->graph(), seq_engine->colors());
    coloring_validated = color_validation.ok;
    coloring_validation_message = color_validation.message;
    final_edges = seq_engine->graph().num_edges();
    for (dgcolor::VertexId v = 0; v < seq_engine->graph().num_vertices(); ++v) {
      if (seq_engine->graph().degree(v) > max_degree) {
        max_degree = seq_engine->graph().degree(v);
      }
    }
  } else {
    const dgcolor::ValidationResult graph_validation =
        dgcolor::validate_graph_invariants(par_relaxed_engine->graph());
    graph_validated = graph_validation.ok;
    graph_validation_message = graph_validation.message;
    const dgcolor::ValidationResult color_validation =
        dgcolor::validate_exact_coloring(par_relaxed_engine->graph(), par_relaxed_engine->colors());
    coloring_validated = color_validation.ok;
    coloring_validation_message = color_validation.message;
    if (coloring_validated) {
      for (const dgcolor::Color c : par_relaxed_engine->colors()) {
        if (c >= par_relaxed_engine->palette_size()) {
          coloring_validated = false;
          coloring_validation_message = "color out of range for configured relaxed palette";
          break;
        }
      }
    }
    final_edges = par_relaxed_engine->graph().num_edges();
    for (dgcolor::VertexId v = 0; v < par_relaxed_engine->graph().num_vertices(); ++v) {
      if (par_relaxed_engine->graph().degree(v) > max_degree) {
        max_degree = par_relaxed_engine->graph().degree(v);
      }
    }
    palette_multiplier_out = par_relaxed_engine->palette_multiplier();
    palette_size_out = par_relaxed_engine->palette_size();
    max_rounds_out = par_relaxed_engine->max_rounds();
    total_rounds_out = par_relaxed_engine->total_rounds();
    fallback_count_out = par_relaxed_engine->fallback_count();
  }
  const double validate_seconds = SecondsSince(validate_start);

  const double throughput =
      (update_seconds > 0.0) ? (static_cast<double>(cfg.updates) / update_seconds) : 0.0;
  const double accepted_ratio =
      (updates_generated > 0) ? (static_cast<double>(applied) / updates_generated) : 0.0;

  PrintMetric("benchmark_name", "foundation_smoke");
  PrintMetric("engine_name", cfg.engine);
  PrintMetric("workload", cfg.workload);
  PrintMetric("seed", cfg.seed);
  PrintMetric("num_vertices", cfg.vertices);
  PrintMetric("delta_cap", cfg.delta_cap);
  PrintMetric("generation_attempts", generation_attempts);
  PrintMetric("updates_requested", cfg.updates);
  PrintMetric("updates_generated", updates_generated);
  PrintMetric("updates_applied", applied);
  PrintMetric("updates_rejected", rejected);
  PrintMetric("accepted_ratio", accepted_ratio);
  PrintMetric("batch_size", cfg.batch_size);
  PrintMetric("initial_edges_requested", cfg.initial_edges_requested);
  PrintMetric("initial_edges", initial_edges);
  PrintMetric("final_edges", final_edges);
  PrintMetric("target_accepted", cfg.target_accepted);
  PrintMetric("insert_ratio", cfg.insert_ratio);
  PrintMetric("max_generation_attempts", cfg.max_generation_attempts);
  PrintMetric("build_seconds", build_seconds);
  PrintMetric("update_seconds", update_seconds);
  PrintMetric("validate_seconds", validate_seconds);
  PrintMetric("throughput_updates_per_second", throughput);
  PrintMetric("max_degree_observed", max_degree);
  PrintMetric("graph_validated", graph_validated ? 1 : 0);
  if (!graph_only) {
    PrintMetric("coloring_validated", coloring_validated ? 1 : 0);
    PrintMetric("vertices_touched_total", vertices_touched_total);
  }
  if (par_relaxed_engine) {
    PrintMetric("palette_multiplier", palette_multiplier_out);
    PrintMetric("palette_size", palette_size_out);
    PrintMetric("max_rounds", max_rounds_out);
    PrintMetric("total_rounds", total_rounds_out);
    PrintMetric("fallback_count", fallback_count_out);
    if (cfg.diagnostics) {
      const dgcolor::ParRelaxedDiagnostics diagnostics = par_relaxed_engine->diagnostics();
      PrintMetric("repair_calls", diagnostics.repair_calls);
      PrintMetric("repair_rounds", diagnostics.repair_rounds);
      PrintMetric("sequential_repair_calls", diagnostics.sequential_repair_calls);
      PrintMetric("sequential_repair_rounds", diagnostics.sequential_repair_rounds);
      PrintMetric("active_vertices_initial_total", diagnostics.active_vertices_initial_total);
      PrintMetric("active_vertices_expanded_total", diagnostics.active_vertices_expanded_total);
      PrintMetric("conflicted_vertices_initial_total", diagnostics.conflicted_vertices_initial_total);
      PrintMetric("neighbor_scans", diagnostics.neighbor_scans);
      PrintMetric("commits_total", diagnostics.commits_total);
      PrintMetric("repair_seconds", diagnostics.repair_seconds);
      PrintMetric("active_build_seconds", diagnostics.active_build_seconds);
    }
  }

  if (!graph_validated) {
    PrintMetric("validation_message", graph_validation_message);
    return 1;
  }
  if (!graph_only && !coloring_validated) {
    PrintMetric("color_validation_message", coloring_validation_message);
    return 1;
  }
  return 0;
}
