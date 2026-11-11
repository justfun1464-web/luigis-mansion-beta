# Luigi's Mansion BETA: textures drawn for the BETA edition itself (no game data).
# Writes src/recomp/beta_assets.inc (BTI files as byte arrays) and preview PNGs.
#   python beta/tools/gen_assets.py [--preview <dir>]
#
# - bahhh:  the "BAHHHH!" scare lettering (RGB5A3 256x64), drawn when a ghost startles Luigi
# - mist:   soft tileable fog (IA8 128x128), the purple mist over the screen during a scare
# - cloth:  green cloth (RGB565 64x64, tileable), the frame around the bordered 3D view
# - glow:   radial glow (IA8 64x64), the E3 title screen's pink glow
import argparse
import math
import os
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lmtools import make_bti  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "C:/Windows/Fonts"]


def font(names, size):
    for d in FONT_DIRS:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def tileable_noise(n, scales, seed):
    """Sum of smooth periodic noise octaves, 0..1, n x n, wraps at the edges."""
    rng = np.random.default_rng(seed)
    acc = np.zeros((n, n), np.float32)
    total = 0.0
    for cells, weight in scales:
        g = rng.random((cells, cells)).astype(np.float32)
        # bicubic-ish periodic upsample: tile 3x3, resize, crop the middle
        big = np.tile(g, (3, 3))
        im = Image.fromarray((big * 255).astype(np.uint8)).resize((n * 3, n * 3), Image.BICUBIC)
        a = np.asarray(im, np.float32)[n:2 * n, n:2 * n] / 255.0
        acc += a * weight
        total += weight
    acc /= total
    acc -= acc.min()
    acc /= max(acc.max(), 1e-6)
    return acc


