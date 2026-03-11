/*
 * Flipper System
 *
 * Translated from engine/pinball_game/flippers.asm (0xe0fe).
 *
 * Flippers have 16 angular positions (high byte 0x00-0x0F, low byte = sub-position).
 * Each frame, the state increments by +0x0333 (button held) or -0x0333 (released).
 * Collision detection walks from previous state to current, checking precomputed
 * radius tables. Force is calculated from radius magnitude * state change.
 */

#include "game/flippers.h"
#include "game/config_data.h"
#include "game/joypad.h"
#include "audio/audio.h"
#include "renderer/tile_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Flipper constants now read from config/physics.json via state->config->physics */

/* Flipper collision data loaded from binary files */
static uint8_t *flipper_radii[2] = { NULL, NULL };       /* Bank 0, Bank 1 */
static size_t flipper_radii_size[2] = { 0, 0 };
static uint8_t *flipper_normals[2] = { NULL, NULL };     /* Bank 0, Bank 1 */
static size_t flipper_normals_size[2] = { 0, 0 };

void load_flipper_collision_data(GameState *state) {
    char path[260];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s/data/collision/flippers/radii_%d",
                 state->asset_base_path, i);
        free(flipper_radii[i]);
        flipper_radii[i] = load_binary_file(path, &flipper_radii_size[i]);
        if (!flipper_radii[i]) {
            fprintf(stderr, "flippers: failed to load '%s'\n", path);
        }

        snprintf(path, sizeof(path), "%s/data/collision/flippers/normal_angles_%d",
                 state->asset_base_path, i);
        free(flipper_normals[i]);
        flipper_normals[i] = load_binary_file(path, &flipper_normals_size[i]);
        if (!flipper_normals[i]) {
            fprintf(stderr, "flippers: failed to load '%s'\n", path);
        }
    }
}

void free_flipper_collision_data(void) {
    for (int i = 0; i < 2; i++) {
        free(flipper_radii[i]);
        flipper_radii[i] = NULL;
        flipper_radii_size[i] = 0;
        free(flipper_normals[i]);
        flipper_normals[i] = NULL;
        flipper_normals_size[i] = 0;
    }
}

/*
 * UpdateFlipperStates (0xe118):
 * Read input, update flipper angular positions with delta ±0x0333/frame.
 */
static void update_flipper_states(GameState *state) {
    const PhysicsConfig *phys = &state->config->physics;
    uint16_t flipper_delta = phys->flipper_delta;
    uint16_t flipper_max = phys->flipper_max;

    /* Save previous states (high byte only) */
    state->previous_left_flipper_state = (uint8_t)(state->left_flipper_state >> 8);
    state->previous_right_flipper_state = (uint8_t)(state->right_flipper_state >> 8);

    /* Left flipper */
    {
        int16_t delta = -(int16_t)flipper_delta;
        if (joypad_is_key_held(state, &state->key_config_left_flipper) &&
            !state->flippers_disabled) {
            delta = (int16_t)flipper_delta;
        }

        uint8_t hi = (uint8_t)(state->left_flipper_state >> 8);
        /* Boundary checks: can't go below 0 or above 0x0F */
        if (hi == 0 && delta < 0) delta = 0;
        if (hi >= 0x0F && delta > 0) delta = 0;

        int32_t new_state = (int32_t)state->left_flipper_state + delta;
        if (new_state < 0) new_state = 0;
        if (new_state > (int32_t)flipper_max) new_state = (int32_t)flipper_max;

        state->left_flipper_state_change = (uint16_t)delta;
        state->left_flipper_state = (uint16_t)new_state;
    }

    /* Right flipper */
    {
        int16_t delta = -(int16_t)flipper_delta;
        if (joypad_is_key_held(state, &state->key_config_right_flipper) &&
            !state->flippers_disabled) {
            delta = (int16_t)flipper_delta;
        }

        uint8_t hi = (uint8_t)(state->right_flipper_state >> 8);
        if (hi == 0 && delta < 0) delta = 0;
        if (hi >= 0x0F && delta > 0) delta = 0;

        int32_t new_state = (int32_t)state->right_flipper_state + delta;
        if (new_state < 0) new_state = 0;
        if (new_state > (int32_t)flipper_max) new_state = (int32_t)flipper_max;

        state->right_flipper_state_change = (uint16_t)delta;
        state->right_flipper_state = (uint16_t)new_state;
    }
}

/*
 * ReadFlipperCollisionAttributes (0xe25a):
 * Checks if the ball is within flipper collision range and walks from
 * previous to current state checking radius lookup tables.
 *
 * Ball must be in range: X [43, 90], Y [123, 154]
 * The flipper collision data is organized as:
 *   16 states * 48 x-columns * 32 y-rows = 24576 bytes per bank
 *   Each state = 0x600 bytes
 *   Each x-column = 32 bytes
 */
