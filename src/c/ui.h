#pragma once

#include <pebble.h>

// Small drawing and formatting helpers shared by the menus and the game.

// A menu section header. The system one is left-aligned, which puts it off the
// glass on a round watch, so round watches get it centred.
void ui_draw_header(GContext *ctx, const Layer *cell, const char *text);

// 12400 -> "12,400".
void ui_fmt_score(char *buf, size_t n, int32_t score);

// A row of three stars centred on `centre`, the first `filled` drawn solid and
// the rest as outlines. `r` is each star's outer radius.
void ui_draw_stars(GContext *ctx, GPoint centre, int filled, int r, GColor on, GColor off);

// The PixelLab icons (resources/images/icon_*.png). They're only bundled for
// the colour watches; on black-and-white ones every icon is NULL and callers
// fall back to text.
typedef enum {
    ICON_MULTIBALL,   // the five abilities first, in Ability order
    ICON_BLAST,
    ICON_FIREBALL,
    ICON_SPOOKY,
    ICON_MAGNET,
    ICON_MIX,
    ICON_TROPHY,
    ICON_DAILY,
    ICON_ENDLESS,
    ICON_LOCK,
    ICON_COUNT
} IconId;

GBitmap *ui_icon(IconId id);  // loaded on first use and kept
void ui_icons_release(void);
