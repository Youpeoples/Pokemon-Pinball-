/*
 * Gengar Bonus Stage (Stage ID $07)
 *
 * Three-phase boss battle: 3 Gastly → 2 Haunter → 1 Gengar.
 * Timer: 1:30. Completion requires 5 hits on Gengar.
 *
 * Translated from engine/pinball_game/:
 *   stage_init/init_gengar_bonus.asm (0x18099)
 *   ball_init/ball_init_gengar_bonus.asm (0x18157)
 *   ball_loss/ball_loss_gengar_bonus.asm (0xdf1a)
 *   object_collision/gengar_bonus_object_collision.asm (0x181b1)
 *   draw_sprites/draw_gengar_bonus_sprites.asm (0x18faf)
 *   load_stage_data/load_gengar_bonus.asm (0x1818b)
 */

#include "game/gengar_bonus.h"
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

/*=============================================================================
 * Constants
 *===========================================================================*/
/* 4-byte BCD scores (little-endian) */
static const uint8_t GASTLY_HIT_SCORE[4]  = {0x00, 0x00, 0x10, 0x00};  /* 100,000 */
static const uint8_t HAUNTER_HIT_SCORE[4] = {0x00, 0x00, 0x00, 0x05};  /* 500,000 */
static const uint8_t GENGAR_HIT_SCORE[4]  = {0x00, 0x00, 0x00, 0x50};  /* 5,000,000 */
static const uint8_t GRAVESTONE_SCORE[4]  = {0x00, 0x01, 0x00, 0x00};  /* 100 */

/* Forward declarations for ghost tile loading helpers */
static void load_gastly_tiles(GameState *state);
static void load_haunter_tiles(GameState *state);
static void load_gengar_tiles(GameState *state);

/*=============================================================================
 * LoadFlippersPalette (home.asm 0x2862)
 * Sets OBJ palette 2 colors 1-2 based on flippers enabled/disabled.
 *===========================================================================*/
static void load_flippers_palette_gengar(GameState *state) {
    if (!state->flippers_disabled) {
        state->obj_palettes[2].colors[1] = RGB(31, 31, 31);
        state->obj_palettes[2].colors[2] = RGB(21, 21, 27);
    } else {
        state->obj_palettes[2].colors[1] = RGB(27, 10, 10);
        state->obj_palettes[2].colors[2] = RGB(20,  4,  4);
    }
}

/*=============================================================================
 * Collision Angle Tables — loaded from binary files
 *===========================================================================*/
static uint8_t *circle_collision_angles = NULL;
static size_t circle_angles_size = 0;
static uint8_t *haunter_collision_angles = NULL;
static size_t haunter_angles_size = 0;
static uint8_t *gengar_collision_angles_data = NULL;
static size_t gengar_angles_size = 0;

static void ensure_collision_angles_loaded(GameState *state) {
    if (!circle_collision_angles) {
        char path[260];
        snprintf(path, sizeof(path), "%s/data/collision/circle_collision_angles.bin",
                 state->asset_base_path);
        circle_collision_angles = load_binary_file(path, &circle_angles_size);
    }
    if (!haunter_collision_angles) {
        char path[260];
        snprintf(path, sizeof(path), "%s/data/collision/haunter_collision_angles.bin",
                 state->asset_base_path);
        haunter_collision_angles = load_binary_file(path, &haunter_angles_size);
    }
    if (!gengar_collision_angles_data) {
        char path[260];
        snprintf(path, sizeof(path), "%s/data/collision/gengar_collision_angles.bin",
                 state->asset_base_path);
        gengar_collision_angles_data = load_binary_file(path, &gengar_angles_size);
    }
}

/*=============================================================================
 * Floating Y-offset Table (GastlyData_18542 / HaunterData_186d7)
 * 32-entry bobbing animation for ghosts
 *===========================================================================*/
static const uint8_t ghost_y_offset_table[32] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x36,
    0x35, 0x34, 0x33, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3A, 0x39, 0x38,
    0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30,
};

/*=============================================================================
 * Animation Data Tables
 *===========================================================================*/

/* Gastly idle animation: ASM frame order {1, 0, 2, 0} */
static const uint8_t gastly_idle_anim[] = {
    13, 1,  13, 0,  13, 2,  13, 0,  0, 0  /* duration, frame pairs; 0 = end */
};

/* Gastly hit animation: AnimationData_185e6
 * Alternates frames 3/4 with decreasing durations for flicker effect.
 * 18 entries (index 0-17), terminator at index 0x12. */
static const uint8_t gastly_hit_anim[] = {
    5, 3,  4, 3,  4, 4,  4, 3,  4, 4,
    3, 3,  3, 4,  3, 3,  3, 4,
    2, 3,  2, 4,  2, 3,  2, 4,
    1, 3,  1, 4,  1, 3,  1, 4,
    128, 4,  0, 0
};

/* Haunter idle animation: 4 frames cycling (0, 1, 2, 3) */
static const uint8_t haunter_idle_anim[] = {
    13, 0,  13, 1,  13, 2,  13, 3,  0, 0
};

/* Haunter hit animation: rapid blinking frames 4/5 */
/* Haunter hit animation: AnimationData_1878a
 * Alternates frames 4/5 with decreasing durations for flicker effect.
 * 19 entries (index 0-18), terminator at index 0x13. */
static const uint8_t haunter_hit_anim[] = {
    5, 4,  4, 4,  4, 5,  4, 4,  4, 5,
    3, 4,  3, 5,  3, 4,  3, 5,
    2, 4,  2, 5,  2, 4,  2, 5,
    1, 4,  1, 5,  1, 4,  1, 5,
    128, 5,  16, 5,  0, 0
};

/* Gengar idle animation (AnimationData_18a61): slow cycle frames 1, 0, 2, 0 */
static const uint8_t gengar_idle_anim[] = {
    0x40, 1,  0x10, 0,  0x40, 2,  0x10, 0,  0, 0
};

/* Gengar approach animation (AnimationData_18a6a): elaborate flicker sequence */
static const uint8_t gengar_approach_anim[] = {
    0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6,
    0x02, 0, 0x01, 6, 0x01, 0, 0x01, 3, 0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3,
    0x01, 6, 0x02, 4, 0x01, 6, 0x02, 4, 0x01, 6, 0x02, 4, 0x01, 6, 0x02, 4,
    0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3, 0x01, 6, 0x01, 3,
    0x01, 0, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6,
    0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3, 0x01, 6,
    0x01, 3, 0x01, 4, 0x01, 6, 0x02, 4, 0x01, 6, 0x02, 4, 0x01, 6, 0x02, 4,
    0x01, 6, 0x01, 4, 0x01, 3, 0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3, 0x01, 6,
    0x02, 3, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6,
    0x02, 0, 0x01, 6, 0x02, 0, 0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3, 0x01, 6,
    0x02, 3, 0x01, 6, 0x02, 4, 0x01, 6, 0x02, 4, 0x01, 6, 0x02, 4, 0x01, 6,
    0x02, 4, 0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3, 0x01, 6, 0x02, 3, 0x01, 6,
    0, 0
};

/* Gengar normal hit animation (AnimationData_18b2b): (frames 5, 1, 0) */
static const uint8_t gengar_normal_hit_anim[] = {
    16, 5,  32, 1,  8, 0,  0, 0
};

/* Gengar waiting animation (AnimationData_18d2f): long idle */
static const uint8_t gengar_waiting_anim[] = {
    0x40, 0,  0x40, 0,  0, 0
};

