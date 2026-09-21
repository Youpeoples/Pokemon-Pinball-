/*
 * Stage asset loading - reads PNGs and tilemaps from disk into virtual VRAM.
 *
 * Asset tables mirror the VIDEO_DATA entries in data/stage_base_gfx.asm.
 * Each stage has a null-terminated array of StageAssetEntry structs.
 */

#include "renderer/stage_assets.h"
#include "renderer/tile_loader.h"
#include "renderer/stage_palettes.h"
#include "data/embedded_data.h"
#include "game/game_state.h"
#include "game/config_data.h"
#include "game/collision.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Interleave tile data for 8x16 sprite mode.
 * Row-major order (tiles_from_png output) → column-interleaved pairs.
 * For a PNG with tiles_per_row columns, pairs of adjacent tile rows
 * are interleaved so that consecutive tiles form 8x16 vertical pairs.
 * E.g. for w32 (4 tiles/row): [A B C D / E F G H] → [A E B F C G D H]
 */
void interleave_tiles(uint8_t *data, size_t size, int tiles_per_row) {
    int tile_size = 16;  /* 2bpp tile = 16 bytes */
    int total_tiles = (int)(size / tile_size);
    int rows = total_tiles / tiles_per_row;

    uint8_t *temp = (uint8_t *)malloc(size);
    if (!temp) return;
    memcpy(temp, data, size);

    int dst = 0;
    for (int row_pair = 0; row_pair < rows; row_pair += 2) {
        for (int col = 0; col < tiles_per_row; col++) {
            /* Top tile of 8x16 pair */
            int src_top = (row_pair * tiles_per_row + col) * tile_size;
            memcpy(&data[dst], &temp[src_top], tile_size);
            dst += tile_size;
            /* Bottom tile of 8x16 pair */
            if (row_pair + 1 < rows) {
                int src_bot = ((row_pair + 1) * tiles_per_row + col) * tile_size;
                memcpy(&data[dst], &temp[src_bot], tile_size);
                dst += tile_size;
            }
        }
    }

    free(temp);
}

/*
 * Red Field Top GBC assets (from StageRedFieldTopGfx_GameBoyColor, line 48-66)
 *
 * VRAM address reference:
 *   vTilesOB = $8000, vTilesSH = $8800, vTilesBG = $9000
 *   vBGMap = $9800, vBGWin = $9C00
 */
