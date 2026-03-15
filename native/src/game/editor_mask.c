/*
 * Editor Mask System - Implementation
 *
 * Collision mask preview, pixel editor, flipper angle tabs,
 * ball test point overlay, and custom mask atlas export.
 */

#include "game/editor_mask.h"
#include "game/editor.h"
#include "game/game_state.h"
#include "game/collision.h"
#include "renderer/tile_loader.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* stb_image_write for PNG export */
#include "stb_image_write.h"

/*=============================================================================
 * BallCollisionTestPointOffsets (duplicated from collision.c for overlay)
 *===========================================================================*/
static const int8_t BallTestPointOffsets[16][2] = {
    { 4,  0}, { 4,  1}, { 3,  3}, { 1,  4},
    { 0,  4}, {-1,  4}, {-3,  3}, {-4,  1},
    {-4,  0}, {-4, -1}, {-3, -3}, {-1, -4},
    { 0, -4}, { 1, -4}, { 3, -3}, { 4, -1},
};

/*=============================================================================
 * Drawing Helpers (duplicated from editor_render.c for standalone use)
 *===========================================================================*/

static void me_set_color(SDL_Renderer *r, uint32_t rgba) {
    SDL_SetRenderDrawColor(r,
        (rgba >> 24) & 0xFF,
        (rgba >> 16) & 0xFF,
        (rgba >> 8) & 0xFF,
        rgba & 0xFF);
}

static void me_fill_rect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t color) {
    me_set_color(r, color);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

static void me_draw_rect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t color) {
    me_set_color(r, color);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(r, &rect);
}

/* Minimal 4x6 bitmap font - just enough for mask editor labels */
static const uint8_t me_font[95][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x40,0x40,0x40,0x00,0x40,0x00}, /* ! */
    {0xA0,0xA0,0x00,0x00,0x00,0x00}, /* " */
    {0xA0,0xF0,0xA0,0xF0,0xA0,0x00}, /* # */
    {0x60,0xC0,0x60,0xC0,0x60,0x00}, /* $ */
    {0x80,0x20,0x40,0x80,0x20,0x00}, /* % */
    {0x40,0xA0,0x40,0xA0,0x50,0x00}, /* & */
    {0x40,0x40,0x00,0x00,0x00,0x00}, /* ' */
    {0x20,0x40,0x40,0x40,0x20,0x00}, /* ( */
    {0x40,0x20,0x20,0x20,0x40,0x00}, /* ) */
    {0xA0,0x40,0xA0,0x00,0x00,0x00}, /* * */
    {0x00,0x40,0xE0,0x40,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x40,0x80,0x00}, /* , */
    {0x00,0x00,0xE0,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x40,0x00}, /* . */
    {0x20,0x20,0x40,0x80,0x80,0x00}, /* / */
    {0x40,0xA0,0xA0,0xA0,0x40,0x00}, /* 0 */
    {0x40,0xC0,0x40,0x40,0xE0,0x00}, /* 1 */
    {0xC0,0x20,0x40,0x80,0xE0,0x00}, /* 2 */
    {0xC0,0x20,0x40,0x20,0xC0,0x00}, /* 3 */
    {0xA0,0xA0,0xE0,0x20,0x20,0x00}, /* 4 */
    {0xE0,0x80,0xC0,0x20,0xC0,0x00}, /* 5 */
    {0x60,0x80,0xC0,0xA0,0x40,0x00}, /* 6 */
    {0xE0,0x20,0x40,0x40,0x40,0x00}, /* 7 */
    {0x40,0xA0,0x40,0xA0,0x40,0x00}, /* 8 */
    {0x40,0xA0,0x60,0x20,0xC0,0x00}, /* 9 */
    {0x00,0x40,0x00,0x40,0x00,0x00}, /* : */
    {0x00,0x40,0x00,0x40,0x80,0x00}, /* ; */
    {0x20,0x40,0x80,0x40,0x20,0x00}, /* < */
    {0x00,0xE0,0x00,0xE0,0x00,0x00}, /* = */
    {0x80,0x40,0x20,0x40,0x80,0x00}, /* > */
    {0xC0,0x20,0x40,0x00,0x40,0x00}, /* ? */
    {0x40,0xA0,0xE0,0x80,0x60,0x00}, /* @ */
    {0x40,0xA0,0xE0,0xA0,0xA0,0x00}, /* A */
    {0xC0,0xA0,0xC0,0xA0,0xC0,0x00}, /* B */
    {0x60,0x80,0x80,0x80,0x60,0x00}, /* C */
    {0xC0,0xA0,0xA0,0xA0,0xC0,0x00}, /* D */
    {0xE0,0x80,0xC0,0x80,0xE0,0x00}, /* E */
    {0xE0,0x80,0xC0,0x80,0x80,0x00}, /* F */
    {0x60,0x80,0xA0,0xA0,0x60,0x00}, /* G */
    {0xA0,0xA0,0xE0,0xA0,0xA0,0x00}, /* H */
    {0xE0,0x40,0x40,0x40,0xE0,0x00}, /* I */
    {0x20,0x20,0x20,0xA0,0x40,0x00}, /* J */
    {0xA0,0xA0,0xC0,0xA0,0xA0,0x00}, /* K */
    {0x80,0x80,0x80,0x80,0xE0,0x00}, /* L */
    {0xA0,0xE0,0xA0,0xA0,0xA0,0x00}, /* M */
    {0xA0,0xE0,0xE0,0xA0,0xA0,0x00}, /* N */
    {0x40,0xA0,0xA0,0xA0,0x40,0x00}, /* O */
    {0xC0,0xA0,0xC0,0x80,0x80,0x00}, /* P */
    {0x40,0xA0,0xA0,0xA0,0x60,0x00}, /* Q */
    {0xC0,0xA0,0xC0,0xA0,0xA0,0x00}, /* R */
    {0x60,0x80,0x40,0x20,0xC0,0x00}, /* S */
    {0xE0,0x40,0x40,0x40,0x40,0x00}, /* T */
    {0xA0,0xA0,0xA0,0xA0,0x40,0x00}, /* U */
    {0xA0,0xA0,0xA0,0x40,0x40,0x00}, /* V */
    {0xA0,0xA0,0xE0,0xE0,0xA0,0x00}, /* W */
    {0xA0,0xA0,0x40,0xA0,0xA0,0x00}, /* X */
    {0xA0,0xA0,0x40,0x40,0x40,0x00}, /* Y */
    {0xE0,0x20,0x40,0x80,0xE0,0x00}, /* Z */
    {0x60,0x40,0x40,0x40,0x60,0x00}, /* [ */
    {0x80,0x80,0x40,0x20,0x20,0x00}, /* \ */
    {0xC0,0x40,0x40,0x40,0xC0,0x00}, /* ] */
    {0x40,0xA0,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0xE0,0x00}, /* _ */
    {0x80,0x40,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x60,0xA0,0xA0,0x60,0x00}, /* a */
    {0x80,0xC0,0xA0,0xA0,0xC0,0x00}, /* b */
    {0x00,0x60,0x80,0x80,0x60,0x00}, /* c */
    {0x20,0x60,0xA0,0xA0,0x60,0x00}, /* d */
    {0x00,0x40,0xE0,0x80,0x60,0x00}, /* e */
    {0x20,0x40,0xE0,0x40,0x40,0x00}, /* f */
    {0x00,0x60,0xA0,0x60,0xC0,0x00}, /* g */
    {0x80,0xC0,0xA0,0xA0,0xA0,0x00}, /* h */
    {0x40,0x00,0x40,0x40,0x40,0x00}, /* i */
    {0x20,0x00,0x20,0x20,0xC0,0x00}, /* j */
    {0x80,0xA0,0xC0,0xA0,0xA0,0x00}, /* k */
    {0xC0,0x40,0x40,0x40,0xE0,0x00}, /* l */
    {0x00,0xA0,0xE0,0xA0,0xA0,0x00}, /* m */
    {0x00,0xC0,0xA0,0xA0,0xA0,0x00}, /* n */
    {0x00,0x40,0xA0,0xA0,0x40,0x00}, /* o */
    {0x00,0xC0,0xA0,0xC0,0x80,0x00}, /* p */
    {0x00,0x60,0xA0,0x60,0x20,0x00}, /* q */
    {0x00,0x60,0x80,0x80,0x80,0x00}, /* r */
    {0x00,0x60,0xC0,0x20,0xC0,0x00}, /* s */
    {0x40,0xE0,0x40,0x40,0x20,0x00}, /* t */
    {0x00,0xA0,0xA0,0xA0,0x60,0x00}, /* u */
    {0x00,0xA0,0xA0,0x40,0x40,0x00}, /* v */
    {0x00,0xA0,0xE0,0xE0,0xA0,0x00}, /* w */
    {0x00,0xA0,0x40,0xA0,0x00,0x00}, /* x */
    {0x00,0xA0,0x60,0x20,0xC0,0x00}, /* y */
    {0x00,0xE0,0x40,0x80,0xE0,0x00}, /* z */
    {0x20,0x40,0xC0,0x40,0x20,0x00}, /* { */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* | */
    {0x80,0x40,0x60,0x40,0x80,0x00}, /* } */
    {0x50,0xA0,0x00,0x00,0x00,0x00}, /* ~ */
};

