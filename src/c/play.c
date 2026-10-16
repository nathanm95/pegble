#include <stdlib.h>
#include <string.h>
#include "play.h"
#include "theme.h"

#define STARTING_BALLS 8
#define STARTING_BALLS_ENDLESS 6
#define ENDLESS_BOARD_BONUS 2500
#define BALL_BONUS 1000          // per unused ball when a board is won
#define RESTITUTION 218          // ~0.85 in Q8
#define BUMPER_KICK 300          // ~1.17 in Q8: a bumper hands back more than it got
#define RESOLVE_TICKS 18         // lit pegs flash ~600ms before vanishing
#define BALL_MAX_AGE 900         // last resort: retire a ball after ~30s (unstick() does the real work)
#define AIM_LIMIT (TRIG_MAX_ANGLE * 75 / 360)
#define AIM_STEP (TRIG_MAX_ANGLE / 240)

#define FIREBALL_TICKS 70        // ~2.3s of burning through pegs
#define MAGNET_TICKS 90
#define BLAST_RADIUS_PEGS 8      // blast reach, in peg radii

#define FEVER_SLOWMO 40          // the moment the last orange goes plays at half speed
#define COMBO_MIN 5              // pegs in one shot before the combo bonus pays
#define LONG_SHOT_BONUS 1000
#define LONG_SHOT_FALL 60        // % of the field fallen between two hits
#define OFF_WALL_BONUS 500
#define OFF_WALL_WINDOW 8        // ticks after a wall bounce that still count
#define NO_HIT_Y INT16_MIN

#define STUCK_SAMPLE 45          // ticks per stuck check (~1.5s)
#define STUCK_SPREAD_PEGS 3      // strayed less than this many peg radii = stuck
#define MAX_KICKS 4              // furniture keeps catching it: let the ball go
#define PORTAL_COOL 6            // ticks clear of every portal before re-entry
#define MAX_PORTAL_HOPS 4        // per shot, so two portals can't loop forever
#define BUMPER_COOL 6
#define BUMPER_POINTS 25

#define EVENT_QUEUE (PEGGLE_LITE ? 20 : 32)

const int32_t FEVER_BUCKET_VALUES[FEVER_SLOTS] = { 2000, 5000, 10000, 5000, 2000 };

static Play s_p;
static Rng s_rng;
static PlayEvent s_events[EVENT_QUEUE];
static uint8_t s_ev_head, s_ev_count;
static uint8_t s_shot_bonus; // the pink peg was lit this shot

const Play *play_get(void) {
    return &s_p;
}

// ---- events ----

// Only the cosmetic events may be dropped if a single tick produces more than
// the queue holds (a big blast can light dozens of pegs at once). The ones the
// trophies and records depend on always get in, displacing the newest event.
static void emit(uint8_t type, uint8_t a, uint8_t b, int16_t x, int16_t y, int32_t value) {
    bool cosmetic = type == EV_PEG_HIT || type == EV_BUMPER || type == EV_PORTAL;
    if (s_ev_count >= EVENT_QUEUE) {
        if (cosmetic) {
            return;
        }
        s_ev_count--;
    }
    PlayEvent *e = &s_events[(s_ev_head + s_ev_count) % EVENT_QUEUE];
    e->type = type;
    e->a = a;
    e->b = b;
    e->x = x;
    e->y = y;
    e->value = value;
    s_ev_count++;
}

bool play_next_event(PlayEvent *out) {
    if (s_ev_count == 0) {
        return false;
    }
    *out = s_events[s_ev_head];
    s_ev_head = (s_ev_head + 1) % EVENT_QUEUE;
    s_ev_count--;
    return true;
}

// ---- small helpers ----

