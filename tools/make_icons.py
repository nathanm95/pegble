#!/usr/bin/env python3
"""Turn the raw PixelLab icons in art/raw/ into watch resources.

Each image is cropped to its content, centred on a SIZE x SIZE canvas, snapped
to the 64 colours a Pebble can show (R, G and B each 0/85/170/255) with alpha
forced to fully on or off, and written to resources/images/icon_<name>.png.

Off-palette art still *builds* — the resource compiler quietly approximates it
— but comes out as dithered mush on the watch, so this refuses to write a file
it had to change by more than rounding. Run from the repo root:

    python3 tools/make_icons.py
"""
from PIL import Image
import glob, os, sys

SIZE = 30
LEVELS = (0, 85, 170, 255)
ROOT = os.path.join(os.path.dirname(__file__), "..")
RAW = os.path.join(ROOT, "art", "raw")
OUT = os.path.join(ROOT, "resources", "images")


def snap(v):
    return min(LEVELS, key=lambda l: abs(l - v))


def convert(path):
    name = os.path.splitext(os.path.basename(path))[0]
    im = Image.open(path).convert("RGBA")
    box = im.getbbox()
    if not box:
        sys.exit(f"{name}: empty image")
    im = im.crop(box)
    if im.width > SIZE or im.height > SIZE:
        sys.exit(f"{name}: {im.width}x{im.height} content won't fit {SIZE}x{SIZE}")
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.paste(im, ((SIZE - im.width) // 2, (SIZE - im.height) // 2))

    px = canvas.load()
    worst = 0
    colours = set()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = px[x, y]
            if a < 128:
                px[x, y] = (0, 0, 0, 0)
                continue
            s = (snap(r), snap(g), snap(b))
            worst = max(worst, abs(s[0] - r), abs(s[1] - g), abs(s[2] - b))
            px[x, y] = s + (255,)
            colours.add(s)
    if worst > 20:
        sys.exit(f"{name}: a pixel was {worst} away from the Pebble palette; regenerate "
                 f"it with the forced palette (art/pebble64.png)")
    out = os.path.join(OUT, f"icon_{name}.png")
    canvas.save(out)
    print(f"{out}: {len(colours)} colours, content {box[2]-box[0]}x{box[3]-box[1]}")


def main():
    os.makedirs(OUT, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(RAW, "*.png"))):
        convert(path)


if __name__ == "__main__":
    main()
