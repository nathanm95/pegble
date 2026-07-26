# Pegble — Pebble C SDK game

A Peggle-style physics game for Pebble watches: aim a launcher, fire a limited supply of
metal balls into a field of coloured pegs, clear the orange ones to win.

## Toolchain

- `pebble-tool` via `uv tool install pebble-tool --python 3.12` (3.13 fails building `stpyv8`)
- Pebble SDK 4.17, `pebble` binary on PATH via `~/.zshenv`
- `pebble build` / `pebble install --emulator <platform>` / `pebble screenshot`
- Targets all 7 platforms: aplite, basalt, chalk, diorite, emery, flint, gabbro (see
  `package.json` → `pebble.targetPlatforms`)

## File layout

```
src/c/
  pegble.c    main(), level-select menu (MenuLayer), unlock/best-score display
  game.c      the game window: physics, rendering, input, win/lose/endless rules
  game.h      game_window_push(level), game_window_push_endless()
  levels.c    level_build() — 6 hand-designed layouts; level_build_endless() — random boards
  levels.h    PegPos, LEVEL_COUNT, MAX_PEGS
resources/images/menu_icon.png   25x25 white-on-transparent launcher icon
```

## Conventions specific to this project

- **Fixed-point physics (Q24.8)** in `game.c` — some Pebble hardware (aplite/basalt-era)
  has no FPU. `FP()`/`TO_PX()` macros convert; don't introduce floats into the hot path.
- **Level coordinates are normalized 0..1000** on both axes (`PegPos.nx/ny` in `levels.c`),
  scaled to the actual screen at runtime in `game.c`'s `start_level()`. This is what makes
  one binary work on 144x168 up to 260x260 round screens — don't hardcode pixel positions.
- **Endless mode** (`game_window_push_endless`) reuses the same rendering/physics as levels;
  only the board generator (`level_build_endless`) and the win-condition branch in
  `finish_turn()` differ (regenerate + bonus balls instead of ending).
- **Color vs B&W:** use `PBL_IF_COLOR_ELSE(...)` for peg colors; aplite/diorite need a
  fallback that encodes peg type by shape (see `draw_peg()` in `game.c`), not just color.
- **Round screens (chalk/gabbro):** HUD and bucket travel are inset further from the edges
  via `PBL_IF_ROUND_ELSE(...)` so nothing clips outside the visible circle.
- **Touch (gabbro only):** gated at runtime via `touch_service_is_enabled()`, not compile-time
  `PBL_TOUCH` — the function is declared on every platform and just returns false where there's
  no touchscreen, so `touch_handler()` in `game.c` is dead code elsewhere rather than needing
  `#ifdef`. Behavior: touchdown+drag sets aim (`set_aim_from_touch_x`), liftoff fires; a tap on
  the win/lose overlay advances, same as Select. `pebble-tool` has no CLI way to simulate
  touchscreen taps (only `emu-button` for physical buttons and `emu-tap` for accelerometer
  taps) — verify touch changes via `pebble emu-control --emulator gabbro` (opens a browser
  sensor panel) or on real Core Time 2 hardware.

## CloudPebble

Nathan also edits this project in CloudPebble (cloudpebble.repebble.com). The zip at
`~/Projects/pebble/pegble-cloudpebble.zip` is a **disconnected copy** — regenerate it after
local changes:

```bash
cd ~/Projects/pebble/pegble
rm -f ../pegble-cloudpebble.zip
zip -qr ../pegble-cloudpebble.zip package.json wscript README.md src resources -x "*.DS_Store"
```

If CloudPebble and local drift out of sync, re-import the zip as a new project rather than
hand-pasting individual files — pasting file contents into the wrong file (e.g. game.c's
contents into pegble.c) causes duplicate-`main`/undefined-reference link errors.
