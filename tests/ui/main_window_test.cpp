#include <QElapsedTimer>
#include <QHeaderView>
#include <QLineEdit>
#include <QTableView>
#include <QtTest/QtTest>
#include <catch2/catch_test_macros.hpp>

#include "core/db/folder_repo.hpp"
#include "core/db/track_repo.hpp"
#include "core/library/scanner.hpp"
#include "fs_test_util.hpp"
#include "ui/item_roles.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_util.hpp"
#include "ui/theme.hpp"
#include "ui/track_delegate.hpp"

using namespace bh;
using namespace bh::ui;
using namespace bh::testutil;

namespace {

template <typename F>
bool wait_until(F cond, int ms = 5000) {
  QElapsedTimer t;
  t.start();
  while (!cond() && t.elapsed() < ms) QTest::qWait(20);
  return cond();
}

// A real app stack (silent audio device) over a small scanned library.
struct App {
  TempDir dir;
  std::unique_ptr<AppServices> services;
  std::unique_ptr<QSettings> settings;
  std::unique_ptr<MainWindow> window;
  QTableView *table = nullptr, *queue = nullptr;
  QLineEdit* search = nullptr;

  App() : dir("ui_app") {
    auto lib = dir.path / "music";
    for (auto* f : {"sine.mp3", "sine.flac", "sine.opus", "sine.ogg", "sine.m4a"}) copy_fixture(f, lib / f);

    auto s = AppServices::create(dir.path / "data", /*null_audio=*/true);
    REQUIRE(s);
    services = std::move(*s);

    WatchedFolder folder{{}, lib, true, true};
    folder.id = *db::FolderRepo(services->db).add(folder);
    REQUIRE(library::Scanner(services->db).scan(folder));

    settings = std::make_unique<QSettings>(from_path(dir.path / "settings.ini"), QSettings::IniFormat);
    window = std::make_unique<MainWindow>(*services, *settings);
    window->show();
    REQUIRE(QTest::qWaitForWindowExposed(window.get()));

    table = window->findChild<QTableView*>("track_table");
    queue = window->findChild<QTableView*>("queue_view");
    search = window->findChild<QLineEdit*>("search_box");
    REQUIRE(table);
    REQUIRE(queue);
    REQUIRE(search);
  }
  ~App() {
    window->close();
    window.reset();  // before the services it points at
  }

  QModelIndex cell(int row, int col) const { return table->model()->index(row, col); }
  // Select the row with a real click, then deliver the double-click event the view reacts to.
  // (QTest::mouseDClick's synthesized double-click doesn't reach item views on the offscreen platform.)
  void double_click_row(int row) {
    const QPoint pos = table->visualRect(cell(row, TrackTableModel::Title)).center();
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, pos);
    QMouseEvent dbl(QEvent::MouseButtonDblClick, pos, table->viewport()->mapToGlobal(pos), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(table->viewport(), &dbl);
  }
};

}  // namespace

TEST_CASE("window lists the scanned library, sorted by artist then album order") {
  App a;
  CHECK(a.table->model()->rowCount() == 5);
  CHECK(a.table->model()->data(a.cell(0, TrackTableModel::Title)).toString() == "Test Song");
  CHECK(a.queue->model()->rowCount() == 0);
  CHECK(a.window->windowTitle() == "BeatHive");
}

TEST_CASE("searching filters the list, and clearing restores it") {
  App a;
  a.search->setText("zzzzqqq");
  REQUIRE(wait_until([&] { return a.table->model()->rowCount() == 0; }));
  a.search->clear();
  REQUIRE(wait_until([&] { return a.table->model()->rowCount() == 5; }));
  a.search->setText("test album");
  REQUIRE(wait_until([&] { return a.table->model()->rowCount() == 5; }));
}

TEST_CASE("double-clicking a track plays the visible list from there") {
  App a;
  a.double_click_row(2);

  REQUIRE(wait_until([&] { return a.services->engine.state() == playback::PlayState::Playing; }));
  REQUIRE(wait_until([&] { return a.services->controller.now_playing().has_value(); }));

  auto snap = a.services->controller.snapshot();
  CHECK(snap.entries.size() == 5);
  // Playback started at the clicked row, not the top of the list.
  auto ids = static_cast<TrackTableModel*>(a.table->model())->ids();
  CHECK(a.services->controller.now_playing()->track == ids[2]);

  // The UI follows: window title, queue view, playing marker in the table.
  REQUIRE(wait_until([&] { return a.window->windowTitle().contains("Test Song"); }));
  CHECK(a.queue->model()->rowCount() == 5);
  CHECK(a.table->model()->data(a.cell(2, TrackTableModel::Number)).toString() == QString::fromUtf8("▶"));

  a.services->controller.stop();
  REQUIRE(wait_until([&] { return a.window->windowTitle() == "BeatHive"; }));
}

