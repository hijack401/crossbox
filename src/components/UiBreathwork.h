#pragma once

#include <FreeInkApp.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace ui_breathwork {
namespace fui = freeink::ui;

namespace detail {

inline int16_t sine(uint16_t angle) {
  static constexpr int16_t QUARTER[] = {0,   49,  98,  147, 195, 243, 290, 337, 383, 428, 471,
                                        514, 556, 596, 634, 672, 707, 741, 773, 803, 831, 858,
                                        882, 904, 924, 942, 957, 970, 981, 989, 995, 999, 1000};
  angle &= 127;
  const uint8_t index = angle & 31;
  const int16_t value = QUARTER[angle & 32 ? 32 - index : index];
  return angle & 64 ? -value : value;
}

inline fui::Point point(fui::Rect area, int16_t x, int16_t y) {
  return fui::Point{static_cast<int16_t>(area.x + static_cast<int32_t>(area.width - 1) * x / 1000),
                    static_cast<int16_t>(area.y + static_cast<int32_t>(area.height - 1) * y / 1000)};
}

inline fui::Rect square(fui::Rect area) {
  const int16_t side = std::min(area.width, area.height);
  return fui::Rect{static_cast<int16_t>(area.x + (area.width - side) / 2),
                   static_cast<int16_t>(area.y + (area.height - side) / 2), side, side};
}

inline void line(fui::DrawTarget& target, fui::Point from, fui::Point to, uint8_t width, const fui::Paint& paint) {
  if (paint.kind != fui::PaintKind::Dither) {
    target.line(from, to, width, paint);
    return;
  }
  // The renderer's line primitive ignores dither; short pixel runs retain the quiet outer contours.
  const int16_t dx = std::abs(to.x - from.x);
  const int16_t dy = -std::abs(to.y - from.y);
  const int16_t sx = from.x < to.x ? 1 : -1;
  const int16_t sy = from.y < to.y ? 1 : -1;
  int32_t error = dx + dy;
  for (;;) {
    target.fill(
        fui::Rect{static_cast<int16_t>(from.x - width / 2), static_cast<int16_t>(from.y - width / 2), width, width},
        paint);
    if (from.x == to.x && from.y == to.y) break;
    const int32_t twice = error * 2;
    if (twice >= dy) {
      error += dy;
      from.x += sx;
    }
    if (twice <= dx) {
      error += dx;
      from.y += sy;
    }
  }
}

inline void contour(fui::DrawTarget& target, fui::Rect area, int16_t radius, uint8_t offset, uint8_t weight,
                    const fui::Paint& ink, bool organic = true) {
  fui::Point previous{};
  for (uint16_t step = 0; step <= 128; ++step) {
    const int16_t ripple = organic ? sine(step * 3 + offset) * radius / 32000 : 0;
    const int16_t x = 500 + sine(step + 32) * (radius + ripple) / 1000;
    const int16_t y = 500 + sine(step) * (radius + ripple) / 1000;
    const auto current = point(area, x, y);
    if (step) line(target, previous, current, weight, ink);
    previous = current;
  }
}

inline void curve(fui::DrawTarget& target, fui::Rect area, fui::Point a, fui::Point b, fui::Point c, fui::Point d,
                  uint8_t weight, const fui::Paint& ink) {
  static constexpr int32_t STEPS = 24;
  static constexpr int32_t CUBE = STEPS * STEPS * STEPS;
  auto previous = point(area, a.x, a.y);
  for (int32_t step = 1; step <= STEPS; ++step) {
    const int32_t rest = STEPS - step;
    const int16_t x = (rest * rest * rest * a.x + 3 * rest * rest * step * b.x + 3 * rest * step * step * c.x +
                       step * step * step * d.x) /
                      CUBE;
    const int16_t y = (rest * rest * rest * a.y + 3 * rest * rest * step * b.y + 3 * rest * step * step * c.y +
                       step * step * step * d.y) /
                      CUBE;
    const auto current = point(area, x, y);
    line(target, previous, current, weight, ink);
    previous = current;
  }
}

inline void bloom(fui::DrawTarget& target, fui::Rect area, uint8_t weight, const fui::Paint& ink) {
  curve(target, area, {500, 810}, {260, 580}, {370, 330}, {500, 120}, weight, ink);
  curve(target, area, {500, 120}, {630, 330}, {740, 580}, {500, 810}, weight, ink);
  curve(target, area, {500, 810}, {180, 830}, {90, 600}, {110, 350}, weight, ink);
  curve(target, area, {110, 350}, {400, 410}, {480, 590}, {500, 810}, weight, ink);
  curve(target, area, {500, 810}, {820, 830}, {910, 600}, {890, 350}, weight, ink);
  curve(target, area, {890, 350}, {600, 410}, {520, 590}, {500, 810}, weight, ink);
}

}  // namespace detail

inline void guide(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, uint16_t expansionPermille,
                  bool paused = false) {
  if (std::min(area.width, area.height) < 40) return;
  const auto box = detail::square(area);
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto quiet =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const uint16_t expansion = std::min<uint16_t>(1000, expansionPermille);
  const uint8_t weight = std::clamp<int16_t>(box.width / 140, 1, 3);
  // Every phase leaves the middle 42% clear for the instruction and count.
  for (uint8_t ring = 0; ring < 4; ++ring) {
    const int16_t radius = 225 + ring * 28 + expansion * (80 + ring * 18) / 1000;
    detail::contour(target, box, radius, ring * 7, ring == 1 ? 1 : weight, paused || ring >= 2 ? quiet : ink);
  }
}

inline void presetIcon(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, uint8_t presetIndex) {
  if (std::min(area.width, area.height) < 12) return;
  const auto box = detail::square(area);
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const uint8_t weight = std::clamp<int16_t>(box.width / 30, 1, 3);
  switch (presetIndex) {
    case 0:
      detail::contour(target, box, 390, 0, weight, ink, false);
      detail::contour(target, box, 230, 0, weight, ink, false);
      break;
    case 1:
      for (uint8_t row = 0; row < 3; ++row) {
        const int16_t y = 300 + row * 200;
        detail::curve(target, box, {110, y}, {370, static_cast<int16_t>(y - 220)}, {570, static_cast<int16_t>(y + 220)},
                      {890, y}, weight, ink);
      }
      break;
    case 2:
      detail::line(target, detail::point(box, 180, 180), detail::point(box, 820, 180), weight, ink);
      detail::line(target, detail::point(box, 820, 180), detail::point(box, 820, 820), weight, ink);
      detail::line(target, detail::point(box, 820, 820), detail::point(box, 180, 820), weight, ink);
      detail::line(target, detail::point(box, 180, 820), detail::point(box, 180, 180), weight, ink);
      for (uint8_t corner = 0; corner < 4; ++corner) {
        const auto position = detail::point(box, corner & 1 ? 820 : 180, corner & 2 ? 820 : 180);
        const int16_t size = std::max<int16_t>(2, box.width / 10);
        target.fill(fui::Rect{static_cast<int16_t>(position.x - size / 2), static_cast<int16_t>(position.y - size / 2),
                              size, size},
                    ink);
      }
      break;
    default:
      detail::bloom(target, box, weight, ink);
      break;
  }
}

inline void completion(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  if (std::min(area.width, area.height) < 20) return;
  const auto box = detail::square(area);
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto quiet =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const uint8_t weight = std::clamp<int16_t>(box.width / 100, 1, 3);
  detail::bloom(target, box, weight, ink);
  detail::curve(target, box, {260, 890}, {420, 940}, {580, 940}, {740, 890}, 1, quiet);
}

}  // namespace ui_breathwork
