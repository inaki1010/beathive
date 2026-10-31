# MVP Spec: A MusicBee-style Music Manager in C++ (Windows + Linux)

Working title: **BeatHive** (placeholder; avoid "MusicBee" in the product name/branding).
Status: Draft v0.1

## 1. Goal

A fast, local-library music player and manager for large collections, inspired by MusicBee's
workflow: browse by tag, tag-edit, queue, play gapless. The MVP delivers the core
"manage and play a big local library" loop on Windows 10+ and Linux (X11/Wayland), from one codebase.

### Non-goals for MVP
Podcasts, radio/streaming services, CD ripping/burning, device sync (phones/iPods), plugins/scripting,
visualizers, lyrics fetching, last.fm scrobbling, skins/theme marketplace, DSP effects beyond EQ,
auto-tagging via online DBs, macOS (kept possible, not tested).

## 2. Users & Core Scenarios

1. Point the app at music folders; library scans and appears within seconds for 100k tracks.
2. Browse/filter by Artist / Album / Genre / Year; search instantly.
3. Double-click to play; queue tracks ("Play next", "Add to queue").
4. Gapless playback of albums; ReplayGain volume leveling.
5. Edit tags of one or many files; rename cover art.
6. Create and edit manual playlists; simple auto-playlists (rule-based).

## 3. Tech Stack

| Concern | Choice | Rationale |
|---|---|---|
| Language | C++23 | Requested C++; `std::expected`, ranges, `std::jthread` (MSVC 2022 17.6+, GCC 13+, Clang 17+) |
| Build | CMake (≥3.25) + vcpkg (manifest mode) | Same deps on both OSes |
| Compilers | MSVC 2022, GCC 13+, Clang 17+ | CI matrix |
| UI | **Qt 6 Widgets** (LGPL, dynamic link) | Mature desktop toolkit, model/view for huge tables, native look on both OSes. Qt Quick considered but Widgets' `QTableView` handles 100k+ rows better with less work |
| Decoding | **FFmpeg** (libavformat/libavcodec/libswresample) | Covers MP3, FLAC, AAC/M4A, OGG/Opus, WAV, WMA, ALAC, APE etc. |
| Audio output | **miniaudio** (single header) | WASAPI on Windows, PulseAudio/PipeWire/ALSA on Linux; low-latency callback API |
| Tags | **TagLib** | Read/write ID3v2, Vorbis, MP4, APE, embedded art |
| Database | **SQLite** (+ FTS5) via thin RAII wrapper | Single file, fast, portable |
| Testing | Catch2 (core), Qt Test (UI smoke) | |
| Packaging | Windows: NSIS/WiX installer + portable zip. Linux: AppImage + Flatpak | |

Licensing note: Qt via LGPL (dynamic), FFmpeg built LGPL-only (no GPL/nonfree codecs flags), TagLib LGPL,
miniaudio public domain/MIT-0, SQLite public domain. Project license TBD (GPL-3 or MIT both compatible).

## 4. Architecture

Strict layering; `core` has **no Qt UI dependency** (may use Qt Core only if wanted, otherwise std-only) so it can be tested headless.

```
┌────────────────────────── ui (Qt Widgets) ──────────────────────────┐
│ MainWindow · NavigationTree · TrackTableView · NowPlayingPanel      │
│ TagEditorDialog · PlaylistEditor · SettingsDialog · TransportBar    │
└───────────────▲─────────────────────────────────────────────────────┘
                │ signals/slots (queued across threads)
┌───────────────┴────────────── app (controllers) ────────────────────┐
│ LibraryController · PlaybackController · QueueModel · PlaylistSvc   │
└───▲───────────────▲───────────────────▲─────────────────────────────┘
    │               │                   │
┌───┴─────┐   ┌─────┴──────┐     ┌──────┴────────┐
│ library │   │  playback  │     │     tags      │
│ scanner │   │ engine     │     │ TagLib wrapper│
│ db/query│   │ decoder    │     │ cover art     │
│ watcher │   │ resampler  │     └───────────────┘
└─────────┘   │ EQ, RG     │
              │ output(ma) │
              └────────────┘
```

