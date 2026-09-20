#pragma once

#include <FreeInkUICore.h>

#include <algorithm>

inline void drawUiFocusComplete(freeink::ui::DrawTarget& target, const freeink::ui::Rect area,
                                const freeink::ui::ThemeTokens& theme) {
  namespace fui = freeink::ui;
  const int16_t side = std::min(area.width, area.height);
  if (side < 16) return;

  const int16_t x = area.x + (area.width - side) / 2;
  const int16_t y = area.y + (area.height - side) / 2;
  target.fill(fui::Rect{x, y, side, side}, fui::Paint::solid(theme.bodyText.color));

  const auto point = [x, y, side](const int px, const int py) {
    return fui::Point{static_cast<int16_t>(x + px * (side - 1) / 100), static_cast<int16_t>(y + py * (side - 1) / 100)};
  };
  const auto width = static_cast<uint8_t>(std::clamp<int>(side * 12 / 100, 1, 255));
  const auto cutout = fui::Paint::solid(fui::invertedColor(theme.bodyText.color));
  // Thick lines avoid the renderer's temporary polygon node allocation.
  target.line(point(20, 40), point(42, 62), width, cutout);
  target.line(point(42, 62), point(80, 24), width, cutout);
}
