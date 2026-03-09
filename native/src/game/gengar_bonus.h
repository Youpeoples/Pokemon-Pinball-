/*
 * Gengar Bonus Stage (Stage ID $07)
 *
 * Translated from engine/pinball_game/:
 *   stage_init/init_gengar_bonus.asm (0x18099)
 *   ball_init/ball_init_gengar_bonus.asm (0x18157)
 *   ball_loss/ball_loss_gengar_bonus.asm (0xdf1a)
 *   object_collision/gengar_bonus_object_collision.asm (0x181b1)
 *   draw_sprites/draw_gengar_bonus_sprites.asm (0x18faf)
 *   load_stage_data/load_gengar_bonus.asm (0x1818b)
 *
 * Three-phase boss battle: 3 Gastly → 2 Haunter → 1 Gengar.
 * Timer: 1:30. Completion requires 5 hits on Gengar.
 */

#ifndef GENGAR_BONUS_H
#define GENGAR_BONUS_H

#include "game/game_state.h"

/* InitGengarBonusStage (0x18099) */
void init_gengar_bonus(GameState *state);

/* InitBallGengarBonusStage (0x18157) */
void init_ball_gengar_bonus(GameState *state);

/* HandleBallLossGengarBonus (0xdf1a) */
void handle_ball_loss_gengar_bonus(GameState *state);

/* CheckGengarBonusStageGameObjectCollisions (0x181b1) */
void check_gengar_bonus_object_collisions(GameState *state);

/* ResolveGengarBonusGameObjectCollisions (0x18377) */
void resolve_gengar_bonus_object_collisions(GameState *state);

/* DrawSpritesGengarBonus (0x18faf) */
void draw_gengar_bonus_sprites(GameState *state);

/* _LoadStageDataGengarBonus (0x1818b) */
void load_stage_data_gengar_bonus(GameState *state);

#endif /* GENGAR_BONUS_H */
