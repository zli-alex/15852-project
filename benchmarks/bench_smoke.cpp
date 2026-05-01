#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "dgcolor/batch.hpp"
#include "dgcolor/graph_store.hpp"
#include "dgcolor/par_exact_engine.hpp"
#include "dgcolor/par_relaxed_engine.hpp"
#include "dgcolor/rng.hpp"
#include "dgcolor/seq_baseline_engine.hpp"
#include "dgcolor/seq_exact_engine.hpp"
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
  bool par_exact_token_repair{false};
  std::string initial_file{};
  std::string updates_file{};
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
          cfg->engine != "par_relaxed" && cfg->engine != "seq_exact" &&
          cfg->engine != "par_exact") {
        throw std::invalid_argument(
            "invalid --engine (expected graph_store_only|seq_baseline|par_relaxed|seq_exact|par_exact)");
      }
    } else if (arg == "--workload" && i + 1 < argc) {
      cfg->workload = argv[++i];
      if (cfg->workload != "random_attempts" && cfg->workload != "valid_insertions" &&
          cfg->workload != "mixed_valid" && cfg->workload != "batch_valid" &&
          cfg->workload != "conflict_heavy" && cfg->workload != "file_stream") {
        throw std::invalid_argument(
            "invalid --workload (expected random_attempts|valid_insertions|mixed_valid|batch_valid|conflict_heavy|file_stream)");
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
    } else if (arg == "--par-exact-token-repair") {
      cfg->par_exact_token_repair = true;
    } else if (arg == "--initial-file" && i + 1 < argc) {
      cfg->initial_file = argv[++i];
    } else if (arg == "--updates-file" && i + 1 < argc) {
      cfg->updates_file = argv[++i];
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
  if (cfg->workload == "conflict_heavy" && cfg->engine == "graph_store_only") {
    throw std::invalid_argument(
        "conflict_heavy currently supports seq_baseline|par_relaxed|seq_exact|par_exact only");
  }
  if (cfg->workload == "file_stream" &&
      (cfg->initial_file.empty() || cfg->updates_file.empty())) {
    throw std::invalid_argument("file_stream requires --initial-file and --updates-file");
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

bool GenerateConflictHeavyBatchInsertion(
    dgcolor::Rng* rng, dgcolor::VertexId n, dgcolor::Degree delta_cap,
    const std::vector<dgcolor::Degree>& degrees,
    const std::unordered_set<std::uint64_t>& existing_edges,
    const std::vector<dgcolor::Degree>& pending_degrees,
    const std::unordered_set<std::uint64_t>& batch_edges,
    const parlay::sequence<dgcolor::Color>& colors, std::size_t* generator_same_color_attempts,
    dgcolor::EdgeUpdate* update) {
  ++(*generator_same_color_attempts);
  const dgcolor::VertexId u = rng->uniform_vertex(n);
  const dgcolor::VertexId v = rng->uniform_vertex(n);
  if (u == v) {
    return false;
  }
  if (colors[u] != colors[v]) {
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

bool FindConflictHeavyInsertion(
    dgcolor::Rng* rng, dgcolor::VertexId n, dgcolor::Degree delta_cap,
    const std::vector<dgcolor::Degree>& degrees, const std::unordered_set<std::uint64_t>& edges,
    const parlay::sequence<dgcolor::Color>& colors, std::size_t max_generation_attempts,
    std::size_t* generation_attempts, std::size_t max_local_attempts,
    std::size_t* generator_same_color_attempts, dgcolor::EdgeUpdate* update) {
  std::size_t local_attempts = 0;
  while (*generation_attempts < max_generation_attempts && local_attempts < max_local_attempts) {
    ++(*generation_attempts);
    ++local_attempts;
    ++(*generator_same_color_attempts);

    const dgcolor::VertexId u = rng->uniform_vertex(n);
    const dgcolor::VertexId v = rng->uniform_vertex(n);
    if (u == v) {
      continue;
    }
    if (colors[u] != colors[v]) {
      continue;
    }
    if (degrees[u] >= delta_cap || degrees[v] >= delta_cap) {
      continue;
    }
    if (edges.find(EdgeKey(u, v)) != edges.end()) {
      continue;
    }
    *update = dgcolor::EdgeUpdate{dgcolor::UpdateKind::Insert, u, v, 0};
    return true;
  }
  return false;
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

std::string Trim(const std::string& text) {
  std::size_t first = 0;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }
  std::size_t last = text.size();
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
    --last;
  }
  return text.substr(first, last - first);
}

bool IsDataLine(const std::string& line) {
  const std::string trimmed = Trim(line);
  return !trimmed.empty() && trimmed[0] != '#' && trimmed[0] != '%';
}

dgcolor::VertexId ParseVertexToken(const std::string& token, dgcolor::VertexId vertices,
                                   const std::string& path, std::size_t line_number) {
  try {
    std::size_t pos = 0;
    const std::uint64_t value = std::stoull(token, &pos);
    if (pos != token.size() || value > UINT32_MAX || value >= vertices) {
      throw std::invalid_argument("bad vertex");
    }
    return static_cast<dgcolor::VertexId>(value);
  } catch (...) {
    throw std::invalid_argument("parse_error: " + path + ":" + std::to_string(line_number) +
                                ": invalid vertex id '" + token + "'");
  }
}

dgcolor::UpdateBatch LoadInitialFile(const std::string& path, dgcolor::VertexId vertices) {
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument("parse_error: could not open initial file: " + path);
  }

  dgcolor::UpdateBatch updates;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (!IsDataLine(line)) {
      continue;
    }

    std::istringstream stream(line);
    std::string u_token;
    std::string v_token;
    std::string extra;
    if (!(stream >> u_token >> v_token) || (stream >> extra)) {
      throw std::invalid_argument("parse_error: " + path + ":" + std::to_string(line_number) +
                                  ": expected 'u v'");
    }

    updates.push_back(dgcolor::EdgeUpdate{
        dgcolor::UpdateKind::Insert, ParseVertexToken(u_token, vertices, path, line_number),
        ParseVertexToken(v_token, vertices, path, line_number), 0});
  }
  return updates;
}

std::vector<dgcolor::EdgeUpdate> LoadUpdatesFile(const std::string& path,
                                                 dgcolor::VertexId vertices) {
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument("parse_error: could not open updates file: " + path);
  }

  std::vector<dgcolor::EdgeUpdate> updates;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (!IsDataLine(line)) {
      continue;
    }

    std::istringstream stream(line);
    std::string kind_token;
    std::string u_token;
    std::string v_token;
    std::string extra;
    if (!(stream >> kind_token >> u_token >> v_token) || (stream >> extra)) {
      throw std::invalid_argument("parse_error: " + path + ":" + std::to_string(line_number) +
                                  ": expected 'I u v' or 'D u v'");
    }
    if (kind_token != "I" && kind_token != "D") {
      throw std::invalid_argument("parse_error: " + path + ":" + std::to_string(line_number) +
                                  ": expected update kind 'I' or 'D'");
    }

    updates.push_back(dgcolor::EdgeUpdate{
        kind_token == "I" ? dgcolor::UpdateKind::Insert : dgcolor::UpdateKind::Delete,
        ParseVertexToken(u_token, vertices, path, line_number),
        ParseVertexToken(v_token, vertices, path, line_number), updates.size()});
  }
  return updates;
}

