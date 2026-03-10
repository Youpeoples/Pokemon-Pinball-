#!/usr/bin/env python3
"""Collision Map Tool for Pokemon Pinball+

Visualize, validate, and inspect collision map files (.collision) and
collision mask PNGs used by the Pokemon Pinball+ collision system.

Requires: Python 3.6+
Optional: Pillow (PIL) for --visualize and --export-masks modes
          Install with: pip install Pillow

Usage examples:

  Show info about a collision map:
    python collision_map_tool.py --info data/collision/maps/red_stage_bottom.collision

  Validate a collision map file:
    python collision_map_tool.py --validate data/collision/maps/red_stage_bottom.collision

  Visualize a collision map as a colored PNG grid:
    python collision_map_tool.py --visualize data/collision/maps/red_stage_bottom.collision -o viz.png

  Export individual mask tiles from a mask PNG:
    python collision_map_tool.py --export-masks data/collision/masks/red_stage_bottom.png -o mask_tiles/

  Visualize with a custom scale factor:
    python collision_map_tool.py --visualize data/collision/maps/red_stage_bottom.collision -o viz.png --scale 16
"""

import argparse
import os
import sys
from collections import Counter

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

EXPECTED_FILE_SIZE = 1024       # .collision files are 1024 bytes on disk
LOADED_SIZE = 768               # Only first 0x300 bytes are loaded by the engine
ROW_STRIDE = 32                 # 32 bytes per row in the collision map
VISIBLE_COLUMNS = 18            # 18 tiles wide (144 pixels)
LOADED_ROWS = LOADED_SIZE // ROW_STRIDE  # 24 rows loaded
TOTAL_ROWS = EXPECTED_FILE_SIZE // ROW_STRIDE  # 32 rows total in file

MASK_EMPTY = 0x00
MASK_STATIC_MAX = 0xCF
MASK_MON_MIN = 0xD0
MASK_MON_MAX = 0xDF
MASK_FLIPPER_LEFT_MIN = 0xE0
MASK_FLIPPER_LEFT_MAX = 0xEF
MASK_FLIPPER_RIGHT_MIN = 0xF0
MASK_FLIPPER_RIGHT_MAX = 0xFF

# Colors for visualization (RGB tuples)
COLOR_EMPTY = (32, 32, 32)            # Dark gray for empty tiles
COLOR_STATIC = (40, 100, 200)         # Blue for static collision masks
COLOR_MON = (220, 200, 40)            # Yellow for animated mon masks
COLOR_FLIPPER_LEFT = (200, 50, 50)    # Red for left flipper masks
COLOR_FLIPPER_RIGHT = (220, 100, 50)  # Orange for right flipper masks
COLOR_PADDING = (16, 16, 16)          # Near-black for padding columns
COLOR_UNLOADED = (10, 10, 10)         # Very dark for unloaded rows (24-31)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def classify_index(value):
    """Return a human-readable classification for a mask index."""
    if value == MASK_EMPTY:
        return "empty"
    elif value <= MASK_STATIC_MAX:
        return "static"
    elif value <= MASK_MON_MAX:
        return "animated_mon"
    elif value <= MASK_FLIPPER_LEFT_MAX:
        return "flipper_left"
    else:
        return "flipper_right"


def color_for_index(value):
    """Return an RGB color tuple for a mask index."""
    if value == MASK_EMPTY:
        return COLOR_EMPTY
    elif value <= MASK_STATIC_MAX:
        # Vary brightness by index for visual distinction
        brightness = 80 + int((value / MASK_STATIC_MAX) * 150)
        return (30, min(brightness, 200), min(brightness + 40, 255))
    elif value <= MASK_MON_MAX:
        return COLOR_MON
    elif value <= MASK_FLIPPER_LEFT_MAX:
        return COLOR_FLIPPER_LEFT
    else:
        return COLOR_FLIPPER_RIGHT


def load_collision_map(filepath):
    """Load a .collision file and return its raw bytes."""
    with open(filepath, "rb") as f:
        data = f.read()
    return data


