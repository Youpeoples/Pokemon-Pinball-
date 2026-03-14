# Table Builder Tool

A Python tool for extracting and building Pokemon Pinball+ table graphics.

**Extract** existing tables as full reconstructed images — use as templates.
**Build** new tables from modder-drawn images back into engine-compatible format.

## Prerequisites

- Python 3.8+
- Pillow: `pip install Pillow`

## Quick Start

```bash
cd "Pokemon Pinball+"

# Extract a single stage as a template
python native/tools/build_table.py extract red_field_bottom

# Extract all 4 field stages
python native/tools/build_table.py extract-all

# Build a custom table from edited images
python native/tools/build_table.py build my_custom_table/
```

## Extract Mode

Reconstructs what the table looks like in-game from the raw tile data, tilemaps, and palettes.

```bash
python native/tools/build_table.py extract <stage_name> [--output-dir DIR] [--project-root DIR]
```

**Stage names:** `red_field_top`, `red_field_bottom`, `blue_field_top`, `blue_field_bottom`

### Output Files

| File | Description |
|------|-------------|
| `table_visual.png` | 256x144 full-color image of the complete tilemap (32 columns) |
| `table_collision.png` | Color-coded collision map (32x22 cells, 8px each) |
| `palette.json` | All 8 BG palettes in GBC RGB555 and hex formats, plus SCX metadata |

The extracted images show the **full 32-column tilemap** (256px wide), not just the 160px GBC screen. A **magenta rectangle** marks the in-game viewport at the stage's default scroll position (SCX). For bottom stages, SCX=0x20 shifts the viewport right to reveal the ball launcher alley on the right side.

### Extract All

```bash
python native/tools/build_table.py extract-all [--output-dir templates]
```

Extracts all 4 field stages into `templates/<stage_name>/` subdirectories.

## Build Mode

Converts modder-drawn images back into engine-compatible binary formats.

```bash
python native/tools/build_table.py build <input_dir> [--output-dir DIR]
```

### Required Input Files

| File | Description |
|------|-------------|
| `table_visual.png` | 256x144 full-color table image (or any multiple-of-8 dimensions) |
| `palette.json` | 8 BG palettes (use extracted palette as starting point) |

**Note:** If editing an extracted template, erase the magenta viewport rectangle first — those pixels would otherwise be interpreted as tile data. The viewport is just a visual guide for where the GBC screen sits within the tilemap.

### Optional Input Files

| File | Description |
|------|-------------|
| `table_collision.png` | Color-coded collision zones |
| `collision_key.json` | Maps hex colors to collision attribute IDs |

### Output Files

| File | Description |
|------|-------------|
| `tileset.png` | Deduplicated grayscale tileset (16 tiles per row) |
| `tilemap.map` | 1024-byte binary tilemap (tile indices) |
| `tilemap_attrs.map` | 1024-byte attribute map (palette, bank, flips) |
| `collision.collision` | 1024-byte binary collision map |
| `collision_masks.png` | 1bpp collision mask tileset |
| `palette.json` | Copy of input palette |

## Complete Workflow

1. **Extract a template:**
   ```bash
   python native/tools/build_table.py extract red_field_bottom --output-dir my_table
   ```

