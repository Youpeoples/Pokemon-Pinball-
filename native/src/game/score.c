/*
 * BCD Score System
 *
 * Translated from home/bcd.asm + engine/pinball_game/score.asm.
 * The GBC uses BCD (Binary Coded Decimal) with the DAA instruction.
 * Each byte holds two decimal digits (0x00-0x99).
 * Scores are 6-byte BCD (12 digits, max 999,999,999,999).
 * Jackpot is 4-byte BCD (8 digits, max 99,999,999).
 *
 * Score Queue System (Func_85c7):
 * Instead of adding directly to wScore, scores go into a circular
 * queue (wAddScoreQueue, 16 entries × 6 bytes = 96 bytes). Every
 * 4 frames, one queue entry is drained into wScore, creating a
 * visual counting-up effect on the scoreboard.
 */

#include "game/score.h"
#include <string.h>

/*
 * BCD addition for a single byte.
 * Simulates the Z80 DAA instruction behavior for addition.
 * Returns the BCD result and sets *carry if there's a carry out.
 */
static uint8_t bcd_add_byte(uint8_t a, uint8_t b, bool carry_in, bool *carry_out) {
    uint16_t sum = (uint16_t)a + (uint16_t)b + (carry_in ? 1 : 0);

    /* DAA correction: adjust if low nibble > 9 OR half-carry occurred */
    if ((sum & 0x0F) > 9 || ((a & 0x0F) + (b & 0x0F) + (carry_in ? 1 : 0)) > 0x0F) {
        sum += 0x06;
    }
    /* Adjust if high nibble > 9 or carry */
    if (sum > 0x99) {
        sum += 0x60;
        *carry_out = true;
    } else {
        *carry_out = false;
    }

    return (uint8_t)(sum & 0xFF);
}

void bcd6_add(uint8_t dst[6], const uint8_t src[6]) {
    bool carry = false;
    for (int i = 0; i < 6; i++) {
        dst[i] = bcd_add_byte(dst[i], src[i], carry, &carry);
    }
}

