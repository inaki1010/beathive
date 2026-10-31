#include <catch2/catch_test_macros.hpp>
#include <numeric>
#include <thread>

#include "core/playback/ring_buffer.hpp"

using bh::playback::SpscRing;

TEST_CASE("ring rounds capacity up and wraps correctly") {
  SpscRing r(10);
  CHECK(r.capacity() == 16);
  std::vector<float> in(12), out(12);
  std::iota(in.begin(), in.end(), 0.f);

  CHECK(r.write(in.data(), 12) == 12);
  CHECK(r.write(in.data(), 12) == 4);  // only 4 left
  CHECK(r.read_available() == 16);
  CHECK(r.read(out.data(), 12) == 12);
  CHECK(out == in);

  // Wrap around the end of the buffer.
  CHECK(r.write(in.data(), 12) == 12);
  CHECK(r.read(out.data(), 4) == 4);  // the 4 leftover from before: 0,1,2,3
  CHECK(out[0] == 0.f);
  CHECK(out[3] == 3.f);
  CHECK(r.read(out.data(), 12) == 12);
  CHECK(out == in);
}

TEST_CASE("discard_all empties the ring") {
  SpscRing r(8);
  float v[5] = {1, 2, 3, 4, 5};
  r.write(v, 5);
  CHECK(r.discard_all() == 5);
  CHECK(r.read_available() == 0);
  CHECK(r.write_available() == 8);
}

TEST_CASE("producer/consumer threads deliver every sample in order") {
  constexpr std::size_t kTotal = 400'000;
  SpscRing r(1024);
  std::thread producer([&] {
    std::size_t next = 0;
    while (next < kTotal) {
      float buf[100];
      auto n = std::min<std::size_t>(100, kTotal - next);
      for (std::size_t i = 0; i < n; ++i) buf[i] = static_cast<float>(next + i);
      auto w = r.write(buf, n);  // may take only part; the rest is regenerated next round
      next += w;
      if (w == 0) std::this_thread::yield();
    }
  });

  std::size_t expect = 0;
  bool in_order = true;
  while (expect < kTotal) {
    float buf[64];
    auto n = r.read(buf, 64);
    for (std::size_t i = 0; i < n; ++i) in_order &= buf[i] == static_cast<float>(expect + i);
    expect += n;
    if (n == 0) std::this_thread::yield();
  }
  producer.join();
  CHECK(in_order);
}
