#ifndef FLIPPERS_H
#define FLIPPERS_H

#include "game_state.h"

/*
 * Flipper system for the pinball engine.
 * Translated from engine/pinball_game/flippers.asm (0xe0fe).
 *
 * Flippers have 16 angular positions (0x00 to 0x0F in high byte).
 * State changes by ±0x0333 per frame based on button input.
 * Only active on bottom-half stages (stage & 1).
 */

/*
 * HandleFlippers (0xe0fe): Main entry point called once per physics frame.
 * Clears flipper collision state, updates flipper states, checks collision.
 */
void handle_flippers(GameState *state);

/*
 * Load flipper collision data (radii and normal angles) from binary files.
 * Must be called once at game init or when entering a bottom stage.
 */
void load_flipper_collision_data(GameState *state);

/*
 * Free flipper collision data.
 */
void free_flipper_collision_data(void);

#endif /* FLIPPERS_H */
