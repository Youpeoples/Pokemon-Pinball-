#ifndef SEEL_BONUS_H
#define SEEL_BONUS_H

#include "game/game_state.h"

void init_seel_bonus(GameState *state);
void init_ball_seel_bonus(GameState *state);
void handle_ball_loss_seel_bonus(GameState *state);
void check_seel_bonus_object_collisions(GameState *state);
void resolve_seel_bonus_object_collisions(GameState *state);
void draw_seel_bonus_sprites(GameState *state);
void load_stage_data_seel_bonus(GameState *state);

#endif /* SEEL_BONUS_H */
