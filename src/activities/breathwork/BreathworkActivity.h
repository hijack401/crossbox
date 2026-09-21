#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/BreathworkSession.h"

class BreathworkActivity final : public Activity, private UiAppHost {
 public:
  BreathworkActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool allowsControlCenter() const override;
  bool handleHomeGesture() override;

 private:
  enum class View : uint8_t { Presets, Setup, Help, Ready, Session, Complete };
  enum Control : int16_t {
    HOW = 1,
    START,
    FASTER,
    STEADY,
    CYCLES_BASE = 10,
    PAUSE = 20,
    END,
    DONE,
    AGAIN,
    CLOSE_HELP,
    CANCEL_READY,
    HELP_PREVIOUS,
    HELP_NEXT,
    BOX_MINUS,
    BOX_PLUS,
    PRESET_BASE = 100
  };
  static constexpr freeink::ui::ActionId ACTION_CONTROL = 1;
  static constexpr uint8_t BOX_PRESET = 2;
  static constexpr uint8_t DEFAULT_BOX_SECONDS = 4;
  static constexpr uint8_t MIN_BOX_SECONDS = 2;
  static constexpr uint8_t MAX_BOX_SECONDS = 10;
  BreathworkSession session;
  freeink::ui::ButtonProps buttonProps;
  View view = View::Presets;
  View helpReturn = View::Setup;
  uint8_t preset = 0;
  uint8_t cycleChoice = 1;
  uint8_t helpPage = 0;
  uint8_t boxSeconds = DEFAULT_BOX_SECONDS;
  uint16_t countMs = 1000;
  int16_t selectedControl = PRESET_BASE;
  int16_t focusTargets[12]{};
  uint8_t focusCount = 0;
  uint32_t readyAt = 0;
  uint32_t renderAt = 0;
  uint32_t displayedTick = UINT32_MAX;
  uint32_t lastCleanRefreshAt = 0;
  bool cleanRefresh = true;
  bool buttonNavigation = false;
  bool endedEarly = false;

  static void breathworkScreen(UiScreen& screen, void* user);
  static void onControl(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  void buildPresets(UiScreen& screen);
  void buildSetup(UiScreen& screen);
  void buildHelp(UiScreen& screen);
  void buildSession(UiScreen& screen);
  void buildComplete(UiScreen& screen);
  void drawButton(UiScreen& screen, freeink::ui::Rect rect, const char* label, int16_t control, bool selected = false,
                  bool outlined = true);
  void drawText(UiScreen& screen, freeink::ui::Rect rect, const char* label, bool title = false,
                freeink::ui::TextAlign align = freeink::ui::TextAlign::Left);
  void drawPattern(UiScreen& screen, freeink::ui::Rect rect, bool active = false);
  void drawHeader(UiScreen& screen, const char* label, bool help = false);
  uint16_t cycles() const;
  uint8_t phaseCount(uint8_t phase) const;
  uint32_t durationMs() const;
  bool updateSession(uint32_t nowMs);
  void activate(int16_t control);
  void goBack();
  void finishSession();
};
