#include <pebble.h>
#include "game.h"
#include "levels.h"

static Window *s_menu_window;
static MenuLayer *s_menu_layer;

static int unlocked_level(void) {
    return persist_read_int(PERSIST_KEY_UNLOCKED);
}

static uint16_t get_num_rows(MenuLayer *layer, uint16_t section, void *ctx) {
    return LEVEL_COUNT + 1; // levels plus Endless
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *data) {
    char title[32];
    char subtitle[24];
    int row = index->row;
    if (row == LEVEL_COUNT) {
        int high = persist_read_int(PERSIST_KEY_BEST_BASE + LEVEL_COUNT);
        if (high > 0) {
            snprintf(subtitle, sizeof(subtitle), "High: %d", high);
        } else {
            snprintf(subtitle, sizeof(subtitle), "Random boards");
        }
        menu_cell_basic_draw(ctx, cell, "Endless", subtitle, NULL);
        return;
    }
    snprintf(title, sizeof(title), "%d. %s", row + 1, level_name(row));
    if (row > unlocked_level()) {
        snprintf(subtitle, sizeof(subtitle), "Locked");
    } else {
        int best = persist_read_int(PERSIST_KEY_BEST_BASE + row);
        if (best > 0) {
            snprintf(subtitle, sizeof(subtitle), "Best: %d", best);
        } else {
            snprintf(subtitle, sizeof(subtitle), "Not cleared");
        }
    }
    menu_cell_basic_draw(ctx, cell, title, subtitle, NULL);
}

static void select_row(MenuLayer *layer, MenuIndex *index, void *ctx) {
    if (index->row == LEVEL_COUNT) {
        game_window_push_endless();
    } else if (index->row <= unlocked_level()) {
        game_window_push(index->row);
    } else {
        vibes_short_pulse();
    }
}

static void menu_window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_menu_layer = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
        .get_num_rows = get_num_rows,
        .draw_row = draw_row,
        .select_click = select_row
    });
#if defined(PBL_COLOR)
    menu_layer_set_highlight_colors(s_menu_layer, GColorOrange, GColorWhite);
#endif
    menu_layer_set_click_config_onto_window(s_menu_layer, window);
    layer_add_child(root, menu_layer_get_layer(s_menu_layer));
}

static void menu_window_appear(Window *window) {
    // Refresh best scores / unlocks after returning from a level.
    menu_layer_reload_data(s_menu_layer);
}

static void menu_window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
}

int main(void) {
    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) {
        .load = menu_window_load,
        .appear = menu_window_appear,
        .unload = menu_window_unload
    });
    window_stack_push(s_menu_window, true);
    app_event_loop();
    window_destroy(s_menu_window);
}