static int read_flipper_collision_attributes(
    GameState *state,
    uint8_t ball_x_hi, uint8_t ball_y_hi,
    uint8_t prev_state, uint8_t cur_state)
{
    const PhysicsConfig *phys = &state->config->physics;

    /* Check if ball is in flipper range */
    if (ball_x_hi < phys->flipper_collision_x_min) return 0;
    int x_offset = ball_x_hi - phys->flipper_collision_x_min;
    if (x_offset >= phys->flipper_collision_x_range) return 0;

    if (ball_y_hi < phys->flipper_collision_y_min) return 0;
    int y_offset = ball_y_hi - phys->flipper_collision_y_min;
    if (y_offset >= phys->flipper_collision_y_range) return 0;

    /* Walk from previous state to current state */
    uint8_t check_state = prev_state;
    while (1) {
        /* Calculate offset into collision data:
         * offset = state * 0x600 + x_offset * 32 + y_offset */
        uint32_t offset = (uint32_t)check_state * 0x600 +
                          (uint32_t)x_offset * 32 +
                          (uint32_t)y_offset;

        /* Determine which bank */
        int bank = (offset >= 0x4000) ? 1 : 0;
        size_t bank_offset = (bank == 0) ? offset : (offset - 0x4000);
        /* ASM adds $40 to high byte for bank 0, meaning data starts at $4000.
         * The binary files should be loaded contiguously. Adjust: */
        /* ASM ReadFlipperCollisionAttributes (0xe25a): uses $4000 boundary for bank split.
         * When computed offset < $4000, data is read from FlipperCollisionRadii (bank 0).
         * When offset >= $4000, data is read from FlipperCollisionRadii2 (bank 1) at
         * offset - $4000. The binary files radii_0/radii_1 correspond to these two banks
         * and should each be exactly $4000 (16384) bytes. If radii_0 is not $4000 bytes,
         * the file-size-based split below still works correctly, but a size mismatch
         * would indicate the collision data files were generated incorrectly.
         * Verified: flipper_radii_size[0] should == 0x4000 for correct bank boundary. */
        bank_offset = offset;
        bank = 0;
        if (bank_offset >= flipper_radii_size[0] && flipper_radii[1]) {
            bank_offset -= flipper_radii_size[0];
            bank = 1;
        }

        uint8_t radius = 0;
        if (flipper_radii[bank] && bank_offset < flipper_radii_size[bank]) {
            radius = flipper_radii[bank][bank_offset];
        }

        if (radius != 0) {
            /* Collision found! */
            state->hram.flipper_collision_radius = radius;

            /* Read normal angle from same offset in normal angles data */
            uint8_t normal = 0;
            /* Normal angles are stored with a different offset mapping.
             * ASM adds $60 to high byte for bank 0, $20 for bank 1 */
            if (flipper_normals[bank] && bank_offset < flipper_normals_size[bank]) {
                normal = flipper_normals[bank][bank_offset];
            }
            state->flipper_collision_normal_angle = normal;
            state->flipper_collision = 1;
            return 1;
        }

        /* Move toward current state */
        if (check_state == cur_state) break;
        if (check_state < cur_state) check_state++;
        else check_state--;
    }
    return 0;
}

/*
 * CheckFlipperCollision (0xe1f0):
 * Determines if ball collides with left or right flipper.
 *
 * Includes a lookahead for high-speed balls: if no collision at the current
 * position and the ball has significant downward velocity, checks up to
 * (velocity >> 8) pixels ahead along the Y axis. This prevents tunneling
 * through the thin flipper surface at high speed.
 *
 * The lookahead uses the ball's current X (pre-move), so balls in the center
 * gap between flippers correctly see radii=0 at all Y positions — no false
 * positives on drain trajectories.
 */