static void me_draw_char(SDL_Renderer *r, int px, int py, char ch, uint32_t color, int s) {
    if (ch < 0x20 || ch > 0x7E) return;
    const uint8_t *glyph = me_font[ch - 0x20];
    me_set_color(r, color);
    for (int row = 0; row < 6; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 4; col++) {
            if (bits & (0x80 >> col)) {
                SDL_Rect pr = { px + col * s, py + row * s, s, s };
                SDL_RenderFillRect(r, &pr);
            }
        }
    }
}

static void me_draw_text(SDL_Renderer *r, int px, int py, const char *text, uint32_t color, int s) {
    int char_w = (4 + 1) * s;
    for (int i = 0; text[i]; i++) {
        me_draw_char(r, px + s, py + s, text[i], 0x000000FF, s); /* shadow */
        me_draw_char(r, px, py, text[i], color, s);
        px += char_w;
    }
}

static int me_text_width(const char *text, int s) {
    int len = (int)strlen(text);
    return len > 0 ? len * (4 + 1) * s - s : 0;
}

/*=============================================================================
 * Collision Mask Filename Resolution (mirrors collision.c logic)
 *===========================================================================*/

static const char *get_mask_filename_for_stage(uint8_t stage, uint8_t coll_state) {
    switch (stage) {
        case 0x00: /* STAGE_RED_FIELD_TOP */
            switch (coll_state / 2) {
                case 0: return "data/collision/masks/red_stage_top_0.png";
                case 1: return "data/collision/masks/red_stage_top_1.png";
                case 2: return "data/collision/masks/red_stage_top_2.png";
                case 3: return "data/collision/masks/red_stage_top_3.png";
                default: return "data/collision/masks/red_stage_top_2.png";
            }
        case 0x01: return "data/collision/masks/red_stage_bottom.png";
        case 0x04: return "data/collision/masks/blue_stage_top.png";
        case 0x05: return "data/collision/masks/blue_stage_bottom.png";
        case 0x07: case 0x06: return "data/collision/masks/gengar_bonus.png";
        case 0x09: case 0x08: return "data/collision/masks/mewtwo_bonus.png";
        case 0x0B: case 0x0A: return "data/collision/masks/meowth_bonus.png";
        case 0x0D: case 0x0C: return "data/collision/masks/diglett_bonus.png";
        case 0x0F: case 0x0E: return "data/collision/masks/seel_bonus.png";
        default: return "data/collision/masks/red_stage_top_2.png";
    }
}

