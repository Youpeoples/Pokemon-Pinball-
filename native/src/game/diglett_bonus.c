/*
 * Diglett Bonus Stage (Stage ID $0D)
 *
 * Whack-a-mole style minigame: 31 digletts pop up from holes, hit them
 * all to spawn Dugtrio boss. Hit Dugtrio 3 times to clear the stage.
 * Timer: 2:00.
 *
 * Translated from engine/pinball_game/:
 *   stage_init/init_diglett_bonus.asm (0x199f2)
 *   ball_init/ball_init_diglett_bonus.asm (0x19a38)
 *   ball_loss/ball_loss_diglett_bonus.asm (0xe056)
 *   object_collision/diglett_bonus_object_collision.asm (0x19ab3)
 *   object_collision/diglett_bonus_resolve_collision.asm (0x19b88)
 *   draw_sprites/draw_diglett_bonus_sprites.asm (0x19a88)
 *   load_stage_data/load_diglett_bonus.asm (0x19a6a)
 */

#include "game/diglett_bonus.h"
#include "game/sprite_data.h"
#include "game/animation.h"
#include "game/timer.h"
#include "game/score.h"
#include "game/collision.h"
#include "game/draw_red_field.h"
#include "game/constants.h"
#include "audio/audio.h"
#include "renderer/tile_loader.h"
#include "renderer/stage_assets.h"
#include "renderer/stage_palettes.h"
#include "renderer/vram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Externals */
extern void draw_flipper_sprites(GameState *state);
extern void draw_pinball(GameState *state);
#include "game/rng.h"

/*=============================================================================
 * Constants
 *===========================================================================*/
#define NUM_DIGLETTS             31
#define DIGLETT_INITIALIZE_DELAY 0x88
#define BALL_X_GATE_THRESHOLD    0x8A

/* 4-byte BCD scores (little-endian) */
static const uint8_t DIGLETT_HIT_SCORE[4]  = {0x00, 0x00, 0x10, 0x00};  /* 100,000 */
static const uint8_t DUGTRIO_HIT_SCORE[4]  = {0x00, 0x00, 0x00, 0x50};  /* 5,000,000 */

/* Diglett bonus has NO timer (unlike other bonus stages) */

/*=============================================================================
 * Diglett Position Data Tables
 *===========================================================================*/

/* BG map offsets for each of the 31 digletts (offset from $9800 / bg_map base).
 * Each diglett occupies 2x2 tiles: row 0 at offset, row 1 at offset+0x20.
 * Extracted from data/queued_tiledata/diglett_bonus/digletts.asm */
static const uint16_t diglett_bg_map_offsets[NUM_DIGLETTS] = {
    0x061, 0x0A1, 0x0E1, 0x083, 0x0C3, 0x103, 0x065, 0x0A5,
    0x0E5, 0x125, 0x087, 0x0C7, 0x107, 0x147, 0x0A9, 0x0E9,
    0x129, 0x08B, 0x0CB, 0x10B, 0x14B, 0x06D, 0x0AD, 0x0ED,
    0x12D, 0x08F, 0x0CF, 0x10F, 0x071, 0x0B1, 0x0F1
};

/* Collision map data for each diglett (Data_19e13).
 * Format: { collision_map_offset, row0_attr0, row0_attr1, row1_attr0, row1_attr1 } */
static const uint16_t diglett_collision_offsets[NUM_DIGLETTS] = {
    0x0A1, 0x0E1, 0x121, 0x0C3, 0x103, 0x143, 0x0A5, 0x0E5,
    0x125, 0x165, 0x0C7, 0x107, 0x147, 0x187, 0x0E9, 0x129,
    0x169, 0x0CB, 0x10B, 0x14B, 0x18B, 0x0AD, 0x0ED, 0x12D,
    0x16D, 0x0CF, 0x10F, 0x14F, 0x0B1, 0x0F1, 0x131
};

/* Normal collision attributes per diglett (4 bytes: row0_a, row0_b, row1_a, row1_b).
 * Digletts 17-30 mirror digletts 0-13's attributes. */
static const uint8_t diglett_collision_attrs[NUM_DIGLETTS][4] = {
    {0x19, 0x19, 0x1A, 0x1B},  /* 0 */
    {0x1C, 0x1C, 0x1D, 0x1E},  /* 1 */
    {0x1F, 0x1F, 0x20, 0x21},  /* 2 */
    {0x22, 0x22, 0x23, 0x24},  /* 3 */
    {0x25, 0x25, 0x26, 0x27},  /* 4 */
    {0x28, 0x28, 0x29, 0x2A},  /* 5 */
    {0x2B, 0x2B, 0x2C, 0x2D},  /* 6 */
    {0x2E, 0x2E, 0x2F, 0x30},  /* 7 */
    {0x31, 0x31, 0x32, 0x33},  /* 8 */
    {0x34, 0x34, 0x35, 0x36},  /* 9 */
    {0x37, 0x37, 0x38, 0x39},  /* 10 */
    {0x3A, 0x3A, 0x3B, 0x3C},  /* 11 */
    {0x3D, 0x3D, 0x3E, 0x3F},  /* 12 */
    {0x40, 0x40, 0x41, 0x42},  /* 13 */
    {0x43, 0x43, 0x44, 0x45},  /* 14 */
    {0x46, 0x46, 0x47, 0x48},  /* 15 */
    {0x49, 0x49, 0x4A, 0x4B},  /* 16 */
    {0x19, 0x19, 0x1A, 0x1B},  /* 17 (=0) */
    {0x1C, 0x1C, 0x1D, 0x1E},  /* 18 (=1) */
    {0x1F, 0x1F, 0x20, 0x21},  /* 19 (=2) */
    {0x22, 0x22, 0x23, 0x24},  /* 20 (=3) */
    {0x25, 0x25, 0x26, 0x27},  /* 21 (=4) */
    {0x28, 0x28, 0x29, 0x2A},  /* 22 (=5) */
    {0x2B, 0x2B, 0x2C, 0x2D},  /* 23 (=6) */
    {0x2E, 0x2E, 0x2F, 0x30},  /* 24 (=7) */
    {0x31, 0x31, 0x32, 0x33},  /* 25 (=8) */
    {0x34, 0x34, 0x35, 0x36},  /* 26 (=9) */
    {0x37, 0x37, 0x38, 0x39},  /* 27 (=10) */
    {0x3A, 0x3A, 0x3B, 0x3C},  /* 28 (=11) */
    {0x3D, 0x3D, 0x3E, 0x3F},  /* 29 (=12) */
    {0x40, 0x40, 0x41, 0x42},  /* 30 (=13) */
};

