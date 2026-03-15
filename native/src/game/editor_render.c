/*
 * Stage Builder - Rendering Implementation
 *
 * Renders the editor viewport directly to the SDL framebuffer,
 * bypassing the GBC tile/sprite renderer.
 *
 * UI text is scaled by UI_SCALE for readability on modern displays.
 * Tilemap uses correct signed/unsigned addressing (matching game renderer).
 */

#include "game/editor_render.h"
#include "game/editor.h"
#include "game/editor_mask.h"
#include "game/game_state.h"
#include "game/config_data.h"
#include "renderer/renderer.h"
#include "renderer/vram.h"
#include "game/constants.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/*=============================================================================
 * Colors (RGBA8888)
 *===========================================================================*/
#define COL_BG_DARK       0x303030FF
#define COL_GRID          0x505050FF
#define COL_GRID_MAJOR    0x707070FF
#define COL_SELECT_BOX    0xFFFF00FF
#define COL_OBJECT_BOX    0x00FF80FF
#define COL_PLACE_GHOST   0xFFFFFF60
#define COL_BUMPER        0xFF4040FF
#define COL_SPINNER       0x40FF40FF
#define COL_TRIGGER       0x4040FFFF
#define COL_SAVER         0xFFFF40FF
#define COL_SLOT          0xFF8040FF
#define COL_CREATURE      0xFF40FFFF
#define COL_TEXT_WHITE    0xFFFFFFFF
#define COL_TEXT_SHADOW   0x000000FF
#define COL_TEXT_DIM      0x909090FF
#define COL_SIDEBAR_BG    0x202020E0
#define COL_SIDEBAR_ITEM  0x383838FF
#define COL_SIDEBAR_SEL   0x505050FF
#define COL_PICKER_BG     0x1A1A1AFF
#define COL_PICKER_ITEM   0x2A2A2AFF
#define COL_PICKER_SEL    0x404060FF
#define COL_PICKER_TITLE  0x80C0FFFF
#define COL_COLL_OUTLINE  0x000000C0
#define COL_COLL_HOVER    0xFFFFFFFF
#define COL_COLL_SELECT   0xFFFF00FF
#define COL_VIEWPORT      0xFF00FFB0   /* Magenta viewport rectangle */
#define COL_STAGE_SEP     0xFF8000FF   /* Orange stage separator line */

/* Color per component type */
static const uint32_t comp_colors[] = {
    0x808080FF, /* NONE */
    COL_BUMPER,    COL_SPINNER,  COL_TRIGGER, COL_SAVER,
    COL_TRIGGER,   COL_CREATURE, COL_CREATURE, COL_TRIGGER,
    COL_SLOT,      COL_TRIGGER,  COL_SPINNER,  COL_CREATURE,
    COL_TRIGGER,   COL_SLOT,
};

/*=============================================================================
 * 4x6 Bitmap Font (95 ASCII glyphs)
 *===========================================================================*/

static const uint8_t editor_font_4x6[95][6] = {
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
    {0x60,0x80,0xE0,0xA0,0x40,0x00}, /* 6 */
    {0xE0,0x20,0x40,0x40,0x40,0x00}, /* 7 */
    {0x40,0xA0,0x40,0xA0,0x40,0x00}, /* 8 */
    {0x40,0xA0,0xE0,0x20,0xC0,0x00}, /* 9 */
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
    {0xA0,0xE0,0xE0,0xA0,0xA0,0x00}, /* M */
    {0xA0,0xE0,0xE0,0xA0,0xA0,0x00}, /* N */
    {0x40,0xA0,0xA0,0xA0,0x40,0x00}, /* O */
    {0xC0,0xA0,0xC0,0x80,0x80,0x00}, /* P */
    {0x40,0xA0,0xA0,0xE0,0x60,0x00}, /* Q */
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

/*=============================================================================
 * Drawing Helpers (directly to SDL renderer, with scale support)
 *===========================================================================*/

static void set_draw_color(SDL_Renderer *r, uint32_t rgba) {
    SDL_SetRenderDrawColor(r,
        (rgba >> 24) & 0xFF,
        (rgba >> 16) & 0xFF,
        (rgba >> 8) & 0xFF,
        rgba & 0xFF);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t color) {
    set_draw_color(r, color);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect_outline(SDL_Renderer *r, int x, int y, int w, int h, uint32_t color) {
    set_draw_color(r, color);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(r, &rect);
}

static void draw_line(SDL_Renderer *r, int x1, int y1, int x2, int y2, uint32_t color) {
    set_draw_color(r, color);
    SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

/* Draw a single character at (px, py), scaled by s */
static void draw_char_scaled(SDL_Renderer *r, int px, int py, char ch, uint32_t color, int s) {
    if (ch < 0x20 || ch > 0x7E) return;
    const uint8_t *glyph = editor_font_4x6[ch - 0x20];
    set_draw_color(r, color);
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

/* Draw a string with shadow, scaled by s */
static void draw_text_s(SDL_Renderer *r, int px, int py, const char *text, uint32_t color, int s) {
    int char_w = (4 + 1) * s;  /* 4px glyph + 1px gap, scaled */
    for (int i = 0; text[i]; i++) {
        draw_char_scaled(r, px + s, py + s, text[i], COL_TEXT_SHADOW, s);
        draw_char_scaled(r, px, py, text[i], color, s);
        px += char_w;
    }
}

/* Measure text width at scale s */
static int text_width_s(const char *text, int s) {
    int len = 0;
    while (text[len]) len++;
    return len * (4 + 1) * s;
}

/*=============================================================================
 * Import Tilemap from VRAM
 *===========================================================================*/

void editor_import_tilemap(EditorState *editor, VirtualVRAM *vram) {
    if (!editor || !vram) return;

    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int idx = row * 32 + col;
            editor->table.tilemap[row][col] = vram->bg_map[0][idx];
            editor->table.tilemap_attrs[row][col] = vram->bg_map[1][idx];
        }
    }
}

/*=============================================================================
 * World-to-Screen Coordinate Conversion
 *===========================================================================*/

static int world_to_screen_x(EditorState *e, float wx) {
    return (int)((wx - e->camera_x) * e->zoom);
}

static int world_to_screen_y(EditorState *e, float wy) {
    return (int)((wy - e->camera_y) * e->zoom);
}

/*=============================================================================
 * Render: Tilemap Preview (with correct addressing mode)
 *===========================================================================*/

static void render_tilemap_preview(EditorState *editor, SDL_Renderer *sdl_r,
                                    VirtualVRAM *vram, GBCPalette *bg_palettes) {
    if (!editor->table.has_vram) return;

    float tile_screen = 8.0f * editor->zoom;
    if (tile_screen < 1.0f) return;

    int win_w, win_h;
    SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);

    int disp_rows = editor_display_rows(editor);
    int total_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;

    int start_col = (int)(editor->camera_x / 8.0f);
    int start_row = (int)(editor->camera_y / 8.0f);
    int end_col = start_col + (int)(win_w / tile_screen) + 2;
    int end_row = start_row + (int)(win_h / tile_screen) + 2;

    if (start_col < 0) start_col = 0;
    if (start_row < 0) start_row = 0;
    if (end_col > total_cols) end_col = total_cols;
    if (end_row > disp_rows) end_row = disp_rows;

    bool use_unsigned = editor->table.unsigned_addressing;

    for (int row = start_row; row < end_row; row++) {
        int data_row = editor_display_to_data_row(editor, row);
        for (int col = start_col; col < end_col; col++) {
            uint8_t tile_num = editor->table.tilemap[data_row][col];
            uint8_t attr = editor->table.tilemap_attrs[data_row][col];

            int pal_num = attr & 0x07;
            int tile_bank = (attr >> 3) & 1;
            bool x_flip = (attr >> 5) & 1;
            bool y_flip = (attr >> 6) & 1;

            /* Compute tile data offset using correct addressing mode */
            int tile_offset;
            if (use_unsigned) {
                tile_offset = (int)tile_num * 16;
            } else {
                /* Signed addressing: tile_num treated as int8_t, base at 0x1000 */
                tile_offset = (int)(int8_t)tile_num * 16 + 0x1000;
            }

            if (tile_offset < 0 || tile_offset + 16 > VRAM_TILE_DATA_SIZE) continue;

            /* Select tile data source: combined view uses snapshots, single uses vram_ref */
            const uint8_t *tile_data;
            GBCPalette *row_palettes;
            if (editor->combined_view) {
                if (data_row < 32) {
                    tile_data = &editor->vram_top[tile_bank][tile_offset];
                    row_palettes = editor->palettes_top;
                } else {
                    tile_data = &editor->vram_bottom[tile_bank][tile_offset];
                    row_palettes = editor->palettes_bottom;
                }
            } else {
                if (!vram) continue;
                tile_data = &vram->tile_data[tile_bank][tile_offset];
                row_palettes = bg_palettes;
            }

            /* Screen position */
            int sx = world_to_screen_x(editor, (float)(col * 8));
            int sy = world_to_screen_y(editor, (float)(row * 8));

            /* If very zoomed out, draw averaged color */
            if (tile_screen < 4.0f) {
                uint8_t lo = tile_data[0];
                uint8_t hi = tile_data[1];
                uint8_t ci = ((lo >> 7) & 1) | (((hi >> 7) & 1) << 1);
                uint16_t c = row_palettes[pal_num].colors[ci];
                uint32_t rgba = rgb555_to_rgba(c);
                fill_rect(sdl_r, sx, sy, (int)tile_screen + 1, (int)tile_screen + 1, rgba);
                continue;
            }

            /* Draw each pixel of the tile (matching game renderer logic) */
            float px_size = editor->zoom;
            if (px_size < 1.0f) px_size = 1.0f;

            for (int ty = 0; ty < 8; ty++) {
                int src_row_idx = y_flip ? (7 - ty) : ty;
                uint8_t lo_byte = tile_data[src_row_idx * 2];
                uint8_t hi_byte = tile_data[src_row_idx * 2 + 1];

                for (int tx = 0; tx < 8; tx++) {
                    int bit = x_flip ? tx : (7 - tx);
                    uint8_t color_idx = ((lo_byte >> bit) & 1) |
                                       (((hi_byte >> bit) & 1) << 1);

                    uint16_t c = row_palettes[pal_num].colors[color_idx];
                    uint32_t rgba = rgb555_to_rgba(c);

                    int px = sx + (int)(tx * editor->zoom);
                    int py = sy + (int)(ty * editor->zoom);
                    int pw = (int)px_size + 1;
                    int ph = (int)px_size + 1;
                    fill_rect(sdl_r, px, py, pw, ph, rgba);
                }
            }
        }
    }
}

/*=============================================================================
 * Render: Grid Overlay
 *===========================================================================*/

static void render_grid(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->show_grid) return;

    int win_w, win_h;
    SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);

    float tile_screen = 8.0f * editor->zoom;
    if (tile_screen < 4.0f) return;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    int grid = editor->grid_size;
    float grid_screen = (float)grid * editor->zoom;

    /* Vertical lines */
    float start_x = fmodf(-(editor->camera_x * editor->zoom), grid_screen);
    if (start_x < 0) start_x += grid_screen;
    for (float x = start_x; x < win_w; x += grid_screen) {
        int ix = (int)x;
        int world_col = (int)((ix / editor->zoom + editor->camera_x) / grid + 0.5f);
        uint32_t col = (world_col % 4 == 0) ? COL_GRID_MAJOR : COL_GRID;
        draw_line(sdl_r, ix, 0, ix, win_h, col);
    }

    /* Horizontal lines */
    float start_y = fmodf(-(editor->camera_y * editor->zoom), grid_screen);
    if (start_y < 0) start_y += grid_screen;
    for (float y = start_y; y < win_h; y += grid_screen) {
        int iy = (int)y;
        int world_row = (int)((iy / editor->zoom + editor->camera_y) / grid + 0.5f);
        uint32_t col = (world_row % 4 == 0) ? COL_GRID_MAJOR : COL_GRID;
        draw_line(sdl_r, 0, iy, win_w, iy, col);
    }
}

/*=============================================================================
 * Render: Object Bounding Boxes
 *===========================================================================*/

static void render_objects(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->show_object_bounds) return;

    int s = editor->ui_scale;
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < editor->table.num_objects; i++) {
        EditorObject *obj = &editor->table.objects[i];

        int cx = world_to_screen_x(editor, (float)obj->x);
        int cy = world_to_screen_y(editor, (float)obj->y);
        int hw = (int)(obj->x_thresh * editor->zoom);
        int hh = (int)(obj->y_thresh * editor->zoom);

        uint32_t color = (obj->type < COMP_COUNT) ? comp_colors[obj->type] : COL_OBJECT_BOX;
        bool selected = (i == editor->selected_object);

        /* Fill (semi-transparent) */
        uint32_t fill_color = (color & 0xFFFFFF00) | 0x30;
        fill_rect(sdl_r, cx - hw, cy - hh, hw * 2, hh * 2, fill_color);

        /* Outline */
        if (selected) {
            draw_rect_outline(sdl_r, cx - hw - 1, cy - hh - 1, hw * 2 + 2, hh * 2 + 2, COL_SELECT_BOX);
            draw_rect_outline(sdl_r, cx - hw, cy - hh, hw * 2, hh * 2, COL_SELECT_BOX);
        } else {
            draw_rect_outline(sdl_r, cx - hw, cy - hh, hw * 2, hh * 2, color);
        }

        /* Label */
        const char *name = editor_component_name(obj->type);
        int text_x = cx - text_width_s(name, s) / 2;
        int text_y = cy - hh - 8 * s;
        draw_text_s(sdl_r, text_x, text_y, name, COL_TEXT_WHITE, s);
    }
}

/*=============================================================================
 * Render: Placement Ghost
 *===========================================================================*/

