/*
 * Stage palette data - hardcoded from data/stage_palettes.asm
 *
 * GBC RGB555 format: r | (g << 5) | (b << 10), components 0-31.
 * Each stage has 8 BG + 8 OBJ palettes, 4 colors each.
 */

#include "renderer/stage_palettes.h"
#include "game/constants.h"

/*
 * Red Field Top palettes (from StageRedFieldTopPalettes at 0xdc980)
 */
static const uint16_t stage_red_field_top_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(24,31, 0), RGB(31, 0, 0), RGB( 3, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(11,25,31), RGB( 0,11,31), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,13,13), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(31, 0,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(24,31, 0), RGB(31, 0,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(13,13,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(31,13,13), RGB(31, 0, 0), RGB( 0, 0, 0),

    /* OBJ Palette 0 */
    RGB(21,21,21), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,19,22), RGB(21, 0, 0), RGB( 4, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(31, 0,31), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,22, 0), RGB(10, 4, 0),
    /* OBJ Palette 4 */
    RGB(20,20,20), RGB(18,31,18), RGB( 5,19, 0), RGB( 0, 7, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,20, 0), RGB(31,15,16), RGB( 5, 2, 0),
    /* OBJ Palette 6 */
    RGB(20,20,20), RGB( 0,31,25), RGB( 0,18,14), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,15,13), RGB(21, 0, 0), RGB( 4, 0, 0),
};

/*
 * High Scores Red Stage palettes (from HighScoresRedStagePalettes at 0xdcd80)
 */
static const uint16_t high_scores_red_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(23,23,23), RGB(14,14,14), RGB( 5, 5, 5),
    /* BG Palette 1 */
    RGB(31,31,31), RGB( 0, 0,31), RGB(31, 6, 6), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB( 0, 8,31), RGB(31, 6, 6), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB( 0,16,31), RGB(31, 6, 6), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB( 0,24,31), RGB(31, 6, 6), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB( 0,31,31), RGB(31, 6, 6), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 7 */
    RGB(31,29, 4), RGB(29,18, 0), RGB(31, 0, 0), RGB( 5, 5, 5),

    /* OBJ Palette 0 */
    RGB(31,31,31), RGB(31,31,31), RGB(31, 6, 6), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,29, 4), RGB(29,18, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(14,14,14), RGB( 5, 5, 5),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 4 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(31,31,31), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
};

/*
 * Option Menu palettes (from OptionMenuPalettes at 0xdce00)
 */
static const uint16_t option_menu_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(31,30, 9), RGB(22,21, 0), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(31,29, 0), RGB(31, 8, 0), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31,29, 0), RGB(26,18, 0), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,29, 0), RGB(22,10, 0), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),

    /* OBJ Palette 0 */
    RGB(31,31,31), RGB(31,29, 0), RGB(31, 8, 0), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,29, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(31,31,31), RGB(31,31,11), RGB(26,23, 0), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(22,22,22), RGB(11,11,11), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(31,31,31), RGB(23,23,27), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
};

/*
 * Title Screen palettes (from TitlescreenPalettes at 0xdcf80)
 */
static const uint16_t titlescreen_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(31,29, 0), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB( 0,12,26), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(23,31,24), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,29, 0), RGB( 0,12,26), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(20,20,31), RGB( 0,12,26), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(23,31,24), RGB( 0,12,26), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(20,20,31), RGB(23,31,24), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0),

    /* OBJ Palette 0 */
    RGB(20,20,20), RGB(31,31,31), RGB(31,29, 0), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,31,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(23,23,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0),
    /* OBJ Palette 4 */
    RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0),
    /* OBJ Palette 5 */
    RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0),
    /* OBJ Palette 6 */
    RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0),
    /* OBJ Palette 7 */
    RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0), RGB( 0, 6, 0),
};

/*
 * Copyright Screen palettes (from CopyrightScreenPalettes at 0xdd000)
 */
static const uint16_t copyright_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(22,22,22), RGB(11,11,11), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),

    /* OBJ Palette 0 */
    RGB(31,31,31), RGB(31,31,31), RGB(22,22,22), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,31,31), RGB(11,11,11), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 4 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
};

/*
 * Field Select Screen palettes (from FieldSelectScreenPalettes at 0xdd100)
 */