### Threads
- **UI thread**: all widgets, models.
- **Audio callback thread** (miniaudio): real-time safe — no allocation, no locks, no I/O. Reads from a lock-free SPSC ring buffer of float PCM.
- **Decoder thread**: fills the ring buffer; owns FFmpeg contexts; prefetches the next track for gapless.
- **Scanner thread pool**: file walk + tag read; batch-writes to SQLite in transactions.
- **DB access**: one writer connection (scanner/edit service), read connections per consumer; WAL mode.

## 5. MVP Features

### 5.1 Library
- Add/remove watched folders; initial scan and incremental rescan (mtime + size; hash not required).
- Live file watching (`QFileSystemWatcher` initially; `ReadDirectoryChangesW`/`inotify` direct if limits hit). Fallback: rescan on startup and via menu.
- Metadata stored per track: path, title, artist, album artist, album, track/disc no., year, genre, duration, bitrate, sample rate, channels, codec, rating (0–5), play count, last played, date added, ReplayGain track/album, embedded-art flag, file mtime/size.
- Missing/moved files flagged, not silently deleted (user can purge).
- Supported import formats: MP3, FLAC, OGG Vorbis, Opus, M4A (AAC/ALAC), WAV, WMA (read-only tags OK), APE/WV best-effort.
- Multi-value tags (multiple artists/genres) supported in storage as separators-normalized lists.

### 5.2 Browsing & Search
- Left navigation: **Library** (All Music), **Artists**, **Albums**, **Genres**, **Years**, **Recently Added**, **Recently Played**, **Playlists**, **Queue**.
- Optional 3-column "filter browser" (Genre | Artist | Album) above the track table (MusicBee signature); toggle on/off.
- Main view: virtualized track table with sortable, reorderable, resizable, show/hide columns; layout persisted.
- Album grid view with cover thumbnails (lazy loaded, disk-cached).
- Instant search box: FTS5 over title/artist/album/genre/path; results update as you type (<100 ms @ 100k tracks).
- Multi-select, drag-and-drop to playlists/queue, context menu (Play, Play Next, Add to Queue, Add to Playlist, Edit Tags, Show in File Manager, Remove from Library).

### 5.3 Playback
- Transport: play/pause, stop, prev/next, seek bar, volume, mute, shuffle, repeat (off/all/one).
- **Gapless** playback (decoder pre-opens next item; sample-accurate join using encoder delay/padding info where available).
- Optional crossfade (0–10 s), off by default.
- Sample-rate conversion to device rate via swresample; float32 internal pipeline.
- **ReplayGain** (track/album/off) read from tags; fall back to none. Pre-amp + clipping prevention.
- **10-band graphic equalizer** with presets, on/off (biquad IIR filters).
- Output device selection; default device-change handling (re-open stream on hot-plug).
- Media keys + OS integration: MPRIS2 over D-Bus (Linux), SMTC (Windows).
- Resume last track/position on launch (optional setting).

### 5.4 Queue ("Now Playing")
- Editable list: reorder via drag, remove, clear, Play Next / Add to End.
- Persisted across sessions.
- Shuffle operates on queue (shuffle-bag, no immediate repeats).

### 5.5 Playlists
- Manual playlists (create, rename, delete, reorder, duplicate entries allowed).
- Auto-playlists: rules over fields (genre, year, rating, play count, date added, last played, text contains) with AND/OR groups, optional limit + sort.
- Import/export M3U/M3U8 and PLS (relative paths supported).

### 5.6 Tag Editing
- Edit single or multiple selected tracks (batch editing with "keep existing" placeholders).
- Fields: title, artist, album artist, album, track/disc (+totals), year, genre, comment, composer, rating, ReplayGain (read-only in MVP).
- Cover art: view, replace from file, remove, apply to album.
- Writes via TagLib atomically (temp file + rename where the format allows); DB updated after success; errors per-file reported.
- Undo for the last tag edit batch (in-memory snapshot).

### 5.7 UI / Settings
- Layout: Navigation pane | Main view | Now Playing/art pane (collapsible); bottom transport bar; menu bar.
- Light/dark theme following the OS, plus a manual override. (Custom skins: post-MVP.)
- Configurable keyboard shortcuts (defaults for space, ←/→ seek, Ctrl+F search, etc.).
- Settings: library folders, audio device, playback (gapless, crossfade, ReplayGain), EQ, columns, startup behavior, language (English only at MVP, strings via `tr()` for later i18n).
- System tray icon with basic controls (optional per-OS).

