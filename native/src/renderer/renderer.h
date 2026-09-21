#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include "platform/platform.h"
#include "renderer/vram.h"

/* Forward declarations */
typedef struct GameState GameState;
typedef struct Renderer Renderer;

/* Initialize the renderer (creates texture for GBC framebuffer) */
Renderer *renderer_init(Platform *platform, int screen_w, int screen_h);

/* Shutdown and free renderer resources */
void renderer_shutdown(Renderer *renderer);

/* Set the VRAM pointer the renderer should use for BG rendering */
void renderer_set_vram(Renderer *renderer, VirtualVRAM *vram);

/* Enable or disable combined view mode (160x288 full table).
 * Recreates the framebuffer texture at the appropriate size. */
void renderer_set_combined_mode(Renderer *renderer, bool enabled);

/* Begin a new frame (clear the framebuffer) */
void renderer_begin_frame(Renderer *renderer);

/* Draw the current game state to the framebuffer */
void renderer_draw_game(Renderer *renderer, GameState *state);

/* Present the framebuffer to screen */
void renderer_end_frame(Renderer *renderer);

/*=============================================================================
 * Tile decoding utilities
 *===========================================================================*/

/* Decode a single 2bpp tile (16 bytes) into 8x8 pixel indices (0-3) */
void decode_tile_2bpp(const uint8_t *tile_data, uint8_t *pixels);

/* Decode a 2bpp tile with horizontal flip */
void decode_tile_2bpp_hflip(const uint8_t *tile_data, uint8_t *pixels);

/* Draw debug overlay (FPS, screen/stage, ball state) onto the framebuffer */
void renderer_draw_debug_overlay(Renderer *renderer, GameState *state);

/* Draw playtest banner (ESC/launch hints) at top of screen during editor playtest */
void renderer_draw_playtest_banner(Renderer *renderer);

#endif /* RENDERER_H */
