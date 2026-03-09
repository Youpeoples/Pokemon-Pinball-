#ifndef SCORE_H
#define SCORE_H

#include "game_state.h"

/*
 * Add a 4-byte BCD value to the score with ball multiplier.
 * Translated from Func_3500 in home/bcd.asm.
 * score_bcd is a 4-byte BCD value (little-endian: [0]=low, [3]=high).
 */
void add_score_with_multiplier(GameState *state, const uint8_t score_bcd[4]);

/*
 * Add a 4-byte BCD value to the current buffer value (no multiplier).
 * Translated from AddBCDEToCurBufferValue in home/bcd.asm (0x351C).
 */
void add_score_no_multiplier(GameState *state, const uint8_t score_bcd[4]);

/*
 * Add a 4-byte BCD value to the jackpot, capped at 99999999.
 * Translated from AddBCDEToJackpot in home/bcd.asm (0x3538).
 */
void add_bcd_to_jackpot(GameState *state, const uint8_t bcd[4]);

/*
 * Clear the jackpot to zero.
 * Translated from Func_3579 in home/bcd.asm.
 */
void clear_jackpot(GameState *state);

/*
 * BCD addition helper: add two 2-byte BCD values.
 * Returns the result. Uses DAA-equivalent logic.
 */
uint16_t bcd_add_16(uint16_t a, uint16_t b);

/*
 * BCD addition: add a 6-byte BCD value (src) to another (dst).
 * Both are arrays of 6 bytes, little-endian BCD.
 */
void bcd6_add(uint8_t dst[6], const uint8_t src[6]);

/*
 * Process score queue (Func_85c7).
 * Called every physics frame. Drains one queue entry every 4 frames
 * into wScore, creating visual counting-up effect.
 */
void process_score_queue(GameState *state);

/*
 * Clear entire score queue (Func_8569).
 * Called during game initialization.
 */
void clear_score_queue(GameState *state);

#endif /* SCORE_H */
