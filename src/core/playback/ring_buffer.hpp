#pragma once
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <vector>

namespace bh::playback {

// Lock-free single-producer / single-consumer ring of floats.
// The producer calls write()/write_available(); the consumer calls read()/read_available()/discard_all().
// Only one thread may act as consumer at a time (the engine serialises its audio callback
// and flush with a flag, see Engine).
class SpscRing {
 public:
  explicit SpscRing(std::size_t min_capacity)
      : buf_(std::bit_ceil(std::max<std::size_t>(min_capacity, 2))), mask_(buf_.size() - 1) {}

  std::size_t capacity() const { return buf_.size(); }

  // --- producer ---
  std::size_t write_available() const {
    return buf_.size() - (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire));
  }
  std::size_t write(const float* src, std::size_t n) {
    auto head = head_.load(std::memory_order_relaxed);
    n = std::min(n, buf_.size() - (head - tail_.load(std::memory_order_acquire)));
    copy_in(head, src, n);
    head_.store(head + n, std::memory_order_release);
    return n;
  }

  // --- consumer ---
  std::size_t read_available() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed);
  }
  std::size_t read(float* dst, std::size_t n) {
    auto tail = tail_.load(std::memory_order_relaxed);
    n = std::min(n, head_.load(std::memory_order_acquire) - tail);
    copy_out(tail, dst, n);
    tail_.store(tail + n, std::memory_order_release);
    return n;
  }
  // Drops everything currently buffered; returns how many floats were dropped.
  std::size_t discard_all() {
    auto tail = tail_.load(std::memory_order_relaxed);
    auto head = head_.load(std::memory_order_acquire);
    tail_.store(head, std::memory_order_release);
    return head - tail;
  }

 private:
  void copy_in(std::size_t pos, const float* src, std::size_t n) {
    auto i = pos & mask_;
    auto first = std::min(n, buf_.size() - i);
    std::memcpy(&buf_[i], src, first * sizeof(float));
    std::memcpy(&buf_[0], src + first, (n - first) * sizeof(float));
  }
  void copy_out(std::size_t pos, float* dst, std::size_t n) const {
    auto i = pos & mask_;
    auto first = std::min(n, buf_.size() - i);
    std::memcpy(dst, &buf_[i], first * sizeof(float));
    std::memcpy(dst + first, &buf_[0], (n - first) * sizeof(float));
  }

  std::vector<float> buf_;
  std::size_t mask_;
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace bh::playback