static const uint16_t field_select_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(31,20, 0), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB( 0,22,31), RGB( 0, 0,31), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31, 0, 0), RGB( 0,25, 0), RGB( 0, 0, 0),
    /* BG Palette 3 - unused (magenta) */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* BG Palette 4 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* BG Palette 5 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* BG Palette 6 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* BG Palette 7 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),

    /* OBJ Palette 0 */
    RGB(10,10,10), RGB(31,31,31), RGB(21,21,21), RGB( 0, 0, 0),
    /* OBJ Palette 1 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* OBJ Palette 2 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* OBJ Palette 3 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* OBJ Palette 4 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* OBJ Palette 5 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* OBJ Palette 6 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
    /* OBJ Palette 7 - unused */
    RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31), RGB(31, 0,31),
};

/*
 * Red Field Bottom palettes (from StageRedFieldBottomPalettes at 0xdca80)
 */
static const uint16_t stage_red_field_bottom_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(24,31, 0), RGB(31, 0, 0), RGB( 3, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(11,25,31), RGB( 0,11,31), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,13,13), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(31, 0,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(29,30,31), RGB(27,20,10), RGB( 2,16, 1), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(29,30,31), RGB( 5,17,31), RGB(26, 3, 1), RGB( 0, 0, 0),

    /* OBJ Palette 0 */
    RGB(21,21,21), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(21,21,21), RGB(27,21, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(21,21,21), RGB(31,31,31), RGB(21,21,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(21,21,21), RGB(31,28, 0), RGB(29, 0, 0), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(20,20,20), RGB( 0,31,25), RGB( 0,18,14), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,30,16), RGB(27,24, 8), RGB(23,19, 3),
};

/*
 * Gengar Bonus stage palettes (from GengarBonusPalettes at 0xdd080)
 */
static const uint16_t stage_gengar_bonus_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(28,31, 4), RGB( 8,14,31), RGB( 4, 5,15), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(13,13,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),

    /* OBJ Palette 0 */
    RGB(20,20,20), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(21,21,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(20,20,20), RGB(31,31,31), RGB(29, 0,31), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(20,20,20), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
};

/*
 * Mewtwo Bonus palettes (from MewtwoBonusStagePalettes at 0xdcf00)
 */
static const uint16_t stage_mewtwo_bonus_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(15,15,21), RGB( 6, 6,11), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(16, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 0 */
    RGB(20,20,20), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(24,19, 0), RGB(13, 8, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(21,21,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(20,20,20), RGB(31,25,31), RGB(31, 0,31), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(20,20,20), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
};

/*
 * Meowth Bonus palettes (from MeowthBonusPalettes at 0xdcc80)
 */
static const uint16_t stage_meowth_bonus_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(31,16, 0), RGB(15, 7, 0), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(16, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),

    /* OBJ Palette 0 */
    RGB(20,20,20), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,26,16), RGB(25, 9, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(21,21,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31, 0), RGB(23, 7, 0), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(20,20,20), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(20,20,20), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
};

/*
 * Diglett Bonus palettes (from DiglettBonusPalettes at 0xdca00)
 */
static const uint16_t stage_diglett_bonus_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(10,24,20), RGB( 5,13,10), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(16, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,18, 8), RGB(27, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),

    /* OBJ Palette 0 */
    RGB(20,20,20), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,18, 8), RGB(27, 0, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(21,21,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(20,20,20), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(20,20,20), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
};

/*
 * Seel Bonus palettes (from SeelBonusPalettes at 0xdc880)
 */
static const uint16_t stage_seel_bonus_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(30,24, 4), RGB(27, 7, 0), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(16, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 0 */
    RGB(20,20,20), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(20,20,26), RGB(31,11,10), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(21,21,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(20,20,26), RGB(11,11,20), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(20,20,20), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(20,20,20), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
};

/*
 * Blue Field Top palettes (from StageBlueFieldTopPalettes at 0xdcb00)
 */
static const uint16_t stage_blue_field_top_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(11,25,31), RGB( 0,11,31), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB( 4,23,13), RGB( 0,13, 4), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,29, 0), RGB(15, 8, 0), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(16, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(13,13,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),

    /* OBJ Palette 0 */
    RGB(21,21,21), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,13,15), RGB(23, 4, 6), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(21,21,21), RGB(31,31,31), RGB(31,26, 0), RGB(10, 6, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(24,22,26), RGB(12,10,14), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(21,21,21), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(21,21,21), RGB( 0,31,25), RGB( 0,18,14), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
};

/*
 * Blue Field Bottom palettes (from StageBlueFieldBottomPalettes at 0xdcb80)
 */
static const uint16_t stage_blue_field_bottom_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(13,20,31), RGB(31, 4, 4), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(11,25,31), RGB( 0,11,31), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB( 4,23,13), RGB( 0,13, 4), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,29, 0), RGB(15, 8, 0), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(16, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(20,20,20), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(15,20,31), RGB( 7,11,21), RGB( 0, 0, 0),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(27,20,10), RGB(24, 7, 5), RGB( 0, 0, 0),

    /* OBJ Palette 0 */
    RGB(21,21,21), RGB(31,31,31), RGB(31, 5, 4), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(21,21,21), RGB(27,21, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(21,21,21), RGB(31,31,31), RGB(21,21,27), RGB( 0, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 4 */
    RGB(21,21,21), RGB(31,28, 0), RGB(29, 0, 0), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB( 8, 8, 8), RGB( 0, 0, 0),
    /* OBJ Palette 6 */
    RGB(21,21,21), RGB( 0,31,25), RGB( 0,18,14), RGB( 0, 0, 0),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,30,16), RGB(27,24, 8), RGB(23,19, 3),
};

const uint16_t *get_stage_bg_palettes(uint8_t stage_id) {
    switch (stage_id) {
        case STAGE_RED_FIELD_BOTTOM:
            return &stage_red_field_bottom_palettes[0];
        case STAGE_GENGAR_BONUS:
            return &stage_gengar_bonus_palettes[0];
        case STAGE_MEWTWO_BONUS:
            return &stage_mewtwo_bonus_palettes[0];
        case STAGE_MEOWTH_BONUS:
            return &stage_meowth_bonus_palettes[0];
        case STAGE_DIGLETT_BONUS:
            return &stage_diglett_bonus_palettes[0];
        case STAGE_SEEL_BONUS:
            return &stage_seel_bonus_palettes[0];
        case STAGE_BLUE_FIELD_TOP:
            return &stage_blue_field_top_palettes[0];
        case STAGE_BLUE_FIELD_BOTTOM:
            return &stage_blue_field_bottom_palettes[0];
        case STAGE_RED_FIELD_TOP:
        default:
            return &stage_red_field_top_palettes[0];
    }
}

const uint16_t *get_stage_obj_palettes(uint8_t stage_id) {
    switch (stage_id) {
        case STAGE_RED_FIELD_BOTTOM:
            return &stage_red_field_bottom_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_GENGAR_BONUS:
            return &stage_gengar_bonus_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_MEWTWO_BONUS:
            return &stage_mewtwo_bonus_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_MEOWTH_BONUS:
            return &stage_meowth_bonus_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_DIGLETT_BONUS:
            return &stage_diglett_bonus_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_SEEL_BONUS:
            return &stage_seel_bonus_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_BLUE_FIELD_TOP:
            return &stage_blue_field_top_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_BLUE_FIELD_BOTTOM:
            return &stage_blue_field_bottom_palettes[PALETTE_COLORS_PER_SET];
        case STAGE_RED_FIELD_TOP:
        default:
            return &stage_red_field_top_palettes[PALETTE_COLORS_PER_SET];
    }
}

/*
 * High Scores Blue Stage palettes (from HighScoresBlueStagePalettes at 0xdcd00)
 */
static const uint16_t high_scores_blue_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB(23,23,23), RGB(14,14,14), RGB( 5, 5, 5),
    /* BG Palette 1 */
    RGB(31,31,31), RGB(31, 0, 0), RGB( 9, 9,27), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31, 8, 0), RGB( 9, 9,27), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,16, 0), RGB( 9, 9,27), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(31,24, 0), RGB( 9, 9,27), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(31,31, 0), RGB( 9, 9,27), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 7 */
    RGB(31,29, 4), RGB(29,18, 0), RGB(31, 0, 0), RGB( 5, 5, 5),

    /* OBJ Palette 0 */
    RGB(31,31,31), RGB(31,31,31), RGB( 9, 9,27), RGB( 0, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,29, 4), RGB(29,18, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(20,20,20), RGB(31,31,31), RGB(14,14,14), RGB( 5, 5, 5),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 4 */
    RGB(31,31,31), RGB(31, 0, 0), RGB(31,31,31), RGB( 0, 0, 0),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
};

