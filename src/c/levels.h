#pragma once

#include <pebble.h>
#include "config.h"
#include "rng.h"

// Peg positions are defined in a normalized 0..1000 space for both axes so
// the same level data scales to every watch (144x168 up to 260x260 round).
// Aplite has only the original six (see config.h).
#if PEGGLE_LITE
#define LEVEL_COUNT 6
#else
#define LEVEL_COUNT 12
#endif
#define MAX_PEGS 64

// What a slot on the board is. PK_PEG is an ordinary peg whose colour (blue,
// orange or green) is dealt at random when the board is built; the others are
// fixed furniture that never clears and never takes a colour.
typedef enum {
    PK_PEG = 0,
    PK_BUMPER,  // indestructible and springy: kicks the ball away faster
    PK_PORTAL   // no collision: a ball entering one leaves by its partner
} PegKind;

typedef struct {
    int16_t nx;   // 0..1000 across playfield width
    int16_t ny;   // 0..1000 down the peg area
    int16_t move; // PK_PEG: horizontal travel either side, 0..1000 units; 0 = still
    uint8_t kind; // PegKind
    uint8_t pair; // PK_PORTAL: portals sharing a pair id are linked
} PegPos;

const char *level_name(int index);

// Fills out[] with the slots for a hand-built level, returns how many (<= MAX_PEGS).
int level_build(int index, PegPos *out);

// Random board for endless mode and the daily challenge, drawn from rng.
//
// `difficulty` is how far into a run this board is. Later boards get more rows
// packed tighter, and bring in furniture one kind at a time — bumpers from
// board 5, a moving row from 7, a portal pair from 10 — so each step up teaches
// one new thing. The rest of the ramp (more orange to clear, fewer
// power pegs, a faster bucket, a smaller ball refund) is about the rules, not
// the layout, and lives in play.c.
int level_build_endless(PegPos *out, int difficulty, Rng *rng);

// Percentage of a level's ordinary pegs dealt orange. More orange is the main
// difficulty knob: later levels ask for more, tuned with tools/host so random
// aim wins less often the further in you are.
int level_target_pct(int index);

// Score needed for 2 and 3 stars on a level (1 star is just clearing it).
// Calibrated by tools/host simulating thousands of shots per level.
int32_t level_par(int index, int stars);
