#pragma once

#include <FreeInkApp.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "fonts/BalanceDigits.h"
#include "util/RouletteGame.h"

namespace ui_roulette {
namespace fui = freeink::ui;
namespace detail {

inline constexpr uint8_t POCKETS[] = {0, 32, 15, 19, 4, 21, 2,  25, 17, 34, 6,  27, 13, 36, 11, 30, 8, 23, 10,
                                      5, 24, 16, 33, 1, 20, 14, 31, 9,  22, 18, 29, 7,  28, 12, 35, 3, 26};

// Pocket boundaries, clockwise from half a pocket before twelve o'clock, scaled by 1024.
inline constexpr int16_t RIM[][2] = {
    {-87, -1020}, {87, -1020},  {258, -991},  {422, -933},  {573, -848},  {709, -739},   {823, -609},  {914, -461},
    {979, -300},  {1016, -130}, {1023, 43},   {1001, 216},  {950, 382},   {872, 537},    {769, 677},   {643, 797},
    {499, 894},   {341, 966},   {173, 1009},  {0, 1024},    {-173, 1009}, {-341, 966},   {-499, 894},  {-643, 797},
    {-769, 677},  {-872, 537},  {-950, 382},  {-1001, 216}, {-1023, 43},  {-1016, -130}, {-979, -300}, {-914, -461},
    {-823, -609}, {-709, -739}, {-573, -848}, {-422, -933}, {-258, -991}};

inline fui::Point rimPoint(fui::Point center, int16_t radius, uint8_t index) {
  return fui::Point{static_cast<int16_t>(center.x + RIM[index][0] * radius / 1024),
                    static_cast<int16_t>(center.y + RIM[index][1] * radius / 1024)};
}

inline fui::Point pocketPoint(fui::Point center, int16_t radius, uint8_t index) {
  const uint8_t next = (index + 1) % 37;
  return fui::Point{static_cast<int16_t>(center.x + (RIM[index][0] + RIM[next][0]) * radius / 2048),
                    static_cast<int16_t>(center.y + (RIM[index][1] + RIM[next][1]) * radius / 2048)};
}

// Fixed four-edge scanlines avoid the renderer's heap-backed polygon path.
inline void sector(fui::DrawTarget& target, const fui::Point (&points)[4], const fui::Paint& paint) {
  int16_t top = points[0].y;
  int16_t bottom = top;
  for (uint8_t i = 1; i < 4; ++i) {
    top = std::min(top, points[i].y);
    bottom = std::max(bottom, points[i].y);
  }
  for (int16_t y = top; y < bottom; ++y) {
    int16_t left = INT16_MAX;
    int16_t right = INT16_MIN;
    for (uint8_t i = 0; i < 4; ++i) {
      const auto a = points[i];
      const auto b = points[(i + 1) % 4];
      if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
        const int16_t x = a.x + static_cast<int32_t>(y - a.y) * (b.x - a.x) / (b.y - a.y);
        left = std::min(left, x);
        right = std::max(right, x);
      }
    }
    if (left <= right) target.fill(fui::Rect{left, y, static_cast<int16_t>(right - left + 1), 1}, paint);
  }
}

inline void circle(fui::DrawTarget& target, fui::Point center, int16_t radius, const fui::Paint& ink,
                   uint8_t width = 1) {
  if (radius < 1) return;
  target.stroke(fui::Rect{static_cast<int16_t>(center.x - radius), static_cast<int16_t>(center.y - radius),
                          static_cast<int16_t>(radius * 2 + 1), static_cast<int16_t>(radius * 2 + 1)},
                ink, width, static_cast<uint8_t>(std::min<int16_t>(radius, 255)));
}

inline void disc(fui::DrawTarget& target, fui::Point center, int16_t radius, const fui::Paint& ink) {
  if (radius < 1) return;
  target.fill(fui::Rect{static_cast<int16_t>(center.x - radius), static_cast<int16_t>(center.y - radius),
                        static_cast<int16_t>(radius * 2 + 1), static_cast<int16_t>(radius * 2 + 1)},
              ink, static_cast<uint8_t>(std::min<int16_t>(radius, 255)));
}

// Preserve thin stems when a pocket figure is much smaller than its source glyph.
inline void engravedGlyph(fui::DrawTarget& target, fui::Rect area, const balance_font::Glyph& glyph,
                          const fui::Paint& ink) {
  if (area.empty()) return;
  const uint8_t* bitmap = balance_font::BITMAPS + glyph.offset;
  const int stride = (glyph.width + 7) / 8;
  for (int y = 0; y < area.height; ++y) {
    const int top = y * glyph.height / area.height;
    const int bottom = ((y + 1) * glyph.height + area.height - 1) / area.height;
    int run = -1;
    for (int x = 0; x <= area.width; ++x) {
      bool marked = false;
      if (x < area.width) {
        const int left = x * glyph.width / area.width;
        const int right = ((x + 1) * glyph.width + area.width - 1) / area.width;
        for (int sy = top; sy < bottom && !marked; ++sy)
          for (int sx = left; sx < right; ++sx)
            if (bitmap[sy * stride + sx / 8] & (0x80 >> (sx % 8))) {
              marked = true;
              break;
            }
      }
      if (marked && run < 0) run = x;
      if (!marked && run >= 0) {
        target.fill(fui::Rect{static_cast<int16_t>(area.x + run), static_cast<int16_t>(area.y + y),
                              static_cast<int16_t>(x - run), 1},
                    ink);
        run = -1;
      }
    }
  }
}

// Reuse the existing proportional figure subset at the pocket's engraved scale.
inline void number(fui::DrawTarget& target, fui::Rect area, uint8_t value, const fui::Paint& ink) {
  namespace font = balance_font;
  if (value > 36 || area.empty()) return;
  const uint8_t first = value < 10 ? value : value / 10;
  const uint8_t last = value % 10;
  const auto& a = font::GLYPHS[first];
  const auto& b = font::GLYPHS[last];
  const int width = value < 10 ? a.width : a.advance + b.left + b.width - a.left;
  const int height = std::min<int>({area.height, font::HEIGHT, area.width * font::HEIGHT / width});
  if (height < 1) return;
  const int x = area.x + (area.width - width * height / font::HEIGHT) / 2;
  const int y = area.y + (area.height - height) / 2;
  for (uint8_t i = 0; i < (value < 10 ? 1 : 2); ++i) {
    const auto& glyph = i ? b : a;
    const int advance = i ? a.advance : 0;
    const int left = (advance + glyph.left - a.left) * height / font::HEIGHT;
    const int top = glyph.top * height / font::HEIGHT;
    const int right = (advance + glyph.left + glyph.width - a.left) * height / font::HEIGHT;
    const int bottom = (glyph.top + glyph.height) * height / font::HEIGHT;
    const fui::Rect figure{static_cast<int16_t>(x + left), static_cast<int16_t>(y + top),
                           static_cast<int16_t>(right - left), static_cast<int16_t>(bottom - top)};
    if (height < font::HEIGHT / 2)
      engravedGlyph(target, figure, glyph, ink);
    else {
      const fui::BitmapRef bitmap{font::BITMAPS + glyph.offset, glyph.width, glyph.height, fui::BitmapFormat::BW1,
                                  true};
      target.bitmap(figure, bitmap, fui::BitmapMode::Stretch, ink);
    }
  }
}

inline void ring(fui::DrawTarget& target, fui::Point center, int16_t outer, int16_t inner,
                 const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto red =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const int16_t labelRadius = (outer + inner) / 2;
  // Circumferential spacing, rather than pocket depth, limits unrotated labels.
  const int16_t pitch = labelRadius * 6 / 37;
  const int16_t figures = pitch * 3 / 5;
  const bool numbered = figures >= 10;
  for (uint8_t i = 0; i < 37; ++i) {
    const uint8_t next = (i + 1) % 37;
    const auto outerStart = rimPoint(center, outer, i);
    const auto outerEnd = rimPoint(center, outer, next);
    const auto innerStart = rimPoint(center, inner, i);
    const auto innerEnd = rimPoint(center, inner, next);
    const bool black = POCKETS[i] && !RouletteGame::isRed(POCKETS[i]);
    const fui::Point points[] = {outerStart, outerEnd, innerEnd, innerStart};
    sector(target, points, POCKETS[i] == 0 ? paper : black ? ink : red);
    target.line(outerStart, outerEnd, 1, ink);
    target.line(innerStart, innerEnd, 1, ink);
    target.line(innerStart, outerStart, 1, POCKETS[i] == 0 ? ink : paper);
  }
  if (!numbered) return;
  for (uint8_t i = 0; i < 37; ++i) {
    const bool black = POCKETS[i] && !RouletteGame::isRed(POCKETS[i]);
    const auto position = pocketPoint(center, labelRadius, i);
    number(target,
           fui::Rect{static_cast<int16_t>(position.x - figures * 3 / 5), static_cast<int16_t>(position.y - figures / 2),
                     static_cast<int16_t>(figures * 6 / 5), figures},
           POCKETS[i], black ? paper : ink);
  }
}

inline void ball(fui::DrawTarget& target, fui::Point position, int16_t size, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  disc(target, fui::Point{static_cast<int16_t>(position.x + 2), static_cast<int16_t>(position.y + 2)}, size, ink);
  disc(target, position, size, paper);
  circle(target, position, size, ink);
  if (size > 4)
    target.fill(fui::Rect{static_cast<int16_t>(position.x + 1), static_cast<int16_t>(position.y + 1), 2, 2}, ink);
}

inline void spindle(fui::DrawTarget& target, fui::Point center, int16_t radius, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t index = 5 + i * 9;
    const auto tip = rimPoint(center, radius, index);
    target.line(center, tip, std::max<int16_t>(2, radius / 6), ink);
    target.line(center, tip, 1, paper);
    disc(target, tip, std::max<int16_t>(2, radius / 9), ink);
  }
  disc(target, center, std::max<int16_t>(4, radius / 4), paper);
  circle(target, center, std::max<int16_t>(4, radius / 4), ink, 2);
  disc(target, center, std::max<int16_t>(1, radius / 12), ink);
}

}  // namespace detail

