"""Render the application's native ball motif as a 128px RGBA launcher icon."""

from pathlib import Path
from PIL import Image, ImageDraw


def main():
    scale = 4
    image = Image.new("RGBA", (128 * scale, 128 * scale), "#f2f2f7")
    draw = ImageDraw.Draw(image)
    draw.line((0, 64 * scale, 128 * scale, 64 * scale), fill="#d8dde3", width=scale)
    draw.line((64 * scale, 0, 64 * scale, 128 * scale), fill="#d8dde3", width=scale)
    draw.ellipse((24 * scale, 24 * scale, 104 * scale, 104 * scale), outline="#d8dde3", width=scale)
    draw.ellipse((44 * scale, 44 * scale, 84 * scale, 84 * scale), outline="#d8dde3", width=scale)
    draw.ellipse((53 * scale, 35 * scale, 107 * scale, 89 * scale), fill="#008b83", outline="#00645f", width=2 * scale)
    draw.ellipse((84 * scale, 48 * scale, 94 * scale, 58 * scale), fill="white")
    image.resize((128, 128), Image.Resampling.LANCZOS).save(Path(__file__).resolve().parents[1] / "assets/app-icon.png")


if __name__ == "__main__":
    main()
