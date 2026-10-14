# Pegble — Pebble C SDK game

A Peggle-style physics game for Pebble watches: aim a launcher, fire a limited supply of
metal balls into a field of coloured pegs, clear the orange ones to win.

## Toolchain

- `pebble-tool` via `uv tool install pebble-tool --python 3.12` (3.13 fails building `stpyv8`)
- Pebble SDK 4.17, `pebble` binary on PATH via `~/.zshenv`
- `pebble build` / `pebble install --emulator <platform>` / `pebble screenshot --no-open`
- Targets all 7 platforms: aplite, basalt, chalk, diorite, emery, flint, gabbro (see
  `package.json` → `pebble.targetPlatforms`)

## File layout

```
src/c/
  pegble.c    main(), level-select menu (MenuLayer), unlock/best-score display
  game.c      the game window: physics, rendering, input, win/lose/endless rules
  game.h      game_window_push(level), game_window_push_endless(), persist keys
  levels.c    level_build() — 6 hand-designed layouts; level_build_endless(difficulty)
  levels.h    PegPos, LEVEL_COUNT, MAX_PEGS
  abilities.c the five power-peg abilities: names, colour, the mark drawn inside
  abilities.h Ability enum
  theme.c     8 table palettes (colour watches only)
  theme.h     Theme struct, fixed bonus-peg colours
  effects.c   sparks, score popups, screen shake, screen flash
  effects.h   effects_* API
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
- **Peg colours are fixed vocabulary, not decoration.** Four colours, always the same
  meaning: **blue** ordinary, **orange** must clear to finish, **pink** roaming bonus peg,
  **green** power peg. All five abilities share the one green — the white mark drawn inside
  by `ability_draw_mark()` is what distinguishes them, so the board never speaks more than
  four colours. `ability_color()` therefore ignores its `Ability` argument on purpose; don't
  "fix" it by giving each ability its own hue.
- **What a level's palette changes is the table, not the pegs**: background, rails, speckle
  and the accent used for bucket, aim guide and progress bar (`theme.c`). Levels 0-5 take
  palettes 0-5; endless takes the next palette on every board, so a run keeps moving.
- **Color vs B&W:** use `PBL_IF_COLOR_ELSE(...)` for peg colors; aplite/diorite need a
  fallback that encodes peg type by shape (see `draw_peg()` in `game.c`), not just color.
  On those watches `theme_get()` returns a single black-and-white palette, so game.c can read
  a `Theme` unconditionally.
- **Abilities** (`abilities.h`, behaviour in `game.c`): MULTIBALL splits a clone, BLAST lights
  every peg in a radius, FIREBALL burns through pegs without bouncing for ~2s, SPOOKY returns
  the ball once from the bottom of the board, MAGNET leans it towards the bucket on the way
  down. Boards deal distinct abilities from a shuffled deck, so you never get three of a kind.
  `trigger_blast()` calls `light_peg_scored()`, **not** `hit_peg()` — lighting through the
  scoring path deliberately does not chain into other power pegs, which would recurse.
- **Endless difficulty ramps with boards cleared** (`s_boards`): `level_build_endless()` adds
  rows and fills more gaps, while game.c raises the share of the board that must be cleared
  (1/4 → 1/3 → 1/2), cuts power pegs (3 → 2 → 1), speeds the bucket up and drops the per-board
  ball refund from 2 to 1 at board 8. Best score *and* best board count persist
  (`PERSIST_KEY_ENDLESS_BOARDS`).
- **Effects are fixed-capacity and drop work rather than frames** (`effects.c`): 28 particles,
  4 popups, no allocation in the 33ms tick. Screen shake moves the whole game layer with
  `layer_set_frame()` rather than offsetting every draw call — which is why the window
  background colour is set to the table colour in `build_board()`, so the strip exposed by the
  shake matches.
- **Round screens (chalk/gabbro):** HUD and bucket travel are inset further from the edges
  via `PBL_IF_ROUND_ELSE(...)` so nothing clips outside the visible circle.
- **Touch (touchscreen platforms — gabbro, emery, flint):** gated at runtime via
  `touch_service_is_enabled()`, not compile-time `PBL_TOUCH` — the touch calls are declared on
  every platform and compile to `(0)` where there's no touchscreen, so `touch_handler()` in
  `game.c` is dead code elsewhere rather than needing `#ifdef`. Two separate mechanisms:
  - **The game window owns its own touch** (`touch_service_subscribe` in `window_appear`):
    touchdown+drag sets aim (`set_aim_from_touch_x`), liftoff fires; a tap on the win/lose
    overlay advances, same as Select. Touchdown is ignored when `touch_is_navigational()` is
    false — an unarmed contact (session inactive at touchdown, e.g. the touch that wakes the
    watch) must not fire a ball. The flag is latched at touchdown for the whole gesture, so
    testing it there is enough.
  - **The level-select menu rides the system gesture bridge**, which maps swipes/taps on a
    MenuLayer/ScrollLayer to Up/Down/Select. Third-party apps are **opt-out by default**, so
    `main()` in `pegble.c` calls `app_touch_navigation_enable(true)`; without it the menu is
    button-only on a touchscreen watch. It still only does anything once the *user* has touch
    navigation enabled system-wide (firmware gate `settings_touch_enabled`) — so "menu doesn't
    respond to taps" is expected on a watch with the system setting off, not a bug here.
    The game window draws into a plain `Layer`, not a MenuLayer/ScrollLayer, so the bridge
    produces nothing there and doesn't fight `touch_handler()`.
  - `app_touch_navigation_enable()` and `TouchEvent.non_navigational` **do not exist in SDK
    4.17** (they arrived with `_PBL_API_EXISTS_app_touch_navigation_enable`; local SDK is
    4.33.1). Uses of the struct field are guarded on that macro so the CloudPebble copy still
    builds if it lands on an older SDK; the call itself needs no guard because older SDKs
    #define it away.
- **Verifying touch:** `pebble-tool` still has no CLI touch command (`emu-button` is physical
  buttons, `emu-tap` is the accelerometer). The QEMU machine does expose a `Pebble Touch
  (absolute)` input device on the monitor socket (`ps aux | grep qemu-pebble` → `monitor
  tcp::<port>`; then `mouse_move <x*0x7FFF/260> <y*0x7FFF/260>` / `mouse_button 1` / `mouse_button
  0`), and injected touchdowns do reach the app — but injecting the release kills `qemu-pebble`
  often enough that it is not a dependable harness. Pair it with `screendump <file>.ppm` on the
  same socket (works when `pebble screenshot` times out). For anything conclusive use
  `pebble emu-control --emulator gabbro` (browser sensor panel) or real hardware.
- **Gabbro emulator gotcha:** the platform's `qemu_spi_flash.bin` is shared across projects, so a
  freshly (re)started gabbro emulator auto-launches whatever app ran last — usually Tennis Tour,
  which then hogs the CPU and makes `pebble install` hang. `pebble kill` + reinstall, and check
  what's actually on screen before believing a screenshot.

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
