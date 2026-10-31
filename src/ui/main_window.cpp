#include "ui/main_window.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "core/db/folder_repo.hpp"
#include "core/util/text.hpp"
#include "ui/glyphs.hpp"
#include "ui/item_roles.hpp"
#include "ui/library_dialog.hpp"
#include "ui/qt_util.hpp"
#include "ui/theme.hpp"
#include "ui/track_delegate.hpp"

namespace bh::ui {

using namespace std::chrono_literals;
using playback::PlayState;

namespace {

Timestamp now_ts() { return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()); }

// Volume slider is perceptual: a quadratic curve feels linear to the ear.
float slider_to_gain(int percent) {
  const float v = static_cast<float>(percent) / 100.0f;
  return v * v;
}

constexpr int kRecentLimit = 500;

QString count_text(std::size_t n) { return n == 1 ? QObject::tr("1 track") : QObject::tr("%1 tracks").arg(n); }

}  // namespace

MainWindow::MainWindow(AppServices& app, QSettings& settings, QWidget* parent)
    : QMainWindow(parent),
      app_(app),
      settings_(settings),
      cache_(app.db),
      covers_(app.db),
      model_(cache_),
      queue_model_(cache_),
      bridge_(app.controller),
      library_(app.db_path) {
  setWindowTitle("BeatHive");
  resize(1280, 780);
  build_ui();
  build_menus();
  restore_settings();

  reload_timer_.setSingleShot(true);
  reload_timer_.setInterval(0);
  connect(&reload_timer_, &QTimer::timeout, this, &MainWindow::reload_all);
  search_timer_.setSingleShot(true);
  search_timer_.setInterval(150);
  connect(&search_timer_, &QTimer::timeout, this, &MainWindow::reload_all);
  tick_.setInterval(250);
  connect(&tick_, &QTimer::timeout, this, &MainWindow::on_tick);
  tick_.start();

  connect(&bridge_, &PlaybackBridge::changed, this, &MainWindow::sync_playback);
  connect(&bridge_, &PlaybackBridge::skipped, this, [this](qlonglong, const QString& msg) {
    statusBar()->showMessage(tr("Skipped a track: %1").arg(msg), 6000);
  });
  connect(&library_, &LibraryService::scanStarted, this, [this] {
    progress_->setRange(0, 0);
    progress_->show();
    status_label_->setText(tr("Scanning library…"));
  });
  connect(&library_, &LibraryService::scanProgress, this, [this](const QString& what, int done, int total) {
    progress_->setRange(0, total);
    progress_->setValue(done);
    status_label_->setText(tr("Scanning %1…").arg(what));
  });
  connect(&library_, &LibraryService::scanFinished, this, &MainWindow::on_scan_finished);

  reload_all();
  sync_playback();
}

MainWindow::~MainWindow() = default;

// ------------------------------------------------------------------ layout

