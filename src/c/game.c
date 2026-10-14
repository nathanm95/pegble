#include <stdlib.h>
#include "game.h"
#include "levels.h"
#include "theme.h"
#include "effects.h"
#include "abilities.h"

// All positions/velocities are Q24.8 fixed point — aplite/diorite have no FPU.
#define FP_SHIFT 8
#define FP(x) ((int32_t)(x) << FP_SHIFT)
#define TO_PX(x) ((int16_t)((x) >> FP_SHIFT))

#define TICK_MS 33
#define STARTING_BALLS 8
#define STARTING_BALLS_ENDLESS 6
#define ENDLESS_BOARD_BONUS 2500
#define MAX_ACTIVE_BALLS 3
#define RESTITUTION 218          // ~0.85 in Q8
#define RESOLVE_TICKS 18         // lit pegs flash ~600ms before vanishing
#define BALL_MAX_AGE 600         // safety: retire a ball after ~20s
#define AIM_LIMIT (TRIG_MAX_ANGLE * 75 / 360)
#define AIM_STEP (TRIG_MAX_ANGLE / 240)

#define TRAIL_LEN 4              // ball comet tail, in past positions
#define STAR_COUNT 14            // background speckle
#define FIREBALL_TICKS 70        // ~2.3s of burning through pegs
#define MAGNET_TICKS 90
#define BLAST_RADIUS_PEGS 8      // blast reach, in peg radii

typedef enum { PEG_BLUE, PEG_TARGET, PEG_POWER } PegType;

typedef enum {
    STATE_AIMING,
    STATE_FLYING,
    STATE_RESOLVING,
    STATE_WON,
    STATE_LOST
} GameState;

typedef struct {
    int16_t x, y;    // center, px
    uint8_t type;
    uint8_t ability; // only meaningful for PEG_POWER
    bool active;
    bool lit;
} Peg;

typedef struct {
    int32_t x, y, vx, vy; // Q24.8
    int16_t slow_ticks;
    int16_t age;
    int16_t fire_ticks;   // burning through pegs instead of bouncing
    int16_t magnet_ticks; // being pulled towards the bucket
    bool spooky;          // one free return from the bottom of the board
    bool active;
    int16_t trail_x[TRAIL_LEN], trail_y[TRAIL_LEN];
    uint8_t trail_n;
} Ball;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static int s_level;
static bool s_endless;
static Peg s_pegs[MAX_PEGS];
static int s_peg_count;
static int s_bonus_idx;
static Ball s_balls[MAX_ACTIVE_BALLS];
static int s_balls_left;
static int s_score;
static int s_target_total;
static int s_target_cleared;
static int s_boards;          // endless: boards cleared this run
static int32_t s_aim;
static GameState s_state;
static int s_resolve_ticks;
static int s_tick;
static char s_msg[20];
static int s_msg_ticks;
static const Theme *s_theme;
static GPoint s_stars[STAR_COUNT];
static GPoint s_shake_applied;

// Layout, computed once per window load so one binary fits every screen.
static GRect s_field;
static GSize s_root_size;
static int s_hud_h;
static int s_peg_r, s_ball_r;
static int s_bucket_w, s_bucket_h, s_bucket_x;
static int32_t s_bucket_phase;
static GPoint s_launch;
static int32_t s_launch_speed, s_gravity, s_max_speed;

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

static void show_msg(const char *text) {
    strncpy(s_msg, text, sizeof(s_msg) - 1);
    s_msg[sizeof(s_msg) - 1] = '\0';
    s_msg_ticks = 30;
}

static int multiplier(void) {
    if (s_target_total == 0) {
        return 1;
    }
    int pct = s_target_cleared * 100 / s_target_total;
    if (pct >= 80) return 5;
    if (pct >= 60) return 3;
    if (pct >= 40) return 2;
    return 1;
}

static int targets_remaining(void) {
    int n = 0;
    for (int i = 0; i < s_peg_count; i++) {
        if (s_pegs[i].active && !s_pegs[i].lit && s_pegs[i].type == PEG_TARGET) {
            n++;
        }
    }
    return n;
}

// Endless gets harder the longer a run lasts. One place to read the ramp from.
static int endless_difficulty(void) {
    return s_endless ? s_boards : 0;
}

// The pink bonus peg roams: a fresh blue peg is promoted every turn.
static void pick_bonus_peg(void) {
    int candidates[MAX_PEGS];
    int n = 0;
    for (int i = 0; i < s_peg_count; i++) {
        if (s_pegs[i].active && !s_pegs[i].lit && s_pegs[i].type == PEG_BLUE) {
            candidates[n++] = i;
        }
    }
    s_bonus_idx = (n > 0) ? candidates[rand() % n] : -1;
}

