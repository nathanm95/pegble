#include "records.h"
#include "config.h"

// Not built for aplite: see config.h.
#if !PEGBLE_LITE
#include "levels.h"
#include "progress.h"
#include "ui.h"

typedef enum {
    STAT_STARS,
    STAT_GAMES,
    STAT_SHOTS,
    STAT_PEGS,
    STAT_BEST_COMBO,
    STAT_FEVERS,
    STAT_STYLE,
    STAT_BUCKETS,
    STAT_PINK,
    STAT_ENDLESS_BEST,
    STAT_ENDLESS_BOARDS,
    STAT_BOARDS_TOTAL,
    STAT_DAILY_BEST,
    STAT_DAILY_STREAK,
    STAT_DAILY_DAYS,
    STAT_COUNT
} StatRow;

static const char *const s_stat_labels[STAT_COUNT] = {
    "Stars", "Games played", "Shots fired", "Pegs cleared", "Best combo",
    "Fevers", "Style shots", "Free balls", "Pink peg hits",
    "Endless best", "Endless deepest run", "Endless boards, all time",
    "Daily best", "Daily best streak", "Dailies played"
};

static Window *s_window;
static MenuLayer *s_menu;
static char s_header[24];

static uint16_t num_sections(MenuLayer *menu, void *ctx) {
    return 2;
}

static uint16_t num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
    return section == 0 ? TROPHY_COUNT : STAT_COUNT;
}

static int16_t header_height(MenuLayer *menu, uint16_t section, void *ctx) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    if (section == 0) {
        snprintf(s_header, sizeof(s_header), "Trophies %d/%d", progress_trophy_count(),
                 TROPHY_COUNT);
        ui_draw_header(ctx, cell, s_header);
    } else {
        ui_draw_header(ctx, cell, "Stats");
    }
}

static void stat_value(StatRow row, char *buf, size_t n) {
    const Stats *s = progress_stats();
    const DailyRecord *d = progress_daily();
    int32_t v = 0;
    switch (row) {
        case STAT_STARS:
            snprintf(buf, n, "%d of %d", progress_total_stars(), 3 * LEVEL_COUNT);
            return;
        case STAT_GAMES:          v = s->games; break;
        case STAT_SHOTS:          v = s->shots; break;
        case STAT_PEGS:           v = s->pegs_cleared; break;
        case STAT_BEST_COMBO:     v = s->best_combo; break;
        case STAT_FEVERS:         v = s->fevers; break;
        case STAT_STYLE:          v = s->style_shots; break;
        case STAT_BUCKETS:        v = s->buckets; break;
        case STAT_PINK:           v = s->bonus_hits; break;
        case STAT_ENDLESS_BEST:   v = progress_endless_best(); break;
        case STAT_ENDLESS_BOARDS:
            snprintf(buf, n, "%d boards", progress_endless_boards());
            return;
        case STAT_BOARDS_TOTAL:   v = s->boards_cleared; break;
        case STAT_DAILY_BEST:     v = d->best_ever; break;
        case STAT_DAILY_STREAK:
            snprintf(buf, n, "%d days", d->best_streak);
            return;
        case STAT_DAILY_DAYS:     v = d->days_played; break;
        default: break;
    }
    ui_fmt_score(buf, n, v);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *data) {
    if (index->section == 0) {
        Trophy t = (Trophy)index->row;
        bool have = progress_has_trophy(t);
#if defined(PBL_COLOR)
        menu_cell_basic_draw(ctx, cell, trophy_name(t), trophy_hint(t),
                             ui_icon(have ? ICON_TROPHY : ICON_LOCK));
#else
        // No icons on black-and-white watches: say it in the title instead.
        char title[28];
        snprintf(title, sizeof(title), "%s%s", have ? "" : "? ", trophy_name(t));
        menu_cell_basic_draw(ctx, cell, title, trophy_hint(t), NULL);
#endif
        return;
    }
    char value[24];
    stat_value((StatRow)index->row, value, sizeof(value));
    // Round menus only show the focused row's subtitle, so there the label goes
    // in the title — a column of bare numbers means nothing.
    menu_cell_basic_draw(ctx, cell, PBL_IF_ROUND_ELSE(s_stat_labels[index->row], value),
                         PBL_IF_ROUND_ELSE(value, s_stat_labels[index->row]), NULL);
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
        .get_num_sections = num_sections,
        .get_num_rows = num_rows,
        .get_header_height = header_height,
        .draw_header = draw_header,
        .draw_row = draw_row
    });
#if defined(PBL_COLOR)
    menu_layer_set_highlight_colors(s_menu, GColorChromeYellow, GColorBlack);
#endif
    menu_layer_set_click_config_onto_window(s_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu);
    s_menu = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void records_push(void) {
    if (s_window) {
        return;
    }
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load,
        .unload = window_unload
    });
    window_stack_push(s_window, true);
}

#endif
