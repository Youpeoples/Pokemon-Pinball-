/*
 * Core Physics Math Library
 *
 * Translated from home.asm: Sine/Cosine (0x2147-0x2367),
 * MultiplyVectorComponentByAngleFactor (0x210b),
 * RotateVector (0x21e7), NegateAngleAndRotateVector (0x21e5),
 * ApplyCollisionForces (0x222b).
 *
 * Uses the same constant-time multiplication via squares table
 * as the original Game Boy CPU code (MultiplyBbyCUnsigned 0x20ab).
 */

#include "game/physics_math.h"
#include "game/config_data.h"
#include "audio/audio.h"
#include <string.h>

/* SineTable from data/sine_table.asm - 65 entries (0 to 90 degrees) */
static const uint8_t SineTable[65] = {
    0x00, 0x06, 0x0D, 0x13, 0x19, 0x1F, 0x26, 0x2C,
    0x32, 0x38, 0x3E, 0x44, 0x4A, 0x50, 0x56, 0x5C,
    0x62, 0x68, 0x6D, 0x73, 0x79, 0x7E, 0x84, 0x89,
    0x8E, 0x93, 0x98, 0x9D, 0xA2, 0xA7, 0xAC, 0xB1,
    0xB5, 0xB9, 0xBE, 0xC2, 0xC6, 0xCA, 0xCE, 0xD1,
    0xD5, 0xD8, 0xDC, 0xDF, 0xE2, 0xE5, 0xE7, 0xEA,
    0xED, 0xEF, 0xF1, 0xF3, 0xF5, 0xF7, 0xF8, 0xFA,
    0xFB, 0xFC, 0xFD, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF
};

/*
 * Sine (0x2149):
 * Input: angle 0-255 (full circle)
 * Output: value = |sin(angle)|, sign = 0x00 positive / 0xFF negative
 *
 * Process:
 *   1. Save sign from bit 7 of angle
 *   2. Mask to 0-127 (half circle)
 *   3. If >= 64 (second quadrant), mirror: a = 128 - a
 *   4. Look up SineTable[a]
 *   5. Apply original sign
 */
SinCosResult gb_sine(uint8_t angle) {
    SinCosResult r;
    uint8_t sign_bit = angle & 0x80;
    uint8_t a = angle & 0x7F;

    if (a >= 0x40) {
        /* Second quadrant: complement and add to convert.
         * ASM: cpl; add $81 which is equivalent to 0x80 - a */
        a = (uint8_t)(~a + 0x81);  /* = 0x80 - a = 128 - a */
    }

    r.value = SineTable[a];
    r.sign = sign_bit ? 0xFF : 0x00;
    return r;
}

/*
 * Cosine (0x2147):
 * cos(a) = sin(a + 0x40)
 */
SinCosResult gb_cosine(uint8_t angle) {
    return gb_sine((uint8_t)(angle + 0x40));
}

/*
 * MultiplyBbyCUnsigned - unsigned 8x8 multiply.
 * ASM uses squares table trick: b*c = (b^2 + c^2 - (b-c)^2) / 2
 * In C we just multiply directly - same result, no timing concerns.
 */
static uint16_t multiply_u8(uint8_t b, uint8_t c) {
    return (uint16_t)b * (uint16_t)c;
}

/*
 * MultiplyVectorComponentByAngleFactor (0x210b):
 * Computes bc * e / 256, where e is the sin/cos magnitude and d is its sign.
 *
 * Process:
 *   1. Determine output sign from xor of bc's sign and d
 *   2. Make bc positive
 *   3. Multiply lo byte by e, round to hi byte
 *   4. Multiply hi byte by e
 *   5. Sum and apply sign
 */
int16_t multiply_vector_by_angle_factor(int16_t bc, uint8_t factor, uint8_t factor_sign) {
    /* Determine result sign: XOR of input sign and factor sign */
    uint8_t bc_sign = (bc < 0) ? 0xFF : 0x00;
    uint8_t result_sign = bc_sign ^ factor_sign;

    /* Make bc positive */
    uint16_t abs_bc;
    if (bc < 0) {
        abs_bc = (uint16_t)(-bc);
    } else {
        abs_bc = (uint16_t)bc;
    }

    uint8_t lo = (uint8_t)(abs_bc & 0xFF);
    uint8_t hi = (uint8_t)(abs_bc >> 8);

    /* Multiply lo byte by factor, round to hi byte */
    uint16_t lo_product = multiply_u8(lo, factor);
    uint16_t lo_result = (lo_product + 0x0080) >> 8;  /* round to nearest */

    /* Multiply hi byte by factor */
    uint16_t hi_product = multiply_u8(hi, factor);

    /* Sum */
    uint16_t result = hi_product + lo_result;

    /* Apply sign */
    if (result_sign & 0x80) {
        return -(int16_t)result;
    }
    return (int16_t)result;
}

