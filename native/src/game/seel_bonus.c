/*
 * Seel Bonus Stage (Stage ID $0F)
 *
 * Three seels swim across the stage. Hit them to score points.
 * Timer: 1:30. Completion: score >= 20 seel hits → next bonus = Mewtwo.
 * Streak system with exponential multiplier (2x, 4x, ... 256x).
 *
 * Translated from engine/pinball_game/:
 *   stage_init/init_seel_bonus.asm (0x25a7c)
 *   ball_init/ball_init_seel_bonus.asm (0x25af1)
 *   ball_loss/ball_loss_seel_bonus.asm (0xe08b)
 *   object_collision/seel_bonus_object_collision.asm (0x25bbc)
 *   object_collision/seel_bonus_resolve_collision.asm (0x25c5a)
 *   draw_sprites/draw_seel_bonus_sprites.asm (0x26b7e)
 *   load_stage_data/load_seel_bonus.asm (0x25b97)
 */

#include "game/seel_bonus.h"
#include "game/config_data.h"
#include "game/sprite_data.h"
#include "game/animation.h"
#include "game/timer.h"
#include "game/score.h"
#include "game/collision.h"
#include "game/draw_red_field.h"
#include "game/constants.h"
#include "game/rng.h"
#include "audio/audio.h"
#include "renderer/tile_loader.h"
#include "renderer/stage_assets.h"
#include "renderer/vram.h"
#include <string.h>

extern void draw_timer(GameState *state, uint8_t x, uint8_t y);
extern void draw_pinball(GameState *state);
extern void draw_flipper_sprites(GameState *state);

/*=============================================================================
 * Constants
 *===========================================================================*/
#define SEEL_COLLISION_RADIUS   0x20   /* 32 pixels */
#define SEEL_HEAD_Y_OFFSET      0x14   /* Head is 20 pixels above Y */
#define SEEL_SWIMMING_BOUND_LO  0x08   /* Left boundary high byte */
#define SEEL_SWIMMING_BOUND_HI  0x7A   /* Right boundary high byte */
#define SEEL_WIN_SCORE           20
#define SEEL_MUSIC_BANK         0x11
#define SEEL_MUSIC_ID           0x03
/* Score values loaded from config/scores.json */

/*=============================================================================
 * Collision Angle Table — loaded from binary
 *===========================================================================*/
static uint8_t *circle_collision_angles_seel = NULL;
static size_t circle_angles_size_seel = 0;

static void ensure_collision_angles_loaded(GameState *state) {
    if (!circle_collision_angles_seel) {
        char path[260];
        snprintf(path, sizeof(path), "%s/data/collision/circle_collision_angles.bin",
                 state->asset_base_path);
        circle_collision_angles_seel = load_binary_file(path, &circle_angles_size_seel);
    }
}

/*=============================================================================
 * Swimming X Deltas (SeelSwimmingXDeltas, 0x25f25)
 * 32-entry table for wave-like horizontal movement
 *===========================================================================*/
static const uint8_t SeelSwimmingXDeltas[32] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x36,
    0x35, 0x34, 0x33, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3A, 0x39, 0x38,
    0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30
};

/*=============================================================================
 * Seel Animation Data Tables (0x2614f-0x261f8)
 * 12 animation sequences for the seel state machine.
 * Format: pairs of (duration, sprite_id), terminated by 0x00.
 *===========================================================================*/
/* Sprite IDs matching seel_sprite_table indices */
#define SEELSP_PEEKING_0                0
#define SEELSP_PEEKING_1                1
#define SEELSP_HIT                      2
#define SEELSP_SWIM_RIGHT_0             3
#define SEELSP_SWIM_RIGHT_1             4
#define SEELSP_SWIM_RIGHT_2             5
#define SEELSP_TURN_R2L_0               6
#define SEELSP_TURN_R2L_1               7
#define SEELSP_SHADOW_CIRCLE_0          8
#define SEELSP_SHADOW_CIRCLE_1          9
#define SEELSP_SHADOW_CIRCLE_2         10
#define SEELSP_EMERGE_0                11
#define SEELSP_EMERGE_1                12
#define SEELSP_SPLASH                  13
#define SEELSP_TURN_L2R_2              14
#define SEELSP_TURN_L2R_3              15
#define SEELSP_SWIM_LEFT_0             16
#define SEELSP_SWIM_LEFT_1             17
#define SEELSP_SWIM_LEFT_2             18
#define SEELSP_TURN_L2R_0             19
#define SEELSP_TURN_L2R_1             20
#define SEELSP_TURN_R2L_2             21
#define SEELSP_TURN_R2L_3             22
#define SEELSP_INVISIBLE              23

/* Animation 0: Peeking */
static const uint8_t anim_seel_peeking[] = {
    0x1C, SEELSP_PEEKING_0,
    0x1C, SEELSP_PEEKING_1,
    0x00
};

/* Animation 1: Swim Right */
static const uint8_t anim_seel_swim_right[] = {
    0x0C, SEELSP_SWIM_RIGHT_0,
    0x08, SEELSP_SWIM_RIGHT_1,
    0x0C, SEELSP_SWIM_RIGHT_2,
    0x08, SEELSP_SWIM_RIGHT_1,
    0x00
};

/* Animation 2: Emerge Right */
static const uint8_t anim_seel_emerge_right[] = {
    0x04, SEELSP_TURN_R2L_0,
    0x04, SEELSP_TURN_R2L_1,
    0x05, SEELSP_SHADOW_CIRCLE_0,
    0x05, SEELSP_SHADOW_CIRCLE_1,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x04, SEELSP_EMERGE_0,
    0x08, SEELSP_EMERGE_1,
    0x00
};

/* Animation 3: Submerge Right */
static const uint8_t anim_seel_submerge_right[] = {
    0x08, SEELSP_EMERGE_1,
    0x04, SEELSP_EMERGE_0,
    0x06, SEELSP_SPLASH,
    0x10, SEELSP_INVISIBLE,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x05, SEELSP_SHADOW_CIRCLE_1,
    0x05, SEELSP_SHADOW_CIRCLE_0,
    0x04, SEELSP_TURN_L2R_2,
    0x04, SEELSP_TURN_L2R_3,
    0x00
};

