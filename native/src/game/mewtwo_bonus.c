/*
 * Mewtwo Bonus Stage (Stage ID $09)
 *
 * Boss battle with 6 orbiting energy balls shielding Mewtwo.
 * Timer: 2:00. Defeat requires 8 hit groups (3 orb hits per group = 24 total).
 *
 * Translated from engine/pinball_game/:
 *   stage_init/init_mewtwo_bonus.asm (0x1924f)
 *   ball_init/ball_init_mewtwo_bonus.asm (0x192e3)
 *   ball_loss/ball_loss_mewtwo_bonus.asm (0xdf7e)
 *   object_collision/mewtwo_bonus_object_collision.asm (0x19330)
 *   object_collision/mewtwo_bonus_resolve_collision.asm (0x19451)
 *   draw_sprites/draw_mewtwo_bonus_sprites.asm (0x1994e)
 *   load_stage_data/load_mewtwo_bonus.asm (0x19310)
 */

#include "game/mewtwo_bonus.h"
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
#include "renderer/stage_assets.h"
#include "renderer/stage_palettes.h"
#include "renderer/vram.h"
#include <string.h>

/*=============================================================================
 * Constants
 *===========================================================================*/
/* Score values loaded from config/scores.json */

/*=============================================================================
 * Orbiting Ball Physics Data — loaded from ball_physics_e4000.bin
 * 32x32 grid, 4 bytes per entry = 4096 bytes
 *===========================================================================*/
static uint8_t *orb_physics_data = NULL;
static size_t orb_physics_size = 0;

static void ensure_orb_physics_loaded(GameState *state) {
    if (!orb_physics_data) {
        char path[260];
        snprintf(path, sizeof(path), "%s/data/collision/ball_physics_e4000.bin",
                 state->asset_base_path);
        orb_physics_data = load_binary_file(path, &orb_physics_size);
    }
}

/*=============================================================================
 * Orbiting Ball Coordinate Table (MewtwoOrbitingBallsCoords, 72 entries)
 * Each entry is (x, y). Ball positions cycle through this table.
 *===========================================================================*/
static const uint8_t orbiting_ball_coords[72][2] = {
    {0x62, 0x08}, {0x62, 0x0A}, {0x62, 0x0D}, {0x61, 0x0F},
    {0x60, 0x11}, {0x60, 0x13}, {0x5F, 0x15}, {0x5D, 0x17},
    {0x5C, 0x19}, {0x5A, 0x1A}, {0x59, 0x1C}, {0x57, 0x1D},
    {0x55, 0x1F}, {0x53, 0x20}, {0x51, 0x20}, {0x4F, 0x21},
    {0x4D, 0x22}, {0x4A, 0x22}, {0x48, 0x22}, {0x46, 0x22},
    {0x43, 0x22}, {0x41, 0x21}, {0x3F, 0x20}, {0x3D, 0x20},
    {0x3B, 0x1F}, {0x39, 0x1D}, {0x37, 0x1C}, {0x36, 0x1A},
    {0x34, 0x19}, {0x33, 0x17}, {0x31, 0x15}, {0x30, 0x13},
    {0x30, 0x11}, {0x2F, 0x0F}, {0x2E, 0x0D}, {0x2E, 0x0A},
    {0x2E, 0x08}, {0x2E, 0x06}, {0x2E, 0x03}, {0x2F, 0x01},
    {0x30, 0xFF}, {0x30, 0xFD}, {0x31, 0xFB}, {0x33, 0xF9},
    {0x34, 0xF7}, {0x36, 0xF6}, {0x37, 0xF4}, {0x39, 0xF3},
    {0x3B, 0xF1}, {0x3D, 0xF0}, {0x3F, 0xF0}, {0x41, 0xEF},
    {0x43, 0xEE}, {0x46, 0xEE}, {0x48, 0xEE}, {0x4A, 0xEE},
    {0x4D, 0xEE}, {0x4F, 0xEF}, {0x51, 0xF0}, {0x53, 0xF0},
    {0x55, 0xF1}, {0x57, 0xF3}, {0x59, 0xF4}, {0x5A, 0xF6},
    {0x5C, 0xF7}, {0x5D, 0xF9}, {0x5F, 0xFB}, {0x60, 0xFD},
    {0x60, 0xFF}, {0x61, 0x01}, {0x62, 0x03}, {0x62, 0x06},
};

/*=============================================================================
 * Orbiting Ball Start Coord Indices (OrbitingBallsStartCoordsIndices)
 * When Mewtwo is hit, orbs reset. This table determines starting orbit
 * positions per regen phase. 0xFF = orb disabled.
 *===========================================================================*/
