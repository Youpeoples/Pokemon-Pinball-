/*
 * Meowth Bonus Stage (Stage ID $0B)
 *
 * Timed jewel-collection minigame. Hit Meowth to spawn jewels,
 * collect 20 jewels to clear the stage. Timer: 1:00.
 *
 * Translated from engine/pinball_game/:
 *   stage_init/init_meowth_bonus.asm (0x24000)
 *   ball_init/ball_init_meowth_bonus.asm (0x24059)
 *   ball_loss/ball_loss_meowth_bonus.asm (0xdfe2)
 *   object_collision/meowth_bonus_object_collision.asm (0x2414d)
 *   object_collision/meowth_bonus_resolve_collision.asm (0x2442a)
 *   draw_sprites/draw_meowth_bonus_sprites.asm (0x2583b)
 *   load_stage_data/load_meowth_bonus.asm (0x24128)
 */

#include "game/meowth_bonus.h"
#include "game/config_data.h"
#include "game/sprite_data.h"
#include "game/animation.h"
#include "game/timer.h"
#include "game/score.h"
#include "game/collision.h"
#include "game/draw_red_field.h"
#include "game/constants.h"
#include "audio/audio.h"
#include "renderer/tile_loader.h"
#include "data/embedded_data.h"
#include "renderer/stage_assets.h"
#include "renderer/stage_palettes.h"
#include "renderer/vram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Externals */
extern void draw_flipper_sprites(GameState *state);

/* Low time SFX: ASM PlayLowTimeSfx (0x107f8) — 3 thresholds */
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
 * Constants
 *===========================================================================*/
/* Score values loaded from config/scores.json */

/*=============================================================================
 * Collision angle data — loaded from binary files
 *===========================================================================*/
static uint8_t *meowth_collision_angles = NULL;
static size_t meowth_collision_angles_size = 0;
static uint8_t *jewel_collision_angles = NULL;
static size_t jewel_collision_angles_size = 0;

static void ensure_meowth_angles_loaded(GameState *state) {
    if (!meowth_collision_angles) {
        meowth_collision_angles = load_binary_data(state->asset_base_path,
            "data/collision/meowth_collision_angles.bin", &meowth_collision_angles_size);
    }
    if (!jewel_collision_angles) {
        jewel_collision_angles = load_binary_data(state->asset_base_path,
            "data/collision/meowth_jewel_collision_angles.bin", &jewel_collision_angles_size);
    }
}

/*=============================================================================
 * Animation Data Tables (from meowth_bonus_resolve_collision.asm)
 * Format: [duration, sprite_id] pairs, terminated by 0x00
 *===========================================================================*/
/* Sprite IDs match MeowthSpriteIds table (0-9) */
#define MEOWTHSPRITE_LEFT_WALK_0   0
#define MEOWTHSPRITE_LEFT_WALK_1   1
#define MEOWTHSPRITE_LEFT_WALK_2   2
#define MEOWTHSPRITE_LEFT_HIT      3
#define MEOWTHSPRITE_RIGHT_WALK_0  4
#define MEOWTHSPRITE_RIGHT_WALK_1  5
#define MEOWTHSPRITE_RIGHT_WALK_2  6
#define MEOWTHSPRITE_RIGHT_HIT     7
#define MEOWTHSPRITE_TIMEOUT_0     8
#define MEOWTHSPRITE_TIMEOUT_1     9

/* MeowthAnimationData1: left walk (0x246ec) */
static const uint8_t MeowthAnimationData1[] = {
    0x10, MEOWTHSPRITE_LEFT_WALK_0,
    0x0B, MEOWTHSPRITE_LEFT_WALK_1,
    0x10, MEOWTHSPRITE_LEFT_WALK_2,
    0x0C, MEOWTHSPRITE_LEFT_WALK_1,
    0x00
};

/* MeowthAnimationData2: right walk (0x246f5) */
static const uint8_t MeowthAnimationData2[] = {
    0x10, MEOWTHSPRITE_RIGHT_WALK_0,
    0x0B, MEOWTHSPRITE_RIGHT_WALK_1,
    0x10, MEOWTHSPRITE_RIGHT_WALK_2,
    0x0C, MEOWTHSPRITE_RIGHT_WALK_1,
    0x00
};

/* MeowthAnimationData3: left hit (0x246fe) */
static const uint8_t MeowthAnimationData3[] = {
    0x16, MEOWTHSPRITE_LEFT_HIT,
    0x00
};

/* MeowthAnimationData4: right hit (0x24701) */
static const uint8_t MeowthAnimationData4[] = {
    0x16, MEOWTHSPRITE_RIGHT_HIT,
    0x00
};

/* MeowthAnimationData5: timeout (0x24704) */
static const uint8_t MeowthAnimationData5[] = {
    0x17, MEOWTHSPRITE_TIMEOUT_0,
    0x17, MEOWTHSPRITE_TIMEOUT_1,
    0x00
};

/* Animation table indexed by meowth_state (0-4) */
static const uint8_t *MeowthAnimationDataTable[] = {
    MeowthAnimationData1,
    MeowthAnimationData2,
    MeowthAnimationData3,
    MeowthAnimationData4,
    MeowthAnimationData5
};

/*=============================================================================
 * Multiplier Animation Data (0x24f3a - 0x24f9f)
 * Sprite IDs index into MeowthMultiplierSpriteIds (0-15, $FF=invisible)
 *===========================================================================*/
#define MULT_2_FRAME_0  0
#define MULT_2_FRAME_1  1
#define MULT_2_FRAME_2  2
#define MULT_3_FRAME_0  3
#define MULT_3_FRAME_1  4
#define MULT_3_FRAME_2  5
#define MULT_4_FRAME_0  6
#define MULT_4_FRAME_1  7
#define MULT_4_FRAME_2  8
#define MULT_5_FRAME_0  9
#define MULT_5_FRAME_1  10
#define MULT_5_FRAME_2  11
#define MULT_6_FRAME_0  12
#define MULT_6_FRAME_1  13
#define MULT_6_FRAME_2  14
#define MULT_INVISIBLE   15

static const uint8_t MeowthMultiplier2Animation[] = {
    0x02, MULT_2_FRAME_0, 0x02, MULT_2_FRAME_1, 0x02, MULT_2_FRAME_2,
    0x10, MULT_2_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_2_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_2_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_2_FRAME_0,
    0x00
};
static const uint8_t MeowthMultiplier3Animation[] = {
    0x02, MULT_3_FRAME_0, 0x02, MULT_3_FRAME_1, 0x02, MULT_3_FRAME_2,
    0x10, MULT_3_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_3_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_3_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_3_FRAME_0,
    0x00
};
static const uint8_t MeowthMultiplier4Animation[] = {
    0x02, MULT_4_FRAME_0, 0x02, MULT_4_FRAME_1, 0x02, MULT_4_FRAME_2,
    0x10, MULT_4_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_4_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_4_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_4_FRAME_0,
    0x00
};
static const uint8_t MeowthMultiplier5Animation[] = {
    0x02, MULT_5_FRAME_0, 0x02, MULT_5_FRAME_1, 0x02, MULT_5_FRAME_2,
    0x10, MULT_5_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_5_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_5_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_5_FRAME_0,
    0x00
};
static const uint8_t MeowthMultiplier6Animation[] = {
    0x02, MULT_6_FRAME_0, 0x02, MULT_6_FRAME_1, 0x02, MULT_6_FRAME_2,
    0x10, MULT_6_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_6_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_6_FRAME_0,
    0x04, MULT_INVISIBLE, 0x04, MULT_6_FRAME_0,
    0x00
};

