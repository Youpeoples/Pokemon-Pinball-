/*
 * Animation System
 *
 * Translated from home/animation.asm.
 * Animations are tables of (duration, frame_id) pairs.
 * A duration of 0 signals the end of the animation.
 */

#include "game/animation.h"

void init_animation(Animation *anim, const uint8_t *frame_data) {
    /*
     * InitAnimation (0x28A0):
     * hl = frame data pointer, de = animation struct
     * Copy first 2 bytes (duration, frame) and set index to 0.
     */
    anim->frame_counter = frame_data[0];
    anim->frame = frame_data[1];
    anim->index = 0;
}

bool update_animation(Animation *anim, const uint8_t *frame_data) {
    /*
     * UpdateAnimation (0x28A9):
     * Decrement counter. If it reaches 0, advance to next frame.
     * ASM returns carry SET on EVERY step transition (counter reaches 0),
     * not just at the animation end. This is critical for handlers that
     * check the current index to trigger game state changes.
     */
    if (anim->frame_counter == 0) {
        return false; /* Animation not active */
    }

    anim->frame_counter--;
    if (anim->frame_counter != 0) {
        return false; /* Still counting down current frame */
    }

    /* Advance to next frame in the table */
    anim->index++;
    uint16_t offset = anim->index * 2; /* Each entry is 2 bytes */

    uint8_t next_duration = frame_data[offset];
    if (next_duration == 0) {
        return true; /* Animation finished (carry set) */
    }

    anim->frame_counter = next_duration;
    anim->frame = frame_data[offset + 1];
    return true; /* Step transition completed (carry set) */
}
