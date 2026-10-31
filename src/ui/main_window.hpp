#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>

#include "ui/app_services.hpp"
#include "ui/cover_cache.hpp"
#include "ui/album_tracks_view.hpp"
#include "ui/controls.hpp"
#include "ui/filter_browser.hpp"
#include "ui/glyphs.hpp"
#include "ui/library_service.hpp"
#include "ui/nav_tree.hpp"
#include "ui/now_playing_panel.hpp"
#include "ui/playback_bridge.hpp"
#include "ui/queue_model.hpp"
#include "ui/tile_grid.hpp"
#include "ui/track_table_model.hpp"
#include "ui/transport_bar.hpp"
#include "ui/view_selector.hpp"

namespace bh::ui {

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow(AppServices& app, QSettings& settings, QWidget* parent = nullptr);
  ~MainWindow() override;

  LibraryService& library() { return library_; }
  TrackTableModel& track_model() { return model_; }
  void reload_all();  // rebuild browse lists and the track list from the database
  void play_row(int row);  // replace the queue with the visible list and start at `row`

 protected:
  void closeEvent(QCloseEvent*) override;
  bool eventFilter(QObject*, QEvent*) override;

 private:
  void build_ui();
  void build_menus();
  void restore_settings();
  void save_settings();

  db::TrackFilter base_filter() const;
  db::TrackSort current_sort() const;
  void reload_tracks();
  void reload_albums();
  void set_view_mode(ViewMode mode);
  std::vector<TrackId> album_track_ids(const AlbumSummary& album);
  DetailData detail_for(const std::vector<TrackId>& ids, const QString& title, const QString& lead);
  void play_ids(const std::vector<TrackId>& ids, int start_row);
  // Play / Play Next / Add to Queue for an album's tracks. `row` >= 0 narrows the queue actions to
  // that one track (and Play starts the album from it).
  void show_tile_menu(const std::vector<TrackId>& ids, int row, const QPoint& global, const QString& play_label);
  db::TrackFilter current_filter() const;
  void update_empty_hint();
  void sync_playback();
  void on_tick();

  std::vector<TrackId> selected_ids() const;
  void show_track_menu(const QPoint& pos);
  void show_header_menu(const QPoint& pos);
  void add_folder_dialog();
  void on_scan_finished(int added, int updated, int missing, int errors, bool cancelled);

  AppServices& app_;
  QSettings& settings_;
  TrackCache cache_;
  CoverCache covers_;
  TrackTableModel model_;
  QueueModel queue_model_;
  PlaybackBridge bridge_;
  LibraryService library_;

  NavTree* nav_;
  FilterBrowser* filter_;
  QLineEdit* search_;
  QTableView* table_;
  TileGrid* albums_grid_;
  AlbumTracksView* album_tracks_view_;
  std::vector<AlbumSummary> albums_;
  QStackedWidget* views_;
  ViewSelector* view_selector_;
  QAction *tracks_view_action_, *album_tracks_view_action_, *covers_view_action_;
  QTableView* queue_view_;
  NowPlayingPanel* now_playing_;
  TransportBar* transport_;
  QWidget* empty_state_;
  QLabel *empty_title_, *empty_hint_, *queue_count_;
  QPushButton* empty_add_;
  IconButton* filter_button_;
  QAction *search_action_, *clear_action_;
  QLabel* status_label_;
  QProgressBar* progress_;
  QSplitter* main_split_;
  QAction *filter_action_, *side_action_;

  QTimer reload_timer_, search_timer_, tick_;
  SortField sort_field_ = SortField::Artist;
  bool sort_desc_ = false;
  bool user_sorted_ = false;
  bool artists_stale_ = true;  // the sidebar artist list needs rebuilding (search or library changed)
  std::uint64_t counted_tag_ = 0;
  TrackId shown_track_;
};

}  // namespace bh::ui
