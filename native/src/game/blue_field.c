/*=============================================================================
 * blue_field.c — Blue Field game objects, collision handlers, and stage data.
 *
 * Translated from:
 *   engine/pinball_game/stage_init/init_blue_field.asm
 *   engine/pinball_game/ball_init/ball_init_blue_field.asm
 *   engine/pinball_game/ball_loss/ball_loss_blue_field.asm
 *   engine/pinball_game/object_collision/blue_stage_object_collision.asm
 *   engine/pinball_game/object_collision/blue_stage_resolve_collision.asm
 *   engine/pinball_game/load_stage_data/load_blue_field.asm
 *   data/collision/game_objects/blue_stage_game_object_collision.asm
 *===========================================================================*/
#include "blue_field.h"
#include "red_field.h"           /* shared: check_special_mode_collision, etc. */
#include "draw_red_field.h"      /* fill_bottom_message_buffer, enable_bottom_text, load_scrolling_text */
#include "score.h"
#include "constants.h"
#include "joypad.h"
#include "rng.h"
#include "billboard.h"
#include "ball_gfx.h"
#include "../audio/audio.h"
#include "../renderer/vram.h"
#include "../renderer/stage_assets.h"
#include "game/collision.h"
#include "game/timer.h"

#include "../renderer/tile_loader.h"  /* load_binary_file */

#include "game/config_data.h"

#include <string.h>
#include <stdio.h>

/* Forward declarations */
static void load_upgrade_triggers_graphics_blue(GameState *state);

/*=============================================================================
 * Force field physics data (BallPhysicsData_ec000, bank 0x3B)
 * 48 rows × 48 cols × 4 bytes = 9216 bytes used (file is 16384 bytes)
 *===========================================================================*/
static uint8_t *force_field_data = NULL;
static size_t force_field_data_size = 0;

static void ensure_force_field_data_loaded(GameState *state) {
    if (!force_field_data) {
        char path[260];
        snprintf(path, sizeof(path), "%s/data/collision/ball_physics_ec000.bin",
                 state->asset_base_path);
        force_field_data = load_binary_file(path, &force_field_data_size);
    }
}

/* Score constants now read from state->config->scores (see config/scores.json) */

/*=============================================================================
 * Collision data tables from blue_stage_game_object_collision.asm
 *
 * Format: {half_w, half_h, id1, x1, y1, id2, x2, y2, ..., 0xFF}
 *===========================================================================*/

/* Shellder: 3 objects (top field) */
static const uint8_t shellder_collision_data[] = {
    0x0E, 0x0E,
    0x03, 0x3A, 0x56,
    0x04, 0x4F, 0x45,
    0x05, 0x64, 0x56,
    0xFF
};
static const uint8_t shellder_collision_attrs[] = {
    0x00,
    0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65,
    0x98, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x99,
    0x6C, 0x6D, 0x6E, 0x6F,
    0xFF
};

/* Spinner: 1 object (top field) */
static const uint8_t spinner_collision_data[] = {
    0x08, 0x04,
    0x06, 0x92, 0x6A,
    0xFF
};

/* Board triggers: 4 objects (top field) */
static const uint8_t board_triggers_collision_data[] = {
    0x09, 0x09,
    0x07, 0x15, 0x43,
    0x08, 0x8B, 0x43,
    0x09, 0x10, 0x8C,
    0x0A, 0x8F, 0x8C,
    0xFF
};

/* Cloyster: 1 object (top field) */
static const uint8_t cloyster_collision_data[] = {
    0x06, 0x05,
    0x0B, 0x73, 0x78,
    0xFF
};

/* Slowpoke: 1 object (top field) */
static const uint8_t slowpoke_collision_data[] = {
    0x06, 0x05,
    0x0C, 0x2C, 0x78,
    0xFF
};

/* Pikachu savers: 2 objects (bottom lower) */
static const uint8_t pikachu_collision_data[] = {
    0x03, 0x05,
    0x0D, 0x0E, 0x7C,
    0x0E, 0x92, 0x7C,
    0xFF
};

/* Bumpers: 2 objects (bottom lower) */
static const uint8_t bumpers_collision_data[] = {
    0x06, 0x0B,
    0x01, 0x30, 0x66,
    0x02, 0x6F, 0x66,
    0xFF
};
static const uint8_t bumpers_collision_attrs[] = {
    0x00,
    0x34, 0x3F, 0x39, 0x3C, 0x38, 0x37, 0x3F, 0x33,
    0x32, 0x3E, 0x3D, 0x3B, 0x3A, 0x3E, 0x36, 0x35,
    0x31, 0x30,
    0xFF
};

/* Bonus multiplier railings: 2 objects (bottom upper) */
static const uint8_t bonus_multiplier_collision_data[] = {
    0x09, 0x08,
    0x0F, 0x2C, 0x20,
    0x10, 0x74, 0x20,
    0xFF
};
static const uint8_t bonus_multiplier_collision_attrs[] = {
    0x00,
    0x56, 0x59, 0x58, 0x57, 0x1C, 0x5A, 0x5B, 0x5C, 0x5D, 0x1D,
    0xFF
};

/* Psyduck/Poliwag: 2 objects (bottom upper) */
static const uint8_t psyduck_poliwag_collision_data[] = {
    0x08, 0x0C,
    0x11, 0x22, 0x3E,
    0x12, 0x7D, 0x3D,
    0xFF
};
static const uint8_t psyduck_poliwag_collision_attrs[] = {
    0x00,
    0x5E, 0x5F, 0x24, 0x25,
    0xFF
};

/* Ball upgrade triggers: 3 objects (top field) */
static const uint8_t upgrade_triggers_collision_data[] = {
    0x06, 0x05,
    0x13, 0x37, 0x34,
    0x14, 0x4F, 0x2F,
    0x15, 0x67, 0x35,
    0xFF
};

/* CAVE lights: 4 objects (bottom lower) */
static const uint8_t cave_lights_collision_data[] = {
    0x05, 0x03,
    0x16, 0x0E, 0x65,
    0x17, 0x1E, 0x65,
    0x18, 0x82, 0x65,
    0x19, 0x92, 0x65,
    0xFF
};

/* Slot cave: 1 object (bottom upper) */
static const uint8_t slot_collision_data[] = {
    0x04, 0x04,
    0x1A, 0x50, 0x16,
    0xFF
};

/* Wild pokemon: 1 object (bottom upper) */
static const uint8_t wild_mon_collision_data[] = {
    0x1A, 0x1A,
    0x1B, 0x50, 0x40,
    0xFF
};
static const uint8_t wild_mon_collision_attrs[] = {
    0x00,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xFF
};

/* Launch alley: 1 object (bottom lower) */
static const uint8_t launch_alley_collision_data[] = {
    0x08, 0x08,
    0x1C, 0xA8, 0x98,
    0xFF
};

/* Evolution trinket coordinates (top) */
static const uint8_t blue_top_evo_trinket_coords[] = {
    0x01, 0x44, 0x11,
    0x01, 0x23, 0x1B,
    0x01, 0x65, 0x1B,
    0x01, 0x0D, 0x2E,
    0x01, 0x7A, 0x2E,
    0x01, 0x05, 0x48,
    0x01, 0x44, 0x88,
    0x01, 0x83, 0x48,
    0x01, 0x02, 0x6E,
    0x01, 0x2E, 0x88,
    0x01, 0x59, 0x88,
    0x01, 0x85, 0x6E,
    0x00
};

/* Evolution trinket coordinates (bottom) */
static const uint8_t blue_bottom_evo_trinket_coords[] = {
    0x01, 0x33, 0x1B,
    0x01, 0x55, 0x1B,
    0x01, 0x29, 0x1F,
    0x01, 0x5F, 0x1F,
    0x01, 0x1D, 0x35,
    0x01, 0x6B, 0x35,
    0x00
};

/* Blue field initial maps for ChooseInitialMap */
static const uint8_t blue_stage_initial_maps[] = {
    MAP_VIRIDIAN_CITY, MAP_VIRIDIAN_FOREST, MAP_MT_MOON, MAP_CERULEAN_CITY,
    MAP_VERMILION_STREETS, MAP_ROCK_MOUNTAIN, MAP_CELADON_CITY
};

/* Bonus stage table for blue field */
static const uint8_t bonus_stages_blue[] = {
    STAGE_GENGAR_BONUS, STAGE_MEWTWO_BONUS, STAGE_MEOWTH_BONUS,
    STAGE_DIGLETT_BONUS, STAGE_SEEL_BONUS
};

/* Ball type progression/degradation for blue field */
static const uint8_t ball_type_progression_blue[] = {
    GREAT_BALL, GREAT_BALL, ULTRA_BALL, MASTER_BALL, MASTER_BALL, MASTER_BALL
};
static const uint8_t ball_type_degradation_blue[] = {
    POKE_BALL, POKE_BALL, POKE_BALL, GREAT_BALL, ULTRA_BALL, ULTRA_BALL
};

/* Bumper collision angle deltas */
static const int8_t bumper_angle_deltas_blue[] = { -8, 8 };

/* Slowpoke animation data: [duration, frame] pairs, 0x00 terminator */
static const uint8_t slowpoke_collision_anim_data[] = {
    0x08, 0x01,  0x06, 0x02,  0x06, 0x02,  0x08, 0x01,
    0x01, 0x00,  0x29, 0x00,  0x28, 0x01,  0x2A, 0x00,
    0x27, 0x01,  0x29, 0x00,  0x28, 0x01,  0x2B, 0x00,
    0x28, 0x01,  0x00
};

/* Cloyster animation data: same format */
static const uint8_t cloyster_collision_anim_data[] = {
    0x08, 0x01,  0x06, 0x02,  0x06, 0x02,  0x08, 0x01,
    0x01, 0x00,  0x29, 0x00,  0x28, 0x01,  0x2A, 0x00,
    0x27, 0x01,  0x29, 0x00,  0x28, 0x01,  0x2B, 0x00,
    0x28, 0x01,  0x00
};

/* Pikachu saver animation data */
static const uint8_t pikachu_saver_anim_data_blue[] = {
    0x0C, 0x02, 0x05, 0x03, 0x05, 0x02, 0x05, 0x04,
    0x05, 0x05, 0x05, 0x02, 0x06, 0x06, 0x06, 0x07,
    0x06, 0x08, 0x06, 0x02, 0x06, 0x05, 0x06, 0x08,
    0x06, 0x07, 0x06, 0x02, 0x06, 0x08, 0x06, 0x07,
    0x06, 0x02, 0x01, 0x00, 0x00
};

static const uint8_t pikachu_saver_anim2_data_blue[] = {
    0x0C, 0x02, 0x01, 0x00, 0x00
};

/* Spinner charging SFX table */
static const uint8_t spinner_charge_sfx_ids_blue[] = {
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x11
};

/* BCD powers of two for GetBCDForNextBonusMultiplier */
static const uint8_t powers_of_two_bcd[] = {
    0x01, 0x02, 0x04, 0x08, 0x16, 0x32, 0x64
};

/*=============================================================================
 * Forward declarations
 *===========================================================================*/
static void init_blue_field_collision_attributes(GameState *state);
static void get_bcd_for_next_bonus_multiplier_blue(GameState *state);
/* Collision checking helpers */
static void check_game_object_collision(GameState *state,
    const uint8_t *collision_data, const uint8_t *collision_attrs,
    uint8_t *which_var, uint8_t *which_id_var, bool has_attrs);
static void check_evolution_trinket_collision_blue(GameState *state);

/* Resolve handlers */
static void resolve_shellder_collision(GameState *state);
static void resolve_spinner_collision_blue(GameState *state);
static void update_spinner_blue(GameState *state);
static void resolve_ball_upgrade_triggers_blue(GameState *state);
static void update_ball_type_upgrade_counter_blue(GameState *state);
static void update_cave_lights_blinking_blue(GameState *state);
static void resolve_board_trigger_collision_blue(GameState *state);
static void resolve_pikachu_collision_blue(GameState *state);
static void resolve_slowpoke_collision(GameState *state);
static void resolve_cloyster_collision(GameState *state);
static void resolve_force_field_collision(GameState *state);
static void update_force_field_direction(GameState *state);
static void update_force_field_graphics(GameState *state);
static void resolve_psyduck_poliwag_collision(GameState *state);
static void open_slot_cave_blue(GameState *state);
static void update_blinking_pokeballs_blue(GameState *state);
static void update_map_move_counters_top_blue(GameState *state);
static void update_map_move_counters_bottom_blue(GameState *state);
static void resolve_wild_mon_collision_blue(GameState *state);
static void resolve_bumpers_collision_blue(GameState *state);
static void resolve_pinball_launch_collision_blue(GameState *state);
static void update_arrow_indicators_blue(GameState *state);
static void resolve_bonus_multiplier_collision_blue(GameState *state);
static void resolve_slot_collision_blue(GameState *state);
static void resolve_cave_light_collision_blue(GameState *state);
static void update_pokeballs_blue(GameState *state);
static void apply_slot_force_field_top_blue(GameState *state);
static void apply_slot_force_field_bottom_blue(GameState *state);
static void update_pinball_upgrade_blinking_blue(GameState *state);

/* Graphics helpers */
static void update_spinner_charge_graphics_blue(GameState *state);
static void load_psyduck_or_poliwag_graphics_blue(GameState *state, uint8_t id);
static void load_psyduck_or_poliwag_number_graphics_blue(GameState *state, uint8_t id);

/* Animation helpers */
static void init_animation(Animation *anim, const uint8_t *data);
static bool update_animation(Animation *anim, const uint8_t *data);

/*=============================================================================
 * InitBlueField (0x1c000)
 *===========================================================================*/
void init_blue_field(GameState *state) {
    if (state->loading_saved_game) return;

    /* Clear score */
    memset(state->score, 0, 6);
    state->num_party_mons = 0;
    state->extra_balls = 0;
    state->lost_ball = 0;
    state->ball_type = 0;
    state->ball_size = 0;
    state->previous_num_pokeballs = 0;
    state->num_pokeballs = 0;
    state->pokeball_blinking_counter = 0;
    state->disable_horizontal_scroll_for_ball_start = 0;
    state->flippers_disabled = 0;
    state->current_map = 0; /* PALLET_TOWN */

    state->cur_ball_life = 1;
    state->cur_bonus_multiplier = 1;
    /* #94: Explicitly initialize alley counts to match ASM.
     * ASM sets right_alley_count=2 and left_alley_count is 0 (from WRAM clear). */
    state->left_alley_count = 0;
    state->right_alley_count = 2;
    state->num_ball_lives = 3;
    state->wd610 = 3;

    /* Blue field starts with MEOWTH bonus order */
    state->next_bonus_stage = BONUS_STAGE_ORDER_MEOWTH;
    state->initial_next_bonus_stage = BONUS_STAGE_ORDER_MEOWTH;

    /* Indicator states: $80 at [0] and [3], $82 at [1] */
    state->indicator_states[0] = 0x80;
    state->indicator_states[3] = 0x80;
    state->indicator_states[1] = 0x82;

    /* #93: Initialize stage_collision_state and previous_field_structure_state.
     * ASM doesn't explicitly set these in InitBlueField because WRAM is zero
     * on fresh start. For C correctness when GameState is reused: */
    state->stage_collision_state = 0;
    state->previous_field_structure_state = 0;

    /* Blue-specific state */
    state->wd648 = 0;
    state->wd649 = 0;
    state->wd64a = 0;
    state->wd643 = 0;
    state->wd644 = 0;
    state->psyduck_state = 0;
    state->poliwag_state = 0;

    start_20_second_saver_timer(state);
    get_bcd_for_next_bonus_multiplier_blue(state);

    /* Play blue field music: bank 0x10, id 0x01 */
    PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
}

/*=============================================================================
 * InitBallBlueField (0x1c08d)
 *===========================================================================*/
static void init_ball_blue_field_normal(GameState *state) {
    /* Ball start position: X=$A700, Y=$9800 */
    state->ball_x_pos = 0xA700;
    state->ball_y_pos = 0x9800;
    state->enable_ball_gravity_and_tilt = 0;
    state->pinball_launched = 0;
    state->wd580 = 0;

    init_blue_field_collision_attributes(state);

    if (!state->lost_ball) return;

    /* Per-ball resets after losing a ball */
    state->lost_ball = 0;
    state->spinner_velocity = 0;
    state->pikachu_saver_slot_reward_active = 0;
    state->pikachu_saver_sound_cooldown = 0;
    state->pikachu_saver_charge = 0;
    memset(state->cave_light_states, 0, 4);
    state->left_map_move_counter = 0;
    state->right_map_move_counter = 0;
    memset(state->ball_upgrade_trigger_states, 0, 3);
    state->ball_type = 0;
    state->wd611 = 0;
    state->wd612 = 0;
    state->num_pokemon_caught_in_ball_bonus = 0;
    state->num_pokemon_evolved_in_ball_bonus = 0;
    state->num_bellsprout_entries = 0;
    state->num_dugtrio_triples = 0;
    state->num_cave_completions = 0;
    state->num_slowpoke_entries = 0;
    state->num_cloyster_entries = 0;
    state->num_poliwag_triples = 0;
    state->num_psyduck_triples = 0;
    state->num_spinner_turns = 0;
    state->num_pikachu_saves = 0;
    state->show_bonus_multiplier_bottom_message = 0;
    state->cur_bonus_multiplier = 1;
    state->left_diglett_anim_controller = 1;
    state->right_diglett_anim_controller = 1;
    state->wd610 = 3;

    get_bcd_for_next_bonus_multiplier_blue(state);

    /* Restart blue field music */
    PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
}

static void start_ball_after_bonus_stage_blue(GameState *state) {
    /* StartBallAfterBonusStageBlueField (0x1c129)
     * Reset ball_size to 0 — bonus ball_loss sets it to 2 (super-mini)
     * but InitializeCurrentStage (which normally resets it) is skipped. */
    state->ball_size = 0;
    state->ball_x_pos = 0x5000;
    state->ball_y_pos = 0x1600;
    state->ball_x_velocity = 0;
    state->ball_y_velocity = 0;
    state->returning_from_bonus_stage = 0;
    state->scx = 0;
    state->flippers_disabled = 0;
    state->ball_type = state->ball_type_backup;

    PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
}