/*=============================================================================
 * Mask Byte Helpers
 *===========================================================================*/

/* Convert 1bpp mask byte array (8 bytes) to 8x8 pixel grid */
static void mask_bytes_to_pixels(const uint8_t mask[MASK_BYTES],
                                  uint8_t pixels[MASK_SIZE][MASK_SIZE]) {
    for (int row = 0; row < MASK_SIZE; row++) {
        for (int col = 0; col < MASK_SIZE; col++) {
            pixels[row][col] = (mask[row] & (0x80 >> col)) ? 1 : 0;
        }
    }
}

/* Convert 8x8 pixel grid to 1bpp mask byte array */
static void pixels_to_mask_bytes(const uint8_t pixels[MASK_SIZE][MASK_SIZE],
                                  uint8_t mask[MASK_BYTES]) {
    for (int row = 0; row < MASK_SIZE; row++) {
        uint8_t byte = 0;
        for (int col = 0; col < MASK_SIZE; col++) {
            if (pixels[row][col]) {
                byte |= (0x80 >> col);
            }
        }
        mask[row] = byte;
    }
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

MaskEditorState *mask_editor_create(void) {
    MaskEditorState *me = calloc(1, sizeof(MaskEditorState));
    if (!me) return NULL;
    me->flipper_angle_tab = 0;
    me->show_test_points = false;
    return me;
}

void mask_editor_free(MaskEditorState *me) {
    if (!me) return;
    /* Free all cache entries */
    for (int i = 0; i < 16; i++) {
        free(me->cache.stage_masks[i].data);
    }
    free(me->cache.bottom_left.data);
    free(me->cache.bottom_right.data);
    free(me->cache.bottom_left_bonus.data);
    free(me->cache.bottom_right_bonus.data);
    free(me);
}

/*=============================================================================
 * Mask Cache Operations
 *===========================================================================*/

const uint8_t *mask_cache_get_stage(MaskEditorState *me, GameState *state,
                                     uint8_t stage_id, int *out_num_masks) {
    if (stage_id >= 16) { *out_num_masks = 0; return NULL; }

    MaskCacheEntry *entry = &me->cache.stage_masks[stage_id];

    /* Build expected path.
     * Use collision_state 0 to match the editor import (editor.c sets
     * stage_collision_state = 0 when importing collision data). */
    uint8_t coll_state = 0;
    const char *rel = get_mask_filename_for_stage(stage_id, coll_state);
    char path[260];
    snprintf(path, sizeof(path), "%s/%s", state->asset_base_path, rel);

    /* Return cached if path matches */
    if (entry->loaded && strcmp(entry->source_path, path) == 0) {
        *out_num_masks = entry->num_masks;
        return entry->data;
    }

    /* Load from PNG */
    free(entry->data);
    entry->data = masks_from_png(path, &entry->data_size);
    if (entry->data) {
        entry->num_masks = (int)(entry->data_size / MASK_BYTES);
        strncpy(entry->source_path, path, sizeof(entry->source_path) - 1);
        entry->source_path[sizeof(entry->source_path) - 1] = '\0';
        entry->loaded = true;
        *out_num_masks = entry->num_masks;
        return entry->data;
    }

    entry->loaded = false;
    *out_num_masks = 0;
    return NULL;
}

const uint8_t *mask_cache_get_flipper(MaskEditorState *me, GameState *state,
                                       bool is_right, bool is_bonus,
                                       size_t *out_size) {
    MaskCacheEntry *entry;
    const char *filename;

    if (is_right) {
        if (is_bonus) {
            entry = &me->cache.bottom_right_bonus;
            filename = "data/collision/masks/bottom_right_bonus_stage_masks.png";
        } else {
            entry = &me->cache.bottom_right;
            filename = "data/collision/masks/bottom_right_masks.png";
        }
    } else {
        if (is_bonus) {
            entry = &me->cache.bottom_left_bonus;
            filename = "data/collision/masks/bottom_left_bonus_stage_masks.png";
        } else {
            entry = &me->cache.bottom_left;
            filename = "data/collision/masks/bottom_left_masks.png";
        }
    }

    char path[260];
    snprintf(path, sizeof(path), "%s/%s", state->asset_base_path, filename);

    if (entry->loaded && strcmp(entry->source_path, path) == 0) {
        *out_size = entry->data_size;
        return entry->data;
    }

    free(entry->data);
    entry->data = masks_from_png(path, &entry->data_size);
    if (entry->data) {
        entry->num_masks = (int)(entry->data_size / MASK_BYTES);
        strncpy(entry->source_path, path, sizeof(entry->source_path) - 1);
        entry->source_path[sizeof(entry->source_path) - 1] = '\0';
        entry->loaded = true;
        *out_size = entry->data_size;
        return entry->data;
    }

    entry->loaded = false;
    *out_size = 0;
    return NULL;
}

void mask_cache_invalidate(MaskEditorState *me) {
    for (int i = 0; i < 16; i++) {
        me->cache.stage_masks[i].loaded = false;
    }
    me->cache.bottom_left.loaded = false;
    me->cache.bottom_right.loaded = false;
    me->cache.bottom_left_bonus.loaded = false;
    me->cache.bottom_right_bonus.loaded = false;
}

/*=============================================================================
 * Mask Data Access
 *===========================================================================*/

void mask_editor_get_mask(MaskEditorState *me, GameState *state,
                          uint8_t attr, uint8_t out_mask[MASK_BYTES]) {
    /* Return custom mask if this attribute has been modified */
    if (me->mask_modified[attr]) {
        memcpy(out_mask, me->custom_masks[attr], MASK_BYTES);
        return;
    }

    /* Load from cache using editor's stage_id (not state->current_stage,
     * which reflects whatever stage the game was on before editor opened) */
    int num_masks = 0;
    const uint8_t *masks = mask_cache_get_stage(me, state,
        me->editor_stage_id, &num_masks);

    if (masks && attr < num_masks) {
        memcpy(out_mask, masks + (size_t)attr * MASK_BYTES, MASK_BYTES);
    } else {
        memset(out_mask, 0, MASK_BYTES);
    }
}

void mask_editor_get_flipper_mask(MaskEditorState *me, GameState *state,
                                   uint8_t mask_index, bool is_right,
                                   int angle_set, uint8_t out_mask[MASK_BYTES]) {
    /* Check custom flipper masks first */
    int flat_idx = angle_set * FLIPPER_MASK_COUNT + mask_index;
    if (is_right) {
        if (me->flipper_right_modified[flat_idx]) {
            memcpy(out_mask, me->flipper_right_custom[flat_idx], MASK_BYTES);
            return;
        }
    } else {
        if (me->flipper_left_modified[flat_idx]) {
            memcpy(out_mask, me->flipper_left_custom[flat_idx], MASK_BYTES);
            return;
        }
    }

    /* Load from cache using editor's stage_id */
    bool is_bonus = (me->editor_stage_id >= 0x07); /* FIRST_BONUS_STAGE */
    size_t data_size = 0;
    const uint8_t *data = mask_cache_get_flipper(me, state, is_right, is_bonus, &data_size);

    if (data) {
        size_t offset = (size_t)angle_set * 0x80 + (size_t)mask_index * MASK_BYTES;
        if (offset + MASK_BYTES <= data_size) {
            memcpy(out_mask, data + offset, MASK_BYTES);
            return;
        }
    }
    memset(out_mask, 0, MASK_BYTES);
}

/*=============================================================================
 * Pixel Editor Operations
 *===========================================================================*/

void mask_editor_open(MaskEditorState *me, GameState *state, uint8_t attr) {
    me->active = true;
    me->edit_attr = attr;
    me->undo_count = 0;
    me->undo_pos = 0;
    me->painting = false;

    /* Determine if this is a flipper mask */
    me->is_flipper_mask = (attr >= 0xE0);
    me->is_right_flipper = (attr >= 0xF0);
    me->flipper_angle_tab = 0;

    if (me->is_flipper_mask) {
        /* Load all 3 angle sets */
        uint8_t mask_index = (attr - 0xE0) & 0x0F;
        for (int angle = 0; angle < FLIPPER_ANGLE_SETS; angle++) {
            uint8_t mask_data[MASK_BYTES];
            mask_editor_get_flipper_mask(me, state, mask_index,
                                          me->is_right_flipper, angle, mask_data);
            mask_bytes_to_pixels(mask_data, me->flipper_pixels[angle]);
        }
        /* Start with angle tab 0 */
        memcpy(me->pixels, me->flipper_pixels[0], sizeof(me->pixels));
    } else {
        /* Load the static mask */
        uint8_t mask_data[MASK_BYTES];
        mask_editor_get_mask(me, state, attr, mask_data);
        mask_bytes_to_pixels(mask_data, me->pixels);
    }

    /* Save initial state for undo */
    mask_editor_push_undo(me);
}

void mask_editor_close(MaskEditorState *me) {
    if (!me->active) return;
    mask_editor_apply(me);
    me->active = false;
}

void mask_editor_apply(MaskEditorState *me) {
    if (me->is_flipper_mask) {
        /* Save current tab's pixels back */
        memcpy(me->flipper_pixels[me->flipper_angle_tab], me->pixels,
               sizeof(me->pixels));

        uint8_t mask_index = (me->edit_attr - 0xE0) & 0x0F;
        for (int angle = 0; angle < FLIPPER_ANGLE_SETS; angle++) {
            uint8_t mask_data[MASK_BYTES];
            pixels_to_mask_bytes(me->flipper_pixels[angle], mask_data);
            int flat_idx = angle * FLIPPER_MASK_COUNT + mask_index;
            if (me->is_right_flipper) {
                memcpy(me->flipper_right_custom[flat_idx], mask_data, MASK_BYTES);
                me->flipper_right_modified[flat_idx] = true;
            } else {
                memcpy(me->flipper_left_custom[flat_idx], mask_data, MASK_BYTES);
                me->flipper_left_modified[flat_idx] = true;
            }
        }
        me->any_flipper_modified = true;
    } else {
        uint8_t mask_data[MASK_BYTES];
        pixels_to_mask_bytes(me->pixels, mask_data);
        memcpy(me->custom_masks[me->edit_attr], mask_data, MASK_BYTES);
        me->mask_modified[me->edit_attr] = true;
        me->any_modified = true;

        /* Live update: push change to the active collision engine */
        collision_apply_custom_mask(me->edit_attr, mask_data, MASK_BYTES);
    }
}

void mask_editor_push_undo(MaskEditorState *me) {
    uint8_t mask_data[MASK_BYTES];
    pixels_to_mask_bytes(me->pixels, mask_data);

    /* If we're not at the end of undo history, truncate redo */
    me->undo_count = me->undo_pos + 1;
    if (me->undo_count > MASK_UNDO_DEPTH) {
        /* Shift everything down */
        memmove(me->undo_stack[0], me->undo_stack[1],
                (MASK_UNDO_DEPTH - 1) * MASK_BYTES);
        me->undo_count = MASK_UNDO_DEPTH;
        me->undo_pos = MASK_UNDO_DEPTH - 1;
    }
    memcpy(me->undo_stack[me->undo_pos], mask_data, MASK_BYTES);
}

void mask_editor_undo(MaskEditorState *me) {
    if (me->undo_pos <= 0) return;
    /* Save current state for redo first */
    uint8_t cur[MASK_BYTES];
    pixels_to_mask_bytes(me->pixels, cur);
    if (me->undo_pos < MASK_UNDO_DEPTH) {
        memcpy(me->undo_stack[me->undo_pos], cur, MASK_BYTES);
        if (me->undo_pos >= me->undo_count) me->undo_count = me->undo_pos + 1;
    }
    me->undo_pos--;
    mask_bytes_to_pixels(me->undo_stack[me->undo_pos], me->pixels);
}

void mask_editor_redo(MaskEditorState *me) {
    if (me->undo_pos + 1 >= me->undo_count) return;
    me->undo_pos++;
    mask_bytes_to_pixels(me->undo_stack[me->undo_pos], me->pixels);
}

/* Tool operations */
void mask_editor_fill(MaskEditorState *me) {
    mask_editor_push_undo(me);
    memset(me->pixels, 1, sizeof(me->pixels));
    me->undo_pos++;
    pixels_to_mask_bytes(me->pixels, me->undo_stack[me->undo_pos]);
    me->undo_count = me->undo_pos + 1;
}

void mask_editor_clear(MaskEditorState *me) {
    mask_editor_push_undo(me);
    memset(me->pixels, 0, sizeof(me->pixels));
    me->undo_pos++;
    pixels_to_mask_bytes(me->pixels, me->undo_stack[me->undo_pos]);
    me->undo_count = me->undo_pos + 1;
}

void mask_editor_mirror_h(MaskEditorState *me) {
    mask_editor_push_undo(me);
    for (int row = 0; row < MASK_SIZE; row++) {
        for (int col = 0; col < MASK_SIZE / 2; col++) {
            uint8_t temp = me->pixels[row][col];
            me->pixels[row][col] = me->pixels[row][MASK_SIZE - 1 - col];
            me->pixels[row][MASK_SIZE - 1 - col] = temp;
        }
    }
    me->undo_pos++;
    pixels_to_mask_bytes(me->pixels, me->undo_stack[me->undo_pos]);
    me->undo_count = me->undo_pos + 1;
}

void mask_editor_mirror_v(MaskEditorState *me) {
    mask_editor_push_undo(me);
    for (int row = 0; row < MASK_SIZE / 2; row++) {
        for (int col = 0; col < MASK_SIZE; col++) {
            uint8_t temp = me->pixels[row][col];
            me->pixels[row][col] = me->pixels[MASK_SIZE - 1 - row][col];
            me->pixels[MASK_SIZE - 1 - row][col] = temp;
        }
    }
    me->undo_pos++;
    pixels_to_mask_bytes(me->pixels, me->undo_stack[me->undo_pos]);
    me->undo_count = me->undo_pos + 1;
}

void mask_editor_rotate_90(MaskEditorState *me) {
    mask_editor_push_undo(me);
    uint8_t temp[MASK_SIZE][MASK_SIZE];
    for (int row = 0; row < MASK_SIZE; row++) {
        for (int col = 0; col < MASK_SIZE; col++) {
            temp[col][MASK_SIZE - 1 - row] = me->pixels[row][col];
        }
    }
    memcpy(me->pixels, temp, sizeof(me->pixels));
    me->undo_pos++;
    pixels_to_mask_bytes(me->pixels, me->undo_stack[me->undo_pos]);
    me->undo_count = me->undo_pos + 1;
}

/*=============================================================================
 * Rendering: Inline Mask Preview (8x8 scaled, shown in tooltip)
 *===========================================================================*/

void mask_editor_render_preview(MaskEditorState *me, GameState *state,
                                 void *sdl_renderer, int x, int y,
                                 int scale, uint8_t attr) {
    SDL_Renderer *r = (SDL_Renderer *)sdl_renderer;
    uint8_t mask_data[MASK_BYTES];

    if (attr >= 0xE0) {
        /* For flipper masks, show angle 0 default */
        uint8_t mask_index = (attr - 0xE0) & 0x0F;
        bool is_right = (attr >= 0xF0);
        mask_editor_get_flipper_mask(me, state, mask_index, is_right, 0, mask_data);
    } else {
        mask_editor_get_mask(me, state, attr, mask_data);
    }

    /* Draw 8x8 grid at the given scale */
    int cell = scale;  /* Each mask pixel = scale screen pixels */
    int total = MASK_SIZE * cell;

    /* Background */
    me_fill_rect(r, x, y, total, total, 0x404040FF);

    /* Draw mask pixels */
    for (int row = 0; row < MASK_SIZE; row++) {
        for (int col = 0; col < MASK_SIZE; col++) {
            bool solid = (mask_data[row] & (0x80 >> col)) != 0;
            uint32_t color = solid ? 0xFFFFFFFF : 0x000000FF;
            me_fill_rect(r, x + col * cell, y + row * cell, cell, cell, color);
        }
    }

    /* Grid lines */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i <= MASK_SIZE; i++) {
        me_set_color(r, 0x808080A0);
        SDL_RenderDrawLine(r, x + i * cell, y, x + i * cell, y + total);
        SDL_RenderDrawLine(r, x, y + i * cell, x + total, y + i * cell);
    }

    /* Outline */
    me_draw_rect(r, x, y, total, total, 0xA0A0A0FF);
}