static void render_placement_ghost(EditorState *editor, SDL_Renderer *sdl_r) {
    if (editor->current_tool != TOOL_PLACE || editor->palette_selection == COMP_NONE)
        return;

    int s = editor->ui_scale;
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    float world_x = editor->camera_x + (float)editor->mouse_x / editor->zoom;
    float world_y = editor->camera_y + (float)editor->mouse_y / editor->zoom;

    if (editor->snap_to_grid) {
        world_x = (float)(((int)world_x / editor->grid_size) * editor->grid_size);
        world_y = (float)(((int)world_y / editor->grid_size) * editor->grid_size);
    }

    uint8_t xt, yt;
    editor_component_default_bbox(editor->palette_selection, &xt, &yt);

    int cx = world_to_screen_x(editor, world_x);
    int cy = world_to_screen_y(editor, world_y);
    int hw = (int)(xt * editor->zoom);
    int hh = (int)(yt * editor->zoom);

    uint32_t color = (editor->palette_selection < COMP_COUNT) ?
        comp_colors[editor->palette_selection] : COL_PLACE_GHOST;
    uint32_t ghost_fill = (color & 0xFFFFFF00) | 0x40;

    fill_rect(sdl_r, cx - hw, cy - hh, hw * 2, hh * 2, ghost_fill);
    draw_rect_outline(sdl_r, cx - hw, cy - hh, hw * 2, hh * 2, color);

    const char *name = editor_component_name(editor->palette_selection);
    draw_text_s(sdl_r, cx - text_width_s(name, s) / 2, cy - hh - 8 * s, name, COL_TEXT_WHITE, s);
}

/*=============================================================================
 * Render: Collision Overlay
 *===========================================================================*/

/* HSV to RGBA8888 conversion (inline helper).
 * h in [0,1], s in [0,1], v in [0,1], alpha 0-255. */
static uint32_t hsv_to_rgba(float h, float s, float v, uint8_t alpha) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    int hi = (int)(h * 6.0f) % 6;
    switch (hi) {
        case 0: r=c; g=x; b=0; break;
        case 1: r=x; g=c; b=0; break;
        case 2: r=0; g=c; b=x; break;
        case 3: r=0; g=x; b=c; break;
        case 4: r=x; g=0; b=c; break;
        default: r=c; g=0; b=x; break;
    }
    return ((uint8_t)((r+m)*255) << 24) | ((uint8_t)((g+m)*255) << 16) |
           ((uint8_t)((b+m)*255) << 8) | alpha;
}

static uint32_t collision_attr_color(uint8_t attr) {
    /* Unique color per collision attribute ID, matching build_table.py golden-angle distribution */
    if (attr == 0)   return 0;                    /* Passable: transparent */
    if (attr == 1)   return 0x000000B0;           /* Solid wall: black */
    if (attr == 255) return 0x303030B0;           /* Border: dark gray */
    if (attr >= 208) {                            /* Animated (0xD0-0xFE): orange gradient */
        float t = (float)(attr - 208) / 47.0f;
        uint8_t g = (uint8_t)(80 + 175 * t);
        return (0xFF << 24) | (g << 16) | (0x00 << 8) | 0xB0;
    }
    /* Static masks (2-207): golden-angle hue distribution */
    float hue = fmodf(attr * 137.508f, 360.0f) / 360.0f;
    return hsv_to_rgba(hue, 0.7f, 0.9f, 0xB0);  /* 69% alpha */
}

/* Get a short type description for a collision attribute */
static const char *collision_attr_type_name(uint8_t attr) {
    if (attr == 0)   return "passable";
    if (attr == 1)   return "solid";
    if (attr == 255) return "border";
    if (attr >= 0xE0) return "flipper";
    if (attr >= 0xD0) return "animated";
    if (attr >= 0x80) return "wall";
    if (attr >= 0x40) return "trigger";
    return "static";
}

static void render_collision_overlay(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->show_collision_overlay) return;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    int win_w, win_h;
    SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);

    int disp_rows = editor_display_rows(editor);
    int total_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;

    /* Culling based on camera */
    int start_col = (int)(editor->camera_x / 8.0f);
    int start_row = (int)(editor->camera_y / 8.0f);
    float tile_screen = 8.0f * editor->zoom;
    int end_col = start_col + (int)(win_w / tile_screen) + 2;
    int end_row = start_row + (int)(win_h / tile_screen) + 2;
    if (start_col < 0) start_col = 0;
    if (start_row < 0) start_row = 0;
    if (end_col > total_cols) end_col = total_cols;
    if (end_row > disp_rows) end_row = disp_rows;

    int s = editor->ui_scale;

    for (int row = start_row; row < end_row; row++) {
        int data_row = editor_display_to_data_row(editor, row);
        for (int col = start_col; col < end_col; col++) {
            uint8_t coll = editor->table.collision_map[data_row][col];
            if (coll == 0) continue;

            int sx = world_to_screen_x(editor, (float)(col * 8));
            int sy = world_to_screen_y(editor, (float)(row * 8));
            int size = (int)(8.0f * editor->zoom);
            if (size < 1) size = 1;

            uint32_t color = collision_attr_color(coll);

            /* Fill with semi-transparent color */
            fill_rect(sdl_r, sx, sy, size, size, color);

            /* Dark outline for contrast against any background */
            if (size >= 4) {
                draw_rect_outline(sdl_r, sx, sy, size, size, COL_COLL_OUTLINE);
            }

            /* When zoomed in enough, draw hex attribute ID inside the tile */
            if (size >= 16) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X", coll);
                int tx = sx + (size - text_width_s(hex, 1)) / 2;
                int ty = sy + (size - 6) / 2;
                draw_text_s(sdl_r, tx, ty, hex, COL_TEXT_WHITE, 1);
            }
        }
    }

    /* Highlight hovered collision tile */
    if (editor->hovered_coll_col >= 0) {
        int sx = world_to_screen_x(editor, (float)(editor->hovered_coll_col * 8));
        int sy = world_to_screen_y(editor, (float)(editor->hovered_coll_row * 8));
        int size = (int)(8.0f * editor->zoom);
        if (size < 1) size = 1;
        draw_rect_outline(sdl_r, sx - 1, sy - 1, size + 2, size + 2, COL_COLL_HOVER);
        draw_rect_outline(sdl_r, sx, sy, size, size, COL_COLL_HOVER);
    }

    /* Highlight selected collision tile */
    if (editor->selected_coll_col >= 0) {
        int sx = world_to_screen_x(editor, (float)(editor->selected_coll_col * 8));
        int sy = world_to_screen_y(editor, (float)(editor->selected_coll_row * 8));
        int size = (int)(8.0f * editor->zoom);
        if (size < 1) size = 1;
        draw_rect_outline(sdl_r, sx - 1, sy - 1, size + 2, size + 2, COL_COLL_SELECT);
        draw_rect_outline(sdl_r, sx, sy, size, size, COL_COLL_SELECT);
    }
}

/*=============================================================================
 * Render: Flipper Sweep Visualizer
 *
 * When toggled with F key, shows flipper physics data on the viewport:
 * - Collision detection rectangle around flipper area
 * - Pivot point crosshairs for left and right flippers
 * - Animated angle-set coloring on flipper tiles (red/yellow/green)
 * - Mask geometry preview cycling through all 16 angles
 * - Current angle indicator text
 *===========================================================================*/

/* Colors for the 3 flipper angle sets */
#define COL_FLIP_SET0  0xFF4040A0   /* Red: angles 0-6 */
#define COL_FLIP_SET1  0xFFFF40A0   /* Yellow: angles 7-13 */
#define COL_FLIP_SET2  0x40FF40A0   /* Green: angles 14+ */
#define COL_FLIP_RECT  0x00FFFFA0   /* Cyan: collision detection rect */
#define COL_FLIP_PIVOT 0xFF80FFFF   /* Pink: pivot crosshairs */

static void render_flipper_sweep(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->show_flipper_sweep) return;
    if (!editor->show_collision_overlay) return;
    if (!editor->table.has_flippers) return;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    int s = editor->ui_scale;
    int angle = editor->flipper_anim_angle;
    int angle_set = (angle <= 6) ? 0 : (angle <= 13) ? 1 : 2;
    uint32_t set_color = (angle_set == 0) ? COL_FLIP_SET0 :
                         (angle_set == 1) ? COL_FLIP_SET1 : COL_FLIP_SET2;

    /* Get flipper collision bounds from config or defaults */
    int x_min = 43, x_range = 48, y_min = 123, y_range = 32;
    if (editor->game_state_ref && editor->game_state_ref->config) {
        PhysicsConfig *phys = &editor->game_state_ref->config->physics;
        x_min = phys->flipper_collision_x_min;
        x_range = phys->flipper_collision_x_range;
        y_min = phys->flipper_collision_y_min;
        y_range = phys->flipper_collision_y_range;
    }

    /* Flipper pivot points in world coordinates (bottom stage) */
    int left_pivot_x = 56, left_pivot_y = 123;
    int right_pivot_x = 104, right_pivot_y = 123;

    /* Y offset for combined view: bottom stage starts at display row 18 */
    int bottom_y_offset = 0;
    if (editor->combined_view && editor->hide_buffer_rows) {
        bottom_y_offset = 18 * 8;  /* 144 pixels */
    } else if (editor->combined_view) {
        bottom_y_offset = 32 * 8;  /* Full buffer rows included */
    }

    /* --- A) Collision Detection Rectangle --- */
    {
        int rect_x = x_min;
        int rect_y = y_min + bottom_y_offset;
        int rect_w = x_range;
        int rect_h = y_range;

        int sx = world_to_screen_x(editor, (float)rect_x);
        int sy = world_to_screen_y(editor, (float)rect_y);
        int sw = (int)((float)rect_w * editor->zoom);
        int sh = (int)((float)rect_h * editor->zoom);

        /* Left flipper detection zone */
        fill_rect(sdl_r, sx, sy, sw, sh, 0x00FFFF20);
        draw_rect_outline(sdl_r, sx, sy, sw, sh, COL_FLIP_RECT);
        draw_rect_outline(sdl_r, sx + 1, sy + 1, sw - 2, sh - 2, COL_FLIP_RECT);

        /* Right flipper detection zone (mirrored: 160 - x_min - x_range to 160 - x_min) */
        int right_x = 160 - x_min - x_range;
        sx = world_to_screen_x(editor, (float)right_x);
        fill_rect(sdl_r, sx, sy, sw, sh, 0x00FFFF20);
        draw_rect_outline(sdl_r, sx, sy, sw, sh, COL_FLIP_RECT);
        draw_rect_outline(sdl_r, sx + 1, sy + 1, sw - 2, sh - 2, COL_FLIP_RECT);

        /* Labels */
        if (sw > 30) {
            draw_text_s(sdl_r, sx + 2, sy + 2, "R", COL_FLIP_RECT, 1);
            int lsx = world_to_screen_x(editor, (float)x_min);
            draw_text_s(sdl_r, lsx + 2, sy + 2, "L", COL_FLIP_RECT, 1);
        }
    }

    /* --- B) Pivot Point Crosshairs --- */
    {
        int pivots[2][2] = {
            { left_pivot_x, left_pivot_y + bottom_y_offset },
            { right_pivot_x, right_pivot_y + bottom_y_offset }
        };
        int cross_len = (int)(4.0f * editor->zoom);
        if (cross_len < 3) cross_len = 3;

        for (int i = 0; i < 2; i++) {
            int cx = world_to_screen_x(editor, (float)pivots[i][0]);
            int cy = world_to_screen_y(editor, (float)pivots[i][1]);

            /* Crosshair */
            draw_line(sdl_r, cx - cross_len, cy, cx + cross_len, cy, COL_FLIP_PIVOT);
            draw_line(sdl_r, cx, cy - cross_len, cx, cy + cross_len, COL_FLIP_PIVOT);

            /* Small filled center */
            fill_rect(sdl_r, cx - 1, cy - 1, 3, 3, COL_FLIP_PIVOT);
        }
    }

    /* --- C) Angle-Colored Flipper Tiles + D) Animated Mask Preview --- */
    {
        int win_w, win_h;
        SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);

        int disp_rows = editor_display_rows(editor);
        int total_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;
        float tile_screen = 8.0f * editor->zoom;

        /* Culling */
        int start_col = (int)(editor->camera_x / 8.0f);
        int start_row = (int)(editor->camera_y / 8.0f);
        int end_col = start_col + (int)(win_w / tile_screen) + 2;
        int end_row = start_row + (int)(win_h / tile_screen) + 2;
        if (start_col < 0) start_col = 0;
        if (start_row < 0) start_row = 0;
        if (end_col > total_cols) end_col = total_cols;
        if (end_row > disp_rows) end_row = disp_rows;

        for (int row = start_row; row < end_row; row++) {
            int data_row = editor_display_to_data_row(editor, row);
            for (int col = start_col; col < end_col; col++) {
                uint8_t attr = editor->table.collision_map[data_row][col];
                if (attr < 0xE0) continue;  /* Only flipper tiles */

                int sx = world_to_screen_x(editor, (float)(col * 8));
                int sy = world_to_screen_y(editor, (float)(row * 8));
                int size = (int)(8.0f * editor->zoom);
                if (size < 1) size = 1;

                /* Color band overlay based on current animation angle set */
                fill_rect(sdl_r, sx, sy, size, size, set_color);

                /* Mask geometry preview when zoomed in enough */
                if (size >= 8 && editor->mask_editor && editor->game_state_ref) {
                    uint8_t mask_index = (attr - 0xE0) & 0x0F;
                    bool is_right = (attr >= 0xF0);
                    uint8_t mask_data[MASK_BYTES];
                    mask_editor_get_flipper_mask(editor->mask_editor,
                        editor->game_state_ref, mask_index, is_right,
                        angle_set, mask_data);

                    /* Render mask pixels as solid black for contrast */
                    float px_size = (float)size / 8.0f;
                    uint32_t solid_color = 0x000000E0;
                    for (int my = 0; my < 8; my++) {
                        uint8_t row_bits = mask_data[my];
                        for (int mx = 0; mx < 8; mx++) {
                            if (row_bits & (0x80 >> mx)) {
                                int px = sx + (int)(mx * px_size);
                                int py = sy + (int)(my * px_size);
                                int pw = (int)((mx + 1) * px_size) - (int)(mx * px_size);
                                int ph = (int)((my + 1) * px_size) - (int)(my * px_size);
                                if (pw < 1) pw = 1;
                                if (ph < 1) ph = 1;
                                fill_rect(sdl_r, px, py, pw, ph, solid_color);
                            }
                        }
                    }
                }

                /* Outline */
                draw_rect_outline(sdl_r, sx, sy, size, size, (set_color | 0xFF));
            }
        }
    }

    /* --- E) Angle Indicator Text --- */
    {
        const char *set_name = (angle_set == 0) ? "0-6" :
                               (angle_set == 1) ? "7-13" : "14+";
        char buf[40];
        snprintf(buf, sizeof(buf), "Angle: %d/15 (Set %s)", angle, set_name);

        /* Position near left flipper pivot */
        int label_y = y_min + y_range + 4 + bottom_y_offset;
        int lx = world_to_screen_x(editor, (float)x_min);
        int ly = world_to_screen_y(editor, (float)label_y);

        /* Background for readability */
        int tw = text_width_s(buf, s);
        fill_rect(sdl_r, lx - 2, ly - 1, tw + 4, 7 * s + 2, 0x000000C0);
        draw_text_s(sdl_r, lx, ly, buf, set_color | 0xFF, s);
    }
}

