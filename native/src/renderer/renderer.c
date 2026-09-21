/*
 * SDL2 Renderer Implementation
 *
 * Renders GBC-style graphics: 2bpp tiles, tilemaps, sprites with palettes.
 * Uses an SDL_Texture as a 160x144 framebuffer, scaled up to the window.
 *
 * Includes 16-step RGB fade for palette transitions (FadeIn/FadeOut).
 */

#include "renderer/renderer.h"
#include "game/game_state.h"
#include "game/constants.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

struct Renderer {
    SDL_Renderer *sdl_renderer;
    SDL_Texture *framebuffer_tex;
    uint32_t *framebuffer;
    uint8_t *bg_color_indices;
    bool *bg_priority;
    int screen_w;
    int screen_h;
    bool combined_mode;
    VirtualVRAM *vram;
    Platform *platform;
};

Renderer *renderer_init(Platform *platform, int screen_w, int screen_h) {
    Renderer *r = calloc(1, sizeof(Renderer));
    if (!r) return NULL;

    r->sdl_renderer = (SDL_Renderer *)platform_get_sdl_renderer(platform);
    r->platform = platform;
    r->screen_w = screen_w;
    r->screen_h = screen_h;

    int total = screen_w * screen_h;
    r->framebuffer = calloc(total, sizeof(uint32_t));
    r->bg_color_indices = calloc(total, sizeof(uint8_t));
    r->bg_priority = calloc(total, sizeof(bool));
    if (!r->framebuffer || !r->bg_color_indices || !r->bg_priority) {
        free(r->framebuffer);
        free(r->bg_color_indices);
        free(r->bg_priority);
        free(r);
        return NULL;
    }

    r->framebuffer_tex = SDL_CreateTexture(
        r->sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        screen_w, screen_h
    );
    if (!r->framebuffer_tex) {
        free(r->framebuffer);
        free(r->bg_color_indices);
        free(r->bg_priority);
        free(r);
        return NULL;
    }

    return r;
}

void renderer_shutdown(Renderer *renderer) {
    if (!renderer) return;
    if (renderer->framebuffer_tex) {
        SDL_DestroyTexture(renderer->framebuffer_tex);
    }
    free(renderer->framebuffer);
    free(renderer->bg_color_indices);
    free(renderer->bg_priority);
    free(renderer);
}

void renderer_set_vram(Renderer *renderer, VirtualVRAM *vram) {
    if (renderer) renderer->vram = vram;
}

void renderer_set_combined_mode(Renderer *r, bool enabled) {
    if (r->combined_mode == enabled) return;
    r->combined_mode = enabled;

    int new_h = enabled ? COMBINED_VIEW_HEIGHT : GBC_SCREEN_HEIGHT;

    /* Recreate SDL texture at new dimensions */
    if (r->framebuffer_tex) {
        SDL_DestroyTexture(r->framebuffer_tex);
    }
    r->framebuffer_tex = SDL_CreateTexture(
        r->sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        r->screen_w, new_h
    );

    /* Reallocate framebuffer arrays */
    int total = r->screen_w * new_h;
    free(r->framebuffer);
    free(r->bg_color_indices);
    free(r->bg_priority);
    r->framebuffer = calloc(total, sizeof(uint32_t));
    r->bg_color_indices = calloc(total, sizeof(uint8_t));
    r->bg_priority = calloc(total, sizeof(bool));

    r->screen_h = new_h;
}

void renderer_begin_frame(Renderer *renderer) {
    int total = renderer->screen_w * renderer->screen_h;
    /* Combined mode: clear to black (border area). Classic: white (GBC default). */
    uint32_t clear_color = renderer->combined_mode ? 0x000000FF : 0xFFFFFFFF;
    for (int i = 0; i < total; i++) {
        renderer->framebuffer[i] = clear_color;
    }
    /* Clear BG priority tracking arrays */
    memset(renderer->bg_color_indices, 0, total * sizeof(uint8_t));
    memset(renderer->bg_priority, 0, total * sizeof(bool));
}

/*=============================================================================
 * Tile decoding: GBC 2bpp format
 * Each tile is 8x8 pixels, 16 bytes (2 bytes per row).
 * Byte 0 = low bit plane, Byte 1 = high bit plane.
 * Pixel 0 is the leftmost (bit 7 of each byte).
 *===========================================================================*/
void decode_tile_2bpp(const uint8_t *tile_data, uint8_t *pixels) {
    for (int row = 0; row < 8; row++) {
        uint8_t lo = tile_data[row * 2];
        uint8_t hi = tile_data[row * 2 + 1];
        for (int col = 0; col < 8; col++) {
            int bit = 7 - col;
            uint8_t color = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            pixels[row * 8 + col] = color;
        }
    }
}

void decode_tile_2bpp_hflip(const uint8_t *tile_data, uint8_t *pixels) {
    for (int row = 0; row < 8; row++) {
        uint8_t lo = tile_data[row * 2];
        uint8_t hi = tile_data[row * 2 + 1];
        for (int col = 0; col < 8; col++) {
            uint8_t color = ((lo >> col) & 1) | (((hi >> col) & 1) << 1);
            pixels[row * 8 + col] = color;
        }
    }
}

/*=============================================================================
 * BG Tilemap Renderer
 *
 * Renders the GBC background layer using virtual VRAM data:
 *   - bg_map[0] = tile indices (32x32 grid)
 *   - bg_map[1] = attribute bytes (palette, bank, flip flags)
 *   - tile_data[0/1] = 2bpp tile pixels in VRAM banks 0/1
 *   - Palettes from GameState bg_palettes[]
 *===========================================================================*/