/* Public entry point called from pinball.c */
void init_ball_blue_field(GameState *state) {
    if (state->returning_from_bonus_stage) {
        start_ball_after_bonus_stage_blue(state);
    } else {
        init_ball_blue_field_normal(state);
    }
}

/*=============================================================================
 * HandleBallLossBlueField (0xde4f)
 *===========================================================================*/
static void handle_ball_loss_blue_field(GameState *state) {
    /* Check ball saver timer */
    if (state->ball_saver_timer_frames || state->ball_saver_timer_seconds) {
        /* Ball is saved */
        if (!(state->num_times_ball_saved_text_will_display & 0x80)) {
            state->num_times_ball_saved_text_will_display--;
            /* Show "BALL SAVED" text */
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 2, "BALL SAVED", "BALL SAVED");
            if (state->num_times_ball_saved_text_will_display == 0) {
                state->ball_saver_timer_frames = 1;
                state->ball_saver_timer_seconds = 1;
            }
        }
        PLAY_SFX(state, "ball_launch_spring", 0x15, 0x02);
        return;
    }

    /* Ball is lost */
    audio_stop_all(state->audio);

    /* 30-frame delay before loss SFX */
    state->ball_loss_sfx_delay = 30;

    start_20_second_saver_timer(state);
    state->lost_ball = 1;
    state->pinball_launched = 0;
    state->wd4df = 0;

    conclude_special_mode_blue_field(state);

    if (state->extra_balls > 0) {
        state->extra_balls--;
        state->extra_ball_state = 1;
        /* Show "END OF BALL BONUS" text */
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        load_scrolling_text(state, 2, "END OF BALL BONUS", "END OF BALL BONUS");
        return;
    }

    if (state->cur_ball_life < state->num_ball_lives) {
        state->cur_ball_life++;
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        load_scrolling_text(state, 2, "END OF BALL BONUS", "END OF BALL BONUS");
        return;
    }

    /* Game over */
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    load_scrolling_text(state, 2, "END OF BALL BONUS", "END OF BALL BONUS");
    state->game_over[0] = 1;
}

/*=============================================================================
 * ConcludeSpecialMode_BlueField (0xded6)
 *===========================================================================*/
void conclude_special_mode_blue_field(GameState *state) {
    if (!state->in_special_mode) return;

    if (state->special_mode == 0) {
        /* ConcludeCatchEmMode */
        state->slot_is_open = 0;
        state->in_special_mode = 0;
        state->wild_mon_is_hittable = 0;
        state->capturing_mon = 0;
        state->wd5c6 = 0;
        state->number_of_catch_mode_tiles_flipped = 0;
        state->num_mon_hits = 0;
        memset(state->mon_animated_collision_mask, 0, 0x80);
        stop_timer(state);
        /* RestoreBallSaverAfterCatchEmMode (ASM 0x10196) */
        state->ball_saver_timer_frames = state->ball_saver_timer_frames_backup;
        state->ball_saver_timer_seconds = state->ball_saver_timer_seconds_backup;
        state->num_times_ball_saved_text_will_display = state->num_times_ball_saved_text_will_display_backup;
        {
            uint8_t sec = state->ball_saver_timer_seconds;
            if (sec < 2) state->ball_saver_flash_rate = 0;
            else if (sec < 6) state->ball_saver_flash_rate = 4;
            else if (sec < 11) state->ball_saver_flash_rate = 0x10;
            else state->ball_saver_flash_rate = 0xFF;
            /* D-02: ASM sets wBallSaverIconOn based on remaining seconds */
            state->ball_saver_icon_on = (sec > 0) ? 1 : 0;
        }

        /* Func_109fc (0x109fc): stage-specific tile cleanup after catch'em mode */
        memset(state->indicator_states, 0, 19);
        state->frames_until_slot_cave_opens = 0x1E;
        /* SetLeftAndRightAlleyArrowIndicatorStates_BlueField */
        {
            uint8_t left = state->left_alley_count;
            if (left >= 3) state->indicator_states[2] = 0x80;
            state->indicator_states[0] = (left != 3) ? (left | 0x80) : left;
            uint8_t right = state->right_alley_count;
            if (right >= 2) state->indicator_states[3] = 0x80;
            state->indicator_states[1] = (right != 3) ? (right | 0x80) : right;
        }
        if (state->current_stage & 1) {
            /* Bottom stage: restore tile data */
            clear_all_blue_indicators(state);
            load_billboard_tilemap(state);
            load_map_billboard_tile_data(state);

            /* StageSharedBonusSlotGlowGfx+$60 → vTilesOB tile $20, $E0 bytes.
             * Catch mode's billboard/pokemon sprites overwrite slot glow
             * tiles at $8200+. Reload them from the PNG. */
            {
                char glow_path[260];
                snprintf(glow_path, sizeof(glow_path), "%s/gfx/stage/shared/bonus_slot_glow.png",
                         state->asset_base_path);
                size_t glow_size = 0;
                uint8_t *glow_data = tiles_from_png(glow_path, &glow_size);
                if (glow_data && glow_size >= 0x60 + 0xE0) {
                    vram_write(state->vram, 0, 0x8200, glow_data + 0x60, 0xE0);
                }
                free(glow_data);
            }

            /* BlankSaverSpaceTileData: restore tiles at $8AE0, $8B00, $8B20 */
            {
                char path[260];
                snprintf(path, sizeof(path), "%s/gfx/stage/blue_bottom/blue_bottom_base_gameboycolor.png",
                         state->asset_base_path);
                for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
                size_t data_size = 0;
                uint8_t *tile_data = tiles_from_png(path, &data_size);
                if (tile_data) {
                    if (data_size >= 0x300) vram_write(state->vram, 0, 0x8AE0, tile_data + 0x2E0, 0x20);
                    if (data_size >= 0x320) vram_write(state->vram, 0, 0x8B00, tile_data + 0x300, 0x20);
                    if (data_size >= 0x340) vram_write(state->vram, 0, 0x8B20, tile_data + 0x320, 0x20);
                    free(tile_data);
                }
            }

            /* CaughtPokeballTileData: load caught pokeball gfx at $8AE0 */
            {
                char path[260];
                snprintf(path, sizeof(path), "%s/gfx/stage/caught_pokeball.png",
                         state->asset_base_path);
                for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
                size_t data_size = 0;
                uint8_t *tile_data = tiles_from_png(path, &data_size);
                if (tile_data) {
                    uint16_t copy = (data_size < 0x20) ? (uint16_t)data_size : 0x20;
                    vram_write(state->vram, 0, 0x8AE0, tile_data, copy);
                    free(tile_data);
                }
            }

            load_pokeball_indicator_graphics(state);
        }

        return;
    }

    if (state->special_mode == SPECIAL_MODE_EVOLUTION) {
        /* M7: Full ConcludeEvolutionMode_BlueField (evolution_mode.asm:833-862) */
        state->slot_is_open = 0;
        state->in_special_mode = 0;
        state->evolution_objects_disabled = 0;
        state->wd643 = 0;
        /* ResetIndicatorStates */
        memset(state->indicator_states, 0, 19);
        /* OpenSlotCave */
        state->frames_until_slot_cave_opens = 0x1E;
        /* SetLeftAndRightAlleyArrowIndicatorStates_BlueField (0x1f2ff) */
        {
            uint8_t left = state->left_alley_count;
            if (left >= 3)
                state->indicator_states[2] = 0x80;
            if (left != 3)
                state->indicator_states[0] = left | 0x80;
            else
                state->indicator_states[0] = left;
            uint8_t right = state->right_alley_count;
            if (right >= 2)
                state->indicator_states[3] = 0x80;
            if (right != 3)
                state->indicator_states[1] = right | 0x80;
            else
                state->indicator_states[1] = right;
        }
        if (state->current_stage & 1) {
            /* Bottom stage: reload graphics */
            load_slot_cave_cover_graphics_blue(state);
            load_map_billboard_tile_data(state);
            /* StageSharedBonusSlotGlowGfx+$60 → vTilesOB tile $20, $E0 bytes.
             * Evolution mode's sprites overwrite slot glow tiles at $8200+. */
            {
                char glow_path[260];
                snprintf(glow_path, sizeof(glow_path), "%s/gfx/stage/shared/bonus_slot_glow.png",
                         state->asset_base_path);
                size_t glow_size = 0;
                uint8_t *glow_data = tiles_from_png(glow_path, &glow_size);
                if (glow_data && glow_size >= 0x60 + 0xE0) {
                    vram_write(state->vram, 0, 0x8200, glow_data + 0x60, 0xE0);
                }
                free(glow_data);
            }
        }
        /* RestoreBallSaverAfterCatchEmMode (ASM 0x10196) */
        state->ball_saver_timer_frames = state->ball_saver_timer_frames_backup;
        state->ball_saver_timer_seconds = state->ball_saver_timer_seconds_backup;
        state->num_times_ball_saved_text_will_display = state->num_times_ball_saved_text_will_display_backup;
        {
            uint8_t sec = state->ball_saver_timer_seconds;
            if (sec < 2) state->ball_saver_flash_rate = 0;
            else if (sec < 6) state->ball_saver_flash_rate = 4;
            else if (sec < 11) state->ball_saver_flash_rate = 0x10;
            else state->ball_saver_flash_rate = 0xFF;
            /* D-02: ASM sets wBallSaverIconOn based on remaining seconds */
            state->ball_saver_icon_on = (sec > 0) ? 1 : 0;
        }
        return;
    }

    /* M8: ConcludeMapMoveMode (map_move.asm 0x3022b → Func_313c3 for blue) */
    state->slot_is_open = 0;
    state->bottom_text_enabled = 0;
    fill_bottom_message_buffer_with_black_tile(state);
    state->in_special_mode = 0;
    state->special_mode = 0;
    stop_timer(state);
    /* Func_313c3: ResetIndicatorStates + OpenSlotCave + SetAlleyArrows */
    memset(state->indicator_states, 0, 19);
    state->frames_until_slot_cave_opens = 0x1E;
    {
        uint8_t left = state->left_alley_count;
        if (left >= 3)
            state->indicator_states[2] = 0x80;
        if (left != 3)
            state->indicator_states[0] = left | 0x80;
        else
            state->indicator_states[0] = left;
        uint8_t right = state->right_alley_count;
        if (right >= 2)
            state->indicator_states[3] = 0x80;
        if (right != 3)
            state->indicator_states[1] = right | 0x80;
        else
            state->indicator_states[1] = right;
    }
    state->wd644 = 0;
    if (state->current_stage & 1) {
        load_slot_cave_cover_graphics_blue(state);
        load_map_billboard_tile_data(state);
    }
}

/*=============================================================================
 * InitBlueFieldCollisionAttributes (0x1c7c7)
 *===========================================================================*/
static void init_blue_field_collision_attributes(GameState *state) {
    state->stage_collision_state = 0;
    load_stage_collision_attributes(state);
}

/*=============================================================================
 * GetBCDForNextBonusMultiplier_BlueField (0x1d65f)
 *===========================================================================*/
static void get_bcd_for_next_bonus_multiplier_blue(GameState *state) {
    uint8_t val = state->cur_bonus_multiplier + 1;
    if (val > MAX_BONUS_MULTIPLIER) val = MAX_BONUS_MULTIPLIER;

    /* BCD conversion using DAA-like logic with powers of two table */
    uint8_t bcd = 0;
    for (int i = 0; i < 7; i++) {
        if (val & 1) {
            /* BCD add */
            uint8_t a = bcd + powers_of_two_bcd[i];
            /* DAA: adjust each nibble */
            if ((a & 0x0F) > 9) a += 6;
            if ((a & 0xF0) > 0x90) a += 0x60;
            bcd = a;
        }
        val >>= 1;
    }
    state->bonus_multiplier_tens_digit = (bcd >> 4) & 0x0F;
    state->bonus_multiplier_ones_digit = bcd & 0x0F;
}

/*=============================================================================
 * OBJECT COLLISION CHECKING
 *
 * check_game_object_collision: generic bounding-box check matching ASM
 * HandleGameObjectCollision (0x27da).
 *===========================================================================*/
static void check_game_object_collision(GameState *state,
    const uint8_t *collision_data, const uint8_t *collision_attrs,
    uint8_t *which_var, uint8_t *which_id_var, bool has_attrs)
{
    /* #89: ASM HandleGameObjectCollision (0x2775) guards:
     * 1. wTriggeredGameObject != $FF → skip (only ONE object triggers per frame)
     * 2. wPreviousTriggeredGameObject == current → skip (no re-trigger) */
    if (state->triggered_game_object != 0xFF)
        return;

    /* Handler busy? (bit 7 of flag) */
    if (*which_var & 0x80)
        return;

    *which_var = 0;

    /* Attribute-gated check: ASM IsCollisionInList (0x27da) checks
     * wIsBallColliding first, then verifies cur_collision_attribute
     * is in the attr list. Only triggers when ball is on a matching tile. */
    if (has_attrs && collision_attrs) {
        if (!state->is_ball_colliding)
            return;
        uint8_t attr = state->cur_collision_attribute;
        const uint8_t *ap = collision_attrs;
        if (*ap == 0x00) ap++; /* skip flat list marker */
        bool found = false;
        while (*ap != 0xFF) {
            if (*ap == attr) { found = true; break; }
            ap++;
        }
        if (!found) return;
    }

    uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t ball_y = (uint8_t)(state->ball_y_pos >> 8);
    uint8_t half_w = collision_data[0];
    uint8_t half_h = collision_data[1];
    const uint8_t *p = collision_data + 2;

    uint8_t iter_index = 1;  /* ASM: 1-based iteration index (HandleGameObjectCollision 0x2775) */
    while (*p != 0xFF) {
        uint8_t id = p[0];
        uint8_t ox = p[1];
        uint8_t oy = p[2];
        p += 3;

        int dx = (int)ball_x - (int)ox;
        int dy = (int)ball_y - (int)oy;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;

        if (dx <= half_w && dy <= half_h) {
            state->triggered_game_object = id;

            /* #89: Previous trigger guard — prevent re-trigger of same object */
            if (state->previous_triggered_game_object == id)
                return;

            /* ASM stores 1-based iteration index in which_var, raw ID in which_id_var */
            *which_var = iter_index;
            if (which_id_var) *which_id_var = id;
            return;
        }
        iter_index++;
    }
}

/* Check evolution trinket point collision (from CheckBlueStageEvolutionTrinketCollision 0x1c5eb) */
static void check_evolution_trinket_collision_blue(GameState *state) {
    state->collided_point_index = 0;
    if (!state->evolution_objects_disabled) return;

    uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t ball_y = (uint8_t)(state->ball_y_pos >> 8);

    const uint8_t *coords;
    if (!(state->current_stage & 1)) {
        coords = blue_top_evo_trinket_coords;
    } else {
        coords = blue_bottom_evo_trinket_coords;
    }

    uint8_t idx = 1;
    while (coords[0]) {
        uint8_t cx = coords[1];
        uint8_t cy = coords[2];
        int dx = (int)ball_x - (int)cx;
        int dy = (int)ball_y - (int)cy;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx <= 8 && dy <= 8) {
            state->collided_point_index = idx;
            return;
        }
        coords += 3;
        idx++;
    }
}

/*=============================================================================
 * Config-Aware Object Group Lookup
 *===========================================================================*/
static const TableObjectGroup *find_config_group(const TableConfig *table, const char *name) {
    for (uint8_t i = 0; i < table->num_groups; i++) {
        if (strcmp(table->groups[i].name, name) == 0)
            return &table->groups[i];
    }
    return NULL;
}

/*
 * Config-aware collision check wrapper for Blue Field's packed data format.
 * Builds packed data from config group if available, else uses defaults.
 */
static void check_object_group_blue(GameState *state,
    const TableConfig *table, const char *group_name,
    const uint8_t *default_data, const uint8_t *default_attrs,
    uint8_t *which_var, uint8_t *which_id_var, bool default_has_attrs)
{
    const TableObjectGroup *grp = find_config_group(table, group_name);
    if (grp && grp->num_objects > 0) {
        /* Build packed data: [half_w, half_h, id, x, y, ..., 0xFF] */
        uint8_t packed[2 + CONFIG_MAX_OBJECTS_PER_GROUP * 3 + 1];
        packed[0] = grp->x_thresh;
        packed[1] = grp->y_thresh;
        int pos = 2;
        for (int i = 0; i < grp->num_objects; i++) {
            packed[pos++] = grp->objects[i].id;
            packed[pos++] = grp->objects[i].x;
            packed[pos++] = grp->objects[i].y;
        }
        packed[pos] = 0xFF;
        check_game_object_collision(state, packed,
            grp->attribute_gated ? grp->collision_attrs : NULL,
            which_var, which_id_var, grp->attribute_gated);
    } else {
        check_game_object_collision(state, default_data, default_attrs,
            which_var, which_id_var, default_has_attrs);
    }
}

/*=============================================================================
 * CheckBlueStageTopGameObjectCollisions (0x1c520)
 *===========================================================================*/
static void check_blue_stage_top_collisions(GameState *state) {
    const TableConfig *t = &state->config->blue_field_top;
    check_object_group_blue(state, t, "shellder", shellder_collision_data, shellder_collision_attrs,
        &state->which_shellder, &state->which_shellder_id, true);
    check_object_group_blue(state, t, "spinner", spinner_collision_data, NULL,
        &state->spinner_collision, NULL, false);
    check_object_group_blue(state, t, "board_triggers", board_triggers_collision_data, NULL,
        &state->which_board_trigger, &state->which_board_trigger_id, false);
    check_object_group_blue(state, t, "slowpoke", slowpoke_collision_data, NULL,
        &state->slowpoke_collision, NULL, false);
    check_object_group_blue(state, t, "cloyster", cloyster_collision_data, NULL,
        &state->cloyster_collision, NULL, false);
    check_object_group_blue(state, t, "upgrade_triggers", upgrade_triggers_collision_data, NULL,
        &state->which_pinball_upgrade_trigger, &state->which_pinball_upgrade_trigger_id, false);
    check_evolution_trinket_collision_blue(state);
}

/*=============================================================================
 * CheckBlueStageBottomGameObjectCollisions (0x1c536)
 *===========================================================================*/
