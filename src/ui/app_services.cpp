#include "ui/app_services.hpp"

#include "core/db/track_repo.hpp"

namespace bh::ui {

AppServices::AppServices(std::filesystem::path dir, db::Database ui_db, db::Database pb_db,
                         std::unique_ptr<playback::IAudioOutput> out)
    : data_dir(std::move(dir)),
      db_path(data_dir / "library.db"),
      db(std::move(ui_db)),
      playback_db(std::move(pb_db)),
      engine(std::move(out)),
      controller(engine, [this](TrackId id) -> std::optional<std::filesystem::path> {
        auto t = db::TrackRepo(playback_db).get(id);
        if (!t || !*t || (*t)->file.missing) return std::nullopt;
        return (*t)->file.path;
      }) {}

Result<std::unique_ptr<AppServices>> AppServices::create(const std::filesystem::path& data_dir, bool null_audio) {
  std::error_code ec;
  std::filesystem::create_directories(data_dir, ec);
  if (ec) return std::unexpected(Error{ErrorCode::Io, "cannot create " + data_dir.string() + ": " + ec.message()});

  auto path = data_dir / "library.db";
  auto ui = db::Database::open(path);
  BH_TRY(ui);
  auto pb = db::Database::open(path);
  BH_TRY(pb);
  return std::unique_ptr<AppServices>(
      new AppServices(data_dir, std::move(*ui), std::move(*pb), playback::make_miniaudio_output(null_audio)));
}

}  // namespace bh::ui
