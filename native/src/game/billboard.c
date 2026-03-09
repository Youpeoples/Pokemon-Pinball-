/*
 * Billboard System
 *
 * Translated from engine/pinball_game/billboard.asm and billboard_tiledata.asm.
 * Loads billboard picture tile data to VRAM $8900 (24 tiles = $180 bytes).
 * Manages palette attributes for the 6x4 billboard area.
 *
 * Billboard picture IDs (LoadBillboardPicture, 0xf178):
 *   0-12:  Slot rewards (ball saver, extra ball, points, upgrades, modes)
 *   13-17: Bonus stage destinations
 *   18-26: Small point values (100-900)
 *   27-35: Big point values (1M-9M)
 *   36-40: Bonus multiplier X1-X5
 *   41-58: Map locations (Pallet Town through Indigo Plateau)
 *
 * Billboard tile data indices (LoadBillboardTileData, 0x30256):
 *   0-17:  Map locations
 *   18-19: HurryUp messages (map move mode)
 *   20:    GoToNext (map move, slot open)
 *   21-25: Bonus stages
 *   26:    Slot (CAVE lights)
 */

#include "game/billboard.h"
#include "renderer/tile_loader.h"
#include "renderer/vram.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*=============================================================================
 * Billboard tilemap positions in bg_map
 *
 * The billboard occupies rows 4-7, columns 7-12 in the 32-wide tilemap.
 * vBGMap offsets: $87, $A7, $C7, $E7 (start of each row).
 * Tile IDs: $90-$A7 under signed addressing (LCDC bit 4=0).
 *===========================================================================*/
static const uint16_t billboard_row_offsets[4] = { 0x87, 0xA7, 0xC7, 0xE7 };

/*=============================================================================
 * PNG path tables
 *
 * Billboard picture IDs (0-58) map to PNG files for on/off variants.
 * IDs 0-40: gfx/billboard/{subdir}/{name}_{on|off}.png
 * IDs 41-58: gfx/billboard/maps/{name}.png (no on/off suffix)
 *===========================================================================*/

/* Base names for billboard picture IDs 0-58.
 * For IDs 0-40: append "_on.png" or "_off.png".
 * For IDs 41-58: append ".png" (maps have no on/off variant). */
static const char *billboard_pic_paths[NUM_BILLBOARD_PICS] = {
    /* 0-12: Slot rewards */
    "gfx/billboard/slot/30secondballsaver",     /*  0 */
    "gfx/billboard/slot/60secondballsaver",     /*  1 */
    "gfx/billboard/slot/90secondballsaver",     /*  2 */
    "gfx/billboard/slot/pikachusaver",           /*  3 */
    "gfx/billboard/slot/extraball",              /*  4 */
    "gfx/billboard/slot/small",                  /*  5 */
    "gfx/billboard/slot/big",                    /*  6 */
    "gfx/billboard/slot/catchem",                /*  7 */
    "gfx/billboard/slot/evolution",              /*  8 */
    "gfx/billboard/slot/greatball",              /*  9 */
    "gfx/billboard/slot/ultraball",              /* 10 */
    "gfx/billboard/slot/masterball",             /* 11 */
    "gfx/billboard/slot/bonusmultiplier",        /* 12 */
    /* 13-17: Bonus stages */
    "gfx/billboard/bonus_stages/gotogengarbonus",  /* 13 */
    "gfx/billboard/bonus_stages/gotomewtwobonus",  /* 14 */
    "gfx/billboard/bonus_stages/gotomeowthbonus",  /* 15 */
    "gfx/billboard/bonus_stages/gotodiglettbonus", /* 16 */
    "gfx/billboard/bonus_stages/gotoseelbonus",    /* 17 */
    /* 18-26: Small point values */
    "gfx/billboard/slot/100points",              /* 18 */
    "gfx/billboard/slot/200points",              /* 19 */
    "gfx/billboard/slot/300points",              /* 20 */
    "gfx/billboard/slot/400points",              /* 21 */
    "gfx/billboard/slot/500points",              /* 22 */
    "gfx/billboard/slot/600points",              /* 23 */
    "gfx/billboard/slot/700points",              /* 24 */
    "gfx/billboard/slot/800points",              /* 25 */
    "gfx/billboard/slot/900points",              /* 26 */
    /* 27-35: Big point values */
    "gfx/billboard/slot/1000000points",          /* 27 */
    "gfx/billboard/slot/2000000points",          /* 28 */
    "gfx/billboard/slot/3000000points",          /* 29 */
    "gfx/billboard/slot/4000000points",          /* 30 */
    "gfx/billboard/slot/5000000points",          /* 31 */
    "gfx/billboard/slot/6000000points",          /* 32 */
    "gfx/billboard/slot/7000000points",          /* 33 */
    "gfx/billboard/slot/8000000points",          /* 34 */
    "gfx/billboard/slot/9000000points",          /* 35 */
    /* 36-40: Bonus multiplier values */
    "gfx/billboard/slot/bonusmultiplierX1",      /* 36 */
    "gfx/billboard/slot/bonusmultiplierX2",      /* 37 */
    "gfx/billboard/slot/bonusmultiplierX3",      /* 38 */
    "gfx/billboard/slot/bonusmultiplierX4",      /* 39 */
    "gfx/billboard/slot/bonusmultiplierX5",      /* 40 */
    /* 41-58: Map locations */
    "gfx/billboard/maps/pallettown",             /* 41 */
    "gfx/billboard/maps/viridiancity",           /* 42 */
    "gfx/billboard/maps/viridianforest",         /* 43 */
    "gfx/billboard/maps/pewtercity",             /* 44 */
    "gfx/billboard/maps/mtmoon",                 /* 45 */
    "gfx/billboard/maps/ceruleancity",           /* 46 */
    "gfx/billboard/maps/vermilioncityseaside",   /* 47 */
    "gfx/billboard/maps/vermilioncitystreets",   /* 48 */
    "gfx/billboard/maps/rockmountain",           /* 49 */
    "gfx/billboard/maps/lavendertown",           /* 50 */
    "gfx/billboard/maps/celadoncity",            /* 51 */
    "gfx/billboard/maps/cyclingroad",            /* 52 */
    "gfx/billboard/maps/fuchsiacity",            /* 53 */
    "gfx/billboard/maps/safarizone",             /* 54 */
    "gfx/billboard/maps/saffroncity",            /* 55 */
    "gfx/billboard/maps/seafoamislands",         /* 56 */
    "gfx/billboard/maps/cinnabarisland",         /* 57 */
    "gfx/billboard/maps/indigoplateau",          /* 58 */
};

