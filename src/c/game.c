#include <stdlib.h>
#include "game.h"
#include "abilities.h"
#include "effects.h"
#include "haptics.h"
#include "pins.h"
#include "progress.h"
#include "theme.h"
#include "ui.h"

#define TICK_MS 33
#define PREVIEW_MAX 10
#define STAR_COUNT 14            // background speckle
#define BANNER_TICKS 30
#define TOAST_TICKS 80

typedef enum { PRIO_LOW, PRIO_MID, PRIO_HIGH, PRIO_TOP } BannerPrio;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;
static GBitmap *s_bucket;    // the PixelLab tray (colour watches); NULL elsewhere

static PlayConfig s_cfg;     // what to start when the window loads
static bool s_resume;        // ...or pick the paused run up instead

static const Theme *s_theme;
static uint8_t s_theme_index = 255;
static GPoint s_stars[STAR_COUNT];
static GPoint s_shake_applied;
static GSize s_root_size;

static char s_msg[24];
static int s_msg_ticks;
static int s_msg_prio;
static int8_t s_toast = -1;  // Trophy being shown, -1 if none
static int s_toast_ticks;
static int s_fever_ticks;    // since the fever started, for the banner's pulse

static GameState s_last_state;
static int32_t s_prev_best;  // the record this run is trying to beat
static int s_prev_boards;    // endless: the deepest run before this one
static bool s_new_best;

#if defined(PEGBLE_AUTOPLAY)
// Debug build only: plays itself so the emulator can be watched (and
// screendumped) without anyone pressing buttons. See wscript.
static Rng s_auto;
static int s_auto_wait;
#endif

static void show_msg(const char *text, BannerPrio prio) {
    if (s_msg_ticks > 0 && (int)prio < s_msg_prio) {
        return;
    }
    strncpy(s_msg, text, sizeof(s_msg) - 1);
    s_msg[sizeof(s_msg) - 1] = '\0';
    s_msg_ticks = BANNER_TICKS;
    s_msg_prio = prio;
}

static GColor flash_colour(void) {
    return PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
}

static void refresh_theme(void) {
    const Play *p = play_get();
    if (p->theme == s_theme_index) {
        return;
    }
    s_theme_index = p->theme;
    s_theme = theme_get(p->theme);
    window_set_background_color(s_window, s_theme->bg);
    const GRect f = p->field;
    for (int i = 0; i < STAR_COUNT; i++) {
        s_stars[i] = GPoint(f.origin.x + 2 + rand() % (f.size.w - 4),
                            f.origin.y + 2 + rand() % (f.size.h - 4));
    }
}

// ---- records for this run ----

static void capture_records(void) {
    const Play *p = play_get();
    s_new_best = false;
    switch (p->cfg.mode) {
        case MODE_LEVEL:
            s_prev_best = progress_best(p->cfg.level);
            break;
        case MODE_DAILY: {
            const DailyRecord *d = progress_daily();
            s_prev_best = (d->day == p->cfg.day) ? d->best_today : 0;
            break;
        }
        default:
            s_prev_best = progress_endless_best();
            s_prev_boards = progress_endless_boards();
            break;
    }
}

static void save_run(void) {
    RunSnapshot snap;
    play_snapshot(&snap);
    progress_save_run(&snap);
}

static void new_run(bool retry) {
    if (retry) {
        play_retry();
    } else {
        play_start(&s_cfg);
    }
    progress_begin_game();
    effects_reset();
    capture_records();
    s_msg_ticks = 0;
    s_toast_ticks = 0;
    s_fever_ticks = 0;
    s_last_state = STATE_AIMING;
    refresh_theme();
    save_run();
}

// ---- turning play events into sparks, banners and buzzes ----

static GColor peg_spark(uint8_t type, bool bonus) {
    if (bonus) return PEG_COLOR_BONUS_LIT;
    switch (type) {
        case PEG_TARGET: return s_theme->target_lit;
        case PEG_POWER:  return ability_color(ABILITY_MULTIBALL, true);
        default:         return s_theme->peg_lit;
    }
}

static void game_over(const PlayEvent *e) {
    const Play *p = play_get();
    progress_clear_run();
    s_new_best = p->score > s_prev_best;
#if !PEGBLE_LITE
    if (p->cfg.mode == MODE_DAILY) {
        const DailyRecord *d = progress_daily();
        pins_daily(p->cfg.day, d->best_today, d->streak);
    } else if (p->cfg.mode == MODE_ENDLESS && e->type == EV_LOST
               && (p->score > s_prev_best || p->boards > s_prev_boards)) {
        pins_endless_record(p->score, p->boards);
    }
#endif
}

