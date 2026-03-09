#ifndef MEOWTH_BONUS_H
#define MEOWTH_BONUS_H

#include "game/game_state.h"

void init_meowth_bonus(GameState *state);
void init_ball_meowth_bonus(GameState *state);
void handle_ball_loss_meowth_bonus(GameState *state);
void check_meowth_bonus_object_collisions(GameState *state);
void resolve_meowth_bonus_object_collisions(GameState *state);
void draw_meowth_bonus_sprites(GameState *state);
void load_stage_data_meowth_bonus(GameState *state);

#endif /* MEOWTH_BONUS_H */