/*=============================================================================
 * Render: Palette Highlight Overlay
 *
 * When TOOL_PALETTE is active, highlights all tiles in the viewport that
 * reference the currently selected palette index. This shows the user
 * exactly which tiles are affected by palette changes.
 *===========================================================================*/

static void render_palette_highlight(EditorState *editor, SDL_Renderer *sdl_r) {
    if (editor->current_tool != TOOL_PALETTE) return;
    if (!editor->table.has_vram) return;

    int win_w, win_h;
    SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);

    int disp_rows = editor_display_rows(editor);
    int total_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;
    float tile_screen = 8.0f * editor->zoom;
    if (tile_screen < 2.0f) return;

    int start_col = (int)(editor->camera_x / 8.0f);
    int start_row = (int)(editor->camera_y / 8.0f);
    int end_col = start_col + (int)(win_w / tile_screen) + 2;
    int end_row = start_row + (int)(win_h / tile_screen) + 2;
    if (start_col < 0) start_col = 0;
    if (start_row < 0) start_row = 0;
    if (end_col > total_cols) end_col = total_cols;
    if (end_row > disp_rows) end_row = disp_rows;

    int sel_pal = editor->pal_selected_palette;
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    for (int row = start_row; row < end_row; row++) {
        int data_row = editor_display_to_data_row(editor, row);
        for (int col = start_col; col < end_col; col++) {
            uint8_t attr = editor->table.tilemap_attrs[data_row][col];
            int tile_pal = attr & 0x07;
            if (tile_pal != sel_pal) continue;

            int sx = world_to_screen_x(editor, (float)(col * 8));
            int sy = world_to_screen_y(editor, (float)(row * 8));
            int size = (int)tile_screen;
            if (size < 1) size = 1;

            /* Green outline only — no fill so tile colors stay true */
            draw_rect_outline(sdl_r, sx, sy, size, size, 0x00FF00C0);
        }
    }
}

/*=============================================================================
 * Render: Viewport Rectangles & Stage Separator
 *
 * Shows the in-game visible area (160x144) for each stage, matching the
 * Python build_table.py reference tool's magenta viewport indicator.
 * Also draws a separator line between top and bottom stages in combined view.
 *===========================================================================*/

static void render_viewport_indicators(EditorState *editor, SDL_Renderer *sdl_r) {
    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    int s = editor->ui_scale;

    if (editor->combined_view && editor->hide_buffer_rows) {
        /* Combined view with hidden buffer rows:
         * Top stage viewport: columns 0-19 (SCX=0), display rows 0-17
         * Bottom stage viewport: depends on SCX, display rows 18-35 */

        /* Top stage viewport (SCX=0, SCY=0) */
        int top_vp_x = world_to_screen_x(editor, 0.0f);         /* SCX=0 */
        int top_vp_y = world_to_screen_y(editor, 0.0f);         /* Row 0 */
        int top_vp_w = (int)(160.0f * editor->zoom);            /* 20 tiles wide */
        int top_vp_h = (int)(144.0f * editor->zoom);            /* 18 tiles tall */
        draw_rect_outline(sdl_r, top_vp_x, top_vp_y, top_vp_w, top_vp_h, COL_VIEWPORT);
        draw_rect_outline(sdl_r, top_vp_x - 1, top_vp_y - 1, top_vp_w + 2, top_vp_h + 2, COL_VIEWPORT);

        /* Bottom stage viewport (aligned to col 0, display rows 18-35) */
        float bottom_y = 18.0f * 8.0f;  /* Display row 18 */
        int bot_vp_x = world_to_screen_x(editor, 0.0f);
        int bot_vp_y = world_to_screen_y(editor, bottom_y);
        int bot_vp_w = (int)(160.0f * editor->zoom);
        int bot_vp_h = (int)(144.0f * editor->zoom);
        draw_rect_outline(sdl_r, bot_vp_x, bot_vp_y, bot_vp_w, bot_vp_h, COL_VIEWPORT);
        draw_rect_outline(sdl_r, bot_vp_x - 1, bot_vp_y - 1, bot_vp_w + 2, bot_vp_h + 2, COL_VIEWPORT);

        /* Stage separator line at the junction (between display rows 17 and 18) */
        float sep_y = 18.0f * 8.0f;
        int sy = world_to_screen_y(editor, sep_y);
        int total_w = editor->table.tilemap_cols * 8;
        int sx1 = world_to_screen_x(editor, 0.0f);
        int sx2 = world_to_screen_x(editor, (float)total_w);
        draw_line(sdl_r, sx1, sy, sx2, sy, COL_STAGE_SEP);
        draw_line(sdl_r, sx1, sy - 1, sx2, sy - 1, COL_STAGE_SEP);

        /* Stage labels near separator */
        draw_text_s(sdl_r, sx1 + 4, sy - 10 * s, "TOP", COL_STAGE_SEP, s);
        draw_text_s(sdl_r, sx1 + 4, sy + 2, "BOTTOM", COL_STAGE_SEP, s);

    } else if (!editor->combined_view) {
        /* Single stage view: one viewport */
        float scx = (float)editor->table.default_scx;
        int vp_x = world_to_screen_x(editor, scx);
        int vp_y = world_to_screen_y(editor, 0.0f);
        int vp_w = (int)(160.0f * editor->zoom);
        int vp_h = (int)(144.0f * editor->zoom);
        draw_rect_outline(sdl_r, vp_x, vp_y, vp_w, vp_h, COL_VIEWPORT);
        draw_rect_outline(sdl_r, vp_x - 1, vp_y - 1, vp_w + 2, vp_h + 2, COL_VIEWPORT);
    }
}

/*=============================================================================
 * Render: Mask Editor Active Tile Highlight
 * When the mask pixel editor is open, dramatically highlight every tile
 * on the viewport that uses the attribute currently being edited.
 *===========================================================================*/

static void render_mask_edit_highlight(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->mask_editor || !editor->mask_editor->active) return;
    if (!editor->show_collision_overlay) return;

    uint8_t target = editor->mask_editor->edit_attr;
    if (target == 0) return;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    int win_w, win_h;
    SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);

    int disp_rows = editor_display_rows(editor);
    int total_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;

    /* Culling */
    int start_col = (int)(editor->camera_x / 8.0f);
    int start_row = (int)(editor->camera_y / 8.0f);
    float tile_screen = 8.0f * editor->zoom;
    int end_col = start_col + (int)(win_w / tile_screen) + 2;
    int end_row = start_row + (int)(win_h / tile_screen) + 2;
    if (start_col < 0) start_col = 0;
    if (start_row < 0) start_row = 0;
    if (end_col > total_cols) end_col = total_cols;
    if (end_row > disp_rows) end_row = disp_rows;

    /* Pulsing animation: cycle brightness over time */
    static int pulse_timer = 0;
    pulse_timer++;
    /* Triangle wave: 0→255→0 over ~60 frames */
    int phase = pulse_timer % 60;
    int brightness = phase < 30 ? (phase * 255 / 30) : ((60 - phase) * 255 / 30);
    uint8_t alpha = (uint8_t)(120 + brightness * 135 / 255);  /* range 120-255 */
    uint8_t glow = (uint8_t)(80 + brightness * 175 / 255);    /* range 80-255 */

    for (int row = start_row; row < end_row; row++) {
        int data_row = editor_display_to_data_row(editor, row);
        for (int col = start_col; col < end_col; col++) {
            uint8_t coll = editor->table.collision_map[data_row][col];
            if (coll != target) continue;

            int sx = world_to_screen_x(editor, (float)(col * 8));
            int sy = world_to_screen_y(editor, (float)(row * 8));
            int size = (int)(8.0f * editor->zoom);
            if (size < 1) size = 1;

            /* Bright fill pulse */
            uint32_t fill = ((uint32_t)glow << 24) | ((uint32_t)glow << 16) |
                            (0x40 << 8) | (alpha / 2);
            fill_rect(sdl_r, sx, sy, size, size, fill);

            /* Thick bright outline */
            uint32_t outline = (0xFF << 24) | ((uint32_t)glow << 16) |
                               (0x00 << 8) | alpha;
            draw_rect_outline(sdl_r, sx - 2, sy - 2, size + 4, size + 4, outline);
            draw_rect_outline(sdl_r, sx - 1, sy - 1, size + 2, size + 2, outline);
            draw_rect_outline(sdl_r, sx, sy, size, size, 0xFFFFFFFF);
        }
    }
}

/*=============================================================================
 * Render: VRAM Tile Address Inspector (toggled with I key)
 *===========================================================================*/

static void render_tile_inspector(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    if (!editor->show_tile_inspector) return;
    if (!editor->table.has_vram) return;
    /* Hide when mask editor panel is open (modal) */
    if (editor->mask_editor && editor->mask_editor->active) return;

    int s = editor->ui_scale;

    /* Compute hovered tile from mouse position */
    float world_x = editor->camera_x + (float)editor->mouse_x / editor->zoom;
    float world_y = editor->camera_y + (float)editor->mouse_y / editor->zoom;
    int col = (int)(world_x / 8.0f);
    int disp_row = (int)(world_y / 8.0f);
    int disp_rows = editor_display_rows(editor);
    int total_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;
    if (col < 0 || col >= total_cols || disp_row < 0 || disp_row >= disp_rows) return;

    int data_row = editor_display_to_data_row(editor, disp_row);

    /* Look up tilemap data */
    uint8_t tile_num = editor->table.tilemap[data_row][col];
    uint8_t attr = editor->table.tilemap_attrs[data_row][col];
    int pal_num = attr & 0x07;
    int tile_bank = (attr >> 3) & 1;
    bool x_flip = (attr >> 5) & 1;
    bool y_flip = (attr >> 6) & 1;
    int priority = (attr >> 7) & 1;

    /* Compute VRAM address */
    bool use_unsigned = editor->table.unsigned_addressing;
    int tile_offset;
    uint16_t vram_addr;
    if (use_unsigned) {
        tile_offset = (int)tile_num * 16;
        vram_addr = 0x8000 + (uint16_t)tile_offset;
    } else {
        tile_offset = (int)(int8_t)tile_num * 16 + 0x1000;
        vram_addr = 0x8000 + (uint16_t)tile_offset;
    }

    if (tile_offset < 0 || tile_offset + 16 > VRAM_TILE_DATA_SIZE) return;

    /* Get tile data pointer */
    const uint8_t *tile_data;
    GBCPalette *pal_source;
    if (editor->combined_view) {
        if (data_row < 32) {
            tile_data = &editor->vram_top[tile_bank][tile_offset];
            pal_source = editor->palettes_top;
        } else {
            tile_data = &editor->vram_bottom[tile_bank][tile_offset];
            pal_source = editor->palettes_bottom;
        }
    } else {
        if (!editor->vram_ref) return;
        tile_data = &editor->vram_ref->tile_data[tile_bank][tile_offset];
        pal_source = editor->table.bg_palettes;
    }

    GBCPalette pal = pal_source[pal_num];

    /* Format tooltip lines */
    int row_h = 7 * s;
    int pad = 4;

    char line1[48], line2[48], line3[48], line4[48];
    snprintf(line1, sizeof(line1), "Tile: 0x%02X (%d)", tile_num, tile_num);
    snprintf(line2, sizeof(line2), "VRAM: $%04X  Bank: %d", vram_addr, tile_bank);

    /* Build flip string */
    char flip_str[8] = "-";
    if (x_flip && y_flip) { flip_str[0] = 'H'; flip_str[1] = ' '; flip_str[2] = 'V'; flip_str[3] = '\0'; }
    else if (x_flip) { flip_str[0] = 'H'; flip_str[1] = '\0'; }
    else if (y_flip) { flip_str[0] = 'V'; flip_str[1] = '\0'; }
    snprintf(line3, sizeof(line3), "Pal: %d  Flip: %s  Pri: %d", pal_num, flip_str, priority);
    snprintf(line4, sizeof(line4), "Pos: (%d, %d)", col, disp_row);

    /* Tile preview size */
    int preview_scale = s * 2;
    int preview_size = 8 * preview_scale;

    /* Color swatch row: 4 swatches */
    int swatch_w = 4 * s;
    int swatch_h = 3 * s;
    int swatch_row_h = swatch_h + row_h + pad;  /* swatch + hex label */

    /* Compute panel size — generous fixed width so all text fits */
    int content_w = 30 * (4 + 1) * s;  /* ~30 chars wide */
    if (preview_size > content_w) content_w = preview_size;
    int tooltip_w = content_w + pad * 2;
    int tooltip_h = row_h * 4 + pad * 5 + preview_size + pad + swatch_row_h;

    /* Fixed position: upper-right, just left of sidebar */
    int sidebar_w = EDITOR_SIDEBAR_W;
    int tx = win_w - sidebar_w - tooltip_w - 8;
    int ty = 8;
    if (tx < 0) tx = 0;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    /* Background */
    fill_rect(sdl_r, tx, ty, tooltip_w, tooltip_h, 0x101010F0);
    draw_rect_outline(sdl_r, tx, ty, tooltip_w, tooltip_h, 0x6090C0FF);

    /* Text lines */
    int text_x = tx + pad;
    int text_y = ty + pad;
    draw_text_s(sdl_r, text_x, text_y, line1, COL_TEXT_WHITE, s);
    text_y += row_h + pad;
    draw_text_s(sdl_r, text_x, text_y, line2, 0x80C0FFFF, s);
    text_y += row_h + pad;
    draw_text_s(sdl_r, text_x, text_y, line3, COL_TEXT_DIM, s);
    text_y += row_h + pad;
    draw_text_s(sdl_r, text_x, text_y, line4, COL_TEXT_DIM, s);
    text_y += row_h + pad;

    /* 8x8 tile preview with actual palette colors and flip */
    int preview_x = tx + pad;
    int preview_y = text_y;
    for (int py = 0; py < 8; py++) {
        int src_row_idx = y_flip ? (7 - py) : py;
        uint8_t lo_byte = tile_data[src_row_idx * 2];
        uint8_t hi_byte = tile_data[src_row_idx * 2 + 1];
        for (int px = 0; px < 8; px++) {
            int bit = x_flip ? px : (7 - px);
            uint8_t color_idx = ((lo_byte >> bit) & 1) |
                               (((hi_byte >> bit) & 1) << 1);
            uint16_t c = pal.colors[color_idx];
            uint32_t rgba = rgb555_to_rgba(c);
            fill_rect(sdl_r, preview_x + px * preview_scale,
                      preview_y + py * preview_scale,
                      preview_scale, preview_scale, rgba);
        }
    }
    /* Outline around preview */
    draw_rect_outline(sdl_r, preview_x - 1, preview_y - 1,
                      preview_size + 2, preview_size + 2, 0x606060FF);
    text_y += preview_size + pad;

    /* 4 palette color swatches with hex labels */
    int sw_x = tx + pad;
    int sw_y = text_y;
    for (int ci = 0; ci < 4; ci++) {
        uint16_t c = pal.colors[ci];
        uint32_t rgba = rgb555_to_rgba(c);
        fill_rect(sdl_r, sw_x, sw_y, swatch_w, swatch_h, rgba);
        draw_rect_outline(sdl_r, sw_x, sw_y, swatch_w, swatch_h, 0x808080FF);
        /* Hex label below swatch */
        char hex[8];
        snprintf(hex, sizeof(hex), "%04X", c);
        draw_text_s(sdl_r, sw_x, sw_y + swatch_h + 2, hex, COL_TEXT_DIM, s > 2 ? s - 1 : s);
        sw_x += swatch_w + pad;
    }
}

