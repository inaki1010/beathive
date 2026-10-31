#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/model/error.hpp"
#include "core/model/track.hpp"

namespace bh::tags {

struct CoverArt {
  std::vector<unsigned char> data;
  std::string mime_type;
};

struct TagFile {
  TagData tags;
  AudioProperties audio;
  std::optional<CoverArt> cover;  // only filled when ReadOptions::want_cover
};

struct ReadOptions {
  bool want_cover = false;
};

// True for file extensions the library imports (case-insensitive, with leading dot).
bool is_supported_extension(const std::filesystem::path& p);

// Safe to call concurrently on different files.
Result<TagFile> read_tags(const std::filesystem::path& file, ReadOptions opt = {});

// Writes the editable fields of `tags` into the file, leaving unrelated
// properties and embedded pictures untouched.
Result<void> write_tags(const std::filesystem::path& file, const TagData& tags);

// Best embedded picture (front cover preferred), if any.
Result<std::optional<CoverArt>> read_cover(const std::filesystem::path& file);

}  // namespace bh::tags
