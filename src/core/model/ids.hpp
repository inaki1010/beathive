#pragma once
#include <compare>
#include <cstdint>
#include <functional>

namespace bh {

// Strongly-typed database id. 0 is "none"; SQLite rowids start at 1.
template <typename Tag>
struct Id {
  std::int64_t value = 0;

  constexpr Id() = default;
  constexpr explicit Id(std::int64_t v) : value(v) {}
  constexpr explicit operator bool() const { return value != 0; }
  friend constexpr auto operator<=>(Id, Id) = default;
};

struct TrackTag {};
struct FolderTag {};
struct ArtTag {};
struct PlaylistTag {};

using TrackId = Id<TrackTag>;
using FolderId = Id<FolderTag>;
using ArtId = Id<ArtTag>;
using PlaylistId = Id<PlaylistTag>;

}  // namespace bh

template <typename Tag>
struct std::hash<bh::Id<Tag>> {
  std::size_t operator()(bh::Id<Tag> id) const noexcept {
    return std::hash<std::int64_t>{}(id.value);
  }
};