void MainWindow::build_ui() {
  nav_ = new NavTree(this);

  filter_ = new FilterBrowser(app_.db, this);
  search_ = new QLineEdit(this);
  search_->setObjectName("search_box");
  search_->setPlaceholderText(tr("Search library…  (Ctrl+F)"));

  table_ = new QTableView(this);
  table_->setObjectName("track_table");
  table_->setHorizontalHeader(new TrackHeader(table_));
  table_->setModel(&model_);
  auto* delegate = new TrackDelegate(table_);
  table_->setItemDelegate(delegate);
  table_->setMouseTracking(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setShowGrid(false);
  table_->setWordWrap(false);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  table_->verticalHeader()->hide();
  table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  table_->verticalHeader()->setDefaultSectionSize(kRowHeight);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  table_->setFrameShape(QFrame::NoFrame);
  auto* header = table_->horizontalHeader();
  header->setSectionsMovable(true);
  header->setSectionsClickable(true);
  header->setSortIndicatorShown(true);
  header->setStretchLastSection(false);
  header->setContextMenuPolicy(Qt::CustomContextMenu);
  header->setHighlightSections(false);

  // Sensible first-run layout.
  const int widths[TrackTableModel::ColumnCount] = {44, 220, 150, 170, 120, 100, 66, 66, 92, 74, 100, 80, 70};
  for (int c = 0; c < TrackTableModel::ColumnCount; ++c) table_->setColumnWidth(c, widths[c]);
  for (int c : {TrackTableModel::AlbumArtist, TrackTableModel::Genre, TrackTableModel::DateAdded, TrackTableModel::Bitrate, TrackTableModel::Codec})
    table_->setColumnHidden(c, true);
  header->setSectionResizeMode(TrackTableModel::Title, QHeaderView::Stretch);  // soaks up spare width
  header->setSortIndicator(TrackTableModel::Artist, Qt::AscendingOrder);

  empty_state_ = new QWidget(this);  // re-parented over the whole centre pane below
  empty_state_->setObjectName("empty_state");
  empty_state_->setAttribute(Qt::WA_StyledBackground);
  auto* elay = new QVBoxLayout(empty_state_);
  elay->setAlignment(Qt::AlignCenter);
  elay->setSpacing(10);
  auto* mark = new BrandMark(64, empty_state_);
  empty_title_ = new QLabel(empty_state_);
  empty_title_->setObjectName("empty_title");
  empty_hint_ = new QLabel(empty_state_);
  empty_hint_->setObjectName("empty_hint");
  empty_add_ = new QPushButton(tr("Add music folder…"), empty_state_);
  empty_add_->setProperty("accent", true);
  empty_add_->setCursor(Qt::PointingHandCursor);
  empty_add_->setMinimumWidth(180);
  empty_title_->setAlignment(Qt::AlignCenter);
  empty_hint_->setAlignment(Qt::AlignCenter);
  empty_hint_->setWordWrap(true);
  empty_hint_->setFixedWidth(470);
  elay->addWidget(mark, 0, Qt::AlignHCenter);
  elay->addSpacing(6);
  elay->addWidget(empty_title_, 0, Qt::AlignHCenter);
  elay->addWidget(empty_hint_, 0, Qt::AlignHCenter);
  elay->addSpacing(10);
  elay->addWidget(empty_add_, 0, Qt::AlignHCenter);
  empty_state_->hide();

  auto* center = new QWidget(this);
  auto* clay = new QVBoxLayout(center);
  clay->setContentsMargins(0, 0, 0, 0);
  clay->setSpacing(1);
  clay->addWidget(filter_);
  albums_grid_ = new TileGrid(covers_, [this](int i) {
    const AlbumSummary& a = albums_[static_cast<std::size_t>(i)];
    const TileItem item = album_item(a);
    return detail_for(album_track_ids(a), item.title, item.subtitle);
  }, this);
  album_tracks_view_ = new AlbumTracksView(covers_, cache_, [this](int i) {
    return album_track_ids(albums_[static_cast<std::size_t>(i)]);
  }, this);
  views_ = new QStackedWidget(this);
  views_->addWidget(table_);
  views_->addWidget(album_tracks_view_);
  views_->addWidget(albums_grid_);
  clay->addWidget(views_, 1);
  empty_state_->setParent(center);
  center->installEventFilter(this);

  now_playing_ = new NowPlayingPanel(covers_, this);
  queue_view_ = new QTableView(this);
  queue_view_->setObjectName("queue_view");
  queue_view_->setModel(&queue_model_);
  queue_view_->setItemDelegate(new TrackDelegate(queue_view_));
  queue_view_->setMouseTracking(true);
  queue_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
  queue_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  queue_view_->setShowGrid(false);
  queue_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  queue_view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  queue_view_->verticalHeader()->hide();
  queue_view_->verticalHeader()->setDefaultSectionSize(kTwoLineRowHeight);
  queue_view_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  queue_view_->horizontalHeader()->hide();
  queue_view_->horizontalHeader()->setStretchLastSection(false);
  queue_view_->horizontalHeader()->setSectionResizeMode(QueueModel::Title, QHeaderView::Stretch);
  queue_view_->horizontalHeader()->setSectionResizeMode(QueueModel::Artist, QHeaderView::Interactive);
  queue_view_->setColumnWidth(QueueModel::Marker, 44);
  queue_view_->setColumnWidth(QueueModel::Time, 64);
  queue_view_->setColumnHidden(QueueModel::Artist, true);  // shown under the title instead
  queue_view_->setFrameShape(QFrame::NoFrame);
  queue_view_->setContextMenuPolicy(Qt::CustomContextMenu);

  auto* queue_head = new QWidget(this);
  queue_head->setObjectName("queue_head");
  queue_head->setAttribute(Qt::WA_StyledBackground);
  auto* qhl = new QHBoxLayout(queue_head);
  qhl->setContentsMargins(18, 8, 10, 8);
  auto* queue_label = new QLabel(tr("UP NEXT"), queue_head);
  queue_label->setObjectName("section_title");
  queue_count_ = new QLabel(queue_head);
  queue_count_->setObjectName("np_album");
  auto* clear_queue = new QPushButton(tr("Clear"), queue_head);
  clear_queue->setObjectName("flat");
  clear_queue->setCursor(Qt::PointingHandCursor);
  qhl->addWidget(queue_label);
  qhl->addWidget(queue_count_);
  qhl->addStretch();
  qhl->addWidget(clear_queue);
  connect(clear_queue, &QPushButton::clicked, this, [this] { app_.controller.clear(); });

  auto* side = new QSplitter(Qt::Vertical, this);
  side->setChildrenCollapsible(false);
  side->setHandleWidth(1);
  side->addWidget(now_playing_);
  auto* queue_box = new QWidget(this);
  queue_box->setObjectName("queue_box");
  auto* qlay = new QVBoxLayout(queue_box);
  qlay->setContentsMargins(0, 0, 0, 0);
  qlay->setSpacing(0);
  qlay->addWidget(queue_head);
  qlay->addWidget(queue_view_, 1);
  side->addWidget(queue_box);
  side->setStretchFactor(0, 3);
  side->setStretchFactor(1, 2);

  main_split_ = new QSplitter(Qt::Horizontal, this);
  main_split_->setChildrenCollapsible(false);
  main_split_->setHandleWidth(9);
  main_split_->addWidget(nav_);
  main_split_->addWidget(center);
  main_split_->addWidget(side);
  main_split_->setStretchFactor(0, 0);
  main_split_->setStretchFactor(1, 1);
  main_split_->setStretchFactor(2, 0);
  main_split_->setSizes({200, 760, 340});
  side->setObjectName("side_split");

  transport_ = new TransportBar(this);

  // Top bar: brand, search, view toggle.
  auto* top = new QWidget(this);
  top->setObjectName("topbar");
  top->setAttribute(Qt::WA_StyledBackground);
  auto* tlay = new QHBoxLayout(top);
  tlay->setContentsMargins(16, 6, 16, 10);
  tlay->setSpacing(10);
  auto* brand = new QLabel("BeatHive", top);
  brand->setObjectName("brand");
  tlay->addWidget(new BrandMark(28, top));
  tlay->addWidget(brand);
  tlay->addSpacing(28);
  search_->setMinimumWidth(240);
  search_->setMaximumWidth(480);
  search_->setFixedHeight(34);
  tlay->addWidget(search_, 3);
  tlay->addStretch(2);
  filter_button_ = new IconButton(Glyph::Columns, 34, 20, top);
  filter_button_->setCheckable(true);
  filter_button_->setChecked(true);
  filter_button_->setToolTip(tr("Show or hide the filter browser (F7)"));
  tlay->addWidget(filter_button_);

  auto refresh_search_icons = [this] {
    search_action_->setIcon(glyph_icon(Glyph::Search, theme().faint));
    clear_action_->setIcon(glyph_icon(Glyph::Close, theme().dim));
  };
  search_action_ = search_->addAction(QIcon(), QLineEdit::LeadingPosition);
  clear_action_ = search_->addAction(QIcon(), QLineEdit::TrailingPosition);
  clear_action_->setVisible(false);
  refresh_search_icons();
  connect(&ThemeNotifier::instance(), &ThemeNotifier::changed, this, refresh_search_icons);
  connect(search_, &QLineEdit::textChanged, this, [this](const QString& t) { clear_action_->setVisible(!t.isEmpty()); });
  connect(clear_action_, &QAction::triggered, search_, &QLineEdit::clear);

  // The view selector gets a strip of its own under the top bar, centred.
  auto* viewbar = new QWidget(this);
  viewbar->setObjectName("viewbar");
  viewbar->setAttribute(Qt::WA_StyledBackground);
  view_selector_ = new ViewSelector(viewbar);
  auto* vlay = new QHBoxLayout(viewbar);
  vlay->setContentsMargins(16, 0, 16, 10);
  vlay->addStretch();
  vlay->addWidget(view_selector_);
  vlay->addStretch();

  auto* root = new QWidget(this);
  auto* rlay = new QVBoxLayout(root);
  rlay->setContentsMargins(0, 0, 0, 0);
  rlay->setSpacing(0);
  rlay->addWidget(top);
  rlay->addWidget(viewbar);
  rlay->addWidget(main_split_, 1);
  rlay->addWidget(transport_);
  setCentralWidget(root);
  setMinimumSize(1100, 620);

  status_label_ = new QLabel(this);
  progress_ = new QProgressBar(this);
  progress_->setMaximumWidth(180);
  statusBar()->setSizeGripEnabled(false);
  statusBar()->setContentsMargins(14, 0, 14, 0);
  progress_->setTextVisible(false);
  progress_->hide();
  statusBar()->addWidget(status_label_, 1);
  statusBar()->addPermanentWidget(progress_);

  // --- wiring ---
  connect(nav_, &NavTree::selectionChanged, this, [this] {
    user_sorted_ = false;
    reload_timer_.start();
  });
  connect(search_, &QLineEdit::textChanged, this, [this] {
    artists_stale_ = true;
    search_timer_.start();
  });
  connect(empty_add_, &QPushButton::clicked, this, &MainWindow::add_folder_dialog);
  connect(filter_, &FilterBrowser::filterChanged, this, &MainWindow::reload_tracks);
  connect(view_selector_, &ViewSelector::modeChanged, this, &MainWindow::set_view_mode);
  connect(albums_grid_, &TileGrid::play_requested, this, [this](int i, int row) {
    play_ids(album_track_ids(albums_[static_cast<std::size_t>(i)]), row);
  });
  connect(albums_grid_, &TileGrid::context_requested, this, [this](int i, const QPoint& global) {
    show_tile_menu(album_track_ids(albums_[static_cast<std::size_t>(i)]), -1, global, tr("Play Album"));
  });
  connect(album_tracks_view_, &AlbumTracksView::play_requested, this, [this](int i, int row) {
    play_ids(album_track_ids(albums_[static_cast<std::size_t>(i)]), row);
  });
  connect(album_tracks_view_, &AlbumTracksView::context_requested, this, [this](int i, int row, const QPoint& global) {
    show_tile_menu(album_track_ids(albums_[static_cast<std::size_t>(i)]), row, global, row < 0 ? tr("Play Album") : tr("Play"));
  });
  connect(header, &QHeaderView::sortIndicatorChanged, this, [this](int col, Qt::SortOrder order) {
    auto f = TrackTableModel::sort_field(col);
    if (!f) return;  // not sortable; leave the previous sort in place
    sort_field_ = *f;
    sort_desc_ = order == Qt::DescendingOrder;
    user_sorted_ = true;
    reload_tracks();
  });
  connect(header, &QHeaderView::customContextMenuRequested, this, &MainWindow::show_header_menu);
  connect(delegate, &TrackDelegate::rating_clicked, this, [this](const QModelIndex& i, int stars) {
    const TrackId id = model_.id_at(i.row());
    if (!id) return;
    if (auto r = db::TrackRepo(app_.db).set_rating(id, Rating::clamped(stars)); !r) {
      statusBar()->showMessage(tr("Couldn't save the rating: %1").arg(qs(r.error().message)), 6000);
      return;
    }
    model_.refresh_track(id);
  });
  connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& i) { play_row(i.row()); });
  connect(table_, &QTableView::customContextMenuRequested, this, &MainWindow::show_track_menu);
  for (auto key : {Qt::Key_Return, Qt::Key_Enter})
    connect(new QShortcut(QKeySequence(key), table_, nullptr, nullptr, Qt::WidgetShortcut), &QShortcut::activated, this,
            [this] {
              if (auto i = table_->currentIndex(); i.isValid()) play_row(i.row());
            });
  connect(new QShortcut(QKeySequence(Qt::Key_Space), table_, nullptr, nullptr, Qt::WidgetShortcut),
          &QShortcut::activated, this, [this] { app_.controller.toggle_pause(); });
  connect(queue_view_, &QTableView::doubleClicked, this, [this](const QModelIndex& i) {
    app_.controller.play_entry(queue_model_.entry_id_at(i.row()));
  });
  connect(new QShortcut(QKeySequence(Qt::Key_Delete), queue_view_, nullptr, nullptr, Qt::WidgetShortcut),
          &QShortcut::activated, this, [this] {
            std::vector<std::uint64_t> ids;
            for (auto& i : queue_view_->selectionModel()->selectedRows()) ids.push_back(queue_model_.entry_id_at(i.row()));
            for (auto id : ids) app_.controller.remove(id);
          });
  connect(queue_view_, &QTableView::customContextMenuRequested, this, [this](const QPoint& pos) {
    QMenu menu(this);
    auto* remove = menu.addAction(tr("Remove from Queue"));
    auto* clear = menu.addAction(tr("Clear Queue"));
    auto* picked = menu.exec(queue_view_->viewport()->mapToGlobal(pos));
    if (picked == remove) {
      std::vector<std::uint64_t> ids;
      for (auto& i : queue_view_->selectionModel()->selectedRows()) ids.push_back(queue_model_.entry_id_at(i.row()));
      for (auto id : ids) app_.controller.remove(id);
    } else if (picked == clear) {
      app_.controller.clear();
    }
  });

  auto& ctl = app_.controller;
  connect(transport_, &TransportBar::previousClicked, this, [&ctl] { ctl.previous(); });
  connect(transport_, &TransportBar::nextClicked, this, [&ctl] { ctl.next(); });
  connect(transport_, &TransportBar::playPauseClicked, this, [&ctl] { ctl.toggle_pause(); });
  connect(transport_, &TransportBar::stopClicked, this, [&ctl] { ctl.stop(); });
  connect(transport_, &TransportBar::seekRequested, this,
          [this](qint64 ms) { app_.engine.seek(std::chrono::milliseconds(ms)); });
  connect(transport_, &TransportBar::volumeChanged, this,
          [this](int pct) { app_.engine.set_volume(slider_to_gain(pct)); });
  connect(transport_, &TransportBar::shuffleToggled, this, [&ctl](bool on) { ctl.set_shuffle(on); });
  connect(transport_, &TransportBar::repeatChanged, this, [&ctl](RepeatMode m) { ctl.set_repeat(m); });
}

