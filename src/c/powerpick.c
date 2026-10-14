#include "powerpick.h"
#include "config.h"

// Not built for aplite: see config.h.
#if !PEGBLE_LITE
#include "abilities.h"
#include "haptics.h"
#include "levels.h"
#include "play.h"
#include "progress.h"
#include "ui.h"

#define ROWS (1 + ABILITY_COUNT) // Mix, then each ability in unlock order

static Window *s_window;
static MenuLayer *s_menu;
static PowerPicked s_done;
static void *s_context;

static const char *const s_blurbs[ABILITY_COUNT] = {
#if MAX_BALLS >= 5
    "Splits into three",
#else
    "Splits your ball",
#endif
    "Lights pegs nearby",
    "Burns through pegs",
    "Back from the bottom",
    "Pulls to the bucket"
};

static const char *const s_titles[ABILITY_COUNT] = {
    "Multiball", "Blast", "Fireball", "Spooky", "Magnet"
};

static uint8_t row_power(int row) {
    return row == 0 ? POWER_MIX : (uint8_t)(row - 1);
}

static bool row_open(int row) {
    return row == 0 || (progress_unlocked_powers() & (1 << (row - 1)));
}

static uint16_t num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
    return ROWS;
}

static int16_t header_height(MenuLayer *menu, uint16_t section, void *ctx) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    ui_draw_header(ctx, cell, "Choose a power");
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *data) {
    int row = index->row;
    char sub[28];
    if (row == 0) {
        menu_cell_basic_draw(ctx, cell, "Mix", "A different one per peg", ui_icon(ICON_MIX));
        return;
    }
    Ability a = (Ability)(row - 1);
    if (row_open(row)) {
        menu_cell_basic_draw(ctx, cell, s_titles[a], s_blurbs[a], ui_icon((IconId)a));
    } else {
        snprintf(sub, sizeof(sub), "Clear level %d", progress_power_unlock_level(a));
        menu_cell_basic_draw(ctx, cell, s_titles[a], sub, ui_icon(ICON_LOCK));
    }
}

static void remove_later(void *data) {
    if (s_window) {
        window_stack_remove(s_window, false);
    }
}

static void select_row(MenuLayer *menu, MenuIndex *index, void *ctx) {
    if (!row_open(index->row)) {
        haptics_play(HAPTIC_DENIED);
        return;
    }
    uint8_t power = row_power(index->row);
    progress_set_last_power(power);
    s_done(power, s_context);
    // Take this window out from under the game, so Back from the game returns
    // to the menu, not here. Not now, though: removing it destroys this
    // MenuLayer, and we're still inside its select callback.
    app_timer_register(50, remove_later, NULL);
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = num_rows,
        .get_header_height = header_height,
        .draw_header = draw_header,
        .draw_row = draw_row,
        .select_click = select_row
    });
#if defined(PBL_COLOR)
    menu_layer_set_highlight_colors(s_menu, GColorIslamicGreen, GColorWhite);
#endif
    menu_layer_set_click_config_onto_window(s_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_menu));

    uint8_t last = progress_last_power();
    int row = (last == POWER_MIX || last >= ABILITY_COUNT) ? 0 : last + 1;
    if (!row_open(row)) {
        row = 0;
    }
    menu_layer_set_selected_index(s_menu, (MenuIndex) { .section = 0, .row = row },
                                  MenuRowAlignCenter, false);
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu);
    s_menu = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void powerpick_push(PowerPicked done, void *context) {
    s_done = done;
    s_context = context;
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