static int32_t isqrt32(int32_t v) {
    int32_t res = 0;
    int32_t bit = 1 << 30;
    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

static bool clearable(const Peg *p) {
    return p->type == PEG_BLUE || p->type == PEG_TARGET || p->type == PEG_POWER;
}

// How hard this board is, on the endless scale: what level_build_endless()
// builds from, and what drives the rules ramp below.
static int board_difficulty(void) {
    switch (s_p.cfg.mode) {
        case MODE_ENDLESS: return s_p.boards;
        case MODE_DAILY:   return 2 + s_p.cfg.day % 5;
        default:           return 0;
    }
}

int play_multiplier(void) {
    if (s_p.target_total == 0) {
        return 1;
    }
    int pct = s_p.target_cleared * 100 / s_p.target_total;
    if (pct >= 80) return 5;
    if (pct >= 60) return 3;
    if (pct >= 40) return 2;
    return 1;
}

int play_targets_remaining(void) {
    int n = 0;
    for (int i = 0; i < s_p.peg_count; i++) {
        const Peg *p = &s_p.pegs[i];
        if (p->active && !p->lit && p->type == PEG_TARGET) {
            n++;
        }
    }
    return n;
}

int play_fever_slot(int16_t x) {
    int slot = (x - s_p.fever_x) * FEVER_SLOTS / (s_p.fever_w > 0 ? s_p.fever_w : 1);
    return slot < 0 ? 0 : (slot >= FEVER_SLOTS ? FEVER_SLOTS - 1 : slot);
}

int32_t play_aim_limit(void) {
    return AIM_LIMIT;
}

// The pink bonus peg roams: a fresh blue peg is promoted every turn.
static void pick_bonus_peg(void) {
    uint8_t candidates[MAX_PEGS];
    int n = 0;
    for (int i = 0; i < s_p.peg_count; i++) {
        const Peg *p = &s_p.pegs[i];
        if (p->active && !p->lit && p->type == PEG_BLUE) {
            candidates[n++] = i;
        }
    }
    s_p.bonus_idx = (n > 0) ? candidates[rng_range(&s_rng, n)] : -1;
}

// ---- layout ----

void play_layout(GRect bounds, bool round) {
    s_p.round = round;
    s_p.hud_h = round ? 24 : 16;
    int side = round ? 20 : 2;
    s_p.field = GRect(bounds.origin.x + side, bounds.origin.y + s_p.hud_h,
                      bounds.size.w - side * 2,
                      bounds.size.h - s_p.hud_h - (round ? 10 : 2));
    s_p.peg_r = (s_p.field.size.w >= 170) ? 5 : 4;
    s_p.ball_r = s_p.peg_r - 1;
    s_p.bucket_w = s_p.field.size.w / 4;
    s_p.bucket_h = s_p.peg_r * 2 + 2;
    // The bottom corners of a round field are off the glass, so the fever
    // buckets share only the part you can see (balls landing wide count for the
    // end bucket on their side).
    int fever_inset = round ? s_p.field.size.w * 3 / 20 : 0;
    s_p.fever_x = s_p.field.origin.x + fever_inset;
    s_p.fever_w = s_p.field.size.w - 2 * fever_inset;
    s_p.launch = GPoint(s_p.field.origin.x + s_p.field.size.w / 2, s_p.field.origin.y + 3);
    // Physics scale with playfield height so the game feels the same everywhere.
    s_p.launch_speed = FP(s_p.field.size.h) / 40;
    s_p.gravity = FP(s_p.field.size.h) / 1100;
    if (s_p.gravity < 20) {
        s_p.gravity = 20;
    }
    s_p.max_speed = s_p.launch_speed * 8 / 5;
}

void play_set_bucket_width(int16_t w) {
    if (w > 0) {
        s_p.bucket_w = w;
    }
}

// ---- building a board ----

static void place_moving_pegs(void) {
#if !PEGGLE_LITE
    for (int i = 0; i < s_p.peg_count; i++) {
        Peg *p = &s_p.pegs[i];
        if (p->move) {
            int32_t a = s_p.move_phase + p->phase * (TRIG_MAX_ANGLE / 256);
            p->x = p->base_x + (int16_t)((int32_t)p->move * sin_lookup(a) / TRIG_MAX_RATIO);
        }
    }
#endif
}

static void build_board(uint32_t seed) {
    PegPos pos[MAX_PEGS];
    int difficulty = board_difficulty();
    s_p.board_seed = seed;
    rng_seed(&s_rng, seed);

    int n;
    switch (s_p.cfg.mode) {
        case MODE_ENDLESS:
            n = level_build_endless(pos, difficulty, &s_rng);
            // Each board takes the next table, so a long run keeps moving.
            s_p.theme = (s_p.boards + 1) % THEME_COUNT;
            break;
        case MODE_DAILY:
            n = level_build_endless(pos, difficulty, &s_rng);
            s_p.theme = s_p.cfg.day % THEME_COUNT;
            break;
        default:
            n = level_build(s_p.cfg.level, pos);
            s_p.theme = s_p.cfg.level % THEME_COUNT;
            break;
    }
    s_p.peg_count = n;

    int margin = s_p.peg_r + 3;
    int width = s_p.field.size.w - 2 * margin;
    int top = s_p.field.origin.y + s_p.field.size.h * 16 / 100;
    int bottom = s_p.field.origin.y + s_p.field.size.h * 80 / 100;
    for (int i = 0; i < n; i++) {
        Peg *p = &s_p.pegs[i];
        memset(p, 0, sizeof(*p));
        p->base_x = s_p.field.origin.x + margin + pos[i].nx * width / 1000;
        p->x = p->base_x;
        p->y = top + pos[i].ny * (bottom - top) / 1000;
        p->active = true;
        p->link = -1;
        switch (pos[i].kind) {
            case PK_BUMPER: p->type = PEG_BUMPER; break;
            case PK_PORTAL: p->type = PEG_PORTAL; break;
            default:        p->type = PEG_BLUE; break;
        }
        if (pos[i].move && pos[i].kind != PK_PORTAL) {
            p->move = pos[i].move * width / 1000;
            p->phase = pos[i].pair;
        }
    }
#if !PEGGLE_LITE
    // Link each portal to the other one with its pair id.
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n && s_p.pegs[i].type == PEG_PORTAL; j++) {
            if (s_p.pegs[j].type == PEG_PORTAL && pos[j].pair == pos[i].pair) {
                s_p.pegs[i].link = j;
                s_p.pegs[j].link = i;
            }
        }
    }
