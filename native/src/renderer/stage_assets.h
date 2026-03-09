#ifndef STAGE_ASSETS_H
#define STAGE_ASSETS_H

#include <stdint.h>
#include "renderer/vram.h"

/* Forward declarations */
typedef struct GameState GameState;

typedef enum {
    ASSET_TILES,     /* PNG -> 2bpp tile data -> VRAM tile region */
    ASSET_TILEMAP,   /* Binary .map file -> VRAM tilemap region */
    ASSET_PALETTES   /* Hardcoded palette data -> GameState palettes */
} AssetType;

typedef struct {
    AssetType type;
    const char *filename;   /* Relative path from project root (e.g. "gfx/stage/...") */
    uint16_t vram_dest;     /* GBC address ($8000-$9FFF) */
    uint8_t vram_bank;      /* 0 or 1 */
    uint16_t size;          /* Max bytes to copy (0 = use full file) */
} StageAssetEntry;

/*
 * Load all assets for a stage into VRAM and palettes.
 * base_path: path to the project root directory (where gfx/ lives).
 * Returns true on success.
 */
bool load_stage_assets(uint8_t stage_id, VirtualVRAM *vram,
                       GameState *state, const char *base_path);

/*
 * Load all assets for a non-pinball screen into VRAM and palettes.
 * screen_id: SCREEN_COPYRIGHT, SCREEN_TITLESCREEN, etc.
 * Returns true on success.
 */
bool load_screen_assets(uint8_t screen_id, VirtualVRAM *vram,
                        GameState *state, const char *base_path);

/*
 * Interleave tile data for 8x16 sprite mode.
 * Row-major → column-interleaved pairs.
 * tiles_per_row: PNG width / 8 (e.g. 4 for .w32.).
 */
void interleave_tiles(uint8_t *data, size_t size, int tiles_per_row);

#endif /* STAGE_ASSETS_H */