/* Animation 4: Swim Left */
static const uint8_t anim_seel_swim_left[] = {
    0x0C, SEELSP_SWIM_LEFT_0,
    0x08, SEELSP_SWIM_LEFT_1,
    0x0C, SEELSP_SWIM_LEFT_2,
    0x08, SEELSP_SWIM_LEFT_1,
    0x00
};

/* Animation 5: Emerge Left */
static const uint8_t anim_seel_emerge_left[] = {
    0x04, SEELSP_TURN_L2R_0,
    0x04, SEELSP_TURN_L2R_1,
    0x05, SEELSP_SHADOW_CIRCLE_0,
    0x05, SEELSP_SHADOW_CIRCLE_1,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x04, SEELSP_EMERGE_0,
    0x08, SEELSP_EMERGE_1,
    0x00
};

/* Animation 6: Submerge Left */
static const uint8_t anim_seel_submerge_left[] = {
    0x08, SEELSP_EMERGE_1,
    0x04, SEELSP_EMERGE_0,
    0x06, SEELSP_SPLASH,
    0x10, SEELSP_INVISIBLE,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x05, SEELSP_SHADOW_CIRCLE_1,
    0x05, SEELSP_SHADOW_CIRCLE_0,
    0x04, SEELSP_TURN_R2L_2,
    0x04, SEELSP_TURN_R2L_3,
    0x00
};

/* Animation 7: Turn Right to Left */
static const uint8_t anim_seel_turn_r2l[] = {
    0x04, SEELSP_TURN_R2L_0,
    0x04, SEELSP_TURN_R2L_1,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x04, SEELSP_TURN_R2L_2,
    0x04, SEELSP_TURN_R2L_3,
    0x00
};

/* Animation 8: Turn Left to Right */
static const uint8_t anim_seel_turn_l2r[] = {
    0x04, SEELSP_TURN_L2R_0,
    0x04, SEELSP_TURN_L2R_1,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x04, SEELSP_TURN_L2R_2,
    0x04, SEELSP_TURN_L2R_3,
    0x00
};

/* Animation 9: Hit */
static const uint8_t anim_seel_hit[] = {
    0x10, SEELSP_HIT,
    0x00
};

/* Animation 10: Submerge After Hit Right */
static const uint8_t anim_seel_submerge_hit_right[] = {
    0x06, SEELSP_SPLASH,
    0x10, SEELSP_INVISIBLE,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x05, SEELSP_SHADOW_CIRCLE_1,
    0x05, SEELSP_SHADOW_CIRCLE_0,
    0x04, SEELSP_TURN_L2R_2,
    0x04, SEELSP_TURN_L2R_3,
    0x00
};

/* Animation 11: Submerge After Hit Left */
static const uint8_t anim_seel_submerge_hit_left[] = {
    0x06, SEELSP_SPLASH,
    0x10, SEELSP_INVISIBLE,
    0x06, SEELSP_SHADOW_CIRCLE_2,
    0x05, SEELSP_SHADOW_CIRCLE_1,
    0x05, SEELSP_SHADOW_CIRCLE_0,
    0x04, SEELSP_TURN_R2L_2,
    0x04, SEELSP_TURN_R2L_3,
    0x00
};

/* Animation table (SeelAnimationsTable, 0x2614f) — 12 entries */
static const uint8_t *const seel_animation_table[] = {
    anim_seel_peeking,             /* 0 */
    anim_seel_swim_right,          /* 1 */
    anim_seel_emerge_right,        /* 2 */
    anim_seel_submerge_right,      /* 3 */
    anim_seel_swim_left,           /* 4 */
    anim_seel_emerge_left,         /* 5 */
    anim_seel_submerge_left,       /* 6 */
    anim_seel_turn_r2l,            /* 7 */
    anim_seel_turn_l2r,            /* 8 */
    anim_seel_hit,                 /* 9 */
    anim_seel_submerge_hit_right,  /* 10 */
    anim_seel_submerge_hit_left,   /* 11 */
};

/*=============================================================================
 * Multiplier Animation Data (0x261f9-0x26293)
 * 9 multiplier levels: 2x(index 0,1), 4x(2), 8x(3), 16x(4), 32x(5),
 *   64x(6), 128x(7), 256x(8)
 * Each: 3-frame intro + 16-frame hold + 4 blink cycles
 *===========================================================================*/
/* Multiplier sprite IDs from seel_multiplier_sprite_table */
#define SMULT_2_0     0
#define SMULT_2_1     1
#define SMULT_2_2     2
#define SMULT_4_0     3
#define SMULT_4_1     4
#define SMULT_4_2     5
#define SMULT_8_0     6
#define SMULT_8_1     7
#define SMULT_8_2     8
#define SMULT_16_0    9
#define SMULT_16_1   10
#define SMULT_16_2   11
#define SMULT_32_0   12
#define SMULT_32_1   13
#define SMULT_32_2   14
#define SMULT_64_0   15
#define SMULT_64_1   16
#define SMULT_64_2   17
#define SMULT_128_0  18
#define SMULT_128_1  19
#define SMULT_128_2  20
#define SMULT_256_0  21
#define SMULT_256_1  22
#define SMULT_256_2  23
#define SMULT_INVIS  24