#endif
    place_moving_pegs();

    // Shuffle the ordinary pegs, then deal targets and power pegs off the top.
    uint8_t pool[MAX_PEGS];
    int pool_n = 0;
    for (int i = 0; i < n; i++) {
        if (s_p.pegs[i].type == PEG_BLUE) {
            pool[pool_n++] = i;
        }
    }
    for (int i = pool_n - 1; i > 0; i--) {
        int j = rng_range(&s_rng, i + 1);
        uint8_t t = pool[i];
        pool[i] = pool[j];
        pool[j] = t;
    }

    // Later boards ask for more of the board to be cleared...
    int targets;
    if (s_p.cfg.mode == MODE_LEVEL) {
        targets = pool_n * level_target_pct(s_p.cfg.level) / 100;
    } else {
        int share = 4 - (difficulty >= 4) - (difficulty >= 9); // 1/4, then 1/3, then 1/2
        targets = pool_n / share;
    }
    if (targets < 5) targets = 5;
    if (targets > pool_n - 3) targets = pool_n - 3;
    if (targets < 1) targets = 1;
    for (int i = 0; i < targets; i++) {
        s_p.pegs[pool[i]].type = PEG_TARGET;
    }

    // ...and hand out fewer power pegs to do it with.
    int powers = 3;
    if (difficulty >= 4) powers = 2;
    if (difficulty >= 8) powers = 1;

    // The daily board has one power for everyone, so each day plays differently.
    s_p.power = (s_p.cfg.mode == MODE_DAILY) ? (s_p.cfg.day * 7) % ABILITY_COUNT : s_p.cfg.power;

    // Mix deals distinct abilities where there are enough to go round: shuffle
    // the unlocked ones and take from the top, so a board is never three of the
    // same thing. A chosen power puts that one ability on every green peg.
    uint8_t deck[ABILITY_COUNT];
    int deck_n = 0;
    if (s_p.power == POWER_MIX) {
        for (int i = 0; i < ABILITY_COUNT; i++) {
            if (s_p.cfg.unlocked_powers & (1 << i)) {
                deck[deck_n++] = i;
            }
        }
        if (deck_n == 0) {
            deck[deck_n++] = ABILITY_MULTIBALL;
        }
        for (int i = deck_n - 1; i > 0; i--) {
            int j = rng_range(&s_rng, i + 1);
            uint8_t t = deck[i];
            deck[i] = deck[j];
            deck[j] = t;
        }
    } else {
        deck[deck_n++] = s_p.power % ABILITY_COUNT;
    }
    for (int i = 0; i < powers && targets + i < pool_n; i++) {
        Peg *p = &s_p.pegs[pool[targets + i]];
        p->type = PEG_POWER;
        p->ability = deck[i % deck_n];
    }

    s_p.target_total = targets;
    s_p.target_cleared = 0;
    s_p.fever = false;
    s_p.slowmo_ticks = 0;
    s_p.shot_hits = 0;
    pick_bonus_peg();
#if defined(PEGGLE_FEVER_DEMO)
    play_debug_leave_targets(PEGGLE_FEVER_DEMO);
#endif
}

static void reset_ball(Ball *b) {
    memset(b, 0, sizeof(*b));
}

static void reset_run(void) {
    s_p.boards = 0;
    s_p.score = 0;
    s_p.run_buckets = 0;
    s_p.aim = 0;
    s_p.state = STATE_AIMING;
    s_p.bucket_phase = 0;
    s_p.shot_hits = 0;
    s_shot_bonus = 0;
    s_ev_count = 0;
    for (int i = 0; i < MAX_BALLS; i++) {
        reset_ball(&s_p.balls[i]);
    }
}

static uint32_t first_board_seed(void) {
    switch (s_p.cfg.mode) {
        case MODE_LEVEL:
            // Fixed per level, so a level's colours are the same every time.
            return s_p.cfg.level * 7919u + 13u;
        case MODE_DAILY:
            // Fixed per day, so every player gets the same board.
            return (s_p.cfg.day + 1u) * 2654435761u;
        default:
            return s_p.cfg.seed ? s_p.cfg.seed
                                : ((uint32_t)time(NULL) ^ (s_p.tick << 8) ^ 0xA5A5u);
    }
}

void play_start(const PlayConfig *cfg) {
    s_p.cfg = *cfg;
    reset_run();
    s_p.balls_left = (cfg->mode == MODE_ENDLESS) ? STARTING_BALLS_ENDLESS : STARTING_BALLS;
#if defined(PEGGLE_BOARD)
    // Debug builds: start endless deeper in, to see what later boards bring.
    if (cfg->mode == MODE_ENDLESS) {
        s_p.boards = PEGGLE_BOARD;
    }
#endif
    build_board(first_board_seed());
}

void play_retry(void) {
    PlayConfig cfg = s_p.cfg;
    cfg.seed = 0; // endless: a new run, not a replay of the last one
    play_start(&cfg);
}