static void compute_layout(void) {
    GRect bounds = layer_get_bounds(s_layer);
    s_root_size = bounds.size;
    s_hud_h = PBL_IF_ROUND_ELSE(24, 16);
    int side = PBL_IF_ROUND_ELSE(20, 2);
    s_field = GRect(bounds.origin.x + side, bounds.origin.y + s_hud_h,
                    bounds.size.w - side * 2,
                    bounds.size.h - s_hud_h - PBL_IF_ROUND_ELSE(10, 2));
    s_peg_r = (s_field.size.w >= 170) ? 5 : 4;
    s_ball_r = s_peg_r - 1;
    s_bucket_w = s_field.size.w / 4;
    s_bucket_h = s_peg_r * 2 + 2;
    s_launch = GPoint(s_field.origin.x + s_field.size.w / 2, s_field.origin.y + 3);
    // Physics scale with playfield height so the game feels the same everywhere.
    s_launch_speed = FP(s_field.size.h) / 40;
    s_gravity = FP(s_field.size.h) / 1100;
    if (s_gravity < 20) {
        s_gravity = 20;
    }
    s_max_speed = s_launch_speed * 8 / 5;
}

static void scatter_stars(void) {
    for (int i = 0; i < STAR_COUNT; i++) {
        s_stars[i] = GPoint(s_field.origin.x + 2 + rand() % (s_field.size.w - 4),
                            s_field.origin.y + 2 + rand() % (s_field.size.h - 4));
    }
}

static void build_board(void) {
    PegPos pos[MAX_PEGS];
    int difficulty = endless_difficulty();
    if (s_endless) {
        srand(time(NULL) ^ (s_tick << 8));
        s_peg_count = level_build_endless(pos, difficulty);
        // Each endless board takes the next palette, so a long run keeps moving.
        s_theme = theme_get(s_boards + 1);
    } else {
        // Deterministic per level so layouts, peg types and abilities are stable.
        srand(s_level * 7919 + 13);
        s_peg_count = level_build(s_level, pos);
        s_theme = theme_get(s_level);
    }
    if (s_window) {
        window_set_background_color(s_window, s_theme->bg);
    }

    int margin = s_peg_r + 3;
    int top = s_field.origin.y + s_field.size.h * 16 / 100;
    int bottom = s_field.origin.y + s_field.size.h * 80 / 100;
    for (int i = 0; i < s_peg_count; i++) {
        s_pegs[i].x = s_field.origin.x + margin
                      + pos[i].nx * (s_field.size.w - 2 * margin) / 1000;
        s_pegs[i].y = top + pos[i].ny * (bottom - top) / 1000;
        s_pegs[i].active = true;
        s_pegs[i].lit = false;
        s_pegs[i].type = PEG_BLUE;
        s_pegs[i].ability = ABILITY_MULTIBALL;
    }

    // Shuffle peg indices, then deal targets and power pegs off the top.
    int idx[MAX_PEGS];
    for (int i = 0; i < s_peg_count; i++) {
        idx[i] = i;
    }
    for (int i = s_peg_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
    }

    // Later endless boards ask for more of the board to be cleared...
    int share = 4 - (difficulty >= 4) - (difficulty >= 9); // 1/4, then 1/3, then 1/2
    int targets = s_peg_count / share;
    if (targets < 5) targets = 5;
    if (targets > s_peg_count - 3) targets = s_peg_count - 3;
    for (int i = 0; i < targets; i++) {
        s_pegs[idx[i]].type = PEG_TARGET;
    }

    // ...and hand out fewer power pegs to do it with.
    int powers = 3;
    if (difficulty >= 4) powers = 2;
    if (difficulty >= 8) powers = 1;

    // Deal distinct abilities where there are enough to go round: shuffle the
    // five and take from the top, so a board is never three of the same thing.
    uint8_t deck[ABILITY_COUNT];
    for (int i = 0; i < ABILITY_COUNT; i++) {
        deck[i] = i;
    }
    for (int i = ABILITY_COUNT - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        uint8_t t = deck[i];
        deck[i] = deck[j];
        deck[j] = t;
    }
    for (int i = 0; i < powers && targets + i < s_peg_count; i++) {
        Peg *p = &s_pegs[idx[targets + i]];
        p->type = PEG_POWER;
        p->ability = deck[i % ABILITY_COUNT];
    }

    s_target_total = targets;
    s_target_cleared = 0;
    scatter_stars();
    pick_bonus_peg();
}

static void reset_ball(Ball *b) {
    b->active = false;
    b->fire_ticks = 0;
    b->magnet_ticks = 0;
    b->spooky = false;
    b->trail_n = 0;
    b->age = 0;
    b->slow_ticks = 0;
}

static void start_level(void) {
    s_boards = 0;
    build_board();
    s_balls_left = s_endless ? STARTING_BALLS_ENDLESS : STARTING_BALLS;
    s_score = 0;
    s_aim = 0;
    s_state = STATE_AIMING;
    s_msg_ticks = 0;
    s_bucket_phase = 0;
    effects_reset();
    for (int i = 0; i < MAX_ACTIVE_BALLS; i++) {
        reset_ball(&s_balls[i]);
    }
}

// ---- abilities ----