void MainWindow::build_menus() {
  auto& ctl = app_.controller;

  auto* file = menuBar()->addMenu(tr("&File"));
  file->addAction(tr("Library &Folders…"), QKeySequence(Qt::CTRL | Qt::Key_O), this, &MainWindow::add_folder_dialog);
  file->addAction(tr("&Rescan Library"), QKeySequence(Qt::Key_F5), this, [this] { library_.scan_all(); });
  file->addSeparator();
  file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

  auto* view = menuBar()->addMenu(tr("&View"));
  filter_action_ = view->addAction(tr("&Filter Browser"));
  filter_action_->setCheckable(true);
  filter_action_->setChecked(true);
  filter_action_->setShortcut(QKeySequence(Qt::Key_F7));
  connect(filter_action_, &QAction::toggled, this, [this](bool on) {
    filter_->setVisible(on);
    reload_tracks();
  });
  side_action_ = view->addAction(tr("&Now Playing Panel"));
  side_action_->setCheckable(true);
  side_action_->setChecked(true);
  side_action_->setShortcut(QKeySequence(Qt::Key_F8));
  connect(side_action_, &QAction::toggled, this, [this](bool on) {
    main_split_->widget(2)->setVisible(on);
    transport_->set_queue_checked(on);
  });
  connect(transport_, &TransportBar::queueToggled, side_action_, &QAction::setChecked);
  connect(filter_button_, &QAbstractButton::toggled, filter_action_, &QAction::setChecked);
  connect(filter_action_, &QAction::toggled, filter_button_, [this](bool on) {
    QSignalBlocker b(filter_button_);
    filter_button_->setChecked(on);
  });
  auto* modes = new QActionGroup(this);
  tracks_view_action_ = view->addAction(tr("&Tracks"));
  album_tracks_view_action_ = view->addAction(tr("&Album && Tracks"));
  covers_view_action_ = view->addAction(tr("Album &Covers"));
  tracks_view_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
  album_tracks_view_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
  covers_view_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
  for (auto* a : {tracks_view_action_, album_tracks_view_action_, covers_view_action_}) {
    a->setCheckable(true);
    modes->addAction(a);
  }
  tracks_view_action_->setChecked(true);
  connect(tracks_view_action_, &QAction::triggered, this, [this] { view_selector_->set_mode(ViewMode::Tracks); });
  connect(album_tracks_view_action_, &QAction::triggered, this, [this] { view_selector_->set_mode(ViewMode::AlbumTracks); });
  connect(covers_view_action_, &QAction::triggered, this, [this] { view_selector_->set_mode(ViewMode::Covers); });
  view->addSeparator();
  auto* themes = view->addMenu(tr("&Theme"));
  auto* group = new QActionGroup(this);
  const ThemeKind current = theme_from_name(settings_.value("view/theme", "dark").toString());
  for (auto kind : {ThemeKind::Dark, ThemeKind::Light}) {
    auto* a = themes->addAction(kind == ThemeKind::Dark ? tr("&Dark") : tr("&Light"));
    a->setCheckable(true);
    a->setChecked(kind == current);
    group->addAction(a);
    connect(a, &QAction::triggered, this, [this, kind] {
      apply_theme(*qApp, kind);
      settings_.setValue("view/theme", theme_name(kind));
      sync_playback();  // cover placeholders and icons pick up the new colours
    });
  }
  view->addSeparator();
  view->addAction(tr("&Search"), QKeySequence::Find, this, [this] {
    search_->setFocus();
    search_->selectAll();
  });

  auto* play = menuBar()->addMenu(tr("&Playback"));
  play->addAction(tr("&Play / Pause"), QKeySequence(Qt::CTRL | Qt::Key_Space), this, [&ctl] { ctl.toggle_pause(); });
  play->addAction(tr("&Stop"), QKeySequence(Qt::CTRL | Qt::Key_Period), this, [&ctl] { ctl.stop(); });
  play->addAction(tr("&Next"), QKeySequence(Qt::CTRL | Qt::Key_Right), this, [&ctl] { ctl.next(); });
  play->addAction(tr("Pre&vious"), QKeySequence(Qt::CTRL | Qt::Key_Left), this, [&ctl] { ctl.previous(); });
  play->addSeparator();
  play->addAction(tr("Volume &Up"), QKeySequence(Qt::CTRL | Qt::Key_Up), this,
                  [this] { transport_->set_volume_percent(std::min(100, transport_->volume_percent() + 5)); app_.engine.set_volume(slider_to_gain(transport_->volume_percent())); });
  play->addAction(tr("Volume &Down"), QKeySequence(Qt::CTRL | Qt::Key_Down), this,
                  [this] { transport_->set_volume_percent(std::max(0, transport_->volume_percent() - 5)); app_.engine.set_volume(slider_to_gain(transport_->volume_percent())); });

  auto* help = menuBar()->addMenu(tr("&Help"));
  help->addAction(tr("&About BeatHive"), this, [this] {
    QMessageBox::about(this, tr("About BeatHive"),
                       tr("<b>BeatHive</b> 0.1<br>A music library manager and player.<br><br>"
                          "Development build."));
  });
}