void play_snapshot(RunSnapshot *o) {
    memset(o, 0, sizeof(*o));
    o->version = RUN_SNAPSHOT_VERSION;
    o->mode = s_p.cfg.mode;
    o->level = s_p.cfg.level;
    o->power = s_p.cfg.power;
    o->unlocked_powers = s_p.cfg.unlocked_powers;
    o->day = s_p.cfg.day;
    o->theme = s_p.theme;
    o->bonus_idx = s_p.bonus_idx;
    o->balls_left = s_p.balls_left;
    o->run_buckets = s_p.run_buckets;
    o->boards = s_p.boards;
    o->board_seed = s_p.board_seed;
    o->score = s_p.score;
    for (int i = 0; i < s_p.peg_count; i++) {
        if (s_p.pegs[i].active) {
            if (i < 32) {
                o->active_lo |= 1u << i;
            } else {
                o->active_hi |= 1u << (i - 32);
            }
        }
    }
}

bool play_restore(const RunSnapshot *s) {
    if (s->version != RUN_SNAPSHOT_VERSION || s->mode > MODE_DAILY) {
        return false;
    }
    PlayConfig cfg = {
        .mode = s->mode, .level = s->level, .power = s->power,
        .unlocked_powers = s->unlocked_powers, .day = s->day, .seed = 0
    };
    if (cfg.mode == MODE_LEVEL && cfg.level >= LEVEL_COUNT) {
        return false;
    }
    s_p.cfg = cfg;
    reset_run();
    s_p.boards = s->boards; // before the build: it sets the difficulty
    build_board(s->board_seed);

    int standing_targets = 0;
    for (int i = 0; i < s_p.peg_count; i++) {
        uint32_t bit = (i < 32) ? (s->active_lo >> i) & 1 : (s->active_hi >> (i - 32)) & 1;
        s_p.pegs[i].active = bit != 0;
        if (s_p.pegs[i].active && s_p.pegs[i].type == PEG_TARGET) {
            standing_targets++;
        }
    }
    s_p.target_cleared = s_p.target_total - standing_targets;
    if (s->bonus_idx >= 0 && s->bonus_idx < s_p.peg_count
        && s_p.pegs[s->bonus_idx].active && s_p.pegs[s->bonus_idx].type == PEG_BLUE) {
        s_p.bonus_idx = s->bonus_idx;
    } else {
        pick_bonus_peg();
    }
    s_p.score = s->score;
    s_p.balls_left = s->balls_left;
    s_p.run_buckets = s->run_buckets;
    return standing_targets > 0;
}

// ---- abilities ----

static void spawn_multiball(const Ball *src) {
    // Big watches split three ways, the rest two. The first clone mirrors the
    // ball; a second one goes up the middle.
    int clones = (MAX_BALLS >= 5) ? 2 : 1;
    int made = 0;
    for (int i = 0; i < MAX_BALLS && made < clones; i++) {
        Ball *c = &s_p.balls[i];
        if (c->active) {
            continue;
        }
        *c = *src;
        c->vx = (made == 0) ? -src->vx : src->vx / 3;
        c->vy = -abs(src->vy) * 3 / 4; // pop the clone upward
        c->age = 0;
        c->slow_ticks = 0;
        c->trail_n = 0;
        c->stuck_ticks = 0;
        made++;
    }
}

static void light_peg(int i, Ball *b, bool fire_ability);

static void trigger_blast(int origin) {
    const Peg *src = &s_p.pegs[origin];
    int radius = s_p.peg_r * BLAST_RADIUS_PEGS;
    int r2 = radius * radius;
    for (int i = 0; i < s_p.peg_count; i++) {
        const Peg *p = &s_p.pegs[i];
        if (i == origin || !p->active || p->lit || !clearable(p)) {
            continue;
        }
        int dx = p->x - src->x;
        int dy = p->y - src->y;
        if (dx * dx + dy * dy <= r2) {
            // No ball and no ability: the blast must not chain into other power
            // pegs, or two blasts in range would recurse.
            light_peg(i, NULL, false);
        }
    }
}

static void trigger_ability(Ability a, int peg_idx, Ball *b) {
    const Peg *p = &s_p.pegs[peg_idx];
    emit(EV_ABILITY, a, 0, p->x, p->y, 0);
    switch (a) {
        case ABILITY_MULTIBALL: spawn_multiball(b); break;
        case ABILITY_BLAST:     trigger_blast(peg_idx); break;
        case ABILITY_FIREBALL:  b->fire_ticks = FIREBALL_TICKS; break;
        case ABILITY_SPOOKY:    b->spooky = true; break;
        case ABILITY_MAGNET:    b->magnet_ticks = MAGNET_TICKS; break;
        default: break;
    }
}

static void start_fever(const Peg *p) {
    s_p.fever = true;
    s_p.slowmo_ticks = FEVER_SLOWMO;
    emit(EV_FEVER, 0, 0, p->x, p->y, 0);
}

