/*
 * Lua API Bindings for Pokemon Pinball+ Scripting Engine
 *
 * Implements the `pinball` module exposed to Lua scripts.
 * All functions are prefixed with `pinball.` in Lua.
 *
 * Categories:
 *   - Ball state (get/set position, velocity, spin, type)
 *   - Stage state (current stage, screen state, generic field access)
 *   - Input (button checks)
 *   - Audio (music, SFX, cries, PCM)
 *   - Score (add score by name or raw BCD, jackpot, bonus multiplier)
 *   - Sprites (push OAM entries, clear)
 *   - VRAM & Rendering (palettes, tiles, tilemaps, scroll)
 *   - Collision (attributes, force, collision map)
 *   - Timer (start, stop, get, set)
 *   - Text (scrolling, stationary, clear)
 *   - Billboard (show picture, palette)
 *   - Utility (RNG, frame counter, logging, config access)
 *   - Table-local storage (per-table key-value store)
 */

#include "game/scripting.h"
#include "game/game_state.h"
#include "game/score.h"
#include "game/timer.h"
#include "game/billboard.h"
#include "game/rng.h"
#include "game/config_data.h"
#include "game/constants.h"
#include "game/red_field.h"
#include "game/blue_field.h"
#include "game/collision.h"
#include "game/joypad.h"
#include "game/ball_gfx.h"
#include "game/sprite_data.h"
#include "game/draw_red_field.h"
#include "game/animation.h"
#include "audio/audio.h"
#include "renderer/vram.h"
#include "renderer/tile_loader.h"
#include "renderer/stage_assets.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdio.h>
#include <string.h>

/* Access GameState from Lua state (stored in registry by scripting.c) */
extern GameState *script_get_game_state(lua_State *L);
extern ScriptEngine *script_get_engine(lua_State *L);

/* Forward declarations for text functions (declared in draw_red_field.h) */
extern void enable_bottom_text(GameState *state);
extern void fill_bottom_message_buffer_with_black_tile(GameState *state);
extern void load_scrolling_text(GameState *state, int slot_index,
                                const uint8_t *header, const char *text);
extern void load_stationary_text(GameState *state, int slot_index,
                                 const uint8_t *header, const char *text);

/*=============================================================================
 * Ball State
 *===========================================================================*/

static int api_get_ball_x(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->ball_x_pos);
    return 1;
}

static int api_get_ball_y(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->ball_y_pos);
    return 1;
}

static int api_set_ball_x(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->ball_x_pos = (ufixed8_8)luaL_checkinteger(L, 1);
    return 0;
}

static int api_set_ball_y(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->ball_y_pos = (ufixed8_8)luaL_checkinteger(L, 1);
    return 0;
}

static int api_get_ball_x_velocity(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->ball_x_velocity);
    return 1;
}

static int api_get_ball_y_velocity(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->ball_y_velocity);
    return 1;
}

static int api_set_ball_x_velocity(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->ball_x_velocity = (fixed8_8)luaL_checkinteger(L, 1);
    return 0;
}

static int api_set_ball_y_velocity(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->ball_y_velocity = (fixed8_8)luaL_checkinteger(L, 1);
    return 0;
}

static int api_get_ball_spin(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->ball_spin);
    return 1;
}

static int api_set_ball_spin(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->ball_spin = (uint8_t)luaL_checkinteger(L, 1);
    return 0;
}

static int api_get_ball_type(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->ball_type);
    return 1;
}

static int api_set_ball_type(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->ball_type = (uint8_t)luaL_checkinteger(L, 1);
    return 0;
}

/*=============================================================================
 * Stage State
 *===========================================================================*/

static int api_get_current_stage(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->current_stage);
    return 1;
}

static int api_set_current_stage(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->current_stage = (uint8_t)luaL_checkinteger(L, 1);
    return 0;
}

static int api_get_screen_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->screen_state);
    return 1;
}

static int api_set_screen_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->screen_state = (uint8_t)luaL_checkinteger(L, 1);
    return 0;
}

/*=============================================================================
 * Generic State Field Access
 *
 * Allows Lua scripts to read/write any named GameState field.
 * Uses a string-to-offset lookup table for known fields.
 *===========================================================================*/

typedef enum { FIELD_U8, FIELD_U16, FIELD_I16 } FieldType;

typedef struct {
    const char *name;
    size_t offset;
    FieldType type;
} StateFieldEntry;

/* Macro for building offset entries */
#define FIELD_ENTRY(field, ftype) { #field, offsetof(GameState, field), ftype }

