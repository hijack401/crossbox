#include "FarkleGame.h"

#include <limits>

namespace {
bool validWager(const int64_t amount) {
  return amount >= FarkleGame::MIN_BET_CENTS && amount <= FarkleGame::MAX_BET_CENTS && amount % 100 == 0;
}

uint8_t countDice(uint8_t mask) {
  uint8_t count = 0;
  while (mask) {
    count += mask & 1;
    mask >>= 1;
  }
  return count;
}

uint32_t scoreGroups(const uint8_t counts[7]) {
  uint32_t score = 0;
  for (uint8_t face = 1; face <= 6; ++face) {
    if (counts[face] >= 3) {
      const uint32_t triple = face == 1 ? 1000 : face * 100;
      score += triple << (counts[face] - 3);
    } else if (face == 1 || face == 5) {
      score += counts[face] * (face == 1 ? 100 : 50);
    } else if (counts[face]) {
      return 0;
    }
  }
  return score;
}

uint8_t bestMask(const uint8_t dice[FarkleGame::DICE], const uint8_t available) {
  uint32_t bestScore = 0;
  uint8_t best = 0;
  for (uint8_t mask = available; mask; mask = (mask - 1) & available) {
    const uint32_t score = FarkleGame::scoreDice(dice, mask);
    if (score > bestScore || (score == bestScore && score && countDice(mask) > countDice(best))) {
      bestScore = score;
      best = mask;
    }
  }
  return best;
}

bool addFits(const uint32_t first, const uint32_t second) {
  return second <= std::numeric_limits<uint32_t>::max() - first;
}
}  // namespace

uint32_t FarkleGame::scoreDice(const uint8_t dice[DICE], const uint8_t mask) {
  if (!dice || !mask || (mask & ~ALL_DICE_MASK)) return 0;
  uint8_t counts[7]{};
  for (size_t i = 0; i < DICE; ++i) {
    if (!(mask & (1 << i))) continue;
    if (dice[i] < 1 || dice[i] > 6) return 0;
    ++counts[dice[i]];
  }
  uint32_t best = scoreGroups(counts);
  bool completeStraight = true;
  for (uint8_t face = 1; face <= 6; ++face) completeStraight &= counts[face] == 1;
  if (completeStraight) return 1500;

  for (uint8_t start = 1; start <= 2; ++start) {
    bool straight = true;
    for (uint8_t face = start; face < start + 5; ++face) straight &= counts[face] > 0;
    if (!straight) continue;
    uint8_t remaining[7]{};
    for (uint8_t face = 1; face <= 6; ++face)
      remaining[face] = counts[face] - (face >= start && face < start + 5 ? 1 : 0);
    const uint32_t extra = scoreGroups(remaining);
    if (countDice(mask) != 5 && !extra) continue;
    const uint32_t score = (start == 1 ? 500 : 750) + extra;
    if (score > best) best = score;
  }
  return best;
}

bool FarkleGame::validTargetScore(const uint32_t targetScore) {
  return targetScore >= 1000 && targetScore <= 10000 && targetScore % 500 == 0;
}

void FarkleGame::reset() { current = {}; }

bool FarkleGame::canBet(const int64_t wagerCents, const int64_t availableCents) const {
  return (current.phase == Phase::Betting || current.phase == Phase::Settled) && validWager(wagerCents) &&
         availableCents >= wagerCents && availableCents <= MAX_BET_CENTS;
}

bool FarkleGame::startMatch(const int64_t wagerCents, const int64_t availableCents, const uint32_t targetScore) {
  if (!canBet(wagerCents, availableCents) || !validTargetScore(targetScore)) return false;
  reset();
  current.wagerCents = wagerCents;
  current.targetScore = targetScore;
  current.phase = Phase::AwaitRoll;
  return true;
}

bool FarkleGame::rollRemaining(State& state, const Random random, void* context) {
  if (!random) return false;
  constexpr uint32_t SIDES = 6;
  constexpr uint32_t THRESHOLD = (uint32_t{0} - SIDES) % SIDES;
  constexpr uint16_t MAX_ATTEMPTS = 128;
  state.rolledMask = ALL_DICE_MASK & ~state.heldMask;
  for (size_t i = 0; i < DICE; ++i) {
    if (!(state.rolledMask & (1 << i))) continue;
    bool accepted = false;
    for (uint16_t attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
      const uint32_t sample = random(context);
      if (sample < THRESHOLD) continue;
      state.dice[i] = sample % SIDES + 1;
      accepted = true;
      break;
    }
    if (!accepted) return false;
  }
  state.endReason = EndReason::None;
  state.phase = Phase::Selecting;
  if (!bestMask(state.dice, state.rolledMask)) {
    state.turnPoints = 0;
    state.endReason = EndReason::Bust;
    state.phase = Phase::TurnEnded;
  }
  return true;
}

bool FarkleGame::roll(const Random random, void* context) {
  if (current.phase != Phase::AwaitRoll) return false;
  State next = current;
  if (!rollRemaining(next, random, context)) return false;
  current = next;
  return true;
}

uint32_t FarkleGame::scoreSelection(const uint8_t mask) const {
  if (current.phase != Phase::Selecting || (mask & ~current.rolledMask)) return 0;
  return scoreDice(current.dice, mask);
}

uint8_t FarkleGame::bestSelection() const {
  return current.phase == Phase::Selecting ? bestMask(current.dice, current.rolledMask) : 0;
}