static const uint8_t orb_start_indices[9][8] = {
    {0x00, 0x0C, 0x18, 0x24, 0x30, 0x3C, 0xFF, 0xFF}, /* phase 0: 6 orbs */
    {0x00, 0x0E, 0x1D, 0x2B, 0x3A, 0xFF, 0xFF, 0xFF}, /* phase 1: 5 orbs */
    {0x00, 0x12, 0x24, 0x36, 0xFF, 0xFF, 0xFF, 0xFF}, /* phase 2: 4 orbs */
    {0x00, 0x12, 0x24, 0x36, 0xFF, 0xFF, 0xFF, 0xFF}, /* phase 3: 4 orbs */
    {0x00, 0x18, 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* phase 4: 3 orbs */
    {0x00, 0x24, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* phase 5: 2 orbs */
    {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* phase 6: 1 orb  */
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* phase 7: 0 orbs */
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, /* phase 8: unused */
};

/*=============================================================================
 * Initial Orbiting Ball Data (InitMewtwoOrbitingBallData)
 * 12 × 4 bytes copied to 6 balls (8 bytes each): first 4 bytes, then next 4.
 *===========================================================================*/
static const uint8_t init_orb_data[48] = {
    0x01, 0x01, 0x00, 0x00,  0x00, 0x62, 0x08, 0x00,  /* ball 0 */
    0x01, 0x01, 0x00, 0x00,  0x00, 0x55, 0x1F, 0x0C,  /* ball 1 */
    0x01, 0x01, 0x00, 0x00,  0x00, 0x3B, 0x1F, 0x18,  /* ball 2 */
    0x01, 0x01, 0x00, 0x00,  0x00, 0x2E, 0x08, 0x24,  /* ball 3 */
    0x01, 0x01, 0x00, 0x00,  0x00, 0x3B, 0xF1, 0x30,  /* ball 4 */
    0x01, 0x01, 0x00, 0x00,  0x00, 0x55, 0xF1, 0x3C,  /* ball 5 */
};

/*=============================================================================
 * Animation Data Tables
 *===========================================================================*/

/* Mewtwo Idle (MewtwoIdleAnimation, 0x19699) */
static const uint8_t mewtwo_idle_anim[] = {
    0x30, 0,  /* BASE for 48 frames */
    0x04, 5,  /* IDLE_2 for 4 frames */
    0x34, 4,  /* IDLE_1 for 52 frames */
    0x03, 5,  /* IDLE_2 for 3 frames */
    0x00       /* terminator */
};

/* Mewtwo Regenerating (MewtwoRegeneratingAnimation, 0x196a2) */
static const uint8_t mewtwo_regen_anim[] = {
    0x0A, 0,  /* BASE */
    0x06, 1,  /* REGENERATING_1 */
    0x05, 2,  /* REGENERATING_2 */
    0x05, 1,
    0x04, 2,
    0x04, 1,
    0x04, 2,
    0x03, 1,
    0x03, 2,
    0x03, 1,
    0x03, 2,
    0x04, 0,  /* BASE */
    0x40, 3,  /* REGENERATING_3 */
    0x00
};

/* Mewtwo Hit (MewtwoHitAnimation, 0x196bd) */
static const uint8_t mewtwo_hit_anim[] = {
    0x10, 6,  /* HIT for 16 frames */
    0x00
};

/* Mewtwo Defeated (MewtwoDefeatedAnimation, 0x196c0) — flashing sequence */
static const uint8_t mewtwo_defeated_anim[] = {
    0x04, 6, 0x04, 7,  /* HIT/INVISIBLE x4 (4 frames each) */
    0x04, 6, 0x04, 7,
    0x04, 6, 0x04, 7,
    0x04, 6, 0x04, 7,
    0x03, 6, 0x03, 7,  /* 3 frames each */
    0x03, 6, 0x03, 7,
    0x03, 6, 0x03, 7,
    0x03, 6, 0x03, 7,
    0x02, 6, 0x02, 7,  /* 2 frames each */
    0x02, 6, 0x02, 7,
    0x02, 6, 0x02, 7,
    0x02, 6, 0x02, 7,
    0x01, 6, 0x01, 7,  /* 1 frame each */
    0x01, 6, 0x01, 7,
    0x01, 6, 0x01, 7,
    0x01, 6, 0x01, 7,
    0x00
};

/* Pointer array for animation dispatch (index 0-3) */
static const uint8_t *mewtwo_animations[] = {
    mewtwo_idle_anim,
    mewtwo_regen_anim,
    mewtwo_hit_anim,
    mewtwo_defeated_anim,
};

/* Orbiting Ball Animation 1: Full-size cycle (OrbitingBallAnimation1, 0x1991e) */
static const uint8_t orb_fullsize_anim[] = {
    0x0A, 0,  /* FULL_SIZE_0 */
    0x08, 1,  /* FULL_SIZE_1 */
    0x08, 2,  /* FULL_SIZE_2 */
    0x0A, 3,  /* FULL_SIZE_3 */
    0x08, 2,  /* FULL_SIZE_2 */
    0x08, 1,  /* FULL_SIZE_1 */
    0x00
};

/* Orbiting Ball Animation 2: Growing (OrbitingBallAnimation2, 0x1992b) */
static const uint8_t orb_growing_anim[] = {
    0x05, 4,  /* GROWING_0 */
    0x06, 5,  /* GROWING_1 */
    0x06, 6,  /* GROWING_2 */
    0x07, 7,  /* GROWING_3 */
    0x07, 8,  /* GROWING_4 */
    0x08, 9,  /* GROWING_5 */
    0x08, 10, /* GROWING_6 */
    0x00
};

/* Orbiting Ball Animation 3: Shrinking (OrbitingBallAnimation3, 0x1993a) */
static const uint8_t orb_shrinking_anim[] = {
    0x05, 10, /* GROWING_6 */
    0x05, 9,  /* GROWING_5 */
    0x04, 8,  /* GROWING_4 */
    0x04, 7,  /* GROWING_3 */
    0x03, 6,  /* GROWING_2 */
    0x03, 5,  /* GROWING_1 */
    0x02, 4,  /* GROWING_0 */
    0x80, 11, /* INVISIBLE long wait */
    0x00
};

/* Orbiting Ball Animation 4: Hidden wait (OrbitingBallAnimation4, 0x1994b) */
static const uint8_t orb_hidden_anim[] = {
    0x0C, 11, /* INVISIBLE */
    0x00
};

static const uint8_t *orb_animations[] = {
    orb_fullsize_anim,
    orb_growing_anim,
    orb_shrinking_anim,
    orb_hidden_anim,
};

/*=============================================================================
 * Mewtwo Sprite OAM Data — from data/sprite_frames.asm
 *===========================================================================*/

/* MewtwoBaseSprite (MEWTWOSPRITE_BASE = 0) — 8 OAM entries */
static const uint8_t sprite_mewtwo_base[] = {
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

/* MewtwoRegeneratingFrame1Sprite (MEWTWOSPRITE_REGENERATING_1 = 1) */
static const uint8_t sprite_mewtwo_regen1[] = {
    0x20, 0x08, 0x22, 0x04,
    0x10, 0x08, 0x20, 0x04,
    0x20, 0x20, 0x9E, 0x04,
    0x20, 0x18, 0x9C, 0x04,
    0x20, 0x10, 0x9A, 0x04,
    0x10, 0x20, 0x96, 0x04,
    0x10, 0x18, 0x94, 0x04,
    0x10, 0x10, 0x92, 0x04,
    0x80
};

/* MewtwoRegeneratingFrame2Sprite (MEWTWOSPRITE_REGENERATING_2 = 2) */
static const uint8_t sprite_mewtwo_regen2[] = {
    0x20, 0x20, 0x1E, 0x04,
    0x20, 0x18, 0x1C, 0x04,
    0x20, 0x10, 0x1A, 0x04,
    0x20, 0x08, 0xA8, 0x04,
    0x10, 0x20, 0xA6, 0x04,
    0x10, 0x18, 0xA4, 0x04,
    0x10, 0x10, 0xA2, 0x04,
    0x10, 0x08, 0xA0, 0x04,
    0x80
};

/* MewtwoRegeneratingFrame3Sprite (MEWTWOSPRITE_REGENERATING_3 = 3) */
static const uint8_t sprite_mewtwo_regen3[] = {
    0x20, 0x20, 0x2A, 0x04,
    0x10, 0x20, 0x28, 0x04,
    0x20, 0x08, 0x26, 0x04,
    0x10, 0x08, 0x24, 0x04,
    0x20, 0x18, 0x9C, 0x04,
    0x20, 0x10, 0x9A, 0x04,
    0x10, 0x18, 0x94, 0x04,
    0x10, 0x10, 0x92, 0x04,
    0x80
};

/* MewtwoIdleFrame1Sprite (MEWTWOSPRITE_IDLE_1 = 4) */
static const uint8_t sprite_mewtwo_idle1[] = {
    0x20, 0x10, 0x7E, 0x04,
    0x10, 0x18, 0x2E, 0x04,
    0x10, 0x10, 0x92, 0x04,
    0x20, 0x20, 0xAC, 0x04,
    0x20, 0x18, 0xAA, 0x04,
    0x20, 0x08, 0x7C, 0x04,
    0x10, 0x20, 0x7A, 0x04,
    0x10, 0x08, 0x2C, 0x04,
    0x80
};

/* MewtwoIdleFrame2Sprite (MEWTWOSPRITE_IDLE_2 = 5) */
static const uint8_t sprite_mewtwo_idle2[] = {
    0x20, 0x10, 0x7E, 0x04,
    0x10, 0x18, 0x94, 0x04,
    0x10, 0x10, 0x92, 0x04,
    0x20, 0x20, 0xC4, 0x04,
    0x20, 0x08, 0xC2, 0x04,
    0x10, 0x20, 0xC0, 0x04,
    0x10, 0x08, 0xBE, 0x04,
    0x20, 0x18, 0xAA, 0x04,
    0x80
};

/* MewtwoHitSprite (MEWTWOSPRITE_HIT = 6) */
static const uint8_t sprite_mewtwo_hit[] = {
    0x1F, 0x20, 0xBC, 0x04,
    0x1F, 0x18, 0xBA, 0x04,
    0x1F, 0x10, 0xB8, 0x04,
    0x1F, 0x08, 0xB6, 0x04,
    0x0F, 0x20, 0xB4, 0x04,
    0x0F, 0x18, 0xB2, 0x04,
    0x0F, 0x10, 0xB0, 0x04,
    0x0F, 0x08, 0xAE, 0x04,
    0x80
};

/* MEWTWOSPRITE_INVISIBLE = 7 → no sprite drawn (NULL) */

/* MewtwoSpriteIds lookup table — index by animation frame (0-7) */
static const uint8_t *mewtwo_sprite_table[] = {
    sprite_mewtwo_base,    /* 0: BASE */
    sprite_mewtwo_regen1,  /* 1: REGENERATING_1 */
    sprite_mewtwo_regen2,  /* 2: REGENERATING_2 */
    sprite_mewtwo_regen3,  /* 3: REGENERATING_3 */
    sprite_mewtwo_idle1,   /* 4: IDLE_1 */
    sprite_mewtwo_idle2,   /* 5: IDLE_2 */
    sprite_mewtwo_hit,     /* 6: HIT */
    NULL,                  /* 7: INVISIBLE */
};

/*=============================================================================
 * Orbiting Ball Sprite OAM Data — from data/sprite_frames.asm
 *===========================================================================*/

/* Full-size frames (2 OAM entries each, palette 1) */
static const uint8_t sprite_orb_full0[] = { 0x10, 0x10, 0x32, 0x11, 0x10, 0x08, 0x30, 0x11, 0x80 };
static const uint8_t sprite_orb_full1[] = { 0x10, 0x10, 0x36, 0x11, 0x10, 0x08, 0x34, 0x11, 0x80 };
static const uint8_t sprite_orb_full2[] = { 0x10, 0x10, 0x3A, 0x11, 0x10, 0x08, 0x38, 0x11, 0x80 };
static const uint8_t sprite_orb_full3[] = { 0x10, 0x10, 0x3E, 0x11, 0x10, 0x08, 0x3C, 0x11, 0x80 };

/* Growing frames (first OAM is pal 3 = $31, second is pal 1 = $11) */
static const uint8_t sprite_orb_grow0[] = { 0x10, 0x10, 0xC6, 0x31, 0x10, 0x08, 0xC6, 0x11, 0x80 };
static const uint8_t sprite_orb_grow1[] = { 0x10, 0x10, 0xC8, 0x31, 0x10, 0x08, 0xC8, 0x11, 0x80 };
static const uint8_t sprite_orb_grow2[] = { 0x10, 0x10, 0xCA, 0x31, 0x10, 0x08, 0xCA, 0x11, 0x80 };
static const uint8_t sprite_orb_grow3[] = { 0x10, 0x10, 0xCC, 0x31, 0x10, 0x08, 0xCC, 0x11, 0x80 };
static const uint8_t sprite_orb_grow4[] = { 0x10, 0x10, 0xCE, 0x31, 0x10, 0x08, 0xCE, 0x11, 0x80 };
static const uint8_t sprite_orb_grow5[] = { 0x10, 0x10, 0xD0, 0x31, 0x10, 0x08, 0xD0, 0x11, 0x80 };
static const uint8_t sprite_orb_grow6[] = { 0x10, 0x10, 0xD2, 0x31, 0x10, 0x08, 0xD2, 0x11, 0x80 };

/* OrbitingBallSpriteIds lookup table — index by animation frame (0-11) */
static const uint8_t *orb_sprite_table[] = {
    sprite_orb_full0,  /* 0: FULL_SIZE_0 */
    sprite_orb_full1,  /* 1: FULL_SIZE_1 */
    sprite_orb_full2,  /* 2: FULL_SIZE_2 */
    sprite_orb_full3,  /* 3: FULL_SIZE_3 */
    sprite_orb_grow0,  /* 4: GROWING_0 */
    sprite_orb_grow1,  /* 5: GROWING_1 */
    sprite_orb_grow2,  /* 6: GROWING_2 */
    sprite_orb_grow3,  /* 7: GROWING_3 */
    sprite_orb_grow4,  /* 8: GROWING_4 */
    sprite_orb_grow5,  /* 9: GROWING_5 */
    sprite_orb_grow6,  /* 10: GROWING_6 */
    NULL,              /* 11: INVISIBLE */
};

/*=============================================================================
 * Helper: LoadFlippersPalette (home.asm 0x2862)
 * Sets OBJ palette 2 colors 1-2 based on flippers enabled/disabled.
 *===========================================================================*/
static void load_flippers_palette(GameState *state) {
    /* Active: white/blue. Disabled: red. */
    if (!state->flippers_disabled) {
        state->obj_palettes[2].colors[1] = RGB(31, 31, 31);
        state->obj_palettes[2].colors[2] = RGB(21, 21, 27);
    } else {
        state->obj_palettes[2].colors[1] = RGB(27, 10, 10);
        state->obj_palettes[2].colors[2] = RGB(20,  4,  4);
    }
}

/*=============================================================================
 * Helper: Set orbiting ball coordinates from orbit table
 * SetOrbitingBallCoordinates (0x1978b)
 *===========================================================================*/
static void set_orb_coordinates(OrbitingBall *ball) {
    /* ASM SetOrbitingBallCoordinates (0x1978b):
     * Reads coords from OLD index, then stores incremented index.
     * c = old_index * 2 for table lookup, a = old_index + 1 for storage. */
    uint8_t old_idx = ball->pos_index;
    uint8_t new_idx = old_idx + 1;
    if (new_idx >= 72) new_idx = 0;
    ball->pos_index = new_idx;
    /* Read x, y from OLD index (ASM: bc = old * 2, hl = base+1+bc) */
    ball->x_pos = orbiting_ball_coords[old_idx][0];
    ball->y_pos = orbiting_ball_coords[old_idx][1];
}

/*=============================================================================
 * Helper: Init animation for Mewtwo (Func_19679)
 *===========================================================================*/
static void set_mewtwo_animation(GameState *state, uint8_t anim_idx) {
    state->mewtwo_anim_index = anim_idx;
    init_animation(&state->mewtwo_anim, mewtwo_animations[anim_idx]);
}

/*=============================================================================
 * Helper: Init animation for orbiting ball (Func_19876)
 *===========================================================================*/
static void set_orb_animation(OrbitingBall *ball, uint8_t anim_group) {
    ball->animation_group = anim_group;
    init_animation(&ball->animation, orb_animations[anim_group]);
}

/*=============================================================================
 * Helper: Reset orbiting balls after Mewtwo hit (ResetOrbitingBalls, 0x1988e)
 *===========================================================================*/
static void reset_orbiting_balls(GameState *state) {
    uint8_t phase = state->mewtwo_regen_phase;
    if (phase > 8) phase = 8;
    for (int i = 0; i < 6; i++) {
        uint8_t start_idx = orb_start_indices[phase][i];
        OrbitingBall *ball = &state->orbiting_balls[i];
        if (start_idx & 0x80) {
            /* Disabled */
            ball->enabled = 0;
        } else {
            ball->pos_index = start_idx;
            set_orb_animation(ball, 3); /* start hidden (animation 3) */
        }
    }
}

/*=============================================================================
 * Gate tilemap updates (Func_194ac)
 * GBC version: write tiles to BG map for gate open/closed state.
 *===========================================================================*/
static void update_gate_tilemap(GameState *state) {
    if (!state->vram) return;
    uint8_t collision_state = state->stage_collision_state;
    if (collision_state == 0) {
        /* Closed gate (state 0) — Data_19507 */
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x113, 0x45);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x133, 0x80);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x152, 0x80);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x153, 0x09);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x172, 0x12);
    } else {
        /* Open gate (state 1) — Data_1951c */
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x113, 0x46);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x133, 0x47);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x152, 0x48);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x153, 0x49);
        put_tile_in_vram(state->vram, 0, 0x9800 + 0x172, 0x4A);
    }
}

