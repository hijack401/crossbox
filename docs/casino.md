# Casino

Casino is a local game using virtual dollars. It has no deposits, purchases,
withdrawals, or network play. Find it after Pomodoro on Home. The lobby shows
the wallet and Slots, Blackjack, Roulette, and Baccarat, in that order.
Blackjack and Baccarat are playable. Slots and Roulette are muted and have no
hit targets.

## Wallet and daily credit

A new wallet starts at $1,000. Casino uses `HalClock::localTime()` and the
configured device timezone to grant $100 per elapsed calendar day. Missed days
accumulate: opening Casino three days later grants $300. While Casino remains
open, the date is checked every 30 seconds. Reopening on the same day does not
add another credit. Moving the clock backward does not lower the last credited
date or issue another payment.

The first valid date establishes the starting day without an additional credit.
An unavailable/invalid clock allows play but shows a clock reminder and defers
daily credits. Use Settings > Clock to sync the device clock and set its time
zone. The X4 Pro simulator uses the computer's date through its clock HAL.
Dates from 2024 through 2099 are accepted. With no trusted server, deliberately
moving the device clock forward can grant credits early.

Both games use the same wallet and daily-credit date. All amounts use integer
cents, including half-dollar blackjack and surrender payments and Baccarat's
Banker commission. New bets are whole dollars, starting at $1, and cannot exceed the
available wallet. Quick bets are $10, $20, $40, and $80; Custom bet opens a
numeric keypad and Max uses the available whole-dollar balance. A balance below
$1 disables betting until a daily credit arrives. The wallet saturates at
$999,999,999 to keep all arithmetic bounded.

## Blackjack table rules