static const StageAssetEntry red_field_top_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/red_top/red_top_1.png",
      0x81A0, 0, 0x0260 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/red_top/red_top_2.png",
      0x8600, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/red_top/status_bar_symbols_gameboycolor.png",
      0x8800, 0, 0x0100 },
    { ASSET_TILES, "gfx/stage/red_top/red_top_3.png",
      0x8900, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/red_top/red_top_base_gameboycolor.png",
      0x8AA0, 0, 0x0D60 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/red_top/red_top_4.png",
      0x8800, 1, 0x1000 },
    { ASSET_TILES, "gfx/stage/red_top/red_top_5.png",
      0x8000, 1, 0x0200 },
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },
    { ASSET_TILES, "gfx/stage/red_bottom/japanese_characters.png",
      0x8200, 1, 0x0400 },
    { ASSET_TILES, "gfx/stage/red_bottom/japanese_characters_2.png",
      0x8900, 1, 0x0200 },
    { ASSET_TILES, "gfx/stage/red_top/status_bar_symbols_gameboycolor.png",
      0x8800, 1, 0x0100 },
    { ASSET_TILES, "gfx/stage/red_top/red_top_6.png",
      0x87C0, 1, 0x0040 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_red_field_top_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_red_field_top_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes (filename unused, stage_id determines palette data) */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

/*
 * Red Field Bottom GBC assets (from StageRedFieldBottomGfx_GameBoyColor, line 79-96)
 *
 * vTilesOB = $8000, vTilesSH = $8800, vBGMap = $9800
 */
static const StageAssetEntry red_field_bottom_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/shared/bonus_slot_glow.png",
      0x81A0, 0, 0x0160 },
    { ASSET_TILES, "gfx/stage/shared/arrows.png",
      0x8300, 0, 0x0080 },
    { ASSET_TILES, "gfx/stage/shared/bonus_slot_glow_2.png",
      0x8380, 0, 0x0020 },
    { ASSET_TILES, "gfx/stage/shared/pika_bolt.png",
      0x83C0, 0, 0x0440 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    /* FlipperGfx (main.asm 0xa8600): 18 tiles at OBJ $60-$71. */
    { ASSET_TILES, "gfx/stage/flipper.png",
      0x8600, 0, 0x0120 },
    /* PikachuSaverGfx (main.asm 0xa8720): tiles at OBJ $72-$7D. */
    { ASSET_TILES, "gfx/stage/pikachu_saver.png",
      0x8720, 0, 0x00E0 },
    { ASSET_TILES, "gfx/stage/red_bottom/red_bottom_base_gameboycolor.png",
      0x8800, 0, 0x1000 },
    /* Status bar symbol tiles ($81-$8F) for scoreboard - must come AFTER base
     * to overwrite first $100 bytes at $8800 with correct symbols. */
    { ASSET_TILES, "gfx/stage/red_top/status_bar_symbols_gameboycolor.png",
      0x8800, 0, 0x0100 },
    { ASSET_TILES, "gfx/stage/saver_off.png",
      0x8AA0, 0, 0x0040 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/red_bottom/red_bottom_5.png",
      0x8800, 1, 0x1000 },
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },
    { ASSET_TILES, "gfx/stage/red_bottom/japanese_characters.png",
      0x8200, 1, 0x0400 },
    { ASSET_TILES, "gfx/stage/red_bottom/japanese_characters_2.png",
      0x8900, 1, 0x0200 },
    { ASSET_TILES, "gfx/stage/red_bottom/red_bottom_base_gameboycolor.png",
      0x8800, 1, 0x0100 },
    { ASSET_TILES, "gfx/stage/red_top/red_top_6.png",
      0x87C0, 1, 0x0040 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_red_field_bottom_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_red_field_bottom_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

/*
 * Erase All Data Screen GBC assets (from EraseAllDataGfx_GameBoyColor 0x81b6)
 * LCDC $41 = unsigned addressing
 * Uses HighScoresRedStagePalettes for palette data.
 */
static const StageAssetEntry erase_all_data_assets[] = {
    { ASSET_TILES, "gfx/erase_all_data.png",
      0x8800, 0, 0x0300 },
    { ASSET_TILEMAP, "gfx/tilemaps/erase_all_data.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/bgattr/erase_all_data.bgattr",
      0x9800, 1, 0x0400 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },
    { 0, NULL, 0, 0, 0 }
};

/*
 * Copyright Screen GBC assets (from CopyrightTextGfx_GameBoyColor)
 * LCDC $43 = signed addressing
 */
static const StageAssetEntry copyright_screen_assets[] = {
    { ASSET_TILES, "gfx/copyright_text.png",
      0x8800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/copyright_screen.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/bgattr/copyright_screen.bgattr",
      0x9800, 1, 0x0400 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },
    { 0, NULL, 0, 0, 0 }
};

/*
 * Title Screen GBC assets (from TitlescreenFadeInGfx_GameBoyColor)
 * LCDC $43 = signed addressing
 */
static const StageAssetEntry titlescreen_assets[] = {
    { ASSET_TILES, "gfx/titlescreen/titlescreen_fade_in.png",
      0x8000, 0, 0x1800 },
    { ASSET_TILEMAP, "gfx/tilemaps/titlescreen.map",
      0x9800, 0, 0x0240 },
    { ASSET_TILEMAP, "gfx/bgattr/titlescreen.bgattr",
      0x9800, 1, 0x0240 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },
    { 0, NULL, 0, 0, 0 }
};

/*
 * Field Select Screen GBC assets (from FieldSelectGfx_GameBoyColor)
 *
 * Original ROM: VIDEO_DATA_TILES FieldSelectScreenGfx, vTilesSH - $100, $D00
 * The $D00 bytes of FieldSelectScreenGfx are structured as:
 *   $000-$0FF: blinking_border.2bpp (16 sprite tiles, loaded at $8700-$87FF)
 *   $100-$CFF: field_select_tiles.2bpp (192 BG tiles, loaded at $8800-$93FF)
 *
 * Since our PNGs are separate files, we load them individually:
 *   - blinking_border.png at $8700 (sprite tiles for border animation)
 *   - field_select_tiles.png at $8800 (BG tiles referenced by tilemap)
 */
static const StageAssetEntry field_select_assets[] = {
    { ASSET_TILES, "gfx/field_select/blinking_border.png",
      0x8700, 0, 0x0100 },
    { ASSET_TILES, "gfx/field_select/field_select_tiles.png",
      0x8800, 0, 0x0C00 },
    { ASSET_TILEMAP, "gfx/tilemaps/field_select.map",
      0x9800, 0, 0x0240 },
    { ASSET_TILEMAP, "gfx/bgattr/field_select.bgattr",
      0x9800, 1, 0x0240 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0048 },
    { 0, NULL, 0, 0, 0 }
};

/*
 * Option Menu GBC assets (from OptionsScreenVideoData_GameBoyColor)
 * All tiles loaded as individual PNGs packed sequentially at $8000 ($1400 total)
 * VRAM addresses computed from ROM layout: base 0xb5800 -> VRAM $8000
 */
static const StageAssetEntry option_menu_assets[] = {
    /* Tile PNGs - 29 files packed sequentially */
    { ASSET_TILES, "gfx/option_menu/blank.png",
      0x8000, 0, 0x0200 },
    { ASSET_TILES, "gfx/option_menu/arrow.png",
      0x8200, 0, 0x0020 },
    { ASSET_TILES, "gfx/option_menu/pika_bubble.png",
      0x8220, 0, 0x0060 },
    { ASSET_TILES, "gfx/option_menu/bouncing_pokeball.png",
      0x8280, 0, 0x00C0 },
    { ASSET_TILES, "gfx/option_menu/rumble_pikachu_animation.png",
      0x8340, 0, 0x00C0 },
    { ASSET_TILES, "gfx/option_menu/psyduck.png",
      0x8400, 0, 0x03C0 },
    { ASSET_TILES, "gfx/option_menu/bold_arrow.png",
      0x87C0, 0, 0x0010 },
    { ASSET_TILES, "gfx/option_menu/solid_colors.png",
      0x87D0, 0, 0x0050 },
    { ASSET_TILES, "gfx/option_menu/option_text.png",
      0x8820, 0, 0x0060 },
    { ASSET_TILES, "gfx/option_menu/pikachu.png",
      0x8880, 0, 0x00F0 },
    { ASSET_TILES, "gfx/option_menu/psyduck_feet.png",
      0x8970, 0, 0x0090 },
    { ASSET_TILES, "gfx/option_menu/blank2.png",
      0x8A00, 0, 0x0050 },
    { ASSET_TILES, "gfx/option_menu/rumble_text.png",
      0x8A50, 0, 0x0060 },
    { ASSET_TILES, "gfx/option_menu/blank3.png",
      0x8AB0, 0, 0x0070 },
    { ASSET_TILES, "gfx/option_menu/key_co_text.png",
      0x8B20, 0, 0x0050 },
    { ASSET_TILES, "gfx/option_menu/sound_test_digits.png",
      0x8B70, 0, 0x0100 },
    { ASSET_TILES, "gfx/option_menu/nfig_text.png",
      0x8C70, 0, 0x0030 },
    { ASSET_TILES, "gfx/option_menu/blank4.png",
      0x8CA0, 0, 0x0060 },
    { ASSET_TILES, "gfx/key_config/reset_text.png",
      0x8D00, 0, 0x0060 },
    { ASSET_TILES, "gfx/key_config/ball_start_text.png",
      0x8D60, 0, 0x0090 },
    { ASSET_TILES, "gfx/key_config/left_flipper_text.png",
      0x8DF0, 0, 0x0090 },
    { ASSET_TILES, "gfx/key_config/right_flipper_text.png",
      0x8E80, 0, 0x0090 },
    { ASSET_TILES, "gfx/key_config/tilt_text.png",
      0x8F10, 0, 0x0100 },
    { ASSET_TILES, "gfx/key_config/menu_text.png",
      0x9010, 0, 0x0070 },
    { ASSET_TILES, "gfx/key_config/key_config_text.png",
      0x9080, 0, 0x0080 },
    { ASSET_TILES, "gfx/key_config/icons.png",
      0x9100, 0, 0x0140 },
    { ASSET_TILES, "gfx/option_menu/sound_test_text.png",
      0x9240, 0, 0x0090 },
    { ASSET_TILES, "gfx/option_menu/on_off_text.png",
      0x92D0, 0, 0x0040 },
    { ASSET_TILES, "gfx/option_menu/bgm_se_text.png",
      0x9310, 0, 0x00F0 },
    /* Tilemaps - BG */
    { ASSET_TILEMAP, "gfx/tilemaps/option_menu.map",
      0x9800, 0, 0x0240 },
    { ASSET_TILEMAP, "gfx/tilemaps/option_menu_3.map",
      0x9800, 1, 0x0240 },
    /* Tilemaps - Window (key config display) */
    { ASSET_TILEMAP, "gfx/tilemaps/option_menu_2.map",
      0x9C00, 0, 0x0240 },
    { ASSET_TILEMAP, "gfx/tilemaps/option_menu_4.map",
      0x9C00, 1, 0x0240 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },
    { 0, NULL, 0, 0, 0 }
};

/*
 * High Scores Red Stage GBC assets (from HighScoresRedStageVideoData_GameBoyColor)
 */
static const StageAssetEntry high_scores_red_assets[] = {
    { ASSET_TILES, "gfx/high_scores/high_scores_base_gameboy.png",
      0x8000, 0, 0x1800 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen_2.map",
      0x9C00, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen_4.map",
      0x9800, 1, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen_5.map",
      0x9C00, 1, 0x0400 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },
    { 0, NULL, 0, 0, 0 }
};

/*
 * High Scores Blue Stage GBC assets (from HighScoresBlueStageVideoData_GameBoyColor)
 * Same tile data and tilemaps as Red, but different palettes.
 */
static const StageAssetEntry high_scores_blue_assets[] = {
    { ASSET_TILES, "gfx/high_scores/high_scores_base_gameboy.png",
      0x8000, 0, 0x1800 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen_2.map",
      0x9C00, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen_4.map",
      0x9800, 1, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/high_scores_screen_5.map",
      0x9C00, 1, 0x0400 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },
    { 0, NULL, 0, 0, 0 }
};

/*
 * Pokedex Screen GBC assets (from Data_280c4)
 * LCDC $23 = signed tile addressing, BG+OBJ on
 */
/*
 * Pokedex Screen GBC assets (from Data_280c4)
 * LCDC $23 = signed tile addressing, BG+OBJ on
 *
 * NOTE: ASM uses raw dw values that include the << 2 encoding from
 * VIDEO_DATA_TILES macro.  Actual sizes = encoded >> 2:
 *   $6000 >> 2 = $1800 (tiles), $1000 >> 2 = $0400 (bg map),
 *   $800 >> 2 = $0200 (win map halves)
 */
static const StageAssetEntry pokedex_assets[] = {
    { ASSET_TILES, "gfx/pokedex/pokedex_initial.png",
      0x8000, 0, 0x1800 },
    { ASSET_TILEMAP, "gfx/tilemaps/pokedex_2.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/bgattr/pokedex_2.bgattr",
      0x9800, 1, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/pokedex.map",
      0x9C00, 0, 0x0200 },
    { ASSET_TILEMAP, "gfx/tilemaps/pokedex.map",
      0x9E00, 0, 0x0200 },
    { ASSET_TILEMAP, "gfx/bgattr/pokedex.bgattr",
      0x9C00, 1, 0x0200 },
    { ASSET_TILEMAP, "gfx/bgattr/pokedex.bgattr",
      0x9E00, 1, 0x0200 },
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },
    { 0, NULL, 0, 0, 0 }
};

