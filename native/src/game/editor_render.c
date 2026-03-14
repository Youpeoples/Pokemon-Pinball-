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
#include "game/game_state.h"
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

        /* Bottom stage viewport (SCX=default_scx, display rows 18-35) */
        float bottom_y = 18.0f * 8.0f;  /* Display row 18 */
        int bot_vp_x = world_to_screen_x(editor, (float)editor->table.default_scx);
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
 * Render: Collision Tooltip (shown when hovering over collision tiles)
 *===========================================================================*/

static void render_collision_tooltip(EditorState *editor, SDL_Renderer *sdl_r) {
    if (!editor->show_collision_overlay || editor->hovered_coll_col < 0) return;

    int s = editor->ui_scale;
    uint8_t attr = editor->hovered_coll_attr;
    int row_h = 7 * s;
    int pad = 4;

    /* Format tooltip lines */
    char line1[32], line2[32], line3[32];
    snprintf(line1, sizeof(line1), "Attr: 0x%02X (%d)", attr, attr);
    snprintf(line2, sizeof(line2), "Pos: (%d, %d)", editor->hovered_coll_col, editor->hovered_coll_row);
    snprintf(line3, sizeof(line3), "Type: %s", collision_attr_type_name(attr));

    /* Compute tooltip size */
    int w1 = text_width_s(line1, s);
    int w2 = text_width_s(line2, s);
    int w3 = text_width_s(line3, s);
    int max_w = w1;
    if (w2 > max_w) max_w = w2;
    if (w3 > max_w) max_w = w3;
    int swatch_size = 3 * s;
    int tooltip_w = max_w + swatch_size + pad * 4;
    int tooltip_h = row_h * 3 + pad * 3;

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
 * Render: Sidebar (Component Palette) — scaled
 *===========================================================================*/

#define SIDEBAR_BASE_W  200  /* Fixed sidebar width in screen pixels */

static void render_sidebar(EditorState *editor, SDL_Renderer *sdl_r, int win_w, int win_h) {
    int s = editor->ui_scale;
    int sidebar_w = SIDEBAR_BASE_W;
    int item_h = 7 * s + 4;   /* Font height (6*s) + padding */
    int pad = 4;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);

    int sx = win_w - sidebar_w;
    fill_rect(sdl_r, sx, 0, sidebar_w, win_h, COL_SIDEBAR_BG);

    /* Title */
    draw_text_s(sdl_r, sx + pad, pad, "COMPONENTS", COL_TEXT_WHITE, s);

    /* Tool mode indicator */
    const char *tool_name = "SELECT";
    switch (editor->current_tool) {
        case TOOL_SELECT:     tool_name = "SELECT"; break;
        case TOOL_PLACE:      tool_name = "PLACE"; break;
        case TOOL_TILE_PAINT: tool_name = "TILE"; break;
        case TOOL_COLL_PAINT: tool_name = "COLLISION"; break;
        case TOOL_ERASE:      tool_name = "ERASE"; break;
    }
    draw_text_s(sdl_r, sx + pad, pad + 7 * s, tool_name, 0xFFFF80FF, s);

    /* Component list */
    int y = pad + 16 * s;
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
        if (y + item_h > win_h) break;
    }

    /* Check for sidebar clicks */
    if (editor->mouse_left_clicked && editor->mouse_x >= sx) {
        int list_start_y = pad + 16 * s;
        int click_y = editor->mouse_y - list_start_y;
        if (click_y >= 0) {
            int idx = click_y / item_h + 1;
            if (idx >= 1 && idx < COMP_COUNT) {
                editor->palette_selection = (ComponentType)idx;
                editor->current_tool = TOOL_PLACE;
            }
        }
    }
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
    int iw = 200;
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

    int s = editor->ui_scale;
    int row_h = 7 * s + 2;
    int pad = 4;
    int iw = 200;
    int ix = 0;
    int iy = 8 * s;
    int label_x = ix + pad;
    int value_x = ix + 40 * s;

    SDL_SetRenderDrawBlendMode(sdl_r, SDL_BLENDMODE_BLEND);
    fill_rect(sdl_r, ix, iy, iw, row_h * 6 + pad * 4, 0x1A1A1AE8);

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

    /* Separator */
    iy += row_h + 4;
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
    int bar_h = 8 * s;
    int bar_y = win_h - bar_h;
    int sidebar_w = SIDEBAR_BASE_W;

    fill_rect(sdl_r, 0, bar_y, win_w, bar_h, 0x181818FF);

    char buf[128];
    if (editor->current_tool == TOOL_TILE_PAINT) {
        snprintf(buf, sizeof(buf), "TILE: idx=%d pal=%d  [/]change  Zoom:%.1fx",
                 editor->paint_tile_index, editor->paint_tile_palette, editor->zoom);
    } else if (editor->current_tool == TOOL_COLL_PAINT) {
        snprintf(buf, sizeof(buf), "COLL: attr=0x%02X  0-9 change  Zoom:%.1fx",
                 editor->paint_coll_attr, editor->zoom);
    } else if (editor->combined_view) {
        snprintf(buf, sizeof(buf), "Top:0x%02X Bot:0x%02X SCX:%d Objs:%d Zoom:%.1fx%s",
                 editor->top_stage_id, editor->bottom_stage_id,
                 editor->table.default_scx, editor->table.num_objects, editor->zoom,
                 editor->hide_buffer_rows ? "" : " [full]");
    } else {
        snprintf(buf, sizeof(buf), "Stage:0x%02X Objs:%d Zoom:%.1fx",
                 editor->table.stage_id, editor->table.num_objects, editor->zoom);
    }
    draw_text_s(sdl_r, 4, bar_y + s, buf, COL_TEXT_WHITE, s);

    /* Mouse world position */
    float world_x = editor->camera_x + (float)editor->mouse_x / editor->zoom;
    float world_y = editor->camera_y + (float)editor->mouse_y / editor->zoom;
    snprintf(buf, sizeof(buf), "(%d,%d)", (int)world_x, (int)world_y);
    draw_text_s(sdl_r, win_w - sidebar_w - text_width_s(buf, s) - 4, bar_y + s, buf, COL_TEXT_WHITE, s);

    /* Save flash indicator */
    if (editor->save_flash_timer > 0) {
        /* Flash green "SAVED!" prominently */
        uint32_t flash_col = (editor->save_flash_timer % 20 < 15) ? 0x40FF40FF : 0x80FFA0FF;
        draw_text_s(sdl_r, win_w - sidebar_w - text_width_s("SAVED!", s) - text_width_s(buf, s) - 16,
                    bar_y + s, "SAVED!", flash_col, s);
    } else if (editor->table.dirty) {
        /* Dirty indicator */
        draw_text_s(sdl_r, win_w - sidebar_w - text_width_s("[MOD]", s) - text_width_s(buf, s) - 12,
                    bar_y + s, "[MOD]", 0xFF8080FF, s);
    }
}