static void present(const PlayEvent *e) {
    const Play *p = play_get();
    GPoint at = GPoint(e->x, e->y);
    char buf[24];
    switch (e->type) {
        case EV_PEG_HIT:
            effects_burst(at, peg_spark(e->a, e->b), (e->a == PEG_BLUE && !e->b) ? 4 : 7, FP(3));
            // Only the hits worth noticing get a number floating off them.
            if (e->value >= 100) {
                effects_popup(at, e->value, s_theme->text);
            }
            break;
        case EV_ABILITY:
            show_msg(ability_name((Ability)e->a), PRIO_MID);
            if (e->a == ABILITY_BLAST) {
                effects_burst(at, ability_color(ABILITY_BLAST, true), 14, FP(4));
                effects_flash(flash_colour(), 2);
                effects_shake(8);
                haptics_play(HAPTIC_BLAST);
            } else {
                effects_burst(at, ability_color((Ability)e->a, true), 8, FP(3));
                effects_shake(3);
                haptics_play(HAPTIC_ABILITY);
            }
            break;
#if !PEGBLE_LITE
        case EV_BUMPER:
            effects_burst(at, s_theme->accent, 5, FP(3));
            break;
        case EV_PORTAL:
            effects_burst(at, s_theme->accent, 6, FP(2));
            break;
#endif
        case EV_SPOOKY:
            show_msg("SPOOKY RETURN", PRIO_MID);
            effects_burst(at, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack), 8, FP(3));
            haptics_play(HAPTIC_ABILITY);
            break;
        case EV_BUCKET:
            show_msg("FREE BALL!", PRIO_LOW);
            effects_burst(at, s_theme->accent, 10, FP(3));
            haptics_play(HAPTIC_BUCKET);
            break;
        case EV_STUCK:
            effects_burst(at, s_theme->peg_hi, 4, FP(1));
            break;
        case EV_COMBO:
            if (e->value >= 10) {
                snprintf(buf, sizeof(buf), "%d COMBO!", (int)e->value);
                show_msg(buf, PRIO_LOW);
                haptics_play(HAPTIC_TICK);
            }
            break;
        case EV_STYLE:
            snprintf(buf, sizeof(buf), "%s +%d",
                     e->a == STYLE_LONG_SHOT ? "LONG SHOT"
                     : e->a == STYLE_OFF_THE_WALL ? "OFF THE WALL" : "COMBO",
                     (int)e->value);
            show_msg(buf, PRIO_MID);
            effects_popup(at, e->value, s_theme->accent);
            haptics_play(HAPTIC_STYLE);
            break;
        case EV_FEVER:
            s_fever_ticks = 0;
            show_msg("", PRIO_TOP); // clear the banner; the fever draws its own
            s_msg_ticks = 0;
            effects_burst(at, s_theme->target_lit, 16, FP(4));
            effects_spray(GRect(p->field.origin.x, p->field.origin.y + p->field.size.h / 3,
                                p->field.size.w, 10), s_theme->accent, 14);
            effects_flash(flash_colour(), 3);
            effects_shake(10);
            haptics_play(HAPTIC_FEVER);
            break;
        case EV_FEVER_BUCKET:
            effects_popup(GPoint(e->x, e->y - 10), e->value, s_theme->text);
            effects_spray(GRect(e->x - 12, e->y - 6, 24, 6), s_theme->accent, 10);
            haptics_play(HAPTIC_BUCKET);
            break;
        case EV_BOARD_CLEAR:
            // The new board's number is already in the HUD; say what happened.
            snprintf(buf, sizeof(buf), "BOARD CLEAR! +%d", e->a);
            show_msg(buf, PRIO_HIGH);
            effects_spray(GRect(p->field.origin.x, p->field.origin.y + p->field.size.h / 3,
                                p->field.size.w, 10), s_theme->accent, 16);
            effects_flash(flash_colour(), 2);
            haptics_play(HAPTIC_WIN);
            break;
        case EV_WON:
            effects_spray(GRect(p->field.origin.x, p->field.origin.y + 4, p->field.size.w, 10),
                          s_theme->accent, 20);
            haptics_play(HAPTIC_WIN);
            break;
        case EV_LOST:
            effects_shake(10);
            haptics_play(HAPTIC_LOSE);
            break;
        default:
            break;
    }
}

