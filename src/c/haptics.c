#include "haptics.h"

static const uint32_t P_TICK[]    = { 20 };
static const uint32_t P_ABILITY[] = { 40 };
static const uint32_t P_BUCKET[]  = { 30, 60, 30 };
static const uint32_t P_STYLE[]   = { 25, 50, 25, 50, 25 };
static const uint32_t P_BLAST[]   = { 120, 40, 60 };
static const uint32_t P_LOSE[]    = { 400 };
static const uint32_t P_WIN[]     = { 100, 100, 100 };
static const uint32_t P_TROPHY[]  = { 150, 80, 40, 40, 40, 40, 40 };
static const uint32_t P_FEVER[]   = { 50, 50, 70, 50, 100, 50, 250 };
static const uint32_t P_DENIED[]  = { 60, 60, 60 };

typedef struct {
    const uint32_t *durations;
    uint8_t count;
} Pattern;

#define PATTERN(p) { p, sizeof(p) / sizeof(p[0]) }

static const Pattern s_patterns[HAPTIC_COUNT] = {
    [HAPTIC_TICK] = PATTERN(P_TICK),
    [HAPTIC_ABILITY] = PATTERN(P_ABILITY),
    [HAPTIC_BUCKET] = PATTERN(P_BUCKET),
    [HAPTIC_STYLE] = PATTERN(P_STYLE),
    [HAPTIC_BLAST] = PATTERN(P_BLAST),
    [HAPTIC_LOSE] = PATTERN(P_LOSE),
    [HAPTIC_WIN] = PATTERN(P_WIN),
    [HAPTIC_TROPHY] = PATTERN(P_TROPHY),
    [HAPTIC_FEVER] = PATTERN(P_FEVER),
    [HAPTIC_DENIED] = PATTERN(P_DENIED),
};

static int s_playing = -1;  // Haptic currently holding the motor, -1 if idle
static AppTimer *s_hold;

static void hold_over(void *data) {
    s_hold = NULL;
    s_playing = -1;
}

void haptics_play(Haptic h) {
    if (h >= HAPTIC_COUNT) {
        return;
    }
    if (s_hold && (int)h <= s_playing) {
        return;
    }
    const Pattern *p = &s_patterns[h];
    uint32_t total = 0;
    for (int i = 0; i < p->count; i++) {
        total += p->durations[i];
    }
    vibes_cancel();
    vibes_enqueue_custom_pattern((VibePattern) {
        .durations = p->durations,
        .num_segments = p->count
    });
    s_playing = h;
    // Hold a little past the end so back-to-back patterns stay distinct.
    if (s_hold) {
        app_timer_cancel(s_hold);
    }
    s_hold = app_timer_register(total + 80, hold_over, NULL);
}