2. **Edit the visual image** in any pixel editor (Aseprite, GIMP, Photoshop, etc.)
   - Keep to 256x144 pixels (full tilemap including alley) or 160x144 (screen-only)
   - Erase the magenta viewport rectangle before editing (it's just a visual guide)
   - Use only colors from the palette (4 colors per 8x8 tile block)
   - Stay within 256 unique tiles

3. **Edit the palette** if you want different colors:
   - Modify `palette.json` — change the `bg_palettes_hex` values
   - Each palette has exactly 4 colors
   - Each 8x8 tile can only use colors from one palette

4. **Edit the collision image** to define solid walls and passable areas:
   - White (#FFFFFF) = passable (ball rolls through)
   - Black (#000000) = solid wall
   - Dark gray (#303030) = border padding
   - Other colors = custom collision attributes (define in `collision_key.json`)

5. **Build the table:**
   ```bash
   python native/tools/build_table.py build my_table/
   ```

6. **Test in game** by copying output files to the appropriate locations or loading via Lua scripts.

## File Format Reference

### Tilemap (`.map`)

- 1024 bytes, 32x32 grid, row-major order
- Each byte = tile index using **signed addressing** (LCDC $67):
  - Index $80-$FF → VRAM $8800-$8FFF (first 128 tiles)
  - Index $00-$7F → VRAM $9000-$97FF (next 128 tiles)
- Only 20 columns and 18 rows are visible on the GBC screen at any time
- The visible region is controlled by SCX (horizontal scroll register)
- Top stages: SCX=0x00, columns 0-19 visible
- Bottom stages: SCX=0x20, columns 4-23 visible (shifted right by 32px to show the alley)

### Attribute Map (`.map`, bank 1)

- 1024 bytes, same 32x32 grid
- Per byte:
  - Bits 0-2: Palette ID (0-7)
  - Bit 3: Tile bank (0 or 1)
  - Bit 5: Horizontal flip
  - Bit 6: Vertical flip
  - Bit 7: BG priority over sprites

### Collision Map (`.collision`)

- 1024 bytes, 32x32 grid, row-major order
- Each byte = collision attribute ID
  - 0 = passable (no collision)
  - 1-207 = solid with specific collision mask shape
  - 208-255 = animated/dynamic collision objects
  - 255 = fully solid (border padding)

### Collision Masks (`.png`)

- 1bpp grayscale PNG
- 8x8 tiles arranged left-to-right, top-to-bottom (16 tiles per row)
- Dark pixels = solid, light pixels = passable
- Each mask ID in the collision map references one of these tiles

### Palette JSON

```json
{
  "format": "gbc_rgb555",
  "default_scx": 32,
  "viewport_note": "In-game viewport starts at pixel X=32...",
  "bg_palettes": [[32767, 32397, 4255, 0], ...],
  "bg_palettes_hex": [["#FFFFFF", "#6AA4FF", "#FF2020", "#000000"], ...]
}
```

- GBC RGB555: `r | (g << 5) | (b << 10)`, each component 0-31
- 8 palettes, 4 colors each
- `bg_palettes` = raw GBC values (lossless), `bg_palettes_hex` = human-readable
- `default_scx` = horizontal scroll offset in pixels (0 for top stages, 32 for bottom stages)

### 2bpp Tile Format

- 8x8 pixels = 16 bytes per tile
- 2 bytes per row: lo plane (bit 0), hi plane (bit 1)
- Bit 7 = leftmost pixel
- Color index: 0 = lightest (white), 3 = darkest (black)

## Scroll and Viewport (SCX)

The GBC tilemap is 256px wide (32 tiles), but the screen only shows 160px (20 tiles) at a time. The **SCX register** controls which part of the tilemap is visible.

| Stage Type | Default SCX | Visible Columns | Notes |
|------------|:-----------:|:---------------:|-------|
| Top stages | 0x00 (0px) | 0-19 | Tilemap left-aligned, columns 20-31 are offscreen |
| Bottom stages | 0x20 (32px) | 4-23 | Shifted right to show the ball launcher alley |

During gameplay, SCX dynamically adjusts ±2px based on the ball's X position (range 0 to 0x22), creating a subtle camera tracking effect.

**For custom tables**: The extracted 256x144 images show the complete tilemap. The magenta rectangle marks the default viewport. When designing a bottom stage, columns 0-3 are normally offscreen (left), and the ball launcher alley occupies columns ~20-23 (right side of the viewport).

## Collision Color Key

Default colors used in `table_collision.png`:

| Color | Hex | Meaning | Attribute ID |
|-------|-----|---------|:------------:|
| White | `#FFFFFF` | Passable | 0 |
| Black | `#000000` | Solid wall | 1 |
| Dark gray | `#303030` | Border padding | 255 |

Custom mappings can be defined in `collision_key.json`:
```json
{
  "#FF0000": 10,
  "#0000FF": 20,
  "#00FF00": 30
}
```

## Tips

- **4-color limit:** Each 8x8 tile block can use at most 4 colors from a single palette. The build tool auto-selects the best-matching palette per tile.
- **256 tile limit:** A stage can have at most 256 unique tiles (per bank). The build tool deduplicates tiles, including flipped variants (H-flip, V-flip, HV-flip reuse the same tile with attribute flags).
- **Palette planning:** Design your palette before drawing. Place frequently-used colors in palette 0, specialized colors in higher palettes.
- **Grid alignment:** All elements must align to 8x8 pixel boundaries. This is a hardware constraint of the GBC.
- **Collision alignment:** Collision cells correspond 1:1 with tile cells. Each 8x8 screen tile has one collision attribute.
- **Bank 1 tiles:** The build tool currently outputs all tiles to bank 0. For advanced users needing bank 1 tiles, manually edit the attribute map's bit 3.
- **Alley area:** Bottom stages have a ball launcher alley on the right side (columns ~20-23 of the tilemap). The extracted 256x144 images include this area. When designing custom bottom stages, make sure to draw the alley chute in the correct columns.
- **Viewport rectangle:** The magenta rectangle in extracted images shows the default GBC screen area. Erase it before building — it's only a visual guide.

## Pre-Extracted Templates

Templates for all 4 field stages are in `native/tools/templates/`:

```
templates/
├── red_field_top/
│   ├── table_visual.png
│   ├── table_collision.png
│   └── palette.json
├── red_field_bottom/
│   ├── table_visual.png
│   ├── table_collision.png
│   └── palette.json
├── blue_field_top/
│   ├── table_visual.png
│   ├── table_collision.png
│   └── palette.json
└── blue_field_bottom/
    ├── table_visual.png
    ├── table_collision.png
    └── palette.json
```
