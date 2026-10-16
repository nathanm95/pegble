#include <pebble.h>
#include "game.h"
#include "haptics.h"
#include "levels.h"
#include "pins.h"
#include "powerpick.h"
#include "progress.h"
#include "records.h"
#include "theme.h"
#include "ui.h"

// Menu sections. Aplite has no trophies screen (see config.h), so no "More".
#if PEGGLE_LITE
enum { SEC_PLAY, SEC_LEVELS, SEC_COUNT, SEC_MORE };
#else
enum { SEC_PLAY, SEC_LEVELS, SEC_MORE, SEC_COUNT };
#endif
enum { PLAY_CONTINUE, PLAY_DAILY, PLAY_ENDLESS };

#define PICK_ENDLESS -1

static Window *s_menu_window;
static MenuLayer *s_menu_layer;
static bool s_has_run;
static RunSnapshot s_run;
static int s_pick_target; // level index, or PICK_ENDLESS, while the power picker is up

static int play_row_kind(int row) {
    return s_has_run ? row : row + 1;
}

static uint16_t num_sections(MenuLayer *menu, void *ctx) {
    return SEC_COUNT;
}

static uint16_t num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
    switch (section) {
        case SEC_PLAY:   return s_has_run ? 3 : 2;
        case SEC_LEVELS: return LEVEL_COUNT;
        default:         return 1;
    }
}

static int16_t header_height(MenuLayer *menu, uint16_t section, void *ctx) {
    return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    static const char *const names[] = { "Play", "Levels", "More" };
    ui_draw_header(ctx, cell, names[section]);
}

static void draw_play_row(GContext *ctx, const Layer *cell, int kind) {
    char sub[40], num[16];
    uint16_t today = progress_today();
    switch (kind) {
        case PLAY_CONTINUE:
            if (s_run.mode == MODE_ENDLESS) {
                snprintf(sub, sizeof(sub), "Endless, board %d", s_run.boards + 1);
            } else if (s_run.mode == MODE_DAILY) {
                snprintf(sub, sizeof(sub), "Daily, %d balls", s_run.balls_left);
            } else {
                snprintf(sub, sizeof(sub), "%s, %d balls", level_name(s_run.level),
                         s_run.balls_left);
            }
            menu_cell_basic_draw(ctx, cell, "Continue", sub,
                                 s_run.mode == MODE_DAILY ? ui_icon(ICON_DAILY)
                                 : s_run.mode == MODE_ENDLESS ? ui_icon(ICON_ENDLESS) : NULL);
            break;
        case PLAY_DAILY: {
            // Subtitles stay within ~17 characters: on a 144px colour screen
            // the icon leaves room for no more, and the title shrinks to "Daily".
            const DailyRecord *d = progress_daily();
            GBitmap *icon = ui_icon(ICON_DAILY);
            bool narrow = icon && layer_get_bounds(cell).size.w < 160;
            if (progress_daily_done(today)) {
                ui_fmt_score(num, sizeof(num), d->best_today);
                snprintf(sub, sizeof(sub), "%s, streak %d", num, d->streak);
            } else if (d->streak > 0 && d->last_played + 1 == today) {
                snprintf(sub, sizeof(sub), "Keep streak %d", d->streak);
            } else {
                snprintf(sub, sizeof(sub), "New board daily");
            }
            menu_cell_basic_draw(ctx, cell, narrow ? "Daily" : "Daily challenge", sub, icon);
            break;
        }
        default:
            if (progress_endless_best() > 0) {
                ui_fmt_score(num, sizeof(num), progress_endless_best());
                snprintf(sub, sizeof(sub), "%s, %d boards", num, progress_endless_boards());
            } else {
                snprintf(sub, sizeof(sub), "Random boards");
            }
            menu_cell_basic_draw(ctx, cell, "Endless", sub, ui_icon(ICON_ENDLESS));
            break;
    }
}

static void draw_level_row(GContext *ctx, const Layer *cell, int level) {
    char title[32], sub[24], num[16];
    GRect b = layer_get_bounds(cell);
    bool open = level <= progress_unlocked_level();
    snprintf(title, sizeof(title), "%d. %s", level + 1, level_name(level));
    if (!open) {
        snprintf(sub, sizeof(sub), "Locked");
    } else if (progress_best(level) > 0) {
        ui_fmt_score(num, sizeof(num), progress_best(level));
        snprintf(sub, sizeof(sub), "Best %s", num);
    } else {
        snprintf(sub, sizeof(sub), "Not cleared");
    }
    menu_cell_basic_draw(ctx, cell, title, sub, NULL);
#if defined(PBL_COLOR)
    // A strip of the level's own table colour down the left edge.
    graphics_context_set_fill_color(ctx, theme_get(level)->bg);
    graphics_fill_rect(ctx, GRect(0, 2, 3, b.size.h - 4), 0, GCornerNone);
#endif
    if (open) {
        bool hl = menu_cell_layer_is_highlighted(cell);
        GColor on = PBL_IF_COLOR_ELSE(hl ? GColorYellow : GColorChromeYellow,
                                      hl ? GColorWhite : GColorBlack);
        GColor off = PBL_IF_COLOR_ELSE(hl ? GColorWhite : GColorDarkGray,
                                       hl ? GColorWhite : GColorBlack);
        ui_draw_stars(ctx, GPoint(b.size.w - PBL_IF_ROUND_ELSE(52, 24), b.size.h - 12),
                      progress_stars(level), 5, on, off);
    }
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *data) {
    char sub[32];
    switch (index->section) {
        case SEC_PLAY:
            draw_play_row(ctx, cell, play_row_kind(index->row));
            break;
        case SEC_LEVELS:
            draw_level_row(ctx, cell, index->row);
            break;
        default:
            snprintf(sub, sizeof(sub), "%d/%d trophies, %d stars", progress_trophy_count(),
                     TROPHY_COUNT, progress_total_stars());
            menu_cell_basic_draw(ctx, cell, "Trophies & stats", sub, ui_icon(ICON_TROPHY));
            break;
    }
}

