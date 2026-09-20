#pragma once

#include <FreeInkApp.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "UiCountdown.h"
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

inline void ring(fui::DrawTarget& target, fui::Point center, int16_t outer, int16_t inner,
                 const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto red =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  for (uint8_t i = 0; i < 37; ++i) {
    const uint8_t next = (i + 1) % 37;
    const auto outerStart = rimPoint(center, outer, i);
    const auto outerEnd = rimPoint(center, outer, next);
    const auto innerStart = rimPoint(center, inner, i);
    const auto innerEnd = rimPoint(center, inner, next);
    const auto paint = POCKETS[i] == 0 ? paper : RouletteGame::isRed(POCKETS[i]) ? red : ink;
    const fui::Point points[] = {outerStart, outerEnd, innerEnd, innerStart};
    sector(target, points, paint);
    target.line(outerStart, outerEnd, 1, ink);
    target.line(innerStart, innerEnd, 1, ink);
    target.line(innerStart, outerStart, 1, i < 2 ? ink : paper);
    target.line(rimPoint(center, outer + theme.spaceSm, i), rimPoint(center, outer + theme.spaceSm, next), 1, ink);
  }
}

inline void labels(fui::DrawTarget& target, fui::Point center, int16_t radius, int16_t width,
                   const fui::ThemeTokens& theme) {
  auto text = theme.smallText;
  text.align = fui::TextAlign::Center;
  text.bold = false;
  const int16_t line = target.lineHeight(text.font);
  char label[8];
  for (uint8_t i = 0; i < 37; i += 5) {
    const auto position = pocketPoint(center, radius, i);
    snprintf(label, sizeof(label), tr(STR_CASINO_TOTAL), static_cast<unsigned>(POCKETS[i]));
    target.text(fui::Rect{static_cast<int16_t>(position.x - width / 2), static_cast<int16_t>(position.y - line / 2),
                          width, line},
                label, text);
  }
}

inline void marker(fui::DrawTarget& target, fui::Point center, int16_t radius, const fui::ThemeTokens& theme,
                   uint8_t result) {
  for (uint8_t i = 0; i < 37; ++i) {
    if (POCKETS[i] != result) continue;
    const auto position = pocketPoint(center, radius, i);
    const int16_t size = std::max<int16_t>(7, theme.spaceMd + 1);
    const fui::Rect ball{static_cast<int16_t>(position.x - size / 2), static_cast<int16_t>(position.y - size / 2), size,
                         size};
    target.fill(ball, fui::Paint::solid(fui::invertedColor(theme.bodyText.color)), size / 2);
    target.stroke(ball, fui::Paint::solid(theme.bodyText.color), 2, size / 2);
    return;
  }
}

}  // namespace detail

inline void wheel(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, bool revealed = false,
                  uint8_t result = 0) {
  const int16_t side = std::min(area.width, area.height);
  if (side < theme.minTouchSize) return;
  const int16_t line = target.lineHeight(theme.smallText.font);
  char label[8];
  snprintf(label, sizeof(label), tr(STR_CASINO_TOTAL), 36U);
  const int16_t labelWidth = target.measureText(theme.smallText.font, label, theme.smallText).width + theme.spaceSm;
  const int16_t labelExtent = std::max(line, labelWidth);
  const bool showLabels = side >= line * 10;
  const int16_t outer = side / 2 - theme.spaceSm * 2 - (showLabels ? labelExtent : 0);
  const int16_t inner = outer * 3 / 4;
  if (inner <= theme.spaceSm * 2) return;
  const fui::Point center{static_cast<int16_t>(area.x + area.width / 2),
                          static_cast<int16_t>(area.y + area.height / 2)};
  detail::ring(target, center, outer, inner, theme);
  if (showLabels) detail::labels(target, center, outer + labelExtent / 2 + theme.spaceSm * 2, labelWidth, theme);

  const int16_t width = inner * 3 / 2;
  const int16_t height = inner * 3 / 4;
  const fui::Rect readout{static_cast<int16_t>(center.x - width / 2),
                          static_cast<int16_t>(center.y - (height + line + theme.spaceSm) / 2), width, height};
  if (revealed && result <= 36) {
    snprintf(label, sizeof(label), tr(STR_CASINO_TOTAL), static_cast<unsigned>(result));
    ui_countdown_detail::drawReadout(target, readout, theme, label);
    auto text = theme.smallText;
    text.align = fui::TextAlign::Center;
    target.text(fui::Rect{readout.x, static_cast<int16_t>(readout.bottom() + theme.spaceSm), width, line},
                result == 0                   ? tr(STR_ROULETTE_ZERO)
                : RouletteGame::isRed(result) ? tr(STR_ROULETTE_RED)
                                              : tr(STR_ROULETTE_BLACK),
                text);
    detail::marker(target, center, (outer + inner) / 2, theme, result);
  } else {
    auto text = theme.titleText;
    text.bold = true;
    text.align = fui::TextAlign::Center;
    target.text(fui::Rect{static_cast<int16_t>(center.x - width / 2), static_cast<int16_t>(center.y - height / 2),
                          width, height},
                tr(STR_ROULETTE_UNKNOWN), text);
  }
}

}  // namespace ui_roulette
