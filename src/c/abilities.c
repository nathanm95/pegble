#include "abilities.h"

static const char *const s_names[ABILITY_COUNT] = {
    "MULTIBALL!",
    "BLAST!",
    "FIREBALL!",
    "SPOOKY BALL!",
    "MAGNET!"
};

const char *ability_name(Ability a) {
    return (a < ABILITY_COUNT) ? s_names[a] : "";
}

GColor ability_color(Ability a, bool lit) {
    // Every power peg is green whichever ability it holds — the white mark in
    // the middle is what tells them apart, so the board only ever speaks four
    // colours. See theme.h.
    (void)a;
#if defined(PBL_COLOR)
    return lit ? GColorScreaminGreen : GColorIslamicGreen;
#else
    (void)lit;
    return GColorBlack;
#endif
}

void ability_draw_mark(GContext *ctx, GPoint c, int r, Ability a) {
    int arm = (r >= 5) ? 2 : 1;
    switch (a) {
        case ABILITY_MULTIBALL: // two dots, like the ball about to become two
            graphics_fill_rect(ctx, GRect(c.x - arm - 1, c.y, 1, 1), 0, GCornerNone);
            graphics_fill_rect(ctx, GRect(c.x + arm, c.y, 1, 1), 0, GCornerNone);
            break;
        case ABILITY_BLAST: // a plus, radiating
            graphics_fill_rect(ctx, GRect(c.x - arm, c.y, arm * 2 + 1, 1), 0, GCornerNone);
            graphics_fill_rect(ctx, GRect(c.x, c.y - arm, 1, arm * 2 + 1), 0, GCornerNone);
            break;
        case ABILITY_FIREBALL: // upright bar, a flame
            graphics_fill_rect(ctx, GRect(c.x, c.y - arm, 1, arm * 2 + 1), 0, GCornerNone);
            break;
        case ABILITY_SPOOKY: // hollow ring, a ghost passing through
            graphics_draw_circle(ctx, c, arm);
            break;
        case ABILITY_MAGNET: // flat bar, the pull
            graphics_fill_rect(ctx, GRect(c.x - arm, c.y, arm * 2 + 1, 1), 0, GCornerNone);
            break;
        default:
            break;
    }
}
