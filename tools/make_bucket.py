#!/usr/bin/env python3
"""Make the bucket sprites from the PixelLab original in art/raw_bucket/.

PixelLab's tray came back with olive tones mixed into the gold, which reads as
dirt at 1x. This folds every colour onto one six-step gold ramp, crops to the
content and writes two sizes:

  resources/images/bucket_big.png    emery, gabbro   (52x18, the original)
  resources/images/bucket_small.png  basalt, chalk   (36x12, box-downscaled
                                                      and snapped back to the ramp)

The physics bucket is sized to match (play_set_bucket_width in game.c). Run
from the repo root:  python3 tools/make_bucket.py
"""
from PIL import Image
import os

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = os.path.join(ROOT, "art", "raw_bucket", "bucket_a.png")
OUT = os.path.join(ROOT, "resources", "images")

# Every colour the original uses -> its place on the ramp.
FOLD = {
    (0, 0, 0): (0, 0, 0),             # outline
    (85, 0, 0): (85, 0, 0),           # shadow
    (85, 85, 0): (170, 85, 0),        # olive: the "dirt", folded into the body
    (170, 85, 0): (170, 85, 0),       # body
    (170, 170, 0): (255, 170, 0),
    (255, 170, 85): (255, 170, 0),
    (255, 170, 0): (255, 170, 0),     # gold
    (255, 255, 85): (255, 255, 0),
    (255, 255, 0): (255, 255, 0),     # bright
    (255, 255, 170): (255, 255, 170), # rim highlight
    (255, 255, 255): (255, 255, 170),
}
RAMP = sorted(set(FOLD.values()))


def nearest(c):
    return min(RAMP, key=lambda r: sum((a - b) ** 2 for a, b in zip(r, c)))


def fold(im):
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a < 128:
                px[x, y] = (0, 0, 0, 0)
            else:
                px[x, y] = FOLD.get((r, g, b), nearest((r, g, b))) + (255,)
    return im


def main():
    big = fold(Image.open(SRC).convert("RGBA"))
    big = big.crop(big.getbbox())
    big.save(os.path.join(OUT, "bucket_big.png"))

    w = 36
    h = round(big.height * w / big.width)
    small = fold(big.resize((w, h), Image.BOX))
    small.save(os.path.join(OUT, "bucket_small.png"))
    for name, im in (("big", big), ("small", small)):
        colours = {p[:3] for p in im.get_flattened_data() if p[3]}
        print(f"bucket_{name}: {im.width}x{im.height}, {len(colours)} colours")


if __name__ == "__main__":
    main()
