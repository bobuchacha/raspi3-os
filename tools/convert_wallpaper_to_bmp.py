#!/usr/bin/env python3
"""
Convert one wallpaper image into a BMP asset for kernels that do not yet
decode JPEG directly.

The script prefers Pillow when available because it lets Python decode the
source image and emit a deterministic BMP. On macOS hosts without Pillow, it
falls back to `sips`, which is present by default and can still convert JPEG
to BMP for staging under `applications/assets/wallpapers/`.
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
from typing import Iterable


DEFAULT_INPUT = pathlib.Path("applications/assets/wallpapers/bliss.jpg")
DEFAULT_OUTPUT = pathlib.Path("applications/assets/wallpapers/bliss.bmp")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert a wallpaper image to BMP.")
    parser.add_argument("input", nargs="?", default=str(DEFAULT_INPUT), help="Input image path.")
    parser.add_argument("output", nargs="?", default=str(DEFAULT_OUTPUT), help="Output BMP path.")
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="Replace the output file when it already exists.",
    )
    return parser.parse_args()


def ensure_parent_directory(path: pathlib.Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def write_bmp_rgba(width: int, height: int, pixels: bytes, output_path: pathlib.Path) -> None:
    """
    Write a top-down 32-bit BGRA BMP.

    The input pixel buffer is expected to be row-major RGBA8888. BMP stores
    pixels in BGRA byte order, so the channel order is rewritten during emit.
    """

    expected_size = width * height * 4
    if len(pixels) != expected_size:
        raise ValueError(f"expected {expected_size} RGBA bytes, got {len(pixels)}")

    dib_size = 124
    pixel_offset = 14 + dib_size
    image_size = expected_size
    file_size = pixel_offset + image_size

    header = bytearray()
    header += b"BM"
    header += struct.pack("<I", file_size)
    header += struct.pack("<HH", 0, 0)
    header += struct.pack("<I", pixel_offset)

    # BITMAPV5HEADER so readers have explicit channel masks for 32-bit pixels.
    header += struct.pack("<I", dib_size)
    header += struct.pack("<i", width)
    header += struct.pack("<i", -height)
    header += struct.pack("<H", 1)
    header += struct.pack("<H", 32)
    header += struct.pack("<I", 3)  # BI_BITFIELDS
    header += struct.pack("<I", image_size)
    header += struct.pack("<i", 2835)
    header += struct.pack("<i", 2835)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0x00FF0000)  # red mask
    header += struct.pack("<I", 0x0000FF00)  # green mask
    header += struct.pack("<I", 0x000000FF)  # blue mask
    header += struct.pack("<I", 0xFF000000)  # alpha mask
    header += b"sRGB"
    header += b"\0" * 36  # endpoints
    header += struct.pack("<III", 0, 0, 0)  # gamma RGB
    header += struct.pack("<I", 4)  # LCS_sRGB
    header += struct.pack("<I", 0)  # profile data
    header += struct.pack("<I", 0)  # profile size
    header += struct.pack("<I", 0)  # reserved

    pixel_bytes = bytearray(image_size)
    for offset in range(0, expected_size, 4):
        red = pixels[offset]
        green = pixels[offset + 1]
        blue = pixels[offset + 2]
        alpha = pixels[offset + 3]
        pixel_bytes[offset : offset + 4] = bytes((blue, green, red, alpha))

    output_path.write_bytes(bytes(header) + bytes(pixel_bytes))


def convert_with_pillow(input_path: pathlib.Path, output_path: pathlib.Path) -> bool:
    try:
        from PIL import Image  # type: ignore
    except Exception:
        return False

    with Image.open(input_path) as image:
        rgba = image.convert("RGBA")
        width, height = rgba.size
        write_bmp_rgba(width, height, rgba.tobytes(), output_path)
    return True


def run_command(command: Iterable[str]) -> None:
    subprocess.run(list(command), check=True)


def convert_with_sips(input_path: pathlib.Path, output_path: pathlib.Path) -> bool:
    sips_path = shutil.which("sips")
    if not sips_path:
        return False

    run_command([sips_path, "-s", "format", "bmp", str(input_path), "--out", str(output_path)])
    return True


def main() -> int:
    args = parse_args()
    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    if not input_path.is_file():
        print(f"error: input file not found: {input_path}", file=sys.stderr)
        return 1
    if output_path.exists() and not args.overwrite:
        print(f"error: output already exists: {output_path}", file=sys.stderr)
        print("hint: pass --overwrite to replace it", file=sys.stderr)
        return 1

    ensure_parent_directory(output_path)

    if convert_with_pillow(input_path, output_path):
        print(f"[ok] wrote {output_path} using Pillow")
        return 0
    if convert_with_sips(input_path, output_path):
        print(f"[ok] wrote {output_path} using sips")
        return 0

    print(
        "error: no image decoder available; install Pillow or run on macOS with /usr/bin/sips",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