/*=============================================================================
 * InitMewtwoBonusStage (0x1924f)
 *===========================================================================*/
void init_mewtwo_bonus(GameState *state) {
    if (state->loading_saved_game)
        return;

    state->stage_collision_state = 0;
    state->disable_horizontal_scroll_for_ball_start = 1;
    state->ball_type_backup = state->ball_type;
    state->ball_size = 0;
    state->ball_type = 0;
    state->completed_bonus_stage = 0;

    /* Initialize 6 orbiting balls from init data table */
    for (int i = 0; i < 6; i++) {
        OrbitingBall *ball = &state->orbiting_balls[i];
        const uint8_t *src = &init_orb_data[i * 8];
        ball->enabled = src[0];
        ball->animation.frame_counter = src[1];
        ball->animation.frame = src[2];
        ball->animation.index = src[3];
        ball->animation_group = src[4];
        ball->x_pos = src[5];
        ball->y_pos = src[6];
        ball->pos_index = src[7];
    }

    /* Initialize Mewtwo animation (Data_192db: $01, $00, $00, $00, ...) */
    state->mewtwo_anim = (Animation){1, 0, 0};
    state->mewtwo_anim_index = 0;

    /* Clear hit counters */
    state->mewtwo_collision_trigger = 0;
    state->mewtwo_collision_object = 0;
    state->mewtwo_hit_counter = 0;
    state->mewtwo_regen_phase = 0;
    state->mewtwo_ball_loss_processed = 0;
    state->mewtwo_bonus_completed = 0;
    state->mewtwo_orb_collision = 0;
    state->mewtwo_triggered_orb_index = 0;
    state->mewtwo_bonus_closed_gate = 0;

    /* Start 2:00 timer */
    start_timer(state, 0x02, 0x00);

    /* Play Mewtwo stage music (bank 0x12, MUSIC_MEWTWO_STAGE)
     * Verified: song_banks[17] = { 0x12, 0x01 } matches Bank(Music_MewtwoStage). */
    PLAY_MUSIC(state, "mewtwo_stage", 0x12, 0x01);
}