/*=============================================================================
 * Render: Collision Tooltip (shown when hovering over collision tiles)
 *===========================================================================*/

static void render_collision_tooltip(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->show_collision_overlay || editor->hovered_coll_col < 0) return;
    /* Hide tooltip when mask editor panel is open */
    if (editor->mask_editor && editor->mask_editor->active) return;

    int s = editor->ui_scale;
    uint8_t attr = editor->hovered_coll_attr;
    int row_h = 7 * s;
    int pad = 4;

    /* Format tooltip lines */
    char line1[32], line2[32], line3[32];
    snprintf(line1, sizeof(line1), "Attr: 0x%02X (%d)", attr, attr);
    snprintf(line2, sizeof(line2), "Pos: (%d, %d)", editor->hovered_coll_col, editor->hovered_coll_row);
    snprintf(line3, sizeof(line3), "Type: %s", collision_attr_type_name(attr));

    /* Mask preview size (8x scaled = 8*8 = 64px, but use s-based scale for consistency) */
    int mask_preview_scale = s * 2;  /* Each mask pixel = 2*ui_scale screen pixels */
    int mask_preview_size = 8 * mask_preview_scale;

    /* Compute tooltip size */
    int w1 = text_width_s(line1, s);
    int w2 = text_width_s(line2, s);
    int w3 = text_width_s(line3, s);
    int max_w = w1;
    if (w2 > max_w) max_w = w2;
    if (w3 > max_w) max_w = w3;
    int swatch_size = 3 * s;

    /* Tooltip width: text area + mask preview side by side */
    int text_area_w = max_w + swatch_size + pad * 3;
    int tooltip_w = text_area_w + mask_preview_size + pad * 2;
    int tooltip_h = row_h * 3 + pad * 3;
    if (mask_preview_size + pad * 2 > tooltip_h) {
        tooltip_h = mask_preview_size + pad * 2;
    }

    /* Position near mouse cursor */
    int tx = editor->mouse_x + 16;
    int ty = editor->mouse_y + 16;

    /* Keep on screen */
    int win_w, win_h;
    SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);
    if (tx + tooltip_w > win_w) tx = editor->mouse_x - tooltip_w - 4;
    if (ty + tooltip_h > win_h) ty = editor->mouse_y - tooltip_h - 4;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    /* Background */
    fill_rect(sdl_r, tx, ty, tooltip_w, tooltip_h, 0x101010F0);
    draw_rect_outline(sdl_r, tx, ty, tooltip_w, tooltip_h, 0x808080FF);

    /* Color swatch */
    uint32_t swatch_color = collision_attr_color(attr) | 0xFF; /* fully opaque */
    fill_rect(sdl_r, tx + pad, ty + pad, swatch_size, swatch_size, swatch_color);

    /* Mask preview (rendered at right side of tooltip) */
    if (editor->mask_editor && attr != 0 && editor->game_state_ref) {
        int preview_x = tx + text_area_w + pad;
        int preview_y = ty + (tooltip_h - mask_preview_size) / 2;
        mask_editor_render_preview(editor->mask_editor, editor->game_state_ref,
                                    sdl_r, preview_x, preview_y,
                                    mask_preview_scale, attr);
    }

    /* Text */
    int text_x = tx + pad + swatch_size + pad;
    int text_y = ty + pad;
    draw_text_s(sdl_r, text_x, text_y, line1, COL_TEXT_WHITE, s);
    text_y += row_h + 1;
    draw_text_s(sdl_r, text_x, text_y, line2, COL_TEXT_DIM, s);
    text_y += row_h + 1;
    draw_text_s(sdl_r, text_x, text_y, line3, COL_TEXT_DIM, s);
}

/*=============================================================================
 * Render: Sidebar (Component Palette / Collision Presets) — scaled
 *===========================================================================*/

#define SIDEBAR_BASE_W  EDITOR_SIDEBAR_W  /* From editor.h */

/* Collision preset table */
static const struct { const char *name; uint8_t attr; uint32_t color; } coll_presets[] = {
    { "Passable",      0x00, 0x404040FF },
    { "Solid Wall",    0x01, 0x101010FF },
    { "Border/Drain",  0xFF, 0x303030FF },
    { "Left Flipper",  0xE0, 0xFF8040FF },
    { "Right Flipper", 0xF0, 0x40A0FFFF },
    { "Bumper Area",   0x32, 0x40FF40FF },
    { "Trigger Zone",  0x40, 0x4040FFFF },
    { "Wild Mon",      0xD0, 0xFF4040FF },
};
#define NUM_FIXED_PRESETS 8

