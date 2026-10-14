#include "ui.h"

void ui_draw_header(GContext *ctx, const Layer *cell, const char *text) {
#if defined(PBL_ROUND)
    GRect b = layer_get_bounds(cell);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(0, -3, b.size.w, b.size.h + 3),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
#else
    menu_cell_basic_header_draw(ctx, cell, text);
#endif
}

void ui_fmt_score(char *buf, size_t n, int32_t score) {
    char digits[12];
    bool neg = score < 0;
    uint32_t v = neg ? (uint32_t)(-score) : (uint32_t)score;
    int len = 0;
    do {
        digits[len++] = '0' + v % 10;
        v /= 10;
    } while (v && len < (int)sizeof(digits));
    size_t o = 0;
    if (neg && o + 1 < n) {
        buf[o++] = '-';
    }
    for (int i = len - 1; i >= 0 && o + 1 < n; i--) {
        buf[o++] = digits[i];
        if (i > 0 && i % 3 == 0 && o + 1 < n) {
            buf[o++] = ',';
        }
    }
    buf[o] = '\0';
}

// Five-pointed star: points alternate between the outer radius and half of it.
static void star_points(GPoint *pts, int r) {
    int inner = r / 2; // a fat star reads better than a spiky one at 5-7px
    for (int i = 0; i < 10; i++) {
        int32_t a = TRIG_MAX_ANGLE * i / 10;
        int rad = (i % 2 == 0) ? r : inner;
        pts[i] = GPoint((int16_t)((int32_t)rad * sin_lookup(a) / TRIG_MAX_RATIO),
                        (int16_t)(-(int32_t)rad * cos_lookup(a) / TRIG_MAX_RATIO));
    }
}

// Even-odd scanline fill of the star. gpath_draw_filled() collapses a star this
// small to a blob a few pixels across (seen on gabbro), so it's filled by hand:
// sample each row at its centre, find where it crosses the ten edges, and fill
// between pairs of crossings.
static void fill_star(GContext *ctx, GPoint c, const GPoint *pts, int r) {
    for (int y = -r; y <= r; y++) {
        int xs[10];
        int n = 0;
        int sy = 2 * y + 1; // row centre, in half pixels
        for (int i = 0; i < 10; i++) {
            GPoint a = pts[i], b = pts[(i + 1) % 10];
            int ay = 2 * a.y, by = 2 * b.y;
            if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                xs[n++] = a.x + (sy - ay) * (b.x - a.x) / (by - ay);
            }
        }
        for (int i = 1; i < n; i++) {
            for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                int t = xs[j];
                xs[j] = xs[j - 1];
                xs[j - 1] = t;
            }
        }
        for (int i = 0; i + 1 < n; i += 2) {
            graphics_fill_rect(ctx, GRect(c.x + xs[i], c.y + y, xs[i + 1] - xs[i] + 1, 1), 0,
                               GCornerNone);
        }
    }
}

void ui_draw_stars(GContext *ctx, GPoint centre, int filled, int r, GColor on, GColor off) {
    GPoint pts[10];
    star_points(pts, r);
    GPath path = { .num_points = 10, .points = pts, .rotation = 0, .offset = centre };
    int gap = r * 2 + 3;
    for (int i = 0; i < 3; i++) {
        path.offset = GPoint(centre.x + (i - 1) * gap, centre.y);
        if (i < filled) {
            graphics_context_set_fill_color(ctx, on);
            fill_star(ctx, path.offset, pts, r);
        } else {
            graphics_context_set_stroke_color(ctx, off);
            gpath_draw_outline(ctx, &path);
        }
    }
}

#if defined(PBL_COLOR)
static const uint32_t s_icon_resources[ICON_COUNT] = {
    RESOURCE_ID_ICON_MULTIBALL,
    RESOURCE_ID_ICON_BLAST,
    RESOURCE_ID_ICON_FIREBALL,
    RESOURCE_ID_ICON_SPOOKY,
    RESOURCE_ID_ICON_MAGNET,
    RESOURCE_ID_ICON_MIX,
    RESOURCE_ID_ICON_TROPHY,
    RESOURCE_ID_ICON_DAILY,
    RESOURCE_ID_ICON_ENDLESS,
    RESOURCE_ID_ICON_LOCK
};
static GBitmap *s_icons[ICON_COUNT];
#endif

GBitmap *ui_icon(IconId id) {
#if defined(PBL_COLOR)
    if (id >= ICON_COUNT) {
        return NULL;
    }
    if (!s_icons[id]) {
        s_icons[id] = gbitmap_create_with_resource(s_icon_resources[id]);
    }
    return s_icons[id];
#else
    (void)id;
    return NULL;
#endif
}

void ui_icons_release(void) {
#if defined(PBL_COLOR)
    for (int i = 0; i < ICON_COUNT; i++) {
        if (s_icons[i]) {
            gbitmap_destroy(s_icons[i]);
            s_icons[i] = NULL;
        }
    }
#endif
}
