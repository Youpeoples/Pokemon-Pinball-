/*
 * Mewtwo Bonus Stage (Stage ID $09)
 *
 * Translated from engine/pinball_game/:
 *   stage_init/init_mewtwo_bonus.asm (0x1924f)
 *   ball_init/ball_init_mewtwo_bonus.asm (0x192e3)
 *   ball_loss/ball_loss_mewtwo_bonus.asm (0xdf7e)
 *   object_collision/mewtwo_bonus_object_collision.asm (0x19330)
 *   object_collision/mewtwo_bonus_resolve_collision.asm (0x19451)
 *   draw_sprites/draw_mewtwo_bonus_sprites.asm (0x1994e)
 *   load_stage_data/load_mewtwo_bonus.asm (0x19310)
 *
 * Mewtwo boss battle with 6 orbiting energy balls.
 * Timer: 2:00. 8 hit groups to defeat (3 orb hits per group).
 */

#ifndef MEWTWO_BONUS_H
#define MEWTWO_BONUS_H

#include "game/game_state.h"

/* InitMewtwoBonusStage (0x1924f) */
void init_mewtwo_bonus(GameState *state);

/* InitBallMewtwoBonusStage (0x192e3) */
void init_ball_mewtwo_bonus(GameState *state);

/* HandleBallLossMewtwoBonus (0xdf7e) */
void handle_ball_loss_mewtwo_bonus(GameState *state);

/* CheckMewtwoBonusStageGameObjectCollisions (0x19330) */
void check_mewtwo_bonus_object_collisions(GameState *state);

/* ResolveMewtwoBonusGameObjectCollisions (0x19451) */
void resolve_mewtwo_bonus_object_collisions(GameState *state);

/* DrawSpritesMewtwoBonus (0x1994e) */
void draw_mewtwo_bonus_sprites(GameState *state);

/* _LoadStageDataMewtwoBonus (0x19310) */
void load_stage_data_mewtwo_bonus(GameState *state);

#endif /* MEWTWO_BONUS_H */
