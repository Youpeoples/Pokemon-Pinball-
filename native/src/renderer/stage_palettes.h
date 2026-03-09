#ifndef STAGE_PALETTES_H
#define STAGE_PALETTES_H

#include <stdint.h>

/* GBC RGB555 color: r | (g << 5) | (b << 10), each component 0-31 */
#define RGB(r, g, b)  ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

/* Each stage has 8 BG palettes + 8 OBJ palettes, 4 colors each = 64 colors total.
 * First 32 uint16_t = BG palettes, next 32 = OBJ palettes. */
#define PALETTE_COLORS_PER_SET  32  /* 8 palettes x 4 colors */
#define PALETTE_TOTAL_COLORS    64  /* BG + OBJ */

/* Get pointer to stage BG palettes (32 uint16_t: 8 palettes x 4 colors) */
const uint16_t *get_stage_bg_palettes(uint8_t stage_id);

/* Get pointer to stage OBJ palettes (32 uint16_t: 8 palettes x 4 colors) */
const uint16_t *get_stage_obj_palettes(uint8_t stage_id);

/* Get pointer to screen BG palettes (32 uint16_t: 8 palettes x 4 colors) */
const uint16_t *get_screen_bg_palettes(uint8_t screen_id);

/* Get pointer to screen OBJ palettes (32 uint16_t: 8 palettes x 4 colors) */
const uint16_t *get_screen_obj_palettes(uint8_t screen_id);

/* Get palettes for a specific high scores stage (0=Red, 1=Blue) */
const uint16_t *get_high_scores_bg_palettes(uint8_t stage);
const uint16_t *get_high_scores_obj_palettes(uint8_t stage);

#endif /* STAGE_PALETTES_H */
