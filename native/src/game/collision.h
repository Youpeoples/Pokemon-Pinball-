#ifndef COLLISION_H
#define COLLISION_H

#include "game_state.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * Tile-based collision detection for the pinball engine.
 * Translated from CheckStageCollision in home.asm (0x22b5-0x2972).
 *
 * Tests 16 points around the ball against the stage collision map
 * and collision mask bitmaps. Finds longest consecutive run of
 * colliding points to determine collision normal angle.
 */

/*
 * Load collision attributes (map + masks) for the current stage.
 * Reads .collision binary files and mask PNGs from data/collision/.
 * Must be called when stage changes or collision_state changes.
 */
void load_stage_collision_attributes(GameState *state);

/*
 * Check if the ball is colliding with stage geometry.
 * Sets state->is_ball_colliding, collision_normal_angle,
 * cur_collision_attribute, and applies position correction deltas.
 * Translated from CheckStageCollision (0x22b5).
 */
void check_stage_collision(GameState *state);

/*
 * Load special flipper-dependent collision masks for bottom stages.
 * Called during collision map loading for stages with flippers.
 */
void load_bottom_collision_masks(GameState *state);

/*
 * Lua-driven path overrides for collision data.
 * When set, the override path replaces the auto-detected collision file
 * for the current stage. Path is relative to asset_base_path.
 */
void collision_set_mask_override(const char *path);
void collision_set_map_override(const char *path);
void collision_clear_overrides(void);

/*
 * Get the currently loaded collision mask data and size.
 * Used by the editor's mask preview system for read access.
 * Returns NULL if no masks are loaded.
 */
const uint8_t *collision_get_masks(size_t *out_size);

/*
 * Get the currently loaded bottom flipper mask data.
 * is_right: false=left flipper masks, true=right flipper masks.
 * Returns NULL if not loaded.
 */
const uint8_t *collision_get_flipper_masks(bool is_right, size_t *out_size);

/*
 * Apply custom mask data from the editor.
 * Copies the provided mask data over the currently loaded masks at the given
 * attribute offset. Used when the mask editor modifies masks in real-time.
 */
void collision_apply_custom_mask(uint8_t attr, const uint8_t *mask_data, int mask_bytes);

#endif /* COLLISION_H */
