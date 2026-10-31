#include <array>

#include "core/db/sqlite.hpp"

namespace bh::db {

namespace {

// Append-only: never edit a shipped migration, add a new one.
// Lists (artists, genres, ...) are stored joined with U+001F (unit separator).
constexpr std::array<std::string_view, 3> kMigrations = {R"sql(
CREATE TABLE folder (
  id         INTEGER PRIMARY KEY,
  path       TEXT NOT NULL UNIQUE,
  recursive  INTEGER NOT NULL DEFAULT 1,
  watch_live INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE art (
  id   INTEGER PRIMARY KEY,
  hash TEXT NOT NULL UNIQUE
);

CREATE TABLE track (
  id               INTEGER PRIMARY KEY,
  folder_id        INTEGER REFERENCES folder(id) ON DELETE CASCADE,
  path             TEXT NOT NULL UNIQUE,
  size_bytes       INTEGER NOT NULL DEFAULT 0,
  mtime_unix       INTEGER NOT NULL DEFAULT 0,
  missing          INTEGER NOT NULL DEFAULT 0,

  title            TEXT NOT NULL DEFAULT '',
  artists          TEXT NOT NULL DEFAULT '',
  album            TEXT NOT NULL DEFAULT '',
  album_artists    TEXT NOT NULL DEFAULT '',
  album_artist_key TEXT NOT NULL DEFAULT '',   -- album artist, falling back to artists; "; "-joined
  genres           TEXT NOT NULL DEFAULT '',
  composers        TEXT NOT NULL DEFAULT '',
  comment          TEXT NOT NULL DEFAULT '',
  track_no         INTEGER,
  track_total      INTEGER,
  disc_no          INTEGER,
  disc_total       INTEGER,
  year             INTEGER,
  rg_track_gain    REAL,
  rg_track_peak    REAL,
  rg_album_gain    REAL,
  rg_album_peak    REAL,
  has_embedded_art INTEGER NOT NULL DEFAULT 0,

  duration_ms      INTEGER NOT NULL DEFAULT 0,
  bitrate_kbps     INTEGER NOT NULL DEFAULT 0,
  sample_rate      INTEGER NOT NULL DEFAULT 0,
  channels         INTEGER NOT NULL DEFAULT 0,
  bit_depth        INTEGER NOT NULL DEFAULT 0,
  codec            TEXT NOT NULL DEFAULT '',

  rating           INTEGER NOT NULL DEFAULT 0,
  play_count       INTEGER NOT NULL DEFAULT 0,
  last_played      INTEGER,
  date_added       INTEGER NOT NULL DEFAULT 0,
  art_id           INTEGER REFERENCES art(id) ON DELETE SET NULL
);
CREATE INDEX track_folder     ON track(folder_id);
CREATE INDEX track_album      ON track(album_artist_key COLLATE NOCASE, album COLLATE NOCASE, disc_no, track_no);
CREATE INDEX track_year       ON track(year);
CREATE INDEX track_date_added ON track(date_added);
CREATE INDEX track_last_played ON track(last_played);

-- Per-name rows so the browser can list/filter individual artists and genres.
CREATE TABLE track_artist (
  track_id INTEGER NOT NULL REFERENCES track(id) ON DELETE CASCADE,
  name     TEXT NOT NULL COLLATE NOCASE,
  PRIMARY KEY (track_id, name)
) WITHOUT ROWID;
CREATE INDEX track_artist_name ON track_artist(name);

CREATE TABLE track_genre (
  track_id INTEGER NOT NULL REFERENCES track(id) ON DELETE CASCADE,
  name     TEXT NOT NULL COLLATE NOCASE,
  PRIMARY KEY (track_id, name)
) WITHOUT ROWID;
CREATE INDEX track_genre_name ON track_genre(name);

CREATE VIRTUAL TABLE track_fts USING fts5(
  title, artists, album, genres, path,
  content='track', content_rowid='id'
);
CREATE TRIGGER track_ai AFTER INSERT ON track BEGIN
  INSERT INTO track_fts(rowid, title, artists, album, genres, path)
  VALUES (new.id, new.title, new.artists, new.album, new.genres, new.path);
END;
CREATE TRIGGER track_ad AFTER DELETE ON track BEGIN
  INSERT INTO track_fts(track_fts, rowid, title, artists, album, genres, path)
  VALUES ('delete', old.id, old.title, old.artists, old.album, old.genres, old.path);
END;
CREATE TRIGGER track_au AFTER UPDATE OF title, artists, album, genres, path ON track BEGIN
  INSERT INTO track_fts(track_fts, rowid, title, artists, album, genres, path)
  VALUES ('delete', old.id, old.title, old.artists, old.album, old.genres, old.path);
  INSERT INTO track_fts(rowid, title, artists, album, genres, path)
  VALUES (new.id, new.title, new.artists, new.album, new.genres, new.path);
END;

CREATE TABLE playlist (
  id         INTEGER PRIMARY KEY,
  name       TEXT NOT NULL,
  kind       INTEGER NOT NULL DEFAULT 0,   -- 0 manual, 1 auto
  rules_json TEXT
);
CREATE TABLE playlist_item (
  playlist_id INTEGER NOT NULL REFERENCES playlist(id) ON DELETE CASCADE,
  position    INTEGER NOT NULL,
  track_id    INTEGER NOT NULL REFERENCES track(id) ON DELETE CASCADE,
  PRIMARY KEY (playlist_id, position)
);
)sql", R"sql(
-- Where the image can be re-read from: "embedded:<track path>" or "file:<image path>".
ALTER TABLE art ADD COLUMN source TEXT;
)sql", R"sql(
-- Remembers which image files were already hashed (and what they looked like), so rescans
-- can check a folder's cover with a stat() instead of reading the image again.
CREATE TABLE art_file (
  path   TEXT PRIMARY KEY,
  size   INTEGER NOT NULL,
  mtime  INTEGER NOT NULL,
  art_id INTEGER NOT NULL REFERENCES art(id) ON DELETE CASCADE
);
)sql"};

}  // namespace

int latest_schema_version() { return static_cast<int>(kMigrations.size()); }

Result<void> migrate(Database& db) {
  auto q = db.prepare("PRAGMA user_version");
  BH_TRY(q);
  auto row = q->step();
  BH_TRY(row);
  int current = q->i32(0);
  if (current > latest_schema_version())
    return std::unexpected(Error{ErrorCode::Unsupported, "database was created by a newer version"});

  for (int v = current; v < latest_schema_version(); ++v) {
    auto tx = Transaction::begin(db);
    BH_TRY(tx);
    BH_TRY(db.exec(kMigrations[static_cast<std::size_t>(v)]));
    BH_TRY(db.exec("PRAGMA user_version = " + std::to_string(v + 1)));
    BH_TRY(tx->commit());
  }
  return {};
}

}  // namespace bh::db