// Lights a peg: scores it, counts it toward the shot's combo and style bonuses,
// and fires its ability. `b` is the ball that did it, NULL for a blast.
static void light_peg(int i, Ball *b, bool fire_ability) {
    Peg *p = &s_p.pegs[i];
    if (p->lit || !clearable(p)) {
        return;
    }
    p->lit = true;
    s_p.shot_hits++;
    int base = 10;
    if (p->type == PEG_TARGET) {
        s_p.target_cleared++;
        base = 100;
    } else if (p->type == PEG_POWER) {
        base = 50;
    }
    bool bonus = (i == s_p.bonus_idx);
    if (bonus) {
        base = 500;
        s_shot_bonus = 1;
    }
    int32_t points = base * play_multiplier();
    s_p.score += points;
    emit(EV_PEG_HIT, p->type, bonus, p->x, p->y, points);

    if (b && p->type == PEG_TARGET) {
        // Long shot: an orange peg after a long fall from the previous hit.
        if (b->last_hit_y != NO_HIT_Y
            && p->y - b->last_hit_y >= s_p.field.size.h * LONG_SHOT_FALL / 100) {
            s_p.score += LONG_SHOT_BONUS;
            emit(EV_STYLE, STYLE_LONG_SHOT, 0, p->x, p->y, LONG_SHOT_BONUS);
        }
        // Off the wall: an orange peg straight off a side wall.
        if (b->wall_ticks <= OFF_WALL_WINDOW) {
            s_p.score += OFF_WALL_BONUS;
            emit(EV_STYLE, STYLE_OFF_THE_WALL, 0, p->x, p->y, OFF_WALL_BONUS);
            b->wall_ticks = OFF_WALL_WINDOW + 1; // once per bounce
        }
    }
    if (b) {
        b->last_hit_y = p->y;
    }
    if (s_p.shot_hits % 5 == 0) {
        emit(EV_COMBO, 0, 0, p->x, p->y, s_p.shot_hits);
    }
    if (p->type == PEG_TARGET && s_p.target_cleared >= s_p.target_total && !s_p.fever) {
        start_fever(p);
    }
    if (fire_ability && b && p->type == PEG_POWER) {
        trigger_ability((Ability)p->ability, i, b);
    }
}

// ---- physics ----

#if !PEGGLE_LITE
static void bump(Ball *b, int i, int32_t nx, int32_t ny) {
    // Hand back more speed than came in, and never less than a decent shove.
    b->vx = b->vx * BUMPER_KICK / 256;
    b->vy = b->vy * BUMPER_KICK / 256;
    int32_t out = (b->vx * nx + b->vy * ny) >> FP_SHIFT;
    int32_t want = s_p.launch_speed * 3 / 4;
    if (out < want) {
        b->vx += ((want - out) * nx) >> FP_SHIFT;
        b->vy += ((want - out) * ny) >> FP_SHIFT;
    }
    // A little sideways scatter, like a real pinball bumper. Without it a ball
    // dropped dead-centre onto a bumper climbs back up the same line, hits the
    // ceiling and comes straight back down, forever (the sim found one).
    int32_t jitter = s_p.launch_speed / 6;
    b->vx += rng_range(&s_rng, 2 * jitter + 1) - jitter;
    if (b->bumper_cool == 0) {
        b->bumper_cool = BUMPER_COOL;
        s_p.score += BUMPER_POINTS;
        emit(EV_BUMPER, 0, 0, s_p.pegs[i].x, s_p.pegs[i].y, BUMPER_POINTS);
    }
}
#endif

static void collide_pegs(Ball *b) {
    int32_t rsum = FP(s_p.ball_r + s_p.peg_r);
    for (int i = 0; i < s_p.peg_count; i++) {
        Peg *p = &s_p.pegs[i];
        if (!p->active || p->type == PEG_PORTAL) {
            continue;
        }
        int32_t dx = b->x - FP(p->x);
        int32_t dy = b->y - FP(p->y);
        if (dx > rsum || dx < -rsum || dy > rsum || dy < -rsum) {
            continue;
        }
        // Work in Q4.4 for the squared distance so int32 can't overflow.
        int32_t dx4 = dx >> 4, dy4 = dy >> 4, r4 = rsum >> 4;
        int32_t d2 = dx4 * dx4 + dy4 * dy4;
        if (d2 > r4 * r4) {
            continue;
        }
        // A burning ball passes straight through anything that can clear.
        if (b->fire_ticks > 0 && clearable(p)) {
            light_peg(i, b, true);
            continue;
        }
        int32_t dist4 = isqrt32(d2);
        if (dist4 == 0) {
            dx4 = 1;
            dist4 = 1;
        }
        int32_t nx = (dx4 << FP_SHIFT) / dist4; // unit normal, Q8
        int32_t ny = (dy4 << FP_SHIFT) / dist4;
        int32_t dot = (b->vx * nx + b->vy * ny) >> FP_SHIFT;
        if (dot < 0) {
            b->vx -= (2 * dot * nx) >> FP_SHIFT;
            b->vy -= (2 * dot * ny) >> FP_SHIFT;
#if !PEGGLE_LITE
            if (p->type == PEG_BUMPER) {
                bump(b, i, nx, ny);
            } else
#endif
            {
                b->vx = b->vx * RESTITUTION / 256;
                b->vy = b->vy * RESTITUTION / 256;
            }
        }
        int32_t pen = rsum - (dist4 << 4);
        if (pen > 0) {
            b->x += (nx * pen) >> FP_SHIFT;
            b->y += (ny * pen) >> FP_SHIFT;
        }
        if (clearable(p)) {
            light_peg(i, b, true);
        }
    }
}

static bool inside_peg(const Ball *b, const Peg *p, int extra) {
    int32_t r = s_p.ball_r + s_p.peg_r + extra;
    int32_t dx = TO_PX(b->x) - p->x, dy = TO_PX(b->y) - p->y;
    return dx * dx + dy * dy <= r * r;
}

