# Pegble — Pebble C SDK game

A Peggle-style physics game for Pebble watches: aim a launcher, fire a limited supply of
metal balls into a field of coloured pegs, clear the orange ones to win. Live on the store
as "Peggle"; the watch displayName is still "Pegble".

## Toolchain

- `pebble-tool` via `uv tool install pebble-tool --python 3.12` (3.13 fails building `stpyv8`)
- Pebble SDK 4.33.1 active (4.17 also installed), `pebble` binary on PATH via `~/.zshenv`
- `pebble build` / `pebble install --emulator <platform>` / `pebble screenshot --no-open`
- Targets all 7 platforms: aplite, basalt, chalk, diorite, emery, flint, gabbro — but aplite
  gets a lighter game (see **Aplite** below)
- A C compiler on the Mac for the rules harness (`tools/host/`)

## File layout

```
src/c/
  play.c/.h      THE RULES: board building, physics, scoring, fever, combos and style
                 shots, abilities, furniture, endless/daily seeding, save/restore. No
                 drawing, no persistence, no vibration — it emits PlayEvents instead
  game.c/.h      the game window: draws play_get(), turns events into effects, haptics,
                 banners and toasts, feeds them to progress, saves the run between shots
  levels.c/.h    12 hand-built levels, the endless/daily generator, orange share, star pars
  progress.c/.h  everything persisted: unlocks, bests, stars, stats, trophies, daily record,
                 last power picked, the paused run
  pegble.c       main(): the menu (Play / Levels / More sections), launcher glance
  powerpick.c/.h "Choose a power" window before a level or endless run
  records.c/.h   Trophies & stats window
  pins.c/.h      sends timeline-pin numbers to the phone (src/pkjs/index.js builds the pin)
  haptics.c/.h   named vibration patterns with priorities
  effects.c/.h   sparks, score popups, screen shake, screen flash
  abilities.c/.h the five abilities: names, colour, the mark drawn inside a power peg
  theme.c/.h     12 table palettes; fixed peg colours
  ui.c/.h        score formatting, star drawing, menu headers, the PixelLab icons
  rng.h          xorshift32 for everything that must replay from a seed
  config.h       PEGBLE_LITE: what aplite leaves out
src/pkjs/index.js  phone side of timeline pins
resources/images/  menu_icon.png + icon_*.png + bucket_big/small.png (PixelLab, colour
                   watches only)
art/raw/, art/raw_bucket/  the PixelLab originals; tools/make_icons.py and
                   tools/make_bucket.py turn them into resources
tools/host/        Mac harness: stub pebble.h + sim.c (tests, pars, stats)
tools/shot.py      screenshot via the QEMU monitor (bypasses pypkjs)
```

## The rules/window split — keep it

`play.c` must stay free of drawing, persistence and vibration so `tools/host` can compile it
on the Mac. Anything the player should see or feel about a rule goes out as a `PlayEvent`
(`emit()` in play.c), and game.c's `present()` turns it into sparks/banners/haptics while
`progress_on_event()` turns the same event into stats and trophies. The queue only drops
cosmetic events (peg hits, bumpers, portals) when a tick overflows it; the ones trophies and
records depend on always get in.

## Testing — the Mac harness first, the emulator second

```bash
tools/host/run.sh test [games]    # every check; exits non-zero on failure
PLATFORM=aplite tools/host/run.sh test   # the same against aplite's lighter game
tools/host/run.sh par 800         # recompute star pars → paste into levels.c s_par
tools/host/run.sh winrate 300     # random-aim win % per level per screen (difficulty tuning)
tools/host/run.sh stats           # how often combos, style shots, buckets... happen
tools/host/run.sh aged <level> [screen]   # trace a ball that hits the 30s age cap
```

The suite builds every level on three screens and checks pegs never overlap (including a
moving row passing a still one) and stay on the field; plays thousands of random-aim games
per level checking every game ends, every win had a fever and a fever bucket, and balls
almost never reach the age cap; checks daily boards are identical for everyone and differ by
day; checks a saved run restores exactly; checks endless furniture arrives on schedule; and
runs progress.c against an in-memory persist store, including **v2 save migration**.

It has already caught real bugs the emulator would have hidden: overlapping pegs on aplite in
two original levels, an indestructible cup that trapped balls forever (in the since-removed
steel pegs), a portal pair that could loop a ball indefinitely, and a ball bouncing
vertically between the ceiling and a bumper forever.
**Run it after any change to play.c, levels.c or progress.c.**

## Debug build flags (wscript)

Environment variables, so a release build never carries them:

