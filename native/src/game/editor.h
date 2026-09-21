/*
 * Stage Builder / Editor Mode
 *
 * In-game editor for creating and modifying pinball tables.
 * F12 opens editor from any screen. Table picker lets you choose which
 * table folder to edit. Game state is snapshot/restored on entry/exit.
 *
 * Architecture:
 *   editor.h/.c       - Core state, lifecycle, mode toggle
 *   editor_render.h/.c - Viewport camera, rendering, UI
 *   editor_serialize.h/.c - Export EditorTable to Lua files + manifest
 */

#ifndef EDITOR_H
#define EDITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "game/types.h"
#include "game/constants.h"

/* Forward declarations */
typedef struct GameState GameState;
typedef struct Platform Platform;
typedef struct Renderer Renderer;
typedef struct VirtualVRAM VirtualVRAM;
typedef struct MaskEditorState MaskEditorState;

/*=============================================================================
 * Editor Constants
 *===========================================================================*/
#define MAX_EDITOR_OBJECTS    64
#define MAX_EDITOR_UNDO      64
#define EDITOR_GRID_SIZE      8    /* Default grid snap: 8px (1 tile) */
#define MAX_PICKER_TABLES    16
#define UI_SCALE              4    /* Scale factor for all UI text/panels */
#define EDITOR_SIDEBAR_W    360    /* Sidebar width in screen pixels */
#define MAX_OBJECT_LINKS      8    /* Max outgoing links per object */
#define MAX_TABLE_LINKS     128    /* Max total links in a table */
/* GBC collision map has 2 padding rows above the visible tilemap.
 * Object/ball coordinates are in game space (16px above tilemap origin).
 * Subtract this from obj->y to get the tilemap-aligned display position. */
#define OBJECT_Y_DISPLAY_OFFSET 16

/* Component type IDs */
typedef enum {
    COMP_NONE = 0,
    COMP_BUMPER,           /* Voltorb / Shellder (14x14 bbox) */
    COMP_SPINNER,          /* Spinner (8x4 bbox) */
    COMP_BALL_UPGRADE,     /* Ball upgrade trigger (6x5 bbox) */
    COMP_PIKACHU_SAVER,    /* Pikachu saver (3x5 bbox) */
    COMP_CAVE_LIGHT,       /* CAVE light (5x3 bbox) */
    COMP_DIGLETT,          /* Diglett map counter (8x12 bbox) */
    COMP_FIELD_CREATURE,   /* Bellsprout/Cloyster/Slowpoke (6x5 bbox) */
    COMP_STARYU,           /* Staryu (8x6 bbox) */
    COMP_SLOT_MACHINE,     /* Slot machine (4x4 bbox) */
    COMP_RAILING,          /* Bonus multiplier railing (7x7 bbox) */
    COMP_LAUNCH_ALLEY,     /* Ball launch area (8x8 bbox) */
    COMP_WILD_POKEMON,     /* Wild mon target (26x26 bbox) */
    COMP_BOARD_TRIGGER,    /* Board trigger (9x9 bbox) */
    COMP_DITTO_SLOT,       /* Ditto slot for evolution (3x3 bbox) */
    COMP_COUNT
} ComponentType;

/* Property types for the inspector */
typedef enum {
    PROP_INT,
    PROP_HEX,
    PROP_BOOL,
    PROP_STRING,
    PROP_COUNT
} PropertyType;

/* Link relationship types between objects */
typedef enum {
    LINK_NONE = 0,
    LINK_TRIGGERS,       /* A hit fires B's action once */
    LINK_CHARGES,        /* A hit increments B's counter, fires at threshold */
    LINK_TOGGLES,        /* A hit toggles B's on/off state */
    LINK_SEQUENCE,       /* A must be hit before B becomes active (ordered chain) */
    LINK_COUNT
} LinkType;

/* A directed link between two objects */
typedef struct {
    int source_idx;          /* Index in table.objects[] */
    int target_idx;          /* Index in table.objects[] */
    LinkType type;
    int threshold;           /* For CHARGES: how many hits to activate target */
    int timer_frames;        /* 0 = permanent, >0 = deactivate after N frames */
} ObjectLink;

/*=============================================================================
 * Editor Screen (which phase of the editor we're in)
 *===========================================================================*/
typedef enum {
    EDITOR_SCREEN_PICKER,    /* Table picker: choose which table to edit */
    EDITOR_SCREEN_EDITOR,    /* Main editor viewport */
} EditorScreen;

/*=============================================================================
 * Table Picker Entry
 *===========================================================================*/
typedef struct {
    char name[64];           /* Display name from manifest */
    char folder[128];        /* Full path to table folder */
    char folder_name[64];    /* Just the folder name */
    bool is_builtin;
} PickerEntry;

/*=============================================================================
 * Property Definition (schema for component properties)
 *===========================================================================*/
