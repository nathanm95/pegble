#pragma once

#include <pebble.h>

// Short-lived visual feedback: sparks when a peg is struck, rising score
// popups, a screen shake and a full-screen flash for the loud moments.
//
// Everything here is fixed-capacity and fixed-point (Q24.8) for the same reason
// the physics in game.c is: no FPU on the older watches, no allocation in a
// 33ms tick. When a pool is full the new effect is dropped — losing a spark is
// always better than dropping a frame.

void effects_reset(void);

// A ring of sparks thrown out from a point; speed is in pixels-per-tick Q24.8.
void effects_burst(GPoint at, GColor color, int count, int32_t speed);

// Confetti: thrown upwards rather than evenly, for board clears and wins.
void effects_spray(GRect area, GColor color, int count);

// A "+250" that floats up from the peg that scored it.
void effects_popup(GPoint at, int amount, GColor color);

void effects_shake(int ticks);
void effects_flash(GColor color, int ticks);

// Advance every live effect by one game tick.
void effects_update(void);

// Sparks and popups. Call after the board and before the HUD.
void effects_draw(GContext *ctx);

// True while a flash frame should cover the screen; writes its colour to out.
bool effects_flash_color(GColor *out);

// Current screen-shake displacement, (0,0) when nothing is shaking.
GPoint effects_shake_offset(void);