/*=============================================================================
 * InitBallMewtwoBonusStage (0x192e3)
 *===========================================================================*/
void init_ball_mewtwo_bonus(GameState *state) {
    state->ball_x_pos = MAKE_UFIXED(0xA6, 0);
    state->ball_y_pos = MAKE_UFIXED(0x56, 0);
    state->ball_x_velocity = 0x0080;
    state->scx = 0;
    state->hram.scx = 0;
    state->stage_collision_state = 0;
    state->mewtwo_bonus_closed_gate = 0;

    if (state->lost_ball) {
        state->lost_ball = 0;
    }
}

/*=============================================================================
 * HandleBallLossMewtwoBonus (0xdf7e)
 *===========================================================================*/
void handle_ball_loss_mewtwo_bonus(GameState *state) {
    /* Check if still in Mewtwo stage */
    if (state->current_stage_backup == state->current_stage)
        return;

    /* Check if bonus completed (wd6b3) */
    if (state->mewtwo_bonus_completed) {
        /* Return to main stage */
        state->going_to_bonus_stage = 0;
        state->returning_from_bonus_stage = 1;
        state->ball_size = 2;
        state->disable_horizontal_scroll_for_ball_start = 0;

        if (!state->completed_bonus_stage) {
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            const char *text = "END MEWTWO STAGE";
            /* scrolling_text_normal 2, 20, 0, 19 → { 5, 0x54, 0x42, 20, 0, 57 } */
            uint8_t header[6] = { 5, 0x54, 0x42, 20, 0, 57 };
            load_scrolling_text(state, 2, header, text);
        }
        return;
    }

    /* Check if 8 regen phases reached (wd6b1 >= 8) — freeze ball for defeat anim */
    if (state->mewtwo_regen_phase >= 8) {
        state->move_to_next_screen_state = 0;
        if (state->mewtwo_ball_loss_processed)
            return;
        state->pinball_is_visible = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->mewtwo_ball_loss_processed = 1;
    }

    /* Ball loss SFX */
    PLAY_SFX(state, "cave_light", 0x00, 0x0B);
}