static void render_sidebar(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    int s = editor->ui_scale;
    int sidebar_w = SIDEBAR_BASE_W;
    int item_h = 7 * s + 4;   /* Font height (6*s) + padding */
    int pad = 4;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    int sx = win_w - sidebar_w;
    fill_rect(sdl_r, sx, 0, sidebar_w, win_h, COL_SIDEBAR_BG);

    /* Tool mode indicator */
    const char *tool_name = "SELECT";
    switch (editor->current_tool) {
        case TOOL_SELECT:     tool_name = "SELECT"; break;
        case TOOL_PLACE:      tool_name = "PLACE"; break;
        case TOOL_TILE_PAINT: tool_name = "TILE"; break;
        case TOOL_COLL_PAINT: tool_name = "COLLISION"; break;
        case TOOL_ERASE:      tool_name = "ERASE"; break;
        case TOOL_PALETTE:    tool_name = "PALETTE"; break;
    }

    if (editor->current_tool == TOOL_PALETTE) {
        /* === Palette Editor Sidebar === */
        int scope = editor->pal_edit_scope;

        /* Pre-compute total content height for vertical centering */
        int swatch_sz_pre = 4 * s;
        int row_height_pre = swatch_sz_pre + 4;
        int content_h = 15 * s;  /* header + tool label */
        if (editor->combined_view) content_h += 7 * s + 2;  /* scope label */
        content_h += 2 + 8 * row_height_pre;  /* 8 palette rows */
        content_h += 10 + (7 * s + 2) * 2 + 4;  /* separator + info + hex */
        content_h += 3 * (4 * s + 3);  /* RGB bars */
        content_h += 2 + 8 * s + 4;  /* preview swatch */
        content_h += 4 + 4 * (7 * s);  /* separator + hints */
        if (editor->combined_view) content_h += 7 * s;  /* tab hint */

        int y_offset = (win_h - content_h) / 2;
        if (y_offset < pad) y_offset = pad;

        draw_text_s(sdl_r, sx + pad, y_offset, "PALETTE", COL_TEXT_WHITE, s);
        draw_text_s(sdl_r, sx + pad, y_offset + 7 * s, tool_name, 0xFFFF80FF, s);

        /* Scope indicator (combined view only) */
        int y = y_offset + 15 * s;
        if (editor->combined_view) {
            const char *scope_label;
            uint32_t scope_col;
            if (scope == 0)      { scope_label = "BOTH HALVES"; scope_col = 0x80FF80FF; }
            else if (scope == 1) { scope_label = "TOP ONLY";    scope_col = 0xFF8080FF; }
            else                 { scope_label = "BOTTOM ONLY"; scope_col = 0x8080FFFF; }
            draw_text_s(sdl_r, sx + pad, y, scope_label, scope_col, s);
            y += 7 * s + 2;
        }

        y += 2;
        int swatch_sz = 4 * s;       /* Each color swatch size */
        int swatch_gap = 2;           /* Gap between swatches */
        int row_height = swatch_sz + 4;

        /* Choose which palette array to read from based on scope */
        GBCPalette *display_palettes;
        if (scope == 1) {
            display_palettes = editor->palettes_top;
        } else if (scope == 2) {
            display_palettes = editor->palettes_bottom;
        } else {
            display_palettes = editor->table.bg_palettes;
        }

        /* Draw 8 palette rows, each with 4 color swatches */
        for (int p = 0; p < 8; p++) {
            bool pal_sel = (p == editor->pal_selected_palette);

            /* Highlight selected palette row */
            if (pal_sel) {
                fill_rect(sdl_r, sx + 2, y - 1, sidebar_w - 4, row_height + 2, COL_SIDEBAR_SEL);
            }

            /* Palette number label */
            char plabel[4];
            snprintf(plabel, sizeof(plabel), "%d", p);
            draw_text_s(sdl_r, sx + pad, y + 2, plabel, pal_sel ? COL_TEXT_WHITE : COL_TEXT_DIM, s);

            /* 4 color swatches */
            int swatch_x = sx + pad + 6 * s;
            for (int c = 0; c < 4; c++) {
                uint16_t color = display_palettes[p].colors[c];
                uint32_t rgba = rgb555_to_rgba(color);

                /* Draw swatch fill */
                fill_rect(sdl_r, swatch_x, y, swatch_sz, swatch_sz, rgba);

                /* Yellow outline on selected swatch */
                if (pal_sel && c == editor->pal_selected_color) {
                    draw_rect_outline(sdl_r, swatch_x - 1, y - 1,
                                      swatch_sz + 2, swatch_sz + 2, COL_SELECT_BOX);
                    draw_rect_outline(sdl_r, swatch_x - 2, y - 2,
                                      swatch_sz + 4, swatch_sz + 4, COL_SELECT_BOX);
                } else {
                    draw_rect_outline(sdl_r, swatch_x, y, swatch_sz, swatch_sz, 0x606060FF);
                }

                swatch_x += swatch_sz + swatch_gap;
            }

            y += row_height;
        }

        /* Separator */
        y += 4;
        draw_line(sdl_r, sx + 4, y, sx + sidebar_w - 4, y, 0x606060FF);
        y += 6;

        /* Selected palette/color info */
        {
            int pal = editor->pal_selected_palette;
            int slot = editor->pal_selected_color;
            uint16_t color = display_palettes[pal].colors[slot];
            int r_val = color & 0x1F;
            int g_val = (color >> 5) & 0x1F;
            int b_val = (color >> 10) & 0x1F;

            char info[48];
            snprintf(info, sizeof(info), "Pal %d  Color %d", pal, slot);
            draw_text_s(sdl_r, sx + pad, y, info, COL_TEXT_WHITE, s);
            y += 7 * s + 2;

            /* RGB555 hex value */
            snprintf(info, sizeof(info), "0x%04X", color);
            draw_text_s(sdl_r, sx + pad, y, info, COL_TEXT_DIM, s);
            y += 7 * s + 4;

            /* RGB channel bars */
            int bar_w = sidebar_w - pad * 2 - 20 * s;
            int bar_h_px = 4 * s;
            int bar_x = sx + pad + 16 * s;

            /* R channel */
            {
                bool active = (editor->pal_edit_channel == 0);
                uint32_t label_col = active ? 0xFF6060FF : COL_TEXT_DIM;
                draw_text_s(sdl_r, sx + pad, y, "R", label_col, s);
                snprintf(info, sizeof(info), "%2d", r_val);
                draw_text_s(sdl_r, sx + pad + 6 * s, y, info, active ? COL_TEXT_WHITE : COL_TEXT_DIM, s);
                fill_rect(sdl_r, bar_x, y, bar_w, bar_h_px, 0x303030FF);
                int fill_w = bar_w > 0 ? (r_val * bar_w) / 31 : 0;
                if (fill_w > 0) fill_rect(sdl_r, bar_x, y, fill_w, bar_h_px, 0xFF4040FF);
                if (active) draw_rect_outline(sdl_r, bar_x - 1, y - 1, bar_w + 2, bar_h_px + 2, COL_TEXT_WHITE);
                y += bar_h_px + 3;
            }

            /* G channel */
            {
                bool active = (editor->pal_edit_channel == 1);
                uint32_t label_col = active ? 0x60FF60FF : COL_TEXT_DIM;
                draw_text_s(sdl_r, sx + pad, y, "G", label_col, s);
                snprintf(info, sizeof(info), "%2d", g_val);
                draw_text_s(sdl_r, sx + pad + 6 * s, y, info, active ? COL_TEXT_WHITE : COL_TEXT_DIM, s);
                fill_rect(sdl_r, bar_x, y, bar_w, bar_h_px, 0x303030FF);
                int fill_w = bar_w > 0 ? (g_val * bar_w) / 31 : 0;
                if (fill_w > 0) fill_rect(sdl_r, bar_x, y, fill_w, bar_h_px, 0x40FF40FF);
                if (active) draw_rect_outline(sdl_r, bar_x - 1, y - 1, bar_w + 2, bar_h_px + 2, COL_TEXT_WHITE);
                y += bar_h_px + 3;
            }

            /* B channel */
            {
                bool active = (editor->pal_edit_channel == 2);
                uint32_t label_col = active ? 0x6060FFFF : COL_TEXT_DIM;
                draw_text_s(sdl_r, sx + pad, y, "B", label_col, s);
                snprintf(info, sizeof(info), "%2d", b_val);
                draw_text_s(sdl_r, sx + pad + 6 * s, y, info, active ? COL_TEXT_WHITE : COL_TEXT_DIM, s);
                fill_rect(sdl_r, bar_x, y, bar_w, bar_h_px, 0x303030FF);
                int fill_w = bar_w > 0 ? (b_val * bar_w) / 31 : 0;
                if (fill_w > 0) fill_rect(sdl_r, bar_x, y, fill_w, bar_h_px, 0x4040FFFF);
                if (active) draw_rect_outline(sdl_r, bar_x - 1, y - 1, bar_w + 2, bar_h_px + 2, COL_TEXT_WHITE);
                y += bar_h_px + 3;
            }

            /* Color preview swatch */
            y += 2;
            uint32_t preview_rgba = rgb555_to_rgba(color);
            fill_rect(sdl_r, sx + pad, y, sidebar_w - pad * 2, 8 * s, preview_rgba);
            draw_rect_outline(sdl_r, sx + pad, y, sidebar_w - pad * 2, 8 * s, 0x808080FF);
            y += 8 * s + 4;
        }

        /* Separator */
        draw_line(sdl_r, sx + 4, y, sx + sidebar_w - 4, y, 0x606060FF);
        y += 4;

        /* Hints */
        draw_text_s(sdl_r, sx + pad, y, "Up/Dn: Value", 0x808080FF, s);
        y += 7 * s;
        draw_text_s(sdl_r, sx + pad, y, "L/R: Channel", 0x808080FF, s);
        y += 7 * s;
        draw_text_s(sdl_r, sx + pad, y, "0-7: Palette", 0x808080FF, s);
        y += 7 * s;
        draw_text_s(sdl_r, sx + pad, y, "[/]: Color", 0x808080FF, s);
        if (editor->combined_view) {
            y += 7 * s;
            draw_text_s(sdl_r, sx + pad, y, "Tab: Scope", 0x808080FF, s);
        }

        /* Click handling for palette swatches */
        if (editor->mouse_left_clicked && editor->mouse_x >= sx) {
            /* Calculate swatch area start (after scope label), matching y_offset */
            int swatch_area_y = y_offset + 15 * s;
            if (editor->combined_view) swatch_area_y += 7 * s + 2;
            swatch_area_y += 2;

            int click_rel_y = editor->mouse_y - swatch_area_y;
            if (click_rel_y >= 0) {
                int clicked_pal = click_rel_y / row_height;
                if (clicked_pal >= 0 && clicked_pal < 8) {
                    editor->pal_selected_palette = clicked_pal;
                    /* Check which color swatch was clicked */
                    int swatch_start_x = sx + pad + 6 * s;
                    int click_x = editor->mouse_x - swatch_start_x;
                    if (click_x >= 0) {
                        int clicked_color = click_x / (swatch_sz + swatch_gap);
                        if (clicked_color >= 0 && clicked_color < 4) {
                            editor->pal_selected_color = clicked_color;
                        }
                    }
                }
            }
        }
    } else if (editor->current_tool == TOOL_COLL_PAINT) {
        /* === Collision Preset Sidebar === */
        int total_presets_pre = NUM_FIXED_PRESETS + (editor->has_sampled_attr ? 1 : 0);
        int coll_content_h = 16 * s + total_presets_pre * item_h + 4 + 4 * (7 * s);
        int coll_y_offset = (win_h - coll_content_h) / 2;
        if (coll_y_offset < pad) coll_y_offset = pad;

        draw_text_s(sdl_r, sx + pad, coll_y_offset, "COLL PRESETS", COL_TEXT_WHITE, s);
        draw_text_s(sdl_r, sx + pad, coll_y_offset + 7 * s, tool_name, 0xFFFF80FF, s);

        int y = coll_y_offset + 16 * s;

        /* Preset list */
        int total_presets = NUM_FIXED_PRESETS + (editor->has_sampled_attr ? 1 : 0);
        for (int i = 0; i < total_presets; i++) {
            uint8_t preset_attr;
            const char *preset_name;
            uint32_t swatch_color;
            char sampled_buf[24];

            if (i < NUM_FIXED_PRESETS) {
                preset_attr = coll_presets[i].attr;
                preset_name = coll_presets[i].name;
                swatch_color = coll_presets[i].color;
            } else {
                /* Sampled entry */
                preset_attr = editor->sampled_coll_attr;
                snprintf(sampled_buf, sizeof(sampled_buf), "Sampled 0x%02X", preset_attr);
                preset_name = sampled_buf;
                uint32_t c = collision_attr_color(preset_attr);
                swatch_color = c ? (c | 0xFF) : 0x808080FF;
            }

            bool selected = (editor->paint_coll_attr == preset_attr);
            uint32_t bg = selected ? COL_SIDEBAR_SEL : COL_SIDEBAR_ITEM;
            fill_rect(sdl_r, sx + 2, y, sidebar_w - 4, item_h - 2, bg);

            /* Color swatch */
            fill_rect(sdl_r, sx + pad, y + 2, 3 * s, item_h - 6, swatch_color);

            /* Name */
            draw_text_s(sdl_r, sx + pad + 4 * s, y + 2, preset_name, COL_TEXT_WHITE, s);

            /* Hex value on right */
            char hex[6];
            snprintf(hex, sizeof(hex), "0x%02X", preset_attr);
            int hex_w = text_width_s(hex, s);
            draw_text_s(sdl_r, sx + sidebar_w - pad - hex_w, y + 2, hex, COL_TEXT_DIM, s);

            y += item_h;
            if (y + item_h > win_h - 60 * s) break;
        }

        /* Hint text below presets */
        y += 4;
        draw_text_s(sdl_r, sx + pad, y, "Alt+Click: Sample", 0x808080FF, s);
        y += 7 * s;
        draw_text_s(sdl_r, sx + pad, y, "Shift+Drag: Fill", 0x808080FF, s);
        y += 7 * s;
        draw_text_s(sdl_r, sx + pad, y, "RClick: Erase", 0x808080FF, s);
        y += 7 * s;
        draw_text_s(sdl_r, sx + pad, y, "Tab: Checklist", 0x808080FF, s);

        /* Sidebar click handling for presets */
        if (editor->mouse_left_clicked && editor->mouse_x >= sx) {
            int list_start_y = coll_y_offset + 16 * s;
            int click_y = editor->mouse_y - list_start_y;
            if (click_y >= 0) {
                int idx = click_y / item_h;
                if (idx >= 0 && idx < total_presets) {
                    if (idx < NUM_FIXED_PRESETS) {
                        editor->paint_coll_attr = coll_presets[idx].attr;
                    } else {
                        editor->paint_coll_attr = editor->sampled_coll_attr;
                    }
                }
            }
        }
    } else if (editor->current_tool == TOOL_TILE_PAINT) {
        /* === Tile Paint Sidebar === */
        int tile_row_h = 7 * s;
        int tile_content_h = 16 * s + 2 * (tile_row_h + 2) + 6 + tile_row_h + 4 +
            14 * tile_row_h + 30 + 2 * tile_row_h;
        int tile_y_offset = (win_h - tile_content_h) / 2;
        if (tile_y_offset < pad) tile_y_offset = pad;

        draw_text_s(sdl_r, sx + pad, tile_y_offset, "TILE PAINT", COL_TEXT_WHITE, s);
        draw_text_s(sdl_r, sx + pad, tile_y_offset + 7 * s, tool_name, 0xFFFF80FF, s);

        int y = tile_y_offset + 16 * s;
        int row_h = 7 * s;

        /* Current tile info */
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "Tile: %d (0x%02X)", editor->paint_tile_index, editor->paint_tile_index);
        draw_text_s(sdl_r, sx + pad, y, tbuf, COL_TEXT_WHITE, s);
        y += row_h + 2;
        snprintf(tbuf, sizeof(tbuf), "Palette: %d", editor->paint_tile_palette);
        draw_text_s(sdl_r, sx + pad, y, tbuf, COL_TEXT_WHITE, s);
        y += row_h + 6;

        /* Separator */
        draw_line(sdl_r, sx + 4, y, sx + sidebar_w - 4, y, 0x606060FF);
        y += 6;

        /* Tile import info panel */
        draw_text_s(sdl_r, sx + pad, y, "=== TILE IMPORT ===", 0x80C0FFFF, s);
        y += row_h + 4;
        draw_text_s(sdl_r, sx + pad, y, "Place PNGs in table's", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "data/ folder:", COL_TEXT_DIM, s);
        y += row_h + 2;
        draw_text_s(sdl_r, sx + pad + 4 * s, y, "top_tiles.png", 0x80FF80FF, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad + 4 * s, y, "bottom_tiles.png", 0x80FF80FF, s);
        y += row_h + 6;
        draw_text_s(sdl_r, sx + pad, y, "Requirements:", 0xFFFF80FF, s);
        y += row_h + 2;
        draw_text_s(sdl_r, sx + pad, y, "- Grayscale PNG", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "  (4 shades)", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "- White=0 LGray=1", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "  DGray=2 Black=3", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "- Width: mult of 8px", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "- Max: 128x128 (256)", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "- 8x8 block = 1 tile", COL_TEXT_DIM, s);
        y += row_h + 6;

        /* Separator */
        draw_line(sdl_r, sx + 4, y, sx + sidebar_w - 4, y, 0x606060FF);
        y += 6;

        draw_text_s(sdl_r, sx + pad, y, "View: 160x144 pixels", COL_TEXT_DIM, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "= 20x18 tiles/half", COL_TEXT_DIM, s);
        y += row_h + 6;
        draw_text_s(sdl_r, sx + pad, y, "R: Reload tiles", 0x80C0FFFF, s);
        y += row_h;
        draw_text_s(sdl_r, sx + pad, y, "[/]: Change tile", 0x808080FF, s);
    } else {
        /* === Component List Sidebar === */
        int comp_count_display = COMP_COUNT - 1;  /* exclude COMP_NONE */
        int comp_content_h = 16 * s + comp_count_display * item_h + 6 + item_h;
        int comp_y_offset = (win_h - comp_content_h) / 2;
        if (comp_y_offset < pad) comp_y_offset = pad;

        draw_text_s(sdl_r, sx + pad, comp_y_offset, "COMPONENTS", COL_TEXT_WHITE, s);
        draw_text_s(sdl_r, sx + pad, comp_y_offset + 7 * s, tool_name, 0xFFFF80FF, s);

        int y = comp_y_offset + 16 * s;
        for (int i = 1; i < COMP_COUNT; i++) {
            bool selected = (editor->palette_selection == i && editor->current_tool == TOOL_PLACE);
            uint32_t bg = selected ? COL_SIDEBAR_SEL : COL_SIDEBAR_ITEM;
            fill_rect(sdl_r, sx + 2, y, sidebar_w - 4, item_h - 2, bg);

            /* Color swatch */
            uint32_t swatch = (i < COMP_COUNT) ? comp_colors[i] : COL_OBJECT_BOX;
            fill_rect(sdl_r, sx + pad, y + 2, 3 * s, item_h - 6, swatch);

            /* Name */
            draw_text_s(sdl_r, sx + pad + 4 * s, y + 2, editor_component_name(i), COL_TEXT_WHITE, s);

            y += item_h;
            if (y + item_h > win_h - 30) break;
        }

        /* Separator before "Solid Wall" shortcut */
        y += 2;
        draw_line(sdl_r, sx + 4, y, sx + sidebar_w - 4, y, 0x606060FF);
        y += 4;

        /* "Solid Wall" collision shortcut entry */
        int solid_wall_y = y;
        bool is_solid_selected = (editor->current_tool == TOOL_COLL_PAINT && editor->paint_coll_attr == 0x01);
        uint32_t sw_bg = is_solid_selected ? COL_SIDEBAR_SEL : COL_SIDEBAR_ITEM;
        fill_rect(sdl_r, sx + 2, y, sidebar_w - 4, item_h - 2, sw_bg);
        fill_rect(sdl_r, sx + pad, y + 2, 3 * s, item_h - 6, 0x101010FF);
        draw_text_s(sdl_r, sx + pad + 4 * s, y + 2, "Solid Wall", COL_TEXT_WHITE, s);
        draw_text_s(sdl_r, sx + sidebar_w - pad - text_width_s("0x01", s), y + 2, "0x01", COL_TEXT_DIM, s);

        /* Click handling for component list + Solid Wall */
        if (editor->mouse_left_clicked && editor->mouse_x >= sx) {
            int list_start_y = comp_y_offset + 16 * s;
            int click_y = editor->mouse_y - list_start_y;

            /* Check if clicked on Solid Wall entry */
            if (editor->mouse_y >= solid_wall_y && editor->mouse_y < solid_wall_y + item_h) {
                editor->current_tool = TOOL_COLL_PAINT;
                editor->paint_coll_attr = 0x01;
                editor->palette_selection = COMP_NONE;
            } else if (click_y >= 0) {
                int idx = click_y / item_h + 1;
                if (idx >= 1 && idx < COMP_COUNT) {
                    editor->palette_selection = (ComponentType)idx;
                    editor->current_tool = TOOL_PLACE;
                }
            }
        }
    }
}