/* Tile IDs per diglett state (2x2 tiles: top-left, top-right, bottom-left, bottom-right).
 * Index 0 = state 1 (hiding), 1-4 = states 2-5 (active frames), 5 = state 6+ (hit) */
static const uint8_t diglett_tiles[6][4] = {
    {0x45, 0x46, 0x47, 0x48},  /* State 1 (hiding) */
    {0x35, 0x36, 0x37, 0x38},  /* State 2 */
    {0x39, 0x3A, 0x3B, 0x3C},  /* State 3 */
    {0x3D, 0x3E, 0x3F, 0x40},  /* State 4 */
    {0x41, 0x42, 0x43, 0x44},  /* State 5 */
    {0x45, 0x46, 0x47, 0x48},  /* State 6+ (hit — same as hiding) */
};

/* Collision attribute to diglett base number (1-17).
 * Indexed by (collision_attr - 0x19). Each group of 3 maps to one diglett.
 * Data_19b18 (0x19B18) */
static const uint8_t collision_attr_to_diglett[51] = {
    1,1,1, 2,2,2, 3,3,3, 4,4,4, 5,5,5, 6,6,6, 7,7,7,
    8,8,8, 9,9,9, 10,10,10, 11,11,11, 12,12,12, 13,13,13,
    14,14,14, 15,15,15, 16,16,16, 17,17,17
};

/*=============================================================================
 * Order Tables (DiglettInitializeOrder / DiglettUpdateOrder)
 *===========================================================================*/

/* DiglettInitializeOrder (0x19ED1) — order digletts pop up during initialization */
static const uint8_t diglett_init_order[NUM_DIGLETTS] = {
    0x00, 0x1C, 0x01, 0x1D, 0x03, 0x19, 0x06, 0x15,
    0x02, 0x1E, 0x04, 0x1A, 0x07, 0x16, 0x0A, 0x11,
    0x05, 0x1B, 0x08, 0x17, 0x0B, 0x12, 0x0E, 0x09,
    0x18, 0x0C, 0x13, 0x0F, 0x0D, 0x14, 0x10
};

/* DiglettUpdateOrder (0x19EF3) — order digletts are updated (4 per frame) */
static const uint8_t diglett_update_order[NUM_DIGLETTS] = {
    0x00, 0x11, 0x03, 0x14, 0x06, 0x17, 0x09, 0x1A,
    0x0C, 0x1D, 0x0F, 0x01, 0x12, 0x04, 0x15, 0x07,
    0x18, 0x0A, 0x1B, 0x0D, 0x1E, 0x10, 0x02, 0x13,
    0x05, 0x16, 0x08, 0x19, 0x0B, 0x1C, 0x0E
};

/*=============================================================================
 * Dugtrio Sprite Frame Data (from data/sprite_frames.asm)
 * Format: {Y, X, TileID, Attrs}... terminated by 0x80
 * All entries use attr $11 (palette 1, bank 1)
 *===========================================================================*/

