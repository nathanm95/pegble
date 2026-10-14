#pragma once

#include <pebble.h>

// A tiny deterministic generator (xorshift32) for everything that has to come
// out the same every time from the same seed: board layouts, which pegs are
// orange or green, the daily challenge, and the physics' own tie-breaks.
//
// It exists because rand() is shared with the effects code — sparks call it on
// every peg hit — so anything seeded with srand() drifts as soon as the game
// draws a frame. It also makes the Mac test harness (tools/host) produce the
// same boards as a watch, since newlib's rand() and macOS's do not agree.
typedef struct {
    uint32_t s;
} Rng;

static inline void rng_seed(Rng *r, uint32_t seed) {
    // xorshift has one fixed point at zero; any other seed is fine.
    r->s = seed ? seed : 0x9E3779B9u;
}

static inline uint32_t rng_next(Rng *r) {
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

// Uniform-enough integer in [0, n). n <= 0 returns 0.
static inline int rng_range(Rng *r, int n) {
    return (n > 0) ? (int)(rng_next(r) % (uint32_t)n) : 0;
}
