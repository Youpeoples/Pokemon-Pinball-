#ifndef BLUE_FIELD_H
#define BLUE_FIELD_H

#include "game_state.h"

/*
 * Blue Field game objects and collision handling.
 * Translated from engine/pinball_game/object_collision/blue_stage_resolve_collision.asm
 * and engine/pinball_game/object_collision/blue_stage_object_collision.asm.
 */

/*
 * Initialize blue field game state.
 * Sets starting lives, map, collision state, multiplier.
 * Translated from init_blue_field.asm (0x1c000).
 */
void init_blue_field(GameState *state);

/*
 * Check game object collisions for the blue field stages.
 * Tests ball against all active objects using bounding box checks.
 * Dispatches by stage (top vs bottom).
 */
void check_blue_field_object_collisions(GameState *state);

/*
 * Resolve game object collisions for the blue field.
 * Called after check, handles shellder bounce, trigger activation, etc.
 */
void resolve_blue_field_object_collisions(GameState *state);

/*
 * Load dynamic BG tilemap graphics for blue field bottom stage.
 * Restores bumper, CAVE light, arrow indicator, psyduck/poliwag tiles.
 * Called after stage transition reload.
 */
void load_blue_field_bottom_graphics(GameState *state);

/*
 * Clear all blue field indicators (Func_1c2cb).
 * Clears blink flags and reloads BG tilemap tiles for all 5 indicators.
 */
void clear_all_blue_indicators(GameState *state);

/*
 * Blue field-specific map move mode initialization (Func_31326).
 * Loads Psyduck/Poliwag graphics, modifies collision map, reloads indicators.
 */
void start_map_move_blue_init(GameState *state);

/*
 * End any active special mode and restore collision state.
 * Called during ball loss flow.
 * From ConcludeSpecialMode_BlueField (0xded6).
 */
void conclude_special_mode_blue_field(GameState *state);

/*
 * Initialize ball for blue field (normal start or bonus return).
 * From InitBallBlueField (0x1c08d).
 */
void init_ball_blue_field(GameState *state);

/*
 * Load all stage data for blue field top stage.
 * Matches _LoadStageDataBlueFieldTop (0x1c165) call list.
 */
void load_stage_data_blue_field_top(GameState *state);

/*
 * Load all stage data for blue field bottom stage.
 * Matches _LoadStageDataBlueFieldBottom (0x1c191) call list.
 */
void load_stage_data_blue_field_bottom(GameState *state);

/*
 * Load slot cave cover graphics for blue field.
 * Uses blue-field-specific tile IDs (different from red field).
 * From LoadSlotCaveCoverGraphics_BlueField (0x1e8f6).
 */
void load_slot_cave_cover_graphics_blue(GameState *state);

#endif /* BLUE_FIELD_H */
