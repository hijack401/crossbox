#pragma once

#include <FreeInkApp.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

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

inline void seven(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  target.fill(box(area, 15, 13, 72, 15), ink);
  stroke(target, area, 77, 24, 38, 85, 16, ink);
  target.fill(box(area, 27, 49, 48, 8), ink);
}

inline void concealed(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto pattern =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const auto inner = area.inset(fui::Insets{theme.spaceSm, theme.spaceSm, theme.spaceSm, theme.spaceSm});
  if (inner.empty()) return;
  const int16_t step = std::max<int16_t>(4, inner.height / 8);
  for (int16_t y = inner.y; y < inner.bottom(); y += step)
    target.fill(fui::makeRect(inner.x, y, inner.width, 1), pattern);
  const int16_t size = std::min(inner.width, inner.height) / 3;
  const int16_t cx = inner.x + inner.width / 2;
  const int16_t cy = inner.y + inner.height / 2;
  target.fill(fui::makeRect(cx - size, cy - size, size * 2 + 1, size * 2 + 1), paper);
  for (int16_t row = -size; row <= size; ++row) {
    const int16_t half = size - std::abs(row);
    target.fill(fui::makeRect(cx - half, cy + row, half * 2 + 1, 1), ink);
  }
  const int16_t hole = std::max<int16_t>(1, size / 3);
  target.fill(fui::makeRect(cx - hole, cy - hole, hole * 2 + 1, hole * 2 + 1), paper);
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
      detail::seven(target, square, theme);
      break;
  }
}

inline void reels(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, const SlotsGame::State& state,
                  bool preview = false) {
  if (area.width < 90 || area.height < 72) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto shade =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const int16_t depth = std::max<int16_t>(3, theme.spaceSm);
  const int16_t rim = std::max<int16_t>(5, theme.spaceSm * 2);
  const fui::Rect face = fui::makeRect(area.x, area.y, area.width - depth, area.height - depth);
  target.fill(fui::makeRect(face.x + depth, face.y + depth, face.width, face.height), ink);
  target.fill(face, paper);
  target.stroke(face, ink, 2);
  target.stroke(face.inset(fui::makeInsets(static_cast<int16_t>(rim / 2))), ink, 1);
  for (int16_t corner = 0; corner < 4; ++corner) {
    const int16_t x = corner & 1 ? face.right() - rim / 2 : face.x + rim / 2;
    const int16_t y = corner & 2 ? face.bottom() - rim / 2 : face.y + rim / 2;
    target.fill(fui::makeRect(x - 1, y - 1, 3, 3), ink);
  }
  const int16_t line = target.lineHeight(theme.smallText.font);
  const bool marquee = face.height >= line * 3 + rim * 4;
  const int16_t topBand = marquee ? line + theme.spaceSm : rim;
  const int16_t lampBand = std::max<int16_t>(rim * 2, line);
  auto caption = theme.smallText;
  caption.bold = true;
  caption.align = fui::TextAlign::Center;
  if (marquee)
    target.text(fui::makeRect(face.x + rim, face.y + rim, face.width - rim * 2, line), tr(STR_SLOTS_CABINET), caption);
  const int16_t gutter = rim + theme.spaceSm * 2;
  const int16_t gap = std::max<int16_t>(4, theme.spaceSm);
  const int16_t width = (face.width - gutter * 2 - gap * 2) / 3;
  const int16_t height = face.height - rim * 2 - topBand - lampBand;
  if (width < 12 || height < 12) return;
  const int16_t left = face.x + (face.width - width * 3 - gap * 2) / 2;
  const int16_t top = face.y + rim + topBand;
  const int16_t centerY = top + height / 2;
  const int16_t lip = std::max<int16_t>(3, std::min<int16_t>(height / 6, theme.spaceMd));
  static constexpr SlotsGame::Symbol PREVIEW[] = {SlotsGame::Symbol::Seven, SlotsGame::Symbol::Bell,
                                                  SlotsGame::Symbol::Cherry};
  const bool settled = !preview && state.phase == SlotsGame::Phase::Settled;
  const bool winning = settled && SlotsGame::returnMultiplier(state.reels) > 0;
  const bool triple = state.reels[0] == state.reels[1] && state.reels[1] == state.reels[2];
  for (uint8_t i = 0; i < SlotsGame::REELS; ++i) {
    const auto reel = fui::makeRect(left + i * (width + gap), top, width, height);
    target.fill(reel, ink);
    const auto window = reel.inset(fui::Insets{2, 2, 2, 2});
    target.fill(window, paper);
    target.fill(fui::makeRect(window.x, window.y, window.width, lip), shade);
    target.fill(fui::makeRect(window.x, window.bottom() - lip, window.width, lip), shade);
    target.fill(fui::makeRect(window.x, window.y + lip, window.width, 1), ink);
    target.fill(fui::makeRect(window.x, window.bottom() - lip - 1, window.width, 1), ink);
    const auto content = window.inset(fui::Insets{static_cast<int16_t>(lip + 3), 4, static_cast<int16_t>(lip + 3), 4});
    const bool revealed = !preview && i < state.revealedReels;
    if (preview || revealed)
      symbol(target, content, theme, preview ? PREVIEW[i] : state.reels[i]);
    else
      detail::concealed(target, content, theme);
    const bool paid = winning && (triple || state.reels[i] == SlotsGame::Symbol::Cherry);
    if (paid) {
      target.stroke(reel, ink, 3);
      target.fill(fui::makeRect(reel.x + gap, reel.bottom() + 3, reel.width - gap * 2, 3), ink);
    }
    const int16_t lamp = std::max<int16_t>(4, std::min<int16_t>(7, lampBand / 3));
    const auto indicator =
        fui::makeRect(reel.x + (reel.width - lamp) / 2, reel.bottom() + (lampBand - lamp) / 2, lamp, lamp);
    if (revealed || paid) {
      target.fill(indicator, ink);
    } else {
      target.stroke(indicator, ink, 1);
      if (!preview && i == state.revealedReels) target.stroke(indicator.inset(fui::Insets{-2, -2, -2, -2}), ink, 1);
    }
  }
  const int16_t tick = std::max<int16_t>(3, std::min<int16_t>(7, gutter - rim));
  for (int16_t row = -tick; row <= tick; ++row) {
    const int16_t span = tick - std::abs(row) + 1;
    target.fill(fui::makeRect(left - tick - 3, centerY + row, span, 1), ink);
    target.fill(fui::makeRect(left + width * 3 + gap * 2 + tick + 2 - span, centerY + row, span, 1), ink);
  }
}

}  // namespace ui_slots