/*=============================================================================
 * Render: Fill Rectangle Preview
 *===========================================================================*/

static void render_fill_preview(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->fill_active) return;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    float world_x = editor->camera_x + (float)editor->mouse_x / editor->zoom;
    float world_y = editor->camera_y + (float)editor->mouse_y / editor->zoom;
    int cur_col = (int)(world_x / 8.0f);
    int cur_row = (int)(world_y / 8.0f);
    int max_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;
    int disp_rows = editor_display_rows(editor);

    int c0 = editor->fill_start_col < cur_col ? editor->fill_start_col : cur_col;
    int c1 = editor->fill_start_col > cur_col ? editor->fill_start_col : cur_col;
    int r0 = editor->fill_start_row < cur_row ? editor->fill_start_row : cur_row;
    int r1 = editor->fill_start_row > cur_row ? editor->fill_start_row : cur_row;

    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= max_cols) c1 = max_cols - 1;
    if (r1 >= disp_rows) r1 = disp_rows - 1;

    int sx = world_to_screen_x(editor, (float)(c0 * 8));
    int sy = world_to_screen_y(editor, (float)(r0 * 8));
    int ex = world_to_screen_x(editor, (float)((c1 + 1) * 8));
    int ey = world_to_screen_y(editor, (float)((r1 + 1) * 8));
    int rw = ex - sx;
    int rh = ey - sy;

    /* Semi-transparent fill */
    uint32_t fill_color = editor->fill_erasing ? 0xFF404040 : 0x40FF4040;
    fill_rect(sdl_r, sx, sy, rw, rh, fill_color);

    /* Double outline */
    uint32_t outline_color = editor->fill_erasing ? 0xFF6060FF : 0x80FF80FF;
    draw_rect_outline(sdl_r, sx, sy, rw, rh, outline_color);
    draw_rect_outline(sdl_r, sx - 1, sy - 1, rw + 2, rh + 2, outline_color);

    /* Dimensions label */
    int s = editor->ui_scale;
    int fill_w = c1 - c0 + 1;
    int fill_h = r1 - r0 + 1;
    char dim[16];
    snprintf(dim, sizeof(dim), "%dx%d", fill_w, fill_h);
    int tw = text_width_s(dim, s);
    int label_x = sx + (rw - tw) / 2;
    int label_y = sy - 8 * s;
    if (label_y < 0) label_y = ey + 2;
    draw_text_s(sdl_r, label_x, label_y, dim, COL_TEXT_WHITE, s);
}

/*=============================================================================
 * Render: Left Info Panel — keyboard shortcuts & tool reference
 *===========================================================================*/

static void render_left_panel(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    int s = editor->ui_scale;
    int line_h = 7 * s;           /* Line height */
    int half_h = 4 * s;           /* Half-line gap */
    int sec_gap = 4 * s;          /* Extra gap before section header */
    int pad = 6 * s;              /* Panel inner padding */
    int top_bar = 7 * s + 6;     /* Help bar height */
    int bot_bar = 7 * s + 6;     /* Status bar height */

    /* Panel width: fit ~30 chars */
    int panel_w = 30 * (4 + 1) * s + pad * 2;
    if (panel_w > win_w / 3) panel_w = win_w / 3;

    int panel_x = 0;
    int panel_y = top_bar;
    int panel_h = win_h - top_bar - bot_bar;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    fill_rect(sdl_r, panel_x, panel_y, panel_w, panel_h, 0x181818D0);
    fill_rect(sdl_r, panel_x + panel_w - 1, panel_y, 1, panel_h, 0x404040FF);

    int x = panel_x + pad;
    int y = panel_y + pad;
    int max_y = panel_y + panel_h - pad;

    #define LP_SEC(label) do { \
        if (y + sec_gap + line_h > max_y) goto done; \
        y += sec_gap; \
        draw_text_s(sdl_r, x, y, label, 0x80C0FFFF, s); \
        y += line_h; \
        fill_rect(sdl_r, x, y - 2, panel_w - pad * 2, 1, 0x405060FF); \
    } while(0)
    #define LP(label) do { \
        if (y + line_h > max_y) goto done; \
        draw_text_s(sdl_r, x, y, label, 0xA0A0A0FF, s); \
        y += line_h; \
    } while(0)
    #define LP_KEY(label) do { \
        if (y + line_h > max_y) goto done; \
        draw_text_s(sdl_r, x, y, label, 0xD0D0D0FF, s); \
        y += line_h; \
    } while(0)
    #define LP_DIM(label) do { \
        if (y + line_h > max_y) goto done; \
        draw_text_s(sdl_r, x, y, label, 0x707070FF, s); \
        y += line_h; \
    } while(0)
    #define LP_GAP() do { y += half_h; } while(0)

    /* ============================================================
     * MASK EDITOR ACTIVE: show dedicated mask editor help
     * ============================================================ */
    if (editor->mask_editor && editor->mask_editor->active) {
        char attr_buf[32];
        snprintf(attr_buf, sizeof(attr_buf), "MASK EDITOR: 0x%02X",
                 editor->mask_editor->edit_attr);
        draw_text_s(sdl_r, x, y, attr_buf, 0xFFFF80FF, s);
        y += line_h;

        LP_SEC("DRAWING");
        LP("Edit the 8x8 collision mask");
        LP("for this attribute. White");
        LP("pixels are solid, dark pixels");
        LP("are passable. The ball tests");
        LP("16 points around its radius");
        LP("against this mask each frame.");
        LP_GAP();
        LP_KEY("L-Click / L-Drag  Solid");
        LP_KEY("R-Click / R-Drag  Passable");
        LP_GAP();
        LP("Changes apply to ALL tiles");
        LP("on the map that share this");
        LP("attribute ID.");

        LP_SEC("TOOLS (click buttons)");
        LP_KEY("Fill     Set all solid");
        LP_KEY("Clear    Set all passable");
        LP_KEY("MirH     Mirror horizontal");
        LP_KEY("MirV     Mirror vertical");
        LP_KEY("Rot90    Rotate 90 degrees");

        LP_SEC("UNDO / CLOSE");
        LP_KEY("^Z       Undo last stroke");
        LP_KEY("^Y       Redo");
        LP_KEY("ESC      Close mask editor");
        LP_DIM("  Mask is saved when closed.");
        LP_DIM("  Use ^S to persist to disk.");

        if (editor->mask_editor->is_flipper_mask) {
            LP_SEC("FLIPPER MASK");
            LP("Flipper masks have 3 angle");
            LP("tabs. Each tab covers a");
            LP("range of flipper positions.");
            LP("Click the tabs above the");
            LP("grid to switch angles.");
            LP_GAP();
            LP_KEY("Tab 1: 0-6 degrees");
            LP_KEY("Tab 2: 7-13 degrees");
            LP_KEY("Tab 3: 14+ degrees");
        }

        LP_SEC("BALL TEST POINTS");
        LP_KEY("Q  Toggle overlay (in coll)");
        LP_DIM("  Shows 16 test points the");
        LP_DIM("  physics engine checks per");
        LP_DIM("  frame. Helps verify mask");
        LP_DIM("  coverage for ball bounce.");

        goto done;
    }

    /* ============================================================
     * NORMAL MODE: Tabbed layout
     * Tab 0 = Tools (tool-specific help + switch tool)
     * Tab 1 = Overlays & Controls (view, masks, flippers, file)
     * Toggle with ` (grave/tilde)
     * ============================================================ */

    /* ---- Clickable tab headers ---- */
    {
        int tab = editor->left_panel_tab;
        const char *tab0_label = "TOOLS";
        const char *tab1_label = "OVERLAYS";
        int tab0_w = text_width_s(tab0_label, s) + 4 * s;
        int tab1_w = text_width_s(tab1_label, s) + 4 * s;
        int tab_h = line_h + 2;
        int tab0_x = x;
        int tab1_x = x + tab0_w + 2 * s;

        /* Tab 0 button */
        uint32_t tab0_bg = (tab == 0) ? 0x405060FF : 0x282828FF;
        uint32_t tab0_col = (tab == 0) ? 0xFFFFFFFF : 0x808080FF;
        fill_rect(sdl_r, tab0_x, y, tab0_w, tab_h, tab0_bg);
        draw_text_s(sdl_r, tab0_x + 2 * s, y + 1, tab0_label, tab0_col, s);

        /* Tab 1 button */
        uint32_t tab1_bg = (tab == 1) ? 0x405060FF : 0x282828FF;
        uint32_t tab1_col = (tab == 1) ? 0xFFFFFFFF : 0x808080FF;
        fill_rect(sdl_r, tab1_x, y, tab1_w, tab_h, tab1_bg);
        draw_text_s(sdl_r, tab1_x + 2 * s, y + 1, tab1_label, tab1_col, s);

        /* Underline active tab */
        int active_x = (tab == 0) ? tab0_x : tab1_x;
        int active_w = (tab == 0) ? tab0_w : tab1_w;
        fill_rect(sdl_r, active_x, y + tab_h - 1, active_w, 1, 0x80C0FFFF);

        /* Click detection */
        int mx = editor->mouse_x, my = editor->mouse_y;
        if (editor->mouse_left_clicked) {
            if (mx >= tab0_x && mx < tab0_x + tab0_w && my >= y && my < y + tab_h) {
                editor->left_panel_tab = 0;
            } else if (mx >= tab1_x && mx < tab1_x + tab1_w && my >= y && my < y + tab_h) {
                editor->left_panel_tab = 1;
            }
        }

        y += tab_h + 2;
    }

    if (editor->left_panel_tab == 0) {
        /* ==== TAB 0: TOOLS ==== */

        /* ---- Current tool indicator ---- */
        {
            const char *tname = "SELECT";
            uint32_t tcol = 0x60FF60FF;
            switch (editor->current_tool) {
                case TOOL_SELECT:     tname = "MODE: SELECT"; break;
                case TOOL_PLACE:      tname = "MODE: PLACE"; tcol = 0xFF8040FF; break;
                case TOOL_TILE_PAINT: tname = "MODE: TILE PAINT"; tcol = 0x40C0FFFF; break;
                case TOOL_COLL_PAINT: tname = "MODE: COLLISION"; tcol = 0xFF6060FF; break;
                case TOOL_ERASE:      tname = "MODE: ERASE"; tcol = 0xFF4040FF; break;
                case TOOL_PALETTE:    tname = "MODE: PALETTE"; tcol = 0xFFC040FF; break;
            }
            draw_text_s(sdl_r, x, y, tname, tcol, s);
            y += line_h;
        }

        /* ---- Context-specific detailed help ---- */
        switch (editor->current_tool) {
        case TOOL_SELECT:
            LP_SEC("SELECT TOOL");
            LP("Click an object to select it.");
            LP("Drag to reposition. Snaps to");
            LP("grid when G is enabled.");
            LP_GAP();
            LP_KEY("Click    Select object");
            LP_KEY("Drag     Move object");
            LP_KEY("Del      Delete selected");
            LP_KEY("^D       Duplicate (+16px)");
            LP_GAP();
            LP_DIM("Click collision tile to");
            LP_DIM("select it (with C overlay).");
            LP_DIM("Drag to move coll tile.");
            break;

        case TOOL_PLACE:
            LP_SEC("PLACE TOOL");
            LP("Click viewport to place the");
            LP("component selected in the");
            LP("right sidebar. A ghost shows");
            LP("where it will go.");
            LP_GAP();
            LP_KEY("Click    Place component");
            LP_KEY("Sidebar  Choose type");
            LP_GAP();
            LP("Components: Bumper, Spinner,");
            LP("Ball Upgrade, Pikachu Saver,");
            LP("CAVE Light, Diglett, Field");
            LP("Creature, Staryu, Slot, Rail,");
            LP("Launch, Wild Mon, Board Trig,");
            LP("Ditto Slot.");
            break;

        case TOOL_TILE_PAINT:
            LP_SEC("TILE PAINT TOOL");
            LP("Paint tilemap indices onto");
            LP("the background layer. Tiles");
            LP("come from VRAM tile data.");
            LP_GAP();
            LP_KEY("L-Click  Paint tile");
            LP_KEY("[  ]     Prev/Next tile idx");
            LP_KEY("R        Reload tileset PNG");
            LP_GAP();
            LP("Custom tiles: place PNG files");
            LP("named top_tiles.png and/or");
            LP("bottom_tiles.png in your");
            LP("table's data/ folder.");
            LP_GAP();
            LP_DIM("Format: 2bpp GBC tiles,");
            LP_DIM("8x8 px each, left-to-right");
            LP_DIM("top-to-bottom (rgbgfx order)");
            break;

        case TOOL_COLL_PAINT:
            LP_SEC("COLLISION PAINT TOOL");
            LP("Paint collision attributes");
            LP("onto the collision map.");
            LP("Each tile has an 8-bit attr");
            LP("ID and an 8x8 pixel mask.");
            LP_GAP();
            LP_KEY("L-Click    Paint attribute");
            LP_KEY("R-Click    Erase (set 0x00)");
            LP_KEY("0-9        Quick attr select");
            LP_DIM("  0=0x00 1=0x10 2=0x20 ...");
            LP_KEY("Alt+Click  Eyedropper sample");
            LP_GAP();
            LP_KEY("Shift+L-Drag  Fill rectangle");
            LP_KEY("Shift+R-Drag  Erase rectangle");
            LP_GAP();
            LP_SEC("COLLISION PRESETS");
            LP("0x00 Passable (no collision)");
            LP("0x01 Solid wall");
            LP("0xFF Border / drain");
            LP("0xE0 Left flipper zones");
            LP("0xF0 Right flipper zones");
            LP("0xD0 Wild mon encounter");
            break;

        case TOOL_ERASE:
            LP_SEC("ERASE TOOL");
            LP("Click on any placed object");
            LP("to delete it immediately.");
            LP_GAP();
            LP_KEY("Click    Delete object");
            LP_GAP();
            LP_DIM("Cannot erase collision or");
            LP_DIM("tiles. Use Collision tool");
            LP_DIM("R-Click to clear coll.");
            break;

        case TOOL_PALETTE:
            LP_SEC("PALETTE EDITOR");
            LP("Edit BG palette colors.");
            LP("GBC uses RGB555 (5 bits per");
            LP("channel, values 0-31).");
            LP("8 palettes x 4 colors each.");
            LP_GAP();
            LP_KEY("0-7      Select palette");
            LP_KEY("[  ]     Prev/Next color");
            LP_KEY("Up/Dn    Adjust channel");
            LP_DIM("  Hold for auto-repeat.");
            LP_KEY("Left/Rt  Cycle R > G > B");
            LP_KEY("Tab      Cycle edit scope");
            LP_DIM("  Both > Top > Bottom");
            LP_GAP();
            LP("Sidebar shows all 8 palettes");
            LP("with 4 color swatches, plus");
            LP("R/G/B sliders for the active");
            LP("color. Hex value shown.");
            LP_GAP();
            LP_DIM("Top/Bottom scope lets you");
            LP_DIM("set different palettes for");
            LP_DIM("each stage half in combined");
            LP_DIM("view tables.");
            break;
        }

        /* ---- Switch tool (compact) ---- */
        LP_SEC("SWITCH TOOL");
        LP_KEY("S Select  P Place  E Erase");
        LP_KEY("T Tile    X Coll   L Palette");

        /* ---- Quick reference ---- */
        LP_SEC("FILE");
        LP_KEY("^S  Save   ^Z  Undo");
        LP_KEY("F5  Play   F6  Reload Lua");
        LP_KEY("ESC Back   Tab Checklist");

    } else {
        /* ==== TAB 1: OVERLAYS & CONTROLS ==== */

        LP_SEC("VIEW CONTROLS");
        LP_KEY("G        Toggle grid + snap");
        LP_KEY("C        Toggle coll overlay");
        LP_KEY("B        Toggle obj bounds");
        LP_KEY("I        Tile VRAM inspector");
        LP_DIM("  Shows tile index, address,");
        LP_DIM("  bank, palette, flip, and");
        LP_DIM("  pixel preview at cursor.");
        LP_KEY("H        Hide buffer rows");
        LP_DIM("  Combined view only. Hides");
        LP_DIM("  off-screen rows 18-31.");
        LP_KEY("Home     Center + zoom 2x");
        LP_KEY("Wheel    Zoom in/out");
        LP_KEY("Mid-Drag Pan camera");

        LP_SEC("FLIPPER SWEEP");
        LP_KEY("F        Toggle flipper viz");
        LP_DIM("  Animates flipper collision");
        LP_DIM("  masks through 16 angles.");
        LP_DIM("  Shows pivot crosshairs,");
        LP_DIM("  detection rect, and mask");
        LP_DIM("  geometry per angle set.");
        LP_GAP();
        LP_DIM("  Requires C (coll overlay).");
        LP_DIM("  Only on tables w/ flippers.");
        LP_GAP();
        LP("  Angle sets:");
        LP("    Red    = 0-6  (down)");
        LP("    Yellow = 7-13 (mid)");
        LP("    Green  = 14+  (up)");

        LP_SEC("MASK EDITOR");
        LP_KEY("M        Open mask editor");
        LP_DIM("  Hover a collision tile");
        LP_DIM("  (C overlay on, attr != 0)");
        LP_DIM("  and press M to edit its");
        LP_DIM("  8x8 solid/passable bitmap.");
        LP_KEY("Q        Ball test points");
        LP_DIM("  Visualize the 16 collision");
        LP_DIM("  check points around ball.");

        LP_SEC("FILE & PLAYTEST");
        LP_KEY("^S       Save table to disk");
        LP_KEY("^Z       Undo last action");
        LP_KEY("F5       Start playtest");
        LP_DIM("  Injects collision, tiles,");
        LP_DIM("  and palettes into game.");
        LP_KEY("F6       Hot-reload Lua");
        LP_DIM("  During playtest only.");
        LP_KEY("ESC      Back / cancel tool");
        LP_DIM("  Tool > Select > Picker");
        LP_KEY("Tab      Readiness checklist");

        /* ---- Switch tool (compact) ---- */
        LP_SEC("SWITCH TOOL");
        LP_KEY("S Select  P Place  E Erase");
        LP_KEY("T Tile    X Coll   L Palette");
    }

    /* ---- Status (shown on both tabs) ---- */
    {
        char buf[32];
        y += sec_gap;
        if (y + line_h * 5 <= max_y) {
            fill_rect(sdl_r, x, y, panel_w - pad * 2, 1, 0x404040FF);
            y += half_h;
            snprintf(buf, sizeof(buf), "Zoom: %.1fx", editor->zoom);
            LP(buf);
            snprintf(buf, sizeof(buf), "Objs: %d/%d",
                     editor->table.num_objects, MAX_EDITOR_OBJECTS);
            LP(buf);
            snprintf(buf, sizeof(buf), "Grid: %s  Coll: %s",
                     editor->snap_to_grid ? "ON" : "OFF",
                     editor->show_collision_overlay ? "ON" : "OFF");
            LP(buf);
            if (editor->show_flipper_sweep) {
                int aset = (editor->flipper_anim_angle <= 6) ? 0 :
                           (editor->flipper_anim_angle <= 13) ? 1 : 2;
                snprintf(buf, sizeof(buf), "Flip: %d/15 (set %d)",
                         editor->flipper_anim_angle, aset);
                LP(buf);
            }
        }
    }