bool ApplyOneUpdate(dgcolor::AdjacencyGraphStore* graph_only,
                    dgcolor::SeqBaselineEngine* seq_engine,
                    dgcolor::SeqExactEngine* seq_exact_engine,
                    dgcolor::ParExactEngine* par_exact_engine,
                    dgcolor::ParRelaxedEngine* par_relaxed_engine,
                    const dgcolor::EdgeUpdate& update, double* engine_apply_seconds,
                    double* graph_apply_seconds, std::size_t* vertices_touched_total,
                    std::size_t* edges_changed) {
  if (graph_only) {
    const auto apply_start = std::chrono::steady_clock::now();
    const dgcolor::UpdateResult result = graph_only->apply_update(update);
    const double apply_seconds = SecondsSince(apply_start);
    *graph_apply_seconds += apply_seconds;
    *engine_apply_seconds += apply_seconds;
    *edges_changed = (result.status == dgcolor::UpdateStatus::Ok) ? 1 : 0;
    return result.status == dgcolor::UpdateStatus::Ok;
  }
  if (seq_engine) {
    const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
    *engine_apply_seconds += stats.seconds;
    *vertices_touched_total += stats.vertices_touched;
    *edges_changed = stats.edges_changed;
    return stats.applied;
  }
  if (seq_exact_engine) {
    const dgcolor::UpdateStats stats = seq_exact_engine->apply_update(update);
    *engine_apply_seconds += stats.seconds;
    *vertices_touched_total += stats.vertices_touched;
    *edges_changed = stats.edges_changed;
    return stats.applied;
  }
  if (par_exact_engine) {
    const dgcolor::UpdateStats stats = par_exact_engine->apply_update(update);
    *engine_apply_seconds += stats.seconds;
    *vertices_touched_total += stats.vertices_touched;
    *edges_changed = stats.edges_changed;
    return stats.applied;
  }

  const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
  *engine_apply_seconds += stats.seconds;
  *vertices_touched_total += stats.vertices_touched;
  *edges_changed = stats.edges_changed;
  return stats.applied;
}