static void spawn_multiball(const Ball *src) {
    for (int i = 0; i < MAX_ACTIVE_BALLS; i++) {
        if (!s_balls[i].active) {
            s_balls[i] = *src;
            s_balls[i].vx = -src->vx;
            s_balls[i].vy = -abs(src->vy) * 3 / 4; // pop the clone upward
            s_balls[i].age = 0;
            s_balls[i].slow_ticks = 0;
            s_balls[i].trail_n = 0;
            return;
        }
    }
}

// Lights a peg for score and sparks without firing its ability. Used by the
// blast, which would otherwise chain into other power pegs and recurse.
static void light_peg_scored(int i) {
    Peg *p = &s_pegs[i];
    if (!p->active || p->lit) {
        return;
    }
    p->lit = true;
    int base = 10;
    if (p->type == PEG_TARGET) {
        s_target_cleared++;
        base = 100;
    } else if (p->type == PEG_POWER) {
        base = 50;
    }
    if (i == s_bonus_idx) {
        base = 500;
    }
    int points = base * multiplier();
    s_score += points;
    effects_burst(GPoint(p->x, p->y), s_theme->peg_lit, 3, FP(2));
}

static void trigger_blast(int origin, Ball *b) {
    (void)b;
    Peg *src = &s_pegs[origin];
    int radius = s_peg_r * BLAST_RADIUS_PEGS;
    int r2 = radius * radius;
    for (int i = 0; i < s_peg_count; i++) {
        if (i == origin || !s_pegs[i].active || s_pegs[i].lit) {
            continue;
        }
        int dx = s_pegs[i].x - src->x;
        int dy = s_pegs[i].y - src->y;
        if (dx * dx + dy * dy <= r2) {
            light_peg_scored(i);
        }
    }
    effects_burst(GPoint(src->x, src->y), ability_color(ABILITY_BLAST, true), 14, FP(4));
    effects_flash(PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack), 2);
    effects_shake(8);
}

static void trigger_ability(Ability a, int peg_idx, Ball *b) {
    switch (a) {
        case ABILITY_MULTIBALL:
            spawn_multiball(b);
            break;
        case ABILITY_BLAST:
            trigger_blast(peg_idx, b);
            break;
        case ABILITY_FIREBALL:
            b->fire_ticks = FIREBALL_TICKS;
            effects_burst(GPoint(TO_PX(b->x), TO_PX(b->y)),
                          ability_color(ABILITY_FIREBALL, true), 8, FP(3));
            break;
        case ABILITY_SPOOKY:
            b->spooky = true;
            break;
        case ABILITY_MAGNET:
            b->magnet_ticks = MAGNET_TICKS;
            break;
        default:
            break;
    }
    show_msg(ability_name(a));
    effects_shake(a == ABILITY_BLAST ? 8 : 3);
    vibes_short_pulse();
}

static void hit_peg(int i, Ball *b) {
    Peg *p = &s_pegs[i];
    if (p->lit) {
        return;
    }
    p->lit = true;
    int base = 10;
    if (p->type == PEG_TARGET) {
        s_target_cleared++;
        base = 100;
    } else if (p->type == PEG_POWER) {
        base = 50;
    }
    if (i == s_bonus_idx) {
        base = 500;
    }
    int points = base * multiplier();
    s_score += points;

    GColor spark = (i == s_bonus_idx) ? PEG_COLOR_BONUS_LIT
                   : (p->type == PEG_TARGET) ? s_theme->target_lit
                   : (p->type == PEG_POWER) ? ability_color((Ability)p->ability, true)
                   : s_theme->peg_lit;
    effects_burst(GPoint(p->x, p->y), spark, (p->type == PEG_BLUE) ? 4 : 7, FP(3));
    // Only the shots worth noticing get a number floating off them.
    if (points >= 100) {
        effects_popup(GPoint(p->x, p->y), points, s_theme->text);
    }

    if (p->type == PEG_POWER) {
        trigger_ability((Ability)p->ability, i, b);
    }
}

// ---- physics ----

