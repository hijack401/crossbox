#pragma once

#include <FreeInkApp.h>

#include <algorithm>
#include <cstdint>

namespace ui_farkle {
namespace fui = freeink::ui;

inline void die(fui::DrawTarget& target, fui::Rect area, const fui::Paint& ink, uint8_t value, bool border = true) {
  const int16_t side = std::min(area.width, area.height);
  if (side < 6) return;
  const fui::Rect box{static_cast<int16_t>(area.x + (area.width - side) / 2),
                      static_cast<int16_t>(area.y + (area.height - side) / 2), side, side};
  if (border) target.stroke(box, ink, std::max<int>(1, side / 32));
  if (!value || value > 6) {
    target.fill(fui::Rect{static_cast<int16_t>(box.x + side / 3), static_cast<int16_t>(box.y + side / 2),
                          static_cast<int16_t>(side / 3), 2},
                ink);
    return;
  }
  static constexpr uint16_t PIPS[] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16d};
  const int16_t dot = std::max<int16_t>(2, side / 7);
  for (int i = 0; i < 9; ++i) {
    if (!(PIPS[value] & (1 << i))) continue;
    const int16_t x = box.x + side * (1 + i % 3) / 4 - dot / 2;
    const int16_t y = box.y + side * (1 + i / 3) / 4 - dot / 2;
    target.fill(fui::Rect{x, y, dot, dot}, ink, dot / 2);
  }
}

namespace detail {

inline void pip(fui::DrawTarget& target, int16_t x, int16_t y, int16_t radius, const fui::Paint& ink) {
  for (int16_t row = -radius; row <= radius; ++row) {
    int16_t half = 0;
    while ((half + 1) * (half + 1) + row * row <= radius * radius) ++half;
    target.fill(
        fui::Rect{static_cast<int16_t>(x - half), static_cast<int16_t>(y + row), static_cast<int16_t>(half * 2 + 1), 1},
        ink);
  }
}

inline void frame(fui::DrawTarget& target, fui::Rect box, int16_t width, const fui::Paint& ink) {
  target.fill(fui::Rect{box.x, box.y, box.width, width}, ink);
  target.fill(fui::Rect{box.x, static_cast<int16_t>(box.bottom() - width), box.width, width}, ink);
  target.fill(fui::Rect{box.x, box.y, width, box.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(box.right() - width), box.y, width, box.height}, ink);
}

inline void diamond(fui::DrawTarget& target, int16_t x, int16_t y, int16_t radius, const fui::Paint& ink) {
  for (int16_t row = -radius; row <= radius; ++row) {
    const int16_t half = radius - (row < 0 ? -row : row);
    target.fill(
        fui::Rect{static_cast<int16_t>(x - half), static_cast<int16_t>(y + row), static_cast<int16_t>(half * 2 + 1), 1},
        ink);
  }
}

}  // namespace detail

inline void carvedDie(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, uint8_t value,
                      bool selected = false, bool held = false, bool focused = false) {
  const int16_t side = std::min(area.width, area.height);
  if (side < 14) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const auto muted =
      fui::Paint::dither(theme.bodyText.color == fui::Color::White ? fui::Color::DarkGray : fui::Color::LightGray);
  const int16_t x = area.x + (area.width - side) / 2;
  const int16_t y = area.y + (area.height - side) / 2;
  const int16_t inset = std::max<int16_t>(2, side / 24);
  const int16_t depth = std::max<int16_t>(2, side / 10);
  const int16_t size = side - inset * 2 - depth;
  const int16_t weight = std::max<int16_t>(1, side / 36);
  const fui::Rect face{static_cast<int16_t>(x + inset), static_cast<int16_t>(y + inset), size, size};

  // Fixed scanlines keep the perspective faces off the heap-backed polygon path.
  for (int16_t row = 0; row < size + depth; ++row) {
    const int16_t left = std::max<int16_t>(0, row - size + 1);
    const int16_t right = std::min(row, depth);
    target.fill(fui::Rect{static_cast<int16_t>(face.right() - 1 + left), static_cast<int16_t>(face.y + row),
                          static_cast<int16_t>(right - left + 1), 1},
                held ? muted : ink);
  }
  for (int16_t row = 0; row <= depth; ++row)
    target.fill(fui::Rect{static_cast<int16_t>(face.x + row), static_cast<int16_t>(face.bottom() - 1 + row), size, 1},
                held ? muted : ink);

  target.fill(face, selected ? ink : paper);
  detail::frame(target, face, weight, held ? muted : ink);
  if (size >= 30) {
    const int16_t bevel = weight + 2;
    const fui::Rect inner{static_cast<int16_t>(face.x + bevel), static_cast<int16_t>(face.y + bevel),
                          static_cast<int16_t>(size - bevel * 2), static_cast<int16_t>(size - bevel * 2)};
    detail::frame(target, inner, 1, selected ? paper : muted);
  }

  const auto pipInk = selected ? paper : held ? muted : ink;
  static constexpr uint16_t PIPS[] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16d};
  if (value && value <= 6) {
    const int16_t radius = std::max<int16_t>(1, size / 12);
    for (uint8_t i = 0; i < 9; ++i) {
      if (!(PIPS[value] & (1 << i))) continue;
      detail::pip(target, face.x + size * (1 + i % 3) / 4, face.y + size * (1 + i / 3) / 4, radius, pipInk);
    }
  } else {
    detail::diamond(target, face.x + size / 2, face.y + size / 2, std::max<int16_t>(1, size / 9), pipInk);
  }

  if (focused) {
    const int16_t corner = std::max<int16_t>(3, side / 7);
    for (int16_t edge = 0; edge < 2; ++edge) {
      const int16_t cx = x + (edge ? side - corner : 0);
      target.fill(fui::Rect{cx, y, corner, 1}, ink);
      target.fill(fui::Rect{cx, static_cast<int16_t>(y + side - 1), corner, 1}, ink);
      const int16_t cy = y + (edge ? side - corner : 0);
      target.fill(fui::Rect{x, cy, 1, corner}, ink);
      target.fill(fui::Rect{static_cast<int16_t>(x + side - 1), cy, 1, corner}, ink);
    }
  }
}

