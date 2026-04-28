#ifndef DGCOLOR_RNG_HPP_
#define DGCOLOR_RNG_HPP_

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

#include "dgcolor/types.hpp"

namespace dgcolor {

class Rng {
 public:
  explicit Rng(std::uint64_t seed);

  std::uint64_t seed() const;

  VertexId uniform_vertex(VertexId n);
  Color uniform_color(Color num_colors);
  std::size_t uniform_index(std::size_t n);
  std::uint64_t uniform_u64(std::uint64_t lo, std::uint64_t hi);
  bool bernoulli(double p);

 private:
  std::uint64_t seed_{0};
  std::mt19937_64 engine_;
};

std::string format_seed(std::uint64_t seed);

}  // namespace dgcolor

#endif  // DGCOLOR_RNG_HPP_