| Flag | Effect |
|---|---|
| `PEGBLE_BOOT=1..6` | start on: 1 a level, 2 endless, 3 the daily, 4 trophies & stats, 5 the power picker, 6 the paused run |
| `PEGBLE_LEVEL=n` | which level BOOT=1/5 opens (0-based) |
| `PEGBLE_AUTOPLAY=1` | aims and fires by itself, restarts when a run ends — a demo you can screendump |
| `PEGBLE_FEVER_DEMO=1` | every board starts with one orange peg left |
| `PEGBLE_BOARD=n` | endless starts with n boards already cleared (to see later furniture) |
| `PEGBLE_HEAP=1` | draws the lowest free heap seen, in the game (aplite headroom) |

Example: `PEGBLE_BOOT=1 PEGBLE_LEVEL=7 PEGBLE_AUTOPLAY=1 pebble build`. The flags become
DEFINES in the compile command, so waf recompiles when they change — no clean needed.
**Rebuild without any flags before releasing.**

## Emulator verification

- Screenshots: `python3 tools/shot.py <platform> out.png` (QEMU monitor screendump with the
  colour rescale). `pebble screenshot` goes through pypkjs and wedges after button presses.
- Buttons: `pebble emu-button` works for Select/Back most of the time; for scrolling, QEMU
  `sendkey up 40` / `sendkey down 40` on the monitor port is more reliable.
- On a used emulator, installing a watch**app** doesn't bring it to the front — open the
  launcher (`emu-button click select`) and launch it, or use a fresh emulator.
- The emulator's flash keeps other projects' apps and this app's persisted progress across
  installs; menus will show progress from earlier sessions.
- **emery screendumps sometimes come back with the wrong colour scale** (orange reads as
  yellow, navy as bright blue). If one frame of the same screen looks right, trust it.

## Conventions specific to this project

- **Fixed-point physics (Q24.8)** — aplite/basalt-era hardware has no FPU. `FP()`/`TO_PX()`.
- **No 64-bit arithmetic.** Trig products (sin_lookup ≤ 65535 × a speed or radius) fit int32.
  A single `(int64_t)` divide pulls ~750 bytes of libgcc in, which aplite can't spare.
- **Everything random that must replay comes from `Rng`** (rng.h), never `rand()`: rand() is
  shared with effects.c, so anything seeded with srand() drifts as soon as a spark is drawn.
  Boards replay from (config, seed); that's what makes the daily identical for everyone and
  a saved run rebuildable from 40 bytes.
- **Level coordinates are normalized 0..1000**; scaled to the screen in `build_board()`.
- **Peg colours are fixed vocabulary, not decoration**: **blue** ordinary, **orange** must
  clear, **pink** roaming bonus, **green** power. All five abilities share the one green — the
  white mark from `ability_draw_mark()` tells them apart, so `ability_color()` ignores its
  argument on purpose. Furniture (bumpers, portals) is drawn in the table's accent. A level's
  palette changes the **table**, not the pegs.
- **Abilities**: MULTIBALL (3-way on emery/gabbro, 2-way elsewhere), BLAST (lights a radius;
  lights through `light_peg(i, NULL, false)` so blasts never chain/recurse), FIREBALL (burns
  through clearable pegs, still bounces off bumpers), SPOOKY (one return from the bottom),
  MAGNET (leans toward the bucket). Powers unlock one per level cleared, in that order; the
  player picks one power or "Mix" (distinct abilities from the unlocked ones). The daily has
  one power for everyone, chosen by the date.
- **Fever**: lighting the last orange starts it — half-speed for 40 ticks, then the bucket
  becomes five bonus buckets (2k/5k/10k/5k/2k). On round screens the buckets span only the
  visible chord (`fever_x/fever_w`), in the physics as well as the drawing.