/*
 * Pokedex palettes (from PokedexPalettes at 0xdce80)
 */
static const uint16_t pokedex_palettes[PALETTE_TOTAL_COLORS] = {
    /* BG Palette 0 */
    RGB(31,31,31), RGB( 7,27,27), RGB( 0,22, 0), RGB( 0, 0, 0),
    /* BG Palette 1 */
    RGB(31,31,31), RGB( 7,27,27), RGB( 0,22, 0), RGB( 0, 0, 0),
    /* BG Palette 2 */
    RGB(31,31,31), RGB(31,19,19), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 3 */
    RGB(31,31,31), RGB(31,29, 0), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 4 */
    RGB(31,31,31), RGB(23,23,31), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* BG Palette 5 */
    RGB(31,31,31), RGB(21,21,21), RGB(11,11,11), RGB( 0, 0, 0),
    /* BG Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* BG Palette 7 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),

    /* OBJ Palette 0 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,19,19), RGB(31, 0, 0),
    /* OBJ Palette 1 */
    RGB(31,31,31), RGB(31,29, 0), RGB(31, 0, 0), RGB( 0, 0, 0),
    /* OBJ Palette 2 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,29, 0), RGB(31, 0, 0),
    /* OBJ Palette 3 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 4 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 5 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 6 */
    RGB(31,31,31), RGB(31,31,31), RGB(31,31,31), RGB(31,31,31),
    /* OBJ Palette 7 */
    RGB(31,31,31), RGB(31,19,19), RGB(31, 0, 0), RGB( 0, 0, 0),
};

