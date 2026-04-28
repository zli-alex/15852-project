#include "dgcolor/rng.hpp"

#include <cassert>
#include <stdexcept>

namespace dgcolor {

Rng::Rng(std::uint64_t seed) : seed_(seed), engine_(seed) {}

std::uint64_t Rng::seed() const {
  return seed_;
}

VertexId Rng::uniform_vertex(VertexId n) {
  assert(n > 0);
  if (n == 0) {
    throw std::invalid_argument("uniform_vertex requires n > 0");
  }
  std::uniform_int_distribution<VertexId> dist(0, static_cast<VertexId>(n - 1));
  return dist(engine_);
}

Color Rng::uniform_color(Color num_colors) {
  assert(num_colors > 0);
  if (num_colors == 0) {
    throw std::invalid_argument("uniform_color requires num_colors > 0");
  }
  std::uniform_int_distribution<Color> dist(0, static_cast<Color>(num_colors - 1));
  return dist(engine_);
}

std::size_t Rng::uniform_index(std::size_t n) {
  assert(n > 0);
  if (n == 0) {
    throw std::invalid_argument("uniform_index requires n > 0");
  }
  std::uniform_int_distribution<std::size_t> dist(0, n - 1);
  return dist(engine_);
}

std::uint64_t Rng::uniform_u64(std::uint64_t lo, std::uint64_t hi) {
  assert(lo <= hi);
  if (lo > hi) {
    throw std::invalid_argument("uniform_u64 requires lo <= hi");
  }
  std::uniform_int_distribution<std::uint64_t> dist(lo, hi);
  return dist(engine_);
}

bool Rng::bernoulli(double p) {
  assert(p >= 0.0 && p <= 1.0);
  if (p < 0.0 || p > 1.0) {
    throw std::invalid_argument("bernoulli requires 0.0 <= p <= 1.0");
  }
  std::bernoulli_distribution dist(p);
  return dist(engine_);
}

std::string format_seed(std::uint64_t seed) {
  return "seed=" + std::to_string(seed);
}

}  // namespace dgcolor