static const uint8_t anim_mult_2x[] = {
    0x02, SMULT_2_0,  0x02, SMULT_2_1,  0x02, SMULT_2_2,
    0x10, SMULT_2_0,
    0x04, SMULT_INVIS, 0x04, SMULT_2_0,
    0x04, SMULT_INVIS, 0x04, SMULT_2_0,
    0x04, SMULT_INVIS, 0x04, SMULT_2_0,
    0x00
};
static const uint8_t anim_mult_4x[] = {
    0x02, SMULT_4_0,  0x02, SMULT_4_1,  0x02, SMULT_4_2,
    0x10, SMULT_4_0,
    0x04, SMULT_INVIS, 0x04, SMULT_4_0,
    0x04, SMULT_INVIS, 0x04, SMULT_4_0,
    0x04, SMULT_INVIS, 0x04, SMULT_4_0,
    0x00
};
static const uint8_t anim_mult_8x[] = {
    0x02, SMULT_8_0,  0x02, SMULT_8_1,  0x02, SMULT_8_2,
    0x10, SMULT_8_0,
    0x04, SMULT_INVIS, 0x04, SMULT_8_0,
    0x04, SMULT_INVIS, 0x04, SMULT_8_0,
    0x04, SMULT_INVIS, 0x04, SMULT_8_0,
    0x00
};
static const uint8_t anim_mult_16x[] = {
    0x02, SMULT_16_0, 0x02, SMULT_16_1, 0x02, SMULT_16_2,
    0x10, SMULT_16_0,
    0x04, SMULT_INVIS, 0x04, SMULT_16_0,
    0x04, SMULT_INVIS, 0x04, SMULT_16_0,
    0x04, SMULT_INVIS, 0x04, SMULT_16_0,
    0x00
};
static const uint8_t anim_mult_32x[] = {
    0x02, SMULT_32_0, 0x02, SMULT_32_1, 0x02, SMULT_32_2,
    0x10, SMULT_32_0,
    0x04, SMULT_INVIS, 0x04, SMULT_32_0,
    0x04, SMULT_INVIS, 0x04, SMULT_32_0,
    0x04, SMULT_INVIS, 0x04, SMULT_32_0,
    0x00
};
static const uint8_t anim_mult_64x[] = {
    0x02, SMULT_64_0, 0x02, SMULT_64_1, 0x02, SMULT_64_2,
    0x10, SMULT_64_0,
    0x04, SMULT_INVIS, 0x04, SMULT_64_0,
    0x04, SMULT_INVIS, 0x04, SMULT_64_0,
    0x04, SMULT_INVIS, 0x04, SMULT_64_0,
    0x00
};
static const uint8_t anim_mult_128x[] = {
    0x02, SMULT_128_0, 0x02, SMULT_128_1, 0x02, SMULT_128_2,
    0x10, SMULT_128_0,
    0x04, SMULT_INVIS, 0x04, SMULT_128_0,
    0x04, SMULT_INVIS, 0x04, SMULT_128_0,
    0x04, SMULT_INVIS, 0x04, SMULT_128_0,
    0x00
};
static const uint8_t anim_mult_256x[] = {
    0x02, SMULT_256_0, 0x02, SMULT_256_1, 0x02, SMULT_256_2,
    0x10, SMULT_256_0,
    0x04, SMULT_INVIS, 0x04, SMULT_256_0,
    0x04, SMULT_INVIS, 0x04, SMULT_256_0,
    0x04, SMULT_INVIS, 0x04, SMULT_256_0,
    0x00
};

/* SeelMultiplierAnimations pointer table (0x26238) — indices 0-8 */
static const uint8_t *const seel_mult_anim_table[] = {
    anim_mult_2x,   /* 0 — streak 1 */
    anim_mult_2x,   /* 1 — streak 1 (duplicate) */
    anim_mult_4x,   /* 2 */
    anim_mult_8x,   /* 3 */
    anim_mult_16x,  /* 4 */
    anim_mult_32x,  /* 5 */
    anim_mult_64x,  /* 6 */
    anim_mult_128x, /* 7 */
    anim_mult_256x, /* 8 */
};

/*=============================================================================
 * Gate Tile Data (Func_25d0e)
 * Two states: open (state 0) and closed (state 1).
 * Each state patches 4 positions in bg_map.
 * GBC tile data (TileDataPointers_25d67).
 *===========================================================================*/

/* Gate open tile data (TileData_25d71) */
static const struct { uint16_t offset; uint8_t count; uint8_t tiles[3]; } gate_open_data[] = {
    { 0x113, 2, {0x11, 0x10, 0x00} },
    { 0x133, 2, {0x0E, 0x0D, 0x00} },
    { 0x152, 3, {0x80, 0x0B, 0x0A} },
    { 0x172, 2, {0x07, 0x06, 0x00} },
};

/* Gate closed tile data (TileData_25d8a) */
static const struct { uint16_t offset; uint8_t count; uint8_t tiles[3]; } gate_closed_data[] = {
    { 0x113, 2, {0x0F, 0xF4, 0x00} },
    { 0x133, 2, {0x0C, 0xFB, 0x00} },
    { 0x152, 3, {0x09, 0x08, 0xF8} },
    { 0x172, 2, {0x04, 0xF4, 0x00} },
};

static void load_gate_graphics(GameState *state) {
    /* Load gate tile data based on collision state (GBC path, TileDataPointers_25d67) */
    const void *data = (state->stage_collision_state == 0) ? (const void *)gate_open_data : (const void *)gate_closed_data;
    for (int i = 0; i < 4; i++) {
        uint16_t offset;
        uint8_t count;
        const uint8_t *tiles;
        if (state->stage_collision_state == 0) {
            offset = gate_open_data[i].offset;
            count = gate_open_data[i].count;
            tiles = gate_open_data[i].tiles;
        } else {
            offset = gate_closed_data[i].offset;
            count = gate_closed_data[i].count;
            tiles = gate_closed_data[i].tiles;
        }
        for (uint8_t j = 0; j < count; j++) {
            state->vram->bg_map[0][offset + j] = tiles[j];
        }
    }
    (void)data;
}

/*=============================================================================
 * Score Progress Display (Func_262f4, 0x262f4)
 * Updates the score bar at the top of the BG map (row 0, columns 0-0x13).
 * Shows filled/empty markers based on seel_stage_score.
 * GBC tile data (TileDataPointers_26764).
 *
 * The tile layout for each score level progressively fills columns with
 * filled tiles (0x1D) replacing empty tiles (0x1A). Bookend tiles
 * ($1B left, $19/$18 right, $FB corner) frame the bar.
 *===========================================================================*/