static void check_blue_stage_bottom_collisions(GameState *state) {
    const TableConfig *t = &state->config->blue_field_bottom;
    uint8_t ball_y = (uint8_t)(state->ball_y_pos >> 8);
    if (ball_y < 0x56) {
        /* Upper half of bottom screen */
        check_object_group_blue(state, t, "wild_mon", wild_mon_collision_data, wild_mon_collision_attrs,
            &state->wild_mon_collision, NULL, true);
        check_object_group_blue(state, t, "psyduck_poliwag", psyduck_poliwag_collision_data, psyduck_poliwag_collision_attrs,
            &state->which_psyduck_poliwag, &state->which_psyduck_poliwag_id, true);
        check_object_group_blue(state, t, "bonus_multipliers", bonus_multiplier_collision_data, bonus_multiplier_collision_attrs,
            &state->which_bonus_multiplier_railing, &state->which_bonus_multiplier_railing_id, true);
        check_object_group_blue(state, t, "slot", slot_collision_data, NULL,
            &state->slot_collision, NULL, false);
        check_evolution_trinket_collision_blue(state);
    } else {
        /* Lower half of bottom screen */
        check_object_group_blue(state, t, "bumpers", bumpers_collision_data, bumpers_collision_attrs,
            &state->which_bumper, &state->which_bumper_id, true);
        check_object_group_blue(state, t, "pikachu", pikachu_collision_data, NULL,
            &state->which_pikachu, &state->which_pikachu_id, false);
        check_object_group_blue(state, t, "cave_lights", cave_lights_collision_data, NULL,
            &state->which_cave_light, &state->which_cave_light_id, false);
        check_object_group_blue(state, t, "launch_alley", launch_alley_collision_data, NULL,
            &state->pinball_launch_collision, NULL, false);
    }
}

void check_blue_field_object_collisions(GameState *state) {
    /* #89: Per-frame setup matching ASM HandleGameObjectCollision (0x2726).
     * Save current triggered as previous, reset triggered to 0xFF. */
    state->previous_triggered_game_object = state->triggered_game_object;
    state->triggered_game_object = 0xFF;

    if (state->current_stage == STAGE_BLUE_FIELD_TOP) {
        check_blue_stage_top_collisions(state);
    } else {
        check_blue_stage_bottom_collisions(state);
    }
}

/*=============================================================================
 * Animation helpers (matching ASM InitAnimation / UpdateAnimation)
 *===========================================================================*/
static void init_animation(Animation *anim, const uint8_t *data) {
    anim->frame_counter = data[0];
    anim->frame = data[1];
    anim->index = 0;
}

static bool update_animation(Animation *anim, const uint8_t *data) {
    if (anim->frame_counter == 0) return false;
    anim->frame_counter--;
    if (anim->frame_counter > 0) return false;

    /* Move to next frame */
    anim->index++;
    const uint8_t *entry = &data[anim->index * 2];
    if (entry[0] == 0x00) {
        /* End of animation */
        anim->frame_counter = 0;
        return true;
    }
    anim->frame_counter = entry[0];
    anim->frame = entry[1];
    return true;
}

/*=============================================================================
 * RESOLVE HANDLERS — ResolveBlueFieldTopGameObjectCollisions (0x1c715)
 *===========================================================================*/
void resolve_blue_field_object_collisions(GameState *state) {
    if (state->current_stage == STAGE_BLUE_FIELD_TOP) {
        /* ResolveBlueFieldTopGameObjectCollisions */
        resolve_shellder_collision(state);
        resolve_spinner_collision_blue(state);
        resolve_ball_upgrade_triggers_blue(state);
        update_ball_type_upgrade_counter_blue(state);
        update_cave_lights_blinking_blue(state);
        resolve_board_trigger_collision_blue(state);
        resolve_pikachu_collision_blue(state);
        resolve_slowpoke_collision(state);
        resolve_cloyster_collision(state);
        apply_slot_force_field_top_blue(state);
        resolve_psyduck_poliwag_collision(state);
        resolve_force_field_collision(state);
        open_slot_cave_blue(state);
        update_force_field_direction(state);
        update_force_field_graphics(state);
        update_ball_saver(state);
        update_blinking_pokeballs_blue(state);
        update_map_move_counters_top_blue(state);
        show_extra_ball_message(state);
        check_special_mode_collision(state, SPECIAL_COLLISION_NOTHING);
    } else {
        /* ResolveBlueFieldBottomGameObjectCollisions */
        resolve_wild_mon_collision_blue(state);
        resolve_bumpers_collision_blue(state);
        resolve_psyduck_poliwag_collision(state);
        update_spinner_blue(state);
        /* ASM: UpdatePinballUpgradeTriggers calls blinking + conditional graphics.
         * But upgrade trigger bg_map positions (0x86/0x69/0x8C) are top-stage tiles;
         * our C code reloads the full bg_map per stage, so those positions contain
         * billboard content on the bottom stage. State-only here, graphics via
         * resolve handler on top stage + load_stage_data_blue_field_top. */
        update_pinball_upgrade_blinking_blue(state);
        update_ball_type_upgrade_counter_blue(state);
        resolve_cave_light_collision_blue(state);
        resolve_pinball_launch_collision_blue(state);
        resolve_pikachu_collision_blue(state);
        update_arrow_indicators_blue(state);
        resolve_bonus_multiplier_collision_blue(state);
        resolve_slot_collision_blue(state);
        open_slot_cave_blue(state);
        apply_slot_force_field_bottom_blue(state);
        update_force_field_direction(state);
        update_again_text(state);
        update_ball_saver(state);
        update_pokeballs_blue(state);
        update_map_move_counters_bottom_blue(state);
        show_extra_ball_message(state);
        check_special_mode_collision(state, SPECIAL_COLLISION_NOTHING);
    }
}

/*=============================================================================
 * ResolveBlueStagePinballLaunchCollision (0x1c7d7)
 *===========================================================================*/
static void resolve_pinball_launch_collision_blue(GameState *state) {
    if (!state->pinball_launch_collision) return;
    state->pinball_launch_collision = 0;

    if (state->pinball_launched) {
        /* Apply launch velocity */
        state->right_alley_trigger = 0;
        state->left_alley_trigger = 0;
        state->secondary_left_alley_trigger[0] = 0;
        state->ball_x_velocity = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
        /* Y velocity = 0xFA71 (upward) */
        state->ball_y_velocity = (int16_t)0xFA71;
        state->enable_ball_gravity_and_tilt = 1;
        PLAY_SFX(state, "pikachu_charge", 0x00, 0x0A);
    }

    state->previous_triggered_game_object = 0xFF;
    state->triggered_game_object = 0xFF;

    if (state->pinball_launched) return;

    if (!state->chose_initial_map) {
        /* Non-blocking map cycling state machine (matching ASM ChooseInitialMap).
         * map_cycling_frames counts down; when 0, cycle to next map on billboard.
         * Ball start key finalizes selection. */
        if (state->map_cycling_frames == 0) {
            /* Timer expired or first entry: show next map */
            load_grey_billboard_palette(state);

            /* Advance index (ASM increments THEN wraps at 7) */
            uint8_t idx = state->initial_map_selection_index + 1;
            if (idx >= 7) idx = 0;
            state->initial_map_selection_index = idx;
            state->current_map = blue_stage_initial_maps[idx];

            /* Play cycling SFX and load billboard picture */
            PLAY_SFX(state, "pikachu_full_charge", 0x00, 0x48);
            load_billboard_picture(state,
                (uint8_t)(BILLBOARD_PALLET_TOWN_PIC + state->current_map));
            state->map_cycling_frames = 32;
            return;
        }

        /* Check ball start key to finalize selection */
        if (joypad_is_key_pressed(state, &state->key_config_ball_start)) {
            load_map_billboard_tile_data(state);
            /* M6: ASM calls LoadScrollingMapNameText with StartFromMapText (0x1c8af) */
            load_scrolling_map_name_text(state, 0);
            state->visited_maps[0] = state->current_map;
            state->num_map_moves = 0;
            state->chose_initial_map = 1;
            state->pinball_launched = 1;
            return;
        } else {
            state->map_cycling_frames--;
            return;
        }
    }

    /* .checkPressedKeysToLaunchBall: wait for ball start key to launch */
    if (joypad_is_key_pressed(state, &state->key_config_ball_start)) {
        state->pinball_launched = 1;
    }
}

/* ChooseInitialMap_BlueField (0x1c839) — inlined into resolve_pinball_launch_collision_blue
 * as a non-blocking state machine using map_cycling_frames countdown. */

/*=============================================================================
 * ResolveShellderCollision (0x1c9c1)
 *===========================================================================*/
static void resolve_shellder_collision(GameState *state) {
    if (state->which_shellder) {
        state->which_shellder = 0;

        /* Apply shellder collision force */
        state->rumble_pattern = 0xFF;
        state->rumble_duration = 3;
        state->flipper_y_force = 0x0200;
        state->flipper_collision = 0x80;
        PLAY_SFX(state, "spinner", 0x00, 0x0E);

        /* Force field handling */
        if (!state->blue_stage_force_field_flipped_down) {
            state->blue_stage_force_field_flipped_down = 1;
            if (state->blue_stage_force_field_direction == 0) { /* up */
                state->blue_stage_force_field_direction = 2; /* down */
                state->blue_stage_force_field_gfx_needs_loading = 1;
                state->blue_field_force_field_frame_counter = 3;
                state->blue_field_force_field_seconds_counter = 3;
            }
        }

        state->shellder_hit_anim_duration = 0x10;
        state->which_animated_shellder = state->which_shellder_id - 3;

        check_special_mode_collision(state, SPECIAL_COLLISION_SHELLDER);
        add_score_with_multiplier(state, state->config->scores.score_500);
        return;
    }

    /* No new collision — update animation timer */
    if (state->shellder_hit_anim_duration > 0) {
        state->shellder_hit_anim_duration--;
        if (state->shellder_hit_anim_duration == 0) {
            state->which_animated_shellder = 0xFF;
        }
    }
}

/*=============================================================================
 * UpdateSpinnerChargeGraphics_BlueField (0x1cb43)
 * Loads the correct bg_map tiles showing the lightning bolt icon for the
 * spinner's current charge. GBC data from TileDataPointers_1cd10 / spinner.asm.
 * Each charge level writes 4 tiles: 2 at bg_map $143 and 2 at bg_map $163.
 *===========================================================================*/
static void update_spinner_charge_graphics_blue(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    uint8_t charge = state->pikachu_saver_charge;
    if (charge > 15) charge = 15;

    /* From data/queued_tiledata/blue_field/spinner.asm TileDataPointers_1cd10:
     * Charges 0-7:  top row = $48,$49 (constant); bottom row varies
     * Charge 8:     top row = $48,$49; bottom row = $58,$59
     * Charges 9-14: top row varies; bottom row = $58,$59
     * Charge 15:    top row = $68,$69; bottom row = $6A,$6B */
    static const uint8_t spinner_charge_tiles[16][4] = {
        /* {top_left, top_right, bottom_left, bottom_right} */
        /* charge 0  */ {0x48, 0x49, 0x4A, 0x4B},
        /* charge 1  */ {0x48, 0x49, 0x4C, 0x4D},
        /* charge 2  */ {0x48, 0x49, 0x4E, 0x4F},
        /* charge 3  */ {0x48, 0x49, 0x50, 0x51},
        /* charge 4  */ {0x48, 0x49, 0x52, 0x53},
        /* charge 5  */ {0x48, 0x49, 0x54, 0x55},
        /* charge 6  */ {0x48, 0x49, 0x56, 0x57},
        /* charge 7  */ {0x48, 0x49, 0x58, 0x59},
        /* charge 8  */ {0x48, 0x49, 0x58, 0x59},
        /* charge 9  */ {0x5C, 0x5D, 0x58, 0x59},
        /* charge 10 */ {0x5E, 0x5F, 0x58, 0x59},
        /* charge 11 */ {0x60, 0x61, 0x58, 0x59},
        /* charge 12 */ {0x62, 0x63, 0x58, 0x59},
        /* charge 13 */ {0x64, 0x65, 0x58, 0x59},
        /* charge 14 */ {0x66, 0x67, 0x58, 0x59},
        /* charge 15 */ {0x68, 0x69, 0x6A, 0x6B},
    };

    bg[0x143] = spinner_charge_tiles[charge][0];
    bg[0x144] = spinner_charge_tiles[charge][1];
    bg[0x163] = spinner_charge_tiles[charge][2];
    bg[0x164] = spinner_charge_tiles[charge][3];
}

/*=============================================================================
 * ResolveBlueStageSpinnerCollision + UpdateBlueStageSpinner (0x1ca5f)
 *===========================================================================*/
static void resolve_spinner_collision_blue(GameState *state) {
    if (state->spinner_collision) {
        state->spinner_collision = 0;
        state->spinner_velocity = state->ball_y_velocity;
        check_special_mode_collision(state, SPECIAL_COLLISION_SPINNER);
    }
    update_spinner_blue(state);
}

static void update_spinner_blue(GameState *state) {
    if (state->spinner_velocity == 0) return;

    int16_t vel = state->spinner_velocity;
    if (vel < 0) {
        vel += 7;
        if (vel > 0) vel = 0;
    } else {
        vel -= 7;
        if (vel < 0) vel = 0;
    }
    state->spinner_velocity = vel;

    /* Update spinner state */
    int16_t s = (int16_t)((state->spinner_state[0]) | ((int16_t)state->spinner_state[1] << 8));
    s += vel;
    int8_t hi = (int8_t)(s >> 8);
    uint8_t completed = 0;

    if (hi < 0) {
        hi += 0x18;
        completed = 1;
    } else if (hi >= 0x18) {
        hi -= 0x18;
        completed = 1;
    }
    state->spinner_state[1] = (uint8_t)hi;
    state->spinner_state[0] = s & 0xFF;

    if (!completed) return;

    /* Completed a full rotation */
    add_score_with_multiplier(state, state->config->scores.score_10);
    increment_max100(&state->num_spinner_turns);

    if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
        /* Play charging SFX */
        if (!state->pikachu_saver_sound_cooldown) {
            audio_play_sfx(state->audio, 0x00, spinner_charge_sfx_ids_blue[state->pikachu_saver_charge]);
        }
        return;
    }

    state->pikachu_saver_charge++;
    /* Play charging SFX */
    if (!state->pikachu_saver_sound_cooldown) {
        audio_play_sfx(state->audio, 0x00, spinner_charge_sfx_ids_blue[state->pikachu_saver_charge]);
    }

    if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
        state->pikachu_saver_sound_cooldown = 0x64;
    }

    /* ASM: only update spinner charge graphics on top stage (bit 0 == 0) */
    if (!(state->current_stage & 1)) {
        update_spinner_charge_graphics_blue(state);
    }
}

/*=============================================================================
 * ResolveWildMonCollision_BlueField (0x1ca4a)
 *===========================================================================*/
static void resolve_wild_mon_collision_blue(GameState *state) {
    if (!state->wild_mon_collision) return;
    state->wild_mon_collision = 0;
    state->ball_hit_wild_mon = 1;
    PLAY_SFX(state, "bumper", 0x00, 0x06);
}

/*=============================================================================
 * ResolveBumpersCollision_BlueField (0x1ce40)
 *===========================================================================*/
static void resolve_bumpers_collision_blue(GameState *state) {
    if (state->which_bumper) {
        uint8_t bumper_id = state->which_bumper_id;
        state->bumper_light_up_duration = 16;
        state->which_bumper_gfx = bumper_id - 1;
        state->which_bumper = 0;

        /* Apply bumper collision force */
        state->rumble_pattern = 0xFF;
        state->rumble_duration = 3;
        state->flipper_y_force = 0x0200;
        state->flipper_collision = 0x80;

        /* Angle delta */
        state->collision_normal_angle += bumper_angle_deltas_blue[bumper_id - 1];
        PLAY_SFX(state, "cave_light", 0x00, 0x0B);
    }

    if (state->bumper_light_up_duration > 0) {
        state->bumper_light_up_duration--;
        if (state->bumper_light_up_duration == 0) {
            /* Reset bumper graphics */
        }
    }
}

/*=============================================================================
 * ResolveBlueStageBoardTriggerCollision (0x1cfaa)
 *===========================================================================*/
static void resolve_board_trigger_collision_blue(GameState *state) {
    if (!state->which_board_trigger) return;
    state->which_board_trigger = 0;

    add_score_with_multiplier(state, state->config->scores.score_5);

    /* First trigger collision: advance collision state */
    if (state->stage_collision_state == 0) {
        state->stage_collision_state = 1;
        load_stage_collision_attributes(state);
        state->wd580 = 1;
    }

    /* Identify which alley trigger was hit */
    uint8_t trigger_idx = state->which_board_trigger_id - 7;
    if (trigger_idx < 8) {
        state->collided_alley_triggers[trigger_idx] = 1;
    }

    /* Process triggered alleys */
    if (state->collided_alley_triggers[0]) {
        /* HandleSecondaryLeftAlleyTrigger_BlueField (0x1d010) */
        state->collided_alley_triggers[0] = 0;
        if (state->left_alley_trigger) {
            state->left_alley_trigger = 0;
            if (!check_special_mode_collision(state, SPECIAL_COLLISION_LEFT_TRIGGER)) { /* ASM: ret c */
                if (state->left_alley_count < 3) {
                    state->left_alley_count++;
                    if (state->left_alley_count == 3) {
                        state->indicator_states[0] = 3;
                        state->indicator_states[2] = 0x80;
                    } else {
                        state->indicator_states[0] = state->left_alley_count | 0x80;
                    }
                }
            }
        }
    }

    if (state->collided_alley_triggers[1]) {
        /* HandleSecondaryRightAlleyTrigger_BlueField (0x1d047) */
        state->collided_alley_triggers[1] = 0;
        if (state->right_alley_trigger) {
            state->right_alley_trigger = 0;
            if (!check_special_mode_collision(state, SPECIAL_COLLISION_RIGHT_TRIGGER)) { /* ASM: ret c */
                if (state->right_alley_count < 3) {
                    state->right_alley_count++;
                    if (state->right_alley_count == 3) {
                        state->indicator_states[1] = 3;
                    } else {
                        state->indicator_states[1] = state->right_alley_count | 0x80;
                    }
                    if (state->right_alley_count >= 2) {
                        state->indicator_states[3] = 0x80;
                    }
                }
            }
        }
    }

    if (state->collided_alley_triggers[2]) {
        /* HandleLeftAlleyTrigger_BlueField (0x1d080) */
        state->collided_alley_triggers[2] = 0;
        state->right_alley_trigger = 0;
        state->secondary_left_alley_trigger[0] = 0;
        state->left_alley_trigger = 1;
    }

    if (state->collided_alley_triggers[3]) {
        /* HandleRightAlleyTrigger_BlueField (0x1d091) */
        state->collided_alley_triggers[3] = 0;
        state->left_alley_trigger = 0;
        state->secondary_left_alley_trigger[0] = 0;
        state->right_alley_trigger = 1;
    }
}