def get_grid_value(data, row, col):
    """Read a single byte from the collision map grid."""
    offset = row * ROW_STRIDE + col
    if offset < len(data):
        return data[offset]
    return 0


def try_import_pil():
    """Try to import PIL/Pillow, return Image module or None."""
    try:
        from PIL import Image
        return Image
    except ImportError:
        return None


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_info(args):
    """Print info about a collision map file."""
    filepath = args.file
    if not os.path.isfile(filepath):
        print(f"Error: File not found: {filepath}", file=sys.stderr)
        return 1

    data = load_collision_map(filepath)
    file_size = len(data)

    print(f"File:       {filepath}")
    print(f"File size:  {file_size} bytes (0x{file_size:04X})")
    print(f"Expected:   {EXPECTED_FILE_SIZE} bytes (0x{EXPECTED_FILE_SIZE:04X})")
    print(f"Grid:       {ROW_STRIDE} columns x {TOTAL_ROWS} rows (full file)")
    print(f"Loaded:     {ROW_STRIDE} columns x {LOADED_ROWS} rows ({LOADED_SIZE} bytes)")
    print(f"Visible:    {VISIBLE_COLUMNS} columns x {LOADED_ROWS} rows")
    print()

    # Analyze only the loaded portion (first 768 bytes), visible columns only
    loaded_data = data[:LOADED_SIZE]
    visible_values = []
    for row in range(LOADED_ROWS):
        for col in range(VISIBLE_COLUMNS):
            offset = row * ROW_STRIDE + col
            if offset < len(loaded_data):
                visible_values.append(loaded_data[offset])

    total_visible = len(visible_values)
    counter = Counter(visible_values)

    # Classification counts
    empty_count = counter.get(0x00, 0)
    static_count = sum(v for k, v in counter.items() if 0x01 <= k <= MASK_STATIC_MAX)
    mon_count = sum(v for k, v in counter.items() if MASK_MON_MIN <= k <= MASK_MON_MAX)
    flipper_count = sum(v for k, v in counter.items()
                        if MASK_FLIPPER_LEFT_MIN <= k <= MASK_FLIPPER_RIGHT_MAX)

    print(f"--- Visible Tile Statistics (columns 0-{VISIBLE_COLUMNS - 1}, rows 0-{LOADED_ROWS - 1}) ---")
    print(f"Total tiles:    {total_visible}")
    print(f"Empty (0x00):   {empty_count:4d}  ({100.0 * empty_count / total_visible:5.1f}%)")
    print(f"Static (01-CF): {static_count:4d}  ({100.0 * static_count / total_visible:5.1f}%)")
    print(f"Mon (D0-DF):    {mon_count:4d}  ({100.0 * mon_count / total_visible:5.1f}%)")
    print(f"Flipper (E0-FF):{flipper_count:4d}  ({100.0 * flipper_count / total_visible:5.1f}%)")
    print()

    # Histogram of unique values
    unique_sorted = sorted(counter.keys())
    print(f"Unique mask indices: {len(unique_sorted)}")
    print()
    print("Index  Count  Category")
    print("-----  -----  --------")
    for idx in unique_sorted:
        cat = classify_index(idx)
        print(f" 0x{idx:02X}  {counter[idx]:5d}  {cat}")

    # Check padding columns
    padding_nonzero = 0
    for row in range(LOADED_ROWS):
        for col in range(VISIBLE_COLUMNS, ROW_STRIDE):
            offset = row * ROW_STRIDE + col
            if offset < len(loaded_data) and loaded_data[offset] != 0:
                padding_nonzero += 1

    if padding_nonzero > 0:
        print()
        print(f"Warning: {padding_nonzero} non-zero bytes found in padding columns ({VISIBLE_COLUMNS}-{ROW_STRIDE - 1})")

    return 0


