#pragma once

#include <pebble.h>

// Persist keys shared with the menu (main file reads best scores / unlock state).
#define PERSIST_KEY_UNLOCKED 1
#define PERSIST_KEY_ENDLESS_BOARDS 2
#define PERSIST_KEY_BEST_BASE 100

// Pushes the game window and starts the given level (0-based).
void game_window_push(int level_index);

// Pushes the game window in endless score-attack mode: random boards that
// regenerate on clear, until the ball supply runs out.
void game_window_push_endless(void);
