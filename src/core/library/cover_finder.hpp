#pragma once
#include <filesystem>
#include <optional>

namespace bh::library {

// Finds the album cover image that belongs to the music files in `dir`.
//
// Handles the naming found in real collections (Soulseek rips, Windows Media Player,
// scene releases): "cover.jpg", "Folder.JPG", "Front.png", "Cover Art.jpg",
// "AlbumArt_{GUID}_Large.jpg", "<album name>.jpg", or just a single unlabelled image.
// Images that are clearly something else (back, inlay, booklet, cd label, ...) and
// thumbnails are avoided. When `dir` is a disc folder ("CD1", "Disc 2 - Name") with no
// image of its own, the parent folder is searched.
//
// Looks only at directory entries (never reads file contents).
std::optional<std::filesystem::path> find_cover_image(const std::filesystem::path& dir);

}  // namespace bh::library