// ------------------------------------------------------------ settings

void MainWindow::restore_settings() {
  restoreGeometry(settings_.value("window/geometry").toByteArray());
  restoreState(settings_.value("window/state").toByteArray());
  if (auto s = settings_.value("window/splitter").toByteArray(); !s.isEmpty()) main_split_->restoreState(s);
  if (auto h = settings_.value("table/header").toByteArray(); !h.isEmpty()) {
    table_->horizontalHeader()->restoreState(h);
    // restoreState also restores the sort indicator; make our sort state agree with it.
    if (auto f = TrackTableModel::sort_field(table_->horizontalHeader()->sortIndicatorSection())) {
      sort_field_ = *f;
      sort_desc_ = table_->horizontalHeader()->sortIndicatorOrder() == Qt::DescendingOrder;
    }
  }
  const int vol = settings_.value("playback/volume", 80).toInt();
  transport_->set_volume_percent(vol);
  app_.engine.set_volume(slider_to_gain(vol));
  filter_action_->setChecked(settings_.value("view/filter_browser", true).toBool());
  side_action_->setChecked(settings_.value("view/side_panel", true).toBool());
  filter_->setVisible(filter_action_->isChecked());
  {
    const QString mode = settings_.value("view/mode", "tracks").toString();
    view_selector_->set_mode(mode == "album_tracks" ? ViewMode::AlbumTracks : mode == "covers" ? ViewMode::Covers : ViewMode::Tracks);
  }
  main_split_->widget(2)->setVisible(side_action_->isChecked());
}

