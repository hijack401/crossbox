# Casino

Casino is a local game using virtual dollars. It has no deposits, purchases,
withdrawals, or network play. Find it after Pomodoro on Home. The lobby shows
the wallet and Slots, Blackjack, Roulette, and Baccarat, in that order. Only
Blackjack is enabled; the other three labels are muted and have no hit targets.

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

All amounts use integer cents, including half-dollar blackjack and surrender
payments. New bets are whole dollars, starting at $1, and cannot exceed the
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

## Persistence and resource use

`/.crosspoint/casino.bin` stores the wallet, credited date, shoe, cards, bets,
active hand, phase, and settlement. The explicit 594-byte format has a version
and CRC32. It never serializes native struct padding or pointers. The store
writes and verifies `casino.tmp`, preserves `casino.bak`, then installs the
new primary. Reads recover from a valid backup or temporary file if needed;
unrecoverable files produce an error instead of resetting the wallet.

Every successful gameplay action and daily credit saves the complete state.
Leaving, sleeping, or restarting resumes the same round and never settles an
already settled round again. A failed write pauses gameplay and retains the
changed state in RAM for Retry, blocking automatic sleep and control-center
entry. Forced power-off before a successful retry can still lose that last
change; the previous valid snapshot remains the recovery point. Filesystem
failure or manual SD-file edits are not a secure multiplayer economy.

The game uses fixed arrays: four player hands, one dealer hand, and 312 shoe
cards. Its state is 608 bytes on the host. Persistence uses one checked
608-byte scratch allocation during verification and 128-byte stream buffers.
The UI reuses the existing framebuffer and Pomodoro number font for bet amounts.
The lobby balance uses a generated proportional Noto Sans subset with 2,012 bitmap
bytes stored in flash and no runtime font buffer. Regenerate it with
`python scripts/generate_balance_font.py` from the bundled, OFL-licensed source.
There are no background tasks or per-card heap allocations. The activity is
allocated once on entry with checked allocation and released on exit.

## Verification

```sh
cmake -S test -B build/host-tests
cmake --build build/host-tests --target BlackjackGameTest CasinoStoreTest CasinoDateTest
ctest --test-dir build/host-tests -R '^(BlackjackGame|CasinoPersistence|CasinoDate)\.' --output-on-failure
pio run -e simulator_x4_pro
pio run -e x4pro
```

Host tests cover payouts, naturals, insurance, soft aces, splits and doubles,
fund limits, idempotent settlement, calendar boundaries, backdated clocks,
credit accumulation, corrupted snapshots, and failed SD transactions. The
simulator supports scripted interactions and screenshots for visual checks.

On the X4 Pro, check card readability, touch targets, all four orientations,
sleep/reopen during a hand, persistence after restart, and daily rollover with
a correct clock. Monitor serial heap before entering and after leaving Casino;
confirm free heap stays above 50 KB and returns to its prior range after exit.
Physical e-ink refresh quality and real RTC/SD behavior require device testing.
