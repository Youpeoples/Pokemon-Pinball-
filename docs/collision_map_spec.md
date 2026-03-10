# Collision Map System -- Technical Specification

This document describes the collision detection data formats and algorithms used by
Pokemon Pinball+. It is intended for modders who want to create custom pinball tables
or modify existing stage collision geometry.

---

## 1. Overview

The collision system uses two complementary data types:

1. **Collision Maps** (`.collision` files) -- grid-based arrays of mask indices that
   define which collision mask applies at each tile position on the playfield.
2. **Collision Masks** (`.png` files loaded as 1bpp bitmaps) -- 8x8 pixel bitmaps that
   define per-pixel solid/passable regions within a single tile.

During gameplay, the ball's pixel position is converted to a tile coordinate. The
collision map is queried to find which mask index applies at that tile. The mask bitmap
is then tested against 16 points distributed around the ball's circumference to
determine whether a collision has occurred and, if so, the surface normal angle.

The collision system also handles two categories of dynamic masks:
- **Animated Pokemon masks** (indices 0xD0-0xDF) for catch'em mode encounters
- **Flipper masks** (indices 0xE0-0xFF) that change based on flipper angular position

---

## 2. Collision Map Binary Format (`.collision` files)

### File Location

```
data/collision/maps/{stage_name}.collision
```

### File Size

Each `.collision` file is exactly **1024 bytes** (0x400) on disk. This represents a
32-row x 32-column grid. However, only the first **768 bytes** (0x300) are loaded into
memory, covering **24 rows x 32 columns**. The remaining 256 bytes (8 rows) are unused
padding.

### Memory Layout

The collision map is loaded into `stage_collision_map[0x300]` (768 bytes) at GBC
address `$C700-$C9FF`. The grid is organized with a **32-byte row stride**:

```
Offset  = (tile_row * 32) + tile_column
```

For a playfield that is 18 tiles wide (144 pixels) and 24 tiles tall (192 pixels):

```
Row 0:   bytes 0x000-0x01F  (columns 0-31, only 0-17 used for 18-tile-wide field)
Row 1:   bytes 0x020-0x03F
Row 2:   bytes 0x040-0x05F
...
Row 23:  bytes 0x2E0-0x2FF
```

Within each 32-byte row, bytes at column indices 18-31 are padding (typically 0x00).
Only columns 0-17 correspond to visible playfield tiles.

### Byte Values (Mask Indices)

Each byte in the collision map is a **mask index** that references a collision mask
bitmap:

| Index Range  | Category              | Description                                    |
|--------------|-----------------------|------------------------------------------------|
| 0x00         | Empty                 | No collision (passable)                        |
| 0x01-0xCF    | Static masks          | Standard collision geometry (walls, ramps, etc)|
| 0xD0-0xDF    | Animated mon masks    | Pokemon collision shapes during catch'em mode  |
| 0xE0-0xEF    | Left flipper masks    | Dynamic masks that change with flipper angle   |
| 0xF0-0xFF    | Right flipper masks   | Dynamic masks that change with flipper angle   |

### Tile Coordinate Calculation

The engine converts the ball's pixel position to a tile offset as follows:

```c
// Ball position is 8.8 fixed-point; extract integer pixel coordinates
uint8_t ball_x = ball_x_pos >> 8;
uint8_t ball_y = ball_y_pos >> 8;

// Subtract ball radius (4 pixels) to get top-left of collision test area
uint8_t adj_x = ball_x - 4;
uint8_t adj_y = ball_y - 4;

// Tile coordinates (divide by 8, then multiply row by 32)
uint8_t tile_x = (adj_x & 0xF8) >> 3;      // 0-17 for an 18-tile-wide field
uint8_t tile_y = (adj_y & 0xF8);            // Pre-multiplied by 8
uint16_t offset = (tile_y * 4) + tile_x;    // tile_y*4 = (adj_y/8)*32
```

The engine then reads a **2x2 block** of four adjacent mask indices from the collision
map at offsets: `[offset]`, `[offset+1]`, `[offset+32]`, `[offset+33]`.

---

## 3. Collision Mask Format

### Mask PNG Files

**Location:**
```
data/collision/masks/{stage_name}.png
```

**Format:** 1-bit (mode `1`) or grayscale PNG containing multiple 8x8 tiles arranged
in a grid. The PNG dimensions must be multiples of 8 in both width and height.

