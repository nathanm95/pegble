#pragma once

#include <pebble.h>

// "Choose a power" before a level or an endless run: Mix (a different power on
// each green peg, from those unlocked) or one power on every green peg. Locked
// powers are listed with the level that opens them, so the list doubles as the
// teaching order. Remembers the last pick, so Select-Select gets you playing.

typedef void (*PowerPicked)(uint8_t power, void *context);

void powerpick_push(PowerPicked done, void *context);
