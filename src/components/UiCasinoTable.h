#pragma once

#include <FreeInkApp.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace ui_casino_table {
namespace fui = freeink::ui;

namespace detail {

inline fui::Rect inset(fui::Rect rect, int16_t amount) {
  return rect.inset(fui::Insets{amount, amount, amount, amount});
}

inline void border(fui::DrawTarget& target, fui::Rect rect, const fui::Paint& ink, int16_t weight = 1) {
  if (rect.empty()) return;
  weight = std::min<int16_t>(weight, std::min(rect.width, rect.height));
  target.fill(fui::Rect{rect.x, rect.y, rect.width, weight}, ink);
  target.fill(fui::Rect{rect.x, static_cast<int16_t>(rect.bottom() - weight), rect.width, weight}, ink);
  target.fill(fui::Rect{rect.x, rect.y, weight, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.right() - weight), rect.y, weight, rect.height}, ink);
}

inline void diamond(fui::DrawTarget& target, fui::Rect rect, const fui::Paint& ink, bool filled = false) {
  if (rect.empty()) return;
  const int16_t half = (rect.width - 1) / 2;
  const int16_t center = rect.x + rect.width / 2;
  for (int16_t row = 0; row < rect.height; ++row) {
    const int16_t spread =
        half * std::min<int16_t>(row, rect.height - 1 - row) / std::max<int16_t>(1, (rect.height - 1) / 2);
    if (filled) {
      target.fill(fui::Rect{static_cast<int16_t>(center - spread), static_cast<int16_t>(rect.y + row),
                            static_cast<int16_t>(spread * 2 + 1), 1},
                  ink);
    } else {
      target.fill(fui::Rect{static_cast<int16_t>(center - spread), static_cast<int16_t>(rect.y + row), 1, 1}, ink);
      target.fill(fui::Rect{static_cast<int16_t>(center + spread), static_cast<int16_t>(rect.y + row), 1, 1}, ink);
    }
  }
}

inline void disc(fui::DrawTarget& target, int16_t cx, int16_t cy, int16_t radius, const fui::Paint& ink) {
  if (radius < 0) return;
  for (int16_t row = -radius; row <= radius; ++row) {
    int16_t half = 0;
    while ((half + 1) * (half + 1) + row * row <= radius * radius) ++half;
    target.fill(fui::Rect{static_cast<int16_t>(cx - half), static_cast<int16_t>(cy + row),
                          static_cast<int16_t>(half * 2 + 1), 1},
                ink);
  }
}

inline bool suitPixel(int16_t x, int16_t y, uint8_t suit) {
  const int16_t ax = x < 0 ? -x : x;
  const int16_t ay = y < 0 ? -y : y;
  if (suit == 1) return ax + ay <= 94;
  if (suit == 2) {
    if (y >= -25) return ax * 4 <= (100 - y) * 3;
    const int16_t dx = ax - 43;
    return dx * dx + (y + 30) * (y + 30) <= 51 * 51;
  }
  const bool stem = y >= 10 && ax <= 10 + std::max<int16_t>(0, (y - 65) / 2);
  if (suit == 3) {
    if (y <= 10) return ax * 10 <= (y + 100) * 7;
    const int16_t dx = ax - 33;
    return stem || dx * dx + (y - 10) * (y - 10) <= 43 * 43;
  }
  const int16_t dx = ax - 40;
  return stem || x * x + (y + 43) * (y + 43) <= 40 * 40 || dx * dx + (y - 10) * (y - 10) <= 40 * 40;
}

inline void suit(fui::DrawTarget& target, fui::Rect rect, const fui::Paint& ink, uint8_t value, bool inverted = false) {
  const int16_t side = std::min(rect.width, rect.height);
  if (side < 3 || value > 3) return;
  const int16_t left = rect.x + (rect.width - side) / 2;
  const int16_t top = rect.y + (rect.height - side) / 2;
  // Scanline runs preserve the heart's notch without polygon scratch memory.
  for (int16_t row = 0; row < side; ++row) {
    const int16_t y = ((row * 2 + 1) * 100 / side - 100) * (inverted ? -1 : 1);
    int16_t start = -1;
    for (int16_t col = 0; col <= side; ++col) {
      const bool active = col < side && suitPixel((col * 2 + 1) * 100 / side - 100, y, value);
      if (active && start < 0) start = col;
      if (!active && start >= 0) {
        target.fill(fui::Rect{static_cast<int16_t>(left + start), static_cast<int16_t>(top + row),
                              static_cast<int16_t>(col - start), 1},
                    ink);
        start = -1;
      }
    }
  }
}

