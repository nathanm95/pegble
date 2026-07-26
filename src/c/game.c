#include <stdlib.h>
#include "game.h"
#include "levels.h"

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

typedef enum { PEG_BLUE, PEG_ORANGE, PEG_GREEN } PegType;

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
    bool active;
    bool lit;
} Peg;

typedef struct {
    int32_t x, y, vx, vy; // Q24.8
    int16_t slow_ticks;
    int16_t age;
    bool active;
} Ball;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static int s_level;
static bool s_endless;
static Peg s_pegs[MAX_PEGS];
static int s_peg_count;
static int s_purple_idx;
static Ball s_balls[MAX_ACTIVE_BALLS];
static int s_balls_left;
static int s_score;
static int s_orange_total;
static int s_orange_cleared;
static int32_t s_aim;
static GameState s_state;
static int s_resolve_ticks;
static int s_tick;
static char s_msg[20];
static int s_msg_ticks;

// Layout, computed once per window load so one binary fits every screen.
static GRect s_field;
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
    if (s_orange_total == 0) {
        return 1;
    }
    int pct = s_orange_cleared * 100 / s_orange_total;
    if (pct >= 80) return 5;
    if (pct >= 60) return 3;
    if (pct >= 40) return 2;
    return 1;
}

static int orange_remaining(void) {
    int n = 0;
    for (int i = 0; i < s_peg_count; i++) {
        if (s_pegs[i].active && !s_pegs[i].lit && s_pegs[i].type == PEG_ORANGE) {
            n++;
        }
    }
    return n;
}

// The purple bonus peg roams: a fresh blue peg is promoted every turn.
static void pick_purple(void) {
    int candidates[MAX_PEGS];
    int n = 0;
    for (int i = 0; i < s_peg_count; i++) {
        if (s_pegs[i].active && !s_pegs[i].lit && s_pegs[i].type == PEG_BLUE) {
            candidates[n++] = i;
        }
    }
    s_purple_idx = (n > 0) ? candidates[rand() % n] : -1;
}

