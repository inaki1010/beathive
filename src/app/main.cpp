#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "core/db/folder_repo.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_util.hpp"
#include "ui/theme.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("beathive");
  QApplication::setOrganizationName("beathive");
  QApplication::setApplicationDisplayName("BeatHive");
  QApplication::setApplicationVersion("0.1.0");
  QApplication::setDesktopFileName("beathive");
  QApplication::setWindowIcon(QIcon::fromTheme("beathive"));

  QCommandLineParser cli;
  cli.setApplicationDescription("BeatHive music library");
  cli.addHelpOption();
  cli.addVersionOption();
  QCommandLineOption dataDir("data-dir", "Where the library database and settings live.", "dir");
  QCommandLineOption addFolder("add-folder", "Add a music folder to the library (repeatable).", "dir");
  QCommandLineOption nullAudio("null-audio", "Play into a silent device.");
  QCommandLineOption screenshot("screenshot", "Scan, render the window to a PNG, and exit.", "file");
  QCommandLineOption size("size", "Window size, e.g. 1280x780.", "WxH");
  QCommandLineOption autoplay("autoplay-row", "Start playing the given row once the library is loaded.", "row");
  cli.addOptions({dataDir, addFolder, nullAudio, screenshot, size, autoplay});
  cli.process(app);

  const QString dir = cli.isSet(dataDir)
                          ? cli.value(dataDir)
                          : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  auto services = bh::ui::AppServices::create(bh::ui::to_path(dir), cli.isSet(nullAudio));
  if (!services) {
    qCritical("Cannot start: %s", services.error().message.c_str());
    return 1;
  }

  for (const auto& folder : cli.values(addFolder))
    if (auto r = bh::db::FolderRepo((*services)->db).add({{}, bh::ui::to_path(QDir(folder).absolutePath()), true, true}); !r)
      qWarning("Cannot add %s: %s", qPrintable(folder), r.error().message.c_str());

  QSettings settings(QDir(dir).filePath("settings.ini"), QSettings::IniFormat);
  bh::ui::apply_theme(app, bh::ui::theme_from_name(settings.value("view/theme", "dark").toString()));
  bh::ui::MainWindow window(**services, settings);
  if (cli.isSet(size)) {
    auto parts = cli.value(size).split('x');
    if (parts.size() == 2) window.resize(parts[0].toInt(), parts[1].toInt());
  }
  window.show();

  // Scan on startup (cheap when nothing changed).
  window.library().scan_all();

  if (cli.isSet(autoplay))
    QObject::connect(&window.library(), &bh::ui::LibraryService::scanFinished, &app,
                     [&window, row = cli.value(autoplay).toInt()] { window.play_row(row); });

  if (cli.isSet(screenshot)) {
    const QString out = cli.value(screenshot);
    const int settle = cli.isSet(autoplay) ? 1500 : 600;  // let playback progress before the capture
    QObject::connect(&window.library(), &bh::ui::LibraryService::scanFinished, &app, [&window, &app, out, settle] {
      QTimer::singleShot(settle, &app, [&window, &app, out] {
        window.grab().save(out);
        app.quit();
      });
    });
  }
  return app.exec();
}