/* Pre-computed tile patterns for score 0-20 (GBC, TileData_267cd-TileData_26b51) */
/* Each row: 20 tiles for bg_map offsets 0x00-0x13 */
static const uint8_t score_bar_tiles[21][20] = {
    /* 0 */  {0xFB,0x18,0x19, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 1 */  {0x1B,0x18,0x19, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 2 */  {0x1B,0x1C,0x19, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 3 */  {0x1B,0x1C,0x1D, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 4 */  {0x1B,0x1C,0x1D, 0x1D,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 5 */  {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 6 */  {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 7 */  {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 8 */  {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 9 */  {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 10 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1A,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 11 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1A, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 12 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1A,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 13 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1A,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 14 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1A, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 15 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1A,0x1A,0x19, 0x18,0xFB},
    /* 16 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1A,0x19, 0x18,0xFB},
    /* 17 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x19, 0x18,0xFB},
    /* 18 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x18,0xFB},
    /* 19 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1E,0xFB},
    /* 20 */ {0x1B,0x1C,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1D,0x1D,0x1D, 0x1E,0x1B},
};

static void update_score_display(GameState *state) {
    /* Func_262f4 — computes sparkle X, wd651, and loads score bar tiles */
    /* Compute sparkle position: score * 8, minus 8 if non-zero */
    uint8_t score = state->seel_stage_score;
    uint8_t spark_x = 0;
    for (uint8_t i = 0; i < score; i++) spark_x += 8;
    if (spark_x > 0) spark_x -= 8;
    state->seel_sparkle_x = spark_x;

    /* Compute score display val */
    if (state->seel_stage_streak > 0) {
        state->seel_score_display_val = (uint8_t)(score + 1 - state->seel_stage_streak);
    } else {
        state->seel_score_display_val = score;
    }

    /* Clamp score to 20 */
    if (score > 20) {
        score = 20;
        state->seel_stage_score = 20;
    }

    /* Load tile row for this score level */
    for (int i = 0; i < 20; i++) {
        state->vram->bg_map[0][i] = score_bar_tiles[score][i];
    }
}

/*=============================================================================
 * Seel Animation Helpers
 *===========================================================================*/

/* Func_26137: Set seel animation state and init the animation */
static void set_seel_anim_state(GameState *state, int seel_idx, uint8_t new_state) {
    if (new_state < 12) {
        init_animation(&state->seel_anim[seel_idx], seel_animation_table[new_state]);
    }
    state->seel_state[seel_idx] = new_state;
}

/*=============================================================================
 * Seel Animation State Machine (CallTable_25f5f)
 * Called when an animation completes. Determines next state transition.
 *===========================================================================*/

/* State 0 complete: Peeking done */
static void seel_on_peeking_done(GameState *state, int idx) {
    /* Only proceed if anim index reached 2 (Func_25f77: dec de, cp $2) */
    if (state->seel_anim[idx].index != 2) return;

    state->seel_timer[idx]--;
    if (state->seel_timer[idx] != 0) {
        /* Keep peeking */
        set_seel_anim_state(state, idx, 0);
        return;
    }

    /* Timer expired: set timer to 3, choose random direction, go to submerge */
    state->seel_timer[idx] = 3;
    state->seel_stage_streak = 0;

    uint8_t rnd = gen_random(state);
    if (rnd & 0x80) {
        state->seel_direction[idx] = 1; /* left */
    } else {
        state->seel_direction[idx] = 0; /* right */
    }

    uint8_t next;
    if (state->seel_direction[idx]) {
        next = 6; /* submerge left */
    } else {
        next = 3; /* submerge right */
    }
    PLAY_SFX(state, "seel_peek", 0x00, 0x31);
    set_seel_anim_state(state, idx, next);
}

/* State 1 complete: Swim right done — check boundary or loop */
static void seel_on_swim_right_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 4) return; /* Func_25fbe: dec de, cp $4 */

    state->seel_timer[idx]--;
    if (state->seel_timer[idx] != 0) {
        set_seel_anim_state(state, idx, 1);
        return;
    }

    /* Timer expired: check stage_state */
    if (state->seel_stage_state != 0) {
        state->seel_timer[idx] = 2;
        state->seel_state[idx] = 4; /* swim left */
        set_seel_anim_state(state, idx, 1);
        return;
    }

    state->seel_stage_state++;
    set_seel_anim_state(state, idx, 2); /* emerge right */
}

/* State 2 complete: Emerge right done */
static void seel_on_emerge_right_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 7) return; /* Func_25ff3: dec de, cp $7 */
    set_seel_anim_state(state, idx, 0); /* peeking */

    /* Set timer based on streak */
    uint8_t streak = state->seel_stage_streak;
    if (streak >= 6) {
        state->seel_timer[idx] = 1;
    } else if (streak >= 2) {
        state->seel_timer[idx] = 2;
    } else {
        state->seel_timer[idx] = 3;
    }
    /* ASM: all three streak branches play SFX 0x31 (M10) */
    PLAY_SFX(state, "seel_peek", 0x00, 0x31);
}

/* State 3 complete: Submerge right done */
static void seel_on_submerge_right_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 9) return; /* Func_2602a: dec de, cp $9 */
    set_seel_anim_state(state, idx, 1); /* swim right */

    /* Random direction for next surfacing */
    uint8_t rnd = gen_random(state);
    state->seel_timer[idx] = (rnd & 0x80) ? 3 : 5;
    state->seel_stage_state--;
}

/* State 4 complete: Swim left done */
static void seel_on_swim_left_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 4) return; /* Func_2604c: dec de, cp $4 */

    state->seel_timer[idx]--;
    if (state->seel_timer[idx] != 0) {
        set_seel_anim_state(state, idx, 4);
        return;
    }

    if (state->seel_stage_state != 0) {
        state->seel_timer[idx] = 2;
        state->seel_state[idx] = 4;
        set_seel_anim_state(state, idx, 4);
        return;
    }

    state->seel_stage_state++;
    set_seel_anim_state(state, idx, 5); /* emerge left */
}

/* State 5 complete: Emerge left done */
static void seel_on_emerge_left_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 7) return; /* Func_2607f: dec de, cp $7 */
    set_seel_anim_state(state, idx, 0); /* peeking */

    uint8_t streak = state->seel_stage_streak;
    if (streak >= 6) {
        state->seel_timer[idx] = 1;
    } else if (streak >= 2) {
        state->seel_timer[idx] = 2;
    } else {
        state->seel_timer[idx] = 3;
    }
    /* ASM: all three streak branches play SFX 0x31 (M10) */
    PLAY_SFX(state, "seel_peek", 0x00, 0x31);
}

