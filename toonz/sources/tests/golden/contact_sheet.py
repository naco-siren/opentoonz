#!/usr/bin/env python3
"""Lays the frames of the fx sweep scenes out as labelled contact sheets.

Reads <project>/extras/fx_sweep.txt (written by the fixture generator) to
know which fx each frame shows, and flags the frames that deserve a look:

  BLANK   nothing was rendered
  FLAT    every pixel is the same
  WEAK    the frame is practically one of the fx's inputs, or the plain
          over of its inputs: the fx shows no effect with these parameters

One sheet per scene is written to --out, and a summary is printed.
"""

import argparse
import collections
import pathlib
import sys

import numpy as np
from PIL import Image, ImageDraw

# Fxs whose output is expected to equal an input.
IDENTITY = {"STD_nothingFx", "overFx", "STD_inoOverFx"}
TOLERANCE = 8  # per channel
WEAK_FRACTION = 0.02  # of the pixels


def load(path):
    return np.asarray(Image.open(path).convert("RGBA")).astype(int)


def differing(a, b):
    return (np.abs(a - b).max(axis=2) > TOLERANCE).mean()


def checker(size, step=10):
    image = Image.new("RGBA", size, (255, 255, 255, 255))
    draw = ImageDraw.Draw(image)
    for y in range(0, size[1], step):
        for x in range(0, size[0], step):
            if (x // step + y // step) % 2:
                draw.rectangle([x, y, x + step - 1, y + step - 1], fill=(215, 215, 215, 255))
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project", required=True, type=pathlib.Path, help="the reference project folder")
    parser.add_argument("--renders", required=True, type=pathlib.Path, help="folder with one sub-folder of frames per scene")
    parser.add_argument("--out", required=True, type=pathlib.Path, help="where the sheets are written")
    parser.add_argument("--columns", type=int, default=4)
    args = parser.parse_args()

    manifest = [line.split() for line in open(args.project / "extras" / "fx_sweep.txt") if not line.startswith("#")]
    cards = {}
    for name in ("main", "back", "ref"):
        path = args.project / "drawings" / f"card_{name}.0001.png"
        if path.exists():
            cards[name] = load(path)

    def frame_path(entry):
        return args.renders / entry[0] / f"{entry[0]}.{entry[1]}.png"

    over = next((load(frame_path(e)) for e in manifest if e[2] == "overFx" and frame_path(e).exists()), None)

    scenes = collections.OrderedDict()
    for entry in manifest:
        scenes.setdefault(entry[0], []).append(entry)

    args.out.mkdir(parents=True, exist_ok=True)
    flagged = collections.defaultdict(list)
    rendered = 0
    pad, label = 6, 14
    for scene, entries in scenes.items():
        sizes = [Image.open(frame_path(e)).size for e in entries if frame_path(e).exists()]
        if not sizes:
            flagged["MISSING"].extend(e[2] for e in entries)
            continue
        width, height = sizes[0]
        rows = (len(entries) + args.columns - 1) // args.columns
        sheet = Image.new("RGB", (pad + args.columns * (width + pad), pad + rows * (height + label + pad)), (70, 70, 70))
        draw = ImageDraw.Draw(sheet)
        for i, entry in enumerate(entries):
            fx, inputs = entry[2], entry[3:]
            x0 = pad + (i % args.columns) * (width + pad)
            y0 = pad + (i // args.columns) * (height + label + pad)
            if not frame_path(entry).exists():
                flagged["MISSING"].append(fx)
                draw.text((x0, y0), f"{fx}  [MISSING]", fill=(255, 120, 120))
                continue
            rendered += 1
            image = Image.open(frame_path(entry)).convert("RGBA")
            pixels = np.asarray(image).astype(int)
            tag = ""
            if (pixels[..., 3] == 0).all():
                tag = "BLANK"
            elif fx != "colorCardFx" and (pixels == pixels[0, 0]).all():
                tag = "FLAT"
            elif fx not in IDENTITY:
                if over is not None and len(inputs) > 1 and differing(pixels, over) < WEAK_FRACTION:
                    tag = "WEAK ~over"
                for item in inputs:
                    card = cards.get(item.split("=")[-1])
                    if card is not None and card.shape == pixels.shape and differing(pixels, card) < WEAK_FRACTION:
                        tag = "WEAK ~" + item.split("=")[-1]
            if tag:
                flagged[tag.split()[0]].append(fx)
            background = checker(image.size)
            background.alpha_composite(image)
            text = fx.replace("STD_", "") + (f"  [{tag}]" if tag else "")
            draw.text((x0, y0), text, fill=(255, 160, 160) if tag else (255, 255, 255))
            sheet.paste(background.convert("RGB"), (x0, y0 + label))
        sheet.save(args.out / f"sheet_{scene}.png")

    print(f"frames found: {rendered} of {len(manifest)}; sheets in {args.out}")
    for tag, fxs in flagged.items():
        print(f"{tag} {len(fxs)}: " + " ".join(fx.replace("STD_", "") for fx in fxs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
