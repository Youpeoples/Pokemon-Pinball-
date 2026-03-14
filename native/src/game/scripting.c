/*
 * Lua Scripting Engine Implementation
 *
 * Manages the Lua 5.4 runtime, loads table scripts, and dispatches
 * hook callbacks from the C pinball engine to Lua functions.
 *
 * Error handling: All Lua errors are caught and logged. On any script error,
 * the hook is treated as "not available" and the C fallback runs instead.
 */

#include "game/scripting.h"
#include "game/game_state.h"
#include "game/billboard.h"
#include "game/collision.h"
#include "audio/audio.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* cJSON for manifest parsing */
#include "cJSON.h"

/*=============================================================================
 * Internal: Store GameState pointer in Lua registry
 *===========================================================================*/
#define SCRIPT_GAME_STATE_KEY "pinball_game_state"
#define SCRIPT_ENGINE_KEY     "pinball_script_engine"

static void store_game_state(lua_State *L, GameState *state) {
    lua_pushlightuserdata(L, state);
    lua_setfield(L, LUA_REGISTRYINDEX, SCRIPT_GAME_STATE_KEY);
}

GameState *script_get_game_state(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, SCRIPT_GAME_STATE_KEY);
    GameState *state = (GameState *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return state;
}

static void store_engine(lua_State *L, ScriptEngine *engine) {
    lua_pushlightuserdata(L, engine);
    lua_setfield(L, LUA_REGISTRYINDEX, SCRIPT_ENGINE_KEY);
}

ScriptEngine *script_get_engine(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, SCRIPT_ENGINE_KEY);
    ScriptEngine *engine = (ScriptEngine *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return engine;
}

/*=============================================================================
 * Internal: Error handler for protected Lua calls
 *===========================================================================*/
static int lua_error_handler(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg == NULL) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            return 1;
        else
            msg = "(unknown error)";
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}

/*=============================================================================
 * Internal: Load and execute a Lua script file
 *===========================================================================*/