static const StateFieldEntry state_fields[] = {
    /* Ball */
    FIELD_ENTRY(ball_x_pos, FIELD_U16),
    FIELD_ENTRY(ball_y_pos, FIELD_U16),
    FIELD_ENTRY(ball_x_velocity, FIELD_I16),
    FIELD_ENTRY(ball_y_velocity, FIELD_I16),
    FIELD_ENTRY(ball_spin, FIELD_U8),
    FIELD_ENTRY(ball_rotation, FIELD_U8),
    FIELD_ENTRY(ball_size, FIELD_U8),
    FIELD_ENTRY(ball_type, FIELD_U8),
    FIELD_ENTRY(lost_ball, FIELD_U8),

    /* Stage/screen */
    FIELD_ENTRY(current_stage, FIELD_U8),
    FIELD_ENTRY(screen_state, FIELD_U8),
    FIELD_ENTRY(current_map, FIELD_U8),
    FIELD_ENTRY(in_special_mode, FIELD_U8),
    FIELD_ENTRY(special_mode, FIELD_U8),
    FIELD_ENTRY(special_mode_state, FIELD_U8),

    /* Lives */
    FIELD_ENTRY(extra_balls, FIELD_U8),
    FIELD_ENTRY(cur_ball_life, FIELD_U8),
    FIELD_ENTRY(num_ball_lives, FIELD_U8),
    FIELD_ENTRY(extra_ball, FIELD_U8),

    /* Ball saver */
    FIELD_ENTRY(ball_saver_icon_on, FIELD_U8),
    FIELD_ENTRY(ball_saver_timer_frames, FIELD_U8),
    FIELD_ENTRY(ball_saver_timer_seconds, FIELD_U8),

    /* Voltorb/bumpers */
    FIELD_ENTRY(which_voltorb, FIELD_U8),
    FIELD_ENTRY(which_voltorb_id, FIELD_U8),
    FIELD_ENTRY(voltorb_hit_anim_duration, FIELD_U8),
    FIELD_ENTRY(which_animated_voltorb, FIELD_U8),
    FIELD_ENTRY(which_bumper, FIELD_U8),
    FIELD_ENTRY(which_bumper_id, FIELD_U8),
    FIELD_ENTRY(bumper_light_up_duration, FIELD_U8),

    /* Collision objects */
    FIELD_ENTRY(triggered_game_object, FIELD_U8),
    FIELD_ENTRY(triggered_game_object_index, FIELD_U8),
    FIELD_ENTRY(previous_triggered_game_object, FIELD_U8),

    /* Diglett */
    FIELD_ENTRY(which_diglett, FIELD_U8),
    FIELD_ENTRY(which_diglett_id, FIELD_U8),
    FIELD_ENTRY(left_diglett_anim_controller, FIELD_U8),
    FIELD_ENTRY(left_map_move_counter, FIELD_U8),
    FIELD_ENTRY(right_diglett_anim_controller, FIELD_U8),
    FIELD_ENTRY(right_map_move_counter, FIELD_U8),

    /* Bellsprout */
    FIELD_ENTRY(bellsprout_collision, FIELD_U8),

    /* Staryu */
    FIELD_ENTRY(staryu_collision, FIELD_U8),
    FIELD_ENTRY(staryu_side, FIELD_U8),
    FIELD_ENTRY(staryu_timer, FIELD_U8),

    /* Spinner */
    FIELD_ENTRY(spinner_collision, FIELD_U8),
    FIELD_ENTRY(spinner_velocity, FIELD_I16),

    /* CAVE lights */
    FIELD_ENTRY(which_cave_light, FIELD_U8),
    FIELD_ENTRY(which_cave_light_id, FIELD_U8),
    FIELD_ENTRY(cave_lights_blinking, FIELD_U8),

    /* Pikachu saver */
    FIELD_ENTRY(pikachu_saver_charge, FIELD_U8),
    FIELD_ENTRY(pikachu_saver_state, FIELD_U8),

    /* Indicators */
    FIELD_ENTRY(left_alley_trigger, FIELD_U8),
    FIELD_ENTRY(left_alley_count, FIELD_U8),
    FIELD_ENTRY(right_alley_trigger, FIELD_U8),
    FIELD_ENTRY(right_alley_count, FIELD_U8),

    /* Pinball launch */
    FIELD_ENTRY(pinball_launch_collision, FIELD_U8),
    FIELD_ENTRY(pinball_launched, FIELD_U8),

    /* Special mode */
    FIELD_ENTRY(special_mode_collision_id, FIELD_U8),
    FIELD_ENTRY(evolution_objects_disabled, FIELD_U8),
    FIELD_ENTRY(current_evolution_mon, FIELD_U8),
    FIELD_ENTRY(current_evolution_type, FIELD_U8),
    FIELD_ENTRY(num_evolution_trinkets, FIELD_U8),
    FIELD_ENTRY(current_catchem_mon, FIELD_U8),

    /* Billboard */
    FIELD_ENTRY(number_of_catch_mode_tiles_flipped, FIELD_U8),
    FIELD_ENTRY(billboard_reveal_frame_counter, FIELD_U8),
    FIELD_ENTRY(billboard_reveal_flicker_count, FIELD_U8),

    /* Wild mon */
    FIELD_ENTRY(wild_mon_is_hittable, FIELD_U8),
    FIELD_ENTRY(ball_hit_wild_mon, FIELD_U8),
    FIELD_ENTRY(num_mon_hits, FIELD_U8),
    FIELD_ENTRY(wild_mon_collision, FIELD_U8),

    /* Timer */
    FIELD_ENTRY(timer_seconds, FIELD_U8),
    FIELD_ENTRY(timer_minutes, FIELD_U8),
    FIELD_ENTRY(timer_active, FIELD_U8),
    FIELD_ENTRY(time_ran_out, FIELD_U8),

    /* Collision system */
    FIELD_ENTRY(is_ball_colliding, FIELD_U8),
    FIELD_ENTRY(collision_normal_angle, FIELD_U8),
    FIELD_ENTRY(collision_force_amplification, FIELD_U8),
    FIELD_ENTRY(cur_collision_attribute, FIELD_U8),
    FIELD_ENTRY(no_collision_applied, FIELD_U8),

    /* Flipper */
    FIELD_ENTRY(flipper_collision, FIELD_U8),
    FIELD_ENTRY(flippers_disabled, FIELD_U8),

    /* Score/multiplier */
    FIELD_ENTRY(cur_bonus_multiplier, FIELD_U8),
    FIELD_ENTRY(bonus_multiplier_tens_digit, FIELD_U8),
    FIELD_ENTRY(bonus_multiplier_ones_digit, FIELD_U8),

    /* Slot */
    FIELD_ENTRY(slot_collision, FIELD_U8),
    FIELD_ENTRY(slot_is_open, FIELD_U8),
    FIELD_ENTRY(num_pokeballs, FIELD_U8),
    FIELD_ENTRY(slot_reward_progress, FIELD_U8),

    /* Tilt */
    FIELD_ENTRY(left_and_right_tilt_pixels_offset, FIELD_U8),
    FIELD_ENTRY(upper_tilt_pixels_offset, FIELD_U8),

    /* Scroll */
    FIELD_ENTRY(scx, FIELD_U8),

    /* Physics flags */
    FIELD_ENTRY(pinball_is_visible, FIELD_U8),
    FIELD_ENTRY(enable_ball_gravity_and_tilt, FIELD_U8),

    /* Map move */
    FIELD_ENTRY(num_map_moves, FIELD_U8),
    FIELD_ENTRY(map_move_direction, FIELD_U8),

    /* Per-ball bonus counters */
    FIELD_ENTRY(num_pokemon_caught_in_ball_bonus, FIELD_U8),
    FIELD_ENTRY(num_pokemon_evolved_in_ball_bonus, FIELD_U8),
    FIELD_ENTRY(num_bellsprout_entries, FIELD_U8),
    FIELD_ENTRY(num_dugtrio_triples, FIELD_U8),
    FIELD_ENTRY(num_cave_completions, FIELD_U8),
    FIELD_ENTRY(num_spinner_turns, FIELD_U8),
    FIELD_ENTRY(num_pikachu_saves, FIELD_U8),
    FIELD_ENTRY(num_mewtwo_bonus_completions, FIELD_U8),

    /* Party */
    FIELD_ENTRY(num_party_mons, FIELD_U8),

    /* Misc */
    FIELD_ENTRY(going_to_bonus_stage, FIELD_U8),
    FIELD_ENTRY(returning_from_bonus_stage, FIELD_U8),
    FIELD_ENTRY(next_stage, FIELD_U8),
    FIELD_ENTRY(next_bonus_stage, FIELD_U8),
    FIELD_ENTRY(initial_next_bonus_stage, FIELD_U8),
    FIELD_ENTRY(completed_bonus_stage, FIELD_U8),
    FIELD_ENTRY(draw_bottom_message_box, FIELD_U8),

    /* Blue field specific */
    FIELD_ENTRY(blue_stage_force_field_direction, FIELD_U8),
    FIELD_ENTRY(which_shellder, FIELD_U8),
    FIELD_ENTRY(which_shellder_id, FIELD_U8),
    FIELD_ENTRY(slowpoke_collision, FIELD_U8),
    FIELD_ENTRY(cloyster_collision, FIELD_U8),
    FIELD_ENTRY(psyduck_state, FIELD_U8),
    FIELD_ENTRY(poliwag_state, FIELD_U8),

    /* Gengar bonus */
    FIELD_ENTRY(gengar_bonus_closed_gate, FIELD_U8),
    FIELD_ENTRY(num_gastly_hits, FIELD_U8),
    FIELD_ENTRY(num_haunter_hits, FIELD_U8),
    FIELD_ENTRY(num_gengar_hits, FIELD_U8),
    FIELD_ENTRY(gengar_defeated, FIELD_U8),

    /* Mewtwo bonus */
    FIELD_ENTRY(mewtwo_bonus_closed_gate, FIELD_U8),
    FIELD_ENTRY(mewtwo_hit_counter, FIELD_U8),
    FIELD_ENTRY(mewtwo_regen_phase, FIELD_U8),
    FIELD_ENTRY(mewtwo_bonus_completed, FIELD_U8),

    /* Meowth bonus */
    FIELD_ENTRY(meowth_bonus_closed_gate, FIELD_U8),
    FIELD_ENTRY(meowth_state, FIELD_U8),
    FIELD_ENTRY(meowth_x_position, FIELD_U8),
    FIELD_ENTRY(meowth_stage_score, FIELD_U8),

    /* Diglett bonus */
    FIELD_ENTRY(diglett_bonus_closed_gate, FIELD_U8),
    FIELD_ENTRY(dugtrio_state, FIELD_U8),

    /* Seel bonus */
    FIELD_ENTRY(seel_bonus_closed_gate, FIELD_U8),
    FIELD_ENTRY(seel_stage_streak, FIELD_U8),
    FIELD_ENTRY(seel_stage_score, FIELD_U8),
    FIELD_ENTRY(seel_stage_state, FIELD_U8),
    FIELD_ENTRY(seel_completion_state, FIELD_U8),

    /* Slot (additional) */
    FIELD_ENTRY(slot_enter_or_exit_counter, FIELD_U8),
    FIELD_ENTRY(slot_roulette_active, FIELD_U8),
    FIELD_ENTRY(slot_roulette_state, FIELD_U8),
    FIELD_ENTRY(slot_glowing_anim_counter, FIELD_U8),
    FIELD_ENTRY(catchem_or_evolution_slot_reward_active, FIELD_U8),
    FIELD_ENTRY(bonus_stage_slot_reward_active, FIELD_U8),
    FIELD_ENTRY(previous_num_pokeballs, FIELD_U8),
    FIELD_ENTRY(pokeball_blinking_counter, FIELD_U8),
    FIELD_ENTRY(frames_until_slot_cave_opens, FIELD_U8),
    FIELD_ENTRY(opened_slot_by_cave_lights, FIELD_U8),
    FIELD_ENTRY(opened_slot_by_pokeballs, FIELD_U8),

    /* Pikachu (additional) */
    FIELD_ENTRY(which_pikachu, FIELD_U8),
    FIELD_ENTRY(which_pikachu_id, FIELD_U8),
    FIELD_ENTRY(which_pikachu_saver_side, FIELD_U8),
    FIELD_ENTRY(pikachu_saver_slot_reward_active, FIELD_U8),
    FIELD_ENTRY(pikachu_saver_sound_cooldown, FIELD_U8),

    /* Ball (additional) */
    FIELD_ENTRY(ball_type_counter, FIELD_U16),
    FIELD_ENTRY(ball_type_backup, FIELD_U8),

    /* Stage (additional) */
    FIELD_ENTRY(stage_collision_state, FIELD_U8),
    FIELD_ENTRY(stage_collision_state_backup, FIELD_U8),
    FIELD_ENTRY(red_stage_structure_backup, FIELD_U8),
    FIELD_ENTRY(previous_field_structure_state, FIELD_U8),

    /* Cave (additional) */
    FIELD_ENTRY(cave_lights_blinking_frames_remaining, FIELD_U8),

    /* Bonus multiplier (additional) */
    FIELD_ENTRY(which_bonus_multiplier_railing, FIELD_U8),
    FIELD_ENTRY(which_bonus_multiplier_railing_id, FIELD_U8),
    FIELD_ENTRY(show_bonus_multiplier_bottom_message, FIELD_U8),
    FIELD_ENTRY(wd610, FIELD_U8),
    FIELD_ENTRY(wd611, FIELD_U8),
    FIELD_ENTRY(wd612, FIELD_U8),
    FIELD_ENTRY(wd614, FIELD_U8),
    FIELD_ENTRY(wd615, FIELD_U8),

    /* Upgrade triggers */
    FIELD_ENTRY(which_pinball_upgrade_trigger, FIELD_U8),
    FIELD_ENTRY(which_pinball_upgrade_trigger_id, FIELD_U8),
    FIELD_ENTRY(ball_upgrade_triggers_blinking, FIELD_U8),
    FIELD_ENTRY(ball_upgrade_triggers_blinking_frames_remaining, FIELD_U8),

    /* Board triggers */
    FIELD_ENTRY(which_board_trigger, FIELD_U8),
    FIELD_ENTRY(which_board_trigger_id, FIELD_U8),

    /* Ditto slot */
    FIELD_ENTRY(ditto_slot_collision, FIELD_U8),
    FIELD_ENTRY(ditto_enter_or_exit_counter, FIELD_U8),

    /* Staryu (additional) */
    FIELD_ENTRY(staryu_anim_active, FIELD_U8),

    /* Spinner (additional) */
    FIELD_ENTRY(spinner_state, FIELD_U8),

    /* Bumper (additional) */
    FIELD_ENTRY(which_bumper_gfx, FIELD_U8),

    /* Launch */
    FIELD_ENTRY(chose_initial_map, FIELD_U8),
    FIELD_ENTRY(initial_map_selection_index, FIELD_U8),
    FIELD_ENTRY(map_cycling_frames, FIELD_U8),

    /* Wild mon (additional) */
    FIELD_ENTRY(current_animated_mon_sprite_type, FIELD_U8),
    FIELD_ENTRY(current_animated_mon_sprite_frame, FIELD_U8),
    FIELD_ENTRY(loops_until_next_catch_sprite_anim_change, FIELD_U8),
    FIELD_ENTRY(catch_mode_mon_update_timer, FIELD_U8),
    FIELD_ENTRY(num_mew_hits, FIELD_U8),
    FIELD_ENTRY(wd5c6, FIELD_U8),

    /* Ball capture */
    FIELD_ENTRY(capturing_mon, FIELD_U8),

    /* Misc */
    FIELD_ENTRY(show_extra_ball_text, FIELD_U8),
    FIELD_ENTRY(extra_ball_state, FIELD_U8),
    FIELD_ENTRY(wd4df, FIELD_U8),
    FIELD_ENTRY(pinball_launched, FIELD_U8),
    FIELD_ENTRY(disable_horizontal_scroll_for_ball_start, FIELD_U8),

    /* Ball saver (additional) */
    FIELD_ENTRY(ball_saver_flash_rate, FIELD_U8),
    FIELD_ENTRY(num_times_ball_saved_text_will_display, FIELD_U8),
    FIELD_ENTRY(ball_saver_timer_frames_backup, FIELD_U8),
    FIELD_ENTRY(ball_saver_timer_seconds_backup, FIELD_U8),
    FIELD_ENTRY(num_times_ball_saved_text_will_display_backup, FIELD_U8),

    /* Evolution (additional) */
    FIELD_ENTRY(num_possible_evolution_objects, FIELD_U8),
    FIELD_ENTRY(evolution_trinket_cooldown_frames, FIELD_U16),
    FIELD_ENTRY(collided_point_index, FIELD_U8),

    /* Rumble */
    FIELD_ENTRY(rumble_pattern, FIELD_U8),
    FIELD_ENTRY(rumble_duration, FIELD_U8),

    /* Flipper (additional) */
    FIELD_ENTRY(left_flipper_state, FIELD_U16),
    FIELD_ENTRY(right_flipper_state, FIELD_U16),
    FIELD_ENTRY(flipper_x_force, FIELD_I16),
    FIELD_ENTRY(flipper_y_force, FIELD_I16),

    /* Diglett (additional) */
    FIELD_ENTRY(left_map_move_diglett_anim_counter, FIELD_U8),
    FIELD_ENTRY(left_map_move_diglett_frame, FIELD_U8),
    FIELD_ENTRY(right_map_move_diglett_anim_counter, FIELD_U8),
    FIELD_ENTRY(right_map_move_diglett_frame, FIELD_U8),
    FIELD_ENTRY(left_map_move_counter_frames_until_decrease, FIELD_U16),
    FIELD_ENTRY(right_map_move_counter_frames_until_decrease, FIELD_U16),

    /* Bottom text */
    FIELD_ENTRY(bottom_text_enabled, FIELD_U8),
    FIELD_ENTRY(disable_draw_scoreboard_info, FIELD_U8),

    /* Loading / saved game */
    FIELD_ENTRY(loading_saved_game, FIELD_U8),
    FIELD_ENTRY(saved_game, FIELD_U8),
    FIELD_ENTRY(rare_mons_flag, FIELD_U8),

    /* Audio control */
    FIELD_ENTRY(stage_song, FIELD_U8),
    FIELD_ENTRY(stage_song_bank, FIELD_U8),
    FIELD_ENTRY(audio_engine_enabled, FIELD_U8),

    { NULL, 0, FIELD_U8 } /* Sentinel */
};

