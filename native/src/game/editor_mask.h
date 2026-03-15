/*
 * Editor Mask System
 *
 * Collision mask preview, pixel editor, flipper angle tabs,
 * ball test point overlay, and custom mask atlas export.
 *
 * Integrates with the stage builder editor (editor.h/editor_render.c).
 */

#ifndef EDITOR_MASK_H
#define EDITOR_MASK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
typedef struct EditorState EditorState;
typedef struct GameState GameState;
typedef struct MaskEditorState MaskEditorState;

/*=============================================================================
 * Constants
 *===========================================================================*/
#define MASK_SIZE           8       /* 8x8 pixel masks */
#define MASK_BYTES          8       /* 1bpp: 1 byte per row */
#define MASK_CELL_SIZE     16       /* Screen pixels per mask pixel in editor */
#define MAX_MASKS         256       /* Total collision attribute masks (0x00-0xFF) */
#define FLIPPER_MASK_COUNT 16       /* 16 flipper mask shapes per angle set */
#define FLIPPER_ANGLE_SETS  3       /* 0-6, 7-13, 14+ degrees */
#define MASK_UNDO_DEPTH    32       /* Undo history size per mask */

/*=============================================================================
 * Mask Cache - Avoids repeated PNG decoding
 *===========================================================================*/
typedef struct {
    uint8_t  *data;           /* 1bpp mask data (num_masks * 8 bytes) */
    size_t    data_size;      /* Total bytes */
    int       num_masks;      /* Number of masks loaded */
    char      source_path[260]; /* PNG path this was loaded from */
    bool      loaded;
} MaskCacheEntry;

typedef struct {
    /* Per-stage mask cache (main masks) */
    MaskCacheEntry stage_masks[16];    /* Indexed by stage ID */

    /* Bottom flipper mask caches */
    MaskCacheEntry bottom_left;        /* Left flipper masks (3 angle sets) */
    MaskCacheEntry bottom_right;       /* Right flipper masks (3 angle sets) */
    MaskCacheEntry bottom_left_bonus;  /* Left flipper masks for bonus stages */
    MaskCacheEntry bottom_right_bonus; /* Right flipper masks for bonus stages */
} MaskCache;

/*=============================================================================
 * Mask Editor State
 *===========================================================================*/
struct MaskEditorState {
    /* Editor panel visibility */
    bool active;                       /* Mask pixel editor panel is open */

    /* Which mask we're editing */
    uint8_t edit_attr;                 /* Collision attribute being edited */
    uint8_t pixels[MASK_SIZE][MASK_SIZE]; /* Current editable mask (0=passable, 1=solid) */

    /* Flipper mask angle tab state */
    bool is_flipper_mask;              /* True if edit_attr >= 0xE0 */
    bool is_right_flipper;             /* True if edit_attr >= 0xF0 */
    int  flipper_angle_tab;            /* 0=0-6, 1=7-13, 2=14+ */
    uint8_t flipper_pixels[FLIPPER_ANGLE_SETS][MASK_SIZE][MASK_SIZE];

    /* Mouse interaction */
    bool painting;                     /* Left/right button held while painting */
    bool paint_value;                  /* True=solid, false=passable */

    /* Undo for the current mask */
    uint8_t undo_stack[MASK_UNDO_DEPTH][MASK_BYTES];
    int     undo_count;
    int     undo_pos;                  /* Current position in undo ring */

    /* Modified mask tracking */
    bool    mask_modified[MAX_MASKS];  /* True if mask has been edited */
    uint8_t custom_masks[MAX_MASKS][MASK_BYTES]; /* In-memory edited masks */
    bool    any_modified;              /* True if any mask has been edited */

    /* Flipper masks: modified tracking
     * Layout: [angle_set * FLIPPER_MASK_COUNT + mask_index] per side */
    bool    flipper_left_modified[FLIPPER_ANGLE_SETS * FLIPPER_MASK_COUNT];
    uint8_t flipper_left_custom[FLIPPER_ANGLE_SETS * FLIPPER_MASK_COUNT][MASK_BYTES];
    bool    flipper_right_modified[FLIPPER_ANGLE_SETS * FLIPPER_MASK_COUNT];
    uint8_t flipper_right_custom[FLIPPER_ANGLE_SETS * FLIPPER_MASK_COUNT][MASK_BYTES];
    bool    any_flipper_modified;

    /* Ball test point overlay */
    bool show_test_points;             /* Toggle with T key */

