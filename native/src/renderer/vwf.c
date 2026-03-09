/*
 * Variable Width Font (VWF) rendering subsystem.
 * Translated from engine/pokedex/variable_width_font_character.asm (LoadDexVWFCharacter_)
 * and engine/pokedex.asm (LoadPokemonDescriptionVWFCharacterTiles, etc.)
 *
 * The VWF engine renders text into a 2bpp pixel buffer with sub-pixel alignment.
 * Each character glyph is shifted right by (pixel_pos & 7) bits, then blended
 * into the buffer using XOR/OR/XOR operations across two tile columns (current
 * and next) using mask/inverted-mask to handle the straddling.
 */

#include "renderer/vwf.h"
#include "renderer/vram.h"
#include "renderer/tile_loader.h"
#include "stb_image.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Shift masks for sub-pixel alignment (Data_8df9 in ASM).
 * mask[n] keeps the rightmost (8-n) bits after rotation. */
static const uint8_t shift_masks[8] = {
    0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01
};

/* Rotate byte right by n bits (emulates the GBC shift lookup tables at pages $58-$5F) */
static inline uint8_t ror8(uint8_t val, uint8_t n) {
    if (n == 0) return val;
    return (uint8_t)((val >> n) | (val << (8 - n)));
}

void vwf_init(VWFState *vwf, uint8_t line_width, uint8_t max_tiles,
              const uint8_t *font_gfx, size_t font_gfx_size, uint8_t bg_color) {
    vwf->line_width = line_width;
    vwf->pixel_limit = line_width;
    vwf->max_tiles = max_tiles;
    vwf->pixel_pos = 0;
    vwf->bg_color = bg_color;
    vwf->force_tall = false;
    vwf->font_gfx = font_gfx;
    vwf->font_gfx_size = font_gfx_size;

    /* Zero out buffer with bg_color (matching Func_28e73 which fills with wd860) */
    uint16_t buf_size = (uint16_t)max_tiles * 16;
    if (buf_size > VWF_BUFFER_SIZE) buf_size = VWF_BUFFER_SIZE;
    memset(vwf->buffer, bg_color, buf_size);
}