/* Gengar death animation (AnimationData_18b32): extended defeat sequence */
static const uint8_t gengar_death_anim[] = {
    0x10, 5, 0x10, 0, 0x08, 3, 0x0C, 4, 0x0A, 3,
    0x10, 0, 0x08, 3, 0x0C, 4, 0x0A, 3,
    0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6,
    0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6,
    0x04, 0, 0x04, 6, 0x04, 0, 0x04, 6,
    0x04, 2, 0x04, 6, 0x04, 2, 0x04, 6, 0x04, 2, 0x04, 6, 0x04, 2, 0x04, 6,
    0x04, 2, 0x04, 6, 0x04, 2, 0x04, 6, 0x04, 2, 0x04, 6, 0x04, 2, 0x04, 6,
    0x04, 0, 0x04, 6, 0x04, 0, 0x04, 6,
    0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6,
    0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6, 0x04, 1, 0x04, 6,
    0x04, 0, 0x04, 6, 0x04, 0, 0x04, 6,
    0x04, 2, 0x04, 6, 0x04, 2, 0x04, 6,
    0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6,
    0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6,
    0x03, 0, 0x03, 6, 0x03, 0, 0x03, 6, 0x03, 0, 0x03, 6,
    0x03, 1, 0x03, 6, 0x03, 1, 0x03, 6, 0x03, 1, 0x03, 6, 0x03, 1, 0x03, 6,
    0x03, 1, 0x03, 6, 0x03, 1, 0x03, 6, 0x03, 1, 0x03, 6, 0x03, 1, 0x03, 6,
    0x03, 1, 0x03, 6, 0x03, 1, 0x03, 6,
    0x02, 1, 0x01, 0, 0x03, 6, 0x03, 0, 0x03, 6, 0x03, 0, 0x03, 6,
    0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6,
    0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6, 0x03, 2, 0x03, 6,
    0x02, 2, 0x02, 6, 0x02, 2, 0x02, 6, 0x02, 2, 0x02, 6, 0x02, 2, 0x02, 6,
    0x02, 0, 0x02, 6, 0x02, 0, 0x02, 6, 0x02, 0, 0x02, 6, 0x02, 0, 0x02, 6,
    0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6,
    0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6,
    0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6,
    0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6, 0x02, 1, 0x02, 6,
    0x02, 0, 0x02, 6, 0x02, 0, 0x02, 6, 0x02, 0, 0x02, 6, 0x02, 0, 0x02, 6,
    0x02, 2, 0x02, 6, 0x02, 2, 0x02, 6, 0x02, 2, 0x02, 6, 0x02, 2, 0x02, 6,
    0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6,
    0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6,
    0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6,
    0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6,
    0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6,
    0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6, 0x01, 2, 0x01, 6,
    0x01, 0, 0x01, 6, 0x01, 0, 0x01, 6,
    0, 0
};

/*=============================================================================
 * Sprite OAM Data — Gastly
 *===========================================================================*/
