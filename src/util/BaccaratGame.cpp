#include "BaccaratGame.h"

#include <cstring>

namespace {
using Bet = BaccaratGame::Bet;

bool validBet(const Bet bet) { return bet == Bet::Player || bet == Bet::Banker || bet == Bet::Tie; }

bool validWager(const int64_t cents) {
  return cents >= BaccaratGame::MIN_BET_CENTS && cents <= BaccaratGame::MAX_BET_CENTS && cents % 100 == 0;
}

bool bankerDraws(const uint8_t total, const bool playerDrew, const uint8_t playerThird) {
  if (!playerDrew) return total <= 5;
  if (total <= 2) return true;
  if (total == 3) return playerThird != 8;
  if (total == 4) return playerThird >= 2 && playerThird <= 7;
  if (total == 5) return playerThird >= 4 && playerThird <= 7;
  return total == 6 && (playerThird == 6 || playerThird == 7);
}

Bet winner(const BaccaratGame::Hand& player, const BaccaratGame::Hand& banker) {
  const uint8_t playerTotal = BaccaratGame::value(player);
  const uint8_t bankerTotal = BaccaratGame::value(banker);
  if (playerTotal == bankerTotal) return Bet::Tie;
  return playerTotal > bankerTotal ? Bet::Player : Bet::Banker;
}

int64_t payout(const Bet bet, const Bet result, const int64_t wager) {
  if (bet != result) return result == Bet::Tie ? wager : 0;
  if (bet == Bet::Tie) return wager * 9;
  if (bet == Bet::Banker) return wager * 2 - wager / 20;
  return wager * 2;
}

bool validHand(const BaccaratGame::Hand& hand) {
  if (hand.cardCount > BaccaratGame::MAX_CARDS) return false;
  for (size_t i = 0; i < BaccaratGame::MAX_CARDS; ++i) {
    if (i < hand.cardCount ? hand.cards[i] >= 52 : hand.cards[i] != 0) return false;
  }
  return true;
}
}  // namespace

uint8_t BaccaratGame::cardValue(const uint8_t card) {
  const uint8_t rank = card % 13 + 1;
  return rank < 10 ? rank : 0;
}

uint8_t BaccaratGame::value(const Hand& hand) {
  uint8_t total = 0;
  for (size_t i = 0; i < hand.cardCount && i < MAX_CARDS; ++i) total += cardValue(hand.cards[i]);
  return total % 10;
}

bool BaccaratGame::natural(const Hand& hand) { return hand.cardCount == 2 && value(hand) >= 8; }

void BaccaratGame::clearRound() {
  current.wagerCents = 0;
  current.returnCents = 0;
  current.player = Hand{};
  current.banker = Hand{};
  current.winner = Bet::Tie;
  current.phase = Phase::Betting;
  current.revealedCards = 0;
}

void BaccaratGame::reset() {
  std::memset(current.shoe, 0, sizeof(current.shoe));
  current.shoePosition = 0;
  current.shoeReady = 0;
  current.bet = Bet::Player;
  clearRound();
}

bool BaccaratGame::canBet(const int64_t wagerCents, const int64_t availableCents) const {
  return current.phase == Phase::Betting && validWager(wagerCents) && availableCents >= wagerCents &&
         availableCents <= MAX_BET_CENTS;
}

void BaccaratGame::shuffle(const Random random, void* context) {
  for (size_t i = 0; i < SHOE_CARDS; ++i) current.shoe[i] = i % 52;
  for (uint32_t i = SHOE_CARDS - 1; i > 0; --i) {
    const uint32_t range = i + 1;
    const uint32_t threshold = (uint32_t{0} - range) % range;
    uint32_t sample;
    do {
      sample = random(context);
    } while (sample < threshold);
    const uint32_t selected = sample % range;
    const uint8_t card = current.shoe[i];
    current.shoe[i] = current.shoe[selected];
    current.shoe[selected] = card;
  }
  current.shoePosition = 0;
  current.shoeReady = 1;
}

void BaccaratGame::deal(Hand& hand) { hand.cards[hand.cardCount++] = current.shoe[current.shoePosition++]; }