/*=============================================================================
 * Rendering: Mask Pixel Editor Panel
 *===========================================================================*/

void mask_editor_render_panel(MaskEditorState *me, void *sdl_renderer,
                               EditorState *editor, int win_w, int win_h) {
    if (!me->active) return;
    SDL_Renderer *r = (SDL_Renderer *)sdl_renderer;
    int s = editor->ui_scale;

    /* Panel dimensions */
    int cell = MASK_CELL_SIZE;
    int grid_w = MASK_SIZE * cell;
    int grid_h = MASK_SIZE * cell;
    int pad = 8;
    int tab_h = me->is_flipper_mask ? (7 * s + 8) : 0;
    int toolbar_h = 7 * s + 8;

    /* Panel width: just make it wide enough */
    int char_w = (4 + 1) * s;
    int panel_w = 5 * (5 * char_w + 16) + pad * 2 + 4 * s;
    int panel_h = tab_h + grid_h + toolbar_h * 2 + pad * 4 + 80;

    /* Center panel on screen */
    int px = (win_w - panel_w) / 2;
    int py = (win_h - panel_h) / 2;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    /* Dim background */
    me_fill_rect(r, 0, 0, win_w, win_h, 0x00000080);

    /* Panel background */
    me_fill_rect(r, px, py, panel_w, panel_h, 0x1A1A1AF0);
    me_draw_rect(r, px, py, panel_w, panel_h, 0x808080FF);

    int cy = py + pad;

    /* Title */
    char title[48];
    snprintf(title, sizeof(title), "MASK EDITOR: 0x%02X", me->edit_attr);
    me_draw_text(r, px + pad, cy, title, 0xFFFF80FF, s);
    cy += 7 * s + 4;

    /* Flipper angle tabs */
    if (me->is_flipper_mask) {
        static const char *tab_labels[] = { "0-6 deg", "7-13 deg", "14+ deg" };
        int tab_w = (panel_w - pad * 2) / 3;
        for (int t = 0; t < FLIPPER_ANGLE_SETS; t++) {
            int tx = px + pad + t * tab_w;
            bool active = (t == me->flipper_angle_tab);
            uint32_t bg = active ? 0x404080FF : 0x303030FF;
            uint32_t fg = active ? 0xFFFFFFFF : 0x909090FF;
            me_fill_rect(r, tx, cy, tab_w - 2, 7 * s + 4, bg);
            me_draw_rect(r, tx, cy, tab_w - 2, 7 * s + 4, 0x606060FF);
            me_draw_text(r, tx + 4, cy + 2, tab_labels[t], fg, s);
        }
        cy += tab_h;
    }

    /* 8x8 pixel grid (centered in panel) */
    int grid_x = px + (panel_w - grid_w) / 2;
    int grid_y = cy;

    for (int row = 0; row < MASK_SIZE; row++) {
        for (int col = 0; col < MASK_SIZE; col++) {
            bool solid = me->pixels[row][col] != 0;
            uint32_t color = solid ? 0xFFFFFFFF : 0x202020FF;
            me_fill_rect(r, grid_x + col * cell, grid_y + row * cell,
                        cell, cell, color);
        }
    }

    /* Grid lines */
    for (int i = 0; i <= MASK_SIZE; i++) {
        me_set_color(r, 0x606060FF);
        SDL_RenderDrawLine(r, grid_x + i * cell, grid_y,
                          grid_x + i * cell, grid_y + grid_h);
        SDL_RenderDrawLine(r, grid_x, grid_y + i * cell,
                          grid_x + grid_w, grid_y + i * cell);
    }
    me_draw_rect(r, grid_x, grid_y, grid_w, grid_h, 0xA0A0A0FF);

    cy = grid_y + grid_h + pad;

    /* Tool buttons */
    static const char *tools[] = { "Fill", "Clear", "MirH", "MirV", "Rot90" };
    int btn_w = (panel_w - pad * 2) / 5;
    for (int i = 0; i < 5; i++) {
        int bx = px + pad + i * btn_w;
        me_fill_rect(r, bx, cy, btn_w - 2, toolbar_h - 2, 0x383838FF);
        me_draw_rect(r, bx, cy, btn_w - 2, toolbar_h - 2, 0x606060FF);
        int tw = me_text_width(tools[i], s);
        me_draw_text(r, bx + (btn_w - 2 - tw) / 2, cy + 4, tools[i], 0xC0C0C0FF, s);
    }
    cy += toolbar_h + 2;

    /* Bottom row: Undo/Redo + Close */
    static const char *actions[] = { "Undo", "Redo", "Close" };
    int act_w = (panel_w - pad * 2) / 3;
    for (int i = 0; i < 3; i++) {
        int bx = px + pad + i * act_w;
        uint32_t bg = (i == 2) ? 0x804040FF : 0x383838FF;
        me_fill_rect(r, bx, cy, act_w - 2, toolbar_h - 2, bg);
        me_draw_rect(r, bx, cy, act_w - 2, toolbar_h - 2, 0x606060FF);
        int tw = me_text_width(actions[i], s);
        me_draw_text(r, bx + (act_w - 2 - tw) / 2, cy + 4, actions[i], 0xFFFFFFFF, s);
    }

    /* Hint text */
    cy += toolbar_h + 4;
    me_draw_text(r, px + pad, cy, "L:Solid R:Pass Drag:Paint",
                0x808080FF, s > 2 ? s - 1 : s);
}

