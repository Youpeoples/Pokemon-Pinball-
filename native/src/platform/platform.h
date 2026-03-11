#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Platform Platform;

/* Initialize the platform: creates window, sets up audio, etc. */
Platform *platform_init(int screen_w, int screen_h, int scale);

/* Shutdown and free all platform resources */
void platform_shutdown(Platform *platform);

/* Poll input events. Returns false if the user requested quit. */
bool platform_poll_events(Platform *platform);

/* Get the current state of keyboard-mapped buttons (GBC button mask) */
uint8_t platform_get_joypad_state(Platform *platform);

/* Present a framebuffer (RGBA8888, screen_w * screen_h pixels) to screen */
void platform_present_frame(Platform *platform, const uint32_t *framebuffer);

/* Get current time in milliseconds (high resolution) */
double platform_get_time_ms(Platform *platform);

/* Submit audio samples (signed 16-bit stereo interleaved) */
void platform_audio_submit(Platform *platform, const int16_t *samples, int num_samples);

/* Clear the audio queue (use before starting new music to avoid latency) */
void platform_audio_clear(Platform *platform);

/* Get the SDL_Renderer pointer (for renderer module) */
void *platform_get_sdl_renderer(Platform *platform);

/* Get the scale factor */
int platform_get_scale(Platform *platform);

/* Returns true (and clears) if F1 was pressed since last call */
bool platform_consume_f1_toggle(Platform *platform);

/* Returns true (and clears) if left mouse button was clicked since last call.
 * out_x/out_y receive the click position in GBC logical coordinates (0-159, 0-143). */
bool platform_consume_mouse_click(Platform *platform, int *out_x, int *out_y);

#endif /* PLATFORM_H */
