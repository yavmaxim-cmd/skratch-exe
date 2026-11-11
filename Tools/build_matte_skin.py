"""Derive the 2.3 RGBA skin from the original SKRATCH photos (Pillow, NumPy)."""
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter


ASSETS = Path(__file__).resolve().parents[1] / "Assets"
CHANNELS = (263, 377, 490, 602)
STROBE_ROWS = ((378, 113, 0.14), (394, 132, 0.47), (410, 136, 0.08))


def photo(name):
    return Image.open(ASSETS / name).convert("RGBA")


def lerp(a, b, t):
    return a + (b - a) * t


def sample(image, x, y):
    left = np.floor(x).astype(int).clip(0, image.shape[1] - 2)
    top = np.floor(y).astype(int).clip(0, image.shape[0] - 2)
    fx, fy = x - left, y - top
    return lerp(lerp(image[top, left], image[top, left + 1], fx),
                lerp(image[top + 1, left], image[top + 1, left + 1], fx), fy)


def geometry(size, centre):
    y, x = np.indices((size, size), dtype=np.float32)
    x += 0.5 - size / 2
    y += 0.5 - size / 2
    return x + centre[0], y + centre[1], np.hypot(x, y), np.arctan2(y, x)


def polar_samples(luma, centre, radius):
    angles = np.linspace(0, 2 * np.pi, 2048, endpoint=False)
    distances = np.arange(0, radius + 1.25, 0.25)
    values = sample(luma,
                    centre[0] + distances[:, None] * np.cos(angles),
                    centre[1] + distances[:, None] * np.sin(angles))
    return distances, values


def save_disc(name, rgb, radius, r):
    alpha = np.round(np.clip(radius + 0.5 - r, 0, 1) * 255)
    output = np.empty((*r.shape, 4), dtype=np.uint8)
    output[..., :3] = np.round(np.where(alpha[..., None] > 0, rgb, 0)).clip(0, 255)
    output[..., 3] = alpha
    Image.fromarray(output).save(ASSETS / name)


def matte_vinyl():
    original = photo("vinyl.png")
    centre, radius = (447.5, 425.5), 386.5
    x, y, r, _ = geometry(778, centre)
    source = np.asarray(original, dtype=np.float32)
    luma = np.asarray(original.convert("L"), dtype=np.float32)
    soft = np.asarray(original.convert("L").filter(ImageFilter.GaussianBlur(4.5)), dtype=np.float32)
    distances, polar = polar_samples(luma, centre, radius)
    # Dark-side angular statistics remove the specular bands while retaining recorded grooves.
    groove = np.quantile(polar, 0.38, axis=1)
    smooth = np.convolve(np.pad(groove, (8, 8), mode="edge"), np.ones(17) / 17, mode="valid")
    profile = 7.5 + 0.62 * groove + 1.05 * (groove - smooth)
    fine = np.clip(sample(luma - soft, x, y), -13, 12) * 0.35
    material = np.clip(np.interp(r, distances, profile) + fine, 5, 43)
    paper = 1 - np.clip((r - 132) / 13, 0, 1)
    rgb = np.stack([lerp(material * tint, sample(source[..., c], x, y), paper)
                    for c, tint in enumerate((1.00, 0.99, 1.02))], axis=-1)
    save_disc("vinyl_matte.png", rgb, radius, r)


def matte_platter():
    original = photo("disk.png")
    centre, radius = (477.67, 510.43), 436.0
    x, y, r, angle = geometry(878, centre)
    luma = np.asarray(original.convert("L"), dtype=np.float32)
    soft = np.asarray(original.convert("L").filter(ImageFilter.GaussianBlur(3.0)), dtype=np.float32)
    distances, polar = polar_samples(luma, centre, radius)
    median = np.quantile(polar, .48, axis=1)
    band = (distances >= 368) & (distances <= 418)
    median[band] = np.quantile(polar[band], .19, axis=1)
    profile = 5 + .82 * np.interp(r, distances, median)
    grain = np.clip(sample(luma - soft, x, y), -10, 10) * .22
    grain *= 1 - np.clip((r - 365) / 3, 0, 1) * np.clip((421 - r) / 3, 0, 1)
    material = profile + grain
    for ring_radius, count, phase in STROBE_ROWS:
        separation = 2 * np.pi / count
        arc = ((angle + phase * separation + separation / 2) % separation - separation / 2) * ring_radius
        distance = np.hypot((r - ring_radius) / 4.7, arc / 4.7)
        alpha = np.clip((1.10 - distance) / .22, 0, 1)
        # Diffuse dot tone from the sampled source row. Identical angular spacing per ring.
        row = polar[int(ring_radius / .25)]
        tone = float(np.percentile(row, 90)) * .77
        dot = tone + np.clip(1 - distance, 0, 1) * 16
        material = lerp(material, dot, alpha)
    rgb = np.stack([material, material * .999, material * 1.013], axis=-1)
    save_disc("platter_matte.png", rgb, radius, r)


def mixer_and_knob():
    original = photo("mixer.png")
    source = np.asarray(original, dtype=np.float32)
    matte = source.copy()
    raw_grain = np.asarray(original.convert("L"), dtype=np.float32)
    raw_grain -= np.asarray(original.convert("L").filter(ImageFilter.GaussianBlur(1.25)), dtype=np.float32)
    grain = raw_grain[296:495, 164:190]

    def repair(x, y, rx, ry, rectangular=False):
        top, bottom, left, right = y - ry, y + ry + 1, x - rx, x + rx + 1
        rows, cols = np.indices((bottom - top, right - left))
        tx, ty = cols[..., None] / (2 * rx), rows[..., None] / (2 * ry)
        sides = lerp(source[top:bottom, left-1:left, :3], source[top:bottom, right:right+1, :3], tx)
        ends = lerp(source[top-1:top, left:right, :3], source[bottom:bottom+1, left:right, :3], ty)
        panel = ends if rectangular else lerp(sides, ends, .28)
        texture = grain[(rows + 3 * x + y) % grain.shape[0], (cols + x + 5 * y) % grain.shape[1]]
        panel += np.clip(texture, -2.5, 2.5)[..., None] * .65
        distance = np.maximum(abs(rows - ry) / ry, abs(cols - rx) / rx) if rectangular else np.hypot(
            (rows - ry) / ry, (cols - rx) / rx)
        mask = np.clip((1 - distance) * (min(rx, ry) / 2 if rectangular else 5.5), 0, 1)[..., None]
        matte[top:bottom, left:right, :3] = lerp(source[top:bottom, left:right, :3], panel, mask)

    for x in CHANNELS:
        for y in (254, 392):
            repair(x, y, 33, 32)
        repair(x, 554, 52, 55, rectangular=True)
    for y in (753, 819, 892, 972):
        repair(118, y, 31, 30)
    for x, y in ((722, 558), (801, 558), (765, 922)):
        repair(x, y, 30, 30)
    Image.fromarray(np.round(matte).clip(0, 255).astype(np.uint8)).crop((21, 27, 856, 1053)).save(
        ASSETS / "mixer_clean.png")
    x, y, r, _ = geometry(124, (1088.2, 154.0))
    cap = np.stack([sample(source[..., c], x, y) for c in range(3)], axis=-1)
    save_disc("knob_cap.png", cap, 59.5, r)


if __name__ == "__main__":
    matte_vinyl()
    matte_platter()
    mixer_and_knob()
