/*
 * Blue Field Sprite Drawing
 *
 * Translated from:
 *   engine/pinball_game/draw_sprites/draw_blue_field_sprites.asm
 *
 * Draws all game object sprites for the blue pinball field (top and bottom).
 */

#include "game/draw_blue_field.h"
#include "game/draw_red_field.h"  /* draw_pinball, draw_flipper_sprites, draw_timer */
#include "game/sprite_data.h"
#include "game/constants.h"
#include <string.h>

/*=============================================================================
 * Draw Shellder Sprites (3 shellders on top field)
 * From DrawShellderSprites (0x1f395):
 *   Shellder 1: world (0x48, 0x2D), ID match 0x01
 *   Shellder 2: world (0x33, 0x3E), ID match 0x00
 *   Shellder 3: world (0x5D, 0x3E), ID match 0x02
 *===========================================================================*/
static const uint8_t shellder_world_pos[3][2] = {
    {0x48, 0x2D},  /* Shellder 1: ID match = 0x01 */
    {0x33, 0x3E},  /* Shellder 2: ID match = 0x00 */
    {0x5D, 0x3E},  /* Shellder 3: ID match = 0x02 */
};
static const uint8_t shellder_id_match[3] = { 0x01, 0x00, 0x02 };

static void draw_shellder_sprites(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    for (int i = 0; i < 3; i++) {
        uint8_t sx = (uint8_t)(shellder_world_pos[i][0] - scx);
        uint8_t sy = (uint8_t)(shellder_world_pos[i][1] - scy);

        /* ASM: if wWhichAnimatedShellder == this shellder's match ID,
         * show collision sprite, else show stationary */
        const uint8_t *sprite;
        if (state->which_animated_shellder == shellder_id_match[i]) {
            sprite = sprite_shellder_collision;
        } else {
            sprite = sprite_shellder_stationary;
        }
        load_sprite_data(state, sprite, sy, sx);
    }
}

/*=============================================================================
 * Draw Spinner - Blue Field (top field)
 * From DrawSpinner_BlueField (0x1f3e1):
 *   World (0x8A, 0x53), frame = spinner_state[1] >> 2, 6 frames
 *===========================================================================*/
