#ifndef PHYSICS_MATH_H
#define PHYSICS_MATH_H

#include "game_state.h"

/*
 * Core physics math library for pinball engine.
 * Translated from home.asm: Sine, Cosine, RotateVector,
 * NegateAngleAndRotateVector, ApplyCollisionForces,
 * MultiplyVectorComponentByAngleFactor.
 *
 * Angles: 0-255 maps to 0-360 degrees.
 * Values: 8-bit magnitude (0-255) + sign flag.
 */

/* Sine/Cosine result: magnitude + sign */
typedef struct {
    uint8_t value;  /* 0-255 magnitude */
    uint8_t sign;   /* 0x00 = positive, 0xFF = negative */
} SinCosResult;

/* Sine lookup (0x2149). Input: angle 0-255. */
SinCosResult gb_sine(uint8_t angle);

/* Cosine lookup (0x2147). cos(a) = sin(a + 0x40). */
SinCosResult gb_cosine(uint8_t angle);

/*
 * MultiplyVectorComponentByAngleFactor (0x210b).
 * Returns: (int16_t)(bc * factor / 256), sign-adjusted by factor_sign.
 */
int16_t multiply_vector_by_angle_factor(int16_t bc, uint8_t factor, uint8_t factor_sign);

/*
 * RotateVector (0x21e7).
 * Rotates velocity vector by angle using rotation matrix.
 * X' = X*cos(a) + Y*sin(a)
 * Y' = Y*cos(a) - X*sin(a)
 */
void rotate_vector(int16_t *vx, int16_t *vy, uint8_t angle);

/*
 * NegateAngleAndRotateVector (0x21e5).
 * Negates the angle then rotates.
 */
void negate_and_rotate_vector(int16_t *vx, int16_t *vy, uint8_t angle);

/*
 * ApplyCollisionForces (0x222b).
 * Applies damping, bounce, and spin transfer in rotated coordinate system.
 * Sets state->no_collision_applied if ball is moving away from collision.
 */
void apply_collision_forces(GameState *state, int16_t *vx, int16_t *vy);

#endif /* PHYSICS_MATH_H */
