#pragma once

#include <FreeInkApp.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "UiCountdown.h"
#include "fonts/BalanceDigits.h"
#include "util/BlackjackGame.h"

namespace ui_casino {
namespace fui = freeink::ui;

inline void money(char* out, size_t size, int64_t cents) {
  char digits[32];
  snprintf(digits, sizeof(digits), "%lld", static_cast<long long>(cents / 100));
  char grouped[40];
  const size_t length = strlen(digits);
  size_t pos = 0;
  for (size_t i = 0; i < length; ++i) {
    grouped[pos++] = digits[i];
    if (i + 1 < length && (length - i - 1) % 3 == 0) grouped[pos++] = ',';
  }
  if (cents % 100) {
    snprintf(grouped + pos, sizeof(grouped) - pos, tr(STR_CASINO_CENTS), static_cast<unsigned>(cents % 100));
  } else
    grouped[pos] = '\0';
  snprintf(out, size, tr(STR_CASINO_MONEY), grouped);
}

inline void soften(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme) {
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  for (int16_t y = rect.y + (rect.y & 1); y < rect.bottom(); y += 2)
    for (int16_t x = rect.x + (rect.x & 1); x < rect.right(); x += 2) target.fill(fui::Rect{x, y, 1, 1}, paper);
}

inline void balance(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, int64_t cents) {
  if (area.empty()) return;
  namespace font = balance_font;
  char text[48];
  money(text, sizeof(text), cents);
  int naturalWidth = 0;
  int firstBearing = 0;
  int advance = 0;
  for (const char* c = text; *c; ++c) {
    const char* character = strchr(font::CHARACTERS, *c);
    if (!character) {
      target.text(area, text, theme.titleText);
      return;
    }
    const auto& glyph = font::GLYPHS[character - font::CHARACTERS];
    if (c == text) firstBearing = glyph.left;
    naturalWidth = std::max<int>(naturalWidth, advance + glyph.left + glyph.width - firstBearing);
    advance += glyph.advance;
  }
  if (naturalWidth <= 0) return;
  const int height = std::min<int>({area.height, font::HEIGHT, area.width * font::HEIGHT / naturalWidth});
  if (height < 1) return;
  const int y = area.y + (area.height - height) / 2;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  advance = -firstBearing;
  for (const char* c = text; *c; ++c) {
    const auto& glyph = font::GLYPHS[strchr(font::CHARACTERS, *c) - font::CHARACTERS];
    if (glyph.width && glyph.height) {
      const int left = (advance + glyph.left) * height / font::HEIGHT;
      const int top = glyph.top * height / font::HEIGHT;
      const int right = (advance + glyph.left + glyph.width) * height / font::HEIGHT;
      const int bottom = (glyph.top + glyph.height) * height / font::HEIGHT;
      const fui::BitmapRef bitmap{font::BITMAPS + glyph.offset, glyph.width, glyph.height, fui::BitmapFormat::BW1,
                                  true};
      target.bitmap(fui::Rect{static_cast<int16_t>(area.x + left), static_cast<int16_t>(y + top),
                              static_cast<int16_t>(right - left), static_cast<int16_t>(bottom - top)},
                    bitmap, fui::BitmapMode::Stretch, ink);
    }
    advance += glyph.advance;
  }
}

inline void amount(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, int64_t cents) {
  if (area.empty()) return;
  namespace font = countdown_font;
  char digits[16];
  snprintf(digits, sizeof(digits), "%lld", static_cast<long long>(cents / 100));
  const int glyphWidth =
      static_cast<int>(strlen(digits)) * font::GLYPHS[0].advance - font::LEFT_BEARING - font::RIGHT_BEARING;
  const int side = target.lineHeight(theme.titleText.font);
  const int fractionWidth = cents % 100 ? side * 2 : 0;
  const int height = std::min<int>(
      {area.height, font::HEIGHT, std::max(1, area.width - side - fractionWidth) * font::HEIGHT / glyphWidth});
  const int width = glyphWidth * height / font::HEIGHT;
  const int x = area.x + (area.width - width - side - fractionWidth) / 2;
  auto style = theme.titleText;
  style.bold = true;
  target.text(fui::Rect{static_cast<int16_t>(x), area.y, static_cast<int16_t>(side), area.height},
              tr(STR_CASINO_DOLLAR), style);
  ui_countdown_detail::drawReadout(
      target, fui::Rect{static_cast<int16_t>(x + side), area.y, static_cast<int16_t>(width), area.height}, theme,
      digits);
  if (fractionWidth) {
    char fraction[8];
    snprintf(fraction, sizeof(fraction), tr(STR_CASINO_CENTS), static_cast<unsigned>(cents % 100));
    target.text(
        fui::Rect{static_cast<int16_t>(x + side + width), area.y, static_cast<int16_t>(fractionWidth), area.height},
        fraction, style);
  }
}

inline void suit(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme, uint8_t value) {
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const int side = std::min(rect.width, rect.height);
  const int cx = rect.x + rect.width / 2;
  const int top = rect.y + (rect.height - side) / 2;
  if (side < 2) return;
  if (value == 0) {
    const int radius = std::max(1, side / 4);
    for (int lobe = 0; lobe < 3; ++lobe) {
      const int x = cx + (lobe == 0 ? 0 : lobe == 1 ? -radius : radius);
      const int centerY = top + (lobe == 0 ? radius : side / 2);
      for (int y = -radius; y <= radius; ++y) {
        int half = 0;
        while ((half + 1) * (half + 1) + y * y <= radius * radius) ++half;
        target.fill(fui::Rect{static_cast<int16_t>(x - half), static_cast<int16_t>(centerY + y),
                              static_cast<int16_t>(half * 2 + 1), 1},
                    ink);
      }
    }
    target.fill(fui::Rect{static_cast<int16_t>(cx - std::max(1, side / 12)), static_cast<int16_t>(top + side / 2),
                          static_cast<int16_t>(std::max(2, side / 6)), static_cast<int16_t>(side / 2)},
                ink);
    return;
  }
  if (value == 3 && side >= 6) {
    const int shoulder = side / 2;
    const int bodyHalf = (side - 1) / 2;
    const int radius = std::max(1, bodyHalf / 2);
    const int offset = bodyHalf - radius;
    for (int y = 0; y <= shoulder; ++y) {
      const int half = bodyHalf * y / shoulder;
      target.fill(fui::Rect{static_cast<int16_t>(cx - half), static_cast<int16_t>(top + y),
                            static_cast<int16_t>(half * 2 + 1), 1},
                  ink);
    }
    for (int y = 1; y <= radius; ++y) {
      int half = 0;
      while ((half + 1) * (half + 1) + y * y <= radius * radius) ++half;
      for (int lobe = -1; lobe <= 1; lobe += 2)
        target.fill(fui::Rect{static_cast<int16_t>(cx + lobe * offset - half), static_cast<int16_t>(top + shoulder + y),
                              static_cast<int16_t>(half * 2 + 1), 1},
                    ink);
    }
    const int stemHalf = side / 24;
    const int baseHalf = std::max(1, side / 10);
    const int flare = side * 3 / 4;
    for (int y = shoulder; y < side; ++y) {
      const int half = stemHalf + (y > flare ? (baseHalf - stemHalf) * (y - flare) / (side - 1 - flare) : 0);
      target.fill(fui::Rect{static_cast<int16_t>(cx - half), static_cast<int16_t>(top + y),
                            static_cast<int16_t>(half * 2 + 1), 1},
                  ink);
    }
    return;
  }
  // Scanline shapes avoid a polygon scratch allocation in the display driver.
  for (int y = 0; y < side; ++y) {
    int half = 0;
    if (value == 1)
      half = std::min(y, side - 1 - y);
    else if (value == 2) {
      if (y < side / 3) {
        const int radius = std::max(1, side / 4);
        const int width = std::min(radius, y + 1);
        target.fill(fui::Rect{static_cast<int16_t>(cx - radius - width), static_cast<int16_t>(top + y),
                              static_cast<int16_t>(width * 2), 1},
                    ink);
        target.fill(fui::Rect{static_cast<int16_t>(cx + radius - width), static_cast<int16_t>(top + y),
                              static_cast<int16_t>(width * 2), 1},
                    ink);
        continue;
      }
      half = (side - y) * 3 / 4;
    } else if (value == 3) {
      half = y < side * 2 / 3 ? y * 3 / 4 : std::max(side / 12, (side - y) / 3);
    } else {
      half = y < side / 3 ? std::min(y + 1, side / 5) : y < side * 2 / 3 ? side * 2 / 5 : side / 10;
    }
    if (half > 0)
      target.fill(
          fui::Rect{static_cast<int16_t>(cx - half), static_cast<int16_t>(top + y), static_cast<int16_t>(half * 2), 1},
          ink);
  }
}

inline void card(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme, uint8_t value, bool hidden) {
  if (rect.empty()) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  target.fill(rect, paper);
  target.stroke(rect, ink, 2);
  const auto inner = rect.inset(fui::Insets{theme.spaceSm, theme.spaceSm, theme.spaceSm, theme.spaceSm});
  if (inner.empty()) return;
  if (hidden) {
    target.stroke(inner, ink, 1);
    const int16_t radius = std::max<int16_t>(2, theme.spaceSm - 1);
    const int16_t step = std::max<int16_t>(radius * 3, theme.spaceMd * 2);
    for (int16_t y = inner.y + theme.spaceSm + radius; y + radius < inner.bottom() - theme.spaceSm; y += step) {
      for (int16_t x = inner.x + theme.spaceSm + radius; x + radius < inner.right() - theme.spaceSm; x += step) {
        const fui::Point top{x, static_cast<int16_t>(y - radius)};
        const fui::Point right{static_cast<int16_t>(x + radius), y};
        const fui::Point bottom{x, static_cast<int16_t>(y + radius)};
        const fui::Point left{static_cast<int16_t>(x - radius), y};
        target.line(top, right, 1, ink);
        target.line(right, bottom, 1, ink);
        target.line(bottom, left, 1, ink);
        target.line(left, top, 1, ink);
      }
    }
    return;
  }
  static constexpr const char* RANKS[] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  const char* rank = RANKS[BlackjackGame::rank(value) - 1];
  auto text = theme.bodyText;
  text.bold = true;
  if (inner.height < target.lineHeight(text.font) + theme.spaceMd ||
      inner.width < target.measureText(text.font, rank, text).width)
    text = theme.smallText;
  text.bold = true;
  const int16_t labelHeight = target.lineHeight(text.font);
  const int16_t rankWidth = target.measureText(text.font, RANKS[9], text).width;
  const int16_t cornerSuit = std::max<int16_t>(theme.spaceSm * 2, labelHeight / 3);
  const bool full =
      inner.width >= rankWidth * 2 + theme.spaceMd && inner.height >= labelHeight * 3 + cornerSuit + theme.spaceSm * 3;
  if (!full) {
    text.align = fui::TextAlign::Center;
    target.text(fui::Rect{inner.x, inner.y, inner.width, std::min<int16_t>(labelHeight, inner.height)}, rank, text);
    const int16_t symbol =
        std::max<int16_t>(0, std::min<int16_t>(inner.width / 2, inner.height - labelHeight - theme.spaceSm));
    suit(target,
         fui::Rect{static_cast<int16_t>(inner.x + (inner.width - symbol) / 2),
                   static_cast<int16_t>(inner.bottom() - symbol), symbol, symbol},
         theme, BlackjackGame::suit(value));
    return;
  }
  text.align = fui::TextAlign::Left;
  target.text(fui::Rect{inner.x, inner.y, rankWidth, labelHeight}, rank, text);
  suit(target,
       fui::Rect{static_cast<int16_t>(inner.x + (rankWidth - cornerSuit) / 2),
                 static_cast<int16_t>(inner.y + labelHeight + theme.spaceSm), cornerSuit, cornerSuit},
       theme, BlackjackGame::suit(value));
  text.align = fui::TextAlign::Right;
  target.text(fui::Rect{static_cast<int16_t>(inner.right() - rankWidth),
                        static_cast<int16_t>(inner.bottom() - labelHeight), rankWidth, labelHeight},
              rank, text);
  const int16_t centerTop = inner.y + labelHeight + cornerSuit + theme.spaceSm * 2;
  const int16_t centerHeight = inner.bottom() - labelHeight - theme.spaceSm - centerTop;
  const int16_t symbol = std::min<int16_t>(inner.width / 2, centerHeight);
  suit(target,
       fui::Rect{static_cast<int16_t>(inner.x + (inner.width - symbol) / 2),
                 static_cast<int16_t>(centerTop + (centerHeight - symbol) / 2), symbol, symbol},
       theme, BlackjackGame::suit(value));
}

inline void lobbyIcon(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme, uint8_t game) {
  const int16_t side = std::min(rect.width, rect.height);
  if (side < 24) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const uint8_t stroke = static_cast<uint8_t>(std::clamp<int>(side / 20, 1, 3));
  const int16_t x = rect.x + (rect.width - side) / 2;
  const int16_t y = rect.y + (rect.height - side) / 2;
  if (game == 0) {
    const int16_t gap = std::max<int16_t>(stroke, side / 12);
    const int16_t width = (side - stroke * 2 - gap * 2) / 3;
    const int16_t height = side * 3 / 4;
    const int16_t left = x + (side - width * 3 - gap * 2) / 2;
    const int16_t top = y + (side - height) / 2;
    for (int i = 0; i < 3; ++i) {
      const int16_t reelX = left + i * (width + gap);
      target.stroke(fui::Rect{reelX, top, width, height}, ink, stroke);
      const int16_t inset = std::max<int16_t>(stroke + 1, width / 4);
      const int16_t sevenY = top + height / 3;
      const fui::Point start{static_cast<int16_t>(reelX + inset), sevenY};
      const fui::Point corner{static_cast<int16_t>(reelX + width - inset - 1), sevenY};
      const fui::Point end{static_cast<int16_t>(reelX + width / 2 - 1), static_cast<int16_t>(top + height * 2 / 3)};
      target.line(start, corner, 1, ink);
      target.line(corner, end, 1, ink);
    }
  } else if (game == 2) {
    static constexpr int8_t RIM[][2] = {{0, -100}, {38, -92},  {71, -71},  {92, -38}, {100, 0},  {92, 38},
                                        {71, 71},  {38, 92},   {0, 100},   {-38, 92}, {-71, 71}, {-92, 38},
                                        {-100, 0}, {-92, -38}, {-71, -71}, {-38, -92}};
    const fui::Point center{static_cast<int16_t>(x + side / 2), static_cast<int16_t>(y + side / 2)};
    const int16_t radius = (side - 1) / 2 - stroke;
    for (int i = 0; i < 16; ++i) {
      const int next = (i + 1) % 16;
      const fui::Point from{static_cast<int16_t>(center.x + RIM[i][0] * radius / 100),
                            static_cast<int16_t>(center.y + RIM[i][1] * radius / 100)};
      const fui::Point to{static_cast<int16_t>(center.x + RIM[next][0] * radius / 100),
                          static_cast<int16_t>(center.y + RIM[next][1] * radius / 100)};
      target.line(from, to, stroke, ink);
      if (i % 2 == 0) target.line(center, from, 1, ink);
    }
    const int16_t hub = std::max<int16_t>(stroke * 2 + 1, side / 6);
    const fui::Rect centerRect{static_cast<int16_t>(center.x - hub / 2), static_cast<int16_t>(center.y - hub / 2), hub,
                               hub};
    target.fill(centerRect, paper);
    target.stroke(centerRect, ink, stroke);
  } else if (game == 1 || game == 3) {
    const int16_t width = side * 3 / 5;
    const int16_t height = side * 4 / 5;
    const int16_t offset = std::max<int16_t>(stroke * 2, side / 6);
    const int16_t left = x + (side - width - offset) / 2;
    const int16_t top = y + (side - height - offset) / 2;
    target.stroke(fui::Rect{left, top, width, height}, ink, stroke);
    const fui::Rect front{static_cast<int16_t>(left + offset), static_cast<int16_t>(top + offset), width, height};
    target.fill(front, paper);
    target.stroke(front, ink, stroke);
    const int16_t symbol = width / 2;
    suit(target,
         fui::Rect{static_cast<int16_t>(front.x + (front.width - symbol) / 2),
                   static_cast<int16_t>(front.y + (front.height - symbol) / 2), symbol, symbol},
         theme, game == 1 ? 3 : 1);
  }
}
}  // namespace ui_casino
