#ifndef BALL_GFX_H
#define BALL_GFX_H

#include "game_state.h"

/*
 * Ball Graphics System
 *
 * Translated from engine/pinball_game/ball_gfx.asm.
 * LoadBallGfx (0xdcc3), LoadMiniBallGfx (0xdd12),
 * LoadSuperMiniPinballGfx (0xdd62).
 *
 * Loads ball sprite tile data to OBJ $40 ($8400 VRAM, 0x200 bytes)
 * and sets OBJ palette #4 based on ball_type.
 */

/* Load full-size ball tiles + palette based on state->ball_type.
 * Sets ball_size = 0. */
void load_ball_gfx(GameState *state);

/* Load mini ball tiles + palette based on state->ball_type.
 * Sets ball_size = 1. */
void load_mini_ball_gfx(GameState *state);

/* Load super-mini ball tiles (fixed, not type-dependent).
 * Sets ball_size = 2. */
void load_super_mini_ball_gfx(GameState *state);

/* Load é character tile to VRAM tile $03 ($8830).
 * Called at start of EndOfBallBonus (0xf55c). */
void load_e_acute_character_gfx(GameState *state);

/* Load special text character glyph tiles into VRAM.
 * Translated from LoadSpecialTextChar (home/text.asm 0x31e1).
 * char_id: 0=Male, 1=Female, 2=Apostrophe, 3=é, 4=Asterisk,
 *          5=Exclamation, 6=Little x, 7=Period, 8=Colon */
void load_special_text_char_gfx(GameState *state, uint8_t char_id);

#endif /* BALL_GFX_H */
