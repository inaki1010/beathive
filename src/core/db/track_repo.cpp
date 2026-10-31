#include "core/db/track_repo.hpp"

#include <algorithm>
#include <array>

#include "core/util/paths.hpp"

namespace bh::db {

namespace {

constexpr char kSep = '\x1f';

std::string encode_list(const std::vector<std::string>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) out += kSep;
    out += v[i];
  }
  return out;
}

std::vector<std::string> decode_list(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size() && !s.empty()) {
    auto end = s.find(kSep, start);
    out.push_back(s.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return out;
}

// Column groups. SQL for insert/update is generated from these so the lists can't drift apart.
constexpr std::array kFileCols = {"folder_id", "path", "size_bytes", "mtime_unix", "missing", "duration_ms",
                                  "bitrate_kbps", "sample_rate", "channels", "bit_depth", "codec", "art_id"};
constexpr std::array kTagCols = {"title", "artists", "album", "album_artists", "album_artist_key", "genres",
                                 "composers", "comment", "track_no", "track_total", "disc_no", "disc_total",
                                 "year", "rg_track_gain", "rg_track_peak", "rg_album_gain", "rg_album_peak",
                                 "has_embedded_art"};

template <std::size_t N>
std::string joined(const std::array<const char*, N>& cols, auto&& fmt) {
  std::string out;
  for (auto* c : cols) {
    if (!out.empty()) out += ", ";
    out += fmt(std::string_view(c));
  }
  return out;
}

const std::string& insert_sql() {
  static const std::string s =
      "INSERT INTO track (" + joined(kFileCols, [](auto c) { return std::string(c); }) + ", " +
      joined(kTagCols, [](auto c) { return std::string(c); }) +
      ", rating, play_count, last_played, date_added) VALUES (" +
      joined(kFileCols, [](auto c) { return ":" + std::string(c); }) + ", " +
      joined(kTagCols, [](auto c) { return ":" + std::string(c); }) +
      ", :rating, :play_count, :last_played, :date_added) "
      "ON CONFLICT(path) DO UPDATE SET " +
      joined(kFileCols, [](auto c) { return std::string(c) + " = excluded." + std::string(c); }) + ", " +
      joined(kTagCols, [](auto c) { return std::string(c) + " = excluded." + std::string(c); });
  return s;
}

const std::string& update_tags_sql() {
  static const std::string s =
      "UPDATE track SET " + joined(kTagCols, [](auto c) { return std::string(c) + " = :" + std::string(c); }) +
      " WHERE id = :id";
  return s;
}

void bind_tags(Statement& st, const TagData& t) {
  st.bind(":title", std::string_view(t.title));
  st.bind(":artists", std::string_view(encode_list(t.artists)));
  st.bind(":album", std::string_view(t.album));
  st.bind(":album_artists", std::string_view(encode_list(t.album_artists)));
  st.bind(":album_artist_key", std::string_view(t.album_artist_display()));
  st.bind(":genres", std::string_view(encode_list(t.genres)));
  st.bind(":composers", std::string_view(encode_list(t.composers)));
  st.bind(":comment", std::string_view(t.comment));
  st.bind(":track_no", t.track_no);
  st.bind(":track_total", t.track_total);
  st.bind(":disc_no", t.disc_no);
  st.bind(":disc_total", t.disc_total);
  st.bind(":year", t.year);
  st.bind(":rg_track_gain", t.replay_gain.track_gain_db);
  st.bind(":rg_track_peak", t.replay_gain.track_peak);
  st.bind(":rg_album_gain", t.replay_gain.album_gain_db);
  st.bind(":rg_album_peak", t.replay_gain.album_peak);
  st.bind(":has_embedded_art", t.has_embedded_art ? 1 : 0);
}

// Column order here must match kSelect.
constexpr const char* kSelect =
    "SELECT id, folder_id, path, size_bytes, mtime_unix, missing, "                          // 0-5
    "title, artists, album, album_artists, genres, composers, comment, "                     // 6-12
    "track_no, track_total, disc_no, disc_total, year, "                                     // 13-17
    "rg_track_gain, rg_track_peak, rg_album_gain, rg_album_peak, has_embedded_art, "         // 18-22
    "duration_ms, bitrate_kbps, sample_rate, channels, bit_depth, codec, "                   // 23-28
    "rating, play_count, last_played, date_added, art_id FROM track ";                       // 29-33

Track read_track(const Statement& s) {
  Track t;
  t.id = TrackId{s.i64(0)};
  t.file.folder = FolderId{s.is_null(1) ? 0 : s.i64(1)};
  t.file.path = path_from_utf8(s.text(2));
  t.file.size_bytes = s.i64(3);
  t.file.mtime_unix = s.i64(4);
  t.file.missing = s.i64(5) != 0;
  t.tags.title = s.text(6);
  t.tags.artists = decode_list(s.text(7));
  t.tags.album = s.text(8);
  t.tags.album_artists = decode_list(s.text(9));
  t.tags.genres = decode_list(s.text(10));
  t.tags.composers = decode_list(s.text(11));
  t.tags.comment = s.text(12);
  t.tags.track_no = s.opt_int(13);
  t.tags.track_total = s.opt_int(14);
  t.tags.disc_no = s.opt_int(15);
  t.tags.disc_total = s.opt_int(16);
  t.tags.year = s.opt_int(17);
  t.tags.replay_gain = {s.opt_f64(18), s.opt_f64(19), s.opt_f64(20), s.opt_f64(21)};
  t.tags.has_embedded_art = s.i64(22) != 0;
  t.audio.duration = std::chrono::milliseconds{s.i64(23)};
  t.audio.bitrate_kbps = s.i32(24);
  t.audio.sample_rate = s.i32(25);
  t.audio.channels = s.i32(26);
  t.audio.bit_depth = s.i32(27);
  t.audio.codec = s.text(28);
  t.lib.rating = Rating::clamped(s.i32(29));
  t.lib.play_count = s.i32(30);
  if (!s.is_null(31)) t.lib.last_played = Timestamp{std::chrono::seconds{s.i64(31)}};
  t.lib.date_added = Timestamp{std::chrono::seconds{s.i64(32)}};
  t.art = ArtId{s.is_null(33) ? 0 : s.i64(33)};
  return t;
}

std::optional<std::int64_t> nullable(std::int64_t v) { return v ? std::optional(v) : std::nullopt; }

}  // namespace