static const StageAssetEntry *get_screen_asset_table(uint8_t screen_id) {
    switch (screen_id) {
        case SCREEN_ERASE_ALL_DATA: return erase_all_data_assets;
        case SCREEN_COPYRIGHT:    return copyright_screen_assets;
        case SCREEN_TITLESCREEN:  return titlescreen_assets;
        case SCREEN_FIELD_SELECT: return field_select_assets;
        case SCREEN_OPTIONS:      return option_menu_assets;
        case SCREEN_HIGH_SCORES:  return high_scores_red_assets;
        case SCREEN_POKEDEX:      return pokedex_assets;
        default:                  return NULL;
    }
}

/* Special: get Blue HS assets table for field switching */
const StageAssetEntry *get_high_scores_asset_table(uint8_t stage) {
    return stage ? high_scores_blue_assets : high_scores_red_assets;
}

/*
 * Gengar Bonus GBC assets (from StageGengarBonusGfx_GameBoyColor, 0xea5a)
 *
 * Ghost sprite tiles (gastly, haunter, gengar) are loaded by
 * load_stage_data_gengar_bonus() since they require partial/offset loading.
 */
static const StageAssetEntry gengar_bonus_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/shared/pika_bolt.png",
      0x83C0, 0, 0x0440 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/flipper.png",
      0x8600, 0, 0x0120 },
    { ASSET_TILES, "gfx/stage/gengar_bonus/gengar_bonus_base_gameboycolor.png",
      0x8800, 0, 0x1000 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/gengar_bonus/gengar_bonus_1.png",
      0x8800, 1, 0x1000 },
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_gengar_bonus_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_gengar_bonus_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