static const uint8_t *MeowthMultiplierAnimations[] = {
    MeowthMultiplier2Animation,
    MeowthMultiplier3Animation,
    MeowthMultiplier4Animation,
    MeowthMultiplier5Animation,
    MeowthMultiplier6Animation
};

/*=============================================================================
 * Jewel Movement Trajectory Tables (from meowth_bonus_resolve_collision.asm)
 * Each entry: {x_delta, y_delta} (signed bytes)
 *===========================================================================*/
/* Data_24af1: primary trajectory (35 entries × 2 bytes, indexed by phase_counter) */
static const int8_t JewelTrajectory1[][2] = {
    { 2, -4}, { 2, -4}, { 2, -4}, { 2,  0}, { 2, -2}, { 2,  0},
    { 2, -2}, { 2,  0}, { 2, -2}, { 2,  0}, { 2, -2}, { 2,  0},
    { 2,  0}, { 2,  0}, { 2,  0}, { 2,  2}, { 2,  2}, { 2,  2},
    { 2,  2}, { 2,  2}, { 2,  3}, { 2,  4}, { 2,  4}, { 2,  4},
    { 2,  4}, { 2,  4}, { 1,  4}, { 1,  4}, { 1,  4}, { 1,  4},
    { 1,  4}, { 1,  4}, { 1,  4}, { 1,  4}, { 1,  4},
};

/* Data_24c0a: secondary trajectory (10 entries × 2 bytes) */
static const int8_t JewelTrajectory2[][2] = {
    { 2, -2}, { 2, -1}, { 2, -1}, { 2,  0}, { 2,  0},
    { 2,  0}, { 2,  1}, { 2,  1}, { 2,  2}, { 2,  4},
};

/*=============================================================================
 * Sprite OAM Data
 * Each sprite is an array of {y_offset, x_offset, tile_id, attr} entries
 * terminated by 0x80.
 *===========================================================================*/
/* Meowth sprites — 8 OAM entries each (walk frames), 7 for hit frames */
static const uint8_t MeowthWalkLeftFrame0[] = {
    0x1E,0x1F,0x9E,0x11, 0x1E,0x17,0x9C,0x11, 0x1E,0x0F,0x9A,0x11, 0x1E,0x07,0x98,0x11,
    0x0E,0x1F,0x96,0x11, 0x0E,0x17,0x94,0x11, 0x0E,0x0F,0x92,0x11, 0x0E,0x07,0x90,0x11, 0x80
};
static const uint8_t MeowthWalkLeftFrame1[] = {
    0x20,0x20,0x1E,0x11, 0x20,0x18,0x1C,0x11, 0x20,0x10,0x1A,0x11, 0x20,0x08,0xA8,0x11,
    0x10,0x20,0xA6,0x11, 0x10,0x18,0xA4,0x11, 0x10,0x10,0xA2,0x11, 0x10,0x08,0xA0,0x11, 0x80
};
static const uint8_t MeowthWalkLeftFrame2[] = {
    0x0E,0x1F,0x96,0x11, 0x0E,0x17,0x94,0x11, 0x0E,0x0F,0x92,0x11, 0x0E,0x07,0x90,0x11,
    0x1E,0x1F,0x26,0x11, 0x1E,0x17,0x24,0x11, 0x1E,0x0F,0x22,0x11, 0x1E,0x07,0x20,0x11, 0x80
};
static const uint8_t MeowthLeftHit[] = {
    0x1A,0x24,0x34,0x11, 0x1A,0x1C,0x32,0x11, 0x1A,0x14,0x30,0x11, 0x1A,0x0C,0x2E,0x11,
    0x0A,0x1C,0x2C,0x11, 0x0A,0x14,0x2A,0x11, 0x0A,0x0C,0x28,0x11, 0x80
};
static const uint8_t MeowthWalkRightFrame0[] = {
    0x1E,0x07,0x9E,0x31, 0x1E,0x0F,0x9C,0x31, 0x1E,0x17,0x9A,0x31, 0x1E,0x1F,0x98,0x31,
    0x0E,0x07,0x96,0x31, 0x0E,0x0F,0x94,0x31, 0x0E,0x17,0x92,0x31, 0x0E,0x1F,0x90,0x31, 0x80
};
static const uint8_t MeowthWalkRightFrame1[] = {
    0x20,0x08,0x1E,0x31, 0x20,0x10,0x1C,0x31, 0x20,0x18,0x1A,0x31, 0x20,0x20,0xA8,0x31,
    0x10,0x08,0xA6,0x31, 0x10,0x10,0xA4,0x31, 0x10,0x18,0xA2,0x31, 0x10,0x20,0xA0,0x31, 0x80
};
static const uint8_t MeowthWalkRightFrame2[] = {
    0x0E,0x07,0x96,0x31, 0x0E,0x0F,0x94,0x31, 0x0E,0x17,0x92,0x31, 0x0E,0x1F,0x90,0x31,
    0x1E,0x07,0x26,0x31, 0x1E,0x0F,0x24,0x31, 0x1E,0x17,0x22,0x31, 0x1E,0x1F,0x20,0x31, 0x80
};
static const uint8_t MeowthRightHit[] = {
    0x1A,0x0C,0x34,0x31, 0x1A,0x14,0x32,0x31, 0x1A,0x1C,0x30,0x31, 0x1A,0x24,0x2E,0x31,
    0x0A,0x14,0x2C,0x31, 0x0A,0x1C,0x2A,0x31, 0x0A,0x24,0x28,0x31, 0x80
};
static const uint8_t MeowthTimeout0[] = {
    0x20,0x20,0xC4,0x11, 0x20,0x18,0xC2,0x11, 0x20,0x10,0xC0,0x11, 0x20,0x08,0xBE,0x11,
    0x10,0x20,0xBC,0x11, 0x10,0x18,0xBA,0x11, 0x10,0x10,0xB8,0x11, 0x10,0x08,0xB6,0x11, 0x80
};
static const uint8_t MeowthTimeout1[] = {
    0x20,0x20,0xCE,0x11, 0x20,0x18,0xCC,0x11, 0x20,0x10,0xCA,0x11, 0x20,0x08,0xC8,0x11,
    0x10,0x10,0xC6,0x11, 0x10,0x20,0xBC,0x11, 0x10,0x18,0xBA,0x11, 0x10,0x08,0xB6,0x11, 0x80
};

/* MeowthSpriteIds: index by animation frame (0-9) → OAM data pointer */
static const uint8_t *MeowthSpriteTable[] = {
    MeowthWalkLeftFrame0, MeowthWalkLeftFrame1, MeowthWalkLeftFrame2, MeowthLeftHit,
    MeowthWalkRightFrame0, MeowthWalkRightFrame1, MeowthWalkRightFrame2, MeowthRightHit,
    MeowthTimeout0, MeowthTimeout1
};

