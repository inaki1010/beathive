#include "ui/theme.hpp"

#include <QApplication>
#include <QMap>
#include <QPalette>
#include <QStyleFactory>

namespace bh::ui {

namespace {

Theme g_theme = Theme::dark();

QString css(const QColor& c) {
  return c.alpha() == 255 ? c.name() : QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString stylesheet(const Theme& t) {
  QMap<QString, QString> v{
      {"bg", css(t.bg)},         {"panel", css(t.panel)},   {"base", css(t.base)},       {"alt", css(t.alt)},
      {"raised", css(t.raised)}, {"border", css(t.border)}, {"text", css(t.text)},       {"dim", css(t.dim)},
      {"faint", css(t.faint)},   {"accent", css(t.accent)}, {"accent_text", css(t.accent_text)},
      {"hover", css(t.hover)},   {"tint", css(t.accent_tint)},
      {"scroll", css(t.kind == ThemeKind::Dark ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 46))},
      {"scroll_hot", css(t.kind == ThemeKind::Dark ? QColor(255, 255, 255, 80) : QColor(0, 0, 0, 100))},
      {"groove", css(t.kind == ThemeKind::Dark ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 40))},
  };

  QString s = R"(
QMainWindow, QDialog { background: @bg@; }
QToolTip { background: @raised@; color: @text@; border: 1px solid @border@; padding: 4px 8px; }

QMenuBar { background: @bg@; color: @dim@; padding: 3px 8px 0 8px; border: none; }
QMenuBar::item { background: transparent; padding: 5px 11px; border-radius: 6px; }
QMenuBar::item:selected { background: @hover@; color: @text@; }
QMenu { background: @raised@; color: @text@; border: 1px solid @border@; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 14px; border-radius: 5px; }
QMenu::item:selected { background: @tint@; }
QMenu::item:disabled { color: @faint@; }
QMenu::separator { height: 1px; background: @border@; margin: 5px 8px; }

#topbar { background: @bg@; }
#brand { color: @text@; font-size: 17px; font-weight: 700; }
QLineEdit#search_box { background: @raised@; color: @text@; border: 1px solid @border@; border-radius: 17px;
                       padding: 6px 10px; selection-background-color: @accent@; selection-color: @accent_text@; }
QLineEdit#search_box:focus { border-color: @accent@; }

#sidebar, QTreeWidget#nav { background: @panel@; border: none; outline: 0; }
QTreeWidget#nav::item { height: 32px; margin: 1px 10px; padding-left: 4px; border-radius: 7px; color: @dim@; }
QTreeWidget#nav::item:hover { background: @hover@; color: @text@; }
QTreeWidget#nav::item:selected { background: @tint@; color: @text@; }

QTableView, QListWidget { background: @base@; alternate-background-color: @alt@; border: none; outline: 0; color: @text@; }
QListWidget::item { padding: 4px 10px; color: @dim@; border: none; }
QListWidget::item:hover { background: @hover@; color: @text@; }
QListWidget::item:selected { background: @tint@; color: @text@; }
QListWidget::item:selected:!active { background: @tint@; color: @text@; }
#column_title { background: @panel@; color: @faint@; font-size: 11px; font-weight: 700; padding: 7px 10px 6px 10px;
                border-bottom: 1px solid @border@; }
#section_title { color: @faint@; font-size: 11px; font-weight: 700; }
#np_title { color: @text@; font-size: 18px; font-weight: 700; }
#np_artist { color: @text@; font-size: 14px; }
#np_album { color: @dim@; font-size: 13px; }
#now_playing, #queue_box { background: @panel@; }
#empty_state { background: @base@; }
#queue_head { background: @panel@; border-top: 1px solid @border@; }
QLabel#badge { background: @raised@; color: @dim@; border: 1px solid @border@; border-radius: 9px; padding: 2px 8px;
               font-size: 11px; font-weight: 600; }
QLabel#empty_title { color: @text@; font-size: 18px; font-weight: 600; }
QLabel#empty_hint { color: @dim@; font-size: 13px; }

#transport { background: @panel@; border-top: 1px solid @border@; }
#tp_title { color: @text@; font-size: 14px; font-weight: 600; }
#tp_artist { color: @dim@; font-size: 12px; }
#tp_time { color: @dim@; font-size: 11px; }
QSlider { background: transparent; }
QSlider::groove:horizontal { height: 4px; background: @groove@; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent@; border-radius: 2px; }
QSlider::sub-page:horizontal:disabled { background: transparent; }
QSlider::handle:horizontal { background: transparent; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
QSlider[hot="true"]::handle:horizontal { background: @text@; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: @scroll@; border-radius: 4px; min-height: 36px; margin: 2px 2px 2px 2px; }
QScrollBar::handle:vertical:hover { background: @scroll_hot@; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: @scroll@; border-radius: 4px; min-width: 36px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: @scroll_hot@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSplitter::handle:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @bg@, stop:0.4 @bg@, stop:0.5 @border@, stop:0.6 @bg@, stop:1 @bg@); }
QSplitter::handle:vertical { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @bg@, stop:0.4 @bg@, stop:0.5 @border@, stop:0.6 @bg@, stop:1 @bg@); }