**Tile ordering:** Left-to-right, top-to-bottom (row-major), matching `rgbgfx` output
order. The position of a tile in the grid determines its mask index:

```
mask_index = tile_row * tiles_per_row + tile_column
```

For example, a 128x128 pixel mask PNG contains 16x16 = 256 tiles (indices 0x00-0xFF).

**Pixel interpretation:**
- **Dark pixels** (grayscale value < 128, or black in 1-bit mode) = **solid** (bit = 1)
- **Light pixels** (grayscale value >= 128, or white in 1-bit mode) = **passable** (bit = 0)

### In-Memory Mask Format (1bpp)

Each 8x8 tile is stored as **8 bytes** (1 byte per row, 1 bit per pixel):

```
Byte 0: Row 0 (top)    -- bit 7 = leftmost pixel, bit 0 = rightmost pixel
Byte 1: Row 1
Byte 2: Row 2
...
Byte 7: Row 7 (bottom)
```

A mask tile's data starts at byte offset `mask_index * 8` in the loaded mask array.

### Mask PNG Dimensions by Stage

| Stage Mask File                        | Dimensions  | Tile Grid | Total Masks |
|----------------------------------------|-------------|-----------|-------------|
| `red_stage_top_0.png` through `_3.png` | 128x128 px  | 16x16     | 256         |
| `red_stage_bottom.png`                 | 128x64 px   | 16x8      | 128         |
| `blue_stage_top.png`                   | 128x128 px  | 16x16     | 256         |
| `blue_stage_bottom.png`               | 128x128 px  | 16x16     | 256         |
| `gengar_bonus.png`                     | 64x48 px    | 8x6       | 48          |
| `mewtwo_bonus.png`                     | 64x32 px    | 8x4       | 32          |
| `meowth_bonus.png`                     | 64x32 px    | 8x4       | 32          |
| `diglett_bonus.png`                    | 64x96 px    | 8x12      | 96          |
| `seel_bonus.png`                       | 64x32 px    | 8x4       | 32          |
| `bottom_left_masks.png`               | 64x48 px    | 8x6       | 48          |
| `bottom_right_masks.png`              | 64x48 px    | 8x6       | 48          |
| `bottom_left_bonus_stage_masks.png`   | 64x48 px    | 8x6       | 48          |
| `bottom_right_bonus_stage_masks.png`  | 64x48 px    | 8x6       | 48          |

---

## 4. Collision Detection Algorithm

The collision detection runs once per physics frame (approximately 59.7 Hz) and tests
**16 points** distributed around the ball's circumference against the collision masks.

### Test Point Offsets

