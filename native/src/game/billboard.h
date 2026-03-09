/*
 * Billboard System
 *
 * Translated from engine/pinball_game/billboard.asm (0xf178) and
 * engine/pinball_game/billboard_tiledata.asm (0x30253).
 *
 * The billboard is a 6x4 tile area on the bottom-half stages displaying
 * contextual images: current map, slot rewards, bonus stage destinations.
 *
 * Tile data: 24 tiles ($180 bytes) at vTilesSH tile $10 (VRAM $8900).
 * Tilemap: signed tile IDs $90-$A7 at bg_map offsets $87,$A7,$C7,$E7.
 *
 * Two loading mechanisms:
 *   1. LoadBillboardPicture/Off (0xf178/0xf196): direct VRAM copy for
 *      slot roulette and ChooseInitialMap (59 picture IDs, on/off pairs).
 *   2. LoadBillboardTileData (0x30256): queued tile+palette loading for
 *      stage loads and mode transitions (27 tile data indices).
 */

#ifndef BILLBOARD_H
#define BILLBOARD_H

#include "game_state.h"

/* Number of billboard picture IDs (0-58) */
#define NUM_BILLBOARD_PICS     59

/* First map billboard picture ID (IDs 41-58 = 18 maps) */
#define BILLBOARD_PALLET_TOWN_PIC  41

/* Number of billboard tile data indices (0-26) */
#define NUM_BILLBOARD_TILE_DATA  27

/* Billboard tile data size: 24 tiles x 16 bytes = $180 */
#define BILLBOARD_TILE_SIZE    0x180

/* Billboard VRAM destination: vTilesSH tile $10 = $8900 */
#define BILLBOARD_VRAM_DEST    0x8900

/*---------------------------------------------------------------------------
 * LoadBillboardPicture (0xf178)
 * Loads the "on" (illuminated) version of a billboard picture into VRAM.
 * pic_id: 0-58 (see BILLBOARD_* constants in constants.h)
 *---------------------------------------------------------------------------*/
void load_billboard_picture(GameState *state, uint8_t pic_id);

/*---------------------------------------------------------------------------
 * LoadBillboardOffPicture (0xf196)
 * Loads the "off" (dimly-lit) version of a billboard picture into VRAM.
 * For maps (IDs 41-58), falls back to the "on" version.
 *---------------------------------------------------------------------------*/
void load_billboard_off_picture(GameState *state, uint8_t pic_id);

/*---------------------------------------------------------------------------
 * LoadBillboardTileData (0x30256)
 * Loads billboard tile data by queued index (0-26).
 * Also sets palette attributes for the billboard area.
 *
 * Index mapping:
 *   0-17: Map locations (Pallet Town through Indigo Plateau)
 *   18:   HurryUp2 (map move, direction 0)
 *   19:   HurryUp (map move, direction 1)
 *   20:   GoToNext (map move, slot open)
 *   21-25: Bonus stages (Gengar, Mewtwo, Meowth, Diglett, Seel)
 *   26:   Slot (4 CAVE lights lit)
 *---------------------------------------------------------------------------*/
void load_billboard_tile_data(GameState *state, uint8_t index);

/*---------------------------------------------------------------------------
 * LoadMapBillboardTileData (0x30253)
 * Loads current map's billboard tile data.
 * Equivalent to load_billboard_tile_data(state, state->current_map).
 *---------------------------------------------------------------------------*/
void load_map_billboard_tile_data(GameState *state);

/*---------------------------------------------------------------------------
 * LoadGreyBillboardPaletteData (0xf269)
 * Sets BG palette 6 to greyscale and all billboard tile attributes to
 * palette 6. Used at the start of slot roulette.
 *---------------------------------------------------------------------------*/
void load_grey_billboard_palette(GameState *state);

/*---------------------------------------------------------------------------
 * Func_f2a0: Load colored billboard palette for slot roulette result display.
 * After the grey roulette phase, this restores per-reward colored palette
 * data into BG palettes 6 and 7.
 *---------------------------------------------------------------------------*/
void load_colored_billboard_palette(GameState *state, uint8_t pic_id);

/*---------------------------------------------------------------------------
 * Write/clear billboard tilemap entries in bg_map.
 * load: writes $90-$A7 to the 6x4 billboard area.
 * clear: writes $80 (blank) to all 24 positions.
 *---------------------------------------------------------------------------*/
void load_billboard_tilemap(GameState *state);
void clear_billboard_tilemap(GameState *state);

/*---------------------------------------------------------------------------
 * Free all cached billboard tile data. Call on game shutdown.
 *---------------------------------------------------------------------------*/
void billboard_free_cache(void);

#endif /* BILLBOARD_H */