inline void wheel(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, bool revealed = false,
                  uint8_t result = 0, bool inPlay = false) {
  const int16_t side = std::min(area.width, area.height);
  if (side < theme.minTouchSize) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto engraving =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const fui::Point center{static_cast<int16_t>(area.x + area.width / 2),
                          static_cast<int16_t>(area.y + area.height / 2)};
  const int16_t radius = (side - 1) / 2 - theme.spaceSm;
  const int16_t rim = std::max<int16_t>(4, radius / 13);
  const int16_t outer = radius - rim;
  const int16_t inner = outer * 2 / 3;
  if (inner < 6) return;
  detail::circle(target, center, radius, ink, 2);
  detail::circle(target, center, radius - 3, ink);
  detail::circle(target, center, outer + 2, ink);
  for (uint8_t i = 0; i < 37; ++i)
    target.line(detail::rimPoint(center, outer + 4, i), detail::rimPoint(center, radius - 4, i), 1, engraving);
  detail::ring(target, center, outer, inner, theme);
  detail::circle(target, center, inner - 3, ink);
  detail::circle(target, center, inner - 6, engraving);
  for (uint8_t i = 0; i < 37; i += 2)
    target.line(detail::pocketPoint(center, inner - 7, i), detail::pocketPoint(center, inner / 2, i), 1, engraving);
  detail::circle(target, center, inner / 2, ink);

  if (revealed && result <= 36) {
    const int16_t plaque = inner * 3 / 5;
    detail::disc(target, center, plaque, paper);
    detail::circle(target, center, plaque, ink, 2);
    detail::circle(target, center, plaque - 4, ink);
    const int16_t line = target.lineHeight(theme.smallText.font);
    const int16_t figures = std::min<int16_t>(plaque, balance_font::HEIGHT);
    const bool withColor = plaque * 2 > figures + line + theme.spaceMd;
    const int16_t top = center.y - (figures + (withColor ? line : 0)) / 2;
    detail::number(target,
                   fui::Rect{static_cast<int16_t>(center.x - plaque + theme.spaceSm), top,
                             static_cast<int16_t>(plaque * 2 - theme.spaceSm * 2), figures},
                   result, ink);
    if (withColor) {
      auto text = theme.smallText;
      text.align = fui::TextAlign::Center;
      text.bold = true;
      target.text(
          fui::Rect{static_cast<int16_t>(center.x - plaque + theme.spaceSm), static_cast<int16_t>(top + figures),
                    static_cast<int16_t>(plaque * 2 - theme.spaceSm * 2), line},
          result == 0                   ? tr(STR_ROULETTE_ZERO)
          : RouletteGame::isRed(result) ? tr(STR_ROULETTE_RED)
                                        : tr(STR_ROULETTE_BLACK),
          text);
    }
    for (uint8_t i = 0; i < 37; ++i) {
      if (detail::POCKETS[i] != result) continue;
      // The ball sits inside its pocket, below the number printed on the rim.
      detail::ball(target, detail::pocketPoint(center, inner + (outer - inner) / 6, i), std::max<int16_t>(3, rim / 2),
                   theme);
      target.line(detail::rimPoint(center, radius + 1, i), detail::rimPoint(center, radius + 1, (i + 1) % 37), 3, ink);
      break;
    }
  } else {
    detail::spindle(target, center, inner / 2 - 3, theme);
    if (inPlay)
      detail::ball(target, detail::pocketPoint(center, radius - rim / 2, 4), std::max<int16_t>(3, rim / 2), theme);
  }
}