TEST_CASE("space toggles pause with the table focused") {
  App a;
  a.double_click_row(0);
  REQUIRE(wait_until([&] { return a.services->engine.state() == playback::PlayState::Playing; }));
  a.table->setFocus();
  QTest::keyClick(a.table, Qt::Key_Space);
  REQUIRE(wait_until([&] { return a.services->engine.state() == playback::PlayState::Paused; }));
  QTest::keyClick(a.table, Qt::Key_Space);
  REQUIRE(wait_until([&] { return a.services->engine.state() == playback::PlayState::Playing; }));
  a.services->controller.stop();
}

TEST_CASE("a track counts as played once half of it has been heard") {
  App a;
  a.double_click_row(0);
  REQUIRE(wait_until([&] { return a.services->controller.now_playing().has_value(); }));
  auto id = a.services->controller.now_playing()->track;

  db::TrackRepo repo(a.services->db);
  CHECK((**repo.get(id)).lib.play_count == 0);
  // The fixtures are ~1 s long, so the half-way mark passes within a couple of seconds.
  REQUIRE(wait_until([&] { return (**repo.get(id)).lib.play_count == 1; }, 6000));
  CHECK((**repo.get(id)).lib.last_played.has_value());
  a.services->controller.stop();
}

TEST_CASE("an empty library explains what to do") {
  TempDir dir("ui_empty");
  auto s = AppServices::create(dir.path / "data", true);
  REQUIRE(s);
  QSettings settings(from_path(dir.path / "s.ini"), QSettings::IniFormat);
  MainWindow w(**s, settings);
  w.show();
  REQUIRE(QTest::qWaitForWindowExposed(&w));
  CHECK(w.track_model().rowCount() == 0);
  bool found = false;
  for (auto* label : w.findChildren<QLabel*>())
    found |= label->isVisible() && label->text().contains("Library Folders");
  CHECK(found);
}

TEST_CASE("a sort click re-queries the library in the new order") {
  App a;
  auto* header = a.table->horizontalHeader();
  header->setSortIndicator(TrackTableModel::DateAdded, Qt::DescendingOrder);
  // Same five tracks, re-ordered by the database (no crash, nothing lost).
  REQUIRE(wait_until([&] { return a.table->model()->rowCount() == 5; }));
  // Non-sortable columns leave the order alone.
  header->setSortIndicator(TrackTableModel::Duration, Qt::AscendingOrder);
  CHECK(a.table->model()->rowCount() == 5);
}

TEST_CASE("clicking a star rates the track, and clicking it again clears the rating") {
  App a;
  const TrackId id = a.table->model()->index(0, 0).data(Qt::UserRole).toLongLong() ? TrackId{a.table->model()->index(0, 0).data(Qt::UserRole).toLongLong()} : TrackId{};
  REQUIRE(id);
  db::TrackRepo repo(a.services->db);
  REQUIRE((**repo.get(id)).lib.rating.stars == 0);

  auto click_star = [&](int star) {
    const QRect cell = a.table->visualRect(a.cell(0, TrackTableModel::Rating));
    QTest::mouseClick(a.table->viewport(), Qt::LeftButton, {}, TrackDelegate::star_rect(cell, star - 1).center().toPoint());
  };
  click_star(4);
  REQUIRE(wait_until([&] { return (**repo.get(id)).lib.rating.stars == 4; }));
  CHECK(a.table->model()->index(0, TrackTableModel::Rating).data(RatingRole).toInt() == 4);

  click_star(4);
  REQUIRE(wait_until([&] { return (**repo.get(id)).lib.rating.stars == 0; }));
}

TEST_CASE("switching theme restyles the window and survives a round trip") {
  App a;
  const auto before = theme().kind;
  apply_theme(*qApp, ThemeKind::Light);
  CHECK(theme().kind == ThemeKind::Light);
  CHECK(theme().base.lightness() > 200);
  a.window->grab();  // paints with the light palette without crashing
  apply_theme(*qApp, ThemeKind::Dark);
  CHECK(theme().base.lightness() < 80);
  CHECK(theme_from_name(theme_name(ThemeKind::Light)) == ThemeKind::Light);
  CHECK(theme_from_name("nonsense") == ThemeKind::Dark);
  (void)before;
}
