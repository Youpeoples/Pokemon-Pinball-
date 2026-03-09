#ifndef TILT_H
#define TILT_H

#include "game_state.h"

/*
 * Handle all three tilt directions per frame.
 * Translated from HandleTilts in home/tilt.asm (0x3582).
 */
void handle_tilts(GameState *state);

/*
 * Apply tilt forces to ball velocity based on active tilt directions.
 * Translated from ApplyTiltForces in home/tilt.asm (0x36C1).
 * Builds a 3-bit direction mask from tilt states, looks up force table,
 * and adds force vector to ball velocity.
 */
void apply_tilt_forces(GameState *state);

#endif /* TILT_H */
