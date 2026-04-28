#include <cassert>
#include <string>

#include "dgcolor/rng.hpp"

namespace {

void TestSameSeedProducesSameSequence() {
  dgcolor::Rng a(12345);
  dgcolor::Rng b(12345);

  for (int i = 0; i < 64; ++i) {
    assert(a.uniform_u64(0, 1'000'000) == b.uniform_u64(0, 1'000'000));
  }
}

void TestDifferentSeedsUsuallyDiffer() {
  dgcolor::Rng a(1);
  dgcolor::Rng b(2);

  bool found_difference = false;
  for (int i = 0; i < 32; ++i) {
    if (a.uniform_u64(0, 1'000'000) != b.uniform_u64(0, 1'000'000)) {
      found_difference = true;
      break;
    }
  }
  assert(found_difference);
}

void TestUniformVertexRange() {
  dgcolor::Rng rng(7);
  const dgcolor::VertexId n = 17;
  for (int i = 0; i < 256; ++i) {
    const dgcolor::VertexId v = rng.uniform_vertex(n);
    assert(v < n);
  }
}

void TestUniformColorRange() {
  dgcolor::Rng rng(9);
  const dgcolor::Color num_colors = 5;
  for (int i = 0; i < 256; ++i) {
    const dgcolor::Color c = rng.uniform_color(num_colors);
    assert(c < num_colors);
  }
}

void TestUniformIndexRange() {
  dgcolor::Rng rng(11);
  const std::size_t n = 21;
  for (int i = 0; i < 256; ++i) {
    const std::size_t idx = rng.uniform_index(n);
    assert(idx < n);
  }
}

void TestBernoulliEdgeCases() {
  dgcolor::Rng rng(13);
  for (int i = 0; i < 64; ++i) {
    assert(!rng.bernoulli(0.0));
    assert(rng.bernoulli(1.0));
  }
}

void TestSeedLoggingHelper() {
  constexpr std::uint64_t kSeed = 424242;
  dgcolor::Rng rng(kSeed);
  assert(rng.seed() == kSeed);

  const std::string text = dgcolor::format_seed(rng.seed());
  assert(text.find("424242") != std::string::npos);
}

}  // namespace

int main() {
  TestSameSeedProducesSameSequence();
  TestDifferentSeedsUsuallyDiffer();
  TestUniformVertexRange();
  TestUniformColorRange();
  TestUniformIndexRange();
  TestBernoulliEdgeCases();
  TestSeedLoggingHelper();
  return 0;
}