static void draw_spinner_blue(GameState *state) {
    uint8_t sx = (uint8_t)(0x8A - state->hram.scx);
    uint8_t sy = (uint8_t)(0x53 - state->hram.scy);

    uint8_t frame = state->spinner_state[1] >> 2;
    if (frame > 5) frame = 5;
    load_sprite_data(state, blue_spinner_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Slowpoke (top field)
 * From DrawSlowpoke (0x1f408):
 *   World (0x18, 0x5F), frame from slowpoke_anim_frame (0-2)
 *===========================================================================*/
static void draw_slowpoke(GameState *state) {
    uint8_t sx = (uint8_t)(0x18 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x5F - state->hram.scy);

    uint8_t frame = state->slowpoke_anim.frame;
    if (frame > 2) frame = 2;
    load_sprite_data(state, slowpoke_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Cloyster (top field)
 * From DrawCloyster (0x1f428):
 *   World (0x70, 0x59), frame from cloyster_anim_frame (0-2)
 *===========================================================================*/
static void draw_cloyster(GameState *state) {
    uint8_t sx = (uint8_t)(0x70 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x59 - state->hram.scy);

    uint8_t frame = state->cloyster_anim.frame;
    if (frame > 2) frame = 2;
    load_sprite_data(state, cloyster_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Pikachu Savers - Blue Stage (bottom)
 * From DrawPikachuSavers_BlueStage (0x1f448):
 *   Left: world (0x0F, 0x7E), Right: world (0x92, 0x7E)
 *   Side selection same as red but with different positions
 *===========================================================================*/
static const uint8_t pikachu_saver_blue_pos[2][2] = {
    {0x0F, 0x7E},  /* Left */
    {0x92, 0x7E},  /* Right */
};

static void draw_pikachu_savers_blue(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Determine which side to draw (same logic as red field) */
    uint8_t side;
    if (!state->pikachu_saver_slot_reward_active) {
        side = state->which_pikachu_saver_side;
    } else if (state->pikachu_saver_state == 0) {
        side = (state->hram.frame_counter >> 3) & 1;
    } else {
        uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
        side = (ball_x >= 80) ? 1 : 0;
    }

    if (side > 1) side = 1;
    uint8_t sx = (uint8_t)(pikachu_saver_blue_pos[side][0] - scx);
    uint8_t sy = (uint8_t)(pikachu_saver_blue_pos[side][1] - scy);

    uint8_t frame = state->pikachu_saver_anim.frame;
    if (frame > 8) frame = 8;
    load_sprite_data(state, pikachu_saver_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Evolution Indicator Arrows - Blue Field
 * From DrawEvolutionIndicatorArrows_BlueFieldTop (0x1f48f) / Bottom (0x1f4a3):
 *   Gate: evolution_objects_disabled == 0 AND hFrameCounter bit 4 set
 *   Top: 6 arrows from indicator_states[5..10]
 *   Bottom: 8 arrows from indicator_states[11..18]
 *===========================================================================*/

/* Blue field top arrow positions + sprites (ASM 0x1f4ce) */
static const uint8_t blue_evo_arrow_top_pos[6][2] = {
    {0x0D, 0x37}, {0x35, 0x0D}, {0x8E, 0x4E},
    {0x36, 0x64}, {0x4C, 0x49}, {0x61, 0x64},
};
static const uint8_t *const blue_evo_arrow_top_spr[6] = {
    sprite_blue_arrow_up,
    sprite_blue_arrow_down_right,
    sprite_blue_arrow_down,
    sprite_blue_arrow_left,
    sprite_blue_arrow_up,
    sprite_blue_arrow_right,
};

/* Blue field bottom arrow positions + sprites (ASM 0x1f4e0) */
static const uint8_t blue_evo_arrow_bot_pos[8][2] = {
    {0x2D, 0x13}, {0x6A, 0x13}, {0x25, 0x2D}, {0x73, 0x2D},
    {0x38, 0x14}, {0x66, 0x14}, {0x79, 0x40}, {0x89, 0x40},
};
static const uint8_t *const blue_evo_arrow_bot_spr[8] = {
    sprite_evo_arrow_bot_up_left,
    sprite_evo_arrow_bot_up_right,
    sprite_evo_arrow_bot_left,
    sprite_evo_arrow_bot_right,
    sprite_evo_arrow_bot_down_left,
    sprite_evo_arrow_bot_down_left,
    sprite_evo_arrow_bot_down_right,
    sprite_evo_arrow_bot_down_right,
};

static void draw_evolution_indicator_arrows_blue(GameState *state) {
    if (state->evolution_objects_disabled) return;
    if (!(state->hram.frame_counter & 0x10)) return;

    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    if (state->current_stage == STAGE_BLUE_FIELD_TOP) {
        for (int i = 0; i < 6; i++) {
            if (!state->indicator_states[5 + i]) continue;
            uint8_t sx = (uint8_t)(blue_evo_arrow_top_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(blue_evo_arrow_top_pos[i][1] - scy);
            load_sprite_data(state, blue_evo_arrow_top_spr[i], sy, sx);
        }
    } else {
        for (int i = 0; i < 8; i++) {
            if (!state->indicator_states[11 + i]) continue;
            uint8_t sx = (uint8_t)(blue_evo_arrow_bot_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(blue_evo_arrow_bot_pos[i][1] - scy);
            load_sprite_data(state, blue_evo_arrow_bot_spr[i], sy, sx);
        }
    }
}

/*=============================================================================
 * Draw Evolution Trinket - Blue Field
 * From DrawEvolutionTrinket_BlueFieldTop (0x1f4f8) / Bottom (0x1f509):
 *   Gate: evolution_objects_disabled != 0
 *   Top: 12 trinkets, Bottom: 6 trinkets
 *   Y bobbing: dec when (hFrameCounter & 0x0E) == 0
 *===========================================================================*/

/* Blue field top trinket positions (ASM 0x1f53a) */
static const uint8_t blue_trinket_top_pos[12][2] = {
    {0x4C, 0x08}, {0x2B, 0x12}, {0x6D, 0x12}, {0x15, 0x25},
    {0x82, 0x25}, {0x0D, 0x3F}, {0x4C, 0x7F}, {0x8B, 0x3F},
    {0x0A, 0x65}, {0x36, 0x7F}, {0x61, 0x7F}, {0x8D, 0x65},
};

/* Blue field bottom trinket positions (ASM 0x1f552) */
static const uint8_t blue_trinket_bot_pos[6][2] = {
    {0x3B, 0x12}, {0x5D, 0x12}, {0x31, 0x16},
    {0x67, 0x16}, {0x25, 0x2C}, {0x73, 0x2C},
};

static void draw_evolution_trinket_blue(GameState *state) {
    if (!state->evolution_objects_disabled) return;

    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;
    uint8_t bob = (state->hram.frame_counter & 0x0E) == 0 ? 1 : 0;

    if (state->current_stage == STAGE_BLUE_FIELD_TOP) {
        /* ASM uses SPRITE_TRINKET_BLUE_TOP (c = base - 1, a = [de] + c) */
        const uint8_t *const *trinket_sprites = evo_trinket_top_sprites;
        for (int i = 0; i < 12; i++) {
            uint8_t type = state->active_evolution_trinkets[i];
            if (type == 0 || type > 7) continue;
            uint8_t sx = (uint8_t)(blue_trinket_top_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(blue_trinket_top_pos[i][1] - scy - bob);
            load_sprite_data(state, trinket_sprites[type], sy, sx);
        }
    } else {
        const uint8_t *const *trinket_sprites = evo_trinket_bot_sprites;
        for (int i = 0; i < 6; i++) {
            uint8_t type = state->active_evolution_trinkets[12 + i];
            if (type == 0 || type > 7) continue;
            uint8_t sx = (uint8_t)(blue_trinket_bot_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(blue_trinket_bot_pos[i][1] - scy - bob);
            load_sprite_data(state, trinket_sprites[type], sy, sx);
        }
    }
}

/*=============================================================================
 * Draw Slot Glow - Blue Field (bottom stage only)
 * From DrawSlotGlow_BlueField (0x1f55e):
 *   World (0x40, 0x01), same animation as red field
 *===========================================================================*/
static void draw_slot_glow_blue(GameState *state) {
    if (!state->slot_is_open) return;

    state->slot_glowing_anim_counter++;

    uint8_t sx = (uint8_t)(0x40 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x01 - state->hram.scy);

    uint8_t frame = (state->slot_glowing_anim_counter >> 3) & 3;
    if (frame < 3) {
        load_sprite_data(state, slot_glow_sprites[frame], sy, sx);
    }
}

/*=============================================================================
 * Draw Animated Mon - Blue Stage (bottom stage only)
 * From DrawAnimatedMon_BlueStage (0x1f58b):
 *   World (0x50, 0x3E), same as red stage
 *===========================================================================*/
static void draw_animated_mon_blue(GameState *state) {
    if (!state->wild_mon_is_hittable) return;

    uint8_t sx = (uint8_t)(0x50 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x3E - state->hram.scy);

    uint8_t frame = state->current_animated_mon_sprite_frame;
    if (frame > 11) frame = 11;
    load_sprite_data(state, animated_mon_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Mon Capture Animation - Blue Stage (bottom stage only)
 * Same as red field version
 *===========================================================================*/
static void draw_mon_capture_animation_blue(GameState *state) {
    if (!state->capturing_mon) return;

    uint8_t sx = (uint8_t)(0x50 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x38 - state->hram.scy);

    uint8_t frame = state->ball_capture_anim.frame;
    if (frame > 12) frame = 12;
    load_sprite_data(state, ball_capture_sprites[frame], sy, sx);
}

/*=============================================================================
 * Main sprite dispatch - Blue Field
 *===========================================================================*/
void draw_blue_field_sprites(GameState *state) {
    memset(state->sprite_buffer, 0, sizeof(state->sprite_buffer));
    state->sprite_buffer_size = 0;

    if (state->current_stage == STAGE_BLUE_FIELD_TOP) {
        /* DrawSpritesBlueFieldTop (0x1f330) — ASM dispatch order */
        draw_timer(state, 0x7F, 0x00);                                   /*  1 */
        draw_shellder_sprites(state);                        /*  2 */
        draw_spinner_blue(state);                            /*  3 */
        draw_slowpoke(state);                                /*  4 */
        draw_cloyster(state);                                /*  5 */
        draw_pinball(state);                                 /*  6 */
        draw_evolution_indicator_arrows_blue(state);         /*  7 */
        draw_evolution_trinket_blue(state);                  /*  8 */
    } else if (state->current_stage == STAGE_BLUE_FIELD_BOTTOM) {
        /* DrawSpritesBlueFieldBottom (0x1f35a) — ASM dispatch order */
        draw_timer(state, 0x7F, 0x00);                                   /*  1 */
        draw_mon_capture_animation_blue(state);              /*  2 */
        draw_animated_mon_blue(state);                       /*  3 */
        draw_pikachu_savers_blue(state);                     /*  4 */
        draw_flipper_sprites(state);                         /*  5 */
        draw_pinball(state);                                 /*  6 */
        draw_evolution_indicator_arrows_blue(state);         /*  7 */
        draw_evolution_trinket_blue(state);                  /*  8 */
        draw_slot_glow_blue(state);                          /*  9 */
    }
}