static const uint8_t dugtrio_health3_f0[] = {
    0x20,0x20,0x9E,0x11, 0x20,0x18,0x9C,0x11,
    0x20,0x10,0x9A,0x11, 0x20,0x08,0x98,0x11,
    0x10,0x20,0x96,0x11, 0x10,0x18,0x94,0x11,
    0x10,0x10,0x92,0x11, 0x10,0x08,0x90,0x11, 0x80
};
static const uint8_t dugtrio_health3_f1[] = {
    0x20,0x20,0x1E,0x11, 0x20,0x18,0x1C,0x11,
    0x20,0x10,0x1A,0x11, 0x20,0x08,0xA8,0x11,
    0x10,0x20,0xA6,0x11, 0x10,0x18,0xA4,0x11,
    0x10,0x10,0xA2,0x11, 0x10,0x08,0xA0,0x11, 0x80
};
static const uint8_t dugtrio_health3_f2[] = {
    0x20,0x20,0x9E,0x11, 0x10,0x20,0x96,0x11,
    0x20,0x10,0x1A,0x11, 0x20,0x08,0xA8,0x11,
    0x10,0x08,0xA0,0x11, 0x20,0x18,0x24,0x11,
    0x10,0x18,0x22,0x11, 0x10,0x10,0x20,0x11, 0x80
};
static const uint8_t dugtrio_health3_hit[] = {
    0x10,0x08,0xA0,0x11, 0x20,0x20,0x32,0x11,
    0x20,0x18,0x30,0x11, 0x20,0x10,0x2E,0x11,
    0x20,0x08,0x2C,0x11, 0x10,0x20,0x2A,0x11,
    0x10,0x18,0x28,0x11, 0x10,0x10,0x26,0x11, 0x80
};
static const uint8_t dugtrio_health2_f0[] = {
    0x20,0x18,0xAA,0x11, 0x20,0x10,0x7E,0x11,
    0x10,0x18,0x7C,0x11, 0x10,0x10,0x7A,0x11,
    0x20,0x20,0x9E,0x11, 0x20,0x08,0x98,0x11,
    0x10,0x20,0x96,0x11, 0x10,0x08,0x90,0x11, 0x80
};
static const uint8_t dugtrio_health2_f1[] = {
    0x20,0x18,0xB2,0x11, 0x20,0x10,0xB0,0x11,
    0x10,0x18,0xAE,0x11, 0x10,0x10,0xAC,0x11,
    0x20,0x20,0x1E,0x11, 0x20,0x08,0xA8,0x11,
    0x10,0x20,0xA6,0x11, 0x10,0x08,0xA0,0x11, 0x80
};
static const uint8_t dugtrio_health2_f2[] = {
    0x20,0x10,0xB0,0x11, 0x10,0x10,0xAC,0x11,
    0x20,0x18,0xAA,0x11, 0x10,0x18,0x7C,0x11,
    0x20,0x20,0x9E,0x11, 0x10,0x20,0x96,0x11,
    0x20,0x08,0xA8,0x11, 0x10,0x08,0xA0,0x11, 0x80
};
static const uint8_t dugtrio_health2_hit[] = {
    0x20,0x18,0xBA,0x11, 0x20,0x10,0xB8,0x11,
    0x10,0x18,0xB6,0x11, 0x10,0x10,0xB4,0x11,
    0x10,0x08,0xA0,0x11, 0x20,0x20,0x32,0x11,
    0x20,0x08,0x2C,0x11, 0x10,0x20,0x2A,0x11, 0x80
};
static const uint8_t dugtrio_health1_f0[] = {
    0x20,0x20,0xC2,0x11, 0x20,0x18,0xC0,0x11,
    0x10,0x20,0xBE,0x11, 0x10,0x18,0xBC,0x11,
    0x20,0x10,0x7E,0x11, 0x10,0x10,0x7A,0x11,
    0x20,0x08,0x98,0x11, 0x10,0x08,0x90,0x11, 0x80
};
static const uint8_t dugtrio_health1_f1[] = {
    0x20,0x10,0xB0,0x11, 0x10,0x10,0xAC,0x11,
    0x20,0x08,0xA8,0x11, 0x10,0x08,0xA0,0x11,
    0x20,0x20,0xC2,0x11, 0x20,0x18,0xC0,0x11,
    0x10,0x20,0xBE,0x11, 0x10,0x18,0xBC,0x11, 0x80
};
static const uint8_t dugtrio_health1_f2[] = {
    0x20,0x10,0xB0,0x11, 0x10,0x10,0xAC,0x11,
    0x20,0x08,0xA8,0x11, 0x10,0x08,0xA0,0x11,
    0x20,0x20,0xC2,0x11, 0x20,0x18,0xC0,0x11,
    0x10,0x20,0xBE,0x11, 0x10,0x18,0xBC,0x11, 0x80
};
static const uint8_t dugtrio_health1_hit[] = {
    0x20,0x20,0xCA,0x11, 0x20,0x18,0xC8,0x11,
    0x10,0x20,0xC6,0x11, 0x10,0x18,0xC4,0x11,
    0x20,0x10,0xB8,0x11, 0x10,0x10,0xB4,0x11,
    0x10,0x08,0xA0,0x11, 0x20,0x08,0x2C,0x11, 0x80
};
static const uint8_t dugtrio_dropped[] = {
    0x20,0x08,0x38,0x11, 0x10,0x18,0x36,0x11,
    0x10,0x10,0x34,0x11, 0x20,0x20,0x3E,0x11,
    0x20,0x18,0x3C,0x11, 0x20,0x10,0x3A,0x11, 0x80
};
static const uint8_t dugtrio_defeated[] = {
    0x20,0x10,0xCE,0x11, 0x20,0x08,0xCC,0x11,
    0x20,0x20,0xC2,0x11, 0x20,0x18,0xC0,0x11,
    0x10,0x20,0xBE,0x11, 0x10,0x18,0xBC,0x11,
    0x10,0x10,0x7A,0x11, 0x10,0x08,0x90,0x11, 0x80
};

/* DugtrioSpriteIds lookup: animation frame index → sprite data pointer.
 * Index $FF (bit 7 set) = don't draw. */
static const uint8_t *dugtrio_sprite_frames[] = {
    dugtrio_health3_f0,   /* 0x00 */
    dugtrio_health3_f1,   /* 0x01 */
    dugtrio_health3_f2,   /* 0x02 */
    dugtrio_health3_hit,  /* 0x03 */
    dugtrio_health2_f0,   /* 0x04 */
    dugtrio_health2_f1,   /* 0x05 */
    dugtrio_health2_f2,   /* 0x06 */
    dugtrio_health2_hit,  /* 0x07 */
    dugtrio_health1_f0,   /* 0x08 */
    dugtrio_health1_f1,   /* 0x09 */
    dugtrio_health1_f2,   /* 0x0A */
    dugtrio_health1_hit,  /* 0x0B */
    dugtrio_dropped,      /* 0x0C */
    dugtrio_defeated,     /* 0x0D */
};