done:
    #undef LP_SEC
    #undef LP
    #undef LP_KEY
    #undef LP_DIM
    #undef LP_GAP
    (void)0;
}

/*=============================================================================
 * Render: Table Readiness Checklist
 *===========================================================================*/

static void render_checklist(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    if (!editor->show_checklist) return;

    int s = editor->ui_scale;
    int row_h = 7 * s + 2;
    int pad = 6;
    int panel_w = SIDEBAR_BASE_W + 120 * s;  /* Wider to fit all content */
    if (panel_w > win_w - 40) panel_w = win_w - 40;
    int panel_x = (win_w - panel_w) / 2;     /* Centered horizontally */
    int panel_y = (win_h) / 3;               /* Upper-third vertically */

    int disp_rows = editor_display_rows(editor);
    int max_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;
    bool has_flippers = editor->table.has_flippers;

    /* --- Perform checks --- */
    typedef struct { const char *label; bool pass; bool visible; } CheckItem;
    CheckItem checks[8];
    int num_checks = 0;

    /* Count collision attributes in various ranges */
    int left_flipper_count = 0;
    int right_flipper_count = 0;
    int drain_count = 0;
    int drain_total = 0;
    int perimeter_nonzero = 0;
    int perimeter_total = 0;
    int total_tiles = 0;
    int nonzero_tiles = 0;

    for (int r = 0; r < disp_rows; r++) {
        int dr = editor_display_to_data_row(editor, r);
        if (dr < 0 || dr >= 64) continue;
        for (int c = 0; c < max_cols; c++) {
            uint8_t attr = editor->table.collision_map[dr][c];
            total_tiles++;
            if (attr != 0) nonzero_tiles++;

            /* Left flipper zones (0xE0-0xEF) */
            if (attr >= 0xE0 && attr <= 0xEF) left_flipper_count++;
            /* Right flipper zones (0xF0-0xFE) */
            if (attr >= 0xF0 && attr <= 0xFE) right_flipper_count++;

            /* Bottom 2 rows drain check */
            if (r >= disp_rows - 2) {
                drain_total++;
                if (attr == 0xFF) drain_count++;
            }

            /* Perimeter check (top row, bottom row, left col, right col) */
            if (r == 0 || r == disp_rows - 1 || c == 0 || c == max_cols - 1) {
                perimeter_total++;
                if (attr != 0) perimeter_nonzero++;
            }
        }
    }

    /* Check objects */
    bool has_launch_alley = false;
    bool has_bumper = false;
    bool has_slot_machine = false;
    for (int i = 0; i < editor->table.num_objects; i++) {
        if (editor->table.objects[i].type == COMP_LAUNCH_ALLEY) has_launch_alley = true;
        if (editor->table.objects[i].type == COMP_BUMPER) has_bumper = true;
        if (editor->table.objects[i].type == COMP_SLOT_MACHINE) has_slot_machine = true;
    }

    /* Build check list */
    if (has_flippers) {
        checks[num_checks++] = (CheckItem){ "L Flipper zones >=8", left_flipper_count >= 8, true };
        checks[num_checks++] = (CheckItem){ "R Flipper zones >=8", right_flipper_count >= 8, true };
        checks[num_checks++] = (CheckItem){ "Drain zone (bottom)", drain_total > 0 && drain_count > drain_total / 2, true };
    }
    checks[num_checks++] = (CheckItem){ "Outer walls >50%", perimeter_total > 0 && perimeter_nonzero > perimeter_total / 2, true };
    if (has_flippers) {
        checks[num_checks++] = (CheckItem){ "Launch alley obj", has_launch_alley, true };
    }
    checks[num_checks++] = (CheckItem){ "At least 1 bumper", has_bumper, true };
    checks[num_checks++] = (CheckItem){ "Slot machine obj", has_slot_machine, true };

    /* Panel size */
    int panel_h = (num_checks + 3) * row_h + pad * 3;  /* +3 for title, separator, coverage */

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    fill_rect(sdl_r, panel_x, panel_y, panel_w, panel_h, 0x1A1A1AE8);
    draw_rect_outline(sdl_r, panel_x, panel_y, panel_w, panel_h, 0x606060FF);

    int y = panel_y + pad;

    /* Title */
    draw_text_s(sdl_r, panel_x + pad, y, "TABLE CHECKLIST", 0xFFFF80FF, s);
    y += row_h + 2;

    /* Check items */
    for (int i = 0; i < num_checks; i++) {
        const char *icon = checks[i].pass ? "[OK]" : "[!!]";
        uint32_t icon_color = checks[i].pass ? 0x40FF40FF : 0xFF4040FF;
        draw_text_s(sdl_r, panel_x + pad, y, icon, icon_color, s);
        draw_text_s(sdl_r, panel_x + pad + text_width_s("[OK] ", s), y,
                    checks[i].label, COL_TEXT_WHITE, s);
        y += row_h;
    }

    /* Separator */
    y += 2;
    draw_line(sdl_r, panel_x + 4, y, panel_x + panel_w - 4, y, 0x606060FF);
    y += 4;

    /* Coverage stat */
    int pct = total_tiles > 0 ? (nonzero_tiles * 100 / total_tiles) : 0;
    char cov[48];
    snprintf(cov, sizeof(cov), "Coverage: %d%% (%d/%d)", pct, nonzero_tiles, total_tiles);
    draw_text_s(sdl_r, panel_x + pad, y, cov, COL_TEXT_DIM, s);
}

/*=============================================================================
 * Render: Property Inspector — scaled
 *===========================================================================*/

