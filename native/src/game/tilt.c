/*
 * Tilt Mechanics
 *
 * Translated from home/tilt.asm.
 * Three tilt directions: left, right, upper.
 * Each tilt has a counter (max 3 for L/R, 4 for up), cooldown logic,
 * and applies a 1-pixel nudge to the ball position.
 * ApplyTiltForces looks up a force vector table based on active tilts
 * and adds it to the ball velocity.
 */

#include "game/tilt.h"
#include "game/joypad.h"
#include "game/config_data.h"
#include "audio/audio.h"
#include "renderer/tile_loader.h"
#include <stdio.h>
#include <string.h>

/*
 * Tilt force table data (from data/tilt/).
 * 5 tables × 256 entries × 4 bytes (int16 X force, int16 Y force).
 * Indexed by collision_normal_angle (0-255).
 * Loaded lazily on first use.
 */
#define TILT_TABLE_SIZE 1024  /* 256 entries × 4 bytes */

static uint8_t *tilt_left_only  = NULL;
static uint8_t *tilt_right_only = NULL;
static uint8_t *tilt_up_only    = NULL;
static uint8_t *tilt_up_left    = NULL;
static uint8_t *tilt_up_right   = NULL;
static bool tilt_tables_loaded  = false;

/* TiltForces pointer table (from home/tilt.asm 0x372d).
 * 8 entries indexed by 3-bit mask: bit2=upper, bit1=right, bit0=left.
 * NULL = no force (mask 0 or mask 3 = cancel). */
static uint8_t **tilt_force_table[8];

static void load_tilt_tables(const char *base_path) {
    char path[260];
    size_t sz;

    snprintf(path, sizeof(path), "%s/data/tilt/left_only", base_path);
    tilt_left_only = load_binary_file(path, &sz);
    snprintf(path, sizeof(path), "%s/data/tilt/right_only", base_path);
    tilt_right_only = load_binary_file(path, &sz);
    snprintf(path, sizeof(path), "%s/data/tilt/up_only", base_path);
    tilt_up_only = load_binary_file(path, &sz);
    snprintf(path, sizeof(path), "%s/data/tilt/up_left", base_path);
    tilt_up_left = load_binary_file(path, &sz);
    snprintf(path, sizeof(path), "%s/data/tilt/up_right", base_path);
    tilt_up_right = load_binary_file(path, &sz);

    /* Build pointer table matching ASM TiltForces (0x372d) */
    tilt_force_table[0] = NULL;              /* 000: no tilt */
    tilt_force_table[1] = &tilt_left_only;   /* 001: left only */
    tilt_force_table[2] = &tilt_right_only;  /* 010: right only */
    tilt_force_table[3] = NULL;              /* 011: left+right cancel */
    tilt_force_table[4] = &tilt_up_only;     /* 100: up only */
    tilt_force_table[5] = &tilt_up_left;     /* 101: up+left */
    tilt_force_table[6] = &tilt_up_right;    /* 110: up+right */
    tilt_force_table[7] = &tilt_up_only;     /* 111: up+left+right = up only */

    tilt_tables_loaded = true;
}

/*
 * Handle a single tilt direction.
 * max_count: 3 for left/right, 4 for upper.
 * pixel_offset_dir: -1 or +1 for how tilt_pixels_offset changes on push.
 * ball_axis: 0 = X, 1 = Y.
 * ball_dir: -1 or +1 for how ball position changes on push.
 */
