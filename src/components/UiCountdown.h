#pragma once

#include <FreeInkApp.h>

#include <algorithm>
#include <cstdio>

#include "fonts/CountdownDigits.h"

namespace ui_countdown_detail {

// The numeric-only font lives in flash; glyphs paint into the existing
// framebuffer. Tabular advances keep the countdown steady between ticks.
inline void drawReadout(freeink::ui::DrawTarget& target, const freeink::ui::Rect area,
                        const freeink::ui::ThemeTokens& theme, const char* readout) {
  namespace fui = freeink::ui;
  namespace font = countdown_font;
  if (area.empty()) return;

  int naturalWidth = -font::LEFT_BEARING - font::RIGHT_BEARING;
  for (const char* c = readout; *c; ++c) {
    naturalWidth += font::GLYPHS[*c == ':' ? 10 : *c - '0'].advance;
  }
  const int height = std::min<int>({area.height, font::HEIGHT, area.width * font::HEIGHT / naturalWidth});
  if (height < 1) return;
  const int width = naturalWidth * height / font::HEIGHT;
  const int x = area.x + (area.width - width) / 2;
  const int y = area.y + (area.height - height) / 2;
  const auto ink = fui::Paint::solid(theme.bodyText.color);

  int advance = -font::LEFT_BEARING;
  for (const char* c = readout; *c; ++c) {
    const auto& glyph = font::GLYPHS[*c == ':' ? 10 : *c - '0'];
    const int left = (advance + glyph.left) * height / font::HEIGHT;
    const int top = glyph.top * height / font::HEIGHT;
    const int right = (advance + glyph.left + glyph.width) * height / font::HEIGHT;
    const int bottom = (glyph.top + glyph.height) * height / font::HEIGHT;
    const fui::BitmapRef bitmap{font::BITMAPS + glyph.offset, glyph.width, glyph.height, fui::BitmapFormat::BW1, true};
    target.bitmap(fui::Rect{static_cast<int16_t>(x + left), static_cast<int16_t>(y + top),
                            static_cast<int16_t>(right - left), static_cast<int16_t>(bottom - top)},
                  bitmap, fui::BitmapMode::Stretch, ink);
    advance += glyph.advance;
  }
}

}  // namespace ui_countdown_detail

inline void drawUiCountdown(freeink::ui::DrawTarget& target, const freeink::ui::Rect area,
                            const freeink::ui::ThemeTokens& theme, const uint32_t seconds) {
  char readout[16];
  snprintf(readout, sizeof(readout), "%02u:%02u", static_cast<unsigned>(seconds / 60),
           static_cast<unsigned>(seconds % 60));
  ui_countdown_detail::drawReadout(target, area, theme, readout);
}

inline void drawUiMinutes(freeink::ui::DrawTarget& target, const freeink::ui::Rect area,
                          const freeink::ui::ThemeTokens& theme, const uint32_t minutes) {
  char readout[16];
  snprintf(readout, sizeof(readout), "%u", static_cast<unsigned>(minutes));
  ui_countdown_detail::drawReadout(target, area, theme, readout);
}
