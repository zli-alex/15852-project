#include <cassert>
#include <cstring>
#include <type_traits>

#include "dgcolor/batch.hpp"
#include "dgcolor/types.hpp"

int main() {
  using namespace dgcolor;

  static_assert(std::is_same<VertexId, std::uint32_t>::value, "VertexId should be uint32_t");
  static_assert(std::is_same<Color, std::uint32_t>::value, "Color should be uint32_t");
  static_assert(std::is_same<Level, std::uint32_t>::value, "Level should be uint32_t");
  static_assert(std::is_same<Timestamp, std::uint64_t>::value, "Timestamp should be uint64_t");
  static_assert(std::is_same<Degree, std::uint32_t>::value, "Degree should be uint32_t");

  assert(kUncolored > 0);

  const Edge e{9, 2};
  const Edge n = normalize_edge(e);
  assert(n.u == 2);
  assert(n.v == 9);

  assert(!is_loop(n));
  assert(is_loop(7, 7));
  assert(!is_loop(7, 8));

  const EdgeUpdate update{};
  assert(update.kind == UpdateKind::Insert);
  assert(update.time == 0);

  UpdateBatch batch;
  batch.push_back(update);
  assert(batch.size() == 1);

  assert(std::strcmp(update_status_name(UpdateStatus::Ok), "Ok") == 0);
  assert(std::strcmp(update_status_name(UpdateStatus::BatchConflict), "BatchConflict") == 0);

  return 0;
}