bool FarkleGame::holdAndRoll(const uint8_t mask, const Random random, void* context) {
  const uint32_t selectedPoints = scoreSelection(mask);
  if (!selectedPoints || !addFits(current.turnPoints, selectedPoints)) return false;
  State next = current;
  next.turnPoints += selectedPoints;
  next.heldMask |= mask;
  if (next.heldMask == ALL_DICE_MASK) next.heldMask = 0;
  if (!rollRemaining(next, random, context)) return false;
  current = next;
  return true;
}

bool FarkleGame::bank(const uint8_t mask) {
  const uint32_t selectedPoints = scoreSelection(mask);
  const uint8_t actor = static_cast<uint8_t>(current.activePlayer);
  if (!selectedPoints || !addFits(current.turnPoints, selectedPoints) ||
      !addFits(current.scores[actor], current.turnPoints + selectedPoints))
    return false;
  current.turnPoints += selectedPoints;
  current.scores[actor] += current.turnPoints;
  current.heldMask |= mask;
  current.rolledMask &= ~mask;
  current.endReason = EndReason::Banked;
  current.phase = Phase::TurnEnded;
  if (current.scores[actor] >= current.targetScore) {
    current.phase = Phase::Settled;
    current.returnCents = current.activePlayer == Player::You ? current.wagerCents * 2 : 0;
  }
  return true;
}

bool FarkleGame::nextTurn() {
  if (current.phase != Phase::TurnEnded) return false;
  current.activePlayer = current.activePlayer == Player::You ? Player::Opponent : Player::You;
  current.turnPoints = 0;
  current.heldMask = 0;
  current.rolledMask = 0;
  for (auto& die : current.dice) die = 0;
  current.endReason = EndReason::None;
  current.phase = Phase::AwaitRoll;
  return true;
}

bool FarkleGame::nextMatch() {
  if (current.phase != Phase::Settled) return false;
  reset();
  return true;
}

bool FarkleGame::computerShouldBank(const uint8_t mask) const {
  const uint32_t selectedPoints = scoreSelection(mask);
  if (!selectedPoints || !addFits(current.turnPoints, selectedPoints)) return false;
  const uint32_t total = current.turnPoints + selectedPoints;
  const uint8_t actor = static_cast<uint8_t>(current.activePlayer);
  if (total >= current.targetScore - current.scores[actor]) return true;
  uint8_t remaining = countDice(current.rolledMask & ~mask);
  if (!remaining) remaining = DICE;
  static constexpr uint32_t BANK_THRESHOLDS[] = {0, 150, 300, 450, 600, 800, 1500};
  uint32_t threshold = BANK_THRESHOLDS[remaining];
  if (current.scores[1 - actor] >= current.scores[actor] + 1000) threshold += 200;
  return total >= threshold;
}

bool FarkleGame::validateState(const State& state) {
  if (state.phase > Phase::Settled || state.activePlayer > Player::Opponent || state.endReason > EndReason::Bust ||
      !validTargetScore(state.targetScore) || (state.heldMask & ~ALL_DICE_MASK) ||
      (state.rolledMask & ~ALL_DICE_MASK) || (state.heldMask & state.rolledMask) || state.turnPoints % 50 ||
      state.scores[0] % 50 || state.scores[1] % 50)
    return false;
  if (state.phase == Phase::Betting) {
    if (state.wagerCents || state.returnCents || state.scores[0] || state.scores[1] || state.turnPoints ||
        state.heldMask || state.rolledMask || state.activePlayer != Player::You || state.endReason != EndReason::None ||
        state.targetScore != DEFAULT_TARGET_SCORE)
      return false;
    for (const auto die : state.dice)
      if (die) return false;
    return true;
  }
  if (!validWager(state.wagerCents)) return false;
  const uint8_t actor = static_cast<uint8_t>(state.activePlayer);
  if (state.scores[1 - actor] >= state.targetScore) return false;
  if (state.phase != Phase::Settled && (state.scores[actor] >= state.targetScore || state.returnCents)) return false;
  if (state.phase == Phase::AwaitRoll) {
    if (state.turnPoints || state.heldMask || state.rolledMask || state.endReason != EndReason::None) return false;
    for (const auto die : state.dice)
      if (die) return false;
    return true;
  }
  for (const auto die : state.dice)
    if (die < 1 || die > 6) return false;
  if ((state.heldMask | state.rolledMask) != ALL_DICE_MASK ||
      (state.heldMask && !scoreDice(state.dice, state.heldMask)))
    return false;
  if (state.phase == Phase::Selecting) {
    return state.endReason == EndReason::None && (!state.heldMask || state.turnPoints) && state.rolledMask &&
           bestMask(state.dice, state.rolledMask);
  }
  if (state.endReason == EndReason::Bust) {
    return state.phase == Phase::TurnEnded && !state.turnPoints && state.rolledMask &&
           !bestMask(state.dice, state.rolledMask);
  }
  if (state.endReason != EndReason::Banked || !state.heldMask || !state.turnPoints ||
      state.turnPoints > state.scores[actor])
    return false;
  if (state.phase == Phase::Settled) {
    return state.scores[actor] >= state.targetScore &&
           state.returnCents == (state.activePlayer == Player::You ? state.wagerCents * 2 : 0);
  }
  return true;
}

bool FarkleGame::restore(const State& state) {
  if (!validateState(state)) return false;
  current = state;
  return true;
}