/*=============================================================================
 * Dugtrio Animation Data
 * Format: [duration, sprite_frame_index] pairs, terminated by 0x00
 *===========================================================================*/
static const uint8_t anim_dugtrio_dropped[]    = { 0x01, 0x0C, 0x00 };
static const uint8_t anim_dugtrio_health3[]    = { 0x0E, 0x00, 0x0E, 0x01, 0x0E, 0x02, 0x00 };
static const uint8_t anim_dugtrio_health3_hit[]= { 0x0D, 0x03, 0x00 };
static const uint8_t anim_dugtrio_health2[]    = { 0x0E, 0x04, 0x0E, 0x05, 0x0E, 0x06, 0x00 };
static const uint8_t anim_dugtrio_health2_hit[]= { 0x0D, 0x07, 0x00 };
static const uint8_t anim_dugtrio_health1[]    = { 0x0E, 0x08, 0x0E, 0x09, 0x0E, 0x0A, 0x00 };
static const uint8_t anim_dugtrio_health1_hit[]= { 0x0D, 0x0B, 0x00 };
static const uint8_t anim_dugtrio_defeated[]   = { 0x01, 0x0D, 0x40, 0x0D, 0x00 };

/* AnimationDataPointers_Dugtrio (0x1AC62) — indexed by wDugrioState */
static const uint8_t *dugtrio_anim_data[8] = {
    anim_dugtrio_dropped,       /* state 0 */
    anim_dugtrio_health3,       /* state 1 */
    anim_dugtrio_health3_hit,   /* state 2 */
    anim_dugtrio_health2,       /* state 3 */
    anim_dugtrio_health2_hit,   /* state 4 */
    anim_dugtrio_health1,       /* state 5 */
    anim_dugtrio_health1_hit,   /* state 6 */
    anim_dugtrio_defeated,      /* state 7 */
};

/*=============================================================================
 * Gate Tile Data (GBC version from Func_19bbd)
 * Gate area: bg_map offsets $113, $133, $152, $172
 *===========================================================================*/
static const uint8_t gate_open_tiles[] = {
    /* offset $113 */ 0x4E, 0x4F,
    /* offset $133 */ 0x80, 0x4D,
    /* offset $152 */ 0x80, 0x4B, 0x4C,
    /* offset $172 */ 0x49, 0x4A
};
static const uint8_t gate_closed_tiles[] = {
    /* offset $113 */ 0x1D, 0xFB,
    /* offset $133 */ 0x1B, 0xFA,
    /* offset $152 */ 0x18, 0x19, 0xFB,
    /* offset $172 */ 0x14, 0x15
};

/*=============================================================================
 * Helper Functions
 *===========================================================================*/

/* Load 2x2 diglett tile graphics to BG map.
 * Func_19da8: state capped at 6, then look up tiles. */
static void load_diglett_tiles(GameState *state, int diglett_idx, uint8_t diglett_state) {
    if (!state->vram || diglett_idx < 0 || diglett_idx >= NUM_DIGLETTS) return;

    /* Cap state at 6 (ASM: cp 6; jr c, .ok; ld a, 6) */
    if (diglett_state >= 6) diglett_state = 6;
    if (diglett_state < 1) return;

    int tile_idx = diglett_state - 1;  /* 0-5 index into diglett_tiles */
    uint16_t offset = diglett_bg_map_offsets[diglett_idx];

    /* Write 2x2 tiles to bg_map bank 0 (tile indices) */
    state->vram->bg_map[0][offset]        = diglett_tiles[tile_idx][0];
    state->vram->bg_map[0][offset + 1]    = diglett_tiles[tile_idx][1];
    state->vram->bg_map[0][offset + 0x20] = diglett_tiles[tile_idx][2];
    state->vram->bg_map[0][offset + 0x21] = diglett_tiles[tile_idx][3];
}

/* Restore diglett collision attributes (Func_19dcd).
 * Called when a diglett pops up — makes it collidable. */
static void restore_diglett_collision(GameState *state, int diglett_idx) {
    if (diglett_idx < 0 || diglett_idx >= NUM_DIGLETTS) return;
    uint16_t offset = diglett_collision_offsets[diglett_idx];
    if (offset + 0x21 >= 0x300) return;

    state->stage_collision_map[offset]        = diglett_collision_attrs[diglett_idx][0];
    state->stage_collision_map[offset + 1]    = diglett_collision_attrs[diglett_idx][1];
    state->stage_collision_map[offset + 0x20] = diglett_collision_attrs[diglett_idx][2];
    state->stage_collision_map[offset + 0x21] = diglett_collision_attrs[diglett_idx][3];
}

/* Clear diglett collision (Func_19df0).
 * Called when a diglett is hit — writes $02 (passthrough). */
static void clear_diglett_collision(GameState *state, int diglett_idx) {
    if (diglett_idx < 0 || diglett_idx >= NUM_DIGLETTS) return;
    uint16_t offset = diglett_collision_offsets[diglett_idx];
    if (offset + 0x21 >= 0x300) return;

    state->stage_collision_map[offset]        = 0x02;
    state->stage_collision_map[offset + 1]    = 0x02;
    state->stage_collision_map[offset + 0x20] = 0x02;
    state->stage_collision_map[offset + 0x21] = 0x02;
}