static void render_bg_tilemap(Renderer *r, GameState *state) {
    VirtualVRAM *vram = r->vram;
    if (!vram) return;

    uint8_t scx = state->hram.scx;
    uint8_t scy = state->hram.scy;

    /* Base LCDC and STAT interrupt split parameters */
    uint8_t base_lcdc = state->hram.lcdc;
    uint8_t lyc = state->hram.lyc;
    uint8_t lcdc_xor = state->hram.stat_lcdc_xor;
    uint8_t stat_routine = state->hram.stat_intr_routine;
    uint8_t lyc_sub = state->hram.lyc_sub;

    /* Pre-compute addressing/tilemap for top portion (before LYC) */
    bool unsigned_addressing = (base_lcdc & 0x10) != 0;
    uint8_t (*tilemap)[VRAM_MAP_SIZE] = (base_lcdc & 0x08)
        ? vram->win_map : vram->bg_map;

    for (int y = 0; y < GBC_SCREEN_HEIGHT; y++) {
        /* STAT interrupt handler dispatch based on stat_intr_routine.
         * GBC STAT fires at start of LYC scanline; register changes take
         * effect for the NEXT scanline, so apply at lyc + 1.
         *
         * stat_routine 1 (Pinball): STAT toggles bit 4 (signed↔unsigned)
         * for the window/scoreboard. On real GBC this also affects BG tiles
         * below LYC, but those are always covered by the window (WY=$86).
         * When the window is hidden (WY=$90 on top stage), BG tiles below
         * LYC must stay signed — so we only apply this in the window renderer,
         * not here. */
        if (stat_routine == 2 && lcdc_xor) {
            /* StatIntrTogglePokedexWindow (home.asm 0xfea):
             * Browse mode (lyc_sub == lyc): single split at LYC.
             *   XOR LCDC bits 3,4 + set SCY = hHBlankSCX.
             * Description mode (lyc_sub != lyc): two-stage split.
             *   At LYC: NO LCDC change, only SCY = lyc - lyc_sub + $40.
             *   At lyc_sub: XOR LCDC bits 3,4 + SCY = hHBlankSCX. */
            if (lyc_sub == lyc) {
                /* Browse mode: single split */
                if (y == lyc + 1) {
                    uint8_t new_lcdc = base_lcdc ^ lcdc_xor;
                    unsigned_addressing = (new_lcdc & 0x10) != 0;
                    tilemap = (new_lcdc & 0x08) ? vram->win_map : vram->bg_map;
                    scy = state->hram.next_frame_hblank_scx;
                }
            } else {
                /* Description mode: two-stage */
                if (y == lyc + 1) {
                    /* First fire: SCY change only, no LCDC XOR */
                    scy = (uint8_t)(lyc - lyc_sub + 0x40);
                }
                if (y == lyc_sub + 1) {
                    /* Second fire: XOR LCDC + SCY = hHBlankSCX */
                    uint8_t new_lcdc = base_lcdc ^ lcdc_xor;
                    unsigned_addressing = (new_lcdc & 0x10) != 0;
                    tilemap = (new_lcdc & 0x08) ? vram->win_map : vram->bg_map;
                    scy = state->hram.next_frame_hblank_scx;
                }
            }
        } else if (stat_routine == 3) {
            /* StatIntrToggleHighScoresWindow: 2-stage SCY split
             * At LYC ($0E): SCY = hHBlankSCX (scores section)
             * At LYC_SUB ($82): SCY = hHBlankSCY (footer section) */
            if (y == lyc) {
                scy = state->hram.next_frame_hblank_scx;
            } else if (y == lyc_sub) {
                scy = state->hram.next_frame_hblank_scy;
            }
        }

        for (int x = 0; x < GBC_SCREEN_WIDTH; x++) {
            /* Apply scroll */
            uint8_t map_x = (uint8_t)(x + scx);
            uint8_t map_y = (uint8_t)(y + scy);

            /* Tile coordinates in the 32x32 map */
            int tx = map_x >> 3;
            int ty = map_y >> 3;
            int map_idx = ty * 32 + tx;

            /* Read tile number and attributes */
            uint8_t tile_num = tilemap[0][map_idx];
            uint8_t attrs = tilemap[1][map_idx];

            uint8_t pal_num  = attrs & 0x07;
            uint8_t tile_bank = (attrs >> 3) & 1;
            bool h_flip = (attrs >> 5) & 1;
            bool v_flip = (attrs >> 6) & 1;

            /* Pixel position within the tile */
            int px = map_x & 7;
            int py = map_y & 7;

            /* Apply flips */
            if (h_flip) px = 7 - px;
            if (v_flip) py = 7 - py;

            /* Compute tile data offset in VRAM */
            int tile_offset;
            if (unsigned_addressing) {
                tile_offset = (int)tile_num * 16;
            } else {
                tile_offset = (int)(int8_t)tile_num * 16 + 0x1000;
            }

            /* Bounds check */
            if (tile_offset < 0 || tile_offset + 15 >= VRAM_TILE_DATA_SIZE) {
                continue;
            }

            /* Read the two bytes for this pixel row */
            const uint8_t *row_data = &vram->tile_data[tile_bank][tile_offset + py * 2];
            uint8_t lo_byte = row_data[0];
            uint8_t hi_byte = row_data[1];

            /* Extract 2-bit color index */
            int bit = 7 - px;
            uint8_t color_idx = ((lo_byte >> bit) & 1) | (((hi_byte >> bit) & 1) << 1);

            /* Track BG color index and BG tile priority for sprite compositing */
            int fb_idx = y * r->screen_w + x;
            r->bg_color_indices[fb_idx] = color_idx;
            /* BG attr bit 7: BG tile has priority over sprites for colors 1-3 */
            r->bg_priority[fb_idx] = (attrs & 0x80) && (color_idx != 0);

            /* Look up color from palette */
            uint16_t rgb555 = state->bg_palettes[pal_num].colors[color_idx];
            uint32_t rgba = rgb555_to_rgba(rgb555);

            r->framebuffer[fb_idx] = rgba;
        }
    }
}

/*=============================================================================
 * Window Layer Renderer
 *
 * Renders the GBC window layer using win_map data.
 * The window overlays the BG starting at pixel (WX-7, WY).
 * LCDC bit 5 enables the window; bit 6 selects the tilemap area.
 * Uses the same tile data and addressing mode as the BG layer.
 *===========================================================================*/
