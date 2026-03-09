/*
 * Timer System
 *
 * Translated from engine/pinball_game/timer.asm.
 * StartTimer (0x867d): initialize countdown.
 * DecrementTimer (0x86a4): per-frame countdown with BCD arithmetic.
 * StopTimer (0x86d2): disable timer.
 *
 * Timer graphics loading (LoadTimerGraphics at 0x1404a) is handled
 * separately via the queued tile data system.
 */

#include "game/timer.h"

/*
 * StartTimer (0x867d):
 * ASM sets wTimerSeconds, wTimerMinutes, clears wTimerFrames,
 * wTimeRanOut, wPauseTimer, sets wTimerActive=1, wd580=1,
 * then calls LoadTimerGraphics to queue digit tile data.
 */
void start_timer(GameState *state, uint8_t minutes, uint8_t seconds) {
    state->timer_seconds = seconds;
    state->timer_minutes = minutes;
    state->timer_frames = 0;
    state->time_ran_out = 0;
    state->pause_timer = 0;
    state->timer_active = 1;
    state->wd580 = 1;
    /* LoadTimerGraphics: GBC no-op (ret nz on hGameBoyColorFlag).
     * Timer digits are rendered as OBJ sprites by draw_timer(). */
}

/*
 * DecrementTimer (0x86a4):
 * Counts frames 0-59. At 60, rolls over and decrements one BCD second.
 * Uses BCD arithmetic (ASM daa) for seconds/minutes.
 * Sets wTimeRanOut=1 when both minutes and seconds reach 0.
 */
void decrement_timer(GameState *state) {
    if (!state->timer_active) return;
    if (state->pause_timer) return;

    /* Increment frame counter */
    uint8_t frames = state->timer_frames + 1;
    if (frames < 60) {
        state->timer_frames = frames;
        return;
    }

    /* Frame rollover: reset frames, decrement one second */
    state->timer_frames = 0;

    /* Check if time already at 0:00 */
    if (state->timer_minutes == 0 && state->timer_seconds == 0) {
        state->time_ran_out = 1;
        return;
    }

    /* BCD subtract 1 from seconds.
     * ASM: sub $1; daa — decimal adjust after subtraction.
     * BCD subtraction: if low nibble borrows (>9 after sub), subtract 6. */
    uint8_t sec = state->timer_seconds;
    uint8_t low = (sec & 0x0F);
    uint8_t high = (sec & 0xF0);
    bool borrow = false;

    if (low == 0) {
        /* Low nibble borrows: 0 - 1 = 9 (BCD), borrow from high nibble */
        low = 9;
        if (high == 0) {
            /* High nibble borrows: seconds wraps from 00 to 59 */
            sec = 0x59;
            borrow = true;
        } else {
            sec = (uint8_t)((high - 0x10) | low);
        }
    } else {
        sec = (uint8_t)(sec - 1);
    }

    state->timer_seconds = sec;

    /* If borrow from seconds, decrement minutes (BCD) */
    if (borrow) {
        uint8_t min = state->timer_minutes;
        uint8_t mlow = (min & 0x0F);
        uint8_t mhigh = (min & 0xF0);
        if (mlow == 0) {
            min = (uint8_t)((mhigh - 0x10) | 9);
        } else {
            min = (uint8_t)(min - 1);
        }
        state->timer_minutes = min;
    }
}

/*
 * StopTimer (0x86d2):
 * Simply clears wTimerActive.
 */
void stop_timer(GameState *state) {
    state->timer_active = 0;
}
