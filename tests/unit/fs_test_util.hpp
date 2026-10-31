#pragma once
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

namespace bh::testutil {

inline const std::filesystem::path kDataDir = BH_TEST_DATA_DIR;

// A scratch directory deleted on destruction.
struct TempDir {
  std::filesystem::path path;
  explicit TempDir(const std::string& name) {
    path = std::filesystem::temp_directory_path() / ("bh_test_" + name);
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

inline std::filesystem::path copy_fixture(const std::string& name, const std::filesystem::path& to) {
  std::filesystem::create_directories(to.parent_path());
  std::filesystem::copy_file(kDataDir / name, to, std::filesystem::copy_options::overwrite_existing);
  return to;
}

// Writes a file of `size` bytes whose content depends on `seed` (so different seeds hash differently).
inline std::filesystem::path write_image(const std::filesystem::path& p, char seed, std::size_t size = 4096) {
  std::filesystem::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  out << std::string(size, seed);
  return p;
}

}  // namespace bh::testutil
