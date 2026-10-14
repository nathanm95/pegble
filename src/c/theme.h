#pragma once

#include <pebble.h>

// Peg colours are fixed for the whole game so a colour always means one thing:
//
//   blue    ordinary peg
//   orange  must be cleared to finish the board
//   pink    roaming bonus peg, worth a pile of points
//   green   power peg, carrying one of the five abilities
//
// The bonus and power colours live here as constants because they are the same
// on every table; the blue and orange are fields on Theme only so the shading
// pairs stay together.
#define PEG_COLOR_BONUS      PBL_IF_COLOR_ELSE(GColorShockingPink, GColorBlack)
#define PEG_COLOR_BONUS_LIT  PBL_IF_COLOR_ELSE(GColorRichBrilliantLavender, GColorBlack)

// A palette for one table. Levels each get their own and endless mode cycles
// through them, so what changes board to board is the table, not the pegs.
//
// Black-and-white platforms (aplite, diorite) get a single monochrome palette
// and lean on shape, not colour, to tell peg types apart — see draw_peg() in
// game.c. Nothing here is #ifdef'd at the call site because of that: game.c
// always reads a Theme, it just happens to be all-black on those watches.
typedef struct {
    GColor bg;         // playfield background
    GColor wall;       // side rails
    GColor star;       // faint background speckle
    GColor peg;        // standard peg body
    GColor peg_hi;     // its top-left highlight
    GColor peg_lit;    // once struck, before it clears
    GColor target;     // must-clear peg body
    GColor target_hi;
    GColor target_lit;
    GColor accent;     // bucket, aim guide, progress bar
    GColor text;       // HUD text
} Theme;

#define THEME_COUNT 12

// index is taken modulo THEME_COUNT, so callers can pass a level or board number.
const Theme *theme_get(int index);