inline void pip(fui::DrawTarget& target, fui::Rect field, int16_t side, int16_t x, int16_t y, const fui::Paint& ink,
                uint8_t value) {
  suit(target,
       fui::Rect{static_cast<int16_t>(field.x + (field.width - side) * x / 12),
                 static_cast<int16_t>(field.y + (field.height - side) * y / 12), side, side},
       ink, value, y > 6);
}

inline const char* rankLabel(uint8_t rank, char (&number)[3]) {
  switch (rank) {
    case 1:
      return tr(STR_CASINO_CARD_ACE);
    case 11:
      return tr(STR_CASINO_CARD_JACK);
    case 12:
      return tr(STR_CASINO_CARD_QUEEN);
    case 13:
      return tr(STR_CASINO_CARD_KING);
    default:
      snprintf(number, sizeof(number), "%u", static_cast<unsigned>(rank));
      return number;
  }
}

inline void compactFace(fui::DrawTarget& target, fui::Rect inner, const fui::ThemeTokens& theme, const char* label,
                        uint8_t suitValue, const fui::Paint& ink) {
  auto text = theme.smallText;
  text.bold = true;
  text.align = fui::TextAlign::Center;
  const int16_t line = target.lineHeight(text.font);
  const int16_t width = target.measureText(text.font, label, text).width;
  const int16_t gap = std::max<int16_t>(2, theme.spaceXs);
  const int16_t symbol = std::max<int16_t>(6, std::min<int16_t>(inner.width / 3, line / 2));
  if (line > inner.height || width > inner.width) {
    suit(target, inner, ink, suitValue);
    return;
  }
  if (line + gap + symbol <= inner.height) {
    const int16_t top = inner.y + (inner.height - line - gap - symbol) / 2;
    target.text(fui::Rect{inner.x, top, inner.width, line}, label, text);
    suit(target,
         fui::Rect{static_cast<int16_t>(inner.x + (inner.width - symbol) / 2), static_cast<int16_t>(top + line + gap),
                   symbol, symbol},
         ink, suitValue);
  } else if (width + gap + symbol <= inner.width) {
    const int16_t left = inner.x + (inner.width - width - gap - symbol) / 2;
    target.text(fui::Rect{left, static_cast<int16_t>(inner.y + (inner.height - line) / 2), width, line}, label, text);
    suit(target,
         fui::Rect{static_cast<int16_t>(left + width + gap),
                   static_cast<int16_t>(inner.y + (inner.height - symbol) / 2), symbol, symbol},
         ink, suitValue);
  } else {
    target.text(inner, label, text);
  }
}

}  // namespace detail

inline void ornament(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme) {
  if (rect.width < 24 || rect.height < 5) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const int16_t side = std::min<int16_t>(rect.height, std::max<int16_t>(5, theme.spaceSm * 2 + 1));
  const int16_t cy = rect.y + rect.height / 2;
  const int16_t cx = rect.x + rect.width / 2;
  const int16_t gap = side / 2 + std::max<int16_t>(3, theme.spaceSm);
  const int16_t line = rect.width / 2 - gap;
  if (line > 0) {
    target.fill(fui::Rect{rect.x, cy, line, 1}, ink);
    target.fill(fui::Rect{static_cast<int16_t>(cx + gap), cy, static_cast<int16_t>(rect.right() - cx - gap), 1}, ink);
  }
  detail::diamond(target,
                  fui::Rect{static_cast<int16_t>(cx - side / 2), static_cast<int16_t>(cy - side / 2), side, side}, ink);
  const int16_t core = std::max<int16_t>(1, side / 3);
  detail::diamond(target,
                  fui::Rect{static_cast<int16_t>(cx - core / 2), static_cast<int16_t>(cy - core / 2), core, core}, ink,
                  true);
}

inline void frame(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme) {
  if (rect.width < 20 || rect.height < 20) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const int16_t gap = std::max<int16_t>(3, std::min<int16_t>(theme.spaceSm, std::min(rect.width, rect.height) / 8));
  detail::border(target, rect, ink);
  const auto inner = detail::inset(rect, gap);
  detail::border(target, inner, ink);
  const int16_t step =
      std::min<int16_t>(std::max<int16_t>(theme.spaceMd, gap * 2), std::min(inner.width, inner.height) / 4);
  for (uint8_t corner = 0; corner < 4; ++corner) {
    const int16_t x = corner & 1 ? inner.right() - step : inner.x;
    const int16_t y = corner & 2 ? inner.bottom() - step : inner.y;
    detail::border(target, fui::Rect{x, y, step, step}, ink);
  }
}

