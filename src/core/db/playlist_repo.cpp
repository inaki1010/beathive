#include "core/db/playlist_repo.hpp"

#include "core/db/rules_json.hpp"

namespace bh::db {

Result<PlaylistId> PlaylistRepo::create_manual(const std::string& name) {
  auto st = db_.prepare("INSERT INTO playlist (name, kind) VALUES (?, 0)");
  BH_TRY(st);
  st->bind(1, std::string_view(name));
  BH_TRY(st->run());
  return PlaylistId{db_.last_insert_rowid()};
}

Result<PlaylistId> PlaylistRepo::create_auto(const std::string& name, const AutoPlaylistSpec& spec) {
  auto st = db_.prepare("INSERT INTO playlist (name, kind, rules_json) VALUES (?, 1, ?)");
  BH_TRY(st);
  st->bind(1, std::string_view(name)).bind(2, std::string_view(rules_to_json(spec)));
  BH_TRY(st->run());
  return PlaylistId{db_.last_insert_rowid()};
}

Result<std::optional<Playlist>> PlaylistRepo::get(PlaylistId id) {
  auto st = db_.prepare("SELECT name, kind, rules_json FROM playlist WHERE id = ?");
  BH_TRY(st);
  st->bind(1, id.value);
  auto row = st->step();
  BH_TRY(row);
  if (!*row) return std::optional<Playlist>{};

  Playlist p;
  p.id = id;
  p.name = st->text(0);
  p.kind = st->i64(1) == 1 ? PlaylistKind::Auto : PlaylistKind::Manual;
  if (p.kind == PlaylistKind::Auto) {
    auto spec = rules_from_json(st->text(2));
    BH_TRY(spec);
    p.rules = std::move(*spec);
  } else {
    auto items = db_.prepare("SELECT track_id FROM playlist_item WHERE playlist_id = ? ORDER BY position");
    BH_TRY(items);
    items->bind(1, id.value);
    for (;;) {
      auto r = items->step();
      BH_TRY(r);
      if (!*r) break;
      p.items.emplace_back(items->i64(0));
    }
  }
  return std::optional<Playlist>{std::move(p)};
}

Result<std::vector<Playlist>> PlaylistRepo::list() {
  auto st = db_.prepare("SELECT id, name, kind FROM playlist ORDER BY name COLLATE NOCASE");
  BH_TRY(st);
  std::vector<Playlist> out;
  for (;;) {
    auto r = st->step();
    BH_TRY(r);
    if (!*r) break;
    Playlist p;
    p.id = PlaylistId{st->i64(0)};
    p.name = st->text(1);
    p.kind = st->i64(2) == 1 ? PlaylistKind::Auto : PlaylistKind::Manual;
    out.push_back(std::move(p));
  }
  return out;
}

Result<void> PlaylistRepo::rename(PlaylistId id, const std::string& name) {
  auto st = db_.prepare("UPDATE playlist SET name = ? WHERE id = ?");
  BH_TRY(st);
  st->bind(1, std::string_view(name)).bind(2, id.value);
  return st->run();
}

Result<void> PlaylistRepo::set_rules(PlaylistId id, const AutoPlaylistSpec& spec) {
  auto st = db_.prepare("UPDATE playlist SET rules_json = ? WHERE id = ? AND kind = 1");
  BH_TRY(st);
  st->bind(1, std::string_view(rules_to_json(spec))).bind(2, id.value);
  return st->run();
}

Result<void> PlaylistRepo::set_items(PlaylistId id, const std::vector<TrackId>& items) {
  auto tx = Transaction::begin(db_);
  BH_TRY(tx);
  auto del = db_.prepare("DELETE FROM playlist_item WHERE playlist_id = ?");
  BH_TRY(del);
  del->bind(1, id.value);
  BH_TRY(del->run());
  BH_TRY(append_items(id, items));
  return tx->commit();
}

Result<void> PlaylistRepo::append_items(PlaylistId id, const std::vector<TrackId>& items) {
  auto q = db_.prepare("SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_item WHERE playlist_id = ?");
  BH_TRY(q);
  q->bind(1, id.value);
  BH_TRY(q->step());
  auto pos = q->i64(0);
  BH_TRY(q->run());

  auto ins = db_.prepare("INSERT INTO playlist_item (playlist_id, position, track_id) VALUES (?, ?, ?)");
  BH_TRY(ins);
  for (auto t : items) {
    ins->bind(1, id.value).bind(2, pos++).bind(3, t.value);
    BH_TRY(ins->run());
    ins->reset();
  }
  return {};
}

Result<void> PlaylistRepo::remove(PlaylistId id) {
  auto st = db_.prepare("DELETE FROM playlist WHERE id = ?");
  BH_TRY(st);
  st->bind(1, id.value);
  return st->run();
}

}  // namespace bh::db
