// Host-side stand-in for <pebble.h>: just enough for the rules (play.c,
// levels.c, progress.c) to compile on the Mac, so they can be simulated and
// tested there instead of in the emulator. Nothing that draws is compiled.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TRIG_MAX_ANGLE 0x10000
#define TRIG_MAX_RATIO 0xffff

static inline int32_t sin_lookup(int32_t angle) {
    return (int32_t)lround(sin((double)angle * 2 * M_PI / TRIG_MAX_ANGLE) * TRIG_MAX_RATIO);
}
static inline int32_t cos_lookup(int32_t angle) {
    return (int32_t)lround(cos((double)angle * 2 * M_PI / TRIG_MAX_ANGLE) * TRIG_MAX_RATIO);
}

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
#define GPoint(x, y) ((GPoint){ (int16_t)(x), (int16_t)(y) })
#define GSize(w, h) ((GSize){ (int16_t)(w), (int16_t)(h) })
#define GRect(x, y, w, h) ((GRect){ { (int16_t)(x), (int16_t)(y) }, { (int16_t)(w), (int16_t)(h) } })

typedef union { uint8_t argb; } GColor8;
typedef GColor8 GColor;
typedef struct GContext GContext;

#define PBL_IF_COLOR_ELSE(a, b) (a)
#define PBL_IF_ROUND_ELSE(a, b) (b)

#define APP_LOG_LEVEL_DEBUG 0
#define APP_LOG_LEVEL_INFO 1
#define APP_LOG(level, fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

// persist_* backed by an in-memory table, so progress.c can be exercised.
#define PERSIST_DATA_MAX_LENGTH 256
bool persist_exists(uint32_t key);
int32_t persist_read_int(uint32_t key);
int persist_write_int(uint32_t key, int32_t value);
int persist_read_data(uint32_t key, void *buffer, size_t size);
int persist_write_data(uint32_t key, const void *data, size_t size);
int persist_delete(uint32_t key);
void host_persist_reset(void);