    /* Editor stage context (used instead of state->current_stage) */
    uint8_t editor_stage_id;           /* Stage ID of the table being edited */

    /* Mask cache */
    MaskCache cache;
};

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

/* Create and initialize a MaskEditorState */
MaskEditorState *mask_editor_create(void);

/* Free all resources */
void mask_editor_free(MaskEditorState *me);

/*=============================================================================
 * Mask Cache Operations
 *===========================================================================*/

/* Load and cache masks for the given stage. Returns pointer to cached data. */
const uint8_t *mask_cache_get_stage(MaskEditorState *me, GameState *state,
                                     uint8_t stage_id, int *out_num_masks);

/* Load and cache flipper masks for bottom stages */
const uint8_t *mask_cache_get_flipper(MaskEditorState *me, GameState *state,
                                       bool is_right, bool is_bonus,
                                       size_t *out_size);

/* Invalidate all cached masks (e.g., after stage change) */
void mask_cache_invalidate(MaskEditorState *me);

/*=============================================================================
 * Mask Data Access
 *===========================================================================*/

/* Get the 8-byte mask data for a collision attribute.
 * Returns custom mask if modified, else loads from cache/PNG.
 * out_mask: 8-byte buffer to fill with 1bpp mask rows. */
void mask_editor_get_mask(MaskEditorState *me, GameState *state,
                          uint8_t attr, uint8_t out_mask[MASK_BYTES]);

/* Get flipper mask for a specific angle set.
 * mask_index: 0-15 (the lower nybble of attr - 0xE0)
 * angle_set: 0, 1, or 2 */
void mask_editor_get_flipper_mask(MaskEditorState *me, GameState *state,
                                   uint8_t mask_index, bool is_right,
                                   int angle_set, uint8_t out_mask[MASK_BYTES]);

/*=============================================================================
 * Pixel Editor Operations
 *===========================================================================*/

/* Open the mask pixel editor for the given attribute */
void mask_editor_open(MaskEditorState *me, GameState *state, uint8_t attr);

/* Close the mask pixel editor, applying changes */
void mask_editor_close(MaskEditorState *me);

/* Apply current pixel grid back to custom_masks storage */
void mask_editor_apply(MaskEditorState *me);

/* Push current state to undo stack */
void mask_editor_push_undo(MaskEditorState *me);

/* Undo last edit */
void mask_editor_undo(MaskEditorState *me);

/* Redo (if available) */
void mask_editor_redo(MaskEditorState *me);

/* Tool operations on current mask */
void mask_editor_fill(MaskEditorState *me);      /* Fill all solid */
void mask_editor_clear(MaskEditorState *me);     /* Clear all passable */
void mask_editor_mirror_h(MaskEditorState *me);  /* Mirror horizontally */
void mask_editor_mirror_v(MaskEditorState *me);  /* Mirror vertically */
void mask_editor_rotate_90(MaskEditorState *me); /* Rotate 90 degrees CW */

/*=============================================================================
 * Rendering
 *===========================================================================*/

/* Render inline mask preview in tooltip (called from render_collision_tooltip) */
void mask_editor_render_preview(MaskEditorState *me, GameState *state,
                                 void *sdl_renderer, int x, int y,
                                 int scale, uint8_t attr);

/* Render the mask pixel editor panel */
void mask_editor_render_panel(MaskEditorState *me, void *sdl_renderer,
                               EditorState *editor, int win_w, int win_h);

/* Render ball test point overlay on the viewport */
void mask_editor_render_test_points(MaskEditorState *me, GameState *state,
                                     EditorState *editor, void *sdl_renderer);

/* Handle mouse input for the mask editor panel.
 * Returns true if the event was consumed (panel is open). */
bool mask_editor_handle_input(MaskEditorState *me, EditorState *editor,
                               int mouse_x, int mouse_y,
                               bool left_click, bool right_click,
                               bool left_down, bool right_down,
                               int win_w, int win_h);

/*=============================================================================
 * Custom Mask Export
 *===========================================================================*/

/* Export all modified masks to a PNG atlas file.
 * Writes to data/custom_masks.png (256 masks in 16x16 grid of 8x8 tiles = 128x128 px).
 * Returns true on success. */
bool mask_editor_export_atlas(MaskEditorState *me, GameState *state);

/* Generate Lua snippet for runtime override.
 * Writes to the table's scripts/ folder. */
bool mask_editor_export_lua_snippet(MaskEditorState *me, const char *table_folder);

#endif /* EDITOR_MASK_H */
