#ifndef DRAW_RED_FIELD_H
#define DRAW_RED_FIELD_H

#include "game/game_state.h"

/*
 * Draw all sprites for the current red field stage.
 * Dispatches to top or bottom drawing based on current_stage.
 * Translated from draw_red_field_top_sprites.asm / draw_red_field_bottom_sprites.asm.
 */
void draw_red_field_sprites(GameState *state);

/*
 * Draw timer sprites at specified screen position.
 * x, y: screen pixel coordinates (ASM bc register: B=x, C=y).
 * Regular stages: (0x7F, 0x00). Bonus stages: (0x7F, 0x65).
 * From DrawTimer_GameBoyColor (0x175f5).
 */
void draw_timer(GameState *state, uint8_t x, uint8_t y);

/*
 * Update the scoreboard in the window layer tilemap.
 * Renders party count, ball lives, pikachu saver icon, and score.
 * Translated from Func_8645, Func_dba9, DrawNumPartyMonsIcon,
 * DrawPikachuSaverLightningBoltIcon.
 */
void update_scoreboard(GameState *state);

/*
 * Bottom text system (from home/text.asm).
 * Scrolling/stationary text messages shown in the window layer.
 */

/* Enable bottom text display: sets WY=$86, enables text system.
 * From EnableBottomText (0x30db). */
void enable_bottom_text(GameState *state);

/* Fill message buffer with black tiles and disable all text entries.
 * From FillBottomMessageBufferWithBlackTile (0x30e8). */
void fill_bottom_message_buffer_with_black_tile(GameState *state);

/* Load a scrolling text entry. text_data is a 6-byte header followed by string.
 * slot_index: 0-2 for which scrolling text slot to use.
 * From LoadScrollingText (0x32aa). */
void load_scrolling_text(GameState *state, int slot_index,
                         const uint8_t *header, const char *text);

/* Load a stationary text entry. header is 4 bytes, text follows.
 * slot_index: 0-2 for which stationary text slot to use.
 * From LoadStationaryTextAndHeader (0x3357). */
void load_stationary_text(GameState *state, int slot_index,
                          const uint8_t *header, const char *text);

/* Per-frame update of all bottom text entries.
 * Scrolls text, decrements timers, auto-disables when done.
 * From UpdateBottomText (0x33e3). */
void update_bottom_text(GameState *state);

/* Show ball loss text (convenience function).
 * Clears buffer, enables text, loads into slot 2.
 * From ShowBallLossText (0xdc6d). */
void show_ball_loss_text(GameState *state, const uint8_t *header, const char *text);

#endif /* DRAW_RED_FIELD_H */
