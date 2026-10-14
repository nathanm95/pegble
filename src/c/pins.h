#pragma once

#include <pebble.h>

// Timeline pins for the moments worth remembering: a finished daily challenge
// and a new endless record. The watch only sends the numbers; the phone side
// (src/pkjs/index.js) builds the pin and hands it to the Pebble app, which
// keeps it as a local pin. Nothing happens if the phone isn't connected.

void pins_init(void);
void pins_daily(uint16_t day, int32_t score, uint16_t streak);
void pins_endless_record(int32_t score, uint16_t boards);