void MainWindow::save_settings() {
  settings_.setValue("window/geometry", saveGeometry());
  settings_.setValue("window/state", saveState());
  settings_.setValue("window/splitter", main_split_->saveState());
  settings_.setValue("table/header", table_->horizontalHeader()->saveState());
  settings_.setValue("playback/volume", transport_->volume_percent());
  settings_.setValue("view/filter_browser", filter_action_->isChecked());
  settings_.setValue("view/side_panel", side_action_->isChecked());
  settings_.setValue("view/mode", view_selector_->mode() == ViewMode::AlbumTracks ? "album_tracks"
                                  : view_selector_->mode() == ViewMode::Covers    ? "covers"
                                                                                  : "tracks");
  settings_.sync();
}

void MainWindow::closeEvent(QCloseEvent* e) {
  save_settings();
  library_.cancel();
  app_.controller.stop();
  QMainWindow::closeEvent(e);
}

// ----------------------------------------------------------------- browsing

db::TrackFilter MainWindow::base_filter() const {
  db::TrackFilter f;
  f.search = stdstr(search_->text());
  if (auto artist = nav_->current_artist()) f.artists = {*artist};
  RuleGroup rules;
  switch (nav_->current_source()) {
    case NavSource::AllMusic: break;
    case NavSource::RecentlyAdded:
      rules.conditions.push_back({RuleField::DateAdded, RuleOp::InLastDays, std::int64_t{30}});
      f.rules = rules;
      break;
    case NavSource::RecentlyPlayed:
      rules.conditions.push_back({RuleField::LastPlayed, RuleOp::InLastDays, std::int64_t{90}});
      f.rules = rules;
      break;
  }
  return f;
}