- **Stuck balls**: if a ball strays less than 3 peg radii over 45 ticks, `unstick()` removes the
  peg it rests on (Peggle's rule); a bumper can't go, so the ball gets a kick, and after 4
  kicks it is let go. `BALL_MAX_AGE` (30s) is only a safety net. This fixed balls parking in
  the bottom of The Funnel.
- **Furniture**: bumpers kick back harder with a little sideways scatter (without it a ball
  dropped dead-centre bounces vertically forever); portals send the ball to their partner and
  need it to be clear of every portal for a few ticks before re-entry, with a cap of 4 hops per
  shot. Endless brings furniture in one kind at a time: bumpers from board 5, a moving row 7,
  portals 10. **Steel pegs were removed (user, 2026-10-14)** — their slots in Hourglass (was
  "Steel Trap"), Wormhole and The Gauntlet became ordinary pegs; don't bring them back.
- **The bucket** is a PixelLab sprite on the colour watches (bucket_big on emery/gabbro,
  bucket_small on basalt/chalk), drawn sitting on the bottom edge; `play_set_bucket_width()`
  makes the physics catch width match the sprite. B&W watches draw it with rects.
- **Difficulty**: levels set their orange share in `s_target_pct` (25% for the original six,
  26–52% for the new ones), tuned with `winrate`. Endless raises the share (1/4 → 1/3 → 1/2),
  cuts power pegs (3 → 2 → 1), speeds the bucket and halves the refund at board 8.
- **Persist keys** (progress.c): 1 unlocked level, 2 endless boards, 3 stats, 4 trophies,
  5 daily record, 6 paused run, 7 last power, **100–105 level bests (v2, levels 1–6)**,
  **106 endless best (v2)**, **120–125 bests for levels 7–12**. 106 was already taken, which is
  why the new levels don't continue at 106. Never move the v2 keys — the harness checks them.
- **The paused run** is written whenever the game reaches AIMING (start, after every shot,
  each new endless board), and its ball count again right after a fire, so quitting mid-shot
  never refunds the ball. Cleared at WON/LOST.
- **Effects are fixed-capacity** and drop work rather than frames. Screen shake moves the
  whole game layer with `layer_set_frame()`; the window background is the table colour so
  the exposed strip matches.
- **Round screens**: HUD and bucket travel are inset; menu headers are drawn centred
  (`ui_draw_header`); round menus show only the focused row's subtitle, so records.c puts the
  stat label in the title there.
- **`gpath_draw_filled()` collapses a small concave star to a blob** — `ui_draw_stars()` fills
  stars with its own scanline fill. Outlines via gpath are fine.

## Aplite (original Pebble / Pebble Steel)

Aplite has 24KB for code, data **and heap** together; the full game is ~31KB static. With
`PEGBLE_LITE` (config.h) it's ~21.5KB, leaving ~1.8KB heap with the menu and game open
(measured with `PEGBLE_HEAP`: 1,140 bytes in a debug build). Aplite keeps fever, combos,
style shots, stars, daily, endless, saved runs, Mix powers, effects and haptics on the
original six levels. It leaves out levels 7–12 and all furniture, the power picker, trophies
and the stats screen, and timeline pins. **Measure aplite after any change**:

```bash
"$HOME/Library/Application Support/Pebble SDK/SDKs/current/toolchain/arm-none-eabi/bin/arm-none-eabi-size" build/aplite/pebble-app.elf
```

If text+data+bss creeps past ~22KB, the app faults at launch inside firmware (an allocation
returning NULL) — it looks like the app doesn't open at all.

## Touch (emery and gabbro only)

- Gated at runtime via `touch_service_is_enabled()`; the calls compile to `(0)` elsewhere.
- **The game window owns its own touch**: drag to aim, lift to fire, tap the results panel to
  continue. Touchdown is ignored when `touch_is_navigational()` is false (the unarmed contact
  that wakes the watch must not fire a ball); guarded on
  `_PBL_API_EXISTS_app_touch_navigation_enable` because SDK 4.17 lacks the field.
- **Menus ride the system gesture bridge**: `main()` calls `app_touch_navigation_enable(true)`
  (third-party apps are opt-out by default). It still needs the user's system-wide touch
  navigation setting on.
- Touch can't be meaningfully injected in the emulator (only touchdown arrives, and it
  poisons the session). Verify on hardware or by dragging on the SDL window by hand.

## Timeline pins

The watch sends `PinKind/PinScore/PinA/PinB` (pins.c); `src/pkjs/index.js` builds the pin and
uses `Pebble.insertTimelinePin()` (the current Pebble app keeps pins locally), falling back to
a PUT on `timeline-api.rebble.io`, which that app intercepts too. A finished daily pins once
per day (`pegble-daily-<day>`, updated by a better score); a new endless record updates one
pin. The JS is covered by a Node stub test; the emulator can't show a pin end to end.

## Icons and bucket (PixelLab)

`art/raw/*.png` are PixelLab generations made with the 64-colour Pebble palette forced
(`art/pebble64.png` as `color_image_base64`). `python3 tools/make_icons.py` crops each to its
content, centres it on 30×30, snaps to the palette and writes `resources/images/icon_*.png`;
it refuses art that's off-palette by more than rounding. They're bundled for basalt, chalk,
emery and gabbro only (`targetPlatforms` in package.json); `ui_icon()` returns NULL elsewhere.
`tools/make_bucket.py` builds the bucket from `art/raw_bucket/bucket_a.png`, folding
PixelLab's olive speckle onto a six-step gold ramp and box-downscaling the small size.

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
hand-pasting individual files.
