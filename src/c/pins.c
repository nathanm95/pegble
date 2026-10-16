#include "pins.h"
#include "config.h"

// Not built for aplite: see config.h.
#if !PEGGLE_LITE

// Keep in step with src/pkjs/index.js.
#define PIN_DAILY 1
#define PIN_ENDLESS 2

static bool s_open;

void pins_init(void) {
    if (!s_open) {
        // Outbound only, and only a handful of ints: tiny buffers.
        app_message_open(16, 64);
        s_open = true;
    }
}

static void send(int kind, int32_t score, int32_t a, int32_t b) {
    if (!s_open || !connection_service_peek_pebble_app_connection()) {
        return;
    }
    DictionaryIterator *it;
    if (app_message_outbox_begin(&it) != APP_MSG_OK) {
        return;
    }
    dict_write_int32(it, MESSAGE_KEY_PinKind, kind);
    dict_write_int32(it, MESSAGE_KEY_PinScore, score);
    dict_write_int32(it, MESSAGE_KEY_PinA, a);
    dict_write_int32(it, MESSAGE_KEY_PinB, b);
    app_message_outbox_send();
}

void pins_daily(uint16_t day, int32_t score, uint16_t streak) {
    send(PIN_DAILY, score, day, streak);
}

void pins_endless_record(int32_t score, uint16_t boards) {
    send(PIN_ENDLESS, score, boards, 0);
}

#endif