static void compute_layout(void) {
    GRect bounds = layer_get_bounds(s_layer);
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

static void build_board(void) {
    PegPos pos[MAX_PEGS];
    if (s_endless) {
        srand(time(NULL) ^ (s_tick << 8));
        s_peg_count = level_build_endless(pos);
    } else {
        // Deterministic per level so layouts and peg types are stable.
        srand(s_level * 7919 + 13);
        s_peg_count = level_build(s_level, pos);
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
    }

    // Shuffle peg indices, then deal orange/green off the top.
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
    int orange = s_peg_count / 4;
    if (orange < 5) orange = 5;
    if (orange > s_peg_count - 3) orange = s_peg_count - 3;
    for (int i = 0; i < orange; i++) {
        s_pegs[idx[i]].type = PEG_ORANGE;
    }
    for (int i = orange; i < orange + 2 && i < s_peg_count; i++) {
        s_pegs[idx[i]].type = PEG_GREEN;
    }

    s_orange_total = orange;
    s_orange_cleared = 0;
    pick_purple();
}

static void start_level(void) {
    build_board();
    s_balls_left = s_endless ? STARTING_BALLS_ENDLESS : STARTING_BALLS;
    s_score = 0;
    s_aim = 0;
    s_state = STATE_AIMING;
    s_msg_ticks = 0;
    s_bucket_phase = 0;
    for (int i = 0; i < MAX_ACTIVE_BALLS; i++) {
        s_balls[i].active = false;
    }
}

static void spawn_multiball(const Ball *src) {
    for (int i = 0; i < MAX_ACTIVE_BALLS; i++) {
        if (!s_balls[i].active) {
            s_balls[i] = *src;
            s_balls[i].vx = -src->vx;
            s_balls[i].vy = -abs(src->vy) * 3 / 4; // pop the clone upward
            s_balls[i].age = 0;
            s_balls[i].slow_ticks = 0;
            show_msg("MULTIBALL!");
            vibes_short_pulse();
            return;
        }
    }
}

static void hit_peg(int i, Ball *b) {
    Peg *p = &s_pegs[i];
    if (p->lit) {
        return;
    }
    p->lit = true;
    int base = 10;
    if (p->type == PEG_ORANGE) {
        s_orange_cleared++;
        base = 100;
    } else if (p->type == PEG_GREEN) {
        base = 50;
    }
    if (i == s_purple_idx) {
        base = 500;
    }
    s_score += base * multiplier();
    if (p->type == PEG_GREEN) {
        spawn_multiball(b);
    }
}

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

static void step_ball(Ball *b) {
    b->vy += s_gravity;
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
            vibes_short_pulse();
            b->active = false;
            return;
        }
    }
    if (TO_PX(b->y) - s_ball_r > s_field.origin.y + s_field.size.h) {
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

static void finish_turn(void) {
    for (int i = 0; i < s_peg_count; i++) {
        if (s_pegs[i].lit) {
            s_pegs[i].active = false;
            s_pegs[i].lit = false;
        }
    }
    if (orange_remaining() == 0) {
        if (s_endless) {
            // Endless never ends on a clear: fresh board, bonus, keep rolling.
            s_score += ENDLESS_BOARD_BONUS;
            s_balls_left += 2;
            build_board();
            show_msg("CLEAR! +2 BALLS");
            vibes_double_pulse();
            s_state = STATE_AIMING;
            return;
        }
        s_score += s_balls_left * 1000;
        s_state = STATE_WON;
        vibes_double_pulse();
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
        if (s_endless) {
            int best = persist_read_int(PERSIST_KEY_BEST_BASE + LEVEL_COUNT);
            if (s_score > best) {
                persist_write_int(PERSIST_KEY_BEST_BASE + LEVEL_COUNT, s_score);
            }
        }
    } else {
        s_state = STATE_AIMING;
        pick_purple();
    }
}

static void game_tick(void *context) {
    s_timer = app_timer_register(TICK_MS, game_tick, NULL);
    s_tick++;
    if (s_msg_ticks > 0) {
        s_msg_ticks--;
    }

    // The bucket patrols the bottom edge at all times.
    s_bucket_phase += TRIG_MAX_ANGLE / 150;
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
    layer_mark_dirty(s_layer);
}

static void fire_ball(void) {
    if (s_balls_left <= 0) {
        return;
    }
    s_balls_left--;
    Ball *b = &s_balls[0];
    b->x = FP(s_launch.x);
    b->y = FP(s_launch.y);
    b->vx = (int32_t)((int64_t)sin_lookup(s_aim) * s_launch_speed / TRIG_MAX_RATIO);
    b->vy = (int32_t)((int64_t)cos_lookup(s_aim) * s_launch_speed / TRIG_MAX_RATIO);
    b->active = true;
    b->age = 0;
    b->slow_ticks = 0;
    s_state = STATE_FLYING;
}

// ---- rendering ----

#if defined(PBL_COLOR)
static GColor peg_color(const Peg *p, int idx) {
    if (idx == s_purple_idx) {
        return p->lit ? GColorRichBrilliantLavender : GColorMagenta;
    }
    switch (p->type) {
        case PEG_ORANGE: return p->lit ? GColorChromeYellow : GColorOrange;
        case PEG_GREEN:  return p->lit ? GColorScreaminGreen : GColorIslamicGreen;
        default:         return p->lit ? GColorCyan : GColorBlueMoon;
    }
}
#endif

static void draw_peg(GContext *ctx, const Peg *p, int idx) {
    GPoint c = GPoint(p->x, p->y);
#if defined(PBL_COLOR)
    graphics_context_set_fill_color(ctx, peg_color(p, idx));
    graphics_fill_circle(ctx, c, s_peg_r);
    if (p->lit) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_draw_circle(ctx, c, s_peg_r);
    }
#else
    // B&W: shape encodes type — orange filled, blue hollow, green dotted,
    // purple filled with a white core. Lit pegs gain an outer ring.
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorBlack);
    if (idx == s_purple_idx) {
        graphics_fill_circle(ctx, c, s_peg_r);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, c, s_peg_r - 2);
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_circle(ctx, c, 1);
    } else if (p->type == PEG_ORANGE) {
        graphics_fill_circle(ctx, c, s_peg_r);
    } else if (p->type == PEG_GREEN) {
        graphics_draw_circle(ctx, c, s_peg_r);
        graphics_fill_circle(ctx, c, 1);
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

    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack));
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
            graphics_fill_circle(ctx, GPoint(TO_PX(x), TO_PX(y)), 1);
        }
    }
}