/* State 6 complete: Submerge left done */
static void seel_on_submerge_left_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 9) return; /* Func_260b6: dec de, cp $9 */
    set_seel_anim_state(state, idx, 4); /* swim left */

    uint8_t rnd = gen_random(state);
    state->seel_timer[idx] = (rnd & 0x80) ? 3 : 5;
    state->seel_stage_state--;
}

/* State 7 complete: Turn right-to-left done */
static void seel_on_turn_r2l_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 5) return; /* Func_260d8: dec de, cp $5 */
    set_seel_anim_state(state, idx, 4); /* swim left */
}

/* State 8 complete: Turn left-to-right done */
static void seel_on_turn_l2r_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 5) return; /* Func_260e2: dec de, cp $5 */
    set_seel_anim_state(state, idx, 1); /* swim right */
}

/* State 9 complete: Hit done — choose submerge direction */
static void seel_on_hit_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 1) return; /* Func_260ec: dec de, cp $1 */

    if (state->seel_direction[idx]) {
        set_seel_anim_state(state, idx, 11); /* submerge after hit left */
    } else {
        set_seel_anim_state(state, idx, 10); /* submerge after hit right */
    }
}

/* State 10 complete: Submerge after hit right done */
static void seel_on_submerge_hit_right_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 7) return; /* Func_26109: dec de, cp $7 */
    set_seel_anim_state(state, idx, 1); /* swim right */
    state->seel_timer[idx] = 5;
    state->seel_stage_state--;
}

/* State 11 complete: Submerge after hit left done */
static void seel_on_submerge_hit_left_done(GameState *state, int idx) {
    if (state->seel_anim[idx].index != 7) return; /* Func_26120: dec de, cp $7 */
    set_seel_anim_state(state, idx, 4); /* swim left */
    state->seel_timer[idx] = 5;
    state->seel_stage_state--;
}

/* Dispatch table for animation completion */
typedef void (*SeelAnimCallback)(GameState *, int);
static const SeelAnimCallback seel_anim_callbacks[] = {
    seel_on_peeking_done,             /* 0 */
    seel_on_swim_right_done,          /* 1 */
    seel_on_emerge_right_done,        /* 2 */
    seel_on_submerge_right_done,      /* 3 */
    seel_on_swim_left_done,           /* 4 */
    seel_on_emerge_left_done,         /* 5 */
    seel_on_submerge_left_done,       /* 6 */
    seel_on_turn_r2l_done,            /* 7 */
    seel_on_turn_l2r_done,            /* 8 */
    seel_on_hit_done,                 /* 9 */
    seel_on_submerge_hit_right_done,  /* 10 */
    seel_on_submerge_hit_left_done,   /* 11 */
};

/* UpdateSeelAnimation (0x25f47) */
static void update_seel_animation(GameState *state, int idx) {
    uint8_t st = state->seel_state[idx];
    if (st >= 12) return;

    const uint8_t *anim_data = seel_animation_table[st];
    bool finished = update_animation(&state->seel_anim[idx], anim_data);
    if (finished && st < 12) {
        seel_anim_callbacks[st](state, idx);
    }
}

/*=============================================================================
 * Seel Swimming Position Update (UpdateSeelPosition, 0x25ec5)
 *===========================================================================*/
static void update_seel_position(GameState *state, int idx) {
    uint8_t st = state->seel_state[idx];
    if (st != 1 && st != 4) return; /* Only update position during swimming */

    /* Get delta from table using low 4 bits of timer as index (ASM: timer_byte & $0F) */
    uint8_t delta_idx = state->seel_timer[idx] & 0x0F;
    uint8_t delta = SeelSwimmingXDeltas[delta_idx];

    if (state->seel_direction[idx] == 0) {
        /* Swimming right: add delta */
        uint16_t x = (uint16_t)state->seel_x_lo[idx] + delta;
        state->seel_x_lo[idx] = (uint8_t)(x & 0xFF);
        uint8_t carry = (x >> 8) & 1;
        uint8_t new_hi = state->seel_x_hi[idx] + carry;
        state->seel_x_hi[idx] = new_hi;

        /* Check right boundary */
        if (new_hi >= SEEL_SWIMMING_BOUND_HI) {
            state->seel_direction[idx] = 1;
            state->seel_state[idx] = 7; /* turn right-to-left */
            init_animation(&state->seel_anim[idx], seel_animation_table[7]);
        }
    } else {
        /* Swimming left: subtract delta */
        uint16_t x = (uint16_t)state->seel_x_lo[idx] - delta;
        state->seel_x_lo[idx] = (uint8_t)(x & 0xFF);
        int borrow = (x >> 8) & 1; /* Will be 0xFF if borrow */
        uint8_t new_hi = state->seel_x_hi[idx] - (borrow ? 1 : 0);
        state->seel_x_hi[idx] = new_hi;

        /* Check left boundary */
        if (new_hi < SEEL_SWIMMING_BOUND_LO) {
            state->seel_direction[idx] = 0;
            state->seel_state[idx] = 8; /* turn left-to-right */
            init_animation(&state->seel_anim[idx], seel_animation_table[8]);
        }
    }
}

/*=============================================================================
 * Scoring: Func_25e85 — exponential score (2^streak * 100K)
 *===========================================================================*/
static void add_streak_score(GameState *state) {
    uint8_t streak = state->seel_stage_streak + 1;
    uint8_t d = 1;
    for (uint8_t i = 1; i < streak; i++) {
        d = (uint8_t)(d << 1);
        if (d == 0) { d = 0xFF; break; } /* overflow protection */
    }

    while (d > 0) {
        if (d >= 0x32) {
            add_score_with_multiplier(state, state->config->scores.seel_dive);
            d -= 0x32;
        } else {
            add_score_with_multiplier(state, state->config->scores.seel_hit);
            d--;
        }
    }
}

