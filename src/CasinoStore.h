#pragma once

#include "util/BaccaratGame.h"
#include "util/BlackjackGame.h"
#include "util/RouletteGame.h"

class CasinoStore {
 public:
  enum class LoadStatus { NotLoaded, Empty, Loaded, Recovered, Error };

  BlackjackGame& game() { return blackjack; }
  const BlackjackGame& game() const { return blackjack; }
  BaccaratGame& baccarat() { return baccaratGame; }
  const BaccaratGame& baccarat() const { return baccaratGame; }
  RouletteGame& roulette() { return rouletteGame; }
  const RouletteGame& roulette() const { return rouletteGame; }
  bool dealBaccarat(BaccaratGame::Bet bet, int64_t wagerCents, BaccaratGame::Random random, void* context = nullptr);
  bool revealBaccarat();
  bool spinRoulette(RouletteGame::Random random, void* context = nullptr);
  bool revealRoulette();
  bool load();
  bool save();
  bool isReadOnly() const { return status == LoadStatus::NotLoaded || status == LoadStatus::Error; }
  LoadStatus loadStatus() const { return status; }

 private:
  BlackjackGame blackjack;
  BaccaratGame baccaratGame;
  RouletteGame rouletteGame;
  LoadStatus status = LoadStatus::NotLoaded;
  bool primaryValid = false;
  bool recoveredFromTemporary = false;
};
