// Mac-side simulator and test suite for the rules in src/c/play.c.
//
//   tools/host/run.sh test      every check below; exits non-zero on failure
//   tools/host/run.sh par       simulate each level and print the star pars
//   tools/host/run.sh stats     how often fevers, combos, style shots happen
//
// A "player" here aims uniformly at random. That is a weak player, which is
// the point for pars: a human who aims on purpose should beat its median.
#include "pebble.h"
#include "play.h"
#include "progress.h"

static int s_fails;
#define CHECK(cond, ...) do { \
    if (!(cond)) { s_fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { const char *name; GRect bounds; bool round; } Screen;
static const Screen SCREENS[] = {
    { "emery",  { { 0, 0 }, { 200, 228 } }, false },
    { "gabbro", { { 0, 0 }, { 260, 260 } }, true },
    { "aplite", { { 0, 0 }, { 144, 168 } }, false },
};
#define SCREEN_COUNT 3

static Rng s_aim_rng;

typedef struct {
    bool won, lost;
    int32_t score;
    int shots, ticks;
    int fevers, fever_buckets, combos, long_shots, off_walls, buckets;
    int abilities[ABILITY_COUNT];
    int stuck, aged_out, boards, portals, bumpers, spooky;
    int fever_before_win; // 1 if EV_FEVER came before EV_WON
} Result;

static void drain(Result *r) {
    PlayEvent e;
    while (play_next_event(&e)) {
        switch (e.type) {
            case EV_FEVER: r->fevers++; break;
            case EV_FEVER_BUCKET: r->fever_buckets++; break;
            case EV_COMBO: r->combos++; break;
            case EV_STYLE:
                if (e.a == STYLE_LONG_SHOT) r->long_shots++;
                if (e.a == STYLE_OFF_THE_WALL) r->off_walls++;
                break;
            case EV_BUCKET: r->buckets++; break;
            case EV_ABILITY: if (e.a < ABILITY_COUNT) r->abilities[e.a]++; break;
            case EV_STUCK: r->stuck++; break;
            case EV_BOARD_CLEAR: r->boards = e.value; break;
            case EV_PORTAL: r->portals++; break;
            case EV_BUMPER: r->bumpers++; break;
            case EV_SPOOKY: r->spooky++; break;
            case EV_WON: r->fever_before_win = r->fevers > 0; break;
            default: break;
        }
    }
}

// Plays one game to the end (or max_shots), aiming at random.
static Result play_game(const PlayConfig *cfg, int max_shots) {
    Result r;
    memset(&r, 0, sizeof(r));
    play_start(cfg);
    drain(&r);
    const Play *p = play_get();
    bool was_active[MAX_BALLS] = { 0 };
    while (p->state != STATE_WON && p->state != STATE_LOST && r.shots < max_shots) {
        if (p->state == STATE_AIMING) {
            int32_t lim = play_aim_limit();
            play_set_aim(rng_range(&s_aim_rng, 2 * lim + 1) - lim);
            if (play_fire()) {
                r.shots++;
            }
        }
        play_tick();
        r.ticks++;
        for (int i = 0; i < MAX_BALLS; i++) {
            // A ball that leaves play at the age limit was retired, not finished.
            if (was_active[i] && !p->balls[i].active && p->balls[i].age > 900) {
                r.aged_out++;
            }
            was_active[i] = p->balls[i].active;
        }
        drain(&r);
        if (r.ticks > 400000) {
            printf("  game never ended (mode %d level %d)\n", cfg->mode, cfg->level);
            s_fails++;
            break;
        }
    }
    r.won = p->state == STATE_WON;
    r.lost = p->state == STATE_LOST;
    r.score = p->score;
    return r;
}

static PlayConfig level_cfg(int level) {
    PlayConfig c = { .mode = MODE_LEVEL, .level = level, .power = POWER_MIX,
                     .unlocked_powers = 0x1f };
    return c;
}

// ---- tests ----

static void test_levels_build(const Screen *s) {
    play_layout(s->bounds, s->round);
    for (int lv = 0; lv < LEVEL_COUNT; lv++) {
        PlayConfig c = level_cfg(lv);
        play_start(&c);
        const Play *p = play_get();
        const GRect f = p->field;
        CHECK(p->peg_count > 10 && p->peg_count <= MAX_PEGS, "%s %s: %d pegs",
              s->name, level_name(lv), p->peg_count);
        int targets = 0, powers = 0, clearable = 0;
        for (int i = 0; i < p->peg_count; i++) {
            const Peg *g = &p->pegs[i];
            int lo = g->base_x - g->move - p->peg_r, hi = g->base_x + g->move + p->peg_r;
            CHECK(lo >= f.origin.x && hi <= f.origin.x + f.size.w
                  && g->y - p->peg_r >= f.origin.y && g->y + p->peg_r <= f.origin.y + f.size.h,
                  "%s %s: peg %d off the field", s->name, level_name(lv), i);
            if (g->type == PEG_TARGET) targets++;
            if (g->type == PEG_POWER) powers++;
            if (g->type <= PEG_POWER) clearable++;
            if (g->type == PEG_PORTAL) {
                CHECK(g->link >= 0 && p->pegs[g->link].link == i,
                      "%s %s: portal %d not paired", s->name, level_name(lv), i);
            }
        }
        CHECK(targets == p->target_total && targets >= 5, "%s %s: %d targets (total %d)",
              s->name, level_name(lv), targets, p->target_total);
        CHECK(powers >= 1 && powers <= 3, "%s %s: %d power pegs", s->name, level_name(lv), powers);
        CHECK(clearable >= targets + powers, "%s %s: clearable", s->name, level_name(lv));

        // No two slots may sit on top of each other — not even a moving row
        // passing a still one. Sample the swing at 32 points.
        int overlaps = 0;
        int min_gap = 2 * p->peg_r;
        for (int i = 0; i < p->peg_count; i++) {
            for (int j = i + 1; j < p->peg_count; j++) {
                const Peg *a = &p->pegs[i], *b = &p->pegs[j];
                int steps = (a->move || b->move) ? 32 : 1;
                for (int k = 0; k < steps; k++) {
                    int32_t ph = k * (TRIG_MAX_ANGLE / 32);
                    int ax = a->base_x + (int)((int32_t)a->move * sin_lookup(ph + a->phase * 256) / TRIG_MAX_RATIO);
                    int bx = b->base_x + (int)((int32_t)b->move * sin_lookup(ph + b->phase * 256) / TRIG_MAX_RATIO);
                    int dx = ax - bx, dy = a->y - b->y;
                    if (dx * dx + dy * dy < min_gap * min_gap) {
                        overlaps++;
                        break;
                    }
                }
            }
        }
        CHECK(overlaps == 0, "%s %s: %d overlapping peg pairs", s->name, level_name(lv), overlaps);
    }
}

static void test_random_games(const Screen *s, int games) {
    play_layout(s->bounds, s->round);
    for (int lv = 0; lv < LEVEL_COUNT; lv++) {
        int won = 0, aged = 0, balls = 0, no_fever = 0, no_bucket = 0;
        for (int g = 0; g < games; g++) {
            PlayConfig c = level_cfg(lv);
            Result r = play_game(&c, 100);
            CHECK(r.won || r.lost, "%s %s: game %d didn't finish", s->name, level_name(lv), g);
            won += r.won;
            aged += r.aged_out;
            balls += r.shots;
            if (r.won && !r.fever_before_win) no_fever++;
            if (r.won && r.fever_buckets == 0 && r.aged_out == 0) no_bucket++;
        }
        CHECK(no_fever == 0, "%s %s: %d wins without a fever", s->name, level_name(lv), no_fever);
        CHECK(no_bucket == 0, "%s %s: %d wins without a fever bucket", s->name, level_name(lv), no_bucket);
        // Balls retired by the 30s safety limit mean unstick() didn't free them
        // (or a ball bounced about for that long, which should be just as rare).
        CHECK(aged * 300 <= balls, "%s %s: %d of %d balls hit the age limit",
              s->name, level_name(lv), aged, balls);
        printf("  %-6s %-14s won %3d%%  aged-out %d/%d\n", s->name, level_name(lv),
               won * 100 / games, aged, balls);
    }
}

static bool same_board(const Play *a, const Play *b) {
    if (a->peg_count != b->peg_count) return false;
    for (int i = 0; i < a->peg_count; i++) {
        const Peg *x = &a->pegs[i], *y = &b->pegs[i];
        if (x->type != y->type || x->x != y->x || x->y != y->y || x->active != y->active
            || (x->type == PEG_POWER && x->ability != y->ability)) {
            return false;
        }
    }
    return true;
}

static void test_daily(void) {
    play_layout(SCREENS[0].bounds, false);
    static Play a, b;
    PlayConfig c = { .mode = MODE_DAILY, .day = 1234, .power = POWER_MIX, .unlocked_powers = 1 };
    play_start(&c);
    a = *play_get();
    play_start(&c);
    b = *play_get();
    CHECK(same_board(&a, &b), "the same day built two different boards");
    c.day = 1235;
    play_start(&c);
    b = *play_get();
    CHECK(!same_board(&a, &b), "two days built the same board");
    // Everyone gets the day's one power, whatever they have unlocked.
    for (int i = 0; i < b.peg_count; i++) {
        if (b.pegs[i].type == PEG_POWER) {
            CHECK(b.pegs[i].ability == (1235 * 7) % ABILITY_COUNT, "daily power is not the day's");
        }
    }
}

static void test_powers(void) {
    play_layout(SCREENS[0].bounds, false);
    PlayConfig c = level_cfg(0);
    c.power = ABILITY_BLAST;
    play_start(&c);
    const Play *p = play_get();
    for (int i = 0; i < p->peg_count; i++) {
        if (p->pegs[i].type == PEG_POWER) {
            CHECK(p->pegs[i].ability == ABILITY_BLAST, "chosen power not dealt");
        }
    }
    c.power = POWER_MIX;
    c.unlocked_powers = 1 << ABILITY_MULTIBALL;
    play_start(&c);
    for (int i = 0; i < p->peg_count; i++) {
        if (p->pegs[i].type == PEG_POWER) {
            CHECK(p->pegs[i].ability == ABILITY_MULTIBALL, "mix dealt a locked power");
        }
    }
    c.unlocked_powers = 0x1f;
    play_start(&c);
    int seen = 0;
    for (int i = 0; i < p->peg_count; i++) {
        if (p->pegs[i].type == PEG_POWER) {
            CHECK(!(seen & (1 << p->pegs[i].ability)), "mix dealt the same power twice");
            seen |= 1 << p->pegs[i].ability;
        }
    }
}

static void test_snapshot(void) {
    play_layout(SCREENS[1].bounds, true);
    PlayConfig c = { .mode = MODE_ENDLESS, .power = POWER_MIX, .unlocked_powers = 0x1f,
                     .seed = 777 };
    play_start(&c);
    const Play *p = play_get();
    int shots = 0;
    while (shots < 4 && p->state != STATE_LOST) {
        if (p->state == STATE_AIMING) {
            play_set_aim(rng_range(&s_aim_rng, 2001) - 1000);
            play_fire();
            shots++;
        }
        play_tick();
        PlayEvent e;
        while (play_next_event(&e)) {}
    }
    while (p->state != STATE_AIMING && p->state != STATE_LOST) {
        play_tick();
    }
    if (p->state == STATE_LOST) {
        return;
    }
    static Play before;
    before = *p;
    RunSnapshot snap;
    play_snapshot(&snap);
    CHECK(sizeof(snap) <= PERSIST_DATA_MAX_LENGTH, "snapshot too big for a persist key");
    play_start(&c); // scramble the state
    CHECK(play_restore(&snap), "restore refused a fresh snapshot");
    CHECK(same_board(&before, p), "restored board differs");
    CHECK(before.score == p->score && before.balls_left == p->balls_left
          && before.boards == p->boards && before.bonus_idx == p->bonus_idx
          && before.target_cleared == p->target_cleared && before.theme == p->theme,
          "restored run state differs");
    snap.version = 0;
    CHECK(!play_restore(&snap), "restore accepted a stale snapshot");
}

static void test_endless_furniture(void) {
#if PEGBLE_LITE
    return; // aplite brings no furniture in at all
#endif
    // Furniture arrives one kind at a time, never before its board.
    static const int first[] = { 0, 4, 9 }; // PK_PEG, bumper, portal
    for (int d = 0; d <= 14; d++) {
        int counts[3] = { 0 }, moving = 0;
        for (int seed = 1; seed <= 40; seed++) {
            PegPos pos[MAX_PEGS];
            Rng r;
            rng_seed(&r, seed * 7919u);
            int n = level_build_endless(pos, d, &r);
            for (int i = 0; i < n; i++) {
                counts[pos[i].kind]++;
                moving += pos[i].move != 0;
            }
        }
        for (int k = 1; k <= 2; k++) {
            if (d < first[k]) {
                CHECK(counts[k] == 0, "difficulty %d already has furniture kind %d", d, k);
            } else {
                CHECK(counts[k] > 0, "difficulty %d never placed furniture kind %d", d, k);
            }
        }
        if (d < 6) CHECK(moving == 0, "difficulty %d already moves", d);
        else CHECK(moving > 0, "difficulty %d never moves", d);
    }
}

// ---- progress ----

static void test_progress(void) {
    host_persist_reset();
    progress_load();
    CHECK(progress_unlocked_level() == 0, "fresh save should unlock level 1 only");
    CHECK(progress_unlocked_powers() == (1 << ABILITY_MULTIBALL), "fresh save powers %x",
          progress_unlocked_powers());

    // Winning level 1 unlocks level 2 and the second power, and records stars.
    play_layout(SCREENS[0].bounds, false);
    PlayConfig c = level_cfg(0);
    Result r;
    int guard = 0;
    do {
        progress_begin_game();
        play_start(&c);
        const Play *p = play_get();
        while (p->state != STATE_WON && p->state != STATE_LOST) {
            if (p->state == STATE_AIMING) {
                play_set_aim(rng_range(&s_aim_rng, 2001) - 1000);
                play_fire();
            }
            play_tick();
            PlayEvent e;
            while (play_next_event(&e)) {
                progress_on_event(&e, p);
            }
        }
        r.won = p->state == STATE_WON;
        r.score = p->score;
    } while (!r.won && ++guard < 200);
    CHECK(r.won, "couldn't win level 1 in 200 tries");
    CHECK(progress_unlocked_level() >= 1, "win didn't unlock level 2");
    CHECK(progress_unlocked_powers() & (1 << ABILITY_BLAST), "win didn't unlock the second power");
    CHECK(progress_best(0) == r.score, "best %ld != score %ld", (long)progress_best(0), (long)r.score);
    CHECK(progress_stars(0) >= 1, "a clear is at least one star");
#if !PEGBLE_LITE
    CHECK(progress_has_trophy(TROPHY_FIRST_CLEAR), "no First Clear trophy");
    CHECK(progress_has_trophy(TROPHY_FEVER), "no Fever Pitch trophy");
    const Stats *st = progress_stats();
    CHECK(st->shots > 0 && st->pegs_cleared > 0 && st->fevers >= 1, "stats not counted");
#endif

    // A new session reads the same thing back from persist.
    int32_t best = progress_best(0);
    progress_load();
    CHECK(progress_best(0) == best && (PEGBLE_LITE || progress_has_trophy(TROPHY_FIRST_CLEAR)),
          "progress didn't survive a reload");

    // v2 players kept their best scores at the old keys; nothing may be lost.
    host_persist_reset();
    persist_write_int(1, 5);      // PERSIST_KEY_UNLOCKED: level 6 open
    persist_write_int(103, 4321); // level 4 best, v2 key
    persist_write_int(106, 9999); // endless best, v2 key
    persist_write_int(2, 7);      // endless boards
    progress_load();
    CHECK(progress_unlocked_level() == 5, "lost v2 unlocks");
    CHECK(progress_best(3) == 4321, "lost a v2 level best");
    CHECK(progress_endless_best() == 9999 && progress_endless_boards() == 7, "lost v2 endless records");
#if !PEGBLE_LITE
    CHECK(progress_best(6) == 0, "new level 7 read a v2 key");
#endif
    CHECK(progress_unlocked_powers() == 0x1f, "v2 players with 6 levels open should have every power");

    // Daily: a streak counts consecutive days, and breaks on a gap.
    host_persist_reset();
    progress_load();
    progress_daily_finished(500, 1200);
    progress_daily_finished(501, 900);
    progress_daily_finished(501, 1500);
    CHECK(progress_daily()->streak == 2 && progress_daily()->best_today == 1500,
          "daily streak %d best %ld", progress_daily()->streak, (long)progress_daily()->best_today);
    progress_daily_finished(503, 100);
    CHECK(progress_daily()->streak == 1 && progress_daily()->best_today == 100, "streak didn't break");

    // The saved run is one slot, written between shots and cleared at the end.
    RunSnapshot snap = { .version = RUN_SNAPSHOT_VERSION, .mode = MODE_ENDLESS, .boards = 3 };
    progress_save_run(&snap);
    RunSnapshot back;
    CHECK(progress_load_run(&back) && back.boards == 3, "saved run didn't come back");
    progress_clear_run();
    CHECK(!progress_load_run(&back), "cleared run still there");
}

// ---- par calibration ----

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

static void par(int games) {
    play_layout(SCREENS[0].bounds, false);
    static int32_t wins[4000];
    printf("static const int32_t s_par[LEVEL_COUNT][2] = {\n");
    for (int lv = 0; lv < LEVEL_COUNT; lv++) {
        int n = 0;
        for (int g = 0; g < games && n < 4000; g++) {
            PlayConfig c = level_cfg(lv);
            Result r = play_game(&c, 100);
            if (r.won) wins[n++] = r.score;
        }
        qsort(wins, n, sizeof(wins[0]), cmp_i32);
        int32_t p50 = n ? wins[n / 2] : 0, p90 = n ? wins[n * 9 / 10] : 0;
        // Round to a friendly number.
        p50 = (p50 + 250) / 500 * 500;
        p90 = (p90 + 250) / 500 * 500;
        printf("    { %6ld, %6ld }, // %-13s won %d/%d\n", (long)p50, (long)p90,
               level_name(lv), n, games);
    }
    printf("};\n");
}

static void stats(int games) {
    play_layout(SCREENS[1].bounds, true);
    Result sum;
    memset(&sum, 0, sizeof(sum));
    int n = 0;
    for (int lv = 0; lv < LEVEL_COUNT; lv++) {
        for (int g = 0; g < games; g++) {
            PlayConfig c = level_cfg(lv);
            Result r = play_game(&c, 100);
            n++;
            sum.shots += r.shots; sum.fevers += r.fevers; sum.combos += r.combos;
            sum.long_shots += r.long_shots; sum.off_walls += r.off_walls;
            sum.buckets += r.buckets; sum.stuck += r.stuck; sum.portals += r.portals;
            sum.bumpers += r.bumpers; sum.spooky += r.spooky; sum.won += r.won;
            for (int a = 0; a < ABILITY_COUNT; a++) sum.abilities[a] += r.abilities[a];
        }
    }
    printf("%d games, %d shots, %d wins\n", n, sum.shots, (int)sum.won);
    printf("per 100 shots: combos %.1f  long shots %.1f  off the wall %.1f  buckets %.1f  "
           "unstuck %.1f  portals %.1f  bumpers %.1f  spooky returns %.1f\n",
           100.0 * sum.combos / sum.shots, 100.0 * sum.long_shots / sum.shots,
           100.0 * sum.off_walls / sum.shots, 100.0 * sum.buckets / sum.shots,
           100.0 * sum.stuck / sum.shots, 100.0 * sum.portals / sum.shots,
           100.0 * sum.bumpers / sum.shots, 100.0 * sum.spooky / sum.shots);
    printf("abilities:");
    for (int a = 0; a < ABILITY_COUNT; a++) printf(" %d", sum.abilities[a]);
    printf("\n");
}

static void endless_run(int runs) {
    play_layout(SCREENS[0].bounds, false);
    int best = 0, total = 0;
    for (int i = 0; i < runs; i++) {
        PlayConfig c = { .mode = MODE_ENDLESS, .power = POWER_MIX, .unlocked_powers = 0x1f,
                         .seed = 1000u + i };
        Result r = play_game(&c, 400);
        CHECK(r.lost || r.shots >= 400, "endless run %d ended without losing", i);
        if (r.boards > best) best = r.boards;
        total += r.boards;
    }
    printf("  endless: %d runs, mean %.1f boards, best %d\n", runs, (double)total / runs, best);
}

// Plays games on one level until a ball reaches the age limit, then prints
// where it spent its last few seconds — to see what "wedged" looks like.
static void aged(int level, int screen) {
    play_layout(SCREENS[screen].bounds, SCREENS[screen].round);
    const Play *p = play_get();
    for (int g = 0; g < 2000; g++) {
        PlayConfig c = level_cfg(level);
        play_start(&c);
        while (p->state != STATE_WON && p->state != STATE_LOST) {
            if (p->state == STATE_AIMING) {
                int32_t lim = play_aim_limit();
                play_set_aim(rng_range(&s_aim_rng, 2 * lim + 1) - lim);
                play_fire();
            }
            play_tick();
            PlayEvent e;
            while (play_next_event(&e)) {}
            for (int i = 0; i < MAX_BALLS; i++) {
                const Ball *b = &p->balls[i];
                if (b->active && b->age >= 780 && b->age % 15 == 0) {
                    printf("game %d ball %d age %d: at (%d,%d) v (%d,%d) spread %d kicks %d hops %d\n",
                           g, i, b->age, TO_PX(b->x), TO_PX(b->y), b->vx, b->vy,
                           b->stuck_spread, b->kicks, b->portal_hops);
                    if (b->age >= 900) {
                        for (int k = 0; k < p->peg_count; k++) {
                            const Peg *q = &p->pegs[k];
                            int dx = q->x - TO_PX(b->x), dy = q->y - TO_PX(b->y);
                            if (q->active && dx * dx + dy * dy < 30 * 30)
                                printf("   peg %d type %d at (%d,%d) lit %d\n", k, q->type, q->x, q->y, q->lit);
                        }
                        printf("   field %d,%d %dx%d\n", p->field.origin.x, p->field.origin.y,
                               p->field.size.w, p->field.size.h);
                        return;
                    }
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    rng_seed(&s_aim_rng, 12345);
    const char *cmd = argc > 1 ? argv[1] : "test";
    if (!strcmp(cmd, "winrate")) {
        int games = argc > 2 ? atoi(argv[2]) : 200;
        for (int sc = 0; sc < SCREEN_COUNT; sc++) {
            play_layout(SCREENS[sc].bounds, SCREENS[sc].round);
            printf("%-7s", SCREENS[sc].name);
            for (int lv = 0; lv < LEVEL_COUNT; lv++) {
                int won = 0;
                for (int g = 0; g < games; g++) {
                    PlayConfig c = level_cfg(lv);
                    won += play_game(&c, 100).won;
                }
                printf(" %3d", won * 100 / games);
            }
            printf("\n");
        }
        return 0;
    }
    if (!strcmp(cmd, "aged")) {
        aged(argc > 2 ? atoi(argv[2]) : 1, argc > 3 ? atoi(argv[3]) : 0);
        return 0;
    }
    if (!strcmp(cmd, "par")) {
        par(argc > 2 ? atoi(argv[2]) : 600);
        return 0;
    }
    if (!strcmp(cmd, "stats")) {
        stats(argc > 2 ? atoi(argv[2]) : 100);
        return 0;
    }
    printf("levels build cleanly on every screen\n");
    for (int i = 0; i < SCREEN_COUNT; i++) test_levels_build(&SCREENS[i]);
    printf("chosen powers and mix dealing\n");
    test_powers();
    printf("daily boards are the same for everyone, and differ by day\n");
    test_daily();
    printf("a saved run restores exactly\n");
    test_snapshot();
    printf("endless brings in furniture one kind at a time\n");
    test_endless_furniture();
    printf("progress, trophies, stats, daily, saved run\n");
    test_progress();
    printf("random games finish, fever on every win, no wedged balls\n");
    for (int i = 0; i < SCREEN_COUNT; i++) test_random_games(&SCREENS[i], argc > 2 ? atoi(argv[2]) : 150);
    printf("endless runs\n");
    endless_run(60);
    printf(s_fails ? "\n%d FAILURES\n" : "\nall passed\n", s_fails);
    return s_fails ? 1 : 0;
}