/*=============================================================================
 * Low Time SFX (PlayLowTimeSfx, 0x107f8)
 *===========================================================================*/
static void play_low_time_sfx(GameState *state) {
    if (state->timer_frames != 0) return;
    if (state->timer_minutes != 0) return;
    if (state->timer_seconds == 32) {
        PLAY_SFX(state, "countdown_32sec", 0x07, 0x49);
    } else if (state->timer_seconds == 16) {
        PLAY_SFX(state, "countdown_16sec", 0x0A, 0x4A);
    } else if (state->timer_seconds == 5) {
        PLAY_SFX(state, "countdown_5sec", 0x0D, 0x4B);
    }
}

/*=============================================================================
 * LoadFlippersPalette — OBJ palette 2 colors 1-2 (active vs disabled)
 *===========================================================================*/
static void load_flippers_palette_seel(GameState *state) {
    uint16_t color1, color2;
    if (state->flippers_disabled) {
        color1 = 0x294A; /* gray */
        color2 = 0x1084; /* dark gray */
    } else {
        color1 = 0xFFFF; /* white */
        color2 = 0x6B55; /* blue-gray */
    }
    state->obj_palettes[2].colors[1] = color1;
    state->obj_palettes[2].colors[2] = color2;
}

/*=============================================================================
 * TryCloseGate_SeelBonus (0x25ced)
 *===========================================================================*/
static void try_close_gate(GameState *state) {
    if (state->seel_bonus_closed_gate) return;
    if ((uint8_t)(state->ball_x_pos >> 8) >= 138) return;

    state->stage_collision_state = 1;
    state->seel_bonus_closed_gate = 1;
    load_stage_collision_attributes(state);
    load_gate_graphics(state);
}

/*=============================================================================
 * Multiplier Display (Func_261f9, Func_26212)
 *===========================================================================*/
static void init_multiplier_anim(GameState *state) {
    state->seel_multiplier_active = 0xFF;
    uint8_t idx = state->seel_multiplier_index;
    if (idx < 9) {
        init_animation(&state->seel_multiplier_anim, seel_mult_anim_table[idx]);
    }
}

static void update_multiplier_anim(GameState *state) {
    uint8_t idx = state->seel_multiplier_index;
    if (idx >= 9) return;

    bool finished = update_animation(&state->seel_multiplier_anim, seel_mult_anim_table[idx]);
    if (finished) {
        /* Check if final frame (blink done, frame_id == 0x0A) */
        if (state->seel_multiplier_anim.frame >= 10) {
            state->seel_multiplier_anim.frame = 0;
            state->seel_multiplier_x = 0;
            state->seel_multiplier_y = 0;
            state->seel_multiplier_active = 0;
        }
    }
}

/*=============================================================================
 * PUBLIC: init_seel_bonus (InitSeelBonusStage, 0x25a7c)
 *===========================================================================*/
void init_seel_bonus(GameState *state) {
    if (state->loading_saved_game) return;

    state->ball_size = 0;
    state->stage_collision_state = 0;
    state->disable_horizontal_scroll_for_ball_start = 1;
    state->ball_type_backup = state->ball_type;
    state->ball_size = 0;
    state->ball_type = 0;
    state->completed_bonus_stage = 0;

    /* Initial seel positions (InitialSeelCoords) */
    state->seel_x_lo[0] = 0x00; state->seel_x_hi[0] = 0x60;
    state->seel_y_lo[0] = 0x00; state->seel_y_hi[0] = 0x00;
    state->seel_x_lo[1] = 0x00; state->seel_x_hi[1] = 0x20;
    state->seel_y_lo[1] = 0x00; state->seel_y_hi[1] = 0x1A;
    state->seel_x_lo[2] = 0x00; state->seel_x_hi[2] = 0x40;
    state->seel_y_lo[2] = 0x00; state->seel_y_hi[2] = 0x34;

    state->seel_stage_score = 0;
    state->seel_stage_state = 0;
    state->seel_stage_streak = 0;
    state->seel_transition_timer = 0;

    /* Start 1:30 timer */
    start_timer(state, 0x01, 0x30);

    /* Play music (Bank $11, MUSIC_SEEL_STAGE = $03) */
    PLAY_MUSIC(state, "seel_stage", 0x11, 0x03);
}

/*=============================================================================
 * PUBLIC: init_ball_seel_bonus (InitBallSeelBonusStage, 0x25af1)
 *===========================================================================*/