bool vwf_render_char(VWFState *vwf, uint8_t char_index, uint8_t char_width) {
    bool is_tall = (char_index >= 0x80) || vwf->force_tall;
    int rows = is_tall ? 16 : 8;

    /* Check if character fits in current line (matching ASM limit check) */
    uint16_t end_pos = vwf->pixel_pos + char_width;
    if (end_pos > vwf->pixel_limit) {
        /* Advance to next line: set position to current limit */
        vwf->pixel_pos = vwf->pixel_limit;
        /* Extend limit by line_width */
        vwf->pixel_limit += vwf->line_width;

        /* Check if buffer is full: (limit / 4) > max_tiles means overflow */
        if ((vwf->pixel_limit >> 2) > vwf->max_tiles) {
            return true; /* Buffer full */
        }
    }

    /* Calculate buffer offset for current tile column.
     * ASM: tall chars (>= $80) use (pos & 0xFFF8) * 4 = 32 bytes/column (sla c; rl b twice)
     *      short chars (< $80) use (pos & 0xFFF8) * 2 = 16 bytes/column (sla c; rl b once)
     * Each tile column holds rows*2 bytes of 2bpp data. */
    uint16_t aligned = (uint16_t)(vwf->pixel_pos & 0xFFF8);
    uint16_t buf_offset = is_tall ? (aligned * 4) : (aligned * 2);

    /* Calculate font data offset: char_index * 32 bytes per character */
    uint32_t font_offset = (uint32_t)char_index * 32;
    if (!is_tall) {
        font_offset += 8; /* Short chars read from middle of the 32-byte slot */
    }

    /* Bounds check font data */
    uint32_t font_end = font_offset + (uint32_t)rows * 2;
    if (font_end > vwf->font_gfx_size) {
        /* Font data out of bounds - skip this character */
        vwf->pixel_pos += char_width;
        return false;
    }

    /* Get sub-pixel shift amount */
    uint8_t sub = (uint8_t)(vwf->pixel_pos & 7);
    uint8_t mask = shift_masks[sub];
    uint8_t inv_mask = (uint8_t)~mask;
    uint8_t bg = vwf->bg_color;

    const uint8_t *font_ptr = vwf->font_gfx + font_offset;

    /* First pass: current tile column (with mask).
     * Reads low bitplane byte (every other byte in 2bpp: bytes 0, 2, 4, ...),
     * rotates right by sub, masks with shift_mask, and XOR/OR/XOR blends
     * into both planes of the buffer. */
    for (int i = 0; i < rows; i++) {
        uint8_t font_byte = font_ptr[i * 2]; /* Low bitplane only */
        uint8_t shifted = ror8(font_byte, sub);
        uint8_t masked = shifted & mask;

        uint16_t idx = buf_offset + (uint16_t)(i * 2);
        if (idx + 1 < VWF_BUFFER_SIZE) {
            vwf->buffer[idx]     = (uint8_t)(((vwf->buffer[idx]     ^ bg) | masked) ^ bg);
            vwf->buffer[idx + 1] = (uint8_t)(((vwf->buffer[idx + 1] ^ bg) | masked) ^ bg);
        }
    }

    /* Second pass: next tile column (with inverted mask).
     * Same font data re-read, but masked with ~shift_mask for overflow bits. */
    uint16_t next_offset = buf_offset + (uint16_t)(rows * 2);
    for (int i = 0; i < rows; i++) {
        uint8_t font_byte = font_ptr[i * 2];
        uint8_t shifted = ror8(font_byte, sub);
        uint8_t masked = shifted & inv_mask;

        uint16_t idx = next_offset + (uint16_t)(i * 2);
        if (idx + 1 < VWF_BUFFER_SIZE) {
            vwf->buffer[idx]     = (uint8_t)(((vwf->buffer[idx]     ^ bg) | masked) ^ bg);
            vwf->buffer[idx + 1] = (uint8_t)(((vwf->buffer[idx + 1] ^ bg) | masked) ^ bg);
        }
    }

    /* Advance pixel position by character width */
    vwf->pixel_pos += char_width;

    return false; /* Not full */
}

bool vwf_newline(VWFState *vwf) {
    /* Advance position to current line end */
    vwf->pixel_pos = vwf->pixel_limit;
    /* Extend limit by line_width */
    vwf->pixel_limit += vwf->line_width;

    /* Check if buffer is full */
    if ((vwf->pixel_limit >> 2) > vwf->max_tiles) {
        return true;
    }
    return false;
}

uint8_t vwf_map_description_char(char c) {
    /* PokedexDescriptionVWFCharacterMapping (0x2957c) */
    if (c >= '0' && c <= '9') return (uint8_t)(c - 0x88); /* '0'=$30 → $A8...$B1 */
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 0x8E); /* 'A'=$41 → $B3...$CC */
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 0x94); /* 'a'=$61 → $CD...$E6 */
    if (c == ' ') return 0x00;
    if (c == ',') return 0xF3;
    if (c == '.') return 0xF4;
    if (c == '`') return 0xFA; /* Apostrophe (backtick in descriptions) */
    if (c == '-') return 0xB2;
    if (c == (char)0xE9) return 0xF9; /* e-acute (é) - UTF-8 single byte won't work,
                                          but descriptions use backtick not é */
    return 0x00; /* Default to space */
}

const char *vwf_render_description(VWFState *vwf, const char *text,
                                    const uint8_t *char_widths) {
    while (*text) {
        unsigned char c = (unsigned char)*text;

        /* Handle UTF-8 é (0xC3 0xA9) → glyph $F9 */
        if (c == 0xC3 && (unsigned char)text[1] == 0xA9) {
            uint8_t glyph = 0xF9;
            uint8_t width = char_widths[glyph];
            if (vwf_render_char(vwf, glyph, width))
                return text;
            text += 2; /* Skip both UTF-8 bytes */
            continue;
        }

        if (c == '\n' || c == '\r') {
            /* Handle newline */
            if (vwf_newline(vwf)) {
                text++;
                return text; /* Buffer full, return next position */
            }
            text++;
            continue;
        }

        /* Map character to glyph index */
        uint8_t glyph = vwf_map_description_char((char)c);
        uint8_t width = char_widths[glyph];

        /* Render the character */
        if (vwf_render_char(vwf, glyph, width)) {
            return text; /* Buffer full */
        }

        text++;
    }

    return NULL; /* All text rendered */
}

