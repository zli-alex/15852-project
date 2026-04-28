#include <type_traits>

#include "dgcolor/coloring_engine.hpp"
#include "dgcolor/graph_store.hpp"

int main() {
  static_assert(std::is_abstract<dgcolor::GraphStore>::value, "GraphStore must remain abstract");
  static_assert(std::is_abstract<dgcolor::ColoringEngine>::value, "ColoringEngine must remain abstract");
  static_assert(std::is_base_of<dgcolor::GraphStore, dgcolor::AdjacencyGraphStore>::value,
                "AdjacencyGraphStore should derive from GraphStore");
  return 0;
}
