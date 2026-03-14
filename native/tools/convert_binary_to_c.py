#!/usr/bin/env python3
"""
convert_binary_to_c.py - Convert binary data files to human-readable C source arrays.

Reads all binary data files (collision maps, flipper tables, tilt forces,
physics grids, collision angles, tilemaps, bgattr) and generates C source
files with structurally formatted, commented static const uint8_t arrays.

Each data type gets its own formatting:
  - Collision maps:     32x24 grids, __ for empty, decimal values
  - Collision angles:   NxM grids, angle in degrees or -- for no-collision
  - Physics forces:     NxM grids of {force_x, force_y} int16 pairs
  - Flipper data:       Per-state 48x32 grids
  - Tilt tables:        256 force vector entries with angle labels
  - Tilemaps:           32xN grids of hex tile indices
  - Background attrs:   32xN grids with decoded palette/bank/flip fields

Usage:
    python convert_binary_to_c.py [project_root]

If project_root is not specified, assumes the script is at native/tools/
and the project root is two levels up.
"""

import os
import sys
import struct
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration - file lists
# ---------------------------------------------------------------------------

COLLISION_MAPS = [
    "data/collision/maps/blue_stage_bottom.collision",
    "data/collision/maps/blue_stage_top.collision",
    "data/collision/maps/blue_stage_top_ball_entrance.collision",
    "data/collision/maps/diglett_bonus.collision",
    "data/collision/maps/diglett_bonus_ball_entrance.collision",
    "data/collision/maps/gengar_bonus.collision",
    "data/collision/maps/gengar_bonus_ball_entrance.collision",
    "data/collision/maps/meowth_bonus.collision",
    "data/collision/maps/meowth_bonus_ball_entrance.collision",
    "data/collision/maps/mewtwo_bonus.collision",
    "data/collision/maps/mewtwo_bonus_ball_entrance.collision",
    "data/collision/maps/red_stage_bottom.collision",
    "data/collision/maps/red_stage_top_0.collision",
    "data/collision/maps/red_stage_top_1.collision",
    "data/collision/maps/red_stage_top_2.collision",
    "data/collision/maps/red_stage_top_3.collision",
    "data/collision/maps/red_stage_top_4.collision",
    "data/collision/maps/red_stage_top_5.collision",
    "data/collision/maps/red_stage_top_6.collision",
    "data/collision/maps/red_stage_top_7.collision",
    "data/collision/maps/seel_bonus.collision",
    "data/collision/maps/seel_bonus_ball_entrance.collision",
]

FLIPPER_FILES = [
    "data/collision/flippers/radii_0",
    "data/collision/flippers/radii_1",
    "data/collision/flippers/normal_angles_0",
    "data/collision/flippers/normal_angles_1",
]

TILT_FILES = [
    "data/tilt/left_only",
    "data/tilt/right_only",
    "data/tilt/up_only",
    "data/tilt/up_left",
    "data/tilt/up_right",
]

PHYSICS_FILES = [
    "data/collision/ball_physics_e4000.bin",
    "data/collision/ball_physics_ec000.bin",
    "data/collision/ball_physics_f0000.bin",
]

COLLISION_ANGLE_FILES = [
    "data/collision/circle_collision_angles.bin",
    "data/collision/gengar_collision_angles.bin",
    "data/collision/haunter_collision_angles.bin",
    "data/collision/meowth_collision_angles.bin",
    "data/collision/meowth_jewel_collision_angles.bin",
]

