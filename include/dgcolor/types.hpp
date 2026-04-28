#ifndef DGCOLOR_TYPES_HPP_
#define DGCOLOR_TYPES_HPP_

#include <cstdint>
#include <limits>

namespace dgcolor {

using VertexId = std::uint32_t;
using Color = std::uint32_t;
using Level = std::uint32_t;
using Timestamp = std::uint64_t;
using Degree = std::uint32_t;

constexpr Color kUncolored = std::numeric_limits<Color>::max();

}  // namespace dgcolor

#endif  // DGCOLOR_TYPES_HPP_
