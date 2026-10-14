#include "progress.h"

// Persist keys. 1, 2, 100-105 and 106 are where v2 kept unlocks, endless boards,
// level bests and the endless best; they stay exactly where they were so an
// update loses nothing. Levels 7-12 are new and live at 120+, because 106 was
// already the endless best and v2's "100 + level" scheme would collide with it.
#define KEY_UNLOCKED        1
#define KEY_ENDLESS_BOARDS  2
#define KEY_STATS           3
#define KEY_TROPHIES        4
#define KEY_DAILY           5
#define KEY_RUN             6
#define KEY_LAST_POWER      7
#define KEY_LEVEL_BEST_V2   100 // levels 0-5
#define KEY_ENDLESS_BEST    106
#define KEY_LEVEL_BEST_NEW  120 // levels 6-11

#define V2_LEVELS 6
#define UNLOCK_QUEUE 4

static int s_unlocked;
static int32_t s_best[LEVEL_COUNT];
static int32_t s_endless_best;
static int s_endless_boards;
static Stats s_stats;
static uint32_t s_trophies;
static DailyRecord s_daily;
static uint8_t s_last_power;
static bool s_stats_dirty;

static uint8_t s_unlock_queue[UNLOCK_QUEUE];
static uint8_t s_unlock_head, s_unlock_count;

#if !PEGBLE_LITE
static const char *const s_trophy_names[TROPHY_COUNT] = {
    "First Clear", "Fever Pitch", "Chain Reaction", "Avalanche",
    "Long Shot", "Off the Wall", "Bucket List", "Full Arsenal",
    "Deep Run", "Marathon", "Gold Standard", "Perfectionist",
    "Daily Dose", "Habit Forming", "Sweet Spot", "Bullseye"
};

static const char *const s_trophy_hints[TROPHY_COUNT] = {
    "Clear any level",
    "Clear the last orange peg",
    "Light 10 pegs in one shot",
    "Light 20 pegs in one shot",
    "Orange peg after a long fall",
    "Orange peg straight off a wall",
    "3 free balls in one game",
    "Use all five powers",
    "Clear 5 endless boards",
    "Clear 10 endless boards",
    "3 stars on a level",
    "3 stars on every level",
    "Finish a daily challenge",
    "Daily 3 days running",
    "Hit the pink peg 25 times",
    "Land the 10k fever bucket"
};
#endif

static uint32_t level_key(int level) {
    return (level < V2_LEVELS) ? KEY_LEVEL_BEST_V2 + level
                               : KEY_LEVEL_BEST_NEW + (level - V2_LEVELS);
}

static void read_data(uint32_t key, void *buf, size_t size) {
    memset(buf, 0, size);
    if (persist_exists(key)) {
        persist_read_data(key, buf, size);
    }
}

void progress_load(void) {
    s_unlocked = persist_read_int(KEY_UNLOCKED);
    if (s_unlocked < 0 || s_unlocked >= LEVEL_COUNT) {
        s_unlocked = 0;
    }
    for (int i = 0; i < LEVEL_COUNT; i++) {
        s_best[i] = persist_read_int(level_key(i));
    }
    s_endless_best = persist_read_int(KEY_ENDLESS_BEST);
    s_endless_boards = persist_read_int(KEY_ENDLESS_BOARDS);
    read_data(KEY_STATS, &s_stats, sizeof(s_stats));
    s_trophies = persist_read_int(KEY_TROPHIES);
    read_data(KEY_DAILY, &s_daily, sizeof(s_daily));
    s_last_power = persist_exists(KEY_LAST_POWER) ? persist_read_int(KEY_LAST_POWER) : POWER_MIX;
    s_stats_dirty = false;
    s_unlock_count = 0;
}

// Days since 2020-01-01 for a civil date (Hinnant's days_from_civil, shifted).
// Consecutive dates always differ by exactly one, across months, years and
// leap days — which is what a streak needs.
static int32_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    int32_t yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468 - 18262; // 18262 = 1970 → 2020
}