inline void caption(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, const char* label) {
  auto style = theme.smallText;
  style.align = fui::TextAlign::Center;
  const int16_t width = target.measureText(style.font, label, style).width;
  const int16_t end = (area.width - width) / 2 - theme.spaceMd;
  if (end > theme.spaceMd) {
    const auto ink = fui::Paint::solid(theme.bodyText.color);
    const int16_t y = area.y + area.height / 2;
    target.line(fui::Point{area.x, y}, fui::Point{static_cast<int16_t>(area.x + end), y}, 1, ink);
    target.line(fui::Point{static_cast<int16_t>(area.right() - end), y}, fui::Point{area.right(), y}, 1, ink);
  }
  target.text(area, label, style);
}

inline void chip(fui::DrawTarget& target, fui::Rect area, const fui::Paint& ink) {
  const int16_t side = std::min(area.width, area.height);
  if (side < 12) return;
  const fui::Point center{static_cast<int16_t>(area.x + area.width / 2),
                          static_cast<int16_t>(area.y + area.height / 2)};
  const int16_t radius = (side - 1) / 2;
  detail::circle(target, center, radius, ink, 2);
  detail::circle(target, center, radius * 2 / 3, ink);
  for (uint8_t i = 0; i < 37; i += 5)
    target.line(detail::rimPoint(center, radius * 2 / 3, i), detail::rimPoint(center, radius - 1, i), 2, ink);
}

inline void frame(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  if (area.empty()) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  target.stroke(area, ink, 1);
  target.stroke(area.inset(fui::makeInsets(theme.spaceSm)), ink, 1);
}

}  // namespace ui_roulette