static void collide_pegs(Ball *b) {
    int32_t rsum = FP(s_ball_r + s_peg_r);
    for (int i = 0; i < s_peg_count; i++) {
        Peg *p = &s_pegs[i];
        if (!p->active) {
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
        // A burning ball passes straight through, lighting pegs as it goes.
        if (b->fire_ticks > 0) {
            hit_peg(i, b);
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
            b->vx = b->vx * RESTITUTION / 256;
            b->vy = b->vy * RESTITUTION / 256;
        }
        int32_t pen = rsum - (dist4 << 4);
        if (pen > 0) {
            b->x += (nx * pen) >> FP_SHIFT;
            b->y += (ny * pen) >> FP_SHIFT;
        }
        hit_peg(i, b);
    }
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

static void step_ball(Ball *b) {
    push_trail(b);
    if (b->fire_ticks > 0) {
        b->fire_ticks--;
    }

    b->vy += s_gravity;
    // The magnet leans the ball towards the bucket on the way down.
    if (b->magnet_ticks > 0) {
        b->magnet_ticks--;
        if (b->vy > 0 && TO_PX(b->y) > s_field.origin.y + s_field.size.h / 2) {
            b->vx += (s_bucket_x > TO_PX(b->x)) ? s_gravity : -s_gravity;
        }
    }
    if (b->vy > s_max_speed) b->vy = s_max_speed;
    if (b->vx > s_max_speed) b->vx = s_max_speed;
    if (b->vx < -s_max_speed) b->vx = -s_max_speed;
    b->x += b->vx;
    b->y += b->vy;

    int32_t left = FP(s_field.origin.x + s_ball_r);
    int32_t right = FP(s_field.origin.x + s_field.size.w - s_ball_r);
    int32_t top = FP(s_field.origin.y + s_ball_r);
    if (b->x < left) {
        b->x = left;
        b->vx = -b->vx * RESTITUTION / 256;
    } else if (b->x > right) {
        b->x = right;
        b->vx = -b->vx * RESTITUTION / 256;
    }
    if (b->y < top && b->vy < 0) {
        b->y = top;
        b->vy = -b->vy * RESTITUTION / 256;
    }

    collide_pegs(b);

    // Bucket catch: refund the ball if it drops into the moving opening.
    int bucket_top = s_field.origin.y + s_field.size.h - s_bucket_h;
    if (b->vy > 0 && TO_PX(b->y) + s_ball_r >= bucket_top) {
        int bx = TO_PX(b->x);
        int half = s_bucket_w / 2 - s_ball_r;
        if (bx >= s_bucket_x - half && bx <= s_bucket_x + half) {
            s_balls_left++;
            show_msg("FREE BALL!");
            effects_burst(GPoint(s_bucket_x, bucket_top), s_theme->accent, 10, FP(3));
            vibes_short_pulse();
            b->active = false;
            return;
        }
    }
    if (TO_PX(b->y) - s_ball_r > s_field.origin.y + s_field.size.h) {
        // Spooky: one free return from the bottom, straight back in at the top.
        if (b->spooky) {
            b->spooky = false;
            b->y = FP(s_field.origin.y + s_ball_r);
            b->vy = s_launch_speed / 2;
            b->trail_n = 0;
            show_msg("SPOOKY RETURN");
            effects_burst(GPoint(TO_PX(b->x), s_field.origin.y + s_ball_r),
                          ability_color(ABILITY_SPOOKY, true), 8, FP(3));
            vibes_short_pulse();
            return;
        }
        b->active = false;
        return;
    }

    // Anti-stuck: a ball resting on a peg gets a sideways kick.
    if (abs(b->vx) + abs(b->vy) < FP(1)) {
        if (++b->slow_ticks > 20) {
            b->vx += (rand() % 2) ? FP(1) : -FP(1);
            b->slow_ticks = 0;
        }
    } else {
        b->slow_ticks = 0;
    }
    if (++b->age > BALL_MAX_AGE) {
        b->active = false;
    }
}

static void save_endless_records(void) {
    int best = persist_read_int(PERSIST_KEY_BEST_BASE + LEVEL_COUNT);
    if (s_score > best) {
        persist_write_int(PERSIST_KEY_BEST_BASE + LEVEL_COUNT, s_score);
    }
    if (s_boards > persist_read_int(PERSIST_KEY_ENDLESS_BOARDS)) {
        persist_write_int(PERSIST_KEY_ENDLESS_BOARDS, s_boards);
    }
}

static void finish_turn(void) {
    for (int i = 0; i < s_peg_count; i++) {
        if (s_pegs[i].lit) {
            s_pegs[i].active = false;
            s_pegs[i].lit = false;
        }
    }
    if (targets_remaining() == 0) {
        if (s_endless) {
            // Endless never ends on a clear: fresh board, bonus, keep rolling.
            s_boards++;
            s_score += ENDLESS_BOARD_BONUS;
            // The refund shrinks as the run goes on, so later boards have to pay
            // for themselves out of bucket catches.
            int refund = (s_boards >= 8) ? 1 : 2;
            s_balls_left += refund;
            char buf[32];
            snprintf(buf, sizeof(buf), "BOARD %d! +%d", s_boards, refund);
            build_board();
            show_msg(buf);
            effects_spray(GRect(s_field.origin.x, s_field.origin.y + s_field.size.h / 3,
                                s_field.size.w, 10), s_theme->accent, 16);
            effects_flash(PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack), 2);
            vibes_double_pulse();
            save_endless_records();
            s_state = STATE_AIMING;
            pick_bonus_peg();
            return;
        }
        s_score += s_balls_left * 1000;
        s_state = STATE_WON;
        vibes_double_pulse();
        effects_spray(GRect(s_field.origin.x, s_field.origin.y + 4, s_field.size.w, 10),
                      s_theme->accent, 20);
        int best = persist_read_int(PERSIST_KEY_BEST_BASE + s_level);
        if (s_score > best) {
            persist_write_int(PERSIST_KEY_BEST_BASE + s_level, s_score);
        }
        if (s_level + 1 < LEVEL_COUNT
            && persist_read_int(PERSIST_KEY_UNLOCKED) < s_level + 1) {
            persist_write_int(PERSIST_KEY_UNLOCKED, s_level + 1);
        }
    } else if (s_balls_left == 0) {
        s_state = STATE_LOST;
        vibes_long_pulse();
        effects_shake(10);
        if (s_endless) {
            save_endless_records();
        }
    } else {
        s_state = STATE_AIMING;
        pick_bonus_peg();
    }
}

static void game_tick(void *context) {
    s_timer = app_timer_register(TICK_MS, game_tick, NULL);
    s_tick++;
    if (s_msg_ticks > 0) {
        s_msg_ticks--;
    }
    effects_update();

    // The bucket patrols the bottom edge at all times, faster as endless ramps up.
    int32_t bucket_speed = TRIG_MAX_ANGLE / 150 + endless_difficulty() * (TRIG_MAX_ANGLE / 1800);
    s_bucket_phase += bucket_speed;
    // Round screens: keep the bucket's travel inside the visible circle.
    int amp = (s_field.size.w - s_bucket_w) / 2 - PBL_IF_ROUND_ELSE(26, 2);
    s_bucket_x = s_field.origin.x + s_field.size.w / 2
                 + (int)((int64_t)amp * sin_lookup(s_bucket_phase) / TRIG_MAX_RATIO);

    if (s_state == STATE_FLYING) {
        bool any = false;
        for (int i = 0; i < MAX_ACTIVE_BALLS; i++) {
            if (s_balls[i].active) {
                step_ball(&s_balls[i]);
                any = any || s_balls[i].active;
            }
        }
        if (!any) {
            s_state = STATE_RESOLVING;
            s_resolve_ticks = RESOLVE_TICKS;
        }
    } else if (s_state == STATE_RESOLVING) {
        if (--s_resolve_ticks <= 0) {
            finish_turn();
        }
    }

    // Screen shake: nudge the whole game layer rather than every draw call.
    GPoint shake = effects_shake_offset();
    if (shake.x != s_shake_applied.x || shake.y != s_shake_applied.y) {
        layer_set_frame(s_layer, GRect(shake.x, shake.y, s_root_size.w, s_root_size.h));
        s_shake_applied = shake;
    }
    layer_mark_dirty(s_layer);
}

static void fire_ball(void) {
    if (s_balls_left <= 0) {
        return;
    }
    s_balls_left--;
    Ball *b = &s_balls[0];
    reset_ball(b);
    b->x = FP(s_launch.x);
    b->y = FP(s_launch.y);
    b->vx = (int32_t)((int64_t)sin_lookup(s_aim) * s_launch_speed / TRIG_MAX_RATIO);
    b->vy = (int32_t)((int64_t)cos_lookup(s_aim) * s_launch_speed / TRIG_MAX_RATIO);
    b->active = true;
    s_state = STATE_FLYING;
}

// ---- rendering ----

#if defined(PBL_COLOR)
static GColor peg_color(const Peg *p, int idx) {
    if (idx == s_bonus_idx) {
        return p->lit ? PEG_COLOR_BONUS_LIT : PEG_COLOR_BONUS;
    }
    switch (p->type) {
        case PEG_TARGET: return p->lit ? s_theme->target_lit : s_theme->target;
        case PEG_POWER:  return ability_color((Ability)p->ability, p->lit);
        default:         return p->lit ? s_theme->peg_lit : s_theme->peg;
    }
}
#endif

static void draw_peg(GContext *ctx, const Peg *p, int idx) {
    GPoint c = GPoint(p->x, p->y);
#if defined(PBL_COLOR)
    graphics_context_set_fill_color(ctx, peg_color(p, idx));
    graphics_fill_circle(ctx, c, s_peg_r);
    // A one-pixel highlight up and left turns a flat disc into a bead.
    graphics_context_set_fill_color(ctx, (p->type == PEG_TARGET) ? s_theme->target_hi
                                                                 : s_theme->peg_hi);
    graphics_fill_circle(ctx, GPoint(c.x - s_peg_r / 3, c.y - s_peg_r / 3), 1);
    if (p->type == PEG_POWER) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_context_set_stroke_color(ctx, GColorWhite);
        ability_draw_mark(ctx, c, s_peg_r, (Ability)p->ability);
    }
    if (idx == s_bonus_idx) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, c, 1);
    }
    if (p->lit) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_draw_circle(ctx, c, s_peg_r);
    }