/*=============================================================================
 * Rendering: Ball Test Point Overlay
 *===========================================================================*/

void mask_editor_render_test_points(MaskEditorState *me, GameState *state,
                                     EditorState *editor, void *sdl_renderer) {
    if (!me->show_test_points) return;
    if (!editor->show_collision_overlay) return;
    if (editor->hovered_coll_col < 0) return;

    SDL_Renderer *r = (SDL_Renderer *)sdl_renderer;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    /* World position of hovered tile center */
    float tile_x = (float)(editor->hovered_coll_col * 8 + 4);
    float tile_y = (float)(editor->hovered_coll_row * 8 + 4);

    /* Get the mask data for the hovered attribute */
    uint8_t mask_data[MASK_BYTES];
    uint8_t attr = editor->hovered_coll_attr;

    if (attr == 0) {
        /* Passable tile - show test points as all green */
        memset(mask_data, 0, MASK_BYTES);
    } else if (attr >= 0xE0) {
        uint8_t mask_index = (attr - 0xE0) & 0x0F;
        bool is_right = (attr >= 0xF0);
        mask_editor_get_flipper_mask(me, state, mask_index, is_right, 0, mask_data);
    } else {
        mask_editor_get_mask(me, state, attr, mask_data);
    }

    /* Draw 16 test points around the mouse cursor position.
     * Each dot is drawn at the ball test point offset from the tile center. */
    float dot_radius = 2.0f * editor->zoom;
    if (dot_radius < 1.0f) dot_radius = 1.0f;

    for (int i = 0; i < 16; i++) {
        int8_t dx = BallTestPointOffsets[i][0];
        int8_t dy = BallTestPointOffsets[i][1];

        /* World position of test point */
        float wx = tile_x + (float)dx;
        float wy = tile_y + (float)dy;

        /* Screen position */
        int sx = (int)((wx - editor->camera_x) * editor->zoom);
        int sy = (int)((wy - editor->camera_y) * editor->zoom);

        /* Check if test point hits solid in mask.
         * The mask pixel is at (dx+4, dy+4) relative to top-left of tile,
         * but we need to consider the ball might overlap adjacent tiles.
         * For the preview, we check within the 8x8 mask of the hovered tile. */
        int mx = dx + 4;  /* 4 = ball radius offset */
        int my = dy + 4;
        bool hits_solid = false;
        if (mx >= 0 && mx < MASK_SIZE && my >= 0 && my < MASK_SIZE) {
            hits_solid = (mask_data[my] & (0x80 >> mx)) != 0;
        }

        /* Red = hits solid, green = passable */
        uint32_t color = hits_solid ? 0xFF0000FF : 0x00FF00FF;

        int r_int = (int)dot_radius;
        if (r_int < 1) r_int = 1;
        me_fill_rect(r, sx - r_int, sy - r_int, r_int * 2 + 1, r_int * 2 + 1, color);
        me_draw_rect(r, sx - r_int - 1, sy - r_int - 1, r_int * 2 + 3, r_int * 2 + 3,
                    0x000000C0);
    }
}

