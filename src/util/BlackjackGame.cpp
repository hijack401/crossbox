#include "BlackjackGame.h"

#include <cstring>

uint8_t BlackjackGame::cardValue(const uint8_t card) {
  const uint8_t face = rank(card);
  return face > 10 ? 10 : face;
}

uint8_t BlackjackGame::value(const Hand& hand) {
  uint8_t total = 0;
  bool ace = false;
  for (size_t i = 0; i < hand.cardCount && i < MAX_CARDS; ++i) {
    const uint8_t points = cardValue(hand.cards[i]);
    total += points;
    ace = ace || points == 1;
  }
  return ace && total <= 11 ? total + 10 : total;
}

bool BlackjackGame::isSoft(const Hand& hand) {
  uint8_t total = 0;
  bool ace = false;
  for (size_t i = 0; i < hand.cardCount && i < MAX_CARDS; ++i) {
    const uint8_t points = cardValue(hand.cards[i]);
    total += points;
    ace = ace || points == 1;
  }
  return ace && total <= 11;
}

bool BlackjackGame::natural(const Hand& hand) { return !hand.fromSplit && hand.cardCount == 2 && value(hand) == 21; }

void BlackjackGame::reset() {
  current.balanceCents = STARTING_BALANCE_CENTS;
  current.lastCreditDay = 0;
  std::memset(current.shoe, 0, sizeof(current.shoe));
  current.shoePosition = 0;
  current.shoeReady = 0;
  clearRound();
}

void BlackjackGame::clearRound() {
  for (auto& hand : current.hands) hand = Hand{};
  current.dealer = Hand{};
  current.handCount = 0;
  current.activeHand = 0;
  current.insuranceCents = 0;
  current.insuranceReturnCents = 0;
  current.roundWagerCents = 0;
  current.roundReturnCents = 0;
  current.phase = Phase::Betting;
}

void BlackjackGame::credit(const int64_t cents) {
  const int64_t room = MAX_BALANCE_CENTS - current.balanceCents;
  current.balanceCents += cents < room ? cents : room;
}

bool BlackjackGame::applyDailyCredit(const int32_t day) {
  if (day <= 0 || day > MAX_CREDIT_DAY || day <= current.lastCreditDay) return false;
  if (current.lastCreditDay != 0) {
    credit(static_cast<int64_t>(day - current.lastCreditDay) * DAILY_CREDIT_CENTS);
  }
  current.lastCreditDay = day;
  return true;
}

bool BlackjackGame::settleExternalWager(const int64_t wagerCents, const int64_t returnCents) {
  if (wagerCents < MIN_BET_CENTS || wagerCents > MAX_BALANCE_CENTS || wagerCents % 100 != 0 ||
      wagerCents > current.balanceCents || returnCents < 0 || returnCents > wagerCents * 9)
    return false;
  current.balanceCents -= wagerCents;
  credit(returnCents);
  return true;
}

bool BlackjackGame::creditExternalReturn(const int64_t returnCents) {
  if (returnCents < 0 || returnCents > MAX_BALANCE_CENTS * 9) return false;
  credit(returnCents);
  return true;
}

bool BlackjackGame::canBet(const int64_t betCents) const {
  return current.phase == Phase::Betting && betCents >= MIN_BET_CENTS && betCents % 100 == 0 &&
         betCents <= current.balanceCents;
}