#else
    // B&W: shape encodes type — target filled, blue hollow, power hollow with
    // the ability's mark inside, bonus filled with a white core. Lit pegs gain
    // an outer ring.
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorBlack);
    if (idx == s_bonus_idx) {
        graphics_fill_circle(ctx, c, s_peg_r);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, c, s_peg_r - 2);
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_circle(ctx, c, 1);
    } else if (p->type == PEG_TARGET) {
        graphics_fill_circle(ctx, c, s_peg_r);
    } else if (p->type == PEG_POWER) {
        graphics_draw_circle(ctx, c, s_peg_r);
        ability_draw_mark(ctx, c, s_peg_r, (Ability)p->ability);
    } else {
        graphics_draw_circle(ctx, c, s_peg_r);
    }
    if (p->lit) {
        graphics_draw_circle(ctx, c, s_peg_r + 2);
    }
#endif
}

static void draw_preview(GContext *ctx) {
    int32_t x = FP(s_launch.x), y = FP(s_launch.y);
    int32_t vx = (int32_t)((int64_t)sin_lookup(s_aim) * s_launch_speed / TRIG_MAX_RATIO);
    int32_t vy = (int32_t)((int64_t)cos_lookup(s_aim) * s_launch_speed / TRIG_MAX_RATIO);
    int32_t left = FP(s_field.origin.x + s_ball_r);
    int32_t right = FP(s_field.origin.x + s_field.size.w - s_ball_r);
    int32_t rsum4 = FP(s_ball_r + s_peg_r) >> 4;

    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
    for (int step = 0; step < 30; step++) {
        vy += s_gravity;
        x += vx;
        y += vy;
        if (x < left || x > right) {
            vx = -vx;
            x = (x < left) ? left : right;
        }
        // Stop the guide at the first peg it would touch, like Peggle's guide.
        bool blocked = false;
        for (int i = 0; i < s_peg_count && !blocked; i++) {
            if (!s_pegs[i].active) continue;
            int32_t dx4 = (x - FP(s_pegs[i].x)) >> 4;
            int32_t dy4 = (y - FP(s_pegs[i].y)) >> 4;
            blocked = (dx4 * dx4 + dy4 * dy4) <= rsum4 * rsum4;
        }
        if (blocked) break;
        if (step % 3 == 2) {
            // Dots thin out with distance, so the near part of the arc reads first.
            graphics_fill_circle(ctx, GPoint(TO_PX(x), TO_PX(y)), (step < 15) ? 2 : 1);
        }
    }
}

