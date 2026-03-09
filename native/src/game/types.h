#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

/*=============================================================================
 * Fixed-point math (8.8 format used by ball physics)
 * Upper byte = integer part, lower byte = fractional part
 * Stored little-endian on GBC (low byte first)
 *===========================================================================*/
/* Signed 8.8 fixed-point (for velocities, forces) */
typedef int16_t fixed8_8;

#define FIXED_TO_INT(x)     ((x) >> 8)
#define INT_TO_FIXED(x)     ((int16_t)(x) << 8)
#define FIXED_FRAC(x)       ((x) & 0xFF)
#define MAKE_FIXED(i, f)    ((int16_t)(((int16_t)(i) << 8) | ((f) & 0xFF)))
#define FIXED_MUL(a, b)     ((fixed8_8)(((int32_t)(a) * (b)) >> 8))

/* Unsigned 8.8 fixed-point (for positions: 0-255 pixel range) */
typedef uint16_t ufixed8_8;

#define UFIXED_TO_INT(x)    ((x) >> 8)
#define UINT_TO_UFIXED(x)   ((uint16_t)(x) << 8)
#define MAKE_UFIXED(i, f)   ((uint16_t)(((uint16_t)(i) << 8) | ((f) & 0xFF)))

/*=============================================================================
 * Animation struct (3 bytes, from wram.asm MACRO animation)
 * Used pervasively for sprite animations
 *===========================================================================*/
typedef struct {
    uint8_t frame_counter;  /* Frames remaining in current animation step */
    uint8_t frame;          /* Current frame ID */
    uint8_t index;          /* Current index in animation table */
} Animation;

/*=============================================================================
 * OAM Entry (4 bytes per sprite, 40 max)
 *===========================================================================*/
typedef struct {
    uint8_t y;
    uint8_t x;
    uint8_t tile;
    uint8_t attr;
} OAMEntry;

/*=============================================================================
 * GBC Palette (4 colors, each 15-bit RGB555)
 * Format: 0bbb bbgg gggr rrrr (little-endian uint16)
 *===========================================================================*/
typedef struct {
    uint16_t colors[4];
} GBCPalette;

/* Extract RGB components from a GBC RGB555 color */
#define RGB555_R(c)  ((c) & 0x1F)
#define RGB555_G(c)  (((c) >> 5) & 0x1F)
#define RGB555_B(c)  (((c) >> 10) & 0x1F)

/* Convert GBC RGB555 to 32-bit RGBA8888 */
static inline uint32_t rgb555_to_rgba(uint16_t c) {
    uint8_t r = (RGB555_R(c) * 255) / 31;
    uint8_t g = (RGB555_G(c) * 255) / 31;
    uint8_t b = (RGB555_B(c) * 255) / 31;
    return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}

/*=============================================================================
 * Scrolling Text struct (8 bytes, from wram.asm MACRO scrolling_text_label)
 *===========================================================================*/
typedef struct {
    uint8_t enabled;
    uint8_t scroll_delay_counter;
    uint8_t scroll_delay;
    uint8_t message_box_offset;
    uint8_t stop_offset;
    uint8_t stop_duration;
    uint8_t source_text_offset;
    uint8_t scroll_steps_remaining;
} ScrollingText;

/*=============================================================================
 * Stationary Text struct (5 bytes, from wram.asm MACRO stationary_text_label)
 *===========================================================================*/
typedef struct {
    uint8_t enabled;
    uint8_t message_box_offset;
    uint8_t source_text_offset;
    uint8_t duration_low;
    uint8_t duration_high;
} StationaryText;

/*=============================================================================
 * Orbiting Ball struct (8 bytes, from wram.asm MACRO orbiting_ball)
 * Used in Mewtwo bonus stage
 *===========================================================================*/
typedef struct {
    uint8_t enabled;
    Animation animation;
    uint8_t animation_group;
    uint8_t x_pos;
    uint8_t y_pos;
    uint8_t pos_index;
} OrbitingBall;

/*=============================================================================
 * Key Config (2 bytes per action - two alternate button masks)
 *===========================================================================*/
typedef struct {
    uint8_t primary;
    uint8_t secondary;
} KeyConfig;

/*=============================================================================
 * High Score entry (13 bytes, from wram.asm MACRO high_scores)
 *===========================================================================*/
typedef struct {
    uint8_t points[6];  /* 6-byte BCD score */
    uint8_t name[3];    /* 3-character name */
    uint8_t id[4];      /* 4 random bytes */
} HighScore;

/*=============================================================================
 * Helper: read/write little-endian 16-bit values
 * (GBC stores 16-bit values little-endian)
 *===========================================================================*/
static inline uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void write_le16(uint8_t *p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

#endif /* TYPES_H */