/* Billboard tile data index paths (0-26).
 * Indices 0-17 = maps (.png), 18-26 = special messages (_on.png). */
static const char *tile_data_paths[NUM_BILLBOARD_TILE_DATA] = {
    "gfx/billboard/maps/pallettown",             /*  0: Pallet Town */
    "gfx/billboard/maps/viridiancity",           /*  1: Viridian City */
    "gfx/billboard/maps/viridianforest",         /*  2: Viridian Forest */
    "gfx/billboard/maps/pewtercity",             /*  3: Pewter City */
    "gfx/billboard/maps/mtmoon",                 /*  4: Mt. Moon */
    "gfx/billboard/maps/ceruleancity",           /*  5: Cerulean City */
    "gfx/billboard/maps/vermilioncityseaside",   /*  6: Vermilion Seaside */
    "gfx/billboard/maps/vermilioncitystreets",   /*  7: Vermilion Streets */
    "gfx/billboard/maps/rockmountain",           /*  8: Rock Mountain */
    "gfx/billboard/maps/lavendertown",           /*  9: Lavender Town */
    "gfx/billboard/maps/celadoncity",            /* 10: Celadon City */
    "gfx/billboard/maps/cyclingroad",            /* 11: Cycling Road */
    "gfx/billboard/maps/fuchsiacity",            /* 12: Fuchsia City */
    "gfx/billboard/maps/safarizone",             /* 13: Safari Zone */
    "gfx/billboard/maps/saffroncity",            /* 14: Saffron City */
    "gfx/billboard/maps/seafoamislands",         /* 15: Seafoam Islands */
    "gfx/billboard/maps/cinnabarisland",         /* 16: Cinnabar Island */
    "gfx/billboard/maps/indigoplateau",          /* 17: Indigo Plateau */
    "gfx/billboard/hurryup2",                    /* 18: HurryUp2 (map move dir 0) */
    "gfx/billboard/hurryup",                     /* 19: HurryUp (map move dir 1) */
    "gfx/billboard/gotonext",                    /* 20: GoToNext (map move slot) */
    "gfx/billboard/bonus_stages/gotogengarbonus",  /* 21: Gengar Bonus */
    "gfx/billboard/bonus_stages/gotomewtwobonus",  /* 22: Mewtwo Bonus */
    "gfx/billboard/bonus_stages/gotomeowthbonus",  /* 23: Meowth Bonus */
    "gfx/billboard/bonus_stages/gotodiglettbonus", /* 24: Diglett Bonus */
    "gfx/billboard/bonus_stages/gotoseelbonus",    /* 25: Seel Bonus */
    "gfx/billboard/slot/slot",                   /* 26: Slot (CAVE) */
};

