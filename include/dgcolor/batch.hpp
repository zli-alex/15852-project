#ifndef DGCOLOR_BATCH_HPP_
#define DGCOLOR_BATCH_HPP_

#include <cstddef>
#include <string>
#include <utility>

#include "dgcolor/types.hpp"

#if !defined(DGCOLOR_HAS_PARLAYLIB)
#error "DGCOLOR_HAS_PARLAYLIB is not defined. Link targets with dgcolor_parlay."
#endif

#if DGCOLOR_HAS_PARLAYLIB
#include <parlay/sequence.h>
#else
#error "ParlayLib headers were not found. Configure PARLAYLIB_DIR or external/parlaylib."
#endif

namespace dgcolor {

struct Edge {
  VertexId u{0};
  VertexId v{0};
};

enum class UpdateKind : std::uint8_t {
  Insert = 0,
  Delete = 1,
};

struct EdgeUpdate {
  UpdateKind kind{UpdateKind::Insert};
  VertexId u{0};
  VertexId v{0};
  Timestamp time{0};  // Metadata timestamp; not wall-clock time.
};

using UpdateBatch = parlay::sequence<EdgeUpdate>;

enum class UpdateStatus : std::uint8_t {
  Ok = 0,
  InvalidVertex = 1,
  Loop = 2,
  DuplicateEdge = 3,
  MissingEdge = 4,
  DegreeCapExceeded = 5,
  BatchConflict = 6,
};

struct UpdateResult {
  UpdateStatus status{UpdateStatus::Ok};
  std::string message{};
};

struct UpdateStats {
  bool applied{false};
  std::size_t edges_changed{0};
  std::size_t vertices_touched{0};
  double seconds{0.0};
};

struct BatchStats {
  bool applied{false};
  std::size_t updates{0};
  std::size_t edges_changed{0};
  std::size_t vertices_touched{0};
  double seconds{0.0};
};

inline Edge normalize_edge(VertexId u, VertexId v) {
  if (u <= v) {
    return Edge{u, v};
  }
  return Edge{v, u};
}

inline Edge normalize_edge(const Edge& edge) {
  return normalize_edge(edge.u, edge.v);
}

inline bool is_loop(const Edge& edge) {
  return edge.u == edge.v;
}

inline bool is_loop(VertexId u, VertexId v) {
  return u == v;
}

inline const char* update_status_name(UpdateStatus status) {
  switch (status) {
    case UpdateStatus::Ok:
      return "Ok";
    case UpdateStatus::InvalidVertex:
      return "InvalidVertex";
    case UpdateStatus::Loop:
      return "Loop";
    case UpdateStatus::DuplicateEdge:
      return "DuplicateEdge";
    case UpdateStatus::MissingEdge:
      return "MissingEdge";
    case UpdateStatus::DegreeCapExceeded:
      return "DegreeCapExceeded";
    case UpdateStatus::BatchConflict:
      return "BatchConflict";
  }
  return "Unknown";
}

}  // namespace dgcolor

#endif  // DGCOLOR_BATCH_HPP_
