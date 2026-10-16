#include "levels.h"

static const char *s_names[LEVEL_COUNT] = {
    "First Bounce",
    "The Funnel",
    "Ring Cycle",
    "Zigzag",
    "Diamond Mine",
    "The Wall",
#if !PEGGLE_LITE
    "Hourglass",
    "Pinball",
    "Conveyor",
    "Wormhole",
    "Spiral",
    "The Gauntlet"
#endif
};

const char *level_name(int index) {
    return (index >= 0 && index < LEVEL_COUNT) ? s_names[index] : "";
}

static int add(PegPos *out, int count, int nx, int ny, uint8_t kind, int move, uint8_t pair) {
    if (count >= MAX_PEGS) {
        return count;
    }
    out[count].nx = nx;
    out[count].ny = ny;
    out[count].kind = kind;
    out[count].move = move;
    out[count].pair = pair;
    return count + 1;
}

static int add_peg(PegPos *out, int count, int nx, int ny) {
    return add(out, count, nx, ny, PK_PEG, 0, 0);
}

// Evenly spaced horizontal row of n slots between x0 and x1. A row with travel
// moves as one piece; `phase` (0..255) sets where in its swing it starts, so two
// rows given 0 and 128 slide in opposite directions.
static int add_row_of(PegPos *out, int count, int n, int x0, int x1, int ny,
                      uint8_t kind, int travel, uint8_t phase) {
    for (int i = 0; i < n; i++) {
        int nx = (n == 1) ? (x0 + x1) / 2 : x0 + (x1 - x0) * i / (n - 1);
        count = add(out, count, nx, ny, kind, travel, phase);
    }
    return count;
}

static int add_row(PegPos *out, int count, int n, int x0, int x1, int ny) {
    return add_row_of(out, count, n, x0, x1, ny, PK_PEG, 0, 0);
}

// n slots along a line from (x0,y0) to (x1,y1).
static int add_line_of(PegPos *out, int count, int n, int x0, int y0, int x1, int y1,
                       uint8_t kind) {
    for (int i = 0; i < n; i++) {
        int t = (n == 1) ? 0 : 1000 * i / (n - 1);
        count = add(out, count, x0 + (x1 - x0) * t / 1000, y0 + (y1 - y0) * t / 1000,
                    kind, 0, 0);
    }
    return count;
}

static int add_line(PegPos *out, int count, int n, int x0, int y0, int x1, int y1) {
    return add_line_of(out, count, n, x0, y0, x1, y1, PK_PEG);
}