/* Load gate visual tiles based on collision state (Func_19bbd) */
static void load_gate_graphics(GameState *state) {
    if (!state->vram) return;
    const uint8_t *tiles = state->stage_collision_state ? gate_closed_tiles : gate_open_tiles;

    state->vram->bg_map[0][0x113] = tiles[0];
    state->vram->bg_map[0][0x114] = tiles[1];
    state->vram->bg_map[0][0x133] = tiles[2];
    state->vram->bg_map[0][0x134] = tiles[3];
    state->vram->bg_map[0][0x152] = tiles[4];
    state->vram->bg_map[0][0x153] = tiles[5];
    state->vram->bg_map[0][0x154] = tiles[6];
    state->vram->bg_map[0][0x172] = tiles[7];
    state->vram->bg_map[0][0x173] = tiles[8];
}

/* Initialize dugtrio animation from animation data table */
static void init_dugtrio_anim(GameState *state, int anim_idx) {
    if (anim_idx < 0 || anim_idx >= 8) return;
    const uint8_t *data = dugtrio_anim_data[anim_idx];
    state->dugtrio_anim.frame_counter = data[0];
    state->dugtrio_anim.frame = data[1];
    state->dugtrio_anim.index = 0;
}

/* Update dugtrio animation. Returns true when a frame's duration expires. */
static int update_dugtrio_anim(GameState *state) {
    if (state->dugtrio_anim.frame_counter > 0) {
        state->dugtrio_anim.frame_counter--;
        if (state->dugtrio_anim.frame_counter > 0)
            return 0;
    }

    /* Frame expired — advance to next entry */
    state->dugtrio_anim.index++;
    const uint8_t *data = dugtrio_anim_data[state->dugtrio_state];
    int entry_offset = state->dugtrio_anim.index * 2;
    uint8_t duration = data[entry_offset];

    if (duration == 0) {
        /* Terminator reached — animation complete */
        return 1;
    }

    state->dugtrio_anim.frame_counter = duration;
    state->dugtrio_anim.frame = data[entry_offset + 1];
    return 1;
}

/* Write dugtrio collision area attrs (Func_1ac2c / Data_1ac4a) */
static void open_dugtrio_collision_area(GameState *state) {
    uint16_t base = 0x68;
    /* Row 0: $00 $00 $00 $00 */
    state->stage_collision_map[base]     = 0x00;
    state->stage_collision_map[base + 1] = 0x00;
    state->stage_collision_map[base + 2] = 0x00;
    state->stage_collision_map[base + 3] = 0x00;
    /* Row 1: $14 $14 $14 $14 */
    state->stage_collision_map[base + 0x20]     = 0x14;
    state->stage_collision_map[base + 0x20 + 1] = 0x14;
    state->stage_collision_map[base + 0x20 + 2] = 0x14;
    state->stage_collision_map[base + 0x20 + 3] = 0x14;
    /* Row 2: $15 $16 $17 $18 */
    state->stage_collision_map[base + 0x40]     = 0x15;
    state->stage_collision_map[base + 0x40 + 1] = 0x16;
    state->stage_collision_map[base + 0x40 + 2] = 0x17;
    state->stage_collision_map[base + 0x40 + 3] = 0x18;
}

/* Write cleared-stage dugtrio collision area (Data_1ac56) */
static void write_cleared_collision_area(GameState *state) {
    uint16_t base = 0x68;
    state->stage_collision_map[base]     = 0x50;
    state->stage_collision_map[base + 1] = 0x02;
    state->stage_collision_map[base + 2] = 0x02;
    state->stage_collision_map[base + 3] = 0x51;
    state->stage_collision_map[base + 0x20]     = 0x02;
    state->stage_collision_map[base + 0x20 + 1] = 0x02;
    state->stage_collision_map[base + 0x20 + 2] = 0x02;
    state->stage_collision_map[base + 0x20 + 3] = 0x02;
    state->stage_collision_map[base + 0x40]     = 0x02;
    state->stage_collision_map[base + 0x40 + 1] = 0x02;
    state->stage_collision_map[base + 0x40 + 2] = 0x02;
    state->stage_collision_map[base + 0x40 + 3] = 0x02;
}

/* LoadFlippersPalette — OBJ palette 2 colors 1-2 (active vs disabled) */
static void load_flippers_palette_diglett(GameState *state) {
    if (state->flippers_disabled) {
        state->obj_palettes[2].colors[1] = 0x5294;  /* grey */
        state->obj_palettes[2].colors[2] = 0x2108;  /* dark grey */
    } else {
        state->obj_palettes[2].colors[1] = 0x7FFF;  /* white (OBJ2 color 1) */
        state->obj_palettes[2].colors[2] = 0x6EB5;  /* lavender (OBJ2 color 2) */
    }
}

/*=============================================================================
 * init_diglett_bonus — InitDiglettBonusStage (0x199F2)
 *===========================================================================*/
void init_diglett_bonus(GameState *state) {
    if (state->loading_saved_game) return;

    state->stage_collision_state = 0;
    state->disable_horizontal_scroll_for_ball_start = 1;

    /* Backup ball type, reset to basic */
    state->ball_type_backup = state->ball_type;
    state->ball_type = 0;
    state->ball_size = 0;
    state->completed_bonus_stage = 0;

    /* Initialize all 31 digletts to state 1 (hiding) */
    for (int i = 0; i < NUM_DIGLETTS; i++)
        state->diglett_states[i] = 1;

    /* Init dugtrio animation to "dropped" */
    state->dugtrio_anim.frame_counter = 1;
    state->dugtrio_anim.frame = 0x0C;  /* DUGTRIOSPRITE_DROPPED */
    state->dugtrio_anim.index = 0;
    state->dugtrio_state = 0;

    /* Play diglett bonus music (Bank $11, MUSIC_WHACK_DIGLETT = $01) */
    audio_play_music(state->audio, 0x11, 0x01);
}