static void render_inspector(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    if (editor->selected_object < 0 || editor->selected_object >= editor->table.num_objects)
        return;

    EditorObject *obj = &editor->table.objects[editor->selected_object];
    int s = editor->ui_scale;
    int row_h = 7 * s + 2;
    int pad = 4;
    int iw = SIDEBAR_BASE_W;
    int ix = 0;
    int iy = 8 * s;
    int label_x = ix + pad;
    int value_x = ix + 40 * s;  /* Offset for values — room for 6-char labels */

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    fill_rect(sdl_r, ix, iy, iw, win_h - iy - 10 * s, 0x1A1A1AE8);

    /* Title */
    char title[64];
    snprintf(title, sizeof(title), "-- %s #%d --",
             editor_component_name(obj->type), editor->selected_object);
    draw_text_s(sdl_r, label_x, iy + 2, title, 0xFFFF80FF, s);
    iy += row_h + 4;

    char buf[64];

    /* Type */
    draw_text_s(sdl_r, label_x, iy, "Type:", COL_TEXT_DIM, s);
    draw_text_s(sdl_r, value_x, iy, editor_component_name(obj->type), COL_TEXT_WHITE, s);
    iy += row_h;

    /* X */
    draw_text_s(sdl_r, label_x, iy, "X:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "%d", obj->x);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* Y */
    draw_text_s(sdl_r, label_x, iy, "Y:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "%d", obj->y);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* X Thresh */
    draw_text_s(sdl_r, label_x, iy, "XThr:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "%d", obj->x_thresh);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* Y Thresh */
    draw_text_s(sdl_r, label_x, iy, "YThr:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "%d", obj->y_thresh);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* Score */
    draw_text_s(sdl_r, label_x, iy, "Score:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "%d", obj->score);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* Force */
    draw_text_s(sdl_r, label_x, iy, "Force:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "0x%04X", obj->bounce_force);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* SFX */
    draw_text_s(sdl_r, label_x, iy, "SFX:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "0x%02X", obj->sfx_id);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* Gate */
    draw_text_s(sdl_r, label_x, iy, "Gate:", COL_TEXT_DIM, s);
    draw_text_s(sdl_r, value_x, iy, obj->attribute_gated ? "Yes" : "No", COL_TEXT_WHITE, s);
    iy += row_h;

    /* Separator */
    iy += 4;
    draw_line(sdl_r, ix + 4, iy, ix + iw - 4, iy, 0x606060FF);
    iy += 6;

    /* Hints */
    draw_text_s(sdl_r, label_x, iy, "Del: Remove", 0x808080FF, s);
    iy += row_h;
    draw_text_s(sdl_r, label_x, iy, "Ctrl+D: Dupe", 0x808080FF, s);
    iy += row_h;
    draw_text_s(sdl_r, label_x, iy, "Drag: Move", 0x808080FF, s);
}

/*=============================================================================
 * Render: Collision Tile Inspector (when collision tile is selected, no object)
 *===========================================================================*/

static void render_collision_inspector(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    if (editor->selected_object >= 0) return;  /* Object inspector takes priority */
    if (editor->selected_coll_col < 0) return;
    /* Hide when mask editor panel is open */
    if (editor->mask_editor && editor->mask_editor->active) return;

    int s = editor->ui_scale;
    int row_h = 7 * s + 2;
    int pad = 6;
    int iw = 30 * (4 + 1) * s + pad * 2;  /* ~30 chars wide + padding */
    int ix = win_w - SIDEBAR_BASE_W - iw - 8;  /* Upper-right, left of sidebar */
    int iy = 40;
    if (ix < 0) ix = 0;
    int label_x = ix + pad;
    int value_x = ix + 10 * (4 + 1) * s;  /* 10 chars in for values */

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    /* Calculate panel height: need extra space for mask preview */
    int mask_preview_h = 0;
    if (editor->mask_editor && editor->game_state_ref && editor->selected_coll_attr != 0) {
        mask_preview_h = 8 * s * 2 + 8;  /* mask_size + padding */
    }
    int panel_total_h = row_h * 7 + pad * 4 + mask_preview_h;
    fill_rect(sdl_r, ix, iy, iw, panel_total_h, 0x1A1A1AE8);

    /* Title */
    draw_text_s(sdl_r, label_x, iy + 2, "-- COLLISION TILE --", 0xFFFF80FF, s);
    iy += row_h + 4;

    char buf[64];

    /* Attribute */
    draw_text_s(sdl_r, label_x, iy, "Attr:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "0x%02X (%d)", editor->selected_coll_attr, editor->selected_coll_attr);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* Position */
    draw_text_s(sdl_r, label_x, iy, "Col:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "%d", editor->selected_coll_col);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    draw_text_s(sdl_r, label_x, iy, "Row:", COL_TEXT_DIM, s);
    snprintf(buf, sizeof(buf), "%d", editor->selected_coll_row);
    draw_text_s(sdl_r, value_x, iy, buf, COL_TEXT_WHITE, s);
    iy += row_h;

    /* Type */
    draw_text_s(sdl_r, label_x, iy, "Type:", COL_TEXT_DIM, s);
    draw_text_s(sdl_r, value_x, iy, collision_attr_type_name(editor->selected_coll_attr), COL_TEXT_WHITE, s);
    iy += row_h;

    /* Color swatch */
    uint32_t swatch = collision_attr_color(editor->selected_coll_attr) | 0xFF;
    fill_rect(sdl_r, label_x, iy + 1, 4 * s, row_h - 2, swatch);

    /* Mask preview in collision inspector */
    if (editor->mask_editor && editor->game_state_ref && editor->selected_coll_attr != 0) {
        iy += 2;
        int mask_scale = s * 2;
        int mask_size = 8 * mask_scale;
        mask_editor_render_preview(editor->mask_editor, editor->game_state_ref,
                                    sdl_r, label_x, iy, mask_scale,
                                    editor->selected_coll_attr);

        /* "M: Edit" label next to preview */
        draw_text_s(sdl_r, label_x + mask_size + pad, iy + mask_size / 2 - 3 * s,
                   "M: Edit", 0x80C0FFFF, s);
        iy += mask_size + 4;
    } else {
        iy += row_h;
    }

    /* Separator */
    iy += 4;
    draw_line(sdl_r, ix + 4, iy, ix + iw - 4, iy, 0x606060FF);
    iy += 6;

    /* Hints */
    draw_text_s(sdl_r, label_x, iy, "Drag: Move", 0x808080FF, s);
    iy += row_h;
    draw_text_s(sdl_r, label_x, iy, "Click empty: Desel", 0x808080FF, s);
}

/*=============================================================================
 * Render: Status Bar — scaled
 *===========================================================================*/

static void render_status_bar(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    int s = editor->ui_scale;
    int bar_h = 7 * s + 6;
    int bar_y = win_h - bar_h;

    fill_rect(sdl_r, 0, bar_y, win_w, bar_h, 0x181818FF);

    /* Left side: context info */
    char buf[64];
    if (editor->current_tool == TOOL_TILE_PAINT) {
        snprintf(buf, sizeof(buf), "TILE:%d P:%d",
                 editor->paint_tile_index, editor->paint_tile_palette);
    } else if (editor->current_tool == TOOL_COLL_PAINT) {
        snprintf(buf, sizeof(buf), "COLL:0x%02X", editor->paint_coll_attr);
    } else {
        snprintf(buf, sizeof(buf), "Objs:%d", editor->table.num_objects);
    }
    draw_text_s(sdl_r, 4, bar_y + 3, buf, COL_TEXT_WHITE, s);

    /* Right side: coords + zoom + status */
    float world_x = editor->camera_x + (float)editor->mouse_x / editor->zoom;
    float world_y = editor->camera_y + (float)editor->mouse_y / editor->zoom;
    snprintf(buf, sizeof(buf), "%.0fx (%d,%d)", editor->zoom, (int)world_x, (int)world_y);
    draw_text_s(sdl_r, win_w - text_width_s(buf, s) - 4, bar_y + 3, buf, COL_TEXT_WHITE, s);

    /* Save flash / dirty indicator (center area) */
    if (editor->save_flash_timer > 0) {
        uint32_t flash_col = (editor->save_flash_timer % 20 < 15) ? 0x40FF40FF : 0x80FFA0FF;
        int cx = (win_w - text_width_s("SAVED!", s)) / 2;
        draw_text_s(sdl_r, cx, bar_y + 3, "SAVED!", flash_col, s);
    } else if (editor->table.dirty) {
        int cx = (win_w - text_width_s("[MOD]", s)) / 2;
        draw_text_s(sdl_r, cx, bar_y + 3, "[MOD]", 0xFF8080FF, s);
    }
}

/*=============================================================================
 * Render: Help Bar — scaled (full UI_SCALE size)
 *===========================================================================*/

static void render_help_bar(EditorState *editor, SDL_Renderer *sdl_r, int win_w) {
    int s = editor->ui_scale;
    int bar_h = 7 * s + 6;

    fill_rect(sdl_r, 0, 0, win_w, bar_h, 0x181818E0);

    /* Two rows of hints at full scale, stacked if needed */
    if (editor->current_tool == TOOL_PALETTE) {
        draw_text_s(sdl_r, 4, 3, "^S:Save  Arrows:Edit  Tab:Scope",
            0xA0A0A0FF, s);
    } else if (editor->current_tool == TOOL_COLL_PAINT) {
        draw_text_s(sdl_r, 4, 3, "^S:Save ^Z:Undo F5:Play  Alt:Sample  M:Mask  Q:Pts",
            0xA0A0A0FF, s);
    } else {
        draw_text_s(sdl_r, 4, 3, "^S:Save ^Z:Undo F5:Play  L:Pal",
            0xA0A0A0FF, s);
    }
}

/*=============================================================================
 * Render: Table Picker Screen
 *===========================================================================*/

static void render_picker_screen(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    int s = editor->ui_scale;
    int char_h = 6 * s;
    int item_h = char_h + 6 * s;  /* Text + generous padding */
    int pad = 8 * s;

    /* Background */
    set_draw_color(sdl_r, COL_PICKER_BG);
    SDL_RenderClear(sdl_r);

    /* Title */
    const char *title = "STAGE BUILDER - Select Table";
    int title_x = (win_w - text_width_s(title, s + 1)) / 2;
    draw_text_s(sdl_r, title_x, pad, title, COL_PICKER_TITLE, s + 1);

    /* Subtitle */
    const char *sub = "Arrow keys to navigate, Enter to open, F12 to exit";
    int sub_x = (win_w - text_width_s(sub, s)) / 2;
    draw_text_s(sdl_r, sub_x, pad + 9 * s, sub, COL_TEXT_DIM, s);

    /* Table list */
    int list_y = pad + 20 * s;
    int list_x = win_w / 6;
    int list_w = win_w * 4 / 6;

    if (editor->picker_num_tables == 0) {
        const char *msg = "No tables found in tables/ directory";
        int msg_x = (win_w - text_width_s(msg, s)) / 2;
        draw_text_s(sdl_r, msg_x, list_y + item_h, msg, 0xFF8080FF, s);
        return;
    }

    for (int i = 0; i < editor->picker_num_tables; i++) {
        PickerEntry *entry = &editor->picker_tables[i];
        int iy = list_y + i * item_h;

        if (iy + item_h > win_h - pad) break;

        bool selected = (i == editor->picker_cursor);
        bool is_new_table = (entry->folder[0] == '\0');  /* [+ New Table] entry */

        /* Background bar */
        uint32_t bg = selected ? COL_PICKER_SEL : COL_PICKER_ITEM;
        if (is_new_table) bg = selected ? 0x205020FF : 0x183018FF;  /* Green tinted */
        fill_rect(sdl_r, list_x, iy, list_w, item_h - 2 * s, bg);

        /* Selection indicator */
        if (selected) {
            uint32_t indicator_col = is_new_table ? 0x40FF40FF : COL_PICKER_TITLE;
            fill_rect(sdl_r, list_x, iy, 3 * s, item_h - 2 * s, indicator_col);
        }

        /* Table name */
        int text_x = list_x + 5 * s;
        int text_y = iy + 2 * s;
        uint32_t name_color;
        if (is_new_table) {
            name_color = selected ? 0x80FF80FF : 0x40C040FF;  /* Green text */
        } else {
            name_color = selected ? COL_TEXT_WHITE : COL_TEXT_DIM;
        }
        draw_text_s(sdl_r, text_x, text_y, entry->name, name_color, s);

        /* Folder name (dimmer) — skip for [+ New Table] */
        if (!is_new_table) {
            char folder_str[80];
            snprintf(folder_str, sizeof(folder_str), "(%s%s)",
                     entry->folder_name, entry->is_builtin ? " - builtin" : "");
            draw_text_s(sdl_r, text_x + text_width_s(entry->name, s) + 4 * s, text_y,
                        folder_str, 0x606060FF, s);
        }
    }

    /* Footer */
    int footer_y = win_h - pad - char_h;
    char footer[64];
    snprintf(footer, sizeof(footer), "%d table(s) found", editor->picker_num_tables);
    int footer_x = (win_w - text_width_s(footer, s)) / 2;
    draw_text_s(sdl_r, footer_x, footer_y, footer, 0x606060FF, s);
}

/*=============================================================================
 * Main Viewport Render — dispatches based on editor screen
 *===========================================================================*/

void editor_render_viewport(EditorState *editor, Renderer *renderer, Platform *platform) {
    if (!editor || !editor->active) return;

    SDL_Renderer *sdl_r = (SDL_Renderer *)platform_get_sdl_renderer(platform);
    if (!sdl_r) return;

    int win_w, win_h;
    SDL_GetRendererOutputSize(sdl_r, &win_w, &win_h);

    if (editor->screen == EDITOR_SCREEN_PICKER) {
        render_picker_screen(editor, sdl_r, win_w, win_h);
        SDL_RenderPresent(sdl_r);
        return;
    }

    /* Editor viewport (EDITOR_SCREEN_EDITOR) */

    /* Center view on first frame after table open */
    if (editor->needs_center_view) {
        int cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;
        int rows = editor_display_rows(editor);
        float stage_w = (float)(cols * 8);
        float stage_h = (float)(rows * 8);
        float view_w = (float)win_w / editor->zoom;
        float view_h = (float)win_h / editor->zoom;
        editor->camera_x = (stage_w / 2.0f) - (view_w / 2.0f);
        editor->camera_y = (stage_h / 2.0f) - (view_h / 2.0f);
        editor->needs_center_view = false;
    }

    /* Clear to dark gray */
    set_draw_color(sdl_r, COL_BG_DARK);
    SDL_RenderClear(sdl_r);

    /* Render layers */
    render_tilemap_preview(editor, sdl_r, editor->vram_ref, editor->table.bg_palettes);
    render_palette_highlight(editor, sdl_r);
    render_collision_overlay(editor, sdl_r);
    render_flipper_sweep(editor, sdl_r);
    render_mask_edit_highlight(editor, sdl_r);
    render_fill_preview(editor, sdl_r);
    render_viewport_indicators(editor, sdl_r);
    render_grid(editor, sdl_r);
    render_objects(editor, sdl_r);
    render_placement_ghost(editor, sdl_r);

    /* Ball test point overlay (before UI panels, on top of collision overlay) */
    if (editor->mask_editor && editor->game_state_ref) {
        mask_editor_render_test_points(editor->mask_editor, editor->game_state_ref,
                                        editor, sdl_r);
    }

    /* UI panels */
    render_left_panel(editor, sdl_r, win_w, win_h);
    render_sidebar(editor, sdl_r, win_w, win_h);
    render_inspector(editor, sdl_r, win_w, win_h);
    render_collision_inspector(editor, sdl_r, win_w, win_h);
    render_checklist(editor, sdl_r, win_w, win_h);
    render_collision_tooltip(editor, sdl_r);
    render_tile_inspector(editor, sdl_r, win_w, win_h);
    render_status_bar(editor, sdl_r, win_w, win_h);
    render_help_bar(editor, sdl_r, win_w);

    /* Mask pixel editor panel (modal, drawn on top of everything) */
    if (editor->mask_editor) {
        mask_editor_render_panel(editor->mask_editor, sdl_r, editor, win_w, win_h);
    }

    SDL_RenderPresent(sdl_r);
}
