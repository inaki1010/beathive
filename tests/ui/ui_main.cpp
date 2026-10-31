#include <QApplication>
#include <catch2/catch_session.hpp>

// The UI tests need a QApplication; run headless unless the caller chose a platform.
int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  return Catch::Session().run(argc, argv);
}
