# pegble

A Peggle-style pachinko game for Pebble, written in C using the Pebble SDK.
Fire balls down through a field of pegs, clear the board before you run out,
and chase the high score in Endless mode.

<p align="center">
  <img src="screenshots/level-select.png" width="200" alt="Level select screen">
  <img src="screenshots/gameplay.png" width="200" alt="Gameplay on a peg board">
</p>

## Gameplay

- **6 hand-built levels** plus an **Endless** score-attack mode.
- Four peg colours, and each one always means the same thing:
  - **Blue** — ordinary pegs, worth a little.
  - **Orange** — clear every one of them to finish the board.
  - **Pink** — the roaming bonus peg, worth a pile of points. It moves to a
    fresh blue peg every turn.
  - **Green** — a power peg. The mark in the middle says which of the five
    abilities it holds.
- **Five abilities**, dealt so no board gives you three of the same:
  - **Multiball** — a second ball splits off the one that hit.
  - **Blast** — every peg in range lights at once.
  - **Fireball** — the ball burns straight through pegs for a few seconds
    instead of bouncing off them.
  - **Spooky** — the next time the ball drops off the bottom it comes back in
    at the top.
  - **Magnet** — the ball is pulled towards the bucket on the way down.
- A moving **bucket** patrols the bottom edge — catch a ball in it for a free
  extra ball.
- **Endless mode** counts the boards you clear and gets harder as you go:
  denser boards, a bigger share of them to clear, fewer power pegs, a quicker
  bucket, and a smaller ball refund once you are past board 8. Best score and
  best board count are both kept.
- Every level plays on its own coloured table, and Endless moves to the next
  one each board.
- High scores persist per level (and separately for Endless) via Pebble's
  persistent storage.
- Runs on round and rectangular displays alike — layout, bucket travel, and
  board generation all adapt to the real screen bounds. Black-and-white
  watches tell peg types apart by shape instead of colour.

## Controls

Up/Down aim, Select fires, and Select again dismisses the win or lose screen.

On watches with a touchscreen you can also drag anywhere on the board to aim
and lift your finger to fire, and tap the win/lose screen to move on. The
level-select menu scrolls and selects by touch too, provided touch navigation
is switched on in the watch's own settings — Pegble opts in, but the system
setting has the final say.

## Building & running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. The
modern Pebble hardware is **emery** (Pebble Time 2), **gabbro** (Pebble Round
2), and **flint** (Pebble 2 Duo); the original Pebble platforms (aplite,
basalt, chalk, diorite) are included by default for backwards compatibility.

## Project layout

```
src/c/           C source for the watchapp
resources/       Images, fonts, and other bundled resources
screenshots/      Emulator screenshots used in this README
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — usually no need to edit
```

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
