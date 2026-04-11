#!/usr/bin/env python3
"""
Generate simple 32-bit cursor assets for the ROS userspace tree.

File format:
    Offset  Size  Field
    0x00    4     Magic: b"CUR\\0"
    0x04    4     Width (little-endian u32)
    0x08    4     Height (little-endian u32)
    0x0C    4     ActionX / hotspot X (little-endian u32)
    0x10    4     ActionY / hotspot Y (little-endian u32)
    0x14    4     DataOffset (little-endian u32)
    0x18    4     DataSize (little-endian u32)
    0x1C    32    Name (UTF-8, NUL padded)
    0x3C    96    Description (UTF-8, NUL padded)
    0x9C    32    Author (UTF-8, NUL padded)
    0xBC    ...   Pixel data, row-major RGBA8888 bytes
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import zlib
from dataclasses import dataclass


MAGIC = b"CUR\0"
NAME_SIZE = 32
DESCRIPTION_SIZE = 96
AUTHOR_SIZE = 32
HEADER_SIZE = 4 + (6 * 4) + NAME_SIZE + DESCRIPTION_SIZE + AUTHOR_SIZE


Color = tuple[int, int, int, int]

TRANSPARENT: Color = (0, 0, 0, 0)
WHITE: Color = (255, 255, 255, 255)
BLACK: Color = (0, 0, 0, 255)
LIGHT_GRAY: Color = (210, 210, 210, 255)
DARK_GRAY: Color = (118, 118, 118, 255)
SOFT_SHADOW: Color = (0, 0, 0, 36)
CORE_SHADOW: Color = (0, 0, 0, 82)
BLUE: Color = (26, 115, 232, 255)
BLUE_GLOW: Color = (26, 115, 232, 120)
GREEN: Color = (30, 160, 92, 255)
GREEN_GLOW: Color = (30, 160, 92, 120)


@dataclass(frozen=True)
class CursorAsset:
    filename: str
    name: str
    description: str
    author: str
    width: int
    height: int
    action_x: int
    action_y: int
    pixels: bytes

    @property
    def png_filename(self) -> str:
        stem = pathlib.Path(self.filename).stem
        return f"{stem}.png"


class Canvas:
    """Very small RGBA software canvas for cursor raster generation."""

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self._pixels = bytearray(width * height * 4)

    def set_pixel(self, x: int, y: int, color: Color) -> None:
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return
        offset = (y * self.width + x) * 4
        self._pixels[offset : offset + 4] = bytes(color)

    def get_pixel(self, x: int, y: int) -> Color:
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return TRANSPARENT
        offset = (y * self.width + x) * 4
        return tuple(self._pixels[offset : offset + 4])  # type: ignore[return-value]

    def blend_pixel(self, x: int, y: int, color: Color) -> None:
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return

        dst_r, dst_g, dst_b, dst_a = self.get_pixel(x, y)
        src_r, src_g, src_b, src_a = color
        if src_a == 0:
            return
        if src_a == 255 or dst_a == 0:
            self.set_pixel(x, y, color)
            return

        src_alpha = src_a / 255.0
        dst_alpha = dst_a / 255.0
        out_alpha = src_alpha + (dst_alpha * (1.0 - src_alpha))
        if out_alpha <= 0.0:
            self.set_pixel(x, y, TRANSPARENT)
            return

        out_r = int(((src_r * src_alpha) + (dst_r * dst_alpha * (1.0 - src_alpha))) / out_alpha)
        out_g = int(((src_g * src_alpha) + (dst_g * dst_alpha * (1.0 - src_alpha))) / out_alpha)
        out_b = int(((src_b * src_alpha) + (dst_b * dst_alpha * (1.0 - src_alpha))) / out_alpha)
        out_a = int(out_alpha * 255.0)
        self.set_pixel(x, y, (out_r, out_g, out_b, out_a))

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, color: Color, thickness: int = 1) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        radius = max(0, thickness // 2)

        while True:
            for oy in range(-radius, radius + 1):
                for ox in range(-radius, radius + 1):
                    self.blend_pixel(x0 + ox, y0 + oy, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = err * 2
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def fill_circle(self, cx: int, cy: int, radius: int, color: Color) -> None:
        radius_sq = radius * radius
        for y in range(cy - radius, cy + radius + 1):
            for x in range(cx - radius, cx + radius + 1):
                dx = x - cx
                dy = y - cy
                if (dx * dx) + (dy * dy) <= radius_sq:
                    self.blend_pixel(x, y, color)

    def stroke_circle(self, cx: int, cy: int, radius: int, color: Color, thickness: int = 1) -> None:
        outer_sq = radius * radius
        inner = max(0, radius - thickness)
        inner_sq = inner * inner
        for y in range(cy - radius, cy + radius + 1):
            for x in range(cx - radius, cx + radius + 1):
                dx = x - cx
                dy = y - cy
                dist_sq = (dx * dx) + (dy * dy)
                if inner_sq <= dist_sq <= outer_sq:
                    self.blend_pixel(x, y, color)

    def fill_rect(self, x: int, y: int, width: int, height: int, color: Color) -> None:
        for iy in range(y, y + height):
            for ix in range(x, x + width):
                self.blend_pixel(ix, iy, color)

    def fill_polygon(self, points: list[tuple[int, int]], color: Color) -> None:
        if len(points) < 3:
            return
        min_y = min(y for _, y in points)
        max_y = max(y for _, y in points)

        for y in range(min_y, max_y + 1):
            intersections: list[int] = []
            for index, start in enumerate(points):
                end = points[(index + 1) % len(points)]
                x0, y0 = start
                x1, y1 = end
                if y0 == y1:
                    continue
                if y < min(y0, y1) or y >= max(y0, y1):
                    continue
                x = x0 + ((y - y0) * (x1 - x0)) / (y1 - y0)
                intersections.append(int(round(x)))

            intersections.sort()
            for index in range(0, len(intersections), 2):
                if index + 1 >= len(intersections):
                    break
                x_start = intersections[index]
                x_end = intersections[index + 1]
                for x in range(x_start, x_end + 1):
                    self.blend_pixel(x, y, color)

    def to_bytes(self) -> bytes:
        return bytes(self._pixels)


def paint_mask(canvas: Canvas, rows: list[str], palette: dict[str, Color], origin_x: int = 0, origin_y: int = 0) -> None:
    for y, row in enumerate(rows):
        for x, symbol in enumerate(row):
            color = palette.get(symbol)
            if color is None:
                continue
            canvas.blend_pixel(origin_x + x, origin_y + y, color)


def encode_text(text: str, size: int) -> bytes:
    encoded = text.encode("utf-8")
    if len(encoded) >= size:
        raise ValueError(f"field too long for fixed size {size}: {text!r}")
    return encoded + (b"\0" * (size - len(encoded)))


def serialize_cursor(asset: CursorAsset) -> bytes:
    expected_size = asset.width * asset.height * 4
    if len(asset.pixels) != expected_size:
        raise ValueError(
            f"{asset.filename}: expected {expected_size} bytes of RGBA pixel data, got {len(asset.pixels)}"
        )

    header = bytearray()
    header += MAGIC
    header += struct.pack(
        "<6I",
        asset.width,
        asset.height,
        asset.action_x,
        asset.action_y,
        HEADER_SIZE,
        len(asset.pixels),
    )
    header += encode_text(asset.name, NAME_SIZE)
    header += encode_text(asset.description, DESCRIPTION_SIZE)
    header += encode_text(asset.author, AUTHOR_SIZE)
    return bytes(header) + asset.pixels


def _png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + chunk_type
        + data
        + struct.pack(">I", zlib.crc32(chunk_type + data) & 0xFFFFFFFF)
    )


def serialize_png_rgba(width: int, height: int, pixels: bytes) -> bytes:
    expected_size = width * height * 4
    if len(pixels) != expected_size:
        raise ValueError(f"expected {expected_size} RGBA bytes, got {len(pixels)}")

    signature = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    scanlines = bytearray()
    stride = width * 4
    for row in range(height):
        scanlines.append(0)
        start = row * stride
        scanlines += pixels[start : start + stride]
    idat = zlib.compress(bytes(scanlines), level=9)
    return signature + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", idat) + _png_chunk(b"IEND", b"")


def _bit_is_set(buffer: bytes, stride: int, x: int, y: int, height: int) -> bool:
    row_offset = (height - 1 - y) * stride
    byte = buffer[row_offset + (x // 8)]
    return (byte & (0x80 >> (x % 8))) != 0


def import_windows_cursor(
    input_path: pathlib.Path,
    output_filename: str,
    name: str | None = None,
    description: str | None = None,
    author: str = "OpenAI Codex",
) -> CursorAsset:
    payload = input_path.read_bytes()
    if len(payload) < 22:
        raise ValueError(f"{input_path}: file too small to be a .cur file")

    reserved, cursor_type, image_count = struct.unpack_from("<HHH", payload, 0)
    if reserved != 0 or cursor_type != 2 or image_count == 0:
        raise ValueError(f"{input_path}: unsupported cursor directory header")

    (
        width_byte,
        height_byte,
        _color_count,
        _entry_reserved,
        hotspot_x,
        hotspot_y,
        image_size,
        image_offset,
    ) = struct.unpack_from("<BBBBHHII", payload, 6)

    width = width_byte or 256
    height = height_byte or 256
    image = payload[image_offset : image_offset + image_size]
    if len(image) != image_size:
        raise ValueError(f"{input_path}: truncated cursor image payload")
    if image.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"{input_path}: PNG-backed .cur images are not supported by this importer")

    if len(image) < 40:
        raise ValueError(f"{input_path}: cursor DIB header is truncated")

    (
        dib_header_size,
        dib_width,
        dib_height,
        dib_planes,
        dib_bpp,
        dib_compression,
        _dib_image_size,
        _dib_xppm,
        _dib_yppm,
        color_used,
        _color_important,
    ) = struct.unpack_from("<IIIHHIIIIII", image, 0)

    if dib_header_size < 40 or dib_planes != 1 or dib_compression != 0:
        raise ValueError(f"{input_path}: unsupported DIB cursor encoding")

    if dib_width != width:
        width = dib_width
    if dib_height % 2 != 0:
        raise ValueError(f"{input_path}: unexpected DIB height {dib_height}")
    height = dib_height // 2

    pixel_offset = dib_header_size
    if dib_bpp == 1:
        palette_entries = color_used or 2
        pixel_offset += palette_entries * 4
        stride = ((width * dib_bpp + 31) // 32) * 4
        plane_size = stride * height
        xor_mask = image[pixel_offset : pixel_offset + plane_size]
        and_mask = image[pixel_offset + plane_size : pixel_offset + (2 * plane_size)]
        if len(xor_mask) != plane_size or len(and_mask) != plane_size:
            raise ValueError(f"{input_path}: truncated monochrome cursor masks")

        pixels = bytearray(width * height * 4)
        for y in range(height):
            for x in range(width):
                xor_bit = _bit_is_set(xor_mask, stride, x, y, height)
                and_bit = _bit_is_set(and_mask, stride, x, y, height)
                if and_bit:
                    color = TRANSPARENT
                elif xor_bit:
                    color = WHITE
                else:
                    color = BLACK
                offset = (y * width + x) * 4
                pixels[offset : offset + 4] = bytes(color)
    elif dib_bpp == 32:
        stride = width * 4
        xor_size = stride * height
        xor_pixels = image[pixel_offset : pixel_offset + xor_size]
        if len(xor_pixels) != xor_size:
            raise ValueError(f"{input_path}: truncated 32-bit cursor pixel data")

        and_stride = ((width + 31) // 32) * 4
        and_size = and_stride * height
        and_mask = image[pixel_offset + xor_size : pixel_offset + xor_size + and_size]
        has_and_mask = len(and_mask) == and_size
        pixels = bytearray(width * height * 4)

        for y in range(height):
            src_row = (height - 1 - y) * stride
            for x in range(width):
                src = src_row + (x * 4)
                blue, green, red, alpha = xor_pixels[src : src + 4]
                if alpha == 0 and has_and_mask and _bit_is_set(and_mask, and_stride, x, y, height):
                    color = TRANSPARENT
                else:
                    color = (red, green, blue, alpha if alpha != 0 else 255)
                dst = (y * width + x) * 4
                pixels[dst : dst + 4] = bytes(color)
    else:
        raise ValueError(f"{input_path}: unsupported cursor bit depth {dib_bpp}")

    return CursorAsset(
        filename=output_filename,
        name=name or pathlib.Path(output_filename).stem,
        description=description or f"Imported from {input_path.name}.",
        author=author,
        width=width,
        height=height,
        action_x=hotspot_x,
        action_y=hotspot_y,
        pixels=bytes(pixels),
    )


def draw_arrow_cursor() -> CursorAsset:
    canvas = Canvas(32, 32)

    # XP-style arrow with a soft shadow and gray bevel on the lower edge.
    arrow_rows = [
        "................................",
        ".O..............................",
        ".OWO............................",
        ".OWWO...........................",
        ".OWWWO..........................",
        ".OWWWWO.........................",
        ".OWWWWWO........................",
        ".OWWWWWWO.......................",
        ".OWWWWWWWO......................",
        ".OWWWWWWWWO.....................",
        ".OWWWWWWWWWO....................",
        ".OWWWWWWWWWWO...................",
        ".OWWWWWWWWWWWO..................",
        ".OWWWWWWWWWWWWO.................",
        ".OWWWWWWWWWWWWWO................",
        ".OWWWWWWWWWWWWWWO...............",
        ".OWWWWWWWWWWWWWWWO..............",
        ".OWWWWWWWWWWWWWWWWOOO...........",
        ".OWWWWWWWWWWWGGGGGDDOO..........",
        ".OWWWWWOOWWWWGGGGDDDDOO.........",
        ".OWWWOO..OWWWWGGGDDDDDO.........",
        ".OWWO....OWWWWGGGDDDDDO.........",
        ".OO......OWWWO.GGDDDDO..........",
        ".........OWWO..GGDDDO...........",
        ".........OWO...GGDDDO...........",
        ".........OO....GGDDO............",
        "...............GDDO.............",
        "...............GDDO.............",
        "...............ODO..............",
        "................O...............",
        "................................",
        "................................",
    ]
    palette = {
        "O": BLACK,
        "W": WHITE,
        "G": LIGHT_GRAY,
        "D": DARK_GRAY,
    }

    shadow_chars = {"O", "W", "G", "D"}
    for y, row in enumerate(arrow_rows):
        for x, symbol in enumerate(row):
            if symbol not in shadow_chars:
                continue
            canvas.blend_pixel(x + 2, y + 2, SOFT_SHADOW)
            canvas.blend_pixel(x + 1, y + 1, CORE_SHADOW)

    paint_mask(canvas, arrow_rows, palette)

    return CursorAsset(
        filename="arrow.cur32",
        name="arrow",
        description="XP-style diagonal pointer with a beveled fill and soft shadow.",
        author="OpenAI Codex",
        width=32,
        height=32,
        action_x=1,
        action_y=1,
        pixels=canvas.to_bytes(),
    )


def draw_crosshair_cursor() -> CursorAsset:
    canvas = Canvas(24, 24)
    center = 12

    canvas.stroke_circle(center, center, 5, BLUE_GLOW, thickness=3)
    canvas.stroke_circle(center, center, 5, BLUE, thickness=1)
    canvas.draw_line(center, 2, center, 21, BLACK, thickness=3)
    canvas.draw_line(2, center, 21, center, BLACK, thickness=3)
    canvas.draw_line(center, 2, center, 21, WHITE, thickness=1)
    canvas.draw_line(2, center, 21, center, WHITE, thickness=1)

    return CursorAsset(
        filename="crosshair.cur32",
        name="crosshair",
        description="Centered crosshair cursor for precision picking.",
        author="OpenAI Codex",
        width=24,
        height=24,
        action_x=center,
        action_y=center,
        pixels=canvas.to_bytes(),
    )


def draw_busy_cursor() -> CursorAsset:
    canvas = Canvas(24, 24)
    center = 12

    canvas.stroke_circle(center, center, 7, GREEN_GLOW, thickness=3)
    canvas.stroke_circle(center, center, 7, GREEN, thickness=2)
    canvas.fill_circle(center, center, 3, WHITE)
    canvas.fill_rect(center - 1, 4, 2, 5, WHITE)
    canvas.fill_rect(15, center - 1, 4, 2, WHITE)

    return CursorAsset(
        filename="busy.cur32",
        name="busy",
        description="Simple busy cursor with a ring and clock hand accent.",
        author="OpenAI Codex",
        width=24,
        height=24,
        action_x=center,
        action_y=center,
        pixels=canvas.to_bytes(),
    )


def write_asset(output_dir: pathlib.Path, asset: CursorAsset) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / asset.filename
    path.write_bytes(serialize_cursor(asset))
    return path


def write_png_preview(output_dir: pathlib.Path, asset: CursorAsset) -> pathlib.Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / asset.png_filename
    path.write_bytes(serialize_png_rgba(asset.width, asset.height, asset.pixels))
    return path


def build_sample_assets(output_dir: pathlib.Path) -> list[pathlib.Path]:
    assets = [
        draw_arrow_cursor(),
        draw_crosshair_cursor(),
        draw_busy_cursor(),
    ]
    written: list[pathlib.Path] = []
    for asset in assets:
        written.append(write_asset(output_dir, asset))
        written.append(write_png_preview(output_dir, asset))
    return written


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate custom CUR\\0 cursor assets.")
    parser.add_argument(
        "--output-dir",
        default="applications/assets/cursors",
        help="Directory that receives generated .cur32 files.",
    )
    parser.add_argument(
        "--import-cur",
        help="Import one Windows .cur file and emit a matching .cur32 asset plus PNG preview.",
    )
    parser.add_argument(
        "--filename",
        help="Output .cur32 filename when using --import-cur.",
    )
    parser.add_argument(
        "--name",
        help="Cursor metadata name for imported assets.",
    )
    parser.add_argument(
        "--description",
        help="Cursor metadata description for imported assets.",
    )
    parser.add_argument(
        "--author",
        default="OpenAI Codex",
        help="Cursor metadata author.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = pathlib.Path(args.output_dir)
    if args.import_cur:
        input_path = pathlib.Path(args.import_cur)
        output_filename = args.filename or f"{input_path.stem}.cur32"
        asset = import_windows_cursor(
            input_path=input_path,
            output_filename=output_filename,
            name=args.name,
            description=args.description,
            author=args.author,
        )
        written = [
            write_asset(output_dir, asset),
            write_png_preview(output_dir, asset),
        ]
    else:
        written = build_sample_assets(output_dir)
    for path in written:
        print(f"[ok] wrote {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