bool BaccaratGame::startRound(const Bet bet, const int64_t wagerCents, const int64_t availableCents,
                              const Random random, void* context) {
  if (!validBet(bet) || !canBet(wagerCents, availableCents)) return false;
  if (!current.shoeReady || SHOE_CARDS - current.shoePosition < 2 * MAX_CARDS) {
    if (!random) return false;
    shuffle(random, context);
  }
  clearRound();
  current.bet = bet;
  current.wagerCents = wagerCents;
  deal(current.player);
  deal(current.banker);
  deal(current.player);
  deal(current.banker);
  if (!natural(current.player) && !natural(current.banker)) {
    const uint8_t bankerTotal = value(current.banker);
    if (value(current.player) <= 5) deal(current.player);
    if (bankerDraws(bankerTotal, current.player.cardCount == 3, cardValue(current.player.cards[2]))) {
      deal(current.banker);
    }
  }
  current.winner = winner(current.player, current.banker);
  current.returnCents = payout(bet, current.winner, wagerCents);
  current.phase = Phase::Revealing;
  return true;
}

bool BaccaratGame::revealNext() {
  if (current.phase != Phase::Revealing) return false;
  if (++current.revealedCards == current.player.cardCount + current.banker.cardCount) {
    current.phase = Phase::Settled;
  }
  return true;
}

bool BaccaratGame::cardRevealed(const bool banker, const uint8_t cardIndex) const {
  const Hand& hand = banker ? current.banker : current.player;
  if (current.phase == Phase::Betting || cardIndex >= hand.cardCount) return false;
  const uint8_t position = cardIndex < 2 ? cardIndex * 2 + banker : (banker ? 2 + current.player.cardCount : 4);
  return current.revealedCards > position;
}

bool BaccaratGame::nextRound() {
  if (current.phase != Phase::Settled) return false;
  clearRound();
  return true;
}

bool BaccaratGame::validateState(const State& state) {
  if (!validBet(state.bet) || !validBet(state.winner) ||
      (state.phase != Phase::Betting && state.phase != Phase::Settled && state.phase != Phase::Revealing) ||
      state.shoeReady > 1 || state.shoePosition > SHOE_CARDS || !validHand(state.player) || !validHand(state.banker)) {
    return false;
  }
  uint8_t copies[52]{};
  for (const uint8_t card : state.shoe) {
    if (state.shoeReady) {
      if (card >= 52 || ++copies[card] > 8) return false;
    } else if (card != 0) {
      return false;
    }
  }
  if (!state.shoeReady && state.shoePosition != 0) return false;
  if (state.phase == Phase::Betting) {
    return state.wagerCents == 0 && state.returnCents == 0 && state.player.cardCount == 0 &&
           state.banker.cardCount == 0 && state.winner == Bet::Tie && state.revealedCards == 0;
  }
  if (!state.shoeReady || !validWager(state.wagerCents) || state.player.cardCount < 2 || state.banker.cardCount < 2) {
    return false;
  }
  const uint8_t dealt = state.player.cardCount + state.banker.cardCount;
  if (state.phase == Phase::Revealing ? state.revealedCards >= dealt : state.revealedCards != dealt) return false;
  if (state.shoePosition < dealt || SHOE_CARDS - (state.shoePosition - dealt) < 2 * MAX_CARDS) return false;
  uint16_t position = state.shoePosition - dealt;
  for (uint8_t i = 0; i < 2; ++i) {
    if (state.player.cards[i] != state.shoe[position++] || state.banker.cards[i] != state.shoe[position++])
      return false;
  }
  if (state.player.cardCount == 3 && state.player.cards[2] != state.shoe[position++]) return false;
  if (state.banker.cardCount == 3 && state.banker.cards[2] != state.shoe[position]) return false;

  const uint8_t playerTotal = (cardValue(state.player.cards[0]) + cardValue(state.player.cards[1])) % 10;
  const uint8_t bankerTotal = (cardValue(state.banker.cards[0]) + cardValue(state.banker.cards[1])) % 10;
  const bool isNatural = playerTotal >= 8 || bankerTotal >= 8;
  const bool playerDrew = !isNatural && playerTotal <= 5;
  const bool bankerDrew = !isNatural && bankerDraws(bankerTotal, playerDrew, cardValue(state.player.cards[2]));
  if (state.player.cardCount != (playerDrew ? 3 : 2) || state.banker.cardCount != (bankerDrew ? 3 : 2)) return false;
  return state.winner == winner(state.player, state.banker) &&
         state.returnCents == payout(state.bet, state.winner, state.wagerCents);
}

bool BaccaratGame::restore(const State& state) {
  if (!validateState(state)) return false;
  current = state;
  return true;
}
