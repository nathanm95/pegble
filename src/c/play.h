#pragma once

#include <pebble.h>
#include "levels.h"
#include "abilities.h"
#include "rng.h"

// The rules and physics of a game, with no drawing, no persistence and no
// vibration — so tools/host can compile this file on a Mac and simulate
// thousands of shots. game.c owns the window: it reads the state below to draw
// it, and turns the events this module emits into sparks, banners and buzzes.
//
// All positions and velocities are Q24.8 fixed point — aplite/diorite have no
// FPU — and everything random comes from a seeded Rng, so a board and a run
// replay exactly from (config, seed).

#define FP_SHIFT 8
#define FP(x) ((int32_t)(x) << FP_SHIFT)
#define TO_PX(x) ((int16_t)((x) >> FP_SHIFT))

// Trig products stay in int32: a sin_lookup value (<= 65535) times a speed or
// radius under ~30,000 fits with room to spare. Don't widen them to int64 — a
// 64-bit divide pulls ~750 bytes of libgcc into the binary, and aplite can't
// spare it (see config.h).

// The new watches have the memory and the screen for a bigger multiball.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define MAX_BALLS 5
#else
#define MAX_BALLS 3
#endif

#define TRAIL_LEN 4
#define FEVER_SLOTS 5
#define POWER_MIX 255     // PlayConfig.power: deal from every unlocked ability

typedef enum { MODE_LEVEL, MODE_ENDLESS, MODE_DAILY } PlayMode;

typedef enum {
    PEG_BLUE,    // ordinary
    PEG_TARGET,  // orange: clear them all to finish the board
    PEG_POWER,   // green: carries an ability
    PEG_BUMPER,  // furniture: never clears, kicks the ball away
    PEG_PORTAL   // furniture: no collision, sends the ball to its partner
} PegType;

typedef enum {
    STATE_AIMING,
    STATE_FLYING,
    STATE_RESOLVING, // lit pegs flash before they go
    STATE_WON,
    STATE_LOST
} GameState;

typedef enum {
    STYLE_COMBO,       // lots of pegs in one shot
    STYLE_LONG_SHOT,   // an orange peg after a long fall
    STYLE_OFF_THE_WALL // an orange peg straight off a side wall
} StyleShot;

typedef enum {
    EV_PEG_HIT,      // a=PegType, b=1 if the bonus peg, value=points, x/y=peg
    EV_ABILITY,      // a=Ability, x/y=peg
    EV_BUMPER,       // value=points, x/y=bumper
    EV_PORTAL,       // x/y=where the ball came out
    EV_SPOOKY,       // the ball came back in at the top; x/y=re-entry
    EV_BUCKET,       // free ball; x/y=bucket
    EV_STUCK,        // a peg the ball was stuck on was removed (or it got a kick)
    EV_COMBO,        // value=pegs this shot, sent at 5, 10, 15...
    EV_STYLE,        // a=StyleShot, value=bonus, x/y=where
    EV_FEVER,        // the last orange peg went; x/y=that peg
    EV_FEVER_BUCKET, // a=slot, value=bonus, x/y=slot centre
    EV_TURN_END,     // value=pegs this shot (always sent, after the combo bonus)
    EV_BOARD_CLEAR,  // endless: value=boards cleared so far, a=balls refunded
    EV_WON,          // value=final score, a=balls left (already scored)
    EV_LOST          // value=final score
} PlayEventType;

typedef struct {
    uint8_t type;
    uint8_t a, b;
    int16_t x, y;
    int32_t value;
} PlayEvent;

typedef struct {
    int16_t x, y;      // current centre, px
    int16_t base_x;    // rest position for a moving peg
    int16_t move;      // px of travel either side of base_x (0 = still)
    uint8_t phase;     // where in its swing a moving peg starts, 0..255
    uint8_t type;      // PegType
    uint8_t ability;   // PEG_POWER only
    int8_t link;       // PEG_PORTAL: index of the partner portal
    bool active;
    bool lit;
} Peg;