## 6. Data Model (SQLite)

```sql
CREATE TABLE folder   (id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL);
CREATE TABLE track (
  id INTEGER PRIMARY KEY,
  folder_id INTEGER REFERENCES folder(id) ON DELETE CASCADE,
  path TEXT UNIQUE NOT NULL, mtime INTEGER, size INTEGER, missing INTEGER DEFAULT 0,
  title TEXT, artist TEXT, album_artist TEXT, album TEXT, genre TEXT, composer TEXT, comment TEXT,
  track_no INTEGER, track_total INTEGER, disc_no INTEGER, disc_total INTEGER, year INTEGER,
  duration_ms INTEGER, bitrate INTEGER, sample_rate INTEGER, channels INTEGER, codec TEXT,
  rg_track_gain REAL, rg_track_peak REAL, rg_album_gain REAL, rg_album_peak REAL,
  rating INTEGER DEFAULT 0, play_count INTEGER DEFAULT 0, last_played INTEGER, date_added INTEGER,
  art_id INTEGER REFERENCES art(id)
);
CREATE TABLE art      (id INTEGER PRIMARY KEY, hash TEXT UNIQUE, thumb_path TEXT);
CREATE TABLE playlist (id INTEGER PRIMARY KEY, name TEXT, kind INTEGER /*0 manual,1 auto*/, rules_json TEXT);
CREATE TABLE playlist_item (playlist_id INTEGER, position INTEGER, track_id INTEGER);
CREATE VIRTUAL TABLE track_fts USING fts5(title, artist, album, genre, path, content='track', content_rowid='id');
CREATE TABLE schema_version (v INTEGER);
```
Indexes on `(album_artist, album, disc_no, track_no)`, `genre`, `year`, `date_added`, `last_played`.
Migrations are numbered SQL scripts embedded in the binary.

Storage locations: Windows `%APPDATA%/BeatHive/`, Linux `$XDG_DATA_HOME/beathive/` (db, art cache) and `$XDG_CONFIG_HOME/beathive/` (settings.json).

## 7. Key Interfaces (sketch)

```cpp
namespace bh::playback {
struct AudioFormat { int sampleRate; int channels; };
class IDecoder {            // FFmpeg impl
 public:
  virtual ~IDecoder() = default;
  virtual std::expected<void, Error> open(const std::filesystem::path&) = 0;
  virtual AudioFormat format() const = 0;
  virtual std::size_t read(float* dst, std::size_t frames) = 0;   // 0 = EOF
  virtual bool seek(std::chrono::milliseconds) = 0;
  virtual std::chrono::milliseconds duration() const = 0;
};
class Engine {              // owns decoder thread + output
 public:
  void setQueueProvider(INextTrackProvider*);
  void play(TrackId); void pause(); void stop();
  void seek(std::chrono::milliseconds); void setVolume(float);
  Signal<State> stateChanged; Signal<Position> positionChanged; Signal<TrackId> trackChanged;
};
}
namespace bh::library {
class Scanner { void scan(std::span<const Path> roots, ScanOptions); /* emits progress */ };
class Query   { /* builds parameterized SQL from filters + FTS + sort */ };
}
```
Qt signals are used at the `app`/`ui` boundary; `core` exposes callbacks or a tiny signal type so it stays headless-testable.

## 8. Project Layout

```
/CMakeLists.txt  /vcpkg.json  /CMakePresets.json
/src/core/{playback,library,tags,playlist,util}
/src/app/            controllers, models (QAbstractItemModel subclasses)
/src/ui/             widgets, dialogs, resources (.qrc, icons)
/src/platform/{win,linux}/   media-session (SMTC/MPRIS), single-instance, paths
/tests/{unit,integration}  /tests/data/   small royalty-free sample files
/docs/  /packaging/{windows,linux}
```

## 9. Non-Functional Requirements

