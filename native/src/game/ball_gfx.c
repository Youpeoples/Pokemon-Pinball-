/*
 * Ball Graphics System
 *
 * Translated from engine/pinball_game/ball_gfx.asm.
 * Loads ball sprite tile data and OBJ palette based on ball_type and ball_size.
 *
 * Ball types: 0=Pokeball, 2=Great, 3=Ultra, 5=Master
 * Ball sizes: 0=normal, 1=mini, 2=super-mini
 *
 * All ball tiles load to OBJ $40 ($8400 VRAM), 0x200 bytes (32 tiles).
 * OBJ palette #4 is set based on ball type.
 */

#include "game/ball_gfx.h"
#include "renderer/tile_loader.h"
#include "renderer/vram.h"
#include "renderer/stage_assets.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Ball VRAM destination: OBJ tile $40 = $8000 + $40*16 = $8400 */
#define BALL_VRAM_DEST  0x8400
#define BALL_VRAM_SIZE  0x0200

/* OBJ palette data for each ball type (ASM: data/ball_palettes.asm).
 * 4 colors × RGB555. Palette index 4 (OBJ palette #4). */
static const uint16_t ball_palettes[4][4] = {
    /* Pokeball (type 0): RGB(21,21,21), (31,31,31), (31,5,4), (0,0,0) */
    { (21 | (21 << 5) | (21 << 10)),
      (31 | (31 << 5) | (31 << 10)),
      (31 | (5 << 5) | (4 << 10)),
      0x0000 },
    /* Great Ball (type 2): RGB(21,21,21), (31,31,31), (2,8,31), (0,0,0) */
    { (21 | (21 << 5) | (21 << 10)),
      (31 | (31 << 5) | (31 << 10)),
      (2 | (8 << 5) | (31 << 10)),
      0x0000 },
    /* Ultra Ball (type 3): RGB(21,21,21), (31,31,31), (27,21,0), (0,0,0) */
    { (21 | (21 << 5) | (21 << 10)),
      (31 | (31 << 5) | (31 << 10)),
      (27 | (21 << 5) | (0 << 10)),
      0x0000 },
    /* Master Ball (type 5): RGB(21,21,21), (31,31,31), (21,3,21), (0,0,0) */
    { (21 | (21 << 5) | (21 << 10)),
      (31 | (31 << 5) | (31 << 10)),
      (21 | (3 << 5) | (21 << 10)),
      0x0000 },
};

/* Map ball_type to palette index (0-3) */
static int ball_type_to_palette_index(uint8_t ball_type) {
    switch (ball_type) {
    case 0: return 0;  /* Pokeball */
    case 2: return 1;  /* Great */
    case 3: return 2;  /* Ultra */
    case 5: return 3;  /* Master */
    default: return 0; /* fallback to Pokeball */
    }
}

/* Map ball_type to PNG filename suffix */
static const char *ball_type_to_name(uint8_t ball_type) {
    switch (ball_type) {
    case 0: return "pokeball";
    case 2: return "greatball";
    case 3: return "ultraball";
    case 5: return "masterball";
    default: return "pokeball";
    }
}

/* Set OBJ palette #0 based on ball_type.
 * ASM: FarCopyCGBPals de=$0040 — bit 6 of e selects OBJ palettes,
 * lower bits = offset 0 = OBJ palette 0. */
static void load_ball_palette(GameState *state) {
    int idx = ball_type_to_palette_index(state->ball_type);
    for (int c = 0; c < 4; c++) {
        state->obj_palettes[0].colors[c] = ball_palettes[idx][c];
    }
}

/* Load ball tile data from PNG to VRAM $8400.
 * The .w32.interleave PNGs need interleave processing. */
static void load_ball_tiles(GameState *state, const char *png_path) {
    if (!state->vram) return;

    char path[260];
    snprintf(path, sizeof(path), "%s/%s", state->asset_base_path, png_path);
    /* Normalize path separators for Windows */
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (!tile_data) return;

    /* Apply interleave for .w32. format (4 tiles per row) */
    if (strstr(png_path, ".w32.interleave.")) {
        interleave_tiles(tile_data, data_size, 4);
    }

    uint16_t copy_size = BALL_VRAM_SIZE;
    if (copy_size > data_size) copy_size = (uint16_t)data_size;

    vram_write(state->vram, 0, BALL_VRAM_DEST, tile_data, copy_size);
    free(tile_data);
}

/*
 * LoadBallGfx (0xdcc3): Load full-size ball tiles + palette.
 * ASM selects PNG by wBallType, loads 0x200 bytes to vTilesOB $40.
 */
void load_ball_gfx(GameState *state) {
    state->ball_size = 0;
    char png[80];
    snprintf(png, sizeof(png), "gfx/stage/ball_%s.w32.interleave.png",
             ball_type_to_name(state->ball_type));
    load_ball_tiles(state, png);
    load_ball_palette(state);
}

/*
 * LoadMiniBallGfx (0xdd12): Load mini ball tiles + palette.
 * ASM selects mini PNG by wBallType.
 */
