/*
 * Random Number Generator
 *
 * Translated from home/random.asm.
 * Uses a subtractive PRNG with a 54-element circular table.
 * This is a variant of the "subtractive method" (Knuth, TAOCP Vol 2).
 */

#include "game/rng.h"
#include <string.h>

/* Permutation offsets from ResetRNG.Data (home/random.asm) */
static const uint8_t rng_init_offsets[54] = {
    0x14, 0x29, 0x07, 0x1C, 0x31, 0x0F, 0x24, 0x02, 0x17,
    0x2C, 0x0A, 0x1F, 0x34, 0x12, 0x27, 0x05, 0x1A, 0x2F,
    0x0D, 0x22, 0x00, 0x15, 0x2A, 0x08, 0x1D, 0x32, 0x10,
    0x25, 0x03, 0x18, 0x2D, 0x0B, 0x20, 0x35, 0x13, 0x28,
    0x06, 0x1B, 0x30, 0x0E, 0x23, 0x01, 0x16, 0x2B, 0x09,
    0x1E, 0x33, 0x11, 0x26, 0x04, 0x19, 0x2E, 0x0C, 0x21
};

uint8_t gen_random(GameState *state) {
    /*
     * GenRandom (0x959):
     * Read the next value from the 54-element table.
     * ASM: loads ptr into bc (index), increments a (ptr copy), checks a==55.
     * When ptr+1 reaches 55 (i.e. ptr was 54), run UpdateRNG and reset both
     * the pointer and index to 0 (so values[0] is read after update).
     */
    uint8_t ptr = state->rng_pointer;
    uint8_t index = ptr;   /* bc = ptr (used for array indexing) */
    ptr++;                 /* inc a */
    if (ptr == 55) {       /* cp 54 + 1 */
        update_rng(state);
        ptr = 0;           /* xor a */
        index = 0;         /* ld bc, $0000 */
    }
    state->rng_pointer = ptr;
    return state->rng_values[index];
}

void reset_rng(GameState *state, uint8_t sram_seed) {
    /*
     * ResetRNG (0x97A):
     * Seed the PRNG table using the SRAM seed and modulus.
     */
    uint8_t mod = state->rng_modulus;
    if (mod == 0) mod = 251; /* Default prime modulus */

    /* Compute seed % modulus */
    uint8_t sub = sram_seed;
    while (sub >= mod) {
        sub -= mod;
    }

    state->rng_sub = sub;
    state->rng_sub2 = sub;

    /* Fill the table using the permutation offsets */
    uint8_t e = 1;
    for (int i = 0; i < 54; i++) {
        uint8_t offset = rng_init_offsets[i];
        state->rng_values[offset] = e;

        /* next e = (rng_sub - e) mod modulus */
        int16_t diff = (int16_t)state->rng_sub - (int16_t)e;
        if (diff < 0) diff += mod;
        e = (uint8_t)diff;

        state->rng_sub = state->rng_values[offset];
    }

    /* Warm up the generator */
    update_rng(state);
    update_rng(state);
    update_rng(state);

    /* ASM: `ld a, $0` is wasted (GenRandom overwrites a), then stores result
     * to sRNGMod. M13: don't reset rng_pointer. M14: store result for re-seeding. */
    state->rng_sram_seed = gen_random(state);
}

void update_rng(GameState *state) {
    /*
     * UpdateRNG (0x9FA):
     * ASM does NOT increment bc/hl in loops — it repeatedly modifies only
     * values[0] (24 times, subtracting values[0x1F] each time) and
     * values[0x18] (31 times, subtracting values[0] each time).
     * Net effect: values[0] = (values[0] - 24*values[0x1F]) % mod
     *             values[0x18] = (values[0x18] - 31*values[0]) % mod
     */
    uint8_t mod = state->rng_modulus;
    if (mod == 0) mod = 251;

    /* Loop 1: bc=wRNGValues, hl=wRNGValues+$1F, e=$18 (24 iterations)
     * values[0] -= values[0x1F] each iteration, mod modulus */
    for (int i = 0; i < 0x18; i++) {
        int16_t diff = (int16_t)state->rng_values[0] - (int16_t)state->rng_values[0x1F];
        if (diff < 0) diff += mod;
        state->rng_values[0] = (uint8_t)diff;
    }

    /* Loop 2: bc=wRNGValues+$18, hl=wRNGValues, e=$1F (31 iterations)
     * values[0x18] -= values[0] each iteration, mod modulus */
    for (int i = 0; i < 0x1F; i++) {
        int16_t diff = (int16_t)state->rng_values[0x18] - (int16_t)state->rng_values[0];
        if (diff < 0) diff += mod;
        state->rng_values[0x18] = (uint8_t)diff;
    }
}

/*
 * EvensAndOdds lookup table (home/random.asm):
 * Element N = (N * 2) with bit 0 set from bit 8 overflow.
 * Effectively: element[N] = (N << 1) | (N >> 7)
 * This is just a rotate-left-by-1 operation.
 */
static uint8_t evens_and_odds(uint8_t n) {
    return (uint8_t)((n << 1) | (n >> 7));
}

uint8_t random_range(GameState *state, uint8_t max) {
    /*
     * RandomRange (0xA21):
     * Returns a value in [0, max].
     * Uses multiplication: result = (random * (max*2+1)) >> 8, roughly.
     */
    uint8_t lookup = evens_and_odds(max);
    uint8_t rnd = gen_random(state);

    /* MultiplyAbyL_AncientEgyptian: result in HL = A * L */
    uint16_t product = (uint16_t)rnd * (uint16_t)lookup;

    /* inc h; srl h - this adds 1 to high byte then divides by 2 */
    uint8_t h = (product >> 8) + 1;
    h >>= 1;
    return h;
}