/* Jewel sprites */
static const uint8_t JewelSpawn0[] = { 0x10,0x0C,0x36,0x13, 0x80 };
static const uint8_t JewelSpawn1[] = { 0x10,0x10,0x3A,0x13, 0x10,0x08,0x38,0x13, 0x80 };
static const uint8_t JewelIdle0[]  = { 0x10,0x10,0x3E,0x13, 0x10,0x08,0x3C,0x13, 0x80 };
static const uint8_t JewelIdle1[]  = { 0x10,0x10,0x7C,0x13, 0x10,0x08,0x7A,0x13, 0x80 };
static const uint8_t JewelCollect0[] = { 0x10,0x10,0xAA,0x13, 0x10,0x08,0x7E,0x13, 0x80 };
static const uint8_t JewelCollect1[] = { 0x10,0x0F,0xAC,0x33, 0x10,0x08,0xAC,0x13, 0x80 };
static const uint8_t JewelCollect2[] = { 0x10,0x0F,0xAE,0x33, 0x10,0x08,0xAE,0x13, 0x80 };
static const uint8_t JewelCollect3[] = { 0x10,0x0F,0xB0,0x33, 0x10,0x08,0xB0,0x13, 0x80 };
static const uint8_t JewelCollect4[] = { 0x10,0x0C,0xB2,0x13, 0x80 };
static const uint8_t JewelCollect5[] = { 0x10,0x0C,0xB4,0x13, 0x80 };

/* Jewel sprite ID tables (11 entries each, indexed by jewel_anim_index mod 11) */
static const uint8_t *JewelSpawnSpriteIds[] = {
    JewelSpawn0, JewelSpawn0, JewelSpawn0, JewelSpawn0,
    JewelSpawn1, JewelSpawn1, JewelSpawn1, JewelSpawn1,
    JewelSpawn1, JewelSpawn1, JewelSpawn1
};
static const uint8_t *JewelIdleSpriteIds[] = {
    JewelIdle0, JewelIdle0, JewelIdle0, JewelIdle0,
    JewelIdle0, JewelIdle0, JewelIdle0,
    JewelIdle1, JewelIdle1, JewelIdle1, JewelIdle1
};
static const uint8_t *JewelCollectSpriteIds[] = {
    JewelCollect0, JewelCollect5, JewelCollect4, JewelCollect3,
    JewelCollect2, JewelCollect1, JewelCollect2, JewelCollect3,
    JewelCollect4, JewelCollect5, JewelCollect5
};

/* JewelSpriteIdsTable: indexed by jewel state (0-3) */
static const uint8_t **JewelSpriteIdsTable[] = {
    JewelSpawnSpriteIds,   /* state 0 = spawn */
    JewelSpawnSpriteIds,   /* state 1 = spawn (falling) */
    JewelIdleSpriteIds,    /* state 2 = idle */
    JewelCollectSpriteIds  /* state 3 = collecting */
};

