"""Retouch the supplied faceplates in their existing atlas coordinates."""
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ASSETS = Path(__file__).resolve().parents[1] / "Assets"


def load(name):
    return np.array(Image.open(ASSETS / name).convert("RGBA"), dtype=np.float32)


def save(name, pixels):
    Image.fromarray(np.rint(np.clip(pixels, 0, 255)).astype("uint8")).save(ASSETS / name, optimize=True)


def smoothstep(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def remove_corner_bolt(deck, cx, cy):
    radius = 32
    rect = np.s_[cy-radius:cy+radius+1, cx-radius:cx+radius+1]
    local = deck[rect]
    yy, xx = np.mgrid[-radius:radius+1, -radius:radius+1]
    r = np.hypot(xx, yy)
    # An uninterrupted strip farther down the same columns carries the grain.
    brushed = deck[620:620 + 2 * radius + 1, cx-radius:cx+radius+1, :3].copy()
    # Match the surrounding corner's light using the annulus on the inside
    # of the faceplate, away from the outside bevel and the adjacent controls.
    toward_x = xx if cx < 600 else -xx
    toward_y = yy if cy < 600 else -yy
    reference = (r > 25) & (r < 31) & (toward_x >= 0) & (toward_y >= 0)
    shift = np.median((local[..., :3] - brushed)[reference], axis=0)
    brushed += shift
    blend = (1 - smoothstep((r - 22) / 10))[..., None]
    local[..., :3] += (brushed - local[..., :3]) * blend


deck = load("deck.png")
for x, y in [(79, 65), (1174, 65), (79, 1183), (1176, 1183)]:
    remove_corner_bolt(deck, x, y)

# Soften the reflection on the small pitch-side cap and retain its edges.
def polish(cx, cy, rx, ry, strength, compression, angle=0):
    margin = max(rx, ry) + 2
    rect = np.s_[cy-margin:cy+margin+1, cx-margin:cx+margin+1]
    patch = deck[rect]
    rgb = patch[..., :3]
    yy, xx = np.mgrid[-margin:margin+1, -margin:margin+1]
    ux = xx * np.cos(angle) + yy * np.sin(angle)
    uy = -xx * np.sin(angle) + yy * np.cos(angle)
    d = np.hypot(ux / rx, uy / ry)
    weight = (1 - smoothstep((d - .38) / .62))[..., None]
    median = np.asarray(Image.fromarray(rgb.astype("uint8")).filter(ImageFilter.MedianFilter(3)), dtype=np.float32)
    patch[..., :3] = rgb + weight * (strength * np.clip(median - rgb, -22, 22)
                                      - compression * np.maximum(0, rgb - 158))


polish(1051, 223, 23, 23, .34, .17)
polish(1063, 175, 17, 10, .42, .12)

yy, xx = np.ogrid[:deck.shape[0], :deck.shape[1]]
radius = np.hypot(xx - 612.57, yy - 732.65)
rim = (radius > 414) & (radius < 433) & (deck[..., 3] < 250)
deck[rim, :3] = deck[rim, :3] @ np.array([.2126, .7152, .0722], dtype=np.float32)[:, None]
# Punch the openings in the shell layer. Put real deck grain underneath the
# openings, where it will be visible through their fully transparent centres.
x0, x1, y0, y1 = 952, 1107, 286, 409
yy, xx = np.mgrid[y0:y1, x0:x1]
edge = np.full(xx.shape, 100.0)
for x, y in [(969.5, 303), (1000.3, 322.5), (1030, 341.8)]:
    edge = np.minimum(edge, np.hypot(xx - x, yy - y) - 12.2)

for ax, ay, bx, by in [(1070, 344, 1094, 360), (1047.6, 377, 1071.3, 392.8)]:
    dx, dy = bx - ax, by - ay
    t = np.clip(((xx - ax) * dx + (yy - ay) * dy) / (dx * dx + dy * dy), 0, 1)
    distance = np.hypot(xx - (ax + t * dx), yy - (ay + t * dy))
    edge = np.minimum(edge, distance - 4.6)

underlay = np.zeros_like(deck)
grain_rows = 448 + np.abs((np.arange(y1-y0) % 24) - 12)
grain = deck[grain_rows, x0:x1, :3]
# A small shadow from the thickness of the black shell sits at the cut edge.
shadow = 1 - smoothstep((-edge - .1) / 3.0)
steel = underlay[y0:y1, x0:x1]
steel[..., :3] = grain * (.86 - .17 * shadow[..., None])
steel[..., 3] = np.where(edge < 1, 255, 0)

opening = deck[y0:y1, x0:x1]
opening[..., 3] *= smoothstep((edge + .8) / 1.8)
opening[opening[..., 3] == 0, :3] = 0
save("deck_xp.png", deck)
save("deck_headshell_underlay.png", underlay)

mixer = load("mixer_clean.png")
# Clear the grille plus its lip and the screw at its upper-right.
# The unobstructed lower column supplies actual faceplate grain at the same x.
x0, x1, y0, y1 = 11, 176, 457, 706
target = mixer[y0:y1, x0:x1].copy()
rows = 747 + (208 - np.abs(np.arange(y1-y0) - 208))
donor = mixer[rows, x0:x1].copy()
yy, xx = np.mgrid[y0:y1, x0:x1]
grille = ((1 - smoothstep((np.maximum(20-xx, xx-167) + 1) / 8)) *
          (1 - smoothstep((np.maximum(488-yy, yy-683) + 2) / 17)))
screw = 1 - smoothstep((np.hypot(xx - 160, yy - 473) - 10) / 5)
mask = np.maximum(grille, screw)[..., None]
# Match the broad illumination above and below the repair. Retain the
# donor's horizontal lighting variation, grain and colour in each row.
level_above = np.median(mixer[460:477, 42:143, :3])
level_below = np.median(mixer[697:716, 42:143, :3])
progress = np.clip((np.arange(y0, y1) - 470) / 232, 0, 1)
desired = level_above + (level_below - level_above) * progress
donor_level = np.median(donor[:, 31:132, :3], axis=(1, 2))
donor[..., :3] += (desired - donor_level)[:, None, None]
target[..., :3] += (donor[..., :3] - target[..., :3]) * mask
mixer[y0:y1, x0:x1] = target
save("mixer_xp.png", mixer)