static void power_picked(uint8_t power, void *context) {
    if (s_pick_target == PICK_ENDLESS) {
        game_window_push_endless(power);
    } else {
        game_window_push_level(s_pick_target, power);
    }
}

// The power picker only earns its extra press once there's a choice to make;
// until the first level is cleared there is only multiball.
static void pick_power_then(int target) {
    s_pick_target = target;
    uint8_t open = progress_unlocked_powers();
    if (PEGGLE_LITE || (open & (open - 1)) == 0) {
        power_picked(POWER_MIX, NULL);
    } else {
#if !PEGGLE_LITE
        powerpick_push(power_picked, NULL);
#endif
    }
}

static void select_row(MenuLayer *layer, MenuIndex *index, void *ctx) {
    switch (index->section) {
        case SEC_PLAY:
            switch (play_row_kind(index->row)) {
                case PLAY_CONTINUE:
                    if (!game_window_resume()) {
                        haptics_play(HAPTIC_DENIED);
                    }
                    break;
                case PLAY_DAILY:
                    game_window_push_daily();
                    break;
                default:
                    pick_power_then(PICK_ENDLESS);
                    break;
            }
            break;
        case SEC_LEVELS:
            if (index->row <= progress_unlocked_level()) {
                pick_power_then(index->row);
            } else {
                haptics_play(HAPTIC_DENIED);
            }
            break;
        default:
#if !PEGGLE_LITE
            records_push();
#endif
            break;
    }
}

static void refresh(void) {
    s_has_run = progress_load_run(&s_run);
    if (s_menu_layer) {
        menu_layer_reload_data(s_menu_layer);
    }
}

static void menu_window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_menu_layer = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
        .get_num_sections = num_sections,
        .get_num_rows = num_rows,
        .get_header_height = header_height,
        .draw_header = draw_header,
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
    // Refresh bests, stars, unlocks and the paused run after a game.
    refresh();
}

static void menu_window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
    s_menu_layer = NULL;
}

// ---- the launcher glance ----

#if defined(_PBL_API_EXISTS_app_glance_reload)
static void add_slice(AppGlanceReloadSession *session, const char *text, time_t expires) {
    AppGlanceSlice slice = {
        .layout = {
            .icon = APP_GLANCE_SLICE_DEFAULT_ICON,
            .subtitle_template_string = text
        },
        .expiration_time = expires
    };
    app_glance_add_slice(session, slice);
}

// What the launcher says under the app's name. A paused run comes first; then
// today's daily, which flips to "a new board" at midnight on its own.
static void glance(AppGlanceReloadSession *session, size_t limit, void *context) {
    char text[64], num[16];
    if (limit == 0) {
        return;
    }
    RunSnapshot run;
    if (progress_load_run(&run)) {
        if (run.mode == MODE_ENDLESS) {
            snprintf(text, sizeof(text), "Continue endless, board %d", run.boards + 1);
        } else if (run.mode == MODE_DAILY) {
            snprintf(text, sizeof(text), "Continue today's daily");
        } else {
            snprintf(text, sizeof(text), "Continue %s", level_name(run.level));
        }
        add_slice(session, text, APP_GLANCE_SLICE_NO_EXPIRATION);
        return;
    }
    const DailyRecord *d = progress_daily();
    if (progress_daily_done(progress_today()) && limit >= 2) {
        ui_fmt_score(num, sizeof(num), d->best_today);
        snprintf(text, sizeof(text), "Daily done: %s", num);
        add_slice(session, text, time_start_of_today() + SECONDS_PER_DAY);
    }
    add_slice(session, "Today's daily board is ready", APP_GLANCE_SLICE_NO_EXPIRATION);
}
#endif

int main(void) {
    // Third-party apps get no touch navigation unless they ask for it, and
    // without it the menus are button-only on a touchscreen watch. Opting in
    // lets the system map swipes/taps on a MenuLayer to Up/Down/Select; the
    // game window draws into a plain Layer, so the bridge produces nothing
    // there and game.c's own touch_handler still owns aiming.
    // Compiles to a no-op on platforms without a touchscreen.
    app_touch_navigation_enable(true);
    progress_load();
#if !PEGGLE_LITE
    pins_init();
#endif

    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) {
        .load = menu_window_load,
        .appear = menu_window_appear,
        .unload = menu_window_unload
    });
    window_stack_push(s_menu_window, false);

#if defined(PEGGLE_BOOT)
    // Debug builds can start on a particular screen; see wscript.
    switch (PEGGLE_BOOT) {
        case 1: game_window_push_level(PEGGLE_LEVEL, POWER_MIX); break;
        case 2: game_window_push_endless(POWER_MIX); break;
        case 3: game_window_push_daily(); break;
        case 6: game_window_resume(); break;
#if !PEGGLE_LITE
        case 4: records_push(); break;
        case 5: s_pick_target = PEGGLE_LEVEL; powerpick_push(power_picked, NULL); break;
#endif
        default: break;
    }
#endif

    app_event_loop();

    progress_flush();
#if defined(_PBL_API_EXISTS_app_glance_reload)
    app_glance_reload(glance, NULL);
#endif
    ui_icons_release();
    window_destroy(s_menu_window);
}
