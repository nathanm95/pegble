// persist_* for the Mac harness: a small in-memory key/value table with the
// watch's limits (256 bytes per value), so progress.c behaves as on a watch.
#include "pebble.h"

#define SLOTS 256

typedef struct {
    bool used;
    uint32_t key;
    uint16_t len;
    uint8_t data[PERSIST_DATA_MAX_LENGTH];
} Slot;

static Slot s_slots[SLOTS];

static Slot *find(uint32_t key, bool create) {
    for (int i = 0; i < SLOTS; i++) {
        if (s_slots[i].used && s_slots[i].key == key) {
            return &s_slots[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (int i = 0; i < SLOTS; i++) {
        if (!s_slots[i].used) {
            s_slots[i].used = true;
            s_slots[i].key = key;
            s_slots[i].len = 0;
            return &s_slots[i];
        }
    }
    fprintf(stderr, "host persist: out of slots\n");
    exit(2);
}

void host_persist_reset(void) {
    memset(s_slots, 0, sizeof(s_slots));
}

bool persist_exists(uint32_t key) {
    return find(key, false) != NULL;
}

int32_t persist_read_int(uint32_t key) {
    Slot *s = find(key, false);
    int32_t v = 0;
    if (s && s->len == sizeof(v)) {
        memcpy(&v, s->data, sizeof(v));
    }
    return v;
}

int persist_write_int(uint32_t key, int32_t value) {
    return persist_write_data(key, &value, sizeof(value));
}

int persist_read_data(uint32_t key, void *buffer, size_t size) {
    Slot *s = find(key, false);
    if (!s) {
        return -1; // E_DOES_NOT_EXIST on the watch is negative too
    }
    size_t n = s->len < size ? s->len : size;
    memcpy(buffer, s->data, n);
    return (int)n;
}

int persist_write_data(uint32_t key, const void *data, size_t size) {
    if (size > PERSIST_DATA_MAX_LENGTH) {
        fprintf(stderr, "host persist: key %u write of %zu bytes exceeds the watch limit\n",
                key, size);
        exit(2);
    }
    Slot *s = find(key, true);
    memcpy(s->data, data, size);
    s->len = (uint16_t)size;
    return (int)size;
}

int persist_delete(uint32_t key) {
    Slot *s = find(key, false);
    if (s) {
        s->used = false;
    }
    return 0;
}