inline void ornament(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  if (area.width < 18 || area.height < 3) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const int16_t radius = std::min<int16_t>(4, (area.height - 1) / 2);
  const int16_t x = area.x + area.width / 2;
  const int16_t y = area.y + area.height / 2;
  const int16_t gap = radius + 5;
  const int16_t width = area.width / 2 - gap;
  if (width > 0) {
    target.fill(fui::Rect{area.x, y, width, 1}, ink);
    target.fill(fui::Rect{static_cast<int16_t>(x + gap), y, static_cast<int16_t>(area.right() - x - gap), 1}, ink);
  }
  detail::diamond(target, x, y, radius, ink);
}

inline void tray(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme) {
  if (area.width < 20 || area.height < 20) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  target.fill(area, paper);
  detail::frame(target, area, 1, ink);
  const int16_t inset = std::min<int16_t>(6, std::min(area.width, area.height) / 8);
  const fui::Rect inner{static_cast<int16_t>(area.x + inset), static_cast<int16_t>(area.y + inset),
                        static_cast<int16_t>(area.width - inset * 2), static_cast<int16_t>(area.height - inset * 2)};
  detail::frame(target, inner, 1, ink);
  for (int16_t corner = 0; corner < 4; ++corner) {
    const int16_t x = corner & 1 ? area.right() - 1 - inset / 2 : area.x + inset / 2;
    const int16_t y = corner & 2 ? area.bottom() - 1 - inset / 2 : area.y + inset / 2;
    detail::diamond(target, x, y, 1, ink);
  }
}

inline void crest(fui::DrawTarget& target, fui::Rect area, const fui::ThemeTokens& theme, bool victory = true) {
  if (area.width < 18 || area.height < 24) return;
  const auto ink = fui::Paint::solid(theme.bodyText.color);
  const auto paper = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  const int16_t width = std::min<int16_t>(area.width, area.height * 4 / 5);
  const int16_t height = std::min<int16_t>(area.height, width * 5 / 4);
  const int16_t x = area.x + area.width / 2;
  const int16_t y = area.y + (area.height - height) / 2;
  const int16_t half = (width - 1) / 2;
  const int16_t shoulder = height * 3 / 5;
  const int16_t rim = std::max<int16_t>(2, width / 18);
  for (int16_t row = 0; row < height; ++row) {
    const int16_t edge = row < shoulder ? half : half * (height - row - 1) / (height - shoulder - 1);
    target.fill(
        fui::Rect{static_cast<int16_t>(x - edge), static_cast<int16_t>(y + row), static_cast<int16_t>(edge * 2 + 1), 1},
        ink);
    if (row >= rim && edge >= rim)
      target.fill(fui::Rect{static_cast<int16_t>(x - edge + rim), static_cast<int16_t>(y + row),
                            static_cast<int16_t>((edge - rim) * 2 + 1), 1},
                  paper);
  }
  const int16_t radius = std::max<int16_t>(1, width / 18);
  const int16_t spread = width / 5;
  const int16_t centerY = y + height * 2 / 5;
  detail::pip(target, x, centerY, radius, ink);
  for (int16_t sign = -1; sign <= 1; sign += 2) {
    detail::pip(target, x + sign * spread, centerY - spread, radius, ink);
    detail::pip(target, x + sign * spread, centerY + spread, radius, ink);
  }
  if (!victory) {
    const int16_t split = std::max<int16_t>(2, width / 15);
    for (int16_t row = 0; row < height; ++row) {
      const int16_t offset = row < height / 3 ? -split : row < height * 2 / 3 ? split : 0;
      target.fill(fui::Rect{static_cast<int16_t>(x + offset - split / 2), static_cast<int16_t>(y + row), split, 1},
                  paper);
    }
  }
}
}  // namespace ui_farkle
