#ifndef VWF_H
#define VWF_H

/*
 * Variable Width Font (VWF) rendering subsystem.
 * Translates the GBC Pokedex VWF engine from engine/pokedex/variable_width_font_character.asm
 *
 * The VWF renders text into a pixel buffer using 2bpp tile format,
 * with sub-pixel alignment via bit rotation and masking.
 * Characters >= 0x80 are "tall" (16 pixel rows = 8x16 glyphs).
 * Characters < 0x80 are "short" (8 pixel rows from the middle of 8x16 data).
 */

#include <stdint.h>
#include <stdbool.h>

struct VirtualVRAM;

/* Max buffer size: 108 tiles * 16 bytes = 1728, but we use 32 bytes per
 * tile column for tall chars, so buffer needs (max_tiles/2) * 32 bytes.
 * 108/2 * 32 = 1728 bytes. Keep some headroom. */
#define VWF_BUFFER_SIZE 3456

typedef struct {
    uint8_t line_width;       /* FF8C: pixels per line */
    uint16_t pixel_limit;     /* FF8D:FF8E: current end-of-line pixel position */
    uint8_t max_tiles;        /* FF8F: max tile count in buffer */
    uint16_t pixel_pos;       /* FF90:FF91: current pixel position in buffer */
    uint8_t bg_color;         /* wd860: background color byte for XOR/OR/XOR blend */
    bool force_tall;          /* wd861 bit 2: force 8x16 rendering for all glyphs */
    uint8_t buffer[VWF_BUFFER_SIZE];
    const uint8_t *font_gfx;  /* pointer to interleaved 2bpp font data */
    size_t font_gfx_size;     /* size of font data in bytes */
} VWFState;

/* Initialize VWF state and zero the buffer.
 * line_width: pixels per line (e.g. 0x90 = 144 for descriptions)
 * max_tiles: max 8x8 tile count (e.g. 0x6C = 108 for 3 lines of descriptions)
 * font_gfx: pointer to interleaved 2bpp font character data
 * bg_color: background fill byte (0x00 = dark text, 0xFF = light text) */
void vwf_init(VWFState *vwf, uint8_t line_width, uint8_t max_tiles,
              const uint8_t *font_gfx, size_t font_gfx_size, uint8_t bg_color);

/* Render a single VWF character into the buffer.
 * char_index: glyph index (>= 0x80 = tall 16-row, < 0x80 = short 8-row)
 * char_width: pixel width of this character
 * Returns true if the buffer is full (no more space). */
bool vwf_render_char(VWFState *vwf, uint8_t char_index, uint8_t char_width);

/* Advance VWF position to next line (handles \n / carriage return).
 * Returns true if buffer is full after advancing. */
bool vwf_newline(VWFState *vwf);

/* Map an ASCII character to its VWF description glyph index.
 * Returns the glyph index for use with character_widths[] and vwf_render_char(). */
uint8_t vwf_map_description_char(char c);

/* Render a description text string into the VWF buffer.
 * text: null-terminated ASCII string with \n for line breaks.
 * char_widths: 256-entry width lookup table.
 * Returns pointer to next unrendered character (for multi-page text),
 * or NULL if all text was rendered. */
const char *vwf_render_description(VWFState *vwf, const char *text,
                                    const uint8_t *char_widths);

/* Copy rendered VWF buffer to virtual VRAM.
 * dest_addr: GBC VRAM address (e.g. $9100 for signed tile $10)
 * bank: VRAM bank (0 or 1) */
void vwf_copy_to_vram(const VWFState *vwf, struct VirtualVRAM *vram,
                       uint16_t dest_addr, uint8_t bank);

/* Load VWF font graphics from PNG and interleave tiles.
 * Returns malloc'd buffer of interleaved 2bpp data. Caller must free().
 * Sets *out_size to the byte count. */
uint8_t *vwf_load_font(const char *png_path, size_t *out_size);

#endif /* VWF_H */