Result<TrackId> TrackRepo::upsert(Track& t) {
  auto st = db_.prepare(insert_sql());
  BH_TRY(st);
  auto path = to_utf8(t.file.path);
  st->bind(":folder_id", nullable(t.file.folder.value));
  st->bind(":path", std::string_view(path));
  st->bind(":size_bytes", t.file.size_bytes);
  st->bind(":mtime_unix", t.file.mtime_unix);
  st->bind(":missing", t.file.missing ? 1 : 0);
  st->bind(":duration_ms", static_cast<std::int64_t>(t.audio.duration.count()));
  st->bind(":bitrate_kbps", t.audio.bitrate_kbps);
  st->bind(":sample_rate", t.audio.sample_rate);
  st->bind(":channels", t.audio.channels);
  st->bind(":bit_depth", t.audio.bit_depth);
  st->bind(":codec", std::string_view(t.audio.codec));
  st->bind(":art_id", nullable(t.art.value));
  bind_tags(*st, t.tags);
  st->bind(":rating", static_cast<int>(t.lib.rating.stars));
  st->bind(":play_count", t.lib.play_count);
  st->bind(":last_played", t.lib.last_played
                               ? std::optional<std::int64_t>(t.lib.last_played->time_since_epoch().count())
                               : std::nullopt);
  st->bind(":date_added", static_cast<std::int64_t>(t.lib.date_added.time_since_epoch().count()));
  BH_TRY(st->run());

  auto idq = db_.prepare("SELECT id FROM track WHERE path = ?");
  BH_TRY(idq);
  idq->bind(1, std::string_view(path));
  auto row = idq->step();
  BH_TRY(row);
  t.id = TrackId{idq->i64(0)};
  BH_TRY(refresh_index(t.id, t.tags));
  return t.id;
}

Result<void> TrackRepo::refresh_index(TrackId id, const TagData& tags) {
  struct Table { const char* name; const std::vector<std::string>* values; };
  for (auto [table, values] : {Table{"track_artist", &tags.artists}, Table{"track_genre", &tags.genres}}) {
    auto del = db_.prepare(std::string("DELETE FROM ") + table + " WHERE track_id = ?");
    BH_TRY(del);
    del->bind(1, id.value);
    BH_TRY(del->run());
    auto ins = db_.prepare(std::string("INSERT OR IGNORE INTO ") + table + " (track_id, name) VALUES (?, ?)");
    BH_TRY(ins);
    for (auto& name : *values) {
      if (name.empty()) continue;
      ins->bind(1, id.value).bind(2, std::string_view(name));
      BH_TRY(ins->run());
      ins->reset();
    }
  }
  return {};
}

