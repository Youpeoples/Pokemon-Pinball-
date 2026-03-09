#ifndef RED_FIELD_H
#define RED_FIELD_H

#include "game_state.h"

/*
 * Red Field game objects and collision handling.
 * Translated from data/collision/game_objects/red_stage_game_object_collision.asm
 * and engine/pinball_game/object_collision/object_collision.asm.
 */

/*
 * Initialize red field game state.
 * Sets starting lives, map, collision state, multiplier.
 * Translated from init_red_field.asm (0x30000).
 */
void init_red_field(GameState *state);

/*
 * Check game object collisions for the red field stages.
 * Tests ball against all active objects using bounding box checks.
 * Dispatches by stage (top vs bottom).
 */
void check_red_field_object_collisions(GameState *state);

/*
 * Resolve game object collisions for the red field.
 * Called after check, handles bumper bounce, trigger activation, etc.
 */
void resolve_red_field_object_collisions(GameState *state);

/*
 * Update BG tilemap for red field structures (Ditto, guard rail, voltorb roof).
 * Called after stage_collision_state changes. From LoadFieldStructureGraphics_RedField (0x159f4).
 */
void load_field_structure_graphics(GameState *state);

/*
 * Load dynamic BG tilemap graphics for red field bottom stage.
 * Restores Diglett, bumper, CAVE light, and arrow indicator tiles.
 * Called after stage transition reload. From _LoadStageDataRedFieldBottom.
 */
void load_red_field_bottom_graphics(GameState *state);

/*
 * End any active special mode and restore collision state.
 * Called during ball loss flow.
 * From ConcludeSpecialMode_RedField (0xddfd).
 */
void conclude_special_mode_red_field(GameState *state);

/*
 * Start the 20-second ball saver timer.
 * Called after ball loss to protect the next ball.
 * From Start20SecondSaverTimer (0xdbba).
 */
void start_20_second_saver_timer(GameState *state);

/*
 * Load all stage data for red field top stage.
 * Matches _LoadStageDataRedFieldTop (0x14000) call list.
 */
void load_stage_data_red_field_top(GameState *state);

/*
 * Load all stage data for red field bottom stage.
 * Matches _LoadStageDataRedFieldBottom (0x1401c) call list.
 */
void load_stage_data_red_field_bottom(GameState *state);

/*=============================================================================
 * Shared functions used by both red and blue fields
 *===========================================================================*/

/* CheckSpecialModeColision (0x14e79) — dispatch collision to active special mode.
 * Returns true if special mode consumed the collision (ASM: carry set). */
bool check_special_mode_collision(GameState *state, uint8_t collision_id);

/* Increment_Max100 (0xe4a) — increment *val capped at 100, returns 1 if incremented */
uint8_t increment_max100(uint8_t *val);

/* AddExtraBall (0x14086) — add an extra ball */
void add_extra_ball(GameState *state);

/* ShowExtraBallMessage (0x14092) — show "EXTRA BALL" scrolling text if pending */
void show_extra_ball_message(GameState *state);

/* UpdateBallSaver (0xdba0) — update ball saver timer + pikachu icon on bottom */
void update_ball_saver(GameState *state);

/* UpdateAgainText — update "AGAIN" text visibility */
void update_again_text(GameState *state);

/* LoadWildMonCollisionMask (0x10464) — load per-species 1bpp collision mask */
void load_wild_mon_collision_mask(GameState *state);

/* StartCatchEmMode (0x1003f) — begin catch'em special mode */
void start_catchem_mode(GameState *state);

/* StartEvolutionMode (0x100a3) — begin evolution special mode */
void start_evolution_mode(GameState *state);
void update_evolution_menu(GameState *state);  /* H4: per-frame evolution selection menu */

/* StartMapMoveMode (0x10112) — begin map move special mode */
void start_map_move_mode(GameState *state);

/* LoadScrollingMapNameText — show "START FROM [map]" or "HEADING FOR [map]" bottom text */
void load_scrolling_map_name_text(GameState *state, int prefix_type);

/* Slot roulette state machine states (DoSlotRewardRoulette, 0xed8e) */
enum {
    SLOT_ROULETTE_WAIT_FLIPPERS,
    SLOT_ROULETTE_INIT,
    SLOT_ROULETTE_SPIN_ENTRY,
    SLOT_ROULETTE_SPIN_DELAY,
    SLOT_ROULETTE_STOPPED_SFX,
    SLOT_ROULETTE_STOPPED_DISPLAY,
    SLOT_ROULETTE_STOPPED_BLINK,
    SLOT_ROULETTE_REWARD_INIT,
    SLOT_ROULETTE_REWARD_BLINK,
    SLOT_ROULETTE_REWARD_FINISH,
    SLOT_ROULETTE_DONE,
};

/* DoSlotRewardRoulette — start the slot roulette state machine */
void start_slot_roulette(GameState *state);

/* UpdateSlotRoulette — non-blocking state machine for slot roulette */
void update_slot_roulette(GameState *state);

/*=============================================================================
 * Graphics reload functions — shared between red and blue fields.
 * Called from _LoadStageDataXxxFieldBottom during stage transitions.
 *===========================================================================*/

/* DrawBallSaverIcon — writes ball saver on/off tiles to bg_map */
void draw_ball_saver_icon(GameState *state);

/* LoadBillboardStatusBarGraphics — pokeball indicators below billboard */
void load_billboard_status_bar_graphics(GameState *state);

/* LoadPokeballIndicatorGraphics — update pokeball count tilemap entries */
void load_pokeball_indicator_graphics(GameState *state);

/* LoadEvolutionTrinketGraphics — trinket tiles when in evolution mode */
void load_evolution_trinket_graphics(GameState *state);

/* Func_1414b — special mode sprite/tile management on stage transition */
void func_1414b_special_mode_gfx(GameState *state);

/* Func_10184 — process billboard illumination state changes into VRAM tile writes */
void process_billboard_illumination(GameState *state);

/* Free cached billboard tile data (call when leaving catch'em mode) */
void free_cached_billboard_data(void);

/* LoadSlotCaveCoverGraphics — slot cave open/closed tiles (stage-aware) */
void load_slot_cave_cover_graphics(GameState *state);

/* LoadAgainTextGraphics — "AGAIN" text on/off (stage-aware for PNG path) */
void load_again_text_graphics(GameState *state);

/* LoadBillboardTileData / LoadMapBillboardTileData — from billboard.c */
void load_billboard_tile_data(GameState *state, uint8_t index);
void load_map_billboard_tile_data(GameState *state);

/* Func_32cc equivalent: load scrolling text with formatted BCD value.
 * Sets up scrolling text slot from header, formats BCD into message buffer. */
void load_scrolling_text_with_bcd_public(GameState *state, int slot_index,
                                          const uint8_t *header,
                                          const uint8_t bcd[4]);

/*=============================================================================
 * Pokedex animated sprite accessors (MonAnimatedSpriteTypes 0x13429).
 * Used by pokedex screen to animate mon image when Start is pressed.
 *===========================================================================*/

/* Get animated sprite type for a pokemon (0xFF = no animation, bit 7 set) */
uint8_t get_mon_animated_sprite_type(uint8_t pokedex_index);

/* Get catch sprite frame durations for a pokemon's animated sprite.
 * Returns idle1, idle2, hit frame durations for the animation. */
void get_catch_sprite_frame_durations_for_mon(uint8_t pokedex_index,
                                               uint8_t *idle1, uint8_t *idle2,
                                               uint8_t *hit);

#endif /* RED_FIELD_H */