#if !PEGGLE_LITE
// A ball entering a portal leaves by its partner with the same velocity. It
// has to spend PORTAL_COOL ticks clear of every portal before another can take
// it, or it would bounce between the two forever.
static void check_portals(Ball *b) {
    bool touching = false;
    for (int i = 0; i < s_p.peg_count; i++) {
        const Peg *p = &s_p.pegs[i];
        if (!p->active || p->type != PEG_PORTAL || p->link < 0 || !inside_peg(b, p, 0)) {
            continue;
        }
        touching = true;
        if (b->portal_cool == 0 && b->portal_hops < MAX_PORTAL_HOPS) {
            b->portal_hops++;
            const Peg *out = &s_p.pegs[p->link];
            b->x = FP(out->x);
            b->y = FP(out->y);
            b->portal_cool = PORTAL_COOL;
            b->trail_n = 0;
            emit(EV_PORTAL, 0, 0, out->x, out->y, 0);
            return;
        }
    }
    if (!touching && b->portal_cool > 0) {
        b->portal_cool--;
    }
}
#endif

// A ball that has barely moved for a few seconds is wedged. Peggle's answer is
// to take away the peg it is sitting on; furniture can't go, so that gets a kick
// instead. This is what fixes balls parking in the bottom of The Funnel.
static void unstick(Ball *b) {
    int best = -1;
    int32_t best_d = INT32_MAX;
    for (int i = 0; i < s_p.peg_count; i++) {
        const Peg *p = &s_p.pegs[i];
        if (!p->active || p->type == PEG_PORTAL || !inside_peg(b, p, 3)) {
            continue;
        }
        int32_t dx = TO_PX(b->x) - p->x, dy = TO_PX(b->y) - p->y;
        if (dx * dx + dy * dy < best_d) {
            best_d = dx * dx + dy * dy;
            best = i;
        }
    }
    int16_t x = TO_PX(b->x), y = TO_PX(b->y);
    if (best >= 0 && clearable(&s_p.pegs[best])) {
        light_peg(best, b, true);
        s_p.pegs[best].active = false; // gone now, not at the end of the shot
    } else if (++b->kicks > MAX_KICKS) {
        // Furniture that keeps catching it back is a pocket with no way out.
        // Let the ball go rather than make the player watch it rattle.
        b->active = false;
    } else {
        b->vy = -s_p.launch_speed * 3 / 4;
        b->vx = (rng_range(&s_rng, 2) ? 1 : -1) * s_p.launch_speed / 2;
    }
    emit(EV_STUCK, 0, 0, x, y, 0);
}

static void push_trail(Ball *b) {
    for (int i = TRAIL_LEN - 1; i > 0; i--) {
        b->trail_x[i] = b->trail_x[i - 1];
        b->trail_y[i] = b->trail_y[i - 1];
    }
    b->trail_x[0] = TO_PX(b->x);
    b->trail_y[0] = TO_PX(b->y);
    if (b->trail_n < TRAIL_LEN) {
        b->trail_n++;
    }
}

static void bucket_or_fever(Ball *b) {
    const GRect *f = &s_p.field;
    int bucket_top = f->origin.y + f->size.h - s_p.bucket_h;
    if (b->vy <= 0 || TO_PX(b->y) + s_p.ball_r < bucket_top) {
        return;
    }
    int bx = TO_PX(b->x);
    if (s_p.fever) {
        // The whole bottom edge is bonus buckets now; every ball lands in one.
        int slot = play_fever_slot(bx);
        int32_t bonus = FEVER_BUCKET_VALUES[slot];
        int16_t cx = s_p.fever_x + (2 * slot + 1) * s_p.fever_w / (2 * FEVER_SLOTS);
        s_p.score += bonus;
        emit(EV_FEVER_BUCKET, slot, 0, cx, bucket_top, bonus);
        b->active = false;
        return;
    }
    int half = s_p.bucket_w / 2 - s_p.ball_r;
    if (bx >= s_p.bucket_x - half && bx <= s_p.bucket_x + half) {
        s_p.balls_left++;
        s_p.run_buckets++;
        emit(EV_BUCKET, 0, 0, s_p.bucket_x, bucket_top, s_p.run_buckets);
        b->active = false;
    }
}