Result<std::optional<Track>> TrackRepo::get(TrackId id) {
  auto st = db_.prepare(std::string(kSelect) + "WHERE id = ?");
  BH_TRY(st);
  st->bind(1, id.value);
  auto row = st->step();
  BH_TRY(row);
  if (!*row) return std::optional<Track>{};
  return std::optional<Track>{read_track(*st)};
}

Result<std::optional<Track>> TrackRepo::find_by_path(const std::filesystem::path& p) {
  auto st = db_.prepare(std::string(kSelect) + "WHERE path = ?");
  BH_TRY(st);
  auto s = to_utf8(p);
  st->bind(1, std::string_view(s));
  auto row = st->step();
  BH_TRY(row);
  if (!*row) return std::optional<Track>{};
  return std::optional<Track>{read_track(*st)};
}

Result<std::vector<Track>> TrackRepo::get_many(const std::vector<TrackId>& ids) {
  std::vector<Track> out;
  out.reserve(ids.size());
  auto st = db_.prepare(std::string(kSelect) + "WHERE id = ?");
  BH_TRY(st);
  for (auto id : ids) {
    st->bind(1, id.value);
    auto row = st->step();
    BH_TRY(row);
    if (*row) out.push_back(read_track(*st));
    st->reset();
  }
  return out;
}

Result<void> TrackRepo::update_tags(TrackId id, const TagData& tags) {
  auto st = db_.prepare(update_tags_sql());
  BH_TRY(st);
  st->bind(":id", id.value);
  bind_tags(*st, tags);
  BH_TRY(st->run());
  if (db_.changes() == 0) return std::unexpected(Error{ErrorCode::NotFound, "no such track"});
  return refresh_index(id, tags);
}

Result<void> TrackRepo::set_rating(TrackId id, Rating r) {
  auto st = db_.prepare("UPDATE track SET rating = ? WHERE id = ?");
  BH_TRY(st);
  st->bind(1, static_cast<int>(r.stars)).bind(2, id.value);
  return st->run();
}

Result<void> TrackRepo::record_play(TrackId id, Timestamp when) {
  auto st = db_.prepare("UPDATE track SET play_count = play_count + 1, last_played = ? WHERE id = ?");
  BH_TRY(st);
  st->bind(1, static_cast<std::int64_t>(when.time_since_epoch().count())).bind(2, id.value);
  return st->run();
}

Result<void> TrackRepo::set_missing(TrackId id, bool missing) {
  auto st = db_.prepare("UPDATE track SET missing = ? WHERE id = ?");
  BH_TRY(st);
  st->bind(1, missing ? 1 : 0).bind(2, id.value);
  return st->run();
}

Result<void> TrackRepo::set_art(TrackId id, ArtId art) {
  auto st = db_.prepare("UPDATE track SET art_id = ? WHERE id = ?");
  BH_TRY(st);
  st->bind(1, nullable(art.value)).bind(2, id.value);
  return st->run();
}

Result<void> TrackRepo::remove(TrackId id) {
  auto st = db_.prepare("DELETE FROM track WHERE id = ?");
  BH_TRY(st);
  st->bind(1, id.value);
  return st->run();
}

Result<int> TrackRepo::purge_missing() {
  BH_TRY(db_.exec("DELETE FROM track WHERE missing = 1"));
  return db_.changes();
}

Result<std::vector<FileStamp>> TrackRepo::stamps(FolderId folder) {
  auto st = db_.prepare("SELECT id, path, size_bytes, mtime_unix, missing, art_id, has_embedded_art FROM track WHERE folder_id = ?");
  BH_TRY(st);
  st->bind(1, folder.value);
  std::vector<FileStamp> out;
  for (;;) {
    auto row = st->step();
    BH_TRY(row);
    if (!*row) break;
    out.push_back({TrackId{st->i64(0)}, path_from_utf8(st->text(1)), st->i64(2), st->i64(3), st->i64(4) != 0,
                   ArtId{st->is_null(5) ? 0 : st->i64(5)}, st->i64(6) != 0});
  }
  return out;
}

Result<std::int64_t> TrackRepo::count() {
  auto st = db_.prepare("SELECT COUNT(*) FROM track");
  BH_TRY(st);
  BH_TRY(st->step());
  return st->i64(0);
}

}  // namespace bh::db