/*=============================================================================
 * Render: Help Bar — scaled
 *===========================================================================*/

static void render_help_bar(EditorState *editor, SDL_Renderer *sdl_r, int win_w) {
    int s = editor->ui_scale;
    int bar_h = 7 * s + 2;
    int sidebar_w = SIDEBAR_BASE_W;

    fill_rect(sdl_r, 0, 0, win_w - sidebar_w, bar_h, 0x181818E0);
    if (editor->show_collision_overlay && editor->current_tool == TOOL_SELECT) {
        draw_text_s(sdl_r, 4, s,
            "^S:Save ^Z:Undo F5:Play  Hover:Info Click:Select Drag:Move",
            0xA0A0A0FF, s);
    } else {
        draw_text_s(sdl_r, 4, s,
            "^S:Save ^Z:Undo F5:Play G:Grid C:Coll B:Bounds H:Buf S:Sel P:Place T:Tile X:Coll",
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

        /* Background bar */
        uint32_t bg = selected ? COL_PICKER_SEL : COL_PICKER_ITEM;
        fill_rect(sdl_r, list_x, iy, list_w, item_h - 2 * s, bg);

        /* Selection indicator */
        if (selected) {
            fill_rect(sdl_r, list_x, iy, 3 * s, item_h - 2 * s, COL_PICKER_TITLE);
        }

        /* Table name */
        int text_x = list_x + 5 * s;
        int text_y = iy + 2 * s;
        draw_text_s(sdl_r, text_x, text_y, entry->name,
                    selected ? COL_TEXT_WHITE : COL_TEXT_DIM, s);

        /* Folder name (dimmer) */
        char folder_str[80];
        snprintf(folder_str, sizeof(folder_str), "(%s%s)",
                 entry->folder_name, entry->is_builtin ? " - builtin" : "");
        draw_text_s(sdl_r, text_x + text_width_s(entry->name, s) + 4 * s, text_y,
                    folder_str, 0x606060FF, s);
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
    render_collision_overlay(editor, sdl_r);
    render_viewport_indicators(editor, sdl_r);
    render_grid(editor, sdl_r);
    render_objects(editor, sdl_r);
    render_placement_ghost(editor, sdl_r);

    /* UI panels */
    render_sidebar(editor, sdl_r, win_w, win_h);
    render_inspector(editor, sdl_r, win_w, win_h);
    render_collision_inspector(editor, sdl_r, win_w, win_h);
    render_collision_tooltip(editor, sdl_r);
    render_status_bar(editor, sdl_r, win_w, win_h);
    render_help_bar(editor, sdl_r, win_w);

    SDL_RenderPresent(sdl_r);
}
