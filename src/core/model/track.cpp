#include "core/model/track.hpp"

#include "core/util/text.hpp"

namespace bh {

std::string TagData::artist_display() const { return text::join(artists); }
std::string TagData::genre_display() const { return text::join(genres); }
std::string TagData::album_artist_display() const {
  return album_artists.empty() ? artist_display() : text::join(album_artists);
}

bool TagPatch::empty() const {
  return !title && !artists && !album && !album_artists && !genres && !composers && !comment &&
         !track_no && !track_total && !disc_no && !disc_total && !year;
}

void TagPatch::apply_to(TagData& t) const {
  if (title) t.title = *title;
  if (artists) t.artists = *artists;
  if (album) t.album = *album;
  if (album_artists) t.album_artists = *album_artists;
  if (genres) t.genres = *genres;
  if (composers) t.composers = *composers;
  if (comment) t.comment = *comment;
  if (track_no) t.track_no = track_no;
  if (track_total) t.track_total = track_total;
  if (disc_no) t.disc_no = disc_no;
  if (disc_total) t.disc_total = disc_total;
  if (year) t.year = year;
}

AlbumKey album_key_of(const Track& t) {
  return {t.tags.album_artist_display(), t.tags.album};
}

}  // namespace bh
