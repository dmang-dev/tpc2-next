#!/usr/bin/env python3
"""Generate the committed PNG, ICO and macOS iconset assets.

The source artwork is packaging/icons/tpc2-next.svg. This script draws the
same cartridge motif with Pillow so the binary assets can be regenerated on
any platform without a vector rasteriser.

Requirements: Pillow (``python -m pip install pillow``).
"""

from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "packaging" / "icons"
ICONSET = OUT / "tpc2-next.iconset"

BG = "#0B1220"
BG_BORDER = "#1E3A4A"
BODY_TOP = (34, 53, 67)
BODY_BOTTOM = (20, 33, 43)
BODY_BORDER = "#3DDC97"
LABEL = "#0A0F14"
LABEL_BORDER = "#2B4A5A"
ACCENT = "#3DDC97"
MUTED = "#8AA0AD"
GOLD = "#D6A84B"

FONT_CANDIDATES = (
    "C:/Windows/Fonts/segoeuib.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
)


def load_font(pixel_size: int) -> ImageFont.FreeTypeFont:
    for candidate in FONT_CANDIDATES:
        if os.path.exists(candidate):
            return ImageFont.truetype(candidate, pixel_size)
    try:
        return ImageFont.load_default(size=pixel_size)
    except TypeError:
        return ImageFont.load_default()


def vertical_gradient(size: tuple[int, int], top: tuple[int, int, int],
                      bottom: tuple[int, int, int]) -> Image.Image:
    width, height = size
    column = Image.new("RGB", (1, height))
    pixels = column.load()
    for y in range(height):
        t = y / max(height - 1, 1)
        pixels[0, y] = tuple(
            round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)
        )
    return column.resize((width, height), Image.Resampling.NEAREST)


def draw_tracked_text(draw: ImageDraw.ImageDraw, center_x: float,
                      center_y: float, text: str,
                      font: ImageFont.FreeTypeFont, fill: str,
                      tracking: float) -> None:
    widths = [draw.textlength(char, font=font) for char in text]
    total = sum(widths) + tracking * (len(text) - 1)
    x = center_x - total / 2
    for char, width in zip(text, widths):
        draw.text((x, center_y), char, font=font, fill=fill, anchor="lm")
        x += width + tracking


def draw_icon(master_size: int = 2048) -> Image.Image:
    scale = master_size / 1024

    def p(value: float) -> int:
        return round(value * scale)

    image = Image.new("RGBA", (master_size, master_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    draw.rounded_rectangle(
        (p(64), p(64), p(960), p(960)),
        radius=p(192),
        fill=BG,
        outline=BG_BORDER,
        width=p(8),
    )

    # Cartridge body with a subtle top-to-bottom gradient.
    body_box = (p(300), p(190), p(724), p(830))
    body_width = body_box[2] - body_box[0]
    body_height = body_box[3] - body_box[1]
    body_mask = Image.new("L", (body_width, body_height), 0)
    ImageDraw.Draw(body_mask).rounded_rectangle(
        (0, 0, body_width - 1, body_height - 1),
        radius=p(36),
        fill=255,
    )
    body = vertical_gradient((body_width, body_height), BODY_TOP, BODY_BOTTOM)
    image.paste(body, (body_box[0], body_box[1]), body_mask)
    draw.rounded_rectangle(
        body_box,
        radius=p(36),
        outline=BODY_BORDER,
        width=p(10),
    )

    # Top seam and label recess.
    draw.line((p(300), p(250), p(724), p(250)), fill=LABEL_BORDER, width=p(6))
    draw.rounded_rectangle(
        (p(356), p(260), p(668), p(520)),
        radius=p(24),
        fill=LABEL,
        outline=LABEL_BORDER,
        width=p(6),
    )

    font_v64 = load_font(p(150))
    font_next = load_font(p(44))
    draw.text((p(512), p(390)), "V64", font=font_v64, fill=ACCENT,
              anchor="mm")
    draw_tracked_text(draw, p(512), p(486), "NEXT", font_next, MUTED,
                      p(8))

    # Edge connector and gold contacts.
    draw.rounded_rectangle(
        (p(356), p(700), p(668), p(786)),
        radius=p(12),
        fill=LABEL,
        outline=LABEL_BORDER,
        width=p(4),
    )
    for index in range(9):
        x = p(376 + index * 30)
        draw.rounded_rectangle(
            (x, p(718), x + p(14), p(768)),
            radius=p(4),
            fill=GOLD,
        )

    # Status LED with a soft halo.
    halo = Image.new("RGBA", image.size, (0, 0, 0, 0))
    halo_draw = ImageDraw.Draw(halo)
    for radius, alpha in ((p(52), 18), (p(38), 28), (p(28), 40)):
        halo_draw.ellipse(
            (p(622) - radius, p(600) - radius,
             p(622) + radius, p(600) + radius),
            fill=(61, 220, 151, alpha),
        )
    image = Image.alpha_composite(image, halo)
    draw = ImageDraw.Draw(image)
    draw.ellipse(
        (p(606), p(584), p(638), p(616)),
        fill=ACCENT,
    )

    # Faint edge highlight.
    draw.line((p(340), p(220), p(340), p(780)),
              fill=(255, 255, 255, 14), width=p(10))
    return image


def save_png(image: Image.Image, path: Path, size: int) -> None:
    resized = image.resize((size, size), Image.Resampling.LANCZOS)
    resized.save(path, "PNG", optimize=True)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    ICONSET.mkdir(parents=True, exist_ok=True)

    master = draw_icon().resize((1024, 1024), Image.Resampling.LANCZOS)
    for size in (16, 24, 32, 48, 64, 128, 256, 512, 1024):
        save_png(master, OUT / f"tpc2-next-{size}.png", size)

    master.save(
        OUT / "tpc2-next.ico",
        format="ICO",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
               (128, 128), (256, 256)],
    )

    iconset_files = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
        "icon_512x512@2x.png": 1024,
    }
    for name, size in iconset_files.items():
        save_png(master, ICONSET / name, size)

    print(f"Generated icon assets in {OUT}")


if __name__ == "__main__":
    main()
