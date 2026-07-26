#pragma once

#include <pebble.h>

// Peg positions are defined in a normalized 0..1000 space for both axes so
// the same level data scales to every watch (144x168 up to 200x228, and round).
#define LEVEL_COUNT 6
#define MAX_PEGS 64

typedef struct {
    int16_t nx; // 0..1000 across playfield width
    int16_t ny; // 0..1000 down the peg area
} PegPos;

const char *level_name(int index);

// Fills out[] with peg positions, returns the number of pegs (<= MAX_PEGS).
int level_build(int index, PegPos *out);

// Random board for endless mode. Caller seeds rand() first; every call
// produces a fresh layout from the current rand() stream.
int level_build_endless(PegPos *out);