QPushButton { background: @raised@; color: @text@; border: 1px solid @border@; border-radius: 7px; padding: 6px 16px; }
QPushButton:hover { border-color: @dim@; }
QPushButton:pressed { background: @hover@; }
QPushButton:disabled { color: @faint@; }
QPushButton[accent="true"] { background: @accent@; color: @accent_text@; border: 1px solid @accent@; font-weight: 600; }
QPushButton[accent="true"]:hover { background: @accent@; border-color: @text@; }
QPushButton#flat { background: transparent; border: none; color: @dim@; padding: 3px 8px; }
QPushButton#flat:hover { color: @text@; background: @hover@; }

QStatusBar { background: @bg@; color: @dim@; border-top: 1px solid @border@; font-size: 12px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @dim@; padding: 3px 14px; font-size: 12px; }
QProgressBar { background: @groove@; border: none; border-radius: 2px; max-height: 4px; }
QProgressBar::chunk { background: @accent@; border-radius: 2px; }
)";
  for (auto it = v.cbegin(); it != v.cend(); ++it) s.replace('@' + it.key() + '@', it.value());
  return s;
}

}  // namespace

Theme Theme::dark() {
  Theme t;
  t.kind = ThemeKind::Dark;
  t.bg = "#16171a";
  t.panel = "#1b1c20";
  t.base = "#1f2025";
  t.alt = "#222328";
  t.raised = "#2a2c32";
  t.border = "#2d2f36";
  t.text = "#e9e9ec";
  t.dim = "#9b9da8";
  t.faint = "#666875";
  t.accent = "#f2a93b";
  t.accent_text = "#1b1407";
  t.hover = QColor(255, 255, 255, 15);
  t.accent_tint = QColor(242, 169, 59, 48);
  return t;
}

Theme Theme::light() {
  Theme t;
  t.kind = ThemeKind::Light;
  t.bg = "#f1efeb";
  t.panel = "#e9e6e1";
  t.base = "#ffffff";
  t.alt = "#f8f7f4";
  t.raised = "#ffffff";
  t.border = "#d8d4cc";
  t.text = "#1e1f24";
  t.dim = "#5d6069";
  t.faint = "#9a9ca5";
  t.accent = "#d6850a";
  t.accent_text = "#ffffff";
  t.hover = QColor(0, 0, 0, 14);
  t.accent_tint = QColor(214, 133, 10, 44);
  return t;
}

const Theme& theme() { return g_theme; }

ThemeNotifier& ThemeNotifier::instance() {
  static ThemeNotifier n;
  return n;
}

QString theme_name(ThemeKind k) { return k == ThemeKind::Light ? "light" : "dark"; }
ThemeKind theme_from_name(const QString& n) { return n == "light" ? ThemeKind::Light : ThemeKind::Dark; }

void apply_theme(QApplication& app, ThemeKind kind) {
  g_theme = kind == ThemeKind::Light ? Theme::light() : Theme::dark();
  const Theme& t = g_theme;

  if (auto* fusion = QStyleFactory::create("Fusion")) app.setStyle(fusion);
  QPalette p;
  p.setColor(QPalette::Window, t.bg);
  p.setColor(QPalette::WindowText, t.text);
  p.setColor(QPalette::Base, t.base);
  p.setColor(QPalette::AlternateBase, t.alt);
  p.setColor(QPalette::Text, t.text);
  p.setColor(QPalette::Button, t.raised);
  p.setColor(QPalette::ButtonText, t.text);
  p.setColor(QPalette::BrightText, t.text);
  p.setColor(QPalette::ToolTipBase, t.raised);
  p.setColor(QPalette::ToolTipText, t.text);
  p.setColor(QPalette::PlaceholderText, t.faint);
  p.setColor(QPalette::Highlight, t.accent);
  p.setColor(QPalette::HighlightedText, t.accent_text);
  p.setColor(QPalette::Link, t.accent);
  p.setColor(QPalette::Light, t.border.lighter(120));
  p.setColor(QPalette::Midlight, t.border);
  p.setColor(QPalette::Mid, t.border);
  p.setColor(QPalette::Dark, t.border.darker(130));
  p.setColor(QPalette::Shadow, Qt::black);
  for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
    p.setColor(QPalette::Disabled, role, t.faint);
  app.setPalette(p);
  app.setStyleSheet(stylesheet(t));
  emit ThemeNotifier::instance().changed();
}

}  // namespace bh::ui
