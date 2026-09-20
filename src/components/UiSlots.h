#pragma once

#include <FreeInkApp.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "UiCountdown.h"
#include "util/SlotsGame.h"

namespace ui_slots {
namespace fui = freeink::ui;
namespace detail {

inline fui::Rect box(fui::Rect area, int x, int y, int width, int height) {
  return fui::Rect{static_cast<int16_t>(area.x + area.width * x / 100),
                   static_cast<int16_t>(area.y + area.height * y / 100),
                   static_cast<int16_t>(std::max(1, area.width * width / 100)),
                   static_cast<int16_t>(std::max(1, area.height * height / 100))};
}

inline fui::Point point(fui::Rect area, int x, int y) {
  return fui::Point{static_cast<int16_t>(area.x + area.width * x / 100),
                    static_cast<int16_t>(area.y + area.height * y / 100)};
}

inline void stroke(fui::DrawTarget& target, fui::Rect area, int x1, int y1, int x2, int y2, int width,
                   const fui::Paint& paint) {
  target.line(point(area, x1, y1), point(area, x2, y2), std::max(1, area.width * width / 100), paint);
}

inline void fruit(fui::DrawTarget& target, fui::Rect area, int x, int y, int size, const fui::Paint& paint) {
  const auto rect = box(area, x, y, size, size);
  target.fill(rect, paint, static_cast<uint8_t>(rect.width / 2));
}

inline void cherry(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  stroke(target, area, 31, 61, 43, 28, 4, ink);
  stroke(target, area, 43, 28, 70, 11, 4, ink);
  stroke(target, area, 67, 64, 57, 38, 4, ink);
  stroke(target, area, 57, 38, 43, 28, 4, ink);
  for (int y = 0; y < area.height / 6; ++y) {
    const int half = std::min(y, area.height / 6 - 1 - y);
    target.fill(
        fui::Rect{static_cast<int16_t>(area.x + area.width * 66 / 100 - half * 2),
                  static_cast<int16_t>(area.y + area.height * 15 / 100 + y), static_cast<int16_t>(half * 4 + 1), 1},
        ink);
  }
  fruit(target, area, 10, 50, 41, ink);
  fruit(target, area, 49, 54, 41, ink);
  stroke(target, area, 21, 63, 25, 59, 4, paper);
  stroke(target, area, 60, 67, 64, 63, 4, paper);
}

inline void lemon(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  for (int y = area.height / 5; y < area.height * 4 / 5; ++y) {
    const int distance = std::abs(50 - y * 100 / area.height);
    const int half = distance <= 20 ? 47 - distance * 17 / 20 : 30 - (distance - 20) * 2;
    target.fill(fui::Rect{static_cast<int16_t>(area.x + area.width * (50 - half) / 100),
                          static_cast<int16_t>(area.y + y), static_cast<int16_t>(area.width * half * 2 / 100), 1},
                ink);
  }
  stroke(target, area, 23, 43, 34, 32, 3, paper);
  stroke(target, area, 34, 32, 49, 28, 3, paper);
}

inline void bell(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  fruit(target, area, 43, 10, 14, ink);
  for (int y = area.height / 5; y < area.height * 3 / 4; ++y) {
    const int position = y * 100 / area.height;
    const int half = position < 32   ? 9 + (position - 20)
                     : position < 61 ? 21 + (position - 32) / 9
                                     : 24 + (position - 61);
    target.fill(fui::Rect{static_cast<int16_t>(area.x + area.width * (50 - half) / 100),
                          static_cast<int16_t>(area.y + y), static_cast<int16_t>(area.width * half * 2 / 100), 1},
                ink);
  }
  fruit(target, area, 41, 77, 18, ink);
  target.fill(box(area, 13, 72, 74, 10), ink);
  stroke(target, area, 35, 37, 32, 55, 3, paper);
  stroke(target, area, 21, 74, 79, 74, 2, paper);
}

inline void bar(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  target.fill(box(area, 4, 20, 92, 8), ink);
  target.fill(box(area, 4, 72, 92, 8), ink);
  auto text = theme.titleText;
  text.bold = true;
  text.align = fui::TextAlign::Center;
  if (target.measureText(text.font, tr(STR_SLOTS_BAR), text).width > area.width) text.font = theme.bodyText.font;
  if (target.measureText(text.font, tr(STR_SLOTS_BAR), text).width > area.width) text.font = theme.smallText.font;
  target.text(box(area, 0, 28, 100, 44), tr(STR_SLOTS_BAR), text);
}

inline void concealed(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto pattern =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const auto inner = area.inset(fui::Insets{theme.spaceSm, theme.spaceSm, theme.spaceSm, theme.spaceSm});
  if (inner.empty()) return;
  target.fill(inner, pattern);
  const int16_t size = std::min(inner.width, inner.height) / 3;
  const fui::Rect center{static_cast<int16_t>(inner.x + (inner.width - size) / 2),
                         static_cast<int16_t>(inner.y + (inner.height - size) / 2), size, size};
  target.fill(center, paper);
  target.stroke(center, ink, 1);
  const auto inset = center.inset(fui::Insets{3, 3, 3, 3});
  if (!inset.empty()) target.stroke(inset, ink, 1);
}

}  // namespace detail

inline void symbol(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, SlotsGame::Symbol value) {
  const int16_t side = std::min(area.width, area.height);
  if (side < 8) return;
  const fui::Rect square{static_cast<int16_t>(area.x + (area.width - side) / 2),
                         static_cast<int16_t>(area.y + (area.height - side) / 2), side, side};
  switch (value) {
    case SlotsGame::Symbol::Cherry:
      detail::cherry(target, square, theme);
      break;
    case SlotsGame::Symbol::Lemon:
      detail::lemon(target, square, theme);
      break;
    case SlotsGame::Symbol::Bell:
      detail::bell(target, square, theme);
      break;
    case SlotsGame::Symbol::Bar:
      detail::bar(target, square, theme);
      break;
    case SlotsGame::Symbol::Seven:
      ui_countdown_detail::drawReadout(target, square, theme, tr(STR_SLOTS_SEVEN));
      break;
  }
}

inline void reels(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, const SlotsGame::State& state,
                  bool preview = false) {
  const int16_t gap = theme.spaceSm;
  const int16_t gutter = theme.spaceMd;
  const int16_t width = (area.width - gutter * 2 - gap * 2) / 3;
  const int16_t height = std::min<int16_t>(area.height - theme.spaceMd, width * 4 / 3);
  if (width < 12 || height < 12) return;
  const int16_t left = area.x + (area.width - width * 3 - gap * 2) / 2;
  const int16_t top = area.y + (area.height - height - theme.spaceMd) / 2;
  const int16_t centerY = top + height / 2;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  for (uint8_t i = 0; i < SlotsGame::REELS; ++i) {
    const fui::Rect reel{static_cast<int16_t>(left + i * (width + gap)), top, width, height};
    target.fill(reel, paper);
    target.stroke(reel, ink, 2);
    const auto content = reel.inset(fui::Insets{theme.spaceMd, theme.spaceSm, theme.spaceMd, theme.spaceSm});
    if (!preview && i < state.revealedReels)
      symbol(target, content, theme, state.reels[i]);
    else
      detail::concealed(target, content, theme);
    if (!preview && state.phase == SlotsGame::Phase::Revealing && i == state.revealedReels)
      target.fill(fui::Rect{static_cast<int16_t>(reel.x + width / 3), static_cast<int16_t>(reel.bottom() + gap),
                            static_cast<int16_t>(width / 3), 2},
                  ink);
  }
  const int16_t tick = std::max<int16_t>(2, std::min<int16_t>(gutter - 2, gap));
  target.line(fui::Point{static_cast<int16_t>(left - tick - 2), static_cast<int16_t>(centerY - tick)},
              fui::Point{static_cast<int16_t>(left - 2), centerY}, 1, ink);
  target.line(fui::Point{static_cast<int16_t>(left - tick - 2), static_cast<int16_t>(centerY + tick)},
              fui::Point{static_cast<int16_t>(left - 2), centerY}, 1, ink);
  const int16_t right = left + width * 3 + gap * 2;
  target.line(fui::Point{static_cast<int16_t>(right + tick + 2), static_cast<int16_t>(centerY - tick)},
              fui::Point{static_cast<int16_t>(right + 2), centerY}, 1, ink);
  target.line(fui::Point{static_cast<int16_t>(right + tick + 2), static_cast<int16_t>(centerY + tick)},
              fui::Point{static_cast<int16_t>(right + 2), centerY}, 1, ink);
}

}  // namespace ui_slots