// n pegs on an arc centered at (cx,cy). Radii are in the same 0..1000 space;
// angles are in Pebble trig units.
static int add_arc(PegPos *out, int count, int n, int cx, int cy, int rx, int ry,
                   int32_t a0, int32_t a1) {
    for (int i = 0; i < n; i++) {
        int32_t a = a0 + (a1 - a0) * i / (n > 1 ? n - 1 : 1);
        int nx = cx + (int)(((int32_t)rx * sin_lookup(a)) / TRIG_MAX_RATIO);
        int ny = cy - (int)(((int32_t)ry * cos_lookup(a)) / TRIG_MAX_RATIO);
        count = add_peg(out, count, nx, ny);
    }
    return count;
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---- endless ----

#if !PEGGLE_LITE
// Turns `want` ordinary pegs from the slice [from, n) into furniture of `kind`.
// Picks are spread out by refusing anything within `gap` of a piece already
// placed, so two bumpers never sit touching.
static void convert_some(PegPos *out, int from, int n, int want, uint8_t kind, int gap,
                         Rng *rng) {
    int placed = 0;
    for (int tries = 0; tries < 60 && placed < want; tries++) {
        int i = from + rng_range(rng, n - from);
        if (out[i].kind != PK_PEG) {
            continue;
        }
        bool crowded = false;
        for (int j = 0; j < n && !crowded; j++) {
            if (out[j].kind == kind) {
                int dx = out[j].nx - out[i].nx, dy = out[j].ny - out[i].ny;
                crowded = dx * dx + dy * dy < gap * gap;
            }
        }
        if (!crowded) {
            out[i].kind = kind;
            placed++;
        }
    }
}
#endif

int level_build_endless(PegPos *out, int difficulty, Rng *rng) {
    int n = 0;
    if (difficulty < 0) {
        difficulty = 0;
    }
    // Board 1 is a loose pachinko field. Later boards add rows and fill more of
    // the gaps, so there is more to clear and less clean air to fall through.
    int rows = 7 + (difficulty >= 3) + (difficulty >= 7);
    int fill = clamp(72 + difficulty * 3, 0, 90);
    int spacing = 1000 / (rows + 1);

    for (int r = 0; r < rows; r++) {
        int cols = (r % 2 == 0) ? 7 : 6;
        int x0 = (r % 2 == 0) ? 70 : 145;
        for (int c = 0; c < cols; c++) {
            if (rng_range(rng, 100) >= fill) {
                continue;
            }
            int x = x0 + (1000 - 2 * x0) * c / (cols - 1) + rng_range(rng, 71) - 35;
            int y = 60 + r * spacing + rng_range(rng, 51) - 25;
            n = add_peg(out, n, clamp(x, 20, 980), clamp(y, 0, 1000));
        }
    }
    // Sparse rolls still need a playable board.
    int floor_count = clamp(24 + difficulty * 2, 0, MAX_PEGS - 4);
    while (n < floor_count) {
        n = add_peg(out, n, 60 + rng_range(rng, 881), 60 + rng_range(rng, 881));
    }

#if !PEGGLE_LITE
    // Furniture arrives one kind at a time. Nothing is placed in the top band,
    // so the first bounce off the launcher is always onto a real peg.
    int first_low = 0;
    while (first_low < n && out[first_low].ny < 200) {
        first_low++;
    }
    if (difficulty >= 4) {
        convert_some(out, first_low, n, 2, PK_BUMPER, 300, rng);
    }
    if (difficulty >= 6) {
        // One band of the board slides side to side as a unit.
        int band = 300 + rng_range(rng, 450);
        int travel = 60 + rng_range(rng, 61);
        uint8_t phase = rng_range(rng, 256);
        for (int i = 0; i < n; i++) {
            if (out[i].kind == PK_PEG && out[i].ny >= band && out[i].ny < band + 130) {
                out[i].move = travel;
                out[i].pair = phase;
            }
        }
    }
    if (difficulty >= 9) {
        // A portal low on the board sends the ball back up to one high on the
        // other side — a second life for a shot that was about to drain.
        int lo = -1, hi = -1;
        for (int tries = 0; tries < 40 && (lo < 0 || hi < 0); tries++) {
            int i = rng_range(rng, n);
            if (out[i].kind != PK_PEG || out[i].move) {
                continue;
            }
            if (lo < 0 && out[i].ny > 700) {
                lo = i;
            } else if (hi < 0 && out[i].ny > 150 && out[i].ny < 400) {
                hi = i;
            }
        }
        if (lo >= 0 && hi >= 0 && (out[lo].nx < 500) != (out[hi].nx < 500)) {
            out[lo].kind = out[hi].kind = PK_PORTAL;
            out[lo].pair = out[hi].pair = 1;
        }
    }
#endif
    return n;
}

// ---- hand-built levels ----

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
            // Four inside the funnel and one outside each wall, clear of the
            // diagonals (a straight row crossed them and overlapped on aplite).
            n = add_row(out, n, 4, 340, 660, 300);
            n = add_peg(out, n, 60, 300);
            n = add_peg(out, n, 940, 300);
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
            n = add_row(out, n, 4, 150, 850, 990);
            break;

        case 5: // The Wall: dense staggered grid
            for (int r = 0; r < 7; r++) {
                int y = 90 + r * 140;
                n = add_row(out, n, (r % 2 == 0) ? 8 : 7,
                            (r % 2 == 0) ? 60 : 130, (r % 2 == 0) ? 940 : 870, y);
            }
            break;

#if !PEGGLE_LITE
        case 6: // Hourglass: rows narrowing through a funnel onto the cluster below
            n = add_row(out, n, 7, 80, 920, 100);
            n = add_row(out, n, 6, 140, 860, 260);
            n = add_row(out, n, 7, 80, 920, 420);
            // The waist is wider than a ball on every screen, so it drains
            // instead of trapping balls even before its pegs are cleared.
            n = add_line(out, n, 4, 250, 560, 420, 760);
            n = add_line(out, n, 4, 750, 560, 580, 760);
            n = add_row(out, n, 3, 400, 600, 600);
            n = add_row(out, n, 3, 380, 620, 880);
            n = add_peg(out, n, 500, 980);
            n = add_peg(out, n, 80, 760);
            n = add_peg(out, n, 920, 760);
            n = add_row(out, n, 2, 60, 170, 920);
            n = add_row(out, n, 2, 830, 940, 920);
            break;

        case 7: // Pinball: five bumpers ringed with pegs
            n = add_row(out, n, 8, 60, 940, 80);
            n = add(out, n, 250, 330, PK_BUMPER, 0, 0);
            n = add(out, n, 750, 330, PK_BUMPER, 0, 0);
            n = add(out, n, 500, 560, PK_BUMPER, 0, 0);
            n = add(out, n, 180, 760, PK_BUMPER, 0, 0);
            n = add(out, n, 820, 760, PK_BUMPER, 0, 0);
            n = add_arc(out, n, 4, 250, 330, 150, 150, -TRIG_MAX_ANGLE / 4, TRIG_MAX_ANGLE / 4);
            n = add_arc(out, n, 4, 750, 330, 150, 150, -TRIG_MAX_ANGLE / 4, TRIG_MAX_ANGLE / 4);
            // Open at the top so a ball can drop straight onto the bumper.
            n = add_arc(out, n, 7, 500, 560, 170, 170, TRIG_MAX_ANGLE / 8,
                        TRIG_MAX_ANGLE * 7 / 8);
            n = add_row(out, n, 7, 80, 920, 940);
            break;

        case 8: // Conveyor: rows sliding against each other
            // Moving rows are inset by their travel so they never leave the board.
            n = add_row_of(out, n, 7, 140, 860, 160, PK_PEG, 120, 0);
            n = add_row(out, n, 6, 140, 860, 320);
            n = add_row_of(out, n, 6, 170, 830, 470, PK_PEG, 150, 128);
            n = add_row(out, n, 7, 80, 920, 620);
            n = add_row_of(out, n, 7, 120, 880, 790, PK_PEG, 100, 64);
            n = add_row(out, n, 5, 200, 800, 950);
            break;

        case 9: // Wormhole: two pockets fenced off by pegs, quickest reached by portal
            n = add(out, n, 140, 140, PK_PORTAL, 0, 1);
            n = add(out, n, 860, 720, PK_PORTAL, 0, 1);
            n = add(out, n, 860, 140, PK_PORTAL, 0, 2);
            n = add(out, n, 140, 720, PK_PORTAL, 0, 2);
            n = add_line(out, n, 4, 330, 560, 330, 900);
            n = add_line(out, n, 4, 670, 560, 670, 900);
            n = add_row(out, n, 5, 300, 700, 100);
            n = add_row(out, n, 7, 80, 920, 300);
            n = add_row(out, n, 6, 140, 860, 450);
            n = add_row(out, n, 3, 80, 250, 860);
            n = add_row(out, n, 3, 750, 920, 860);
            n = add_row(out, n, 2, 120, 230, 600);
            n = add_row(out, n, 2, 770, 880, 600);
            n = add_peg(out, n, 500, 650);
            n = add_row(out, n, 2, 430, 570, 820);
            break;

        case 10: { // Spiral: two turns around a bumper
            // Starts at r=180: any tighter and neighbours touch on aplite, where
            // the peg area is squashed vertically.
            for (int i = 0; i < 26; i++) {
                int32_t a = (int32_t)TRIG_MAX_ANGLE * 2 * i / 25;
                int r = 180 + 300 * i / 25;
                int nx = 500 + (int)(((int32_t)r * sin_lookup(a)) / TRIG_MAX_RATIO);
                int ny = 500 - (int)(((int32_t)r * cos_lookup(a)) / TRIG_MAX_RATIO);
                n = add_peg(out, n, clamp(nx, 20, 980), clamp(ny, 0, 1000));
            }
            n = add(out, n, 500, 500, PK_BUMPER, 0, 0);
            break;
        }

        case 11: // The Gauntlet: a bit of everything
            n = add_row_of(out, n, 7, 120, 880, 100, PK_PEG, 100, 0);
            n = add_row(out, n, 5, 260, 740, 260);
            n = add(out, n, 120, 320, PK_BUMPER, 0, 0);
            n = add(out, n, 880, 320, PK_BUMPER, 0, 0);
            n = add_line(out, n, 4, 80, 440, 420, 520);
            n = add_line(out, n, 4, 920, 600, 580, 680);
            n = add_peg(out, n, 500, 520);
            n = add_peg(out, n, 430, 600);
            n = add_peg(out, n, 570, 600);
            n = add_peg(out, n, 500, 720);
            n = add_row(out, n, 3, 650, 900, 450);
            n = add_row(out, n, 3, 100, 350, 640);
            n = add(out, n, 90, 860, PK_PORTAL, 0, 1);
            n = add(out, n, 910, 200, PK_PORTAL, 0, 1);
            n = add_row(out, n, 6, 250, 750, 800);
            n = add_row_of(out, n, 5, 200, 800, 950, PK_PEG, 120, 128);
            break;
#endif
    }
    return n;
}

