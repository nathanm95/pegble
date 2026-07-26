# pegble

A Peggle-style pachinko game for Pebble, written in C using the Pebble SDK.
Fire balls down through a field of pegs, clear the board before you run out,
and chase the high score in Endless mode.

<p align="center">
  <img src="screenshots/level-select.png" width="200" alt="Level select screen">
  <img src="screenshots/gameplay.png" width="200" alt="Gameplay on a peg board">
</p>

## Gameplay

- **6 hand-built levels** plus an **Endless** score-attack mode — random
  boards regenerate every time you clear one, awarding a board-clear bonus
  and a couple of extra balls so the run keeps going.
- **Blue pegs** are the standard target; **orange pegs** light up as you hit
  them; clearing every lit orange peg finishes the level.
- **Green pegs** trigger a multiball split when hit.
- A **roaming purple bonus peg** re-picks a fresh blue peg to sit on each
  turn, worth extra when you land it.
- A moving **bucket** patrols the bottom edge — catch a ball in it for a free
  extra ball.
- High scores persist per level (and separately for Endless) via Pebble's
  persistent storage.
- Runs on round and rectangular displays alike — layout, bucket travel, and
  board generation all adapt to the real screen bounds.

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
