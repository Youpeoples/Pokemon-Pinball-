#ifndef TIMER_H
#define TIMER_H

#include "game_state.h"

/*
 * Timer System
 *
 * Translated from engine/pinball_game/timer.asm.
 * StartTimer (0x867d), DecrementTimer (0x86a4), StopTimer (0x86d2).
 *
 * The timer counts down from minutes:seconds at ~59.7 Hz.
 * Seconds/minutes are BCD (daa used in ASM).
 * When time reaches 0:00, wTimeRanOut is set.
 */

/* Start the timer with given BCD minutes and seconds.
 * ASM: b = minutes, c = seconds. */
void start_timer(GameState *state, uint8_t minutes, uint8_t seconds);

/* Decrement the timer by one frame. Called once per physics frame.
 * Respects wPauseTimer flag. Sets wTimeRanOut when 0:00 reached. */
void decrement_timer(GameState *state);

/* Stop the timer (sets wTimerActive = 0). */
void stop_timer(GameState *state);

#endif /* TIMER_H */