/*=============================================================================
 * ResolveBlueStagePikachuCollision (0x1d0a1) — simplified
 *===========================================================================*/
static void resolve_pikachu_collision_blue(GameState *state) {
    if (state->which_pikachu) {
        state->which_pikachu = 0;
        if (state->pikachu_saver_state) goto update;
        if (state->pikachu_saver_slot_reward_active) goto do_save;

        /* Check if correct side */
        uint8_t side = state->which_pikachu_id - 0x0D;
        if (side != state->which_pikachu_saver_side) goto update;

        if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
            goto do_save;
        }

        /* Partial bounce animation */
        init_animation(&state->pikachu_saver_anim, pikachu_saver_anim2_data_blue);
        state->pikachu_saver_state = 2;
        PLAY_SFX(state, "slot_trigger", 0x00, 0x3B);
        goto update;

    do_save:
        /* Full save animation */
        init_animation(&state->pikachu_saver_anim, pikachu_saver_anim_data_blue);
        if (!state->pikachu_saver_slot_reward_active) {
            state->pikachu_saver_charge = 0;
        }
        state->pikachu_saver_state = 1;
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
        state->enable_ball_gravity_and_tilt = 0;
        fill_bottom_message_buffer_with_black_tile(state);
    }

update:
    /* SetPikachuSaverSide — ASM uses IsKeyPressed2 (continuous hold),
     * not IsKeyPressed1 (one-shot newly pressed). */
    if (!state->pikachu_saver_state) {
        if (joypad_is_key_held(state, &state->key_config_left_flipper)) {
            state->which_pikachu_saver_side = 0;
        } else if (joypad_is_key_held(state, &state->key_config_right_flipper)) {
            state->which_pikachu_saver_side = 1;
        }
    }

    /* UpdatePikachuSaverAnimation */
    if (state->pikachu_saver_state == 1) {
        bool done = update_animation(&state->pikachu_saver_anim, pikachu_saver_anim_data_blue);
        if (done) {
            uint8_t idx = state->pikachu_saver_anim.index;
            if (idx == 1) {
                /* Pikachu sound + rumble.
                 * This is the best we can do — the original uses raw GBC wave
                 * channel PCM streaming via NR32 volume modulation
                 * (PlayPikachuSoundClip at bank 0x50) which can't be reproduced
                 * through the bytecode SFX system. */
                state->rumble_pattern = 0xFF;
                state->rumble_duration = 0x60;
                increment_max100(&state->num_pikachu_saves);
                if (state->num_pikachu_saves % 10 == 0) {
                    add_extra_ball(state);
                }
                audio_play_pcm(state->audio, 1);
                PLAY_SFX(state, "extra_ball", 0x16, 0x10);
            } else if (idx == 17) {
                /* Launch ball upward */
                state->ball_y_velocity = (int16_t)0xFC00;
                state->enable_ball_gravity_and_tilt = 1;
                add_score_with_multiplier(state, state->config->scores.score_5000);
                state->pikachu_saver_state = 0;
            }
        }
    } else if (state->pikachu_saver_state == 2) {
        bool done = update_animation(&state->pikachu_saver_anim, pikachu_saver_anim2_data_blue);
        if (done && state->pikachu_saver_anim.index == 1) {
            state->pikachu_saver_state = 0;
        }
    } else {
        /* Idle animation */
        state->pikachu_saver_anim.frame = (state->hram.frame_counter >> 4) & 1;
    }

    /* Pikachu saver sound cooldown */
    if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
        if (state->pikachu_saver_sound_cooldown > 0) {
            state->pikachu_saver_sound_cooldown--;
            if (state->pikachu_saver_sound_cooldown == 0x5A) {
                PLAY_SFX(state, "slot_spin", 0x0F, 0x22);
            }
        }
    }
}

/*=============================================================================
 * ResolveSlowpokeCollision (0x1d216)
 *===========================================================================*/
static void resolve_slowpoke_collision(GameState *state) {
    if (state->slowpoke_collision) {
        state->slowpoke_collision = 0;
        add_score_with_multiplier(state, state->config->scores.score_10000);
        PLAY_SFX(state, "bumper_small", 0x00, 0x05);

        init_animation(&state->slowpoke_anim, slowpoke_collision_anim_data);

        /* Freeze ball */
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->ball_x_pos &= 0xFF00;
        state->ball_y_pos &= 0xFF00;
        state->enable_ball_gravity_and_tilt = 0;
    }

    /* Always update animation */
    bool done = update_animation(&state->slowpoke_anim, slowpoke_collision_anim_data);
    if (state->slowpoke_anim.frame_counter == 0) {
        state->slowpoke_anim_frame_counter = 0x19;
        state->slowpoke_anim_frame = 0;
        state->slowpoke_anim_index = 6;
    }

    if (!done) return;

    uint8_t idx = state->slowpoke_anim.index;
    if (idx == 1) {
        /* Ball enters slowpoke mouth */
        state->pinball_is_visible = 0;

        /* Check if left alley count triggers evolution mode */
        if (state->left_alley_count >= 3) {
            start_evolution_mode(state);
            if (state->wd643) {
                state->wd642 = 1;
            }
        }

        increment_max100(&state->num_slowpoke_entries);
        if (increment_max100(&state->num_bellsprout_entries)) {
            if (state->num_bellsprout_entries % 25 == 0) {
                add_extra_ball(state);
            }
        }
    } else if (idx == 4) {
        state->pinball_is_visible = 1;
    } else if (idx == 5) {
        /* Ball ejected from slowpoke */
        state->enable_ball_gravity_and_tilt = 1;
        state->ball_x_velocity = 0x00B0; /* rightward */
        state->ball_y_velocity = 0;
        PLAY_SFX(state, "bumper", 0x00, 0x06);

        if (state->wd642 == 0) {
            check_special_mode_collision(state, SPECIAL_COLLISION_SLOWPOKE);
        }
        state->wd642 = 0;

        /* Force field: set to down, mark as flipped */
        state->blue_field_force_field_frame_counter = 0;
        state->blue_field_force_field_seconds_counter = 0;
        state->blue_stage_force_field_flipped_down = 1;
        state->blue_stage_force_field_direction = 2; /* down */
        state->blue_stage_force_field_gfx_needs_loading = 1;
    }
}

/*=============================================================================
 * ResolveCloysterCollision (0x1d32d)
 *===========================================================================*/
static void resolve_cloyster_collision(GameState *state) {
    if (state->cloyster_collision) {
        state->cloyster_collision = 0;
        add_score_with_multiplier(state, state->config->scores.score_10000);
        PLAY_SFX(state, "bumper_small", 0x00, 0x05);

        init_animation(&state->cloyster_anim, cloyster_collision_anim_data);

        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->ball_x_pos &= 0xFF00;
        state->ball_y_pos &= 0xFF00;
        state->enable_ball_gravity_and_tilt = 0;
    }

    bool done = update_animation(&state->cloyster_anim, cloyster_collision_anim_data);
    if (state->cloyster_anim.frame_counter == 0) {
        state->cloyster_anim_frame_counter = 0x19;
        state->cloyster_anim_frame = 0;
        state->cloyster_anim_index = 6;
    }

    if (!done) return;

    uint8_t idx = state->cloyster_anim.index;
    if (idx == 1) {
        state->pinball_is_visible = 0;

        /* Check if right alley count triggers catch'em mode */
        if (state->right_alley_count >= 2) {
            uint8_t rare = (state->right_alley_count > 2) ? 8 : 0;
            state->rare_mons_flag = rare;
            start_catchem_mode(state);
        }

        increment_max100(&state->num_cloyster_entries);
        if (increment_max100(&state->num_bellsprout_entries)) {
            if (state->num_bellsprout_entries % 25 == 0) {
                add_extra_ball(state);
            }
        }
    } else if (idx == 4) {
        state->pinball_is_visible = 1;
    } else if (idx == 5) {
        /* Ball ejected from cloyster */
        state->enable_ball_gravity_and_tilt = 1;
        state->ball_x_velocity = (int16_t)0xFF4F; /* leftward */
        state->ball_y_velocity = 0;
        PLAY_SFX(state, "bumper", 0x00, 0x06);

        check_special_mode_collision(state, SPECIAL_COLLISION_CLOYSTER);

        state->blue_field_force_field_frame_counter = 0;
        state->blue_field_force_field_seconds_counter = 0;
        state->blue_stage_force_field_flipped_down = 1;
        state->blue_stage_force_field_direction = 2;
        state->blue_stage_force_field_gfx_needs_loading = 1;
    }
}

/*=============================================================================
 * ResolvePsyduckPoliwagCollision (0x1dbd2) + UpdatePsyduckAndPoliwag
 *===========================================================================*/
static void update_poliwag(GameState *state);
static void update_psyduck(GameState *state);

static void resolve_psyduck_poliwag_collision(GameState *state) {
    if (!state->which_psyduck_poliwag) {
        update_poliwag(state);
        update_psyduck(state);
        return;
    }

    if (state->which_psyduck_poliwag_id == 0x12) {
        /* Hit psyduck (right) */
        state->which_psyduck_poliwag = 0;
        if (state->right_map_move_counter >= 3) {
            update_poliwag(state);
            update_psyduck(state);
            return;
        }
        state->right_map_move_counter++;
        state->right_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;

        if (state->current_stage & 1) {
            state->stage_collision_map[0xF0] = 0x52;
            state->stage_collision_map[0x110] = 0x53;
        }

        /* ASM .asm_1dc5c: load_gfx -> check_special_mode -> HitPsyduck3Times -> set_state */
        load_psyduck_or_poliwag_graphics_blue(state, 3);
        load_psyduck_or_poliwag_number_graphics_blue(state,
            state->right_map_move_counter);

        check_special_mode_collision(state, SPECIAL_COLLISION_PSYDUCK);

        if (state->right_map_move_counter == 3) {
            /* HitPsyduck3Times */
            increment_max100(&state->num_psyduck_triples);
            increment_max100(&state->num_dugtrio_triples);
            if (state->num_dugtrio_triples % 10 == 0) {
                add_extra_ball(state);
            }
            state->map_move_direction = 1;
            start_map_move_mode(state);
        }

        state->psyduck_state = 2;
        state->right_map_move_psyduck_anim_counter = 0x28;
        state->right_map_move_psyduck_frame = 0x78;
    } else {
        /* Hit poliwag (left) */
        state->which_psyduck_poliwag = 0;
        if (state->left_map_move_counter >= 3) {
            update_poliwag(state);
            update_psyduck(state);
            return;
        }
        state->left_map_move_counter++;
        state->left_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;

        if (state->current_stage & 1) {
            state->stage_collision_map[0xE3] = 0x54;
            state->stage_collision_map[0x103] = 0x55;
        }

        /* #23: ASM line 1537: _LoadPsyduckOrPoliwagGraphics(1) + number gfx */
        load_psyduck_or_poliwag_graphics_blue(state, 1);
        load_psyduck_or_poliwag_number_graphics_blue(state,
            state->left_map_move_counter);

        state->poliwag_state = 2;
        state->left_map_move_poliwag_anim_counter = 0x78;
        state->left_map_move_poliwag_frame = 0x14;

        /* HitPoliwag3Times is handled ONLY in update_poliwag(), not here.
         * ASM: HitPoliwag3Times called only from UpdatePoliwag (0x1ddc7). */

        check_special_mode_collision(state, SPECIAL_COLLISION_POLIWAG);
    }

    /* Score for hitting psyduck or poliwag */
    if (state->current_stage & 1) {
        state->rumble_pattern = 0x55;
        state->rumble_duration = 4;
        state->collision_force_amplification = 2;
        add_score_with_multiplier(state, state->config->scores.score_500);
        PLAY_SFX(state, "ball_drain", 0x00, 0x0F);
    }
}

static void update_poliwag(GameState *state) {
    if (state->poliwag_state == 0) return;

    if (state->left_map_move_poliwag_anim_counter > 0) {
        state->left_map_move_poliwag_anim_counter--;
        if (state->wd644) return;
        if (state->left_map_move_poliwag_frame <= 1) {
            if (state->poliwag_state != 2) return;
            /* ASM .asm_1dcb9: Func_1130 guards transition (graphics queue caught up).
             * In C, graphics are immediate so queue is always caught up.
             * #19: Load number graphics and check for 3 hits before transitioning. */
            /* #23: LoadPsyduckOrPoliwagNumberGraphics (GBC) */
            if (state->left_map_move_counter > 0) {
                load_psyduck_or_poliwag_number_graphics_blue(state,
                    (uint8_t)(state->left_map_move_counter + 7));
            } else {
                load_psyduck_or_poliwag_number_graphics_blue(state, 0);
            }
            /* ASM: HitPoliwag3Times check (backup path from update) */
            if (state->left_map_move_counter == 3) {
                increment_max100(&state->num_poliwag_triples);
                increment_max100(&state->num_dugtrio_triples);
                if (state->num_dugtrio_triples % 10 == 0) {
                    add_extra_ball(state);
                }
                state->map_move_direction = 0;
                start_map_move_mode(state);
            }
            state->poliwag_state = 1;
            return;
        }
        state->left_map_move_poliwag_frame--;
        return;
    }

    if (state->poliwag_state != 1) return;

    /* Reset poliwag to default graphics */
    /* #23: Load default poliwag graphics */
    load_psyduck_or_poliwag_graphics_blue(state, 0);
    if (state->current_stage & 1) {
        state->stage_collision_map[0xE3] = 0x5E;
        state->stage_collision_map[0x103] = 0x5F;
    }
    /* ASM: .asm_1dd0c resets wPoliwagState = 0 unconditionally (jump target
     * reached from both top and bottom stage paths). */
    state->poliwag_state = 0;
    if (state->left_map_move_counter == 3) {
        state->left_map_move_counter -= 3;
        load_psyduck_or_poliwag_number_graphics_blue(state,
            state->left_map_move_counter);
        load_psyduck_or_poliwag_graphics_blue(state, 0);
        state->poliwag_state = 0;
    }
}

static void update_psyduck(GameState *state) {
    if (state->psyduck_state == 0) return;

    if (state->psyduck_state == 2) {
        /* ASM .asm_1dd2e state 2: countdown, then load graphics + go to state 1 */
        if (state->right_map_move_psyduck_anim_counter > 0) {
            state->right_map_move_psyduck_anim_counter--;
            return;
        }
        /* #23: ASM .asm_1dd48: _LoadPsyduckOrPoliwagGraphics(2) on counter expiry */
        load_psyduck_or_poliwag_graphics_blue(state, 2);
        state->psyduck_state = 1;
    }

    if (state->psyduck_state == 1) {
        /* #23: ASM .asm_1dd53: Load number + graphics before going to state 3 */
        load_psyduck_or_poliwag_number_graphics_blue(state,
            (uint8_t)(state->right_map_move_counter + 4));
        load_psyduck_or_poliwag_graphics_blue(state,
            (uint8_t)(state->right_map_move_counter + 3));
        state->psyduck_state = 3;
        return;
    }

    if (state->psyduck_state == 3) {
        if (state->right_map_move_psyduck_frame > 0) {
            state->right_map_move_psyduck_frame--;
            return;
        }
        /* #23: ASM .asm_1dd74: Load number graphics (GBC) */
        if (state->right_map_move_counter > 0) {
            load_psyduck_or_poliwag_number_graphics_blue(state,
                (uint8_t)(state->right_map_move_counter + 0x0A));
        } else {
            load_psyduck_or_poliwag_number_graphics_blue(state, 4);
        }
        load_psyduck_or_poliwag_graphics_blue(state, 2);
        /* Reset collision map */
        if (state->current_stage & 1) {
            state->stage_collision_map[0xF0] = 0x24;
            state->stage_collision_map[0x110] = 0x25;
            state->psyduck_state = 0;  /* Only reset on bottom stage */
        }
        if (state->right_map_move_counter == 3) {
            state->right_map_move_counter -= 3;
            load_psyduck_or_poliwag_number_graphics_blue(state, 4);
            load_psyduck_or_poliwag_graphics_blue(state, 2);
            state->psyduck_state = 0;
        }
    }
}

/* Forward declarations for bonus multiplier helpers (defined below) */
static void load_bonus_mult_railing_gfx_blue(GameState *state, uint8_t a);
static void load_bonus_mult_railing_indicator_blue(GameState *state, uint8_t indicator);
static void update_bonus_multiplier_railing_blue(GameState *state);

/*=============================================================================
 * ResolveBonusMultiplierCollision_BlueField (0x1d438)
 *===========================================================================*/
