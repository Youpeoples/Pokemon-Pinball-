#ifndef JOYPAD_H
#define JOYPAD_H

#include "game_state.h"
#include "platform/platform.h"

/*
 * Update joypad state from platform input.
 * Translated from ReadJoypad in home/joypad.asm (0xAB8).
 * Tracks current, previous, newly-pressed, and held-repeat states.
 */
void joypad_update(GameState *state, Platform *platform);

/*
 * Clear persistent joypad accumulator states.
 * Translated from ClearPersistentJoypadStates in home/joypad.asm (0xB2E).
 */
void joypad_clear_persistent(GameState *state);

/*
 * Check if a key config action is pressed (newly pressed check).
 * Translated from IsKeyPressed in home/joypad.asm (0xB4C).
 * Returns true if the key combo for the given config is newly pressed.
 */
bool joypad_is_key_pressed(GameState *state, const KeyConfig *config);

/*
 * Check if a key config action is held (no newly-pressed requirement).
 * Translated from IsKeyPressed2 in home/joypad.asm (0xB36).
 */
bool joypad_is_key_held(GameState *state, const KeyConfig *config);

#endif /* JOYPAD_H */