TILEMAP_FILES = [
    "gfx/tilemaps/copyright_screen.map",
    "gfx/tilemaps/erase_all_data.map",
    "gfx/tilemaps/field_select.map",
    "gfx/tilemaps/high_scores_screen.map",
    "gfx/tilemaps/high_scores_screen_2.map",
    "gfx/tilemaps/high_scores_screen_4.map",
    "gfx/tilemaps/high_scores_screen_5.map",
    "gfx/tilemaps/option_menu.map",
    "gfx/tilemaps/option_menu_2.map",
    "gfx/tilemaps/option_menu_3.map",
    "gfx/tilemaps/option_menu_4.map",
    "gfx/tilemaps/pokedex.map",
    "gfx/tilemaps/pokedex_2.map",
    "gfx/tilemaps/stage_blue_field_bottom_gameboycolor.map",
    "gfx/tilemaps/stage_blue_field_bottom_gameboycolor_2.map",
    "gfx/tilemaps/stage_blue_field_top_gameboycolor.map",
    "gfx/tilemaps/stage_blue_field_top_gameboycolor_2.map",
    "gfx/tilemaps/stage_diglett_bonus_gameboycolor.map",
    "gfx/tilemaps/stage_diglett_bonus_gameboycolor_2.map",
    "gfx/tilemaps/stage_gengar_bonus_gameboycolor.map",
    "gfx/tilemaps/stage_gengar_bonus_gameboycolor_2.map",
    "gfx/tilemaps/stage_meowth_bonus_gameboycolor.map",
    "gfx/tilemaps/stage_meowth_bonus_gameboycolor_2.map",
    "gfx/tilemaps/stage_mewtwo_bonus_gameboycolor.map",
    "gfx/tilemaps/stage_mewtwo_bonus_gameboycolor_2.map",
    "gfx/tilemaps/stage_red_field_bottom_gameboycolor.map",
    "gfx/tilemaps/stage_red_field_bottom_gameboycolor_2.map",
    "gfx/tilemaps/stage_red_field_top_gameboycolor.map",
    "gfx/tilemaps/stage_red_field_top_gameboycolor_2.map",
    "gfx/tilemaps/stage_seel_bonus_gameboycolor.map",
    "gfx/tilemaps/stage_seel_bonus_gameboycolor_2.map",
    "gfx/tilemaps/titlescreen.map",
]

BGATTR_FILES = [
    "gfx/bgattr/copyright_screen.bgattr",
    "gfx/bgattr/erase_all_data.bgattr",
    "gfx/bgattr/field_select.bgattr",
    "gfx/bgattr/pokedex.bgattr",
    "gfx/bgattr/pokedex_2.bgattr",
    "gfx/bgattr/titlescreen.bgattr",
]

# ---------------------------------------------------------------------------
# Known grid dimensions for collision angle files
# ---------------------------------------------------------------------------

COLLISION_ANGLE_DIMS = {
    "circle_collision_angles":       (32, 32),
    "haunter_collision_angles":      (32, 40),
    "gengar_collision_angles":       (48, 64),
    "meowth_collision_angles":       (48, 40),
    "meowth_jewel_collision_angles": (24, 24),
}