/*=============================================================================
 * init_ball_diglett_bonus — InitBallDiglettBonusStage (0x19A38)
 *===========================================================================*/
void init_ball_diglett_bonus(GameState *state) {
    /* Ball starting position and velocity */
    state->ball_x_pos = 0xA600;
    state->ball_y_pos = 0x5600;
    state->ball_x_velocity = 0x0040;
    state->ball_y_velocity = 0;
    state->scx = 0;  /* ASM: ld [wSCX], a — must set wSCX, not hSCX */

    /* Reset gate */
    state->stage_collision_state = 0;
    state->diglett_bonus_closed_gate = 0;

    /* Reset all non-zero digletts to state 1 (hiding) */
    for (int i = 0; i < NUM_DIGLETTS; i++) {
        if (state->diglett_states[i] != 0)
            state->diglett_states[i] = 1;
    }

    /* Reset init/update state */
    state->current_diglett = 0;
    state->digletts_initialized_flag = 0;
    state->wd765 = 0;
}

/*=============================================================================
 * handle_ball_loss_diglett_bonus — HandleBallLossDiglettBonus (0xE056)
 *===========================================================================*/
void handle_ball_loss_diglett_bonus(GameState *state) {
    if (state->current_stage_backup == state->current_stage) return;

    audio_play_sfx(state->audio, 0x00, 0x0B);
    state->going_to_bonus_stage = 0;
    state->returning_from_bonus_stage = 1;
    state->ball_size = 2;
    state->disable_horizontal_scroll_for_ball_start = 0;

    if (state->completed_bonus_stage) return;

    /* Show "END DIGLETT STAGE" text */
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    /* scrolling_text_normal 1, 20, 0, 19 → { 5, 0x54, 0x41, 20, 0, 58 } */
    const uint8_t header[6] = { 5, 0x54, 0x41, 20, 0, 58 };
    load_scrolling_text(state, 2, header, "END DIGLETT STAGE");
}

/*=============================================================================
 * check_diglett_bonus_object_collisions (0x19AB3)
 *===========================================================================*/
void check_diglett_bonus_object_collisions(GameState *state) {
    /* ASM: CheckGameObjectCollisions (0x2720) wrapper —
     * init triggered to $FF, copy to previous after check. */
    state->triggered_game_object = 0xFF;

    /* === Check Diglett Heads (0x19ABA) === */
    if (state->triggered_game_object == 0xFF &&
        !(state->diglett_collision_trigger & 0x80) &&
        state->is_ball_colliding)
    {
        uint8_t attr = state->cur_collision_attribute;
        if (attr >= 0x19 && attr <= 0x4B) {
            int lookup = attr - 0x19;
            uint8_t diglett_num = collision_attr_to_diglett[lookup];

            /* Side detection based on ball X and diglett number */
            uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
            if (diglett_num < 0x0A) {
                if (ball_x >= 0x48)
                    diglett_num += 0x11;  /* right side */
            } else {
                if (ball_x >= 0x68)
                    diglett_num += 0x11;  /* right side */
            }

            /* Bounds check */
            if (diglett_num >= 1 && diglett_num <= NUM_DIGLETTS) {
                state->triggered_game_object_index = diglett_num;
                state->triggered_game_object = diglett_num;
                state->diglett_collision_trigger = 0;

                if (state->previous_triggered_game_object != state->triggered_game_object) {
                    state->diglett_collision_trigger = diglett_num;
                    state->diglett_collision_object = diglett_num;
                }
            }
        }
    }

    /* === Check Dugtrio (0x19B4B) === */
    if (state->triggered_game_object == 0xFF &&
        !(state->dugtrio_collision_trigger & 0x80) &&
        state->is_ball_colliding)
    {
        uint8_t attr = state->cur_collision_attribute;
        if (attr >= 0x14 && attr <= 0x18) {
            state->triggered_game_object_index = 1;
            state->triggered_game_object = 1 + NUM_DIGLETTS;  /* 0x20 = 32 */
            state->dugtrio_collision_trigger = 0;

            if (state->previous_triggered_game_object != state->triggered_game_object) {
                state->dugtrio_collision_trigger = 1;
                state->dugtrio_collision_data = 1;
            }
        }
    }

    /* ASM: CheckGameObjectCollisions (0x2726) — copy triggered to previous */
    state->previous_triggered_game_object = state->triggered_game_object;
}

/*=============================================================================
 * Diglett State Machine Update (Func_19cdd)
 *===========================================================================*/