/*=============================================================================
 * CheckMewtwoBonusStageGameObjectCollisions (0x19330)
 *
 * Two-part: Func_19414 (tile collision) then Func_19337 (orb collision).
 *===========================================================================*/
void check_mewtwo_bonus_object_collisions(GameState *state) {
    ensure_orb_physics_loaded(state);

    /* Func_19414: Tile-based Mewtwo collision detection */
    /* Check wTriggeredGameObject not already set (must be $FF initially) */
    if (state->is_ball_colliding) {
        uint8_t attr = state->cur_collision_attribute;
        if (attr >= 0x10 && attr < 0x1C) {
            /* Check wd6aa not already set with bit 7 */
            if (!(state->mewtwo_collision_trigger & 0x80)) {
                uint8_t obj_id = 1 + 6; /* $07 */
                /* Only trigger if different from previous */
                state->mewtwo_collision_trigger = 1;
                state->mewtwo_collision_object = obj_id;
            }
        }
    }

    /* Func_19337: Orbiting ball collision detection */
    for (int i = 0; i < 6; i++) {
        OrbitingBall *ball = &state->orbiting_balls[i];
        if (!(ball->enabled & 1)) continue;

        /* Apply position offset: x+0xF8, y+0x08 (ASM: add $f8, add $8) */
        uint8_t orb_x = (uint8_t)(ball->x_pos + 0xF8);
        uint8_t orb_y = (uint8_t)(ball->y_pos + 0x08);

        /* Check animation group — skip if 0x0B (ASM: cp $b, jp z) */
        if (ball->animation.frame == 0x0B) continue;

        /* Bounding box: 32x32 (cp $20) */
        uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
        uint8_t ball_y = UFIXED_TO_INT(state->ball_y_pos);
        uint8_t dx = (uint8_t)(ball_x - orb_x);
        uint8_t dy = (uint8_t)(ball_y - orb_y);
        if (dx >= 0x20 || dy >= 0x20) continue;

        /* Physics lookup from BallPhysicsData_e4000 */
        if (!orb_physics_data) continue;
        uint32_t offset = ((uint32_t)dy * 32 + dx) * 4;
        if (offset + 4 > orb_physics_size) continue;

        /* Apply force vectors to ball velocity (signed 16-bit add) */
        int16_t fx = (int16_t)(orb_physics_data[offset] | (orb_physics_data[offset + 1] << 8));
        int16_t fy = (int16_t)(orb_physics_data[offset + 2] | (orb_physics_data[offset + 3] << 8));
        state->ball_x_velocity = (int16_t)((int16_t)state->ball_x_velocity + fx);
        state->ball_y_velocity = (int16_t)((int16_t)state->ball_y_velocity + fy);

        /* Rumble check: if combined force magnitude >= $0200 */
        int16_t abs_fx = fx < 0 ? -fx : fx;
        int16_t abs_fy = fy < 0 ? -fy : fy;
        int32_t combined = abs_fx + abs_fy;
        if ((combined >> 1) >= 0x0200) {
            /* Only if not already rumbling */
        }

        /* Mark collision */
        state->mewtwo_orb_collision = (uint8_t)(i + 1);
        state->mewtwo_triggered_orb_index = (uint8_t)(i + 1);
        return;
    }
}

