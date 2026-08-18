# Level-switch macro chain

A small patch on top of GP2040-CE **v0.7.12** that runs

```
Macro 1 × 1  →  [ Macro 2 × N  →  Macro 3 × 1 ] × M groups  →  Macro 4 × 1  →  repeat
                └──────────────────── forever ─────────────────────────────────┘
```

for as long as one GPIO sits at its enabled level. Leave that level and the chain stops
immediately — mid-macro, not at the end of it — clears both counters, and the next time
it starts over from the init macro.

The default polarity is **open = run, shorted to GND = stop**, so the board starts on its
own when powered and needs no jumper to farm. Flip `CHAIN_RUNS_WHEN_SHORTED` for the
opposite (a dedicated jumper you fit to start).

Intended for long unattended grinding sessions: a "collect" macro that has to be followed
periodically by a "clean up" macro, and — because games drift out of a known state over
hours — a "reset" macro every so often that closes and reopens the game.

A group is counted when the **cleanup** macro finishes, so with the defaults below the
reset macro runs once per 10 × 20 = 200 main-macro runs.

## The knobs

All at the top of [`src/addons/input_macro.cpp`](../src/addons/input_macro.cpp):

```cpp
constexpr uint32_t CHAIN_REPEAT_COUNT = 10;      // main macro runs per cleanup macro
constexpr uint32_t GAME_RESET_EVERY_GROUPS = 20; // groups per game reset macro
constexpr int CHAIN_ENABLE_PIN = 16;             // which GPIO gates the chain
constexpr bool CHAIN_RUNS_WHEN_SHORTED = false;  // false: open = run, shorted = stop
```

Change, rebuild, reflash. Nothing else in the firmware needs touching. Which macros run
is set by the `*_MACRO_INDEX` constants in the same block — by default macros 1 to 4:

```cpp
constexpr int CHAIN_INIT_MACRO_INDEX = 0;       // once per start; negative = skip
constexpr int CHAIN_MAIN_MACRO_INDEX = 1;
constexpr int CHAIN_CLEANUP_MACRO_INDEX = 2;
constexpr int GAME_RESET_MACRO_INDEX = 3;
```

When the reset macro has already performed the main macro's opening steps, set
`CHAIN_MAIN_RESUME_INPUT_AFTER_RESET` to the input index the main macro should pick up
from for that one run (0 = always start from the top). Later runs are unaffected, and a
resume point past the end of the macro falls back to the top.

The init macro is for one-off setup at power-up — connecting the controller, dismissing a
title screen — so it runs once when the chain starts and is **not** repeated after the
reset macro. Point `CHAIN_INIT_MACRO_INDEX` at the reset macro's own slot, or add the same
inputs to the front of the reset macro, if the cycle needs it every time.

## Wiring

```
CHAIN_ENABLE_PIN ---- jumper/switch ---- GND
```

The pin is read as a level, never as an edge, so a plain jumper or a latching switch
both work. **Never wire it to 3.3V** — the pull-up does that job, so GPIO ↔ GND is all
that is needed.

Two ways to use it:

- **Dedicated free pin.** Set `CHAIN_RUNS_WHEN_SHORTED = true` and fit a jumper to run.
  Pick a pin whose neighbours are unassigned: if the pin next door triggers a macro, a
  one-pin miscount silently runs that macro forever and looks exactly like a firmware
  bug.
- **Shared with a button.** The pin does not have to be unassigned: when the gamepad
  already owns it, its pull-up setup is left alone and only its level is read. Config
  mode still works even when sharing the web-config boot button, because add-ons never
  run in config mode. The catch is that shorting the pin also presses that button, so
  only share one whose press is harmless.

The default is GP16 with `CHAIN_RUNS_WHEN_SHORTED = false`: unassigned, the bottom-right
corner pin on a Pico (physical pin 21), two pins from the GND at physical pin 23.

## Requirements

1. **Every macro the chain runs must be enabled** and have at least one input each, or the chain
   fail-safes and stops rather than indexing into an empty macro.
2. Macros default to *interruptible*: touching the controller aborts the running macro.
   The chain then restarts that same step and keeps its count. Turn interruptible off
   (and exclusive on) for uninterrupted running.

Macro contents are read from storage on every repetition, so editing any of them in
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