static void resolve_bonus_multiplier_collision_blue(GameState *state) {
    /* UpdateBonusMultiplierRailingLight (0x1d692): railing light timer */
    if (state->bonus_multiplier_railing_end_light_duration > 0) {
        if (state->bonus_multiplier_railing_end_light_duration == 1) {
            /* Light expired: load unlit railing graphics (GBC: indices $2A and $28) */
            state->bonus_multiplier_railing_end_light_duration = 0;
            load_bonus_mult_railing_indicator_blue(state, 0);  /* unlit left ($28) */
            load_bonus_mult_railing_indicator_blue(state, 2);  /* unlit right ($2A) */
        } else {
            state->bonus_multiplier_railing_end_light_duration--;
        }
    }

    if (!state->which_bonus_multiplier_railing) {
        /* No collision — run per-frame update (blinking + message) */
        update_bonus_multiplier_railing_blue(state);
        return;
    }

    /* Collision occurred */
    uint8_t railing_id = state->which_bonus_multiplier_railing_id;
    state->which_bonus_multiplier_railing = 0;
    PLAY_SFX(state, "ball_saver", 0x00, 0x0D);

    if (railing_id == 0x0F) {
        /* Hit left railing (blue field ID $0F)
         * ASM (GBC): passes gfx index $29 to _LoadBonusMultiplierRailingGraphics_BlueField
         * for the lit railing indicator (5 tile writes at bg_map offsets $24-$26, $44-$45). */
        state->bonus_multiplier_railing_end_light_duration = 0x3C;
        /* Load lit railing indicator tiles (GBC index $29 = lit left) */
        load_bonus_mult_railing_indicator_blue(state, 1);
        /* Reload lit digit graphics (set bit 7 = lit state) */
        load_bonus_mult_railing_gfx_blue(state, state->bonus_multiplier_tens_digit | 0x80);
        load_bonus_mult_railing_gfx_blue(state, (uint8_t)((state->bonus_multiplier_ones_digit + 0x14) | 0x80));

        check_special_mode_collision(state, SPECIAL_COLLISION_LEFT_BONUS_MULTIPLIER);

        if (state->wd610 == 3) {
            state->wd610 = 1;
            state->wd611 = 3;
            state->bonus_multiplier_tens_digit |= 0x80;
        }
    } else {
        /* Hit right railing (blue field ID $10)
         * ASM (GBC): passes gfx index $2B to _LoadBonusMultiplierRailingGraphics_BlueField
         * for the lit railing indicator (5 tile writes at bg_map offsets $2D-$2F, $4E-$4F). */
        state->bonus_multiplier_railing_end_light_duration = 0x1E;
        /* Load lit railing indicator tiles (GBC index $2B = lit right) */
        load_bonus_mult_railing_indicator_blue(state, 3);
        /* Reload lit digit graphics (set bit 7 = lit state) */
        load_bonus_mult_railing_gfx_blue(state, state->bonus_multiplier_tens_digit | 0x80);
        load_bonus_mult_railing_gfx_blue(state, (uint8_t)((state->bonus_multiplier_ones_digit + 0x14) | 0x80));

        check_special_mode_collision(state, SPECIAL_COLLISION_RIGHT_BONUS_MULTIPLIER);

        if (state->wd611 == 3) {
            state->wd610 = 1;
            state->wd611 = 1;
            state->wd612 = 0x80;
            state->bonus_multiplier_ones_digit |= 0x80;

            /* Increment bonus multiplier */
            if (state->cur_bonus_multiplier < MAX_BONUS_MULTIPLIER) {
                state->cur_bonus_multiplier++;
                if (state->cur_bonus_multiplier % 25 == 0) {
                    add_extra_ball(state);
                }
            }
            state->wd614 = state->bonus_multiplier_tens_digit;
            state->wd615 = state->bonus_multiplier_ones_digit;
            state->show_bonus_multiplier_bottom_message = 1;
        }
    }

    add_score_with_multiplier(state, state->config->scores.score_10);
}

/* LoadSlotCaveCoverGraphics_BlueField (0x1e8f6)
 * Writes tile IDs to bg_map for the slot cave cover (blue field GBC tiles).
 * Index = (stage & 1) * 2 + slot_is_open:
 *   0 = Top closed:    bg_map[0x229]={$F0,$F1}, bg_map[0x224]=$D8, bg_map[0x22F]=$EC
 *   1 = Top open:      bg_map[0x229]={$F2,$F3}
 *   2 = Bottom closed: bg_map[0x09]={$15,$16}, bg_map[0x29]={$17,$18}
 *   3 = Bottom open:   bg_map[0x09]={$19,$1A}, bg_map[0x29]={$1B,$1C}
 */
void load_slot_cave_cover_graphics_blue(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    uint8_t idx = (uint8_t)(((state->current_stage & 1) * 2) + (state->slot_is_open ? 1 : 0));

    switch (idx) {
    case 0: /* Top, closed */
        bg[0x229] = 0xF0; bg[0x22A] = 0xF1;
        bg[0x224] = 0xD8; bg[0x22F] = 0xEC;
        break;
    case 1: /* Top, open */
        bg[0x229] = 0xF2; bg[0x22A] = 0xF3;
        break;
    case 2: /* Bottom, closed */
        bg[0x09] = 0x15; bg[0x0A] = 0x16;
        bg[0x29] = 0x17; bg[0x2A] = 0x18;
        break;
    case 3: /* Bottom, open */
        bg[0x09] = 0x19; bg[0x0A] = 0x1A;
        bg[0x29] = 0x1B; bg[0x2A] = 0x1C;
        break;
    }
}

/* LoadPinballUpgradeTriggersGraphics_BlueField (0x1e475)
 * Updates BG tilemap tiles for each of the 3 upgrade trigger dots.
 * CGB tiles: ON=$43,$43, OFF=$42,$42 at bg_map 0x86, 0x69, 0x8C.
 * Each trigger writes 2 consecutive tiles. */
static void load_upgrade_triggers_graphics_blue(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    /* Trigger 0: bg_map 0x86-0x87 (row 4, col 6) */
    uint8_t t0 = state->ball_upgrade_trigger_states[0] ? 0x43 : 0x42;
    bg[0x86] = t0; bg[0x87] = t0;
    /* Trigger 1: bg_map 0x69-0x6A (row 3, col 9) */
    uint8_t t1 = state->ball_upgrade_trigger_states[1] ? 0x43 : 0x42;
    bg[0x69] = t1; bg[0x6A] = t1;
    /* Trigger 2: bg_map 0x8C-0x8D (row 4, col 12) */
    uint8_t t2 = state->ball_upgrade_trigger_states[2] ? 0x43 : 0x42;
    bg[0x8C] = t2; bg[0x8D] = t2;
}

/*=============================================================================
 * ResolveBallUpgradeTriggersCollision_BlueField (0x1e356)
 * ASM: no-collision and early-exit paths all jp UpdatePinballUpgradeTriggers
 * which calls blinking update + conditionally LoadPinballUpgradeTriggersGraphics.
 * Collision path always jp LoadPinballUpgradeTriggersGraphics.
 *===========================================================================*/
static void resolve_ball_upgrade_triggers_blue(GameState *state) {
    if (!state->which_pinball_upgrade_trigger)
        goto update_triggers;

    state->which_pinball_upgrade_trigger = 0;

    /* First trigger collision: advance collision state */
    if (state->stage_collision_state == 0) {
        state->stage_collision_state = 1;
        load_stage_collision_attributes(state);
        state->wd580 = 1;
    }

    if (!(state->stage_collision_state & 1))
        goto update_triggers;

    if (state->ball_upgrade_triggers_blinking)
        goto update_triggers;

    state->right_alley_trigger = 0;
    state->left_alley_trigger = 0;
    state->secondary_left_alley_trigger[0] = 0;

    check_special_mode_collision(state, SPECIAL_COLLISION_BALL_UPGRADE);

    /* Toggle the trigger state */
    uint8_t trigger_idx = state->which_pinball_upgrade_trigger_id - 0x13;
    if (trigger_idx < 3) {
        state->ball_upgrade_trigger_states[trigger_idx] =
            state->ball_upgrade_trigger_states[trigger_idx] ? 0 : 1;
    }

    add_score_with_multiplier(state, state->config->scores.score_100);

    /* Check if all 3 triggers are on */
    if (state->ball_upgrade_trigger_states[0] &&
        state->ball_upgrade_trigger_states[1] &&
        state->ball_upgrade_trigger_states[2]) {

        /* All on: start blinking + upgrade ball */
        state->ball_upgrade_triggers_blinking = 1;
        state->ball_upgrade_triggers_blinking_frames_remaining = 0x80;
        state->ball_type_counter = PINBALL_UPGRADE_FRAMES_COUNTER;

        add_score_with_multiplier(state, state->config->scores.score_400);

        /* FieldMultiplierText header: scrolling_text_normal 0, 20, 0, 20 */
        static const uint8_t FIELD_MULT_HEADER_BLUE[6] = { 5, 0x54, 0x40, 20, 0x00, 60 };
        /* FieldMultiplierSpecialBonusText header: scrolling_text_nopause 7, 51 */
        static const uint8_t FIELD_MULT_SPECIAL_BLUE[6] = { 7, 0x54, 0, 0, 0, 51 };
        /* DigitsText1to8 header: scrolling_text 7, 51, 6, 20, 2, 15 */
        static const uint8_t DIGITS_1_8_BLUE[6] = { 7, 0x73, 0x46, 20, 0x20, 80 };

        if (state->ball_type >= MASTER_BALL) {
            PLAY_SFX(state, "slot_start", 0x0F, 0x4D);
            add_score_no_multiplier(state, state->config->scores.score_1000000);
            static const uint8_t bcd_1m_blue[4] = { 0x01, 0x00, 0x00, 0x00 };
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text_with_bcd_public(state, 1, DIGITS_1_8_BLUE, bcd_1m_blue);
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_BLUE,
                                "FIELD MULTIPLIER SPECIAL BONUS");
        } else {
            PLAY_SFX(state, "slot_reel_stop", 0x06, 0x3A);
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 0, FIELD_MULT_HEADER_BLUE,
                                "FIELD MULTIPLIER x0");
            uint8_t new_type = ball_type_progression_blue[state->ball_type];
            state->ball_type = new_type;
            state->bottom_message_text[18] = (uint8_t)(0x86 + new_type);
        }
        /* TransitionPinballUpgrade: reload ball sprite tiles + palette */
        load_ball_gfx(state);
    } else {
        PLAY_SFX(state, "object_hit", 0x00, 0x09);
    }
    goto load_gfx;

update_triggers:
    update_pinball_upgrade_blinking_blue(state);
load_gfx:
    load_upgrade_triggers_graphics_blue(state);
}

/*=============================================================================
 * UpdateBallTypeUpgradeCounter_BlueField (0x1e58c)
 *===========================================================================*/
static void update_ball_type_upgrade_counter_blue(GameState *state) {
    if (state->capturing_mon) return;
    if (state->ball_type_counter == 0) return;

    state->ball_type_counter--;
    if (state->ball_type_counter != 0) return;

    /* Degrade ball type */
    state->ball_type = ball_type_degradation_blue[state->ball_type];
    if (state->ball_type != 0) {
        state->ball_type_counter = PINBALL_UPGRADE_FRAMES_COUNTER;
    }
}

/*=============================================================================
 * UpdatePinballUpgradeBlinkingAnimation_BlueField (0x1e4b8)
 *===========================================================================*/
static void update_pinball_upgrade_blinking_blue(GameState *state) {
    if (!state->ball_upgrade_triggers_blinking) {
        /* Flipper rotation of trigger states */
        if (joypad_is_key_pressed(state, &state->key_config_left_flipper)) {
            /* Rotate left: {[0],[1],[2]} -> {[1],[2],[0]} */
            uint8_t a = state->ball_upgrade_trigger_states[0];
            uint8_t b = state->ball_upgrade_trigger_states[1];
            uint8_t c = state->ball_upgrade_trigger_states[2];
            state->ball_upgrade_trigger_states[0] = b;
            state->ball_upgrade_trigger_states[1] = c;
            state->ball_upgrade_trigger_states[2] = a;
        } else if (joypad_is_key_pressed(state, &state->key_config_right_flipper)) {
            /* Rotate right: {[0],[1],[2]} -> {[2],[0],[1]} */
            uint8_t a = state->ball_upgrade_trigger_states[0];
            uint8_t b = state->ball_upgrade_trigger_states[1];
            uint8_t c = state->ball_upgrade_trigger_states[2];
            state->ball_upgrade_trigger_states[0] = c;
            state->ball_upgrade_trigger_states[1] = a;
            state->ball_upgrade_trigger_states[2] = b;
        }
        return;
    }

    state->ball_upgrade_triggers_blinking_frames_remaining--;
    if (state->ball_upgrade_triggers_blinking_frames_remaining == 0) {
        state->ball_upgrade_triggers_blinking = 0;
    }

    if ((state->ball_upgrade_triggers_blinking_frames_remaining & 7) == 0) {
        uint8_t val = (state->ball_upgrade_triggers_blinking_frames_remaining >> 3) & 1;
        state->ball_upgrade_trigger_states[0] = val;
        state->ball_upgrade_trigger_states[1] = val;
        state->ball_upgrade_trigger_states[2] = val;
    }
}

/* LoadCAVELightsGraphics_BlueField (0x1e627)
 * Updates BG tilemap tiles for each of the 4 CAVE lights based on their state.
 * GBC mode tiles from data/queued_tiledata/blue_field/cave_lights.asm */
static void load_cave_lights_graphics_blue(GameState *state) {
    uint8_t *bg = state->vram->bg_map[0];
    /* Light C (index 0): BG map 0x121 */
    bg[0x121] = state->cave_light_states[0] ? 0x49 : 0x47;
    /* Light A (index 1): BG map 0x123 */
    bg[0x123] = state->cave_light_states[1] ? 0x4A : 0x48;
    /* Light V (index 2): BG map 0x130 */
    bg[0x130] = state->cave_light_states[2] ? 0x4B : 0x7A;
    /* Light E (index 3): BG map 0x132 */
    bg[0x132] = state->cave_light_states[3] ? 0x4C : 0x7B;
}

/*=============================================================================
 * ResolveCAVELightCollision_BlueField (0x1e5c5)
 *===========================================================================*/
static void resolve_cave_light_collision_blue(GameState *state) {
    if (state->which_cave_light) {
        state->which_cave_light = 0;
        if (state->cave_lights_blinking) goto no_collision;

        uint8_t light_idx = state->which_cave_light_id - 0x16;
        if (light_idx >= 4) goto no_collision;

        if (state->cave_light_states[light_idx]) goto no_collision; /* already on */
        state->cave_light_states[light_idx] = 1;

        add_score_with_multiplier(state, state->config->scores.score_100);

        /* Check if all 4 CAVE lights are on */
        if (state->cave_light_states[0] && state->cave_light_states[1] &&
            state->cave_light_states[2] && state->cave_light_states[3]) {
            state->cave_lights_blinking = 1;
            state->cave_lights_blinking_frames_remaining = 0x80;
            add_score_with_multiplier(state, state->config->scores.score_400);
            PLAY_SFX(state, "object_hit", 0x00, 0x09);
            increment_max100(&state->num_cave_completions);
        }
        /* ASM: falls through to LoadCAVELightsGraphics_BlueField */
        load_cave_lights_graphics_blue(state);
        return;
    }

no_collision:
    update_cave_lights_blinking_blue(state);
    /* ASM: bottom stage always falls through to LoadCAVELightsGraphics */
    load_cave_lights_graphics_blue(state);
}

/*=============================================================================
 * UpdateCAVELightsBlinking_BlueField (0x1e66a)
 *===========================================================================*/
static void update_cave_lights_blinking_blue(GameState *state) {
    /* ASM: UpdateCAVELightsBlinking_BlueField (0x1e66a)
     * STATE-ONLY when called from top stage dispatch. Graphics are loaded
     * by resolve_cave_light_collision_blue (bottom stage only). */
    if (state->cave_lights_blinking) {
        state->cave_lights_blinking_frames_remaining--;
        if (state->cave_lights_blinking_frames_remaining == 0) {
            state->cave_lights_blinking = 0;
            state->opened_slot_by_cave_lights = 1;
            state->frames_until_slot_cave_opens = 3;
        }

        if ((state->cave_lights_blinking_frames_remaining & 7) == 0) {
            uint8_t val = (state->cave_lights_blinking_frames_remaining >> 3) & 1;
            state->cave_light_states[0] = val;
            state->cave_light_states[1] = val;
            state->cave_light_states[2] = val;
            state->cave_light_states[3] = val;
        }
        return;
    }

    /* Flipper rotation (state only — no graphics) */
    if (joypad_is_key_pressed(state, &state->key_config_left_flipper)) {
        uint8_t tmp = state->cave_light_states[0];
        state->cave_light_states[0] = state->cave_light_states[1];
        state->cave_light_states[1] = state->cave_light_states[2];
        state->cave_light_states[2] = state->cave_light_states[3];
        state->cave_light_states[3] = tmp;
    } else if (joypad_is_key_pressed(state, &state->key_config_right_flipper)) {
        uint8_t tmp = state->cave_light_states[3];
        state->cave_light_states[3] = state->cave_light_states[2];
        state->cave_light_states[2] = state->cave_light_states[1];
        state->cave_light_states[1] = state->cave_light_states[0];
        state->cave_light_states[0] = tmp;
    }
}

/*=============================================================================
 * ResolveSlotCollision_BlueField (0x1e757)
 *===========================================================================*/