static void drain_events(void) {
    const Play *p = play_get();
    PlayEvent e;
    while (play_next_event(&e)) {
        // A board clear swaps the table, so the effects need the new theme.
        if (e.type == EV_BOARD_CLEAR) {
            refresh_theme();
        }
        present(&e);
        progress_on_event(&e, p);
        if (e.type == EV_WON || e.type == EV_LOST) {
            game_over(&e);
        }
    }
#if !PEGBLE_LITE
    Trophy t;
    if (s_toast_ticks <= 0 && progress_next_unlock(&t)) {
        s_toast = t;
        s_toast_ticks = TOAST_TICKS;
        haptics_play(HAPTIC_TROPHY);
    }
#endif
}

// ---- the tick ----

#if defined(PEGBLE_AUTOPLAY)
static void autoplay(void) {
    const Play *p = play_get();
    static GameState seen;
    if (p->state != seen) {
        seen = p->state;
        s_auto_wait = 0; // each state gets its own wait, the results panel included
    }
    if (++s_auto_wait < 20) {
        return;
    }
    if (p->state == STATE_AIMING) {
        int32_t lim = play_aim_limit();
        play_set_aim(rng_range(&s_auto, 2 * lim + 1) - lim);
        play_fire();
        s_auto_wait = 0;
    } else if ((p->state == STATE_WON || p->state == STATE_LOST) && s_auto_wait > 90) {
        new_run(true);
        s_auto_wait = 0;
    }
}
#endif

static void game_tick(void *context) {
    s_timer = app_timer_register(TICK_MS, game_tick, NULL);
    if (s_msg_ticks > 0) s_msg_ticks--;
    if (s_toast_ticks > 0) s_toast_ticks--;
    s_fever_ticks++;

    play_tick();
    drain_events();
    effects_update();

    const Play *p = play_get();
    // Between shots the run is saved, so quitting never costs more than the
    // shot in flight.
    if (p->state == STATE_AIMING && s_last_state != STATE_AIMING) {
        save_run();
    }
    s_last_state = p->state;
#if defined(PEGBLE_AUTOPLAY)
    autoplay();
#endif

    // Screen shake: nudge the whole game layer rather than every draw call.
    GPoint shake = effects_shake_offset();
    if (shake.x != s_shake_applied.x || shake.y != s_shake_applied.y) {
        layer_set_frame(s_layer, GRect(shake.x, shake.y, s_root_size.w, s_root_size.h));
        s_shake_applied = shake;
    }
    layer_mark_dirty(s_layer);
}

static void fire(void) {
    if (play_fire()) {
        progress_save_run_balls(play_get()->balls_left);
    }
}

// ---- drawing ----

#if !PEGBLE_LITE
static void draw_portal(GContext *ctx, const Peg *g, int r) {
    // Four dots chasing round a ring, so it reads as a hole, not a peg.
    GColor c = PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack);
    graphics_context_set_fill_color(ctx, c);
    int32_t spin = play_get()->tick * (TRIG_MAX_ANGLE / 48);
    for (int k = 0; k < 4; k++) {
        int32_t a = spin + k * (TRIG_MAX_ANGLE / 4);
        graphics_fill_circle(ctx, GPoint(g->x + (int16_t)(r * sin_lookup(a) / TRIG_MAX_RATIO),
                                         g->y - (int16_t)(r * cos_lookup(a) / TRIG_MAX_RATIO)), 1);
    }
    graphics_context_set_stroke_color(ctx, c);
    graphics_draw_circle(ctx, GPoint(g->x, g->y), 1);
}
#endif

static void draw_peg(GContext *ctx, const Peg *p, int idx, int r) {
    GPoint c = GPoint(p->x, p->y);
    bool bonus = idx == play_get()->bonus_idx;
#if !PEGBLE_LITE
    if (p->type == PEG_PORTAL) {
        draw_portal(ctx, p, r + 1);
        return;
    }
#endif
#if defined(PBL_COLOR)
    switch (p->type) {
        case PEG_BUMPER:
            graphics_context_set_fill_color(ctx, s_theme->accent);
            graphics_fill_circle(ctx, c, r + 1);
            graphics_context_set_fill_color(ctx, s_theme->bg);
            graphics_fill_circle(ctx, c, r - 2);
            graphics_context_set_fill_color(ctx, s_theme->accent);
            graphics_fill_circle(ctx, c, 1);
            return;
        default:
            break;
    }
    GColor body;
    if (bonus) {
        body = p->lit ? PEG_COLOR_BONUS_LIT : PEG_COLOR_BONUS;
    } else if (p->type == PEG_TARGET) {
        body = p->lit ? s_theme->target_lit : s_theme->target;
    } else if (p->type == PEG_POWER) {
        body = ability_color((Ability)p->ability, p->lit);
    } else {
        body = p->lit ? s_theme->peg_lit : s_theme->peg;
    }
    graphics_context_set_fill_color(ctx, body);
    graphics_fill_circle(ctx, c, r);
    // A one-pixel highlight up and left turns a flat disc into a bead.
    graphics_context_set_fill_color(ctx, p->type == PEG_TARGET ? s_theme->target_hi
                                                               : s_theme->peg_hi);
    graphics_fill_circle(ctx, GPoint(c.x - r / 3, c.y - r / 3), 1);
    if (p->type == PEG_POWER) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_context_set_stroke_color(ctx, GColorWhite);
        ability_draw_mark(ctx, c, r, (Ability)p->ability);
    }
    if (bonus) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, c, 1);
    }
    if (p->lit) {
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_draw_circle(ctx, c, r);
    }