static void step_ball(Ball *b) {
    push_trail(b);
    if (b->fire_ticks > 0) b->fire_ticks--;
    if (b->bumper_cool > 0) b->bumper_cool--;
    if (b->wall_ticks < 1000) b->wall_ticks++;

    const GRect *f = &s_p.field;
    b->vy += s_p.gravity;
    // The magnet leans the ball towards the bucket on the way down.
    if (b->magnet_ticks > 0) {
        b->magnet_ticks--;
        if (b->vy > 0 && TO_PX(b->y) > f->origin.y + f->size.h / 2) {
            b->vx += (s_p.bucket_x > TO_PX(b->x)) ? s_p.gravity : -s_p.gravity;
        }
    }
    int32_t max = s_p.max_speed;
    if (b->vy > max) b->vy = max;
    if (b->vy < -max) b->vy = -max;
    if (b->vx > max) b->vx = max;
    if (b->vx < -max) b->vx = -max;
    b->x += b->vx;
    b->y += b->vy;

    int32_t left = FP(f->origin.x + s_p.ball_r);
    int32_t right = FP(f->origin.x + f->size.w - s_p.ball_r);
    int32_t top = FP(f->origin.y + s_p.ball_r);
    if (b->x < left) {
        b->x = left;
        b->vx = -b->vx * RESTITUTION / 256;
        b->wall_ticks = 0;
    } else if (b->x > right) {
        b->x = right;
        b->vx = -b->vx * RESTITUTION / 256;
        b->wall_ticks = 0;
    }
    if (b->y < top && b->vy < 0) {
        b->y = top;
        b->vy = -b->vy * RESTITUTION / 256;
    }

    collide_pegs(b);
#if !PEGGLE_LITE
    check_portals(b);
#endif
    bucket_or_fever(b);
    if (!b->active) {
        return;
    }

    if (TO_PX(b->y) - s_p.ball_r > f->origin.y + f->size.h) {
        // Spooky: one free return from the bottom, straight back in at the top.
        if (b->spooky) {
            b->spooky = false;
            b->y = FP(f->origin.y + s_p.ball_r);
            b->vy = s_p.launch_speed / 2;
            b->trail_n = 0;
            emit(EV_SPOOKY, 0, 0, TO_PX(b->x), f->origin.y + s_p.ball_r, 0);
            return;
        }
        b->active = false;
        return;
    }

    // Anti-stuck, gentle: a ball resting on a peg gets a sideways nudge...
    if (abs(b->vx) + abs(b->vy) < FP(1)) {
        if (++b->slow_ticks > 20) {
            b->vx += rng_range(&s_rng, 2) ? FP(1) : -FP(1);
            b->slow_ticks = 0;
        }
    } else {
        b->slow_ticks = 0;
    }
    // ...and firm: one that hasn't really gone anywhere in a while loses its
    // peg. "Gone anywhere" is the furthest it strayed over the whole window, not
    // two snapshots, so a ball rattling back and forth in a pocket counts too.
    int16_t x = TO_PX(b->x), y = TO_PX(b->y);
    int16_t spread = abs(x - b->stuck_x) > abs(y - b->stuck_y) ? abs(x - b->stuck_x)
                                                               : abs(y - b->stuck_y);
    if (spread > b->stuck_spread) {
        b->stuck_spread = spread;
    }
    if (++b->stuck_ticks >= STUCK_SAMPLE) {
        bool stuck = b->stuck_spread < s_p.peg_r * STUCK_SPREAD_PEGS;
        b->stuck_ticks = 0;
        b->stuck_spread = 0;
        b->stuck_x = x;
        b->stuck_y = y;
        if (stuck) {
            unstick(b);
            if (!b->active) {
                return;
            }
        }
    }
    if (++b->age > BALL_MAX_AGE) {
        b->active = false;
    }
}

static void finish_turn(void) {
    for (int i = 0; i < s_p.peg_count; i++) {
        Peg *p = &s_p.pegs[i];
        if (p->lit) {
            p->active = false;
            p->lit = false;
        }
    }

    int hits = s_p.shot_hits;
    if (hits >= COMBO_MIN) {
        int32_t bonus = (int32_t)hits * hits * 10;
        s_p.score += bonus;
        emit(EV_STYLE, STYLE_COMBO, 0, s_p.launch.x,
             s_p.field.origin.y + s_p.field.size.h / 2, bonus);
    }
    emit(EV_TURN_END, 0, s_shot_bonus, 0, 0, hits);
    s_p.shot_hits = 0;
    s_shot_bonus = 0;

    if (play_targets_remaining() == 0) {
        if (s_p.cfg.mode == MODE_ENDLESS) {
            // Endless never ends on a clear: fresh board, bonus, keep rolling.
            s_p.boards++;
            s_p.score += ENDLESS_BOARD_BONUS;
            // The refund shrinks as the run goes on, so later boards have to pay
            // for themselves out of bucket catches.
            int refund = (s_p.boards >= 8) ? 1 : 2;
            s_p.balls_left += refund;
            build_board(rng_next(&s_rng) | 1u);
            emit(EV_BOARD_CLEAR, refund, 0, 0, 0, s_p.boards);
            s_p.state = STATE_AIMING;
            return;
        }
        s_p.score += s_p.balls_left * BALL_BONUS;
        s_p.state = STATE_WON;
        emit(EV_WON, s_p.balls_left, 0, 0, 0, s_p.score);
    } else if (s_p.balls_left <= 0) {
        s_p.state = STATE_LOST;
        emit(EV_LOST, 0, 0, 0, 0, s_p.score);
    } else {
        s_p.state = STATE_AIMING;
        pick_bonus_peg();
    }
}