typedef struct {
    int32_t x, y, vx, vy;  // Q24.8
    int16_t age;
    int16_t slow_ticks;
    int16_t fire_ticks;    // burning through pegs instead of bouncing
    int16_t magnet_ticks;  // being pulled towards the bucket
    int16_t wall_ticks;    // ticks since the last side-wall bounce
    int16_t last_hit_y;    // px, where the previous peg hit was (long shots)
    int16_t portal_cool;   // ticks before a portal can take this ball again
    int16_t bumper_cool;
    int16_t stuck_x, stuck_y, stuck_ticks;
    int16_t stuck_spread;  // furthest it has strayed from stuck_x/y this window
    uint8_t kicks;         // times furniture had to knock it loose
    uint8_t portal_hops;   // portals taken this shot
    bool spooky;           // one free return from the bottom of the board
    bool active;
    int16_t trail_x[TRAIL_LEN], trail_y[TRAIL_LEN];
    uint8_t trail_n;
} Ball;

typedef struct {
    uint8_t mode;            // PlayMode
    uint8_t level;           // MODE_LEVEL
    uint8_t power;           // an Ability, or POWER_MIX
    uint8_t unlocked_powers; // bit per Ability; what POWER_MIX may deal
    uint16_t day;            // MODE_DAILY: the date as a day number
    uint32_t seed;           // MODE_ENDLESS: first board's seed (0 = pick one)
} PlayConfig;

// Everything game.c draws from. Read it, don't write it.
typedef struct {
    // Layout, from play_layout().
    GRect field;
    int16_t hud_h, peg_r, ball_r;
    int16_t bucket_w, bucket_h, bucket_x;
    int16_t fever_x, fever_w;  // the span the fever buckets share: inset on round screens
    GPoint launch;
    int32_t launch_speed, gravity, max_speed;
    bool round;

    // The run.
    PlayConfig cfg;
    uint32_t board_seed;
    uint8_t theme;
    uint8_t power;           // what this board's green pegs carry (or POWER_MIX)
    Peg pegs[MAX_PEGS];
    uint8_t peg_count;
    int8_t bonus_idx;        // the roaming pink peg, -1 if none
    Ball balls[MAX_BALLS];
    int32_t aim;
    GameState state;
    int16_t resolve_ticks;
    uint32_t tick;
    int32_t score;
    int16_t balls_left;
    uint16_t boards;         // endless: boards cleared this run
    uint8_t target_total, target_cleared;
    uint8_t shot_hits;       // pegs lit by the current shot
    uint8_t run_buckets;     // free balls caught this run
    int32_t bucket_phase;
    uint16_t move_phase;

    // Fever: the last orange peg is gone and the bottom becomes bonus buckets.
    bool fever;
    int16_t slowmo_ticks;
} Play;

// A paused run, small enough for one persist key. Taken between shots, so the
// board is rebuilt from its seed and only what has changed since is stored.
typedef struct {
    uint8_t version;
    uint8_t mode, level, power, unlocked_powers;
    uint8_t theme;
    int8_t bonus_idx;
    uint8_t balls_left;
    uint8_t run_buckets;
    uint16_t day;
    uint16_t boards;
    uint32_t board_seed;
    int32_t score;
    uint32_t active_lo, active_hi; // which of the board's pegs are still standing
} RunSnapshot;

#define RUN_SNAPSHOT_VERSION 4 // 4: boards rebuilt without steel

extern const int32_t FEVER_BUCKET_VALUES[FEVER_SLOTS];

const Play *play_get(void);

// Size the board to the screen. Call before play_start/play_restore.
void play_layout(GRect bounds, bool round);
// The bucket defaults to a quarter of the field; the window sets it to the width
// of the bucket sprite where there is one, so what you see is what catches.
void play_set_bucket_width(int16_t w);

void play_start(const PlayConfig *cfg);
// Rebuilds a run saved with play_snapshot(). False if the snapshot is stale.
bool play_restore(const RunSnapshot *snap);
// Only meaningful between shots (STATE_AIMING).
void play_snapshot(RunSnapshot *out);

// Same mode again: the same level, the same day's board, or a fresh endless run.
void play_retry(void);

void play_set_aim(int32_t aim);
void play_nudge_aim(int direction);
int32_t play_aim_limit(void);
bool play_fire(void);
void play_tick(void);

// The dotted aim guide: up to `max` points, stopping at the first peg in the way.
int play_preview(GPoint *out, int max);

int play_targets_remaining(void);
int play_multiplier(void);
// Which fever bucket a point at x falls into.
int play_fever_slot(int16_t x);

// Pops the oldest pending event. Call until it returns false after every tick.
bool play_next_event(PlayEvent *out);

#if defined(PEGBLE_FEVER_DEMO)
// Debug builds only: clear all but `keep` orange pegs, so the next lucky shot
// starts a fever. For filming and checking the fever without playing a level.
void play_debug_leave_targets(int keep);
#endif