typedef struct {
    const char *name;
    PropertyType type;
    int default_value;
    const char *description;
} PropertyDef;

/*=============================================================================
 * Editor Object (placed component on the table)
 *===========================================================================*/
typedef struct {
    ComponentType type;
    uint16_t x, y;              /* World position (uint16_t for combined view) */
    uint8_t x_thresh, y_thresh; /* Bounding box half-widths */
    bool attribute_gated;
    uint8_t attrs[16];          /* Collision attributes (if gated) */
    int num_attrs;
    int score;                  /* Points on hit */
    int bounce_force;           /* Force applied on collision (hex) */
    int sfx_id;                 /* Sound effect ID */
    char custom_lua[256];       /* Optional custom Lua handler */
    bool selected;
} EditorObject;

/*=============================================================================
 * Editor Table (complete editable stage data)
 *===========================================================================*/
typedef struct {
    char name[64];
    char source_folder[128];           /* Folder this was loaded from (for save) */
    uint8_t tilemap[64][32];           /* BG tile indices (top 0-17, bottom 18-35 for combined) */
    uint8_t tilemap_attrs[64][32];     /* Palette, flip, bank */
    uint8_t collision_map[64][32];     /* Collision attribute IDs */
    int tilemap_rows;                  /* 18 (single visible) or 36 (combined top+bottom visible) */
    int tilemap_cols;                  /* 20 (visible GBC screen width in tiles) */
    GBCPalette bg_palettes[8];
    GBCPalette obj_palettes[8];
    EditorObject objects[MAX_EDITOR_OBJECTS];
    int num_objects;
    ObjectLink links[MAX_TABLE_LINKS];
    int num_links;
    uint8_t default_scx;
    bool has_flippers;
    uint8_t stage_id;
    bool dirty;                        /* Unsaved changes */
    bool unsigned_addressing;          /* LCDC bit 4: tile addressing mode */
    bool has_vram;                     /* True if VRAM tile data is available */
} EditorTable;

/*=============================================================================
 * Editor Tool Mode
 *===========================================================================*/
typedef enum {
    TOOL_SELECT,       /* Click to select/move objects */
    TOOL_PLACE,        /* Click to place from palette */
    TOOL_TILE_PAINT,   /* Paint tilemap */
    TOOL_COLL_PAINT,   /* Paint collision attributes */
    TOOL_ERASE,        /* Delete objects */
    TOOL_PALETTE,      /* Edit palette colors */
} EditorTool;

/*=============================================================================
 * Editor State (master struct)
 *===========================================================================*/