#else
    // B&W: shape encodes type — target filled, blue hollow, power hollow with
    // the ability's mark inside, bonus filled with a white core, bumper a
    // double ring. Lit pegs gain an outer ring.
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorBlack);
#if !PEGBLE_LITE
    if (p->type == PEG_BUMPER) {
        graphics_draw_circle(ctx, c, r + 1);
        graphics_draw_circle(ctx, c, r - 2);
        graphics_fill_circle(ctx, c, 1);
        return;
    }
#endif
    if (bonus) {
        graphics_fill_circle(ctx, c, r);
        graphics_context_set_fill_color(ctx, GColorWhite);
        graphics_fill_circle(ctx, c, r - 2);
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_circle(ctx, c, 1);
    } else if (p->type == PEG_TARGET) {
        graphics_fill_circle(ctx, c, r);
    } else if (p->type == PEG_POWER) {
        graphics_draw_circle(ctx, c, r);
        ability_draw_mark(ctx, c, r, (Ability)p->ability);
    } else {
        graphics_draw_circle(ctx, c, r);
    }
    if (p->lit) {
        graphics_draw_circle(ctx, c, r + 2);
    }
#endif
}

static void draw_ball(GContext *ctx, const Ball *b, int r) {
    bool burning = b->fire_ticks > 0;
    GColor body = burning ? PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack)
                          : PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack);
    // Comet tail: older positions are smaller.
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(
        burning ? s_theme->accent : s_theme->peg_hi, GColorBlack));
    for (int i = 1; i < b->trail_n; i++) {
        int tr = r - i;
        if (tr < 1) {
            break;
        }
        graphics_fill_circle(ctx, GPoint(b->trail_x[i], b->trail_y[i]), tr);
    }
    GPoint c = GPoint(TO_PX(b->x), TO_PX(b->y));
    graphics_context_set_fill_color(ctx, body);
    graphics_fill_circle(ctx, c, r);
    if (b->spooky) {
        graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
        graphics_draw_circle(ctx, c, r + 2);
    }
#if defined(PBL_COLOR)
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(c.x - 1, c.y - 1), 1);
#endif
}

static void draw_bucket(GContext *ctx) {
    const Play *p = play_get();
    int bottom = p->field.origin.y + p->field.size.h;
#if defined(PBL_COLOR)
    if (s_bucket) {
        // Sitting on the bottom edge, so its top rim rides a few pixels above
        // the catch line and a ball drops into the tray rather than onto it.
        GRect b = gbitmap_get_bounds(s_bucket);
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_bucket, GRect(p->bucket_x - b.size.w / 2,
                                     bottom - b.size.h, b.size.w, b.size.h));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
        return;
    }
#endif
    int by = bottom - p->bucket_h;
    int x0 = p->bucket_x - p->bucket_w / 2;
    GColor accent = PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack);
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_rect(ctx, GRect(x0, by, 2, p->bucket_h), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(x0 + p->bucket_w - 2, by, 2, p->bucket_h), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(x0, by + p->bucket_h - 2, p->bucket_w, 2), 0, GCornerNone);
#if defined(PBL_COLOR)
    // A lighter mouth so the opening reads as a target, not a gap in the wall.
    graphics_context_set_stroke_color(ctx, s_theme->target_lit);
    graphics_draw_line(ctx, GPoint(x0 + 2, by), GPoint(x0 + p->bucket_w - 3, by));
#endif
}

