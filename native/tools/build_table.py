#!/usr/bin/env python3
"""
build_table.py - Pokemon Pinball+ Table Builder Tool

Extract existing tables as full reconstructed images (visual + collision),
or build new tables from modder-drawn images back into engine-compatible format.

Usage:
    python build_table.py extract <stage_name> [--output-dir DIR] [--project-root DIR]
    python build_table.py build <input_dir> [--output-dir DIR] [--project-root DIR]

Stage names: red_field_top, red_field_bottom, blue_field_top, blue_field_bottom

Requirements: Python 3.8+, Pillow (pip install Pillow)
"""

import argparse
import colorsys
import json
import os
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: Pillow is required. Install with: pip install Pillow")
    sys.exit(1)


# ============================================================================
# Constants
# ============================================================================

VRAM_SIZE = 0x1800        # $8000-$97FF per bank (6144 bytes, 384 tiles)
TILE_BYTES = 16           # 16 bytes per 2bpp tile
MAP_WIDTH = 32
MAP_HEIGHT = 32
MAP_SIZE = MAP_WIDTH * MAP_HEIGHT  # 1024 bytes

# Visible GBC screen area in tiles
SCREEN_TILES_X = 20       # 160 pixels
SCREEN_TILES_Y = 18       # 144 pixels
SCREEN_WIDTH = 160
SCREEN_HEIGHT = 144

# Full tilemap dimensions for rendering
FULL_MAP_TILES_X = 32     # Full tilemap width (256 pixels)
FULL_MAP_PX = 256          # 32 * 8


# ============================================================================
# GBC Color Helpers
# ============================================================================

def rgb555(r, g, b):
    """Create a GBC RGB555 color value from 5-bit components (0-31)."""
    return r | (g << 5) | (b << 10)


def rgb555_to_rgb888(c):
    """Convert GBC RGB555 to standard RGB888 tuple."""
    r = (c & 0x1F) * 255 // 31
    g = ((c >> 5) & 0x1F) * 255 // 31
    b = ((c >> 10) & 0x1F) * 255 // 31
    return (r, g, b)


def rgb888_to_rgb555(r, g, b):
    """Convert standard RGB888 to closest GBC RGB555."""
    r5 = min(31, round(r * 31 / 255))
    g5 = min(31, round(g * 31 / 255))
    b5 = min(31, round(b * 31 / 255))
    return r5 | (g5 << 5) | (b5 << 10)


# ============================================================================
# 2bpp Tile Helpers
# ============================================================================

def png_to_2bpp(image_path):
    """Convert a grayscale PNG to 2bpp tile data (matching tiles_from_png in C).

    Returns bytes of tile data. Each tile is 16 bytes (8 rows * 2 bytes/row).
    Tiles are read left-to-right, top-to-bottom from the image.
    """
    img = Image.open(image_path).convert('L')
    w, h = img.size
    if w % 8 != 0 or h % 8 != 0:
        raise ValueError(f"Image {image_path} dimensions {w}x{h} not multiple of 8")

    tiles_per_row = w // 8
    tiles_per_col = h // 8
    pixels = img.load()
    tile_data = bytearray()

    for ty in range(tiles_per_col):
        for tx in range(tiles_per_row):
            for row in range(8):
                lo = 0
                hi = 0
                for col in range(8):
                    gray = pixels[tx * 8 + col, ty * 8 + row]
                    # Match C: 0=white(lightest), 3=black(darkest)
                    idx = 3 - (gray >> 6)
                    bit = 7 - col
                    lo |= (idx & 1) << bit
                    hi |= ((idx >> 1) & 1) << bit
                tile_data.append(lo)
                tile_data.append(hi)

    return bytes(tile_data)


def decode_2bpp_tile(tile_bytes):
    """Decode 16 bytes of 2bpp tile data to 8x8 list of color indices (0-3)."""
    pixels = []
    for row in range(8):
        lo = tile_bytes[row * 2]
        hi = tile_bytes[row * 2 + 1]
        row_pixels = []
        for col in range(8):
            bit = 7 - col
            idx = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)
            row_pixels.append(idx)
        pixels.append(row_pixels)
    return pixels


def encode_pixels_to_2bpp(pixels_8x8):
    """Encode 8x8 array of color indices (0-3) to 16 bytes of 2bpp tile data."""
    tile_data = bytearray()
    for row in range(8):
        lo = 0
        hi = 0
        for col in range(8):
            idx = pixels_8x8[row][col] & 3
            bit = 7 - col
            lo |= (idx & 1) << bit
            hi |= ((idx >> 1) & 1) << bit
        tile_data.append(lo)
        tile_data.append(hi)
    return bytes(tile_data)


def flip_tile_h(pixels_8x8):
    """Horizontally flip an 8x8 pixel array."""
    return [row[::-1] for row in pixels_8x8]


def flip_tile_v(pixels_8x8):
    """Vertically flip an 8x8 pixel array."""
    return pixels_8x8[::-1]


def tile_data_hash(tile_bytes_16):
    """Hash 16 bytes of tile data for deduplication."""
    return bytes(tile_bytes_16)


# ============================================================================
# VRAM Simulation
# ============================================================================