static void handle_tilt_direction(
    GameState *state,
    const KeyConfig *key_config,
    uint8_t *counter,
    uint8_t *reset,
    uint8_t *pushing,
    uint8_t *pixel_offset,
    int pixel_offset_dir,
    int ball_axis,
    int ball_dir,
    uint8_t max_count
) {
    if (*reset) {
        /* Cooldown active */
        goto cooldown;
    }

    if (!joypad_is_key_held(state, key_config)) {
        goto cooldown;
    }

    if (*counter >= max_count) {
        /* Start cooldown */
        *reset = 1;
        goto cooldown;
    }

    (*counter)++;

    /* ASM: play tilt SFX on first push (counter 0→1) */
    if (*counter == 1) {
        PLAY_SFX(state, "tilt", 0x00, 0x3F);
    }

    /* Move ball position if visible and tilt enabled */
    if (state->pinball_is_visible && state->enable_ball_gravity_and_tilt) {
        if (ball_axis == 0) {
            /* X axis: modify integer part of ball_x_pos (±1 pixel) */
            state->ball_x_pos = (ufixed8_8)((uint16_t)state->ball_x_pos + (uint16_t)UINT_TO_UFIXED(ball_dir));
        } else {
            state->ball_y_pos = (ufixed8_8)((uint16_t)state->ball_y_pos + (uint16_t)UINT_TO_UFIXED(ball_dir));
        }
    }

    /* Update pixel offset */
    *pixel_offset = (uint8_t)((int8_t)*pixel_offset + pixel_offset_dir);
    *pushing = 1;
    return;

cooldown:
    *pushing = 0;
    if (*counter == 0) {
        /* Done cooling down - check if key released to clear reset */
        if (!joypad_is_key_held(state, key_config)) {
            *reset = 0;
        }
        return;
    }

    /* Decrement counter and reverse the pixel offset */
    (*counter)--;
    *pixel_offset = (uint8_t)((int8_t)*pixel_offset - pixel_offset_dir);
}

void handle_tilts(GameState *state) {
    /* HandleTilts (0x3582): call all three directions */

    /* Left tilt: moves ball left (x-1), screen offset right (+1) */
    handle_tilt_direction(
        state,
        &state->key_config_left_tilt,
        &state->left_tilt_counter,
        &state->left_tilt_reset,
        &state->left_tilt_pushing,
        &state->left_and_right_tilt_pixels_offset,
        +1,   /* pixel offset increases (screen shifts right) */
        0,    /* X axis */
        -1,   /* ball moves left */
        3     /* max 3 pushes */
    );

    /* Right tilt: moves ball right (x+1), screen offset left (-1) */
    handle_tilt_direction(
        state,
        &state->key_config_right_tilt,
        &state->right_tilt_counter,
        &state->right_tilt_reset,
        &state->right_tilt_pushing,
        &state->left_and_right_tilt_pixels_offset,
        -1,   /* pixel offset decreases (screen shifts left) */
        0,    /* X axis */
        +1,   /* ball moves right */
        3     /* max 3 pushes */
    );

    /* Upper tilt: moves ball down (y+1), screen offset up (-1) */
    handle_tilt_direction(
        state,
        &state->key_config_upper_tilt,
        &state->upper_tilt_counter,
        &state->upper_tilt_reset,
        &state->upper_tilt_pushing,
        &state->upper_tilt_pixels_offset,
        -1,   /* pixel offset decreases (screen shifts up) */
        1,    /* Y axis */
        +1,   /* ball moves down */
        4     /* max 4 pushes */
    );
}

void apply_tilt_forces(GameState *state) {
    /*
     * ApplyTiltForces (0x36C1):
     * Build a 3-bit direction mask from active tilt pushes,
     * look up the force table indexed by collision_normal_angle,
     * and add the force vector to ball velocity.
     */
    if (!state->pinball_is_visible || !state->enable_ball_gravity_and_tilt) {
        return;
    }

    /* Build direction mask: bit 0 = left, bit 1 = right, bit 2 = up
     * (ASM: samples upper first into bit 2, right into bit 1, left into bit 0) */
    uint8_t mask = 0;
    if (state->left_tilt_pushing)  mask |= 1;
    if (state->right_tilt_pushing) mask |= 2;
    if (state->upper_tilt_pushing) mask |= 4;

    if (mask == 0 || mask == 3) {
        /* No tilt or left+right cancel out (ASM: $FFFF pointer → return) */
        return;
    }

    /* Lazy-load tilt force tables */
    if (!tilt_tables_loaded) {
        load_tilt_tables(state->asset_base_path);
    }

    /* Look up the force table for this direction combination */
    uint8_t **table_ptr = tilt_force_table[mask];
    if (!table_ptr || !*table_ptr) return;

    uint8_t *table = *table_ptr;

    /* Index by collision_normal_angle × 4 bytes per entry (ASM 0x370d-0x3720) */
    uint16_t offset = (uint16_t)state->collision_normal_angle * 4;
    if (offset + 3 >= TILT_TABLE_SIZE) return;

    /* Read little-endian int16 force vectors and add to ball velocity */
    int16_t x_force = (int16_t)(table[offset] | (table[offset + 1] << 8));
    int16_t y_force = (int16_t)(table[offset + 2] | (table[offset + 3] << 8));

    state->ball_x_velocity += x_force;
    state->ball_y_velocity += y_force;
}
