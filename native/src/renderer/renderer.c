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
    uint32_t framebuffer[GBC_SCREEN_WIDTH * GBC_SCREEN_HEIGHT];
    uint8_t bg_color_indices[GBC_SCREEN_WIDTH * GBC_SCREEN_HEIGHT];
    bool bg_priority[GBC_SCREEN_WIDTH * GBC_SCREEN_HEIGHT];
    int screen_w;
    int screen_h;
    VirtualVRAM *vram;
};

Renderer *renderer_init(Platform *platform, int screen_w, int screen_h) {
    Renderer *r = calloc(1, sizeof(Renderer));
    if (!r) return NULL;

    r->sdl_renderer = (SDL_Renderer *)platform_get_sdl_renderer(platform);
    r->screen_w = screen_w;
    r->screen_h = screen_h;

    r->framebuffer_tex = SDL_CreateTexture(
        r->sdl_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        screen_w, screen_h
    );
    if (!r->framebuffer_tex) {
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
    free(renderer);
}

void renderer_set_vram(Renderer *renderer, VirtualVRAM *vram) {
    if (renderer) renderer->vram = vram;
}

void renderer_begin_frame(Renderer *renderer) {
    /* Clear framebuffer to white (GBC default) */
    for (int i = 0; i < renderer->screen_w * renderer->screen_h; i++) {
        renderer->framebuffer[i] = 0xFFFFFFFF;
    }
    /* Clear BG priority tracking arrays */
    memset(renderer->bg_color_indices, 0, sizeof(renderer->bg_color_indices));
    memset(renderer->bg_priority, 0, sizeof(renderer->bg_priority));
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

        /* Render BG tilemap from VRAM */
        render_bg_tilemap(renderer, state);
        /* Render window layer on top of BG */
        render_window_tilemap(renderer, state);
        /* Render OAM sprites on top of BG/window */
        render_sprites(renderer, state);

        /* Restore original palettes after rendering */
        if (fading) {
            memcpy(state->bg_palettes, saved_bg, sizeof(saved_bg));
            memcpy(state->obj_palettes, saved_obj, sizeof(saved_obj));
        }

        return;
    }

    /* No graphics loaded yet — show white (matches GBC boot/fade behavior) */
}

void renderer_end_frame(Renderer *renderer) {
    SDL_UpdateTexture(
        renderer->framebuffer_tex, NULL,
        renderer->framebuffer,
        renderer->screen_w * sizeof(uint32_t)
    );
    SDL_RenderClear(renderer->sdl_renderer);
    SDL_RenderCopy(renderer->sdl_renderer, renderer->framebuffer_tex, NULL, NULL);
    SDL_RenderPresent(renderer->sdl_renderer);
}