class VirtualVRAM:
    """Simulates GBC VRAM with two banks of tile data and tilemap."""

    def __init__(self):
        # Two banks of tile data, each covering $8000-$97FF
        self.tile_data = [bytearray(VRAM_SIZE), bytearray(VRAM_SIZE)]
        # Tilemap: bank 0 = tile indices, bank 1 = attributes
        self.bg_map = [bytearray(MAP_SIZE), bytearray(MAP_SIZE)]

    def write_tiles(self, bank, vram_addr, data, max_size=None):
        """Write tile data to VRAM at the given address."""
        offset = vram_addr - 0x8000
        if offset < 0 or offset >= VRAM_SIZE:
            return
        size = len(data) if max_size is None else min(len(data), max_size)
        end = min(offset + size, VRAM_SIZE)
        actual = end - offset
        if actual > 0:
            self.tile_data[bank][offset:end] = data[:actual]

    def write_map(self, bank, vram_addr, data, max_size=None):
        """Write tilemap/bgattr data."""
        offset = vram_addr - 0x9800
        if offset < 0 or offset >= MAP_SIZE:
            return
        size = len(data) if max_size is None else min(len(data), max_size)
        end = min(offset + size, MAP_SIZE)
        actual = end - offset
        if actual > 0:
            self.bg_map[bank][offset:end] = data[:actual]

    def get_tile_offset_signed(self, tile_num):
        """Compute tile data offset using signed addressing (LCDC $67).

        tile_num 0-127   -> offset 0x1000 + tile_num * 16  ($9000-$97FF)
        tile_num 128-255 -> offset 0x800 + (tile_num-128)*16 ($8800-$8FFF)
        """
        signed_val = tile_num if tile_num < 128 else tile_num - 256
        return signed_val * 16 + 0x1000

    def get_tile_data(self, bank, tile_num):
        """Get 16 bytes of tile data using signed addressing."""
        offset = self.get_tile_offset_signed(tile_num)
        if offset < 0 or offset + TILE_BYTES > VRAM_SIZE:
            return bytes(TILE_BYTES)  # Return blank tile
        return bytes(self.tile_data[bank][offset:offset + TILE_BYTES])


# ============================================================================
# Stage Definitions
# ============================================================================

def _pal(entries):
    """Convert list of (r,g,b) tuples to list of RGB555 values."""
    return [rgb555(r, g, b) for r, g, b in entries]