static void update_diglett_states(GameState *state) {
    if (!state->digletts_initialized_flag) {
        /* === Initialization mode: pop up one diglett per delay tick === */
        uint16_t acc = state->diglett_init_delay_counter + DIGLETT_INITIALIZE_DELAY;
        state->diglett_init_delay_counter = (uint8_t)(acc & 0xFF);
        if (acc < 256) return;  /* no overflow = not time yet */

        /* Process one diglett from init order */
        uint8_t pos = state->current_diglett;
        if (pos >= NUM_DIGLETTS) pos = 0;
        uint8_t diglett_idx = diglett_init_order[pos];

        if (diglett_idx < NUM_DIGLETTS) {
            uint8_t ds = state->diglett_states[diglett_idx];
            if (ds == 1) {
                /* Pop up: random state 2-5 */
                uint8_t new_state = (gen_random(state) & 3) + 2;
                state->diglett_states[diglett_idx] = new_state;
                load_diglett_tiles(state, diglett_idx, new_state);
                restore_diglett_collision(state, diglett_idx);
            } else if (ds > 1) {
                /* Already active: cycle state */
                uint8_t new_state = ((ds - 1) & 3) + 2;
                state->diglett_states[diglett_idx] = new_state;
                load_diglett_tiles(state, diglett_idx, new_state);
            }
        }

        state->current_diglett++;
        if (state->current_diglett >= NUM_DIGLETTS) {
            state->digletts_initialized_flag = 1;
            state->current_diglett = 0;
        }
    } else {
        /* === Update mode: process 4 digletts per frame === */
        for (int i = 0; i < 4; i++) {
            uint8_t pos = state->current_diglett;
            if (pos >= NUM_DIGLETTS) pos = 0;
            state->current_diglett = pos;

            uint8_t diglett_idx = diglett_update_order[pos];
            if (diglett_idx < NUM_DIGLETTS) {
                uint8_t ds = state->diglett_states[diglett_idx];
                if (ds == 0) {
                    /* Empty — skip */
                } else if (ds == 1) {
                    /* Hiding — pop up with random state 2-5 */
                    uint8_t new_state = (gen_random(state) & 3) + 2;
                    state->diglett_states[diglett_idx] = new_state;
                    load_diglett_tiles(state, diglett_idx, new_state);
                    restore_diglett_collision(state, diglett_idx);
                } else if (ds >= 7) {
                    /* Hit states: decay toward 0 (ASM: ld [hl], state-1).
                     * 8→7→6→0 over 3 update cycles. */
                    state->diglett_states[diglett_idx] = ds - 1;
                } else if (ds == 6) {
                    /* Hit transition final — go to dead, show hiding tiles */
                    state->diglett_states[diglett_idx] = 0;
                    load_diglett_tiles(state, diglett_idx, 1);
                } else if (ds >= 2 && ds <= 5) {
                    /* Active — cycle through frames 2-5 */
                    uint8_t new_state = ((ds - 1) & 3) + 2;
                    state->diglett_states[diglett_idx] = new_state;
                    load_diglett_tiles(state, diglett_idx, new_state);
                }
            }

            state->current_diglett++;
            if (state->current_diglett >= NUM_DIGLETTS)
                state->current_diglett = 0;
        }
    }
}

/*=============================================================================
 * Process Diglett Hit (Func_19c52)
 *===========================================================================*/
static void process_diglett_hit(GameState *state) {
    if (state->diglett_collision_trigger == 0) return;

    state->diglett_collision_trigger = 0;

    /* Award 100,000 points */
    add_score_with_multiplier(state, DIGLETT_HIT_SCORE);

    /* SFX */
    audio_play_sfx(state->audio, 0x00, 0x35);

    /* Collision force */
    state->flipper_y_force = 0x0100;
    state->flipper_collision = 0x80;

    /* Get 0-based diglett index */
    int diglett_idx = state->diglett_collision_object - 1;
    if (diglett_idx < 0 || diglett_idx >= NUM_DIGLETTS) return;

    /* Only process if not already hit */
    if (state->diglett_states[diglett_idx] >= 6) return;

    /* Set to hit state */
    state->diglett_states[diglett_idx] = 8;
    load_diglett_tiles(state, diglett_idx, 8);  /* shows hit graphics (capped at 6) */
    clear_diglett_collision(state, diglett_idx);

    /* Check if all digletts are down */
    int down_count = 0;
    for (int i = 0; i < NUM_DIGLETTS; i++) {
        if (state->diglett_states[i] == 0 || state->diglett_states[i] >= 6)
            down_count++;
    }

    if (down_count >= NUM_DIGLETTS) {
        /* All digletts knocked down — spawn Dugtrio */
        init_dugtrio_anim(state, 1);  /* Health3 animation */
        state->dugtrio_state = 1;
        open_dugtrio_collision_area(state);
        audio_play_music(state->audio, 0x11, 0x02);  /* Bank $11, MUSIC_WHACK_DUGTRIO */
    }
}

/*=============================================================================
 * Process Dugtrio Hit (Func_1aad4)
 *===========================================================================*/
