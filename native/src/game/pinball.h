#ifndef PINBALL_H
#define PINBALL_H

#include "game_state.h"

/*
 * Handle pinball game screen logic.
 * Translated from HandlePinballGame in engine/pinball_game.asm (0xD853).
 * Dispatches to sub-functions based on wScreenState:
 *   0 = LoadGFX, 1 = StartBall, 2 = HandleBallPhysics,
 *   3 = HandleBallLoss, 4 = EndBall
 */
void handle_pinball_game(GameState *state);

#endif /* PINBALL_H */
