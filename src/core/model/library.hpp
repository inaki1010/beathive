#pragma once
#include <filesystem>

#include "core/model/ids.hpp"

namespace bh {

struct WatchedFolder {
  FolderId id;
  std::filesystem::path path;
  bool recursive = true;
  bool watch_live = true;
};

}  // namespace bh
