# Peggle — store listing

## Title

Peggle

## Short description

Pachinko on your wrist: clear the orange pegs, then catch the fever.

## Full description

A Peggle clone for Pebble. A physics puzzle game of aim, ricochet, and lucky bounces. Original game is inspired by pachinko, pinball, and pool.

Launch a limited supply of balls from the top of the board and watch them carom through a field of pegs. Clear all the orange pegs to beat a level. Rack up points on the blue ones, and the more oranges you clear, the higher your score multiplier climbs. Hit the last orange peg and everything slows down for Extreme Fever, as the bottom of the board turns into bonus buckets worth up to 10,000.

Features:
• 12 hand-crafted levels — from your first gentle bounce to The Gauntlet, with bumpers, sliding rows and portals in the later ones
• Daily challenge — a new board every day, the same for everyone. Keep your streak going
• Endless mode — boards keep coming and get tougher the deeper you go
• Five powers on the green pegs — Multiball, Blast, Fireball, Spooky and Magnet. Unlock one per level cleared, then pick your favourite (or a mix) before you play
• Combos and trick shots — bonus points for big combos, long shots and shots off the wall
• Up to three stars on every level
• Free-ball bucket — catch a falling ball in the moving golden bucket to earn it back
• Pink bonus peg — roams the board each turn for a big score
• 16 trophies and a page of lifetime stats
• Leave mid-game and carry on later — your run is saved
• Timeline pins for your daily score and new endless records
• A trajectory guide to help line up the perfect shot

Simple controls: Up/Down to aim, Select to fire. On Time 2 and Round 2 you can also drag to aim and lift your finger to fire.

Built for Time 2 and Round 2, and plays on every Pebble. The original Pebble and Pebble Steel get a lighter version with the first six levels.

## Release notes (3.0.1)

What's new in 3.0.1

• The game is now called Peggle on your watch too.
• Power announcements no longer cover the Extreme Fever banner.
• The Daily row in the menu is no longer cut off on smaller screens.

Your progress, trophies and saved run carry over.

## Release notes (3.0)

What's new in 3.0

• Extreme Fever: clear the last orange peg and the bottom turns into bonus buckets worth up to 10,000.
• 6 new levels, with bumpers, sliding rows and portals.
• Daily challenge: a new board every day, the same for everyone, with a streak to keep.
• Star ratings: earn up to 3 stars on every level.
• Choose your power before you play: Multiball, Blast, Fireball, Spooky, Magnet or a Mix. A new power unlocks with each level you clear.
• Combos and trick shots: bonus points for big combos, long shots and shots off the wall.
• Endless gets tougher as you go, adding new obstacles every few boards.
• Continue where you left off: leaving mid-game saves your run.
• 16 trophies and a lifetime stats page.
• New golden bucket, new icons and a different vibration for each big moment.
• Timeline pins for your daily score and new endless records.
• Bigger multiball (three balls) on Pebble Time 2 and Round 2.
• Original Pebble and Pebble Steel get a lighter version: fever, stars, combos, daily, endless and saved games on the first six levels, without the new levels, power picker, trophies or pins.

## Images in this folder

| File | What it shows |
|---|---|
| `<platform>_1_preview.gif` | Level 1 played through to a fever and the results panel |
| `<platform>_2_level.png` | A new level mid-shot (Pinball; Ring Cycle on the original Pebble) |
| `<platform>_3_fever.png` | Extreme Fever with the bonus buckets |
| `<platform>_4_clear.png` | The results panel with its stars |
| `<platform>_5_menu.png` | The menu |
| `icon_80x80.png`, `icon_144x144.png` | Store icons |
| `banner_720x320.png` | Store header image |

Platforms: aplite (original Pebble/Steel), basalt (Time), chalk (Time Round), diorite (Pebble 2),
emery (Time 2), flint (Pebble 2 Duo), gabbro (Round 2).

Regenerate with `tools/record_store.py <dir>` then `tools/store_assets.py <dir>` (see CLAUDE.md).
For an app that's already on the store, `pebble publish` goes live at once and *adds* screenshots
without replacing the old ones, and ignores description and icon changes — so swap the
images, icon, banner and description in the developer portal instead.