void BlackjackGame::shuffle(const Random random, void* context) {
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

void BlackjackGame::deal(Hand& hand) {
  if (hand.cardCount < MAX_CARDS && current.shoePosition < SHOE_CARDS) {
    hand.cards[hand.cardCount++] = current.shoe[current.shoePosition++];
  }
}

bool BlackjackGame::startRound(const int64_t betCents, const Random random, void* context) {
  if (!canBet(betCents)) return false;
  if (!current.shoeReady || SHOE_CARDS - current.shoePosition < ROUND_RESERVE) {
    if (!random) return false;
    shuffle(random, context);
  }
  clearRound();
  current.balanceCents -= betCents;
  current.roundWagerCents = betCents;
  current.handCount = 1;
  current.hands[0].wagerCents = betCents;
  deal(current.hands[0]);
  deal(current.dealer);
  deal(current.hands[0]);
  deal(current.dealer);
  if (rank(current.dealer.cards[0]) == 1) {
    current.phase = Phase::Insurance;
  } else {
    afterPeek();
  }
  return true;
}

void BlackjackGame::afterPeek() {
  current.phase = Phase::Playing;
  if (natural(current.dealer) || natural(current.hands[0])) {
    settle();
  }
}

bool BlackjackGame::canInsure() const {
  return current.phase == Phase::Insurance && current.balanceCents >= current.hands[0].wagerCents / 2;
}

bool BlackjackGame::insurance(const bool take) {
  if (current.phase != Phase::Insurance || (take && !canInsure())) return false;
  if (take) {
    current.insuranceCents = current.hands[0].wagerCents / 2;
    current.balanceCents -= current.insuranceCents;
    current.roundWagerCents += current.insuranceCents;
  }
  afterPeek();
  return true;
}

bool BlackjackGame::canStand() const {
  return current.phase == Phase::Playing && current.activeHand < current.handCount &&
         !current.hands[current.activeHand].finished;
}

bool BlackjackGame::canHit() const {
  return canStand() && value(current.hands[current.activeHand]) < 21 &&
         current.hands[current.activeHand].cardCount < MAX_CARDS;
}

bool BlackjackGame::canDouble() const {
  if (!canHit()) return false;
  const Hand& hand = current.hands[current.activeHand];
  return hand.cardCount == 2 && !hand.splitAces && current.balanceCents >= hand.wagerCents;
}

bool BlackjackGame::canSplit() const {
  if (!canHit() || current.handCount == MAX_HANDS) return false;
  const Hand& hand = current.hands[current.activeHand];
  return hand.cardCount == 2 && !hand.splitAces && cardValue(hand.cards[0]) == cardValue(hand.cards[1]) &&
         current.balanceCents >= hand.wagerCents;
}

bool BlackjackGame::canSurrender() const {
  return canStand() && current.handCount == 1 && current.hands[0].cardCount == 2 && !current.hands[0].fromSplit;
}

bool BlackjackGame::hit() {
  if (!canHit()) return false;
  Hand& hand = current.hands[current.activeHand];
  deal(hand);
  if (value(hand) >= 21 || hand.cardCount == MAX_CARDS) {
    hand.finished = 1;
    if (value(hand) > 21) hand.result = Result::Bust;
    advance();
  }
  return true;
}

bool BlackjackGame::stand() {
  if (!canStand()) return false;
  current.hands[current.activeHand].finished = 1;
  advance();
  return true;
}

bool BlackjackGame::doubleDown() {
  if (!canDouble()) return false;
  Hand& hand = current.hands[current.activeHand];
  current.balanceCents -= hand.wagerCents;
  current.roundWagerCents += hand.wagerCents;
  hand.wagerCents *= 2;
  hand.doubled = 1;
  deal(hand);
  hand.finished = 1;
  if (value(hand) > 21) hand.result = Result::Bust;
  advance();
  return true;
}

bool BlackjackGame::split() {
  if (!canSplit()) return false;
  const size_t index = current.activeHand;
  for (size_t i = current.handCount; i > index + 1; --i) current.hands[i] = current.hands[i - 1];
  Hand& first = current.hands[index];
  Hand& second = current.hands[index + 1];
  second = Hand{};
  second.cards[0] = first.cards[1];
  second.cardCount = 1;
  second.wagerCents = first.wagerCents;
  second.fromSplit = first.fromSplit = 1;
  second.splitAces = first.splitAces = rank(first.cards[0]) == 1;
  first.cards[1] = 0;
  first.cardCount = 1;
  current.balanceCents -= first.wagerCents;
  current.roundWagerCents += first.wagerCents;
  ++current.handCount;
  deal(first);
  deal(second);
  first.finished = first.splitAces || value(first) == 21;
  second.finished = second.splitAces || value(second) == 21;
  if (first.finished) advance();
  return true;
}

bool BlackjackGame::surrender() {
  if (!canSurrender()) return false;
  current.hands[0].result = Result::Surrender;
  current.hands[0].finished = 1;
  settle();
  return true;
}

void BlackjackGame::advance() {
  for (size_t i = current.activeHand; i < current.handCount; ++i) {
    if (!current.hands[i].finished) {
      current.activeHand = i;
      return;
    }
  }
  settle();
}

void BlackjackGame::settle() {
  if (current.phase == Phase::Settled) return;
  const bool dealerBlackjack = natural(current.dealer);
  bool dealerNeedsCards = false;
  for (size_t i = 0; i < current.handCount; ++i) {
    const Hand& hand = current.hands[i];
    dealerNeedsCards = dealerNeedsCards || (value(hand) <= 21 && !natural(hand) && hand.result != Result::Surrender);
  }
  if (!dealerBlackjack && dealerNeedsCards) {
    while (value(current.dealer) < 17 && current.dealer.cardCount < MAX_CARDS) deal(current.dealer);
  }
  const uint8_t dealerTotal = value(current.dealer);
  current.insuranceReturnCents = dealerBlackjack ? current.insuranceCents * 3 : 0;
  current.roundReturnCents = current.insuranceReturnCents;
  for (size_t i = 0; i < current.handCount; ++i) {
    Hand& hand = current.hands[i];
    const uint8_t total = value(hand);
    hand.returnCents = 0;
    if (hand.result == Result::Surrender) {
      hand.returnCents = hand.wagerCents / 2;
    } else if (total > 21) {
      hand.result = Result::Bust;
    } else if (dealerBlackjack) {
      hand.result = natural(hand) ? Result::Push : Result::Lose;
      if (natural(hand)) hand.returnCents = hand.wagerCents;
    } else if (natural(hand)) {
      hand.result = Result::Blackjack;
      hand.returnCents = hand.wagerCents * 5 / 2;
    } else if (dealerTotal > 21 || total > dealerTotal) {
      hand.result = Result::Win;
      hand.returnCents = hand.wagerCents * 2;
    } else if (total == dealerTotal) {
      hand.result = Result::Push;
      hand.returnCents = hand.wagerCents;
    } else {
      hand.result = Result::Lose;
    }
    hand.finished = 1;
    current.roundReturnCents += hand.returnCents;
  }
  credit(current.roundReturnCents);
  current.phase = Phase::Settled;
}

bool BlackjackGame::nextRound() {
  if (current.phase != Phase::Settled) return false;
  clearRound();
  return true;
}

bool BlackjackGame::restore(const State& state) {
  if (!validateState(state)) return false;
  current = state;
  return true;
}

bool BlackjackGame::validateState(const State& state) {
  if (state.balanceCents < 0 || state.balanceCents > MAX_BALANCE_CENTS || state.lastCreditDay < 0 ||
      state.lastCreditDay > MAX_CREDIT_DAY || state.shoePosition > SHOE_CARDS || state.shoeReady > 1 ||
      state.phase > Phase::Settled || state.handCount > MAX_HANDS || state.activeHand >= MAX_HANDS ||
      state.insuranceCents < 0 || state.insuranceCents > MAX_BALANCE_CENTS / 2 || state.roundWagerCents < 0 ||
      state.roundWagerCents > MAX_BALANCE_CENTS * 9 || state.roundReturnCents < 0 ||
      state.roundReturnCents > MAX_BALANCE_CENTS * 20 || state.insuranceReturnCents < 0 ||
      state.insuranceReturnCents > MAX_BALANCE_CENTS * 3 / 2)
    return false;

  uint8_t cards[52]{};
  uint8_t dealt[52]{};
  for (size_t i = 0; i < SHOE_CARDS; ++i) {
    if (!state.shoeReady) {
      if (state.shoe[i] != 0 || state.shoePosition != 0) return false;
      continue;
    }
    if (state.shoe[i] >= 52 || ++cards[state.shoe[i]] > 6) return false;
    if (i < state.shoePosition) ++dealt[state.shoe[i]];
  }
  int64_t wager = state.insuranceCents;
  int64_t returned = state.insuranceReturnCents;
  size_t roundCards = 0;
  bool dealerNeededCards = false;
  for (size_t i = 0; i <= MAX_HANDS; ++i) {
    const Hand& hand = i == MAX_HANDS ? state.dealer : state.hands[i];
    const bool used = i == MAX_HANDS ? state.phase != Phase::Betting : i < state.handCount;
    if (hand.cardCount > MAX_CARDS || hand.finished > 1 || hand.fromSplit > 1 || hand.splitAces > 1 ||
        hand.doubled > 1 || hand.result > Result::Surrender || hand.wagerCents < 0 ||
        hand.wagerCents > MAX_BALANCE_CENTS * 2 || hand.returnCents < 0 || hand.returnCents > MAX_BALANCE_CENTS * 5)
      return false;
    for (size_t card = 0; card < MAX_CARDS; ++card) {
      if (card < hand.cardCount) {
        if (hand.cards[card] >= 52 || !dealt[hand.cards[card]]) return false;
        --dealt[hand.cards[card]];
      } else if (hand.cards[card] != 0)
        return false;
    }
    if (!used) {
      if (hand.cardCount || hand.wagerCents || hand.returnCents || hand.finished || hand.fromSplit || hand.splitAces ||
          hand.doubled || hand.result != Result::Pending)
        return false;
      continue;
    }
    if (hand.cardCount < 2) return false;
    roundCards += hand.cardCount;
    if (i == MAX_HANDS) {
      if (hand.wagerCents || hand.returnCents || hand.finished || hand.fromSplit || hand.splitAces || hand.doubled ||
          hand.result != Result::Pending)
        return false;
      continue;
    }
    if (hand.wagerCents < MIN_BET_CENTS || hand.wagerCents % 100 || hand.fromSplit != (state.handCount > 1) ||
        (hand.splitAces && (!hand.fromSplit || rank(hand.cards[0]) != 1 || hand.cardCount != 2 || !hand.finished)) ||
        (hand.doubled && (hand.cardCount != 3 || !hand.finished || hand.wagerCents % 200)))
      return false;
    const int64_t originalBet = state.hands[0].wagerCents / (state.hands[0].doubled ? 2 : 1);
    if (hand.wagerCents != originalBet * (hand.doubled ? 2 : 1)) return false;
    wager += hand.wagerCents;
    returned += hand.returnCents;
    dealerNeededCards = dealerNeededCards || (value(hand) <= 21 && !natural(hand) && hand.result != Result::Surrender);
    if (state.phase != Phase::Settled) {
      if (hand.returnCents || (hand.result != Result::Pending && hand.result != Result::Bust) ||
          (hand.result == Result::Bust && (value(hand) <= 21 || !hand.finished)) ||
          (value(hand) > 21 && hand.result != Result::Bust) ||
          (state.phase == Phase::Playing && value(hand) >= 21 && !hand.finished))
        return false;
    } else {
      if (!hand.finished || hand.result == Result::Pending) return false;
      int64_t expected = 0;
      Result expectedResult;
      if (hand.result == Result::Surrender) {
        if (state.handCount != 1 || hand.cardCount != 2 || natural(hand) || natural(state.dealer)) return false;
        expected = hand.wagerCents / 2;
        expectedResult = Result::Surrender;
      } else if (value(hand) > 21) {
        expectedResult = Result::Bust;
      } else if (natural(state.dealer)) {
        expectedResult = natural(hand) ? Result::Push : Result::Lose;
        expected = natural(hand) ? hand.wagerCents : 0;
      } else if (natural(hand)) {
        expectedResult = Result::Blackjack;
        expected = hand.wagerCents * 5 / 2;
      } else if (value(state.dealer) > 21 || value(hand) > value(state.dealer)) {
        expectedResult = Result::Win;
        expected = hand.wagerCents * 2;
      } else if (value(hand) == value(state.dealer)) {
        expectedResult = Result::Push;
        expected = hand.wagerCents;
      } else {
        expectedResult = Result::Lose;
      }
      if (hand.result != expectedResult || hand.returnCents != expected) return false;
    }
  }
  if (state.phase == Phase::Betting) {
    return !state.handCount && !state.activeHand && !state.roundWagerCents && !state.roundReturnCents &&
           !state.insuranceCents && !state.insuranceReturnCents;
  }
  if (!state.shoeReady || !state.handCount || state.activeHand >= state.handCount || wager != state.roundWagerCents ||
      returned != state.roundReturnCents || state.shoePosition < roundCards ||
      state.shoePosition - roundCards > SHOE_CARDS - ROUND_RESERVE ||
      (state.insuranceCents &&
       (rank(state.dealer.cards[0]) != 1 ||
        state.insuranceCents != state.hands[0].wagerCents / (state.hands[0].doubled ? 4 : 2)))) {
    return false;
  }
  if (state.phase == Phase::Insurance) {
    return state.handCount == 1 && !state.activeHand && state.dealer.cardCount == 2 &&
           rank(state.dealer.cards[0]) == 1 && state.hands[0].cardCount == 2 && !state.hands[0].finished &&
           !state.insuranceCents && !state.roundReturnCents;
  }
  if (state.phase == Phase::Playing) {
    if (state.dealer.cardCount != 2 || natural(state.dealer) || !state.hands[state.activeHand].cardCount ||
        state.hands[state.activeHand].finished || value(state.hands[state.activeHand]) >= 21 ||
        state.roundReturnCents || state.insuranceReturnCents)
      return false;
    for (size_t i = 0; i < state.activeHand; ++i)
      if (!state.hands[i].finished) return false;
    return true;
  }
  return (!dealerNeededCards || value(state.dealer) >= 17) &&
         state.insuranceReturnCents == (natural(state.dealer) ? state.insuranceCents * 3 : 0);
}