void init_ball_seel_bonus(GameState *state) {
    /* Ball position: X=$00A6, Y=$0056 */
    state->ball_x_pos = 0xA600;
    state->ball_y_pos = 0x5600;
    state->ball_x_velocity = 0x80;
    state->ball_y_velocity = 0;

    state->scx = 0;
    state->stage_collision_state = 0;
    state->seel_bonus_closed_gate = 0;

    /* Seel directions: 0=right, 1=left, 0=right */
    state->seel_direction[0] = 0;
    state->seel_direction[1] = 1;
    state->seel_direction[2] = 0;

    /* Seel initial states: 4=swim_left, 4=swim_left, 4=swim_left */
    /* ASM: wd76b=4 (anim counter), wd76c=1 (state 1 for seel0) */
    /* But actually: the ASM sets wd775=4, wd77f=4, wd76b=4 then wd76c=1 */
    /* This means seel0 state=1 (swim_right), seel1 state=4 (swim_left), seel2 state=1 */
    /* Let me re-read the ASM more carefully */

    /* From ball_init ASM:
     * wd76b = $4 → seel 0 anim counter part
     * wd76c = $1 → seel 0 state = 1 (swim right)
     * wd775 = $4 → seel 1 anim counter part
     * wd776 = $4 → seel 1 state = 4 (swim left)
     * wd77f = $4 → seel 2 anim counter part
     * wd780 = $1 → seel 2 state = 1
     * Wait, ASM sets wd780=$1 after wd77f=$4
     */

    /* Actually let me look again at the ASM:
     * ld a, $4 / ld [wd775], a / ld [wd77f], a / ld [wd76b], a
     * → all three get initial value 4
     * ld a, $1 / ld [wd76c], a
     * → seel 0 state = 1
     * ld a, $4 / ld [wd776], a
     * → seel 1 state = 4
     * ld a, $1 / ld [wd780], a
     * → seel 2 state = 1
     */

    /* So: seel 0 state=1 (swim right), seel 1 state=4 (swim left), seel 2 state=1 (swim right) */

    /* Timer values */
    state->seel_timer[0] = 5;
    state->seel_timer[1] = 5;
    state->seel_timer[2] = 5;

    /* Multiplier reset */
    state->seel_multiplier_index = 0xFF;
    state->seel_stage_streak = 0;
    state->seel_stage_state = 0;
    state->seel_sparkle_active = 0;
    state->seel_sparkle_frame = 0;
    state->seel_sparkle_cycle = 0;
    state->seel_score_display_val = 0;
    state->seel_multiplier_active = 0;
    state->seel_multiplier_x = 0;
    state->seel_multiplier_y = 0;
    state->seel_multiplier_index = 0;
    state->seel_collision_flag = 0;
    state->seel_completion_state = 0;

    /* Init animations via Func_26137 */
    set_seel_anim_state(state, 0, 1);  /* swim right */
    set_seel_anim_state(state, 1, 4);  /* swim left */
    set_seel_anim_state(state, 2, 1);  /* swim right */

    /* Clear lost ball flag */
    if (state->lost_ball) {
        state->lost_ball = 0;
    }
}

/*=============================================================================
 * PUBLIC: handle_ball_loss_seel_bonus (HandleBallLossSeelBonus, 0xe08b)
 *===========================================================================*/
void handle_ball_loss_seel_bonus(GameState *state) {
    state->seel_sparkle_active = 0;

    /* Score penalty only if flippers enabled OR stage not completed */
    if (state->flippers_disabled && !state->completed_bonus_stage) {
        /* No penalty — handled by flippersEnabled path */
    } else {
        /* Apply score penalty if score < 20 */
        if (state->seel_stage_score < 20) {
            if (state->seel_stage_score >= 5) {
                state->seel_stage_score -= 4;
            } else {
                state->seel_stage_score = 0;
            }
            update_score_display(state);
        }
    }

    /* Check if returning from bonus stage */
    if (state->current_stage_backup == state->current_stage) return;
    if (state->seel_completion_state == 0) return;

    /* Return to main field */
    PLAY_SFX(state, "wall_bounce", 0x00, 0x02);
    state->timer_active = 0;
    state->going_to_bonus_stage = 0;
    state->returning_from_bonus_stage = 1;
    state->ball_size = 2;
    state->disable_horizontal_scroll_for_ball_start = 0;
    state->seel_completion_state = 0;

    /* Show end text (unless completed) */
    if (!state->completed_bonus_stage) {
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        /* scrolling_text_normal 3, 20, 0, 18 → { 5, 0x54, 0x43, 20, 0, 55 } */
        const uint8_t header[6] = { 5, 0x54, 0x43, 20, 0, 55 };
        load_scrolling_text(state, 2, header, "END SEEL STAGE");
    }
}

/*=============================================================================
 * PUBLIC: check_seel_bonus_object_collisions (0x25bbc)
 * Circular collision check for 3 seel heads
 *===========================================================================*/
void check_seel_bonus_object_collisions(GameState *state) {
    ensure_collision_angles_loaded(state);

    for (int i = 0; i < 3; i++) {
        /* Only check if seel state byte (at offset -1 from state) is 0
         * ASM checks wd76c, wd776, wd780 == 0 to skip.
         * But wait - the ASM actually checks wd76c (seel 0's state).
         * If state != 0, skip this seel. State 0 = peeking (vulnerable). */
        if (state->seel_state[i] != 0) continue;

        /* Get seel head position */
        uint8_t seel_x = state->seel_x_hi[i];
        uint8_t seel_y = state->seel_y_hi[i] + SEEL_HEAD_Y_OFFSET;

        /* Delta from ball */
        uint8_t dx = (uint8_t)(state->ball_x_pos >> 8) - seel_x;
        if (dx >= SEEL_COLLISION_RADIUS) continue;

        uint8_t dy = (uint8_t)(state->ball_y_pos >> 8) - seel_y;
        if (dy >= SEEL_COLLISION_RADIUS) continue;

        /* Circular collision lookup */
        if (!circle_collision_angles_seel) continue;

        uint16_t offset = ((uint16_t)dy << 5) | dx;
        if (offset >= circle_angles_size_seel) continue;

        uint8_t angle = circle_collision_angles_seel[offset];
        if (angle & 0x80) continue; /* No collision */

        /* Hit! */
        state->collision_normal_angle = (uint8_t)(angle << 1);
        state->is_ball_colliding = 1;
        state->seel_collision_index = (uint8_t)i;
        state->seel_collision_flag = 1;
        return;
    }
}

/*=============================================================================
 * PUBLIC: resolve_seel_bonus_object_collisions (0x25c5a)
 *===========================================================================*/
