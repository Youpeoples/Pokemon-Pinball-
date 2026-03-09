#ifndef COLLISION_H
#define COLLISION_H

#include "game_state.h"

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

#endif /* COLLISION_H */
