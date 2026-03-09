#ifndef DIGLETT_BONUS_H
#define DIGLETT_BONUS_H

#include "game/game_state.h"

void init_diglett_bonus(GameState *state);
void init_ball_diglett_bonus(GameState *state);
void handle_ball_loss_diglett_bonus(GameState *state);
void check_diglett_bonus_object_collisions(GameState *state);
void resolve_diglett_bonus_object_collisions(GameState *state);
void draw_diglett_bonus_sprites(GameState *state);
void load_stage_data_diglett_bonus(GameState *state);

#endif /* DIGLETT_BONUS_H */