/*=============================================================================
 * Func_195ac: Check if any orb reached pos_index 0x2B in growing anim
 * If so, set Mewtwo to regenerating animation (index 1)
 *===========================================================================*/
static void check_orb_regen_trigger(GameState *state) {
    if (state->mewtwo_anim_index != 0) return;
    for (int i = 0; i < 6; i++) {
        OrbitingBall *ball = &state->orbiting_balls[i];
        if (ball->pos_index == 0x2B) {
            if (ball->animation_group == 2) {
                set_mewtwo_animation(state, 1); /* regenerating */
                return;
            }
        }
    }
}

/*=============================================================================
 * Func_195d3: Check if any orb reached pos_index 0x18 in shrinking anim
 * If so, restart orb with growing animation (group 1)
 *===========================================================================*/
static void check_orb_shrink_complete(GameState *state) {
    for (int i = 0; i < 6; i++) {
        OrbitingBall *ball = &state->orbiting_balls[i];
        if (ball->pos_index == 0x18) {
            if (ball->animation_group == 2) {
                set_orb_animation(ball, 1); /* growing */
                return;
            }
        }
    }
}

/*=============================================================================
 * Mewtwo animation completion callbacks (CallTable_1960d)
 *===========================================================================*/
static void mewtwo_anim_idle_done(GameState *state) {
    /* Func_19615: ASM checks animation INDEX (dec de from wd6af → wd6ae) */
    if (state->mewtwo_anim.index == 4) {
        set_mewtwo_animation(state, 0);
    }
}

static void mewtwo_anim_regen_done(GameState *state) {
    /* Func_1961e: ASM checks animation INDEX (dec de from wd6af → wd6ae) */
    if (state->mewtwo_anim.index == 0x0C) {
        /* index $0C: call Func_195d3 (orb shrink check) */
        check_orb_shrink_complete(state);
    } else if (state->mewtwo_anim.index == 0x0D) {
        /* index $0D: switch to anim 0 (idle) */
        set_mewtwo_animation(state, 0);
    }
}

static void mewtwo_anim_hit_done(GameState *state) {
    /* Func_1962f: ASM checks animation INDEX == 1 */
    if (state->mewtwo_anim.index == 1) {
        set_mewtwo_animation(state, 0);
    }
}