static const uint8_t s_target_pct[LEVEL_COUNT] = {
    25, 25, 25, 25, 25, 25,  // the original six, as they always were
#if !PEGGLE_LITE
    26, 52, 34, 34, 40, 38
#endif
};

int level_target_pct(int index) {
    return (index >= 0 && index < LEVEL_COUNT) ? s_target_pct[index] : 25;
}

// Pars from `tools/host/run.sh par 800` (emery): 2 stars sits around the
// median winning score of a simulated player who aims at random, 3 stars near
// its top tenth, so a player who aims on purpose clears 2 stars comfortably and
// has to work for 3.
static const int32_t s_par[LEVEL_COUNT][2] = {
    { 14500, 21500 }, // First Bounce
    { 14000, 19500 }, // The Funnel
    { 15500, 21000 }, // Ring Cycle
    { 13500, 21000 }, // Zigzag
    { 12000, 18500 }, // Diamond Mine
    { 24000, 32000 }, // The Wall
#if !PEGGLE_LITE
    { 15500, 22500 }, // Hourglass
    { 17500, 24000 }, // Pinball
    { 15500, 21000 }, // Conveyor
    { 19000, 27000 }, // Wormhole
    { 13500, 20000 }, // Spiral
    { 17000, 23500 }  // The Gauntlet
#endif
};

int32_t level_par(int index, int stars) {
    if (index < 0 || index >= LEVEL_COUNT || stars < 2 || stars > 3) {
        return 0;
    }
    return s_par[index][stars - 2];
}
