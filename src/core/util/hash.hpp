#pragma once
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace bh {

// FNV-1a 64-bit. Not cryptographic; used to de-duplicate identical cover images.
inline std::string content_hash(std::span<const unsigned char> bytes) {
  std::uint64_t h = 1469598103934665603ull;
  for (auto b : bytes) {
    h ^= b;
    h *= 1099511628211ull;
  }
  char buf[40];
  std::snprintf(buf, sizeof buf, "%016llx-%zx", static_cast<unsigned long long>(h), bytes.size());
  return buf;
}

}  // namespace bh