db::TrackSort MainWindow::current_sort() const {
  if (user_sorted_ || nav_->current_source() == NavSource::AllMusic) return {sort_field_, sort_desc_};
  return {nav_->current_source() == NavSource::RecentlyAdded ? SortField::DateAdded : SortField::LastPlayed, true};
}

void MainWindow::reload_all() {
  if (artists_stale_) {
    artists_stale_ = false;
    db::TrackFilter all;  // the sidebar lists every artist that matches the search, whatever is selected
    all.search = stdstr(search_->text());
    if (auto artists = db::Query(app_.db).artists(all)) nav_->set_artists(*artists);
  }
  filter_->set_base(base_filter());
  reload_tracks();
}

db::TrackFilter MainWindow::current_filter() const {
  return filter_->isVisible() || filter_action_->isChecked() ? filter_->filter() : base_filter();
}

void MainWindow::set_view_mode(ViewMode mode) {
  switch (mode) {
    case ViewMode::Tracks:
      views_->setCurrentWidget(table_);
      tracks_view_action_->setChecked(true);
      break;
    case ViewMode::AlbumTracks:
      views_->setCurrentWidget(album_tracks_view_);
      album_tracks_view_action_->setChecked(true);
      break;
    case ViewMode::Covers:
      views_->setCurrentWidget(albums_grid_);
      covers_view_action_->setChecked(true);
      break;
  }
  reload_tracks();
}

void MainWindow::reload_albums() {
  auto albums = db::Query(app_.db).albums(current_filter());
  albums_ = albums ? std::move(*albums) : std::vector<AlbumSummary>{};
  if (view_selector_->mode() == ViewMode::Covers) {
    std::vector<TileItem> items;
    for (const auto& a : albums_) items.push_back(album_item(a));
    albums_grid_->set_items(std::move(items));
  } else {
    std::vector<AlbumBlock> blocks;
    for (const auto& a : albums_) {
      const TileItem item = album_item(a);
      blocks.push_back({item.title, item.subtitle, a.art, a.track_count});
    }
    album_tracks_view_->set_albums(std::move(blocks));
    album_tracks_view_->set_now_playing(shown_track_);
  }
}