static const StageAssetEntry mewtwo_bonus_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/shared/pika_bolt.png",
      0x83C0, 0, 0x0440 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/flipper.png",
      0x8600, 0, 0x0120 },
    { ASSET_TILES, "gfx/stage/mewtwo_bonus/mewtwo_bonus_base_gameboycolor.png",
      0x8800, 0, 0x1000 },

    /* Bank 1 tile data — mewtwo_1.png + timer_digits */
    { ASSET_TILES, "gfx/stage/mewtwo_bonus/mewtwo_1.png",
      0x8800, 1, 0x1000 },
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_mewtwo_bonus_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_mewtwo_bonus_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

/*
 * Meowth Bonus GBC assets (from StageMeowthBonusGfx_GameBoyColor, 0xeb88)
 *
 * Meowth sprite tiles (meowth_1-4) loaded as part of base graphics.
 * load_stage_data_meowth_bonus() handles additional per-state loads.
 */
static const StageAssetEntry meowth_bonus_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/meowth_bonus/meowth_1.png",
      0x81A0, 0, 0x0260 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/flipper.png",
      0x8600, 0, 0x0120 },
    { ASSET_TILES, "gfx/stage/meowth_bonus/meowth_2.png",
      0x87A0, 0, 0x0060 },
    { ASSET_TILES, "gfx/stage/meowth_bonus/meowth_bonus_base_gameboycolor.png",
      0x8800, 0, 0x0900 },
    { ASSET_TILES, "gfx/stage/meowth_bonus/meowth_3.png",
      0x8900, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/meowth_bonus/meowth_4.png",
      0x8AA0, 0, 0x0360 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_meowth_bonus_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_meowth_bonus_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

/*
 * Diglett Bonus GBC assets (from StageDiglettBonusGfx_GameBoyColor, 0xec11)
 *
 * Dugtrio sprite tiles are loaded AFTER base BG tiles and overwrite
 * parts of the BG tile data at $8900 and $8AA0. Order matters.
 */
static const StageAssetEntry diglett_bonus_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/diglett_bonus/dugtrio_1.png",
      0x81A0, 0, 0x0260 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/flipper.png",
      0x8600, 0, 0x0120 },
    { ASSET_TILES, "gfx/stage/diglett_bonus/dugtrio_2.png",
      0x87A0, 0, 0x0060 },
    { ASSET_TILES, "gfx/stage/diglett_bonus/diglett_bonus_base_gameboycolor.png",
      0x8800, 0, 0x0E00 },
    { ASSET_TILES, "gfx/stage/diglett_bonus/dugtrio_3.png",
      0x8900, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/diglett_bonus/dugtrio_4.png",
      0x8AA0, 0, 0x0280 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_diglett_bonus_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_diglett_bonus_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

/*
 * Seel Bonus Stage assets (StageSeelBonusGfx_GameBoyColor, 0xEC9A)
 *
 * Seel sprite tiles loaded at multiple offsets, base BG tiles at $8800.
 * Timer digits in bank 1 at $8600.
 */
static const StageAssetEntry seel_bonus_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/seel_bonus/seel_1.png",
      0x81A0, 0, 0x0260 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/flipper.png",
      0x8600, 0, 0x0120 },
    { ASSET_TILES, "gfx/stage/seel_bonus/seel_2.png",
      0x87A0, 0, 0x0060 },
    { ASSET_TILES, "gfx/stage/seel_bonus/seel_bonus_base_gameboycolor.png",
      0x8800, 0, 0x0B00 },
    { ASSET_TILES, "gfx/stage/seel_bonus/seel_3.png",
      0x8900, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/seel_bonus/seel_4.png",
      0x8AA0, 0, 0x04A0 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_seel_bonus_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_seel_bonus_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

/*
 * Blue Field Top GBC assets (from StageBlueFieldTopGfx_GameBoyColor, 0xe92c)
 *
 * Simpler than red top: no japanese_characters, no extra bank 1 tile files.
 */
static const StageAssetEntry blue_field_top_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/blue_top/blue_top_1.png",
      0x81A0, 0, 0x0260 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/blue_top/blue_top_2.png",
      0x8600, 0, 0x0200 },
    { ASSET_TILES, "gfx/stage/blue_top/status_bar_symbols_gameboycolor.png",
      0x8800, 0, 0x0100 },
    { ASSET_TILES, "gfx/stage/blue_top/blue_top_3.png",
      0x8900, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/blue_top/blue_top_base_gameboycolor.png",
      0x8AA0, 0, 0x0D60 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/blue_top/blue_top_4.png",
      0x8800, 1, 0x1000 },
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_blue_field_top_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_blue_field_top_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