void load_mini_ball_gfx(GameState *state) {
    state->ball_size = 1;
    char png[80];
    snprintf(png, sizeof(png), "gfx/stage/ball_%s_mini.w32.interleave.png",
             ball_type_to_name(state->ball_type));
    load_ball_tiles(state, png);
    load_ball_palette(state);
}

/*
 * LoadSuperMiniPinballGfx (0xdd62): Load super-mini ball tiles.
 * ASM: fixed PNG (ball_mini), not ball-type-dependent.
 * Palette still set by ball_type.
 */
void load_super_mini_ball_gfx(GameState *state) {
    state->ball_size = 2;
    load_ball_tiles(state, "gfx/stage/ball_mini.w32.interleave.png");
    load_ball_palette(state);
}

/*
 * LoadEAcuteCharacterGfx (0xf55c): Load é tile to VRAM tile $03 ($8830).
 * Called at start of EndOfBallBonus.
 * GBC path: loads e_acute_color.png (1 tile, 16 bytes).
 */
void load_e_acute_character_gfx(GameState *state) {
    if (!state->vram) return;

    char path[260];
    snprintf(path, sizeof(path), "%s/gfx/stage/e_acute_color.png",
             state->asset_base_path);
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (tile_data && data_size >= 16) {
        /* vTilesSH tile $03 = $8800 + 3*16 = $8830 */
        vram_write(state->vram, 0, 0x8830, tile_data, 16);
    }
    free(tile_data);
}

/*
 * load_special_text_tile: Helper to load a single tile from a PNG file into VRAM.
 * vram_addr is the destination VRAM address (e.g. 0x8830 for tile $03).
 */
static void load_special_text_tile(GameState *state, const char *png_name,
                                   uint16_t vram_addr) {
    if (!state->vram) return;

    char path[260];
    snprintf(path, sizeof(path), "%s/gfx/stage/%s",
             state->asset_base_path, png_name);
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (tile_data && data_size >= 16) {
        vram_write(state->vram, 0, vram_addr, tile_data, 16);
    }
    free(tile_data);
}

/*
 * load_special_text_tile_from_menu_symbols: Load one tile from the menu_symbols
 * sprite sheet at a given tile offset. menu_symbols.png is a multi-tile image;
 * each tile is 16 bytes in the 2bpp data.
 */
static void load_special_text_tile_from_symbols(GameState *state,
                                                 uint16_t src_tile_offset,
                                                 uint16_t vram_addr) {
    if (!state->vram) return;

    char path[260];
    snprintf(path, sizeof(path), "%s/gfx/stage/menu_symbols.png",
             state->asset_base_path);
    for (char *p = path; *p; p++) {
        if (*p == '/') *p = '\\';
    }

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (tile_data && src_tile_offset + 16 <= data_size) {
        vram_write(state->vram, 0, vram_addr, tile_data + src_tile_offset, 16);
    }
    free(tile_data);
}

/*
 * LoadSpecialTextChar (home/text.asm 0x31e1):
 * Load custom glyph tile data into shared VRAM tile positions for bottom text.
 *
 * ASM SpecialTextCharPointers (GBC entries, index 9+):
 *   0/9:  Male (♂)       → menu_symbols+$40 → vTilesSH tile 3 ($8830)
 *   1/10: Female (♀)     → menu_symbols+$30 → vTilesSH tile 4 ($8840)
 *   2/11: Apostrophe (`) → apostrophe_color  → vTilesSH tile 5 ($8850)
 *   3/12: é              → e_acute_color     → vTilesSH tile 3 ($8830)
 *   4/13: Asterisk (*)   → menu_symbols+$80  → vTilesSH tile 7 ($8870)
 *   5/14: Exclamation(!) → exclamation_color  → vTilesSH tile 5 ($8850)
 *   6/15: Little x       → menu_symbols+$10  → vTilesSH tile 5 ($8850)
 *   7/16: Period (.)     → period_color       → vTilesSH tile 6 ($8860)
 *   8/17: Colon (:)      → colon_color        → vTilesSH tile 3 ($8830)
 *
 * The char_id parameter matches the ASM index (0-8).
 */
void load_special_text_char_gfx(GameState *state, uint8_t char_id) {
    switch (char_id) {
    case 0: /* Male ♂ */
        load_special_text_tile_from_symbols(state, 0x40, 0x8830);
        break;
    case 1: /* Female ♀ */
        load_special_text_tile_from_symbols(state, 0x30, 0x8840);
        break;
    case 2: /* Apostrophe */
        load_special_text_tile(state, "apostrophe_color.png", 0x8850);
        break;
    case 3: /* é */
        load_special_text_tile(state, "e_acute_color.png", 0x8830);
        break;
    case 4: /* Asterisk */
        load_special_text_tile_from_symbols(state, 0x80, 0x8870);
        break;
    case 5: /* Exclamation ! */
        load_special_text_tile(state, "exclamation_point_color.png", 0x8850);
        break;
    case 6: /* Little x */
        load_special_text_tile_from_symbols(state, 0x10, 0x8850);
        break;
    case 7: /* Period */
        load_special_text_tile(state, "period_color.png", 0x8860);
        break;
    case 8: /* Colon */
        load_special_text_tile(state, "colon_color.png", 0x8830);
        break;
    }
}
