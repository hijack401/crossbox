#pragma once

#include "CasinoStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

class CasinoActivity final : public Activity, private UiAppHost {
 public:
  CasinoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool preventAutoSleep() override { return dirty; }
  bool allowsControlCenter() const override { return !dirty; }

 private:
  enum class View : uint8_t { Lobby, Table, BetEntry, Rules, StorageError };
  enum Control : int16_t {
    BLACKJACK,
    RULES,
    CLOSE,
    DEAL,
    CUSTOM,
    MAX_BET,
    HIT,
    STAND,
    DOUBLE,
    SPLIT,
    SURRENDER,
    INSURE,
    DECLINE,
    AGAIN,
    RETRY,
    ERASE,
    ACCEPT_BET,
    PREVIOUS_RULE,
    NEXT_RULE,
    PRESET_BASE = 40,
    HAND_BASE = 50,
    DIGIT_BASE = 60
  };
  static constexpr freeink::ui::ActionId ACTION_CONTROL = 1;
  static constexpr int64_t PRESETS[] = {1000, 2000, 4000, 8000};
  CasinoStore store;
  freeink::ui::ButtonProps buttonProps;
  int16_t focusTargets[24]{};
  int focusCount = 0;
  int selectedControl = BLACKJACK;
  int displayedHand = 0;
  int rulesPage = 0;
  int rulesPerPage = 1;
  int64_t betCents = 2000;
  uint32_t lastClockCheck = 0;
  char betInput[10]{};
  View view = View::Lobby;
  View returnView = View::Lobby;
  View storageReturnView = View::Lobby;
  bool dirty = false;
  bool loadFailed = false;
  bool clockValid = false;
  bool buttonNavigation = false;

  static void casinoScreen(UiScreen& screen, void* user);
  static void onControl(const freeink::ui::ActionEvent& event, void* user);
  static uint32_t randomWord(void*);
  void buildScreen(UiScreen& screen);
  void buildLobby(UiScreen& screen);
  void buildTable(UiScreen& screen);
  void buildBetting(UiScreen& screen);
  void buildRound(UiScreen& screen);
  void buildBetEntry(UiScreen& screen);
  void buildRules(UiScreen& screen);
  void buildStorageError(UiScreen& screen);
  void drawButton(UiScreen& screen, freeink::ui::Rect rect, const char* label, int control, bool primary = false,
                  bool enabled = true, bool outlined = true);
  void drawLabel(UiScreen& screen, freeink::ui::Rect rect, const char* label, bool title = false,
                 freeink::ui::TextAlign align = freeink::ui::TextAlign::Left);
  void drawHands(UiScreen& screen, freeink::ui::Rect area);
  void drawHand(UiScreen& screen, freeink::ui::Rect area, const BlackjackGame::Hand& hand, bool dealer);
  void drawLobbyGame(UiScreen& screen, freeink::ui::Rect row, uint8_t game);
  void activate(int control);
  void goBack();
  void refresh();
  bool saveChanges();
  void checkDailyCredit();
  void normalizeBet();
  int64_t enteredBet() const;
};