/*
 * Blue Field Bottom GBC assets (from StageBlueFieldBottomGfx_GameBoyColor, 0xe9bc)
 *
 * Same shared bottom-stage assets as red (slot glow, arrows, pika bolt, flippers).
 * Ball/flipper/pikachu_saver tiles loaded here for convenience (ASM loads separately).
 */
static const StageAssetEntry blue_field_bottom_assets[] = {
    /* Bank 0 tile data */
    { ASSET_TILES, "gfx/stage/alphabet_2.png",
      0x8000, 0, 0x01A0 },
    { ASSET_TILES, "gfx/stage/shared/bonus_slot_glow.png",
      0x81A0, 0, 0x0160 },
    { ASSET_TILES, "gfx/stage/shared/arrows.png",
      0x8300, 0, 0x0080 },
    { ASSET_TILES, "gfx/stage/shared/bonus_slot_glow_2.png",
      0x8380, 0, 0x0020 },
    { ASSET_TILES, "gfx/stage/shared/pika_bolt.png",
      0x83C0, 0, 0x0440 },
    { ASSET_TILES, "gfx/stage/ball_pokeball.w32.interleave.png",
      0x8400, 0, 0x0200 },
    /* FlipperGfx: 18 tiles at OBJ $60-$71 */
    { ASSET_TILES, "gfx/stage/flipper.png",
      0x8600, 0, 0x0120 },
    /* PikachuSaverGfx: tiles at OBJ $72-$7D */
    { ASSET_TILES, "gfx/stage/pikachu_saver.png",
      0x8720, 0, 0x00E0 },
    { ASSET_TILES, "gfx/stage/blue_bottom/blue_bottom_base_gameboycolor.png",
      0x8800, 0, 0x1000 },
    /* Status bar symbols - must come AFTER base to overwrite first $100 at $8800 */
    { ASSET_TILES, "gfx/stage/blue_top/status_bar_symbols_gameboycolor.png",
      0x8800, 0, 0x0100 },
    { ASSET_TILES, "gfx/stage/saver_off.png",
      0x8AA0, 0, 0x0040 },

    /* Bank 1 tile data */
    { ASSET_TILES, "gfx/stage/blue_bottom/blue_bottom_1.png",
      0x8800, 1, 0x1000 },
    { ASSET_TILES, "gfx/stage/timer_digits.png",
      0x8600, 1, 0x0160 },

    /* Tilemaps */
    { ASSET_TILEMAP, "gfx/tilemaps/stage_blue_field_bottom_gameboycolor.map",
      0x9800, 0, 0x0400 },
    { ASSET_TILEMAP, "gfx/tilemaps/stage_blue_field_bottom_gameboycolor_2.map",
      0x9800, 1, 0x0400 },

    /* Palettes */
    { ASSET_PALETTES, NULL, 0, 0, 0x0080 },

    /* Sentinel */
    { 0, NULL, 0, 0, 0 }
};