typedef struct EditorState {
    /* Which screen we're on */
    EditorScreen screen;

    /* UI scale factor */
    int ui_scale;

    /* Table picker */
    PickerEntry picker_tables[MAX_PICKER_TABLES];
    int picker_num_tables;
    int picker_cursor;
    bool picker_scanned;

    /* Viewport camera */
    float camera_x, camera_y;     /* World position (top-left corner) */
    float zoom;                    /* 1.0 = 1 tile = 8 screen pixels */
    float target_zoom;             /* Smooth zoom target */
    int drag_start_x, drag_start_y;  /* For pan dragging */
    bool panning;

    /* Grid */
    bool show_grid;
    bool snap_to_grid;
    int grid_size;                 /* 8 = tile, 1 = pixel */

    /* Overlays */
    bool show_collision_overlay;
    bool show_object_bounds;
    bool show_tile_inspector;
    bool show_link_overlay;        /* Toggle with I key (Interactions) */

    /* Current tool */
    EditorTool current_tool;
    ComponentType palette_selection;   /* Which component to place */

    /* Selection */
    int selected_object;           /* Index into table.objects, -1 = none */
    bool dragging_object;
    int drag_offset_x, drag_offset_y;

    /* Link creation mode */
    int link_mode_source;          /* Object index when in link-creation mode, -1 = inactive */
    bool linking;                  /* True = waiting for target click */
    int selected_link;             /* Index of selected link for editing, -1 = none */
    int link_type_cycle;           /* Cycles through LinkType on repeated L presses */

    /* Template placement */
    bool template_picker_open;     /* True = template picker popup visible */
    int template_cursor;           /* Selected template in picker */
    bool template_placing;         /* True = ghost preview follows mouse */
    int template_selected;         /* Which template is being placed */

    /* Collision tile hover state */
    int hovered_coll_col;          /* Tile column under mouse (-1 = none) */
    int hovered_coll_row;          /* Display row under mouse (-1 = none) */
    uint8_t hovered_coll_attr;     /* Collision attribute at hovered tile */

    /* Collision tile selection */
    int selected_coll_col;         /* Selected collision tile column (-1 = none) */
    int selected_coll_row;         /* Selected collision tile display row (-1 = none) */
    uint8_t selected_coll_attr;    /* Attribute value of selected tile */
    bool dragging_coll_tile;       /* True when drag-moving a collision tile */
    int drag_coll_src_col;         /* Source column for drag-move */
    int drag_coll_src_row;         /* Source display row for drag-move */

    /* Editor table data */
    EditorTable table;

    /* Playtest state */
    bool playtesting;

    /* Playtest collision injection */
    uint8_t playtest_collision_top[64][32];    /* Editor collision for top stage */
    uint8_t playtest_collision_bottom[64][32]; /* Editor collision for bottom stage */
    bool playtest_has_collision;               /* True = override collision after load */

    /* Custom tileset flags */
    bool has_custom_tiles_top;                 /* True = vram_top loaded from custom PNG */
    bool has_custom_tiles_bottom;              /* True = vram_bottom loaded from custom PNG */
    bool playtest_has_custom_tiles;            /* True = inject custom VRAM during playtest */

    /* Playtest palette injection */
    GBCPalette playtest_palettes_top[8];       /* Editor BG palettes for top stage */
    GBCPalette playtest_palettes_bottom[8];    /* Editor BG palettes for bottom stage */
    bool playtest_has_custom_palettes;         /* True = override palettes after load */

    /* Saved game state for snapshot/restore on editor entry/exit */
    uint8_t *saved_game_state;     /* malloc'd copy of GameState */
    size_t saved_game_state_size;

    /* Editor framebuffer (larger than 160x144 for the viewport) */
    uint32_t *framebuffer;
    int fb_width, fb_height;

    /* Mouse state (in screen coordinates) */
    int mouse_x, mouse_y;
    bool mouse_left_down, mouse_right_down, mouse_middle_down;
    bool mouse_left_clicked, mouse_right_clicked;
    int mouse_wheel_delta;

    /* Sidebar state */
    int palette_scroll;
    int inspector_scroll;

    /* Property inspector state */
    int inspector_active_field;    /* Which property field is being edited (-1 = none) */
    char inspector_edit_buf[64];   /* Text input buffer for active field */
    int inspector_cursor;          /* Cursor position in edit buffer */

    /* Tile painting state */
    uint8_t paint_tile_index;      /* Which tile index to paint */
    uint8_t paint_tile_palette;    /* Palette for painted tiles */
    uint8_t paint_coll_attr;       /* Collision attribute to paint */

    /* Eyedropper state */
    uint8_t sampled_coll_attr;     /* Last Alt+clicked attribute */
    bool has_sampled_attr;         /* True after first eyedropper use */

    /* Palette editor state */
    int pal_selected_palette;      /* Which palette (0-7), default 0 */
    int pal_selected_color;        /* Which color slot (0-3), default 0 */
    int pal_edit_channel;          /* 0=R, 1=G, 2=B */
    int pal_edit_scope;            /* 0=both, 1=top only, 2=bottom only */

    /* Fill rectangle state */
    bool fill_active;              /* Shift+drag in progress */
    bool fill_erasing;             /* Right-click fill = erase */
    int fill_start_col;            /* Start tile column */
    int fill_start_row;            /* Start display row */

    /* Table readiness checklist */
    bool show_checklist;           /* Toggled by Tab key */

    /* VRAM reference for tilemap preview (points to live VRAM, safe while game paused) */
    VirtualVRAM *vram_ref;

    /* VRAM tile data snapshots for combined top+bottom view */
    uint8_t vram_top[2][6144];         /* 2 banks of tile data, top stage */
    uint8_t vram_bottom[2][6144];      /* 2 banks of tile data, bottom stage */
    GBCPalette palettes_top[8];        /* Top stage BG palettes */
    GBCPalette palettes_bottom[8];     /* Bottom stage BG palettes */
    bool combined_view;                /* True = both halves loaded */
    bool hide_buffer_rows;             /* True = skip off-screen buffer rows (18-31 per half) */
    uint8_t top_stage_id;
    uint8_t bottom_stage_id;

    /* Undo stack */
    EditorTable *undo_stack;           /* malloc'd array[MAX_EDITOR_UNDO] */
    int undo_count;                    /* Number of snapshots stored */
    bool undo_pushed_this_stroke;      /* Prevents multiple pushes per drag/stroke */

    /* Save flash indicator */
    int save_flash_timer;              /* Counts down frames, shows "SAVED!" when > 0 */

    /* Center view on table open */
    bool needs_center_view;

    /* Asset base path (for scanning tables/ directory) */
    char asset_base_path[260];

    /* Editor active flag */
    bool active;

    /* True if we forced fullscreen on editor entry (restore on exit) */
    bool forced_fullscreen;

    /* Collision mask editor */
    MaskEditorState *mask_editor;

    /* Flipper sweep visualizer */
    bool show_flipper_sweep;
    int flipper_anim_angle;        /* 0-15, cycles for animation */
    int flipper_anim_timer;        /* Frame counter for animation */

    /* Left panel tab (0 = Tools, 1 = Overlays & Controls) */
    int left_panel_tab;

    /* GameState reference (set during editor_update, valid during render) */
    GameState *game_state_ref;
} EditorState;