static void resolve_slot_collision_blue(GameState *state) {
    if (state->slot_collision) {
        state->slot_collision = 0;
        if (!state->slot_is_open) return;
        if (state->slot_enter_or_exit_counter) return;

        /* Ball enters slot */
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->ball_x_pos = 0x5000;
        state->ball_y_pos = 0x1600;
        state->slot_enter_or_exit_counter = 0x13;
    }

    /* Roulette in progress — keep glow alive, tick state machine, PAUSE counter.
     * Matches red field resolve_slot() behavior (ASM: DoSlotRewardRoulette is blocking). */
    if (state->slot_roulette_active) {
        state->slot_glowing_anim_counter = 0x18;
        update_slot_roulette(state);
        return;
    }

    if (state->slot_enter_or_exit_counter == 0) return;
    state->slot_enter_or_exit_counter--;
    state->slot_glowing_anim_counter = 0x18;

    uint8_t cnt = state->slot_enter_or_exit_counter;
    if (cnt == 0x12) {
        PLAY_SFX(state, "arrow_indicator", 0x00, 0x21);
        /* #24: ASM calls LoadMiniBallGfx at cnt==$12 */
        load_mini_ball_gfx(state);
    } else if (cnt == 0x0F) {
        /* ASM calls LoadSuperMiniPinballGfx */
        load_super_mini_ball_gfx(state);
    } else if (cnt == 0x0C) {
        state->pinball_is_visible = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
    } else if (cnt == 0x09) {
        /* DoSlotLogic_BlueField */
        state->indicator_states[4] = 0;
        if (check_special_mode_collision(state, SPECIAL_COLLISION_SLOT_HOLE)) {
            /* ASM: jr nc skips slot logic; carry set means special mode consumed it */
            state->pinball_is_visible = 1;
            state->enable_ball_gravity_and_tilt = 1;
            return;
        }

        /* Check if pokeballs = 3: go to bonus stage */
        if (state->previous_num_pokeballs >= 3 && !state->frames_until_slot_cave_opens) {
            if (!state->bonus_stage_slot_reward_active) {
                state->num_pokeballs = 0;
                state->pokeball_blinking_counter = 0x40;
            }
            state->bonus_stage_slot_reward_active = 0;
            state->going_to_bonus_stage = 1;
            state->move_to_next_screen_state = 1;
            state->next_stage = bonus_stages_blue[state->next_bonus_stage];

            /* Show bonus stage text */
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            uint8_t stage = state->next_stage;
            if (stage == STAGE_MEOWTH_BONUS)
                load_scrolling_text(state, 2, "GO TO", "MEOWTH BONUS STAGE");
            else if (stage == STAGE_SEEL_BONUS)
                load_scrolling_text(state, 2, "GO TO", "SEEL BONUS STAGE");
            else
                load_scrolling_text(state, 2, "GO TO", "MEWTWO BONUS STAGE");

            audio_stop_all(state->audio);
            PLAY_SFX(state, "bonus_stage_enter", 0x3C, 0x23);
            state->opened_slot_by_pokeballs = 0;
            state->catchem_or_evolution_slot_reward_active = 0;
            state->frames_until_slot_cave_opens = 30;
        } else {
            /* Normal slot roulette — just initiate; roulette runs over many frames.
             * Counter is PAUSED by slot_roulette_active check above.
             * Results are checked at cnt==0x00 after roulette completes. */
            start_slot_roulette(state);
            state->opened_slot_by_cave_lights = 0;
        }
    } else if (cnt == 0x06) {
        state->slot_is_open = 0;
        state->rumble_pattern = 5;
        state->rumble_duration = 8;
        /* ASM: callba LoadMiniBallGfx at cnt==$6 (ball grows back) */
        load_mini_ball_gfx(state);
    } else if (cnt == 0x03) {
        /* ASM: callba LoadBallGfx — restore normal ball graphics */
        load_ball_gfx(state);
        state->ball_y_velocity = 0x0200;
        state->ball_x_velocity = 0x0080;
    } else if (cnt == 0x00) {
        /* #25: ASM calls LoadSlotCaveCoverGraphics_BlueField at cnt==0 */
        load_slot_cave_cover_graphics_blue(state);

        /* Check roulette result — bonus stage? */
        if (state->slot_roulette_billboard_picture >= 0x15) {
            goto bonus_from_roulette;
        }
        state->pinball_is_visible = 1;
        state->enable_ball_gravity_and_tilt = 1;

        /* Ball exits slot — dispatch reward */
        if (state->catchem_or_evolution_slot_reward_active == 1) {
            /* Catch'em mode from slot */
            state->rare_mons_flag = gen_random(state) & 8;
            start_catchem_mode(state);
            state->catchem_or_evolution_slot_reward_active = 0;
        } else if (state->catchem_or_evolution_slot_reward_active == 2) {
            /* Evolution mode from slot */
            start_evolution_mode(state);
            state->catchem_or_evolution_slot_reward_active = 0;
        }
    }
    return;

bonus_from_roulette:
    /* Same as bonus stage entry from roulette */
    if (!state->bonus_stage_slot_reward_active) {
        state->num_pokeballs = 0;
        state->pokeball_blinking_counter = 0x40;
    }
    state->bonus_stage_slot_reward_active = 0;
    state->going_to_bonus_stage = 1;
    state->move_to_next_screen_state = 1;
    state->next_stage = bonus_stages_blue[state->next_bonus_stage];

    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    uint8_t stage2 = state->next_stage;
    if (stage2 == STAGE_MEOWTH_BONUS)
        load_scrolling_text(state, 2, "GO TO", "MEOWTH BONUS STAGE");
    else if (stage2 == STAGE_SEEL_BONUS)
        load_scrolling_text(state, 2, "GO TO", "SEEL BONUS STAGE");
    else
        load_scrolling_text(state, 2, "GO TO", "MEWTWO BONUS STAGE");

    audio_stop_all(state->audio);
    PLAY_SFX(state, "bonus_stage_enter", 0x3C, 0x23);
    state->opened_slot_by_pokeballs = 0;
    state->catchem_or_evolution_slot_reward_active = 0;
    state->frames_until_slot_cave_opens = 30;
}

/*=============================================================================
 * OpenSlotCave_BlueField (0x1e9c0)
 *===========================================================================*/
static void open_slot_cave_blue(GameState *state) {
    if (state->frames_until_slot_cave_opens == 0) return;
    state->frames_until_slot_cave_opens--;
    if (state->frames_until_slot_cave_opens != 0) return;

    if (state->in_special_mode) return;

    uint8_t billboard_id;
    if (state->opened_slot_by_pokeballs) {
        billboard_id = state->next_bonus_stage + 0x15;
    } else if (state->opened_slot_by_cave_lights) {
        billboard_id = 0x1A; /* "Slot On" */
    } else {
        return;
    }

    if (state->current_stage & 1) {
        load_billboard_tile_data(state, billboard_id);
    }

    if (state->slot_is_open) return;
    state->slot_is_open = 1;
    state->indicator_states[4] = 0x80;

    /* #21: ASM calls LoadSlotCaveCoverGraphics_BlueField on bottom stage
     * after setting slot_is_open=1 */
    if (state->current_stage & 1) {
        load_slot_cave_cover_graphics_blue(state);
    }
}

/*=============================================================================
 * UpdateForceFieldDirection (0x1c8b6)
 *===========================================================================*/
static void update_force_field_direction(GameState *state) {
    state->blue_field_force_field_frame_counter++;
    if (state->blue_field_force_field_frame_counter < 60) return;

    state->blue_field_force_field_frame_counter = 0;
    state->blue_field_force_field_seconds_counter++;
    if (state->blue_field_force_field_seconds_counter < 5) return;

    /* Every 5 seconds, evaluate force field direction */
    if (state->wd644 || (!state->wd643 && state->right_alley_count < 2)) {
        state->wd64b = 0;
    }
    if (state->wd644 || (!state->wd643 && state->left_alley_count < 3)) {
        state->wd64b = 0;
    }

    state->blue_field_force_field_seconds_counter = 0;
    state->wd64a = 0;
    state->wd649 = 0;
    state->wd648 = 0;

    uint8_t dir = state->blue_stage_force_field_direction;
    if (dir == 1 || dir == 3) {
        /* Already pointing left or right: try to flip to up */
        goto try_flip_up;
    }

    /* Try to point right */
    if (state->wd644) {
        if (state->map_move_direction != 0) goto try_right;
        goto try_left;
    }
    if (state->wd643) goto try_right;
    if (state->right_alley_count >= 2) goto try_right;
    goto try_left;

try_right:
    if (state->wd64b == 1) goto try_left;
    state->blue_stage_force_field_direction = 1;
    state->wd64b = 1;
    state->blue_stage_force_field_gfx_needs_loading = 1;
    goto set_indicators;

try_left:
    if (state->wd644) {
        if (state->map_move_direction == 0) goto try_flip_up;
    } else if (!state->wd643) {
        if (state->left_alley_count != 3) goto try_flip_up;
        if (state->in_special_mode) goto try_flip_up;
    }

    if (state->wd64b == 3) goto try_right;  /* Would bounce — go right instead */
    state->blue_stage_force_field_direction = 3;
    state->wd64b = 3;
    state->blue_stage_force_field_gfx_needs_loading = 1;
    goto set_indicators;

try_flip_up:
    if (state->blue_stage_force_field_flipped_down) {
        state->blue_stage_force_field_direction = 2; /* down */
        state->blue_stage_force_field_gfx_needs_loading = 1;
        return;
    }
    state->blue_stage_force_field_direction = 0; /* up */
    state->blue_stage_force_field_gfx_needs_loading = 1;
    state->wd64a = 1;

set_indicators:
    if (state->blue_stage_force_field_direction == 0)
        state->wd64a = 1;
    else if (state->blue_stage_force_field_direction == 1)
        state->wd649 = 1;
    else if (state->blue_stage_force_field_direction == 3)
        state->wd648 = 1;
}

/*=============================================================================
 * UpdateForceFieldGraphics (0x1f18a)
 *===========================================================================*/
static void update_force_field_graphics(GameState *state) {
    /* UpdateForceFieldGraphics (0x1f18a): write arrow tiles to bg_map per direction.
     * GBC tile data from TileDataPointers_1f201 (force_field.asm).
     * 3 rows × 2 tiles at bg_map offsets $189, $1a9, $1c9. (#90) */
    if (!state->blue_stage_force_field_gfx_needs_loading) return;
    if (!state->vram) {
        state->blue_stage_force_field_gfx_needs_loading = 0;
        return;
    }

    /* GBC tile IDs per direction (from TileDataPointers_1f201):
     * Dir 0: 6C 6D / 6E 6F / 70 71
     * Dir 1: 72 80 / 73 74 / 75 80
     * Dir 2: 76 77 / 78 79 / 7A 7B
     * Dir 3: 80 7C / 7D 7E / 80 7F */
    static const uint8_t force_field_tiles[4][6] = {
        { 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71 },  /* direction 0 */
        { 0x72, 0x80, 0x73, 0x74, 0x75, 0x80 },  /* direction 1 */
        { 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B },  /* direction 2 */
        { 0x80, 0x7C, 0x7D, 0x7E, 0x80, 0x7F },  /* direction 3 */
    };

    uint8_t dir = state->blue_stage_force_field_direction;
    if (dir > 3) dir = 0;

    const uint8_t *tiles = force_field_tiles[dir];
    /* vBGMap + $189 = bg_map offset 0x189 (row 12, col 9) */
    state->vram->bg_map[0][0x189] = tiles[0];
    state->vram->bg_map[0][0x18A] = tiles[1];
    /* vBGMap + $1a9 (row 13, col 9) */
    state->vram->bg_map[0][0x1A9] = tiles[2];
    state->vram->bg_map[0][0x1AA] = tiles[3];
    /* vBGMap + $1c9 (row 14, col 9) */
    state->vram->bg_map[0][0x1C9] = tiles[4];
    state->vram->bg_map[0][0x1CA] = tiles[5];

    state->blue_stage_force_field_gfx_needs_loading = 0;
}

/*=============================================================================
 * ResolveForceFieldCollision (0x1ef09)
 * Applies directional force from BallPhysicsData_ec000 ROM table.
 * Four direction handlers compute offset differently, then read 4-byte
 * signed force vectors and apply them to ball velocity.
 *
 * Force field area: X in [0x38..0x68), Y in [0x60..0x90) — 48×48 grid.
 * Table offset = primary_axis * 192 + secondary_axis * 4
 *===========================================================================*/
static void apply_force_field_rumble(GameState *state, int16_t force_a, int16_t force_b) {
    /* ASM: abs both components, add, shift left 1, check high byte >= 2 */
    int16_t abs_a = (force_a < 0) ? -force_a : force_a;
    int16_t abs_b = (force_b < 0) ? -force_b : force_b;
    uint16_t magnitude = (uint16_t)(abs_a + abs_b) << 1;
    if ((magnitude >> 8) < 2) return;
    if (state->rumble_duration) return;
    state->rumble_pattern = 0x05;
    state->rumble_duration = 0x08;
}

static void resolve_force_field_collision(GameState *state) {
    ensure_force_field_data_loaded(state);
    if (!force_field_data) return;

    uint8_t dir = state->blue_stage_force_field_direction;
    uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t ball_y = (uint8_t)(state->ball_y_pos >> 8);

    uint8_t dy = ball_y - 0x60;
    uint8_t dx = ball_x - 0x38;
    uint16_t offset;

    switch (dir) {
    case 0: /* Up — primary=dy, secondary=dx */
        if (dy >= 0x30) return;
        if (dx >= 0x30) return;
        offset = (uint16_t)dy * 192 + (uint16_t)dx * 4;
        break;
    case 1: /* Right — primary=(0x30-dx), secondary=dy, axes swapped */
        if (dx >= 0x30) return;
        if (dy >= 0x30) return;
        offset = (uint16_t)(0x30 - dx) * 192 + (uint16_t)dy * 4;
        break;
    case 2: /* Down — primary=(0x30-dy), secondary=dx */
        if (dy >= 0x30) return;
        if (dx >= 0x30) return;
        offset = (uint16_t)(0x30 - dy) * 192 + (uint16_t)dx * 4;
        break;
    case 3: /* Left — primary=dx, secondary=(0x30-dy) */
        if (dx >= 0x30) return;
        if (dy >= 0x30) return;
        offset = (uint16_t)dx * 192 + (uint16_t)(0x30 - dy) * 4;
        break;
    default:
        return;
    }

    if (offset + 4 > force_field_data_size) return;
    const uint8_t *entry = &force_field_data[offset];
    int16_t f0 = (int16_t)((uint16_t)entry[0] | ((uint16_t)entry[1] << 8));
    int16_t f1 = (int16_t)((uint16_t)entry[2] | ((uint16_t)entry[3] << 8));

    switch (dir) {
    case 0: /* Up: add f0→Xvel, add f1→Yvel */
        state->ball_x_velocity = (int16_t)((uint16_t)state->ball_x_velocity + (uint16_t)f0);
        state->ball_y_velocity = (int16_t)((uint16_t)state->ball_y_velocity + (uint16_t)f1);
        apply_force_field_rumble(state, f0, f1);
        break;
    case 1: /* Right: add f0→Yvel, sub f1 from Xvel */
        state->ball_y_velocity = (int16_t)((uint16_t)state->ball_y_velocity + (uint16_t)f0);
        state->ball_x_velocity = (int16_t)((uint16_t)state->ball_x_velocity - (uint16_t)f1);
        apply_force_field_rumble(state, f0, f1);
        break;
    case 2: /* Down: add f0→Xvel, sub f1 from Yvel */
        /* ASM: bit 2, l; ret nz — alignment check on low byte of offset */
        if (offset & 0x04) return;
        state->ball_x_velocity = (int16_t)((uint16_t)state->ball_x_velocity + (uint16_t)f0);
        state->ball_y_velocity = (int16_t)((uint16_t)state->ball_y_velocity - (uint16_t)f1);
        apply_force_field_rumble(state, f0, f1);
        break;
    case 3: /* Left: sub f0 from Yvel, add f1→Xvel */
        state->ball_y_velocity = (int16_t)((uint16_t)state->ball_y_velocity - (uint16_t)f0);
        state->ball_x_velocity = (int16_t)((uint16_t)state->ball_x_velocity + (uint16_t)f1);
        apply_force_field_rumble(state, f0, f1);
        break;
    }
}

/*=============================================================================
 * ApplySlotForceField_BlueFieldTop (0x1ea3b) / Bottom (0x1ea0a)
 * Uses BallPhysicsData_f0000 ROM table (same as red field slot force field).
 * Top: Y range [0x86..0xB6), Bottom: Y range [0xFE..0x2E) (wrapping 8-bit)
 * Both: X range [0x38..0x68)
 * Offset = dy * 192 + dx * 4, same Up-direction pattern as directional field.
 * Force: add f0→Xvel, add f1→Yvel, rumble+SFX on threshold.
 *===========================================================================*/
static void apply_slot_force_field_impl(GameState *state, uint8_t ref_y) {
    if (!state->slot_is_open) return;

    /* Load slot force field data lazily (shared with red field) */
    if (!state->slot_force_field_data) {
        char path[260];
        snprintf(path, sizeof(path), "%s/data/collision/ball_physics_f0000.bin",
                 state->asset_base_path);
        state->slot_force_field_data = load_binary_file(path,
                                                         &state->slot_force_field_data_size);
        if (!state->slot_force_field_data) return;
    }

    uint8_t ball_y = (uint8_t)(state->ball_y_pos >> 8);
    uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t dy = ball_y - ref_y;
    if (dy >= 0x30) return;
    uint8_t dx = ball_x - 0x38;
    if (dx >= 0x30) return;

    uint16_t offset = (uint16_t)dy * 192 + (uint16_t)dx * 4;
    if (offset + 4 > state->slot_force_field_data_size) return;

    const uint8_t *entry = &state->slot_force_field_data[offset];
    int16_t force_vx = (int16_t)((uint16_t)entry[0] | ((uint16_t)entry[1] << 8));
    int16_t force_vy = (int16_t)((uint16_t)entry[2] | ((uint16_t)entry[3] << 8));

    state->ball_x_velocity = (int16_t)((uint16_t)state->ball_x_velocity + (uint16_t)force_vx);
    state->ball_y_velocity = (int16_t)((uint16_t)state->ball_y_velocity + (uint16_t)force_vy);

    int16_t abs_vx = (force_vx < 0) ? -force_vx : force_vx;
    int16_t abs_vy = (force_vy < 0) ? -force_vy : force_vy;
    uint16_t magnitude = (uint16_t)(abs_vx + abs_vy) << 1;
    if ((magnitude >> 8) < 2) return;
    if (state->rumble_duration) return;
    state->rumble_pattern = 0x05;
    state->rumble_duration = 0x08;
    PLAY_SFX(state, "map_move", 0x00, 0x04);
}

static void apply_slot_force_field_top_blue(GameState *state) {
    apply_slot_force_field_impl(state, 0x86);
}

static void apply_slot_force_field_bottom_blue(GameState *state) {
    apply_slot_force_field_impl(state, 0xFE);
}

/*=============================================================================
 * LoadArrowIndicatorGraphics_BlueStage (0x1eb41)
 *
 * Blue field equivalent of load_arrow_indicator_graphics (red field).
 * Uses LoadTileLists-style tile data derived from arrow_indicators.asm.
 *
 * Indicator layout (GBC / TileDataPointers_1ed51):
 *   0: left alley  - 3 tiles at bg_map offsets 0x64, 0x84, 0xA5 (5 states)
 *   1: right alley - 3 tiles at bg_map offsets 0x6F, 0x8F, 0xAE (5 states)
 *   2: left force  - 2 tiles at bg_map offsets 0x48, 0x68 (4 states)
 *   3: right force - 4 tiles at bg_map offsets 0x4B-0x4C, 0x6B-0x6C (4 states)
 *   4: slot cave   - 4 tiles at bg_map offsets 0x49-0x4A, 0x69-0x6A (4 states)
 *===========================================================================*/