static void process_dugtrio_hit(GameState *state) {
    if (state->dugtrio_collision_trigger != 0) {
        state->dugtrio_collision_trigger = 0;

        /* Only hittable in odd states (1, 3, 5) and not defeated (7) */
        if ((state->dugtrio_state & 1) && state->dugtrio_state != 7) {
            state->dugtrio_state++;
            init_dugtrio_anim(state, state->dugtrio_state);

            /* Award 5,000,000 points */
            add_score_with_multiplier(state, DUGTRIO_HIT_SCORE);
            audio_play_sfx(state->audio, 0x00, 0x36);

            /* Rumble feedback (ASM: wRumblePattern=$33, wRumbleDuration=$08) */
            state->rumble_pattern = 0x33;
            state->rumble_duration = 0x08;

            /* Collision force */
            state->flipper_y_force = 0x0200;
            state->flipper_collision = 0x80;
        }
    }

    /* === Dugtrio Animation State Machine (Func_1ab30) === */
    if (state->dugtrio_state == 0) return;

    int frame_expired = update_dugtrio_anim(state);
    if (!frame_expired) return;

    /* Check animation index at expiry for state transitions */
    uint8_t idx = state->dugtrio_anim.index;
    const uint8_t *data = dugtrio_anim_data[state->dugtrio_state];

    /* Check if we hit the terminator (duration == 0) */
    if (data[idx * 2] != 0) return;  /* not at terminator */

    switch (state->dugtrio_state) {
    case 1: /* Health 3 idle — loop */
        if (idx == 3) init_dugtrio_anim(state, 1);
        break;
    case 2: /* Health 3 hit → Health 2 idle */
        if (idx == 1) {
            state->dugtrio_state = 3;
            init_dugtrio_anim(state, 3);
        }
        break;
    case 3: /* Health 2 idle — loop */
        if (idx == 3) init_dugtrio_anim(state, 3);
        break;
    case 4: /* Health 2 hit → Health 1 idle */
        if (idx == 1) {
            state->dugtrio_state = 5;
            init_dugtrio_anim(state, 5);
        }
        break;
    case 5: /* Health 1 idle — loop */
        if (idx == 3) init_dugtrio_anim(state, 5);
        break;
    case 6: /* Health 1 hit → Defeated */
        if (idx == 1) {
            state->dugtrio_state = 7;
            init_dugtrio_anim(state, 7);
        }
        break;
    case 7: /* Defeated animation */
        if (idx == 1) {
            /* First entry expired — stop music */
            audio_play_music(state->audio, 0x00, 0x00);
        } else if (idx == 2) {
            /* Second entry expired — stage cleared */
            init_dugtrio_anim(state, 0);  /* dropped animation */
            state->dugtrio_state = 0;
            state->next_bonus_stage = BONUS_STAGE_ORDER_GENGAR;
            state->completed_bonus_stage = 1;

            /* Show "DIGLETT STAGE CLEARED" text */
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            /* scrolling_text_normal -1, 20, 0, 21 → { 5, 0x54, 0x3F, 20, 0, 62 } */
            const uint8_t header[6] = { 5, 0x54, 0x3F, 20, 0, 62 };
            load_scrolling_text(state, 2, header, "DIGLETT STAGE CLEARED");

            audio_play_sfx(state->audio, 0x4B, 0x2A);
            state->flippers_disabled = 1;
            load_flippers_palette_diglett(state);
            write_cleared_collision_area(state);
        }
        break;
    }
}

/*=============================================================================
 * Gate Logic — TryCloseGate_DiglettBonus (0x19B92)
 *===========================================================================*/
static void try_close_gate(GameState *state) {
    if (state->diglett_bonus_closed_gate) return;

    uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
    if (ball_x >= BALL_X_GATE_THRESHOLD) return;

    state->stage_collision_state = 1;
    state->diglett_bonus_closed_gate = 1;

    /* Patch collision map — close the gate */
    state->stage_collision_map[0x153] = 0x00;
    state->stage_collision_map[0x173] = 0x00;
    state->stage_collision_map[0x193] = 0x00;
    state->stage_collision_map[0x172] = 0x05;
    state->stage_collision_map[0x192] = 0x07;

    load_gate_graphics(state);
}

/*=============================================================================
 * resolve_diglett_bonus_object_collisions (0x19B88)
 *===========================================================================*/
void resolve_diglett_bonus_object_collisions(GameState *state) {
    /* Process diglett hit + update diglett state machine */
    process_diglett_hit(state);
    update_diglett_states(state);

    /* wd765 flag: ensures dugtrio collision area is written once */
    if (state->wd765 == 0) {
        state->wd765 = 1;
        if (state->dugtrio_state != 0) {
            open_dugtrio_collision_area(state);
        }
    }

    /* Process dugtrio hit + animation */
    process_dugtrio_hit(state);

    /* Gate logic */
    try_close_gate(state);

    /* Diglett bonus has no timer — nothing here */
}

/*=============================================================================
 * draw_diglett_bonus_sprites (0x19A88)
 *===========================================================================*/
void draw_diglett_bonus_sprites(GameState *state) {
    /* DrawFlippers */
    draw_flipper_sprites(state);

    /* DrawPinball */
    draw_pinball(state);

    /* DrawDugtrio — draw at world position ($40, $00) adjusted by scroll */
    uint8_t frame = state->dugtrio_anim.frame;
    if (frame < 14) {
        uint8_t offset_y = (uint8_t)(0x00 - state->hram.scy);
        uint8_t offset_x = (uint8_t)(0x40 - state->hram.scx);
        load_sprite_data(state, dugtrio_sprite_frames[frame], offset_y, offset_x);
    }
}

/*=============================================================================
 * load_stage_data_diglett_bonus (0x19A6A)
 * Called on stage reload (after EndOfBallBonus, etc.)
 *===========================================================================*/
void load_stage_data_diglett_bonus(GameState *state) {
    if (!state->vram) return;

    /* LoadFlippersPalette */
    load_flippers_palette_diglett(state);

    /* Reload all diglett tile graphics based on current states */
    for (int i = 0; i < NUM_DIGLETTS; i++) {
        uint8_t ds = state->diglett_states[i];
        if (ds >= 1) {
            load_diglett_tiles(state, i, ds);
        }
    }

    /* Reload gate graphics */
    load_gate_graphics(state);

    /* Reload dugtrio collision area if active (ASM: call nz, Func_1ac2c) */
    if (state->dugtrio_state != 0) {
        open_dugtrio_collision_area(state);
    }
}