/* Helper: get palettes for a specific high scores stage (0=Red, 1=Blue) */
const uint16_t *get_high_scores_bg_palettes(uint8_t stage) {
    return stage ? &high_scores_blue_palettes[0]
                 : &high_scores_red_palettes[0];
}
const uint16_t *get_high_scores_obj_palettes(uint8_t stage) {
    return stage ? &high_scores_blue_palettes[PALETTE_COLORS_PER_SET]
                 : &high_scores_red_palettes[PALETTE_COLORS_PER_SET];
}

const uint16_t *get_screen_bg_palettes(uint8_t screen_id) {
    switch (screen_id) {
        case SCREEN_ERASE_ALL_DATA:
            return &high_scores_red_palettes[0];  /* ASM: HighScoresRedStagePalettes */
        case SCREEN_COPYRIGHT:
            return &copyright_palettes[0];
        case SCREEN_TITLESCREEN:
            return &titlescreen_palettes[0];
        case SCREEN_FIELD_SELECT:
            return &field_select_palettes[0];
        case SCREEN_OPTIONS:
            return &option_menu_palettes[0];
        case SCREEN_HIGH_SCORES:
            return &high_scores_red_palettes[0];
        case SCREEN_POKEDEX:
            return &pokedex_palettes[0];
        default:
            return &copyright_palettes[0];
    }
}

const uint16_t *get_screen_obj_palettes(uint8_t screen_id) {
    switch (screen_id) {
        case SCREEN_ERASE_ALL_DATA:
            return &high_scores_red_palettes[PALETTE_COLORS_PER_SET];
        case SCREEN_COPYRIGHT:
            return &copyright_palettes[PALETTE_COLORS_PER_SET];
        case SCREEN_TITLESCREEN:
            return &titlescreen_palettes[PALETTE_COLORS_PER_SET];
        case SCREEN_FIELD_SELECT:
            return &field_select_palettes[PALETTE_COLORS_PER_SET];
        case SCREEN_OPTIONS:
            return &option_menu_palettes[PALETTE_COLORS_PER_SET];
        case SCREEN_HIGH_SCORES:
            return &high_scores_red_palettes[PALETTE_COLORS_PER_SET];
        case SCREEN_POKEDEX:
            return &pokedex_palettes[PALETTE_COLORS_PER_SET];
        default:
            return &copyright_palettes[PALETTE_COLORS_PER_SET];
    }
}