static void mewtwo_anim_defeated_done(GameState *state) {
    /* Func_19638: ASM checks animation INDEX (dec de from state var) */
    if (state->mewtwo_anim.index == 1) {
        PLAY_SFX(state, "mewtwo_orb_hit", 0x00, 0x40);
    } else if (state->mewtwo_anim.index == 0x20) {
        /* Stage cleared! */
        state->mewtwo_bonus_completed = 1;
        state->next_bonus_stage = state->initial_next_bonus_stage;
        if (state->num_mewtwo_bonus_completions < 2) {
            state->num_mewtwo_bonus_completions++;
        }
        state->completed_bonus_stage = 1;
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        const char *text = "MEWTWO STAGE CLEARED";
        /* scrolling_text_normal 0, 20, 0, 21 → { 5, 0x54, 0x40, 20, 0, 61 } */
        uint8_t header[6] = { 5, 0x54, 0x40, 20, 0, 61 };
        load_scrolling_text(state, 2, header, text);
        PLAY_SFX(state, "bonus_stage_clear", 0x4B, 0x2A);
    }
}

/*=============================================================================
 * Orbiting ball animation completion callbacks (CallTable_19852)
 *===========================================================================*/
static void orb_anim_done(OrbitingBall *ball) {
    uint8_t group = ball->animation_group;
    uint8_t idx = ball->animation.index;
    switch (group) {
        case 0: /* full-size cycle done (Func_1985a: dec de, cp $6) */
            if (idx == 6) set_orb_animation(ball, 0); /* loop */
            break;
        case 1: /* growing done (Func_19863: dec de, cp $7) */
            if (idx == 7) set_orb_animation(ball, 0); /* → full size */
            break;
        case 2: /* shrinking — just return (Func_1986c: ret) */
            break;
        case 3: /* hidden done (Func_1986d: dec de, cp $1) */
            if (idx == 1) set_orb_animation(ball, 0); /* → full size */
            break;
    }
}

/*=============================================================================
 * Func_19531: Mewtwo hit resolution
 *===========================================================================*/
static void resolve_mewtwo_hit(GameState *state) {
    if (!state->mewtwo_collision_trigger)
        goto after_hit;

    state->mewtwo_collision_trigger = 0;

    if (state->flippers_disabled)
        goto after_hit;

    /* Check animation index < 2 (only idle/regen can be hit) */
    if (state->mewtwo_anim_index >= 2)
        goto after_hit;

    /* Award 5,000,000 points */
    add_score_with_multiplier(state, state->config->scores.mewtwo_hit);

    /* Increment hit counter within group (ASM: Func_19531 at 0x19531) */
    state->mewtwo_hit_counter++;
    if (state->mewtwo_hit_counter >= 3) {
        /* Group complete — increment regen phase, reset counter */
        state->mewtwo_regen_phase++;
        state->mewtwo_hit_counter = 0;
    }

    /* ASM: ResetOrbitingBalls runs on EVERY hit (after .asm_19565) */
    reset_orbiting_balls(state);

    if (state->mewtwo_regen_phase >= 8) {
        /* Defeated! (ASM: .asm_19582) */
        set_mewtwo_animation(state, 3); /* defeated flash */
        state->flippers_disabled = 1;
        load_flippers_palette(state);
        stop_timer(state);
        PLAY_MUSIC(state, "nothing", 0x00, 0x00); /* stop music */
    } else {
        /* Not yet defeated — hit animation + SFX (every hit) */
        set_mewtwo_animation(state, 2); /* hit */
        PLAY_SFX(state, "mewtwo_hit", 0x00, 0x39);
    }

after_hit:
    /* Func_195ac: check orb regen trigger */
    check_orb_regen_trigger(state);

    /* Func_195f5: update Mewtwo animation */
    {
        const uint8_t *anim_data = mewtwo_animations[state->mewtwo_anim_index];
        bool done = update_animation(&state->mewtwo_anim, anim_data);
        if (done) {
            switch (state->mewtwo_anim_index) {
                case 0: mewtwo_anim_idle_done(state); break;
                case 1: mewtwo_anim_regen_done(state); break;
                case 2: mewtwo_anim_hit_done(state); break;
                case 3: mewtwo_anim_defeated_done(state); break;
            }
        }
    }
}

/*=============================================================================
 * Func_19701: Orbiting ball updates (per-frame)
 *===========================================================================*/
static void update_orbiting_balls(GameState *state) {
    /* Handle orb collision event */
    if (state->mewtwo_orb_collision) {
        state->mewtwo_orb_collision = 0;

        if (!state->flippers_disabled) {
            uint8_t idx = state->mewtwo_triggered_orb_index - 1;
            if (idx < 6) {
                OrbitingBall *ball = &state->orbiting_balls[idx];
                if (ball->animation_group == 0) {
                    /* Start shrinking animation */
                    set_orb_animation(ball, 2);
                    add_score_with_multiplier(state, state->config->scores.mewtwo_orb_hit);
                    PLAY_SFX(state, "mewtwo_orbit", 0x00, 0x38);
                }
            }
        }
    }

    /* Update coordinates and animations for all 6 balls */
    for (int i = 0; i < 6; i++) {
        OrbitingBall *ball = &state->orbiting_balls[i];
        set_orb_coordinates(ball);
    }
    for (int i = 0; i < 6; i++) {
        OrbitingBall *ball = &state->orbiting_balls[i];
        if (!ball->enabled) continue;

        const uint8_t *anim_data = orb_animations[ball->animation_group];
        bool done = update_animation(&ball->animation, anim_data);
        if (done) {
            orb_anim_done(ball);
        }
    }
}

/*=============================================================================
 * TryCloseGate_MewtwoBonus (0x1948b)
 *===========================================================================*/
static void try_close_gate(GameState *state) {
    if (state->mewtwo_bonus_closed_gate)
        return;
    uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
    if (ball_x >= 138)
        return;
    state->stage_collision_state = 1;
    state->mewtwo_bonus_closed_gate = 1;
    load_stage_collision_attributes(state);
    update_gate_tilemap(state);
}