static void draw_ball(GContext *ctx, const Ball *b) {
    bool burning = b->fire_ticks > 0;
    GColor body = burning ? PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack)
                          : PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
    // Comet tail: older positions are smaller.
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(
        burning ? s_theme->accent : s_theme->peg_hi, GColorBlack));
    for (int i = 1; i < b->trail_n; i++) {
        int r = s_ball_r - i;
        if (r < 1) {
            break;
        }
        graphics_fill_circle(ctx, GPoint(b->trail_x[i], b->trail_y[i]), r);
    }
    GPoint c = GPoint(TO_PX(b->x), TO_PX(b->y));
    graphics_context_set_fill_color(ctx, body);
    graphics_fill_circle(ctx, c, s_ball_r);
    if (b->spooky) {
        graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
        graphics_draw_circle(ctx, c, s_ball_r + 2);
    }
#if defined(PBL_COLOR)
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(c.x - 1, c.y - 1), 1);
#endif
}

static void draw_bucket(GContext *ctx) {
    int by = s_field.origin.y + s_field.size.h - s_bucket_h;
    int x0 = s_bucket_x - s_bucket_w / 2;
    GColor accent = PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack);
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_rect(ctx, GRect(x0, by, 2, s_bucket_h), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(x0 + s_bucket_w - 2, by, 2, s_bucket_h), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(x0, by + s_bucket_h - 2, s_bucket_w, 2), 0, GCornerNone);
#if defined(PBL_COLOR)
    // A lighter mouth so the opening reads as a target, not a gap in the wall.
    graphics_context_set_stroke_color(ctx, s_theme->target_lit);
    graphics_draw_line(ctx, GPoint(x0 + 2, by), GPoint(x0 + s_bucket_w - 3, by));
#endif
}

// Round screens have very little width up at the bezel, so long scores are
// abbreviated rather than truncated to "...".
static void format_score(char *buf, size_t n, int score) {
    if (score >= 10000) {
        snprintf(buf, n, "%dk", score / 1000);
    } else {
        snprintf(buf, n, "%d", score);
    }
}