uint16_t progress_today(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int32_t d = days_from_civil(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    return d > 0 ? (uint16_t)d : 1;
}

int progress_unlocked_level(void) {
    return s_unlocked;
}

// Powers open in order, one per level cleared: multiball from the start, then
// blast, fireball, spooky and magnet. Clearing levels is how they're taught.
int progress_power_unlock_level(Ability a) {
    return (int)a;
}

uint8_t progress_unlocked_powers(void) {
    uint8_t mask = 0;
    for (int a = 0; a < ABILITY_COUNT; a++) {
        if (s_unlocked >= progress_power_unlock_level(a)) {
            mask |= 1 << a;
        }
    }
    return mask;
}

int32_t progress_best(int level) {
    return (level >= 0 && level < LEVEL_COUNT) ? s_best[level] : 0;
}

int progress_stars(int level) {
    int32_t best = progress_best(level);
    if (best <= 0) {
        return 0; // a best is only ever written by a win
    }
    return 1 + (best >= level_par(level, 2)) + (best >= level_par(level, 3));
}

int progress_total_stars(void) {
    int n = 0;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        n += progress_stars(i);
    }
    return n;
}

int32_t progress_endless_best(void) {
    return s_endless_best;
}

int progress_endless_boards(void) {
    return s_endless_boards;
}

const Stats *progress_stats(void) {
    return &s_stats;
}

const DailyRecord *progress_daily(void) {
    return &s_daily;
}

bool progress_daily_done(uint16_t day) {
    return s_daily.last_played == day && day != 0;
}

bool progress_has_trophy(Trophy t) {
    return (s_trophies >> t) & 1;
}

int progress_trophy_count(void) {
    int n = 0;
    for (int i = 0; i < TROPHY_COUNT; i++) {
        n += progress_has_trophy(i);
    }
    return n;
}

#if PEGBLE_LITE
const char *trophy_name(Trophy t) { return ""; }
const char *trophy_hint(Trophy t) { return ""; }
#else
const char *trophy_name(Trophy t) {
    return (t < TROPHY_COUNT) ? s_trophy_names[t] : "";
}

const char *trophy_hint(Trophy t) {
    return (t < TROPHY_COUNT) ? s_trophy_hints[t] : "";
}
#endif

uint8_t progress_last_power(void) {
    return s_last_power;
}

void progress_set_last_power(uint8_t power) {
    if (power != s_last_power) {
        s_last_power = power;
        persist_write_int(KEY_LAST_POWER, power);
    }
}

static void award(Trophy t) {
#if PEGBLE_LITE
    (void)t;
    return;
#endif
    if (progress_has_trophy(t)) {
        return;
    }
    s_trophies |= 1u << t;
    persist_write_int(KEY_TROPHIES, (int32_t)s_trophies);
    if (s_unlock_count < UNLOCK_QUEUE) {
        s_unlock_queue[(s_unlock_head + s_unlock_count) % UNLOCK_QUEUE] = t;
        s_unlock_count++;
    }
}

bool progress_next_unlock(Trophy *out) {
    if (s_unlock_count == 0) {
        return false;
    }
    *out = (Trophy)s_unlock_queue[s_unlock_head];
    s_unlock_head = (s_unlock_head + 1) % UNLOCK_QUEUE;
    s_unlock_count--;
    return true;
}

void progress_flush(void) {
    if (s_stats_dirty) {
        persist_write_data(KEY_STATS, &s_stats, sizeof(s_stats));
        s_stats_dirty = false;
    }
}

void progress_begin_game(void) {
    s_stats.games++;
    s_stats_dirty = true;
}

void progress_daily_finished(uint16_t day, int32_t score) {
    if (s_daily.day != day) {
        s_daily.day = day;
        s_daily.best_today = score;
    } else if (score > s_daily.best_today) {
        s_daily.best_today = score;
    }
    if (s_daily.last_played != day) {
        s_daily.streak = (s_daily.last_played != 0 && s_daily.last_played + 1 == day)
                         ? s_daily.streak + 1 : 1;
        s_daily.last_played = day;
        s_daily.days_played++;
        if (s_daily.streak > s_daily.best_streak) {
            s_daily.best_streak = s_daily.streak;
        }
    }
    if (score > s_daily.best_ever) {
        s_daily.best_ever = score;
    }
    persist_write_data(KEY_DAILY, &s_daily, sizeof(s_daily));
    award(TROPHY_DAILY);
    if (s_daily.streak >= 3) {
        award(TROPHY_STREAK_3);
    }
}

static void level_won(const Play *p) {
    int lv = p->cfg.level;
    if (p->score > s_best[lv]) {
        s_best[lv] = p->score;
        persist_write_int(level_key(lv), p->score);
    }
    if (lv + 1 < LEVEL_COUNT && s_unlocked < lv + 1) {
        s_unlocked = lv + 1;
        persist_write_int(KEY_UNLOCKED, s_unlocked);
    }
    award(TROPHY_FIRST_CLEAR);
    if (progress_stars(lv) == 3) {
        award(TROPHY_THREE_STARS);
    }
    if (progress_total_stars() == 3 * LEVEL_COUNT) {
        award(TROPHY_ALL_STARS);
    }
}