static const StateFieldEntry *find_field(const char *name) {
    for (int i = 0; state_fields[i].name != NULL; i++) {
        if (strcmp(state_fields[i].name, name) == 0)
            return &state_fields[i];
    }
    return NULL;
}

static int api_get_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    const StateFieldEntry *f = find_field(name);
    if (!f) {
        return luaL_error(L, "unknown state field: '%s'", name);
    }
    uint8_t *base = (uint8_t *)s;
    switch (f->type) {
        case FIELD_U8:  lua_pushinteger(L, *(uint8_t *)(base + f->offset)); break;
        case FIELD_U16: lua_pushinteger(L, *(uint16_t *)(base + f->offset)); break;
        case FIELD_I16: lua_pushinteger(L, *(int16_t *)(base + f->offset)); break;
    }
    return 1;
}

static int api_set_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    lua_Integer val = luaL_checkinteger(L, 2);
    const StateFieldEntry *f = find_field(name);
    if (!f) {
        return luaL_error(L, "unknown state field: '%s'", name);
    }
    uint8_t *base = (uint8_t *)s;
    switch (f->type) {
        case FIELD_U8:  *(uint8_t *)(base + f->offset) = (uint8_t)val; break;
        case FIELD_U16: *(uint16_t *)(base + f->offset) = (uint16_t)val; break;
        case FIELD_I16: *(int16_t *)(base + f->offset) = (int16_t)val; break;
    }
    return 0;
}

/*=============================================================================
 * Table-Local Storage
 *
 * Per-table Lua table stored in registry, persists during game session.
 *===========================================================================*/
#define LOCAL_STORAGE_KEY "pinball_local_storage"

static void ensure_local_storage(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, LOCAL_STORAGE_KEY);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, LOCAL_STORAGE_KEY);
    }
}

static int api_get_local(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    ensure_local_storage(L);
    lua_getfield(L, -1, key);
    lua_remove(L, -2); /* Remove storage table */
    return 1;
}

static int api_set_local(lua_State *L) {
    const char *key = luaL_checkstring(L, 1);
    /* Value is arg 2 */
    ensure_local_storage(L);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, key);
    lua_pop(L, 1); /* Pop storage table */
    return 0;
}

/*=============================================================================
 * Input
 *===========================================================================*/

static int api_is_button_held(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int button = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, (s->hram.joypad_state & button) != 0);
    return 1;
}

static int api_is_button_pressed(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int button = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, (s->hram.newly_pressed_buttons & button) != 0);
    return 1;
}

static int api_get_joypad_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->hram.joypad_state);
    return 1;
}

/*=============================================================================
 * Audio
 *===========================================================================*/

static int api_play_music(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    uint8_t bank, id;
    int clip_index = -1;
    if (s->config && config_get_music(s->config, name, &bank, &id, &clip_index)) {
        if (clip_index >= 0)
            audio_play_custom_music(s->audio, clip_index);
        else
            audio_play_music(s->audio, bank, id);
    }
    return 0;
}

static int api_play_sfx(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    uint8_t bank, id;
    int clip_index = -1;
    if (s->config && config_get_sfx(s->config, name, &bank, &id, &clip_index)) {
        if (clip_index >= 0)
            audio_play_custom_sfx(s->audio, clip_index);
        else
            audio_play_sfx(s->audio, bank, id);
    }
    return 0;
}

static int api_play_sfx_raw(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int bank = (int)luaL_checkinteger(L, 1);
    int id = (int)luaL_checkinteger(L, 2);
    audio_play_sfx(s->audio, (uint8_t)bank, (uint8_t)id);
    return 0;
}

static int api_play_music_raw(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int bank = (int)luaL_checkinteger(L, 1);
    int id = (int)luaL_checkinteger(L, 2);
    audio_play_music(s->audio, (uint8_t)bank, (uint8_t)id);
    return 0;
}

static int api_play_sfx_if_none_active(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int bank = (int)luaL_checkinteger(L, 1);
    int id = (int)luaL_checkinteger(L, 2);
    audio_play_sfx_if_none_active(s->audio, (uint8_t)bank, (uint8_t)id);
    return 0;
}

static int api_play_cry(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int mon_id = (int)luaL_checkinteger(L, 1);
    audio_play_cry(s->audio, (uint8_t)mon_id);
    return 0;
}

static int api_play_pcm(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int clip = (int)luaL_checkinteger(L, 1);
    audio_play_pcm(s->audio, clip);
    return 0;
}

static int api_stop_music(lua_State *L) {
    GameState *s = script_get_game_state(L);
    audio_stop_all(s->audio);
    return 0;
}

static int api_play_table_music(lua_State *L) {
    GameState *s = script_get_game_state(L);
    ScriptEngine *engine = script_get_engine(L);
    const char *key = luaL_checkstring(L, 1);
    for (int i = 0; i < engine->num_table_music; i++) {
        if (strcmp(engine->table_music[i].key, key) == 0) {
            audio_play_custom_music(s->audio, engine->table_music[i].clip_index);
            return 0;
        }
    }
    /* Key not found — do nothing. C code already started default music. */
    return 0;
}

/*=============================================================================
 * Score
 *===========================================================================*/

/* Add score by config name (e.g. "score_100", "bumper", "gastly_hit") */
static int api_add_score(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);

    /* Look up the named score in the ScoreConfig struct by checking known names */
    const uint8_t *score_bcd = NULL;

    /* Use a simple name->field mapping */
    if (s->config) {
        const ScoreConfig *sc = &s->config->scores;
        if (strcmp(name, "score_5") == 0) score_bcd = sc->score_5;
        else if (strcmp(name, "score_10") == 0) score_bcd = sc->score_10;
        else if (strcmp(name, "score_100") == 0) score_bcd = sc->score_100;
        else if (strcmp(name, "score_400") == 0) score_bcd = sc->score_400;
        else if (strcmp(name, "score_500") == 0) score_bcd = sc->score_500;
        else if (strcmp(name, "score_5000") == 0) score_bcd = sc->score_5000;
        else if (strcmp(name, "score_10000") == 0) score_bcd = sc->score_10000;
        else if (strcmp(name, "score_100000") == 0) score_bcd = sc->score_100000;
        else if (strcmp(name, "score_300000") == 0) score_bcd = sc->score_300000;
        else if (strcmp(name, "score_1000000") == 0) score_bcd = sc->score_1000000;
        else if (strcmp(name, "score_10000000") == 0) score_bcd = sc->score_10000000;
        else if (strcmp(name, "gastly_hit") == 0) score_bcd = sc->gastly_hit;
        else if (strcmp(name, "haunter_hit") == 0) score_bcd = sc->haunter_hit;
        else if (strcmp(name, "gengar_hit") == 0) score_bcd = sc->gengar_hit;
        else if (strcmp(name, "gravestone") == 0) score_bcd = sc->gravestone;
        else if (strcmp(name, "mewtwo_hit") == 0) score_bcd = sc->mewtwo_hit;
        else if (strcmp(name, "mewtwo_orb_hit") == 0) score_bcd = sc->mewtwo_orb_hit;
        else if (strcmp(name, "meowth_jewel") == 0) score_bcd = sc->meowth_jewel;
        else if (strcmp(name, "meowth_hit") == 0) score_bcd = sc->meowth_hit;
        else if (strcmp(name, "diglett_hit") == 0) score_bcd = sc->diglett_hit;
        else if (strcmp(name, "dugtrio_hit") == 0) score_bcd = sc->dugtrio_hit;
        else if (strcmp(name, "seel_hit") == 0) score_bcd = sc->seel_hit;
        else if (strcmp(name, "seel_dive") == 0) score_bcd = sc->seel_dive;
    }

    if (score_bcd) {
        add_score_with_multiplier(s, score_bcd);
    } else {
        fprintf(stderr, "[Script] Unknown score name: '%s'\n", name);
    }
    return 0;
}

/* Add raw BCD score (4-byte array as 4 integers) */
static int api_add_score_raw(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t bcd[4] = {0};
    for (int i = 0; i < 4 && i < lua_gettop(L); i++) {
        bcd[i] = (uint8_t)luaL_checkinteger(L, i + 1);
    }
    add_score_with_multiplier(s, bcd);
    return 0;
}

/* Add score without multiplier */
static int api_add_score_no_mult(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t bcd[4] = {0};
    for (int i = 0; i < 4 && i < lua_gettop(L); i++) {
        bcd[i] = (uint8_t)luaL_checkinteger(L, i + 1);
    }
    add_score_no_multiplier(s, bcd);
    return 0;
}

static int api_add_jackpot(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    const uint8_t *bcd = NULL;

    if (s->config) {
        const ScoreConfig *sc = &s->config->scores;
        if (strcmp(name, "jackpot_catch_voltorb") == 0) bcd = sc->jackpot_catch_voltorb;
        else if (strcmp(name, "jackpot_catch_spinner") == 0) bcd = sc->jackpot_catch_spinner;
        else if (strcmp(name, "jackpot_catch_bellsprout") == 0) bcd = sc->jackpot_catch_bellsprout;
        else if (strcmp(name, "jackpot_evo_voltorb") == 0) bcd = sc->jackpot_evo_voltorb;
        else if (strcmp(name, "jackpot_evo_bellsprout") == 0) bcd = sc->jackpot_evo_bellsprout;
        else if (strcmp(name, "jackpot_evo_spinner") == 0) bcd = sc->jackpot_evo_spinner;
    }

    if (bcd) {
        add_bcd_to_jackpot(s, bcd);
    }
    return 0;
}