/*=============================================================================
 * Tile data cache
 *
 * Lazy-loads PNG tile data on first access, caches for subsequent calls.
 * billboard_pic_cache: 59 entries for on/off picture variants.
 * tile_data_cache: 27 entries for LoadBillboardTileData indices.
 *===========================================================================*/
static uint8_t *pic_cache_on[NUM_BILLBOARD_PICS];
static uint8_t *pic_cache_off[NUM_BILLBOARD_PICS];
static uint8_t *td_cache[NUM_BILLBOARD_TILE_DATA];

/* Load tile data from a PNG file. Returns cached or newly loaded data.
 * Caller must NOT free the returned pointer (it's cached). */
static uint8_t *load_cached_png(const char *asset_base, const char *rel_path,
                                uint8_t **cache_slot) {
    if (*cache_slot)
        return *cache_slot;

    char full_path[300];
    snprintf(full_path, sizeof(full_path), "%s/%s", asset_base, rel_path);
    for (char *p = full_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(full_path, &data_size);
    if (!tile_data || data_size < BILLBOARD_TILE_SIZE) {
        free(tile_data);
        return NULL;
    }

    *cache_slot = (uint8_t *)malloc(BILLBOARD_TILE_SIZE);
    if (!*cache_slot) {
        free(tile_data);
        return NULL;
    }
    memcpy(*cache_slot, tile_data, BILLBOARD_TILE_SIZE);
    free(tile_data);
    return *cache_slot;
}

/* Get billboard picture tile data for a given ID and on/off state. */
static uint8_t *get_pic_data(GameState *state, uint8_t pic_id, int is_off) {
    if (pic_id >= NUM_BILLBOARD_PICS)
        return NULL;

    const char *base = billboard_pic_paths[pic_id];
    if (!base) return NULL;

    /* Build full relative path with suffix.
     * IDs 0-40: "{base}_{on|off}.png"
     * IDs 41-58 (maps): "{base}.png" (no on/off) */
    char rel_path[200];
    if (pic_id >= BILLBOARD_PALLET_TOWN_PIC) {
        /* Map: single version, no on/off suffix */
        snprintf(rel_path, sizeof(rel_path), "%s.png", base);
        return load_cached_png(state->asset_base_path, rel_path,
                               &pic_cache_on[pic_id]);
    }

    uint8_t **cache = is_off ? &pic_cache_off[pic_id] : &pic_cache_on[pic_id];
    snprintf(rel_path, sizeof(rel_path), "%s_%s.png", base,
             is_off ? "off" : "on");
    return load_cached_png(state->asset_base_path, rel_path, cache);
}

/* Get tile data for a LoadBillboardTileData index. */
static uint8_t *get_td_data(GameState *state, uint8_t index) {
    if (index >= NUM_BILLBOARD_TILE_DATA)
        return NULL;

    const char *base = tile_data_paths[index];
    if (!base) return NULL;

    /* Build path: maps (0-17) use ".png", others use "_on.png" */
    char rel_path[200];
    if (index <= 17) {
        snprintf(rel_path, sizeof(rel_path), "%s.png", base);
    } else {
        snprintf(rel_path, sizeof(rel_path), "%s_on.png", base);
    }
    return load_cached_png(state->asset_base_path, rel_path, &td_cache[index]);
}

/*=============================================================================
 * Billboard palette attribute helpers
 *===========================================================================*/

/* Write a 24-byte palette attribute map to the billboard area in bg_map[1].
 * Each byte is the palette index (0-7) for that tile position. */
static void write_palette_map(VirtualVRAM *vram, const uint8_t *pal_map) {
    uint8_t *attr = vram->bg_map[1];
    for (int row = 0; row < 4; row++) {
        uint16_t off = billboard_row_offsets[row];
        for (int col = 0; col < 6; col++) {
            attr[off + col] = pal_map[row * 6 + col];
        }
    }
}

/* Grey palette map: all 24 tiles use palette 6. */
static const uint8_t grey_palette_map[24] = {
    6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6,
};

/* Per-map palette attribute maps (from ASM BillboardBGPaletteMap data).
 * Each 24-byte array specifies palette 6 or 7 per tile (6×4 grid).
 * Indices 18-26 (special/bonus) all use grey_palette_map (all 6). */
static const uint8_t map_palette_maps[18][24] = {
    /* 0: Pallet Town */
    { 6,7,7,7,7,7, 6,6,6,6,6,7, 6,6,6,6,6,6, 6,6,7,7,7,7 },
    /* 1: Viridian City */
    { 6,6,6,6,7,6, 6,6,6,6,7,6, 7,7,7,7,7,6, 7,7,7,7,7,6 },
    /* 2: Viridian Forest */
    { 6,7,6,6,7,6, 7,7,7,7,7,6, 6,7,6,6,7,6, 6,7,7,7,7,6 },
    /* 3: Pewter City */
    { 7,7,7,7,7,7, 6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6 },
    /* 4: Mt. Moon */
    { 6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6 },
    /* 5: Cerulean City */
    { 7,7,7,7,7,7, 7,7,7,7,7,7, 6,6,6,6,6,6, 6,6,6,6,6,6 },
    /* 6: Vermilion Seaside */
    { 6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6, 7,7,7,6,6,6 },
    /* 7: Vermilion Streets */
    { 6,6,6,6,6,7, 6,6,7,7,7,7, 6,7,7,7,7,7, 7,7,7,7,7,7 },
    /* 8: Rock Mountain */
    { 7,7,7,7,7,7, 6,6,7,7,7,6, 6,6,6,6,6,6, 6,6,6,6,6,6 },
    /* 9: Lavender Town */
    { 6,7,7,6,6,6, 6,7,7,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6 },
    /* 10: Celadon City */
    { 6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,7,7,7,7, 6,6,7,7,7,7 },
    /* 11: Cycling Road */
    { 7,7,7,7,6,6, 7,6,6,6,6,7, 6,6,6,6,6,7, 6,6,6,6,7,7 },
    /* 12: Fuchsia City */
    { 7,6,6,6,6,6, 7,6,6,6,6,6, 7,7,7,6,6,7, 7,7,7,7,7,7 },
    /* 13: Safari Zone */
    { 6,6,6,6,6,6, 7,7,7,7,7,7, 7,7,7,7,7,7, 7,7,7,7,7,7 },
    /* 14: Saffron City */
    { 6,6,6,6,6,6, 6,6,6,6,6,6, 7,7,7,6,7,7, 7,7,7,7,7,7 },
    /* 15: Seafoam Islands */
    { 7,7,7,7,7,7, 7,7,6,7,7,7, 7,7,7,6,7,7, 7,7,7,7,7,7 },
    /* 16: Cinnabar Island */
    { 6,6,6,6,6,6, 6,6,7,7,6,6, 6,6,7,7,7,6, 6,6,6,6,6,6 },
    /* 17: Indigo Plateau */
    { 6,6,6,6,6,6, 6,6,6,6,6,6, 6,6,6,6,6,6, 7,7,7,7,7,7 },
};

/*=============================================================================
 * Per-index billboard palette COLORS (from ASM map_palettes.asm etc.)
 *
 * Each entry: [2] palettes (pal 6 and pal 7), [4] RGB555 colors each.
 * For indices 18-25 (bonus/special) only palette 6 is meaningful;
 * palette 7 is set identical to palette 6.
 * Index 26 (Slot) has both palettes identical.
 *===========================================================================*/
#define RGB555(r,g,b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

static const uint16_t billboard_palette_colors[NUM_BILLBOARD_TILE_DATA][2][4] = {
    /* 0: Pallet Town */
    {{ RGB555(31,31,31), RGB555(22,18,17), RGB555(0,19,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(24,9,3),   RGB555(0,4,25),  RGB555(0,0,0) }},
    /* 1: Viridian City */
    {{ RGB555(31,31,31), RGB555(0,14,31),  RGB555(0,22,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(26,15,3),  RGB555(0,22,0),  RGB555(0,0,0) }},
    /* 2: Viridian Forest */
    {{ RGB555(31,31,31), RGB555(31,20,3),  RGB555(2,16,1),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(31,20,3),  RGB555(24,6,0),  RGB555(0,0,0) }},
    /* 3: Pewter City */
    {{ RGB555(31,31,31), RGB555(27,20,10), RGB555(2,16,1),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(5,17,31),  RGB555(26,3,1),  RGB555(0,0,0) }},
    /* 4: Mt. Moon */
    {{ RGB555(31,28,2),  RGB555(19,20,27), RGB555(2,7,20),  RGB555(0,0,0) },
     { RGB555(31,28,2),  RGB555(19,20,27), RGB555(2,7,20),  RGB555(0,0,0) }},
    /* 5: Cerulean City */
    {{ RGB555(31,22,5),  RGB555(16,22,4),  RGB555(1,15,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(16,22,31), RGB555(3,11,31), RGB555(0,0,0) }},
    /* 6: Vermilion Seaside */
    {{ RGB555(31,31,31), RGB555(8,20,31),  RGB555(2,8,23),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(22,22,22), RGB555(21,8,0),  RGB555(0,0,0) }},
    /* 7: Vermilion Streets */
    {{ RGB555(31,31,31), RGB555(20,22,25), RGB555(31,8,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(20,22,25), RGB555(7,8,13),  RGB555(0,0,0) }},
    /* 8: Rock Mountain */
    {{ RGB555(31,31,31), RGB555(27,13,4),  RGB555(21,5,0),  RGB555(0,0,0) },
     { RGB555(3,18,31),  RGB555(27,13,4),  RGB555(2,16,1),  RGB555(0,0,0) }},
    /* 9: Lavender Town */
    {{ RGB555(31,31,10), RGB555(11,18,31), RGB555(2,6,19),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(11,18,31), RGB555(2,6,19),  RGB555(0,0,0) }},
    /* 10: Celadon City */
    {{ RGB555(31,31,31), RGB555(11,19,31), RGB555(29,8,4),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(31,9,9),   RGB555(16,2,2),  RGB555(0,0,0) }},
    /* 11: Cycling Road */
    {{ RGB555(31,24,15), RGB555(11,21,5),  RGB555(31,9,5),  RGB555(0,0,0) },
     { RGB555(31,22,13), RGB555(11,21,5),  RGB555(0,15,0),  RGB555(0,0,0) }},
    /* 12: Fuchsia City */
    {{ RGB555(31,31,31), RGB555(10,25,31), RGB555(26,3,1),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(27,23,6),  RGB555(28,6,3),  RGB555(0,0,0) }},
    /* 13: Safari Zone */
    {{ RGB555(31,31,31), RGB555(13,27,31), RGB555(4,19,27), RGB555(0,0,0) },
     { RGB555(29,21,17), RGB555(13,19,5),  RGB555(0,14,0),  RGB555(0,0,0) }},
    /* 14: Saffron City */
    {{ RGB555(31,31,31), RGB555(8,19,31),  RGB555(2,7,26),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(27,28,1),  RGB555(24,7,5),  RGB555(0,0,0) }},
    /* 15: Seafoam Islands */
    {{ RGB555(24,27,30), RGB555(31,24,1),  RGB555(2,15,1),  RGB555(0,0,0) },
     { RGB555(24,27,30), RGB555(0,14,31),  RGB555(0,9,23),  RGB555(0,0,0) }},
    /* 16: Cinnabar Island */
    {{ RGB555(31,31,31), RGB555(14,21,0),  RGB555(0,10,31), RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(14,21,0),  RGB555(2,11,1),  RGB555(0,0,0) }},
    /* 17: Indigo Plateau */
    {{ RGB555(31,31,31), RGB555(11,18,31), RGB555(7,9,19),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(11,18,31), RGB555(9,20,0),  RGB555(0,0,0) }},
    /* 18: HurryUp2 (pal 6 only) */
    {{ RGB555(31,31,31), RGB555(0,11,31),  RGB555(29,0,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(0,11,31),  RGB555(29,0,0),  RGB555(0,0,0) }},
    /* 19: HurryUp (same palette as HurryUp2) */
    {{ RGB555(31,31,31), RGB555(0,11,31),  RGB555(29,0,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(0,11,31),  RGB555(29,0,0),  RGB555(0,0,0) }},
    /* 20: GoToNext */
    {{ RGB555(31,31,31), RGB555(31,16,0),  RGB555(29,0,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(31,16,0),  RGB555(29,0,0),  RGB555(0,0,0) }},
    /* 21: Gengar Bonus */
    {{ RGB555(31,31,31), RGB555(15,15,19), RGB555(31,0,31), RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(15,15,19), RGB555(31,0,31), RGB555(0,0,0) }},
    /* 22: Mewtwo Bonus */
    {{ RGB555(31,31,31), RGB555(31,25,31), RGB555(31,0,31), RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(31,25,31), RGB555(31,0,31), RGB555(0,0,0) }},
    /* 23: Meowth Bonus */
    {{ RGB555(31,31,31), RGB555(31,31,0),  RGB555(27,11,2), RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(31,31,0),  RGB555(27,11,2), RGB555(0,0,0) }},
    /* 24: Diglett Bonus */
    {{ RGB555(31,31,31), RGB555(31,18,8),  RGB555(27,0,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(31,18,8),  RGB555(27,0,0),  RGB555(0,0,0) }},
    /* 25: Seel Bonus */
    {{ RGB555(31,31,31), RGB555(20,20,26), RGB555(31,11,10),RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(20,20,26), RGB555(31,11,10),RGB555(0,0,0) }},
    /* 26: Slot (CAVE) */
    {{ RGB555(31,31,31), RGB555(31,26,2),  RGB555(31,3,0),  RGB555(0,0,0) },
     { RGB555(31,31,31), RGB555(31,26,2),  RGB555(31,3,0),  RGB555(0,0,0) }},
};

#undef RGB555

/*=============================================================================
 * Public API
 *===========================================================================*/

void load_billboard_picture(GameState *state, uint8_t pic_id) {
    if (!state->vram) return;
    uint8_t *data = get_pic_data(state, pic_id, 0);
    if (data)
        vram_write(state->vram, 0, BILLBOARD_VRAM_DEST, data, BILLBOARD_TILE_SIZE);
}

void load_billboard_off_picture(GameState *state, uint8_t pic_id) {
    if (!state->vram) return;
    uint8_t *data = get_pic_data(state, pic_id, 1);
    if (!data) /* Maps have no off version: fall back to on */
        data = get_pic_data(state, pic_id, 0);
    if (data)
        vram_write(state->vram, 0, BILLBOARD_VRAM_DEST, data, BILLBOARD_TILE_SIZE);
}

void load_billboard_tile_data(GameState *state, uint8_t index) {
    if (!state->vram) return;
    uint8_t *data = get_td_data(state, index);
    if (data)
        vram_write(state->vram, 0, BILLBOARD_VRAM_DEST, data, BILLBOARD_TILE_SIZE);

    if (index >= NUM_BILLBOARD_TILE_DATA) return;

    /* Set palette attributes: maps 0-17 use per-map palette maps,
     * other indices (bonus stages, slot, hurryup) use all-palette-6. */
    const uint8_t *pal_map = (index < 18) ? map_palette_maps[index] : grey_palette_map;
    write_palette_map(state->vram, pal_map);

    /* Load per-index palette COLORS into BG palettes 6 and 7.
     * ASM: LoadBillboardTileData calls QueueGraphicsToLoad with palette data
     * that writes 8 colors (pal 6 + pal 7) starting at hardware offset $30. */
    for (int c = 0; c < 4; c++) {
        state->bg_palettes[6].colors[c] = billboard_palette_colors[index][0][c];
        state->bg_palettes[7].colors[c] = billboard_palette_colors[index][1][c];
    }
}

void load_map_billboard_tile_data(GameState *state) {
    if (state->current_map < 18)
        load_billboard_tile_data(state, state->current_map);
}

/*---------------------------------------------------------------------------
 * LoadGreyBillboardPaletteData (0xf269)
 *
 * ASM: copies StageRedFieldBottomBGPalette5 to BG palette slot 6,
 * then writes all-$06 attribute map to the billboard area.
 * GBC grey palette: (31,31,31), (20,20,20), (8,8,8), (0,0,0)
 *---------------------------------------------------------------------------*/
void load_grey_billboard_palette(GameState *state) {
    if (!state->vram) return;

    /* Copy stage palette 5 to palette 6 (ASM copies from fixed ROM data,
     * but the palette is already loaded as bg_palettes[5] during stage init).
     * If palette 5 isn't loaded yet, use hardcoded greyscale. */
    static const uint16_t fallback_grey[4] = {
        0x7FFF,  /* RGB(31,31,31) white */
        0x5294,  /* RGB(20,20,20) light grey */
        0x2108,  /* RGB(8,8,8) dark grey */
        0x0000,  /* RGB(0,0,0) black */
    };

    /* Check if palette 5 looks valid (not all zeros) */
    int pal5_valid = 0;
    for (int c = 0; c < 4; c++) {
        if (state->bg_palettes[5].colors[c] != 0) {
            pal5_valid = 1;
            break;
        }
    }

    const uint16_t *src = pal5_valid ? state->bg_palettes[5].colors : fallback_grey;
    for (int c = 0; c < 4; c++)
        state->bg_palettes[6].colors[c] = src[c];

    /* Set all billboard tiles to palette 6 */
    write_palette_map(state->vram, grey_palette_map);
}

/*---------------------------------------------------------------------------
 * Write/clear billboard tilemap
 *---------------------------------------------------------------------------*/
void load_billboard_tilemap(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    for (int row = 0; row < 4; row++) {
        uint16_t off = billboard_row_offsets[row];
        for (int col = 0; col < 6; col++) {
            bg[off + col] = (uint8_t)(0x90 + row * 6 + col);
        }
    }
}

void clear_billboard_tilemap(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    for (int row = 0; row < 4; row++) {
        uint16_t off = billboard_row_offsets[row];
        for (int col = 0; col < 6; col++) {
            bg[off + col] = 0x80;
        }
    }
}

/*---------------------------------------------------------------------------
 * Func_f2a0: Load colored billboard palette for slot roulette result.
 *
 * ASM: PaletteDataPointerTable_f2be maps pic_id (0-58) to 16-byte palette
 * blocks. Func_7dc copies 16 bytes to BG palette register offset $30
 * (palettes 6 and 7). During roulette, all billboard tiles use palette 6
 * (set by load_grey_billboard_palette), so only palette 6 matters visually.
 *---------------------------------------------------------------------------*/
#define SPAL_RGB(r,g,b) ((uint16_t)((r) | ((g)<<5) | ((b)<<10)))

/* Unique palette 6 color sets referenced by PaletteDataPointerTable_f2be */
static const uint16_t slot_pal6_data[][4] = {
    /* 0: PaletteData_dcc00 — yellow/blue */
    { 0x7FFF, SPAL_RGB(31,28,0), SPAL_RGB(0,11,31), 0x0000 },
    /* 1: PaletteData_dcc08 — yellow/red */
    { 0x7FFF, SPAL_RGB(31,28,0), SPAL_RGB(29,0,0), 0x0000 },
    /* 2: PaletteData_dcc10 — red */
    { 0x7FFF, SPAL_RGB(31,0,0), SPAL_RGB(16,0,0), 0x0000 },
    /* 3: PaletteData_dcc18 — orange */
    { 0x7FFF, SPAL_RGB(31,29,0), SPAL_RGB(15,8,0), 0x0000 },
    /* 4: PaletteData_dcc20 — green/red */
    { 0x7FFF, SPAL_RGB(4,23,13), SPAL_RGB(29,0,0), 0x0000 },
    /* 5: PaletteData_dcc28 — red/blue */
    { 0x7FFF, SPAL_RGB(29,0,0), SPAL_RGB(0,0,22), 0x0000 },
    /* 6: PaletteData_dcc30 — magenta */
    { 0x7FFF, SPAL_RGB(31,0,15), SPAL_RGB(11,0,13), 0x0000 },
    /* 7: PaletteData_dcc38 — cyan/blue */
    { 0x7FFF, SPAL_RGB(11,25,31), SPAL_RGB(0,11,31), 0x0000 },
    /* 8: GoToGengarBonusOnBillboardBGPalette — purple */
    { 0x7FFF, SPAL_RGB(15,15,19), SPAL_RGB(31,0,31), 0x0000 },
    /* 9: GoToMewtwoBonusOnBillboardBGPalette — pink */
    { 0x7FFF, SPAL_RGB(31,25,31), SPAL_RGB(31,0,31), 0x0000 },
    /* 10: GoToMeowthBonusOnBillboardBGPalette — yellow */
    { 0x7FFF, SPAL_RGB(31,31,0), SPAL_RGB(27,11,2), 0x0000 },
    /* 11: GoToDiglettBonusOnBillboardBGPalette — orange */
    { 0x7FFF, SPAL_RGB(31,18,8), SPAL_RGB(27,0,0), 0x0000 },
    /* 12: GoToSeelBonusOnBillboardBGPalette — blue/red */
    { 0x7FFF, SPAL_RGB(20,20,26), SPAL_RGB(31,11,10), 0x0000 },
};

/* Palette 7 color sets — consecutive 8 bytes after pal6 in ROM.
 * All billboard tiles use palette 6 during roulette, so pal7 doesn't
 * visually matter. Included for accuracy. */
static const uint16_t slot_pal7_data[][4] = {
    /* 0: dcc08 (follows dcc00) */ { 0x7FFF, SPAL_RGB(31,28,0), SPAL_RGB(29,0,0), 0x0000 },
    /* 1: dcc10 (follows dcc08) */ { 0x7FFF, SPAL_RGB(31,0,0), SPAL_RGB(16,0,0), 0x0000 },
    /* 2: dcc18 (follows dcc10) */ { 0x7FFF, SPAL_RGB(31,29,0), SPAL_RGB(15,8,0), 0x0000 },
    /* 3: dcc20 (follows dcc18) */ { 0x7FFF, SPAL_RGB(4,23,13), SPAL_RGB(29,0,0), 0x0000 },
    /* 4: dcc28 (follows dcc20) */ { 0x7FFF, SPAL_RGB(29,0,0), SPAL_RGB(0,0,22), 0x0000 },
    /* 5: dcc30 (follows dcc28) */ { 0x7FFF, SPAL_RGB(31,0,15), SPAL_RGB(11,0,13), 0x0000 },
    /* 6: dcc38 (follows dcc30) */ { 0x7FFF, SPAL_RGB(11,25,31), SPAL_RGB(0,11,31), 0x0000 },
    /* 7: Gengar (follows dcc38) */ { 0x7FFF, SPAL_RGB(15,15,19), SPAL_RGB(31,0,31), 0x0000 },
    /* 8: Mewtwo (follows Gengar) */ { 0x7FFF, SPAL_RGB(31,25,31), SPAL_RGB(31,0,31), 0x0000 },
    /* 9: Meowth (follows Mewtwo) */ { 0x7FFF, SPAL_RGB(31,31,0), SPAL_RGB(27,11,2), 0x0000 },
    /* 10: Diglett (follows Meowth) */ { 0x7FFF, SPAL_RGB(31,18,8), SPAL_RGB(27,0,0), 0x0000 },
    /* 11: Seel (follows Diglett) */ { 0x7FFF, SPAL_RGB(20,20,26), SPAL_RGB(31,11,10), 0x0000 },
    /* 12: unknown (follows Seel) */ { 0x7FFF, SPAL_RGB(20,20,26), SPAL_RGB(31,11,10), 0x0000 },
};

/* Map pic_id (0-17) to slot_pal6_data/slot_pal7_data indices.
 * IDs 18+ all use index 0. Matches PaletteDataPointerTable_f2be. */
static const uint8_t slot_pal6_map[18] = {
    0, 0, 0,  /* 0-2: BALL_SAVER_30/60/90 → dcc00 */
    1,        /* 3: PIKACHU_SAVER → dcc08 */
    1,        /* 4: EXTRA_BALL → dcc08 */
    2,        /* 5: SMALL_REWARD → dcc10 */
    3,        /* 6: BIG_REWARD → dcc18 */
    4,        /* 7: CATCHEM_MODE → dcc20 */
    1,        /* 8: EVOLUTION_MODE → dcc08 */
    5,        /* 9: GREAT_BALL → dcc28 */
    1,        /* 10: ULTRA_BALL → dcc08 */
    6,        /* 11: MASTER_BALL → dcc30 */
    7,        /* 12: BONUS_MULTIPLIER → dcc38 */
    8,        /* 13: GENGAR_BONUS */
    9,        /* 14: MEWTWO_BONUS */
    10,       /* 15: MEOWTH_BONUS */
    11,       /* 16: DIGLETT_BONUS */
    12,       /* 17: SEEL_BONUS */
};

void load_colored_billboard_palette(GameState *state, uint8_t pic_id) {
    uint8_t pal6_idx = (pic_id < 18) ? slot_pal6_map[pic_id] : 0;
    uint8_t pal7_idx = pal6_idx; /* Default pal7 = pal6 */
    /* ASM copies 16 consecutive bytes: pal6 data followed by pal7 data.
     * The pal7 data is the next 8 bytes in ROM after the pal6 pointer. */
    if (pal6_idx < 12)
        pal7_idx = (uint8_t)(pal6_idx + 1); /* Consecutive in ROM: pal7 = next entry */
    else
        pal7_idx = pal6_idx; /* Last entry: no known successor */

    for (int c = 0; c < 4; c++) {
        state->bg_palettes[6].colors[c] = slot_pal6_data[pal6_idx][c];
        state->bg_palettes[7].colors[c] = (pal7_idx < 13) ? slot_pal7_data[pal7_idx][c]
                                                           : slot_pal6_data[pal6_idx][c];
    }
}

/*---------------------------------------------------------------------------
 * Free cache
 *---------------------------------------------------------------------------*/
void billboard_free_cache(void) {
    for (int i = 0; i < NUM_BILLBOARD_PICS; i++) {
        free(pic_cache_on[i]);  pic_cache_on[i] = NULL;
        free(pic_cache_off[i]); pic_cache_off[i] = NULL;
    }
    for (int i = 0; i < NUM_BILLBOARD_TILE_DATA; i++) {
        free(td_cache[i]); td_cache[i] = NULL;
    }
}
