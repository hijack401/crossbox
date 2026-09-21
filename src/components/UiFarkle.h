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
}  // namespace ui_farkle