inline void chip(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme, bool filled = false) {
  const int16_t side = std::min(rect.width, rect.height);
  if (side < 14) return;
  const int16_t cx = rect.x + rect.width / 2;
  const int16_t cy = rect.y + rect.height / 2;
  const int16_t radius = (side - 1) / 2;
  const int16_t rim = std::max<int16_t>(2, side / 10);
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  detail::disc(target, cx, cy, radius, ink);
  detail::disc(target, cx, cy, radius - 1, paper);
  detail::disc(target, cx, cy, radius - rim, ink);
  if (!filled) detail::disc(target, cx, cy, radius - rim - 1, paper);
  static constexpr int8_t DIRECTIONS[][2] = {{0, -100}, {71, -71}, {100, 0},  {71, 71},
                                             {0, 100},  {-71, 71}, {-100, 0}, {-71, -71}};
  const int16_t block = std::max<int16_t>(1, rim - 1);
  for (const auto& direction : DIRECTIONS) {
    const int16_t x = cx + direction[0] * (radius - rim / 2 - 1) / 100 - block / 2;
    const int16_t y = cy + direction[1] * (radius - rim / 2 - 1) / 100 - block / 2;
    target.fill(fui::Rect{x, y, block, block}, ink);
  }
  const int16_t mark = std::max<int16_t>(3, side / 4);
  detail::diamond(target,
                  fui::Rect{static_cast<int16_t>(cx - mark / 2), static_cast<int16_t>(cy - mark / 2), mark, mark},
                  filled ? paper : ink, true);
}

