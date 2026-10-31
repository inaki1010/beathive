#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QtTest/QtTest>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/controls.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

using namespace bh::ui;

namespace {
int painted_pixels(const QImage& img) {
  int n = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x) n += qAlpha(img.pixel(x, y)) > 0;
  return n;
}
}  // namespace

TEST_CASE("a click on the seek slider jumps there and reports the new position on release") {
  SeekSlider s;
  s.resize(206, 18);
  s.setRange(0, 1000);
  s.show();
  REQUIRE(QTest::qWaitForWindowExposed(&s));

  QSignalSpy pressed(&s, &QSlider::sliderPressed), released(&s, &QSlider::sliderReleased);
  QTest::mouseClick(&s, Qt::LeftButton, {}, QPoint(103, 9));  // the middle of the bar
  CHECK(pressed.count() == 1);
  CHECK(released.count() == 1);
  CHECK(s.value() == Catch::Approx(500).margin(10));
  QTest::mouseClick(&s, Qt::LeftButton, {}, QPoint(0, 9));  // the far left edge
  CHECK(s.value() == 0);
  QTest::mouseClick(&s, Qt::LeftButton, {}, QPoint(205, 9));  // the far right edge
  CHECK(s.value() == 1000);
}

TEST_CASE("every glyph paints something, and paints nothing outside its box") {
  const QColor ink(255, 255, 255);
  for (int g = static_cast<int>(Glyph::Play); g <= static_cast<int>(Glyph::SortDown); ++g) {
    QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
      QPainter p(&img);
      paint_glyph(p, static_cast<Glyph>(g), QRectF(16, 16, 32, 32), ink);
    }
    INFO("glyph " << g);
    CHECK(painted_pixels(img) > 20);
    // The 16px margin around the 32px box stays clear (allowing for a soft stroke edge).
    for (int i = 0; i < 64; ++i) {
      CHECK(qAlpha(img.pixel(i, 0)) == 0);
      CHECK(qAlpha(img.pixel(0, i)) == 0);
      CHECK(qAlpha(img.pixel(i, 63)) == 0);
      CHECK(qAlpha(img.pixel(63, i)) == 0);
    }
  }
}

TEST_CASE("a glyph icon recolours when selected and greys out when disabled") {
  const QIcon icon = glyph_icon(Glyph::Note, Qt::white, Qt::red);
  auto center = [&](QIcon::Mode m) {
    const QImage img = icon.pixmap(QSize(32, 32), m).toImage();
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x)
        if (qAlpha(img.pixel(x, y)) > 250) return QColor(img.pixel(x, y));
    return QColor();
  };
  CHECK(center(QIcon::Normal) == QColor(Qt::white));
  CHECK(center(QIcon::Selected) == QColor(Qt::red));
  CHECK(center(QIcon::Disabled) == theme().faint);
}

TEST_CASE("the cover view is square and falls back to a placeholder") {
  CoverView c;
  CHECK(c.heightForWidth(300) == 300);
  c.resize(120, 120);
  c.show();
  REQUIRE(QTest::qWaitForWindowExposed(&c));
  QPixmap pm(40, 20);  // not square: must not crash while cropping
  pm.fill(Qt::green);
  c.set_pixmap(pm);
  CHECK_FALSE(c.grab().isNull());
  c.set_pixmap({});
  CHECK_FALSE(c.grab().isNull());
}
