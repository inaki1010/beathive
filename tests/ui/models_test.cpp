#include <QApplication>
#include <QSignalSpy>
#include <catch2/catch_test_macros.hpp>

#include "db_test_util.hpp"
#include "ui/filter_browser.hpp"
#include "ui/queue_model.hpp"
#include "ui/track_table_model.hpp"

using namespace bh;
using namespace bh::ui;
using namespace bh::testutil;

namespace {

std::vector<TrackId> add_tracks(db::Database& d) {
  db::TrackRepo repo(d);
  std::vector<TrackId> ids;
  auto add = [&](Track t) {
    REQUIRE(repo.upsert(t));
    ids.push_back(t.id);
  };
  auto a = make_track("/m/dp/1.flac", "One More Time", {"Daft Punk"}, "Discovery", {"Electronic", "House"}, 2001);
  a.tags.track_no = 1;
  a.lib.rating = Rating{4};
  a.lib.play_count = 7;
  auto b = make_track("/m/dp/2.flac", "Aerodynamic", {"Daft Punk"}, "Discovery", {"Electronic"}, 2001);
  auto c = make_track("/m/mj/1.flac", "Thriller", {"Michael Jackson"}, "Thriller", {"Pop"}, 1982);
  auto untitled = make_track("/m/x/untagged song.mp3", "", {}, "");
  add(a); add(b); add(c); add(untitled);
  return ids;
}

QString cell(const QAbstractItemModel& m, int row, int col, int role = Qt::DisplayRole) {
  return m.data(m.index(row, col), role).toString();
}

}  // namespace

TEST_CASE("track table model shows formatted rows loaded lazily") {
  auto d = open_db();
  auto ids = add_tracks(d);
  TrackCache cache(d);
  TrackTableModel m(cache);
  QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
  m.set_ids(ids);

  CHECK(reset.count() == 1);
  CHECK(m.rowCount() == 4);
  CHECK(m.columnCount() == TrackTableModel::ColumnCount);
  CHECK(cell(m, 0, TrackTableModel::Title) == "One More Time");
  CHECK(cell(m, 0, TrackTableModel::Artist) == "Daft Punk");
  CHECK(cell(m, 0, TrackTableModel::Genre) == "Electronic; House");
  CHECK(cell(m, 0, TrackTableModel::Year) == "2001");
  CHECK(cell(m, 0, TrackTableModel::Duration) == "3:00");
  CHECK(cell(m, 0, TrackTableModel::Rating) == QString(4, QChar(0x2605)));
  CHECK(cell(m, 0, TrackTableModel::PlayCount) == "7");
  CHECK(cell(m, 0, TrackTableModel::Number) == "1");
  CHECK(cell(m, 0, TrackTableModel::Codec) == "FLAC");
  CHECK(cell(m, 1, TrackTableModel::PlayCount).isEmpty());  // zero plays shown blank
  // A track without a title falls back to its file name.
  CHECK(cell(m, 3, TrackTableModel::Title) == "untagged song");
  CHECK(cell(m, 0, TrackTableModel::Title, Qt::UserRole).toLongLong() == ids[0].value);
}

TEST_CASE("track table model marks the playing row and survives deleted tracks") {
  auto d = open_db();
  auto ids = add_tracks(d);
  TrackCache cache(d);
  TrackTableModel m(cache);
  m.set_ids(ids);

  QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
  m.set_now_playing(ids[1]);
  CHECK(changed.count() == 1);
  CHECK(cell(m, 1, TrackTableModel::Number) == QString::fromUtf8("▶"));
  CHECK(m.data(m.index(1, TrackTableModel::Title), Qt::FontRole).value<QFont>().bold());
  CHECK_FALSE(m.data(m.index(0, TrackTableModel::Title), Qt::FontRole).isValid());
  m.set_now_playing(ids[1]);
  CHECK(changed.count() == 1);  // no churn when nothing changed

  REQUIRE(db::TrackRepo(d).remove(ids[2]));
  cache.invalidate();
  CHECK(cell(m, 2, TrackTableModel::Title).isEmpty());  // stale id: blank, not a crash
}

TEST_CASE("sortable columns map to library sort fields") {
  CHECK(TrackTableModel::sort_field(TrackTableModel::Title) == SortField::Title);
  CHECK(TrackTableModel::sort_field(TrackTableModel::Artist) == SortField::Artist);
  CHECK(TrackTableModel::sort_field(TrackTableModel::DateAdded) == SortField::DateAdded);
  CHECK_FALSE(TrackTableModel::sort_field(TrackTableModel::Duration));
  CHECK_FALSE(TrackTableModel::sort_field(TrackTableModel::Codec));
}

TEST_CASE("filter browser cascades and reports the combined filter") {
  auto d = open_db();
  add_tracks(d);
  FilterBrowser fb(d);
  fb.set_base({});

  auto lists = fb.findChildren<QListWidget*>();
  REQUIRE(lists.size() == 3);
  auto *genres = lists[0], *artists = lists[1], *albums = lists[2];
  CHECK(genres->count() == 1 + 3);   // All + Electronic, House, Pop
  CHECK(artists->count() == 1 + 2);  // All + Daft Punk, Michael Jackson
  CHECK(albums->count() == 1 + 3);   // All + Discovery, Thriller, [Unknown Album]

  QSignalSpy spy(&fb, &FilterBrowser::filterChanged);
  genres->item(3)->setSelected(true);  // Pop
  CHECK(spy.count() == 1);
  CHECK(artists->count() == 1 + 1);  // only Michael Jackson has Pop
  CHECK(artists->item(1)->text() == "Michael Jackson");
  CHECK(fb.filter().genres == std::vector<std::string>{"Pop"});

  artists->item(1)->setSelected(true);
  CHECK(albums->count() == 1 + 1);
  CHECK(fb.filter().artists == std::vector<std::string>{"Michael Jackson"});

  albums->item(1)->setSelected(true);
  auto f = fb.filter();
  CHECK(f.album == "Thriller");
  CHECK(f.album_artist == "Michael Jackson");

  // Resulting filter really selects that album.
  CHECK(db::Query(d).tracks(f)->size() == 1);

  // Changing the base (e.g. a search) rebuilds the lists but keeps selections that still apply.
  db::TrackFilter base;
  base.search = "thriller";
  fb.set_base(base);
  CHECK(genres->count() == 1 + 1);
  CHECK(fb.filter().genres == std::vector<std::string>{"Pop"});
}

TEST_CASE("queue model shows the queue and highlights what is playing") {
  auto d = open_db();
  auto ids = add_tracks(d);
  TrackCache cache(d);
  QueueModel m(cache);

  playback::QueueSnapshot snap;
  snap.entries = {{10, ids[0]}, {11, ids[2]}, {12, TrackId{9999}}};
  snap.now_playing = 11;
  m.set_snapshot(snap);

  CHECK(m.rowCount() == 3);
  CHECK(cell(m, 0, QueueModel::Title) == "One More Time");
  CHECK(cell(m, 1, QueueModel::Marker) == QString::fromUtf8("▶"));
  CHECK(cell(m, 0, QueueModel::Marker) == "1");
  CHECK(cell(m, 1, QueueModel::Artist) == "Michael Jackson");
  CHECK(cell(m, 2, QueueModel::Title) == "(removed from library)");
  CHECK(m.entry_id_at(1) == 11);
  CHECK(m.row_of_entry(12) == 2);
  CHECK(m.row_of_entry(99) == -1);
}