static void render_window_tilemap(Renderer *r, GameState *state) {
    VirtualVRAM *vram = r->vram;
    if (!vram) return;

    /* LCDC bit 5: window enable, bit 0: BG/window master enable */
    if (!(state->hram.lcdc & 0x20)) return;
    if (!(state->hram.lcdc & 0x01)) return;

    int wy = (int)state->hram.wy;
    int wx = (int)state->hram.wx - 7;

    /* Window must be on screen */
    if (wy >= GBC_SCREEN_HEIGHT || wx >= GBC_SCREEN_WIDTH) return;

    /* Apply STAT split to window LCDC if window starts below LYC.
     * On real GBC hardware, STAT-modified LCDC affects the window too.
     * E.g. Pokedex: STAT at LYC=$3B XORs bits 3,4 to switch to unsigned;
     * window at WY=$8C (140) must also use unsigned addressing. */
    uint8_t effective_lcdc = state->hram.lcdc;
    uint8_t lyc = state->hram.lyc;
    uint8_t lcdc_xor = state->hram.stat_lcdc_xor;
    if (state->hram.stat_intr_routine == 1 && wy > (int)lyc) {
        /* Pinball STAT: ASM toggles bit 4 then ANDs mask */
        effective_lcdc = (effective_lcdc ^ 0x10) & state->hram.lcdc_mask;
    } else if (state->hram.stat_intr_routine == 2 && lcdc_xor && wy > (int)lyc) {
        effective_lcdc ^= lcdc_xor;
    }

    bool unsigned_addressing = (effective_lcdc & 0x10) != 0;

    /* LCDC bit 6: window tilemap select
     * 0 = $9800 (bg_map), 1 = $9C00 (win_map) */
    uint8_t (*tilemap)[VRAM_MAP_SIZE] = (effective_lcdc & 0x40)
        ? vram->win_map : vram->bg_map;

    for (int y = (wy < 0 ? 0 : wy); y < GBC_SCREEN_HEIGHT; y++) {
        for (int x = (wx < 0 ? 0 : wx); x < GBC_SCREEN_WIDTH; x++) {
            /* Window-relative coordinates */
            int win_y = y - wy;
            int win_x = x - wx;
            if (win_x < 0) continue;

            /* Tile coordinates in the 32x32 window map */
            int tx = win_x >> 3;
            int ty = win_y >> 3;
            if (tx >= 32 || ty >= 32) continue;
            int map_idx = ty * 32 + tx;

            /* Read tile number and attributes */
            uint8_t tile_num = tilemap[0][map_idx];
            uint8_t attrs = tilemap[1][map_idx];

            uint8_t pal_num   = attrs & 0x07;
            uint8_t tile_bank = (attrs >> 3) & 1;
            bool h_flip = (attrs >> 5) & 1;
            bool v_flip = (attrs >> 6) & 1;

            /* Pixel position within the tile */
            int px = win_x & 7;
            int py = win_y & 7;

            if (h_flip) px = 7 - px;
            if (v_flip) py = 7 - py;

            /* Compute tile data offset */
            int tile_offset;
            if (unsigned_addressing) {
                tile_offset = (int)tile_num * 16;
            } else {
                tile_offset = (int)(int8_t)tile_num * 16 + 0x1000;
            }

            if (tile_offset < 0 || tile_offset + 15 >= VRAM_TILE_DATA_SIZE) {
                continue;
            }

            const uint8_t *row_data = &vram->tile_data[tile_bank][tile_offset + py * 2];
            uint8_t lo_byte = row_data[0];
            uint8_t hi_byte = row_data[1];

            int bit = 7 - px;
            uint8_t color_idx = ((lo_byte >> bit) & 1) | (((hi_byte >> bit) & 1) << 1);

            /* Track window color index and priority for sprite compositing */
            int fb_idx = y * r->screen_w + x;
            r->bg_color_indices[fb_idx] = color_idx;
            /* Window attr bit 7: window tile has priority over sprites for colors 1-3 */
            r->bg_priority[fb_idx] = (attrs & 0x80) && (color_idx != 0);

            uint16_t rgb555 = state->bg_palettes[pal_num].colors[color_idx];
            uint32_t rgba = rgb555_to_rgba(rgb555);

            r->framebuffer[fb_idx] = rgba;
        }
    }
}

/*=============================================================================
 * OAM Sprite Renderer
 *
 * Renders GBC OAM sprites from the sprite_buffer.
 * Sprites always use unsigned tile addressing (tile_num * 16 from $8000).
 * Lower OAM index = higher priority (drawn last to overwrite).
 * Color index 0 is transparent for sprites.
 *===========================================================================*/
static void render_sprites(Renderer *r, GameState *state) {
    VirtualVRAM *vram = r->vram;
    if (!vram) return;

    /* LCDC bit 1: OBJ display enable */
    if (!(state->hram.lcdc & 0x02)) return;

    /* LCDC bit 2: OBJ size (0 = 8x8, 1 = 8x16) */
    int sprite_height = (state->hram.lcdc & 0x04) ? 16 : 8;

    /* STAT split: if stat_intr_routine == 1 (pinball), the STAT interrupt
     * toggles LCDC bits at LYC. If the effective LCDC below LYC has OBJ
     * disabled (bit 1 clear), we must skip sprite rows below LYC+1.
     * Formula: effective = (base_lcdc ^ 0x10) & lcdc_mask */
    int obj_cutoff_y = GBC_SCREEN_HEIGHT;  /* default: no cutoff */
    if (state->hram.stat_intr_routine == 1) {
        uint8_t eff_lcdc = (state->hram.lcdc ^ 0x10) & state->hram.lcdc_mask;
        if (!(eff_lcdc & 0x02)) {
            obj_cutoff_y = (int)state->hram.lyc + 1;
        }
    }

    /* Iterate in reverse so lower-index sprites (higher priority) draw last */
    for (int i = GBC_OAM_ENTRIES - 1; i >= 0; i--) {
        OAMEntry *oam = &state->sprite_buffer[i];

        /* Skip unused/off-screen entries */
        if (oam->y == 0 || oam->x == 0) continue;

        /* GBC hardware offsets: y is offset by 16, x by 8 */
        int screen_y = (int)oam->y - 16;
        int screen_x = (int)oam->x - 8;

        /* Extract attributes */
        uint8_t pal_num   = oam->attr & OAM_PALETTE;
        uint8_t tile_bank = (oam->attr & OAM_TILE_BANK) ? 1 : 0;
        bool h_flip       = (oam->attr & OAM_X_FLIP) != 0;
        bool v_flip       = (oam->attr & OAM_Y_FLIP) != 0;

        /* For 8x16 mode, mask the tile number's bit 0 */
        uint8_t base_tile = oam->tile;
        if (sprite_height == 16) {
            base_tile &= 0xFE;
        }

        /* Render each pixel row */
        for (int row = 0; row < sprite_height; row++) {
            int draw_y = screen_y + row;
            if (draw_y < 0 || draw_y >= GBC_SCREEN_HEIGHT) continue;
            /* STAT split: skip rows below LYC when OBJ is disabled there */
            if (draw_y >= obj_cutoff_y) continue;

            /* Apply V-flip */
            int src_row = v_flip ? (sprite_height - 1 - row) : row;

            /* Determine which tile this row belongs to (for 8x16) */
            uint8_t tile_num;
            int tile_row;
            if (sprite_height == 16) {
                if (src_row < 8) {
                    tile_num = base_tile;
                    tile_row = src_row;
                } else {
                    tile_num = base_tile | 0x01;
                    tile_row = src_row - 8;
                }
            } else {
                tile_num = base_tile;
                tile_row = src_row;
            }

            /* Sprites always use unsigned addressing: tile * 16 */
            int tile_offset = (int)tile_num * 16;
            if (tile_offset + tile_row * 2 + 1 >= VRAM_TILE_DATA_SIZE) continue;

            const uint8_t *row_data = &vram->tile_data[tile_bank][tile_offset + tile_row * 2];
            uint8_t lo_byte = row_data[0];
            uint8_t hi_byte = row_data[1];

            for (int col = 0; col < 8; col++) {
                int draw_x = screen_x + col;
                if (draw_x < 0 || draw_x >= GBC_SCREEN_WIDTH) continue;

                /* Apply H-flip */
                int bit = h_flip ? col : (7 - col);
                uint8_t color_idx = ((lo_byte >> bit) & 1) | (((hi_byte >> bit) & 1) << 1);

                /* Color 0 is transparent for sprites */
                if (color_idx == 0) continue;

                int fb_idx = draw_y * r->screen_w + draw_x;

                /* BG tile priority (attr bit 7): BG/window always on top for colors 1-3 */
                if (r->bg_priority[fb_idx]) continue;

                /* OAM priority (attr bit 7): sprite behind BG/window for colors 1-3 */
                if ((oam->attr & OAM_PRIORITY) && r->bg_color_indices[fb_idx] != 0) continue;

                /* Look up color from OBJ palette */
                uint16_t rgb555 = state->obj_palettes[pal_num].colors[color_idx];
                uint32_t rgba = rgb555_to_rgba(rgb555);

                r->framebuffer[fb_idx] = rgba;
            }
        }
    }
}

