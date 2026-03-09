#ifndef DRAW_BLUE_FIELD_H
#define DRAW_BLUE_FIELD_H

#include "game/game_state.h"

/*
 * Draw all sprites for the current blue field stage.
 * Dispatches to top or bottom drawing based on current_stage.
 * Translated from draw_blue_field_sprites.asm.
 */
void draw_blue_field_sprites(GameState *state);

#endif /* DRAW_BLUE_FIELD_H */