# Palette data extracted from stage_palettes.c
PALETTES = {
    'red_field_top': [
        _pal([(31,31,31), (13,20,31), (31, 4, 4), ( 0, 0, 0)]),
        _pal([(31,31,31), (24,31, 0), (31, 0, 0), ( 3, 0, 0)]),
        _pal([(31,31,31), (11,25,31), ( 0,11,31), ( 0, 0, 0)]),
        _pal([(31,31,31), (31,13,13), (31, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (31, 0,31), (31, 0, 0), ( 0, 0, 0)]),
        _pal([(24,31, 0), (31, 0,31), (31, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (13,13,31), (31, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (31,13,13), (31, 0, 0), ( 0, 0, 0)]),
    ],
    'red_field_bottom': [
        _pal([(31,31,31), (13,20,31), (31, 4, 4), ( 0, 0, 0)]),
        _pal([(31,31,31), (24,31, 0), (31, 0, 0), ( 3, 0, 0)]),
        _pal([(31,31,31), (11,25,31), ( 0,11,31), ( 0, 0, 0)]),
        _pal([(31,31,31), (31,13,13), (31, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (31, 0,31), (31, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (20,20,20), ( 8, 8, 8), ( 0, 0, 0)]),
        _pal([(29,30,31), (27,20,10), ( 2,16, 1), ( 0, 0, 0)]),
        _pal([(29,30,31), ( 5,17,31), (26, 3, 1), ( 0, 0, 0)]),
    ],
    'blue_field_top': [
        _pal([(31,31,31), (13,20,31), (31, 4, 4), ( 0, 0, 0)]),
        _pal([(31,31,31), (11,25,31), ( 0,11,31), ( 0, 0, 0)]),
        _pal([(31,31,31), ( 4,23,13), ( 0,13, 4), ( 0, 0, 0)]),
        _pal([(31,31,31), (31,29, 0), (15, 8, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (31, 0, 0), (16, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (20,20,20), ( 8, 8, 8), ( 0, 0, 0)]),
        _pal([(31,31,31), (13,13,31), (31, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (20,20,20), ( 8, 8, 8), ( 0, 0, 0)]),
    ],
    'blue_field_bottom': [
        _pal([(31,31,31), (13,20,31), (31, 4, 4), ( 0, 0, 0)]),
        _pal([(31,31,31), (11,25,31), ( 0,11,31), ( 0, 0, 0)]),
        _pal([(31,31,31), ( 4,23,13), ( 0,13, 4), ( 0, 0, 0)]),
        _pal([(31,31,31), (31,29, 0), (15, 8, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (31, 0, 0), (16, 0, 0), ( 0, 0, 0)]),
        _pal([(31,31,31), (20,20,20), ( 8, 8, 8), ( 0, 0, 0)]),
        _pal([(31,31,31), (15,20,31), ( 7,11,21), ( 0, 0, 0)]),
        _pal([(31,31,31), (27,20,10), (24, 7, 5), ( 0, 0, 0)]),
    ],
}

# Stage asset definitions: tile PNGs, tilemaps, collision data
# Each tile_assets entry: (path, vram_addr, bank, max_size)
STAGES = {
    'red_field_top': {
        'default_scx': 0x00,  # Top stages: no horizontal scroll
        'tile_assets': [
            ('gfx/stage/alphabet_2.png', 0x8000, 0, 0x01A0),
            ('gfx/stage/red_top/red_top_1.png', 0x81A0, 0, 0x0260),
            ('gfx/stage/ball_pokeball.w32.interleave.png', 0x8400, 0, 0x0200),
            ('gfx/stage/red_top/red_top_2.png', 0x8600, 0, 0x0200),
            ('gfx/stage/red_top/status_bar_symbols_gameboycolor.png', 0x8800, 0, 0x0100),
            ('gfx/stage/red_top/red_top_3.png', 0x8900, 0, 0x01A0),
            ('gfx/stage/red_top/red_top_base_gameboycolor.png', 0x8AA0, 0, 0x0D60),
            # Bank 1
            ('gfx/stage/red_top/red_top_4.png', 0x8800, 1, 0x1000),
            ('gfx/stage/red_top/red_top_5.png', 0x8000, 1, 0x0200),
            ('gfx/stage/timer_digits.png', 0x8600, 1, 0x0160),
            ('gfx/stage/red_bottom/japanese_characters.png', 0x8200, 1, 0x0400),
            ('gfx/stage/red_bottom/japanese_characters_2.png', 0x8900, 1, 0x0200),
            ('gfx/stage/red_top/status_bar_symbols_gameboycolor.png', 0x8800, 1, 0x0100),
            ('gfx/stage/red_top/red_top_6.png', 0x87C0, 1, 0x0040),
        ],
        'tilemap_bank0': 'gfx/tilemaps/stage_red_field_top_gameboycolor.map',
        'tilemap_bank1': 'gfx/tilemaps/stage_red_field_top_gameboycolor_2.map',
        'collision_map': 'data/collision/maps/red_stage_top_0.collision',
        'collision_masks': 'data/collision/masks/red_stage_top_0.png',
    },
    'red_field_bottom': {
        'default_scx': 0x20,  # Bottom stages: scroll right by 32px to show alley
        'tile_assets': [
            ('gfx/stage/alphabet_2.png', 0x8000, 0, 0x01A0),
            ('gfx/stage/shared/bonus_slot_glow.png', 0x81A0, 0, 0x0160),
            ('gfx/stage/shared/arrows.png', 0x8300, 0, 0x0080),
            ('gfx/stage/shared/bonus_slot_glow_2.png', 0x8380, 0, 0x0020),
            ('gfx/stage/shared/pika_bolt.png', 0x83C0, 0, 0x0440),
            ('gfx/stage/ball_pokeball.w32.interleave.png', 0x8400, 0, 0x0200),
            ('gfx/stage/flipper.png', 0x8600, 0, 0x0120),
            ('gfx/stage/pikachu_saver.png', 0x8720, 0, 0x00E0),
            ('gfx/stage/red_bottom/red_bottom_base_gameboycolor.png', 0x8800, 0, 0x1000),
            ('gfx/stage/red_top/status_bar_symbols_gameboycolor.png', 0x8800, 0, 0x0100),
            ('gfx/stage/saver_off.png', 0x8AA0, 0, 0x0040),
            # Bank 1
            ('gfx/stage/red_bottom/red_bottom_5.png', 0x8800, 1, 0x1000),
            ('gfx/stage/timer_digits.png', 0x8600, 1, 0x0160),
            ('gfx/stage/red_bottom/japanese_characters.png', 0x8200, 1, 0x0400),
            ('gfx/stage/red_bottom/japanese_characters_2.png', 0x8900, 1, 0x0200),
            ('gfx/stage/red_bottom/red_bottom_base_gameboycolor.png', 0x8800, 1, 0x0100),
            ('gfx/stage/red_top/red_top_6.png', 0x87C0, 1, 0x0040),
        ],
        'tilemap_bank0': 'gfx/tilemaps/stage_red_field_bottom_gameboycolor.map',
        'tilemap_bank1': 'gfx/tilemaps/stage_red_field_bottom_gameboycolor_2.map',
        'collision_map': 'data/collision/maps/red_stage_bottom.collision',
        'collision_masks': 'data/collision/masks/red_stage_bottom.png',
    },
    'blue_field_top': {
        'default_scx': 0x00,  # Top stages: no horizontal scroll
        'tile_assets': [
            ('gfx/stage/alphabet_2.png', 0x8000, 0, 0x01A0),
            ('gfx/stage/blue_top/blue_top_1.png', 0x81A0, 0, 0x0260),
            ('gfx/stage/ball_pokeball.w32.interleave.png', 0x8400, 0, 0x0200),
            ('gfx/stage/blue_top/blue_top_2.png', 0x8600, 0, 0x0200),
            ('gfx/stage/blue_top/status_bar_symbols_gameboycolor.png', 0x8800, 0, 0x0100),
            ('gfx/stage/blue_top/blue_top_3.png', 0x8900, 0, 0x01A0),
            ('gfx/stage/blue_top/blue_top_base_gameboycolor.png', 0x8AA0, 0, 0x0D60),
            # Bank 1
            ('gfx/stage/blue_top/blue_top_4.png', 0x8800, 1, 0x1000),
            ('gfx/stage/timer_digits.png', 0x8600, 1, 0x0160),
        ],
        'tilemap_bank0': 'gfx/tilemaps/stage_blue_field_top_gameboycolor.map',
        'tilemap_bank1': 'gfx/tilemaps/stage_blue_field_top_gameboycolor_2.map',
        'collision_map': 'data/collision/maps/blue_stage_top.collision',
        'collision_masks': 'data/collision/masks/blue_stage_top.png',
    },
    'blue_field_bottom': {
        'default_scx': 0x20,  # Bottom stages: scroll right by 32px to show alley
        'tile_assets': [
            ('gfx/stage/alphabet_2.png', 0x8000, 0, 0x01A0),
            ('gfx/stage/shared/bonus_slot_glow.png', 0x81A0, 0, 0x0160),
            ('gfx/stage/shared/arrows.png', 0x8300, 0, 0x0080),
            ('gfx/stage/shared/bonus_slot_glow_2.png', 0x8380, 0, 0x0020),
            ('gfx/stage/shared/pika_bolt.png', 0x83C0, 0, 0x0440),
            ('gfx/stage/ball_pokeball.w32.interleave.png', 0x8400, 0, 0x0200),
            ('gfx/stage/flipper.png', 0x8600, 0, 0x0120),
            ('gfx/stage/pikachu_saver.png', 0x8720, 0, 0x00E0),
            ('gfx/stage/blue_bottom/blue_bottom_base_gameboycolor.png', 0x8800, 0, 0x1000),
            ('gfx/stage/blue_top/status_bar_symbols_gameboycolor.png', 0x8800, 0, 0x0100),
            ('gfx/stage/saver_off.png', 0x8AA0, 0, 0x0040),
            # Bank 1
            ('gfx/stage/blue_bottom/blue_bottom_1.png', 0x8800, 1, 0x1000),
            ('gfx/stage/timer_digits.png', 0x8600, 1, 0x0160),
        ],
        'tilemap_bank0': 'gfx/tilemaps/stage_blue_field_bottom_gameboycolor.map',
        'tilemap_bank1': 'gfx/tilemaps/stage_blue_field_bottom_gameboycolor_2.map',
        'collision_map': 'data/collision/maps/blue_stage_bottom.collision',
        'collision_masks': 'data/collision/masks/blue_stage_bottom.png',
    },
}


# ============================================================================
# Collision Visualization
# ============================================================================

def collision_attr_color(attr_id):
    """Generate a distinct color for a collision attribute ID."""
    if attr_id == 0:
        return (255, 255, 255)   # White: passable
    if attr_id == 255:
        return (48, 48, 48)      # Dark gray: border/solid padding
    if attr_id == 1:
        return (0, 0, 0)         # Black: solid wall

    # Animated objects (208-255)
    if attr_id >= 208:
        t = (attr_id - 208) / 47.0
        return (255, int(80 + 175 * t), 0)  # Orange gradient

    # Use golden-angle hue distribution for good visual separation
    hue = ((attr_id * 137.508) % 360) / 360.0
    r, g, b = colorsys.hsv_to_rgb(hue, 0.7, 0.9)
    return (int(r * 255), int(g * 255), int(b * 255))


# ============================================================================
# Project Root Detection
# ============================================================================

def find_project_root(start_path=None):
    """Find the project root by looking for gfx/ directory."""
    if start_path:
        p = Path(start_path).resolve()
        if (p / 'gfx').is_dir():
            return p
        return p

    # Try current directory and parents
    p = Path.cwd()
    for _ in range(5):
        if (p / 'gfx').is_dir() and (p / 'native').is_dir():
            return p
        p = p.parent

    # Try relative to this script
    script_dir = Path(__file__).resolve().parent
    for _ in range(3):
        if (script_dir / 'gfx').is_dir():
            return script_dir
        script_dir = script_dir.parent

    return Path.cwd()


# ============================================================================
# Extract Mode
# ============================================================================

def load_binary_file(path):
    """Load a binary file, return bytes."""
    with open(path, 'rb') as f:
        return f.read()


def extract_stage(stage_name, project_root, output_dir):
    """Extract a stage as visual + collision images and palette JSON."""
    if stage_name not in STAGES:
        print(f"Error: Unknown stage '{stage_name}'")
        print(f"Available stages: {', '.join(STAGES.keys())}")
        return False

    stage = STAGES[stage_name]
    palettes = PALETTES[stage_name]
    root = Path(project_root)
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    print(f"Extracting {stage_name}...")

    # Step 1: Build virtual VRAM from tile assets
    vram = VirtualVRAM()

    for png_path, vram_addr, bank, max_size in stage['tile_assets']:
        full_path = root / png_path
        if not full_path.exists():
            print(f"  WARN: Missing tile asset: {png_path}")
            continue

        tile_data = png_to_2bpp(str(full_path))
        copy_size = min(len(tile_data), max_size) if max_size else len(tile_data)
        vram.write_tiles(bank, vram_addr, tile_data[:copy_size])
        print(f"  Tiles: {png_path} -> ${vram_addr:04X} bank {bank} ({copy_size} bytes)")

    # Step 2: Load tilemaps
    tilemap_path = root / stage['tilemap_bank0']
    bgattr_path = root / stage['tilemap_bank1']

    # Try external file first, fall back to embedded data
    tilemap_data = None
    bgattr_data = None

    for path_to_try in [tilemap_path]:
        if path_to_try.exists():
            tilemap_data = load_binary_file(str(path_to_try))
            print(f"  Tilemap: {stage['tilemap_bank0']} ({len(tilemap_data)} bytes)")
            break

    for path_to_try in [bgattr_path]:
        if path_to_try.exists():
            bgattr_data = load_binary_file(str(path_to_try))
            print(f"  Bgattr: {stage['tilemap_bank1']} ({len(bgattr_data)} bytes)")
            break

    if tilemap_data is None or bgattr_data is None:
        # Try loading from the embedded C data
        print("  Trying embedded data...")
        embedded_dir = root / 'native' / 'src' / 'data'
        tilemap_data, bgattr_data = try_load_embedded_maps(
            stage_name, embedded_dir, root)

    if tilemap_data is None:
        print(f"  ERROR: Could not load tilemap for {stage_name}")
        return False
    if bgattr_data is None:
        print(f"  ERROR: Could not load bgattr for {stage_name}")
        return False

    # Pad to full MAP_SIZE if needed
    if len(tilemap_data) < MAP_SIZE:
        tilemap_data = tilemap_data + bytes(MAP_SIZE - len(tilemap_data))
    if len(bgattr_data) < MAP_SIZE:
        bgattr_data = bgattr_data + bytes(MAP_SIZE - len(bgattr_data))

    vram.bg_map[0][:MAP_SIZE] = tilemap_data[:MAP_SIZE]
    vram.bg_map[1][:MAP_SIZE] = bgattr_data[:MAP_SIZE]

    # Step 3: Render the visual image (full 256x144 tilemap)
    default_scx = stage.get('default_scx', 0)
    print(f"  Rendering visual image (SCX={default_scx:#04x})...")
    visual = render_visual(vram, palettes, default_scx)
    visual_path = out / 'table_visual.png'
    visual.save(str(visual_path))
    print(f"  Saved: {visual_path} ({visual.size[0]}x{visual.size[1]})")

    # Step 4: Load and render collision map
    collision_path = root / stage['collision_map']
    if collision_path.exists():
        collision_data = load_binary_file(str(collision_path))
        print(f"  Collision: {stage['collision_map']} ({len(collision_data)} bytes)")
        collision_img = render_collision(collision_data, default_scx=default_scx)
        collision_img_path = out / 'table_collision.png'
        collision_img.save(str(collision_img_path))
        print(f"  Saved: {collision_img_path}")
    else:
        print(f"  WARN: No collision map at {collision_path}")

    # Step 5: Export palette JSON (includes scroll metadata)
    palette_json = export_palette_json(palettes, default_scx)
    palette_path = out / 'palette.json'
    with open(str(palette_path), 'w') as f:
        json.dump(palette_json, f, indent=2)
    print(f"  Saved: {palette_path}")

    print(f"  Done! Output in {out}")
    return True


def try_load_embedded_maps(stage_name, embedded_dir, project_root):
    """Try alternative locations for tilemap data."""
    # The .map files might be in different locations
    stage = STAGES[stage_name]

    # Try the project root paths directly
    tilemap_data = None
    bgattr_data = None

    for base in [project_root]:
        tp = base / stage['tilemap_bank0']
        bp = base / stage['tilemap_bank1']
        if tp.exists() and tilemap_data is None:
            tilemap_data = load_binary_file(str(tp))
        if bp.exists() and bgattr_data is None:
            bgattr_data = load_binary_file(str(bp))

    return tilemap_data, bgattr_data


def render_visual(vram, palettes, default_scx=0):
    """Render the full tilemap to a color image using VRAM data and palettes.

    Uses signed tile addressing (LCDC $67 mode).
    Renders all 32 columns x 18 rows (256x144) to show the complete table
    including the ball launcher alley. Draws a 1px viewport rectangle showing
    the in-game visible area at the default SCX position.
    """
    render_cols = FULL_MAP_TILES_X  # 32 columns = 256px
    render_rows = SCREEN_TILES_Y    # 18 rows = 144px
    img_w = render_cols * 8          # 256
    img_h = render_rows * 8          # 144

    img = Image.new('RGB', (img_w, img_h), (255, 255, 255))
    pixels = img.load()

    for ty in range(render_rows):
        for tx in range(render_cols):
            map_idx = ty * MAP_WIDTH + tx

            tile_num = vram.bg_map[0][map_idx]
            attrs = vram.bg_map[1][map_idx]

            pal_id = attrs & 0x07
            tile_bank = (attrs >> 3) & 1
            hflip = (attrs >> 5) & 1
            vflip = (attrs >> 6) & 1

            # Get tile data from correct bank
            tile_bytes = vram.get_tile_data(tile_bank, tile_num)
            tile_pixels = decode_2bpp_tile(tile_bytes)

            # Apply flips
            if vflip:
                tile_pixels = tile_pixels[::-1]
            if hflip:
                tile_pixels = [row[::-1] for row in tile_pixels]

            # Get palette colors
            pal = palettes[pal_id] if pal_id < len(palettes) else [0x7FFF, 0x5294, 0x294A, 0x0000]

            # Write pixels
            for py in range(8):
                for px in range(8):
                    color_idx = tile_pixels[py][px]
                    rgb = rgb555_to_rgb888(pal[color_idx])
                    sx = tx * 8 + px
                    sy = ty * 8 + py
                    pixels[sx, sy] = rgb

    # Draw viewport rectangle showing the in-game visible area
    # The GBC screen is 160px wide; SCX controls where in the 256px map it starts
    vp_x = default_scx
    vp_y = 0
    vp_w = SCREEN_WIDTH   # 160
    vp_h = SCREEN_HEIGHT  # 144
    vp_color = (255, 0, 255)  # Magenta

    for x in range(vp_x, min(vp_x + vp_w, img_w)):
        if 0 <= vp_y < img_h:
            pixels[x, vp_y] = vp_color
        if 0 <= vp_y + vp_h - 1 < img_h:
            pixels[x, vp_y + vp_h - 1] = vp_color
    for y in range(vp_y, min(vp_y + vp_h, img_h)):
        if 0 <= vp_x < img_w:
            pixels[vp_x, y] = vp_color
        if 0 <= vp_x + vp_w - 1 < img_w:
            pixels[vp_x + vp_w - 1, y] = vp_color

    return img


def render_collision(collision_data, cell_size=8, default_scx=0):
    """Render collision map as a color-coded image.

    Each collision cell is rendered as a cell_size x cell_size pixel block.
    Renders the full 32-column map to match the visual image width.
    Draws a viewport rectangle for the in-game visible area.
    """
    data_len = len(collision_data)
    grid_w = MAP_WIDTH   # 32
    grid_h = data_len // grid_w if data_len >= grid_w else 1

    # Render full width, up to 22 rows (visible 18 + a few extra for drain)
    render_w = grid_w
    render_h = min(SCREEN_TILES_Y + 4, grid_h)  # 22 rows

    img_w = render_w * cell_size
    img_h = render_h * cell_size
    img = Image.new('RGB', (img_w, img_h), (255, 255, 255))
    pixels = img.load()

    for gy in range(render_h):
        for gx in range(render_w):
            idx = gy * grid_w + gx
            if idx >= len(collision_data):
                continue
            attr_id = collision_data[idx]
            color = collision_attr_color(attr_id)

            for py in range(cell_size):
                for px in range(cell_size):
                    x = gx * cell_size + px
                    y = gy * cell_size + py
                    # Draw grid lines
                    if px == 0 or py == 0:
                        grid_color = tuple(max(0, c - 40) for c in color)
                        pixels[x, y] = grid_color
                    else:
                        pixels[x, y] = color

    # Draw viewport rectangle (visible screen area at default SCX)
    vp_x = default_scx  # In pixels (1 collision cell = 1 tile = cell_size px)
    vp_y = 0
    vp_w = SCREEN_WIDTH   # 160px
    vp_h = SCREEN_HEIGHT   # 144px
    vp_color = (255, 0, 255)  # Magenta

    for x in range(vp_x, min(vp_x + vp_w, img_w)):
        if 0 <= vp_y < img_h:
            pixels[x, vp_y] = vp_color
        if 0 <= vp_y + vp_h - 1 < img_h:
            pixels[x, vp_y + vp_h - 1] = vp_color
    for y in range(vp_y, min(vp_y + vp_h, img_h)):
        if 0 <= vp_x < img_w:
            pixels[vp_x, y] = vp_color
        if 0 <= vp_x + vp_w - 1 < img_w:
            pixels[vp_x + vp_w - 1, y] = vp_color

    return img


def export_palette_json(palettes, default_scx=0):
    """Export palette data as JSON with both RGB555 and hex representations."""
    result = {
        "format": "gbc_rgb555",
        "note": "GBC RGB555: r | (g << 5) | (b << 10), components 0-31. "
                "bg_palettes has 8 palettes of 4 colors each.",
        "default_scx": default_scx,
        "viewport_note": f"In-game viewport starts at pixel X={default_scx} "
                         f"(magenta rectangle in table_visual.png). "
                         f"The full tilemap is 256px wide, the GBC screen shows 160px.",
        "bg_palettes": [],
        "bg_palettes_hex": [],
    }
    for pal in palettes:
        result["bg_palettes"].append(pal)
        hex_colors = []
        for c in pal:
            r, g, b = rgb555_to_rgb888(c)
            hex_colors.append(f"#{r:02X}{g:02X}{b:02X}")
        result["bg_palettes_hex"].append(hex_colors)
    return result


# ============================================================================
# Build Mode
# ============================================================================

def build_table(input_dir, output_dir):
    """Build engine-compatible table data from modder's images.

    Expected input files:
      - table_visual.png  (160x144 or 256x256 full-color image)
      - palette.json      (8 BG palettes with 4 colors each)
      - table_collision.png (optional, color-coded collision zones)
      - collision_key.json  (optional, color->attribute ID mapping)
    """
    inp = Path(input_dir)
    out = Path(output_dir) if output_dir else inp / 'build_output'
    out.mkdir(parents=True, exist_ok=True)

    # Load palette
    palette_path = inp / 'palette.json'
    if not palette_path.exists():
        print(f"Error: No palette.json found in {inp}")
        return False

    with open(str(palette_path)) as f:
        pal_data = json.load(f)

    palettes = pal_data['bg_palettes']
    print(f"Loaded {len(palettes)} palettes from palette.json")

    # Load visual image
    visual_path = inp / 'table_visual.png'
    if not visual_path.exists():
        print(f"Error: No table_visual.png found in {inp}")
        return False

    img = Image.open(str(visual_path)).convert('RGB')
    w, h = img.size
    print(f"Loaded table_visual.png: {w}x{h}")

    # Determine tile grid dimensions
    if w == SCREEN_WIDTH and h == SCREEN_HEIGHT:
        tiles_x, tiles_y = SCREEN_TILES_X, SCREEN_TILES_Y
    elif w % 8 == 0 and h % 8 == 0:
        tiles_x, tiles_y = w // 8, h // 8
    else:
        print(f"Error: Image dimensions {w}x{h} must be multiples of 8")
        return False

    print(f"  Grid: {tiles_x}x{tiles_y} tiles")

    # Convert palettes from RGB555 to RGB888 for matching
    pal_rgb = []
    for pal in palettes:
        pal_rgb.append([rgb555_to_rgb888(c) for c in pal])

    # Step 1: Slice image into 8x8 tiles, match palettes, quantize
    tiles = {}           # tile_data_bytes -> tile_index
    tilemap = bytearray(MAP_SIZE)    # 32x32, filled with 0
    bgattr = bytearray(MAP_SIZE)     # 32x32, filled with 0
    tile_list = []       # ordered list of unique tile data

    # Reserve tile index space. In signed mode:
    # Indices $80-$FF (128-255) -> $8800-$8FFF (first 128 tiles loaded)
    # Indices $00-$7F (0-127)   -> $9000-$97FF (next 128 tiles)
    # So we can use up to 256 unique tiles total.
    next_tile_idx = 0x80  # Start at $80 (maps to $8800)

    pixel_data = img.load()

    for ty in range(min(tiles_y, MAP_HEIGHT)):
        for tx in range(min(tiles_x, MAP_WIDTH)):
            # Extract 8x8 block
            block = []
            for py in range(8):
                row = []
                for px in range(8):
                    x = tx * 8 + px
                    y = ty * 8 + py
                    if x < w and y < h:
                        row.append(pixel_data[x, y])
                    else:
                        row.append((255, 255, 255))
                block.append(row)

            # Find best palette for this block
            best_pal = find_best_palette(block, pal_rgb)

            # Quantize pixels to palette indices
            quant_block = quantize_block(block, pal_rgb[best_pal])

            # Encode as 2bpp
            tile_bytes = encode_pixels_to_2bpp(quant_block)

            # Check for duplicates (including flipped variants)
            tile_idx, hflip, vflip = find_or_add_tile(
                tile_bytes, quant_block, tiles, tile_list, next_tile_idx)

            if tile_idx is None:
                if len(tile_list) >= 256:
                    print(f"  WARN: Tile limit (256) reached at ({tx},{ty}), reusing last tile")
                    tile_idx = tile_list[-1][1]
                    hflip, vflip = 0, 0
                else:
                    tile_idx = next_tile_idx
                    tiles[tile_bytes] = tile_idx
                    tile_list.append((tile_bytes, tile_idx))
                    next_tile_idx += 1
                    # Wrap from $FF to $00 for the second 128 tiles
                    if next_tile_idx == 0x100:
                        next_tile_idx = 0x00
                    hflip, vflip = 0, 0

            # Write to tilemap
            map_idx = ty * MAP_WIDTH + tx
            tilemap[map_idx] = tile_idx & 0xFF

            # Build attribute byte
            attr = best_pal & 0x07
            # All tiles go to bank 0 for simplicity
            if hflip:
                attr |= 0x20
            if vflip:
                attr |= 0x40
            bgattr[map_idx] = attr

    print(f"  Unique tiles: {len(tile_list)} / 256")

    # Step 2: Build tileset PNG
    tileset_img = build_tileset_png(tile_list)
    tileset_path = out / 'tileset.png'
    tileset_img.save(str(tileset_path))
    print(f"  Saved: {tileset_path}")

    # Step 3: Save tilemap and bgattr
    tilemap_path = out / 'tilemap.map'
    with open(str(tilemap_path), 'wb') as f:
        f.write(tilemap)
    print(f"  Saved: {tilemap_path} ({len(tilemap)} bytes)")

    bgattr_path = out / 'tilemap_attrs.map'
    with open(str(bgattr_path), 'wb') as f:
        f.write(bgattr)
    print(f"  Saved: {bgattr_path} ({len(bgattr)} bytes)")

    # Step 4: Process collision image if present
    collision_path = inp / 'table_collision.png'
    if collision_path.exists():
        print("  Processing collision image...")
        build_collision(str(collision_path), inp, out)
    else:
        print("  No table_collision.png found, skipping collision generation")

    # Copy palette.json to output
    import shutil
    shutil.copy2(str(palette_path), str(out / 'palette.json'))

    print(f"  Done! Output in {out}")
    return True


def find_best_palette(block_8x8, pal_rgb):
    """Find the palette that best matches the colors in an 8x8 block."""
    best_pal = 0
    best_error = float('inf')

    # Collect unique colors in the block
    block_colors = set()
    for row in block_8x8:
        for pixel in row:
            block_colors.add(pixel)

    for pal_idx, pal in enumerate(pal_rgb):
        error = 0
        for color in block_colors:
            min_dist = min(color_distance(color, pc) for pc in pal)
            error += min_dist
        if error < best_error:
            best_error = error
            best_pal = pal_idx

    return best_pal


def color_distance(c1, c2):
    """Squared RGB distance between two color tuples."""
    return sum((a - b) ** 2 for a, b in zip(c1, c2))


def quantize_block(block_8x8, pal_rgb):
    """Quantize an 8x8 RGB block to palette indices (0-3)."""
    result = []
    for row in block_8x8:
        qrow = []
        for pixel in row:
            best_idx = 0
            best_dist = float('inf')
            for i, pal_color in enumerate(pal_rgb):
                d = color_distance(pixel, pal_color)
                if d < best_dist:
                    best_dist = d
                    best_idx = i
            qrow.append(best_idx)
        result.append(qrow)
    return result


def find_or_add_tile(tile_bytes, pixels, tiles_dict, tile_list, next_idx):
    """Check if tile already exists (including flipped variants).

    Returns (tile_idx, hflip, vflip) if found, or (None, 0, 0) if new.
    """
    # Check exact match
    if tile_bytes in tiles_dict:
        return tiles_dict[tile_bytes], 0, 0

    # Check H-flip
    hflipped = encode_pixels_to_2bpp(flip_tile_h(pixels))
    if hflipped in tiles_dict:
        return tiles_dict[hflipped], 1, 0

    # Check V-flip
    vflipped = encode_pixels_to_2bpp(flip_tile_v(pixels))
    if vflipped in tiles_dict:
        return tiles_dict[vflipped], 0, 1

    # Check HV-flip
    hvflipped = encode_pixels_to_2bpp(flip_tile_h(flip_tile_v(pixels)))
    if hvflipped in tiles_dict:
        return tiles_dict[hvflipped], 1, 1

    return None, 0, 0


def build_tileset_png(tile_list):
    """Build a tileset PNG from a list of (tile_bytes, tile_idx) tuples.

    Output is a grayscale image with tiles arranged 16 per row.
    """
    num_tiles = len(tile_list)
    tiles_per_row = 16
    rows = (num_tiles + tiles_per_row - 1) // tiles_per_row
    if rows == 0:
        rows = 1

    img_w = tiles_per_row * 8
    img_h = rows * 8
    img = Image.new('L', (img_w, img_h), 255)
    pixels = img.load()

    # Grayscale LUT: index 0=white, 1=light, 2=dark, 3=black
    gray_lut = [255, 170, 85, 0]

    for i, (tile_bytes, _) in enumerate(tile_list):
        tx = (i % tiles_per_row) * 8
        ty = (i // tiles_per_row) * 8
        tile_pixels = decode_2bpp_tile(tile_bytes)
        for py in range(8):
            for px in range(8):
                pixels[tx + px, ty + py] = gray_lut[tile_pixels[py][px]]

    return img


# ============================================================================
# Collision Build
# ============================================================================

# Default collision color key
DEFAULT_COLLISION_KEY = {
    "#FFFFFF": 0,     # White: passable
    "#000000": 1,     # Black: solid wall
    "#303030": 255,   # Dark gray: border padding
}


def build_collision(collision_img_path, input_dir, output_dir):
    """Build collision map and masks from a color-coded collision image."""
    img = Image.open(collision_img_path).convert('RGB')
    w, h = img.size
    pixels = img.load()

    # Load collision key if available
    key_path = Path(input_dir) / 'collision_key.json'
    if key_path.exists():
        with open(str(key_path)) as f:
            collision_key = json.load(f)
        # Convert hex keys to RGB tuples
        color_to_attr = {}
        for hex_color, attr_id in collision_key.items():
            hex_color = hex_color.lstrip('#')
            r = int(hex_color[0:2], 16)
            g = int(hex_color[2:4], 16)
            b = int(hex_color[4:6], 16)
            color_to_attr[(r, g, b)] = attr_id
    else:
        color_to_attr = {}
        for hex_color, attr_id in DEFAULT_COLLISION_KEY.items():
            hex_color = hex_color.lstrip('#')
            r = int(hex_color[0:2], 16)
            g = int(hex_color[2:4], 16)
            b = int(hex_color[4:6], 16)
            color_to_attr[(r, g, b)] = attr_id

    # Determine cell size from image dimensions
    cell_size = 8
    grid_w = w // cell_size
    grid_h = h // cell_size

    # Build collision map
    collision_map = bytearray(MAP_SIZE)

    # Collect unique colors for auto-assignment
    unique_colors = set()
    for y in range(h):
        for x in range(w):
            unique_colors.add(pixels[x, y])

    # Auto-assign attribute IDs to unknown colors
    next_attr = 2
    for color in sorted(unique_colors):
        if color not in color_to_attr:
            # Find closest known color
            closest = min(color_to_attr.keys(),
                         key=lambda c: color_distance(color, c),
                         default=None)
            if closest and color_distance(color, closest) < 2000:
                color_to_attr[color] = color_to_attr[closest]
            else:
                color_to_attr[color] = next_attr
                next_attr += 1

    # Sample center pixel of each cell
    for gy in range(min(grid_h, MAP_HEIGHT)):
        for gx in range(min(grid_w, MAP_WIDTH)):
            cx = gx * cell_size + cell_size // 2
            cy = gy * cell_size + cell_size // 2
            if cx < w and cy < h:
                color = pixels[cx, cy]
                attr_id = color_to_attr.get(color, 0)
                collision_map[gy * MAP_WIDTH + gx] = attr_id & 0xFF

    # Fill remaining cells with 255 (solid border)
    for gy in range(grid_h, MAP_HEIGHT):
        for gx in range(MAP_WIDTH):
            collision_map[gy * MAP_WIDTH + gx] = 255

    # Save collision map
    out = Path(output_dir)
    collision_map_path = out / 'collision.collision'
    with open(str(collision_map_path), 'wb') as f:
        f.write(collision_map)
    print(f"  Saved: {collision_map_path} ({len(collision_map)} bytes)")

    # Build 1bpp collision masks from the collision image
    # For each unique non-zero attribute ID, generate a mask from the image
    unique_attrs = set(collision_map)
    unique_attrs.discard(0)  # Don't need mask for passable
    num_masks = max(unique_attrs) + 1 if unique_attrs else 1

    # Generate mask tileset: each 8x8 tile is a 1bpp mask
    # For the collision system, masks define the solid shape within each cell
    # For build mode, we derive masks from the collision image pixels
    masks_per_row = 16
    mask_rows = (num_masks + masks_per_row - 1) // masks_per_row
    if mask_rows == 0:
        mask_rows = 1
    mask_img = Image.new('L', (masks_per_row * 8, mask_rows * 8), 255)
    mask_pixels = mask_img.load()

    # For each attribute ID, create a representative mask
    # Simple approach: solid (all black) for non-zero attributes
    for attr_id in unique_attrs:
        if attr_id == 0 or attr_id > 255:
            continue
        mx = (attr_id % masks_per_row) * 8
        my = (attr_id // masks_per_row) * 8
        for py in range(8):
            for px in range(8):
                if mx + px < mask_img.width and my + py < mask_img.height:
                    mask_pixels[mx + px, my + py] = 0  # Solid (dark)

    mask_path = out / 'collision_masks.png'
    mask_img.save(str(mask_path))
    print(f"  Saved: {mask_path}")


# ============================================================================
# CLI Entry Point
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Pokemon Pinball+ Table Builder Tool',
        epilog='Examples:\n'
               '  python build_table.py extract red_field_bottom\n'
               '  python build_table.py extract blue_field_top --output-dir my_templates\n'
               '  python build_table.py build my_custom_table/\n',
        formatter_class=argparse.RawDescriptionHelpFormatter)

    subparsers = parser.add_subparsers(dest='command', help='Command to run')

    # Extract subcommand
    extract_parser = subparsers.add_parser(
        'extract',
        help='Extract a stage as visual + collision images')
    extract_parser.add_argument(
        'stage_name',
        choices=list(STAGES.keys()),
        help='Stage to extract')
    extract_parser.add_argument(
        '--output-dir', '-o',
        help='Output directory (default: templates/<stage_name>)')
    extract_parser.add_argument(
        '--project-root', '-r',
        help='Project root directory (auto-detected if not specified)')

    # Build subcommand
    build_parser = subparsers.add_parser(
        'build',
        help='Build engine-compatible table data from images')
    build_parser.add_argument(
        'input_dir',
        help='Directory containing table_visual.png and palette.json')
    build_parser.add_argument(
        '--output-dir', '-o',
        help='Output directory (default: <input_dir>/build_output)')
    build_parser.add_argument(
        '--project-root', '-r',
        help='Project root directory (auto-detected if not specified)')

    # Extract-all subcommand
    extract_all_parser = subparsers.add_parser(
        'extract-all',
        help='Extract all 4 field stages as templates')
    extract_all_parser.add_argument(
        '--output-dir', '-o',
        default='templates',
        help='Base output directory (default: templates)')
    extract_all_parser.add_argument(
        '--project-root', '-r',
        help='Project root directory (auto-detected if not specified)')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    project_root = find_project_root(args.project_root if hasattr(args, 'project_root') else None)
    print(f"Project root: {project_root}")

    if args.command == 'extract':
        output_dir = args.output_dir or f'templates/{args.stage_name}'
        success = extract_stage(args.stage_name, project_root, output_dir)
        sys.exit(0 if success else 1)

    elif args.command == 'extract-all':
        base_out = Path(args.output_dir)
        all_ok = True
        for stage_name in STAGES:
            output_dir = base_out / stage_name
            ok = extract_stage(stage_name, project_root, str(output_dir))
            if not ok:
                all_ok = False
            print()
        sys.exit(0 if all_ok else 1)

    elif args.command == 'build':
        output_dir = args.output_dir
        success = build_table(args.input_dir, output_dir)
        sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
