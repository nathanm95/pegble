#include <stdlib.h>
#include "effects.h"

#define FP_SHIFT 8
#define FP(x) ((int32_t)(x) << FP_SHIFT)
#define TO_PX(x) ((int16_t)((x) >> FP_SHIFT))

#include "config.h"

// Fewer sparks on aplite, where every byte of static memory is heap the
// windows can't have (see config.h).
#define FX_PARTICLES (PEGBLE_LITE ? 16 : 28)
#define FX_POPUPS 4
#define FX_GRAVITY 22
#define SHAKE_AMPLITUDE 2

typedef struct {
    int32_t x, y, vx, vy; // Q24.8
    int16_t life;
    GColor color;
    uint8_t size;
} Particle;

typedef struct {
    int32_t x, y;         // Q24.8
    int16_t life;
    int amount;
    GColor color;
} Popup;

static Particle s_particles[FX_PARTICLES];
static Popup s_popups[FX_POPUPS];
static int16_t s_shake_ticks;
static int16_t s_flash_ticks;
static GColor s_flash_color;
static uint16_t s_seq;

void effects_reset(void) {
    for (int i = 0; i < FX_PARTICLES; i++) {
        s_particles[i].life = 0;
    }
    for (int i = 0; i < FX_POPUPS; i++) {
        s_popups[i].life = 0;
    }
    s_shake_ticks = 0;
    s_flash_ticks = 0;
}

static Particle *free_particle(void) {
    for (int i = 0; i < FX_PARTICLES; i++) {
        if (s_particles[i].life <= 0) {
            return &s_particles[i];
        }
    }
    return NULL;
}

void effects_burst(GPoint at, GColor color, int count, int32_t speed) {
    for (int i = 0; i < count; i++) {
        Particle *p = free_particle();
        if (!p) {
            return;
        }
        // Spread evenly round the circle, with a little jitter so repeated
        // bursts don't stamp out the same snowflake every time.
        int32_t angle = TRIG_MAX_ANGLE * i / count + (rand() % (TRIG_MAX_ANGLE / 16));
        int32_t v = speed / 2 + rand() % (speed / 2 + 1);
        p->x = FP(at.x);
        p->y = FP(at.y);
        p->vx = (int32_t)((int32_t)sin_lookup(angle) * v / TRIG_MAX_RATIO);
        p->vy = (int32_t)((int32_t)cos_lookup(angle) * v / TRIG_MAX_RATIO);
        p->life = 9 + rand() % 7;
        p->color = color;
        p->size = (rand() % 3 == 0) ? 2 : 1;
    }
}

void effects_spray(GRect area, GColor color, int count) {
    for (int i = 0; i < count; i++) {
        Particle *p = free_particle();
        if (!p) {
            return;
        }
        p->x = FP(area.origin.x + (area.size.w > 0 ? rand() % area.size.w : 0));
        p->y = FP(area.origin.y + (area.size.h > 0 ? rand() % area.size.h : 0));
        p->vx = FP(rand() % 5 - 2) / 2;
        p->vy = -FP(1) - rand() % FP(2);
        p->life = 16 + rand() % 12;
        p->color = color;
        p->size = (rand() % 2) ? 2 : 1;
    }
}

void effects_popup(GPoint at, int amount, GColor color) {
    Popup *slot = NULL;
    for (int i = 0; i < FX_POPUPS; i++) {
        if (s_popups[i].life <= 0) {
            slot = &s_popups[i];
            break;
        }
        // All busy: reuse the oldest so the newest score is always the one seen.
        if (!slot || s_popups[i].life < slot->life) {
            slot = &s_popups[i];
        }
    }
    slot->x = FP(at.x);
    slot->y = FP(at.y);
    slot->life = 22;
    slot->amount = amount;
    slot->color = color;
}

void effects_shake(int ticks) {
    if (ticks > s_shake_ticks) {
        s_shake_ticks = ticks;
    }
}

void effects_flash(GColor color, int ticks) {
    s_flash_color = color;
    if (ticks > s_flash_ticks) {
        s_flash_ticks = ticks;
    }
}

void effects_update(void) {
    s_seq++;
    for (int i = 0; i < FX_PARTICLES; i++) {
        Particle *p = &s_particles[i];
        if (p->life <= 0) {
            continue;
        }
        p->vy += FX_GRAVITY;
        p->x += p->vx;
        p->y += p->vy;
        p->life--;
    }
    for (int i = 0; i < FX_POPUPS; i++) {
        Popup *u = &s_popups[i];
        if (u->life <= 0) {
            continue;
        }
        u->y -= FP(1);
        u->life--;
    }
    if (s_shake_ticks > 0) {
        s_shake_ticks--;
    }
    if (s_flash_ticks > 0) {
        s_flash_ticks--;
    }
}

void effects_draw(GContext *ctx) {
    for (int i = 0; i < FX_PARTICLES; i++) {
        Particle *p = &s_particles[i];
        if (p->life <= 0) {
            continue;
        }
        // Flicker out over the last few ticks instead of vanishing mid-air.
        if (p->life < 4 && ((p->life + s_seq) & 1)) {
            continue;
        }
        graphics_context_set_fill_color(ctx, p->color);
        graphics_fill_rect(ctx, GRect(TO_PX(p->x), TO_PX(p->y), p->size, p->size),
                           0, GCornerNone);
    }

    char buf[12];
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    for (int i = 0; i < FX_POPUPS; i++) {
        Popup *u = &s_popups[i];
        if (u->life <= 0) {
            continue;
        }
        if (u->life < 6 && (u->life & 1)) {
            continue;
        }
        snprintf(buf, sizeof(buf), "+%d", u->amount);
        graphics_context_set_text_color(ctx, u->color);
        graphics_draw_text(ctx, buf, font, GRect(TO_PX(u->x) - 24, TO_PX(u->y) - 9, 48, 16),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
}

bool effects_flash_color(GColor *out) {
    if (s_flash_ticks <= 0) {
        return false;
    }
    *out = s_flash_color;
    return true;
}

GPoint effects_shake_offset(void) {
    if (s_shake_ticks <= 0) {
        return GPoint(0, 0);
    }
    // Decay with the remaining ticks so the kick settles rather than stopping dead.
    int amp = (s_shake_ticks > 4) ? SHAKE_AMPLITUDE : 1;
    return GPoint((s_shake_ticks & 1) ? amp : -amp, (s_shake_ticks & 2) ? amp : -amp);
}