static void endless_over(const Play *p) {
    if (p->score > s_endless_best) {
        s_endless_best = p->score;
        persist_write_int(KEY_ENDLESS_BEST, s_endless_best);
    }
    if (p->boards > s_endless_boards) {
        s_endless_boards = p->boards;
        persist_write_int(KEY_ENDLESS_BOARDS, s_endless_boards);
    }
}

void progress_on_event(const PlayEvent *e, const Play *p) {
    switch (e->type) {
        case EV_ABILITY:
            if (e->a < ABILITY_COUNT && !(s_stats.powers_used & (1 << e->a))) {
                s_stats.powers_used |= 1 << e->a;
                s_stats_dirty = true;
                if (s_stats.powers_used == (1 << ABILITY_COUNT) - 1) {
                    award(TROPHY_ALL_POWERS);
                }
            }
            break;
        case EV_BUCKET:
            s_stats.buckets++;
            s_stats_dirty = true;
            if (e->value >= 3) {
                award(TROPHY_BUCKETS_3);
            }
            break;
        case EV_STYLE:
            s_stats.style_shots++;
            s_stats_dirty = true;
            if (e->a == STYLE_LONG_SHOT) award(TROPHY_LONG_SHOT);
            if (e->a == STYLE_OFF_THE_WALL) award(TROPHY_OFF_THE_WALL);
            break;
        case EV_FEVER:
            s_stats.fevers++;
            s_stats_dirty = true;
            award(TROPHY_FEVER);
            break;
        case EV_FEVER_BUCKET:
            if (e->a == FEVER_SLOTS / 2) {
                award(TROPHY_BULLSEYE);
            }
            break;
        case EV_TURN_END:
            s_stats.shots++;
            s_stats.pegs_cleared += e->value;
            s_stats.bonus_hits += e->b;
            if (e->value > s_stats.best_combo) {
                s_stats.best_combo = e->value;
            }
            s_stats_dirty = true;
            if (e->value >= 10) award(TROPHY_COMBO_10);
            if (e->value >= 20) award(TROPHY_COMBO_20);
            if (s_stats.bonus_hits >= 25) award(TROPHY_BONUS_25);
            break;
        case EV_BOARD_CLEAR:
            s_stats.boards_cleared++;
            s_stats_dirty = true;
            if (e->value >= 5) award(TROPHY_BOARD_5);
            if (e->value >= 10) award(TROPHY_BOARD_10);
            if (e->value > s_endless_boards) {
                // Record a deep run as it happens, not only when it ends.
                s_endless_boards = e->value;
                persist_write_int(KEY_ENDLESS_BOARDS, s_endless_boards);
            }
            progress_flush();
            break;
        case EV_WON:
            s_stats.wins++;
            s_stats_dirty = true;
            if (p->cfg.mode == MODE_LEVEL) {
                level_won(p);
            } else if (p->cfg.mode == MODE_DAILY) {
                progress_daily_finished(p->cfg.day, p->score);
            }
            progress_flush();
            break;
        case EV_LOST:
            if (p->cfg.mode == MODE_ENDLESS) {
                endless_over(p);
            } else if (p->cfg.mode == MODE_DAILY) {
                progress_daily_finished(p->cfg.day, p->score);
            }
            progress_flush();
            break;
        default:
            break;
    }
}

void progress_save_run(const RunSnapshot *s) {
    persist_write_data(KEY_RUN, s, sizeof(*s));
    // A run can be abandoned (a new one started over it) and never reach
    // EV_LOST, so an endless best is banked as it's reached, not at the end.
    if (s->mode == MODE_ENDLESS && s->score > s_endless_best) {
        s_endless_best = s->score;
        persist_write_int(KEY_ENDLESS_BEST, s_endless_best);
    }
}

bool progress_load_run(RunSnapshot *out) {
    if (!persist_exists(KEY_RUN)) {
        return false;
    }
    read_data(KEY_RUN, out, sizeof(*out));
    return out->version == RUN_SNAPSHOT_VERSION;
}

void progress_clear_run(void) {
    persist_delete(KEY_RUN);
}

void progress_save_run_balls(uint8_t balls_left) {
    RunSnapshot s;
    if (progress_load_run(&s)) {
        s.balls_left = balls_left;
        progress_save_run(&s);
    }
}