bool ApplyUpdateBatch(dgcolor::AdjacencyGraphStore* graph_only,
                      dgcolor::SeqBaselineEngine* seq_engine,
                      dgcolor::SeqExactEngine* seq_exact_engine,
                      dgcolor::ParExactEngine* par_exact_engine,
                      dgcolor::ParRelaxedEngine* par_relaxed_engine,
                      const dgcolor::UpdateBatch& batch, double* engine_apply_seconds,
                      double* graph_apply_seconds, std::size_t* vertices_touched_total,
                      std::size_t* edges_changed) {
  if (graph_only) {
    const auto apply_start = std::chrono::steady_clock::now();
    const dgcolor::BatchApplyResult result = graph_only->apply_batch(batch);
    const double apply_seconds = SecondsSince(apply_start);
    *graph_apply_seconds += apply_seconds;
    *engine_apply_seconds += apply_seconds;
    *edges_changed = (result.status == dgcolor::UpdateStatus::Ok) ? result.updates_applied : 0;
    return result.status == dgcolor::UpdateStatus::Ok;
  }
  if (seq_engine) {
    const dgcolor::BatchStats stats = seq_engine->apply_batch(batch);
    *engine_apply_seconds += stats.seconds;
    *vertices_touched_total += stats.vertices_touched;
    *edges_changed = stats.edges_changed;
    return stats.applied;
  }
  if (seq_exact_engine) {
    const dgcolor::BatchStats stats = seq_exact_engine->apply_batch(batch);
    *engine_apply_seconds += stats.seconds;
    *vertices_touched_total += stats.vertices_touched;
    *edges_changed = stats.edges_changed;
    return stats.applied;
  }
  if (par_exact_engine) {
    const dgcolor::BatchStats stats = par_exact_engine->apply_batch(batch);
    *engine_apply_seconds += stats.seconds;
    *vertices_touched_total += stats.vertices_touched;
    *edges_changed = stats.edges_changed;
    return stats.applied;
  }

  const dgcolor::BatchStats stats = par_relaxed_engine->apply_batch(batch);
  *engine_apply_seconds += stats.seconds;
  *vertices_touched_total += stats.vertices_touched;
  *edges_changed = stats.edges_changed;
  return stats.applied;
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

  dgcolor::UpdateBatch initial_file_updates;
  std::vector<dgcolor::EdgeUpdate> file_stream_updates;
  if (cfg.workload == "file_stream") {
    try {
      initial_file_updates = LoadInitialFile(cfg.initial_file, cfg.vertices);
      file_stream_updates = LoadUpdatesFile(cfg.updates_file, cfg.vertices);
      cfg.initial_edges_requested = initial_file_updates.size();
      cfg.updates = file_stream_updates.size();
      cfg.max_generation_attempts = 0;
    } catch (const std::exception& e) {
      std::cerr << "argument_error=" << e.what() << "\n";
      return 2;
    }
  }

  dgcolor::Rng rng(cfg.seed);
  std::size_t generation_attempts = 0;
  std::size_t updates_generated = 0;
  std::size_t initial_edges = 0;
  std::size_t final_edges = 0;
  std::size_t applied = 0;
  std::size_t rejected = 0;
  std::size_t batches_generated = 0;
  std::size_t batches_applied = 0;
  std::size_t generator_same_color_attempts = 0;
  std::size_t generator_same_color_chosen = 0;
  std::size_t generator_same_color_fallbacks = 0;
  std::size_t vertices_touched_total = 0;
  double engine_apply_seconds = 0.0;
  double graph_apply_seconds = 0.0;
  std::uint32_t palette_multiplier_out = 0;
  dgcolor::Color palette_size_out = 0;
  std::uint32_t max_rounds_out = 0;
  std::uint64_t total_rounds_out = 0;
  std::uint64_t fallback_count_out = 0;
  std::size_t seq_exact_palette_size_out = 0;
  std::uint64_t seq_exact_recolor_calls_out = 0;
  std::uint64_t seq_exact_recolored_vertices_total_out = 0;
  std::uint64_t seq_exact_cascade_steps_total_out = 0;
  std::uint64_t seq_exact_full_fallback_count_out = 0;
  std::uint64_t seq_exact_level_conflict_choices_out = 0;
  std::size_t par_exact_palette_size_out = 0;
  std::uint32_t par_exact_max_rounds_out = 0;
  std::uint64_t par_exact_active_vertices_total_out = 0;
  std::uint64_t par_exact_repair_rounds_total_out = 0;
  std::uint64_t par_exact_fallback_count_out = 0;
  std::uint64_t par_exact_proposal_count_out = 0;
  std::uint64_t par_exact_commit_count_out = 0;
  std::uint64_t par_exact_unresolved_count_out = 0;
  std::uint64_t par_exact_sequential_fast_path_count_out = 0;
  std::uint64_t par_exact_vertices_touched_total_out = 0;
  dgcolor::Degree max_degree = 0;
  bool graph_validated = false;
  bool coloring_validated = false;
  std::string graph_validation_message;
  std::string coloring_validation_message;

  const auto build_start = std::chrono::steady_clock::now();
  std::unique_ptr<dgcolor::AdjacencyGraphStore> graph_only;
  std::unique_ptr<dgcolor::SeqBaselineEngine> seq_engine;
  std::unique_ptr<dgcolor::SeqExactEngine> seq_exact_engine;
  std::unique_ptr<dgcolor::ParRelaxedEngine> par_relaxed_engine;
  std::unique_ptr<dgcolor::ParExactEngine> par_exact_engine;
  try {
    if (cfg.engine == "graph_store_only") {
      graph_only = std::make_unique<dgcolor::AdjacencyGraphStore>(cfg.vertices, cfg.delta_cap);
      if (!initial_file_updates.empty()) {
        const dgcolor::BatchApplyResult init_result = graph_only->apply_batch(initial_file_updates);
        if (init_result.status != dgcolor::UpdateStatus::Ok) {
          throw std::invalid_argument("initial graph updates were rejected: " +
                                      std::string(dgcolor::update_status_name(init_result.status)) +
                                      (init_result.message.empty() ? "" : ": " + init_result.message));
        }
      }
      initial_edges = graph_only->num_edges();
    } else if (cfg.engine == "seq_baseline") {
      if (cfg.workload == "file_stream") {
        seq_engine =
            std::make_unique<dgcolor::SeqBaselineEngine>(cfg.vertices, cfg.delta_cap, initial_file_updates);
      } else {
        seq_engine = std::make_unique<dgcolor::SeqBaselineEngine>(cfg.vertices, cfg.delta_cap);
      }
      seq_engine->initialize_coloring();
      initial_edges = seq_engine->graph().num_edges();
    } else if (cfg.engine == "seq_exact") {
      if (cfg.workload == "file_stream") {
        seq_exact_engine = std::make_unique<dgcolor::SeqExactEngine>(
            cfg.vertices, cfg.delta_cap, cfg.seed, initial_file_updates);
      } else {
        seq_exact_engine =
            std::make_unique<dgcolor::SeqExactEngine>(cfg.vertices, cfg.delta_cap, cfg.seed);
      }
      seq_exact_engine->initialize_coloring();
      initial_edges = seq_exact_engine->graph().num_edges();
      seq_exact_palette_size_out = seq_exact_engine->palette_size();
    } else if (cfg.engine == "par_exact") {
      if (cfg.workload == "file_stream") {
        par_exact_engine = std::make_unique<dgcolor::ParExactEngine>(
            cfg.vertices, cfg.delta_cap, cfg.seed, cfg.max_rounds, initial_file_updates);
      } else {
        par_exact_engine = std::make_unique<dgcolor::ParExactEngine>(
            cfg.vertices, cfg.delta_cap, cfg.seed, cfg.max_rounds);
      }
      par_exact_engine->initialize_coloring();
      par_exact_engine->set_diagnostics_enabled(cfg.diagnostics);
      par_exact_engine->set_token_repair_enabled(cfg.par_exact_token_repair);
      if (cfg.validate_final_only) {
        par_exact_engine->set_validate_after_apply(false);
      }
      initial_edges = par_exact_engine->graph().num_edges();
      par_exact_palette_size_out = par_exact_engine->palette_size();
      par_exact_max_rounds_out = par_exact_engine->max_rounds();
    } else {
      if (cfg.workload == "file_stream") {
        par_relaxed_engine = std::make_unique<dgcolor::ParRelaxedEngine>(
            cfg.vertices, cfg.delta_cap, cfg.seed, cfg.palette_multiplier, cfg.max_rounds,
            initial_file_updates);
      } else {
        par_relaxed_engine = std::make_unique<dgcolor::ParRelaxedEngine>(
            cfg.vertices, cfg.delta_cap, cfg.seed, cfg.palette_multiplier, cfg.max_rounds);
      }
      par_relaxed_engine->initialize_coloring();
      par_relaxed_engine->set_diagnostics_enabled(cfg.diagnostics);
      if (cfg.validate_final_only) {
        par_relaxed_engine->set_validate_after_apply(false);
      }
      initial_edges = par_relaxed_engine->graph().num_edges();
      palette_multiplier_out = par_relaxed_engine->palette_multiplier();
      palette_size_out = par_relaxed_engine->palette_size();
      max_rounds_out = par_relaxed_engine->max_rounds();
    }
  } catch (const std::exception& e) {
    std::cerr << "argument_error=" << e.what() << "\n";
    return 2;
  }
  const double build_seconds = SecondsSince(build_start);

  const auto update_start = std::chrono::steady_clock::now();
  if (cfg.workload == "file_stream") {
    updates_generated = file_stream_updates.size();
    if (cfg.batch_size <= 1) {
      for (const dgcolor::EdgeUpdate& update : file_stream_updates) {
        std::size_t edges_changed = 0;
        const bool update_applied = ApplyOneUpdate(
            graph_only.get(), seq_engine.get(), seq_exact_engine.get(), par_exact_engine.get(),
            par_relaxed_engine.get(), update, &engine_apply_seconds, &graph_apply_seconds,
            &vertices_touched_total, &edges_changed);
        if (update_applied) {
          applied += edges_changed;
        } else {
          ++rejected;
        }
      }
    } else {
      dgcolor::UpdateBatch batch;
      batch.reserve(cfg.batch_size);
      for (std::size_t i = 0; i < file_stream_updates.size(); ++i) {
        batch.push_back(file_stream_updates[i]);
        const bool batch_full = batch.size() >= cfg.batch_size;
        const bool last_update = (i + 1 == file_stream_updates.size());
        if (!batch_full && !last_update) {
          continue;
        }

        ++batches_generated;
        std::size_t edges_changed = 0;
        const bool batch_applied = ApplyUpdateBatch(
            graph_only.get(), seq_engine.get(), seq_exact_engine.get(), par_exact_engine.get(),
            par_relaxed_engine.get(), batch, &engine_apply_seconds, &graph_apply_seconds,
            &vertices_touched_total, &edges_changed);
        if (batch_applied) {
          ++batches_applied;
          applied += edges_changed;
          rejected += (batch.size() - edges_changed);
        } else {
          rejected += batch.size();
        }
        batch.clear();
      }
    }
  } else if (cfg.workload == "valid_insertions") {
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
        const auto apply_start = std::chrono::steady_clock::now();
        const dgcolor::UpdateResult result = graph_only->apply_update(update);
        const double apply_seconds = SecondsSince(apply_start);
        graph_apply_seconds += apply_seconds;
        engine_apply_seconds += apply_seconds;
        update_applied = (result.status == dgcolor::UpdateStatus::Ok);
      } else if (seq_engine) {
        const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else if (seq_exact_engine) {
        const dgcolor::UpdateStats stats = seq_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else if (par_exact_engine) {
        const dgcolor::UpdateStats stats = par_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
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
  } else if (cfg.workload == "conflict_heavy" && cfg.batch_size <= 1) {
    std::vector<dgcolor::Degree> shadow_degrees(cfg.vertices, 0);
    std::unordered_set<std::uint64_t> shadow_edges;
    std::vector<std::uint64_t> shadow_edge_list;
    shadow_edges.reserve(cfg.updates * 2 + 1);
    shadow_edge_list.reserve(cfg.updates);

    while (updates_generated < cfg.updates && generation_attempts < cfg.max_generation_attempts) {
      const std::size_t local_same_color_attempts = static_cast<std::size_t>(cfg.vertices) * 4 + 16;
      const std::size_t local_fallback_attempts = static_cast<std::size_t>(cfg.vertices) * 4 + 16;
      parlay::sequence<dgcolor::Color> colors_snapshot =
          seq_engine ? seq_engine->colors()
                     : (seq_exact_engine ? seq_exact_engine->colors()
                                         : (par_exact_engine ? par_exact_engine->colors()
                                                             : par_relaxed_engine->colors()));

      dgcolor::EdgeUpdate update{};
      bool generated = FindConflictHeavyInsertion(
          &rng, cfg.vertices, cfg.delta_cap, shadow_degrees, shadow_edges, colors_snapshot,
          cfg.max_generation_attempts, &generation_attempts, local_same_color_attempts,
          &generator_same_color_attempts, &update);

      if (generated) {
        ++generator_same_color_chosen;
      } else {
        ++generator_same_color_fallbacks;
        generated = FindValidInsertion(&rng, cfg.vertices, cfg.delta_cap, shadow_degrees,
                                       shadow_edges, cfg.max_generation_attempts,
                                       &generation_attempts, local_fallback_attempts, &update);
      }

      if (!generated) {
        break;
      }

      ++updates_generated;
      bool update_applied = false;
      if (seq_engine) {
        const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else if (seq_exact_engine) {
        const dgcolor::UpdateStats stats = seq_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else if (par_exact_engine) {
        const dgcolor::UpdateStats stats = par_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
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
  } else if (cfg.workload == "conflict_heavy") {
    std::vector<dgcolor::Degree> shadow_degrees(cfg.vertices, 0);
    std::unordered_set<std::uint64_t> shadow_edges;
    std::vector<std::uint64_t> shadow_edge_list;
    shadow_edges.reserve(cfg.updates * 2 + 1);
    shadow_edge_list.reserve(cfg.updates);

    while (updates_generated < cfg.updates && generation_attempts < cfg.max_generation_attempts) {
      const std::size_t remaining_updates = cfg.updates - updates_generated;
      const std::size_t target_batch_size =
          (cfg.batch_size < remaining_updates) ? cfg.batch_size : remaining_updates;
      const std::size_t local_same_color_attempts =
          (static_cast<std::size_t>(cfg.vertices) * 4 + 16) * target_batch_size;
      const std::size_t local_fallback_attempts =
          (static_cast<std::size_t>(cfg.vertices) * 4 + 16) * target_batch_size;

      dgcolor::UpdateBatch batch;
      batch.reserve(target_batch_size);
      std::vector<dgcolor::Degree> pending_degrees(cfg.vertices, 0);
      std::unordered_set<std::uint64_t> batch_edges;
      batch_edges.reserve(target_batch_size * 2 + 1);
      const parlay::sequence<dgcolor::Color> colors_snapshot =
          seq_engine ? seq_engine->colors()
                     : (seq_exact_engine ? seq_exact_engine->colors()
                                         : (par_exact_engine ? par_exact_engine->colors()
                                                             : par_relaxed_engine->colors()));

      std::size_t same_color_attempts_local = 0;
      while (batch.size() < target_batch_size &&
             generation_attempts < cfg.max_generation_attempts &&
             same_color_attempts_local < local_same_color_attempts) {
        ++generation_attempts;
        ++same_color_attempts_local;
        dgcolor::EdgeUpdate update{};
        if (!GenerateConflictHeavyBatchInsertion(
                &rng, cfg.vertices, cfg.delta_cap, shadow_degrees, shadow_edges, pending_degrees,
                batch_edges, colors_snapshot, &generator_same_color_attempts, &update)) {
          continue;
        }
        ++generator_same_color_chosen;
        batch.push_back(update);
        batch_edges.insert(EdgeKey(update.u, update.v));
        ++pending_degrees[update.u];
        ++pending_degrees[update.v];
      }

      std::size_t fallback_attempts_local = 0;
      bool used_fallback = false;
      while (batch.size() < target_batch_size &&
             generation_attempts < cfg.max_generation_attempts &&
             fallback_attempts_local < local_fallback_attempts) {
        ++generation_attempts;
        ++fallback_attempts_local;
        dgcolor::EdgeUpdate update{};
        if (!GenerateValidBatchInsertion(&rng, cfg.vertices, cfg.delta_cap, shadow_degrees,
                                         shadow_edges, pending_degrees, batch_edges, &update)) {
          continue;
        }
        used_fallback = true;
        batch.push_back(update);
        batch_edges.insert(EdgeKey(update.u, update.v));
        ++pending_degrees[update.u];
        ++pending_degrees[update.v];
      }

      if (used_fallback) {
        ++generator_same_color_fallbacks;
      }
      if (batch.empty()) {
        break;
      }

      ++batches_generated;
      updates_generated += batch.size();
      bool batch_ok = false;
      std::size_t edges_changed = 0;
      if (seq_engine) {
        const dgcolor::BatchStats stats = seq_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        batch_ok = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      } else if (seq_exact_engine) {
        const dgcolor::BatchStats stats = seq_exact_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        batch_ok = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      } else if (par_exact_engine) {
        const dgcolor::BatchStats stats = par_exact_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        batch_ok = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::BatchStats stats = par_relaxed_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        batch_ok = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      }

      if (batch_ok) {
        ++batches_applied;
        applied += edges_changed;
        rejected += (batch.size() - edges_changed);
        for (const dgcolor::EdgeUpdate& update : batch) {
          RecordShadowInsertion(update, &shadow_degrees, &shadow_edges, &shadow_edge_list);
        }
      } else {
        rejected += batch.size();
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
        const auto apply_start = std::chrono::steady_clock::now();
        const dgcolor::UpdateResult result = graph_only->apply_update(update);
        const double apply_seconds = SecondsSince(apply_start);
        graph_apply_seconds += apply_seconds;
        engine_apply_seconds += apply_seconds;
        update_applied = (result.status == dgcolor::UpdateStatus::Ok);
      } else if (seq_engine) {
        const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else if (seq_exact_engine) {
        const dgcolor::UpdateStats stats = seq_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else if (par_exact_engine) {
        const dgcolor::UpdateStats stats = par_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        update_applied = stats.applied;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
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
          const auto apply_start = std::chrono::steady_clock::now();
          const dgcolor::UpdateResult result = graph_only->apply_update(update);
          const double apply_seconds = SecondsSince(apply_start);
          graph_apply_seconds += apply_seconds;
          engine_apply_seconds += apply_seconds;
          batch_applied = (result.status == dgcolor::UpdateStatus::Ok);
          edges_changed = batch_applied ? 1 : 0;
        } else if (seq_engine) {
          const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
          engine_apply_seconds += stats.seconds;
          batch_applied = stats.applied;
          edges_changed = stats.edges_changed;
          vertices_touched_total += stats.vertices_touched;
        } else if (seq_exact_engine) {
          const dgcolor::UpdateStats stats = seq_exact_engine->apply_update(update);
          engine_apply_seconds += stats.seconds;
          batch_applied = stats.applied;
          edges_changed = stats.edges_changed;
          vertices_touched_total += stats.vertices_touched;
        } else if (par_exact_engine) {
          const dgcolor::UpdateStats stats = par_exact_engine->apply_update(update);
          engine_apply_seconds += stats.seconds;
          batch_applied = stats.applied;
          edges_changed = stats.edges_changed;
          vertices_touched_total += stats.vertices_touched;
        } else {
          const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
          engine_apply_seconds += stats.seconds;
          batch_applied = stats.applied;
          edges_changed = stats.edges_changed;
          vertices_touched_total += stats.vertices_touched;
        }
      } else if (graph_only) {
        const auto apply_start = std::chrono::steady_clock::now();
        const dgcolor::BatchApplyResult result = graph_only->apply_batch(batch);
        const double apply_seconds = SecondsSince(apply_start);
        graph_apply_seconds += apply_seconds;
        engine_apply_seconds += apply_seconds;
        batch_applied = (result.status == dgcolor::UpdateStatus::Ok);
        edges_changed = batch_applied ? result.updates_applied : 0;
      } else if (seq_engine) {
        const dgcolor::BatchStats stats = seq_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        batch_applied = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      } else if (seq_exact_engine) {
        const dgcolor::BatchStats stats = seq_exact_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        batch_applied = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      } else if (par_exact_engine) {
        const dgcolor::BatchStats stats = par_exact_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        batch_applied = stats.applied;
        edges_changed = stats.edges_changed;
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::BatchStats stats = par_relaxed_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
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
        const auto apply_start = std::chrono::steady_clock::now();
        const dgcolor::UpdateResult result = graph_only->apply_update(update);
        const double apply_seconds = SecondsSince(apply_start);
        graph_apply_seconds += apply_seconds;
        engine_apply_seconds += apply_seconds;
        if (result.status == dgcolor::UpdateStatus::Ok) {
          ++applied;
        } else {
          ++rejected;
        }
      } else if (seq_engine) {
        const dgcolor::UpdateStats stats = seq_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        if (stats.applied) {
          ++applied;
        } else {
          ++rejected;
        }
        vertices_touched_total += stats.vertices_touched;
      } else if (seq_exact_engine) {
        const dgcolor::UpdateStats stats = seq_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        if (stats.applied) {
          ++applied;
        } else {
          ++rejected;
        }
        vertices_touched_total += stats.vertices_touched;
      } else if (par_exact_engine) {
        const dgcolor::UpdateStats stats = par_exact_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
        if (stats.applied) {
          ++applied;
        } else {
          ++rejected;
        }
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::UpdateStats stats = par_relaxed_engine->apply_update(update);
        engine_apply_seconds += stats.seconds;
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
        const auto apply_start = std::chrono::steady_clock::now();
        const dgcolor::BatchApplyResult result = graph_only->apply_batch(batch);
        const double apply_seconds = SecondsSince(apply_start);
        graph_apply_seconds += apply_seconds;
        engine_apply_seconds += apply_seconds;
        if (result.status == dgcolor::UpdateStatus::Ok) {
          applied += result.updates_applied;
        } else {
          rejected += batch.size();
        }
      } else if (seq_engine) {
        const dgcolor::BatchStats stats = seq_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        if (stats.applied) {
          applied += stats.edges_changed;
          rejected += (batch.size() - stats.edges_changed);
        } else {
          rejected += batch.size();
        }
        vertices_touched_total += stats.vertices_touched;
      } else if (seq_exact_engine) {
        const dgcolor::BatchStats stats = seq_exact_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        if (stats.applied) {
          applied += stats.edges_changed;
          rejected += (batch.size() - stats.edges_changed);
        } else {
          rejected += batch.size();
        }
        vertices_touched_total += stats.vertices_touched;
      } else if (par_exact_engine) {
        const dgcolor::BatchStats stats = par_exact_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
        if (stats.applied) {
          applied += stats.edges_changed;
          rejected += (batch.size() - stats.edges_changed);
        } else {
          rejected += batch.size();
        }
        vertices_touched_total += stats.vertices_touched;
      } else {
        const dgcolor::BatchStats stats = par_relaxed_engine->apply_batch(batch);
        engine_apply_seconds += stats.seconds;
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
  } else if (seq_exact_engine) {
    const dgcolor::ValidationResult graph_validation =
        dgcolor::validate_graph_invariants(seq_exact_engine->graph());
    graph_validated = graph_validation.ok;
    graph_validation_message = graph_validation.message;
    const dgcolor::ValidationResult color_validation =
        dgcolor::validate_exact_coloring(seq_exact_engine->graph(), seq_exact_engine->colors());
    coloring_validated = color_validation.ok;
    coloring_validation_message = color_validation.message;
    final_edges = seq_exact_engine->graph().num_edges();
    for (dgcolor::VertexId v = 0; v < seq_exact_engine->graph().num_vertices(); ++v) {
      if (seq_exact_engine->graph().degree(v) > max_degree) {
        max_degree = seq_exact_engine->graph().degree(v);
      }
    }
    seq_exact_palette_size_out = seq_exact_engine->palette_size();
    seq_exact_recolor_calls_out = seq_exact_engine->recolor_calls();
    seq_exact_recolored_vertices_total_out = seq_exact_engine->recolored_vertices_total();
    seq_exact_cascade_steps_total_out = seq_exact_engine->cascade_steps_total();
    seq_exact_full_fallback_count_out = seq_exact_engine->full_fallback_count();
    seq_exact_level_conflict_choices_out = seq_exact_engine->level_conflict_choices();
  } else if (par_exact_engine) {
    const dgcolor::ValidationResult graph_validation =
        dgcolor::validate_graph_invariants(par_exact_engine->graph());
    graph_validated = graph_validation.ok;
    graph_validation_message = graph_validation.message;
    const dgcolor::ValidationResult color_validation =
        dgcolor::validate_exact_coloring(par_exact_engine->graph(), par_exact_engine->colors());
    coloring_validated = color_validation.ok;
    coloring_validation_message = color_validation.message;
    if (coloring_validated) {
      for (const dgcolor::Color c : par_exact_engine->colors()) {
        if (c >= par_exact_engine->palette_size()) {
          coloring_validated = false;
          coloring_validation_message = "color out of range for exact palette";
          break;
        }
      }
    }
    final_edges = par_exact_engine->graph().num_edges();
    for (dgcolor::VertexId v = 0; v < par_exact_engine->graph().num_vertices(); ++v) {
      if (par_exact_engine->graph().degree(v) > max_degree) {
        max_degree = par_exact_engine->graph().degree(v);
      }
    }
    par_exact_palette_size_out = par_exact_engine->palette_size();
    par_exact_max_rounds_out = par_exact_engine->max_rounds();
    par_exact_active_vertices_total_out = par_exact_engine->active_vertices_total();
    par_exact_repair_rounds_total_out = par_exact_engine->repair_rounds_total();
    par_exact_fallback_count_out = par_exact_engine->fallback_count();
    par_exact_proposal_count_out = par_exact_engine->proposal_count();
    par_exact_commit_count_out = par_exact_engine->commit_count();
    par_exact_unresolved_count_out = par_exact_engine->unresolved_count();
    par_exact_sequential_fast_path_count_out = par_exact_engine->sequential_fast_path_count();
    par_exact_vertices_touched_total_out = par_exact_engine->vertices_touched_total();
    vertices_touched_total = static_cast<std::size_t>(par_exact_vertices_touched_total_out);
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
  if (par_exact_engine) {
    graph_apply_seconds = par_exact_engine->diagnostics().graph_apply_seconds;
  } else if (par_relaxed_engine) {
    graph_apply_seconds = par_relaxed_engine->diagnostics().graph_apply_seconds;
  }

  const double throughput =
      (update_seconds > 0.0) ? (static_cast<double>(cfg.updates) / update_seconds) : 0.0;
  const double accepted_ratio =
      (updates_generated > 0) ? (static_cast<double>(applied) / updates_generated) : 0.0;
  const double batch_accepted_ratio =
      (batches_generated > 0) ? (static_cast<double>(batches_applied) / batches_generated) : 0.0;

  PrintMetric("benchmark_name", "foundation_smoke");
  PrintMetric("engine_name", cfg.engine);
  PrintMetric("workload", cfg.workload);
  if (cfg.workload == "file_stream") {
    PrintMetric("initial_file", cfg.initial_file);
    PrintMetric("updates_file", cfg.updates_file);
  }
  PrintMetric("seed", cfg.seed);
  PrintMetric("num_vertices", cfg.vertices);
  PrintMetric("delta_cap", cfg.delta_cap);
  PrintMetric("generation_attempts", generation_attempts);
  PrintMetric("updates_requested", cfg.updates);
  PrintMetric("updates_generated", updates_generated);
  PrintMetric("updates_applied", applied);
  PrintMetric("updates_rejected", rejected);
  PrintMetric("accepted_ratio", accepted_ratio);
  PrintMetric("batches_generated", batches_generated);
  PrintMetric("batches_applied", batches_applied);
  PrintMetric("batch_accepted_ratio", batch_accepted_ratio);
  PrintMetric("generator_same_color_attempts", generator_same_color_attempts);
  PrintMetric("generator_same_color_chosen", generator_same_color_chosen);
  PrintMetric("generator_same_color_fallbacks", generator_same_color_fallbacks);
  PrintMetric("batch_size", cfg.batch_size);
  PrintMetric("initial_edges_requested", cfg.initial_edges_requested);
  PrintMetric("initial_edges", initial_edges);
  PrintMetric("final_edges", final_edges);
  PrintMetric("target_accepted", cfg.target_accepted);
  PrintMetric("insert_ratio", cfg.insert_ratio);
  PrintMetric("max_generation_attempts", cfg.max_generation_attempts);
  PrintMetric("build_seconds", build_seconds);
  PrintMetric("update_seconds", update_seconds);
  PrintMetric("generation_seconds", update_seconds - engine_apply_seconds);
  PrintMetric("engine_apply_seconds", engine_apply_seconds);
  PrintMetric("graph_apply_seconds", graph_apply_seconds);
  PrintMetric("validate_seconds", validate_seconds);
  PrintMetric("validate_final_only", cfg.validate_final_only ? 1 : 0);
  PrintMetric("par_exact_token_repair", cfg.par_exact_token_repair ? 1 : 0);
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
    const dgcolor::ParRelaxedDiagnostics diagnostics = par_relaxed_engine->diagnostics();
    PrintMetric("repair_seconds", diagnostics.repair_seconds);
    PrintMetric("active_build_seconds", diagnostics.active_build_seconds);
    PrintMetric("internal_validation_seconds", diagnostics.internal_validation_seconds);
    PrintMetric("max_active_size", diagnostics.max_active_size);
    PrintMetric("active_size_round_total", diagnostics.active_size_round_total);
    if (cfg.diagnostics) {
      PrintMetric("repair_calls", diagnostics.repair_calls);
      PrintMetric("repair_rounds", diagnostics.repair_rounds);
      PrintMetric("sequential_repair_calls", diagnostics.sequential_repair_calls);
      PrintMetric("sequential_repair_rounds", diagnostics.sequential_repair_rounds);
      PrintMetric("active_vertices_initial_total", diagnostics.active_vertices_initial_total);
      PrintMetric("active_vertices_expanded_total", diagnostics.active_vertices_expanded_total);
      PrintMetric("conflicted_vertices_initial_total", diagnostics.conflicted_vertices_initial_total);
      PrintMetric("neighbor_scans", diagnostics.neighbor_scans);
      PrintMetric("commits_total", diagnostics.commits_total);
    }
  }
  if (seq_exact_engine) {
    PrintMetric("palette_size", seq_exact_palette_size_out);
    PrintMetric("recolor_calls", seq_exact_recolor_calls_out);
    PrintMetric("recolored_vertices_total", seq_exact_recolored_vertices_total_out);
    PrintMetric("cascade_steps_total", seq_exact_cascade_steps_total_out);
    PrintMetric("full_fallback_count", seq_exact_full_fallback_count_out);
    PrintMetric("level_conflict_choices", seq_exact_level_conflict_choices_out);
  }
  if (par_exact_engine) {
    const dgcolor::ParExactDiagnostics diagnostics = par_exact_engine->diagnostics();
    PrintMetric("palette_size", par_exact_palette_size_out);
    PrintMetric("max_rounds", par_exact_max_rounds_out);
    PrintMetric("active_vertices_total", par_exact_active_vertices_total_out);
    PrintMetric("repair_rounds_total", par_exact_repair_rounds_total_out);
    PrintMetric("fallback_count", par_exact_fallback_count_out);
    PrintMetric("proposal_count", par_exact_proposal_count_out);
    PrintMetric("commit_count", par_exact_commit_count_out);
    PrintMetric("unresolved_count", par_exact_unresolved_count_out);
    PrintMetric("sequential_fast_path_count", par_exact_sequential_fast_path_count_out);
    PrintMetric("repair_calls", diagnostics.repair_calls);
    PrintMetric("repair_seconds", diagnostics.repair_seconds);
    PrintMetric("active_build_seconds", diagnostics.active_build_seconds);
    PrintMetric("internal_validation_seconds", diagnostics.internal_validation_seconds);
    PrintMetric("max_active_size", diagnostics.max_active_size);
    PrintMetric("active_size_round_total", diagnostics.active_size_round_total);
    PrintMetric("neighbor_materializations", diagnostics.neighbor_materializations);
    PrintMetric("direct_neighbor_scans", diagnostics.direct_neighbor_scans);
    PrintMetric("level_ge_neighbor_scans", diagnostics.level_ge_neighbor_scans);
    PrintMetric("level_le_neighbor_scans", diagnostics.level_le_neighbor_scans);
    PrintMetric("level_palette_candidates_total", diagnostics.level_palette_candidates_total);
    PrintMetric("level_diagnostic_vertices", diagnostics.level_diagnostic_vertices);
    PrintMetric("active_dense_rebuilds", diagnostics.active_dense_rebuilds);
    PrintMetric("token_repair_calls", diagnostics.token_repair_calls);
    PrintMetric("token_safe_commits", diagnostics.token_safe_commits);
    PrintMetric("token_active_conflict_rejections", diagnostics.token_active_conflict_rejections);
    PrintMetric("token_lower_equal_conflict_rejections",
                diagnostics.token_lower_equal_conflict_rejections);
    PrintMetric("token_unique_higher_moves", diagnostics.token_unique_higher_moves);
    PrintMetric("token_multi_higher_conflicts", diagnostics.token_multi_higher_conflicts);
    PrintMetric("level_histogram_1", diagnostics.level_histogram_1);
    PrintMetric("level_histogram_2", diagnostics.level_histogram_2);
    PrintMetric("level_histogram_3", diagnostics.level_histogram_3);
    PrintMetric("level_histogram_4", diagnostics.level_histogram_4);
    PrintMetric("level_histogram_5_plus", diagnostics.level_histogram_5_plus);
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
