/*
 * Red Field Sprite Drawing & Scoreboard
 *
 * Translated from:
 *   engine/pinball_game/draw_sprites/draw_pinball.asm (0x17e81)
 *   engine/pinball_game/draw_sprites/draw_red_field_top_sprites.asm
 *   engine/pinball_game/draw_sprites/draw_red_field_bottom_sprites.asm
 *   engine/pinball_game/flippers.asm (DrawFlippers, 0xe4a4)
 *   engine/pinball_game/draw_sprites/draw_scoreboard_info.asm
 *
 * Draws all game object sprites for the red pinball field (top and bottom
 * stages) and populates the window-layer scoreboard with score/lives.
 */

#include "game/draw_red_field.h"
#include "game/ball_gfx.h"
#include "game/sprite_data.h"
#include "game/rng.h"
#include "renderer/vram.h"
#include "renderer/tile_loader.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*=============================================================================
 * Draw Pinball (ball sprite)
 * From draw_pinball.asm (0x17e81):
 *   screen_x = ball_x_hi + 1 - SCX
 *   screen_y = ball_y_hi + 1 - $10 - SCY
 *   frame = (ball_rotation >> 4) & 7
 *===========================================================================*/
void draw_pinball(GameState *state) {
    if (!state->pinball_is_visible) return;

    /* Update ball rotation based on spin */
    state->ball_rotation += state->ball_spin;

    uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
    uint8_t ball_y = UFIXED_TO_INT(state->ball_y_pos);

    /* ASM: screen coords = world + 1 - scroll (- $10 for Y GBC offset) */
    uint8_t screen_x = (uint8_t)((int)ball_x + 1 - (int)state->hram.scx);
    uint8_t screen_y = (uint8_t)((int)ball_y + 1 - 0x10 - (int)state->hram.scy);

    /* Select rotation frame: 8 frames, ball_rotation / 16 */
    uint8_t frame = (state->ball_rotation >> 4) & 7;

    load_sprite_data(state, ball_spin_sprites[frame], screen_y, screen_x);
}

/*=============================================================================
 * Draw Flippers (bottom stage only)
 * From flippers.asm DrawFlippers (0xe4a4):
 *   Left flipper at world (0x38, 0x7B)
 *   Right flipper at world (0x68, 0x7B)
 *   Sprite selected by flipper angle (high byte of flipper_state, 0-15)
 *===========================================================================*/
void draw_flipper_sprites(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Left flipper: world (0x38, 0x7B) */
    uint8_t lx = (uint8_t)(0x38 - scx);
    uint8_t ly = (uint8_t)(0x7B - scy);
    uint8_t left_angle = (uint8_t)(state->left_flipper_state >> 8);
    if (left_angle > 20) left_angle = 20;  /* ASM: 21 entries (0-20) */
    load_sprite_data(state, left_flipper_sprites_by_angle[left_angle], ly, lx);

    /* Right flipper: world (0x68, 0x7B) */
    uint8_t rx = (uint8_t)(0x68 - scx);
    uint8_t ry = (uint8_t)(0x7B - scy);
    uint8_t right_angle = (uint8_t)(state->right_flipper_state >> 8);
    if (right_angle > 20) right_angle = 20;  /* ASM: 21 entries (0-20) */
    load_sprite_data(state, right_flipper_sprites_by_angle[right_angle], ry, rx);
}

/*=============================================================================
 * Draw Pikachu Savers (bottom stage only)
 * From DrawPikachuSavers_RedStage (0x17e08):
 *   ASM draws ONE Pikachu per frame, on the active side.
 *   Side selection:
 *     - Normal: wWhichPikachuSaverSide (0=left, 1=right)
 *     - Slot reward active + wd51c==0: alternate every 8 frames
 *     - Slot reward active + wd51c!=0: side based on ball X (>=$50 = right)
 *   Positions: left=(0x0F, 0x7E), right=(0x92, 0x7E)
 *===========================================================================*/