static int api_add_jackpot_raw(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t bcd[4] = {0};
    for (int i = 0; i < 4 && i < lua_gettop(L); i++) {
        bcd[i] = (uint8_t)luaL_checkinteger(L, i + 1);
    }
    add_bcd_to_jackpot(s, bcd);
    return 0;
}

static int api_get_bonus_multiplier(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->cur_bonus_multiplier);
    return 1;
}

static int api_set_bonus_multiplier(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->cur_bonus_multiplier = (uint8_t)luaL_checkinteger(L, 1);
    return 0;
}

/*=============================================================================
 * Sprites
 *===========================================================================*/

/* Push a single OAM sprite entry */
static int api_push_sprite(lua_State *L) {
    GameState *s = script_get_game_state(L);
    if (s->sprite_buffer_size >= GBC_OAM_ENTRIES) return 0;

    int y    = (int)luaL_checkinteger(L, 1);
    int x    = (int)luaL_checkinteger(L, 2);
    int tile = (int)luaL_checkinteger(L, 3);
    int attr = (int)luaL_checkinteger(L, 4);

    OAMEntry *e = &s->sprite_buffer[s->sprite_buffer_size++];
    e->y = (uint8_t)y;
    e->x = (uint8_t)x;
    e->tile = (uint8_t)tile;
    e->attr = (uint8_t)attr;
    return 0;
}

/* Push an 8x16 sprite (two OAM entries) */
static int api_push_sprite_8x16(lua_State *L) {
    GameState *s = script_get_game_state(L);
    if (s->sprite_buffer_size + 1 >= GBC_OAM_ENTRIES) return 0;

    int y    = (int)luaL_checkinteger(L, 1);
    int x    = (int)luaL_checkinteger(L, 2);
    int tile = (int)luaL_checkinteger(L, 3);
    int attr = (int)luaL_checkinteger(L, 4);

    /* Top half */
    OAMEntry *e = &s->sprite_buffer[s->sprite_buffer_size++];
    e->y = (uint8_t)y;
    e->x = (uint8_t)x;
    e->tile = (uint8_t)(tile & 0xFE); /* Even tile for top */
    e->attr = (uint8_t)attr;

    /* Bottom half */
    e = &s->sprite_buffer[s->sprite_buffer_size++];
    e->y = (uint8_t)(y + 8);
    e->x = (uint8_t)x;
    e->tile = (uint8_t)(tile | 0x01); /* Odd tile for bottom */
    e->attr = (uint8_t)attr;
    return 0;
}

static int api_clear_sprites(lua_State *L) {
    GameState *s = script_get_game_state(L);
    memset(s->sprite_buffer, 0, sizeof(s->sprite_buffer));
    s->sprite_buffer_size = 0;
    return 0;
}

static int api_get_sprite_count(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->sprite_buffer_size);
    return 1;
}

/*=============================================================================
 * VRAM & Rendering
 *===========================================================================*/

static int api_set_bg_palette(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int pal_idx = (int)luaL_checkinteger(L, 1);
    if (pal_idx < 0 || pal_idx >= GBC_NUM_BG_PALETTES) return 0;

    luaL_checktype(L, 2, LUA_TTABLE);
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, 2, i + 1);
        s->bg_palettes[pal_idx].colors[i] = (uint16_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    return 0;
}

static int api_set_obj_palette(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int pal_idx = (int)luaL_checkinteger(L, 1);
    if (pal_idx < 0 || pal_idx >= GBC_NUM_OBJ_PALETTES) return 0;

    luaL_checktype(L, 2, LUA_TTABLE);
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, 2, i + 1);
        s->obj_palettes[pal_idx].colors[i] = (uint16_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    return 0;
}

static int api_set_scroll(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->hram.scx = (uint8_t)luaL_checkinteger(L, 1);
    s->hram.scy = (uint8_t)luaL_checkinteger(L, 2);
    return 0;
}

static int api_set_window_pos(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->hram.wx = (uint8_t)luaL_checkinteger(L, 1);
    s->hram.wy = (uint8_t)luaL_checkinteger(L, 2);
    return 0;
}

/* pinball.load_tiles(path, vram_addr, [bank], [interleave_width])
 * Enhanced: optional explicit bank override and interleave support. */
static int api_load_tiles(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *path = luaL_checkstring(L, 1);
    int dest_addr = (int)luaL_checkinteger(L, 2);

    char full_path[520];
    snprintf(full_path, sizeof(full_path), "%s%s", s->asset_base_path, path);

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(full_path, &data_size);
    if (tile_data && s->vram) {
        /* Bank: use explicit arg 3 if provided, else auto-detect */
        int bank;
        if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
            bank = (int)lua_tointeger(L, 3);
        } else {
            bank = (dest_addr >= 0x8800) ? 1 : 0;
        }

        /* Interleave: if arg 4 provided (pixel width), apply 8x16 interleave */
        if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) {
            int interleave_px = (int)lua_tointeger(L, 4);
            int tiles_per_row = interleave_px / 8;
            if (tiles_per_row > 0)
                interleave_tiles(tile_data, data_size, tiles_per_row);
        }

        int offset = dest_addr - (bank ? 0x8800 : 0x8000);
        vram_write(s->vram, (uint8_t)bank, (uint16_t)offset, tile_data, (uint16_t)data_size);
        free(tile_data);
    }
    return 0;
}

/* Write raw data to VRAM */
static int api_vram_write(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int bank = (int)luaL_checkinteger(L, 1);
    int addr = (int)luaL_checkinteger(L, 2);

    /* Arg 3 is a table of byte values */
    luaL_checktype(L, 3, LUA_TTABLE);
    int len = (int)luaL_len(L, 3);

    if (s->vram && len > 0) {
        uint8_t *buf = (uint8_t *)malloc(len);
        if (buf) {
            for (int i = 0; i < len; i++) {
                lua_rawgeti(L, 3, i + 1);
                buf[i] = (uint8_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
            vram_write(s->vram, bank, addr, buf, len);
            free(buf);
        }
    }
    return 0;
}

/*=============================================================================
 * Collision
 *===========================================================================*/

static int api_get_collision_attribute(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->cur_collision_attribute);
    return 1;
}

static int api_set_collision_force(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->collision_force_amplification = (uint8_t)luaL_checkinteger(L, 1);
    return 0;
}

static int api_get_last_collision_id(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->triggered_game_object);
    return 1;
}

/*=============================================================================
 * Timer
 *===========================================================================*/

static int api_set_timer(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int minutes = (int)luaL_checkinteger(L, 1);
    int seconds = (int)luaL_checkinteger(L, 2);
    start_timer(s, (uint8_t)minutes, (uint8_t)seconds);
    return 0;
}

static int api_get_timer(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->timer_minutes);
    lua_pushinteger(L, s->timer_seconds);
    return 2;
}

static int api_start_timer(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->timer_active = 1;
    return 0;
}

static int api_stop_timer(lua_State *L) {
    GameState *s = script_get_game_state(L);
    stop_timer(s);
    return 0;
}

/*=============================================================================
 * Text Display
 *===========================================================================*/

static int api_show_scrolling_text(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int slot = (int)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);

    /* Default header: speed=4, normal scroll */
    static const uint8_t default_header[6] = { 4, 0x54, 0x44, 20, 0x00, 51 };

    if (slot >= 0 && slot < 3) {
        enable_bottom_text(s);
        load_scrolling_text(s, slot, default_header, text);
    }
    return 0;
}

static int api_show_stationary_text(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int slot = (int)luaL_checkinteger(L, 1);
    const char *text = luaL_checkstring(L, 2);

    static const uint8_t default_header[6] = { 0, 0x44, 0x44, 60, 0x00, 0 };

    if (slot >= 0 && slot < 3) {
        enable_bottom_text(s);
        load_stationary_text(s, slot, default_header, text);
    }
    return 0;
}

static int api_clear_bottom_text(lua_State *L) {
    GameState *s = script_get_game_state(L);
    fill_bottom_message_buffer_with_black_tile(s);
    s->draw_bottom_message_box = 0;
    return 0;
}

/*=============================================================================
 * Billboard
 *===========================================================================*/

static int api_show_billboard(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int pic_id = (int)luaL_checkinteger(L, 1);
    load_billboard_picture(s, (uint8_t)pic_id);
    return 0;
}

static int api_load_billboard_palette(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int pic_id = (int)luaL_checkinteger(L, 1);
    load_colored_billboard_palette(s, (uint8_t)pic_id);
    return 0;
}

static int api_clear_billboard(lua_State *L) {
    GameState *s = script_get_game_state(L);
    clear_billboard_tilemap(s);
    return 0;
}

/*=============================================================================
 * Utility
 *===========================================================================*/

static int api_random(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int max = (int)luaL_checkinteger(L, 1);
    if (max <= 0) {
        lua_pushinteger(L, 0);
    } else {
        lua_pushinteger(L, random_range(s, (uint8_t)max));
    }
    return 1;
}

static int api_random_raw(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, gen_random(s));
    return 1;
}

static int api_frame_counter(lua_State *L) {
    GameState *s = script_get_game_state(L);
    lua_pushinteger(L, s->hram.frame_counter);
    return 1;
}

static int api_log(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    printf("[Script] %s\n", msg);
    return 0;
}

/* Access array fields in GameState by name and index */
static int api_get_indicator(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 0x13) {
        lua_pushinteger(L, s->indicator_states[idx]);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int api_set_indicator(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 0x13) {
        s->indicator_states[idx] = (uint8_t)val;
    }
    return 0;
}

static int api_get_cave_light(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 4) {
        lua_pushinteger(L, s->cave_light_states[idx]);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int api_set_cave_light(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 4) {
        s->cave_light_states[idx] = (uint8_t)val;
    }
    return 0;
}

/* Party mon access */
static int api_get_party_mon(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 256) {
        lua_pushinteger(L, s->party_mons[idx]);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int api_set_party_mon(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 256) {
        s->party_mons[idx] = (uint8_t)val;
    }
    return 0;
}

/*=============================================================================
 * Animation Struct Accessors
 * Get/set Animation fields (frame, counter, data_idx) by named ID.
 *===========================================================================*/

typedef struct {
    const char *name;
    size_t offset; /* offsetof(GameState, field) */
} AnimEntry;

#define ANIM_ENTRY(field) { #field, offsetof(GameState, field) }