void vwf_copy_to_vram(const VWFState *vwf, struct VirtualVRAM *vram,
                       uint16_t dest_addr, uint8_t bank) {
    uint16_t size = (uint16_t)vwf->max_tiles * 16;
    if (size > VWF_BUFFER_SIZE) size = VWF_BUFFER_SIZE;

    vram_write(vram, bank, dest_addr, vwf->buffer, size);
}

uint8_t *vwf_load_font(const char *png_path, size_t *out_size) {
    /* Step 1: Get image dimensions for interleaving */
    int width, height, channels;
    unsigned char *pixels = stbi_load(png_path, &width, &height, &channels, 1);
    if (!pixels) {
        fprintf(stderr, "vwf_load_font: failed to load '%s': %s\n",
                png_path, stbi_failure_reason());
        *out_size = 0;
        return NULL;
    }

    if (width % 8 != 0 || height % 8 != 0) {
        fprintf(stderr, "vwf_load_font: '%s' dimensions %dx%d not multiple of 8\n",
                png_path, width, height);
        stbi_image_free(pixels);
        *out_size = 0;
        return NULL;
    }

    int tiles_per_row = width / 8;
    int tiles_per_col = height / 8;
    int total_tiles = tiles_per_row * tiles_per_col;
    size_t tile_size = 16; /* 2bpp: 8 rows * 2 bytes */
    size_t data_size = (size_t)total_tiles * tile_size;

    /* Step 2: Convert pixels to 2bpp tiles (same as tiles_from_png) */
    uint8_t *raw_tiles = malloc(data_size);
    if (!raw_tiles) {
        stbi_image_free(pixels);
        *out_size = 0;
        return NULL;
    }

    for (int ty = 0; ty < tiles_per_col; ty++) {
        for (int tx = 0; tx < tiles_per_row; tx++) {
            int tile_idx = ty * tiles_per_row + tx;
            uint8_t *dst = raw_tiles + tile_idx * 16;

            for (int row = 0; row < 8; row++) {
                uint8_t lo = 0, hi = 0;
                for (int col = 0; col < 8; col++) {
                    int px = tx * 8 + col;
                    int py = ty * 8 + row;
                    uint8_t gray = pixels[py * width + px];

                    /* Quantize to 2-bit: 0=white, 3=black */
                    uint8_t color;
                    if (gray >= 192) color = 0;
                    else if (gray >= 128) color = 1;
                    else if (gray >= 64) color = 2;
                    else color = 3;

                    lo |= ((color & 1) << (7 - col));
                    hi |= (((color >> 1) & 1) << (7 - col));
                }
                dst[row * 2] = lo;
                dst[row * 2 + 1] = hi;
            }
        }
    }

    stbi_image_free(pixels);

    /* Step 3: Interleave tiles (matching tools/gfx.c interleave function).
     * Pairs consecutive tile rows: row 0 + row 1 form 8x16 characters.
     * Output: char0_top, char0_bottom, char1_top, char1_bottom, ... */
    uint8_t *interleaved = malloc(data_size);
    if (!interleaved) {
        free(raw_tiles);
        *out_size = 0;
        return NULL;
    }
    memset(interleaved, 0, data_size);

    for (int i = 0; i < total_tiles; i++) {
        int tile = i * 2;
        int row = i / tiles_per_row;
        tile -= tiles_per_row * row;
        if (row % 2) {
            tile -= tiles_per_row;
            tile += 1;
        }

        if (tile >= 0 && (size_t)tile * tile_size + tile_size <= data_size) {
            memcpy(&interleaved[tile * tile_size],
                   &raw_tiles[i * tile_size], tile_size);
        }
    }

    free(raw_tiles);
    *out_size = data_size;
    return interleaved;
}
