/*
 * Save/Load System
 *
 * Translated from home/save.asm.
 * Format: [data bytes][signature "NT"][16-bit checksum][backup copy of all above]
 * The backup copy provides redundancy - if primary fails validation,
 * the backup is tried.
 *
 * Four save blocks (matching sram.asm):
 *   sHighScores  ($82 bytes)  — 10 high scores (5 red + 5 blue)
 *   sPokedexFlags ($98 bytes) — pokemon seen/caught flags
 *   sKeyConfigs  ($0E bytes)  — 7 key config mappings
 *   sSaveGame    ($4C3 bytes) — in-game state for continue
 */

#include "game/save.h"
#include "game/constants.h"
#include "game/score.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compute 16-bit checksum: sum of all bytes (with carry into high byte) */
static uint16_t compute_checksum(const uint8_t *data, size_t length) {
    uint16_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum;
}

/* Validate signature bytes at data[length..length+1] */
static bool validate_signature(const uint8_t *data, size_t length) {
    return data[length] == SAVE_SIGNATURE_BYTE1 &&
           data[length + 1] == SAVE_SIGNATURE_BYTE2;
}

/* Validate checksum at data[length+2..length+3] */
static bool validate_checksum(const uint8_t *data, size_t length) {
    uint16_t computed = compute_checksum(data, length + 2); /* data + signature */
    uint16_t stored = (uint16_t)data[length + 2] | ((uint16_t)data[length + 3] << 8);
    return computed == stored;
}

bool save_data(const uint8_t *data, size_t length, const char *filename) {
    /*
     * SaveData (0xF1A):
     * 1. Copy data
     * 2. Sign (append "NT")
     * 3. Compute checksum (append 2-byte sum)
     * 4. Create backup copy
     */
    size_t record_size = length + 4; /* data + 2 sig + 2 checksum */
    size_t total_size = record_size * 2; /* primary + backup */

    uint8_t *buffer = malloc(total_size);
    if (!buffer) return false;

    /* Write primary copy */
    memcpy(buffer, data, length);

    /* Sign */
    buffer[length] = SAVE_SIGNATURE_BYTE1;
    buffer[length + 1] = SAVE_SIGNATURE_BYTE2;

    /* Checksum over data + signature */
    uint16_t checksum = compute_checksum(buffer, length + 2);
    buffer[length + 2] = checksum & 0xFF;
    buffer[length + 3] = (checksum >> 8) & 0xFF;

    /* Backup copy */
    memcpy(buffer + record_size, buffer, record_size);

    /* Write to file */
    FILE *f = fopen(filename, "wb");
    if (!f) {
        free(buffer);
        return false;
    }

    bool success = fwrite(buffer, 1, total_size, f) == total_size;
    fclose(f);
    free(buffer);
    return success;
}

bool load_saved_data(uint8_t *data, size_t length, const char *filename) {
    /*
     * LoadSavedData (0xF0C):
     * 1. Validate primary: check signature, then checksum
     * 2. If primary fails, try backup copy
     * 3. If both fail, return false
     */
    size_t record_size = length + 4;
    size_t total_size = record_size * 2;

    FILE *f = fopen(filename, "rb");
    if (!f) return false;

    uint8_t *buffer = malloc(total_size);
    if (!buffer) {
        fclose(f);
        return false;
    }

    size_t read_count = fread(buffer, 1, total_size, f);
    fclose(f);

    if (read_count < total_size) {
        free(buffer);
        return false;
    }

    /* Try primary copy */
    if (validate_signature(buffer, length) && validate_checksum(buffer, length)) {
        memcpy(data, buffer, length);
        free(buffer);
        return true;
    }

    /* Try backup copy */
    uint8_t *backup = buffer + record_size;
    if (validate_signature(backup, length) && validate_checksum(backup, length)) {
        memcpy(data, backup, length);
        free(buffer);
        return true;
    }

    free(buffer);
    return false;
}