/* Indicator 0 (left alley): 5 states, 3 tiles at bg_map 0x64, 0x84, 0xA5 */
static const uint8_t blue_arrow_ind0_tiles[5][3] = {
    {0x31, 0x0D, 0x7C},  /* State 0: off */
    {0x32, 0x0D, 0x7C},  /* State 1: 1 lit */
    {0x32, 0x0E, 0x7C},  /* State 2: 2 lit */
    {0x32, 0x0E, 0x7D},  /* State 3: 3 lit */
    {0x31, 0x0E, 0x7C},  /* State 4: wrap-around */
};
static const uint16_t blue_arrow_ind0_offsets[3] = {0x64, 0x84, 0xA5};

/* Indicator 1 (right alley): 5 states, 3 tiles at bg_map 0x6F, 0x8F, 0xAE */
static const uint8_t blue_arrow_ind1_tiles[5][3] = {
    {0x33, 0x0F, 0x7E},  /* State 0: off */
    {0x34, 0x0F, 0x7E},  /* State 1: 1 lit */
    {0x34, 0x10, 0x7E},  /* State 2: 2 lit */
    {0x34, 0x10, 0x7F},  /* State 3: 3 lit */
    {0x33, 0x10, 0x7E},  /* State 4: wrap-around */
};
static const uint16_t blue_arrow_ind1_offsets[3] = {0x6F, 0x8F, 0xAE};

/* Indicator 2 (left force field): 4 states, 2 tiles at bg_map 0x48, 0x68 */
static const uint8_t blue_arrow_ind2_tiles[4][2] = {
    {0x05, 0x06},  /* State 0 */
    {0x07, 0x08},  /* State 1 */
    {0x09, 0x0A},  /* State 2 */
    {0x0B, 0x0C},  /* State 3 */
};
static const uint16_t blue_arrow_ind2_offsets[2] = {0x48, 0x68};

/* Indicator 3 (right force field): 4 states, 4 tiles at bg_map 0x4B-0x4C, 0x6B-0x6C */
static const uint8_t blue_arrow_ind3_tiles[4][4] = {
    {0x26, 0x27, 0x28, 0x29},  /* State 0 */
    {0x2A, 0x2B, 0x2C, 0x2D},  /* State 1 */
    {0x2E, 0x2F, 0x30, 0x31},  /* State 2 */
    {0x32, 0x33, 0x34, 0x35},  /* State 3 */
};
static const uint16_t blue_arrow_ind3_offsets[4] = {0x4B, 0x4C, 0x6B, 0x6C};

/* Indicator 4 (slot cave): 4 states, 4 tiles at bg_map 0x49-0x4A, 0x69-0x6A */
static const uint8_t blue_arrow_ind4_tiles[4][4] = {
    {0x16, 0x17, 0x18, 0x19},  /* State 0 */
    {0x1A, 0x1B, 0x1C, 0x1D},  /* State 1 */
    {0x1E, 0x1F, 0x20, 0x21},  /* State 2 */
    {0x22, 0x23, 0x24, 0x25},  /* State 3 */
};
static const uint16_t blue_arrow_ind4_offsets[4] = {0x49, 0x4A, 0x69, 0x6A};

static void load_arrow_indicator_graphics_blue(GameState *state, uint8_t indicator, uint8_t gfx_state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    switch (indicator) {
    case 0:
        if (gfx_state > 4) gfx_state = 4;
        for (int i = 0; i < 3; i++)
            bg[blue_arrow_ind0_offsets[i]] = blue_arrow_ind0_tiles[gfx_state][i];
        break;
    case 1:
        if (gfx_state > 4) gfx_state = 4;
        for (int i = 0; i < 3; i++)
            bg[blue_arrow_ind1_offsets[i]] = blue_arrow_ind1_tiles[gfx_state][i];
        break;
    case 2:
        if (gfx_state > 3) gfx_state = 3;
        for (int i = 0; i < 2; i++)
            bg[blue_arrow_ind2_offsets[i]] = blue_arrow_ind2_tiles[gfx_state][i];
        break;
    case 3:
        if (gfx_state > 3) gfx_state = 3;
        for (int i = 0; i < 4; i++)
            bg[blue_arrow_ind3_offsets[i]] = blue_arrow_ind3_tiles[gfx_state][i];
        break;
    case 4:
        if (gfx_state > 3) gfx_state = 3;
        for (int i = 0; i < 4; i++)
            bg[blue_arrow_ind4_offsets[i]] = blue_arrow_ind4_tiles[gfx_state][i];
        break;
    }
}

/*=============================================================================
 * UpdateArrowIndicators_BlueField (0x1ead4)
 *
 * Updates the 5 blinking arrow indicators in the blue field bottom.
 * Two-pass structure matching ASM:
 *   Pass 1 (indicators 0-1): toggle bit 7 visibility via hFrameCounter bit 4,
 *           call LoadArrowIndicatorGraphics_BlueStage with counter +/- 1.
 *   Pass 2 (indicators 2-4): only on bottom stage (bit 0 set).
 *           Same toggle, but gfx_state offset by wd648/wd649/wd64a values.
 *           If indicator is not blinking (bit 7 clear), use gfx_state=0 + wd64x.
 *===========================================================================*/
static void update_arrow_indicators_blue(GameState *state) {
    /* Only update every 16 frames */
    if ((state->hram.frame_counter & 0x0F) != 0) return;

    /* Pass 1: Indicators 0-1 (left/right alley arrows) */
    for (uint8_t c = 0; c < 2; c++) {
        uint8_t ind = state->indicator_states[c];
        if (ind == 1) continue;       /* ASM: cp $1 / jr z, skip */
        if (!(ind & 0x80)) continue;   /* ASM: bit 7, [hl] / jr z, skip */
        uint8_t gfx = ind & 0x7F;
        if (state->hram.frame_counter & 0x10)
            gfx++;
        load_arrow_indicator_graphics_blue(state, c, gfx);
    }

    /* Pass 2: Indicators 2-4 (force field / slot cave arrows)
     * Only on bottom stage (stage ID bit 0 set) */
    if (!(state->current_stage & 1)) return;

    for (uint8_t c = 2; c < 5; c++) {
        uint8_t gfx;
        uint8_t ind = state->indicator_states[c];
        if (ind == 1 || !(ind & 0x80)) {
            /* Not blinking: use base gfx_state 0 */
            gfx = 0;
        } else {
            /* Blinking: toggle between counter and counter+2 */
            gfx = ind & 0x7F;
            if (state->hram.frame_counter & 0x10)
                gfx += 2;
        }
        /* Add wd648/wd649/wd64a offset.
         * ASM: hl = wd648 + (c - 2), i.e. wd648 for c=2, wd649 for c=3, wd64a for c=4 */
        uint8_t d = 0;
        switch (c) {
        case 2: d = state->wd648; break;
        case 3: d = state->wd649; break;
        case 4: d = state->wd64a; break;
        }
        gfx += d;
        load_arrow_indicator_graphics_blue(state, c, gfx);
    }
}

/*=============================================================================
 * UpdateMapMoveCounters (top/bottom)
 *===========================================================================*/
static void update_map_move_counters_top_blue(GameState *state) {
    /* Decrement left counter timer */
    if (state->left_map_move_counter_frames_until_decrease > 0) {
        state->left_map_move_counter_frames_until_decrease--;
        if (state->left_map_move_counter_frames_until_decrease == 0) {
            state->left_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;
            if (state->left_map_move_counter > 0 && state->left_map_move_counter < 3) {
                state->left_map_move_counter--;
            }
        }
    }
    /* Decrement right counter timer */
    if (state->right_map_move_counter_frames_until_decrease > 0) {
        state->right_map_move_counter_frames_until_decrease--;
        if (state->right_map_move_counter_frames_until_decrease == 0) {
            state->right_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;
            if (state->right_map_move_counter > 0 && state->right_map_move_counter < 3) {
                state->right_map_move_counter--;
            }
        }
    }
}

static void update_map_move_counters_bottom_blue(GameState *state) {
    /* UpdateMapMoveCounters_BlueFieldBottom (0x1de93)
     * Same logic as top but also loads number graphics on counter decrease. */

    /* Left counter */
    if (state->left_map_move_counter_frames_until_decrease > 0) {
        state->left_map_move_counter_frames_until_decrease--;
        if (state->left_map_move_counter_frames_until_decrease == 0) {
            state->left_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;
            if (state->left_map_move_counter > 0 && state->left_map_move_counter < 3) {
                state->left_map_move_counter--;
                /* #20: ASM calls LoadPsyduckOrPoliwagNumberGraphics on decrease */
                load_psyduck_or_poliwag_number_graphics_blue(state,
                    state->left_map_move_counter);
                /* GBC: additional number gfx (palette-specific offset) */
                if (state->left_map_move_counter > 0) {
                    load_psyduck_or_poliwag_number_graphics_blue(state,
                        (uint8_t)(state->left_map_move_counter + 7));
                } else {
                    load_psyduck_or_poliwag_number_graphics_blue(state, 0);
                }
            }
        }
    }

    /* Right counter */
    if (state->right_map_move_counter_frames_until_decrease > 0) {
        state->right_map_move_counter_frames_until_decrease--;
        if (state->right_map_move_counter_frames_until_decrease == 0) {
            state->right_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;
            if (state->right_map_move_counter > 0 && state->right_map_move_counter < 3) {
                state->right_map_move_counter--;
                /* #20: ASM calls LoadPsyduckOrPoliwagNumberGraphics on decrease */
                load_psyduck_or_poliwag_number_graphics_blue(state,
                    (uint8_t)(state->right_map_move_counter + 4));
                /* GBC: additional number gfx (palette-specific offset) */
                if (state->right_map_move_counter > 0) {
                    load_psyduck_or_poliwag_number_graphics_blue(state,
                        (uint8_t)(state->right_map_move_counter + 0x0A));
                } else {
                    load_psyduck_or_poliwag_number_graphics_blue(state, 4);
                }
            }
        }
    }
}

/*=============================================================================
 * UpdatePokeballs/BlinkingPokeballs
 *===========================================================================*/
static void update_blinking_pokeballs_blue(GameState *state) {
    if (state->previous_num_pokeballs == state->num_pokeballs) return;
    if (state->pokeball_blinking_counter > 0) {
        state->pokeball_blinking_counter--;
        if (state->pokeball_blinking_counter == 0) {
            state->previous_num_pokeballs = state->num_pokeballs;
            if (state->num_pokeballs >= 3) {
                state->opened_slot_by_pokeballs = 1;
                state->frames_until_slot_cave_opens = 3;
            }
        }
    }
}

static void update_pokeballs_blue(GameState *state) {
    update_blinking_pokeballs_blue(state);
}

/*=============================================================================
 * Railing indicator tile writes (GBC pointer table indices $28-$2B)
 *
 * The ASM's BonusMultiplierRailingTileDataPointers_1d97a GBC table has 44 entries.
 * Indices 0-39 handle digit tile data, indices 40-43 ($28-$2B) handle the
 * railing end indicator tiles. Each indicator writes 5 tiles to bg_map.
 *
 * indicator 0 ($28): unlit left  -> bg_map $24-$26 = {$25,$26,$27}, $44-$45 = {$28,$29}
 * indicator 1 ($29): lit left    -> bg_map $24-$26 = {$2A,$2B,$2C}, $44-$45 = {$2D,$2E}
 * indicator 2 ($2A): unlit right -> bg_map $2D-$2F = {$27,$26,$2F}, $4E-$4F = {$29,$28}
 * indicator 3 ($2B): lit right   -> bg_map $2D-$2F = {$2C,$2B,$30}, $4E-$4F = {$2E,$2D}
 *===========================================================================*/
static void load_bonus_mult_railing_indicator_blue(GameState *state, uint8_t indicator) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    switch (indicator) {
    case 0: /* Unlit left (GBC $28) */
        bg[0x24] = 0x25; bg[0x25] = 0x26; bg[0x26] = 0x27;
        bg[0x44] = 0x28; bg[0x45] = 0x29;
        break;
    case 1: /* Lit left (GBC $29) */
        bg[0x24] = 0x2A; bg[0x25] = 0x2B; bg[0x26] = 0x2C;
        bg[0x44] = 0x2D; bg[0x45] = 0x2E;
        break;
    case 2: /* Unlit right (GBC $2A) */
        bg[0x2D] = 0x27; bg[0x2E] = 0x26; bg[0x2F] = 0x2F;
        bg[0x4E] = 0x29; bg[0x4F] = 0x28;
        break;
    case 3: /* Lit right (GBC $2B) */
        bg[0x2D] = 0x2C; bg[0x2E] = 0x2B; bg[0x2F] = 0x30;
        bg[0x4E] = 0x2E; bg[0x4F] = 0x2D;
        break;
    }
}

/*=============================================================================
 * Blue Field Bonus Multiplier Digit Graphics (GBC)
 *
 * _LoadBonusMultiplierRailingGraphics_BlueField (0x1d5f2)
 * LoadBonusMultiplierRailingGraphics_BlueField_GameboyColor (0x1d645)
 * Blue field uses single-tile digits at bg_map[0x04] (tens) and [0x0F] (ones).
 * Tile IDs: tens lit 0x58+d, unlit 0x62+d; ones lit 0x6C+d, unlit 0x76+d.
 * Bit 7 of input = lit state (GBC: res 7 then add $0A for pointer table offset).
 *===========================================================================*/
static void load_bonus_mult_railing_gfx_blue(GameState *state, uint8_t a) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    uint8_t lit = (a & 0x80) ? 1 : 0;
    a &= 0x7F;

    if (a < 0x14) {
        /* Tens digit at bg_map[0x04] */
        uint8_t digit = a;
        if (digit > 9) digit = 9;
        bg[0x04] = lit ? (uint8_t)(0x58 + digit) : (uint8_t)(0x62 + digit);
    } else {
        /* Ones digit at bg_map[0x0F] */
        uint8_t digit = a - 0x14;
        if (digit > 9) digit = 9;
        bg[0x0F] = lit ? (uint8_t)(0x6C + digit) : (uint8_t)(0x76 + digit);
    }
}

/* LoadBonusMultiplierRailingGraphics_BlueField (0x1c21e)
 * Wrapper that loads both tens and ones digits. */
