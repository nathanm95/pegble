#include "levels.h"

static const char *s_names[LEVEL_COUNT] = {
    "First Bounce",
    "The Funnel",
    "Ring Cycle",
    "Zigzag",
    "Diamond Mine",
    "The Wall"
};

const char *level_name(int index) {
    return s_names[index];
}

static int add_peg(PegPos *out, int count, int nx, int ny) {
    if (count >= MAX_PEGS) {
        return count;
    }
    out[count].nx = nx;
    out[count].ny = ny;
    return count + 1;
}

// Evenly spaced horizontal row of n pegs between x0 and x1.
static int add_row(PegPos *out, int count, int n, int x0, int x1, int ny) {
    for (int i = 0; i < n; i++) {
        int nx = (n == 1) ? (x0 + x1) / 2 : x0 + (x1 - x0) * i / (n - 1);
        count = add_peg(out, count, nx, ny);
    }
    return count;
}

// n pegs along a line from (x0,y0) to (x1,y1).
static int add_line(PegPos *out, int count, int n, int x0, int y0, int x1, int y1) {
    for (int i = 0; i < n; i++) {
        int t = (n == 1) ? 0 : 1000 * i / (n - 1);
        count = add_peg(out, count, x0 + (x1 - x0) * t / 1000, y0 + (y1 - y0) * t / 1000);
    }
    return count;
}

// n pegs on an arc centered at (cx,cy). Radii are in the same 0..1000 space;
// angles are in Pebble trig units.
static int add_arc(PegPos *out, int count, int n, int cx, int cy, int rx, int ry,
                   int32_t a0, int32_t a1) {
    for (int i = 0; i < n; i++) {
        int32_t a = a0 + (a1 - a0) * i / (n > 1 ? n - 1 : 1);
        int nx = cx + (int)(((int64_t)rx * sin_lookup(a)) / TRIG_MAX_RATIO);
        int ny = cy - (int)(((int64_t)ry * cos_lookup(a)) / TRIG_MAX_RATIO);
        count = add_peg(out, count, nx, ny);
    }
    return count;
}

int level_build_endless(PegPos *out) {
    int n = 0;
    // Staggered rows with random gaps and jitter — pachinko, not a fixed puzzle.
    for (int r = 0; r < 7; r++) {
        int cols = (r % 2 == 0) ? 7 : 6;
        int x0 = (r % 2 == 0) ? 70 : 145;
        for (int c = 0; c < cols; c++) {
            if (rand() % 100 >= 72) {
                continue;
            }
            int x = x0 + (1000 - 2 * x0) * c / (cols - 1) + (rand() % 71) - 35;
            int y = 60 + r * 145 + (rand() % 51) - 25;
            if (x < 20) x = 20;
            if (x > 980) x = 980;
            if (y < 0) y = 0;
            if (y > 1000) y = 1000;
            n = add_peg(out, n, x, y);
        }
    }
    // Sparse rolls still need a playable board.
    while (n < 24) {
        n = add_peg(out, n, 60 + rand() % 881, 60 + rand() % 881);
    }
    return n;
}

int level_build(int index, PegPos *out) {
    int n = 0;
    switch (index) {
        default:
        case 0: // First Bounce: five staggered rows, the classic opener
            n = add_row(out, n, 7, 80, 920, 120);
            n = add_row(out, n, 6, 140, 860, 320);
            n = add_row(out, n, 7, 80, 920, 520);
            n = add_row(out, n, 6, 140, 860, 720);
            n = add_row(out, n, 7, 80, 920, 920);
            break;

        case 1: // The Funnel: two diagonals guiding into a bottom cluster
            n = add_line(out, n, 8, 40, 60, 420, 700);
            n = add_line(out, n, 8, 960, 60, 580, 700);
            n = add_row(out, n, 5, 300, 700, 900);
            n = add_row(out, n, 4, 360, 640, 780);
            n = add_row(out, n, 6, 100, 900, 300);
            break;

        case 2: // Ring Cycle: concentric rings around a center peg
            n = add_arc(out, n, 12, 500, 500, 260, 260, 0, TRIG_MAX_ANGLE * 11 / 12);
            n = add_arc(out, n, 18, 500, 500, 440, 440, TRIG_MAX_ANGLE / 36,
                        TRIG_MAX_ANGLE * 35 / 36);
            n = add_peg(out, n, 500, 500);
            n = add_row(out, n, 3, 60, 220, 60);
            n = add_row(out, n, 3, 780, 940, 60);
            n = add_row(out, n, 3, 60, 220, 950);
            n = add_row(out, n, 3, 780, 940, 950);
            break;

        case 3: // Zigzag: alternating diagonal shelves
            n = add_line(out, n, 8, 60, 80, 800, 280);
            n = add_line(out, n, 8, 940, 400, 200, 600);
            n = add_line(out, n, 8, 60, 720, 800, 920);
            n = add_row(out, n, 4, 250, 750, 990);
            break;

        case 4: // Diamond Mine: four diamond clusters
            for (int d = 0; d < 4; d++) {
                int cx = (d % 2 == 0) ? 280 : 720;
                int cy = 200 + (d / 2) * 500 + ((d % 2) * 130);
                n = add_peg(out, n, cx, cy - 130);
                n = add_peg(out, n, cx - 130, cy);
                n = add_peg(out, n, cx + 130, cy);
                n = add_peg(out, n, cx, cy + 130);
                n = add_peg(out, n, cx, cy);
            }
            n = add_row(out, n, 5, 150, 850, 990);
            break;

        case 5: // The Wall: dense staggered grid
            for (int r = 0; r < 7; r++) {
                int y = 90 + r * 140;
                n = add_row(out, n, (r % 2 == 0) ? 8 : 7,
                            (r % 2 == 0) ? 60 : 130, (r % 2 == 0) ? 940 : 870, y);
            }
            break;
    }
    return n;
}
