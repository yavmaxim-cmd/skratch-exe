"""Measure highlights, concentric rows and transparency in the released skin."""
import numpy as np
from PIL import Image

from build_matte_skin import ASSETS, STROBE_ROWS, sample


angles = np.arange(8192) * (2 * np.pi / 8192)


def array(name):
    return np.asarray(Image.open(ASSETS / name).convert("RGBA"))


def sectors(name, centre, start, end):
    data = array(name)[..., :3].astype(np.float32).mean(axis=2)
    radius = np.arange(start, end + 1, 2)[:, None]
    y = centre[1] + radius * np.sin(angles)
    x = centre[0] + radius * np.cos(angles)
    return sample(data, x, y).mean(axis=0)


def spread(values):
    p5, p95 = np.percentile(values, (5, 95))
    return p95 - p5


for label, old, old_axis, new, low, high in [
    ("Vinyl", "vinyl.png", (447.5, 425.5), "vinyl_matte.png", 151, 377),
    ("Rim", "disk.png", (477.67, 510.43), "platter_matte.png", 425, 432),
]:
    size = array(new).shape[0]
    axis = ((size - 1) / 2, (size - 1) / 2)
    baseline = spread(sectors(old, old_axis, low, high))
    matte = spread(sectors(new, axis, low, high))
    assert matte < baseline * 0.25
    print(f"{label} azimuth P95-P5: {baseline:.2f} -> {matte:.2f} / 255")

for name in ("vinyl_matte.png", "platter_matte.png", "knob_cap.png"):
    rgba = array(name)
    alpha = rgba[..., 3]
    assert not alpha[0, 0] and alpha[alpha.shape[0] // 2, alpha.shape[1] // 2] == 255
    assert np.array_equal(alpha, np.flip(alpha, axis=(0, 1)))
    assert np.all(rgba[alpha == 0, :3] == 0)
    print(f"{name}: centred RGBA aperture and transparent corners")

plate = array("platter_matte.png")
luma = plate[..., :3].astype(np.float32).mean(axis=2)
centre = (luma.shape[0] - 1) / 2
for radius, count, phase in STROBE_ROWS:
    row = sample(luma, centre + radius * np.cos(angles), centre + radius * np.sin(angles))
    bright = row > 95
    starts = np.flatnonzero(bright & ~np.roll(bright, 1))
    angular_gaps = np.diff(np.r_[starts, starts[0] + angles.size])
    assert len(starts) == count
    assert np.max(np.abs(angular_gaps - angles.size / count)) < 2.5
    print(f"Strobe radius {radius}: {count} dots, ideal step {360 / count:.4f} degrees")

original = array("mixer.png")[27:1053, 21:856, 3]
assert np.array_equal(original, array("mixer_clean.png")[..., 3])
print("Mixer chassis alpha matches the original crop exactly")
