#ifndef ANIMATION_H
#define ANIMATION_H

#include "types.h"

/*
 * Initialize an animation struct from a frame data table.
 * Translated from InitAnimation in home/animation.asm (0x28A0).
 * frame_data points to pairs of (duration, frame_id).
 * The first pair is read immediately to set the initial state.
 */
void init_animation(Animation *anim, const uint8_t *frame_data);

/*
 * Update an animation struct for one frame tick.
 * Translated from UpdateAnimation in home/animation.asm (0x28A9).
 * frame_data is the same table passed to init_animation.
 * Returns true if the animation has finished (next duration == 0).
 */
bool update_animation(Animation *anim, const uint8_t *frame_data);

#endif /* ANIMATION_H */