/*=============================================================================
 * Combined View Rendering
 *
 * When combined_mode is active, these functions render the full table
 * (top + bottom halves) into a 160x288 framebuffer.
 *===========================================================================*/

static bool is_bonus_stage(uint8_t stage) {
    return stage >= FIRST_BONUS_STAGE;
}

/* Render one half of the table BG into the combined framebuffer.
 * Always uses signed tile addressing (pinball LCDC $67, bit 4=0).
 * height: number of rows to render (may be < 144 to clip overlap). */
static void render_bg_half(Renderer *r, VirtualVRAM *vram, GBCPalette *pals,
                           uint8_t scx, int y_offset, int height) {
    uint8_t (*tilemap)[VRAM_MAP_SIZE] = vram->bg_map;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < GBC_SCREEN_WIDTH; x++) {
            uint8_t map_x = (uint8_t)(x + scx);
            uint8_t map_y = (uint8_t)y;

            int tx = map_x >> 3;
            int ty = map_y >> 3;
            int map_idx = ty * 32 + tx;

            uint8_t tile_num = tilemap[0][map_idx];
            uint8_t attrs = tilemap[1][map_idx];

            uint8_t pal_num  = attrs & 0x07;
            uint8_t tile_bank = (attrs >> 3) & 1;
            bool h_flip = (attrs >> 5) & 1;
            bool v_flip = (attrs >> 6) & 1;

            int px = map_x & 7;
            int py = map_y & 7;
            if (h_flip) px = 7 - px;
            if (v_flip) py = 7 - py;

            /* Signed addressing: (int8_t)tile_num * 16 + 0x1000 */
            int tile_offset = (int)(int8_t)tile_num * 16 + 0x1000;
            if (tile_offset < 0 || tile_offset + 15 >= VRAM_TILE_DATA_SIZE) continue;

            const uint8_t *row_data = &vram->tile_data[tile_bank][tile_offset + py * 2];
            uint8_t lo_byte = row_data[0];
            uint8_t hi_byte = row_data[1];

            int bit = 7 - px;
            uint8_t color_idx = ((lo_byte >> bit) & 1) | (((hi_byte >> bit) & 1) << 1);

            int fb_idx = (y + y_offset) * r->screen_w + x;
            r->bg_color_indices[fb_idx] = color_idx;
            r->bg_priority[fb_idx] = (attrs & 0x80) && (color_idx != 0);

            uint16_t rgb555 = pals[pal_num].colors[color_idx];
            r->framebuffer[fb_idx] = rgb555_to_rgba(rgb555);
        }
    }
}

static void render_bg_combined(Renderer *r, GameState *state) {
    uint8_t scx = state->hram.scx;
    /* Top half: clip bottom row (overlap with bottom half) */
    render_bg_half(r, state->vram_top, state->bg_palettes_top, scx, 0, COMBINED_TOP_HEIGHT);
    /* Bottom half: starts right after clipped top */
    render_bg_half(r, state->vram_bottom, state->bg_palettes_bottom, scx, COMBINED_TOP_HEIGHT, GBC_SCREEN_HEIGHT);
}

/* Render the window layer (scoreboard) in the bottom half of combined view.
 * Uses vram_bottom for tilemap data and bg_palettes_bottom for colors. */
static void render_window_combined(Renderer *r, GameState *state) {
    VirtualVRAM *vram = state->vram_bottom;
    if (!vram) return;

    if (!(state->hram.lcdc & 0x20)) return;
    if (!(state->hram.lcdc & 0x01)) return;

    int wy = (int)state->hram.wy;
    int wx = (int)state->hram.wx - 7;

    /* Bottom half starts after clipped top half in the combined framebuffer */
    int combined_wy = wy + COMBINED_TOP_HEIGHT;
    if (combined_wy >= r->screen_h || wx >= r->screen_w) return;

    /* Determine effective LCDC for window addressing.
     * Pinball STAT routine 1 toggles bit 4 for the window area. */
    uint8_t effective_lcdc = state->hram.lcdc;
    if (state->hram.stat_intr_routine == 1 && wy > (int)state->hram.lyc) {
        effective_lcdc = (effective_lcdc ^ 0x10) & state->hram.lcdc_mask;
    }

    bool unsigned_addressing = (effective_lcdc & 0x10) != 0;
    uint8_t (*tilemap)[VRAM_MAP_SIZE] = (effective_lcdc & 0x40)
        ? vram->win_map : vram->bg_map;

    GBCPalette *pals = state->bg_palettes_bottom;

    for (int y = (combined_wy < 0 ? 0 : combined_wy); y < r->screen_h; y++) {
        for (int x = (wx < 0 ? 0 : wx); x < r->screen_w; x++) {
            int win_y = y - combined_wy;
            int win_x = x - wx;
            if (win_x < 0) continue;

            int tx = win_x >> 3;
            int ty = win_y >> 3;
            if (tx >= 32 || ty >= 32) continue;
            int map_idx = ty * 32 + tx;

            uint8_t tile_num = tilemap[0][map_idx];
            uint8_t attrs = tilemap[1][map_idx];

            uint8_t pal_num   = attrs & 0x07;
            uint8_t tile_bank = (attrs >> 3) & 1;
            bool h_flip = (attrs >> 5) & 1;
            bool v_flip = (attrs >> 6) & 1;

            int px = win_x & 7;
            int py = win_y & 7;
            if (h_flip) px = 7 - px;
            if (v_flip) py = 7 - py;

            int tile_offset;
            if (unsigned_addressing) {
                tile_offset = (int)tile_num * 16;
            } else {
                tile_offset = (int)(int8_t)tile_num * 16 + 0x1000;
            }

            if (tile_offset < 0 || tile_offset + 15 >= VRAM_TILE_DATA_SIZE) continue;

            const uint8_t *row_data = &vram->tile_data[tile_bank][tile_offset + py * 2];
            uint8_t lo_byte = row_data[0];
            uint8_t hi_byte = row_data[1];

            int bit = 7 - px;
            uint8_t color_idx = ((lo_byte >> bit) & 1) | (((hi_byte >> bit) & 1) << 1);

            int fb_idx = y * r->screen_w + x;
            r->bg_color_indices[fb_idx] = color_idx;
            r->bg_priority[fb_idx] = (attrs & 0x80) && (color_idx != 0);

            uint16_t rgb555 = pals[pal_num].colors[color_idx];
            r->framebuffer[fb_idx] = rgb555_to_rgba(rgb555);
        }
    }
}

