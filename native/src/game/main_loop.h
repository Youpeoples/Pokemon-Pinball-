#ifndef MAIN_LOOP_H
#define MAIN_LOOP_H

#include "game_state.h"

/*
 * Initialize the main game loop state.
 * Corresponds to the setup portion of Main in home.asm (0x1ffc).
 */
void main_loop_init(GameState *state);

/*
 * Run one frame of the main game loop.
 * Corresponds to .master_loop in home.asm:
 *   TickRumbleDuration → DoScreenLogic → CleanSpriteBuffer → ClearPersistentJoypadStates
 */
void main_loop_update(GameState *state);

/*
 * Screen handler dispatch.
 * Corresponds to DoScreenLogic in home.asm (0x2043).
 * Dispatches to the handler for wCurrentScreen.
 */
void do_screen_logic(GameState *state);

#endif /* MAIN_LOOP_H */
