#ifndef DGCOLOR_VALIDATOR_HPP_
#define DGCOLOR_VALIDATOR_HPP_

#include <string>

#include "dgcolor/graph_store.hpp"

namespace dgcolor {

struct ValidationResult {
  bool ok{true};
  std::string message{};
};

ValidationResult validate_graph_invariants(const GraphStore& graph);
ValidationResult validate_exact_coloring(const GraphStore& graph,
                                         const parlay::sequence<Color>& colors);

}  // namespace dgcolor

#endif  // DGCOLOR_VALIDATOR_HPP_