/*=============================================================================
 * Editor Lifecycle
 *===========================================================================*/

/* Create editor state (allocated on first use) */
EditorState *editor_create(void);

/* Free editor state and all resources */
void editor_free(EditorState *editor);

/* Toggle editor mode on/off. Opens table picker from any screen. */
void editor_toggle(GameState *state, Platform *platform);

/* Enter editor mode with table picker */
void editor_enter(EditorState *editor, GameState *state);

/* Exit editor mode (restore game state) */
void editor_exit(EditorState *editor, GameState *state);

/*=============================================================================
 * Editor Update & Render (called from main loop when editor_mode == 1)
 *===========================================================================*/

/* Process one frame of editor input and logic */
void editor_update(EditorState *editor, Platform *platform, GameState *state);

/* Render the editor viewport and UI */
void editor_render(EditorState *editor, Renderer *renderer, Platform *platform);

/*=============================================================================
 * Component Info
 *===========================================================================*/

/* Get the display name for a component type */
const char *editor_component_name(ComponentType type);

/* Get default bounding box for a component type */
void editor_component_default_bbox(ComponentType type, uint8_t *x_thresh, uint8_t *y_thresh);

/* Get default score for a component type */
int editor_component_default_score(ComponentType type);

/* Import current stage from game state into editor table (internal) */
void editor_import_current_stage(EditorState *editor, GameState *state);

/* Map a display row to a data row (accounts for hidden buffer rows in combined view).
 * display_row: 0-based row in the visible layout
 * Returns: index into tilemap[][]/collision_map[][] arrays */
static inline int editor_display_to_data_row(EditorState *e, int display_row) {
    if (!e->combined_view || !e->hide_buffer_rows) return display_row;
    /* Compact layout: display 0-17 = data 0-17 (top visible),
     *                 display 18-35 = data 32-49 (bottom visible) */
    if (display_row < 18) return display_row;
    return display_row - 18 + 32;
}

/* Get the number of display rows (may differ from tilemap_rows when buffer is hidden) */
static inline int editor_display_rows(EditorState *e) {
    if (e->combined_view && e->hide_buffer_rows) return 36;  /* 18 + 18 */
    return e->table.tilemap_rows > 0 ? e->table.tilemap_rows : 32;
}

/* Scan tables/ directory for available table folders */
void editor_scan_tables(EditorState *editor);

/* Open a table folder for editing (load manifest, init editor) */
void editor_open_table(EditorState *editor, int picker_index, GameState *state);

/* Create a new blank table with template collision data */
void editor_create_new_table(EditorState *editor, GameState *state);

/* Load persisted custom data (collision, tilemaps, tilesets) from table folder */
void editor_load_custom_data(EditorState *editor);

/*=============================================================================
 * Live Playtest
 *===========================================================================*/

/* Start playtesting the editor table (F5) */
void editor_start_playtest(EditorState *editor, GameState *state);

/* Stop playtesting and return to editor (ESC during playtest) */
void editor_stop_playtest(EditorState *editor, GameState *state);

/* Hot-reload Lua scripts only (F6 during playtest) */
void editor_hot_reload(EditorState *editor, GameState *state);

/*=============================================================================
 * Object Links
 *===========================================================================*/

/* Add a link between two objects. Returns link index or -1 on failure. */
int editor_add_link(EditorTable *table, int source, int target, LinkType type);

/* Remove a link by index. Adjusts selected_link if needed. */
void editor_remove_link(EditorTable *table, int link_idx);

/* Remove all links referencing a given object index. Called when deleting objects. */
void editor_remove_links_for_object(EditorTable *table, int obj_idx);

/* Fix link indices after an object is removed (shift indices down). */
void editor_fix_link_indices_after_delete(EditorTable *table, int deleted_idx);

/* Get the display name for a link type */
const char *editor_link_type_name(LinkType type);

/*=============================================================================
 * Behavior Templates
 *===========================================================================*/

#define MAX_TEMPLATE_OBJECTS  8
#define MAX_TEMPLATE_LINKS    8
#define NUM_BEHAVIOR_TEMPLATES 5

typedef struct {
    const char *name;
    const char *description;
    int num_objects;
    struct { ComponentType type; int8_t dx, dy; } objects[MAX_TEMPLATE_OBJECTS];
    int num_links;
    struct { int src, dst; LinkType type; int threshold; } links[MAX_TEMPLATE_LINKS];
} BehaviorTemplate;

/* Get the built-in behavior templates array */
const BehaviorTemplate *editor_get_templates(void);

#endif /* EDITOR_H */
