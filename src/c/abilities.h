#pragma once

#include <pebble.h>

// Power pegs used to be one thing (green = multiball). Now every power peg
// carries one of five abilities, fired when a ball lights it.
//
//   MULTIBALL  a second ball splits off the one that hit
//   BLAST      every peg in a radius lights at once
//   FIREBALL   the ball burns through pegs for a few seconds instead of bouncing
//   SPOOKY     the next time the ball falls off the bottom it returns at the top
//   MAGNET     the ball is pulled towards the bucket on the way down
//
// Behaviour lives in game.c (it needs the ball and board state); this module
// owns only what an ability looks like and is called.
typedef enum {
    ABILITY_MULTIBALL = 0,
    ABILITY_BLAST,
    ABILITY_FIREBALL,
    ABILITY_SPOOKY,
    ABILITY_MAGNET,
    ABILITY_COUNT
} Ability;

// Banner text shown when the ability triggers, e.g. "MULTIBALL!".
const char *ability_name(Ability a);

// Peg body colour. Monochrome watches always get black — the mark tells them
// apart there. `lit` is the brighter just-struck version.
GColor ability_color(Ability a, bool lit);

// Draws the ability's mark inside a peg of radius r centred at c. The caller
// sets both the fill and stroke colour first; this only draws the shape.
void ability_draw_mark(GContext *ctx, GPoint c, int r, Ability a);