/*=============================================================================
 * Input Handling
 *===========================================================================*/

bool mask_editor_handle_input(MaskEditorState *me, EditorState *editor,
                               int mouse_x, int mouse_y,
                               bool left_click, bool right_click,
                               bool left_down, bool right_down,
                               int win_w, int win_h) {
    if (!me->active) return false;

    int s = editor->ui_scale;
    int cell = MASK_CELL_SIZE;
    int grid_w = MASK_SIZE * cell;
    int grid_h = MASK_SIZE * cell;
    int pad = 8;
    int tab_h = me->is_flipper_mask ? (7 * s + 8) : 0;
    int toolbar_h = 7 * s + 8;

    /* Match render panel width calculation */
    int char_w = (4 + 1) * s;
    int panel_w = 5 * (5 * char_w + 16) + pad * 2 + 4 * s;
    int panel_h = tab_h + grid_h + toolbar_h * 2 + pad * 4 + 80;

    int px = (win_w - panel_w) / 2;
    int py = (win_h - panel_h) / 2;

    int cy = py + pad + 7 * s + 4; /* After title */
    if (me->is_flipper_mask) {
        /* Check tab clicks */
        if (left_click) {
            int tab_w = (panel_w - pad * 2) / 3;
            for (int t = 0; t < FLIPPER_ANGLE_SETS; t++) {
                int tx = px + pad + t * tab_w;
                if (mouse_x >= tx && mouse_x < tx + tab_w - 2 &&
                    mouse_y >= cy && mouse_y < cy + 7 * s + 4) {
                    /* Save current tab's pixels */
                    memcpy(me->flipper_pixels[me->flipper_angle_tab], me->pixels,
                           sizeof(me->pixels));
                    /* Switch tab */
                    me->flipper_angle_tab = t;
                    memcpy(me->pixels, me->flipper_pixels[t], sizeof(me->pixels));
                    return true;
                }
            }
        }
        cy += tab_h;
    }

    int grid_x = px + (panel_w - grid_w) / 2;
    int grid_y = cy;

    /* Check pixel grid clicks/drags */
    if ((left_down || right_down) &&
        mouse_x >= grid_x && mouse_x < grid_x + grid_w &&
        mouse_y >= grid_y && mouse_y < grid_y + grid_h) {

        int col = (mouse_x - grid_x) / cell;
        int row = (mouse_y - grid_y) / cell;
        if (col >= 0 && col < MASK_SIZE && row >= 0 && row < MASK_SIZE) {
            if (!me->painting) {
                /* Start of a new stroke - push undo */
                mask_editor_push_undo(me);
                me->painting = true;
                me->paint_value = left_down; /* left=solid, right=passable */
                me->undo_pos++;
            }
            me->pixels[row][col] = me->paint_value ? 1 : 0;
            /* Update undo top */
            pixels_to_mask_bytes(me->pixels, me->undo_stack[me->undo_pos]);
            me->undo_count = me->undo_pos + 1;
        }
        return true;
    }

    /* End painting when buttons released */
    if (!left_down && !right_down) {
        me->painting = false;
    }

    cy = grid_y + grid_h + pad;

    /* Tool buttons: Fill, Clear, MirH, MirV, Rot90 */
    if (left_click) {
        int btn_w = (panel_w - pad * 2) / 5;
        for (int i = 0; i < 5; i++) {
            int bx = px + pad + i * btn_w;
            if (mouse_x >= bx && mouse_x < bx + btn_w - 2 &&
                mouse_y >= cy && mouse_y < cy + toolbar_h - 2) {
                switch (i) {
                    case 0: mask_editor_fill(me); break;
                    case 1: mask_editor_clear(me); break;
                    case 2: mask_editor_mirror_h(me); break;
                    case 3: mask_editor_mirror_v(me); break;
                    case 4: mask_editor_rotate_90(me); break;
                }
                return true;
            }
        }
    }
    cy += toolbar_h + 2;

    /* Action buttons: Undo, Redo, Close */
    if (left_click) {
        int act_w = (panel_w - pad * 2) / 3;
        for (int i = 0; i < 3; i++) {
            int bx = px + pad + i * act_w;
            if (mouse_x >= bx && mouse_x < bx + act_w - 2 &&
                mouse_y >= cy && mouse_y < cy + toolbar_h - 2) {
                switch (i) {
                    case 0: mask_editor_undo(me); break;
                    case 1: mask_editor_redo(me); break;
                    case 2: mask_editor_close(me); break;
                }
                return true;
            }
        }
    }

    /* Consume all input while panel is open (prevent click-through) */
    return true;
}