/* Render a sprite buffer into the combined framebuffer at a given Y offset.
 * Uses the specified VRAM for tile data and palettes for colors. */
static void render_sprite_buffer(Renderer *r, OAMEntry *sprites, int count,
                                 VirtualVRAM *vram, GBCPalette *obj_pals,
                                 int sprite_height, int y_offset) {
    for (int i = count - 1; i >= 0; i--) {
        OAMEntry *oam = &sprites[i];
        if (oam->y == 0 || oam->x == 0) continue;

        int screen_y = (int)oam->y - 16 + y_offset;
        int screen_x = (int)oam->x - 8;

        uint8_t pal_num   = oam->attr & OAM_PALETTE;
        uint8_t tile_bank = (oam->attr & OAM_TILE_BANK) ? 1 : 0;
        bool h_flip       = (oam->attr & OAM_X_FLIP) != 0;
        bool v_flip       = (oam->attr & OAM_Y_FLIP) != 0;

        uint8_t base_tile = oam->tile;
        if (sprite_height == 16) base_tile &= 0xFE;

        for (int row = 0; row < sprite_height; row++) {
            int draw_y = screen_y + row;
            if (draw_y < 0 || draw_y >= r->screen_h) continue;

            int src_row = v_flip ? (sprite_height - 1 - row) : row;

            uint8_t tile_num;
            int tile_row;
            if (sprite_height == 16) {
                if (src_row < 8) {
                    tile_num = base_tile;
                    tile_row = src_row;
                } else {
                    tile_num = base_tile | 0x01;
                    tile_row = src_row - 8;
                }
            } else {
                tile_num = base_tile;
                tile_row = src_row;
            }

            int tile_offset = (int)tile_num * 16;
            if (tile_offset + tile_row * 2 + 1 >= VRAM_TILE_DATA_SIZE) continue;

            const uint8_t *row_data = &vram->tile_data[tile_bank][tile_offset + tile_row * 2];
            uint8_t lo_byte = row_data[0];
            uint8_t hi_byte = row_data[1];

            for (int col = 0; col < 8; col++) {
                int draw_x = screen_x + col;
                if (draw_x < 0 || draw_x >= GBC_SCREEN_WIDTH) continue;

                int bit = h_flip ? col : (7 - col);
                uint8_t color_idx = ((lo_byte >> bit) & 1) | (((hi_byte >> bit) & 1) << 1);

                if (color_idx == 0) continue;

                int fb_idx = draw_y * r->screen_w + draw_x;
                if (r->bg_priority[fb_idx]) continue;
                if ((oam->attr & OAM_PRIORITY) && r->bg_color_indices[fb_idx] != 0) continue;

                uint16_t rgb555 = obj_pals[pal_num].colors[color_idx];
                r->framebuffer[fb_idx] = rgb555_to_rgba(rgb555);
            }
        }
    }
}

/* Render sprites for both halves into the combined framebuffer. */
static void render_sprites_combined(Renderer *r, GameState *state) {
    if (!(state->hram.lcdc & 0x02)) return;
    int sprite_height = (state->hram.lcdc & 0x04) ? 16 : 8;

    /* Determine which buffer is top and which is bottom */
    OAMEntry *top_sprites, *bottom_sprites;
    int top_count, bottom_count;
    VirtualVRAM *top_vram, *bottom_vram;
    GBCPalette *top_pals, *bottom_pals;

    if (state->combined_active_half == 0) {
        /* Active = top, other = bottom */
        top_sprites = state->sprite_buffer;
        top_count = GBC_OAM_ENTRIES;
        top_vram = state->vram_top;
        top_pals = state->obj_palettes_top;
        bottom_sprites = state->sprite_buffer_other;
        bottom_count = GBC_OAM_ENTRIES;
        bottom_vram = state->vram_bottom;
        bottom_pals = state->obj_palettes_bottom;
    } else {
        /* Active = bottom, other = top */
        top_sprites = state->sprite_buffer_other;
        top_count = GBC_OAM_ENTRIES;
        top_vram = state->vram_top;
        top_pals = state->obj_palettes_top;
        bottom_sprites = state->sprite_buffer;
        bottom_count = GBC_OAM_ENTRIES;
        bottom_vram = state->vram_bottom;
        bottom_pals = state->obj_palettes_bottom;
    }

    /* Render top half sprites (Y offset 0) */
    if (top_vram)
        render_sprite_buffer(r, top_sprites, top_count, top_vram, top_pals,
                             sprite_height, 0);
    /* Render bottom half sprites (Y offset = COMBINED_TOP_HEIGHT) */
    if (bottom_vram)
        render_sprite_buffer(r, bottom_sprites, bottom_count, bottom_vram, bottom_pals,
                             sprite_height, COMBINED_TOP_HEIGHT);
}

/* Render the standard 160x144 view centered in the combined framebuffer.
 * Used for bonus stages and non-pinball screens when combined mode is on. */
static void render_game_centered_in_combined(Renderer *r, GameState *state) {
    int y_offset = (r->screen_h - GBC_SCREEN_HEIGHT) / 2;

    /* Shift framebuffer pointers to the centered position.
     * The standard render functions write to indices 0..(160*144-1),
     * so shifting the base pointer places output at the correct offset. */
    uint32_t *orig_fb = r->framebuffer;
    uint8_t *orig_ci = r->bg_color_indices;
    bool *orig_bp = r->bg_priority;
    int orig_h = r->screen_h;

    r->framebuffer = orig_fb + y_offset * r->screen_w;
    r->bg_color_indices = orig_ci + y_offset * r->screen_w;
    r->bg_priority = orig_bp + y_offset * r->screen_w;
    r->screen_h = GBC_SCREEN_HEIGHT;

    /* Clear the centered region to white (GBC default) */
    int area = GBC_SCREEN_WIDTH * GBC_SCREEN_HEIGHT;
    for (int i = 0; i < area; i++) r->framebuffer[i] = 0xFFFFFFFF;
    memset(r->bg_color_indices, 0, area * sizeof(uint8_t));
    memset(r->bg_priority, 0, area * sizeof(bool));

    render_bg_tilemap(r, state);
    render_window_tilemap(r, state);
    render_sprites(r, state);

    /* Restore original pointers and dimensions */
    r->framebuffer = orig_fb;
    r->bg_color_indices = orig_ci;
    r->bg_priority = orig_bp;
    r->screen_h = orig_h;
}

/*
 * Simple color per screen so you can see state changes visually.
 * Each screen gets a distinct background color.
 */