static void draw_hud(GContext *ctx, GRect bounds) {
    char buf[32];
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    int y = PBL_IF_ROUND_ELSE(4, -2);
    int cx = bounds.size.w / 2;
    // On round screens keep the HUD inside the visible band near the bezel top.
    int x0 = PBL_IF_ROUND_ELSE(cx - 46, s_field.origin.x + 2);
    int x1 = PBL_IF_ROUND_ELSE(cx + 46, s_field.origin.x + s_field.size.w - 2);
    GColor fg = PBL_IF_COLOR_ELSE(s_theme->text, GColorBlack);
    graphics_context_set_text_color(ctx, fg);
    graphics_context_set_fill_color(ctx, fg);

    // Ball stock on the left: one pip plus a count.
    graphics_fill_circle(ctx, GPoint(x0 + 3, PBL_IF_ROUND_ELSE(13, 8)), 3);
    snprintf(buf, sizeof(buf), "x%d", s_balls_left);
    graphics_draw_text(ctx, buf, font, GRect(x0 + 9, y, 24, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Board number (endless) and targets left in the middle, score on the right.
    int mid_w = PBL_IF_ROUND_ELSE(34, 64);
    if (s_endless) {
        snprintf(buf, sizeof(buf), PBL_IF_ROUND_ELSE("B%d-%d", "B%d - %d left"),
                 s_boards + 1, targets_remaining());
    } else {
        snprintf(buf, sizeof(buf), PBL_IF_ROUND_ELSE("%d", "%d left"),
                 targets_remaining());
    }
    graphics_draw_text(ctx, buf, font, GRect(cx - mid_w / 2, y, mid_w, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    format_score(buf, sizeof(buf), s_score);
    int score_x = cx + mid_w / 2 + 1;
    graphics_draw_text(ctx, buf, font, GRect(score_x, y, x1 - score_x, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

    // Progress bar along the top of the playfield: how much of the board is done.
    int done = s_target_total ? s_target_cleared * s_field.size.w / s_target_total : 0;
    if (done > s_field.size.w) {
        done = s_field.size.w;
    }
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->wall, GColorBlack));
    graphics_fill_rect(ctx, GRect(s_field.origin.x, s_field.origin.y - 2,
                                  s_field.size.w, 1), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
    graphics_fill_rect(ctx, GRect(s_field.origin.x, s_field.origin.y - 3, done, 2),
                       0, GCornerNone);
}

static void draw_overlay(GContext *ctx, GRect bounds) {
    int w = 134, h = 92;
    if (w > bounds.size.w - 8) {
        w = bounds.size.w - 8;
    }
    GRect box = GRect((bounds.size.w - w) / 2, (bounds.size.h - h) / 2, w, h);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->bg, GColorWhite));
    graphics_fill_rect(ctx, box, 6, GCornersAll);
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
    graphics_draw_round_rect(ctx, box, 6);

    GColor fg = PBL_IF_COLOR_ELSE(s_theme->text, GColorBlack);
    GColor accent = PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack);
    char buf[32];
    const char *title = (s_state == STATE_WON) ? "LEVEL CLEAR!"
                        : s_endless ? "GAME OVER" : "OUT OF BALLS";

    graphics_context_set_text_color(ctx, accent);
    graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(box.origin.x, box.origin.y + 4, box.size.w, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    snprintf(buf, sizeof(buf), "%d", s_score);
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(box.origin.x, box.origin.y + 24, box.size.w, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    if (s_endless) {
        snprintf(buf, sizeof(buf), "%d boards · best %d", s_boards,
                 (int)persist_read_int(PERSIST_KEY_ENDLESS_BOARDS));
    } else {
        snprintf(buf, sizeof(buf), "best %d",
                 (int)persist_read_int(PERSIST_KEY_BEST_BASE + s_level));
    }
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(box.origin.x, box.origin.y + 50, box.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    graphics_context_set_text_color(ctx, accent);
    graphics_draw_text(ctx, (s_state == STATE_WON) ? "SELECT: menu" : "SELECT: retry",
                       fonts_get_system_font(FONT_KEY_GOTHIC_14),
                       GRect(box.origin.x, box.origin.y + 68, box.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void layer_update(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->bg, GColorWhite));
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

#if defined(PBL_COLOR)
    // Faint speckle so the table has some depth behind the pegs.
    graphics_context_set_fill_color(ctx, s_theme->star);
    for (int i = 0; i < STAR_COUNT; i++) {
        graphics_fill_rect(ctx, GRect(s_stars[i].x, s_stars[i].y, 1, 1), 0, GCornerNone);
    }
#endif

    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(s_theme->wall, GColorBlack));
    graphics_draw_line(ctx, s_field.origin,
                       GPoint(s_field.origin.x, s_field.origin.y + s_field.size.h));
    graphics_draw_line(ctx, GPoint(s_field.origin.x + s_field.size.w, s_field.origin.y),
                       GPoint(s_field.origin.x + s_field.size.w,
                              s_field.origin.y + s_field.size.h));

    for (int i = 0; i < s_peg_count; i++) {
        if (!s_pegs[i].active) {
            continue;
        }
        // Lit pegs blink during the resolve flash.
        if (s_state == STATE_RESOLVING && s_pegs[i].lit && (s_tick & 2)) {
            continue;
        }
        draw_peg(ctx, &s_pegs[i], i);
    }

    draw_bucket(ctx);

    if (s_state == STATE_AIMING) {
        draw_preview(ctx);
    }

    // Launcher: a barrel plus a hub, so the pivot is obvious.
    int lx = s_launch.x + (int)((int64_t)(s_peg_r * 3) * sin_lookup(s_aim) / TRIG_MAX_RATIO);
    int ly = s_launch.y + (int)((int64_t)(s_peg_r * 3) * cos_lookup(s_aim) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(s_theme->text, GColorBlack));
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, s_launch, GPoint(lx, ly));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
    graphics_fill_circle(ctx, s_launch, 2);

    for (int i = 0; i < MAX_ACTIVE_BALLS; i++) {
        if (s_balls[i].active) {
            draw_ball(ctx, &s_balls[i]);
        }
    }

    effects_draw(ctx);

    GColor flash;
    if (effects_flash_color(&flash)) {
        graphics_context_set_fill_color(ctx, flash);
        graphics_fill_rect(ctx, s_field, 0, GCornerNone);
    }

    draw_hud(ctx, bounds);

    if (s_msg_ticks > 0) {
        graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
        graphics_draw_text(ctx, s_msg, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                           GRect(0, bounds.size.h / 2 - 30, bounds.size.w, 24),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    if (s_state == STATE_WON || s_state == STATE_LOST) {
        draw_overlay(ctx, bounds);
    }
}

// ---- input ----

static void select_click(ClickRecognizerRef rec, void *ctx) {
    if (s_state == STATE_AIMING) {
        fire_ball();
    } else if (s_state == STATE_WON) {
        window_stack_pop(true);
    } else if (s_state == STATE_LOST) {
        start_level();
    }
}

static void aim_click(ClickRecognizerRef rec, void *ctx) {
    if (s_state != STATE_AIMING && s_state != STATE_FLYING) {
        return;
    }
    s_aim += (click_recognizer_get_button_id(rec) == BUTTON_ID_UP) ? -AIM_STEP : AIM_STEP;
    if (s_aim > AIM_LIMIT) s_aim = AIM_LIMIT;
    if (s_aim < -AIM_LIMIT) s_aim = -AIM_LIMIT;
    layer_mark_dirty(s_layer);
}

static void click_config(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_single_repeating_click_subscribe(BUTTON_ID_UP, 40, aim_click);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 40, aim_click);
}

// Touch is only live on watches with a touchscreen (touch_service_is_enabled()
// is false everywhere else), so this is a no-op extra on button-only Pebbles.
static bool s_touch_active;

// A gesture that started while the interaction session was inactive (an unarmed
// contact on the idle watchface) must not drive the game, or the contact that
// wakes the watch also fires a ball. The flag is latched at Touchdown and
// carried across the gesture, so checking it there is enough. Guarded because
// the field only exists in SDKs that also have app_touch_navigation_enable().
static bool touch_is_navigational(const TouchEvent *event) {
#if defined(_PBL_API_EXISTS_app_touch_navigation_enable)
    return !event->non_navigational;
#else
    return true;
#endif
}

static void set_aim_from_touch_x(int16_t x) {
    int half = s_field.size.w / 2;
    int dx = x - s_launch.x;
    if (dx > half) dx = half;
    if (dx < -half) dx = -half;
    s_aim = (int32_t)dx * AIM_LIMIT / half;
    layer_mark_dirty(s_layer);
}

static void touch_handler(const TouchEvent *event, void *context) {
    switch (event->type) {
        case TouchEvent_Touchdown:
            s_touch_active = touch_is_navigational(event)
                              && (s_state == STATE_AIMING || s_state == STATE_WON
                                   || s_state == STATE_LOST);
            if (s_state == STATE_AIMING) {
                set_aim_from_touch_x(event->x);
            }
            break;
        case TouchEvent_PositionUpdate:
            if (s_touch_active && s_state == STATE_AIMING) {
                set_aim_from_touch_x(event->x);
            }
            break;
        case TouchEvent_Liftoff:
            if (!s_touch_active) {
                break;
            }
            s_touch_active = false;
            if (s_state == STATE_AIMING) {
                fire_ball();
            } else if (s_state == STATE_WON) {
                window_stack_pop(true);
            } else if (s_state == STATE_LOST) {
                start_level();
            }
            break;
    }
}

// ---- window lifecycle ----

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_layer = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_layer, layer_update);
    layer_add_child(root, s_layer);
    s_shake_applied = GPoint(0, 0);
    compute_layout();
    start_level();
}

static void window_appear(Window *window) {
    if (!s_timer) {
        s_timer = app_timer_register(TICK_MS, game_tick, NULL);
    }
    if (touch_service_is_enabled()) {
        touch_service_subscribe(touch_handler, NULL);
    }
}

static void window_disappear(Window *window) {
    if (s_timer) {
        app_timer_cancel(s_timer);
        s_timer = NULL;
    }
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
    s_touch_active = false;
}

static void window_unload(Window *window) {
    layer_destroy(s_layer);
    s_layer = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

static void push_game_window(void) {
    if (!s_window) {
        s_window = window_create();
        window_set_window_handlers(s_window, (WindowHandlers) {
            .load = window_load,
            .appear = window_appear,
            .disappear = window_disappear,
            .unload = window_unload
        });
        window_set_click_config_provider(s_window, click_config);
    }
    window_stack_push(s_window, true);
}

void game_window_push(int level_index) {
    s_level = level_index;
    s_endless = false;
    s_theme = theme_get(level_index);
    push_game_window();
}

void game_window_push_endless(void) {
    s_level = 0;
    s_endless = true;
    s_boards = 0;
    s_theme = theme_get(1);
    push_game_window();
}