def bahhh():
    W, H = 256, 64
    text = "BAHHHH!"
    f = font(["DejaVuSerifCondensed-Bold.ttf", "DejaVuSerif-Bold.ttf", "georgiab.ttf", "timesbd.ttf"], 50)
    # each letter on its own, slightly rotated and bobbing: hand-lettered, uneasy
    layer = Image.new("RGBA", (W * 2, H * 2), (0, 0, 0, 0))
    rng = random.Random(7)
    x = 18
    for i, ch in enumerate(text):
        g = Image.new("L", (90, 110), 0)
        ImageDraw.Draw(g).text((12, 4), ch, font=font(["DejaVuSerifCondensed-Bold.ttf", "DejaVuSerif-Bold.ttf"], 92 - i * 3), fill=255)
        g = g.rotate(rng.uniform(-9, 9), resample=Image.BICUBIC, expand=False)
        bbox = g.getbbox() or (0, 0, 1, 1)
        g = g.crop((bbox[0], 0, bbox[2], g.height))
        y = 8 + int(6 * math.sin(i * 1.7)) + i * 2
        layer.paste(Image.new("RGBA", g.size, (255, 255, 255, 255)), (x, y), g)
        x += g.width - 6
    alpha = layer.split()[3]
    # fit to the texture
    bbox = alpha.getbbox()
    alpha = alpha.crop(bbox)
    scale = min((W - 12) / alpha.width, (H - 12) / alpha.height)
    alpha = alpha.resize((int(alpha.width * scale), int(alpha.height * scale)), Image.LANCZOS)
    a = Image.new("L", (W, H), 0)
    a.paste(alpha, ((W - alpha.width) // 2, (H - alpha.height) // 2))
    fill = np.asarray(a, np.float32) / 255.0
    outline = np.asarray(a.filter(ImageFilter.MaxFilter(5)), np.float32) / 255.0
    shadow = np.asarray(a.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(3)), np.float32) / 255.0
    shadow = np.roll(np.roll(shadow, 3, 0), 3, 1)
    yy = np.linspace(0, 1, H)[:, None] * np.ones((1, W))
    # fill: pale lilac at the top to a sick purple at the bottom
    top = np.array([236, 220, 255], np.float32)
    bot = np.array([150, 70, 200], np.float32)
    col = top[None, None, :] * (1 - yy[..., None]) + bot[None, None, :] * yy[..., None]
    edge = np.array([40, 0, 60], np.float32)
    out = np.zeros((H, W, 4), np.float32)
    # shadow, then outline, then fill
    out[..., :3] = 0
    out[..., 3] = shadow * 160
    o_a = outline
    out[..., :3] = out[..., :3] * (1 - o_a[..., None]) + edge * o_a[..., None]
    out[..., 3] = np.maximum(out[..., 3], o_a * 255)
    out[..., :3] = out[..., :3] * (1 - fill[..., None]) + col * fill[..., None]
    out[..., 3] = np.maximum(out[..., 3], fill * 255)
    return np.clip(out, 0, 255).astype(np.uint8)


def mist():
    n = 128
    a = tileable_noise(n, [(4, 1.0), (8, 0.6), (16, 0.35), (32, 0.15)], 11)
    a = np.clip((a - 0.2) / 0.8, 0, 1) ** 1.3
    img = np.zeros((n, n, 4), np.uint8)
    img[..., 0:3] = 255
    img[..., 3] = (a * 255).astype(np.uint8)
    return img


def cloth():
    n = 64
    base = tileable_noise(n, [(4, 1.0), (8, 0.5)], 3)
    fine = tileable_noise(n, [(32, 1.0), (64, 0.8)], 5)
    yy, xx = np.mgrid[0:n, 0:n]
    # plain weave: alternating raised warp/weft threads every 2 texels
    weave = (((xx // 2) + (yy // 2)) % 2).astype(np.float32)
    thread_x = 0.5 + 0.5 * np.cos((xx % 2) * math.pi)
    thread_y = 0.5 + 0.5 * np.cos((yy % 2) * math.pi)
    tex = 0.55 + 0.18 * (weave * thread_x + (1 - weave) * thread_y) + 0.17 * (base - 0.5) + 0.12 * (fine - 0.5)
    tex = np.clip(tex, 0, 1)
    dark = np.array([12, 46, 22], np.float32)
    light = np.array([56, 128, 66], np.float32)
    rgb = dark[None, None, :] + (light - dark)[None, None, :] * tex[..., None]
    img = np.zeros((n, n, 4), np.uint8)
    img[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    img[..., 3] = 255
    return img


def glow():
    n = 64
    yy, xx = np.mgrid[0:n, 0:n]
    r = np.sqrt(((xx + 0.5) - n / 2) ** 2 + ((yy + 0.5) - n / 2) ** 2) / (n / 2)
    a = np.clip(1 - r, 0, 1) ** 2.2
    img = np.zeros((n, n, 4), np.uint8)
    img[..., 0:3] = 255
    img[..., 3] = (a * 255).astype(np.uint8)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write PNG previews to this folder")
    args = ap.parse_args()
    assets = [
        ("bahhh", bahhh(), 5, 0, 0),
        ("mist", mist(), 3, 1, 1),
        ("cloth", cloth(), 4, 1, 1),
        ("glow", glow(), 3, 0, 0),
    ]
    out = ["// Generated by beta/tools/gen_assets.py: textures drawn for the BETA edition (no game data).",
           "// Each is a whole .bti file (32-byte header, one image).", ""]
    for name, img, fmt, ws, wt in assets:
        b = make_bti(img, fmt, ws, wt)
        out.append("static const unsigned char kBeta_%s[%d] = {" % (name, len(b)))
        for i in range(0, len(b), 24):
            out.append("    " + ", ".join("0x%02X" % v for v in b[i:i + 24]) + ",")
        out.append("};")
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            Image.fromarray(img, "RGBA").save(os.path.join(args.preview, name + ".png"))
    path = os.path.join(ROOT, "src", "recomp", "beta_assets.inc")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", path)


if __name__ == "__main__":
    main()