/*=============================================================================
 * In-game state serialization (sSaveGame equivalent)
 *
 * ASM saves wPartyMons through 0x4C3 bytes of contiguous WRAM.
 * We pack our GameState fields into a fixed-size buffer matching
 * the important fields from that WRAM range.
 *
 * Save buffer layout (SAVE_GAME_SIZE = 0x4C3 = 1219 bytes):
 *   0x000-0x0FF: party_mons[256]
 *   0x100-0x15F: score_queue[96] (transient, zero-filled)
 *   0x160:       num_party_mons
 *   0x161:       cur_selected_party_mon
 *   0x162-0x163: (padding)
 *   0x164-0x169: score_to_add[6] (transient, zero-filled)
 *   0x16A-0x16F: score[6]
 *   0x170-0x172: player_name[3]
 *   0x173-0x176: high_score_id[4]
 *   0x177:       add_score_queue_offset
 *   0x178-0x179: (padding)
 *   0x17A-0x17D: current_jackpot[4]
 *   0x17E:       ball_type
 *   0x17F-0x180: ball_type_counter (LE 16-bit)
 *   0x181:       ball_type_backup
 *   0x182:       cur_bonus_multiplier
 *   0x183-0x18E: (EOBB scratch, zero)
 *   0x18F-0x194: (EOBB scratch, zero)
 *   0x195:       going_to_bonus_stage
 *   0x196:       returning_from_bonus_stage
 *   0x197:       next_stage
 *   0x198:       next_bonus_stage
 *   0x199:       initial_next_bonus_stage
 *   0x19A:       completed_bonus_stage
 *   0x19B:       extra_balls
 *   0x19C:       extra_ball_state
 *   0x19D:       cur_ball_life
 *   0x19E:       num_ball_lives
 *   0x19F-0x1A0: (padding)
 *   0x1A1:       ball_saver_icon_on
 *   0x1A2:       ball_saver_flash_rate
 *   0x1A3:       ball_saver_timer_frames
 *   0x1A4:       ball_saver_timer_seconds
 *   0x1A5:       num_times_ball_saved_text_will_display
 *   0x1A6:       ball_saver_timer_frames_backup
 *   0x1A7:       ball_saver_timer_seconds_backup
 *   0x1A8:       num_times_ball_saved_text_will_display_backup
 *   0x1A9:       extra_ball
 *   0x1AA:       draw_bottom_message_box
 *   0x1AB:       (padding)
 *   0x1AC:       current_stage
 *   0x1AD:       current_stage_backup
 *   0x1AE:       (padding)
 *   0x1AF:       stage_collision_state
 *   0x1B0:       stage_collision_state_backup
 *   0x1B1-0x1B2: (padding)
 *   0x1B3-0x1B4: ball_x_pos (LE)
 *   0x1B5-0x1B6: ball_y_pos (LE)
 *   0x1B7-0x1BA: (prev ball pos, transient)
 *   0x1BB-0x1BC: ball_x_velocity (LE)
 *   0x1BD-0x1BE: ball_y_velocity (LE)
 *   0x1BF-0x1C2: (padding)
 *   0x1C3:       ball_spin
 *   0x1C4:       ball_rotation
 *   0x1C5-0x1C7: (padding)
 *   0x1C8:       ball_size
 *   0x1C9:       lost_ball
 *   0x1CA:       show_extra_ball_text
 *   ... continues with per-object state ...
 * (Fields after 0x1CA are less critical for save/restore and are
 *  re-initialized by stage data loading on continue.)
 *
 * Rather than mapping every byte, we pack critical progression fields
 * and zero-fill the rest. The game reloads stage data on continue.
 *===========================================================================*/

#define SAVE_GAME_SIZE 0x4C3

