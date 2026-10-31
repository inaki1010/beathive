#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace bh {

// Paths are stored in the database as UTF-8 with forward slashes on every OS,
// so a library file is portable between Windows and Linux installs of the same layout.
inline std::string to_utf8(const std::filesystem::path& p) {
  auto s = p.generic_u8string();
  return {reinterpret_cast<const char*>(s.data()), s.size()};
}

inline std::filesystem::path path_from_utf8(std::string_view s) {
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

}  // namespace bh