static const uint8_t sprite_gastly_frame0[] = {
    0x20, 0x20, 0x9E, 0x04,
    0x20, 0x18, 0x9C, 0x04,
    0x20, 0x10, 0x9A, 0x04,
    0x20, 0x08, 0x98, 0x04,
    0x10, 0x20, 0x96, 0x04,
    0x10, 0x18, 0x94, 0x04,
    0x10, 0x10, 0x92, 0x04,
    0x10, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_gastly_frame1[] = {
    0x21, 0x20, 0x9E, 0x04,
    0x21, 0x18, 0x9C, 0x04,
    0x21, 0x10, 0x9A, 0x04,
    0x21, 0x08, 0x98, 0x04,
    0x11, 0x20, 0x96, 0x04,
    0x11, 0x18, 0x94, 0x04,
    0x11, 0x10, 0x92, 0x04,
    0x11, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_gastly_frame2[] = {
    0x1F, 0x20, 0x9E, 0x04,
    0x1F, 0x18, 0x9C, 0x04,
    0x1F, 0x10, 0x9A, 0x04,
    0x1F, 0x08, 0x98, 0x04,
    0x0F, 0x20, 0x96, 0x04,
    0x0F, 0x18, 0x94, 0x04,
    0x0F, 0x10, 0x92, 0x04,
    0x0F, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_gastly_hit[] = {
    0x1F, 0x18, 0xA6, 0x04,
    0x1F, 0x10, 0xA4, 0x04,
    0x0F, 0x18, 0xA2, 0x04,
    0x0F, 0x10, 0xA0, 0x04,
    0x1F, 0x20, 0x9E, 0x04,
    0x1F, 0x08, 0x98, 0x04,
    0x0F, 0x20, 0x96, 0x04,
    0x0F, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t *gastly_sprites[] = {
    sprite_gastly_frame0, sprite_gastly_frame1, sprite_gastly_frame2,
    sprite_gastly_hit, sprite_gastly_hit
};

/*=============================================================================
 * Sprite OAM Data — Haunter
 *===========================================================================*/
static const uint8_t sprite_haunter_frame0[] = {
    /* HaunterFrame0Sprite (data/sprite_frames.asm:2439) */
    0x1A, 0x21, 0x1A, 0x04,
    0x1A, 0x19, 0xA8, 0x04,
    0x1E, 0x00, 0xA2, 0x24,
    0x1E, 0x08, 0xA0, 0x24,
    0x30, 0x10, 0x9E, 0x04,
    0x20, 0x18, 0x9C, 0x04,
    0x20, 0x10, 0x9A, 0x04,
    0x20, 0x08, 0x98, 0x04,
    0x10, 0x20, 0x96, 0x04,
    0x10, 0x18, 0x94, 0x04,
    0x10, 0x10, 0x92, 0x04,
    0x10, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_haunter_frame1[] = {
    /* HaunterFrame1Sprite (data/sprite_frames.asm:2454) */
    0x1E, 0x02, 0xA6, 0x24,
    0x1E, 0x0A, 0xA4, 0x24,
    0x1C, 0x23, 0xA6, 0x04,
    0x1C, 0x1B, 0xA4, 0x04,
    0x2E, 0x11, 0x9E, 0x04,
    0x1E, 0x18, 0x9C, 0x04,
    0x1E, 0x10, 0x9A, 0x04,
    0x1E, 0x08, 0x98, 0x04,
    0x0E, 0x20, 0x96, 0x04,
    0x0E, 0x18, 0x94, 0x04,
    0x0E, 0x10, 0x92, 0x04,
    0x0E, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_haunter_frame2[] = {
    /* HaunterFrame2Sprite (data/sprite_frames.asm:2469) */
    0x1E, 0x23, 0xA2, 0x04,
    0x1E, 0x1B, 0xA0, 0x04,
    0x1A, 0x02, 0x1A, 0x24,
    0x1A, 0x0A, 0xA8, 0x24,
    0x2D, 0x10, 0x9E, 0x04,
    0x1D, 0x18, 0x9C, 0x04,
    0x1D, 0x10, 0x9A, 0x04,
    0x1D, 0x08, 0x98, 0x04,
    0x0D, 0x20, 0x96, 0x04,
    0x0D, 0x18, 0x94, 0x04,
    0x0D, 0x10, 0x92, 0x04,
    0x0D, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_haunter_frame3[] = {
    /* HaunterFrame3Sprite (data/sprite_frames.asm:2484) */
    0x1C, 0x00, 0xA6, 0x24,
    0x1C, 0x08, 0xA4, 0x24,
    0x1E, 0x21, 0xA6, 0x04,
    0x1E, 0x19, 0xA4, 0x04,
    0x2E, 0x0F, 0x9E, 0x04,
    0x1E, 0x18, 0x9C, 0x04,
    0x1E, 0x10, 0x9A, 0x04,
    0x1E, 0x08, 0x98, 0x04,
    0x0E, 0x20, 0x96, 0x04,
    0x0E, 0x18, 0x94, 0x04,
    0x0E, 0x10, 0x92, 0x04,
    0x0E, 0x08, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_haunter_hit[] = {
    /* HaunterHitSprite (data/sprite_frames.asm:2499) */
    0x17, 0x09, 0xA8, 0x24,
    0x17, 0x01, 0x1A, 0x24,
    0x0F, 0x1E, 0xA8, 0x04,
    0x0F, 0x26, 0x1A, 0x04,
    0x2D, 0x18, 0x28, 0x04,
    0x1D, 0x18, 0x26, 0x04,
    0x1D, 0x10, 0x24, 0x04,
    0x0D, 0x20, 0x22, 0x04,
    0x0D, 0x18, 0x20, 0x04,
    0x0D, 0x10, 0x1E, 0x04,
    0x0E, 0x08, 0x1C, 0x04,
    0x80
};

static const uint8_t *haunter_sprites[] = {
    sprite_haunter_frame0, sprite_haunter_frame1,
    sprite_haunter_frame2, sprite_haunter_frame3,
    sprite_haunter_hit, sprite_haunter_hit
};

/*=============================================================================
 * Sprite OAM Data — Gengar
 *===========================================================================*/
static const uint8_t sprite_gengar_frame0[] = {
    0x20, 0x30, 0x32, 0x24,
    0x30, 0x30, 0x3A, 0x04,
    0x10, 0x30, 0x38, 0x04,
    0x30, 0x08, 0x34, 0x04,
    0x20, 0x08, 0x32, 0x04,
    0x10, 0x08, 0x30, 0x04,
    0x38, 0x28, 0x1E, 0x04,
    0x38, 0x20, 0x1C, 0x04,
    0x38, 0x18, 0x1A, 0x04,
    0x38, 0x10, 0xA8, 0x04,
    0x28, 0x28, 0xA6, 0x04,
    0x28, 0x20, 0xA4, 0x04,
    0x28, 0x18, 0xA2, 0x04,
    0x28, 0x10, 0xA0, 0x04,
    0x18, 0x28, 0x9E, 0x04,
    0x18, 0x20, 0x9C, 0x04,
    0x18, 0x18, 0x9A, 0x04,
    0x18, 0x10, 0x98, 0x04,
    0x08, 0x28, 0x96, 0x04,
    0x08, 0x20, 0x94, 0x04,
    0x08, 0x18, 0x92, 0x04,
    0x08, 0x10, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_gengar_frame1[] = {
    0x21, 0x30, 0x32, 0x24,
    0x31, 0x30, 0x3A, 0x04,
    0x11, 0x30, 0x38, 0x04,
    0x31, 0x08, 0x34, 0x04,
    0x21, 0x08, 0x32, 0x04,
    0x11, 0x08, 0x30, 0x04,
    0x39, 0x28, 0x1E, 0x04,
    0x39, 0x20, 0x1C, 0x04,
    0x39, 0x18, 0x1A, 0x04,
    0x39, 0x10, 0xA8, 0x04,
    0x29, 0x28, 0xA6, 0x04,
    0x29, 0x20, 0xA4, 0x04,
    0x29, 0x18, 0xA2, 0x04,
    0x29, 0x10, 0xA0, 0x04,
    0x19, 0x28, 0x9E, 0x04,
    0x19, 0x20, 0x9C, 0x04,
    0x19, 0x18, 0x9A, 0x04,
    0x19, 0x10, 0x98, 0x04,
    0x09, 0x28, 0x96, 0x04,
    0x09, 0x20, 0x94, 0x04,
    0x09, 0x18, 0x92, 0x04,
    0x09, 0x10, 0x90, 0x04,
    0x80
};

static const uint8_t sprite_gengar_frame2[] = {
    0x1F, 0x30, 0x32, 0x24,
    0x2F, 0x30, 0x3A, 0x04,
    0x0F, 0x30, 0x38, 0x04,
    0x2F, 0x08, 0x34, 0x04,
    0x1F, 0x08, 0x32, 0x04,
    0x0F, 0x08, 0x30, 0x04,
    0x37, 0x28, 0x1E, 0x04,
    0x37, 0x20, 0x1C, 0x04,
    0x37, 0x18, 0x1A, 0x04,
    0x37, 0x10, 0xA8, 0x04,
    0x27, 0x28, 0xA6, 0x04,
    0x27, 0x20, 0xA4, 0x04,
    0x27, 0x18, 0xA2, 0x04,
    0x27, 0x10, 0xA0, 0x04,
    0x17, 0x28, 0x9E, 0x04,
    0x17, 0x20, 0x9C, 0x04,
    0x17, 0x18, 0x9A, 0x04,
    0x17, 0x10, 0x98, 0x04,
    0x07, 0x28, 0x96, 0x04,
    0x07, 0x20, 0x94, 0x04,
    0x07, 0x18, 0x92, 0x04,
    0x07, 0x10, 0x90, 0x04,
    0x80
};

/* Frames 3 and 4: same as frame 0/1 pattern (slight variations) */
static const uint8_t *sprite_gengar_frame3 = sprite_gengar_frame0;
static const uint8_t *sprite_gengar_frame4 = sprite_gengar_frame1;

static const uint8_t sprite_gengar_hit[] = {
    0x23, 0x04, 0xB2, 0x04,
    0x27, 0x2C, 0xD2, 0x04,
    0x27, 0x24, 0xD0, 0x04,
    0x27, 0x1C, 0xCE, 0x04,
    0x27, 0x14, 0xCC, 0x04,
    0x27, 0x0C, 0xCA, 0x04,
    0x17, 0x2C, 0xC8, 0x04,
    0x17, 0x24, 0xC6, 0x04,
    0x17, 0x1C, 0xC4, 0x04,
    0x17, 0x14, 0xC2, 0x04,
    0x17, 0x0C, 0xC0, 0x04,
    0x07, 0x2C, 0xBE, 0x04,
    0x07, 0x24, 0xBC, 0x04,
    0x07, 0x1C, 0xBA, 0x04,
    0x07, 0x14, 0xB8, 0x04,
    0x07, 0x0C, 0xB6, 0x04,
    0x23, 0x34, 0xB4, 0x04,
    0x37, 0x28, 0x1E, 0x04,
    0x37, 0x20, 0x1C, 0x04,
    0x37, 0x18, 0x1A, 0x04,
    0x37, 0x10, 0xA8, 0x04,
    0x80
};

static const uint8_t *gengar_sprites[] = {
    sprite_gengar_frame0, sprite_gengar_frame1, sprite_gengar_frame2,
    (const uint8_t*)0, (const uint8_t*)0, /* frames 3,4 assigned at runtime */
    sprite_gengar_hit
};

/*=============================================================================
 * Gravestone collision data (4 gravestones on the field)
 *===========================================================================*/
static const struct { uint8_t id; uint8_t x; uint8_t y; } gravestones[4] = {
    { 1, 0x24, 0x52 },
    { 2, 0x44, 0x3A },
    { 3, 0x74, 0x5A },
    { 4, 0x7C, 0x32 },
};
#define GRAVESTONE_SIZE 0x11  /* 17x17 bounding box */

/*=============================================================================
 * InitGengarBonusStage (0x18099)
 *===========================================================================*/
void init_gengar_bonus(GameState *state) {
    if (state->loading_saved_game) {
        state->gastly_gfx_countdown = 0;
        state->haunter_gfx_countdown = 8;
        state->gengar_gfx_countdown = 8;
        return;
    }

    state->disable_horizontal_scroll_for_ball_start = 1;
    state->ball_type_backup = state->ball_type;
    state->ball_size = 0;
    state->ball_type = 0;
    state->completed_bonus_stage = 0;

    /* Initialize 3 Gastly */
    state->gastly1_enabled = 1;
    state->gastly1_anim = (Animation){ 1, 0, 0 };
    state->gastly1_in_hit_anim = 0;
    state->gastly1_x_pos = MAKE_UFIXED(0x08, 0);
    state->gastly1_y_pos = MAKE_UFIXED(0x08, 0);

    state->gastly2_enabled = 1;
    state->gastly2_anim = (Animation){ 1, 0, 0 };
    state->gastly2_in_hit_anim = 0;
    state->gastly2_x_pos = MAKE_UFIXED(0x50, 0);
    state->gastly2_y_pos = MAKE_UFIXED(0x20, 0);

    state->gastly3_enabled = 1;
    state->gastly3_anim = (Animation){ 1, 0, 0 };
    state->gastly3_in_hit_anim = 0;
    state->gastly3_x_pos = MAKE_UFIXED(0x30, 0);
    state->gastly3_y_pos = MAKE_UFIXED(0x38, 0);

    /* Initialize 2 Haunter (disabled) */
    state->haunter1_enabled = 0;
    state->haunter1_anim = (Animation){ 1, 0, 0 };
    state->haunter1_in_hit_anim = 0;
    state->haunter1_x_pos = MAKE_UFIXED(0x50, 0);
    state->haunter1_y_pos = MAKE_UFIXED(0x10, 0);

    state->haunter2_enabled = 0;
    state->haunter2_anim = (Animation){ 1, 0, 0 };
    state->haunter2_in_hit_anim = 0;
    state->haunter2_x_pos = MAKE_UFIXED(0x10, 0);
    state->haunter2_y_pos = MAKE_UFIXED(0x34, 0);

    /* Initialize Gengar (disabled, below screen) */
    state->gengar_enabled = 0;
    state->gengar_anim = (Animation){ 1, 0, 0 };
    state->gengar_phase = 4;  /* ASM GengarInitialData byte 5 = $04 (waiting phase) */
    state->gengar_x_pos = MAKE_UFIXED(0x38, 0);
    state->gengar_y_pos = MAKE_UFIXED(0xF8, 0);

    /* Clear counters */
    state->num_gastly_hits = 0;
    state->num_haunter_hits = 0;
    state->num_gengar_hits = 0;
    state->gengar_defeated = 0;
    state->gengar_returning = 0;
    state->gengar_gate_state = 0;
    state->gengar_upper_tilt_active = 0;
    state->gengar_upper_tilt_counter = 0;
    state->gengar_upper_tilt_cooldown = 0;
    state->gengar_prev_anim_state = 0;
    state->gengar_collision_trigger = 0;
    state->gengar_collision_object = 0;
    state->haunter_collision_trigger = 0;
    state->haunter_collision_object = 0;
    state->gengar_hit_trigger = 0;
    state->gengar_hit_object = 0;
    memset(state->gastly_y_offset_counter, 0, 3);
    memset(state->haunter_y_offset_counter, 0, 2);

    /* GFX loading countdowns */
    state->gastly_gfx_countdown = 0;
    state->haunter_gfx_countdown = 8;
    state->gengar_gfx_countdown = 8;

    /* Start 1:30 timer */
    start_timer(state, 0x01, 0x30);

    /* Play Gastly Graveyard music (bank 0x0F, id 0x05) */
    audio_play_music(state->audio, 0x0F, 0x05);
}

/*=============================================================================
 * InitBallGengarBonusStage (0x18157)
 *===========================================================================*/
void init_ball_gengar_bonus(GameState *state) {
    state->ball_x_pos = MAKE_UFIXED(0xA6, 0);
    state->ball_y_pos = MAKE_UFIXED(0x56, 0);
    state->scx = 0;
    state->hram.scx = 0;
    state->stage_collision_state = 0;
    state->gengar_bonus_closed_gate = 0;
    state->gastly_gfx_countdown = 0;
    state->haunter_gfx_countdown = 8;
    state->gengar_gfx_countdown = 8;

    if (state->lost_ball) {
        state->lost_ball = 0;
    }
}

/*=============================================================================
 * HandleBallLossGengarBonus (0xdf1a)
 *===========================================================================*/
void handle_ball_loss_gengar_bonus(GameState *state) {
    /* Check if still in Gengar stage */
    if (state->current_stage_backup == state->current_stage)
        return;

    /* Check if Gengar defeated */
    if (state->gengar_defeated) {
        /* Returning from bonus stage */
        state->going_to_bonus_stage = 0;
        state->returning_from_bonus_stage = 1;
        state->ball_size = 2;
        state->disable_horizontal_scroll_for_ball_start = 0;

        if (!state->completed_bonus_stage) {
            /* Show "END GENGAR STAGE" text */
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            const char *text = "END GENGAR STAGE";
            /* scrolling_text_normal 2, 20, 0, 19 → {5, 0x54, 0x42, 20, 0, 57} */
            uint8_t header[6] = { 5, 0x54, 0x42, 20, 0, 57 };
            load_scrolling_text(state, 2, header, text);
        }
        return;
    }

    /* Gengar not yet defeated */
    if (state->num_gengar_hits >= 5) {
        /* All 5 hits but stage not "complete" (wd6a8) — freeze ball */
        state->move_to_next_screen_state = 0;
        if (state->gengar_returning)
            return;
        state->pinball_is_visible = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->gengar_returning = 1;
    }

    /* Ball loss SFX */
    audio_play_sfx(state->audio, 0x00, 0x02);
}

/*=============================================================================
 * Ghost Collision Detection — shared helper
 * Checks if ball is within bounding box and looks up collision angle.
 * Returns angle (0-127) or -1 if no collision.
 *===========================================================================*/
/* ASM collision: unsigned 8-bit subtraction from top-left corner.
 * dx = ball_x_hi - entity_x_hi, dy = ball_y_hi - entity_y_hi.
 * If dx >= threshold_x or dy >= threshold_y (unsigned), no collision.
 * Table offset = dy * row_width + dx. */
static int check_collision_table(GameState *state,
    uint16_t entity_x, uint16_t entity_y,
    uint8_t threshold_x, uint8_t threshold_y,
    const uint8_t *angle_table, int row_width)
{
    if (!angle_table)
        return -1;

    uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t ball_y = (uint8_t)(state->ball_y_pos >> 8);
    uint8_t ex = (uint8_t)(entity_x >> 8);
    uint8_t ey = (uint8_t)(entity_y >> 8);

    uint8_t dx = ball_x - ex;
    if (dx >= threshold_x)
        return -1;

    uint8_t dy = ball_y - ey;
    if (dy >= threshold_y)
        return -1;

    int idx = (int)dy * row_width + dx;
    uint8_t val = angle_table[idx];
    if (val & 0x80)
        return -1;  /* No collision */

    return val;
}

/*=============================================================================
 * CheckGengarBonusStageGameObjectCollisions (0x181b1)
 *
 * ASM: Check function ONLY detects collision and sets trigger flags.
 * wTriggeredGameObjectIndex / wTriggeredGameObject / wd657 / wd67c / wd696.
 * All scoring, SFX, force params, and animation are in the resolve function.
 *===========================================================================*/
void check_gengar_bonus_object_collisions(GameState *state) {
    ensure_collision_angles_loaded(state);

    state->which_board_trigger = 0;
    state->which_board_trigger_id = 0;

    /* Check Gastly collisions (CheckGengarBonusStageGastlyCollision, 0x181be) */
    if (state->gastly1_enabled) {
        uint8_t *enabled[3] = { &state->gastly1_enabled, &state->gastly2_enabled, &state->gastly3_enabled };
        Animation *anims[3] = { &state->gastly1_anim, &state->gastly2_anim, &state->gastly3_anim };
        uint16_t *gx[3] = { &state->gastly1_x_pos, &state->gastly2_x_pos, &state->gastly3_x_pos };
        uint16_t *gy[3] = { &state->gastly1_y_pos, &state->gastly2_y_pos, &state->gastly3_y_pos };

        for (int i = 0; i < 3; i++) {
            if (!*enabled[i]) continue;
            if (anims[i]->frame == 4) continue;  /* Skip during fade-out frame */

            /* ASM: add $10 to gastly Y pos before collision check.
             * Table: 32×32 = 1024 bytes. */
            int angle = check_collision_table(state, *gx[i], *gy[i] + MAKE_UFIXED(0x10, 0),
                0x20, 0x20, circle_collision_angles, 32);
            if (angle >= 0) {
                state->collision_normal_angle = (uint8_t)(angle * 2);
                state->is_ball_colliding = 1;
                /* ASM: ld [wTriggeredGameObjectIndex], a  / ld [wd657], a */
                state->gengar_collision_trigger = (uint8_t)(i + 1);
                /* ASM: add $4 / ld [wTriggeredGameObject], a / ld [wd658], a */
                state->gengar_collision_object = (uint8_t)(i + 1 + 4);
                return;
            }
        }
    }

    /* Check Haunter collisions (CheckGengarBonusStageHaunterCollision, 0x18259) */
    if (state->haunter1_enabled || state->haunter2_enabled) {
        uint8_t *h_enabled[2] = { &state->haunter1_enabled, &state->haunter2_enabled };
        Animation *h_anims[2] = { &state->haunter1_anim, &state->haunter2_anim };
        uint16_t *hx[2] = { &state->haunter1_x_pos, &state->haunter2_x_pos };
        uint16_t *hy[2] = { &state->haunter1_y_pos, &state->haunter2_y_pos };

        for (int i = 0; i < 2; i++) {
            if (!*h_enabled[i]) continue;
            if (h_anims[i]->frame == 5) continue;

            /* ASM: add $FE (-2) to haunter X, add $0C to haunter Y.
             * Table: 32×40 = 1280 bytes. */
            int angle = check_collision_table(state, (uint16_t)(*hx[i] - 0x0200), *hy[i] + MAKE_UFIXED(0x0C, 0),
                0x20, 0x28, haunter_collision_angles, 32);
            if (angle >= 0) {
                state->collision_normal_angle = (uint8_t)(angle * 2);
                state->is_ball_colliding = 1;
                /* ASM: ld [wTriggeredGameObjectIndex], a / ld [wd67c], a */
                state->haunter_collision_trigger = (uint8_t)(i + 1);
                /* ASM: add $7 / ld [wTriggeredGameObject], a / ld [wd67d], a */
                state->haunter_collision_object = (uint8_t)(i + 1 + 7);
                return;
            }
        }
    }

    /* Check Gengar collision (CheckGengarBonusStageGengarCollision, 0x182e4) */
    if (state->gengar_enabled) {
        /* ASM: add $c to gengar Y pos before collision check.
         * Table: 48×64 = 3072 bytes. Row width = 48 (dy*3*16). */
        int angle = check_collision_table(state, state->gengar_x_pos, state->gengar_y_pos + MAKE_UFIXED(0x0C, 0),
            0x30, 0x40, gengar_collision_angles_data, 48);
        if (angle >= 0) {
            state->collision_normal_angle = (uint8_t)(angle * 2);
            state->is_ball_colliding = 1;
            /* ASM: ld [wTriggeredGameObjectIndex], a / ld [wd696], a */
            state->gengar_hit_trigger = 1;
            /* ASM: add $9 / ld [wTriggeredGameObject], a / ld [wd697], a */
            state->gengar_hit_object = 0x0A;
            return;
        }
    }

    /* Check Gravestone collisions (GengarBonusStageGravestonesCollision, 0x18350)
     * ASM uses HandleGameObjectCollision (0x2775) → CheckGameObjectCollision (0x27a4)
     * which does centered AABB: |object.x - ball.x| < threshold AND
     * |object.y - ball.y| < threshold, with threshold = $11 (17).
     * ASM also uses IsCollisionInList to pre-check tile collision attributes
     * ($19-$1C, $27, $1D-$20) before AABB — the bounce comes from the tile
     * collision, not the gravestone AABB. We only set which_gravestone for
     * scoring/SFX; the tile collision system handles the bounce direction. */
    if (state->is_ball_colliding) {
        int ball_x = UFIXED_TO_INT(state->ball_x_pos);
        int ball_y = UFIXED_TO_INT(state->ball_y_pos);
        for (int i = 0; i < 4; i++) {
            int dx = ball_x - gravestones[i].x;
            int dy = ball_y - gravestones[i].y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx < GRAVESTONE_SIZE && dy < GRAVESTONE_SIZE) {
                state->which_gravestone = gravestones[i].id;
                return;
            }
        }
    }
}

/* PlayLowTimeSfx (0x107f8) — shared low-time warning SFX */
static void play_low_time_sfx(GameState *state) {
    if (state->timer_frames != 0) return;
    if (state->timer_minutes != 0) return;
    if (state->timer_seconds == 32) {
        audio_play_sfx(state->audio, 0x07, 0x49);
    } else if (state->timer_seconds == 16) {
        audio_play_sfx(state->audio, 0x0A, 0x4A);
    } else if (state->timer_seconds == 5) {
        audio_play_sfx(state->audio, 0x0D, 0x4B);
    }
}

/*=============================================================================
 * ResolveGengarBonusGameObjectCollisions (0x18377)
 *
 * ASM calls: Func_18464 (gastly resolve), Func_1860b (haunter resolve),
 *            Func_187b1 (gengar resolve), Func_18d34 (gravestone resolve),
 *            TryCloseGate, PlayLowTimeSfx, timer expiry.
 *
 * All scoring, SFX, force params, and animation are handled here.
 * The check function only sets trigger flags.
 *===========================================================================*/
void resolve_gengar_bonus_object_collisions(GameState *state) {
    /* ---- Func_18464: Resolve Gastly collision ---- */
    if (state->gastly1_enabled) {
        /* Check if a gastly collision was triggered */
        if (state->gengar_collision_trigger) {
            uint8_t trigger = state->gengar_collision_trigger;
            state->gengar_collision_trigger = 0;

            if (!state->flippers_disabled) {
                /* Find which gastly was hit (trigger=1,2,3 → index 0,1,2) */
                uint8_t *in_hit[3] = { &state->gastly1_in_hit_anim, &state->gastly2_in_hit_anim, &state->gastly3_in_hit_anim };
                Animation *anims[3] = { &state->gastly1_anim, &state->gastly2_anim, &state->gastly3_anim };
                int idx = trigger - 1;
                if (idx >= 0 && idx < 3 && !*in_hit[idx]) {
                    /* Init hit animation */
                    init_animation(anims[idx], gastly_hit_anim);
                    *in_hit[idx] = 1;
                    state->num_gastly_hits++;
                    add_score_with_multiplier(state, GASTLY_HIT_SCORE);
                    /* Rumble */
                    state->rumble_pattern = 0x33;
                    state->rumble_duration = 0x08;
                    /* Force params (ASM: Func_18464 lines after score) */
                    state->flipper_y_force = 0x0100;
                    state->flipper_collision = 0x80;
                    /* SFX */
                    audio_play_sfx(state->audio, 0x00, 0x2C);
                }
            }
        }

        /* Update floating offset for all 3 gastly */
        uint8_t *g_en[3] = { &state->gastly1_enabled, &state->gastly2_enabled, &state->gastly3_enabled };
        Animation *g_anims[3] = { &state->gastly1_anim, &state->gastly2_anim, &state->gastly3_anim };
        uint8_t *g_hit[3] = { &state->gastly1_in_hit_anim, &state->gastly2_in_hit_anim, &state->gastly3_in_hit_anim };
        uint16_t *g_xpos[3] = { &state->gastly1_x_pos, &state->gastly2_x_pos, &state->gastly3_x_pos };
        /* M11: Horizontal floating — Func_1850c bounds per gastly:
         *   gastly1: lower=$08, upper=$30
         *   gastly2: lower=$50, upper=$78
         *   gastly3: lower=$30, upper=$50 */
        static const uint8_t gastly_x_lower[3] = { 0x08, 0x50, 0x30 };
        static const uint8_t gastly_x_upper[3] = { 0x30, 0x78, 0x50 };
        for (int i = 0; i < 3; i++) {
            if (!*g_en[i]) continue;
            if (*g_hit[i]) continue;  /* Don't update floating when in hit */
            /* Y floating */
            state->gastly_y_offset_counter[i] =
                (uint8_t)((state->gastly_y_offset_counter[i] + 1) & 0x1F);
            /* X floating (Func_1850c): update counter, apply delta, check bounds */
            uint8_t cnt = (state->gastly_x_offset_counter[i] + 1) & 0x1F;
            state->gastly_x_offset_counter[i] = cnt;
            uint8_t delta = ghost_y_offset_table[cnt]; /* Same table used for X */
            uint8_t x_lo = (uint8_t)(*g_xpos[i] & 0xFF);
            uint8_t x_hi = (uint8_t)(*g_xpos[i] >> 8);
            if (state->gastly_x_direction[i] == 0) {
                /* Moving right: add delta */
                uint16_t sum = (uint16_t)x_lo + delta;
                x_lo = (uint8_t)(sum & 0xFF);
                x_hi = x_hi + (uint8_t)(sum >> 8);
                *g_xpos[i] = ((uint16_t)x_hi << 8) | x_lo;
                if (x_hi >= gastly_x_upper[i])
                    state->gastly_x_direction[i] = 1;
            } else {
                /* Moving left: subtract delta */
                uint16_t diff = (uint16_t)x_lo - delta;
                x_lo = (uint8_t)(diff & 0xFF);
                x_hi = x_hi - ((diff >> 8) & 1); /* borrow */
                *g_xpos[i] = ((uint16_t)x_hi << 8) | x_lo;
                if (x_hi < gastly_x_lower[i])
                    state->gastly_x_direction[i] = 0;
            }
        }

        /* Update Gastly animations (Func_18562) */
        for (int i = 0; i < 3; i++) {
            const uint8_t *data = *g_hit[i] ? gastly_hit_anim : gastly_idle_anim;
            if (update_animation(g_anims[i], data)) {
                /* Animation finished */
                if (!*g_hit[i]) {
                    /* Idle finished — check if done (index == 4, ASM checks wd69b) */
                    if (g_anims[i]->index == 4) {
                        init_animation(g_anims[i], gastly_idle_anim);
                    }
                } else {
                    /* Hit anim: ASM checks index == 0x12 before transitioning */
                    if (g_anims[i]->index != 0x12) continue;
                    /* Check Gastly → Haunter transition (10 total hits) */
                    if (state->num_gastly_hits >= 10) {
                        state->gastly1_enabled = 0;
                        state->gastly2_enabled = 0;
                        state->gastly3_enabled = 0;
                        state->haunter1_enabled = 1;
                        state->haunter2_enabled = 1;
                        load_haunter_tiles(state);
                        audio_play_music(state->audio, 0x0F, 0x06);
                    } else {
                        /* Count non-hit gastly + hits to see if we've reached 10 */
                        uint8_t count = state->num_gastly_hits;
                        for (int j = 0; j < 3; j++) {
                            if (!*g_hit[j]) count++;
                        }
                        if (count >= 10) {
                            /* About to transition — don't restart idle */
                        } else {
                            *g_hit[i] = 0;
                            init_animation(g_anims[i], gastly_idle_anim);
                        }
                    }
                }
            }
        }
    }

    /* ---- Func_1860b: Resolve Haunter collision ---- */
    if (state->haunter1_enabled || state->haunter2_enabled) {
        /* Check if a haunter collision was triggered */
        if (state->haunter_collision_trigger) {
            uint8_t trigger = state->haunter_collision_trigger;
            state->haunter_collision_trigger = 0;

            if (!state->flippers_disabled) {
                uint8_t *h_hit[2] = { &state->haunter1_in_hit_anim, &state->haunter2_in_hit_anim };
                Animation *h_anims[2] = { &state->haunter1_anim, &state->haunter2_anim };
                int idx = trigger - 1;
                if (idx >= 0 && idx < 2 && !*h_hit[idx]) {
                    init_animation(h_anims[idx], haunter_hit_anim);
                    *h_hit[idx] = 1;
                    state->num_haunter_hits++;
                    add_score_with_multiplier(state, HAUNTER_HIT_SCORE);
                    state->rumble_pattern = 0x33;
                    state->rumble_duration = 0x08;
                    state->flipper_y_force = 0x0100;
                    state->flipper_collision = 0x80;
                    audio_play_sfx(state->audio, 0x00, 0x2D);
                }
            }
        }

        /* Update floating offset + horizontal floating (M11: Func_186a1) */
        uint8_t *h_en[2] = { &state->haunter1_enabled, &state->haunter2_enabled };
        uint8_t *h_hit2[2] = { &state->haunter1_in_hit_anim, &state->haunter2_in_hit_anim };
        Animation *h_anims2[2] = { &state->haunter1_anim, &state->haunter2_anim };
        uint16_t *h_xpos[2] = { &state->haunter1_x_pos, &state->haunter2_x_pos };
        /* Haunter 1: lower=$50, upper=$78; Haunter 2: lower=$10, upper=$38 */
        static const uint8_t haunter_x_lower[2] = { 0x50, 0x10 };
        static const uint8_t haunter_x_upper[2] = { 0x78, 0x38 };
        for (int i = 0; i < 2; i++) {
            if (!*h_en[i]) continue;
            if (*h_hit2[i]) continue;
            /* Y floating */
            state->haunter_y_offset_counter[i] =
                (uint8_t)((state->haunter_y_offset_counter[i] + 1) & 0x1F);
            /* X floating (Func_186a1) */
            uint8_t cnt = (state->haunter_x_offset_counter[i] + 1) & 0x1F;
            state->haunter_x_offset_counter[i] = cnt;
            uint8_t delta = ghost_y_offset_table[cnt];
            uint8_t x_lo = (uint8_t)(*h_xpos[i] & 0xFF);
            uint8_t x_hi = (uint8_t)(*h_xpos[i] >> 8);
            if (state->haunter_x_direction[i] == 0) {
                uint16_t sum = (uint16_t)x_lo + delta;
                x_lo = (uint8_t)(sum & 0xFF);
                x_hi = x_hi + (uint8_t)(sum >> 8);
                *h_xpos[i] = ((uint16_t)x_hi << 8) | x_lo;
                if (x_hi >= haunter_x_upper[i])
                    state->haunter_x_direction[i] = 1;
            } else {
                uint16_t diff = (uint16_t)x_lo - delta;
                x_lo = (uint8_t)(diff & 0xFF);
                x_hi = x_hi - ((diff >> 8) & 1);
                *h_xpos[i] = ((uint16_t)x_hi << 8) | x_lo;
                if (x_hi < haunter_x_lower[i])
                    state->haunter_x_direction[i] = 0;
            }
        }

        /* Update Haunter animations (Func_186f7) */
        for (int i = 0; i < 2; i++) {
            const uint8_t *data = *h_hit2[i] ? haunter_hit_anim : haunter_idle_anim;
            if (update_animation(h_anims2[i], data)) {
                if (!*h_hit2[i]) {
                    /* ASM checks index == 4 (wd69b field) */
                    if (h_anims2[i]->index == 4) {
                        init_animation(h_anims2[i], haunter_idle_anim);
                    }
                } else {
                    /* Hit anim: ASM checks index for state transitions */
                    if (h_anims2[i]->index == 0x12) {
                        /* Index $12: check if all 10 hits done */
                        if (state->num_haunter_hits >= 10) {
                            /* M12: Ground tiles + collision map patches at $12
                             * ASM: wd656=1, Func_18d72, Func_18d91, MUSIC_NOTHING */
                            state->gengar_gate_state = 1;
                            /* Func_18d72: queue ground tile graphics (cosmetic) */
                            /* Func_18d91: patch collision map at 4 locations
                             * Data_18dd2 = all zeros (clear ground collision) */
                            {
                                static const uint8_t clear_patch[9] = {0,0,0,0,0,0,0,0,0};
                                uint16_t offsets[4] = {0xC7, 0xAE, 0x123, 0x14D};
                                for (int p = 0; p < 4; p++) {
                                    uint16_t base = offsets[p];
                                    for (int row = 0; row < 3; row++) {
                                        for (int col = 0; col < 3; col++) {
                                            uint16_t addr = base + row * 0x20 + col;
                                            if (addr < 0x300)
                                                state->stage_collision_map[addr] = clear_patch[row*3+col];
                                        }
                                    }
                                }
                            }
                            /* L1: MUSIC_NOTHING plays at index $12 (not $13) */
                            audio_play_music(state->audio, 0x00, 0x00);
                        } else {
                            uint8_t count = state->num_haunter_hits;
                            for (int j = 0; j < 2; j++) {
                                if (!*h_hit2[j]) count++;
                            }
                            if (count < 10) {
                                *h_hit2[i] = 0;
                                init_animation(h_anims2[i], haunter_idle_anim);
                            }
                        }
                    } else if (h_anims2[i]->index == 0x13) {
                        /* Index $13: Haunter → Gengar transition (ASM .asm_18761)
                         * L1: PlayCry(GENGAR) at index $13 */
                        if (state->num_haunter_hits >= 10) {
                            state->gengar_enabled = 1;
                            state->haunter1_enabled = 0;
                            state->haunter2_enabled = 0;
                            load_gengar_tiles(state);
                            /* PlayCry(GENGAR) — ASM: ld de, GENGAR / call PlayCry */
                            audio_play_cry(state->audio, 94); /* GENGAR = species 94 */
                        }
                    }
                }
            }
        }
    }

    /* ---- Func_187b1: Resolve Gengar collision + movement ---- */
    if (state->gengar_enabled) {
        /* Handle Gengar hit trigger from check function */
        if (state->gengar_hit_trigger) {
            state->gengar_hit_trigger = 0;

            if (!state->flippers_disabled) {
                state->num_gengar_hits++;
                if (state->num_gengar_hits >= 5) {
                    /* Death sequence (ASM .asm_18804) */
                    init_animation(&state->gengar_anim, gengar_death_anim);
                    state->gengar_phase = 3;  /* ASM: ld a, $3 / ld [de], a */
                    state->flippers_disabled = 1;
                    load_flippers_palette_gengar(state); /* ASM line 722 */
                    stop_timer(state);
                    audio_play_music(state->audio, 0x00, 0x00);
                } else {
                    /* Normal hit */
                    init_animation(&state->gengar_anim, gengar_normal_hit_anim);
                    state->gengar_phase = 2;  /* ASM: ld a, $2 / ld [de], a */
                    audio_play_sfx(state->audio, 0x00, 0x37);
                }
                add_score_with_multiplier(state, GENGAR_HIT_SCORE);
                state->rumble_pattern = 0x33;
                state->rumble_duration = 0x08;
                state->flipper_y_force = 0x0200;
                state->flipper_collision = 0x80;
                /* ASM: nudge Gengar Y up by 0xFF00 (subtract 1 pixel) on hit */
                state->gengar_y_pos = (uint16_t)(state->gengar_y_pos - MAKE_UFIXED(1, 0));
            }
        }

        /* Gengar descent/ascent movement (Func_18876 for phase<2, Func_188e1 for phase>=2) */
        if (state->gengar_phase < 2) {
            /* ---- Func_18876: Pre-hit descent ---- */
            /* Check for rumble: if prev_anim_state was NOT 1 or 2, but current IS 1 or 2 */
            uint8_t prev = state->gengar_prev_anim_state;
            uint8_t curr_anim = state->gengar_anim.frame;
            if (prev != 1 && prev != 2) {
                if (curr_anim == 1 || curr_anim == 2) {
                    state->gengar_upper_tilt_active = 1;
                    state->rumble_pattern = 0x11;
                    state->rumble_duration = 0x08;
                }
            }

            /* Move Y based on animation state changes */
            if (curr_anim != state->gengar_prev_anim_state) {
                if (state->gengar_phase == 0) {
                    /* Phase 0: check Y range (0x80-0xA0 in signed = 0x00-0x20 unsigned) */
                    uint8_t gy = UFIXED_TO_INT(state->gengar_y_pos);
                    uint8_t gy_signed = (uint8_t)(gy + 128);
                    if (gy_signed < 160) {
                        /* Within visible range — move based on anim state */
                        if (curr_anim != 0) {
                            /* Non-zero anim: add $0300 to Y (move down by 3) */
                            state->gengar_y_pos = (uint16_t)(state->gengar_y_pos + MAKE_UFIXED(3, 0));
                        } else {
                            /* Zero anim: add $0100 to Y (move down by 1) */
                            state->gengar_y_pos = (uint16_t)(state->gengar_y_pos + MAKE_UFIXED(1, 0));
                        }
                    }
                }
            }
            state->gengar_prev_anim_state = curr_anim;
        } else {
            /* ---- Func_188e1: Post-hit ascent (phase >= 2) ---- */
            /* Rumble on animation transition to frame 1 or 2 */
            uint8_t prev = state->gengar_prev_anim_state;
            uint8_t curr_anim = state->gengar_anim.frame;
            if (prev != 1 && prev != 2) {
                if (curr_anim == 1 || curr_anim == 2) {
                    state->rumble_pattern = 0x01;
                    state->rumble_duration = 0x08;
                }
            }

            /* Don't move if frame == 6 (invisible/flash frame) */
            if (curr_anim != 6) {
                if (curr_anim != state->gengar_prev_anim_state) {
                    /* Phase 3 death: check if animation frame < 9 (early death) */
                    if (state->gengar_phase == 3 && state->gengar_anim.index < 9) {
                        /* Don't move during early death frames */
                    } else {
                        /* Move based on anim state.
                         * Clamp Y so Gengar stops at the gate (top of stage)
                         * instead of wrapping past Y=0. The descent handler
                         * has a similar range check at Y=32; for ascent we
                         * stop at Y <= 8 (gate area). */
                        uint8_t gy = UFIXED_TO_INT(state->gengar_y_pos);
                        if (gy >= 8 && gy < 0x80) {
                            if (curr_anim != 0) {
                                /* Non-zero: add $FD00 to Y (move up by 3) */
                                state->gengar_y_pos = (uint16_t)(state->gengar_y_pos - MAKE_UFIXED(3, 0));
                            } else {
                                /* Zero: add $FF00 to Y (move up by 1) */
                                state->gengar_y_pos = (uint16_t)(state->gengar_y_pos - MAKE_UFIXED(1, 0));
                            }
                        }
                    }
                }
            }
            state->gengar_prev_anim_state = curr_anim;
        }

        /* Update Gengar animation (Func_189af)
         * AnimationDataPointers_18a57: [idle, approach, normal_hit, death, waiting] */
        {
            const uint8_t *anim_data;
            switch (state->gengar_phase) {
                case 0: anim_data = gengar_idle_anim; break;
                case 1: anim_data = gengar_approach_anim; break;
                case 2: anim_data = gengar_normal_hit_anim; break;
                case 3: anim_data = gengar_death_anim; break;
                case 4: default: anim_data = gengar_waiting_anim; break;
            }

            if (update_animation(&state->gengar_anim, anim_data)) {
                /* Animation frame transition — handle phase transitions (Func_189af)
                 * ASM callbacks check animation INDEX field, not frame. */
                switch (state->gengar_phase) {
                    case 0:
                        /* Idle: index == 4 → restart idle */
                        if (state->gengar_anim.index == 4) {
                            init_animation(&state->gengar_anim, gengar_idle_anim);
                        }
                        break;
                    case 1:
                        /* Approach: index == 0x60 → go to idle (phase 0) */
                        if (state->gengar_anim.index == 0x60) {
                            init_animation(&state->gengar_anim, gengar_idle_anim);
                            state->gengar_phase = 0;
                        }
                        break;
                    case 2:
                        /* Normal hit: index == 3 → back to approach (phase 1) */
                        if (state->gengar_anim.index == 3) {
                            init_animation(&state->gengar_anim, gengar_approach_anim);
                            state->gengar_phase = 1;
                        }
                        break;
                    case 3:
                        /* Death: index == 1 → play SFX, index == 0xFE → stage cleared */
                        if (state->gengar_anim.index == 1) {
                            audio_play_sfx(state->audio, 0x00, 0x2E);
                        } else if (state->gengar_anim.index == 0xFE) {
                            state->gengar_defeated = 1;
                            state->next_bonus_stage = BONUS_STAGE_ORDER_MEWTWO;
                            state->completed_bonus_stage = 1;
                            fill_bottom_message_buffer_with_black_tile(state);
                            enable_bottom_text(state);
                            const char *text = "GENGAR STAGE CLEARED";
                            /* scrolling_text_normal 0, 20, 0, 21 → {5, 0x54, 0x40, 20, 0, 61} */
                            uint8_t header[6] = { 5, 0x54, 0x40, 20, 0, 61 };
                            load_scrolling_text(state, 2, header, text);
                            audio_play_sfx(state->audio, 0x4B, 0x2A);
                        }
                        break;
                    case 4:
                        /* Waiting: index == 2 → restart idle, go to phase 0, play music */
                        if (state->gengar_anim.index == 2) {
                            init_animation(&state->gengar_anim, gengar_idle_anim);
                            state->gengar_phase = 0;
                            audio_play_music(state->audio, 0x0F, 0x05);
                        }
                        break;
                }
            }
        }

        /* Upper tilt push logic (Func_1894c) */
        if (!state->gengar_upper_tilt_cooldown) {
            if (state->gengar_upper_tilt_active) {
                if (state->gengar_upper_tilt_counter >= 3) {
                    /* Reached max push: play SFX and start cooldown */
                    audio_play_sfx(state->audio, 0x00, 0x2B);
                    state->gengar_upper_tilt_cooldown = 1;
                    state->gengar_upper_tilt_active = 0;
                } else {
                    state->gengar_upper_tilt_counter++;
                    /* Push ball down and scroll up */
                    if (state->pinball_is_visible && state->enable_ball_gravity_and_tilt) {
                        state->ball_y_pos += MAKE_UFIXED(1, 0);
                    }
                    state->upper_tilt_pixels_offset--;
                    state->upper_tilt_pushing = 1;
                }
            } else {
                state->upper_tilt_pushing = 0;
                /* Recovery: move tilt offset back */
                if (state->gengar_upper_tilt_counter > 0) {
                    state->gengar_upper_tilt_counter--;
                    state->upper_tilt_pixels_offset++;
                } else {
                    if (!state->gengar_upper_tilt_active) {
                        state->gengar_upper_tilt_cooldown = 0;
                    }
                }
            }
        } else {
            state->upper_tilt_pushing = 0;
            if (state->gengar_upper_tilt_counter > 0) {
                state->gengar_upper_tilt_counter--;
                state->upper_tilt_pixels_offset++;
            } else {
                if (!state->gengar_upper_tilt_active) {
                    state->gengar_upper_tilt_cooldown = 0;
                }
            }
        }

        /* Stage cleared is now handled by the death animation callback
         * (case 3, index == 0xFE) in the animation dispatch above.
         * ASM Func_189af .asm_18a14 triggers at animation index $FE. */
    }

    /* ---- Func_18d34: Resolve Gravestone collision ---- */
    if (state->which_gravestone) {
        uint8_t grav = state->which_gravestone;
        state->which_gravestone = 0;
        if (!state->flippers_disabled) {
            add_score_with_multiplier(state, GRAVESTONE_SCORE);
            state->rumble_pattern = 0xFF;
            state->rumble_duration = 0x03;
            /* Bounce direction comes from tile collision, not AABB.
             * ASM: HandleGameObjectCollision uses IsCollisionInList to
             * gate on tile attribute — tile collision handles the force. */
            audio_play_sfx(state->audio, 0x00, 0x2F);
        }
        (void)grav;
    }

    /* ---- TryCloseGate_GengarBonus (0x183b7) ---- */
    if (!state->gengar_bonus_closed_gate) {
        uint8_t bx = UFIXED_TO_INT(state->ball_x_pos);
        if (bx < 0x8A) {
            state->gengar_bonus_closed_gate = 1;
            state->stage_collision_state = 1;
            load_stage_collision_attributes(state);
        }
    }

    /* PlayLowTimeSfx + timer expiry check */
    play_low_time_sfx(state);
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->flippers_disabled = 1;
        load_flippers_palette_gengar(state); /* ASM line 15 */
        stop_timer(state);
        if (state->num_gengar_hits < 5) {
            state->gengar_defeated = 1;
        }
    }
}

/*=============================================================================
 * DrawSpritesGengarBonus (0x18faf)
 *===========================================================================*/

/* Forward declarations for shared draw functions from draw_red_field.c */
extern void draw_timer(GameState *state, uint8_t x, uint8_t y);
extern void draw_pinball(GameState *state);
extern void draw_flipper_sprites(GameState *state);

void draw_gengar_bonus_sprites(GameState *state) {
    /* Draw timer — ld bc, $7f00 (top-right) */
    draw_timer(state, 0x7F, 0x00);

    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Draw Gastly (Func_19020 → Func_19033)
     * ASM SpriteIds_Gastly: frames 0-3 valid, frame 4 = $FF (don't draw).
     * ASM uses wd674 countdown to suppress drawing during tile loading. */
    if (state->gastly1_enabled || state->gastly2_enabled || state->gastly3_enabled) {
        if (state->gastly_gfx_countdown > 0) {
            state->gastly_gfx_countdown--;
        } else {
            uint8_t *g_en[3] = { &state->gastly1_enabled, &state->gastly2_enabled, &state->gastly3_enabled };
            Animation *g_anim[3] = { &state->gastly1_anim, &state->gastly2_anim, &state->gastly3_anim };
            uint16_t *g_x[3] = { &state->gastly1_x_pos, &state->gastly2_x_pos, &state->gastly3_x_pos };
            uint16_t *g_y[3] = { &state->gastly1_y_pos, &state->gastly2_y_pos, &state->gastly3_y_pos };
            for (int i = 0; i < 3; i++) {
                if (!*g_en[i]) continue;
                uint8_t frame = g_anim[i]->frame;
                if (frame >= 4) continue;  /* Frame 4 = $FF in ASM = invisible */
                uint8_t sx = (uint8_t)(UFIXED_TO_INT(*g_x[i]) - scx);
                uint8_t sy = (uint8_t)(UFIXED_TO_INT(*g_y[i]) - scy);
                load_sprite_data(state, gastly_sprites[frame], sy, sx);
            }
        }
    }

    /* Draw Haunter (Func_190b9 → Func_190c6)
     * ASM SpriteIds_Haunter: frames 0-4 valid, frame 5 = $FF (don't draw).
     * ASM uses wd690 countdown to suppress drawing during tile loading. */
    if (state->haunter1_enabled || state->haunter2_enabled) {
        if (state->haunter_gfx_countdown > 0) {
            state->haunter_gfx_countdown--;
        } else {
            uint8_t *h_en[2] = { &state->haunter1_enabled, &state->haunter2_enabled };
            Animation *h_anim[2] = { &state->haunter1_anim, &state->haunter2_anim };
            uint16_t *h_x[2] = { &state->haunter1_x_pos, &state->haunter2_x_pos };
            uint16_t *h_y[2] = { &state->haunter1_y_pos, &state->haunter2_y_pos };
            for (int i = 0; i < 2; i++) {
                if (!*h_en[i]) continue;
                uint8_t frame = h_anim[i]->frame;
                if (frame >= 5) continue;  /* Frame 5 = $FF in ASM = invisible */
                uint8_t sx = (uint8_t)(UFIXED_TO_INT(*h_x[i]) - scx);
                uint8_t sy = (uint8_t)(UFIXED_TO_INT(*h_y[i]) - scy);
                load_sprite_data(state, haunter_sprites[frame], sy, sx);
            }
        }
    }

    /* Draw Gengar (Func_19185 → Func_1918c)
     * ASM SpriteIds_Gengar: frames 0-5 valid, frame 6 = $FF (don't draw).
     * ASM uses wd6a1 countdown to suppress drawing during tile loading. */
    if (state->gengar_enabled) {
        if (state->gengar_gfx_countdown > 0) {
            state->gengar_gfx_countdown--;
        } else {
            uint8_t frame = state->gengar_anim.frame;
            if (frame != 6) {
                const uint8_t *sprite;
                if (frame == 5)
                    sprite = sprite_gengar_hit;
                else if (frame < 3)
                    sprite = gengar_sprites[frame];
                else if (frame == 3)
                    sprite = sprite_gengar_frame0;
                else if (frame == 4)
                    sprite = sprite_gengar_frame1;
                else
                    sprite = sprite_gengar_frame0;

                uint8_t sx = (uint8_t)(UFIXED_TO_INT(state->gengar_x_pos) - scx);
                uint8_t sy = (uint8_t)(UFIXED_TO_INT(state->gengar_y_pos) - scy);
                load_sprite_data(state, sprite, sy, sx);
            }
        }
    }

    /* Draw flippers and ball */
    draw_flipper_sprites(state);
    draw_pinball(state);
}

/*=============================================================================
 * Ghost sprite tile loading helpers
 *
 * ASM loads ghost tiles progressively via countdown in DrawSpritesGengarBonus.
 * In C we load all tiles at once during phase transitions since we have no
 * VRAM timing constraints.  The base stage image (gengar_bonus_base_gameboycolor.png)
 * already contains gastly tiles at $8900, so only haunter/gengar need explicit loads.
 *
 * VRAM layout (from ASM GastlyVideoData / HaunterGfxTable / GengarGfxTable):
 *   Entries 0-4: source $000-$19F → $8900 bank 0 (vTilesSH tile $10, $1A0 bytes)
 *   Entries 5-7: source $1A0-$29F → $81A0 bank 0 (vTilesOB tile $1A, $100 bytes)
 *   Gengar extra: source $2A0→$82A0 ($160), $400→$87A0 ($60), $460→$8AA0 ($2A0)
 *===========================================================================*/
static void load_gastly_tiles(GameState *state) {
    if (!state->vram) return;
    char path[260];
    size_t data_size;
    snprintf(path, sizeof(path), "%s/gfx/stage/gengar_bonus/gastly.interleave.png",
             state->asset_base_path);
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        /* PNG is 32px wide = 4 tiles/row */
        if (data_size >= 32) interleave_tiles(tile_data, data_size, 4);
        uint16_t copy = (data_size > 0x180) ? 0x180 : (uint16_t)data_size;
        vram_write(state->vram, 0, 0x8900, tile_data, copy);
        free(tile_data);
    }
}

static void load_haunter_tiles(GameState *state) {
    if (!state->vram) return;
    char path[260];
    size_t data_size;
    snprintf(path, sizeof(path), "%s/gfx/stage/gengar_bonus/haunter.interleave.png",
             state->asset_base_path);
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        /* PNG is 32px wide = 4 tiles/row */
        if (data_size >= 32) interleave_tiles(tile_data, data_size, 4);
        if (data_size >= 0x2A0) {
            /* Entries 0-4: overwrite gastly tile area + extra ($8900-$8A9F) */
            vram_write(state->vram, 0, 0x8900, tile_data, 0x1A0);
            /* Entries 5-7: OBJ tile area ($81A0-$829F) */
            vram_write(state->vram, 0, 0x81A0, tile_data + 0x1A0, 0x100);
        }
        free(tile_data);
    }
}

static void load_gengar_tiles(GameState *state) {
    if (!state->vram) return;
    char path[260];
    size_t data_size;
    snprintf(path, sizeof(path), "%s/gfx/stage/gengar_bonus/gengar.interleave.png",
             state->asset_base_path);
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        /* PNG is 32px wide = 4 tiles/row */
        if (data_size >= 32) interleave_tiles(tile_data, data_size, 4);
        if (data_size >= 0x2A0) {
            /* Entries 0-4: overwrite previous tile area ($8900-$8A9F) */
            vram_write(state->vram, 0, 0x8900, tile_data, 0x1A0);
            /* Entries 5-7: OBJ tile area ($81A0-$829F) */
            vram_write(state->vram, 0, 0x81A0, tile_data + 0x1A0, 0x100);
        }
        /* Extra gengar data beyond the 8-entry table */
        if (data_size >= 0x460 + 0x2A0) {
            vram_write(state->vram, 0, 0x82A0, tile_data + 0x2A0, 0x160);
            vram_write(state->vram, 0, 0x87A0, tile_data + 0x400, 0x60);
            vram_write(state->vram, 0, 0x8AA0, tile_data + 0x460, 0x2A0);
        }
        free(tile_data);
    }
}

/*=============================================================================
 * _LoadStageDataGengarBonus (0x1818b)
 *
 * ASM calls: LoadBallGraphics, LoadFlippersPalette, Func_18d72,
 *            LoadTimerGraphics.  The ASM does NOT load ghost tiles here —
 * those are loaded progressively by the draw function's countdown system
 * (Func_19070/19104/191cb).  In C, we load all tiles at once but must
 * load the CORRECT phase's tiles (not always gastly).
 *===========================================================================*/
void load_stage_data_gengar_bonus(GameState *state) {
    if (!state->vram) return;

    /* ASM: LoadFlippersPalette (load_gengar_bonus.asm line 3) */
    load_flippers_palette_gengar(state);

    /* Load ghost tiles for the current active phase.
     * On first entry, gastly are enabled → load gastly tiles.
     * On ball respawn during haunter/gengar, load their tiles instead.
     * The ASM relies on countdown-based progressive loading in the draw
     * function, but since we load all at once, we must pick the right set. */
    if (state->gengar_enabled) {
        load_gengar_tiles(state);
    } else if (state->haunter1_enabled || state->haunter2_enabled) {
        load_haunter_tiles(state);
    } else {
        /* Gastly phase (default / first entry) */
        load_gastly_tiles(state);
    }
}
