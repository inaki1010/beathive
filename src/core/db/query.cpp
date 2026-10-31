#include "core/db/query.hpp"

#include <cctype>

namespace bh::db {

namespace {

const char* column_for(RuleField f) {
  switch (f) {
    case RuleField::Title: return "t.title";
    case RuleField::Artist: return "t.artists";
    case RuleField::AlbumArtist: return "t.album_artist_key";
    case RuleField::Album: return "t.album";
    case RuleField::Genre: return "t.genres";
    case RuleField::Composer: return "t.composers";
    case RuleField::Comment: return "t.comment";
    case RuleField::Codec: return "t.codec";
    case RuleField::Year: return "t.year";
    case RuleField::Rating: return "t.rating";
    case RuleField::PlayCount: return "t.play_count";
    case RuleField::BitrateKbps: return "t.bitrate_kbps";
    case RuleField::DurationSeconds: return "(t.duration_ms / 1000)";
    case RuleField::DateAdded: return "t.date_added";
    case RuleField::LastPlayed: return "t.last_played";
  }
  return "NULL";
}

bool is_text(RuleField f) { return f <= RuleField::Codec; }
bool is_date(RuleField f) { return f == RuleField::DateAdded || f == RuleField::LastPlayed; }

// Multi-value columns are U+001F-joined, so "element equals X" is a delimited substring test.
constexpr const char* kSepSql = "char(31)";

std::string compile_text(const Condition& c, SqlFragment& out) {
  const auto* needle = std::get_if<std::string>(&c.value);
  if (!needle) return "0";
  std::string col = column_for(c.field);
  bool empty = needle->empty();

  switch (c.op) {
    case RuleOp::Contains: if (empty) return "1"; break;
    case RuleOp::NotContains: if (empty) return "0"; break;
    case RuleOp::StartsWith: if (empty) return "1"; break;
    case RuleOp::Is: if (empty) return "(" + col + " = '')"; break;
    case RuleOp::IsNot: if (empty) return "(" + col + " <> '')"; break;
    default: return "0";
  }

  out.params.emplace_back(*needle);
  const std::string sep = kSepSql;
  switch (c.op) {
    case RuleOp::Contains: return "(instr(lower(" + col + "), lower(?)) > 0)";
    case RuleOp::NotContains: return "(instr(lower(" + col + "), lower(?)) = 0)";
    case RuleOp::StartsWith:
      return "(instr(" + sep + " || lower(" + col + "), " + sep + " || lower(?)) > 0)";
    case RuleOp::Is:
      return "(instr(" + sep + " || lower(" + col + ") || " + sep + ", " + sep + " || lower(?) || " + sep + ") > 0)";
    default:  // IsNot
      return "(instr(" + sep + " || lower(" + col + ") || " + sep + ", " + sep + " || lower(?) || " + sep + ") = 0)";
  }
}

std::string compile_numeric(const Condition& c, SqlFragment& out) {
  const auto* v = std::get_if<std::int64_t>(&c.value);
  if (!v) return "0";
  const char* op = c.op == RuleOp::Greater ? ">" : c.op == RuleOp::Less ? "<" : c.op == RuleOp::Equal ? "=" : nullptr;
  if (!op) return "0";
  out.params.emplace_back(*v);
  return std::string("(") + column_for(c.field) + " " + op + " ?)";  // NULL never matches
}

std::string compile_date(const Condition& c, SqlFragment& out, Timestamp now) {
  const auto* days = std::get_if<std::int64_t>(&c.value);
  if (!days) return "0";
  auto hi = now.time_since_epoch().count();
  auto lo = (now - std::chrono::days{*days}).time_since_epoch().count();
  out.params.emplace_back(lo);
  out.params.emplace_back(hi);
  std::string col = column_for(c.field);
  std::string in_window = "(" + col + " IS NOT NULL AND " + col + " >= ? AND " + col + " <= ?)";
  if (c.op == RuleOp::InLastDays) return in_window;
  if (c.op == RuleOp::NotInLastDays) return "(NOT " + in_window + ")";
  out.params.pop_back();
  out.params.pop_back();
  return "0";
}

void compile_group(const RuleGroup& g, Timestamp now, SqlFragment& out) {
  if (g.conditions.empty() && g.children.empty()) {
    out.sql += "1";
    return;
  }
  const char* glue = g.match == Match::All ? " AND " : " OR ";
  out.sql += "(";
  bool first = true;
  for (auto& c : g.conditions) {
    if (!first) out.sql += glue;
    first = false;
    out.sql += is_text(c.field) ? compile_text(c, out)
               : is_date(c.field) ? compile_date(c, out, now)
                                  : compile_numeric(c, out);
  }
  for (auto& child : g.children) {
    if (!first) out.sql += glue;
    first = false;
    compile_group(child, now, out);
  }
  out.sql += ")";
}

// ---- filter -> WHERE ----

void add_in_list(SqlFragment& f, const std::string& prefix, const std::vector<std::string>& values) {
  f.sql += prefix;
  for (std::size_t i = 0; i < values.size(); ++i) {
    f.sql += i ? ", ?" : "?";
    f.params.emplace_back(values[i]);
  }
  f.sql += "))";
}

// Names are matched case-insensitively, like the browse lists that group them: files of one
// album are often tagged "Hail to the Thief" on some tracks and "Hail To The Thief" on others.
SqlFragment where_for(const TrackFilter& flt, Timestamp now) {
  SqlFragment f;
  f.sql = flt.include_missing ? "1" : "t.missing = 0";
  if (!flt.genres.empty())
    add_in_list(f, " AND EXISTS (SELECT 1 FROM track_genre g WHERE g.track_id = t.id AND g.name COLLATE NOCASE IN (", flt.genres);
  if (!flt.artists.empty())
    add_in_list(f, " AND EXISTS (SELECT 1 FROM track_artist a WHERE a.track_id = t.id AND a.name COLLATE NOCASE IN (", flt.artists);
  if (flt.album_artist) {
    f.sql += " AND t.album_artist_key = ? COLLATE NOCASE";
    f.params.emplace_back(*flt.album_artist);
  }
  if (flt.album) {
    f.sql += " AND t.album = ? COLLATE NOCASE";
    f.params.emplace_back(*flt.album);
  }
  if (flt.year) {
    f.sql += " AND t.year = ?";
    f.params.emplace_back(static_cast<std::int64_t>(*flt.year));
  }
  if (auto q = fts_query(flt.search); !q.empty()) {
    f.sql += " AND t.id IN (SELECT rowid FROM track_fts WHERE track_fts MATCH ?)";
    f.params.emplace_back(std::move(q));
  }
  if (flt.rules) {
    f.sql += " AND ";
    SqlFragment r = compile_rules(*flt.rules, now);
    f.sql += r.sql;
    for (auto& p : r.params) f.params.push_back(std::move(p));
  }
  return f;
}

void bind_all(Statement& st, const std::vector<SqlParam>& params) {
  int i = 1;
  for (auto& p : params) {
    std::visit([&](const auto& v) { st.bind(i, v); }, p);
    ++i;
  }
}

// Runs `sql` and hands each row to `row`.
template <typename RowFn>
Result<void> for_each_row(Database& db, const std::string& sql, const std::vector<SqlParam>& params, RowFn row) {
  auto st = db.prepare(sql);
  BH_TRY(st);
  bind_all(*st, params);
  for (;;) {
    auto r = st->step();
    BH_TRY(r);
    if (!*r) return {};
    row(*st);
  }
}

std::string order_by(TrackSort s) {
  const char* dir = s.descending ? " DESC" : " ASC";
  std::string primary;
  switch (s.field) {
    case SortField::Title: primary = "t.title COLLATE NOCASE"; break;
    case SortField::Artist: primary = "t.artists COLLATE NOCASE"; break;
    case SortField::Album: primary = "t.album COLLATE NOCASE"; break;
    case SortField::Year: primary = "t.year"; break;
    case SortField::Rating: primary = "t.rating"; break;
    case SortField::PlayCount: primary = "t.play_count"; break;
    case SortField::DateAdded: primary = "t.date_added"; break;
    case SortField::LastPlayed: primary = "t.last_played"; break;
    case SortField::Random: return " ORDER BY RANDOM()";
  }
  return " ORDER BY " + primary + dir +
         ", t.album_artist_key COLLATE NOCASE, t.album COLLATE NOCASE, t.disc_no, t.track_no, t.id";
}

}  // namespace

SqlFragment compile_rules(const RuleGroup& g, Timestamp now) {
  SqlFragment f;
  compile_group(g, now, f);
  return f;
}

std::string fts_query(std::string_view in) {
  std::string out, word;
  auto flush = [&] {
    if (word.empty()) return;
    if (!out.empty()) out += " AND ";
    out += "\"" + word + "\"*";
    word.clear();
  };
  for (unsigned char ch : in) {
    // Keep ASCII alphanumerics and all UTF-8 bytes; everything else separates words
    // (this also neutralises FTS5 syntax characters such as quotes and operators).
    if (std::isalnum(ch) || ch >= 0x80) word += static_cast<char>(ch);
    else flush();
  }
  flush();
  return out;
}

Result<std::vector<TrackId>> Query::tracks(const TrackFilter& flt, TrackSort sort, std::optional<int> limit,
                                           Timestamp now) {
  auto w = where_for(flt, now);
  std::string sql = "SELECT t.id FROM track t WHERE " + w.sql + order_by(sort);
  if (limit) sql += " LIMIT " + std::to_string(*limit);
  std::vector<TrackId> out;
  BH_TRY(for_each_row(db_, sql, w.params, [&](Statement& s) { out.emplace_back(s.i64(0)); }));
  return out;
}

Result<TrackSummary> Query::summary(const TrackFilter& flt, Timestamp now) {
  auto w = where_for(flt, now);
  TrackSummary out;
  BH_TRY(for_each_row(db_, "SELECT COUNT(*), COALESCE(SUM(t.duration_ms), 0) FROM track t WHERE " + w.sql, w.params,
                      [&](Statement& s) {
                        out.track_count = s.i64(0);
                        out.total_duration = std::chrono::milliseconds{s.i64(1)};
                      }));
  return out;
}

Result<std::vector<TrackId>> Query::resolve(const AutoPlaylistSpec& spec, Timestamp now) {
  TrackFilter f;
  f.rules = spec.root;
  return tracks(f, {spec.sort, spec.descending}, spec.limit, now);
}

Result<std::vector<GenreCount>> Query::genres(const TrackFilter& flt) {
  auto w = where_for(flt, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  std::vector<GenreCount> out;
  BH_TRY(for_each_row(db_,
      "SELECT MIN(g.name), COUNT(*) FROM track t JOIN track_genre g ON g.track_id = t.id WHERE " + w.sql +
          " GROUP BY g.name COLLATE NOCASE ORDER BY g.name COLLATE NOCASE",
      w.params, [&](Statement& s) { out.push_back({s.text(0), s.i32(1)}); }));
  return out;
}

Result<std::vector<ArtistSummary>> Query::artists(const TrackFilter& flt) {
  auto w = where_for(flt, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  std::vector<ArtistSummary> out;
  BH_TRY(for_each_row(db_,
      "SELECT MIN(a.name), COUNT(DISTINCT t.album_artist_key || char(31) || t.album), COUNT(*) "
      "FROM track t JOIN track_artist a ON a.track_id = t.id WHERE " + w.sql +
          " GROUP BY a.name COLLATE NOCASE ORDER BY a.name COLLATE NOCASE",
      w.params, [&](Statement& s) { out.push_back({s.text(0), s.i32(1), s.i32(2)}); }));
  return out;
}

Result<std::vector<AlbumSummary>> Query::albums(const TrackFilter& flt) {
  auto w = where_for(flt, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  std::vector<AlbumSummary> out;
  BH_TRY(for_each_row(db_,
      "SELECT t.album_artist_key, t.album, COUNT(*), MIN(t.year), SUM(t.duration_ms), MIN(t.art_id) "
      "FROM track t WHERE " + w.sql +
          " GROUP BY t.album_artist_key COLLATE NOCASE, t.album COLLATE NOCASE "
          "ORDER BY t.album_artist_key COLLATE NOCASE, MIN(t.year), t.album COLLATE NOCASE",
      w.params, [&](Statement& s) {
        AlbumSummary a;
        a.key = {s.text(0), s.text(1)};
        a.track_count = s.i32(2);
        a.year = s.opt_int(3);
        a.total_duration = std::chrono::milliseconds{s.i64(4)};
        a.art = ArtId{s.is_null(5) ? 0 : s.i64(5)};
        out.push_back(std::move(a));
      }));
  return out;
}

Result<std::vector<int>> Query::years(const TrackFilter& flt) {
  auto w = where_for(flt, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
  std::vector<int> out;
  BH_TRY(for_each_row(db_, "SELECT DISTINCT t.year FROM track t WHERE t.year IS NOT NULL AND " + w.sql +
                               " ORDER BY t.year DESC",
                      w.params, [&](Statement& s) { out.push_back(s.i32(0)); }));
  return out;
}

}  // namespace bh::db