static void check_flipper_collision(GameState *state) {
    uint8_t ball_x_hi = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t ball_y_hi = (uint8_t)(state->ball_y_pos >> 8);

    if (ball_x_hi < 80) {
        /* Left flipper - use ball position directly */
        if (read_flipper_collision_attributes(
                state, ball_x_hi, ball_y_hi,
                state->previous_left_flipper_state,
                (uint8_t)(state->left_flipper_state >> 8))) {
            state->hram.flipper_state_change = state->left_flipper_state_change;
        }
    } else {
        /* Right flipper - mirror X position.
         * ASM: full 16-bit mirror = 0xA000 - ball_x_pos.
         * sub $1; cpl (lo byte), sbc 160; cpl (hi byte with carry). */
        uint16_t mirror = (uint16_t)(0xA000 - state->ball_x_pos);
        uint8_t mirror_x = (uint8_t)(mirror >> 8);

        if (read_flipper_collision_attributes(
                state, mirror_x, ball_y_hi,
                state->previous_right_flipper_state,
                (uint8_t)(state->right_flipper_state >> 8))) {
            state->hram.flipper_state_change = state->right_flipper_state_change;
        }
    }

    /* Lookahead: if no collision found and ball is falling fast, check future
     * Y positions to prevent tunneling through the flipper surface. */
    if (!state->flipper_collision && state->ball_y_velocity > 0x0200) {
        uint8_t steps = (uint8_t)(state->ball_y_velocity >> 8);
        if (steps > 7) steps = 7;

        for (uint8_t i = 1; i <= steps; i++) {
            uint8_t look_y = ball_y_hi + i;

            if (ball_x_hi < 80) {
                if (read_flipper_collision_attributes(
                        state, ball_x_hi, look_y,
                        state->previous_left_flipper_state,
                        (uint8_t)(state->left_flipper_state >> 8))) {
                    state->hram.flipper_state_change = state->left_flipper_state_change;
                    break;
                }
            } else {
                uint16_t mirror = (uint16_t)(0xA000 - state->ball_x_pos);
                uint8_t mirror_x = (uint8_t)(mirror >> 8);
                if (read_flipper_collision_attributes(
                        state, mirror_x, look_y,
                        state->previous_right_flipper_state,
                        (uint8_t)(state->right_flipper_state >> 8))) {
                    state->hram.flipper_state_change = state->right_flipper_state_change;
                    break;
                }
            }
        }
    }
}

/*
 * CalculateFlipperYForce (0xe379):
 * Computes force = radius_magnitude * (state_change * 4) as 32-bit multiply.
 * Returns the middle 16 bits (treating result as 16.16 fixed-point).
 */
static int16_t calculate_flipper_y_force(int16_t radius_magnitude, int16_t state_change) {
    /* Multiply state change by 4 */
    int32_t sc4 = (int32_t)state_change * 4;
    /* 32-bit multiply */
    int32_t result = (int32_t)radius_magnitude * sc4;
    /* Extract 16.16 -> take bits [23:8] which is the middle 16 bits */
    int16_t force = (int16_t)(result >> 8);
    return force;
}

/*
 * HandleFlipperCollision (0xe442):
 * Called when ball collides with a flipper. Sets collision state,
 * calculates force vectors, and applies collision normal.
 */
static void handle_flipper_collision(GameState *state) {
    state->is_ball_colliding = 1;
    state->ball_position_tile_offset = 0;
    state->cur_collision_attribute = 0;
    state->cur_collision_tile_offset = 0;

    /* Look up radius magnitude */
    uint8_t radius = state->hram.flipper_collision_radius;
    uint16_t magnitude = 0;
    if (radius < 32) {
        magnitude = state->config->physics.flipper_radius_magnitudes[radius];
    }

    /* Get state change and multiply by 4 */
    int16_t state_change = (int16_t)state->hram.flipper_state_change;

    /* Calculate Y force */
    int16_t y_force = calculate_flipper_y_force((int16_t)magnitude, state_change);

    state->flipper_y_force = y_force;

    /* Set collision normal angle.
     * For right flipper: mirror the angle (cpl + inc) */
    uint8_t ball_x_hi = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t normal = state->flipper_collision_normal_angle;
    if (ball_x_hi >= 80) {
        normal = (uint8_t)(~normal + 1);  /* mirror across Y axis */
    }
    state->collision_normal_angle = normal;

    /* Set amplification for collision response */
    state->collision_force_amplification = 1;

    /* Don't apply Y force if it would push ball downward into flipper */
    if (y_force < 0) {  /* ASM checks bit 7 of high byte */
        state->flipper_y_force = 0;
    }
}

/*
 * HandleFlippers (0xe0fe): Main entry point.
 */
void handle_flippers(GameState *state) {
    /* Clear flipper collision state */
    state->flipper_collision = 0;
    state->hram.flipper_collision_radius = 0;
    state->flipper_x_force = 0;

    /* ASM: PlayFlipperSoundIfPressed (0xe118) — play SFX $0C on newly pressed */
    if (!state->flippers_disabled) {
        if (joypad_is_key_pressed(state, &state->key_config_left_flipper) ||
            joypad_is_key_pressed(state, &state->key_config_right_flipper)) {
            PLAY_SFX(state, "flipper", 0x00, 0x0C);
        }
    }

    /* Update flipper angular positions */
    update_flipper_states(state);

    /* Check for ball-flipper collision */
    check_flipper_collision(state);

    /* Handle collision if detected */
    if (state->flipper_collision) {
        handle_flipper_collision(state);
    }
}
