#ifndef RNG_H
#define RNG_H

#include "game_state.h"

/*
 * Generate a random number.
 * Translated from GenRandom in home/random.asm (0x959).
 * Returns a pseudo-random byte from the 54-element table.
 */
uint8_t gen_random(GameState *state);

/*
 * Reset/seed the RNG system.
 * Translated from ResetRNG in home/random.asm (0x97A).
 * Seeds from SRAM modulus value into the 54-element table,
 * then runs UpdateRNG 3 times to warm up.
 */
void reset_rng(GameState *state, uint8_t sram_seed);

/*
 * Update the RNG table by subtracting pairs of values.
 * Translated from UpdateRNG in home/random.asm (0x9FA).
 */
void update_rng(GameState *state);

/*
 * Generate a random value in range [0, max].
 * Translated from RandomRange in home/random.asm (0xA21).
 */
uint8_t random_range(GameState *state, uint8_t max);

#endif /* RNG_H */