/* Helper: write a 16-bit LE value to buffer */
static void put16(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

/* Helper: read a 16-bit LE value from buffer */
static uint16_t get16(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void pack_game_state(const GameState *state, uint8_t *buf) {
    memset(buf, 0, SAVE_GAME_SIZE);

    /* wPartyMons: 0x000 */
    memcpy(buf + 0x000, state->party_mons, 256);

    /* wNumPartyMons: 0x160 */
    buf[0x160] = state->num_party_mons;
    buf[0x161] = state->cur_selected_party_mon;

    /* wScore: 0x16A */
    memcpy(buf + 0x16A, state->score, 6);

    /* wPlayerName: 0x170 */
    memcpy(buf + 0x170, state->player_name, 3);
    memcpy(buf + 0x173, state->high_score_id, 4);

    /* wCurrentJackpot: 0x17A */
    memcpy(buf + 0x17A, state->current_jackpot, 4);

    /* wBallType: 0x17E */
    buf[0x17E] = state->ball_type;
    put16(buf + 0x17F, state->ball_type_counter);
    buf[0x181] = state->ball_type_backup;

    /* wCurBonusMultiplier: 0x182 */
    buf[0x182] = state->cur_bonus_multiplier;

    /* wGoingToBonusStage: 0x195 */
    buf[0x195] = state->going_to_bonus_stage;
    buf[0x196] = state->returning_from_bonus_stage;
    buf[0x197] = state->next_stage;
    buf[0x198] = state->next_bonus_stage;
    buf[0x199] = state->initial_next_bonus_stage;
    buf[0x19A] = state->completed_bonus_stage;

    /* wExtraBalls: 0x19B */
    buf[0x19B] = state->extra_balls;
    buf[0x19C] = state->extra_ball_state;
    buf[0x19D] = state->cur_ball_life;
    buf[0x19E] = state->num_ball_lives;

    /* Ball saver state: 0x1A1 */
    buf[0x1A1] = state->ball_saver_icon_on;
    buf[0x1A2] = state->ball_saver_flash_rate;
    buf[0x1A3] = state->ball_saver_timer_frames;
    buf[0x1A4] = state->ball_saver_timer_seconds;
    buf[0x1A5] = state->num_times_ball_saved_text_will_display;
    buf[0x1A6] = state->ball_saver_timer_frames_backup;
    buf[0x1A7] = state->ball_saver_timer_seconds_backup;
    buf[0x1A8] = state->num_times_ball_saved_text_will_display_backup;
    buf[0x1A9] = state->extra_ball;

    /* wCurrentStage: 0x1AC */
    buf[0x1AC] = state->current_stage;
    buf[0x1AD] = state->current_stage_backup;

    /* wStageCollisionState: 0x1AF */
    buf[0x1AF] = state->stage_collision_state;
    buf[0x1B0] = state->stage_collision_state_backup;

    /* Ball position: 0x1B3 */
    put16(buf + 0x1B3, state->ball_x_pos);
    put16(buf + 0x1B5, state->ball_y_pos);

    /* Ball velocity: 0x1BB */
    put16(buf + 0x1BB, (uint16_t)state->ball_x_velocity);
    put16(buf + 0x1BD, (uint16_t)state->ball_y_velocity);

    /* wBallSpin/Rotation: 0x1C3 */
    buf[0x1C3] = (uint8_t)(int8_t)state->ball_spin;
    buf[0x1C4] = state->ball_rotation;
    buf[0x1C8] = state->ball_size;

    /* wChoseInitialMap: 0x1E0 */
    buf[0x1E0] = state->chose_initial_map;
    buf[0x1E1] = state->initial_map_selection_index;
    buf[0x1E2] = state->num_map_moves;
    memcpy(buf + 0x1E3, state->visited_maps, 7);

    /* wIndicatorStates: 0x22F */
    memcpy(buf + 0x22F, state->indicator_states, 19);

    /* wCurrentMap: 0x24A */
    buf[0x24A] = state->current_map;
    buf[0x24B] = state->in_special_mode;
    buf[0x24D] = state->special_mode_state;
    buf[0x250] = state->special_mode;

    /* wMapMoveDirection: 0x25A */
    buf[0x25A] = state->map_move_direction;
    buf[0x25B] = state->rare_mons_flag;

    /* wCAVELightStates: 0x20F */
    memcpy(buf + 0x20F, state->cave_light_states, 4);

    /* wPikachuSaverCharge: 0x217 */
    buf[0x217] = state->pikachu_saver_charge;
    buf[0x218] = state->which_pikachu_saver_side;

    /* wBallUpgradeTriggerStates: 0x2F9 */
    memcpy(buf + 0x2F9, state->ball_upgrade_trigger_states, 3);

    /* wNumPokeballs: 0x300 + custom offset area for fields past the WRAM range */
    /* Use the tail of the buffer (0x400+) for fields not in contiguous WRAM */
    buf[0x400] = state->num_pokeballs;
    buf[0x401] = state->num_pokemon_caught_in_ball_bonus;
    buf[0x402] = state->num_pokemon_evolved_in_ball_bonus;
    buf[0x403] = state->num_bellsprout_entries;
    buf[0x404] = state->num_dugtrio_triples;
    buf[0x405] = state->num_cave_completions;
    buf[0x406] = state->num_spinner_turns;
    buf[0x407] = state->num_pikachu_saves;
    buf[0x408] = state->bonus_multiplier_ones_digit;
    buf[0x409] = state->bonus_multiplier_tens_digit;
    buf[0x40A] = state->red_stage_structure_backup;
    buf[0x40B] = state->left_map_move_counter;   /* Redundant: also at 0x1F0 (ASM writes both WRAM offsets) */
    buf[0x40C] = state->right_map_move_counter; /* Redundant: also at 0x1F2 */
    buf[0x40D] = state->wd644;
    buf[0x40E] = state->num_mewtwo_bonus_completions;

    /* wLeftDiglettAnimationController: 0x1EF */
    buf[0x1EF] = state->left_diglett_anim_controller;
    /* wLeftMapMoveCounter: 0x1F0 */
    buf[0x1F0] = state->left_map_move_counter;
    /* wRightDiglettAnimationController: 0x1F1 */
    buf[0x1F1] = state->right_diglett_anim_controller;
    /* wRightMapMoveCounter: 0x1F2 */
    buf[0x1F2] = state->right_map_move_counter;
    /* wLeftMapMoveCounterFramesUntilDecrease: 0x1F7 (2 bytes) */
    put16(buf + 0x1F7, state->left_map_move_counter_frames_until_decrease);
    /* wRightMapMoveCounterFramesUntilDecrease: 0x1F9 (2 bytes) */
    put16(buf + 0x1F9, state->right_map_move_counter_frames_until_decrease);
}

static void unpack_game_state(GameState *state, const uint8_t *buf) {
    /* wPartyMons: 0x000 */
    memcpy(state->party_mons, buf + 0x000, 256);

    /* wNumPartyMons: 0x160 */
    state->num_party_mons = buf[0x160];
    state->cur_selected_party_mon = buf[0x161];

    /* wScore: 0x16A */
    memcpy(state->score, buf + 0x16A, 6);

    /* wPlayerName: 0x170 */
    memcpy(state->player_name, buf + 0x170, 3);
    memcpy(state->high_score_id, buf + 0x173, 4);

    /* wCurrentJackpot: 0x17A */
    memcpy(state->current_jackpot, buf + 0x17A, 4);

    /* wBallType: 0x17E */
    state->ball_type = buf[0x17E];
    state->ball_type_counter = get16(buf + 0x17F);
    state->ball_type_backup = buf[0x181];

    /* wCurBonusMultiplier: 0x182 */
    state->cur_bonus_multiplier = buf[0x182];

    /* wGoingToBonusStage: 0x195 */
    state->going_to_bonus_stage = buf[0x195];
    state->returning_from_bonus_stage = buf[0x196];
    state->next_stage = buf[0x197];
    state->next_bonus_stage = buf[0x198];
    state->initial_next_bonus_stage = buf[0x199];
    state->completed_bonus_stage = buf[0x19A];

    /* wExtraBalls: 0x19B */
    state->extra_balls = buf[0x19B];
    state->extra_ball_state = buf[0x19C];
    state->cur_ball_life = buf[0x19D];
    state->num_ball_lives = buf[0x19E];

    /* Ball saver: 0x1A1 */
    state->ball_saver_icon_on = buf[0x1A1];
    state->ball_saver_flash_rate = buf[0x1A2];
    state->ball_saver_timer_frames = buf[0x1A3];
    state->ball_saver_timer_seconds = buf[0x1A4];
    state->num_times_ball_saved_text_will_display = buf[0x1A5];
    state->ball_saver_timer_frames_backup = buf[0x1A6];
    state->ball_saver_timer_seconds_backup = buf[0x1A7];
    state->num_times_ball_saved_text_will_display_backup = buf[0x1A8];
    state->extra_ball = buf[0x1A9];

    /* wCurrentStage: 0x1AC */
    state->current_stage = buf[0x1AC];
    state->current_stage_backup = buf[0x1AD];

    /* wStageCollisionState: 0x1AF */
    state->stage_collision_state = buf[0x1AF];
    state->stage_collision_state_backup = buf[0x1B0];

    /* Ball position: 0x1B3 */
    state->ball_x_pos = get16(buf + 0x1B3);
    state->ball_y_pos = get16(buf + 0x1B5);

    /* Ball velocity: 0x1BB */
    state->ball_x_velocity = (int16_t)get16(buf + 0x1BB);
    state->ball_y_velocity = (int16_t)get16(buf + 0x1BD);

    /* wBallSpin/Rotation: 0x1C3 */
    state->ball_spin = (int8_t)buf[0x1C3];
    state->ball_rotation = buf[0x1C4];
    state->ball_size = buf[0x1C8];

    /* wChoseInitialMap: 0x1E0 */
    state->chose_initial_map = buf[0x1E0];
    state->initial_map_selection_index = buf[0x1E1];
    state->num_map_moves = buf[0x1E2];
    memcpy(state->visited_maps, buf + 0x1E3, 7);

    /* wIndicatorStates: 0x22F */
    memcpy(state->indicator_states, buf + 0x22F, 19);

    /* wCurrentMap: 0x24A */
    state->current_map = buf[0x24A];
    state->in_special_mode = buf[0x24B];
    state->special_mode_state = buf[0x24D];
    state->special_mode = buf[0x250];

    /* wMapMoveDirection: 0x25A */
    state->map_move_direction = buf[0x25A];
    state->rare_mons_flag = buf[0x25B];

    /* wCAVELightStates: 0x20F */
    memcpy(state->cave_light_states, buf + 0x20F, 4);

    /* wPikachuSaverCharge: 0x217 */
    state->pikachu_saver_charge = buf[0x217];
    state->which_pikachu_saver_side = buf[0x218];

    /* wBallUpgradeTriggerStates: 0x2F9 */
    memcpy(state->ball_upgrade_trigger_states, buf + 0x2F9, 3);

    /* Extended fields at 0x400+ */
    state->num_pokeballs = buf[0x400];
    state->num_pokemon_caught_in_ball_bonus = buf[0x401];
    state->num_pokemon_evolved_in_ball_bonus = buf[0x402];
    state->num_bellsprout_entries = buf[0x403];
    state->num_dugtrio_triples = buf[0x404];
    state->num_cave_completions = buf[0x405];
    state->num_spinner_turns = buf[0x406];
    state->num_pikachu_saves = buf[0x407];
    state->bonus_multiplier_ones_digit = buf[0x408];
    state->bonus_multiplier_tens_digit = buf[0x409];
    state->red_stage_structure_backup = buf[0x40A];
    /* left/right_map_move_counter also read from 0x1F0/0x1F2 below (overrides these) */
    state->left_map_move_counter = buf[0x40B];
    state->right_map_move_counter = buf[0x40C];
    state->wd644 = buf[0x40D];
    state->num_mewtwo_bonus_completions = buf[0x40E];

    /* wLeftDiglettAnimationController: 0x1EF */
    state->left_diglett_anim_controller = buf[0x1EF];
    /* wLeftMapMoveCounter: 0x1F0 */
    state->left_map_move_counter = buf[0x1F0];
    /* wRightDiglettAnimationController: 0x1F1 */
    state->right_diglett_anim_controller = buf[0x1F1];
    /* wRightMapMoveCounter: 0x1F2 */
    state->right_map_move_counter = buf[0x1F2];
    /* wLeftMapMoveCounterFramesUntilDecrease: 0x1F7 (2 bytes) */
    state->left_map_move_counter_frames_until_decrease = get16(buf + 0x1F7);
    /* wRightMapMoveCounterFramesUntilDecrease: 0x1F9 (2 bytes) */
    state->right_map_move_counter_frames_until_decrease = get16(buf + 0x1F9);

    /* Mark as loading saved game so pinball.c skips certain inits */
    state->loading_saved_game = 1;
}

/*=============================================================================
 * Public save/load interface
 *===========================================================================*/

bool save_game(GameState *state) {
    /*
     * Save all persistent game data to separate files,
     * matching the SRAM layout from sram.asm.
     */
    bool ok = true;

    /* Save high scores */
    ok &= save_data((uint8_t *)state->red_high_scores,
                     sizeof(state->red_high_scores) + sizeof(state->blue_high_scores),
                     "save_scores.dat");

    /* Save pokedex */
    ok &= save_data(state->pokedex_flags, sizeof(state->pokedex_flags),
                     "save_pokedex.dat");

    /* Save key configs */
    ok &= save_data((uint8_t *)&state->key_config_ball_start,
                     sizeof(KeyConfig) * 7,
                     "save_keyconfig.dat");

    /* Save in-game state only when player explicitly saved (pause menu).
     * Only create save_game.dat when saved_game flag is set, to avoid
     * false "continue?" prompts from incidental save_game() calls. */
    if (state->saved_game) {
        uint8_t save_buf[SAVE_GAME_SIZE];
        pack_game_state(state, save_buf);
        ok &= save_data(save_buf, SAVE_GAME_SIZE, "save_game.dat");
    }

    return ok;
}

bool load_game(GameState *state) {
    /* Load persistent data that applies regardless of saved game.
     * High scores, pokedex, and key configs are always loaded. */
    load_saved_data((uint8_t *)state->red_high_scores,
                     sizeof(state->red_high_scores) + sizeof(state->blue_high_scores),
                     "save_scores.dat");

    load_saved_data(state->pokedex_flags, sizeof(state->pokedex_flags),
                     "save_pokedex.dat");

    load_saved_data((uint8_t *)&state->key_config_ball_start,
                     sizeof(KeyConfig) * 7,
                     "save_keyconfig.dat");

    /* Check for in-game save */
    uint8_t save_buf[SAVE_GAME_SIZE];
    if (load_saved_data(save_buf, SAVE_GAME_SIZE, "save_game.dat")) {
        state->saved_game = 1;
    }

    return true;
}

bool load_saved_game_state(GameState *state) {
    /* Load in-game state from save file and restore into GameState */
    uint8_t save_buf[SAVE_GAME_SIZE];
    if (!load_saved_data(save_buf, SAVE_GAME_SIZE, "save_game.dat"))
        return false;

    unpack_game_state(state, save_buf);
    return true;
}

void delete_saved_game(void) {
    /* ASM: After loading saved game, SRAM is cleared so you can't reload.
     * We just delete the save file. */
    remove("save_game.dat");
}