# Known grid dimensions for physics force fields
PHYSICS_DIMS = {
    "ball_physics_e4000":  (32, 32),
    "ball_physics_f0000":  (48, 48),
    # ec000 is 16384 bytes = 4096 entries; accessed as raw lookup
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def path_to_c_name(rel_path: str) -> str:
    """Convert a relative file path to a valid C identifier."""
    name = os.path.basename(rel_path)
    for ext in [".collision", ".bin", ".map", ".bgattr"]:
        if name.endswith(ext):
            name = name[: -len(ext)]
            break
    return "".join(c if c.isalnum() else "_" for c in name)


def format_array_raw(data: bytes, bytes_per_line: int = 16) -> str:
    """Fallback: format binary data as plain hex rows (no structural comments)."""
    lines = []
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Type-specific formatters
# ---------------------------------------------------------------------------

def format_collision_map(data: bytes) -> str:
    """Format a 768-byte collision map as a 32x24 grid.
    0x00 = empty (shown as __), other values shown as decimal."""
    COLS = 32
    ROWS = len(data) // COLS
    lines = []
    lines.append("    /* 32x%d collision attribute grid. 0=empty/passable, other=collision mask index." % ROWS)
    lines.append("     * D0-DF=animated objects, E0-FF=flipper-dependent masks */")
    # Column header
    hdr = "    /*       col:"
    for c in range(COLS):
        hdr += f" {c:>3d}"
        if c == 15:
            hdr += "  "
    hdr += " */"
    lines.append(hdr)

    for r in range(ROWS):
        row_data = data[r * COLS : (r + 1) * COLS]
        def fmt_cmap(b):
            return f"  0" if b == 0 else f"{b:>3d}"
        left = ",".join(fmt_cmap(b) for b in row_data[:16])
        right = ",".join(fmt_cmap(b) for b in row_data[16:])
        line = f"    /* row {r:>2d} */ {left},  {right},"
        lines.append(line)

    return "\n".join(lines)


def format_collision_angles(data: bytes, c_name: str) -> str:
    """Format collision angle data as an NxM grid.
    Values 0x00-0x7F = collision angle (shown in degrees), 0x80+ = no collision (--)."""
    # Look up dimensions
    key = c_name
    for prefix in ["cang_"]:
        if key.startswith(prefix):
            key = key[len(prefix):]
    dims = COLLISION_ANGLE_DIMS.get(key)
    if dims is None or dims[0] * dims[1] != len(data):
        return format_array_raw(data)

    cols, rows = dims
    lines = []
    lines.append(f"    /* {cols}x{rows} collision angle grid (dx x dy from entity center)")
    lines.append("     * Values: GBC angle units (0-127), 0x80+=no collision (--)")
    lines.append("     * Angle: 0=right(0deg), 32=down(45deg), 64=left(90deg), 96=up(135deg) */")

    # Column header
    hdr = "    /*       dx:"
    for c in range(cols):
        hdr += f" {c:>3d}"
        if c % 16 == 15 and c < cols - 1:
            hdr += "   "
    hdr += " */"
    lines.append(hdr)

    for r in range(rows):
        row_data = data[r * cols : (r + 1) * cols]
        def fmt_angle(b):
            return f"{b:>3d}"
        # Split into 16-column groups with visual gap
        groups = []
        for g in range(0, cols, 16):
            chunk = row_data[g:g+16]
            groups.append(",".join(fmt_angle(b) for b in chunk))
        line = f"    /* dy {r:>2d} */ " + ",  ".join(groups) + ","
        lines.append(line)

    return "\n".join(lines)


def format_physics_field(data: bytes, c_name: str) -> str:
    """Format physics force field as grid of {force_x, force_y} int16 pairs.
    Each entry is 4 bytes: int16_le force_x, int16_le force_y."""
    key = c_name
    for prefix in ["phys_"]:
        if key.startswith(prefix):
            key = key[len(prefix):]
    dims = PHYSICS_DIMS.get(key)
    num_entries = len(data) // 4

    if dims is not None and dims[0] * dims[1] == num_entries:
        cols, rows = dims
    else:
        # Raw dump for ec000 or unknown - use 16 entries per line
        return _format_physics_raw(data)

    lines = []
    lines.append(f"    /* {cols}x{rows} force field grid. Each entry = (vx, vy) as int16 LE.")
    lines.append("     * Positive vx=push right, positive vy=push down. Units: fixed8.8 */")

    for r in range(rows):
        entries = []
        for c in range(cols):
            off = (r * cols + c) * 4
            fx = struct.unpack_from("<h", data, off)[0]
            fy = struct.unpack_from("<h", data, off + 2)[0]
            # Output as raw bytes (the array is uint8_t) but with decoded comment
            b0, b1, b2, b3 = data[off], data[off+1], data[off+2], data[off+3]
            entries.append((b0, b1, b2, b3, fx, fy))

        # Write raw bytes with decoded values as end-of-line comment
        raw_parts = []
        decoded_parts = []
        for b0, b1, b2, b3, fx, fy in entries:
            raw_parts.append(f"0x{b0:02X},0x{b1:02X},0x{b2:02X},0x{b3:02X}")
            decoded_parts.append(f"({fx:+5d},{fy:+5d})")
        raw_str = ", ".join(raw_parts) + ","
        dec_str = " ".join(decoded_parts)
        line = f"    /*[row {r:>2d}]*/ {raw_str} /* {dec_str} */"
        lines.append(line)

    return "\n".join(lines)


def _format_physics_raw(data: bytes) -> str:
    """Format physics data that doesn't fit a known grid (ec000).
    Show 4 entries per line with decoded force values."""
    num_entries = len(data) // 4
    ENTRIES_PER_LINE = 8
    lines = []
    lines.append(f"    /* {num_entries} force vector entries (int16 vx, int16 vy each).")
    lines.append("     * Raw lookup table, indexed by collision geometry. */")

    for i in range(0, num_entries, ENTRIES_PER_LINE):
        count = min(ENTRIES_PER_LINE, num_entries - i)
        raw_parts = []
        decoded_parts = []
        for j in range(count):
            off = (i + j) * 4
            b0, b1, b2, b3 = data[off], data[off+1], data[off+2], data[off+3]
            fx = struct.unpack_from("<h", data, off)[0]
            fy = struct.unpack_from("<h", data, off + 2)[0]
            raw_parts.append(f"0x{b0:02X},0x{b1:02X},0x{b2:02X},0x{b3:02X}")
            decoded_parts.append(f"({fx:+d},{fy:+d})")
        raw_str = ", ".join(raw_parts) + ","
        dec_str = " ".join(decoded_parts)
        line = f"    /*[{i:>4d}]*/ {raw_str} /* {dec_str} */"
        lines.append(line)

    return "\n".join(lines)


def format_flipper_data(data: bytes, c_name: str) -> str:
    """Format flipper radii or normal angle data as per-state 48x32 grids.
    Total data is split across bank 0 and bank 1 files."""
    STATE_COLS = 48
    STATE_ROWS = 32
    STATE_SIZE = STATE_COLS * STATE_ROWS  # 1536 bytes per state

    key = c_name
    for prefix in ["flip_"]:
        if key.startswith(prefix):
            key = key[len(prefix):]

    is_radii = "radii" in key
    is_bank0 = key.endswith("_0")
    data_type = "radii" if is_radii else "normal angles"

    num_states = len(data) // STATE_SIZE
    remainder = len(data) % STATE_SIZE

    if num_states == 0:
        return format_array_raw(data)

    # Determine starting state index based on bank
    if is_bank0:
        start_state = 0
    else:
        # Bank 1 continues from where bank 0 left off
        # radii_0=16384 bytes=10.67 states, radii_1=8192 bytes=5.33 states
        # The actual split: bank 0 has floor(size/STATE_SIZE) full states + remainder
        # For radii: bank 0 = 16384/1536 = 10 full states + 1024 remainder
        # Bank 1 starts at state 10 but has partial first state from bank 0 remainder
        # Actually the data is continuous - bank 0 has first 16384 bytes, bank 1 has next 8192
        # So states span across the boundary.
        # For radii: 16384+8192=24576 = 16 states exactly (16*1536=24576)
        # Bank 0: states 0-9 full (15360 bytes) + 1024 bytes of state 10
        # Bank 1: 512 bytes of state 10 + states 11-15 full (7680 bytes) = 8192
        # For normal_angles: 8192+16384=24576 = 16 states too
        # Bank 0: 8192 / 1536 = 5 full states (7680) + 512 bytes of state 5
        # Bank 1: 1024 bytes of state 5 + states 6-15 (15360) = 16384
        # So we can't neatly label states for bank files - they're split mid-state.
        # Best approach: just label by byte offset within the continuous data.
        pass

    lines = []
    lines.append(f"    /* Flipper {data_type} lookup table ({'bank 0' if is_bank0 else 'bank 1'}).")
    lines.append(f"     * Combined banks form 16 states x {STATE_COLS} cols x {STATE_ROWS} rows = 24576 bytes.")
    lines.append(f"     * Per state: indexed as offset = col * {STATE_ROWS} + row.")
    if is_radii:
        lines.append("     * Values: distance from flipper pivot (0=no collision zone). */")
    else:
        lines.append("     * Values: collision normal angle in GBC units (0-255). 0=no collision zone. */")

    # Format as state-aligned blocks where possible, raw grid otherwise
    byte_offset = 0
    # Calculate which states are fully contained in this bank
    if is_bank0:
        global_offset = 0
    else:
        # radii_0 and normal_angles_0 sizes
        if is_radii:
            global_offset = 16384
        else:
            global_offset = 8192

    # Process data in state-sized chunks where they align
    pos = 0
    while pos < len(data):
        global_pos = global_offset + pos
        state_idx = global_pos // STATE_SIZE
        offset_in_state = global_pos % STATE_SIZE

        if offset_in_state == 0 and pos + STATE_SIZE <= len(data):
            # Full state available - format as labeled grid
            state_data = data[pos : pos + STATE_SIZE]
            lines.append(f"    /* --- State {state_idx:>2d} --- */")

            for col in range(STATE_COLS):
                row_vals = []
                for row in range(STATE_ROWS):
                    b = state_data[col * STATE_ROWS + row]
                    row_vals.append(f"{b:>3d}")
                line = f"    /* s{state_idx:>2d} c{col:>2d} */ " + ",".join(row_vals) + ","
                lines.append(line)
            pos += STATE_SIZE
        else:
            # Partial state at boundary - format raw with offset labels
            chunk_end = min(len(data), pos + (STATE_SIZE - offset_in_state))
            chunk = data[pos:chunk_end]
            col_in_state = offset_in_state // STATE_ROWS
            row_in_state = offset_in_state % STATE_ROWS

            lines.append(f"    /* --- State {state_idx} (partial, cols {col_in_state}-{STATE_COLS-1 if chunk_end - pos >= STATE_SIZE - offset_in_state else col_in_state + (chunk_end - pos) // STATE_ROWS}) --- */")

            ci = col_in_state
            ri = row_in_state
            idx = 0
            while idx < len(chunk):
                # How many bytes left in this column
                remaining_in_col = STATE_ROWS - ri
                count = min(remaining_in_col, len(chunk) - idx)
                row_vals = []
                for k in range(count):
                    b = chunk[idx + k]
                    row_vals.append(f"{b:>3d}")
                # Pad if partial column at start
                if ri > 0 and idx == 0:
                    line = f"    /* s{state_idx:>2d} c{ci:>2d} r{ri:>2d}+ */ " + ",".join(row_vals) + ","
                else:
                    line = f"    /* s{state_idx:>2d} c{ci:>2d} */ " + ",".join(row_vals) + ","
                lines.append(line)
                idx += count
                ci += 1
                ri = 0

            pos = chunk_end

    return "\n".join(lines)


def format_tilt_table(data: bytes) -> str:
    """Format a 1024-byte tilt force table as 256 force vector entries.
    Each entry: 4 bytes = int16_le force_x, int16_le force_y.
    Index = collision normal angle (0-255)."""
    if len(data) != 1024:
        return format_array_raw(data)

    lines = []
    lines.append("    /* 256 force vectors indexed by collision normal angle.")
    lines.append("     * angle 0=right(0deg), 64=down(90deg), 128=left(180deg), 192=up(270deg)")
    lines.append("     * Each entry: int16 force_x (LE), int16 force_y (LE) */")

    for i in range(256):
        off = i * 4
        b0, b1, b2, b3 = data[off], data[off+1], data[off+2], data[off+3]
        fx = struct.unpack_from("<h", data, off)[0]
        fy = struct.unpack_from("<h", data, off + 2)[0]
        deg = i * 360.0 / 256.0
        line = f"    /*[{i:>3d} = {deg:>5.1f}deg]*/ 0x{b0:02X},0x{b1:02X}, 0x{b2:02X},0x{b3:02X}, /* vx={fx:+6d}, vy={fy:+6d} */"
        lines.append(line)

    return "\n".join(lines)


def format_tilemap(data: bytes) -> str:
    """Format tilemap data as a 32xN grid of hex tile indices."""
    COLS = 32
    ROWS = len(data) // COLS
    if ROWS * COLS != len(data):
        return format_array_raw(data)

    lines = []
    lines.append(f"    /* {COLS}x{ROWS} tilemap grid. Tile indices reference VRAM tile data.")
    lines.append("     * Addressing mode depends on LCDC: $43=unsigned (menus), $67=signed (pinball) */")

    # Column header
    hdr = "    /*       col:"
    for c in range(COLS):
        hdr += f"  {c:02X}"
        if c == 15:
            hdr += "  "
    hdr += " */"
    lines.append(hdr)

    for r in range(ROWS):
        row_data = data[r * COLS : (r + 1) * COLS]
        left = ",".join(f"0x{b:02X}" for b in row_data[:16])
        right = ",".join(f"0x{b:02X}" for b in row_data[16:])
        line = f"    /* row {r:>2d} */ {left},  {right},"
        lines.append(line)

    return "\n".join(lines)


def _decode_bgattr(b: int) -> str:
    """Decode a background attribute byte into a short label.
    Bits: palette[0:2], bank[3], xflip[5], yflip[6], priority[7]."""
    pal = b & 0x07
    bank = (b >> 3) & 1
    xflip = (b >> 5) & 1
    yflip = (b >> 6) & 1
    pri = (b >> 7) & 1
    label = f"P{pal}B{bank}"
    if xflip:
        label += "x"
    if yflip:
        label += "y"
    if pri:
        label += "p"
    return label


def format_bgattr(data: bytes) -> str:
    """Format background attribute data as a 32xN grid with decoded fields.
    Each byte: pal[0:2] bank[3] xflip[5] yflip[6] priority[7]."""
    COLS = 32
    ROWS = len(data) // COLS
    if ROWS * COLS != len(data):
        return format_array_raw(data)

    lines = []
    lines.append(f"    /* {COLS}x{ROWS} bgattr grid. Each byte: pal[0:2] bank[3] xflip[5] yflip[6] priority[7]")
    lines.append("     * P=palette(0-7), B=tile bank, x=xflip, y=yflip, p=priority */")

    for r in range(ROWS):
        row_data = data[r * COLS : (r + 1) * COLS]
        hex_vals = []
        decoded = []
        for b in row_data:
            hex_vals.append(f"0x{b:02X}")
            decoded.append(_decode_bgattr(b))
        hex_str = ",".join(hex_vals) + ","
        dec_str = " ".join(f"{d:<5s}" for d in decoded)
        line = f"    /* row {r:>2d} */ {hex_str}"
        # Add decoded comment (can be long, but useful)
        line += f" /* {dec_str} */"
        lines.append(line)

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Format dispatcher
# ---------------------------------------------------------------------------

def format_data(data: bytes, category: str, c_name: str) -> str:
    """Dispatch to the appropriate formatter based on data category."""
    if category == "cmap":
        return format_collision_map(data)
    elif category == "cang":
        return format_collision_angles(data, c_name)
    elif category == "phys":
        return format_physics_field(data, c_name)
    elif category == "flip":
        return format_flipper_data(data, c_name)
    elif category == "tilt":
        return format_tilt_table(data)
    elif category == "tmap":
        return format_tilemap(data)
    elif category == "bga":
        return format_bgattr(data)
    else:
        return format_array_raw(data)


# ---------------------------------------------------------------------------
# File generation
# ---------------------------------------------------------------------------

def generate_data_file(
    out_path: str,
    file_list: list,
    root: str,
    category: str,
    description: str,
    lookup_fn_name: str,
):
    """Generate a C source file with embedded arrays and a lookup function."""
    entries = []
    for rel_path in file_list:
        full_path = os.path.join(root, rel_path)
        if not os.path.isfile(full_path):
            print(f"  WARNING: {rel_path} not found, skipping")
            continue
        with open(full_path, "rb") as f:
            data = f.read()
        c_name = path_to_c_name(rel_path)
        entries.append((rel_path, c_name, data))

    with open(out_path, "w", newline="\n") as f:
        f.write(f"/* {os.path.basename(out_path)} - {description}\n")
        f.write(f" * Auto-generated by convert_binary_to_c.py\n")
        f.write(f" * {len(entries)} embedded arrays, category: {category}\n")
        f.write(f" */\n\n")
        f.write(f'#include "embedded_data.h"\n')
        f.write(f"#include <string.h>\n\n")

        # Write all arrays with type-specific formatting
        for rel_path, c_name, data in entries:
            f.write(f"/* Source: {rel_path}\n")
            f.write(f" * Size: {len(data)} bytes */\n")
            f.write(
                f"static const uint8_t {category}_{c_name}[{len(data)}] = {{\n"
            )
            f.write(format_data(data, category, f"{category}_{c_name}"))
            f.write(f"\n}};\n\n")

        # Write lookup table
        f.write(f"/* Lookup table for {category} data */\n")
        f.write(f"typedef struct {{\n")
        f.write(f"    const char *path;\n")
        f.write(f"    const uint8_t *data;\n")
        f.write(f"    size_t size;\n")
        f.write(f"}} EmbeddedEntry;\n\n")

        f.write(
            f"static const EmbeddedEntry {category}_entries[] = {{\n"
        )
        for rel_path, c_name, data in entries:
            norm_path = rel_path.replace("\\", "/")
            f.write(
                f'    {{ "{norm_path}", {category}_{c_name}, {len(data)} }},\n'
            )
        f.write(f"}};\n\n")

        f.write(
            f"#define {category.upper()}_COUNT (sizeof({category}_entries) / sizeof({category}_entries[0]))\n\n"
        )

        # Write lookup function
        f.write(
            f"const uint8_t *{lookup_fn_name}(const char *path, size_t *out_size) {{\n"
        )
        f.write(
            f"    for (size_t i = 0; i < {category.upper()}_COUNT; i++) {{\n"
        )
        f.write(
            f"        if (strcmp(path, {category}_entries[i].path) == 0) {{\n"
        )
        f.write(f"            *out_size = {category}_entries[i].size;\n")
        f.write(f"            return {category}_entries[i].data;\n")
        f.write(f"        }}\n")
        f.write(f"    }}\n")
        f.write(f"    *out_size = 0;\n")
        f.write(f"    return NULL;\n")
        f.write(f"}}\n")

    total_bytes = sum(len(d) for _, _, d in entries)
    print(
        f"  Generated {os.path.basename(out_path)}: "
        f"{len(entries)} arrays, {total_bytes:,} bytes total"
    )


def generate_header(out_path: str):
    """Generate the embedded_data.h header file."""
    with open(out_path, "w", newline="\n") as f:
        f.write("/* embedded_data.h - Embedded binary data accessors\n")
        f.write(" * Auto-generated by convert_binary_to_c.py\n")
        f.write(" */\n\n")
        f.write("#ifndef EMBEDDED_DATA_H\n")
        f.write("#define EMBEDDED_DATA_H\n\n")
        f.write("#include <stdint.h>\n")
        f.write("#include <stddef.h>\n\n")

        lookups = [
            ("embedded_collision_map", "Collision map (.collision) by relative path"),
            ("embedded_flipper_data", "Flipper radii/normal angle data by relative path"),
            ("embedded_tilt_table", "Tilt force table by relative path"),
            ("embedded_physics_data", "Ball physics force field by relative path"),
            ("embedded_collision_angles", "Collision angle table by relative path"),
            ("embedded_tilemap", "Tilemap (.map) by relative path"),
            ("embedded_bgattr", "Background attributes (.bgattr) by relative path"),
        ]
        for fn_name, desc in lookups:
            f.write(f"/* Look up {desc}.\n")
            f.write(f" * Returns pointer to static const data; DO NOT free().\n")
            f.write(f" * Sets *out_size to data length. Returns NULL if not found. */\n")
            f.write(
                f"const uint8_t *{fn_name}(const char *path, size_t *out_size);\n\n"
            )

        f.write(
            "/* Try external file first, fall back to embedded data.\n"
            " * ALWAYS returns a malloc'd copy (caller must free).\n"
            " * Returns NULL only if both sources unavailable. */\n"
            "uint8_t *load_binary_data(const char *base_path,\n"
            "                          const char *relative_path,\n"
            "                          size_t *out_size);\n\n"
        )

        f.write("#endif /* EMBEDDED_DATA_H */\n")

    print(f"  Generated {os.path.basename(out_path)}")


def main():
    if len(sys.argv) > 1:
        root = sys.argv[1]
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        root = os.path.normpath(os.path.join(script_dir, "..", ".."))

    out_dir = os.path.join(root, "native", "src", "data")
    os.makedirs(out_dir, exist_ok=True)

    print(f"Project root: {root}")
    print(f"Output directory: {out_dir}")
    print()

    # Generate header
    generate_header(os.path.join(out_dir, "embedded_data.h"))

    # Generate each data category
    categories = [
        (COLLISION_MAPS, "cmap", "Embedded collision map data",
         "embedded_collision_map", "collision_maps_data.c"),
        (FLIPPER_FILES, "flip", "Embedded flipper collision data",
         "embedded_flipper_data", "flipper_data.c"),
        (TILT_FILES, "tilt", "Embedded tilt force table data",
         "embedded_tilt_table", "tilt_data.c"),
        (PHYSICS_FILES, "phys", "Embedded ball physics force field data",
         "embedded_physics_data", "physics_data.c"),
        (COLLISION_ANGLE_FILES, "cang", "Embedded collision angle data",
         "embedded_collision_angles", "collision_angles_data.c"),
        (TILEMAP_FILES, "tmap", "Embedded tilemap data",
         "embedded_tilemap", "tilemap_data.c"),
        (BGATTR_FILES, "bga", "Embedded background attribute data",
         "embedded_bgattr", "bgattr_data.c"),
    ]

    for file_list, category, description, lookup_fn, out_name in categories:
        generate_data_file(
            os.path.join(out_dir, out_name),
            file_list,
            root,
            category,
            description,
            lookup_fn,
        )

    print("\nDone! Generated files in", out_dir)


if __name__ == "__main__":
    main()
