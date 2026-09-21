/*
 * Game State - Initialization and lifecycle
 *
 * Corresponds to the WRAM/HRAM clearing in Start (home.asm 0x150)
 * and the initial setup in Main (home.asm 0x1FFC).
 */

#include "game/game_state.h"
#include "game/config_data.h"
#include "game/editor.h"
#include "game/rng.h"
#include "renderer/vram.h"
#include <stdlib.h>
#include <string.h>

GameState *game_state_init(void) {
    GameState *state = calloc(1, sizeof(GameState));
    if (!state) return NULL;

    /* Create config with hardcoded defaults (JSON loading happens later in main.c) */
    state->config = config_data_create();

    /* Allocate combined view VRAM snapshots */
    state->vram_top = vram_create();
    state->vram_bottom = vram_create();

    game_state_reset(state);
    return state;
}

void game_state_reset(GameState *state) {
    /* Save native-only pointers that must survive memset (L11) */
    VirtualVRAM *saved_vram = state->vram;
    VirtualVRAM *saved_vram_top = state->vram_top;
    VirtualVRAM *saved_vram_bottom = state->vram_bottom;
    bool saved_combined_view = state->combined_view_active;
    AudioEngine *saved_audio = state->audio;
    ConfigData *saved_config = state->config;
    ScriptEngine *saved_script_engine = state->script_engine;
    uint8_t *saved_vwf_font_gfx = state->vwf_font_gfx;
    uint8_t *saved_slot_force_field_data = state->slot_force_field_data;
    uint8_t saved_debug_mode = state->debug_mode;
    void *saved_editor_state = state->editor_state;
    char saved_asset_base_path[260];
    memcpy(saved_asset_base_path, state->asset_base_path, sizeof(saved_asset_base_path));
    char saved_active_table_folder[64];
    memcpy(saved_active_table_folder, state->active_table_folder, sizeof(saved_active_table_folder));

    /* Clear all state to zero (equivalent to ClearData on WRAM + HRAM) */
    memset(state, 0, sizeof(GameState));

    /* Restore native-only pointers */
    state->vram = saved_vram;
    state->vram_top = saved_vram_top;
    state->vram_bottom = saved_vram_bottom;
    state->combined_view_active = saved_combined_view;
    state->audio = saved_audio;
    state->config = saved_config;
    state->script_engine = saved_script_engine;
    state->vwf_font_gfx = saved_vwf_font_gfx;
    state->slot_force_field_data = saved_slot_force_field_data;
    state->debug_mode = saved_debug_mode;
    state->editor_state = saved_editor_state;
    memcpy(state->asset_base_path, saved_asset_base_path, sizeof(state->asset_base_path));
    memcpy(state->active_table_folder, saved_active_table_folder, sizeof(state->active_table_folder));

    /* Set GBC flag - we're always running in GBC mode */
    state->hram.gbc_flag = 1;
    state->hram.gbc_flag_backup = 1;

    /*
     * Initial setup from Main (home.asm 0x1FFC):
     * wd806 = $0B (joy initial delay)
     * wd807 = $04 (joy repeat rate)
     */
    state->joy_initial_delay = 0x0B;
    state->joy_repeat_rate = 0x04;

    /* Audio engine enabled by default */
    state->audio_engine_enabled = 1;

    /* Player name defaults to spaces (0x37 in charmap = space) */
    state->player_name[0] = 0x37;
    state->player_name[1] = 0x37;
    state->player_name[2] = 0x37;

    /* Start at the erase data / copyright screen flow */
    state->current_screen = SCREEN_ERASE_ALL_DATA;
    state->screen_state = 0;

    /* Default key configurations - matches DefaultKeyConfigs (0xca55) */
    state->key_config_ball_start.primary = BTN_A;
    state->key_config_ball_start.secondary = 0;
    state->key_config_left_flipper.primary = BTN_LEFT;
    state->key_config_left_flipper.secondary = 0;
    state->key_config_right_flipper.primary = BTN_A;
    state->key_config_right_flipper.secondary = 0;
    state->key_config_left_tilt.primary = BTN_DOWN;
    state->key_config_left_tilt.secondary = 0;
    state->key_config_right_tilt.primary = BTN_B;
    state->key_config_right_tilt.secondary = 0;
    state->key_config_upper_tilt.primary = BTN_SELECT;
    state->key_config_upper_tilt.secondary = 0;
    state->key_config_menu.primary = BTN_START;
    state->key_config_menu.secondary = 0;

    /* Default GBC palettes - set BG palette 0 to a grayscale ramp */
    state->bg_palettes[0].colors[0] = 0x7FFF; /* White */
    state->bg_palettes[0].colors[1] = 0x56B5; /* Light gray */
    state->bg_palettes[0].colors[2] = 0x294A; /* Dark gray */
    state->bg_palettes[0].colors[3] = 0x0000; /* Black */

    /* Initialize RNG — ASM sets wRNGModulus = $FF then calls ResetRNG */
    state->rng_modulus = 0xFF;
    reset_rng(state, state->rng_sram_seed);

    /* Bonus multiplier starts at 1x */
    state->cur_bonus_multiplier = 1;

    /* Ball lives */
    state->num_ball_lives = 3;

    /* Default high scores (from InitialHighScores, 0xd42e)
     * BCD 6-byte scores, 3-char names (stored as char index = ASCII - 0x37),
     * 4-byte ID (zeros). */
    {
        static const struct {
            uint8_t points[6];
            uint8_t name[3];
        } default_scores[5] = {
            /* 50,000,000 "NIN" - bigBCD6 is little-endian (x=0 is LSB) */
            {{0x00, 0x00, 0x00, 0x50, 0x00, 0x00},
             {0x17, 0x12, 0x17}},  /* N=0x4E-0x37, I=0x49-0x37, N=0x4E-0x37 */
            /* 40,000,000 "CRE" */
            {{0x00, 0x00, 0x00, 0x40, 0x00, 0x00},
             {0x0C, 0x1B, 0x0E}},  /* C=0x43-0x37, R=0x52-0x37, E=0x45-0x37 */
            /* 30,000,000 "GAM" */
            {{0x00, 0x00, 0x00, 0x30, 0x00, 0x00},
             {0x10, 0x0A, 0x16}},  /* G=0x47-0x37, A=0x41-0x37, M=0x4D-0x37 */
            /* 20,000,000 "HAL" */
            {{0x00, 0x00, 0x00, 0x20, 0x00, 0x00},
             {0x11, 0x0A, 0x15}},  /* H=0x48-0x37, A=0x41-0x37, L=0x4C-0x37 */
            /* 10,000,000 "JUP" */
            {{0x00, 0x00, 0x00, 0x10, 0x00, 0x00},
             {0x13, 0x1E, 0x19}},  /* J=0x4A-0x37, U=0x55-0x37, P=0x50-0x37 */
        };
        for (int i = 0; i < 5; i++) {
            memcpy(state->red_high_scores[i].points,
                   default_scores[i].points, 6);
            memcpy(state->red_high_scores[i].name,
                   default_scores[i].name, 3);
            memset(state->red_high_scores[i].id, 0, 4);
            memcpy(state->blue_high_scores[i].points,
                   default_scores[i].points, 6);
            memcpy(state->blue_high_scores[i].name,
                   default_scores[i].name, 3);
            memset(state->blue_high_scores[i].id, 0, 4);
        }
    }

    /* Pokedex flags start at zero. Catch'em mode and evolution mode set
     * seen/caught flags during gameplay. Save file loading restores them. */
}