The baseline mechanics follow [Bicycle's Blackjack rules](https://bicyclecards.com/how-to-play/blackjack),
with the following explicit house rules:

- Six decks; a shuffled shoe is reused and reshuffled only between rounds.
- Dealer stands on all 17s, including soft 17. The dealer checks for blackjack
  before player actions, after offering insurance against an ace.
- Ordinary wins pay 1:1; initial, unsplit two-card blackjack pays 3:2.
  A tie returns the stake. A bust loses, even if the dealer also busts.
- Hit, stand, or double on any first two cards. Doubling adds an equal stake,
  deals exactly one card, and finishes that hand.
- Split equal-value pairs, including differently ranked ten-value cards, into
  up to four hands. Additional stakes require enough available balance.
  Doubling after splitting is allowed.
- Split aces get one card each and cannot be resplit. Split-hand 21 pays as an
  ordinary win, never as a natural blackjack.
- Insurance against a dealer ace costs half the original stake and pays 2:1
  if the dealer has blackjack. Taking insurance with a natural blackjack
  provides the same net result as accepting even money.
- Late surrender returns half the initial stake after the dealer's check,
  before hitting, doubling, or splitting.

Each completed round displays its net result including insurance. Split hands
can be inspected individually with the Hand buttons. The Rules screen explains
all actions on the device. Tap buttons, or use directional keys and Confirm;
Back returns to the lobby, then Home.

## Baccarat table rules

The drawing rules follow [New Jersey's published Baccarat rules, section 13:69F-3.9](https://www.nj.gov/oag/ge/docs/Regulations/CHAPTER69F.pdf).
This table uses the standard commission payouts below and the three main bets;
optional pair and bonus side bets are not included.

Choose Player, Banker, or Tie and set a wager before dealing. Deal reserves the
stake and places the opening cards face down. Press Reveal card to turn over
one card at a time, alternating Player and Banker for the opening four cards,
then revealing any required third cards in drawing order. Hand totals and the
winner stay hidden until the final card; any returned stake and winnings are
credited at that point. There is no automatic countdown or repeated animation,
so each reveal is deliberate and suits the e-ink display.

The hands and any third cards are determined automatically by the rules;
Player and Banker name betting positions rather than the person operating the
device. Baccarat uses an independent eight-deck shoe,
reused across rounds and reshuffled before a round when fewer than six cards
remain. Aces count as one, 2–9 count at face value, and tens/face cards count as
zero. Only the last digit of the total counts; the hand closest to nine wins.

- A two-card eight or nine is a natural: both hands stand immediately.
- Otherwise Player draws on 0–5 and stands on 6–7.
- If Player stands, Banker draws on 0–5 and stands on 6–7.
- If Player draws, Banker draws on 0–2; on 3 except when Player's third card is
  eight; on 4 when that card is 2–7; on 5 when it is 4–7; and on 6 when it is
  6–7. Banker stands on 7. The third-card values use the same zero value for
  tens and face cards.
- A winning Player wager pays 1:1. A winning Banker wager pays 0.95:1 after a
  5% commission on winnings; a $10 wager returns $19.50 including its stake.
- A winning Tie wager pays 8:1. A tie returns Player and Banker stakes. A Tie
  wager loses when either hand wins.

The result shows both hands and scores, the winning position, and the wager's
net outcome. Starting another round does not change the wallet until Deal.
Returning to Blackjack preserves its unfinished hand; its reserved stakes are
already excluded from the available wallet used for Baccarat wagers.
A Baccarat round can also be left partially revealed and resumed later. Its
reserved stake remains unavailable to Blackjack until the final reveal returns
any stake or winnings.

## Persistence and resource use

`/.crosspoint/casino.bin` stores the shared wallet and credited date plus each
game's shoe, cards, wager, phase, and settlement, including Blackjack's active
hand and Baccarat's reveal position. The explicit version-3 format is 1,041 bytes with a version and CRC32. It
never serializes native struct padding or pointers. Legacy 594-byte version-1
files load with their exact wallet, date, Blackjack shoe, and current hand;
Baccarat starts with an unused shoe. Version-2 files preserve both games and
the wallet; their completed Baccarat rounds load fully revealed without paying
again. The next successful save upgrades the
file while preserving the legacy snapshot as the backup. The store
writes and verifies `casino.tmp`, preserves `casino.bak`, then installs the
new primary. Reads recover from a valid backup or temporary file if needed;
unrecoverable files produce an error instead of resetting the wallet.

Every successful gameplay action and daily credit saves the complete state.
Baccarat saves each individual reveal. Its dealt cards, reveal position,
result, and wallet are one snapshot, so a recovery cannot combine a pending
reveal with an already-paid wallet or pay a completed round twice.
Leaving, sleeping, or restarting resumes the same round and never settles an
already settled round again. A failed write pauses gameplay and retains the
changed state in RAM for Retry, blocking automatic sleep and control-center
entry. Forced power-off before a successful retry can still lose that last
change; the previous valid snapshot remains the recovery point. Filesystem
failure or manual SD-file edits are not a secure multiplayer economy.

Both games use fixed arrays: Blackjack has four player hands, one dealer hand,
and 312 shoe cards; Baccarat has two three-card hands and 416 shoe cards.
Their states are 608 and 448 bytes on the host. Persistence uses one checked
1,056-byte scratch allocation during load or verification, released on return,
and 128-byte stream buffers. Keeping the scratch snapshot off the task stack
preserves live state when a read or verification fails without adding a
permanent static buffer.
The UI reuses the existing framebuffer and Pomodoro number font for bet amounts.
The lobby balance uses a generated proportional Noto Sans subset with 2,012 bitmap
bytes stored in flash and no runtime font buffer. Regenerate it with
`python scripts/generate_balance_font.py` from the bundled, OFL-licensed source.
There are no background tasks or per-card heap allocations. The activity is
allocated once on entry with checked allocation and released on exit.

## Verification

```sh
cmake -S test -B build/host-tests
cmake --build build/host-tests --target BlackjackGameTest BaccaratGameTest CasinoStoreTest CasinoDateTest
ctest --test-dir build/host-tests -R '^(BlackjackGame|BaccaratGame|CasinoPersistence|CasinoDate)\.' --output-on-failure
pio run -e simulator_x4_pro
pio run -e x4pro
```

Host tests cover payouts, naturals, insurance, soft aces, splits and doubles,
fund limits, idempotent settlement, calendar boundaries, backdated clocks,
credit accumulation, Baccarat's drawing rules and commission, shared-wallet
settlement, legacy migration, corrupted snapshots, and failed SD transactions. The
simulator supports scripted interactions and screenshots for visual checks.
Reveal tests also cover resuming at every card, withheld winnings, upgrading
version-2 results, and retrying or recovering the final reveal without a second
payment.

On the X4 Pro, check card readability, touch targets, all four orientations,
sleep/reopen during a hand or between Baccarat reveals, persistence after restart, and daily rollover with
a correct clock. Monitor serial heap before entering and after leaving Casino;
confirm free heap stays above 50 KB and returns to its prior range after exit.
Physical e-ink refresh quality and real RTC/SD behavior require device testing.
