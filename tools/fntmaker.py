"""Generate prerasterized ROS raster font binaries from a TTF face."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont


DEFAULT_INPUT = Path("applications/assets/fonts/tahoma.ttf")
DEFAULT_OUTPUT_DIR = Path("applications/assets/fonts")
DEFAULT_SIZES = (12, 14, 16, 18)
ASCII_GLYPH_COUNT = 128
RTF_MAGIC = b"ROSRTF1\0"
RTF_VERSION = 1
RTF_FAMILY_BYTES = 64
RTF_RESERVED_ENTRY = 0
RTF_HEADER_STRUCT = struct.Struct("<8sIIIIiiIII64s")
RTF_GLYPH_ENTRY_STRUCT = struct.Struct("<IIIiiIII")


@dataclass(frozen=True)
class RasterGlyph:
    """Store one prerasterized ASCII glyph.

    Args:
        codepoint: ASCII codepoint written into the descriptor.
        advance: Cursor advance in pixels after drawing this glyph.
        bitmap_left: Horizontal left-side bearing relative to the cursor.
        bitmap_top: Distance in pixels from the baseline to the glyph top.
        width: Glyph bitmap width in pixels.
        height: Glyph bitmap height in pixels.
        coverage: Packed eight-bit alpha coverage rows.

    Returns:
        A value object describing one glyph.
    """

    codepoint: int
    advance: int
    bitmap_left: int
    bitmap_top: int
    width: int
    height: int
    coverage: bytes


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for raster font generation.

    Args:
        None.

    Returns:
        Parsed command-line options.
    """

    parser = argparse.ArgumentParser(
        description="Rasterize one TTF face into ROS raster font binaries.",
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=DEFAULT_INPUT,
        help="Path to the source TTF file.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory that receives generated .rtf files.",
    )
    parser.add_argument(
        "--prefix",
        default="tahoma",
        help="Output file prefix, for example 'tahoma'.",
    )
    parser.add_argument(
        "--family",
        default="Tahoma",
        help="Family name embedded into the generated descriptor.",
    )
    parser.add_argument(
        "--sizes",
        type=int,
        nargs="+",
        default=list(DEFAULT_SIZES),
        help="Pixel heights to prerasterize.",
    )
    return parser.parse_args()


def normalize_output_sizes(raw_sizes: Iterable[int]) -> list[int]:
    """Validate and de-duplicate requested pixel sizes.

    Args:
        raw_sizes: Sizes requested on the command line.

    Returns:
        Sorted unique positive sizes.
    """

    normalized = sorted({size for size in raw_sizes if size > 0})
    if not normalized:
        raise ValueError("at least one positive size is required")
    return normalized


def render_glyph(font: ImageFont.FreeTypeFont, codepoint: int) -> RasterGlyph:
    """Rasterize one ASCII glyph using Pillow's grayscale coverage.

    Args:
        font: Loaded Pillow font at the target pixel size.
        codepoint: ASCII codepoint to prerender.

    Returns:
        One prerasterized glyph descriptor.
    """

    character = chr(codepoint)
    advance = int(round(font.getlength(character)))

    if codepoint != 32 and (codepoint < 32 or codepoint == 127):
        return RasterGlyph(codepoint, 0, 0, 0, 0, 0, b"")

    bbox = font.getbbox(character, anchor="ls")
    if bbox is None:
        return RasterGlyph(codepoint, max(advance, 0), 0, 0, 0, 0, b"")

    left, top, right, bottom = (int(value) for value in bbox)
    width = max(right - left, 0)
    height = max(bottom - top, 0)
    if width == 0 or height == 0:
        return RasterGlyph(codepoint, max(advance, 0), left, -top, 0, 0, b"")

    # Drawing from a baseline anchor preserves the metrics that GDI uses when
    # it later positions glyph bitmaps against the current text baseline.
    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    draw.text((-left, -top), character, font=font, fill=255, anchor="ls")

    if advance <= 0:
        advance = max(right, width)

    return RasterGlyph(
        codepoint=codepoint,
        advance=advance,
        bitmap_left=left,
        bitmap_top=-top,
        width=width,
        height=height,
        coverage=image.tobytes(),
    )


def pack_family_name(family: str) -> bytes:
    """Pack the family name into the fixed-width binary header field.

    Args:
        family: Human-readable family name stored in the file metadata.

    Returns:
        A null-padded fixed-width family-name field.
    """

    encoded = family.encode("utf-8")[: RTF_FAMILY_BYTES - 1]
    return encoded.ljust(RTF_FAMILY_BYTES, b"\0")


def build_raster_font_blob(
    source_path: Path,
    family: str,
    size: int,
) -> bytes:
    """Build one binary raster font payload.

    Args:
        source_path: Source TTF file used for rasterization.
        family: Family name written into the binary metadata.
        size: Pixel height to prerasterize.

    Returns:
        Full binary `.rtf` payload ready to write to disk.
    """

    font = ImageFont.truetype(str(source_path), size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    glyphs = [render_glyph(font, codepoint) for codepoint in range(ASCII_GLYPH_COUNT)]
    header_size = RTF_HEADER_STRUCT.size
    lookup_table_offset = header_size
    glyph_entry_size = RTF_GLYPH_ENTRY_STRUCT.size
    glyph_data_offset = lookup_table_offset + (glyph_entry_size * len(glyphs))
    lookup_entries: list[bytes] = []
    glyph_blob = bytearray()

    for glyph in glyphs:
        coverage_size = len(glyph.coverage)
        if coverage_size == 0:
            coverage_offset = 0
        else:
            coverage_offset = glyph_data_offset + len(glyph_blob)
            glyph_blob.extend(glyph.coverage)

        lookup_entries.append(
            RTF_GLYPH_ENTRY_STRUCT.pack(
                coverage_offset,
                coverage_size,
                glyph.advance,
                glyph.bitmap_left,
                glyph.bitmap_top,
                glyph.width,
                glyph.height,
                RTF_RESERVED_ENTRY,
            )
        )

    header = RTF_HEADER_STRUCT.pack(
        RTF_MAGIC,
        RTF_VERSION,
        header_size,
        size,
        line_height,
        ascent,
        descent,
        len(glyphs),
        lookup_table_offset,
        glyph_entry_size,
        pack_family_name(family),
    )
    return header + b"".join(lookup_entries) + bytes(glyph_blob)


def write_raster_fonts(
    source_path: Path,
    output_dir: Path,
    prefix: str,
    family: str,
    sizes: Iterable[int],
) -> None:
    """Generate and write every requested prerasterized font file.

    Args:
        source_path: Source TTF file used for rasterization.
        output_dir: Destination directory for generated raster files.
        prefix: File name prefix used for every output file.
        family: Family name written into the binary metadata.
        sizes: Pixel sizes to generate.

    Returns:
        None.
    """

    output_dir.mkdir(parents=True, exist_ok=True)
    for size in sizes:
        font_blob = build_raster_font_blob(source_path, family, size)
        output_path = output_dir / f"{prefix}-{size}.rtf"
        output_path.write_bytes(font_blob)
        print(output_path)


def main() -> int:
    """Run the font generator CLI.

    Args:
        None.

    Returns:
        Process exit status.
    """

    args = parse_args()
    sizes = normalize_output_sizes(args.sizes)
    write_raster_fonts(args.input, args.output_dir, args.prefix, args.family, sizes)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())