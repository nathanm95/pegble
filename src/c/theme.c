#include "theme.h"

#if defined(PBL_COLOR)
// Peg colours never change table to table — blue, orange, pink and green always
// mean the same thing (see theme.h). What a level's palette changes is the table
// it is all sitting on: background, rails, speckle and the accent used for the
// bucket, aim guide and progress bar.
#define PEGS \
    .peg = GColorBlueMoon, .peg_hi = GColorPictonBlue, .peg_lit = GColorCyan, \
    .target = GColorOrange, .target_hi = GColorRajah, .target_lit = GColorChromeYellow, \
    .text = GColorWhite

static const Theme s_themes[THEME_COUNT] = {
    { // 0 — Midnight
        .bg = GColorOxfordBlue, .wall = GColorCadetBlue, .star = GColorDukeBlue,
        .accent = GColorYellow, PEGS
    },
    { // 1 — Slate
        .bg = GColorBlack, .wall = GColorDarkGray, .star = GColorDarkGray,
        .accent = GColorYellow, PEGS
    },
    { // 2 — Plum
        .bg = GColorImperialPurple, .wall = GColorPurpureus, .star = GColorIndigo,
        .accent = GColorCeleste, PEGS
    },
    { // 3 — Maroon
        .bg = GColorBulgarianRose, .wall = GColorRoseVale, .star = GColorDarkCandyAppleRed,
        .accent = GColorCeleste, PEGS
    },
    { // 4 — Deep Sea
        .bg = GColorMidnightGreen, .wall = GColorTiffanyBlue, .star = GColorCobaltBlue,
        .accent = GColorYellow, PEGS
    },
    { // 5 — Ember
        .bg = GColorDarkCandyAppleRed, .wall = GColorRoseVale, .star = GColorBulgarianRose,
        .accent = GColorYellow, PEGS
    },
    { // 6 — Violet
        .bg = GColorIndigo, .wall = GColorLavenderIndigo, .star = GColorImperialPurple,
        .accent = GColorYellow, PEGS
    },
    { // 7 — Charcoal
        .bg = GColorDarkGray, .wall = GColorLightGray, .star = GColorBlack,
        .accent = GColorYellow, PEGS
    },
    { // 8 — Aurora
        .bg = GColorOxfordBlue, .wall = GColorMediumAquamarine, .star = GColorDarkGreen,
        .accent = GColorCyan, PEGS
    },
    { // 9 — Ink
        .bg = GColorBlack, .wall = GColorCobaltBlue, .star = GColorOxfordBlue,
        .accent = GColorCyan, PEGS
    },
    { // 10 — Mulberry
        .bg = GColorImperialPurple, .wall = GColorLavenderIndigo, .star = GColorPurple,
        .accent = GColorPastelYellow, PEGS
    },
    { // 11 — Night Ember
        .bg = GColorBulgarianRose, .wall = GColorFolly, .star = GColorDarkCandyAppleRed,
        .accent = GColorIcterine, PEGS
    }
};
#undef PEGS
#else
// One monochrome palette: white table, black everything. draw_peg() carries the
// type information on these watches.
static const Theme s_themes[1] = {
    {
        .bg = GColorWhite, .wall = GColorBlack, .star = GColorBlack,
        .peg = GColorBlack, .peg_hi = GColorWhite, .peg_lit = GColorBlack,
        .target = GColorBlack, .target_hi = GColorWhite, .target_lit = GColorBlack,
        .accent = GColorBlack, .text = GColorBlack
    }
};
#endif

const Theme *theme_get(int index) {
#if defined(PBL_COLOR)
    if (index < 0) {
        index = -index;
    }
    return &s_themes[index % THEME_COUNT];
#else
    (void)index;
    return &s_themes[0];
#endif
}
