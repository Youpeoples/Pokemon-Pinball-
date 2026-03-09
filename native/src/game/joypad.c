/*
 * Joypad Input Handling
 *
 * Translated from home/joypad.asm.
 * Tracks multi-frame button state: current, previous, newly pressed,
 * held-repeat with configurable delay, and persistent accumulators.
 */

#include "game/joypad.h"

void joypad_update(GameState *state, Platform *platform) {
    /*
     * ReadJoypad (0xAB8):
     * On the GBC this reads the hardware joypad register (rJOYP).
     * In our C version, we read from the platform abstraction instead.
     */
    uint8_t current = platform_get_joypad_state(platform);
    HRAMState *h = &state->hram;

    h->joypad_state = current;

    /* Detect newly pressed buttons vs previous frame */
    uint8_t changed = current ^ h->previous_joypad;
    uint8_t newly_pressed = changed & current;

    h->newly_pressed_buttons = newly_pressed;
    h->pressed_buttons = newly_pressed;

    /* Buttons released since two frames ago */
    h->prev_previous_joypad = changed & h->previous_joypad;

    /* Held-button repeat logic */
    if (current != 0 && current == h->previous_joypad) {
        /* Same buttons held as last frame */
        h->joy_repeat_delay--;
        if (h->joy_repeat_delay == 0) {
            h->pressed_buttons = current;
            h->joy_repeat_delay = state->joy_repeat_rate;
        }
    } else {
        h->joy_repeat_delay = state->joy_initial_delay;
    }

    /* Save current as previous for next frame */
    h->previous_joypad = current;

    /* Accumulate into persistent joypad states (OR'd each frame) */
    state->joypad_state_persistent |= current;
    state->newly_pressed_persistent |= h->newly_pressed_buttons;
    state->pressed_persistent |= h->pressed_buttons;
}

void joypad_clear_persistent(GameState *state) {
    /* ClearPersistentJoypadStates (0xB2E) */
    state->joypad_state_persistent = 0;
    state->newly_pressed_persistent = 0;
    state->pressed_persistent = 0;
}

bool joypad_is_key_pressed(GameState *state, const KeyConfig *config) {
    /*
     * IsKeyPressed (0xB4C):
     * Checks if a key config action is newly pressed.
     * Each KeyConfig has two alternate button combos (primary, secondary).
     * A combo matches if ALL its bits are set in joypad_state AND
     * at least one of its bits is newly pressed.
     */
    uint8_t joy = state->hram.joypad_state;
    uint8_t newly = state->hram.newly_pressed_buttons;

    /* Check primary key combo */
    if ((joy & config->primary) == config->primary && config->primary != 0) {
        if (newly & config->primary) return true;
    }

    /* Check secondary key combo */
    if ((joy & config->secondary) == config->secondary && config->secondary != 0) {
        if (newly & config->secondary) return true;
    }

    return false;
}

bool joypad_is_key_held(GameState *state, const KeyConfig *config) {
    /*
     * IsKeyPressed2 (0xB36):
     * Similar to IsKeyPressed but doesn't require newly-pressed.
     * Just checks if any combo bits are all held.
     */
    uint8_t joy = state->hram.joypad_state;

    if ((joy & config->primary) == config->primary && config->primary != 0) {
        return true;
    }
    if ((joy & config->secondary) == config->secondary && config->secondary != 0) {
        return true;
    }

    return false;
}