static const AnimEntry anim_entries[] = {
    ANIM_ENTRY(voltorb1_anim),
    ANIM_ENTRY(voltorb2_anim),
    ANIM_ENTRY(voltorb3_anim),
    ANIM_ENTRY(bellsprout_anim),
    ANIM_ENTRY(staryu_anim),
    ANIM_ENTRY(pikachu_saver_anim),
    ANIM_ENTRY(ball_capture_anim),
    ANIM_ENTRY(slowpoke_anim),
    ANIM_ENTRY(cloyster_anim),
    { NULL, 0 }
};

static const AnimEntry *find_anim(const char *name) {
    for (int i = 0; anim_entries[i].name != NULL; i++) {
        if (strcmp(anim_entries[i].name, name) == 0)
            return &anim_entries[i];
    }
    return NULL;
}

/* pinball.get_anim(name) -> {frame_counter, frame, index} table */
static int api_get_anim(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    const AnimEntry *e = find_anim(name);
    if (!e) return luaL_error(L, "unknown animation: '%s'", name);
    Animation *a = (Animation *)((uint8_t *)s + e->offset);
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, a->frame_counter);
    lua_setfield(L, -2, "frame_counter");
    lua_pushinteger(L, a->frame);
    lua_setfield(L, -2, "frame");
    lua_pushinteger(L, a->index);
    lua_setfield(L, -2, "index");
    return 1;
}

/* pinball.set_anim(name, frame_counter, frame, index) */
static int api_set_anim(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    const AnimEntry *e = find_anim(name);
    if (!e) return luaL_error(L, "unknown animation: '%s'", name);
    Animation *a = (Animation *)((uint8_t *)s + e->offset);
    a->frame_counter = (uint8_t)luaL_checkinteger(L, 2);
    a->frame = (uint8_t)luaL_checkinteger(L, 3);
    a->index = (uint8_t)luaL_checkinteger(L, 4);
    return 0;
}

/*=============================================================================
 * Array Field Accessors
 * Access ball_upgrade_trigger_states[], collided_alley_triggers[],
 * evolution_object_states[], active_evolution_trinkets[], visited_maps[]
 *===========================================================================*/

static int api_get_upgrade_trigger_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 3)
        lua_pushinteger(L, s->ball_upgrade_trigger_states[idx]);
    else
        lua_pushinteger(L, 0);
    return 1;
}

static int api_set_upgrade_trigger_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 3)
        s->ball_upgrade_trigger_states[idx] = (uint8_t)val;
    return 0;
}

static int api_get_alley_trigger(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 8)
        lua_pushinteger(L, s->collided_alley_triggers[idx]);
    else
        lua_pushinteger(L, 0);
    return 1;
}

static int api_set_alley_trigger(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 8)
        s->collided_alley_triggers[idx] = (uint8_t)val;
    return 0;
}

static int api_get_evolution_object_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 10)
        lua_pushinteger(L, s->evolution_object_states[idx]);
    else
        lua_pushinteger(L, 0);
    return 1;
}

static int api_set_evolution_object_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 10)
        s->evolution_object_states[idx] = (uint8_t)val;
    return 0;
}

static int api_get_trinket(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 18)
        lua_pushinteger(L, s->active_evolution_trinkets[idx]);
    else
        lua_pushinteger(L, 0);
    return 1;
}

static int api_set_trinket(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 18)
        s->active_evolution_trinkets[idx] = (uint8_t)val;
    return 0;
}

static int api_get_spinner_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 2)
        lua_pushinteger(L, s->spinner_state[idx]);
    else
        lua_pushinteger(L, 0);
    return 1;
}

static int api_set_spinner_state(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 2)
        s->spinner_state[idx] = (uint8_t)val;
    return 0;
}

static int api_get_secondary_alley(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 2)
        lua_pushinteger(L, s->secondary_left_alley_trigger[idx]);
    else
        lua_pushinteger(L, 0);
    return 1;
}

static int api_set_secondary_alley(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 2)
        s->secondary_left_alley_trigger[idx] = (uint8_t)val;
    return 0;
}

/*=============================================================================
 * HRAM Field Accessors
 * Access nested HRAMState fields via string names.
 *===========================================================================*/

static int api_get_hram(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    if (strcmp(name, "scy") == 0)           lua_pushinteger(L, s->hram.scy);
    else if (strcmp(name, "scx") == 0)      lua_pushinteger(L, s->hram.scx);
    else if (strcmp(name, "frame_counter") == 0) lua_pushinteger(L, s->hram.frame_counter);
    else if (strcmp(name, "wy") == 0)       lua_pushinteger(L, s->hram.wy);
    else if (strcmp(name, "wx") == 0)       lua_pushinteger(L, s->hram.wx);
    else if (strcmp(name, "lcdc") == 0)     lua_pushinteger(L, s->hram.lcdc);
    else if (strcmp(name, "ball_x_pos") == 0) lua_pushinteger(L, s->hram.ball_x_pos);
    else if (strcmp(name, "ball_y_pos") == 0) lua_pushinteger(L, s->hram.ball_y_pos);
    else return luaL_error(L, "unknown hram field: '%s'", name);
    return 1;
}

static int api_set_hram(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    lua_Integer val = luaL_checkinteger(L, 2);
    if (strcmp(name, "scy") == 0)           s->hram.scy = (uint8_t)val;
    else if (strcmp(name, "scx") == 0)      s->hram.scx = (uint8_t)val;
    else if (strcmp(name, "wy") == 0)       s->hram.wy = (uint8_t)val;
    else if (strcmp(name, "wx") == 0)       s->hram.wx = (uint8_t)val;
    else if (strcmp(name, "lcdc") == 0)     s->hram.lcdc = (uint8_t)val;
    else return luaL_error(L, "unknown hram field: '%s'", name);
    return 0;
}

/*=============================================================================
 * C Helper Function Wrappers
 * Expose internal C graphics/logic functions to Lua scripts.
 *===========================================================================*/