uint16_t bcd_add_16(uint16_t a, uint16_t b) {
    bool carry = false;
    uint8_t lo = bcd_add_byte(a & 0xFF, b & 0xFF, false, &carry);
    uint8_t hi = bcd_add_byte((a >> 8) & 0xFF, (b >> 8) & 0xFF, carry, &carry);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

/*
 * AddBigBCD6FromQueueWithBallMultiplier (0x8576):
 * Adds a 4-byte BCD score × ball_type multiplier into the score queue
 * at the current write offset. The queue is drained by process_score_queue.
 *
 * ASM: multiplier = max(wBallType, 1). Adds score × multiplier to queue
 * entry at wAddScoreQueueOffset, then advances offset by 6 (wrapping at $60).
 */
void add_score_with_multiplier(GameState *state, const uint8_t score_bcd[4]) {
    uint8_t to_add[6];
    to_add[0] = score_bcd[0];
    to_add[1] = score_bcd[1];
    to_add[2] = score_bcd[2];
    to_add[3] = score_bcd[3];
    to_add[4] = 0;
    to_add[5] = 0;

    /* ASM: multiplier = max(wBallType, 1) */
    uint8_t mult = state->ball_type;
    if (mult == 0) mult = 1;

    /* ASM loops b times. Each iteration adds score to the queue entry at
     * the current offset, then advances DE by 6 (wrapping at $60).
     * This writes to mult CONSECUTIVE queue entries. */
    uint8_t off = state->add_score_queue_offset;
    for (uint8_t m = 0; m < mult; m++) {
        uint8_t *qentry = &state->add_score_queue[off];
        bool carry = false;
        for (int i = 0; i < 6; i++) {
            qentry[i] = bcd_add_byte(qentry[i], to_add[i], carry, &carry);
        }
        off += 6;
        if (off >= 0x60) off = 0;
    }
    state->add_score_queue_offset = off;
}

/*
 * AddBigBCD6FromQueue (0x8588):
 * Same as above but without multiplier (multiplier = 1).
 */
void add_score_no_multiplier(GameState *state, const uint8_t score_bcd[4]) {
    uint8_t to_add[6];
    to_add[0] = score_bcd[0];
    to_add[1] = score_bcd[1];
    to_add[2] = score_bcd[2];
    to_add[3] = score_bcd[3];
    to_add[4] = 0;
    to_add[5] = 0;

    uint8_t off = state->add_score_queue_offset;
    uint8_t *qentry = &state->add_score_queue[off];
    bool carry = false;
    for (int i = 0; i < 6; i++) {
        qentry[i] = bcd_add_byte(qentry[i], to_add[i], carry, &carry);
    }

    off += 6;
    if (off >= 0x60) off = 0;
    state->add_score_queue_offset = off;
}

/*
 * Func_85c7 (0x85c7): Process Score Queue
 *
 * Called every physics frame. Only processes one entry every 4 frames.
 * Drains the oldest queue entry into wScore by BCD-adding each byte,
 * then clearing the queue entry. Creates visual counting effect.
 */
void process_score_queue(GameState *state) {
    /* Only process every 4 frames (ASM: hFrameCounter & $3, ret nz) */
    if ((state->hram.frame_counter & 3) != 0)
        return;

    uint8_t read_off = state->score_queue_read_offset;
    uint8_t write_off = state->add_score_queue_offset;

    /* ASM: if read == write, save write offset to caught_up and return */
    if (write_off == read_off) {
        state->score_queue_caught_up = write_off;
    }

    /* Check if current queue entry is all zeros */
    uint8_t *qentry = &state->add_score_queue[read_off];
    uint8_t any_nonzero = 0;
    for (int i = 0; i < 6; i++) {
        any_nonzero |= qentry[i];
    }

    if (!any_nonzero) {
        /* Queue entry is empty: sync read pointer to caught_up */
        state->score_queue_read_offset = state->score_queue_caught_up;
        return;
    }

    /* Queue entry has value: add to score, clear entry.
     * ASM: BCD add each of the 6 bytes with carry chain. */
    bool carry = false;
    for (int i = 0; i < 6; i++) {
        state->score[i] = bcd_add_byte(state->score[i], qentry[i], carry, &carry);
        qentry[i] = 0;
    }

    /* Cap score at 999,999,999,999 on overflow */
    if (carry) {
        memset(state->score, 0x99, 6);
    }

    /* Advance read pointer by 6, wrapping at $60 */
    read_off += 6;
    if (read_off >= 0x60) read_off = 0;
    state->score_queue_read_offset = read_off;

    /* Flag that score display needs update */
    state->score_changed = 1;
}

/*
 * Func_8569 (0x8569): Clear entire score queue.
 * Called during game initialization.
 */
void clear_score_queue(GameState *state) {
    memset(state->add_score_queue, 0, sizeof(state->add_score_queue));
    state->add_score_queue_offset = 0;
    state->score_queue_read_offset = 0;
    state->score_queue_caught_up = 0;
    state->score_changed = 0;
}

void add_bcd_to_jackpot(GameState *state, const uint8_t bcd[4]) {
    /*
     * AddBCDEToJackpot (0x3538):
     * Add 4-byte BCD to wCurrentJackpot. Cap at 99999999.
     */
    bool carry = false;
    for (int i = 0; i < 4; i++) {
        state->current_jackpot[i] = bcd_add_byte(
            state->current_jackpot[i], bcd[i], carry, &carry
        );
    }

    /* If carry out, cap at 99999999 */
    if (carry) {
        state->current_jackpot[0] = 0x99;
        state->current_jackpot[1] = 0x99;
        state->current_jackpot[2] = 0x99;
        state->current_jackpot[3] = 0x99;
    }
}

void clear_jackpot(GameState *state) {
    /* Func_3579 */
    memset(state->current_jackpot, 0, 4);
}
