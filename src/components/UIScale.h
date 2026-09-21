#pragma once
#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives all metric tokens from the body font's line
// height (themeTokensForLineHeight). One fixed tier for every board — the
// user-facing UI-scale setting was removed.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId = UI_12_FONT_ID;
  // Titles use the UI font, not a reader font: fui headers draw book and
  // directory titles, and the built-in Ubuntu UI fonts cover Hebrew (plus the
  // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
  // Same font develop's drawHeader used, so script coverage matches develop.
  spec.titleFontId = UI_12_FONT_ID;
  return spec;
}

// Optional display accent for casino tables; controls retain the UI fonts.
inline int uiGameDisplayFontId() {
#ifdef OMIT_FONTS
  return uiScaleSpec().titleFontId;
#else
  return NOTOSERIF_18_FONT_ID;
#endif
}

// A quiet sans-serif display face for the breathing guide.
inline int uiBreathworkDisplayFontId() {
#ifdef OMIT_FONTS
  return uiScaleSpec().titleFontId;
#else
  return NOTOSANS_18_FONT_ID;
#endif
}
