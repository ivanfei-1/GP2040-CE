# Example configuration

`backup.gp2040` is a full Web Config backup wired up for the chain in this branch, and
`macro4-game-reset.json` is just macro 4's input list, for pasting into the **Advanced**
tab of the macro editor.

The firmware only decides *when* to run each slot; what a slot contains lives in the
device's own storage, so these files are what make the slot numbers mean anything.

## Slot roles

| Slot | Constant | Role in this setup |
|------|----------|--------------------|
| Macro 1 | `CHAIN_INIT_MACRO_INDEX` | connects the controller once at power-up |
| Macro 2 | `CHAIN_MAIN_MACRO_INDEX` | the farming run, repeated `CHAIN_REPEAT_COUNT` times |
| Macro 3 | `CHAIN_CLEANUP_MACRO_INDEX` | disposes of what the run collected |
| Macro 4 | `GAME_RESET_MACRO_INDEX` | closes and relaunches the game, then reselects the target |
| Macro 5–6 | — | not used by the chain; still triggerable by their own pins |

Macro 2 is deliberately left out of `DIGITAL_DPAD_MACRO_MASK`: its two directional inputs
move the character, so they need to stay analog. Every other macro navigates menus and is
safer on the digital D-pad.

## Restoring

Web Config → Backup and Restore → load `backup.gp2040` → save. This overwrites the whole
device configuration, pin mapping included, so export your own backup first.

Editing a macro afterwards takes effect on the chain's next repetition — no rebuild, and
reflashing the firmware leaves stored macros alone.

## Two tricks worth keeping

Both fight the same problem: the game remembers what you last picked, so a mis-step is
inherited by every later cycle instead of being corrected.

- **Re-anchor before navigating.** Macro 4 cycles the map view (`ZR`, `ZR`) because that
  parks the cursor on a fixed entry, giving the steps after it a known starting point.
- **Aim at a boundary that does not wrap.** The difficulty list does not wrap, so two
  `Down` presses land on the bottom entry from *any* starting position — an absolute
  choice rather than a counted one. Where a list does wrap, counting steps only inherits
  whatever error was already there.
