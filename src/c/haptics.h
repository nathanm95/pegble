#pragma once

#include <pebble.h>

// The game's vibration vocabulary: every moment that buzzes has its own shape,
// so the wrist can tell a free ball from a fever without looking.
//
// Listed lowest priority first. While a pattern is playing, anything of equal
// or lower priority is dropped and anything higher cuts in — so a blast that
// also lights twenty pegs is one buzz, not twenty, and a fever is never lost
// under a bucket catch.
typedef enum {
    HAPTIC_TICK,     // combo milestone: a single tap
    HAPTIC_ABILITY,  // a power fired
    HAPTIC_BUCKET,   // free ball / fever bucket: two quick taps
    HAPTIC_STYLE,    // long shot, off the wall, big combo: a triple flutter
    HAPTIC_BLAST,    // one heavy thump
    HAPTIC_LOSE,     // a long sag
    HAPTIC_WIN,      // two even pulses
    HAPTIC_TROPHY,   // a long buzz then a little drumroll
    HAPTIC_FEVER,    // rising, the biggest one in the game
    HAPTIC_DENIED,   // a locked menu row
    HAPTIC_COUNT
} Haptic;

void haptics_play(Haptic h);