static const StageAssetEntry *get_stage_asset_table(uint8_t stage_id) {
    switch (stage_id) {
        case STAGE_RED_FIELD_TOP:
            return red_field_top_assets;
        case STAGE_RED_FIELD_BOTTOM:
            return red_field_bottom_assets;
        case STAGE_GENGAR_BONUS:
            return gengar_bonus_assets;
        case STAGE_MEWTWO_BONUS:
            return mewtwo_bonus_assets;
        case STAGE_MEOWTH_BONUS:
            return meowth_bonus_assets;
        case STAGE_DIGLETT_BONUS:
            return diglett_bonus_assets;
        case STAGE_SEEL_BONUS:
            return seel_bonus_assets;
        case STAGE_BLUE_FIELD_TOP:
            return blue_field_top_assets;
        case STAGE_BLUE_FIELD_BOTTOM:
            return blue_field_bottom_assets;
        default:
            return NULL;
    }
}

bool load_stage_assets(uint8_t stage_id, VirtualVRAM *vram,
                       GameState *state, const char *base_path) {
    const StageAssetEntry *table = get_stage_asset_table(stage_id);
    if (!table) {
        fprintf(stderr, "load_stage_assets: no asset table for stage %d\n", stage_id);
        return false;
    }

    char path_buf[512];
    bool success = true;

    for (const StageAssetEntry *entry = table; ; entry++) {
        /* Check for sentinel: both ASSET_TILES/ASSET_TILEMAP need a filename,
         * ASSET_PALETTES has NULL filename. A sentinel has size=0 and type=0. */
        if (entry->type == ASSET_PALETTES && entry->size > 0) {
            /* Try config palettes first, fall back to hardcoded arrays */
            const uint16_t *bg_pals = config_get_stage_bg_palettes(state->config, stage_id);
            const uint16_t *obj_pals = config_get_stage_obj_palettes(state->config, stage_id);
            if (!bg_pals) bg_pals = get_stage_bg_palettes(stage_id);
            if (!obj_pals) obj_pals = get_stage_obj_palettes(stage_id);
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 4; c++) {
                    state->bg_palettes[i].colors[c] = bg_pals[i * 4 + c];
                }
            }
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 4; c++) {
                    state->obj_palettes[i].colors[c] = obj_pals[i * 4 + c];
                }
            }
            printf("  Loaded palettes for stage %d\n", stage_id);
            continue;
        }

        if (!entry->filename) break; /* Sentinel */

        /* Build full path */
        snprintf(path_buf, sizeof(path_buf), "%s/%s", base_path, entry->filename);
        /* Normalize path separators for Windows */
        for (char *p = path_buf; *p; p++) {
            if (*p == '/') *p = '\\';
        }

        if (entry->type == ASSET_TILES) {
            size_t data_size;
            uint8_t *tile_data = tiles_from_png(path_buf, &data_size);
            if (!tile_data) {
                fprintf(stderr, "  WARN: failed to load tiles from '%s'\n", path_buf);
                success = false;
                continue;
            }

            /* Handle .interleave. format: rearrange tiles for 8x16 sprite pairs.
             * Extract width from .wN. prefix (e.g. ".w32." = 32px = 4 tiles/row). */
            if (strstr(entry->filename, ".interleave.")) {
                const char *wp = strstr(entry->filename, ".w");
                if (wp) {
                    int px_width = atoi(wp + 2);
                    int tiles_per_row = px_width / 8;
                    if (tiles_per_row > 0) {
                        interleave_tiles(tile_data, data_size, tiles_per_row);
                    }
                }
            }

            uint16_t copy_size = entry->size;
            if (copy_size == 0 || copy_size > data_size) {
                copy_size = (uint16_t)data_size;
            }

            vram_write(vram, entry->vram_bank, entry->vram_dest, tile_data, copy_size);
            printf("  Tiles: %s -> $%04X bank %d (%d bytes)\n",
                   entry->filename, entry->vram_dest, entry->vram_bank, copy_size);
            free(tile_data);

        } else if (entry->type == ASSET_TILEMAP) {
            size_t data_size;
            uint8_t *map_data = load_binary_data(base_path, entry->filename, &data_size);
            if (!map_data) {
                fprintf(stderr, "  WARN: failed to load tilemap from '%s'\n", entry->filename);
                success = false;
                continue;
            }

            uint16_t copy_size = entry->size;
            if (copy_size == 0 || copy_size > data_size) {
                copy_size = (uint16_t)data_size;
            }

            vram_write(vram, entry->vram_bank, entry->vram_dest, map_data, copy_size);
            printf("  Tilemap: %s -> $%04X bank %d (%d bytes)\n",
                   entry->filename, entry->vram_dest, entry->vram_bank, copy_size);
            free(map_data);
        }
    }

    return success;
}

