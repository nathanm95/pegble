#pragma once

#include <pebble.h>
#include "play.h"

// Everything the game remembers between launches: which levels and powers are
// open, best scores and stars, lifetime stats, trophies, the daily challenge
// record, the last power picked, and one paused run.
//
// It learns about play only through PlayEvents (progress_on_event), so the same
// code runs in the Mac harness against an in-memory persist store.

typedef enum {
    TROPHY_FIRST_CLEAR,
    TROPHY_FEVER,
    TROPHY_COMBO_10,
    TROPHY_COMBO_20,
    TROPHY_LONG_SHOT,
    TROPHY_OFF_THE_WALL,
    TROPHY_BUCKETS_3,
    TROPHY_ALL_POWERS,
    TROPHY_BOARD_5,
    TROPHY_BOARD_10,
    TROPHY_THREE_STARS,
    TROPHY_ALL_STARS,
    TROPHY_DAILY,
    TROPHY_STREAK_3,
    TROPHY_BONUS_25,
    TROPHY_BULLSEYE,
    TROPHY_COUNT
} Trophy;

typedef struct {
    uint32_t shots;
    uint32_t pegs_cleared;
    uint32_t bonus_hits;
    uint32_t boards_cleared; // endless, lifetime
    uint16_t games;
    uint16_t wins;           // levels and dailies won
    uint16_t fevers;
    uint16_t buckets;
    uint16_t style_shots;
    uint16_t best_combo;
    uint8_t powers_used;     // bit per Ability ever fired
    uint8_t reserved[3];
} Stats;

typedef struct {
    uint16_t day;         // the day best_today belongs to
    uint16_t last_played; // the most recent day a daily was finished
    uint16_t streak;      // consecutive days finished, ending at last_played
    uint16_t best_streak;
    uint16_t days_played;
    uint16_t reserved;
    int32_t best_today;
    int32_t best_ever;
} DailyRecord;

void progress_load(void);

// Today as a day number (days since 2020-01-01, local time), for the daily.
uint16_t progress_today(void);

int progress_unlocked_level(void);      // highest open level index
uint8_t progress_unlocked_powers(void); // bit per Ability; one more per level cleared
int progress_power_unlock_level(Ability a); // level to clear to open it (0 = from the start)

int32_t progress_best(int level);
int progress_stars(int level);          // 0 = not cleared, else 1..3
int progress_total_stars(void);
int32_t progress_endless_best(void);
int progress_endless_boards(void);
const Stats *progress_stats(void);
const DailyRecord *progress_daily(void);
bool progress_daily_done(uint16_t day);

bool progress_has_trophy(Trophy t);
int progress_trophy_count(void);
const char *trophy_name(Trophy t);
const char *trophy_hint(Trophy t);

uint8_t progress_last_power(void);
void progress_set_last_power(uint8_t power);

// ---- fed by the game ----

void progress_begin_game(void);
void progress_on_event(const PlayEvent *e, const Play *p);
// Trophies earned since the last call, oldest first, for the toast.
bool progress_next_unlock(Trophy *out);
void progress_daily_finished(uint16_t day, int32_t score);
// Write anything still only in memory. Cheap when nothing changed.
void progress_flush(void);

// ---- the paused run ----

void progress_save_run(const RunSnapshot *s);
bool progress_load_run(RunSnapshot *out);
void progress_clear_run(void);
// Just the balls, after a shot is fired: quitting mid-shot must not refund it.
void progress_save_run_balls(uint8_t balls_left);
