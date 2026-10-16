#!/usr/bin/env python3
"""Build the appstore image set from tools/record_store.py recordings.

Reads <rec_dir>/<platform>_{demo,level,menu}/ and writes into store/:

  <platform>_1_preview.gif   level 1 played through a fever to the results panel
  <platform>_2_level.png     a new level mid-shot (Pinball; Ring Cycle on aplite)
  <platform>_3_fever.png     Extreme Fever, with the bonus buckets out
  <platform>_4_clear.png     the results panel and its stars
  <platform>_5_menu.png      the menu
  icon_80x80.png, icon_144x144.png   store icons
  banner_720x320.png                 store header image

The recordings come from a debug build with PEGGLE_BEACON: twelve pixels at the
top of every frame spell out the game state, the fever flag and the low byte of
the game tick. That decides everything here — where the GIF starts and stops,
how long each frame shows (game ticks x 33ms, so it plays at the watch's
speed however slowly the emulator was recorded), and which frames make the
stills. The beacon is painted out of every image written.

Screenshot filenames start with the platform because `pebble publish` infers
the platform from the text before the first underscore.

Usage: store_assets.py <rec_dir>
"""
import glob, json, os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import full_scale, snap_table  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'store')
BW = {'aplite', 'diorite', 'flint'}
ROUND = {'chalk', 'gabbro'}
NATIVE = {'aplite': (144, 168), 'basalt': (144, 168), 'chalk': (180, 180),
          'diorite': (144, 168), 'emery': (200, 228), 'flint': (144, 168),
          'gabbro': (260, 260)}

TICK_MS = 33               # game.c TICK_MS
STATE_AIMING, STATE_FLYING, STATE_RESOLVING, STATE_WON, STATE_LOST = range(5)
MAX_GIF = 4_300_000        # the store's limit is 4.4MB
BUCKET = os.path.join(ROOT, 'resources', 'images', 'bucket_big.png')
BUCKET_SMALL = os.path.join(ROOT, 'resources', 'images', 'bucket_small.png')
ROUNDED = '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf'

# The game's own colours (theme 0, "Midnight", and the fixed peg colours).
NAVY, BLUE, BLUE_HI = (0, 0, 85), (0, 85, 255), (85, 170, 255)
ORANGE, ORANGE_HI = (255, 85, 0), (255, 170, 85)
PINK, GREEN = (255, 0, 170), (0, 170, 85)
YELLOW, WHITE, CYAN = (255, 255, 0), (255, 255, 255), (0, 255, 255)


# ----------------------------------------------------------------- frames --

def normalise(im, platform):
    """Snap anything still on the emulator's reduced scale to the watch's
    0/85/170/255 levels, the same way shot.py does."""
    levels = sorted({v for band in im.split() for _, v in band.getcolors(256)} - {0})
    if not levels or set(levels) <= {85, 170, 255}:
        return im
    table = list(snap_table(full_scale(levels, platform)))
    return im.point(table * 3)


BEACON_BITS = 15