def cmd_validate(args):
    """Validate a collision map file."""
    filepath = args.file
    if not os.path.isfile(filepath):
        print(f"Error: File not found: {filepath}", file=sys.stderr)
        return 1

    data = load_collision_map(filepath)
    file_size = len(data)
    errors = []
    warnings = []

    # Check file size
    if file_size != EXPECTED_FILE_SIZE:
        if file_size == LOADED_SIZE:
            warnings.append(
                f"File is {file_size} bytes (loaded size only). "
                f"Standard files are {EXPECTED_FILE_SIZE} bytes. "
                f"The engine will load this but it lacks the standard padding rows."
            )
        elif file_size < LOADED_SIZE:
            errors.append(
                f"File is {file_size} bytes, which is less than the minimum "
                f"loaded size of {LOADED_SIZE} bytes. The engine will zero-fill "
                f"the missing portion, which may cause unexpected behavior."
            )
        else:
            warnings.append(
                f"File is {file_size} bytes (expected {EXPECTED_FILE_SIZE}). "
                f"Only the first {LOADED_SIZE} bytes will be loaded."
            )

    # Analyze loaded portion
    loaded_data = data[:min(len(data), LOADED_SIZE)]
    unique_indices = set()
    mon_indices = set()
    flipper_indices = set()

    for row in range(min(LOADED_ROWS, len(loaded_data) // ROW_STRIDE + 1)):
        for col in range(VISIBLE_COLUMNS):
            offset = row * ROW_STRIDE + col
            if offset < len(loaded_data):
                val = loaded_data[offset]
                unique_indices.add(val)
                if MASK_MON_MIN <= val <= MASK_MON_MAX:
                    mon_indices.add(val)
                elif MASK_FLIPPER_LEFT_MIN <= val <= MASK_FLIPPER_RIGHT_MAX:
                    flipper_indices.add(val)

    # Check for dynamic mask usage
    if mon_indices:
        warnings.append(
            f"Uses animated mon mask indices: "
            f"{', '.join(f'0x{v:02X}' for v in sorted(mon_indices))}. "
            f"These are only valid on bottom-half stages during catch'em mode."
        )

    if flipper_indices:
        warnings.append(
            f"Uses flipper mask indices: "
            f"{', '.join(f'0x{v:02X}' for v in sorted(flipper_indices))}. "
            f"These are only valid on bottom-half stages (odd stage IDs) with flippers."
        )

    # Find highest static mask index
    max_static = 0
    for idx in unique_indices:
        if 0x01 <= idx <= MASK_STATIC_MAX:
            max_static = max(max_static, idx)

    if max_static > 0:
        min_masks_needed = max_static + 1
        print(f"Highest static mask index used: 0x{max_static:02X} ({max_static})")
        print(f"Mask PNG must contain at least {min_masks_needed} tiles "
              f"(indices 0x00-0x{max_static:02X})")

    # Check padding columns for non-zero data
    padding_nonzero_positions = []
    for row in range(min(LOADED_ROWS, len(loaded_data) // ROW_STRIDE + 1)):
        for col in range(VISIBLE_COLUMNS, ROW_STRIDE):
            offset = row * ROW_STRIDE + col
            if offset < len(loaded_data) and loaded_data[offset] != 0:
                padding_nonzero_positions.append((row, col, loaded_data[offset]))

    if padding_nonzero_positions:
        count = len(padding_nonzero_positions)
        warnings.append(
            f"{count} non-zero byte(s) in padding columns ({VISIBLE_COLUMNS}-{ROW_STRIDE - 1}). "
            f"These are in memory but not part of the visible 18-tile-wide playfield."
        )

    # Report results
    print()
    print(f"File: {filepath}")
    print(f"Size: {file_size} bytes")
    print(f"Unique mask indices: {len(unique_indices)}")
    print(f"  {', '.join(f'0x{v:02X}' for v in sorted(unique_indices))}")
    print()

    if errors:
        print(f"ERRORS ({len(errors)}):")
        for i, err in enumerate(errors, 1):
            print(f"  {i}. {err}")
        print()

    if warnings:
        print(f"WARNINGS ({len(warnings)}):")
        for i, warn in enumerate(warnings, 1):
            print(f"  {i}. {warn}")
        print()

    if not errors and not warnings:
        print("PASSED: No issues found.")
    elif not errors:
        print("PASSED with warnings.")
    else:
        print("FAILED: Errors detected.")

    return 1 if errors else 0


def cmd_visualize(args):
    """Generate a colored PNG visualization of a collision map."""
    Image = try_import_pil()
    if Image is None:
        print(
            "Error: Pillow (PIL) is required for --visualize mode.\n"
            "Install with: pip install Pillow",
            file=sys.stderr,
        )
        return 1

    filepath = args.file
    if not os.path.isfile(filepath):
        print(f"Error: File not found: {filepath}", file=sys.stderr)
        return 1

    data = load_collision_map(filepath)
    scale = args.scale

    # Create image: full 32x32 grid
    img_width = ROW_STRIDE * scale
    img_height = TOTAL_ROWS * scale
    img = Image.new("RGB", (img_width, img_height), COLOR_UNLOADED)

    for row in range(TOTAL_ROWS):
        for col in range(ROW_STRIDE):
            offset = row * ROW_STRIDE + col
            if offset < len(data):
                val = data[offset]
            else:
                val = 0

            # Determine color
            if row >= LOADED_ROWS:
                color = COLOR_UNLOADED
            elif col >= VISIBLE_COLUMNS:
                color = COLOR_PADDING if val == 0 else color_for_index(val)
            else:
                color = color_for_index(val)

            # Fill the scaled pixel block
            for dy in range(scale):
                for dx in range(scale):
                    px = col * scale + dx
                    py = row * scale + dy
                    # Draw a 1-pixel border between tiles for readability
                    if scale >= 4 and (dx == 0 or dy == 0):
                        # Slightly darker border
                        border = tuple(max(0, c - 30) for c in color)
                        img.putpixel((px, py), border)
                    else:
                        img.putpixel((px, py), color)

    # Draw region boundaries
    if scale >= 4:
        # Draw a visible-column boundary line
        boundary_x = VISIBLE_COLUMNS * scale
        for py in range(img_height):
            if py % 2 == 0:  # Dashed line
                img.putpixel((min(boundary_x, img_width - 1), py), (255, 255, 0))

        # Draw the loaded-row boundary line
        boundary_y = LOADED_ROWS * scale
        for px in range(img_width):
            if px % 2 == 0:  # Dashed line
                img.putpixel((px, min(boundary_y, img_height - 1)), (255, 255, 0))

    output = args.output
    if not output:
        base = os.path.splitext(os.path.basename(filepath))[0]
        output = base + "_viz.png"

    img.save(output)
    print(f"Visualization saved to: {output}")
    print(f"Image size: {img_width}x{img_height} pixels (scale={scale}x)")
    print()
    print("Legend:")
    print(f"  Dark gray   = Empty (0x00)")
    print(f"  Blue shades = Static collision masks (0x01-0xCF)")
    print(f"  Yellow      = Animated mon masks (0xD0-0xDF)")
    print(f"  Red         = Left flipper masks (0xE0-0xEF)")
    print(f"  Orange      = Right flipper masks (0xF0-0xFF)")
    print(f"  Near-black  = Padding columns / unloaded rows")
    if scale >= 4:
        print(f"  Yellow dashed lines = visible area boundaries")

    return 0


def cmd_export_masks(args):
    """Export individual 8x8 mask tiles from a collision mask PNG."""
    Image = try_import_pil()
    if Image is None:
        print(
            "Error: Pillow (PIL) is required for --export-masks mode.\n"
            "Install with: pip install Pillow",
            file=sys.stderr,
        )
        return 1

    filepath = args.file
    if not os.path.isfile(filepath):
        print(f"Error: File not found: {filepath}", file=sys.stderr)
        return 1

    output_dir = args.output
    if not output_dir:
        base = os.path.splitext(os.path.basename(filepath))[0]
        output_dir = base + "_masks"

    os.makedirs(output_dir, exist_ok=True)

    src_img = Image.open(filepath).convert("L")
    width, height = src_img.size

    if width % 8 != 0 or height % 8 != 0:
        print(
            f"Error: Image dimensions {width}x{height} are not multiples of 8.",
            file=sys.stderr,
        )
        return 1

    tiles_per_row = width // 8
    tiles_per_col = height // 8
    total_tiles = tiles_per_row * tiles_per_col
    scale = args.scale

    print(f"Source: {filepath}")
    print(f"Dimensions: {width}x{height} pixels")
    print(f"Tile grid: {tiles_per_row}x{tiles_per_col} ({total_tiles} tiles)")
    print(f"Output scale: {scale}x")
    print(f"Output directory: {output_dir}")
    print()

    for tile_idx in range(total_tiles):
        ty = tile_idx // tiles_per_row
        tx = tile_idx % tiles_per_row

        # Extract 8x8 tile
        tile_img = Image.new("RGB", (8 * scale, 8 * scale), (255, 255, 255))

        solid_pixels = 0
        for row in range(8):
            for col in range(8):
                px = tx * 8 + col
                py = ty * 8 + row
                gray = src_img.getpixel((px, py))
                is_solid = gray < 128

                if is_solid:
                    solid_pixels += 1
                    color = (40, 40, 40)  # Dark = solid
                else:
                    color = (240, 240, 240)  # Light = passable

                for dy in range(scale):
                    for dx in range(scale):
                        tile_img.putpixel((col * scale + dx, row * scale + dy), color)

        out_path = os.path.join(output_dir, f"mask_{tile_idx:03d}_0x{tile_idx:02X}.png")
        tile_img.save(out_path)

        # Print a summary line
        coverage = 100.0 * solid_pixels / 64
        tag = "empty" if solid_pixels == 0 else ("full" if solid_pixels == 64 else f"{coverage:.0f}%")
        print(f"  0x{tile_idx:02X} ({tile_idx:3d}): {tag:>5s}  -> {os.path.basename(out_path)}")

    print()
    print(f"Exported {total_tiles} mask tiles to {output_dir}/")
    return 0


# ---------------------------------------------------------------------------
# Argument Parsing
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Collision Map Tool for Pokemon Pinball+",
        epilog=(
            "Examples:\n"
            "  %(prog)s --info data/collision/maps/red_stage_bottom.collision\n"
            "  %(prog)s --validate data/collision/maps/red_stage_bottom.collision\n"
            "  %(prog)s --visualize data/collision/maps/red_stage_bottom.collision -o viz.png\n"
            "  %(prog)s --export-masks data/collision/masks/red_stage_bottom.png -o masks/\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    mode_group = parser.add_mutually_exclusive_group(required=True)
    mode_group.add_argument(
        "--info",
        action="store_true",
        help="Print file size, grid dimensions, and attribute histogram",
    )
    mode_group.add_argument(
        "--validate",
        action="store_true",
        help="Check file format and warn about unusual mask indices",
    )
    mode_group.add_argument(
        "--visualize",
        action="store_true",
        help="Generate a colored PNG grid showing mask index categories (requires Pillow)",
    )
    mode_group.add_argument(
        "--export-masks",
        action="store_true",
        help="Split a collision mask PNG into individual 8x8 tile previews (requires Pillow)",
    )

    parser.add_argument(
        "file",
        help="Path to a .collision map file (for --info/--validate/--visualize) "
             "or a mask .png file (for --export-masks)",
    )
    parser.add_argument(
        "-o", "--output",
        default=None,
        help="Output file path (for --visualize) or directory (for --export-masks). "
             "Defaults to <input_name>_viz.png or <input_name>_masks/",
    )
    parser.add_argument(
        "--scale",
        type=int,
        default=8,
        help="Scale factor for visualization or mask export (default: 8)",
    )

    args = parser.parse_args()

    if args.info:
        return cmd_info(args)
    elif args.validate:
        return cmd_validate(args)
    elif args.visualize:
        return cmd_visualize(args)
    elif args.export_masks:
        return cmd_export_masks(args)

    return 0


if __name__ == "__main__":
    sys.exit(main())