bool load_screen_assets(uint8_t screen_id, VirtualVRAM *vram,
                        GameState *state, const char *base_path) {
    const StageAssetEntry *table = get_screen_asset_table(screen_id);
    if (!table) {
        fprintf(stderr, "load_screen_assets: no asset table for screen %d\n", screen_id);
        return false;
    }

    char path_buf[512];
    bool success = true;

    for (const StageAssetEntry *entry = table; ; entry++) {
        if (entry->type == ASSET_PALETTES && entry->size > 0) {
            /* Try config palettes first, fall back to hardcoded arrays */
            const uint16_t *bg_pals = config_get_screen_bg_palettes(state->config, screen_id);
            const uint16_t *obj_pals = config_get_screen_obj_palettes(state->config, screen_id);
            if (!bg_pals) bg_pals = get_screen_bg_palettes(screen_id);
            if (!obj_pals) obj_pals = get_screen_obj_palettes(screen_id);
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 4; c++) {
                    state->bg_palettes[i].colors[c] = bg_pals[i * 4 + c];
                }
            }
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 4; c++) {
                    state->obj_palettes[i].colors[c] = obj_pals[i * 4 + c];
                }
            }
            printf("  Loaded palettes for screen %d\n", screen_id);
            continue;
        }

        if (!entry->filename) break;

        snprintf(path_buf, sizeof(path_buf), "%s/%s", base_path, entry->filename);
        for (char *p = path_buf; *p; p++) {
            if (*p == '/') *p = '\\';
        }

        if (entry->type == ASSET_TILES) {
            size_t data_size;
            uint8_t *tile_data = tiles_from_png(path_buf, &data_size);
            if (!tile_data) {
                fprintf(stderr, "  WARN: failed to load tiles from '%s'\n", path_buf);
                success = false;
                continue;
            }

            uint16_t copy_size = entry->size;
            if (copy_size == 0 || copy_size > data_size) {
                copy_size = (uint16_t)data_size;
            }

            vram_write(vram, entry->vram_bank, entry->vram_dest, tile_data, copy_size);
            printf("  Tiles: %s -> $%04X bank %d (%d bytes)\n",
                   entry->filename, entry->vram_dest, entry->vram_bank, copy_size);
            free(tile_data);

        } else if (entry->type == ASSET_TILEMAP) {
            size_t data_size;
            uint8_t *map_data = load_binary_data(base_path, entry->filename, &data_size);
            if (!map_data) {
                fprintf(stderr, "  WARN: failed to load tilemap from '%s'\n", entry->filename);
                success = false;
                continue;
            }

            uint16_t copy_size = entry->size;
            if (copy_size == 0 || copy_size > data_size) {
                copy_size = (uint16_t)data_size;
            }

            vram_write(vram, entry->vram_bank, entry->vram_dest, map_data, copy_size);
            printf("  Tilemap: %s -> $%04X bank %d (%d bytes)\n",
                   entry->filename, entry->vram_dest, entry->vram_bank, copy_size);
            free(map_data);
        }
    }

    return success;
}