static uint32_t screen_colors[] = {
    0x404040FF, /* 0: SELECT_GAMEBOY_TARGET - dark gray */
    0xFF4040FF, /* 1: ERASE_ALL_DATA - red */
    0x4040FFFF, /* 2: COPYRIGHT - blue */
    0x40FF40FF, /* 3: TITLESCREEN - green */
    0x202020FF, /* 4: PINBALL_GAME - near black (game field) */
    0xFFFF40FF, /* 5: POKEDEX - yellow */
    0xFF8040FF, /* 6: OPTIONS - orange */
    0xFF40FFFF, /* 7: HIGH_SCORES - magenta */
    0x40FFFFFF, /* 8: FIELD_SELECT - cyan */
};

/* Draw a small filled rectangle */
static void draw_rect(Renderer *r, int rx, int ry, int rw, int rh, uint32_t color) {
    for (int y = ry; y < ry + rh && y < r->screen_h; y++) {
        for (int x = rx; x < rx + rw && x < r->screen_w; x++) {
            if (x >= 0 && y >= 0)
                r->framebuffer[y * r->screen_w + x] = color;
        }
    }
}

/* Draw a ball (small circle approximation) */
static void draw_ball(Renderer *r, int cx, int cy, int radius, uint32_t color) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx, dy = y - cy;
            if (dx*dx + dy*dy <= radius*radius) {
                if (x >= 0 && x < r->screen_w && y >= 0 && y < r->screen_h)
                    r->framebuffer[y * r->screen_w + x] = color;
            }
        }
    }
}

void renderer_draw_game(Renderer *renderer, GameState *state) {
    /*
     * If graphics are loaded into VRAM, render the BG tilemap.
     * This works for all screens (pinball, copyright, title, etc.)
     */
    if (state->gfx_loaded && renderer->vram) {
        /* During palette fade, temporarily swap active palettes with faded versions.
         * FadeIn/FadeOut from home/palettes.asm: renderer uses faded palette data
         * while fade_counter > 0, showing the transition from/to white. */
        GBCPalette saved_bg[GBC_NUM_BG_PALETTES];
        GBCPalette saved_obj[GBC_NUM_OBJ_PALETTES];
        bool fading = (state->fade_counter > 0);
        if (fading) {
            memcpy(saved_bg, state->bg_palettes, sizeof(saved_bg));
            memcpy(saved_obj, state->obj_palettes, sizeof(saved_obj));
            memcpy(state->bg_palettes, state->fade_bg_palettes, sizeof(saved_bg));
            memcpy(state->obj_palettes, state->fade_obj_palettes, sizeof(saved_obj));
        }

        if (renderer->combined_mode) {
            if (state->current_screen == SCREEN_PINBALL_GAME &&
                !is_bonus_stage(state->current_stage)) {
                /* Combined view: render both halves of the field */
                render_bg_combined(renderer, state);
                render_window_combined(renderer, state);
                render_sprites_combined(renderer, state);
            } else {
                /* Bonus stage or non-pinball screen: render centered */
                render_game_centered_in_combined(renderer, state);
            }
        } else {
            /* Classic rendering path */
            render_bg_tilemap(renderer, state);
            render_window_tilemap(renderer, state);
            render_sprites(renderer, state);
        }

        /* Restore original palettes after rendering */
        if (fading) {
            memcpy(state->bg_palettes, saved_bg, sizeof(saved_bg));
            memcpy(state->obj_palettes, saved_obj, sizeof(saved_obj));
        }

        return;
    }

    /* No graphics loaded yet — show white (matches GBC boot/fade behavior) */
}

/*=============================================================================
 * Debug Overlay — 4x6 bitmap font + text drawing
 *
 * Minimal embedded font covering ASCII 0x20-0x7E (95 glyphs).
 * Each glyph is 4 pixels wide, 6 pixels tall, stored as 6 bytes per glyph
 * (one byte per row, MSB-first, only upper 4 bits used).
 *===========================================================================*/