/*=============================================================================
 * PlayLowTimeSfx (0x107f8) — plays SFX at exactly 32 seconds remaining
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
 * ResolveMewtwoBonusGameObjectCollisions (0x19451)
 *===========================================================================*/
void resolve_mewtwo_bonus_object_collisions(GameState *state) {
    resolve_mewtwo_hit(state);
    update_orbiting_balls(state);
    try_close_gate(state);
    play_low_time_sfx(state);

    /* Check time ran out */
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->flippers_disabled = 1;
        load_flippers_palette(state);
        stop_timer(state);
        /* If not yet defeated, mark as completed (failed) */
        if (state->mewtwo_regen_phase < 8) {
            state->mewtwo_bonus_completed = 1;
        }
    }
}

/*=============================================================================
 * DrawSpritesMewtwoBonus (0x1994e)
 *===========================================================================*/
extern void draw_timer(GameState *state, uint8_t x, uint8_t y);
extern void draw_pinball(GameState *state);
extern void draw_flipper_sprites(GameState *state);

void draw_mewtwo_bonus_sprites(GameState *state) {
    /* Draw timer — ld bc, $7f65 (bottom-right) */
    draw_timer(state, 0x7F, 0x65);

    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Draw orbiting balls */
    for (int i = 0; i < 6; i++) {
        OrbitingBall *ball = &state->orbiting_balls[i];
        if (!ball->enabled) continue;
        uint8_t frame = ball->animation.frame;
        if (frame >= 12 || !orb_sprite_table[frame]) continue;
        uint8_t sx = (uint8_t)(ball->x_pos - scx);
        uint8_t sy = (uint8_t)(ball->y_pos - scy);
        load_sprite_data(state, orb_sprite_table[frame], sy, sx);
    }

    /* Draw flippers and ball */
    draw_flipper_sprites(state);
    draw_pinball(state);

    /* Draw Mewtwo (Func_19976)
     * Position: B = 0x40 - SCX, C = 0 - SCY */
    uint8_t mx = (uint8_t)(0x40 - scx);
    uint8_t my = (uint8_t)(0x00 - scy);
    uint8_t mewtwo_frame = state->mewtwo_anim.frame;
    if (mewtwo_frame < 8 && mewtwo_sprite_table[mewtwo_frame]) {
        load_sprite_data(state, mewtwo_sprite_table[mewtwo_frame], my, mx);
    }
}

/*=============================================================================
 * _LoadStageDataMewtwoBonus (0x19310)
 *
 * ASM calls: LoadBallGraphics, LoadFlippersPalette, LoadTimerGraphics.
 * If loading saved game, also loads gate graphics.
 * Sprite tiles are loaded by the base asset table (mewtwo_1.png → bank 1).
 * Mewtwo-specific sprite tiles are in mewtwo_2/3/4.png loaded here.
 *===========================================================================*/
void load_stage_data_mewtwo_bonus(GameState *state) {
    if (!state->vram) return;

    /* LoadFlippersPalette */
    load_flippers_palette(state);

    /* Load Mewtwo OBJ sprite tiles (ASM: StageMewtwoBonusGfx_GameBoyColor).
     * These are sequential sprite tiles — no interleave needed. */
    char path[260];
    size_t data_size;
    uint8_t *tile_data;

    /* mewtwo_1.png: tiles 0x1A-0x3F (regen frames + orb full-size)
     * ASM: vTilesOB + $1a0, size $260 */
    snprintf(path, sizeof(path), "%s/gfx/stage/mewtwo_bonus/mewtwo_1.png",
             state->asset_base_path);
    tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        uint16_t copy = (uint16_t)(data_size > 0x260 ? 0x260 : data_size);
        vram_write(state->vram, 0, 0x81a0, tile_data, copy);
        free(tile_data);
    }

    /* mewtwo_2.png: tiles 0x7A-0x7F (idle frame parts)
     * ASM: vTilesOB + $7a0, size $60 */
    snprintf(path, sizeof(path), "%s/gfx/stage/mewtwo_bonus/mewtwo_2.png",
             state->asset_base_path);
    tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        uint16_t copy = (uint16_t)(data_size > 0x60 ? 0x60 : data_size);
        vram_write(state->vram, 0, 0x87a0, tile_data, copy);
        free(tile_data);
    }

    /* mewtwo_3.png: tiles 0x90-0xA9 (base/regen Mewtwo frames)
     * ASM: vTilesSH + $100, size $1a0 */
    snprintf(path, sizeof(path), "%s/gfx/stage/mewtwo_bonus/mewtwo_3.png",
             state->asset_base_path);
    tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        uint16_t copy = (uint16_t)(data_size > 0x1a0 ? 0x1a0 : data_size);
        vram_write(state->vram, 0, 0x8900, tile_data, copy);
        free(tile_data);
    }

    /* mewtwo_4.png: tiles 0xAA-0xD3 (hit/idle2/orbiting ball tiles)
     * ASM: vTilesSH + $2a0, size $2a0 */
    snprintf(path, sizeof(path), "%s/gfx/stage/mewtwo_bonus/mewtwo_4.png",
             state->asset_base_path);
    tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        uint16_t copy = (uint16_t)(data_size > 0x2a0 ? 0x2a0 : data_size);
        vram_write(state->vram, 0, 0x8aa0, tile_data, copy);
        free(tile_data);
    }

    /* If loading saved game, update gate tilemap */
    if (state->loading_saved_game) {
        update_gate_tilemap(state);
    }
}
