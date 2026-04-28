#ifndef DGCOLOR_COLORING_ENGINE_HPP_
#define DGCOLOR_COLORING_ENGINE_HPP_

#include <string>

#include "dgcolor/batch.hpp"
#include "dgcolor/graph_store.hpp"
#include "dgcolor/types.hpp"

namespace dgcolor {

class ColoringEngine {
 public:
  virtual ~ColoringEngine() = default;

  virtual std::string name() const = 0;
  virtual const GraphStore& graph() const = 0;
  virtual Color color_of(VertexId v) const = 0;
  virtual parlay::sequence<Color> colors() const = 0;

  // For exact algorithms, implementations must be properly colored
  // after each completed update or batch.
  virtual void initialize_coloring() = 0;
  virtual UpdateStats apply_update(const EdgeUpdate& update) = 0;
  virtual BatchStats apply_batch(const UpdateBatch& batch) = 0;
};

}  // namespace dgcolor

#endif  // DGCOLOR_COLORING_ENGINE_HPP_