static void draw_pikachu_savers(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Determine which side to draw (ASM 0x17e0c-0x17e3c) */
    uint8_t side;
    if (!state->pikachu_saver_slot_reward_active) {
        side = state->which_pikachu_saver_side;
    } else if (state->pikachu_saver_state == 0) {
        /* Slot reward active, no save in progress: alternate every 8 frames */
        side = (state->hram.frame_counter >> 3) & 1;
    } else {
        /* Slot reward active, save in progress: side based on ball X */
        uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
        side = (ball_x >= 0x50) ? 1 : 0;
    }

    /* Position from PikachuSaverSpriteOffsets_RedStage */
    uint8_t world_x = (side == 0) ? 0x0F : 0x92;
    uint8_t world_y = 0x7E;
    uint8_t sx = (uint8_t)(world_x - scx);
    uint8_t sy = (uint8_t)(world_y - scy);

    /* Sprite from animation frame */
    uint8_t frame = state->pikachu_saver_anim.frame;
    if (frame >= 9) frame = 0;
    load_sprite_data(state, pikachu_saver_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Voltorb Sprites (top stage only)
 * From draw_red_field_top_sprites.asm (0x17ceb):
 *   3 Voltorbs at world coords from object table.
 *   Stationary or collision sprite based on animation.
 *===========================================================================*/

/* Voltorb world positions (from red_field.c object data) */
static const uint8_t voltorb_positions[3][2] = {
    {0x3A, 0x4E},  /* Voltorb 1: x, y */
    {0x53, 0x44},  /* Voltorb 2 */
    {0x4D, 0x60},  /* Voltorb 3 */
};

/* Per-voltorb shake sprites (each voltorb has its own shake variant) */
static const uint8_t *const voltorb_shake_sprites[3] = {
    sprite_voltorb_shake_1,
    sprite_voltorb_shake_2,
    sprite_voltorb_shake_3,
};

/*
 * VoltorbAnimation data (from ASM DrawVoltorbSprite 0x17cdc):
 * Variable-duration shake sequence, then reset with random interval.
 * Format: {duration, frame}... pairs. Frames: 0=stationary, 1=shake.
 */
static const uint8_t voltorb_anim_data[] = {
    0x1E, 0,   /* idle 30 frames */
    0x02, 1,   /* shake 2 frames */
    0x03, 0,   /* idle 3 frames */
    0x02, 1,   /* shake 2 frames */
    0x03, 0,   /* idle 3 frames */
    0x02, 1,   /* shake 2 frames */
    0xFF       /* end marker (ASM: 6 entries, not 7) */
};

static void draw_voltorb_sprites(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Per-voltorb animation state: use voltorb1/2/3_anim structs.
     * ASM UpdateAnimation: decrement frame_counter, advance when 0. */
    Animation *anims[3] = {
        &state->voltorb1_anim, &state->voltorb2_anim, &state->voltorb3_anim
    };

    for (int i = 0; i < 3; i++) {
        uint8_t sx = (uint8_t)(voltorb_positions[i][0] - scx);
        uint8_t sy = (uint8_t)(voltorb_positions[i][1] - scy);

        const uint8_t *sprite;

        /* Check if this voltorb was just hit (collision sprite) */
        if (state->voltorb_hit_anim_duration > 0 &&
            state->which_animated_voltorb == i) {
            sprite = sprite_voltorb_collision;
        } else {
            /* UpdateAnimation (0x28a9): ASM behavior:
             * counter==0 at entry → ret z (no action, no advance)
             * counter>0 → dec, if result!=0 → ret nz, if result==0 → advance
             * After UpdateAnimation, DrawVoltorbSprite checks counter==0:
             *   if 0 → reset with random duration + STATIONARY frame */
            Animation *anim = anims[i];
            if (anim->frame_counter > 0) {
                anim->frame_counter--;
                if (anim->frame_counter == 0) {
                    /* Advance to next entry in animation data */
                    uint8_t idx = anim->index;
                    if (idx < sizeof(voltorb_anim_data) - 2 &&
                        voltorb_anim_data[idx + 2] != 0xFF) {
                        idx += 2;
                    } else {
                        /* End of sequence: mark for reset */
                        idx = 0;
                        anim->frame_counter = 0;
                    }
                    anim->index = idx;
                    if (anim->frame_counter > 0 || idx > 0) {
                        anim->frame_counter = voltorb_anim_data[idx];
                        anim->frame = voltorb_anim_data[idx + 1];
                    }
                }
            }
            /* ASM: after UpdateAnimation, checks counter==0 → reset.
             * Handles both first-frame (all zeros) and end-of-sequence. */
            if (anim->frame_counter == 0) {
                anim->frame_counter = (uint8_t)((gen_random(state) & 7) + 0x1E);
                anim->frame = 0;  /* STATIONARY */
                anim->index = 0;
            }

            if (anim->frame == 1) {
                sprite = voltorb_shake_sprites[i];
            } else {
                sprite = sprite_voltorb_stationary;
            }
        }

        load_sprite_data(state, sprite, sy, sx);
    }
}

/*=============================================================================
 * Draw Bellsprout (top stage only)
 * From draw_red_field_top_sprites.asm (0x17d42):
 *   Head at world (0x74, 0x52), body at world (0x67, 0x54)
 *===========================================================================*/
static void draw_bellsprout(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Head: world (0x74, 0x52) */
    uint8_t hx = (uint8_t)(0x74 - scx);
    uint8_t hy = (uint8_t)(0x52 - scy);
    uint8_t head_frame = state->bellsprout_anim.frame;
    if (head_frame >= 4) head_frame = 0;
    load_sprite_data(state, bellsprout_head_sprites[head_frame], hy, hx);

    /* Body: world (0x67, 0x54) - GBC only (always drawn in our case) */
    uint8_t bx = (uint8_t)(0x67 - scx);
    uint8_t by = (uint8_t)(0x54 - scy);
    load_sprite_data(state, sprite_bellsprout_body, by, bx);
}

/*=============================================================================
 * Draw Staryu (top and bottom stages)
 * From draw_red_field_top_sprites.asm (0x17d7e):
 *   Top stage: world (0x2B, 0x69)
 *   Bottom stage: world (0x2B, 0x08) (from bottom object table: 0x40, 0x08)
 *===========================================================================*/
/*
 * StaryuAnimation durations (from ASM DrawStaryu 0x17d92):
 * Variable timing per frame: 20, 19, 21, 18, 20, 19, 22, 19 frames.
 * StaryuAnimationSpriteIds alternates between frame 0 and frame 1.
 */
static const uint8_t staryu_anim_durations[] = {
    0x14, 0x13, 0x15, 0x12, 0x14, 0x13, 0x16, 0x13
};
#define STARYU_ANIM_STEPS 8

static void draw_staryu(GameState *state, uint8_t world_x, uint8_t world_y) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    uint8_t sx = (uint8_t)(world_x - scx);
    uint8_t sy = (uint8_t)(world_y - scy);

    /* UpdateAnimation (0x28a9) + DrawStaryu (0x17d92):
     * ASM: counter==0 at entry → ret z (no action).
     * counter>0 → dec, if 0 → advance index, load next duration+frame.
     * After UpdateAnimation: if counter==0, reset to {0x13, frame=0, idx=0}.
     * The post-check handles both first-frame init AND end-of-sequence wrap. */
    if (state->staryu_anim.frame_counter > 0) {
        state->staryu_anim.frame_counter--;
        if (state->staryu_anim.frame_counter == 0) {
            /* Advance to next step */
            uint8_t idx = state->staryu_anim.index + 1;
            if (idx >= STARYU_ANIM_STEPS) {
                /* Will be caught by the post-check below (counter==0) */
            } else {
                state->staryu_anim.frame_counter = staryu_anim_durations[idx];
                state->staryu_anim.index = idx;
                state->staryu_anim.frame ^= 1;
            }
        }
    }
    /* ASM post-check: if counter==0 (first frame OR wrap), reset */
    if (state->staryu_anim.frame_counter == 0) {
        state->staryu_anim.frame_counter = 0x13;
        state->staryu_anim.frame = 0;
        state->staryu_anim.index = 0;
    }

    uint8_t frame = state->staryu_anim.frame & 1;
    load_sprite_data(state, staryu_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Spinner (top stage only)
 * From draw_red_field_top_sprites.asm (0x17db9):
 *   World (0x88, 0x5A), frame from spinner_state[1] >> 2 (mod 6)
 *===========================================================================*/
static void draw_spinner(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    uint8_t sx = (uint8_t)(0x88 - scx);
    uint8_t sy = (uint8_t)(0x5A - scy);

    /* ASM: spinner_state[1] >> 2, modulo 6 for frame */
    uint8_t frame = (state->spinner_state[1] >> 2) % 6;
    load_sprite_data(state, spinner_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Ditto (top stage only)
 * From draw_red_field_top_sprites.asm (0x17d24):
 *   World (0x00, 0x10), sprite selected by stage_collision_state
 *===========================================================================*/
static void draw_ditto(GameState *state) {
    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    uint8_t sx = (uint8_t)(0x00 - scx);
    uint8_t sy = (uint8_t)(0x10 - scy);

    uint8_t collision_state = state->stage_collision_state;
    if (collision_state > 7) collision_state = 7;
    load_sprite_data(state, ditto_sprites_by_state[collision_state], sy, sx);
}

/*=============================================================================
 * Draw Timer (top and bottom stages)
 * From draw_timer.asm DrawTimer_GameBoyColor (0x175f5):
 *   GBC path: 4 sprites (minutes, colon, tens, ones) at fixed screen position.
 *   Initial bc=$7f00 from dispatch. Each digit advances X by 8.
 *===========================================================================*/
void draw_timer(GameState *state, uint8_t x, uint8_t y) {
    if (!state->timer_active) return;

    /* Minutes (lower nibble of BCD timer_minutes) */
    uint8_t m = state->timer_minutes & 0x0F;
    if (m > 9) m = 9;
    load_sprite_data(state, timer_digit_sprites[m], y, x);
    x += 8;

    /* Colon */
    load_sprite_data(state, timer_digit_sprites[10], y, x);
    x += 8;

    /* Tens seconds (upper nibble of BCD timer_seconds) */
    uint8_t tens = (state->timer_seconds >> 4) & 0x0F;
    if (tens > 9) tens = 9;
    load_sprite_data(state, timer_digit_sprites[tens], y, x);
    x += 8;

    /* Ones seconds (lower nibble) */
    uint8_t ones = state->timer_seconds & 0x0F;
    if (ones > 9) ones = 9;
    load_sprite_data(state, timer_digit_sprites[ones], y, x);
}

/*=============================================================================
 * Draw Mon Capture Animation (bottom stage only)
 * From draw_red_field_sprites.asm DrawMonCaptureAnimation (0x17c67):
 *   World (0x50, 0x38), 13 frames from ball_capture_anim.frame
 *===========================================================================*/
static void draw_mon_capture_animation(GameState *state) {
    if (!state->capturing_mon) return;

    uint8_t sx = (uint8_t)(0x50 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x38 - state->hram.scy);

    uint8_t frame = state->ball_capture_anim.frame;
    if (frame > 12) frame = 12;
    load_sprite_data(state, ball_capture_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Animated Mon - Red Stage (bottom stage only)
 * From draw_red_field_sprites.asm DrawAnimatedMon_RedStage (0x17c96):
 *   World (0x50, 0x3E), frame from current_animated_mon_sprite_frame (0-11)
 *   4 animation types x 3 frames each
 *===========================================================================*/
static void draw_animated_mon_red_stage(GameState *state) {
    if (!state->wild_mon_is_hittable) return;

    uint8_t sx = (uint8_t)(0x50 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x3E - state->hram.scy);

    uint8_t frame = state->current_animated_mon_sprite_frame;
    if (frame > 11) frame = 11;

    load_sprite_data(state, animated_mon_sprites[frame], sy, sx);
}

/*=============================================================================
 * Draw Evolution Indicator Arrows (top and bottom stages)
 * From draw_red_field_sprites.asm (0x17efb, 0x17f0f):
 *   Gate: evolution_objects_disabled == 0 (arrows visible in normal mode)
 *   Blinks every 16 frames (hFrameCounter bit 4)
 *   Top: 6 arrows from indicator_states[5..10]
 *   Bottom: 8 arrows from indicator_states[11..18]
 *===========================================================================*/

/* Top field arrow world positions + sprite pointers (ASM 0x17f3d) */
static const uint8_t evo_arrow_top_pos[6][2] = {
    {0x0D, 0x37}, {0x46, 0x22}, {0x8A, 0x4A},
    {0x41, 0x81}, {0x3D, 0x65}, {0x73, 0x74},
};
static const uint8_t *const evo_arrow_top_spr[6] = {
    sprite_evo_arrow_top_up,
    sprite_evo_arrow_top_right_down,
    sprite_evo_arrow_top_down,
    sprite_evo_arrow_top_left_up,
    sprite_evo_arrow_top_right_up,
    sprite_evo_arrow_top_up_right_up,
};

/* Bottom field arrow world positions + sprite pointers (ASM 0x17f4f) */
static const uint8_t evo_arrow_bot_pos[8][2] = {
    {0x2D, 0x13}, {0x6A, 0x13}, {0x25, 0x2D}, {0x73, 0x2D},
    {0x0F, 0x40}, {0x1F, 0x40}, {0x79, 0x40}, {0x89, 0x40},
};
static const uint8_t *const evo_arrow_bot_spr[8] = {
    sprite_evo_arrow_bot_up_left,
    sprite_evo_arrow_bot_up_right,
    sprite_evo_arrow_bot_left,
    sprite_evo_arrow_bot_right,
    sprite_evo_arrow_bot_down_left,
    sprite_evo_arrow_bot_down_left,
    sprite_evo_arrow_bot_down_right,
    sprite_evo_arrow_bot_down_right,
};

static void draw_evolution_indicator_arrows(GameState *state) {
    if (state->evolution_objects_disabled) return;

    /* Blink: visible only when hFrameCounter bit 4 is set */
    if (!(state->hram.frame_counter & 0x10)) return;

    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        for (int i = 0; i < 6; i++) {
            if (!state->indicator_states[5 + i]) continue;
            uint8_t sx = (uint8_t)(evo_arrow_top_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(evo_arrow_top_pos[i][1] - scy);
            load_sprite_data(state, evo_arrow_top_spr[i], sy, sx);
        }
    } else {
        for (int i = 0; i < 8; i++) {
            if (!state->indicator_states[11 + i]) continue;
            uint8_t sx = (uint8_t)(evo_arrow_bot_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(evo_arrow_bot_pos[i][1] - scy);
            load_sprite_data(state, evo_arrow_bot_spr[i], sy, sx);
        }
    }
}

/*=============================================================================
 * Draw Evolution Trinket (top and bottom stages)
 * From draw_red_field_sprites.asm (0x17f64, 0x17f75):
 *   Gate: evolution_objects_disabled != 0 (trinkets visible in evolution mode)
 *   Y bobbing: when (hFrameCounter & 0x0E) == 0, Y -= 1
 *   Top: 12 slots from active_evolution_trinkets[0..11]
 *   Bottom: 6 slots from active_evolution_trinkets[12..17]
 *===========================================================================*/

/* Top field trinket world positions (ASM 0x17fa6) */
static const uint8_t evo_trinket_top_pos[12][2] = {
    {0x4C, 0x0C}, {0x32, 0x12}, {0x66, 0x12}, {0x19, 0x25},
    {0x7F, 0x25}, {0x1E, 0x36}, {0x7F, 0x36}, {0x0E, 0x65},
    {0x8B, 0x65}, {0x49, 0x7A}, {0x59, 0x7A}, {0x71, 0x7A},
};

/* Bottom field trinket world positions (ASM 0x17fbe) */
static const uint8_t evo_trinket_bot_pos[6][2] = {
    {0x3D, 0x13}, {0x5B, 0x13}, {0x31, 0x17},
    {0x67, 0x17}, {0x2E, 0x2C}, {0x6A, 0x2C},
};

static void draw_evolution_trinket(GameState *state) {
    if (!state->evolution_objects_disabled) return;

    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;
    /* Y bobbing: bob up 1px when (frame_counter & 0x0E) == 0 */
    uint8_t bob = (state->hram.frame_counter & 0x0E) == 0 ? 1 : 0;

    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        const uint8_t *const *trinket_sprites = evo_trinket_top_sprites;
        for (int i = 0; i < 12; i++) {
            uint8_t type = state->active_evolution_trinkets[i];
            if (type == 0 || type > 7) continue;
            uint8_t sx = (uint8_t)(evo_trinket_top_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(evo_trinket_top_pos[i][1] - scy - bob);
            load_sprite_data(state, trinket_sprites[type], sy, sx);
        }
    } else {
        const uint8_t *const *trinket_sprites = evo_trinket_bot_sprites;
        for (int i = 0; i < 6; i++) {
            uint8_t type = state->active_evolution_trinkets[12 + i];
            if (type == 0 || type > 7) continue;
            uint8_t sx = (uint8_t)(evo_trinket_bot_pos[i][0] - scx);
            uint8_t sy = (uint8_t)(evo_trinket_bot_pos[i][1] - scy - bob);
            load_sprite_data(state, trinket_sprites[type], sy, sx);
        }
    }
}

/*=============================================================================
 * Draw Slot Glow - Red Field (bottom stage only)
 * From draw_red_field_sprites.asm DrawSlotGlow_RedField (0x17fca):
 *   Gate: slot_is_open != 0
 *   World (0x40, 0x01), 3 frames + 1 blank (4-cycle), counter >> 3 & 3
 *   Increments slot_glowing_anim_counter each frame
 *===========================================================================*/
static void draw_slot_glow(GameState *state) {
    if (!state->slot_is_open) return;

    state->slot_glowing_anim_counter++;

    uint8_t sx = (uint8_t)(0x40 - state->hram.scx);
    uint8_t sy = (uint8_t)(0x01 - state->hram.scy);

    /* 4-cycle animation: frames 0-2 draw, frame 3 is blank */
    uint8_t frame = (state->slot_glowing_anim_counter >> 3) & 3;
    if (frame < 3) {
        load_sprite_data(state, slot_glow_sprites[frame], sy, sx);
    }
}

/*=============================================================================
 * Main sprite dispatch
 *===========================================================================*/
void draw_red_field_sprites(GameState *state) {
    /* Clear sprite buffer for this frame (zero all OAM entries so stale sprites
     * aren't rendered - renderer skips entries with y==0 or x==0) */
    memset(state->sprite_buffer, 0, sizeof(state->sprite_buffer));
    state->sprite_buffer_size = 0;

    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        /* DrawSpritesRedFieldTop (0x1755c) — ASM dispatch order */
        draw_timer(state, 0x7F, 0x00);                               /*  1 */
        draw_voltorb_sprites(state);                     /*  2 */
        draw_ditto(state);                               /*  3 */
        draw_bellsprout(state);                           /*  4-5 (head+body) */
        draw_staryu(state, 0x2B, 0x69);                  /*  6 */
        draw_spinner(state);                              /*  7 */
        draw_pinball(state);                              /*  8 */
        draw_evolution_indicator_arrows(state);           /*  9 */
        draw_evolution_trinket(state);                    /* 10 */
    } else if (state->current_stage == STAGE_RED_FIELD_BOTTOM) {
        /* DrawSpritesRedFieldBottom (0x1757e) — ASM dispatch order */
        draw_timer(state, 0x7F, 0x00);                               /*  1 */
        draw_mon_capture_animation(state);               /*  2 */
        draw_animated_mon_red_stage(state);              /*  3 */
        draw_pikachu_savers(state);                      /*  4 */
        draw_flipper_sprites(state);                     /*  5 */
        draw_pinball(state);                              /*  6 */
        draw_evolution_indicator_arrows(state);           /*  7 */
        draw_evolution_trinket(state);                    /*  8 */
        draw_slot_glow(state);                            /*  9 */
    }
}

/*=============================================================================
 * Scoreboard Update
 *
 * Populates the window-layer tilemap with score, lives, and status icons.
 * Tile encoding for status bar:
 *   $81 = blank/space
 *   $82 = comma
 *   $83 = pokeball icon (party count label)
 *   $84 = lightning bolt (pikachu saver charged)
 *   $85 = heart icon (ball life label)
 *   $86-$8F = digits 0-9
 *
 * Translated from Func_8645 (score), Func_dba9 (lives),
 * DrawNumPartyMonsIcon, DrawPikachuSaverLightningBoltIcon.
 *===========================================================================*/
void update_scoreboard(GameState *state) {
    if (!state->vram) return;

    /* When bottom text is active, don't overwrite the message buffer with score data.
     * ASM: wDisableDrawScoreboardInfo controls this. */
    if (state->disable_draw_scoreboard_info) return;

    uint8_t *buf = state->bottom_message_buffer;

    /* Fill visible area with blanks (row 0 and row 1 shadow) */
    memset(&buf[0x40], 0x81, 0x20);
    memset(&buf[0xC0], 0x81, 0x20);

    /* --- Party Pokemon icon + count (offsets $40-$43) ---
     * ASM DrawNumPartyMonsIcon (0xdc7c): pokeball icon ALWAYS at $40.
     * ConvertHexByteToDecWord: d=hundreds, e=(tens<<4)|ones.
     * Digits written at $41+ with leading zero suppression via .drawDigit.
     * hl auto-increments so digits fill $41, $42, $43 as needed. */
    buf[0x40] = 0x83;  /* Pokeball icon - always stays here */
    {
        uint8_t n = state->num_party_mons;
        uint8_t hundreds = n / 100;
        uint8_t tens = (n / 10) % 10;
        uint8_t ones = n % 10;
        int out = 0x41;  /* ASM: ld hl, wBottomMessageBuffer + $41 */
        int suppress = 1;  /* c = 1: leading zero suppression active */

        /* Hundreds digit (from d register in ASM) */
        if (hundreds != 0) {
            buf[out++] = (uint8_t)(0x86 + hundreds);
            suppress = 0;
        }
        /* Tens digit (from swap(e) & 0xF in ASM) */
        if (tens != 0 || !suppress) {
            buf[out++] = (uint8_t)(0x86 + tens);
            suppress = 0;
        }
        /* Ones digit (from e & 0xF in ASM, c forced to 0) */
        buf[out++] = (uint8_t)(0x86 + ones);
    }

    /* --- Spacer ($43) --- */
    buf[0x43] = 0x81;

    /* --- Ball life icon + count (offsets $44-$45) --- */
    buf[0x44] = 0x85;  /* Heart icon */
    /* ASM: (cur_ball_life XOR 3) + 1 + $86
     * Ball life 0 → 4, 1 → 3, 2 → 2, 3 → 1 */
    {
        uint8_t lives_display = (state->cur_ball_life ^ 3) + 1;
        if (lives_display > 9) lives_display = 9;
        buf[0x45] = (uint8_t)(0x86 + lives_display);
    }

    /* --- Pikachu saver icon (offset $46) --- */
    if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
        buf[0x46] = 0x84;  /* Lightning bolt - charged */
    } else {
        buf[0x46] = 0x81;  /* Blank - not charged */
    }

    /* --- Score display (offsets $47 onwards) ---
     * ASM Func_8524: 6-byte BCD score (score[5] = MSB, score[0] = LSB).
     * Processes all 12 digits with leading zero suppression.
     * Digits go to row 0 of window ($47-$52 = 12 positions).
     * Commas go to row 1 of window via set 7 trick ($C7, $CA, $CD, $D0).
     * Trailing "0" tile at $53 (always written after the loop).
     * Row 1 is only ~2px visible (WY=$86), so commas appear as small marks. */
    {
        int out = 0x47;
        int leading_zero = 1;
        int b = 12;  /* ASM: lb bc, $0c, $01 */

        for (int i = 5; i >= 0; i--) {
            uint8_t byte = state->score[i];

            /* High nibble */
            uint8_t hi = (byte >> 4) & 0x0F;
            if (hi != 0 || !leading_zero || (b == 1)) {
                buf[out] = (uint8_t)(0x86 + hi);
                leading_zero = 0;
                /* Comma check: at b = 12, 9, 6, 3 */
                if (b == 12 || b == 9 || b == 6 || b == 3) {
                    buf[out | 0x80] = 0x82;  /* Comma in row 1 (set 7, e) */
                }
            } else {
                /* Leading zero: leave blank (buf already $81 from init) */
            }
            out++;
            b--;

            /* Low nibble */
            uint8_t lo = byte & 0x0F;
            if (lo != 0 || !leading_zero || (b == 1)) {
                buf[out] = (uint8_t)(0x86 + lo);
                leading_zero = 0;
                if (b == 12 || b == 9 || b == 6 || b == 3) {
                    buf[out | 0x80] = 0x82;
                }
            }
            out++;
            b--;
        }

        /* Trailing "0" (ASM: ld a, $86; ld [de], a after loop) */
        buf[out] = 0x86;
    }

    /* --- Copy buffer to window tilemap in VRAM ---
     * Window tilemap is at win_map[0] (tile indices).
     * Bank 1 holds attributes (palette, bank, flip) - set to $00.
     * The scoreboard occupies the first row of the window map (20 tiles).
     * WY=$86 covers 10 scanlines (lines 134-143) = 1.25 tile rows,
     * so clear row 2 (offset 32+) with blanks to prevent garbage. */
    VirtualVRAM *vram = state->vram;
    for (int i = 0; i < 20; i++) {
        vram->win_map[0][i] = buf[0x40 + i];
        vram->win_map[1][i] = 0x00;  /* palette 0, bank 0, no flips */
    }
    /* Copy row 1 from buffer (commas go here via set 7 trick).
     * Only top ~2 pixels of this row are visible (WY=$86). */
    for (int i = 0; i < 20; i++) {
        vram->win_map[0][32 + i] = buf[0xC0 + i];
        vram->win_map[1][32 + i] = 0x00;
    }
}

/*=============================================================================
 * Bottom Text System
 *
 * Translated from home/text.asm.
 * Manages scrolling and stationary text displayed in the window layer
 * (WY=$86 bottom status bar area).
 *
 * Text chars are mapped to tile indices: 'A'=0x91, ' '=0x81, '0'=0x86.
 *===========================================================================*/

/* Convert ASCII char to GBC tile index (from PlaceTextLow 0x3129).
 * ASM: letters use `add $bf` which wraps 'A'($41) to $00, 'Z'($5A) to $19.
 * These map to Alphabet2Gfx tiles loaded at vTilesOB ($8000).
 * Digits use `add $56` which maps '0'($30) to $86.
 * Special chars (!, ., etc.) use LoadSpecialTextChar to dynamically load
 * glyph data into shared tile positions ($83, $85, $86, $87). */
static uint8_t char_to_tile(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');  /* $00-$19 */
    if (c >= '0' && c <= '9') return (uint8_t)(0x86 + (c - '0'));
    if (c == ',') return 0x82;
    if (c == '!') return 0x85;   /* ASM: LoadSpecialTextChar(5), tile $85 */
    if (c == '.') return 0x86;   /* ASM: LoadSpecialTextChar(7), tile $86 */
    if (c == ':') return 0x83;   /* ASM: LoadSpecialTextChar(8), tile $83 */
    if (c == '\'') return 0x85;  /* ASM: LoadSpecialTextChar(2), tile $85 */
    if (c == '*') return 0x87;   /* ASM: LoadSpecialTextChar(4), tile $87 */
    if (c == 'x') return 0x85;   /* ASM: LoadSpecialTextChar(6), tile $85 */
    if (c == 'e') return 0x83;   /* ASM: LoadSpecialTextChar(3), tile $83 */
    return 0x81;  /* Space / default = blank tile */
}

/* Map special ASCII char to LoadSpecialTextChar char_id (0-8).
 * Returns -1 if the char is not a special text char.
 * The ASM PlaceText calls LoadSpecialTextChar each time a special char
 * is encountered, dynamically loading glyph tile data into shared VRAM
 * positions. Since multiple chars share the same tile slot, the last
 * one loaded is what gets displayed. */
static int char_to_special_id(char c) {
    switch (c) {
    case '!':  return 5;  /* Exclamation */
    case '.':  return 7;  /* Period */
    case ':':  return 8;  /* Colon */
    case '\'': return 2;  /* Apostrophe */
    case '*':  return 4;  /* Asterisk */
    case 'x':  return 6;  /* Little x */
    case 'e':  return 3;  /* é (lowercase e in our ASCII mapping) */
    default:   return -1;
    }
}

/* Load all special text char glyph tiles needed by a text string.
 * ASM PlaceText (0x312b) calls LoadSpecialTextChar for each special char
 * as it processes the string left-to-right. Since multiple chars share
 * VRAM tile positions, we process in order so the last one wins. */
static void load_special_chars_for_text(GameState *state, const char *text) {
    for (int i = 0; text[i] != '\0'; i++) {
        int sid = char_to_special_id(text[i]);
        if (sid >= 0) {
            load_special_text_char_gfx(state, (uint8_t)sid);
        }
    }
}

/* EnableBottomText (0x30db) */
void enable_bottom_text(GameState *state) {
    state->hram.wy = 0x86;
    state->bottom_text_enabled = 1;
    state->disable_draw_scoreboard_info = 1;
}

/* FillBottomMessageBufferWithBlackTile (0x30e8) */
void fill_bottom_message_buffer_with_black_tile(GameState *state) {
    memset(state->bottom_message_buffer, 0x81, 256);
    memset(state->bottom_message_text, 0x81, 256);
    /* Disable all text entries */
    for (int i = 0; i < 3; i++) {
        state->scrolling_text[i].enabled = 0;
        state->stationary_text[i].enabled = 0;
    }
}

/* LoadScrollingText (0x32aa)
 * Header format (6 bytes):
 *   [0] scroll_delay, [1] start_offset+0x40, [2] stop_offset+0x40,
 *   [3] stop_duration, [4] text_offset*0x10, [5] total_steps */
void load_scrolling_text(GameState *state, int slot_index,
                         const uint8_t *header, const char *text) {
    if (slot_index < 0 || slot_index >= 3) return;

    ScrollingText *st = &state->scrolling_text[slot_index];
    st->enabled = 1;
    st->scroll_delay_counter = header[0];
    st->scroll_delay = header[0];
    st->message_box_offset = header[1];
    st->stop_offset = header[2];
    st->stop_duration = header[3];
    st->source_text_offset = header[4];
    st->scroll_steps_remaining = header[5];

    /* Load glyph tile data for any special characters in the text */
    load_special_chars_for_text(state, text);

    /* Copy text string into bottom_message_text at source_text_offset */
    uint8_t offset = header[4];
    for (int i = 0; text[i] != '\0'; i++) {
        if (offset + i >= 255) break;
        state->bottom_message_text[offset + i] = char_to_tile(text[i]);
    }
}

/* LoadStationaryTextAndHeader (0x3357)
 * Header format (4 bytes):
 *   [0] offset+0x40, [1] text_offset*0x10,
 *   [2] duration_low, [3] duration_high */
void load_stationary_text(GameState *state, int slot_index,
                          const uint8_t *header, const char *text) {
    if (slot_index < 0 || slot_index >= 3) return;

    StationaryText *st = &state->stationary_text[slot_index];
    st->enabled = 1;
    st->message_box_offset = header[0];
    st->source_text_offset = header[1];
    st->duration_low = header[2];
    st->duration_high = header[3];

    /* Load glyph tile data for any special characters in the text */
    load_special_chars_for_text(state, text);

    /* Copy text string into bottom_message_text at source_text_offset */
    uint8_t offset = header[1];
    for (int i = 0; text[i] != '\0'; i++) {
        if (offset + i >= 255) break;
        state->bottom_message_text[offset + i] = char_to_tile(text[i]);
    }
}

/*
 * HandleScrollingText (0x3325): per-frame update for one scrolling text entry.
 * Returns 1 if text is still active, 0 if done.
 */
static int handle_scrolling_text(ScrollingText *st, uint8_t *msg_text,
                                 uint8_t *msg_buffer) {
    if (!st->enabled) return 0;

    /* Delay countdown */
    st->scroll_delay_counter--;
    if (st->scroll_delay_counter != 0) return 1;

    /* Reset delay counter */
    st->scroll_delay_counter = st->scroll_delay;

    /* ASM HandleScrollingText (0x3325): check if at stop position.
     * If paused, skip position decrement but still place text and decrement steps. */
    bool skip_scroll = false;
    if (st->message_box_offset == st->stop_offset) {
        /* ASM: dec stop_duration, then jr nz .SkipScroll */
        st->stop_duration--;
        if (st->stop_duration != 0) {
            skip_scroll = true;
        }
        /* When stop_duration hits 0, fall through to scroll */
    }

    if (!skip_scroll) {
        /* Scroll one step: decrement position (text moves left on screen) */
        st->message_box_offset--;
    }

    /* Place text at current position (always runs, even during pause).
     * ASM PlaceTextLow copies until 0x00 terminator; the ASM text data
     * includes a trailing space before @ that clears the position after
     * the last character.  We replicate this by writing a blank tile (0x81)
     * after the 20 text tiles, preventing stale chars from lingering.
     * ASM PlaceTextLow also writes row 1 (+0x80) for commas/blanks. */
    uint8_t src_off = st->source_text_offset;
    uint8_t dst_off = st->message_box_offset;
    int i;
    for (i = 0; i < 20 && (dst_off + i) < 0x60; i++) {
        msg_buffer[dst_off + i] = msg_text[src_off + i];
        msg_buffer[(dst_off + i) + 0x80] = msg_text[(src_off + i) + 0x80];
    }
    /* Clear trailing position (matches ASM trailing space before @ terminator) */
    if ((dst_off + i) < 0x60) {
        msg_buffer[dst_off + i] = 0x81;
        msg_buffer[(dst_off + i) + 0x80] = 0x81;
    }

    /* Decrement remaining steps (ASM: always decremented, even during pause) */
    st->scroll_steps_remaining--;
    if (st->scroll_steps_remaining == 0) {
        st->enabled = 0;
        return 0;
    }

    return 1;
}

/*
 * HandleStationaryText (0x33c3): per-frame update for one stationary text entry.
 * Returns 1 if text is still active, 0 if done.
 */
static int handle_stationary_text(StationaryText *st, uint8_t *msg_text,
                                  uint8_t *msg_buffer) {
    if (!st->enabled) return 0;

    /* Place text at position.
     * ASM HandleStationaryText calls PlaceTextLow which writes BOTH
     * row 0 (character tiles) and row 1 (+0x80, commas/blanks). */
    uint8_t src_off = st->source_text_offset;
    uint8_t dst_off = st->message_box_offset;
    for (int i = 0; i < 20 && (dst_off + i) < 0x60; i++) {
        msg_buffer[dst_off + i] = msg_text[src_off + i];
        msg_buffer[(dst_off + i) + 0x80] = msg_text[(src_off + i) + 0x80];
    }

    /* Decrement duration byte-by-byte (ASM HandleStationaryText 0x33c3).
     * ASM decrements low byte first; only touches high byte when low hits 0.
     * High byte underflow (bit 7 set after dec) triggers disable.
     * L03: duration (0,0) wraps to 0xFF → runs 256 frames, not 0. */
    st->duration_low--;
    if (st->duration_low != 0) return 1;

    /* Low byte hit zero — decrement high byte */
    st->duration_high--;
    if (!(st->duration_high & 0x80)) return 1;  /* bit 7 clear = still counting */

    /* High byte underflowed — disable text */
    st->enabled = 0;
    return 0;
}

/* UpdateBottomText (0x33e3) - per-frame update of all text entries */
void update_bottom_text(GameState *state) {
    if (!state->bottom_text_enabled) {
        state->disable_draw_scoreboard_info = 0;
        return;
    }
    /* ASM does NOT clear the buffer each frame. On frames where the scroll
     * delay counter prevents scrolling, HandleScrollingText returns early
     * without placing text. The buffer retains the PREVIOUS frame's content,
     * so the window shows stable text. Clearing here would blank the display
     * on non-scroll frames, causing visible flashing. */
    int active_count = 0;

    /* Process all 3 scrolling text slots */
    for (int i = 0; i < 3; i++) {
        active_count += handle_scrolling_text(
            &state->scrolling_text[i],
            state->bottom_message_text,
            state->bottom_message_buffer);
    }

    /* Process all 3 stationary text slots */
    for (int i = 0; i < 3; i++) {
        active_count += handle_stationary_text(
            &state->stationary_text[i],
            state->bottom_message_text,
            state->bottom_message_buffer);
    }

    /* Copy buffer to window tilemap.
     * Row 0: text tiles from buffer[0x40+].
     * Row 1: comma/blank tiles from buffer[0xC0+] (the +0x80 "set 7" row).
     * ASM PlaceTextLow writes blanks ($81) to row 1 for normal chars and
     * comma tile ($82) for commas, so row 1 is always properly populated. */
    if (state->vram) {
        for (int i = 0; i < 20; i++) {
            state->vram->win_map[0][i] = state->bottom_message_buffer[0x40 + i];
            state->vram->win_map[1][i] = 0x00;
        }
        for (int i = 0; i < 20; i++) {
            state->vram->win_map[0][32 + i] = state->bottom_message_buffer[0xC0 + i];
            state->vram->win_map[1][32 + i] = 0x00;
        }
    }

    /* Auto-disable when all text completes (ASM UpdateBottomText 0x33e3) */
    if (active_count == 0) {
        state->bottom_text_enabled = 0;
        /* L04: ASM does NOT clear disable_draw_scoreboard_info here.
         * It clears one frame later, at the top of UpdateBottomText when
         * it detects bottom_text_enabled == 0. */
        fill_bottom_message_buffer_with_black_tile(state);
        /* M16: ASM reloads StageRedFieldTopStatusBarSymbolsGfx at $8830
         * (4 tiles, $40 bytes) to restore tiles $83-$86 after text finishes. */
        if (state->vram) {
            char path[260];
            snprintf(path, sizeof(path), "%s/gfx/stage/red_top/status_bar_symbols_gameboycolor.png",
                     state->asset_base_path);
            size_t data_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &data_size);
            if (tile_data && data_size > 0x70) {
                vram_write(state->vram, 0, 0x8830, tile_data + 0x30, 0x40);
            }
            free(tile_data);
        }
    }
}

/* ShowBallLossText (0xdc6d) - convenience wrapper */
void show_ball_loss_text(GameState *state, const uint8_t *header, const char *text) {
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    load_scrolling_text(state, 2, header, text);  /* Slot 2 = wScrollingText3 */
}