inline void card(fui::DrawTarget& target, fui::Rect rect, const fui::ThemeTokens& theme, uint8_t rank, uint8_t suit,
                 bool hidden = false) {
  if (rect.width < 24 || rect.height < 32) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto shade =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const int16_t depth = std::clamp<int16_t>(rect.width / 24, 2, std::max<int16_t>(2, theme.spaceSm));
  const fui::Rect face{rect.x, rect.y, static_cast<int16_t>(rect.width - depth),
                       static_cast<int16_t>(rect.height - depth)};
  target.fill(
      fui::Rect{static_cast<int16_t>(rect.x + depth), static_cast<int16_t>(rect.y + depth), face.width, face.height},
      ink);
  target.fill(face, paper);
  detail::border(target, face, ink);
  const int16_t margin = std::clamp<int16_t>(face.width / 18, 2, std::max<int16_t>(2, theme.spaceSm));
  const auto inner = detail::inset(face, margin);
  if (hidden || rank < 1 || rank > 13 || suit > 3) {
    detail::border(target, inner, ink);
    const auto weave = detail::inset(inner, std::max<int16_t>(2, margin));
    const int16_t cell = std::clamp<int16_t>(face.width / 5, 7, 18);
    for (int16_t y = weave.y; y + cell <= weave.bottom(); y += cell - 1)
      for (int16_t x = weave.x; x + cell <= weave.right(); x += cell - 1)
        detail::diamond(target, fui::Rect{x, y, cell, cell}, ink);
    const int16_t width = inner.width * 3 / 5;
    const int16_t height = std::min<int16_t>(inner.height * 3 / 5, width * 3 / 2);
    const fui::Rect seal{static_cast<int16_t>(inner.x + (inner.width - width) / 2),
                         static_cast<int16_t>(inner.y + (inner.height - height) / 2), width, height};
    detail::diamond(target, seal, paper, true);
    detail::diamond(target, seal, ink);
    const auto motif = detail::inset(seal, std::max<int16_t>(2, margin));
    detail::suit(target, motif, ink, 3);
    return;
  }

  char number[3];
  const char* label = detail::rankLabel(rank, number);
  auto indexStyle = face.width >= theme.minTouchSize * 2 ? theme.bodyText : theme.smallText;
  indexStyle.bold = true;
  indexStyle.align = fui::TextAlign::Left;
  const int16_t labelHeight = target.lineHeight(indexStyle.font);
  const int16_t labelWidth =
      std::max<int16_t>(target.measureText(indexStyle.font, label, indexStyle).width, theme.spaceSm * 2);
  const int16_t cornerSuit = std::clamp<int16_t>(face.width / 8, 6, 14);
  const int16_t verticalInset = labelHeight + std::max<int16_t>(2, theme.spaceXs);
  const int16_t centerHeight = rank > 10 ? labelHeight + cornerSuit + theme.spaceXs : cornerSuit * 3;
  if (inner.width < labelWidth + cornerSuit + theme.spaceXs || inner.height < verticalInset * 2 + centerHeight) {
    detail::compactFace(target, inner, theme, label, suit, ink);
    return;
  }
  target.text(fui::Rect{inner.x, inner.y, std::min<int16_t>(labelWidth + 1, inner.width), labelHeight}, label,
              indexStyle);
  indexStyle.align = fui::TextAlign::Right;
  target.text(fui::Rect{static_cast<int16_t>(inner.right() - std::min(labelWidth + 1, int(inner.width))),
                        static_cast<int16_t>(inner.bottom() - labelHeight),
                        std::min<int16_t>(labelWidth + 1, inner.width), labelHeight},
              label, indexStyle);
  const bool sideIndices = inner.width >= labelWidth * 2 + cornerSuit * 3;
  const int16_t suitX = sideIndices ? inner.x + (labelWidth - cornerSuit) / 2 : inner.right() - cornerSuit;
  const int16_t suitY = sideIndices ? inner.y + labelHeight : inner.y + (labelHeight - cornerSuit) / 2;
  detail::suit(target, fui::Rect{suitX, suitY, cornerSuit, cornerSuit}, ink, suit);
  detail::suit(target,
               fui::Rect{static_cast<int16_t>(inner.x + inner.right() - suitX - cornerSuit),
                         static_cast<int16_t>(inner.y + inner.bottom() - suitY - cornerSuit), cornerSuit, cornerSuit},
               ink, suit, true);

  const fui::Rect field{static_cast<int16_t>(inner.x + inner.width / 5), static_cast<int16_t>(inner.y + verticalInset),
                        static_cast<int16_t>(inner.width * 3 / 5),
                        static_cast<int16_t>(inner.height - verticalInset * 2)};
  if (field.width < 3 || field.height < 3) return;
  if (rank == 1) {
    const int16_t side = std::min(field.width, field.height);
    detail::suit(target,
                 fui::Rect{static_cast<int16_t>(field.x + (field.width - side) / 2),
                           static_cast<int16_t>(field.y + (field.height - side) / 2), side, side},
                 ink, suit);
    return;
  }
  if (rank > 10) {
    detail::diamond(target, field, shade, true);
    detail::diamond(target, field, ink);
    auto courtStyle = theme.titleText;
    courtStyle.bold = true;
    courtStyle.align = fui::TextAlign::Center;
    const int16_t height = target.lineHeight(courtStyle.font);
    if (height > field.height / 2 || target.measureText(courtStyle.font, label, courtStyle).width > field.width / 2)
      courtStyle = indexStyle;
    courtStyle.align = fui::TextAlign::Center;
    const int16_t line = std::min<int16_t>(target.lineHeight(courtStyle.font), field.height);
    const int16_t crest = std::min<int16_t>(field.width / 3, std::max<int16_t>(0, (field.height - line) / 2));
    const int16_t center = field.x + field.width / 2;
    const int16_t top = field.y + (field.height - line - crest) / 2;
    target.text(fui::Rect{field.x, top, field.width, line}, label, courtStyle);
    detail::suit(target,
                 fui::Rect{static_cast<int16_t>(center - crest / 2), static_cast<int16_t>(top + line), crest, crest},
                 ink, suit);
    return;
  }

  const int16_t rows = rank >= 9 ? 4 : rank >= 6 ? 3 : rank == 3 || rank == 5 ? 3 : 2;
  const int16_t pipSize = std::max<int16_t>(3, std::min<int16_t>(field.width / 3, field.height / rows));
  if (rank <= 3) {
    detail::pip(target, field, pipSize, 6, 0, ink, suit);
    detail::pip(target, field, pipSize, 6, 12, ink, suit);
    if (rank == 3) detail::pip(target, field, pipSize, 6, 6, ink, suit);
  } else {
    const int16_t pairs = rank >= 9 ? 4 : rank >= 6 ? 3 : 2;
    for (int16_t row = 0; row < pairs; ++row) {
      detail::pip(target, field, pipSize, 0, row * 12 / (pairs - 1), ink, suit);
      detail::pip(target, field, pipSize, 12, row * 12 / (pairs - 1), ink, suit);
    }
    if (rank == 5 || rank == 9) detail::pip(target, field, pipSize, 6, 6, ink, suit);
    if (rank == 7 || rank == 8) detail::pip(target, field, pipSize, 6, 3, ink, suit);
    if (rank == 8) detail::pip(target, field, pipSize, 6, 9, ink, suit);
    if (rank == 10) {
      detail::pip(target, field, pipSize, 6, 2, ink, suit);
      detail::pip(target, field, pipSize, 6, 10, ink, suit);
    }
  }
}

}  // namespace ui_casino_table