void resolve_seel_bonus_object_collisions(GameState *state) {
    /* Func_25da3: Handle hit resolution */
    if (state->seel_collision_flag) {
        state->seel_collision_flag = 0;
        uint8_t idx = state->seel_collision_index;

        /* Set seel to hit state (9) */
        set_seel_anim_state(state, idx, 9);

        /* Set multiplier position */
        state->seel_multiplier_x = state->seel_x_hi[idx];
        state->seel_multiplier_y = state->seel_y_hi[idx] + 8;

        /* Handle streak: if streak == 9, reset */
        if (state->seel_stage_streak >= 9) {
            state->seel_stage_streak = 0;
            state->seel_multiplier_index = 0;
        }

        /* Multiplier display: streak - 1 (pre-decrement for display) */
        if (state->seel_stage_streak > 0) {
            state->seel_multiplier_index = state->seel_stage_streak - 1;
            init_multiplier_anim(state);
        } else {
            state->seel_multiplier_index = 0xFF;
        }

        /* Rumble + SFX */
        state->rumble_pattern = 0x33;
        state->rumble_duration = 0x08;
        PLAY_SFX(state, "seel_hit", 0x00, 0x30);

        /* Add score */
        add_streak_score(state);

        /* Increment streak */
        state->seel_stage_streak++;

        /* Increment score (capped at 20) — ASM: inc [hl] then adds (streak-1)
         * A hit at streak N adds N to the score bar, not just 1. (Func_25da3) */
        if (state->seel_stage_score < 20) {
            state->seel_stage_score++;
            if (state->seel_stage_streak > 1) {
                uint8_t extra = state->seel_stage_streak - 1;
                uint8_t new_score = state->seel_stage_score + extra;
                if (new_score > 20) new_score = 20;
                state->seel_stage_score = new_score;
            }
            state->seel_sparkle_active = 1;
            update_score_display(state);
        }
    }

    /* Update all 3 seel animations */
    for (int i = 0; i < 3; i++) {
        update_seel_animation(state, i);
    }

    /* Update multiplier animation */
    if (state->seel_stage_streak > 0) {
        uint8_t disp_idx = state->seel_stage_streak - 1;
        state->seel_multiplier_index = disp_idx;
        update_multiplier_anim(state);
    } else {
        state->seel_multiplier_index = 0xFF;
    }

    /* Update seel positions */
    for (int i = 0; i < 3; i++) {
        update_seel_position(state, i);
    }

    /* Check gate closure */
    try_close_gate(state);

    /* Check win condition: score >= 20 */
    if (state->seel_stage_score >= SEEL_WIN_SCORE && state->seel_completion_state < 2) {
        state->next_bonus_stage = BONUS_STAGE_ORDER_MEWTWO;
        PLAY_MUSIC(state, "nothing", 0, 0); /* MUSIC_NOTHING */
        state->completed_bonus_stage = 1;
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        /* scrolling_text_normal 1, 20, 0, 20 → { 5, 0x54, 0x41, 20, 0, 59 } */
        const uint8_t header[6] = { 5, 0x54, 0x41, 20, 0, 59 };
        load_scrolling_text(state, 2, header, "SEEL STAGE CLEARED");
        state->seel_completion_state = 2;
        PLAY_SFX(state, "bonus_stage_clear", 0x4B, 0x2A);
    }

    /* State 2: wait for SFX to finish, then resume music */
    if (state->seel_completion_state == 2) {
        if (state->sfx_timer == 0) {
            PLAY_MUSIC(state, "seel_stage", 0x11, 0x03);
            state->seel_completion_state = 5;
        }
    }

    /* Play low time SFX (skip if in cleared state 2) */
    if (state->seel_completion_state != 2) {
        play_low_time_sfx(state);
    }

    /* Handle time running out */
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->flippers_disabled = 1;
        load_flippers_palette_seel(state);
        stop_timer(state);
        state->seel_stage_state = 3; /* timeout */

        if (state->seel_completion_state != 5) {
            state->seel_completion_state = 1;
        }
    }
}

/*=============================================================================
 * PUBLIC: draw_seel_bonus_sprites (DrawSpritesSeelBonus, 0x26b7e)
 *===========================================================================*/
void draw_seel_bonus_sprites(GameState *state) {
    /* 1. Timer — ld bc, $7f65 (bottom-right) */
    draw_timer(state, 0x7F, 0x65);

    /* 2. Multiplier sprite (Func_26bf7) */
    if (state->seel_multiplier_active) {
        uint8_t mx = state->seel_multiplier_x - state->hram.scx;
        uint8_t my = state->seel_multiplier_y - state->hram.scy;
        uint8_t sprite_idx = state->seel_multiplier_anim.frame;
        if (sprite_idx < 25 && seel_multiplier_sprite_table[sprite_idx] != NULL) {
            load_sprite_data(state, seel_multiplier_sprite_table[sprite_idx], my, mx);
        }
    }

    /* 3. Flippers */
    draw_flipper_sprites(state);

    /* 4. Ball */
    draw_pinball(state);

    /* 5. 3 Seels (Func_26ba9) */
    for (int i = 0; i < 3; i++) {
        uint8_t sx = state->seel_x_hi[i] - state->hram.scx;
        uint8_t sy = state->seel_y_hi[i] - state->hram.scy;
        uint8_t sprite_idx = state->seel_anim[i].frame;
        if (sprite_idx < 23 && seel_sprite_table[sprite_idx] != NULL) {
            load_sprite_data(state, seel_sprite_table[sprite_idx], sy, sx);
        }
    }

    /* 6. Progress sparkle (Func_26c3c) */
    if (state->seel_sparkle_active) {
        uint8_t spark_x = state->seel_sparkle_x - state->hram.scx;
        uint8_t spark_y = 0 - state->hram.scy;

        /* Choose sprite based on frame counter */
        int sprite_sel = (state->seel_sparkle_frame < 10) ? 0 : 1;
        load_sprite_data(state, seel_sparkle_sprites[sprite_sel], spark_y, spark_x);

        /* Advance frame */
        state->seel_sparkle_frame++;
        if (state->seel_sparkle_frame >= 20) {
            state->seel_sparkle_frame = 0;
            state->seel_sparkle_cycle++;
            if (state->seel_sparkle_cycle >= 10) {
                state->seel_sparkle_active = 0;
            }
        }
    }
}

/*=============================================================================
 * PUBLIC: load_stage_data_seel_bonus (_LoadStageDataSeelBonus, 0x25b97)
 *===========================================================================*/
void load_stage_data_seel_bonus(GameState *state) {
    /* LoadBallGraphics — handled by asset system */
    /* LoadFlippersPalette */
    load_flippers_palette_seel(state);

    /* Func_262f4 — update score display */
    update_score_display(state);

    /* Func_25d0e — load gate graphics */
    load_gate_graphics(state);

    /* LoadTimerGraphics — handled by asset system */
}