std::vector<TrackId> MainWindow::album_track_ids(const AlbumSummary& album) {
  auto filter = current_filter();
  filter.album_artist = album.key.album_artist;
  filter.album = album.key.album;
  auto ids = db::Query(app_.db).tracks(filter, {SortField::Album, false});
  return ids ? std::move(*ids) : std::vector<TrackId>{};
}

DetailData MainWindow::detail_for(const std::vector<TrackId>& ids, const QString& title, const QString& lead) {
  DetailData d;
  d.title = title;
  for (TrackId id : ids)
    if (const Track* t = cache_.get(id)) d.tracks.push_back(t);
  d.meta = track_meta(lead, d.tracks);
  return d;
}

void MainWindow::play_ids(const std::vector<TrackId>& ids, int start_row) {
  if (ids.empty()) return;
  app_.controller.play_tracks(ids, static_cast<std::size_t>(std::clamp<int>(start_row, 0, static_cast<int>(ids.size()) - 1)));
}

void MainWindow::show_tile_menu(const std::vector<TrackId>& ids, int row, const QPoint& global, const QString& play_label) {
  if (ids.empty()) return;
  const bool one = row >= 0 && row < static_cast<int>(ids.size());
  const std::vector<TrackId> picked_ids = one ? std::vector<TrackId>{ids[static_cast<std::size_t>(row)]} : ids;
  QMenu menu(this);
  auto* play = menu.addAction(play_label);
  auto* next = menu.addAction(tr("Play Next"));
  auto* queue = menu.addAction(tr("Add to Queue"));
  auto* picked = menu.exec(global);
  if (picked == play) play_ids(ids, one ? row : 0);
  else if (picked == next) app_.controller.play_next(picked_ids);
  else if (picked == queue) app_.controller.enqueue(picked_ids);
}

void MainWindow::reload_tracks() {
  cache_.invalidate();
  auto filter = current_filter();
  const bool limited = nav_->current_source() != NavSource::AllMusic;
  const std::optional<int> limit = limited ? std::optional<int>(kRecentLimit) : std::nullopt;

  db::Query q(app_.db);
  auto ids = q.tracks(filter, current_sort(), limit);
  model_.set_ids(ids ? std::move(*ids) : std::vector<TrackId>{});
  model_.set_now_playing(shown_track_);

  QString status = count_text(model_.ids().size());
  if (!limited)
    if (auto s = q.summary(filter)) {
      const auto total_min = std::chrono::duration_cast<std::chrono::minutes>(s->total_duration).count();
      status += total_min >= 60 * 24 ? tr(" · %1 days").arg(static_cast<double>(total_min) / (60 * 24), 0, 'f', 1)
                : total_min >= 60    ? tr(" · %1 h %2 min").arg(total_min / 60).arg(total_min % 60)
                                     : tr(" · %1 min").arg(total_min);
    }
  if (view_selector_->mode() != ViewMode::Tracks) {
    reload_albums();
    const auto n = albums_.size();
    status = (n == 1 ? tr("1 album") : tr("%1 albums").arg(n)) + " · " + status;
  }
  if (!library_.scanning()) status_label_->setText(status);
  update_empty_hint();
}

void MainWindow::update_empty_hint() {
  const bool empty = model_.ids().empty();
  if (empty) {
    bool has_folders = false;
    if (auto f = db::FolderRepo(app_.db).list()) has_folders = !f->empty();
    const bool searching = !search_->text().isEmpty();
    empty_title_->setText(!has_folders ? tr("Welcome to BeatHive")
                          : searching  ? tr("No matches")
                          : library_.scanning() ? tr("Scanning your music…")
                                                : tr("Nothing here yet"));
    empty_hint_->setText(!has_folders ? tr("Add the folder where your music lives and BeatHive will scan it.\n"
                                           "You can change folders any time under File ▸ Library Folders…")
                         : searching  ? tr("No tracks match your search.")
                         : library_.scanning() ? tr("Tracks will appear here as they are found.")
                                               : tr("No tracks to show here."));
    empty_add_->setVisible(!has_folders);
    empty_state_->setGeometry(empty_state_->parentWidget()->rect());
    empty_state_->raise();
  }
  empty_state_->setVisible(empty);
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
  if (o == empty_state_->parentWidget() && e->type() == QEvent::Resize)
    empty_state_->setGeometry(empty_state_->parentWidget()->rect());
  return QMainWindow::eventFilter(o, e);
}

// ------------------------------------------------------------------ playback

void MainWindow::play_row(int row) {
  if (row < 0) return;
  app_.controller.play_tracks(model_.ids(), static_cast<std::size_t>(row));
}

std::vector<TrackId> MainWindow::selected_ids() const {
  std::vector<int> rows;
  for (auto& i : table_->selectionModel()->selectedRows()) rows.push_back(i.row());
  std::sort(rows.begin(), rows.end());
  std::vector<TrackId> out;
  for (int r : rows) out.push_back(model_.id_at(r));
  return out;
}

