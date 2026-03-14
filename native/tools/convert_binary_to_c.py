#!/usr/bin/env python3
"""
convert_binary_to_c.py - Convert binary data files to C source arrays.

Reads all binary data files (collision maps, flipper tables, tilt forces,
physics grids, collision angles, tilemaps, bgattr) and generates C source
files with static const uint8_t arrays that can be compiled into the
executable, eliminating the need for external binary data files at runtime.

Usage:
    python convert_binary_to_c.py [project_root]

If project_root is not specified, assumes the script is at native/tools/
and the project root is two levels up.
"""

import os
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
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


def path_to_c_name(rel_path: str) -> str:
    """Convert a relative file path to a valid C identifier."""
    name = os.path.basename(rel_path)
    # Remove extension
    for ext in [".collision", ".bin", ".map", ".bgattr"]:
        if name.endswith(ext):
            name = name[: -len(ext)]
            break
    # Replace non-alphanumeric with underscore
    return "".join(c if c.isalnum() else "_" for c in name)


def format_array(data: bytes, bytes_per_line: int = 16) -> str:
    """Format binary data as a C array initializer."""
    lines = []
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i : i + bytes_per_line]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    return "\n".join(lines)


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

        # Write all arrays
        for rel_path, c_name, data in entries:
            f.write(f"/* Source: {rel_path}\n")
            f.write(f" * Size: {len(data)} bytes */\n")
            f.write(
                f"static const uint8_t {category}_{c_name}[{len(data)}] = {{\n"
            )
            f.write(format_array(data))
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
            # Normalize path separators to forward slashes for matching
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
        # Script is at native/tools/, project root is 2 levels up
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