| Metric | Target (reference: mid-range 2020 laptop, SSD) |
|---|---|
| Cold start to usable UI | < 1.5 s (library of 100k tracks) |
| Initial scan | ≥ 300 tracks/s |
| Search latency | < 100 ms @ 100k tracks |
| Memory (idle, 100k tracks) | < 400 MB (thumbnails cached on disk, LRU in RAM) |
| Playback | No underruns under UI load; audio thread never blocks; < 100 ms start latency |
| Gapless | No audible gap on gapless-encoded albums (verified with test tone files) |
| Reliability | DB corruption-safe (WAL, integrity check on start, backup on migration); a bad file never crashes the scanner or player |
| Accessibility | Full keyboard navigation; screen reader names on controls (Qt a11y) |
| Unicode | UTF-8 internally; correct handling of non-ASCII Windows paths (`std::filesystem::path` / wide APIs) |

## 10. Testing & CI

- Unit tests: tag round-trips, query builder, auto-playlist rule engine, ring buffer, EQ filter coefficients, gapless join math, path normalization.
- Integration: scan fixture folder → expected DB rows; decode fixtures → checksum of PCM; headless playback to a null output device.
- GitHub Actions matrix: windows-latest (MSVC), ubuntu-latest (GCC + Clang); sanitizers (ASan/UBSan, TSan on playback) on Linux; clang-tidy + clang-format check.
- Manual QA checklist per release: device hot-plug, sleep/resume mid-playback, network-drive library, huge tags/art, files with broken headers.

## 11. Milestones

| # | Milestone | Deliverable |
|---|---|---|
| M0 | Skeleton (1 wk) | CMake+vcpkg builds on both OSes in CI; empty Qt window; test harness |
| M1 | Playback core (3 wks) | FFmpeg decode → miniaudio out; play/pause/seek/volume via CLI test app; ring buffer; device selection |
| M2 | Library (3 wks) | SQLite schema, scanner, TagLib reader, FTS search, track table UI with columns/sort |
| M3 | Player UI (2 wks) | Transport bar, queue, now playing + cover art, nav tree, filter browser |
| M4 | Gapless / RG / EQ (2 wks) | Gapless join, ReplayGain, 10-band EQ, crossfade |
| M5 | Playlists & tags (3 wks) | Manual + auto playlists, M3U import/export, tag editor incl. batch + art |
| M6 | OS integration & polish (2 wks) | MPRIS/SMTC, tray, file watching, settings, theming, shortcuts |
| M7 | Hardening & release (2 wks) | Perf pass on 100k+ library, installers/AppImage/Flatpak, docs, beta |

Rough total: ~18 weeks for one developer; M2/M3 can parallelize with a second.

## 12. Risks & Open Questions

1. **Gapless accuracy** across codecs (MP3 delay/padding via LAME/Xing header; AAC edit lists). Mitigation: test corpus, fall back to tight pre-buffering.
2. **Linux audio fragmentation** (PulseAudio vs PipeWire vs ALSA). miniaudio abstracts but needs testing on PipeWire-only systems.
3. **Wayland**: tray icons, global media keys, and window geometry restore vary; rely on MPRIS and StatusNotifier.
4. **File watching limits** (inotify watch counts on big trees) → periodic rescan fallback.
5. **FFmpeg licensing/size** on Windows; build minimal decoders-only via vcpkg features.
6. **WASAPI exclusive/bit-perfect & ASIO** — post-MVP; confirm whether audiophile features are priority.
7. Open: Qt Widgets vs Qt Quick (revisit if a modern animated UI becomes a goal).
8. Open: project name/license; whether to support portable mode (settings beside the exe) in MVP — recommended yes, cheap.

## 13. Post-MVP Backlog (priority order)

Smart-playlist enhancements & virtual tags → Lyrics (embedded + LRC sync) → Last.fm scrobbling →
Duplicate finder, auto-organize/rename files → Skins & layout customization → Visualizer →
Podcasts/radio → Device sync → Plugin API (C ABI or scripting) → WASAPI exclusive / ASIO / DSD → macOS build.

## 14. Definition of Done for MVP

A user on a clean Windows 11 or Ubuntu 24.04 machine can install the app, add a 50k-track folder,
browse and search it, play an album gapless with ReplayGain and EQ, manage a queue and playlists,
edit tags including cover art, and restart the app to find everything where they left it —
with all tests green in CI on both platforms.