static const uint8_t debug_font_4x6[95][6] = {
    /* 0x20 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00},
    /* 0x21 '!' */ {0x40,0x40,0x40,0x00,0x40,0x00},
    /* 0x22 '"' */ {0xA0,0xA0,0x00,0x00,0x00,0x00},
    /* 0x23 '#' */ {0xA0,0xF0,0xA0,0xF0,0xA0,0x00},
    /* 0x24 '$' */ {0x60,0xC0,0x60,0xC0,0x40,0x00},
    /* 0x25 '%' */ {0xA0,0x20,0x40,0x80,0xA0,0x00},
    /* 0x26 '&' */ {0x40,0xA0,0x60,0xA0,0x50,0x00},
    /* 0x27 ''' */ {0x40,0x40,0x00,0x00,0x00,0x00},
    /* 0x28 '(' */ {0x20,0x40,0x40,0x40,0x20,0x00},
    /* 0x29 ')' */ {0x80,0x40,0x40,0x40,0x80,0x00},
    /* 0x2A '*' */ {0xA0,0x40,0xA0,0x00,0x00,0x00},
    /* 0x2B '+' */ {0x00,0x40,0xE0,0x40,0x00,0x00},
    /* 0x2C ',' */ {0x00,0x00,0x00,0x40,0x80,0x00},
    /* 0x2D '-' */ {0x00,0x00,0xE0,0x00,0x00,0x00},
    /* 0x2E '.' */ {0x00,0x00,0x00,0x00,0x40,0x00},
    /* 0x2F '/' */ {0x20,0x20,0x40,0x80,0x80,0x00},
    /* 0x30 '0' */ {0x60,0xA0,0xA0,0xA0,0xC0,0x00},
    /* 0x31 '1' */ {0x40,0xC0,0x40,0x40,0xE0,0x00},
    /* 0x32 '2' */ {0xC0,0x20,0x40,0x80,0xE0,0x00},
    /* 0x33 '3' */ {0xC0,0x20,0x40,0x20,0xC0,0x00},
    /* 0x34 '4' */ {0xA0,0xA0,0xE0,0x20,0x20,0x00},
    /* 0x35 '5' */ {0xE0,0x80,0xC0,0x20,0xC0,0x00},
    /* 0x36 '6' */ {0x60,0x80,0xE0,0xA0,0xE0,0x00},
    /* 0x37 '7' */ {0xE0,0x20,0x40,0x40,0x40,0x00},
    /* 0x38 '8' */ {0xE0,0xA0,0xE0,0xA0,0xE0,0x00},
    /* 0x39 '9' */ {0xE0,0xA0,0xE0,0x20,0xC0,0x00},
    /* 0x3A ':' */ {0x00,0x40,0x00,0x40,0x00,0x00},
    /* 0x3B ';' */ {0x00,0x40,0x00,0x40,0x80,0x00},
    /* 0x3C '<' */ {0x20,0x40,0x80,0x40,0x20,0x00},
    /* 0x3D '=' */ {0x00,0xE0,0x00,0xE0,0x00,0x00},
    /* 0x3E '>' */ {0x80,0x40,0x20,0x40,0x80,0x00},
    /* 0x3F '?' */ {0xC0,0x20,0x40,0x00,0x40,0x00},
    /* 0x40 '@' */ {0x60,0xA0,0xE0,0x80,0x60,0x00},
    /* 0x41 'A' */ {0x40,0xA0,0xE0,0xA0,0xA0,0x00},
    /* 0x42 'B' */ {0xC0,0xA0,0xC0,0xA0,0xC0,0x00},
    /* 0x43 'C' */ {0x60,0x80,0x80,0x80,0x60,0x00},
    /* 0x44 'D' */ {0xC0,0xA0,0xA0,0xA0,0xC0,0x00},
    /* 0x45 'E' */ {0xE0,0x80,0xC0,0x80,0xE0,0x00},
    /* 0x46 'F' */ {0xE0,0x80,0xC0,0x80,0x80,0x00},
    /* 0x47 'G' */ {0x60,0x80,0xA0,0xA0,0x60,0x00},
    /* 0x48 'H' */ {0xA0,0xA0,0xE0,0xA0,0xA0,0x00},
    /* 0x49 'I' */ {0xE0,0x40,0x40,0x40,0xE0,0x00},
    /* 0x4A 'J' */ {0x20,0x20,0x20,0xA0,0x40,0x00},
    /* 0x4B 'K' */ {0xA0,0xA0,0xC0,0xA0,0xA0,0x00},
    /* 0x4C 'L' */ {0x80,0x80,0x80,0x80,0xE0,0x00},
    /* 0x4D 'M' */ {0xA0,0xE0,0xE0,0xA0,0xA0,0x00},
    /* 0x4E 'N' */ {0xA0,0xE0,0xE0,0xA0,0xA0,0x00},
    /* 0x4F 'O' */ {0x40,0xA0,0xA0,0xA0,0x40,0x00},
    /* 0x50 'P' */ {0xC0,0xA0,0xC0,0x80,0x80,0x00},
    /* 0x51 'Q' */ {0x40,0xA0,0xA0,0xA0,0x60,0x00},
    /* 0x52 'R' */ {0xC0,0xA0,0xC0,0xA0,0xA0,0x00},
    /* 0x53 'S' */ {0x60,0x80,0x40,0x20,0xC0,0x00},
    /* 0x54 'T' */ {0xE0,0x40,0x40,0x40,0x40,0x00},
    /* 0x55 'U' */ {0xA0,0xA0,0xA0,0xA0,0xE0,0x00},
    /* 0x56 'V' */ {0xA0,0xA0,0xA0,0xA0,0x40,0x00},
    /* 0x57 'W' */ {0xA0,0xA0,0xE0,0xE0,0xA0,0x00},
    /* 0x58 'X' */ {0xA0,0xA0,0x40,0xA0,0xA0,0x00},
    /* 0x59 'Y' */ {0xA0,0xA0,0x40,0x40,0x40,0x00},
    /* 0x5A 'Z' */ {0xE0,0x20,0x40,0x80,0xE0,0x00},
    /* 0x5B '[' */ {0x60,0x40,0x40,0x40,0x60,0x00},
    /* 0x5C '\' */ {0x80,0x80,0x40,0x20,0x20,0x00},
    /* 0x5D ']' */ {0xC0,0x40,0x40,0x40,0xC0,0x00},
    /* 0x5E '^' */ {0x40,0xA0,0x00,0x00,0x00,0x00},
    /* 0x5F '_' */ {0x00,0x00,0x00,0x00,0xE0,0x00},
    /* 0x60 '`' */ {0x80,0x40,0x00,0x00,0x00,0x00},
    /* 0x61 'a' */ {0x00,0x60,0xA0,0xA0,0x60,0x00},
    /* 0x62 'b' */ {0x80,0xC0,0xA0,0xA0,0xC0,0x00},
    /* 0x63 'c' */ {0x00,0x60,0x80,0x80,0x60,0x00},
    /* 0x64 'd' */ {0x20,0x60,0xA0,0xA0,0x60,0x00},
    /* 0x65 'e' */ {0x00,0x60,0xE0,0x80,0x60,0x00},
    /* 0x66 'f' */ {0x20,0x40,0xE0,0x40,0x40,0x00},
    /* 0x67 'g' */ {0x00,0x60,0xA0,0x60,0xC0,0x00},
    /* 0x68 'h' */ {0x80,0xC0,0xA0,0xA0,0xA0,0x00},
    /* 0x69 'i' */ {0x40,0x00,0x40,0x40,0x40,0x00},
    /* 0x6A 'j' */ {0x20,0x00,0x20,0x20,0xC0,0x00},
    /* 0x6B 'k' */ {0x80,0xA0,0xC0,0xA0,0xA0,0x00},
    /* 0x6C 'l' */ {0xC0,0x40,0x40,0x40,0xE0,0x00},
    /* 0x6D 'm' */ {0x00,0xA0,0xE0,0xA0,0xA0,0x00},
    /* 0x6E 'n' */ {0x00,0xC0,0xA0,0xA0,0xA0,0x00},
    /* 0x6F 'o' */ {0x00,0x40,0xA0,0xA0,0x40,0x00},
    /* 0x70 'p' */ {0x00,0xC0,0xA0,0xC0,0x80,0x00},
    /* 0x71 'q' */ {0x00,0x60,0xA0,0x60,0x20,0x00},
    /* 0x72 'r' */ {0x00,0x60,0x80,0x80,0x80,0x00},
    /* 0x73 's' */ {0x00,0x60,0xC0,0x20,0xC0,0x00},
    /* 0x74 't' */ {0x40,0xE0,0x40,0x40,0x20,0x00},
    /* 0x75 'u' */ {0x00,0xA0,0xA0,0xA0,0x60,0x00},
    /* 0x76 'v' */ {0x00,0xA0,0xA0,0xA0,0x40,0x00},
    /* 0x77 'w' */ {0x00,0xA0,0xA0,0xE0,0xA0,0x00},
    /* 0x78 'x' */ {0x00,0xA0,0x40,0xA0,0x00,0x00},
    /* 0x79 'y' */ {0x00,0xA0,0x60,0x20,0xC0,0x00},
    /* 0x7A 'z' */ {0x00,0xE0,0x40,0x80,0xE0,0x00},
    /* 0x7B '{' */ {0x20,0x40,0x80,0x40,0x20,0x00},
    /* 0x7C '|' */ {0x40,0x40,0x40,0x40,0x40,0x00},
    /* 0x7D '}' */ {0x80,0x40,0x20,0x40,0x80,0x00},
    /* 0x7E '~' */ {0x50,0xA0,0x00,0x00,0x00,0x00},
};