/*=============================================================================
 * Custom Mask Export: PNG Atlas
 *===========================================================================*/

bool mask_editor_export_atlas(MaskEditorState *me, GameState *state) {
    if (!me->any_modified && !me->any_flipper_modified) return true;

    /* Atlas layout: 16x16 grid of 8x8 tiles = 128x128 pixels
     * 256 masks total, attribute index = row * 16 + col */
    int atlas_w = 16 * MASK_SIZE;  /* 128 */
    int atlas_h = 16 * MASK_SIZE;  /* 128 */
    uint8_t *pixels = calloc((size_t)atlas_w * atlas_h, 1);
    if (!pixels) return false;

    /* Fill atlas with all 256 masks */
    for (int attr = 0; attr < MAX_MASKS; attr++) {
        uint8_t mask_data[MASK_BYTES];
        mask_editor_get_mask(me, state, (uint8_t)attr, mask_data);

        int tile_row = attr / 16;
        int tile_col = attr % 16;

        for (int row = 0; row < MASK_SIZE; row++) {
            for (int col = 0; col < MASK_SIZE; col++) {
                int px = tile_col * MASK_SIZE + col;
                int py = tile_row * MASK_SIZE + row;
                bool solid = (mask_data[row] & (0x80 >> col)) != 0;
                pixels[py * atlas_w + px] = solid ? 0 : 255; /* 0=dark=solid, 255=light=passable */
            }
        }
    }

    /* Write PNG */
    char path[260];
    snprintf(path, sizeof(path), "%s/data/custom_masks.png", state->asset_base_path);
    int result = stbi_write_png(path, atlas_w, atlas_h, 1, pixels, atlas_w);
    free(pixels);

    if (result) {
        fprintf(stderr, "mask_editor: exported atlas to '%s'\n", path);
    } else {
        fprintf(stderr, "mask_editor: failed to export atlas to '%s'\n", path);
    }
    return result != 0;
}

/*=============================================================================
 * Custom Mask Export: Lua Snippet
 *===========================================================================*/

bool mask_editor_export_lua_snippet(MaskEditorState *me, const char *table_folder) {
    if (!me->any_modified && !me->any_flipper_modified) return true;

    char path[260];
    snprintf(path, sizeof(path), "%s/scripts/custom_masks.lua", table_folder);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "mask_editor: failed to write Lua snippet to '%s'\n", path);
        return false;
    }

    fprintf(f, "-- Custom collision masks generated by the editor.\n");
    fprintf(f, "-- Place this in your table's scripts/ folder.\n");
    fprintf(f, "-- The on_stage_init hook will apply the custom mask path.\n\n");
    fprintf(f, "function on_stage_init()\n");
    fprintf(f, "    pinball.set_collision_mask_path(\"data/custom_masks.png\")\n");
    fprintf(f, "end\n");

    fclose(f);
    fprintf(stderr, "mask_editor: wrote Lua snippet to '%s'\n", path);
    return true;
}