/* Multiplier sprites */
static const uint8_t MultSprite2_0[] = { 0x0A,0x10,0xD2,0x00, 0x0A,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite2_1[] = { 0x08,0x10,0xD2,0x00, 0x08,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite2_2[] = { 0x06,0x10,0xD2,0x00, 0x06,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite3_0[] = { 0x0A,0x10,0xD4,0x00, 0x0A,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite3_1[] = { 0x08,0x10,0xD4,0x00, 0x08,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite3_2[] = { 0x06,0x10,0xD4,0x00, 0x06,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite4_0[] = { 0x0A,0x10,0xD6,0x00, 0x0A,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite4_1[] = { 0x08,0x10,0xD6,0x00, 0x08,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite4_2[] = { 0x05,0x10,0xD6,0x00, 0x05,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite5_0[] = { 0x0A,0x10,0xD8,0x00, 0x0A,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite5_1[] = { 0x08,0x10,0xD8,0x00, 0x08,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite5_2[] = { 0x05,0x10,0xD8,0x00, 0x05,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite6_0[] = { 0x0A,0x10,0xDA,0x00, 0x0A,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite6_1[] = { 0x08,0x10,0xDA,0x00, 0x08,0x08,0xD0,0x00, 0x80 };
static const uint8_t MultSprite6_2[] = { 0x05,0x10,0xDA,0x00, 0x05,0x08,0xD0,0x00, 0x80 };

static const uint8_t *MultiplierSpriteTable[] = {
    MultSprite2_0, MultSprite2_1, MultSprite2_2,
    MultSprite3_0, MultSprite3_1, MultSprite3_2,
    MultSprite4_0, MultSprite4_1, MultSprite4_2,
    MultSprite5_0, MultSprite5_1, MultSprite5_2,
    MultSprite6_0, MultSprite6_1, MultSprite6_2,
    NULL  /* $FF = invisible */
};

/* Progress sparkle sprites */
static const uint8_t SparkleFrame0[] = { 0x10,0x08,0xDC,0x00, 0x80 };
static const uint8_t SparkleFrame1[] = { 0x10,0x08,0xDE,0x00, 0x80 };

/*=============================================================================
 * Gate tilemap data (GBC mode)
 * From TileData_24579 (open) and TileData_24592 (closed)
 *===========================================================================*/
/* Gate open tile IDs at specific BG map offsets */
static const uint8_t GateOpenTiles_113[]  = {0xF6, 0xF5};
static const uint8_t GateOpenTiles_133[]  = {0xF4, 0xF3};
static const uint8_t GateOpenTiles_152[]  = {0x80, 0xF1, 0xF2};
static const uint8_t GateOpenTiles_172[]  = {0xEA, 0xEE};

/* Gate closed tile IDs */
static const uint8_t GateClosedTiles_113[] = {0xE5, 0xE3};
static const uint8_t GateClosedTiles_133[] = {0xE4, 0xE2};
static const uint8_t GateClosedTiles_152[] = {0xF0, 0xEF, 0xE3};
static const uint8_t GateClosedTiles_172[] = {0xED, 0xEC};

static void update_gate_tilemap(GameState *state) {
    /* Write gate tiles to BG map (from Func_24516, GBC branch) */
    const uint8_t *t113, *t133, *t152, *t172;
    if (state->stage_collision_state == 0) {
        t113 = GateOpenTiles_113; t133 = GateOpenTiles_133;
        t152 = GateOpenTiles_152; t172 = GateOpenTiles_172;
    } else {
        t113 = GateClosedTiles_113; t133 = GateClosedTiles_133;
        t152 = GateClosedTiles_152; t172 = GateClosedTiles_172;
    }
    /* BG map base $9800, offsets $113, $133, $152, $172 */
    put_tile_in_vram(state->vram, 0x9800 + 0x113, t113[0], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x114, t113[1], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x133, t133[0], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x134, t133[1], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x152, t152[0], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x153, t152[1], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x154, t152[2], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x172, t172[0], 0);
    put_tile_in_vram(state->vram, 0x9800 + 0x173, t172[1], 0);
}

/*=============================================================================
 * LoadFlippersPalette — writes OBJ palette 2 colors 1-2
 * From home.asm (0x3247). Active vs disabled colors.
 *===========================================================================*/
static void load_flippers_palette_meowth(GameState *state) {
    uint16_t c1, c2;
    if (state->flippers_disabled) {
        c1 = RGB(27,10,10);
        c2 = RGB(20,4,4);
    } else {
        c1 = RGB(31,31,31);
        c2 = RGB(21,21,27);
    }
    state->obj_palettes[2].colors[1] = c1;
    state->obj_palettes[2].colors[2] = c2;
}

/*=============================================================================
 * Score display update (Func_24fa3)
 * Updates the progress bar tile graphics based on meowth_stage_score.
 *===========================================================================*/
static void update_meowth_score_display(GameState *state) {
    uint8_t score = state->meowth_stage_score;
    /* Calculate sparkle X position: (score-1) * 8, or 0 if score==0 */
    uint8_t sparkle_x = 0;
    if (score > 0) {
        sparkle_x = (score - 1) * 8;
    }
    state->meowth_sparkle_x = sparkle_x;

    /* Calculate score display offset */
    if (state->meowth_stage_bonus_counter > 0) {
        state->meowth_score_display_offset = score + 1 - state->meowth_stage_bonus_counter;
    } else {
        state->meowth_score_display_offset = score;
    }

    /* Reset sparkle state */
    state->meowth_sparkle_active = 0;

    if (score == 0) return;

    /* Cap score at 20 */
    if (score >= 0x15) {
        state->meowth_stage_score = 0x14;
        score = 0x14;
    } else {
        /* Start sparkle animation */
        state->meowth_sparkle_cycle = 0;
        state->meowth_sparkle_active = 1;
    }

    /* Queue score progress tile data (GBC mode) */
    /* In the original, this loads pre-rendered tile data for the progress bar.
     * For the C reconstruction, the progress bar tiles are part of the stage
     * tileset and are updated via the BG tilemap. We load the appropriate
     * tile data using QueueGraphicsToLoad equivalent. */
    /* Score tile data: each score level (1-20) has specific tiles.
     * The tiles are already in VRAM from stage load; we just need to
     * update the BG map to show the filled progress. */
    /* For now, the score bar visual is handled by the tile data tables
     * that were loaded at stage init time. The score value itself
     * controls which tiles are visible. */
}

/*=============================================================================
 * init_meowth_bonus — InitMeowthBonusStage (0x24000)
 *===========================================================================*/
void init_meowth_bonus(GameState *state) {
    if (state->loading_saved_game) return;

    state->ball_size = 0;
    state->stage_collision_state = 0;
    state->ball_type_backup = state->ball_type;
    state->ball_size = 0;
    state->ball_type = 0;
    state->completed_bonus_stage = 0;
    state->disable_horizontal_scroll_for_ball_start = 1;

    state->meowth_x_position = 0x40;
    state->meowth_y_position = 0x20;
    state->meowth_anim.frame_counter = 0x10;

    state->meowth_stage_score = 0;
    state->num_active_jewels_bottom = 0;
    state->meowth_stage_bonus_counter = 0;
    state->disable_meowth_jewel_production = 0;
    state->meowth_transition_timer = 0;

    /* Timer: 1 minute 0 seconds */
    start_timer(state, 1, 0);

    /* Music: bank 0x12, id 0x04 (Meowth Stage) */
    PLAY_MUSIC(state, "meowth_stage", 0x12, 0x04);
}

/*=============================================================================
 * init_ball_meowth_bonus — InitBallMeowthBonusStage (0x24059)
 *===========================================================================*/
void init_ball_meowth_bonus(GameState *state) {
    /* Ball position: X=$00A6, Y=$0056 */
    state->ball_x_pos = MAKE_UFIXED(0xA6, 0x00);
    state->ball_y_pos = MAKE_UFIXED(0x56, 0x00);
    state->ball_x_velocity = 0x40;
    state->hram.scx = 0;
    state->scx = 0;
    state->stage_collision_state = 0;
    state->meowth_bonus_closed_gate = 0;

    /* Normalize wd6f3-wd708 block: non-zero → 1, zero stays 0 */
    /* This covers jewel phase/collision/bounce counters */
    for (int i = 0; i < 6; i++) {
        if (state->meowth_jewel_phase_counter[i]) state->meowth_jewel_phase_counter[i] = 1;
        if (state->meowth_jewel_collision_ctr[i]) state->meowth_jewel_collision_ctr[i] = 1;
        if (state->meowth_jewel_bounce_ctr[i]) state->meowth_jewel_bounce_ctr[i] = 1;
    }

    state->disable_horizontal_scroll_for_ball_start = 1;

    /* Re-init Meowth position and animation */
    state->meowth_x_position = 0x40;
    state->meowth_y_position = 0x20;
    state->meowth_anim.frame_counter = 0x10;
    state->meowth_x_movement = -1;  /* walk left */
    state->meowth_anim.frame = 0;
    state->meowth_state = 0;
    state->meowth_anim.index = 0;

    state->num_active_jewels_bottom = 0;
    state->num_active_jewels_top = 0;

    /* All 6 jewels off-screen at $C8,$C8 */
    for (int i = 0; i < 6; i++) {
        state->meowth_jewel_x_coord[i] = 0xC8;
        state->meowth_jewel_y_coord[i] = 0xC8;
        state->meowth_jewel_state[i] = 0;
        state->meowth_jewel_anim_index[i] = 0;
    }

    /* Clear all scratch registers */
    state->meowth_sparkle_active = 0;
    state->meowth_sparkle_counter = 0;
    state->meowth_sparkle_cycle = 0;
    state->meowth_score_display_offset = 0;
    state->meowth_multiplier_active = 0;
    memset(&state->meowth_multiplier_anim, 0, sizeof(Animation));
    state->meowth_multiplier_index = 0;
    state->meowth_multiplier_x = 0;
    state->meowth_multiplier_y = 0;

    /* Clear lost ball flag */
    if (state->lost_ball) {
        state->lost_ball = 0;
    }
}

/*=============================================================================
 * handle_ball_loss_meowth_bonus — HandleBallLossMeowthBonus (0xdfe2)
 *===========================================================================*/
void handle_ball_loss_meowth_bonus(GameState *state) {
    state->meowth_sparkle_active = 0;

    /* Ball save logic: ASM applies penalty when (!flippers_disabled || completed_bonus_stage) */
    if (!state->flippers_disabled || state->completed_bonus_stage) {
        if (state->meowth_stage_score < 20) {
            /* Apply score penalty and save ball */
            if (state->meowth_stage_score >= 5) {
                state->meowth_stage_score -= 4;
            } else {
                state->meowth_stage_score = 0;
            }
            update_meowth_score_display(state);
        }
    }

    /* Check if returning to main stage */
    if (state->current_stage_backup == state->current_stage) {
        return; /* Stay on bonus stage */
    }

    if (state->meowth_completion_state == 0) {
        /* Play ball saved SFX */
        PLAY_SFX(state, "wall_bounce", 0x00, 0x02);
        return;
    }

    /* Setup return to main stage */
    state->time_ran_out = 0;
    state->timer_active = 0;
    state->going_to_bonus_stage = 0;
    state->returning_from_bonus_stage = 1;
    state->ball_size = 2; /* mini ball */
    state->disable_horizontal_scroll_for_ball_start = 0;
    state->meowth_completion_state = 0;

    if (state->completed_bonus_stage) return;

    /* Show "END MEOWTH STAGE" text */
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    /* scrolling_text_normal 2, 20, 0, 19 → { 5, 0x54, 0x42, 20, 0, 57 } */
    const uint8_t header[6] = { 5, 0x54, 0x42, 20, 0, 57 };
    load_scrolling_text(state, 2, header, "END MEOWTH STAGE");
}

/*=============================================================================
 * Collision Detection
 *===========================================================================*/

/* CheckMeowthCollision (0x24170): AABB 48×40, angle lookup */
static bool check_meowth_collision(GameState *state, uint8_t box_x, uint8_t box_y) {
    ensure_meowth_angles_loaded(state);

    uint8_t dx = (uint8_t)(state->ball_x_pos >> 8) - box_x;
    if (dx >= 0x30) return false;
    uint8_t dy = (uint8_t)(state->ball_y_pos >> 8) - box_y;
    if (dy >= 0x28) return false;

    /* Index: dy * 48 + dx (from ASM: dy*8 + dy*16 = dy*24, then *2, then +dx) */
    uint16_t idx = (uint16_t)dy * 48 + dx;
    if (!meowth_collision_angles || idx >= meowth_collision_angles_size) return false;

    uint8_t angle = meowth_collision_angles[idx];
    if (angle & 0x80) return false; /* No collision */

    state->collision_normal_angle = angle << 1;
    state->is_ball_colliding = 1;

    /* Check if Meowth can produce jewels */
    if (state->meowth_state >= 2) return true; /* hit animation playing */
    if (state->disable_meowth_jewel_production) return true;

    /* Determine jewel production based on movement direction */
    if (state->meowth_y_movement != 0) {
        if (state->meowth_y_movement == 1) {
            /* Moving down → bottom jewels */
            if (state->num_active_jewels_bottom >= 3) { /* full */ }
            else { state->meowth_jewel_production_state = 2; }
        } else {
            /* Moving up → top jewels */
            if (state->num_active_jewels_top >= 3) { /* full */ }
            else { state->meowth_jewel_production_state = 2; }
        }
    } else {
        /* Stationary horizontal movement */
        if (state->meowth_y_position == 32) {
            if (state->num_active_jewels_bottom < 3)
                state->meowth_jewel_production_state = 1;
        } else if (state->meowth_y_position == 16) {
            if (state->num_active_jewels_top < 3)
                state->meowth_jewel_production_state = 1;
        }
    }
    return true;
}

/* CheckJewelCollision (0x24272): AABB 24×24, angle lookup */
static bool check_jewel_collision(GameState *state, uint8_t box_x, uint8_t box_y) {
    ensure_meowth_angles_loaded(state);

    uint8_t dx = (uint8_t)(state->ball_x_pos >> 8) - box_x;
    if (dx >= 0x18) return false;
    uint8_t dy = (uint8_t)(state->ball_y_pos >> 8) - box_y;
    if (dy >= 0x18) return false;

    /* Index: dy * 24 + dx */
    uint16_t idx = (uint16_t)dy * 24 + dx;
    if (!jewel_collision_angles || idx >= jewel_collision_angles_size) return false;

    uint8_t angle = jewel_collision_angles[idx];
    if (angle & 0x80) return false;

    state->collision_normal_angle = angle << 1;
    state->is_ball_colliding = 1;
    return true;
}

/* CheckMeowthBonusStageGameObjectCollisions (0x2414d) */
void check_meowth_bonus_object_collisions(GameState *state) {
    /* 1. Check Meowth collision */
    if (state->meowth_jewel_production_state == 0) {
        uint8_t bx = state->meowth_x_position - 9;
        uint8_t by = state->meowth_y_position + 6;
        check_meowth_collision(state, bx, by);
    }

    /* 2. Check bottom jewels (0-2) */
    for (int i = 0; i < 3; i++) {
        if (state->meowth_jewel_state[i] != 2) continue;
        uint8_t bx = state->meowth_jewel_x_coord[i] - 4;
        uint8_t by = state->meowth_jewel_y_coord[i] + 12;
        if (check_jewel_collision(state, bx, by)) {
            state->meowth_jewel_state[i] = 3;
            state->meowth_jewel_anim_index[i] = 0;
            break; /* Only one jewel collision per frame */
        }
    }

    /* 3. Check top jewels (3-5) */
    for (int i = 3; i < 6; i++) {
        if (state->meowth_jewel_state[i] != 2) continue;
        uint8_t bx = state->meowth_jewel_x_coord[i] - 4;
        uint8_t by = state->meowth_jewel_y_coord[i] + 12;
        if (check_jewel_collision(state, bx, by)) {
            state->meowth_jewel_state[i] = 3;
            state->meowth_jewel_anim_index[i] = 0;
            break;
        }
    }
}

/*=============================================================================
 * Jewel spawn — places a new jewel at Meowth's position
 *===========================================================================*/
static void spawn_bottom_jewel(GameState *state) {
    /* Func_247d9: spawn one bottom jewel (0-2) */
    if (!state->meowth_jewel_spawn_flag) return;

    for (int i = 0; i < 3; i++) {
        if (state->meowth_jewel_x_coord[i] != 0xC8) continue;

        state->meowth_jewel_x_coord[i] = state->meowth_x_position + 8;
        state->meowth_jewel_y_coord[i] = state->meowth_y_position + 0xFB; /* -5 unsigned */
        state->meowth_jewel_state[i] = 1;
        state->meowth_jewel_spawn_flag = 0;
        state->meowth_jewel_anim_index[i] = 0;
        state->meowth_jewel_phase_counter[i] = 0;
        state->meowth_jewel_collision_ctr[i] = 0;
        state->meowth_jewel_bounce_ctr[i] = 0;

        /* Direction based on ball X vs Meowth X + 20 */
        uint8_t threshold = state->meowth_x_position + 0x14;
        state->meowth_jewel_direction[i] = ((state->ball_x_pos >> 8) >= threshold) ? 1 : 0;
        break;
    }
}

static void spawn_top_jewel(GameState *state) {
    /* Func_24c28: spawn one top jewel (3-5) */
    if (!state->meowth_jewel_spawn_flag) return;

    for (int i = 3; i < 6; i++) {
        if (state->meowth_jewel_x_coord[i] != 0xC8) continue;

        state->meowth_jewel_x_coord[i] = state->meowth_x_position + 8;
        state->meowth_jewel_y_coord[i] = state->meowth_y_position + 0xFB;
        state->meowth_jewel_state[i] = 1;
        state->num_active_jewels_top++;
        state->meowth_jewel_spawn_flag = 0;
        state->meowth_jewel_anim_index[i] = 0;
        state->meowth_jewel_phase_counter[i] = 0;
        state->meowth_jewel_collision_ctr[i] = 0;
        state->meowth_jewel_bounce_ctr[i] = 0;

        uint8_t threshold = state->meowth_x_position + 0x14;
        state->meowth_jewel_direction[i] = ((state->ball_x_pos >> 8) >= threshold) ? 1 : 0;
        break;
    }
}

/*=============================================================================
 * Jewel hit bonus — called when a jewel enters collect state (anim_index==2)
 * Func_24e7f
 *===========================================================================*/
static void handle_jewel_hit_bonus(GameState *state, uint8_t jewel_x, uint8_t jewel_y) {
    state->meowth_multiplier_x = jewel_x;
    state->meowth_multiplier_y = jewel_y;

    state->meowth_stage_bonus_counter++;
    if (state->meowth_stage_bonus_counter >= 7) {
        state->meowth_stage_bonus_counter = 0;
    }

    state->rumble_pattern = 0xFF;
    state->rumble_duration = 3;
    PLAY_SFX(state, "meowth_hit", 0x00, 0x32);

    /* Add score: 100,000 × bonus_counter times */
    uint8_t count = state->meowth_stage_bonus_counter;
    if (count == 0) count = 7; /* Just wrapped: was 7, add 7× */
    for (uint8_t i = 0; i < count; i++) {
        add_score_with_multiplier(state, state->config->scores.meowth_hit);
        state->meowth_stage_score++;
    }

    /* Start multiplier animation if bonus >= 2 */
    int8_t mult_idx = (int8_t)state->meowth_stage_bonus_counter - 2;
    if (mult_idx >= 0 && mult_idx < 5) {
        state->meowth_multiplier_active = 0xFF;
        state->meowth_multiplier_index = (uint8_t)mult_idx;
        init_animation(&state->meowth_multiplier_anim,
                       MeowthMultiplierAnimations[mult_idx]);
    } else {
        state->meowth_multiplier_index = 0;
        state->meowth_multiplier_active = 0;
    }

    /* Activate sparkle and update score display */
    state->meowth_sparkle_active = 1;
    update_meowth_score_display(state);
}

/*=============================================================================
 * Jewel movement physics — Func_24a30 + Func_24b41
 *===========================================================================*/
static void update_jewel_movement(GameState *state, int jewel_idx) {
    uint8_t *phase = &state->meowth_jewel_phase_counter[jewel_idx];
    uint8_t *coll_ctr = &state->meowth_jewel_collision_ctr[jewel_idx];
    uint8_t *dir = &state->meowth_jewel_direction[jewel_idx];
    uint8_t *jx = &state->meowth_jewel_x_coord[jewel_idx];
    uint8_t *jy = &state->meowth_jewel_y_coord[jewel_idx];
    uint8_t *anim_idx = &state->meowth_jewel_anim_index[jewel_idx];

    if (*coll_ctr != 0) {
        /* Phase 2 movement (Func_24b41) */
        if (*phase >= 0x14) {
            /* Done: reset and set anim to idle */
            *coll_ctr = 0;
            *anim_idx = 0x0A;
            return;
        }

        uint8_t phase_idx = *phase / 2;
        if (phase_idx >= 10) phase_idx = 9;
        int8_t x_delta = JewelTrajectory2[phase_idx][0];
        int8_t y_delta = JewelTrajectory2[phase_idx][1];

        /* X movement with direction and boundary */
        if (*dir == 0) {
            uint8_t new_x = *jx + x_delta;
            if (new_x >= 0x90) {
                *dir = 1;
                new_x = *jx + (uint8_t)(0xFF - x_delta + 1);
                if (new_x < 6) { *dir = 0; new_x = *jx + x_delta; }
            }
            *jx = new_x;
        } else {
            uint8_t neg_delta = (uint8_t)(0xFF - x_delta + 1);
            uint8_t new_x = *jx + neg_delta;
            if (new_x < 6) {
                *dir = 0;
                new_x = *jx + x_delta;
                if (new_x >= 0x90) { *dir = 1; new_x = *jx + neg_delta; }
            }
            *jx = new_x;
        }

        /* Y movement */
        *jy = *jy + (uint8_t)y_delta;

        *phase += 2;
        if (*phase == 0x12) {
            /* Trigger bonus check */
            if (*coll_ctr == 0) *coll_ctr = 1;
        }
        return;
    }

    /* Phase 1 movement (main Func_24a30 path) */
    uint8_t phase_idx = *phase / 2;
    if (phase_idx >= 35) phase_idx = 34;
    int8_t x_delta = JewelTrajectory1[phase_idx][0];
    int8_t y_delta = JewelTrajectory1[phase_idx][1];

    /* Increment anim_index every 4th step */
    if ((*phase & 0x04) && !(*phase & 0x02) && !(*phase & 0x01)) {
        (*anim_idx)++;
    }

    /* X movement with direction and boundary */
    if (*dir == 0) {
        uint8_t new_x = *jx + x_delta;
        if (new_x >= 0x8E) {
            *dir = 1;
            uint8_t neg = (uint8_t)(0xFF - x_delta + 1);
            new_x = *jx + neg;
            if (new_x < 5) { *dir = 0; new_x = *jx + x_delta; }
        }
        *jx = new_x;
    } else {
        uint8_t neg = (uint8_t)(0xFF - x_delta + 1);
        uint8_t new_x = *jx + neg;
        if (new_x < 5) {
            *dir = 0;
            new_x = *jx + x_delta;
            if (new_x >= 0x8E) { *dir = 1; new_x = *jx + neg; }
        }
        *jx = new_x;
    }

    /* Y movement */
    *jy = *jy + (uint8_t)y_delta;

    *phase += 2;
    if (*phase == 0x46) {
        /* End of trajectory — no more movement */
    }
}

/*=============================================================================
 * Meowth movement — Func_24709
 *===========================================================================*/
static void update_meowth_horizontal_movement(GameState *state) {
    /* UpdateMeowthHorizontalMovement (0x24737) */
    if (state->meowth_x_position < 8) {
        state->meowth_x_movement = 1;
    } else if (state->meowth_x_position >= 120) {
        state->meowth_x_movement = -1;
    } else {
        if ((state->hram.frame_counter & 0x3F) != 0) return;
        uint8_t rnd = gen_random(state);
        state->meowth_x_movement = (rnd & 0x80) ? -1 : 1;
    }

    /* Update state based on direction */
    state->meowth_state = (state->meowth_x_movement < 0) ? 0 : 1;
    state->meowth_anim.frame_counter = 2;
}

static void update_meowth_vertical_movement(GameState *state) {
    /* UpdateMeowthVerticalMovement (0x2476d) */
    if (state->meowth_y_movement != 0) {
        if (state->meowth_y_movement == 1) {
            /* Moving down */
            if (state->meowth_y_position == 32) {
                if (state->meowth_jewel_production_state == 2)
                    state->meowth_jewel_production_state = 1;
                state->meowth_y_movement = 0;
            } else {
                state->meowth_y_movement = 1;
            }
        } else {
            /* Moving up */
            if (state->meowth_y_position == 16) {
                if (state->meowth_jewel_production_state == 2)
                    state->meowth_jewel_production_state = 1;
                state->meowth_y_movement = 0;
            } else {
                state->meowth_y_movement = -1;
            }
        }
        return;
    }

    /* Horizontal mode — check if either zone is full */
    if (state->num_active_jewels_bottom >= 3) {
        state->meowth_y_movement = -1; /* Move up */
        return;
    }
    if (state->num_active_jewels_top >= 3) {
        state->meowth_y_movement = 1; /* Move down */
        return;
    }

    /* Random vertical movement every 64 frames */
    if ((state->hram.frame_counter & 0x3F) != 0) return;
    uint8_t rnd = gen_random(state);
    state->meowth_y_movement = (rnd & 0x01) ? 1 : -1;
}

static void update_meowth_movement(GameState *state) {
    /* Func_24709 */
    state->meowth_x_position += state->meowth_x_movement;

    if (state->meowth_y_movement != 0) {
        uint8_t new_y;
        if (state->meowth_y_movement > 0)
            new_y = state->meowth_y_position + 1;
        else
            new_y = state->meowth_y_position - 1;

        /* Clamp to [16, 32] (15=stop, 33=stop in ASM using cp $21 and cp $f) */
        if (new_y != 0x21 && new_y != 0x0F)
            state->meowth_y_position = new_y;
    }

    update_meowth_horizontal_movement(state);
    update_meowth_vertical_movement(state);
}

/*=============================================================================
 * Meowth animation update — Func_2465d
 *===========================================================================*/
static void update_meowth_animation(GameState *state) {
    if (state->meowth_state > 4) return;
    const uint8_t *anim_data = MeowthAnimationDataTable[state->meowth_state];
    bool frame_changed = update_animation(&state->meowth_anim, anim_data);
    if (!frame_changed) return;

    /* Handle animation completion */
    switch (state->meowth_state) {
    case 0: /* Left walk */
        if (state->meowth_anim.index == 4) {
            init_animation(&state->meowth_anim, MeowthAnimationData1);
        }
        break;
    case 1: /* Right walk */
        if (state->meowth_anim.index == 4) {
            init_animation(&state->meowth_anim, MeowthAnimationData2);
        }
        break;
    case 2: /* Left hit → back to left walk */
        if (state->meowth_anim.index == 1) {
            init_animation(&state->meowth_anim, MeowthAnimationData1);
            state->meowth_state = 0;
        }
        break;
    case 3: /* Right hit → back to right walk */
        if (state->meowth_anim.index == 1) {
            init_animation(&state->meowth_anim, MeowthAnimationData2);
            state->meowth_state = 1;
        }
        break;
    case 4: /* Timeout → loop */
        if (state->meowth_anim.index == 2) {
            init_animation(&state->meowth_anim, MeowthAnimationData5);
        }
        break;
    }
}

/*=============================================================================
 * Bottom jewel update — Func_248ac
 *===========================================================================*/
static void update_bottom_jewels(GameState *state) {
    /* State 1 (falling): move, transition to state 2 at anim_index 10 */
    for (int i = 0; i < 3; i++) {
        if (state->meowth_jewel_state[i] == 1) {
            if (state->meowth_jewel_anim_index[i] >= 0x0A) {
                state->num_active_jewels_bottom++;
                state->meowth_jewel_state[i] = 2;
                PLAY_SFX(state, "jewel_collect", 0x00, 0x34);
            } else {
                update_jewel_movement(state, i);
            }
        }
    }

    /* State 2 (active): increment anim index */
    for (int i = 0; i < 3; i++) {
        if (state->meowth_jewel_state[i] == 2) {
            state->meowth_jewel_anim_index[i]++;
        }
    }

    /* State 3 (hit): increment anim, trigger bonus at 2, transition at 10 */
    for (int i = 0; i < 3; i++) {
        if (state->meowth_jewel_state[i] == 3) {
            state->meowth_jewel_anim_index[i]++;
            if (state->meowth_jewel_anim_index[i] == 2) {
                handle_jewel_hit_bonus(state,
                    state->meowth_jewel_x_coord[i],
                    state->meowth_jewel_y_coord[i]);
            } else if (state->meowth_jewel_anim_index[i] >= 0x0A) {
                state->meowth_jewel_state[i] = 4;
            }
        }
    }

    /* State 4 (cleanup): reset jewel, decrement count */
    for (int i = 0; i < 3; i++) {
        if (state->meowth_jewel_state[i] == 4) {
            state->meowth_jewel_x_coord[i] = 0xC8;
            state->meowth_jewel_y_coord[i] = 0xC8;
            state->meowth_jewel_state[i] = 0;
            state->num_active_jewels_bottom--;
            if (state->num_active_jewels_bottom == 2) {
                /* Transition Meowth back to walking */
                if (state->disable_meowth_jewel_production) break;
                if (state->meowth_x_movement == -1) {
                    init_animation(&state->meowth_anim, MeowthAnimationData1);
                    state->meowth_state = 0;
                } else {
                    init_animation(&state->meowth_anim, MeowthAnimationData2);
                    state->meowth_state = 1;
                }
                break;
            }
        }
    }
}

/*=============================================================================
 * Top jewel update — Func_24d07
 *===========================================================================*/
static void update_top_jewels(GameState *state) {
    /* State 1 (falling): move, transition to state 2 at anim_index 10 */
    for (int i = 3; i < 6; i++) {
        if (state->meowth_jewel_state[i] == 1) {
            if (state->meowth_jewel_anim_index[i] >= 0x0A) {
                state->meowth_jewel_state[i] = 2;
                PLAY_SFX(state, "jewel_collect", 0x00, 0x34);
            } else {
                update_jewel_movement(state, i);
            }
        }
    }

    /* State 2 (active): increment anim index */
    for (int i = 3; i < 6; i++) {
        if (state->meowth_jewel_state[i] == 2) {
            state->meowth_jewel_anim_index[i]++;
        }
    }

    /* State 3 (hit): increment anim, trigger bonus at 2, transition at 10 */
    for (int i = 3; i < 6; i++) {
        if (state->meowth_jewel_state[i] == 3) {
            state->meowth_jewel_anim_index[i]++;
            if (state->meowth_jewel_anim_index[i] == 2) {
                handle_jewel_hit_bonus(state,
                    state->meowth_jewel_x_coord[i],
                    state->meowth_jewel_y_coord[i]);
            } else if (state->meowth_jewel_anim_index[i] >= 0x0A) {
                state->meowth_jewel_state[i] = 4;
            }
        }
    }

    /* State 4 (cleanup): reset jewel, decrement count */
    for (int i = 3; i < 6; i++) {
        if (state->meowth_jewel_state[i] == 4) {
            state->meowth_jewel_x_coord[i] = 0xC8;
            state->meowth_jewel_y_coord[i] = 0xC8;
            state->meowth_jewel_state[i] = 0;
            state->num_active_jewels_top--;
            if (state->num_active_jewels_top == 2) {
                if (state->disable_meowth_jewel_production) break;
                if (state->meowth_x_movement == -1) {
                    init_animation(&state->meowth_anim, MeowthAnimationData1);
                    state->meowth_state = 0;
                } else {
                    init_animation(&state->meowth_anim, MeowthAnimationData2);
                    state->meowth_state = 1;
                }
                break;
            }
        }
    }
}

/*=============================================================================
 * Multiplier animation update — Func_24f00
 *===========================================================================*/
static void update_multiplier_animation(GameState *state) {
    if (state->meowth_multiplier_index >= 5) return;
    const uint8_t *anim_data = MeowthMultiplierAnimations[state->meowth_multiplier_index];
    bool done = update_animation(&state->meowth_multiplier_anim, anim_data);
    state->meowth_anim_update_flag = 1;

    if (done && state->meowth_multiplier_anim.index >= 10) {
        /* Animation complete — hide multiplier */
        state->meowth_multiplier_anim.index = 0;
        state->meowth_multiplier_x = 0;
        state->meowth_multiplier_y = 0;
        state->meowth_multiplier_active = 0;
        state->meowth_anim_update_flag = 0;
    }
}

/*=============================================================================
 * resolve_meowth_bonus_object_collisions — ResolveMeowthBonusGameObjectCollisions (0x2442a)
 *===========================================================================*/
void resolve_meowth_bonus_object_collisions(GameState *state) {
    /* Update multiplier display logic */
    if (!state->meowth_anim_update_flag) {
        int8_t val = (int8_t)state->meowth_stage_bonus_counter - 2;
        if (val >= 0 && val < 5) {
            state->meowth_multiplier_index = (uint8_t)val;
        } else {
            state->meowth_multiplier_index = 0;
        }
    } else {
        state->meowth_multiplier_index = 0;
    }

    update_multiplier_animation(state);

    /* Gate closure check */
    if (!state->meowth_bonus_closed_gate) {
        if ((state->ball_x_pos >> 8) < 138) {
            state->stage_collision_state = 1;
            state->meowth_bonus_closed_gate = 1;
            load_stage_collision_attributes(state);
            update_gate_tilemap(state);
        }
    }

    /* Jewel production + Meowth movement */
    if (state->meowth_jewel_production_state != 0 &&
        state->meowth_jewel_production_state != 2) {
        state->meowth_jewel_spawn_flag = 1;
        if (state->meowth_y_position == 32) {
            spawn_bottom_jewel(state);
        } else if (state->meowth_y_position == 16) {
            spawn_top_jewel(state);
        }
        state->meowth_jewel_production_state = 0;
        state->meowth_jewel_spawn_flag = 0;

        state->rumble_pattern = 0xFF;
        state->rumble_duration = 3;
        PLAY_SFX(state, "meowth_move", 0x00, 0x33);
        add_score_with_multiplier(state, state->config->scores.meowth_jewel);
        state->meowth_stage_bonus_counter = 0;

        /* Transition to hit animation */
        if (state->meowth_state < 2) {
            if (state->meowth_state == 0) {
                init_animation(&state->meowth_anim, MeowthAnimationData3);
                state->meowth_state = 2;
            } else {
                init_animation(&state->meowth_anim, MeowthAnimationData4);
                state->meowth_state = 3;
            }
        }
    } else {
        /* No jewel production — check timeout */
        if (state->disable_meowth_jewel_production) {
            state->meowth_state = 4;
        } else if (state->meowth_state < 2) {
            /* Check if both zones full → timeout animation */
            if (state->num_active_jewels_bottom >= 3 && state->num_active_jewels_top >= 3) {
                init_animation(&state->meowth_anim, MeowthAnimationData5);
                state->meowth_state = 4;
            }
        }
    }

    /* Movement only when in walk state (0 or 1) */
    if (state->meowth_state < 2) {
        update_meowth_movement(state);
    }
    update_meowth_animation(state);

    /* Update jewels */
    update_bottom_jewels(state);
    update_top_jewels(state);

    /* Check win condition: score >= 20 */
    if (state->meowth_stage_score >= 0x14) {
        if (state->meowth_completion_state < 2) {
            if (state->next_bonus_stage != BONUS_STAGE_ORDER_SEEL) {
                state->meowth_completion_state = BONUS_STAGE_ORDER_SEEL;
                state->next_bonus_stage = BONUS_STAGE_ORDER_SEEL;
                state->meowth_transition_timer = 0x96;
                PLAY_MUSIC(state, "nothing", 0, 0); /* Stop music */
                state->completed_bonus_stage = 1;
                fill_bottom_message_buffer_with_black_tile(state);
                enable_bottom_text(state);
                /* scrolling_text_normal 0, 20, 0, 21 → { 5, 0x54, 0x40, 20, 0, 61 } */
                const uint8_t header[6] = { 5, 0x54, 0x40, 20, 0, 61 };
                load_scrolling_text(state, 2, header, "MEOWTH STAGE CLEARED");
                PLAY_SFX(state, "bonus_stage_clear", 0x4B, 0x2A);
            }
        }
    }

    /* Music restart after time SFX */
    if (state->meowth_completion_state == 4) {
        if (state->sfx_timer == 0) {
            PLAY_MUSIC(state, "meowth_stage", 0x12, 0x04);
            state->meowth_completion_state = 5;
        }
    }

    /* Play low time SFX (skip if in state 4 = time up transition) */
    if (state->meowth_completion_state != 4) {
        play_low_time_sfx(state);
    }

    /* Handle time running out */
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->flippers_disabled = 1;
        load_flippers_palette_meowth(state);
        stop_timer(state);
        state->disable_meowth_jewel_production = 1;
        state->meowth_completion_state = 1;
        init_animation(&state->meowth_anim, MeowthAnimationData5);
        state->meowth_state = 4;
    }
}

/*=============================================================================
 * draw_meowth_bonus_sprites — DrawSpritesMeowthBonus (0x2583b)
 *===========================================================================*/
void draw_meowth_bonus_sprites(GameState *state) {
    /* 1. Timer */
    draw_timer(state, 0x7F, 0x65);

    /* 2. Flippers */
    draw_flipper_sprites(state);

    /* 3. Multiplier sprite (Func_259fe) */
    if (state->meowth_multiplier_active) {
        uint8_t frame = state->meowth_multiplier_anim.frame;
        if (frame < 16 && MultiplierSpriteTable[frame] != NULL) {
            uint8_t mx = state->meowth_multiplier_x - state->hram.scx;
            uint8_t my = state->meowth_multiplier_y - state->hram.scy;
            load_sprite_data(state, MultiplierSpriteTable[frame], my, mx);
        }
    }

    /* 4. Bottom jewels (0-2) */
    for (int i = 0; i < 3; i++) {
        uint8_t anim_idx = state->meowth_jewel_anim_index[i];
        if (anim_idx >= 11) {
            anim_idx = 0;
            state->meowth_jewel_anim_index[i] = 0;
        }
        uint8_t jstate = state->meowth_jewel_state[i];
        if (jstate > 3) jstate = 3;
        const uint8_t *sprite_data = JewelSpriteIdsTable[jstate][anim_idx];
        uint8_t jx = state->meowth_jewel_x_coord[i] - state->hram.scx;
        uint8_t jy = state->meowth_jewel_y_coord[i] - state->hram.scy;
        load_sprite_data(state, sprite_data, jy, jx);
    }

    /* 5. Top jewels (3-5) */
    for (int i = 3; i < 6; i++) {
        uint8_t anim_idx = state->meowth_jewel_anim_index[i];
        if (anim_idx >= 11) {
            anim_idx = 0;
            state->meowth_jewel_anim_index[i] = 0;
        }
        uint8_t jstate = state->meowth_jewel_state[i];
        if (jstate > 3) jstate = 3;
        const uint8_t *sprite_data = JewelSpriteIdsTable[jstate][anim_idx];
        uint8_t jx = state->meowth_jewel_x_coord[i] - state->hram.scx;
        uint8_t jy = state->meowth_jewel_y_coord[i] - state->hram.scy;
        load_sprite_data(state, sprite_data, jy, jx);
    }

    /* 6. Meowth sprite (Func_2586c) */
    {
        uint8_t mx = state->meowth_x_position - state->hram.scx;
        uint8_t my = state->meowth_y_position - state->hram.scy;
        uint8_t frame = state->meowth_anim.frame;
        if (frame < 10) {
            load_sprite_data(state, MeowthSpriteTable[frame], my, mx);
        }
    }

    /* 7. Pinball */
    draw_pinball(state);

    /* 8. Progress sparkle (Func_25a39) */
    if (state->meowth_sparkle_active) {
        uint8_t sx = state->meowth_sparkle_x - state->hram.scx;
        uint8_t sy = 0 - state->hram.scy;
        const uint8_t *sparkle = (state->meowth_sparkle_counter < 10) ?
                                  SparkleFrame1 : SparkleFrame0;
        load_sprite_data(state, sparkle, sy, sx);

        state->meowth_sparkle_counter++;
        if (state->meowth_sparkle_counter >= 0x14) {
            state->meowth_sparkle_counter = 0;
            state->meowth_sparkle_cycle++;
            if (state->meowth_sparkle_cycle >= 0x0A) {
                state->meowth_sparkle_active = 0;
            }
        }
    }
}

/*=============================================================================
 * load_stage_data_meowth_bonus — _LoadStageDataMeowthBonus (0x24128)
 *===========================================================================*/
void load_stage_data_meowth_bonus(GameState *state) {
    /* _LoadStageDataMeowthBonus (0x24128):
     * 1. LoadBallGraphics    — already handled by base asset table
     * 2. LoadFlippersPalette — palette color adjustment
     * 3. Func_24fa3          — update score display in VRAM
     * 4. Func_24516          — update gate tilemap
     * 5. LoadTimerGraphics   — GBC: already in bank 1 from asset table
     *
     * Sprite tile PNGs (meowth_1-4) are loaded by the base asset table. */

    load_flippers_palette_meowth(state);
    update_meowth_score_display(state);
    update_gate_tilemap(state);
}