// During a fever the bucket gives way to five bonus buckets across the bottom.
static void draw_fever_buckets(GContext *ctx) {
    const Play *p = play_get();
    const GRect f = p->field;
    int h = p->bucket_h + 4;
    int y = f.origin.y + f.size.h - h;
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    for (int i = 0; i < FEVER_SLOTS; i++) {
        int x0 = p->fever_x + i * p->fever_w / FEVER_SLOTS;
        int x1 = p->fever_x + (i + 1) * p->fever_w / FEVER_SLOTS;
        GRect slot = GRect(x0 + 1, y, x1 - x0 - 2, h);
#if defined(PBL_COLOR)
        GColor fill = (i == FEVER_SLOTS / 2) ? GColorYellow
                      : (i % 2) ? s_theme->target : s_theme->accent;
        // The middle bucket pulses: it's the one worth aiming for.
        if (i == FEVER_SLOTS / 2 && (s_fever_ticks & 4)) {
            fill = GColorWhite;
        }
        graphics_context_set_fill_color(ctx, fill);
        graphics_fill_rect(ctx, slot, 2, GCornersTop);
        graphics_context_set_text_color(ctx, GColorBlack);
#else
        graphics_context_set_stroke_color(ctx, GColorBlack);
        graphics_draw_rect(ctx, slot);
        graphics_context_set_text_color(ctx, GColorBlack);
#endif
        // Thousands; the "k" goes when a slot is too narrow for it (chalk).
        char label[12];
        snprintf(label, sizeof(label), slot.size.w >= 22 ? "%dk" : "%d",
                 (int)(FEVER_BUCKET_VALUES[i] / 1000));
        graphics_draw_text(ctx, label, font, GRect(slot.origin.x, y - 4, slot.size.w, 16),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
}

static void draw_preview(GContext *ctx) {
    GPoint pts[PREVIEW_MAX];
    int n = play_preview(pts, PREVIEW_MAX);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
    for (int i = 0; i < n; i++) {
        // Dots thin out with distance, so the near part of the arc reads first.
        graphics_fill_circle(ctx, pts[i], (i < n / 2) ? 2 : 1);
    }
}

// Round screens have very little width up at the bezel, so long scores are
// abbreviated rather than truncated to "...".
static void format_hud_score(char *buf, size_t n, int32_t score) {
    if (score >= 10000) {
        snprintf(buf, n, "%dk", (int)(score / 1000));
    } else {
        snprintf(buf, n, "%d", (int)score);
    }
}

static void draw_hud(GContext *ctx, GRect bounds) {
    const Play *p = play_get();
    char buf[32];
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    int y = PBL_IF_ROUND_ELSE(4, -2);
    int cx = bounds.size.w / 2;
    // On round screens keep the HUD inside the visible band near the bezel top.
    int x0 = PBL_IF_ROUND_ELSE(cx - 46, p->field.origin.x + 2);
    int x1 = PBL_IF_ROUND_ELSE(cx + 46, p->field.origin.x + p->field.size.w - 2);
    GColor fg = PBL_IF_COLOR_ELSE(s_theme->text, GColorBlack);
    graphics_context_set_text_color(ctx, fg);
    graphics_context_set_fill_color(ctx, fg);

    // Ball stock on the left: one pip plus a count.
    graphics_fill_circle(ctx, GPoint(x0 + 3, PBL_IF_ROUND_ELSE(13, 8)), 3);
    snprintf(buf, sizeof(buf), "x%d", p->balls_left);
    graphics_draw_text(ctx, buf, font, GRect(x0 + 9, y, 24, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Board (endless) or "daily", and targets left in the middle; score right.
    int mid_w = PBL_IF_ROUND_ELSE(38, 70);
    int left = play_targets_remaining();
    if (p->cfg.mode == MODE_ENDLESS) {
        snprintf(buf, sizeof(buf), PBL_IF_ROUND_ELSE("B%d-%d", "B%d - %d left"),
                 p->boards + 1, left);
    } else if (p->cfg.mode == MODE_DAILY) {
        snprintf(buf, sizeof(buf), PBL_IF_ROUND_ELSE("D-%d", "Daily - %d"), left);
    } else {
        snprintf(buf, sizeof(buf), PBL_IF_ROUND_ELSE("%d", "%d left"), left);
    }
    graphics_draw_text(ctx, buf, font, GRect(cx - mid_w / 2, y, mid_w, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    format_hud_score(buf, sizeof(buf), p->score);
    int score_x = cx + mid_w / 2 + 1;
    graphics_draw_text(ctx, buf, font, GRect(score_x, y, x1 - score_x, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

    // Progress bar along the top of the playfield: how much of the board is done.
    const GRect f = p->field;
    int done = p->target_total ? p->target_cleared * f.size.w / p->target_total : 0;
    if (done > f.size.w) {
        done = f.size.w;
    }
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->wall, GColorBlack));
    graphics_fill_rect(ctx, GRect(f.origin.x, f.origin.y - 2, f.size.w, 1), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
    graphics_fill_rect(ctx, GRect(f.origin.x, f.origin.y - 3, done, 2), 0, GCornerNone);
}

// The running count of pegs lit by the shot in flight.
static void draw_combo(GContext *ctx) {
    const Play *p = play_get();
    if (p->state != STATE_FLYING || p->shot_hits < 3 || p->fever) {
        return;
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%d PEGS", p->shot_hits);
    int y = p->field.origin.y + p->field.size.h - p->bucket_h - 22;
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(
        p->shot_hits >= 10 ? s_theme->accent : s_theme->text, GColorBlack));
    graphics_draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(p->field.origin.x, y, p->field.size.w, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void draw_fever_banner(GContext *ctx, GRect bounds) {
    const Play *p = play_get();
    if (!p->fever || p->state == STATE_WON || p->state == STATE_LOST) {
        return;
    }
#if defined(PBL_COLOR)
    // Cycles through the board's own colours every few frames.
    const GColor cycle[] = { s_theme->target_lit, s_theme->accent, PEG_COLOR_BONUS,
                             GColorScreaminGreen, GColorWhite };
    graphics_context_set_text_color(ctx, cycle[(s_fever_ticks / 3) % 5]);
#else
    graphics_context_set_text_color(ctx, GColorBlack);
#endif
    graphics_draw_text(ctx, "EXTREME FEVER", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(0, bounds.size.h / 2 - 38, bounds.size.w, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Over the results panel the full toast would hide its last lines, and a win
// is exactly when trophies arrive, so there it shrinks to one line underneath.
static void draw_toast_line(GContext *ctx, GRect bounds) {
    char text[32];
    snprintf(text, sizeof(text), "TROPHY: %s", trophy_name((Trophy)s_toast));
    GRect box = GRect(bounds.size.w / 2 - 66, (bounds.size.h + 104) / 2 + 2, 132, 20);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
    graphics_fill_rect(ctx, box, 4, GCornersAll);
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack));
    graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(box.origin.x, box.origin.y, box.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void draw_toast(GContext *ctx, GRect bounds) {
#if PEGBLE_LITE
    return;
#endif
    if (s_toast_ticks <= 0 || s_toast < 0) {
        return;
    }
    const Play *p = play_get();
    if (p->state == STATE_WON || p->state == STATE_LOST) {
        draw_toast_line(ctx, bounds);
        return;
    }
    const GRect f = p->field;
    GRect box = GRect(f.origin.x + 6, f.origin.y + f.size.h - p->bucket_h - 44,
                      f.size.w - 12, 36);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
    graphics_fill_rect(ctx, box, 6, GCornersAll);
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack));
    graphics_draw_round_rect(ctx, box, 6);
    int text_x = box.origin.x + 6;
    GBitmap *cup = ui_icon(ICON_TROPHY);
    if (cup) {
        GRect ib = gbitmap_get_bounds(cup);
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, cup, GRect(box.origin.x + 4,
            box.origin.y + (box.size.h - ib.size.h) / 2, ib.size.w, ib.size.h));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
        text_x = box.origin.x + ib.size.w + 6;
    }
    GRect text = GRect(text_x, box.origin.y, box.origin.x + box.size.w - text_x - 4, 18);
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack));
    graphics_draw_text(ctx, "TROPHY", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), text,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    text.origin.y += 14;
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
    graphics_draw_text(ctx, trophy_name((Trophy)s_toast),
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), text,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static int stars_for(const Play *p) {
    return 1 + (p->score >= level_par(p->cfg.level, 2)) + (p->score >= level_par(p->cfg.level, 3));
}

static void draw_overlay(GContext *ctx, GRect bounds) {
    const Play *p = play_get();
    bool won = p->state == STATE_WON;
    int w = 140, h = 104;
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
    GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    char buf[40], num[16];
    const char *title;
    switch (p->cfg.mode) {
        case MODE_ENDLESS: title = "GAME OVER"; break;
        case MODE_DAILY:   title = won ? "DAILY CLEAR!" : "DAILY OVER"; break;
        default:           title = won ? "LEVEL CLEAR!" : "OUT OF BALLS"; break;
    }
    int y = box.origin.y + 2;
    graphics_context_set_text_color(ctx, accent);
    graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(box.origin.x, y, box.size.w, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    y += 20;

    ui_fmt_score(num, sizeof(num), p->score);
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, num, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(box.origin.x, y, box.size.w, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    y += 28;

    if (p->cfg.mode == MODE_LEVEL && won) {
        ui_draw_stars(ctx, GPoint(box.origin.x + box.size.w / 2, y + 7), stars_for(p), 7,
                      PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack),
                      PBL_IF_COLOR_ELSE(s_theme->wall, GColorBlack));
        y += 16;
    } else {
        if (p->cfg.mode == MODE_ENDLESS) {
            snprintf(buf, sizeof(buf), "%d board%s, deepest %d", p->boards,
                     p->boards == 1 ? "" : "s", progress_endless_boards());
        } else if (p->cfg.mode == MODE_DAILY) {
            snprintf(buf, sizeof(buf), "%d-day streak", progress_daily()->streak);
        } else {
            snprintf(buf, sizeof(buf), "%d orange left", play_targets_remaining());
        }
        graphics_draw_text(ctx, buf, small, GRect(box.origin.x + 2, y, box.size.w - 4, 18),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        y += 16;
    }

    if (s_new_best && p->score > 0) {
        graphics_context_set_text_color(ctx, accent);
        snprintf(buf, sizeof(buf), "NEW BEST!");
    } else {
        int32_t best = p->cfg.mode == MODE_LEVEL ? progress_best(p->cfg.level)
                       : p->cfg.mode == MODE_DAILY ? progress_daily()->best_today
                       : progress_endless_best();
        ui_fmt_score(num, sizeof(num), best);
        snprintf(buf, sizeof(buf), "best %s", num);
    }
    graphics_draw_text(ctx, buf, small, GRect(box.origin.x, y, box.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    y += 16;

    graphics_context_set_text_color(ctx, accent);
    graphics_draw_text(ctx, won ? "SELECT: menu" : "SELECT: retry", small,
                       GRect(box.origin.x, y, box.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void layer_update(Layer *layer, GContext *ctx) {
    const Play *p = play_get();
    GRect bounds = layer_get_bounds(layer);
    const GRect f = p->field;
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
    graphics_draw_line(ctx, f.origin, GPoint(f.origin.x, f.origin.y + f.size.h));
    graphics_draw_line(ctx, GPoint(f.origin.x + f.size.w, f.origin.y),
                       GPoint(f.origin.x + f.size.w, f.origin.y + f.size.h));

    for (int i = 0; i < p->peg_count; i++) {
        const Peg *g = &p->pegs[i];
        if (!g->active) {
            continue;
        }
        // Lit pegs blink during the resolve flash.
        if (p->state == STATE_RESOLVING && g->lit && (p->tick & 2)) {
            continue;
        }
        draw_peg(ctx, g, i, p->peg_r);
    }

    if (p->fever) {
        draw_fever_buckets(ctx);
    } else {
        draw_bucket(ctx);
    }
    if (p->state == STATE_AIMING) {
        draw_preview(ctx);
    }

    // Launcher: a barrel plus a hub, so the pivot is obvious.
    int lx = p->launch.x + (int)((int32_t)(p->peg_r * 3) * sin_lookup(p->aim) / TRIG_MAX_RATIO);
    int ly = p->launch.y + (int)((int32_t)(p->peg_r * 3) * cos_lookup(p->aim) / TRIG_MAX_RATIO);
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(s_theme->text, GColorBlack));
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, p->launch, GPoint(lx, ly));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
    graphics_fill_circle(ctx, p->launch, 2);

    for (int i = 0; i < MAX_BALLS; i++) {
        if (p->balls[i].active) {
            draw_ball(ctx, &p->balls[i], p->ball_r);
        }
    }

    effects_draw(ctx);

    GColor flash;
    if (effects_flash_color(&flash)) {
        graphics_context_set_fill_color(ctx, flash);
        graphics_fill_rect(ctx, f, 0, GCornerNone);
    }

    draw_hud(ctx, bounds);
    draw_combo(ctx);
    draw_fever_banner(ctx, bounds);

    if (s_msg_ticks > 0 && s_msg[0]) {
        graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(s_theme->accent, GColorBlack));
        graphics_draw_text(ctx, s_msg, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                           GRect(0, bounds.size.h / 2 - 30, bounds.size.w, 24),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    if (p->state == STATE_WON || p->state == STATE_LOST) {
        draw_overlay(ctx, bounds);
    }
    draw_toast(ctx, bounds);
#if defined(PEGBLE_HEAP)
    // Debug: the lowest free heap seen, big enough to read off a screendump.
    static size_t low = (size_t)-1;
    size_t now = heap_bytes_free();
    if (now < low) low = now;
    char heap[24];
    snprintf(heap, sizeof(heap), "heap %d", (int)low);
    graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack));
    graphics_draw_text(ctx, heap, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                       GRect(0, bounds.size.h / 2, bounds.size.w, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
#endif
}

// ---- input ----

static void select_click(ClickRecognizerRef rec, void *ctx) {
    switch (play_get()->state) {
        case STATE_AIMING: fire(); break;
        case STATE_WON:    window_stack_pop(true); break;
        case STATE_LOST:   new_run(true); break;
        default: break;
    }
}

static void aim_click(ClickRecognizerRef rec, void *ctx) {
    GameState s = play_get()->state;
    if (s != STATE_AIMING && s != STATE_FLYING) {
        return;
    }
    play_nudge_aim(click_recognizer_get_button_id(rec) == BUTTON_ID_UP ? -1 : 1);
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
    const Play *p = play_get();
    int half = p->field.size.w / 2;
    int dx = x - p->launch.x;
    if (dx > half) dx = half;
    if (dx < -half) dx = -half;
    play_set_aim((int32_t)dx * play_aim_limit() / half);
    layer_mark_dirty(s_layer);
}

static void touch_handler(const TouchEvent *event, void *context) {
    GameState s = play_get()->state;
    switch (event->type) {
        case TouchEvent_Touchdown:
            s_touch_active = touch_is_navigational(event)
                              && (s == STATE_AIMING || s == STATE_WON || s == STATE_LOST);
            if (s_touch_active && s == STATE_AIMING) {
                set_aim_from_touch_x(event->x);
            }
            break;
        case TouchEvent_PositionUpdate:
            if (s_touch_active && s == STATE_AIMING) {
                set_aim_from_touch_x(event->x);
            }
            break;
        case TouchEvent_Liftoff:
            if (!s_touch_active) {
                break;
            }
            s_touch_active = false;
            if (s == STATE_AIMING) {
                fire();
            } else if (s == STATE_WON) {
                window_stack_pop(true);
            } else if (s == STATE_LOST) {
                new_run(true);
            }
            break;
    }
}

// ---- window lifecycle ----

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    s_root_size = bounds.size;
    s_layer = layer_create(bounds);
    layer_set_update_proc(s_layer, layer_update);
    layer_add_child(root, s_layer);
    s_shake_applied = GPoint(0, 0);
    s_theme_index = 255;
    play_layout(bounds, PBL_IF_ROUND_ELSE(true, false));
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
    s_bucket = gbitmap_create_with_resource(RESOURCE_ID_BUCKET_BIG);
#elif defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
    s_bucket = gbitmap_create_with_resource(RESOURCE_ID_BUCKET_SMALL);
#endif
#if defined(PBL_COLOR)
    if (s_bucket) {
        play_set_bucket_width(gbitmap_get_bounds(s_bucket).size.w);
    }
#endif
#if defined(PEGBLE_AUTOPLAY)
    rng_seed(&s_auto, (uint32_t)time(NULL));
#endif

    RunSnapshot snap;
    if (s_resume && progress_load_run(&snap) && play_restore(&snap)) {
        effects_reset();
        capture_records();
        s_msg_ticks = 0;
        s_toast_ticks = 0;
        s_last_state = STATE_AIMING;
        refresh_theme();
        show_msg("WELCOME BACK", PRIO_LOW);
    } else {
        new_run(false);
    }
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
    progress_flush();
}

static void window_unload(Window *window) {
    if (s_bucket) {
        gbitmap_destroy(s_bucket);
        s_bucket = NULL;
    }
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

static void launch(PlayMode mode, int level, uint8_t power) {
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.mode = mode;
    s_cfg.level = level;
    s_cfg.power = power;
    s_cfg.unlocked_powers = progress_unlocked_powers();
    s_cfg.day = progress_today();
    s_resume = false;
    push_game_window();
}

void game_window_push_level(int level, uint8_t power) {
    launch(MODE_LEVEL, level, power);
}

void game_window_push_endless(uint8_t power) {
    launch(MODE_ENDLESS, 0, power);
}

void game_window_push_daily(void) {
    launch(MODE_DAILY, 0, POWER_MIX);
}

bool game_window_resume(void) {
    RunSnapshot snap;
    if (!progress_load_run(&snap)) {
        return false;
    }
    s_resume = true;
    push_game_window();
    return true;
}