void combined_view_load_both_halves(GameState *state) {
    /* Determine which field (red/blue) based on current stage */
    uint8_t top_stage, bottom_stage;
    if (state->current_stage == STAGE_RED_FIELD_TOP ||
        state->current_stage == STAGE_RED_FIELD_BOTTOM) {
        top_stage = STAGE_RED_FIELD_TOP;
        bottom_stage = STAGE_RED_FIELD_BOTTOM;
    } else if (state->current_stage == STAGE_BLUE_FIELD_TOP ||
               state->current_stage == STAGE_BLUE_FIELD_BOTTOM) {
        top_stage = STAGE_BLUE_FIELD_TOP;
        bottom_stage = STAGE_BLUE_FIELD_BOTTOM;
    } else {
        /* Bonus stage -- don't load combined view data */
        return;
    }

    /* Save current palettes — load_stage_assets overwrites them */
    GBCPalette saved_bg[8], saved_obj[8];
    memcpy(saved_bg, state->bg_palettes, sizeof(saved_bg));
    memcpy(saved_obj, state->obj_palettes, sizeof(saved_obj));

    /* Load top half VISUAL assets (tiles, tilemaps, palettes) into vram_top */
    printf("Combined view: loading top half (stage 0x%02X)\n", top_stage);
    load_stage_assets(top_stage, state->vram_top, state, state->asset_base_path);
    memcpy(state->bg_palettes_top, state->bg_palettes, sizeof(state->bg_palettes_top));
    memcpy(state->obj_palettes_top, state->obj_palettes, sizeof(state->obj_palettes_top));

    /* Load bottom half VISUAL assets into vram_bottom */
    printf("Combined view: loading bottom half (stage 0x%02X)\n", bottom_stage);
    load_stage_assets(bottom_stage, state->vram_bottom, state, state->asset_base_path);
    memcpy(state->bg_palettes_bottom, state->bg_palettes, sizeof(state->bg_palettes_bottom));
    memcpy(state->obj_palettes_bottom, state->obj_palettes, sizeof(state->obj_palettes_bottom));

    /* Restore original palettes (active half's palettes should stay unchanged) */
    memcpy(state->bg_palettes, saved_bg, sizeof(saved_bg));
    memcpy(state->obj_palettes, saved_obj, sizeof(saved_obj));

    /* Determine active half from current stage (odd = bottom) */
    state->combined_active_half = (state->current_stage & 1) ? 1 : 0;

    /* Point VRAM to active half's data for sprite tile lookup */
    if (state->combined_active_half == 0) {
        state->vram = state->vram_top;
    } else {
        state->vram = state->vram_bottom;
    }

    /* DO NOT touch collision state — the collision map and masks are already
     * correct for the current half from normal gameplay. Collision will be
     * reloaded via load_stage_collision_attributes() when the ball transitions
     * between halves (in combined_view_swap_half). */

    printf("Combined view: loaded both halves (active=%s)\n",
           state->combined_active_half ? "bottom" : "top");
}
