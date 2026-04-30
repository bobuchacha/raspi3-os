#!/usr/bin/env python3
"""Generate the Explorer start-button glyph header from the staged PNG asset."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image

ICON_SIZE = 16
DEFAULT_INPUT = Path("applications/assets/icons/start_menu.png")


def build_alpha_values(input_path: Path) -> list[int]:
    """Return one 16x16 alpha mask sampled from the staged Start-menu PNG."""
    image = Image.open(input_path).convert("RGBA")
    alpha = image.getchannel("A")
    bounding_box = alpha.getbbox()

    if bounding_box is None:
        raise ValueError(f"start icon asset {input_path} has no visible alpha content")

    resized_alpha = alpha.crop(bounding_box).resize((ICON_SIZE, ICON_SIZE), Image.Resampling.LANCZOS)
    return list(resized_alpha.getdata())


def render_header(input_path: Path) -> str:
    """Render the generated C header for the Explorer in-binary Start icon."""
    values = build_alpha_values(input_path)
    lines = [
        "#ifndef ROS_EXPLORER_GENERATED_START_ICON_H",
        "#define ROS_EXPLORER_GENERATED_START_ICON_H",
        "",
        f"static const unsigned char g_start_icon_alpha[{ICON_SIZE} * {ICON_SIZE}] = {{",
    ]

    for row_index in range(0, len(values), ICON_SIZE):
        row = ", ".join(str(value) for value in values[row_index:row_index + ICON_SIZE])
        suffix = "," if row_index + ICON_SIZE < len(values) else ""
        lines.append(f"    {row}{suffix}")

    lines.extend([
        "};",
        "",
        "#endif",
    ])
    return "\n".join(lines) + "\n"


def main() -> int:
    """Parse CLI arguments and write the generated Start icon header."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default=str(DEFAULT_INPUT), help="PNG asset path to sample")
    parser.add_argument("--output", required=True, help="Header path to generate")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_header(input_path), encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