static bool execute_script_file(lua_State *L, const char *path) {
    int status = luaL_loadfile(L, path);
    if (status != LUA_OK) {
        fprintf(stderr, "[Script] Failed to load '%s': %s\n",
                path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    /* Push error handler before the chunk */
    int err_idx = lua_gettop(L) - 1;
    lua_pushcfunction(L, lua_error_handler);
    lua_insert(L, err_idx + 1);

    status = lua_pcall(L, 0, 0, err_idx + 1);
    lua_remove(L, err_idx + 1); /* Remove error handler */

    if (status != LUA_OK) {
        fprintf(stderr, "[Script] Error executing '%s': %s\n",
                path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    return true;
}

/*=============================================================================
 * Internal: Check if a global function exists in Lua
 *===========================================================================*/
static bool check_global_function(lua_State *L, const char *name) {
    lua_getglobal(L, name);
    bool exists = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return exists;
}

/*=============================================================================
 * Internal: Update hook availability flags
 *===========================================================================*/
static void update_hook_flags(ScriptEngine *engine) {
    lua_State *L = engine->L;
    engine->has_on_stage_init         = check_global_function(L, "on_stage_init");
    engine->has_on_ball_init          = check_global_function(L, "on_ball_init");
    engine->has_on_ball_loss          = check_global_function(L, "on_ball_loss");
    engine->has_on_draw_sprites       = check_global_function(L, "on_draw_sprites");
    engine->has_on_object_collision   = check_global_function(L, "on_object_collision");
    engine->has_on_stage_transition   = check_global_function(L, "on_stage_transition");
    engine->has_on_bonus_frame        = check_global_function(L, "on_bonus_frame");
    engine->has_on_slot_update        = check_global_function(L, "on_slot_update");
    engine->has_on_catchem_update     = check_global_function(L, "on_catchem_update");
    engine->has_on_evolution_update   = check_global_function(L, "on_evolution_update");
    engine->has_on_map_move_update    = check_global_function(L, "on_map_move_update");
    engine->has_on_attribute_collision = check_global_function(L, "on_attribute_collision");
    engine->has_on_ball_saved         = check_global_function(L, "on_ball_saved");
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

ScriptEngine *script_engine_init(GameState *state) {
    ScriptEngine *engine = (ScriptEngine *)calloc(1, sizeof(ScriptEngine));
    if (!engine) {
        fprintf(stderr, "[Script] Failed to allocate ScriptEngine\n");
        return NULL;
    }

    engine->game_state = state;

    /* Create Lua state with standard libraries */
    engine->L = luaL_newstate();
    if (!engine->L) {
        fprintf(stderr, "[Script] Failed to create Lua state\n");
        free(engine);
        return NULL;
    }

    luaL_openlibs(engine->L);

    /* Store references in Lua registry for API callbacks */
    store_game_state(engine->L, state);
    store_engine(engine->L, engine);

    /* Register the pinball.* API */
    script_api_register(engine->L, state);

    engine->initialized = true;
    printf("[Script] Lua 5.4 scripting engine initialized\n");
    return engine;
}

void script_engine_shutdown(ScriptEngine *engine) {
    if (!engine) return;

    if (engine->L) {
        lua_close(engine->L);
        engine->L = NULL;
    }

    printf("[Script] Scripting engine shut down\n");
    free(engine);
}

/*=============================================================================
 * Table Loading
 *===========================================================================*/

bool script_load_table(ScriptEngine *engine, const char *table_path) {
    if (!engine || !engine->initialized) return false;

    /* Build manifest path */
    char manifest_path[520];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", table_path);

    /* Read manifest file */
    FILE *f = fopen(manifest_path, "rb");
    if (!f) {
        /* No manifest = no Lua table, fall back to C */
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = (char *)malloc(size + 1);
    if (!json_str) {
        fclose(f);
        return false;
    }
    fread(json_str, 1, size, f);
    json_str[size] = '\0';
    fclose(f);

    /* Parse manifest */
    cJSON *manifest = cJSON_Parse(json_str);
    free(json_str);

    if (!manifest) {
        fprintf(stderr, "[Script] Failed to parse manifest: %s\n", manifest_path);
        return false;
    }

    /* Store table path */
    strncpy(engine->table_path, table_path, sizeof(engine->table_path) - 1);
    engine->table_path[sizeof(engine->table_path) - 1] = '\0';

    /* Set the table path as a global in Lua so scripts can find their assets */
    lua_pushstring(engine->L, table_path);
    lua_setglobal(engine->L, "TABLE_PATH");

    /* Load scripts referenced in manifest */
    cJSON *scripts = cJSON_GetObjectItem(manifest, "scripts");
    if (scripts) {
        /* Iterate all script entries */
        cJSON *script_entry = NULL;
        cJSON_ArrayForEach(script_entry, scripts) {
            if (cJSON_IsString(script_entry)) {
                char script_path[520];
                snprintf(script_path, sizeof(script_path), "%s/%s",
                         table_path, script_entry->valuestring);

                printf("[Script] Loading: %s\n", script_path);
                if (!execute_script_file(engine->L, script_path)) {
                    fprintf(stderr, "[Script] Warning: Failed to load script '%s'\n",
                            script_path);
                    /* Continue loading other scripts */
                }
            }
        }
    }

    /* Load music referenced in manifest */
    cJSON *music = cJSON_GetObjectItem(manifest, "music");
    if (music) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, music) {
            if (cJSON_IsString(entry) && engine->num_table_music < TABLE_MUSIC_MAX) {
                /* Try table-relative path first, then asset-base-relative */
                char music_path[520];
                snprintf(music_path, sizeof(music_path), "%s/%s",
                         table_path, entry->valuestring);
                int idx = audio_load_custom_clip(engine->game_state->audio, music_path);
                if (idx < 0) {
                    /* Fallback: asset-base-relative */
                    snprintf(music_path, sizeof(music_path), "%s%s",
                             engine->game_state->asset_base_path, entry->valuestring);
                    idx = audio_load_custom_clip(engine->game_state->audio, music_path);
                }
                if (idx >= 0) {
                    strncpy(engine->table_music[engine->num_table_music].key,
                            entry->string, 31);
                    engine->table_music[engine->num_table_music].key[31] = '\0';
                    engine->table_music[engine->num_table_music].clip_index = idx;
                    engine->num_table_music++;
                    printf("[Script] Table music '%s' loaded as clip %d\n",
                           entry->string, idx);
                }
            }
        }
    }

    cJSON_Delete(manifest);

    /* Update hook availability flags */
    update_hook_flags(engine);

    engine->table_loaded = true;
    printf("[Script] Table loaded from: %s\n", table_path);
    return true;
}

void script_unload_table(ScriptEngine *engine) {
    if (!engine || !engine->initialized) return;

    /* Clear any Lua-driven asset path overrides */
    billboard_clear_overrides();
    collision_clear_overrides();

    /* Close and recreate the Lua state to clean up all script globals */
    if (engine->L) {
        lua_close(engine->L);
    }

    engine->L = luaL_newstate();
    if (engine->L) {
        luaL_openlibs(engine->L);
        store_game_state(engine->L, engine->game_state);
        store_engine(engine->L, engine);
        script_api_register(engine->L, engine->game_state);
    }

    engine->table_loaded = false;
    memset(engine->table_path, 0, sizeof(engine->table_path));

    /* Clear table music */
    engine->num_table_music = 0;

    /* Clear all hook flags */
    engine->has_on_stage_init = false;
    engine->has_on_ball_init = false;
    engine->has_on_ball_loss = false;
    engine->has_on_draw_sprites = false;
    engine->has_on_object_collision = false;
    engine->has_on_stage_transition = false;
    engine->has_on_bonus_frame = false;
    engine->has_on_slot_update = false;
    engine->has_on_catchem_update = false;
    engine->has_on_evolution_update = false;
    engine->has_on_map_move_update = false;
    engine->has_on_attribute_collision = false;
    engine->has_on_ball_saved = false;
}

bool script_load_table_for_stage(ScriptEngine *engine, uint8_t stage_id) {
    if (!engine || !engine->initialized) return false;

    /* If active_table_folder is set, use it for main field stages */
    if (engine->game_state->active_table_folder[0] != '\0') {
        if (stage_id == 0x0 || stage_id == 0x1 ||
            stage_id == 0x4 || stage_id == 0x5) {
            char table_path[520];
            snprintf(table_path, sizeof(table_path), "%stables/%s",
                     engine->game_state->asset_base_path,
                     engine->game_state->active_table_folder);

            if (engine->table_loaded && strcmp(engine->table_path, table_path) == 0)
                return true;

            script_unload_table(engine);
            return script_load_table(engine, table_path);
        }
    }

    /* Map stage IDs to table folder names */
    const char *table_name = NULL;
    switch (stage_id) {
        case 0x0: case 0x1:  /* Red field top/bottom */
            table_name = "red_field";
            break;
        case 0x4: case 0x5:  /* Blue field top/bottom */
            table_name = "blue_field";
            break;
        case 0x7:  /* Gengar bonus */
            table_name = "gengar_bonus";
            break;
        case 0x9:  /* Mewtwo bonus */
            table_name = "mewtwo_bonus";
            break;
        case 0xB:  /* Meowth bonus */
            table_name = "meowth_bonus";
            break;
        case 0xD:  /* Diglett bonus */
            table_name = "diglett_bonus";
            break;
        case 0xF:  /* Seel bonus */
            table_name = "seel_bonus";
            break;
        default:
            return false;
    }

    /* Build full path: {asset_base_path}/tables/{table_name} */
    char table_path[520];
    snprintf(table_path, sizeof(table_path), "%stables/%s",
             engine->game_state->asset_base_path, table_name);

    /* Don't reload if same table already loaded */
    if (engine->table_loaded && strcmp(engine->table_path, table_path) == 0) {
        return true;
    }

    /* Unload previous table and load new one */
    script_unload_table(engine);
    return script_load_table(engine, table_path);
}

/*=============================================================================
 * Hook Queries
 *===========================================================================*/

bool script_has_hook(ScriptEngine *engine, const char *hook_name) {
    if (!engine || !engine->table_loaded) return false;
    return check_global_function(engine->L, hook_name);
}

/*=============================================================================
 * Hook Dispatch Helpers
 *===========================================================================*/

/* Call a Lua hook with no arguments. Returns true if called successfully. */
static bool call_hook_void(ScriptEngine *engine, const char *hook_name) {
    if (!engine || !engine->table_loaded || !engine->L) return false;

    lua_pushcfunction(engine->L, lua_error_handler);
    int err_idx = lua_gettop(engine->L);

    lua_getglobal(engine->L, hook_name);
    if (!lua_isfunction(engine->L, -1)) {
        lua_pop(engine->L, 2); /* pop non-function + error handler */
        return false;
    }

    int status = lua_pcall(engine->L, 0, 0, err_idx);
    lua_remove(engine->L, err_idx);

    if (status != LUA_OK) {
        fprintf(stderr, "[Script] Error in %s: %s\n",
                hook_name, lua_tostring(engine->L, -1));
        lua_pop(engine->L, 1);
        return false;
    }

    return true;
}

/* Call a Lua hook with one integer argument. */
static bool call_hook_int(ScriptEngine *engine, const char *hook_name, int arg1) {
    if (!engine || !engine->table_loaded || !engine->L) return false;

    lua_pushcfunction(engine->L, lua_error_handler);
    int err_idx = lua_gettop(engine->L);

    lua_getglobal(engine->L, hook_name);
    if (!lua_isfunction(engine->L, -1)) {
        lua_pop(engine->L, 2);
        return false;
    }

    lua_pushinteger(engine->L, arg1);
    int status = lua_pcall(engine->L, 1, 0, err_idx);
    lua_remove(engine->L, err_idx);

    if (status != LUA_OK) {
        fprintf(stderr, "[Script] Error in %s: %s\n",
                hook_name, lua_tostring(engine->L, -1));
        lua_pop(engine->L, 1);
        return false;
    }

    return true;
}

/* Call a Lua hook with three integer arguments. */
static bool call_hook_int3(ScriptEngine *engine, const char *hook_name,
                           int arg1, int arg2, int arg3) {
    if (!engine || !engine->table_loaded || !engine->L) return false;

    lua_pushcfunction(engine->L, lua_error_handler);
    int err_idx = lua_gettop(engine->L);

    lua_getglobal(engine->L, hook_name);
    if (!lua_isfunction(engine->L, -1)) {
        lua_pop(engine->L, 2);
        return false;
    }

    lua_pushinteger(engine->L, arg1);
    lua_pushinteger(engine->L, arg2);
    lua_pushinteger(engine->L, arg3);
    int status = lua_pcall(engine->L, 3, 0, err_idx);
    lua_remove(engine->L, err_idx);

    if (status != LUA_OK) {
        fprintf(stderr, "[Script] Error in %s: %s\n",
                hook_name, lua_tostring(engine->L, -1));
        lua_pop(engine->L, 1);
        return false;
    }

    return true;
}

/* Call a Lua hook with two integer arguments. */
static bool call_hook_int2(ScriptEngine *engine, const char *hook_name,
                           int arg1, int arg2) {
    if (!engine || !engine->table_loaded || !engine->L) return false;

    lua_pushcfunction(engine->L, lua_error_handler);
    int err_idx = lua_gettop(engine->L);

    lua_getglobal(engine->L, hook_name);
    if (!lua_isfunction(engine->L, -1)) {
        lua_pop(engine->L, 2);
        return false;
    }

    lua_pushinteger(engine->L, arg1);
    lua_pushinteger(engine->L, arg2);
    int status = lua_pcall(engine->L, 2, 0, err_idx);
    lua_remove(engine->L, err_idx);

    if (status != LUA_OK) {
        fprintf(stderr, "[Script] Error in %s: %s\n",
                hook_name, lua_tostring(engine->L, -1));
        lua_pop(engine->L, 1);
        return false;
    }

    return true;
}

/*=============================================================================
 * Hook Dispatch — Public API
 *===========================================================================*/

void script_call_on_stage_init(ScriptEngine *engine, uint8_t stage_id) {
    if (engine && engine->has_on_stage_init)
        call_hook_int(engine, "on_stage_init", stage_id);
}

void script_call_on_ball_init(ScriptEngine *engine, uint8_t stage_id) {
    if (engine && engine->has_on_ball_init)
        call_hook_int(engine, "on_ball_init", stage_id);
}

void script_call_on_ball_loss(ScriptEngine *engine, uint8_t stage_id) {
    if (engine && engine->has_on_ball_loss)
        call_hook_int(engine, "on_ball_loss", stage_id);
}

void script_call_on_draw_sprites(ScriptEngine *engine, uint8_t stage_id) {
    if (engine && engine->has_on_draw_sprites)
        call_hook_int(engine, "on_draw_sprites", stage_id);
}

void script_call_on_object_collision(ScriptEngine *engine,
                                     uint8_t collision_id, uint16_t x, uint16_t y) {
    if (engine && engine->has_on_object_collision)
        call_hook_int3(engine, "on_object_collision", collision_id, x, y);
}

void script_call_on_stage_transition(ScriptEngine *engine,
                                     uint8_t from_stage, uint8_t to_stage) {
    if (engine && engine->has_on_stage_transition)
        call_hook_int2(engine, "on_stage_transition", from_stage, to_stage);
}

void script_call_on_bonus_frame(ScriptEngine *engine) {
    if (engine && engine->has_on_bonus_frame)
        call_hook_void(engine, "on_bonus_frame");
}

void script_call_on_slot_update(ScriptEngine *engine, uint8_t slot_state) {
    if (engine && engine->has_on_slot_update)
        call_hook_int(engine, "on_slot_update", slot_state);
}

void script_call_on_catchem_update(ScriptEngine *engine, uint8_t catchem_state) {
    if (engine && engine->has_on_catchem_update)
        call_hook_int(engine, "on_catchem_update", catchem_state);
}

void script_call_on_evolution_update(ScriptEngine *engine, uint8_t evo_state) {
    if (engine && engine->has_on_evolution_update)
        call_hook_int(engine, "on_evolution_update", evo_state);
}

void script_call_on_map_move_update(ScriptEngine *engine, uint8_t map_state) {
    if (engine && engine->has_on_map_move_update)
        call_hook_int(engine, "on_map_move_update", map_state);
}

void script_call_on_attribute_collision(ScriptEngine *engine, uint8_t attr_id) {
    if (engine && engine->has_on_attribute_collision)
        call_hook_int(engine, "on_attribute_collision", attr_id);
}

void script_call_on_ball_saved(ScriptEngine *engine) {
    if (engine && engine->has_on_ball_saved)
        call_hook_void(engine, "on_ball_saved");
}