/*
 * RotateVector (0x21e7):
 * Rotates (vx, vy) by angle using rotation matrix:
 *   X' = X*cos(a) + Y*sin(a)
 *   Y' = Y*cos(a) - X*sin(a)
 *
 * Note: Matrix is inverted vertically for GBC coordinate system
 * (negative Y = up). The ASM computes:
 *   new_x = x*cos + y*sin
 *   new_y = y*cos + x*(-sin)  = y*cos - x*sin
 */
void rotate_vector(int16_t *vx, int16_t *vy, uint8_t angle) {
    int16_t x = *vx;
    int16_t y = *vy;

    SinCosResult cos_r = gb_cosine(angle);
    SinCosResult sin_r = gb_sine(angle);

    /* X' = X*cos + Y*sin */
    int16_t x_cos = multiply_vector_by_angle_factor(x, cos_r.value, cos_r.sign);
    int16_t y_sin = multiply_vector_by_angle_factor(y, sin_r.value, sin_r.sign);
    int16_t new_x = x_cos + y_sin;

    /* Y' = Y*cos - X*sin  (ASM: X * -sin(a) + Y * cos(a)) */
    /* ASM inverts sin sign: cpl of sin_r.sign (without +1, just complement) */
    uint8_t neg_sin_sign = ~sin_r.sign;
    int16_t x_neg_sin = multiply_vector_by_angle_factor(x, sin_r.value, neg_sin_sign);
    int16_t y_cos = multiply_vector_by_angle_factor(y, cos_r.value, cos_r.sign);
    int16_t new_y = x_neg_sin + y_cos;

    *vx = new_x;
    *vy = new_y;
}

/*
 * NegateAngleAndRotateVector (0x21e5):
 * Negates angle (cpl + inc a = two's complement) then rotates.
 */
void negate_and_rotate_vector(int16_t *vx, int16_t *vy, uint8_t angle) {
    uint8_t neg_angle = (uint8_t)(~angle + 1);  /* cpl; inc a */
    rotate_vector(vx, vy, neg_angle);
}

/*
 * ApplyCollisionForces (0x222b):
 * Called after velocity has been rotated into collision-normal coordinates.
 * Y component points toward/away from collision surface.
 *
 * Algorithm:
 *   1. If Y is negative (ball moving away from surface): set no_collision_applied, exit
 *   2. If Y >= 3 (hard hit): rumble + sound
 *   3. Dampen Y: Y/4, then add Y/8 * amplification factor
 *   4. Negate Y (bounce)
 *   5. Add spin/2 to X
 *   6. Recalculate spin: (X * 4) >> 8
 */
void apply_collision_forces(GameState *state, int16_t *vx, int16_t *vy) {
    int16_t x = *vx;
    int16_t y = *vy;

    /* Default: no collision applied */
    state->no_collision_applied = 0xFF;

    /* Early exit if ball moving away from collision normal (Y negative) */
    if (y < 0) {
        return;
    }

    state->no_collision_applied = 0;

    /* Hard collision: rumble and sound */
    if ((y >> 8) >= 3) {
        state->rumble_pattern = 0xFF;
        state->rumble_duration = 1;
        /* ASM: PlaySFXIfNoneActive $0008, only if NOT a flipper collision.
         * Only plays wall collision SFX if no other SFX channel is active. */
        if (!state->flipper_collision) {
            audio_play_sfx_if_none_active(state->audio, 0x00, 0x08);
        }
    }

    /* Dampen Y velocity (configurable shift values) */
    /* ASM uses unsigned shift: srl d; rr e (twice) */
    const PhysicsConfig *phys = &state->config->physics;
    uint16_t uy = (uint16_t)y;
    uint16_t damped = uy >> phys->damping_shift;       /* default: Y / 4 */
    uint16_t damped_amp = uy >> phys->amplification_shift; /* default: Y / 8 */

    /* Apply amplification: add damped_amp for each unit of amplification */
    uint8_t amp = state->collision_force_amplification;
    uint16_t total = damped;
    for (uint8_t i = 0; i < amp; i++) {
        total += damped_amp;
    }

    /* Negate (bounce): two's complement */
    y = -(int16_t)total;

    /* Spin transfer: add spin >> spin_transfer_shift to X velocity */
    int8_t spin = (int8_t)state->ball_spin;
    int8_t half_spin = spin >> phys->spin_transfer_shift;  /* default: spin / 2 */
    int16_t spin16 = (int16_t)half_spin;  /* sign-extend to 16 bits */
    x += spin16;

    /* Recalculate ball spin from new X velocity: (X << spin_recalc_shift) >> 8 */
    /* ASM: sla c; rl b; sla c; rl b; result = b */
    int16_t x_shifted = x << phys->spin_recalc_shift;  /* default: X * 4 */
    state->ball_spin = (uint8_t)(int8_t)(x_shifted >> 8);

    *vx = x;
    *vy = y;
}
