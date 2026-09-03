#!/usr/bin/env python3
"""Render scenes from the display simulator to PNG (scaled, optional LCD grid).

    python3 tools/sim/sim.py out_dir [scale]
Builds tools/sim/sim.cpp, renders every scene, writes <scene>.png and a
contact sheet all.png.
"""
import os, subprocess, sys, struct
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
W, H = 284, 76
SCENES = [
    ("main", ["0.47", "0.82"]), ("main-hot", None), ("main-near", None), ("main-crit", None),
    ("main-over", []), ("main-noalarm", ["0.47", "0.82"]), ("tick", ["0.76", "0.82"]),
    ("main-err", ["0.47", "0.82"]), ("main-nodata", []), ("overlay", ["0.47", "0.82"]),
    ("token", []), ("notoken", []), ("config", []), ("config-fail", []),
    ("connecting", []), ("error", []), ("ota", ["63"]), ("ota-fail", []), ("message", []),
]

def build(exe):
    cmd = ["g++", "-std=c++17", "-O1", "-w", "-I", "tools/sim", "-I", "include", "-o", str(exe),
           "tools/sim/sim.cpp", "tools/sim/glcdfont.cpp", "src/display/ui.cpp", "src/display/pacman.cpp"]
    subprocess.run(cmd, cwd=ROOT, check=True)

def raw_to_img(raw):
    img = Image.new("RGB", (W, H))
    px = img.load()
    for i in range(W * H):
        v = struct.unpack_from("<H", raw, i * 2)[0]
        r = (v >> 11) & 0x1F; g = (v >> 5) & 0x3F; b = v & 0x1F
        px[i % W, i // W] = ((r * 255) // 31, (g * 255) // 63, (b * 255) // 31)
    return img

def upscale(img, scale, grid=True):
    big = img.resize((W * scale, H * scale), Image.NEAREST)
    if grid and scale >= 3:
        d = ImageDraw.Draw(big, "RGBA")
        for x in range(0, W * scale, scale):
            d.line([(x, 0), (x, H * scale)], fill=(0, 0, 0, 70))
        for y in range(0, H * scale, scale):
            d.line([(0, y), (W * scale, y)], fill=(0, 0, 0, 70))
    return big

def main():
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    exe = out / "sim"
    build(exe)
    imgs = []
    for name, args in SCENES:
        scene, a = name, args
        if name == "main-hot":  scene, a = "main", ["0.78", "0.82"]
        if name == "main-crit": scene, a = "main", ["0.96", "0.99"]
        if name == "main-near": scene, a = "main", ["0.99", "0.60"]
        raw = out / f"{name}.raw"
        subprocess.run([str(exe), scene, *a, str(raw)], cwd=ROOT, check=True, env={**os.environ, "TZ": "Europe/Warsaw"})
        img = raw_to_img(raw.read_bytes())
        upscale(img, scale).save(out / f"{name}.png")
        imgs.append((name, img))
    # contact sheet: 2 columns, labels
    cols, pad, s = 2, 14, 3
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (W * s + pad) + pad, rows * (H * s + pad + 16) + pad), (24, 24, 32))
    d = ImageDraw.Draw(sheet)
    for i, (name, img) in enumerate(imgs):
        x = pad + (i % cols) * (W * s + pad); y = pad + (i // cols) * (H * s + pad + 16)
        d.text((x, y), name, fill=(200, 200, 200))
        sheet.paste(upscale(img, s), (x, y + 14))
    sheet.save(out / "all.png")
    print("wrote", out / "all.png")

if __name__ == "__main__":
    main()
