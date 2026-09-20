#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/PomodoroTimer.h"

class PomodoroActivity final : public Activity, private UiAppHost {
 public:
  explicit PomodoroActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool allowsControlCenter() const override;
  bool handleHomeGesture() override;

 private:
  enum class View : uint8_t { Setup, Duration, Session, StopPrompt };
  static constexpr uint16_t PRESETS[] = {25, 50, 90};
  static constexpr int MAX_MINUTES = PomodoroTimer::MAX_DURATION_SECONDS / 60;
  static constexpr freeink::ui::ActionId ACTION_CONTROL = 1;
  static constexpr freeink::ui::ActionId ACTION_DURATION_SLIDER = 2;
  static constexpr freeink::ui::ActionId ACTION_DURATION_STEP = 3;

  PomodoroTimer timer;
  // Reused render scratch: ButtonProps is too large for a small local buffer.
  freeink::ui::ButtonProps controlProps;
  View view = View::Setup;
  uint16_t selectedMinutes = 25;
  uint16_t draftMinutes = 25;
  int selectedControl = 4;
  uint32_t displayedSeconds = 25 * 60;
  uint32_t lastCleanRefreshAt = 0;
  uint32_t lastDurationStepAt = 0;
  bool cleanRefresh = true;
  bool exitAfterStop = false;
  bool buttonNavigation = false;
  bool draggingDuration = false;

  static void timerScreen(UiScreen& screen, void* user);
  static void onControl(const freeink::ui::ActionEvent& event, void* user);
  static void onDurationSlider(const freeink::ui::ActionEvent& event, void* user);
  static void onDurationStep(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  void buildDurationScreen(UiScreen& screen);
  void buildCompleteScreen(UiScreen& screen);
  void drawRemainingProgress(UiScreen& screen, freeink::ui::Rect rect);
  void drawPresets(UiScreen& screen, freeink::ui::Rect rect);
  void drawControl(UiScreen& screen, freeink::ui::Rect rect, const char* label, int control, bool primary = false,
                   bool checked = false, freeink::ui::ActionId action = ACTION_CONTROL, bool enabled = true);
  void activate(int control);
  void goBack();
  void selectCustomDuration();
  void setDraftMinutes(int minutes);
  void handleDurationButtons();
  void startSession();
  void updateTimer(uint32_t nowMs);
};