def beacon_xy(platform):
    w, _ = NATIVE[platform]
    return (w // 2 - 7, 2 if platform in ROUND else 0)


class Recording:
    def __init__(self, rec, platform, variant):
        self.platform = platform
        self.d = os.path.join(rec, '%s_%s' % (platform, variant))
        self.times = json.load(open(os.path.join(self.d, 'times.json')))
        self._cache = {}

    def __len__(self):
        return len(self.times)

    def raw(self, i):
        if i not in self._cache:
            im = Image.open(os.path.join(self.d, 'f%03d.png' % i)).convert('RGB')
            im = normalise(im, self.platform)
            # The basalt emulator's dump has a 2px black frame round the
            # screen; the store rejects anything but the native size.
            w, h = NATIVE[self.platform]
            if im.size != (w, h):
                x, y = (im.width - w) // 2, (im.height - h) // 2
                im = im.crop((x, y, x + w, y + h))
            self._cache[i] = im
        return self._cache[i]

    def beacon(self, i):
        """(state, fever, tick) or None if this frame isn't the game."""
        im = self.raw(i)
        x, y = beacon_xy(self.platform)
        px = [im.getpixel((x + k, y)) for k in range(BEACON_BITS)]
        if any(p not in ((0, 0, 0), (255, 255, 255)) for p in px):
            return None
        bits = sum(1 << k for k, p in enumerate(px) if p == (255, 255, 255))
        state = bits & 7
        if state > STATE_LOST or (bits >> 12) != state:
            return None  # a dump caught mid-draw: the two copies of the state disagree
        return (state, bool(bits & 8), (bits >> 4) & 0xff)

    def clean(self, i):
        """The frame with the beacon painted out (each pixel takes the colour of
        the one below it)."""
        im = self.raw(i).copy()
        x, y = beacon_xy(self.platform)
        for k in range(BEACON_BITS):
            im.putpixel((x + k, y), im.getpixel((x + k, y + 1)))
        return im


MAX_TICK_STEP = 30  # ticks between two dumps (about a second); more needs confirming


def trusted(rec, start=0, end=None):
    """(index, beacon) for the frames whose beacon can be believed.

    A dump can land while the game is still drawing, and then the beacon row
    holds a mix of old and new pixels: usually an impossible state (dropped by
    beacon()), but sometimes a plausible one with the wrong tick. So a reading
    counts if its tick moved forward a small step from the last trusted one, or
    — after an emulator stall — jumped further forward but the next two
    readings carry on from it. Garbage never lines up with what follows it."""
    end = len(rec) if end is None else end
    raw = [(i, rec.beacon(i)) for i in range(start, end)]
    raw = [(i, b) for i, b in raw if b is not None]

    def step(a, b):
        return (b[2] - a[2]) % 256

    def confirmed(k):
        nxt = raw[k + 1:k + 3]
        return len(nxt) == 2 and step(raw[k][1], nxt[0][1]) <= MAX_TICK_STEP \
            and step(nxt[0][1], nxt[1][1]) <= MAX_TICK_STEP

    out = []
    for k, (i, b) in enumerate(raw):
        if not out:
            if confirmed(k):
                out.append((i, b))
        elif step(out[-1][1], b) <= MAX_TICK_STEP or (step(out[-1][1], b) < 128 and confirmed(k)):
            out.append((i, b))
    return out


def by_tick(frames):
    """One frame per game tick: the *last* capture of each. The black-and-white
    emulators redraw top-down and the beacon is at the top, so a dump caught
    mid-redraw already shows the new tick with the old picture underneath; a
    later capture of the same tick is whole."""
    picked = []
    for i, b in frames:
        if picked and picked[-1][1][2] == b[2]:
            picked[-1] = (i, b)
        else:
            picked.append((i, b))
    return picked


def all_runs(frames):
    """Every complete run in trusted frames, each from the start of the game to
    the last frame of its results panel, just before autoplay restarts. A state
    change only counts once three readings in a row agree. The first run is
    usually joined part-way through, since recording starts after the install."""
    def held(k, states):
        return k + 3 <= len(frames) and all(b[0] in states for _, b in frames[k:k + 3])

    runs, k = [], 0
    while True:
        won = next((j for j in range(k, len(frames)) if held(j, (STATE_WON, STATE_LOST))), None)
        if won is None:
            break
        again = next((j for j in range(won, len(frames)) if held(j, (STATE_AIMING,))), None)
        if again is None:
            break
        runs.append(frames[k:again])
        k = again
    if not runs:
        raise SystemExit('no complete run in the recording')
    return runs


def stars_shown(rec, i):
    """How much of the results panel's star row is filled in: ChromeYellow on
    colour watches, black on black-and-white ones. More is more stars."""
    im = rec.clean(i)
    w, h = im.size
    bw, bh = min(140, w - 8), 104
    x0, y0 = (w - bw) // 2, (h - bh) // 2 + 50
    want = (0, 0, 0) if rec.platform in BW else (255, 170, 0)
    return sum(1 for y in range(y0, y0 + 16) for x in range(x0, x0 + bw)
               if im.getpixel((x, y)) == want)


def save_gif(rec, frames, path):
    """frames: [(index, beacon)]. Each frame shows for the game ticks until the
    next one. Drops frames (and stretches the rest) if it comes out too big."""
    images = [rec.clean(i) for i, _ in frames]
    ticks = [b[2] for _, b in frames]
    colours = sorted({c for im in images for _, c in im.getcolors(1 << 16)})
    pal = Image.new('P', (1, 1))
    flat = [v for c in colours for v in c]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    step = 1
    while True:
        keep = list(range(0, len(images), step))
        if keep[-1] != len(images) - 1:
            keep.append(len(images) - 1)
        durations = []
        for k, idx in enumerate(keep):
            if k + 1 < len(keep):
                gap = (ticks[keep[k + 1]] - ticks[idx]) % 256 or 1
                durations.append(max(20, round(gap * TICK_MS / 10) * 10))
            else:
                durations.append(1500)  # linger on the results panel
        pframes = [images[i].quantize(palette=pal, dither=Image.Dither.NONE) for i in keep]
        pframes[0].save(path, save_all=True, append_images=pframes[1:], duration=durations,
                        loop=0, optimize=False, disposal=1)
        if os.path.getsize(path) <= MAX_GIF or step >= 4:
            return len(keep), sum(durations)
        step += 1


def pick(frames, state=None, fever=None, at=0.5, after_ticks=None):
    """A frame from the span matching state/fever: `at` of the way through it,
    or `after_ticks` game ticks after it starts."""
    span = [(i, b) for i, b in frames
            if (state is None or b[0] in state) and (fever is None or b[1] == fever)]
    if not span:
        return None
    if after_ticks is not None:
        t0 = span[0][1][2]
        for i, b in span:
            if (b[2] - t0) % 256 >= after_ticks:
                return i
        return span[-1][0]
    return span[min(len(span) - 1, int(len(span) * at))][0]


# ----------------------------------------------------------------- artwork --

def peg(d, x, y, r, body, hi):
    d.ellipse((x - r, y - r, x + r, y + r), fill=body)
    d.point((x - r // 2, y - r // 2), fill=hi)


def icon(grid, scale):
    """The icon, drawn as pixel art on a grid and scaled up square: the
    launcher, its dotted aim, a board of pegs in the game's four colours, a ball
    with its trail, and the golden bucket."""
    im = Image.new('RGBA', (grid, grid), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    g = grid
    d.rounded_rectangle((0, 0, g - 1, g - 1), radius=g // 6, fill=NAVY,
                        outline=(85, 85, 170))
    cx = g // 2
    # Launcher: barrel pointing down-left, and its hub.
    d.line((cx, 4, cx - 3, 8), fill=WHITE, width=2)
    d.ellipse((cx - 2, 2, cx + 2, 6), fill=YELLOW)
    # Pegs, staggered.
    r = 2 if g < 44 else 3
    rows = [
        (0.30, [BLUE, ORANGE, BLUE, GREEN, ORANGE]),
        (0.48, [ORANGE, PINK, BLUE, ORANGE]),
        (0.66, [BLUE, GREEN, ORANGE, BLUE, BLUE]),
    ]
    for ry, cols in rows:
        n = len(cols)
        y = round(g * ry)
        for k, c in enumerate(cols):
            x = round(g * (0.16 + 0.68 * k / (n - 1))) if n == 5 else round(g * (0.25 + 0.5 * k / (n - 1)))
            hi = ORANGE_HI if c == ORANGE else (BLUE_HI if c == BLUE else WHITE)
            peg(d, x, y, r, c, hi)
    # The aim, dotted from the barrel towards the second row.
    for k in range(1, 5):
        d.point((cx - 3 - k * 2, 8 + k * 2 + k * k // 3), fill=YELLOW)
    # A ball just off a peg, with its trail.
    bx, by = round(g * 0.62), round(g * 0.40)
    for k, c in enumerate((CYAN, CYAN)):
        d.point((bx - 2 - 2 * k, by - 2 - 2 * k), fill=c)
    d.ellipse((bx - 2, by - 2, bx + 2, by + 2), fill=WHITE)
    # The bucket, from the game's own sprite.
    bucket = Image.open(BUCKET_SMALL).convert('RGBA')
    if bucket.width > g - 8:
        w = g - 8
        bucket = bucket.resize((w, round(bucket.height * w / bucket.width)), Image.BOX)
        px = bucket.load()
        for y in range(bucket.height):
            for x in range(bucket.width):
                p = px[x, y]
                px[x, y] = p[:3] + ((255,) if p[3] >= 128 else (0,))
    im.alpha_composite(bucket, ((g - bucket.width) // 2, g - bucket.height - 3))
    return im.resize((g * scale, g * scale), Image.NEAREST)


def round_mask(im):
    m = Image.new('L', im.size, 0)
    ImageDraw.Draw(m).ellipse((0, 0, im.width - 1, im.height - 1), fill=255)
    return m


def make_banner(left_shot, right_shot):
    """A night table of faint pegs behind the title, a shot arcing in, the
    golden bucket on the bottom edge, and two watches: emery (left_shot) and
    gabbro (right_shot). The layout (scale 0.86, 24px right margin, 26px gap)
    is what ~/Projects/pebble/showcase/build_showcase.py expects when it plays
    the preview GIFs on these screens."""
    W, H = 720, 320
    out = Image.new('RGBA', (W, H))
    d = ImageDraw.Draw(out)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(4 + 10 * t), int(4 + 8 * t), int(28 + 40 * t), 255))
    pegs = Image.new('RGBA', out.size, (0, 0, 0, 0))
    pd = ImageDraw.Draw(pegs)
    cols = [BLUE] * 6 + [ORANGE, ORANGE, GREEN, PINK]
    k = 0
    for row in range(7):
        for c in range(19):
            x = 18 + c * 40 + (20 if row % 2 else 0)
            y = 26 + row * 44
            pd.ellipse((x - 6, y - 6, x + 6, y + 6), fill=cols[(k * 7 + row * 3) % len(cols)] + (70,))
            k += 1
    out.alpha_composite(pegs)
    for i in range(8):
        x, y = 150 + i * 7, 4 + i * 6 + i * i * 0.35
        d.ellipse((x - 1.6, y - 1.6, x + 1.6, y + 1.6), fill=YELLOW + (255,))
    d.ellipse((205, 64, 217, 76), fill=WHITE + (255,))
    bucket = Image.open(BUCKET).convert('RGBA')
    bucket = bucket.resize((bucket.width * 2, bucket.height * 2), Image.NEAREST)
    out.alpha_composite(bucket, (40, H - bucket.height - 10))

    scale = 0.86
    em = left_shot.resize((round(left_shot.width * scale), round(left_shot.height * scale)),
                          Image.LANCZOS)
    gb = right_shot.resize((round(right_shot.width * scale), round(right_shot.height * scale)),
                           Image.LANCZOS)
    gx, gy = W - gb.width - 24, (H - gb.height) // 2
    ex, ey = gx - em.width - 26, (H - em.height) // 2
    glow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.rounded_rectangle((ex - 7, ey - 7, ex + em.width + 7, ey + em.height + 7), 16,
                         fill=(255, 255, 255, 36))
    gd.ellipse((gx - 7, gy - 7, gx + gb.width + 7, gy + gb.height + 7), fill=(255, 255, 255, 36))
    out.alpha_composite(glow.filter(ImageFilter.GaussianBlur(6)))
    bez = ImageDraw.Draw(out)
    bez.rounded_rectangle((ex - 5, ey - 5, ex + em.width + 4, ey + em.height + 4), 14,
                          fill=(34, 34, 40))
    bez.ellipse((gx - 5, gy - 5, gx + gb.width + 4, gy + gb.height + 4), fill=(34, 34, 40))
    out.paste(em, (ex, ey))
    out.paste(gb, (gx, gy), round_mask(gb))

    d = ImageDraw.Draw(out)
    d.text((28, 82), 'Peggle', font=ImageFont.truetype(ROUNDED, 62), fill=WHITE)
    tag = ImageFont.truetype(ROUNDED, 18)
    lines = ['Pachinko on your wrist.', 'Clear the orange.', 'Catch the fever.']
    for k, line in enumerate(lines):
        # Keep clear of the first watch's bezel.
        if d.textlength(line, font=tag) > ex - 12 - 32:
            raise SystemExit('banner tagline "%s" runs into the watch' % line)
        d.text((32, 162 + k * 23), line, font=tag, fill=(255, 170, 0))
    return out.convert('RGB')


# -------------------------------------------------------------------- main --

def main():
    rec_dir = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    platforms = sorted({os.path.basename(p).split('_')[0]
                        for p in glob.glob(os.path.join(rec_dir, '*_demo'))})
    written = []
    stills = {}
    for p in platforms:
        for old in glob.glob(os.path.join(OUT, p + '_[1-5]_*')):
            os.remove(old)
        demo = Recording(rec_dir, p, 'demo')
        runs = [by_tick(r) for r in all_runs(trusted(demo))]
        clears = [pick(r, state=(STATE_WON,), after_ticks=40) for r in runs]
        scores = [stars_shown(demo, c) if c is not None else -1 for c in clears]
        # The results still: whichever run earned the most stars. The GIF: the
        # best run filmed from its first frame (the first run usually isn't),
        # so it loops cleanly, then the shortest.
        best = max(range(len(runs)), key=lambda k: scores[k])
        clear = clears[best]
        whole = list(range(1, len(runs))) or [0]
        g = max(whole, key=lambda k: (scores[k], -len(runs[k])))
        frames = runs[g]
        print('%s: %d runs, star pixels %s; still from run %d, GIF from run %d'
              % (p, len(runs), scores, best, g))
        path = os.path.join(OUT, '%s_1_preview.gif' % p)
        n, ms = save_gif(demo, frames, path)
        print('%s: GIF %d frames, %.1fs' % (p, n, ms / 1000))
        written.append(path)

        fever = pick(frames, state=(STATE_FLYING,), fever=True, at=0.2)
        level_rec = Recording(rec_dir, p, 'level')
        level = pick(by_tick(trusted(level_rec)), state=(STATE_FLYING,), after_ticks=22)
        menu_rec = Recording(rec_dir, p, 'menu')
        shots = {
            '2_level': level_rec.clean(level) if level is not None else None,
            '3_fever': demo.clean(fever) if fever is not None else None,
            '4_clear': demo.clean(clear) if clear is not None else None,
            # The menu has no beacon: it's not the game window.
            '5_menu': menu_rec.raw(len(menu_rec) - 1),
        }
        stills[p] = shots
        for slot, im in shots.items():
            if im is None:
                print('  %s: no frame for %s' % (p, slot))
                continue
            path = os.path.join(OUT, '%s_%s.png' % (p, slot))
            im.save(path)
            written.append(path)
    for size, grid, scale in ((80, 40, 2), (144, 48, 3)):
        path = os.path.join(OUT, 'icon_%dx%d.png' % (size, size))
        icon(grid, scale).save(path)
        written.append(path)
    if 'emery' in stills and 'gabbro' in stills:
        path = os.path.join(OUT, 'banner_720x320.png')
        # The story in two screens: the fever, then the stars it earned.
        make_banner(stills['emery']['3_fever'], stills['gabbro']['4_clear']).save(path)
        written.append(path)
    for w in written:
        im = Image.open(w)
        print('%-28s %dx%d  %dKB' % (os.path.relpath(w, ROOT), im.size[0], im.size[1],
                                     os.path.getsize(w) // 1024))


if __name__ == '__main__':
    main()
