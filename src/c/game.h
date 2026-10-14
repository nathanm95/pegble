#pragma once

#include <pebble.h>
#include "play.h"

// The game window. Each launcher starts a fresh run except game_window_resume,
// which picks up the paused one (progress_load_run must have one).
void game_window_push_level(int level, uint8_t power);
void game_window_push_endless(uint8_t power);
void game_window_push_daily(void);
bool game_window_resume(void);