#define DEBUG_FONT_W 4
#define DEBUG_FONT_H 6
#define DEBUG_CHAR_SPACING 1  /* 1px between characters → 5px per char cell */
#define DEBUG_LINE_SPACING 1  /* 1px between lines → 7px per line */

static void debug_draw_char(uint32_t *fb, int fb_w, int fb_h,
                            int x, int y, char ch, uint32_t color) {
    if (ch < 0x20 || ch > 0x7E) return;
    const uint8_t *glyph = debug_font_4x6[ch - 0x20];
    for (int row = 0; row < DEBUG_FONT_H; row++) {
        int py = y + row;
        if (py < 0 || py >= fb_h) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < DEBUG_FONT_W; col++) {
            int px = x + col;
            if (px < 0 || px >= fb_w) continue;
            if (bits & (0x80 >> col)) {
                fb[py * fb_w + px] = color;
            }
        }
    }
}

static void debug_draw_string(uint32_t *fb, int fb_w, int fb_h,
                              int x, int y, const char *str, uint32_t color) {
    int cx = x;
    for (const char *p = str; *p; p++) {
        debug_draw_char(fb, fb_w, fb_h, cx, y, *p, color);
        cx += DEBUG_FONT_W + DEBUG_CHAR_SPACING;
    }
}

static void debug_draw_bg_rect(uint32_t *fb, int fb_w, int fb_h,
                               int rx, int ry, int rw, int rh) {
    for (int y = ry; y < ry + rh && y < fb_h; y++) {
        if (y < 0) continue;
        for (int x = rx; x < rx + rw && x < fb_w; x++) {
            if (x < 0) continue;
            /* Darken existing pixel: halve each channel */
            uint32_t px = fb[y * fb_w + x];
            uint32_t r = (px >> 24) & 0xFF;
            uint32_t g = (px >> 16) & 0xFF;
            uint32_t b = (px >> 8) & 0xFF;
            fb[y * fb_w + x] = ((r >> 1) << 24) | ((g >> 1) << 16) |
                                ((b >> 1) << 8) | 0xFF;
        }
    }
}

void renderer_draw_debug_overlay(Renderer *renderer, GameState *state) {
    uint32_t *fb = renderer->framebuffer;
    int fw = renderer->screen_w;
    int fh = renderer->screen_h;
    uint32_t white = 0xFFFFFFFF;
    uint32_t green = 0x80FF80FF;
    char line[40];

    bool show_ball = (state->current_screen == 4);

    /* Count active sprites for display */
    uint8_t spr_count = 0;
    for (int i = 0; i < GBC_OAM_ENTRIES; i++) {
        if (state->sprite_buffer[i].y != 0 || state->sprite_buffer[i].x != 0)
            spr_count++;
    }
    state->debug_sprite_count = spr_count;

    /* Determine how many lines to show */
    int num_lines = 2;  /* FPS + SCR always shown */
    if (show_ball) num_lines = 6;  /* +STG, BALL, VEL, SPR */

    int line_h = DEBUG_FONT_H + DEBUG_LINE_SPACING;
    int pad = 2;
    int bg_h = num_lines * line_h + pad * 2;
    int bg_w = 20 * (DEBUG_FONT_W + DEBUG_CHAR_SPACING) + pad * 2;

    /* Draw semi-transparent background */
    debug_draw_bg_rect(fb, fw, fh, 0, 0, bg_w, bg_h);

    int tx = pad;
    int ty = pad;

    /* Line 1: FPS */
    snprintf(line, sizeof(line), "FPS: %.1f", (double)state->debug_fps);
    debug_draw_string(fb, fw, fh, tx, ty, line, green);
    ty += line_h;

    /* Line 2: Screen/State */
    snprintf(line, sizeof(line), "SCR: %d/%d", state->current_screen, state->screen_state);
    debug_draw_string(fb, fw, fh, tx, ty, line, white);
    ty += line_h;

    if (show_ball) {
        /* Line 3: Stage */
        snprintf(line, sizeof(line), "STG: 0x%02X", state->current_stage);
        debug_draw_string(fb, fw, fh, tx, ty, line, white);
        ty += line_h;

        /* Line 4: Ball position (hex 8.8 fixed-point) */
        snprintf(line, sizeof(line), "BALL:%03X.%02X %03X.%02X",
            (state->ball_x_pos >> 8) & 0xFFF, state->ball_x_pos & 0xFF,
            (state->ball_y_pos >> 8) & 0xFFF, state->ball_y_pos & 0xFF);
        debug_draw_string(fb, fw, fh, tx, ty, line, white);
        ty += line_h;

        /* Line 5: Ball velocity (hex signed) */
        snprintf(line, sizeof(line), " VEL:%+05X %+05X",
            (int)(int16_t)state->ball_x_velocity,
            (int)(int16_t)state->ball_y_velocity);
        debug_draw_string(fb, fw, fh, tx, ty, line, white);
        ty += line_h;

        /* Line 6: Sprite count */
        snprintf(line, sizeof(line), "SPR: %d/%d", spr_count, GBC_OAM_ENTRIES);
        debug_draw_string(fb, fw, fh, tx, ty, line, white);
    }
}

void renderer_draw_playtest_banner(Renderer *renderer) {
    uint32_t *fb = renderer->framebuffer;
    int fw = renderer->screen_w;
    int fh = renderer->screen_h;

    /* Semi-transparent dark bar at the top */
    int bar_h = DEBUG_FONT_H + 3;
    debug_draw_bg_rect(fb, fw, fh, 0, 0, fw, bar_h);

    /* Draw banner text */
    uint32_t yellow = 0xFFFF40FF;
    uint32_t white  = 0xFFFFFFFF;
    int x = 2;
    int y = 2;
    debug_draw_string(fb, fw, fh, x, y, "PLAYTEST", yellow);
    x += 9 * (DEBUG_FONT_W + DEBUG_CHAR_SPACING);
    debug_draw_string(fb, fw, fh, x, y, "ESC=Exit Z=Launch", white);
}

void renderer_end_frame(Renderer *renderer) {
    SDL_UpdateTexture(
        renderer->framebuffer_tex, NULL,
        renderer->framebuffer,
        renderer->screen_w * sizeof(uint32_t)
    );
    SDL_SetRenderDrawColor(renderer->sdl_renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer->sdl_renderer);

    /* Compute pixel-perfect centered viewport rect */
    int vx, vy, vw, vh;
    platform_get_viewport_rect(renderer->platform, renderer->screen_w, renderer->screen_h,
                               &vx, &vy, &vw, &vh);
    SDL_Rect dest = { vx, vy, vw, vh };
    SDL_RenderCopy(renderer->sdl_renderer, renderer->framebuffer_tex, NULL, &dest);

    SDL_RenderPresent(renderer->sdl_renderer);
}
