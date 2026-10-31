#pragma once
#include <QColor>
#include <QObject>
#include <QString>

class QApplication;

namespace bh::ui {

enum class ThemeKind { Dark, Light };

// One set of colour tokens drives the palette, the stylesheet and all custom painting.
struct Theme {
  ThemeKind kind = ThemeKind::Dark;
  QColor bg;           // window chrome
  QColor panel;        // sidebar, headers, bars
  QColor base;         // list and table background
  QColor alt;          // alternate row
  QColor raised;       // inputs, buttons, menus
  QColor border;
  QColor text;
  QColor dim;          // secondary text
  QColor faint;        // tertiary text, disabled
  QColor accent;
  QColor accent_text;  // text drawn on top of the accent colour
  QColor hover;        // translucent overlay for hovered rows and buttons
  QColor accent_tint;  // translucent accent for selections

  static Theme dark();
  static Theme light();
};

const Theme& theme();
void apply_theme(QApplication& app, ThemeKind kind);

QString theme_name(ThemeKind kind);
ThemeKind theme_from_name(const QString& name);

// Emitted after the theme changes so widgets that cache colours or icons can refresh.
class ThemeNotifier : public QObject {
  Q_OBJECT
 public:
  static ThemeNotifier& instance();
 signals:
  void changed();
};

}  // namespace bh::ui