void play_tick(void) {
    s_p.tick++;
#if !PEGGLE_LITE
    s_p.move_phase += TRIG_MAX_ANGLE / 200; // a moving row swings every ~6.6s
    place_moving_pegs();
#endif

    // The bucket patrols the bottom edge, faster as endless ramps up.
    s_p.bucket_phase += TRIG_MAX_ANGLE / 150 + board_difficulty() * (TRIG_MAX_ANGLE / 1800);
    // Round screens: keep the bucket's travel inside the visible circle.
    int amp = (s_p.field.size.w - s_p.bucket_w) / 2 - (s_p.round ? 26 : 2);
    s_p.bucket_x = s_p.field.origin.x + s_p.field.size.w / 2
                   + (int)((int32_t)amp * sin_lookup(s_p.bucket_phase) / TRIG_MAX_RATIO);

    if (s_p.state == STATE_FLYING) {
        // The moment the last orange goes plays out at half speed.
        bool step = true;
        if (s_p.slowmo_ticks > 0) {
            s_p.slowmo_ticks--;
            step = (s_p.tick & 1) == 0;
        }
        bool any = false;
        for (int i = 0; i < MAX_BALLS; i++) {
            Ball *b = &s_p.balls[i];
            if (b->active && step) {
                step_ball(b);
            }
            any = any || b->active;
        }
        if (!any) {
            s_p.state = STATE_RESOLVING;
            s_p.resolve_ticks = RESOLVE_TICKS;
        }
    } else if (s_p.state == STATE_RESOLVING) {
        if (--s_p.resolve_ticks <= 0) {
            finish_turn();
        }
    }
}

void play_set_aim(int32_t aim) {
    if (aim > AIM_LIMIT) aim = AIM_LIMIT;
    if (aim < -AIM_LIMIT) aim = -AIM_LIMIT;
    s_p.aim = aim;
}

void play_nudge_aim(int direction) {
    play_set_aim(s_p.aim + direction * AIM_STEP);
}

bool play_fire(void) {
    if (s_p.state != STATE_AIMING || s_p.balls_left <= 0) {
        return false;
    }
    s_p.balls_left--;
    Ball *b = &s_p.balls[0];
    reset_ball(b);
    b->x = FP(s_p.launch.x);
    b->y = FP(s_p.launch.y);
    b->vx = (int32_t)((int32_t)sin_lookup(s_p.aim) * s_p.launch_speed / TRIG_MAX_RATIO);
    b->vy = (int32_t)((int32_t)cos_lookup(s_p.aim) * s_p.launch_speed / TRIG_MAX_RATIO);
    b->active = true;
    b->last_hit_y = NO_HIT_Y;
    b->wall_ticks = 1000;
    b->stuck_x = s_p.launch.x;
    b->stuck_y = s_p.launch.y;
    s_p.shot_hits = 0;
    s_shot_bonus = 0;
    s_p.state = STATE_FLYING;
    return true;
}

#if defined(PEGGLE_FEVER_DEMO)
void play_debug_leave_targets(int keep) {
    int seen = 0;
    for (int i = 0; i < s_p.peg_count; i++) {
        Peg *p = &s_p.pegs[i];
        if (p->type == PEG_TARGET && ++seen > keep) {
            p->type = PEG_BLUE;
            s_p.target_total--;
        }
    }
}
#endif

// The aim guide for a given aim: fills up to `max` dots, returns how many, and
// reports the peg it stops at (-1 if it runs out first).
static int preview(int32_t aim, GPoint *out, int max, int *hit) {
    int32_t x = FP(s_p.launch.x), y = FP(s_p.launch.y);
    int32_t vx = (int32_t)((int32_t)sin_lookup(aim) * s_p.launch_speed / TRIG_MAX_RATIO);
    int32_t vy = (int32_t)((int32_t)cos_lookup(aim) * s_p.launch_speed / TRIG_MAX_RATIO);
    int32_t left = FP(s_p.field.origin.x + s_p.ball_r);
    int32_t right = FP(s_p.field.origin.x + s_p.field.size.w - s_p.ball_r);
    int32_t rsum4 = FP(s_p.ball_r + s_p.peg_r) >> 4;
    int n = 0;
    *hit = -1;
    for (int step = 0; step < 30 && n < max; step++) {
        vy += s_p.gravity;
        x += vx;
        y += vy;
        if (x < left || x > right) {
            vx = -vx;
            x = (x < left) ? left : right;
        }
        // Stop the guide at the first peg it would touch, like Peggle's guide.
        // Portals don't stop it — the ball goes through them, not off them.
        for (int i = 0; i < s_p.peg_count && *hit < 0; i++) {
            const Peg *p = &s_p.pegs[i];
            if (!p->active || p->type == PEG_PORTAL) continue;
            int32_t dx4 = (x - FP(p->x)) >> 4;
            int32_t dy4 = (y - FP(p->y)) >> 4;
            if ((dx4 * dx4 + dy4 * dy4) <= rsum4 * rsum4) {
                *hit = i;
            }
        }
        if (*hit >= 0) break;
        if (step % 3 == 2 && out) {
            out[n++] = GPoint(TO_PX(x), TO_PX(y));
        }
    }
    return n;
}

int play_preview(GPoint *out, int max) {
    int hit;
    return preview(s_p.aim, out, max, &hit);
}

#if defined(PEGGLE_AUTOPLAY)
int play_aim_peg(int32_t aim) {
    int hit;
    preview(aim, NULL, 30, &hit);
    return hit;
}

int32_t play_aim_step(void) {
    return AIM_STEP;
}
#endif