static void load_bonus_multiplier_railing_graphics_blue(GameState *state) {
    load_bonus_mult_railing_gfx_blue(state, state->bonus_multiplier_tens_digit);
    load_bonus_mult_railing_gfx_blue(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
}

/*=============================================================================
 * ShowBonusMultiplierMessage_BlueField
 * Shows "BONUS MULTIPLIER xNN" scrolling text when flag is set.
 *===========================================================================*/
static const uint8_t BONUS_MULT_TEXT_HEADER_BLUE[6] = { 5, 0x54, 0x40, 20, 0x00, 61 };

static void show_bonus_multiplier_message_blue(GameState *state) {
    if (state->bottom_text_enabled)
        return;
    if (!state->show_bonus_multiplier_bottom_message)
        return;
    state->show_bonus_multiplier_bottom_message = 0;

    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    load_scrolling_text(state, 0, BONUS_MULT_TEXT_HEADER_BLUE,
                        "BONUS MULTIPLIER x0  ");

    /* Write actual digits at buffer position $12 (18).
     * Tens digit: leading-zero suppression (skip if 0). */
    uint8_t tens = state->wd614 & 0x7F;
    uint8_t ones = state->wd615 & 0x7F;
    if (tens != 0) {
        state->bottom_message_text[18] = (uint8_t)(0x86 + tens);
        state->bottom_message_text[19] = (uint8_t)(0x86 + ones);
    } else {
        state->bottom_message_text[18] = (uint8_t)(0x86 + ones);
    }
}

/*=============================================================================
 * UpdateBonusMultiplierRailing_BlueField (0x1d4b7)
 * Per-frame handler: timer countdown, blinking animation, digit reload.
 *===========================================================================*/
static void update_bonus_multiplier_railing_blue(GameState *state) {
    show_bonus_multiplier_message_blue(state);

    /* Timer countdown from wd612 */
    if (state->wd612) {
        state->wd612--;
        if (state->wd612 == 0x70) {
            /* Transition to fast blink state */
            state->wd610 = 2;
            state->wd611 = 2;
        } else if (state->wd612 == 0) {
            /* Timer expired: return to idle, recalculate BCD digits */
            state->wd610 = 3;
            state->wd611 = 0;
            get_bcd_for_next_bonus_multiplier_blue(state);
            load_bonus_mult_railing_gfx_blue(state, state->bonus_multiplier_tens_digit);
            load_bonus_mult_railing_gfx_blue(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
            return;
        }
    }

    /* Tens digit blinking (wd610 >= 2) */
    if (state->wd610 >= 2) {
        uint8_t b = state->hram.frame_counter;
        if (state->wd610 >= 3) {
            b >>= 2;  /* slower blink */
        }
        if ((b & 3) == 0) {
            if (b & 8) {
                state->bonus_multiplier_tens_digit |= 0x80;
            } else {
                state->bonus_multiplier_tens_digit &= 0x7F;
            }
            load_bonus_mult_railing_gfx_blue(state, state->bonus_multiplier_tens_digit);
        }
    }

    /* Ones digit blinking (wd611 >= 2) */
    if (state->wd611 >= 2) {
        uint8_t b = state->hram.frame_counter;
        if (state->wd611 >= 3) {
            b >>= 2;  /* slower blink */
        }
        if ((b & 3) == 0) {
            if (b & 8) {
                state->bonus_multiplier_ones_digit |= 0x80;
            } else {
                state->bonus_multiplier_ones_digit &= 0x7F;
            }
            load_bonus_mult_railing_gfx_blue(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
        }
    }
}

/*=============================================================================
 * LoadBillboardGraphics_BlueField (0x1c4b6)
 *
 * Decides which billboard picture to show and loads its tile data.
 * Called from _LoadStageDataBlueFieldBottom on every top→bottom transition.
 * Same logic as LoadBillboardGraphics_RedField.
 *===========================================================================*/
static void load_billboard_graphics_blue_field(GameState *state) {
    /* If slot roulette is active, restore the roulette's billboard state
     * instead of loading the map/slot billboard. Stage transition would
     * otherwise overwrite palette 6 with the map's colors. */
    if (state->slot_roulette_active) {
        uint8_t pic = state->slot_roulette_billboard_picture;
        if (state->slot_roulette_state >= SLOT_ROULETTE_STOPPED_BLINK) {
            load_billboard_picture(state, pic);
            load_grey_billboard_palette(state);  /* attrs → all palette 6 */
            load_colored_billboard_palette(state, pic); /* palette 6 → colored */
        } else {
            load_billboard_off_picture(state, pic);
            load_grey_billboard_palette(state);  /* attrs + palette 6 → grey */
        }
        return;
    }

    if (!state->in_special_mode) {
        /* Not in special mode: check pokeballs / CAVE / default map */
        if (state->opened_slot_by_pokeballs) {
            load_billboard_tile_data(state,
                (uint8_t)(state->next_bonus_stage + 0x15));
        } else if (state->opened_slot_by_cave_lights) {
            load_billboard_tile_data(state, 0x1A);
        } else {
            load_map_billboard_tile_data(state);
        }
        return;
    }

    /* In special mode: only handle map move (mode 2) */
    if (state->special_mode != SPECIAL_MODE_MAP_MOVE)
        return;

    if (state->special_mode_state == 3) {
        load_map_billboard_tile_data(state);
    } else if (state->slot_is_open) {
        load_billboard_tile_data(state, 0x14); /* GO TO NEXT */
    } else {
        load_billboard_tile_data(state,
            (uint8_t)(state->map_move_direction + 0x12)); /* HURRY UP */
    }
}

/*=============================================================================
 * ClearAllBlueIndicators
 * Blue field equivalent of ClearAllRedIndicators (Func_1c2cb).
 * Clears bit 7 (blink flag) of each indicator state.
 *===========================================================================*/
void clear_all_blue_indicators(GameState *state) {
    for (uint8_t i = 0; i < 5; i++) {
        state->indicator_states[i] &= 0x7F;
    }
}

/*=============================================================================
 * Stage Data Loading
 *===========================================================================*/
void load_stage_data_blue_field_top(GameState *state) {
    /* _LoadStageDataBlueFieldTop (0x1c165)
     * Full call list from ASM:
     *   1. LoadPinballUpgradeTriggersGraphics_BlueField
     *   2. UpdateSpinnerChargeGraphics_BlueField
     *   3. LoadEvolutionTrinketGraphics_BlueField
     *   4. LoadSlotCaveCoverGraphics_BlueField
     *   5. LoadBallGraphics
     *   6. Set force field gfx flag + UpdateForceFieldGraphics
     *   7. LoadTimerGraphics (NOP on GBC)
     *   8. Func_1c203
     */

    /* 1. LoadPinballUpgradeTriggersGraphics_BlueField */
    load_upgrade_triggers_graphics_blue(state);

    /* #22: 2. UpdateSpinnerChargeGraphics — reload lightning bolt tiles */
    update_spinner_charge_graphics_blue(state);

    /* 3. LoadEvolutionTrinketGraphics */
    load_evolution_trinket_graphics(state);

    /* 4. LoadSlotCaveCoverGraphics */
    load_slot_cave_cover_graphics_blue(state);

    /* #22: 5. LoadBallGraphics — reload ball tiles by type/size */
    switch (state->ball_size) {
    case 1:  load_mini_ball_gfx(state); break;
    case 2:  load_super_mini_ball_gfx(state); break;
    default: load_ball_gfx(state); break;
    }

    /* 6. Force field graphics */
    state->blue_stage_force_field_gfx_needs_loading = 1;
    update_force_field_graphics(state);

    /* #22: 7. LoadTimerGraphics — NOP on GBC (ret nz after hGameBoyColorFlag) */

    /* 8. Func_1c203: reset animation states */
    state->which_animated_shellder = 0xFF;
    state->which_bumper_gfx = 0xFF;
}

void load_stage_data_blue_field_bottom(GameState *state) {
    /* _LoadStageDataBlueFieldBottom (0x1c191)
     * Full call list from ASM:
     *   1. Func_1c1db (force field check)
     *   2. LoadBillboardGraphics_BlueField
     *   3. Func_1c2cb (arrow indicators)
     *   4. LoadCAVELightsGraphics_BlueField
     *   5. LoadBillboardStatusBarGraphics_BlueField
     *   6. Func_1c305 (special mode gfx)
     *   7. LoadEvolutionTrinketGraphics_BlueField
     *   8. LoadAgainTextGraphics
     *   9. DrawBallSaverIcon
     *  10. LoadPsyduckOrPoliwagGraphics
     *  11. LoadBonusMultiplierRailingGraphics_BlueField
     *  12. LoadSlotCaveCoverGraphics_BlueField
     *  13. LoadBallGraphics
     *  14. LoadTimerGraphics (NOP on GBC)
     *  15. Func_1c203
     */

    /* 1. Force field state check on transition to bottom */
    if (state->blue_stage_force_field_flipped_down) {
        state->blue_stage_force_field_gfx_needs_loading = 1;
        if (state->blue_stage_force_field_direction == 2) {
            state->blue_stage_force_field_direction = 0;
            state->wd64a = 1;
            state->wd649 = 0;
            state->wd648 = 0;
        }
        state->blue_stage_force_field_flipped_down = 0;
    }

    /* 2. Billboard — critical for HURRY UP / map / bonus stage display */
    load_billboard_graphics_blue_field(state);

    /* 3. Arrow indicators — clear blink flags */
    clear_all_blue_indicators(state);

    /* 4. CAVE lights — load current light states into bg_map */
    load_cave_lights_graphics_blue(state);

    /* 5. Billboard status bar — pokeball indicators (shared) */
    load_billboard_status_bar_graphics(state);

    /* 6. Special mode gfx — catch'em/evolution sprite management (shared) */
    func_1414b_special_mode_gfx(state);

    /* 7. Evolution trinket tiles (shared) */
    load_evolution_trinket_graphics(state);

    /* 8. AGAIN text — on/off tile swap (shared, stage-aware) */
    load_again_text_graphics(state);

    /* 9. Ball saver icon — on/off tiles at bg_map (shared) */
    draw_ball_saver_icon(state);

    /* 10. Psyduck/Poliwag graphics (LoadPsyduckOrPoliwagGraphics 0x1c235).
     * #93: Full reload of psyduck/poliwag tile data + number graphics.
     * This matches the ASM which calls the full LoadPsyduckOrPoliwagGraphics. */
    {
        /* Poliwag side */
        if (state->left_map_move_poliwag_anim_counter > 0) {
            /* Poliwag is animating — load collision map entries + hit gfx */
            if (state->current_stage & 1) {
                state->stage_collision_map[0xE3] = 0x54;
                state->stage_collision_map[0x103] = 0x55;
            }
            load_psyduck_or_poliwag_graphics_blue(state, 1);
        } else {
            load_psyduck_or_poliwag_graphics_blue(state, 0);
        }
        load_psyduck_or_poliwag_number_graphics_blue(state,
            state->left_map_move_counter);
        /* GBC: color-specific number gfx */
        if (state->left_map_move_counter > 0) {
            load_psyduck_or_poliwag_number_graphics_blue(state,
                (uint8_t)(state->left_map_move_counter + 7));
        } else {
            load_psyduck_or_poliwag_number_graphics_blue(state, 0);
        }

        /* Psyduck side */
        if (state->right_map_move_psyduck_frame > 0) {
            if (state->current_stage & 1) {
                state->stage_collision_map[0xF0] = 0x52;
                state->stage_collision_map[0x110] = 0x53;
            }
            if (state->wd644 && state->map_move_direction == 0) {
                load_psyduck_or_poliwag_graphics_blue(state, 3);
            } else if (state->wd644) {
                load_psyduck_or_poliwag_graphics_blue(state, 6);
            } else {
                load_psyduck_or_poliwag_graphics_blue(state,
                    (uint8_t)(state->right_map_move_counter + 3));
            }
        } else {
            load_psyduck_or_poliwag_graphics_blue(state, 2);
        }
        load_psyduck_or_poliwag_number_graphics_blue(state,
            (uint8_t)(state->right_map_move_counter + 4));
        /* GBC: color-specific number gfx */
        if (state->right_map_move_counter > 0) {
            load_psyduck_or_poliwag_number_graphics_blue(state,
                (uint8_t)(state->right_map_move_counter + 0x0A));
        } else {
            load_psyduck_or_poliwag_number_graphics_blue(state, 4);
        }
    }

    /* 11. Bonus multiplier railing digits */
    load_bonus_multiplier_railing_graphics_blue(state);

    /* 12. Slot cave cover tiles (shared, stage-aware) */
    load_slot_cave_cover_graphics_blue(state);

    /* 13. Ball graphics */
    switch (state->ball_size) {
    case 1:  load_mini_ball_gfx(state); break;
    case 2:  load_super_mini_ball_gfx(state); break;
    default: load_ball_gfx(state); break;
    }

    /* 14. LoadTimerGraphics — NOP on GBC */

    state->which_animated_shellder = 0xFF;
    state->which_bumper_gfx = 0xFF;
}

void load_blue_field_bottom_graphics(GameState *state) {
    /* Called during stage transition reload to restore bottom-stage tiles */
    load_stage_data_blue_field_bottom(state);
}

/*=============================================================================
 * _LoadPsyduckOrPoliwagGraphics (0x1de4b) -- GBC version
 * Writes bg_map tile IDs for Poliwag (IDs 0-1) and Psyduck (IDs 2-6).
 * Only active on bottom stage (bit 0 of current_stage set).
 * Data from TileDataPointers_1e00f in poliwag_psyduck.asm.
 *===========================================================================*/
static void load_psyduck_or_poliwag_graphics_blue(GameState *state, uint8_t id) {
    /* Only on bottom stage */
    if (!(state->current_stage & 1)) return;
    if (!state->vram || id > 6) return;

    uint8_t *bg = state->vram->bg_map[0];

    switch (id) {
    case 0: /* Poliwag default */
        bg[0xA3] = 0x35;
        bg[0xC3] = 0x36;
        break;
    case 1: /* Poliwag hit */
        bg[0xA3] = 0x37;
        bg[0xC3] = 0x38;
        break;
    case 2: /* Psyduck default */
        bg[0x90] = 0x4F;
        bg[0xAF] = 0x50; bg[0xB0] = 0x51;
        bg[0xCF] = 0x52; bg[0xD0] = 0x53;
        break;
    case 3: /* Psyduck hit */
        bg[0x90] = 0x54;
        bg[0xAF] = 0x55; bg[0xB0] = 0x56;
        bg[0xCF] = 0x57; bg[0xD0] = 0x58;
        break;
    case 4: /* Psyduck variant */
        bg[0x90] = 0x59;
        bg[0xAF] = 0x5A; bg[0xB0] = 0x5B;
        bg[0xCF] = 0x5C; bg[0xD0] = 0x5D;
        break;
    case 5: /* Psyduck variant (map move direction) */
        bg[0x90] = 0x59;
        bg[0xAF] = 0x5A; bg[0xB0] = 0x5E;
        bg[0xCF] = 0x5C; bg[0xD0] = 0x5F;
        break;
    case 6: /* Psyduck variant (map move active) */
        bg[0x90] = 0x60;
        bg[0xAF] = 0x61; bg[0xB0] = 0x62;
        bg[0xCF] = 0x63; bg[0xD0] = 0x64;
        break;
    }
}

/*=============================================================================
 * LoadPsyduckOrPoliwagNumberGraphics (0x1de6f) -- GBC version
 * Writes bg_map tile IDs for the map move counter numbers next to
 * Poliwag (IDs 0-3, 8-10) and Psyduck (IDs 4-7, 11-13).
 * Only active on bottom stage (bit 0 of current_stage set).
 * Data from TileDataPointers_1e1d6 in poliwag_psyduck.asm.
 *===========================================================================*/
static void load_psyduck_or_poliwag_number_graphics_blue(GameState *state, uint8_t id) {
    /* Only on bottom stage */
    if (!(state->current_stage & 1)) return;
    if (!state->vram || id > 13) return;

    uint8_t *bg = state->vram->bg_map[0];

    switch (id) {
    case 0: /* Poliwag counter: 4 rows x 3 cols at $60/$80/$A0/$C0 */
        bg[0x60] = 0x36; bg[0x61] = 0x37; bg[0x62] = 0x38;
        bg[0x80] = 0x39; bg[0x81] = 0x3A; bg[0x82] = 0x3B;
        bg[0xA0] = 0x4C; bg[0xA1] = 0x4D; bg[0xA2] = 0x4E;
        bg[0xC0] = 0x4F; bg[0xC1] = 0x50; bg[0xC2] = 0x51;
        break;
    case 1: /* Poliwag counter variant 1 */
        bg[0x60] = 0x3C; bg[0x61] = 0x37; bg[0x62] = 0x38;
        bg[0x80] = 0x3D; bg[0x81] = 0x3E; bg[0x82] = 0x3B;
        bg[0xA0] = 0x52; bg[0xA1] = 0x53; bg[0xA2] = 0x54;
        bg[0xC0] = 0x55; bg[0xC1] = 0x56; bg[0xC2] = 0x57;
        break;
    case 2: /* Poliwag counter variant 2 */
        bg[0x60] = 0x40; bg[0x61] = 0x41; bg[0x62] = 0x38;
        bg[0x80] = 0x42; bg[0x81] = 0x43; bg[0x82] = 0x3B;
        bg[0xA0] = 0x52; bg[0xA1] = 0x53; bg[0xA2] = 0x54;
        bg[0xC0] = 0x55; bg[0xC1] = 0x56; bg[0xC2] = 0x57;
        break;
    case 3: /* Poliwag counter variant 3 */
        bg[0x60] = 0x36; bg[0x61] = 0x46; bg[0x62] = 0x47;
        bg[0x80] = 0x48; bg[0x81] = 0x49; bg[0x82] = 0x4A;
        bg[0xA0] = 0x52; bg[0xA1] = 0x53; bg[0xA2] = 0x54;
        bg[0xC0] = 0x55; bg[0xC1] = 0x56; bg[0xC2] = 0x57;
        break;
    case 4: /* Psyduck counter: 3 rows x 3 cols at $91/$B1/$D1 */
        bg[0x91] = 0x4D; bg[0x92] = 0x65; bg[0x93] = 0x4E;
        bg[0xB1] = 0x66; bg[0xB2] = 0x67; bg[0xB3] = 0x68;
        bg[0xD1] = 0x69; bg[0xD2] = 0x6A; bg[0xD3] = 0x6B;
        break;
    case 5: /* Psyduck counter variant 1 */
        bg[0x91] = 0x4D; bg[0x92] = 0x6C; bg[0x93] = 0x4E;
        bg[0xB1] = 0x6D; bg[0xB2] = 0x6E; bg[0xB3] = 0x68;
        bg[0xD1] = 0x6F; bg[0xD2] = 0x70; bg[0xD3] = 0x6B;
        break;
    case 6: /* Psyduck counter variant 2 */
        bg[0x91] = 0x4D; bg[0x92] = 0x6C; bg[0x93] = 0x4E;
        bg[0xB1] = 0x66; bg[0xB2] = 0x72; bg[0xB3] = 0x68;
        bg[0xD1] = 0x69; bg[0xD2] = 0x73; bg[0xD3] = 0x6B;
        break;
    case 7: /* Psyduck counter variant 3 */
        bg[0x91] = 0x4D; bg[0x92] = 0x75; bg[0x93] = 0x4E;
        bg[0xB1] = 0x66; bg[0xB2] = 0x76; bg[0xB3] = 0x77;
        bg[0xD1] = 0x69; bg[0xD2] = 0x78; bg[0xD3] = 0x79;
        break;
    case 8: /* Poliwag counter (GBC second call): 3 rows at $80/$A0/$C0 */
        bg[0x80] = 0x3F; bg[0x81] = 0x3A; bg[0x82] = 0x3B;
        bg[0xA0] = 0x4C; bg[0xA1] = 0x4D; bg[0xA2] = 0x4E;
        bg[0xC0] = 0x4F; bg[0xC1] = 0x50; bg[0xC2] = 0x51;
        break;
    case 9: /* Poliwag counter (GBC second call variant) */
        bg[0x80] = 0x44; bg[0x81] = 0x45; bg[0x82] = 0x3B;
        bg[0xA0] = 0x4C; bg[0xA1] = 0x4D; bg[0xA2] = 0x4E;
        bg[0xC0] = 0x4F; bg[0xC1] = 0x50; bg[0xC2] = 0x51;
        break;
    case 10: /* Poliwag counter (GBC second call variant 2) */
        bg[0x80] = 0x39; bg[0x81] = 0x4B; bg[0x82] = 0x4A;
        bg[0xA0] = 0x4C; bg[0xA1] = 0x4D; bg[0xA2] = 0x4E;
        bg[0xC0] = 0x4F; bg[0xC1] = 0x50; bg[0xC2] = 0x51;
        break;
    case 11: /* Psyduck counter (GBC second call): 2 rows at $91/$B1 */
        bg[0x91] = 0x4D; bg[0x92] = 0x65; bg[0x93] = 0x4E;
        bg[0xB1] = 0x6D; bg[0xB2] = 0x71; bg[0xB3] = 0x68;
        break;
    case 12: /* Psyduck counter (GBC second call variant) */
        bg[0x91] = 0x4D; bg[0x92] = 0x65; bg[0x93] = 0x4E;
        bg[0xB1] = 0x66; bg[0xB2] = 0x74; bg[0xB3] = 0x68;
        break;
    case 13: /* Psyduck counter (GBC second call variant 2) */
        bg[0x91] = 0x4D; bg[0x92] = 0x65; bg[0x93] = 0x4E;
        bg[0xB1] = 0x66; bg[0xB2] = 0x67; bg[0xB3] = 0x77;
        break;
    }
}