void MainWindow::show_track_menu(const QPoint& pos) {
  auto idx = table_->indexAt(pos);
  if (idx.isValid() && !table_->selectionModel()->isRowSelected(idx.row()))
    table_->selectRow(idx.row());
  auto ids = selected_ids();
  if (ids.empty()) return;

  QMenu menu(this);
  auto* play = menu.addAction(tr("Play"));
  auto* next = menu.addAction(tr("Play Next"));
  auto* queue = menu.addAction(tr("Add to Queue"));
  menu.addSeparator();
  auto* show = menu.addAction(tr("Show in File Manager"));
  show->setEnabled(ids.size() == 1);

  auto* picked = menu.exec(table_->viewport()->mapToGlobal(pos));
  if (!picked) return;
  if (picked == play) {
    if (ids.size() == 1) play_row(idx.row());  // single track: play it within the whole list
    else app_.controller.play_tracks(ids);
  } else if (picked == next) {
    app_.controller.play_next(ids);
  } else if (picked == queue) {
    app_.controller.enqueue(ids);
  } else if (picked == show) {
    if (const Track* t = cache_.get(ids[0]))
      QDesktopServices::openUrl(QUrl::fromLocalFile(from_path(t->file.path.parent_path())));
  }
}

void MainWindow::show_header_menu(const QPoint& pos) {
  QMenu menu(this);
  for (int c = 0; c < TrackTableModel::ColumnCount; ++c) {
    auto* a = menu.addAction(TrackTableModel::column_title(c));
    a->setCheckable(true);
    a->setChecked(!table_->isColumnHidden(c));
    connect(a, &QAction::toggled, this, [this, c](bool on) { table_->setColumnHidden(c, !on); });
  }
  menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}

void MainWindow::sync_playback() {
  auto snap = app_.controller.snapshot();
  queue_model_.set_snapshot(snap);
  if (snap.now_playing)
    if (int row = queue_model_.row_of_entry(*snap.now_playing); row >= 0)
      queue_view_->scrollTo(queue_model_.index(row, 0), QAbstractItemView::PositionAtCenter);

  auto now = app_.controller.now_playing();
  shown_track_ = now ? now->track : TrackId{};
  model_.set_now_playing(shown_track_);
  album_tracks_view_->set_now_playing(shown_track_);
  const Track* t = shown_track_ ? cache_.get(shown_track_) : nullptr;
  now_playing_->show_track(t);
  transport_->set_cover(t && t->art ? covers_.get(t->art, static_cast<int>(transport_->cover_side() * devicePixelRatioF())) : QPixmap());
  queue_count_->setText(snap.entries.empty() ? QString() : count_text(snap.entries.size()));

  const auto state = app_.engine.state();
  transport_->set_state(state);
  transport_->set_shuffle(snap.shuffle);
  transport_->set_repeat(snap.repeat);
  if (t) {
    const QString title = t->tags.title.empty() ? from_path(t->file.path.stem()) : qs(t->tags.title);
    transport_->set_title(title, qs(t->tags.artist_display()));
    setWindowTitle(QString("%1 – %2 · BeatHive").arg(title, qs(t->tags.artist_display())));
  } else {
    transport_->set_title({}, {});
    transport_->clear_position();
    setWindowTitle("BeatHive");
  }
}

void MainWindow::on_tick() {
  auto pos = app_.engine.position();
  if (!pos) return;
  transport_->set_position(pos->position, pos->duration);

  // Count a play once half the track (or four minutes) has been heard.
  if (pos->item.tag != counted_tag_ && pos->duration.count() > 0 &&
      pos->position >= std::min<std::chrono::milliseconds>(pos->duration / 2, 240s)) {
    counted_tag_ = pos->item.tag;
    (void)db::TrackRepo(app_.db).record_play(pos->item.id, now_ts());  // best effort
    cache_.invalidate(pos->item.id);
    model_.refresh_track(pos->item.id);
  }
}

// ------------------------------------------------------------------- library

void MainWindow::add_folder_dialog() {
  LibraryDialog dlg(app_.db, this);
  dlg.exec();
  if (dlg.changed()) {
    artists_stale_ = true;
    reload_all();
    library_.scan_all();
  }
}

void MainWindow::on_scan_finished(int added, int updated, int missing, int errors, bool cancelled) {
  progress_->hide();
  artists_stale_ = true;
  reload_all();
  if (cancelled) {
    statusBar()->showMessage(tr("Scan cancelled"), 5000);
  } else {
    QString msg = tr("Scan complete: %1 new, %2 updated").arg(added).arg(updated);
    if (missing) msg += tr(", %1 missing").arg(missing);
    if (errors) msg += tr(", %1 unreadable").arg(errors);
    statusBar()->showMessage(msg, 8000);
  }
}

}  // namespace bh::ui