/* Graphics loaders */
static int api_load_field_structure_graphics(lua_State *L) {
    load_field_structure_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_cave_lights_graphics(lua_State *L) {
    load_cave_lights_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_bumper_graphics(lua_State *L) {
    load_bumper_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_diglett_graphics(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t idx = (uint8_t)luaL_checkinteger(L, 1);
    load_diglett_graphics(s, idx);
    return 0;
}

static int api_load_diglett_number_graphics(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t idx = (uint8_t)luaL_checkinteger(L, 1);
    load_diglett_number_graphics(s, idx);
    return 0;
}

static int api_load_slot_cave_cover_graphics(lua_State *L) {
    load_slot_cave_cover_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_pokeball_indicator_graphics(lua_State *L) {
    load_pokeball_indicator_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_again_text_graphics(lua_State *L) {
    load_again_text_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_bonus_mult_railing_gfx(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t val = (uint8_t)luaL_checkinteger(L, 1);
    load_bonus_mult_railing_gfx(s, val);
    return 0;
}

static int api_load_upgrade_triggers_graphics(lua_State *L) {
    load_upgrade_triggers_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_staryu_graphics_top(lua_State *L) {
    load_staryu_graphics_top(script_get_game_state(L));
    return 0;
}

static int api_load_staryu_graphics_bottom(lua_State *L) {
    load_staryu_graphics_bottom(script_get_game_state(L));
    return 0;
}

static int api_load_arrow_indicator_graphics(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t i = (uint8_t)luaL_checkinteger(L, 1);
    uint8_t st = (uint8_t)luaL_checkinteger(L, 2);
    load_arrow_indicator_graphics(s, i, st);
    return 0;
}

static int api_load_evolution_trinket_graphics(lua_State *L) {
    load_evolution_trinket_graphics(script_get_game_state(L));
    return 0;
}

static int api_load_stage_collision_attributes(lua_State *L) {
    load_stage_collision_attributes(script_get_game_state(L));
    return 0;
}

static int api_load_billboard_status_bar_graphics(lua_State *L) {
    load_billboard_status_bar_graphics(script_get_game_state(L));
    return 0;
}

static int api_update_spinner_charge_graphics(lua_State *L) {
    update_spinner_charge_graphics(script_get_game_state(L));
    return 0;
}

static int api_update_field_structures(lua_State *L) {
    update_field_structures(script_get_game_state(L));
    return 0;
}

static int api_draw_ball_saver_icon(lua_State *L) {
    draw_ball_saver_icon(script_get_game_state(L));
    return 0;
}

/* Game logic helpers */
static int api_add_extra_ball(lua_State *L) {
    add_extra_ball(script_get_game_state(L));
    return 0;
}

static int api_show_extra_ball_message(lua_State *L) {
    show_extra_ball_message(script_get_game_state(L));
    return 0;
}

static int api_check_special_mode_collision(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t id = (uint8_t)luaL_checkinteger(L, 1);
    bool consumed = check_special_mode_collision(s, id);
    lua_pushboolean(L, consumed);
    return 1;
}

static int api_start_slot_roulette(lua_State *L) {
    start_slot_roulette(script_get_game_state(L));
    return 0;
}

static int api_update_slot_roulette(lua_State *L) {
    update_slot_roulette(script_get_game_state(L));
    return 0;
}

static int api_update_ball_saver(lua_State *L) {
    update_ball_saver(script_get_game_state(L));
    return 0;
}

static int api_update_again_text(lua_State *L) {
    update_again_text(script_get_game_state(L));
    return 0;
}

static int api_update_cave_lights_blinking(lua_State *L) {
    update_cave_lights_blinking(script_get_game_state(L));
    return 0;
}

static int api_update_arrow_indicators(lua_State *L) {
    update_arrow_indicators(script_get_game_state(L));
    return 0;
}

static int api_open_slot_cave(lua_State *L) {
    open_slot_cave(script_get_game_state(L));
    return 0;
}

static int api_apply_slot_force_field(lua_State *L) {
    apply_slot_force_field(script_get_game_state(L));
    return 0;
}

static int api_start_catchem_mode(lua_State *L) {
    start_catchem_mode(script_get_game_state(L));
    return 0;
}

static int api_start_evolution_mode(lua_State *L) {
    start_evolution_mode(script_get_game_state(L));
    return 0;
}

static int api_start_map_move_mode(lua_State *L) {
    start_map_move_mode(script_get_game_state(L));
    return 0;
}

static int api_conclude_special_mode(lua_State *L) {
    conclude_special_mode_red_field(script_get_game_state(L));
    return 0;
}

static int api_load_scrolling_map_name_text(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int prefix = (int)luaL_checkinteger(L, 1);
    load_scrolling_map_name_text(s, prefix);
    return 0;
}

static int api_fill_bottom_message_black(lua_State *L) {
    fill_bottom_message_buffer_with_black_tile(script_get_game_state(L));
    return 0;
}

static int api_enable_bottom_text(lua_State *L) {
    enable_bottom_text(script_get_game_state(L));
    return 0;
}

static int api_load_red_field_bottom_graphics(lua_State *L) {
    load_red_field_bottom_graphics(script_get_game_state(L));
    return 0;
}

static int api_increment_max100(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *field = luaL_checkstring(L, 1);
    const StateFieldEntry *f = find_field(field);
    if (!f || f->type != FIELD_U8) return luaL_error(L, "unknown u8 field: '%s'", field);
    uint8_t *ptr = (uint8_t *)((uint8_t *)s + f->offset);
    lua_pushinteger(L, increment_max100(ptr));
    return 1;
}

static int api_rumble(lua_State *L) {
    GameState *s = script_get_game_state(L);
    s->rumble_pattern = (uint8_t)luaL_checkinteger(L, 1);
    s->rumble_duration = (uint8_t)luaL_checkinteger(L, 2);
    return 0;
}

static int api_get_visited_map(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < 7) {
        lua_pushinteger(L, s->visited_maps[idx]);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int api_set_visited_map(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int idx = (int)luaL_checkinteger(L, 1);
    int val = (int)luaL_checkinteger(L, 2);
    if (idx >= 0 && idx < 7) {
        s->visited_maps[idx] = (uint8_t)val;
    }
    return 0;
}

/* Input: key-config-aware checks */
static int api_is_key_pressed(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *action = luaL_checkstring(L, 1);
    KeyConfig *kc = NULL;
    if (strcmp(action, "left_flipper") == 0) kc = &s->key_config_left_flipper;
    else if (strcmp(action, "right_flipper") == 0) kc = &s->key_config_right_flipper;
    else if (strcmp(action, "ball_start") == 0) kc = &s->key_config_ball_start;
    else if (strcmp(action, "left_tilt") == 0) kc = &s->key_config_left_tilt;
    else if (strcmp(action, "right_tilt") == 0) kc = &s->key_config_right_tilt;
    else if (strcmp(action, "upper_tilt") == 0) kc = &s->key_config_upper_tilt;
    else if (strcmp(action, "menu") == 0) kc = &s->key_config_menu;
    else return luaL_error(L, "unknown action: '%s'", action);

    lua_pushboolean(L, joypad_is_key_pressed(s, kc));
    return 1;
}

static int api_is_key_held(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *action = luaL_checkstring(L, 1);
    KeyConfig *kc = NULL;
    if (strcmp(action, "left_flipper") == 0) kc = &s->key_config_left_flipper;
    else if (strcmp(action, "right_flipper") == 0) kc = &s->key_config_right_flipper;
    else if (strcmp(action, "ball_start") == 0) kc = &s->key_config_ball_start;
    else if (strcmp(action, "left_tilt") == 0) kc = &s->key_config_left_tilt;
    else if (strcmp(action, "right_tilt") == 0) kc = &s->key_config_right_tilt;
    else if (strcmp(action, "upper_tilt") == 0) kc = &s->key_config_upper_tilt;
    else if (strcmp(action, "menu") == 0) kc = &s->key_config_menu;
    else return luaL_error(L, "unknown action: '%s'", action);

    lua_pushboolean(L, joypad_is_key_held(s, kc));
    return 1;
}

/* Named sprite drawing: push sprites from C sprite_data.h arrays by name */
static int api_draw_named_sprite(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *name = luaL_checkstring(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int x = (int)luaL_checkinteger(L, 3);

    /* Look up sprite data by name. The sprite_data.h arrays contain
     * {count, y_off, x_off, tile, attr, ...} format entries.
     * load_sprite_data() handles pushing them to the OAM buffer. */
    const uint8_t *data = NULL;

    /* Ball spin sprites (8 rotation frames) */
    if (strncmp(name, "ball_spin_", 10) == 0) {
        int idx = name[10] - '0';
        if (idx >= 0 && idx < 8) data = ball_spin_sprites[idx];
    }
    /* Left flipper sprites (by angle index) */
    else if (strncmp(name, "left_flipper_", 13) == 0) {
        int idx = atoi(name + 13);
        if (idx >= 0 && idx <= 20) data = left_flipper_sprites_by_angle[idx];
    }
    /* Right flipper sprites (by angle index) */
    else if (strncmp(name, "right_flipper_", 14) == 0) {
        int idx = atoi(name + 14);
        if (idx >= 0 && idx <= 20) data = right_flipper_sprites_by_angle[idx];
    }
    /* Singleton sprites (no index) */
    else if (strcmp(name, "voltorb_stationary") == 0) data = sprite_voltorb_stationary;
    else if (strcmp(name, "voltorb_collision") == 0) data = sprite_voltorb_collision;
    else if (strcmp(name, "bellsprout_body") == 0) data = sprite_bellsprout_body;

    if (data) {
        load_sprite_data(s, data, (uint8_t)y, (uint8_t)x);
    } else {
        fprintf(stderr, "[Script] Unknown sprite name: '%s'\n", name);
    }
    return 0;
}

/* Load sprite data from C arrays by name and index */
static int api_load_sprite_data(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *array_name = luaL_checkstring(L, 1);
    int index = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int x = (int)luaL_checkinteger(L, 4);

    const uint8_t *data = NULL;

    if (strcmp(array_name, "ball_spin") == 0 && index >= 0 && index < 8)
        data = ball_spin_sprites[index];
    else if (strcmp(array_name, "left_flipper") == 0 && index >= 0 && index <= 20)
        data = left_flipper_sprites_by_angle[index];
    else if (strcmp(array_name, "right_flipper") == 0 && index >= 0 && index <= 20)
        data = right_flipper_sprites_by_angle[index];
    else if (strcmp(array_name, "pikachu_saver") == 0 && index >= 0 && index < 9)
        data = pikachu_saver_sprites[index];
    else if (strcmp(array_name, "spinner") == 0 && index >= 0 && index < 6)
        data = spinner_sprites[index];
    else if (strcmp(array_name, "ditto") == 0 && index >= 0 && index < 8)
        data = ditto_sprites_by_state[index];
    else if (strcmp(array_name, "timer_digit") == 0 && index >= 0 && index < 11)
        data = timer_digit_sprites[index];
    else if (strcmp(array_name, "ball_capture") == 0 && index >= 0 && index < 13)
        data = ball_capture_sprites[index];
    else if (strcmp(array_name, "animated_mon") == 0 && index >= 0 && index < 12)
        data = animated_mon_sprites[index];
    else if (strcmp(array_name, "slot_glow") == 0 && index >= 0 && index < 3)
        data = slot_glow_sprites[index];
    else if (strcmp(array_name, "bellsprout_head") == 0 && index >= 0 && index < 4)
        data = bellsprout_head_sprites[index];
    else if (strcmp(array_name, "staryu") == 0 && index >= 0 && index < 2)
        data = staryu_sprites[index];
    else if (strcmp(array_name, "voltorb_shake") == 0 && index >= 0 && index < 3) {
        static const uint8_t *const vshake[3] = {
            sprite_voltorb_shake_1, sprite_voltorb_shake_2, sprite_voltorb_shake_3
        };
        data = vshake[index];
    }
    else if (strcmp(array_name, "evo_arrow_top") == 0 && index >= 0 && index < 6) {
        static const uint8_t *const ea_top[6] = {
            sprite_evo_arrow_top_up, sprite_evo_arrow_top_right_down,
            sprite_evo_arrow_top_down, sprite_evo_arrow_top_left_up,
            sprite_evo_arrow_top_right_up, sprite_evo_arrow_top_up_right_up,
        };
        data = ea_top[index];
    }
    else if (strcmp(array_name, "evo_arrow_bot") == 0 && index >= 0 && index < 8) {
        static const uint8_t *const ea_bot[8] = {
            sprite_evo_arrow_bot_up_left, sprite_evo_arrow_bot_up_right,
            sprite_evo_arrow_bot_left, sprite_evo_arrow_bot_right,
            sprite_evo_arrow_bot_down_left, sprite_evo_arrow_bot_down_left,
            sprite_evo_arrow_bot_down_right, sprite_evo_arrow_bot_down_right,
        };
        data = ea_bot[index];
    }
    else if (strcmp(array_name, "evo_trinket_top") == 0 && index >= 1 && index <= 7)
        data = evo_trinket_top_sprites[index];
    else if (strcmp(array_name, "evo_trinket_bot") == 0 && index >= 1 && index <= 7)
        data = evo_trinket_bot_sprites[index];

    if (data)
        load_sprite_data(s, data, (uint8_t)y, (uint8_t)x);
    return 0;
}

/* Billboard operations */
static int api_load_billboard_tile_data(lua_State *L) {
    GameState *s = script_get_game_state(L);
    uint8_t index = (uint8_t)luaL_checkinteger(L, 1);
    load_billboard_tile_data(s, index);
    return 0;
}

static int api_load_map_billboard_tile_data(lua_State *L) {
    load_map_billboard_tile_data(script_get_game_state(L));
    return 0;
}

static int api_process_billboard_illumination(lua_State *L) {
    process_billboard_illumination(script_get_game_state(L));
    return 0;
}

/* Load ball graphics (type-dependent) */
static int api_load_ball_gfx(lua_State *L) {
    load_ball_gfx(script_get_game_state(L));
    return 0;
}

/* Mini/super-mini ball graphics for slot/ditto shrink animations */
static int api_load_mini_ball_gfx(lua_State *L) {
    load_mini_ball_gfx(script_get_game_state(L));
    return 0;
}

static int api_load_super_mini_ball_gfx(lua_State *L) {
    load_super_mini_ball_gfx(script_get_game_state(L));
    return 0;
}

/* Grey billboard palette (for slot roulette / map selection) */
static int api_load_grey_billboard_palette(lua_State *L) {
    load_grey_billboard_palette(script_get_game_state(L));
    return 0;
}

/* Billboard "off" picture (greyed-out version for roulette) */
static int api_show_billboard_off(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int pic_id = (int)luaL_checkinteger(L, 1);
    load_billboard_off_picture(s, (uint8_t)pic_id);
    return 0;
}

/* Direct collision map byte write (for diglett collision state changes) */
static int api_set_collision_map(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int offset = (int)luaL_checkinteger(L, 1);
    int value = (int)luaL_checkinteger(L, 2);
    if (offset >= 0 && offset < 0x300)
        s->stage_collision_map[offset] = (uint8_t)value;
    return 0;
}

/* Direct bottom message text byte write (for BCD digit insertion) */
static int api_set_bottom_message_byte(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int offset = (int)luaL_checkinteger(L, 1);
    int value = (int)luaL_checkinteger(L, 2);
    if (offset >= 0 && offset < 256)
        s->bottom_message_text[offset] = (uint8_t)value;
    return 0;
}

/* Full scrolling text with custom header (6-byte table) */
static int api_show_scrolling_text_full(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int slot = (int)luaL_checkinteger(L, 1);
    uint8_t header[6];
    luaL_checktype(L, 2, LUA_TTABLE);
    for (int i = 0; i < 6; i++) {
        lua_rawgeti(L, 2, i + 1);
        header[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    const char *text = luaL_checkstring(L, 3);
    if (slot >= 0 && slot < 3)
        load_scrolling_text(s, slot, header, text);
    return 0;
}

/* Scrolling text with BCD value formatting (for score displays) */
static int api_show_scrolling_text_bcd(lua_State *L) {
    GameState *s = script_get_game_state(L);
    int slot = (int)luaL_checkinteger(L, 1);
    uint8_t header[6];
    luaL_checktype(L, 2, LUA_TTABLE);
    for (int i = 0; i < 6; i++) {
        lua_rawgeti(L, 2, i + 1);
        header[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    uint8_t bcd[4];
    luaL_checktype(L, 3, LUA_TTABLE);
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, 3, i + 1);
        bcd[i] = (uint8_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    if (slot >= 0 && slot < 3)
        load_scrolling_text_with_bcd_public(s, slot, header, bcd);
    return 0;
}

/*=============================================================================
 * Asset Path Overrides (Billboard, Collision, Tiles, Tilemaps)
 *===========================================================================*/

/* pinball.set_billboard_pic_path(pic_id, path) */
static int api_set_billboard_pic_path(lua_State *L) {
    int pic_id = (int)luaL_checkinteger(L, 1);
    const char *path = luaL_checkstring(L, 2);
    billboard_set_pic_override((uint8_t)pic_id, path);
    return 0;
}

/* pinball.set_billboard_td_path(td_index, path) */
static int api_set_billboard_td_path(lua_State *L) {
    int td_index = (int)luaL_checkinteger(L, 1);
    const char *path = luaL_checkstring(L, 2);
    billboard_set_td_override((uint8_t)td_index, path);
    return 0;
}

/* pinball.clear_billboard_overrides() */
static int api_clear_billboard_overrides(lua_State *L) {
    (void)L;
    billboard_clear_overrides();
    return 0;
}

/* pinball.set_collision_mask_path(path) */
static int api_set_collision_mask_path(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    collision_set_mask_override(path);
    return 0;
}

/* pinball.set_collision_map_path(path) */
static int api_set_collision_map_path(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    collision_set_map_override(path);
    return 0;
}

/* pinball.clear_collision_overrides() */
static int api_clear_collision_overrides(lua_State *L) {
    (void)L;
    collision_clear_overrides();
    return 0;
}

/* pinball.reload_collision() — re-load collision data (applies any overrides) */
static int api_reload_collision(lua_State *L) {
    GameState *s = script_get_game_state(L);
    load_stage_collision_attributes(s);
    return 0;
}

/* pinball.load_tilemap(path, dest, [bank], [size])
 * Load a binary tilemap file into VRAM bg_map. */
static int api_load_tilemap(lua_State *L) {
    GameState *s = script_get_game_state(L);
    const char *path = luaL_checkstring(L, 1);
    int dest_addr = (int)luaL_checkinteger(L, 2);
    int bank = (int)luaL_optinteger(L, 3, 0);
    int max_size = (int)luaL_optinteger(L, 4, 0);

    char full_path[520];
    snprintf(full_path, sizeof(full_path), "%s%s", s->asset_base_path, path);

    size_t data_size = 0;
    uint8_t *map_data = load_binary_file(full_path, &data_size);
    if (map_data && s->vram) {
        size_t copy_size = data_size;
        if (max_size > 0 && (size_t)max_size < copy_size)
            copy_size = (size_t)max_size;
        /* Write to the appropriate bg_map bank */
        if (dest_addr >= 0x9800 && dest_addr < 0xA000) {
            int offset = dest_addr - 0x9800;
            int map_idx = (dest_addr >= 0x9C00) ? 1 : 0;
            if (map_idx == 1) offset = dest_addr - 0x9C00;
            uint8_t *dest = s->vram->bg_map[bank] + offset;
            if (map_idx == 0) dest = s->vram->bg_map[bank] + (dest_addr - 0x9800);
            if (offset + (int)copy_size <= 0x800)
                memcpy(dest, map_data, copy_size);
        } else {
            /* Fallback: treat as tile data VRAM write */
            vram_write(s->vram, (uint8_t)bank, dest_addr, map_data, (uint16_t)copy_size);
        }
        free(map_data);
    }
    return 0;
}

/*=============================================================================
 * Registration
 *===========================================================================*/

static const luaL_Reg pinball_funcs[] = {
    /* Ball state */
    {"get_ball_x",            api_get_ball_x},
    {"get_ball_y",            api_get_ball_y},
    {"set_ball_x",            api_set_ball_x},
    {"set_ball_y",            api_set_ball_y},
    {"get_ball_x_velocity",   api_get_ball_x_velocity},
    {"get_ball_y_velocity",   api_get_ball_y_velocity},
    {"set_ball_x_velocity",   api_set_ball_x_velocity},
    {"set_ball_y_velocity",   api_set_ball_y_velocity},
    {"get_ball_spin",         api_get_ball_spin},
    {"set_ball_spin",         api_set_ball_spin},
    {"get_ball_type",         api_get_ball_type},
    {"set_ball_type",         api_set_ball_type},

    /* Stage state */
    {"get_current_stage",     api_get_current_stage},
    {"set_current_stage",     api_set_current_stage},
    {"get_screen_state",      api_get_screen_state},
    {"set_screen_state",      api_set_screen_state},

    /* Generic state access */
    {"get_state",             api_get_state},
    {"set_state",             api_set_state},

    /* Table-local storage */
    {"get_local",             api_get_local},
    {"set_local",             api_set_local},

    /* Input */
    {"is_button_held",        api_is_button_held},
    {"is_button_pressed",     api_is_button_pressed},
    {"get_joypad_state",      api_get_joypad_state},

    /* Audio */
    {"play_music",            api_play_music},
    {"play_sfx",              api_play_sfx},
    {"play_sfx_raw",          api_play_sfx_raw},
    {"play_music_raw",        api_play_music_raw},
    {"play_sfx_if_none_active", api_play_sfx_if_none_active},
    {"play_cry",              api_play_cry},
    {"play_pcm",              api_play_pcm},
    {"stop_music",            api_stop_music},
    {"play_table_music",      api_play_table_music},

    /* Score */
    {"add_score",             api_add_score},
    {"add_score_raw",         api_add_score_raw},
    {"add_score_no_mult",     api_add_score_no_mult},
    {"add_jackpot",           api_add_jackpot},
    {"add_jackpot_raw",       api_add_jackpot_raw},
    {"get_bonus_multiplier",  api_get_bonus_multiplier},
    {"set_bonus_multiplier",  api_set_bonus_multiplier},

    /* Sprites */
    {"push_sprite",           api_push_sprite},
    {"push_sprite_8x16",      api_push_sprite_8x16},
    {"clear_sprites",         api_clear_sprites},
    {"get_sprite_count",      api_get_sprite_count},

    /* VRAM & Rendering */
    {"set_bg_palette",        api_set_bg_palette},
    {"set_obj_palette",       api_set_obj_palette},
    {"set_scroll",            api_set_scroll},
    {"set_window_pos",        api_set_window_pos},
    {"load_tiles",            api_load_tiles},
    {"vram_write",            api_vram_write},

    /* Collision */
    {"get_collision_attribute", api_get_collision_attribute},
    {"set_collision_force",   api_set_collision_force},
    {"get_last_collision_id", api_get_last_collision_id},

    /* Timer */
    {"set_timer",             api_set_timer},
    {"get_timer",             api_get_timer},
    {"start_timer",           api_start_timer},
    {"stop_timer",            api_stop_timer},

    /* Text */
    {"show_scrolling_text",   api_show_scrolling_text},
    {"show_stationary_text",  api_show_stationary_text},
    {"clear_bottom_text",     api_clear_bottom_text},

    /* Billboard */
    {"show_billboard",        api_show_billboard},
    {"load_billboard_palette", api_load_billboard_palette},
    {"clear_billboard",       api_clear_billboard},

    /* Utility */
    {"random",                api_random},
    {"random_raw",            api_random_raw},
    {"frame_counter",         api_frame_counter},
    {"log",                   api_log},

    /* Array accessors */
    {"get_indicator",         api_get_indicator},
    {"set_indicator",         api_set_indicator},
    {"get_cave_light",        api_get_cave_light},
    {"set_cave_light",        api_set_cave_light},
    {"get_party_mon",         api_get_party_mon},
    {"set_party_mon",         api_set_party_mon},

    /* Animation accessors */
    {"get_anim",              api_get_anim},
    {"set_anim",              api_set_anim},

    /* Additional array accessors */
    {"get_upgrade_trigger_state", api_get_upgrade_trigger_state},
    {"set_upgrade_trigger_state", api_set_upgrade_trigger_state},
    {"get_alley_trigger",     api_get_alley_trigger},
    {"set_alley_trigger",     api_set_alley_trigger},
    {"get_evolution_object_state", api_get_evolution_object_state},
    {"set_evolution_object_state", api_set_evolution_object_state},
    {"get_trinket",           api_get_trinket},
    {"set_trinket",           api_set_trinket},
    {"get_spinner_state",     api_get_spinner_state},
    {"set_spinner_state",     api_set_spinner_state},
    {"get_secondary_alley",   api_get_secondary_alley},
    {"set_secondary_alley",   api_set_secondary_alley},
    {"get_visited_map",       api_get_visited_map},
    {"set_visited_map",       api_set_visited_map},

    /* HRAM accessors */
    {"get_hram",              api_get_hram},
    {"set_hram",              api_set_hram},

    /* Graphics loaders */
    {"load_field_structure_graphics", api_load_field_structure_graphics},
    {"load_cave_lights_graphics", api_load_cave_lights_graphics},
    {"load_bumper_graphics",  api_load_bumper_graphics},
    {"load_diglett_graphics", api_load_diglett_graphics},
    {"load_diglett_number_graphics", api_load_diglett_number_graphics},
    {"load_slot_cave_cover_graphics", api_load_slot_cave_cover_graphics},
    {"load_pokeball_indicator_graphics", api_load_pokeball_indicator_graphics},
    {"load_again_text_graphics", api_load_again_text_graphics},
    {"load_bonus_mult_railing_gfx", api_load_bonus_mult_railing_gfx},
    {"load_upgrade_triggers_graphics", api_load_upgrade_triggers_graphics},
    {"load_staryu_graphics_top", api_load_staryu_graphics_top},
    {"load_staryu_graphics_bottom", api_load_staryu_graphics_bottom},
    {"load_arrow_indicator_graphics", api_load_arrow_indicator_graphics},
    {"load_evolution_trinket_graphics", api_load_evolution_trinket_graphics},
    {"load_stage_collision_attributes", api_load_stage_collision_attributes},
    {"load_billboard_status_bar_graphics", api_load_billboard_status_bar_graphics},
    {"update_spinner_charge_graphics", api_update_spinner_charge_graphics},
    {"update_field_structures", api_update_field_structures},
    {"draw_ball_saver_icon",  api_draw_ball_saver_icon},
    {"load_red_field_bottom_graphics", api_load_red_field_bottom_graphics},

    /* Game logic helpers */
    {"add_extra_ball",        api_add_extra_ball},
    {"show_extra_ball_message", api_show_extra_ball_message},
    {"check_special_mode_collision", api_check_special_mode_collision},
    {"start_slot_roulette",   api_start_slot_roulette},
    {"update_slot_roulette",  api_update_slot_roulette},
    {"update_ball_saver",     api_update_ball_saver},
    {"update_again_text",     api_update_again_text},
    {"update_cave_lights_blinking", api_update_cave_lights_blinking},
    {"update_arrow_indicators", api_update_arrow_indicators},
    {"open_slot_cave",        api_open_slot_cave},
    {"apply_slot_force_field", api_apply_slot_force_field},
    {"start_catchem_mode",    api_start_catchem_mode},
    {"start_evolution_mode",  api_start_evolution_mode},
    {"start_map_move_mode",   api_start_map_move_mode},
    {"conclude_special_mode", api_conclude_special_mode},
    {"load_scrolling_map_name_text", api_load_scrolling_map_name_text},
    {"fill_bottom_message_black", api_fill_bottom_message_black},
    {"enable_bottom_text",    api_enable_bottom_text},
    {"increment_max100",      api_increment_max100},
    {"rumble",                api_rumble},

    /* Key-config-aware input */
    {"is_key_pressed",        api_is_key_pressed},
    {"is_key_held",           api_is_key_held},

    /* Named sprite drawing */
    {"draw_named_sprite",     api_draw_named_sprite},
    {"load_sprite_data",      api_load_sprite_data},

    /* Billboard operations */
    {"load_billboard_tile_data", api_load_billboard_tile_data},
    {"load_map_billboard_tile_data", api_load_map_billboard_tile_data},
    {"process_billboard_illumination", api_process_billboard_illumination},

    /* Ball graphics */
    {"load_ball_gfx",         api_load_ball_gfx},
    {"load_mini_ball_gfx",    api_load_mini_ball_gfx},
    {"load_super_mini_ball_gfx", api_load_super_mini_ball_gfx},

    /* Extended billboard */
    {"load_grey_billboard_palette", api_load_grey_billboard_palette},
    {"show_billboard_off",    api_show_billboard_off},

    /* Direct memory access */
    {"set_collision_map",     api_set_collision_map},
    {"set_bottom_message_byte", api_set_bottom_message_byte},

    /* Extended scrolling text */
    {"show_scrolling_text_full", api_show_scrolling_text_full},
    {"show_scrolling_text_bcd", api_show_scrolling_text_bcd},

    /* Asset path overrides */
    {"set_billboard_pic_path", api_set_billboard_pic_path},
    {"set_billboard_td_path",  api_set_billboard_td_path},
    {"clear_billboard_overrides", api_clear_billboard_overrides},
    {"set_collision_mask_path", api_set_collision_mask_path},
    {"set_collision_map_path", api_set_collision_map_path},
    {"clear_collision_overrides", api_clear_collision_overrides},
    {"reload_collision",       api_reload_collision},
    {"load_tilemap",           api_load_tilemap},

    {NULL, NULL}
};

void script_api_register(lua_State *L, GameState *state) {
    (void)state; /* State is accessed via registry */

    /* Create pinball module table */
    luaL_newlib(L, pinball_funcs);

    /* Add button constants */
    lua_pushinteger(L, 0x01); lua_setfield(L, -2, "BUTTON_A");
    lua_pushinteger(L, 0x02); lua_setfield(L, -2, "BUTTON_B");
    lua_pushinteger(L, 0x04); lua_setfield(L, -2, "BUTTON_SELECT");
    lua_pushinteger(L, 0x08); lua_setfield(L, -2, "BUTTON_START");
    lua_pushinteger(L, 0x10); lua_setfield(L, -2, "BUTTON_RIGHT");
    lua_pushinteger(L, 0x20); lua_setfield(L, -2, "BUTTON_LEFT");
    lua_pushinteger(L, 0x40); lua_setfield(L, -2, "BUTTON_UP");
    lua_pushinteger(L, 0x80); lua_setfield(L, -2, "BUTTON_DOWN");

    /* Add stage constants */
    lua_pushinteger(L, 0x0); lua_setfield(L, -2, "STAGE_RED_FIELD_TOP");
    lua_pushinteger(L, 0x1); lua_setfield(L, -2, "STAGE_RED_FIELD_BOTTOM");
    lua_pushinteger(L, 0x4); lua_setfield(L, -2, "STAGE_BLUE_FIELD_TOP");
    lua_pushinteger(L, 0x5); lua_setfield(L, -2, "STAGE_BLUE_FIELD_BOTTOM");
    lua_pushinteger(L, 0x7); lua_setfield(L, -2, "STAGE_GENGAR_BONUS");
    lua_pushinteger(L, 0x9); lua_setfield(L, -2, "STAGE_MEWTWO_BONUS");
    lua_pushinteger(L, 0xB); lua_setfield(L, -2, "STAGE_MEOWTH_BONUS");
    lua_pushinteger(L, 0xD); lua_setfield(L, -2, "STAGE_DIGLETT_BONUS");
    lua_pushinteger(L, 0xF); lua_setfield(L, -2, "STAGE_SEEL_BONUS");

    /* Ball type constants */
    lua_pushinteger(L, POKE_BALL);  lua_setfield(L, -2, "POKE_BALL");
    lua_pushinteger(L, GREAT_BALL); lua_setfield(L, -2, "GREAT_BALL");
    lua_pushinteger(L, ULTRA_BALL); lua_setfield(L, -2, "ULTRA_BALL");
    lua_pushinteger(L, MASTER_BALL); lua_setfield(L, -2, "MASTER_BALL");

    /* Special collision constants */
    lua_pushinteger(L, SPECIAL_COLLISION_NOTHING); lua_setfield(L, -2, "SPECIAL_COLLISION_NOTHING");
    lua_pushinteger(L, SPECIAL_COLLISION_LEFT_TRIGGER); lua_setfield(L, -2, "SPECIAL_COLLISION_LEFT_TRIGGER");
    lua_pushinteger(L, SPECIAL_COLLISION_RIGHT_TRIGGER); lua_setfield(L, -2, "SPECIAL_COLLISION_RIGHT_TRIGGER");
    lua_pushinteger(L, SPECIAL_COLLISION_VOLTORB); lua_setfield(L, -2, "SPECIAL_COLLISION_VOLTORB");
    lua_pushinteger(L, SPECIAL_COLLISION_BELLSPROUT); lua_setfield(L, -2, "SPECIAL_COLLISION_BELLSPROUT");
    lua_pushinteger(L, SPECIAL_COLLISION_STARYU); lua_setfield(L, -2, "SPECIAL_COLLISION_STARYU");
    lua_pushinteger(L, SPECIAL_COLLISION_LEFT_DIGLETT); lua_setfield(L, -2, "SPECIAL_COLLISION_LEFT_DIGLETT");
    lua_pushinteger(L, SPECIAL_COLLISION_RIGHT_DIGLETT); lua_setfield(L, -2, "SPECIAL_COLLISION_RIGHT_DIGLETT");
    lua_pushinteger(L, SPECIAL_COLLISION_LEFT_BONUS_MULTIPLIER); lua_setfield(L, -2, "SPECIAL_COLLISION_LEFT_BONUS_MULTIPLIER");
    lua_pushinteger(L, SPECIAL_COLLISION_RIGHT_BONUS_MULTIPLIER); lua_setfield(L, -2, "SPECIAL_COLLISION_RIGHT_BONUS_MULTIPLIER");
    lua_pushinteger(L, SPECIAL_COLLISION_BALL_UPGRADE); lua_setfield(L, -2, "SPECIAL_COLLISION_BALL_UPGRADE");
    lua_pushinteger(L, SPECIAL_COLLISION_SPINNER); lua_setfield(L, -2, "SPECIAL_COLLISION_SPINNER");
    lua_pushinteger(L, SPECIAL_COLLISION_SLOT_HOLE); lua_setfield(L, -2, "SPECIAL_COLLISION_SLOT_HOLE");

    /* Special mode constants */
    lua_pushinteger(L, SPECIAL_MODE_CATCHEM); lua_setfield(L, -2, "SPECIAL_MODE_CATCHEM");
    lua_pushinteger(L, SPECIAL_MODE_EVOLUTION); lua_setfield(L, -2, "SPECIAL_MODE_EVOLUTION");
    lua_pushinteger(L, SPECIAL_MODE_MAP_MOVE); lua_setfield(L, -2, "SPECIAL_MODE_MAP_MOVE");

    /* Billboard picture IDs */
    lua_pushinteger(L, BILLBOARD_BALL_SAVER_30); lua_setfield(L, -2, "BILLBOARD_BALL_SAVER_30");
    lua_pushinteger(L, BILLBOARD_PIKACHU_SAVER); lua_setfield(L, -2, "BILLBOARD_PIKACHU_SAVER");
    lua_pushinteger(L, BILLBOARD_EXTRA_BALL); lua_setfield(L, -2, "BILLBOARD_EXTRA_BALL");
    lua_pushinteger(L, BILLBOARD_CATCHEM_MODE); lua_setfield(L, -2, "BILLBOARD_CATCHEM_MODE");
    lua_pushinteger(L, BILLBOARD_EVOLUTION_MODE); lua_setfield(L, -2, "BILLBOARD_EVOLUTION_MODE");
    lua_pushinteger(L, BILLBOARD_GREAT_BALL); lua_setfield(L, -2, "BILLBOARD_GREAT_BALL");
    lua_pushinteger(L, BILLBOARD_BONUS_MULTIPLIER); lua_setfield(L, -2, "BILLBOARD_BONUS_MULTIPLIER");
    lua_pushinteger(L, BILLBOARD_GENGAR_BONUS); lua_setfield(L, -2, "BILLBOARD_GENGAR_BONUS");

    /* Pikachu saver */
    lua_pushinteger(L, MAX_PIKACHU_SAVER_CHARGE); lua_setfield(L, -2, "MAX_PIKACHU_SAVER_CHARGE");
    lua_pushinteger(L, MAX_BONUS_MULTIPLIER); lua_setfield(L, -2, "MAX_BONUS_MULTIPLIER");

    /* Set as global "pinball" */
    lua_setglobal(L, "pinball");
}