static void draw_hud(GContext *ctx, GRect bounds) {
    char buf[16];
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    int y = PBL_IF_ROUND_ELSE(4, -2);
    int cx = bounds.size.w / 2;
    // On round screens keep the HUD inside the visible band near the bezel top.
    int x0 = PBL_IF_ROUND_ELSE(cx - 44, s_field.origin.x + 2);
    int x1 = PBL_IF_ROUND_ELSE(cx + 44, s_field.origin.x + s_field.size.w - 2);
    GColor fg = PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
    graphics_context_set_text_color(ctx, fg);
    graphics_context_set_fill_color(ctx, fg);

    // Ball stock on the left: one pip plus a count.
    graphics_fill_circle(ctx, GPoint(x0 + 3, PBL_IF_ROUND_ELSE(13, 8)), 3);
    snprintf(buf, sizeof(buf), "x%d", s_balls_left);
    graphics_draw_text(ctx, buf, font, GRect(x0 + 9, y, 28, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Orange pegs remaining in the middle, score on the right.
    snprintf(buf, sizeof(buf), "%d left", orange_remaining());
    graphics_draw_text(ctx, buf, font, GRect(cx - 27, y, 54, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    snprintf(buf, sizeof(buf), "%d", s_score);
    graphics_draw_text(ctx, buf, font, GRect(cx + 28, y, x1 - cx - 28, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

static void draw_overlay(GContext *ctx, GRect bounds) {
    GRect box = GRect(bounds.size.w / 2 - 65, bounds.size.h / 2 - 40, 130, 80);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorWhite));
    graphics_fill_rect(ctx, box, 6, GCornersAll);
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    graphics_draw_round_rect(ctx, box, 6);

    char buf[64];
    if (s_state == STATE_WON) {
        snprintf(buf, sizeof(buf), "LEVEL CLEAR!\n%d\nSELECT: menu", s_score);
    } else if (s_endless) {
        snprintf(buf, sizeof(buf), "GAME OVER\n%d\nSELECT: retry", s_score);
    } else {
        snprintf(buf, sizeof(buf), "OUT OF BALLS\n%d\nSELECT: retry", s_score);
    }
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       grect_inset(box, GEdgeInsets(4)),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void layer_update(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorOxfordBlue, GColorWhite));
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack));
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

    // Bucket: two side walls and a base with an open top.
    int by = s_field.origin.y + s_field.size.h - s_bucket_h;
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorYellow, GColorBlack));
    graphics_fill_rect(ctx, GRect(s_bucket_x - s_bucket_w / 2, by, 2, s_bucket_h),
                       0, GCornerNone);
    graphics_fill_rect(ctx, GRect(s_bucket_x + s_bucket_w / 2 - 2, by, 2, s_bucket_h),
                       0, GCornerNone);
    graphics_fill_rect(ctx, GRect(s_bucket_x - s_bucket_w / 2,
                                  by + s_bucket_h - 2, s_bucket_w, 2),
                       0, GCornerNone);

    if (s_state == STATE_AIMING) {
        draw_preview(ctx);
    }

    // Launcher barrel.
    int lx = s_launch.x + (int)((int64_t)(s_peg_r * 3) * sin_lookup(s_aim) / TRIG_MAX_RATIO);
    int ly = s_launch.y + (int)((int64_t)(s_peg_r * 3) * cos_lookup(s_aim) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, s_launch, GPoint(lx, ly));
    graphics_context_set_stroke_width(ctx, 1);

    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    for (int i = 0; i < MAX_ACTIVE_BALLS; i++) {
        if (s_balls[i].active) {
            graphics_fill_circle(ctx, GPoint(TO_PX(s_balls[i].x), TO_PX(s_balls[i].y)),
                                 s_ball_r);
        }
    }

    draw_hud(ctx, bounds);

    if (s_msg_ticks > 0) {
        graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorYellow, GColorBlack));
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
            s_touch_active = (s_state == STATE_AIMING || s_state == STATE_WON
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
    push_game_window();
}

void game_window_push_endless(void) {
    s_level = 0;
    s_endless = true;
    push_game_window();
}
