# Level-switch macro chain

A small patch on top of GP2040-CE **v0.7.12** that runs

```
[ Macro 2 × N  →  Macro 4 × 1 ] × M groups  →  Macro 5 × 1  →  repeat forever
```

for as long as one dedicated GPIO is **shorted to GND**. Open the jumper and the chain
stops immediately — mid-macro, not at the end of it — clears both counters, and the next
short starts again from Macro 2 #1.

Intended for long unattended grinding sessions: a "collect" macro that has to be followed
periodically by a "clean up" macro, and — because games drift out of a known state over
hours — a "reset" macro every so often that closes and reopens the game.

A group is counted when the **cleanup** macro finishes, so with the defaults below the
reset macro runs once per 10 × 20 = 200 main-macro runs.

## The three knobs

All at the top of [`src/addons/input_macro.cpp`](../src/addons/input_macro.cpp):

```cpp
constexpr uint32_t CHAIN_REPEAT_COUNT = 10;      // main macro runs per cleanup macro
constexpr uint32_t GAME_RESET_EVERY_GROUPS = 20; // groups per game reset macro
constexpr int CHAIN_ENABLE_PIN = 21;             // GP21, active-low (GP21 <-> GND)
```

Change, rebuild, reflash. Nothing else in the firmware needs touching. Which macros run
is fixed at `macroList[1]` ("Macro 2"), `macroList[3]` ("Macro 4") and `macroList[4]`
("Macro 5") via the `*_MACRO_INDEX` constants in the same block.

## Wiring

```
GP21 (physical pin 27) ---- jumper/switch ---- GND (physical pin 28)
```

On a Raspberry Pi Pico those are adjacent pins, 7th and 8th counting up from the
bottom-right corner. **Never wire the enable pin to 3.3V** — it uses the internal
pull-up, so GPIO ↔ GND is all that is needed.

Pick the pin with its neighbours in mind. If the pin next door triggers a macro, a
one-pin miscount silently runs that macro forever and looks exactly like a firmware
bug. GP21 is surrounded by pins that are unassigned in the stock map, so miscounting
merely fails to start the chain.

## Requirements

1. **The enable pin must be unassigned** in the active profile's Pin Mapping. If
   anything claims it, the chain disables itself on purpose — otherwise shorting the
   pin to GND would also inject a real button press.
2. **All three macros must be enabled** and have at least one input each, or the chain
   fail-safes and stops rather than indexing into an empty macro.
3. Macros default to *interruptible*: touching the controller aborts the running macro.
   The chain then restarts that same step and keeps its count. Turn interruptible off
   (and exclusive on) for uninterrupted running.

Macro contents are read from storage on every repetition, so editing Macro 2, 4 or 5 in
the Web Config takes effect on the next loop — no rebuild, and reflashing the firmware
does not disturb the stored macros.

The reset macro is whatever you make it. A typical one uses the system-level HOME menu to
close and relaunch the game, waits out the loading screen, and drives the game back to the
same starting screen the main macro expects.

## Design notes

Only two files change: `headers/addons/input_macro.h` and `src/addons/input_macro.cpp`.
No protobuf, Web Config or storage-format changes, and no new macro config fields.

- Macro data is never copied into C++. The scheduler points `macroPosition` at the
  existing entry in `inputMacroOptions->macroList` and lets the stock
  `runCurrentMacro()` emit the inputs.
- The enable pin is **level-sensed every frame**, not edge-triggered, so unplugging is
  noticed on the next `preprocess()` rather than at the end of the current macro.
- With the pin open, macro handling is the stock path
  (`checkMacroPress` / `checkMacroAction` / `runCurrentMacro`) untouched, so manually
  triggered macros behave exactly as upstream.
- With the pin shorted, the two trigger-scanning calls are skipped so physical macro
  buttons cannot disturb the scheduler, and `ON_HOLD_REPEAT`'s "trigger released →
  reset" guard is bypassed because the chain, not a held button, owns the lifetime.
- End-of-macro is intercepted *before* the stock `reset()`/`restart()` branch, and
  returns immediately after switching macros so the now-stale `macro` / `macroInput`
  references are never reused.
- Both counters live only in RAM and are cleared by `stopChain()`, which every exit path
  (pin released, Focus Mode lock, `reinit()`, a disabled/empty macro) goes through — so
  the chain can never resume a half-finished cycle.
- Focus Mode's macro lock stops the chain too, matching the addon's existing semantics.

## Building

See [`scripts/`](scripts) for the WSL/Linux build used here — pico-sdk 2.1.1 with the
`lib/lwip`, `lib/tinyusb` **and** `lib/mbedtls` submodules initialised (`pico_mbedtls`
is what transitively supplies `pico/unique_id.h`), Node 20 for the web bundle, then:

```bash
PICO_SDK_PATH=~/pico-sdk GP2040_BOARDCONFIG=Pico SKIP_WEBBUILD=TRUE \
  cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
GP2040_BOARDCONFIG=Pico cmake --build build --parallel "$(nproc)"
```