/*
 * CopyInitialHighScores (home/save.asm 0x1e70):
 * Reset both red and blue high scores to factory defaults.
 */
void reset_high_scores_to_defaults(GameState *state) {
    static const struct {
        uint8_t points[6];
        uint8_t name[3];
    } default_scores[5] = {
        {{0x00, 0x00, 0x00, 0x50, 0x00, 0x00}, {0x17, 0x12, 0x17}},  /* 50M "NIN" */
        {{0x00, 0x00, 0x00, 0x40, 0x00, 0x00}, {0x0C, 0x1B, 0x0E}},  /* 40M "CRE" */
        {{0x00, 0x00, 0x00, 0x30, 0x00, 0x00}, {0x10, 0x0A, 0x16}},  /* 30M "GAM" */
        {{0x00, 0x00, 0x00, 0x20, 0x00, 0x00}, {0x11, 0x0A, 0x15}},  /* 20M "HAL" */
        {{0x00, 0x00, 0x00, 0x10, 0x00, 0x00}, {0x13, 0x1E, 0x19}},  /* 10M "JUP" */
    };
    for (int i = 0; i < 5; i++) {
        memcpy(state->red_high_scores[i].points, default_scores[i].points, 6);
        memcpy(state->red_high_scores[i].name, default_scores[i].name, 3);
        memset(state->red_high_scores[i].id, 0, 4);
        memcpy(state->blue_high_scores[i].points, default_scores[i].points, 6);
        memcpy(state->blue_high_scores[i].name, default_scores[i].name, 3);
        memset(state->blue_high_scores[i].id, 0, 4);
    }
}

void game_state_free(GameState *state) {
    if (state) {
        config_data_free(state->config);
        editor_free(state->editor_state);
        vram_free(state->vram_top);
        vram_free(state->vram_bottom);
        free(state->vwf_font_gfx);
        free(state->slot_force_field_data);
        free(state);
    }
}
