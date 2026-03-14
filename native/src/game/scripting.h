/*
 * Lua Scripting Engine for Pokemon Pinball+
 *
 * Embeds Lua 5.4 to make table logic fully moddable.
 * Each table (red field, blue field, bonus stages) can be driven by Lua scripts
 * instead of hardcoded C logic. The engine provides a `pinball` module with ~90
 * functions for accessing game state, audio, sprites, collision, etc.
 *
 * If no Lua scripts are found, the engine falls back to the original C code.
 */

#ifndef SCRIPTING_H
#define SCRIPTING_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct GameState GameState;
typedef struct lua_State lua_State;

/*=============================================================================
 * Script Engine
 *===========================================================================*/
typedef struct ScriptEngine {
    lua_State *L;              /* Main Lua state */
    GameState *game_state;     /* Pointer back to game state */
    bool initialized;          /* True if Lua state is ready */

    /* Loaded table info */
    char table_path[260];      /* Path to currently loaded table folder */
    bool table_loaded;         /* True if a table's scripts are active */

    /* Per-table custom music clips (loaded from manifest "music" section) */
#define TABLE_MUSIC_MAX 8
    struct {
        char key[32];       /* "stage", "catch", "evolution", etc. */
        int clip_index;     /* Index into AudioEngine.custom_clips[] */
    } table_music[TABLE_MUSIC_MAX];
    int num_table_music;

    /* Hook availability flags (cached after script load) */
    bool has_on_stage_init;
    bool has_on_ball_init;
    bool has_on_ball_loss;
    bool has_on_draw_sprites;
    bool has_on_object_collision;
    bool has_on_stage_transition;
    bool has_on_bonus_frame;
    bool has_on_slot_update;
    bool has_on_catchem_update;
    bool has_on_evolution_update;
    bool has_on_map_move_update;
    bool has_on_attribute_collision;
    bool has_on_ball_saved;
} ScriptEngine;

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

/* Initialize the scripting engine. Returns NULL on failure. */
ScriptEngine *script_engine_init(GameState *state);

/* Shut down the scripting engine and free resources. */
void script_engine_shutdown(ScriptEngine *engine);

/*=============================================================================
 * Table Loading
 *===========================================================================*/

/* Load a table from a folder path (e.g. "tables/red_field").
 * Reads manifest.json and executes all referenced Lua scripts.
 * Returns true on success. */
bool script_load_table(ScriptEngine *engine, const char *table_path);

/* Unload the current table scripts (resets Lua state for next load). */
void script_unload_table(ScriptEngine *engine);

/* Scan the tables/ directory and load the appropriate table for the given stage.
 * Maps stage IDs to table folders via manifest.json stage definitions. */
bool script_load_table_for_stage(ScriptEngine *engine, uint8_t stage_id);

/*=============================================================================
 * Hook Queries
 *===========================================================================*/

/* Check if a specific hook function is defined in the loaded scripts. */
bool script_has_hook(ScriptEngine *engine, const char *hook_name);

/*=============================================================================
 * Hook Dispatch — Called from pinball.c at each dispatch point
 *===========================================================================*/

/* Called once when a stage loads (pinball state 0: LoadGFX). */
void script_call_on_stage_init(ScriptEngine *engine, uint8_t stage_id);

/* Called once when ball starts (pinball state 1: StartBall). */
void script_call_on_ball_init(ScriptEngine *engine, uint8_t stage_id);

/* Called when ball drains (pinball state 3: HandleBallLoss). */
void script_call_on_ball_loss(ScriptEngine *engine, uint8_t stage_id);

/* Called every frame to render stage-specific sprites. */
void script_call_on_draw_sprites(ScriptEngine *engine, uint8_t stage_id);

/* Called when a game object collision is detected.
 * collision_id: the object ID that was hit
 * x, y: ball position at time of collision */
void script_call_on_object_collision(ScriptEngine *engine,
                                     uint8_t collision_id, uint16_t x, uint16_t y);

/* Called when stage transitions (top<->bottom scroll). */
void script_call_on_stage_transition(ScriptEngine *engine,
                                     uint8_t from_stage, uint8_t to_stage);

/* Called every frame for bonus stage updates. */
void script_call_on_bonus_frame(ScriptEngine *engine);

/* Called for slot machine state updates. */
void script_call_on_slot_update(ScriptEngine *engine, uint8_t slot_state);

/* Called for catch'em mode state updates. */
void script_call_on_catchem_update(ScriptEngine *engine, uint8_t catchem_state);

/* Called for evolution mode state updates. */
void script_call_on_evolution_update(ScriptEngine *engine, uint8_t evo_state);

/* Called for map move state updates. */
void script_call_on_map_move_update(ScriptEngine *engine, uint8_t map_state);

/* Called when collision attribute hit detected. */
void script_call_on_attribute_collision(ScriptEngine *engine, uint8_t attr_id);

/* Called when ball saver activates. */
void script_call_on_ball_saved(ScriptEngine *engine);

/*=============================================================================
 * Lua API Registration (implemented in script_api.c)
 *===========================================================================*/

/* Register all pinball.* functions into the Lua state. */
void script_api_register(lua_State *L, GameState *state);

#endif /* SCRIPTING_H */