The 16 test points are arranged clockwise around the ball center, starting from the
rightmost point (3 o'clock):

```
Point  0: ( +4,   0)   -- Right
Point  1: ( +4,  +1)   -- Right-low
Point  2: ( +3,  +3)   -- Lower-right
Point  3: ( +1,  +4)   -- Low-right
Point  4: (  0,  +4)   -- Bottom
Point  5: ( -1,  +4)   -- Low-left
Point  6: ( -3,  +3)   -- Lower-left
Point  7: ( -4,  +1)   -- Left-low
Point  8: ( -4,   0)   -- Left
Point  9: ( -4,  -1)   -- Left-high
Point 10: ( -3,  -3)   -- Upper-left
Point 11: ( -1,  -4)   -- High-left
Point 12: (  0,  -4)   -- Top
Point 13: ( +1,  -4)   -- High-right
Point 14: ( +3,  -3)   -- Upper-right
Point 15: ( +4,  -1)   -- Right-high
```

### Detection Steps

1. **Calculate tile offset.** The ball's pixel position (minus the 4-pixel radius)
   is divided by 8 to get the tile coordinate, which indexes into the collision map.

2. **Read 2x2 tile block.** Four mask indices are read from the collision map at
   the ball's tile position: upper-left, upper-right, lower-left, lower-right.

3. **Sub-tile position.** The ball's position within the tile (0-7 in both X and Y)
   selects a pre-computed collision test table. There are 8 sub-tables (one per X
   sub-position), each containing 16 entries of 3 bytes:
   - `y_offset`: row offset within the mask (high bit selects right-column tile)
   - `bitmask`: the single bit to test in the mask byte
   - `store_index`: which of the 16 test point results to write

4. **Test each point.** For each of the 16 entries, the engine:
   - Determines which of the 4 tiles the test point falls in
   - Looks up the mask byte for the appropriate row of that tile's mask
   - Tests the specific bit against the mask
   - Records whether the point is colliding (nonzero) or passable (zero)

5. **Find longest consecutive run.** The 16 test point results are scanned (with
   wrap-around from point 15 back to point 0) to find the longest consecutive
   sequence of colliding points. This run defines the collision surface.

6. **Compute collision normal.** The start and end indices of the longest run are
   packed into a single byte: `(start << 4) | end`. This packed byte indexes into
   the `CollisionForceAngles[256]` lookup table to produce a collision normal angle
   (0-255, where 0 = right, 64 = down, 128 = left, 192 = up).

7. **Apply position correction.** The same packed byte indexes into
   `CollisionYDeltas[256]` and `CollisionXDeltas[256]` tables to apply a position
   correction that pushes the ball out of the colliding surface.

### Special Cases

- If the ball position is less than 4 pixels from the top or left edge, collision
  detection is skipped entirely to prevent unsigned arithmetic underflow.
- If all 16 points are colliding (ball fully embedded in geometry), no collision is
  reported (the ball is treated as being inside a wall and must escape naturally).

---

## 5. Collision Response

After a collision is detected, the velocity is transformed through a 4-step process:

### Step 1: Rotate into Collision Frame

The ball's velocity vector `(vx, vy)` is rotated into a coordinate system aligned
with the collision normal using sin/cos lookup tables (256 entries each):

```
rotated_x =  vx * cos(angle) + vy * sin(angle)
rotated_y = -vx * sin(angle) + vy * cos(angle)
```

The angle is the collision normal from `CollisionForceAngles[]`.

### Step 2: Apply Damping

The rotated velocity is divided by 4 (arithmetic right shift by 2):

```
rotated_x >>= 2
rotated_y >>= 2
```

### Step 3: Bounce and Spin Transfer

The X component (perpendicular to the collision surface) is negated to create the
bounce effect:

```
rotated_x = -rotated_x
```

Half of the ball's spin value is transferred to the X velocity:

```
rotated_x += ball_spin / 2
```

### Step 4: Rotate Back to World Frame

The modified velocity is rotated back to world coordinates using the negated angle:

```
vx =  rotated_x * cos(-angle) + rotated_y * sin(-angle)
vy = -rotated_x * sin(-angle) + rotated_y * cos(-angle)
```

### Force Amplification

The `collision_force_amplification` field can multiply the resulting velocity for
special collision objects (flippers, bumpers). This is set by the object collision
system before the collision response is applied.

---

## 6. Flipper Collision Masks

Bottom-half stages (those with odd stage IDs that have flippers) use dynamic collision
masks for the flipper regions.

### Mask Index Mapping

- **0xE0-0xEF**: Left flipper masks (index within set = `attribute - 0xE0`)
- **0xF0-0xFF**: Right flipper masks (index within set = `attribute - 0xF0`)

### Flipper Position Sets

Each flipper has **3 sets of 16 masks** corresponding to flipper angular positions:

| Flipper State Range | Set Offset | Description           |
|---------------------|------------|-----------------------|
| 0-6                 | 0x00       | Flipper at rest (low) |
| 7-13                | 0x80       | Flipper mid-swing     |
| 14-15               | 0x100      | Flipper fully raised  |

Each set is 0x80 bytes (16 masks x 8 bytes per mask).

### Mask Files

- **Main field flippers:**
  - `data/collision/masks/bottom_left_masks.png` (64x48 px, 48 tiles = 3 sets of 16)
  - `data/collision/masks/bottom_right_masks.png` (64x48 px, 48 tiles = 3 sets of 16)

- **Bonus stage flippers:**
  - `data/collision/masks/bottom_left_bonus_stage_masks.png` (64x48 px)
  - `data/collision/masks/bottom_right_bonus_stage_masks.png` (64x48 px)

### Runtime Behavior

When the collision system encounters a mask index in the 0xE0-0xFF range on a bottom
stage, it:

1. Determines whether it is a left (0xE0-0xEF) or right (0xF0-0xFF) flipper mask
2. Reads the current flipper angular state (0-15)
3. Selects the appropriate set based on the flipper state range
4. Indexes into the set to find the correct 8-byte mask

---

## 7. Pokemon Collision Masks

During catch'em mode, a wild Pokemon appears on the playfield and the ball can
collide with it. Each of the 151 Pokemon species has a dedicated collision mask.

### File Location

```
data/collision/mon_masks/{species_name}_collision.png
```

### Format

- **Dimensions:** 32x32 pixels (4x4 tiles = 16 tiles per species)
- **Mode:** Grayscale (mode `L`), dark = solid, light = passable
- **In memory:** Loaded as 1bpp data and stored in `mon_animated_collision_mask[]`
  (up to 128 bytes for 16 tiles at 8 bytes each)

### Collision Map Integration

When a Pokemon is present on the field, the collision map is patched with indices in
the range **0xD0-0xDF** at the tiles where the Pokemon sprite appears. The engine
intercepts these indices and redirects to `mon_animated_collision_mask[]` instead of
the static mask array:

```
mask_offset = (attribute - 0xD0) * 8 + row_within_tile
```

---

## 8. Stage Files Reference

### Collision Map Files (`data/collision/maps/`)

All files are 1024 bytes. Only the first 768 bytes are loaded.

#### Red Field
| File                              | Stage              | Collision State |
|-----------------------------------|--------------------|-----------------|
| `red_stage_top_0.collision`       | Red Field Top      | 0               |
| `red_stage_top_1.collision`       | Red Field Top      | 1               |
| `red_stage_top_2.collision`       | Red Field Top      | 2               |
| `red_stage_top_3.collision`       | Red Field Top      | 3               |
| `red_stage_top_4.collision`       | Red Field Top      | 4               |
| `red_stage_top_5.collision`       | Red Field Top      | 5               |
| `red_stage_top_6.collision`       | Red Field Top      | 6               |
| `red_stage_top_7.collision`       | Red Field Top      | 7               |
| `red_stage_bottom.collision`      | Red Field Bottom   | (all)           |

#### Blue Field
| File                                    | Stage              | Collision State |
|-----------------------------------------|--------------------|-----------------|
| `blue_stage_top_ball_entrance.collision` | Blue Field Top     | 0 (entrance)    |
| `blue_stage_top.collision`              | Blue Field Top     | 1+ (normal)     |
| `blue_stage_bottom.collision`           | Blue Field Bottom  | (all)           |

#### Bonus Stages
| File                                    | Stage         | Collision State      |
|-----------------------------------------|---------------|----------------------|
| `gengar_bonus_ball_entrance.collision`  | Gengar Bonus  | 0 (ball entrance)    |
| `gengar_bonus.collision`                | Gengar Bonus  | 1+ (normal play)     |
| `mewtwo_bonus_ball_entrance.collision`  | Mewtwo Bonus  | 0 (ball entrance)    |
| `mewtwo_bonus.collision`                | Mewtwo Bonus  | 1+ (normal play)     |
| `meowth_bonus_ball_entrance.collision`  | Meowth Bonus  | 0 (ball entrance)    |
| `meowth_bonus.collision`                | Meowth Bonus  | 1+ (normal play)     |
| `diglett_bonus_ball_entrance.collision` | Diglett Bonus | 0 (ball entrance)    |
| `diglett_bonus.collision`               | Diglett Bonus | 1+ (normal play)     |
| `seel_bonus_ball_entrance.collision`    | Seel Bonus    | 0 (ball entrance)    |
| `seel_bonus.collision`                  | Seel Bonus    | 1+ (normal play)     |

### Collision Mask Files (`data/collision/masks/`)

| File                                     | Stage               | Masks |
|------------------------------------------|----------------------|-------|
| `red_stage_top_0.png`                    | Red Field Top (states 0-1) | 256 |
| `red_stage_top_1.png`                    | Red Field Top (states 2-3) | 256 |
| `red_stage_top_2.png`                    | Red Field Top (states 4-5) | 256 |
| `red_stage_top_3.png`                    | Red Field Top (states 6-7) | 256 |
| `red_stage_bottom.png`                   | Red Field Bottom     | 128   |
| `blue_stage_top.png`                     | Blue Field Top       | 256   |
| `blue_stage_bottom.png`                  | Blue Field Bottom    | 256   |
| `gengar_bonus.png`                       | Gengar Bonus         | 48    |
| `mewtwo_bonus.png`                       | Mewtwo Bonus         | 32    |
| `meowth_bonus.png`                       | Meowth Bonus         | 32    |
| `diglett_bonus.png`                      | Diglett Bonus        | 96    |
| `seel_bonus.png`                         | Seel Bonus           | 32    |
| `bottom_left_masks.png`                  | Main field flippers  | 48    |
| `bottom_right_masks.png`                 | Main field flippers  | 48    |
| `bottom_left_bonus_stage_masks.png`      | Bonus stage flippers | 48    |
| `bottom_right_bonus_stage_masks.png`     | Bonus stage flippers | 48    |

### Pokemon Collision Mask Files (`data/collision/mon_masks/`)

79 species-specific files, each 32x32 pixels:
`abra_collision.png`, `aerodactyl_collision.png`, `articuno_collision.png`, ...,
`zapdos_collision.png`, `zubat_collision.png`

---

## 9. Creating a Custom Collision Map

### Step-by-Step Guide

#### 1. Start with an Existing File

Copy an existing `.collision` file as your starting point:

```
cp data/collision/maps/red_stage_bottom.collision data/collision/maps/my_custom.collision
```

#### 2. Understand the Grid Layout

The file is 1024 bytes representing a 32x32 grid. The engine uses a 24-row x 32-column
window (768 bytes). Only columns 0-17 correspond to visible playfield tiles.

```
Byte offset = (row * 32) + column

Row 0, Col 0  = offset 0x000   (top-left tile of playfield)
Row 0, Col 17 = offset 0x011   (top-right tile of playfield)
Row 23, Col 0 = offset 0x2E0   (bottom-left tile of playfield)
```

#### 3. Visualize the Current Map

Use the collision map tool to generate a visual representation:

```
python tools/collision_map_tool.py --visualize data/collision/maps/my_custom.collision -o my_custom_viz.png
```

#### 4. Edit the Collision Map

Edit the file using a hex editor or create it programmatically. Each byte is a mask
index (0x00-0xFF) referencing the corresponding collision mask bitmap.

**Tips:**
- Set bytes to `0x00` for open/passable areas
- Use mask indices that correspond to the shapes you need (walls, curves, ramps)
- Keep columns 18-31 as `0x00` (they are not visible but are in the memory buffer)
- Rows 24-31 (offsets 0x300-0x3FF) are not loaded and can be left as zero

#### 5. Validate the Map

Check that the file is correctly formatted and all referenced mask indices exist:

```
python tools/collision_map_tool.py --validate data/collision/maps/my_custom.collision
```

#### 6. Ensure Mask Coverage

Every non-zero mask index referenced in the collision map must have a corresponding
tile in the collision mask PNG. If your collision map uses index 0x3F, the mask PNG
must contain at least 64 tiles (indices 0x00-0x3F).

#### 7. Create or Modify Mask PNGs

Collision mask PNGs are 1-bit or grayscale images where dark pixels are solid. Each
8x8 tile in the image is one mask, indexed left-to-right, top-to-bottom.

To add new masks:
- Open the mask PNG in an image editor
- Ensure dimensions are multiples of 8
- Draw solid (black) pixels where the ball should collide
- Draw passable (white) pixels where the ball should pass through
- Save as PNG

You can also extract and inspect individual masks:

```
python tools/collision_map_tool.py --export-masks data/collision/masks/red_stage_bottom.png -o mask_tiles/
```

#### 8. Test In-Game

Replace the appropriate collision files and run the game. The collision system loads
files at stage initialization time, so you need to enter the stage to see changes.

### Common Patterns

- **Solid wall:** All 8 bytes = `0xFF` (every pixel is solid)
- **Empty tile:** All 8 bytes = `0x00` (every pixel is passable)
- **Left slope:** Progressively wider bit patterns per row (e.g., `0x80, 0xC0, 0xE0, 0xF0, ...`)
- **Circle:** Approximate a circle shape within the 8x8 grid for bumpers
- **Flipper region:** Use indices 0xE0-0xFF and ensure the flipper mask PNGs are present

### Validation Checklist

- [ ] File is exactly 1024 bytes
- [ ] Columns 18-31 are zero (or at least not relied upon)
- [ ] All non-zero mask indices have corresponding mask tiles in the PNG
- [ ] Mask indices 0xD0-0xDF are only used on bottom stages with Pokemon encounters
- [ ] Mask indices 0xE0-0xFF are only used on bottom stages with flippers
- [ ] The mask PNG dimensions are multiples of 8
- [ ] Solid areas form continuous surfaces (gaps cause the ball to clip through)
- [ ] Entry/exit paths are not blocked (the ball must be able to reach the playfield)
