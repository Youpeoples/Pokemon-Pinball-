/*
 * Stage Builder / Editor Mode - Core Implementation
 *
 * Handles editor lifecycle (create, enter, exit, free),
 * input processing, table picker, and object manipulation.
 */

#include "game/editor.h"
#include "game/game_state.h"
#include "game/editor_render.h"
#include "game/editor_serialize.h"
#include "game/scripting.h"
#include "game/collision.h"
#include "game/flippers.h"
#include "game/billboard.h"
#include "audio/audio.h"
#include "renderer/stage_assets.h"
#include "cJSON.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

/*=============================================================================
 * Component Info Tables
 *===========================================================================*/

static const char *component_names[] = {
    "None",
    "Bumper",
    "Spinner",
    "Ball Upgrade",
    "Pikachu Saver",
    "CAVE Light",
    "Diglett",
    "Field Creature",
    "Staryu",
    "Slot Machine",
    "Railing",
    "Launch Alley",
    "Wild Pokemon",
    "Board Trigger",
    "Ditto Slot",
};

typedef struct {
    uint8_t x_thresh, y_thresh;
    int default_score;
    int default_force;
    int default_sfx;
} ComponentDefaults;

static const ComponentDefaults component_defaults[] = {
    /* COMP_NONE */           { 0,  0,     0,      0, 0x00 },
    /* COMP_BUMPER */         { 14, 14,  500, 0x0200, 0x0E },
    /* COMP_SPINNER */        { 8,  4,   100,      0, 0x14 },
    /* COMP_BALL_UPGRADE */   { 6,  5,     0,      0, 0x17 },
    /* COMP_PIKACHU_SAVER */  { 3,  5,     0,      0, 0x00 },
    /* COMP_CAVE_LIGHT */     { 5,  3,     0,      0, 0x1B },
    /* COMP_DIGLETT */        { 8,  12,    0,      0, 0x00 },
    /* COMP_FIELD_CREATURE */ { 6,  5,   100,      0, 0x0E },
    /* COMP_STARYU */         { 8,  6,     0,      0, 0x00 },
    /* COMP_SLOT_MACHINE */   { 4,  4,     0,      0, 0x00 },
    /* COMP_RAILING */        { 7,  7,     0,      0, 0x00 },
    /* COMP_LAUNCH_ALLEY */   { 8,  8,     0,      0, 0x00 },
    /* COMP_WILD_POKEMON */   { 26, 26,    0,      0, 0x00 },
    /* COMP_BOARD_TRIGGER */  { 9,  9,     0,      0, 0x00 },
    /* COMP_DITTO_SLOT */     { 3,  3,     0,      0, 0x00 },
};

const char *editor_component_name(ComponentType type) {
    if (type >= 0 && type < COMP_COUNT)
        return component_names[type];
    return "Unknown";
}

void editor_component_default_bbox(ComponentType type, uint8_t *x_thresh, uint8_t *y_thresh) {
    if (type >= 0 && type < COMP_COUNT) {
        *x_thresh = component_defaults[type].x_thresh;
        *y_thresh = component_defaults[type].y_thresh;
    } else {
        *x_thresh = 8;
        *y_thresh = 8;
    }
}

int editor_component_default_score(ComponentType type) {
    if (type >= 0 && type < COMP_COUNT)
        return component_defaults[type].default_score;
    return 0;
}

/*=============================================================================
 * Editor Lifecycle
 *===========================================================================*/

EditorState *editor_create(void) {
    EditorState *editor = calloc(1, sizeof(EditorState));
    if (!editor) return NULL;

    /* Start at picker screen */
    editor->screen = EDITOR_SCREEN_PICKER;
    editor->ui_scale = UI_SCALE;

    /* Default viewport */
    editor->camera_x = 0.0f;
    editor->camera_y = 0.0f;
    editor->zoom = 2.0f;
    editor->target_zoom = 2.0f;

    /* Grid defaults */
    editor->show_grid = true;
    editor->snap_to_grid = true;
    editor->grid_size = EDITOR_GRID_SIZE;

    /* Overlays */
    editor->show_collision_overlay = true;  /* Default ON - collision editing is the main purpose */
    editor->show_object_bounds = true;

    /* Tool defaults */
    editor->current_tool = TOOL_SELECT;
    editor->palette_selection = COMP_NONE;
    editor->selected_object = -1;

    /* Collision tile selection/hover */
    editor->hovered_coll_col = -1;
    editor->hovered_coll_row = -1;
    editor->selected_coll_col = -1;
    editor->selected_coll_row = -1;
    editor->dragging_coll_tile = false;

    /* Undo stack */
    editor->undo_stack = calloc(MAX_EDITOR_UNDO, sizeof(EditorTable));
    editor->undo_count = 0;

    return editor;
}

void editor_free(EditorState *editor) {
    if (!editor) return;
    free(editor->saved_game_state);
    free(editor->framebuffer);
    free(editor->undo_stack);
    free(editor);
}

/*=============================================================================
 * Table Picker - Scan tables/ directory
 *===========================================================================*/

void editor_scan_tables(EditorState *editor) {
    if (!editor) return;

    memset(editor->picker_tables, 0, sizeof(editor->picker_tables));
    editor->picker_num_tables = 0;
    editor->picker_cursor = 0;

#ifdef _WIN32
    char search_path[520];
    snprintf(search_path, sizeof(search_path), "%stables\\*", editor->asset_base_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("[EDITOR] No tables/ directory found at %s\n", editor->asset_base_path);
        editor->picker_scanned = true;
        return;
    }

    do {
        /* Skip non-directories and . / .. / _editor_* temp folders */
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        if (fd.cFileName[0] == '_') continue;  /* Skip _editor_preview, _editor_output */

        if (editor->picker_num_tables >= MAX_PICKER_TABLES) break;

        /* Check for manifest.json */
        char manifest_path[520];
        snprintf(manifest_path, sizeof(manifest_path),
                 "%stables/%s/manifest.json", editor->asset_base_path, fd.cFileName);
        FILE *f = fopen(manifest_path, "rb");
        if (!f) continue;  /* No manifest = skip */

        PickerEntry *entry = &editor->picker_tables[editor->picker_num_tables];

        /* Read manifest for display name */
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *json = (char *)malloc(sz + 1);
        if (json) {
            fread(json, 1, sz, f);
            json[sz] = '\0';
            cJSON *root = cJSON_Parse(json);
            if (root) {
                cJSON *name = cJSON_GetObjectItem(root, "name");
                if (name && cJSON_IsString(name)) {
                    strncpy(entry->name, name->valuestring, sizeof(entry->name) - 1);
                } else {
                    strncpy(entry->name, fd.cFileName, sizeof(entry->name) - 1);
                }
                cJSON_Delete(root);
            } else {
                strncpy(entry->name, fd.cFileName, sizeof(entry->name) - 1);
            }
            free(json);
        }
        fclose(f);

        /* Store folder path and name */
        snprintf(entry->folder, sizeof(entry->folder),
                 "%stables/%s", editor->asset_base_path, fd.cFileName);
        strncpy(entry->folder_name, fd.cFileName, sizeof(entry->folder_name) - 1);

        /* Check if it's a builtin table */
        entry->is_builtin = (strcmp(fd.cFileName, "red_field") == 0 ||
                            strcmp(fd.cFileName, "blue_field") == 0);

        editor->picker_num_tables++;
        printf("[EDITOR] Found table: %s (%s)\n", entry->name, fd.cFileName);

    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#endif

    editor->picker_scanned = true;
    printf("[EDITOR] Scan complete: %d tables found\n", editor->picker_num_tables);
}

/*=============================================================================
 * Open Table from Picker
 *===========================================================================*/

void editor_open_table(EditorState *editor, int picker_index, GameState *state) {
    if (!editor || picker_index < 0 || picker_index >= editor->picker_num_tables) return;

    PickerEntry *entry = &editor->picker_tables[picker_index];

    /* Initialize the editor table with info from the picker entry */
    memset(&editor->table, 0, sizeof(EditorTable));
    strncpy(editor->table.name, entry->name, sizeof(editor->table.name) - 1);
    strncpy(editor->table.source_folder, entry->folder, sizeof(editor->table.source_folder) - 1);
    editor->table.tilemap_rows = 32;  /* Default: single stage */
    editor->combined_view = false;

    /* Parse manifest.json */
    uint8_t top_stage_id = 0xFF;   /* 0xFF = not present */
    uint8_t bottom_stage_id = 0xFF;
    uint8_t single_stage_id = 1;
    bool is_main_field = false;

    char manifest_path[520];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", entry->folder);
    FILE *mf = fopen(manifest_path, "rb");
    if (mf) {
        fseek(mf, 0, SEEK_END);
        long msz = ftell(mf);
        fseek(mf, 0, SEEK_SET);
        char *mjson = (char *)malloc(msz + 1);
        if (mjson) {
            fread(mjson, 1, msz, mf);
            mjson[msz] = '\0';
            cJSON *mroot = cJSON_Parse(mjson);
            if (mroot) {
                cJSON *stages = cJSON_GetObjectItem(mroot, "stages");
                if (stages) {
                    cJSON *top = cJSON_GetObjectItem(stages, "top");
                    cJSON *bottom = cJSON_GetObjectItem(stages, "bottom");
                    if (top && bottom) {
                        is_main_field = true;
                        cJSON *tid = cJSON_GetObjectItem(top, "id");
                        if (tid && cJSON_IsNumber(tid))
                            top_stage_id = (uint8_t)tid->valueint;
                        cJSON *bid = cJSON_GetObjectItem(bottom, "id");
                        if (bid && cJSON_IsNumber(bid))
                            bottom_stage_id = (uint8_t)bid->valueint;
                    } else if (bottom) {
                        cJSON *sid = cJSON_GetObjectItem(bottom, "id");
                        if (sid && cJSON_IsNumber(sid))
                            single_stage_id = (uint8_t)sid->valueint;
                    }
                }
                /* Fallback: check for single stage_id */
                cJSON *sid_root = cJSON_GetObjectItem(mroot, "stage_id");
                if (sid_root && cJSON_IsNumber(sid_root) && !is_main_field) {
                    single_stage_id = (uint8_t)sid_root->valueint;
                }
                cJSON_Delete(mroot);
            }
            free(mjson);
        }
        fclose(mf);
    }

    if (state->vram) {
        state->hram.lcdc = 0x67;  /* Pinball LCDC: signed addressing, OBJ 8x16 */

        if (is_main_field && top_stage_id != 0xFF && bottom_stage_id != 0xFF) {
            /* === Combined top+bottom view ===
             * Import full 32x32 tilemap from each stage, stacked vertically (64 rows total).
             * All data is preserved — rows 18-31 are off-screen GBC tilemap buffer
             * (may contain preloaded collision/catch data). */

            /* 1. Load TOP stage */
            state->current_stage = top_stage_id;
            state->stage_collision_state = 0;  /* Always use initial/complete collision layout */
            load_stage_assets(top_stage_id, state->vram, state, state->asset_base_path);
            load_stage_collision_attributes(state);

            /* Snapshot top VRAM tile data */
            memcpy(editor->vram_top[0], state->vram->tile_data[0], 6144);
            memcpy(editor->vram_top[1], state->vram->tile_data[1], 6144);
            memcpy(editor->palettes_top, state->bg_palettes, sizeof(editor->palettes_top));

            /* Import top tilemap into rows 0-31 */
            for (int row = 0; row < 32; row++) {
                for (int col = 0; col < 32; col++) {
                    int idx = row * 32 + col;
                    editor->table.tilemap[row][col] = state->vram->bg_map[0][idx];
                    editor->table.tilemap_attrs[row][col] = state->vram->bg_map[1][idx];
                }
            }
            /* Import top collision (0x300 bytes = 24 rows x 32 cols).
             * The GBC collision map has 3 rows of padding above the visible tilemap area.
             * Collision source row 3 aligns with tilemap row 0. Shift by -3 during import
             * so collision_map[R][C] visually aligns with tilemap[R][C] in the editor. */
            for (int i = 0; i < 0x300; i++) {
                int coll_row = i / 32;
                int col = i % 32;
                int dest_row = coll_row - 3;
                if (dest_row >= 0 && dest_row < 32) {
                    editor->table.collision_map[dest_row][col] = state->stage_collision_map[i];
                }
            }

            /* 2. Load BOTTOM stage */
            state->current_stage = bottom_stage_id;
            state->stage_collision_state = 0;  /* Always use initial/complete collision layout */
            load_stage_assets(bottom_stage_id, state->vram, state, state->asset_base_path);
            load_stage_collision_attributes(state);

            /* Snapshot bottom VRAM tile data */
            memcpy(editor->vram_bottom[0], state->vram->tile_data[0], 6144);
            memcpy(editor->vram_bottom[1], state->vram->tile_data[1], 6144);
            memcpy(editor->palettes_bottom, state->bg_palettes, sizeof(editor->palettes_bottom));

            /* Import bottom tilemap into rows 32-63 */
            for (int row = 0; row < 32; row++) {
                for (int col = 0; col < 32; col++) {
                    int idx = row * 32 + col;
                    editor->table.tilemap[row + 32][col] = state->vram->bg_map[0][idx];
                    editor->table.tilemap_attrs[row + 32][col] = state->vram->bg_map[1][idx];
                }
            }
            /* Import bottom collision into rows 32+ (same 3-row offset as top) */
            for (int i = 0; i < 0x300; i++) {
                int coll_row = i / 32;
                int col = i % 32;
                int dest_row = coll_row - 3;
                if (dest_row >= 0 && dest_row < 32) {
                    editor->table.collision_map[dest_row + 32][col] = state->stage_collision_map[i];
                }
            }

            editor->table.tilemap_rows = 64;
            editor->table.tilemap_cols = 32;
            editor->combined_view = true;
            editor->hide_buffer_rows = true;  /* Default: show only visible game rows */
            editor->top_stage_id = top_stage_id;
            editor->bottom_stage_id = bottom_stage_id;
            editor->table.stage_id = bottom_stage_id;
            editor->table.has_flippers = (bottom_stage_id & 1) != 0;
            editor->table.unsigned_addressing = false;
            editor->table.has_vram = true;
            /* Bottom stages use SCX=0x20 for the ball launcher alley offset */
            editor->table.default_scx = (bottom_stage_id & 1) ? 0x20 : 0;

            /* Copy palettes (use bottom stage as primary) */
            memcpy(editor->table.bg_palettes, state->bg_palettes, sizeof(editor->table.bg_palettes));
            memcpy(editor->table.obj_palettes, state->obj_palettes, sizeof(editor->table.obj_palettes));

            printf("[EDITOR] Loaded combined view: top=0x%02X bottom=0x%02X for '%s'\n",
                   top_stage_id, bottom_stage_id, entry->name);

        } else {
            /* === Single stage view === */
            state->current_stage = single_stage_id;
            state->stage_collision_state = 0;  /* Always use initial/complete collision layout */
            load_stage_assets(single_stage_id, state->vram, state, state->asset_base_path);
            load_stage_collision_attributes(state);
            editor_import_current_stage(editor, state);
            editor->table.tilemap_rows = 32;
            editor->table.tilemap_cols = 32;

            printf("[EDITOR] Loaded stage 0x%02X assets from disk for table '%s'\n",
                   single_stage_id, entry->name);
        }
    }

    /* Keep reference to VRAM for tilemap rendering */
    editor->vram_ref = state->vram;

    /* Reset editor viewport and center on the stage */
    editor->selected_object = -1;
    editor->current_tool = TOOL_SELECT;
    editor->screen = EDITOR_SCREEN_EDITOR;
    editor->needs_center_view = true;

    /* Reset undo stack */
    editor->undo_count = 0;
    editor->undo_pushed_this_stroke = false;

    printf("[EDITOR] Opened table: %s (folder: %s)\n", entry->name, entry->folder_name);
}

/*=============================================================================
 * Enter / Exit Editor Mode
 *===========================================================================*/

void editor_enter(EditorState *editor, GameState *state) {
    if (!editor) return;

    /* Snapshot the entire game state */
    if (editor->saved_game_state) {
        free(editor->saved_game_state);
    }
    editor->saved_game_state_size = sizeof(GameState);
    editor->saved_game_state = malloc(editor->saved_game_state_size);
    if (editor->saved_game_state) {
        memcpy(editor->saved_game_state, state, editor->saved_game_state_size);
    }

    /* Copy asset base path for table scanning */
    memcpy(editor->asset_base_path, state->asset_base_path, sizeof(editor->asset_base_path));

    /* Scan for available tables */
    editor->picker_scanned = false;
    editor_scan_tables(editor);

    /* Start at the picker screen */
    editor->screen = EDITOR_SCREEN_PICKER;
    editor->picker_cursor = 0;
    editor->active = true;

    printf("[EDITOR] Entered editor mode (picker shown, %d tables)\n", editor->picker_num_tables);
}

void editor_exit(EditorState *editor, GameState *state) {
    if (!editor || !editor->saved_game_state) return;

    /* Restore game state from snapshot, preserving native pointers */
    VirtualVRAM *saved_vram = state->vram;
    void *saved_audio = state->audio;
    void *saved_config = state->config;
    void *saved_script = state->script_engine;
    uint8_t *saved_vwf = state->vwf_font_gfx;
    uint8_t *saved_slot_ff = state->slot_force_field_data;
    char saved_asset_path[260];
    memcpy(saved_asset_path, state->asset_base_path, sizeof(saved_asset_path));

    memcpy(state, editor->saved_game_state, editor->saved_game_state_size);

    /* Restore native pointers (they may have been from the snapshot's stale values) */
    state->vram = saved_vram;
    state->audio = saved_audio;
    state->config = saved_config;
    state->script_engine = saved_script;
    state->vwf_font_gfx = saved_vwf;
    state->slot_force_field_data = saved_slot_ff;
    memcpy(state->asset_base_path, saved_asset_path, sizeof(state->asset_base_path));

    editor->active = false;

    printf("[EDITOR] Exited editor mode\n");
}

void editor_toggle(GameState *state, Platform *platform) {
    /* Lazy-create editor state on first use */
    if (!state->editor_state) {
        state->editor_state = editor_create();
        if (!state->editor_state) {
            printf("[EDITOR] Failed to allocate editor state\n");
            return;
        }
    }

    EditorState *editor = state->editor_state;

    if (state->editor_mode) {
        /* Exit editor */
        editor_exit(editor, state);
        state->editor_mode = 0;
        platform_set_esc_quits(platform, true);
    } else {
        /* Enter editor from any screen — table picker will be shown */
        editor_enter(editor, state);
        state->editor_mode = 1;
        /* Prevent ESC from quitting while editor is open */
        platform_set_esc_quits(platform, false);
    }
}

/*=============================================================================
 * Import Current Stage (from live VRAM when in pinball mode)
 *===========================================================================*/

void editor_import_current_stage(EditorState *editor, GameState *state) {
    EditorTable *t = &editor->table;

    snprintf(t->name, sizeof(t->name), "Stage 0x%02X", state->current_stage);
    t->stage_id = state->current_stage;
    t->has_flippers = (state->current_stage & 1) != 0;
    t->default_scx = state->scx;

    /* Store addressing mode from LCDC */
    t->unsigned_addressing = (state->hram.lcdc & 0x10) != 0;
    t->has_vram = true;

    /* Copy current palettes */
    memcpy(t->bg_palettes, state->bg_palettes, sizeof(t->bg_palettes));
    memcpy(t->obj_palettes, state->obj_palettes, sizeof(t->obj_palettes));

    /* Copy tilemap from VRAM if available */
    if (state->vram) {
        extern void editor_import_tilemap(EditorState *editor, VirtualVRAM *vram);
        editor_import_tilemap(editor, state->vram);
    }

    /* Copy collision map with 3-row alignment offset.
     * GBC collision rows 0-2 are above-screen border padding;
     * collision row 3 aligns with tilemap row 0. */
    memset(t->collision_map, 0, sizeof(t->collision_map));
    for (int i = 0; i < (int)sizeof(state->stage_collision_map); i++) {
        int coll_row = i / 32;
        int col = i % 32;
        int dest_row = coll_row - 3;
        if (dest_row >= 0 && dest_row < 64) {
            t->collision_map[dest_row][col] = state->stage_collision_map[i];
        }
    }

    printf("[EDITOR] Imported stage 0x%02X (addressing: %s)\n",
           t->stage_id, t->unsigned_addressing ? "unsigned" : "signed");
}

/*=============================================================================
 * Live Playtest
 *===========================================================================*/

void editor_start_playtest(EditorState *editor, GameState *state) {
    if (!editor || editor->playtesting) return;

    /* Save the editor table to a temp directory */
    char preview_path[512];
    snprintf(preview_path, sizeof(preview_path), "%stables/_editor_preview",
             state->asset_base_path);
    editor_serialize_table(editor, preview_path);

    /* Restore the game state from snapshot (so we start fresh) */
    if (editor->saved_game_state) {
        VirtualVRAM *saved_vram = state->vram;
        void *saved_audio = state->audio;
        void *saved_config = state->config;
        void *saved_script = state->script_engine;
        uint8_t *saved_vwf = state->vwf_font_gfx;
        uint8_t *saved_slot_ff = state->slot_force_field_data;
        void *saved_editor = state->editor_state;
        char saved_asset_path[260];
        memcpy(saved_asset_path, state->asset_base_path, sizeof(saved_asset_path));

        memcpy(state, editor->saved_game_state, editor->saved_game_state_size);

        state->vram = saved_vram;
        state->audio = saved_audio;
        state->config = saved_config;
        state->script_engine = saved_script;
        state->vwf_font_gfx = saved_vwf;
        state->slot_force_field_data = saved_slot_ff;
        state->editor_state = saved_editor;
        memcpy(state->asset_base_path, saved_asset_path, sizeof(state->asset_base_path));
    }

    /* Clear active table folder so the scripting engine does NOT load the
     * auto-generated Lua scripts.  The serialized sprites.lua defines an active
     * on_draw_sprites hook that replaces the C sprite drawing with a minimal
     * stub — which omits bumpers, indicators, and most ball rendering.
     * By clearing the folder, pinball_load_gfx will find no scripts and all
     * C draw/init/collision code runs natively, giving a fully working playtest. */
    state->active_table_folder[0] = '\0';

    /* === Critical: set stage and clear loading flags === */

    /* Determine the correct stage for playtest:
     * - Combined view (top+bottom): use bottom stage (has flippers)
     * - Single stage: use the table's stage_id */
    if (editor->combined_view) {
        state->current_stage = editor->bottom_stage_id;
    } else {
        state->current_stage = editor->table.stage_id;
    }

    /* Force fresh asset/collision loading */
    state->gfx_loaded = 0;
    state->loading_saved_game = 0;

    /* Free stale flipper collision data and billboard cache */
    free_flipper_collision_data();
    billboard_free_cache();

    /* Snapshot the editor's collision data for injection after game loads */
    memcpy(editor->playtest_collision_top, editor->table.collision_map,
           sizeof(editor->playtest_collision_top));
    if (editor->combined_view) {
        memcpy(editor->playtest_collision_bottom,
               &editor->table.collision_map[32],
               sizeof(editor->playtest_collision_bottom));
    }
    editor->playtest_has_collision = true;

    /* Set game to start pinball from loading state */
    state->current_screen = 4;  /* PINBALL_GAME */
    state->screen_state = 0;    /* LoadGFX */
    state->editor_mode = 0;     /* Disable editor mode so game runs */
    editor->playtesting = true;

    printf("[EDITOR] Starting playtest for stage 0x%02X\n", state->current_stage);
}

void editor_stop_playtest(EditorState *editor, GameState *state) {
    if (!editor || !editor->playtesting) return;

    /* Restore game state from the original snapshot */
    if (editor->saved_game_state) {
        VirtualVRAM *saved_vram = state->vram;
        void *saved_audio = state->audio;
        void *saved_config = state->config;
        void *saved_script = state->script_engine;
        uint8_t *saved_vwf = state->vwf_font_gfx;
        uint8_t *saved_slot_ff = state->slot_force_field_data;
        void *saved_editor = state->editor_state;
        char saved_asset_path[260];
        memcpy(saved_asset_path, state->asset_base_path, sizeof(saved_asset_path));

        memcpy(state, editor->saved_game_state, editor->saved_game_state_size);

        state->vram = saved_vram;
        state->audio = saved_audio;
        state->config = saved_config;
        state->script_engine = saved_script;
        state->vwf_font_gfx = saved_vwf;
        state->slot_force_field_data = saved_slot_ff;
        state->editor_state = saved_editor;
        memcpy(state->asset_base_path, saved_asset_path, sizeof(state->asset_base_path));
    }

    /* Stop all audio — the playtest music would otherwise keep playing
     * since the audio engine state is independent of the GameState snapshot */
    audio_stop_all(state->audio);

    state->editor_mode = 1;
    editor->playtesting = false;
    editor->active = true;

    printf("[EDITOR] Stopped playtest, returning to editor\n");
}

void editor_hot_reload(EditorState *editor, GameState *state) {
    if (!editor || !editor->playtesting) return;

    /* Re-serialize scripts and reload them */
    char preview_path[512];
    snprintf(preview_path, sizeof(preview_path), "%stables/_editor_preview",
             state->asset_base_path);
    editor_serialize_scripts_only(editor, preview_path);

    /* Reload the table scripts via the scripting engine */
    if (state->script_engine) {
        script_unload_table(state->script_engine);
        script_load_table(state->script_engine, preview_path);
    }

    printf("[EDITOR] Hot-reloaded Lua scripts\n");
}

/*=============================================================================
 * Undo System
 *===========================================================================*/

static void editor_push_undo(EditorState *editor) {
    if (!editor->undo_stack) return;
    if (editor->undo_count >= MAX_EDITOR_UNDO) {
        /* Shift stack down, dropping oldest */
        memmove(&editor->undo_stack[0], &editor->undo_stack[1],
                (MAX_EDITOR_UNDO - 1) * sizeof(EditorTable));
        editor->undo_count = MAX_EDITOR_UNDO - 1;
    }
    editor->undo_stack[editor->undo_count] = editor->table;
    editor->undo_count++;
}

static void editor_undo(EditorState *editor) {
    if (!editor->undo_stack || editor->undo_count <= 0) return;
    editor->undo_count--;
    editor->table = editor->undo_stack[editor->undo_count];
    editor->selected_object = -1;
    editor->dragging_object = false;
}

/*=============================================================================
 * Editor Update - Picker Screen
 *===========================================================================*/

/* Returns true if picker wants to open the current selection */
static bool editor_update_picker(EditorState *editor, Platform *platform) {
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    static Uint8 prev_keys_picker[SDL_NUM_SCANCODES] = {0};
    bool confirmed = false;

    #define PICK_PRESSED(sc) (keys[(sc)] && !prev_keys_picker[(sc)])

    /* Navigate picker */
    if (PICK_PRESSED(SDL_SCANCODE_UP)) {
        if (editor->picker_cursor > 0) editor->picker_cursor--;
    }
    if (PICK_PRESSED(SDL_SCANCODE_DOWN)) {
        if (editor->picker_cursor < editor->picker_num_tables - 1)
            editor->picker_cursor++;
    }

    /* Enter or Space confirms */
    if (PICK_PRESSED(SDL_SCANCODE_RETURN) || PICK_PRESSED(SDL_SCANCODE_SPACE)) {
        confirmed = true;
    }

    /* Mouse click on picker items */
    if (editor->mouse_left_clicked) {
        int s = editor->ui_scale;
        int char_h = 6 * s;
        int item_h = char_h + 6 * s;
        int pad = 8 * s;
        int start_y = pad + 20 * s;

        int click_y = editor->mouse_y - start_y;
        if (click_y >= 0) {
            int idx = click_y / item_h;
            if (idx >= 0 && idx < editor->picker_num_tables) {
                if (idx == editor->picker_cursor) {
                    /* Clicked same item = confirm (like double-click) */
                    confirmed = true;
                } else {
                    /* First click = move cursor */
                    editor->picker_cursor = idx;
                }
            }
        }
    }

    memcpy(prev_keys_picker, keys, SDL_NUM_SCANCODES);
    #undef PICK_PRESSED

    return confirmed;
}

/*=============================================================================
 * Editor Update - Main Editor Screen
 *===========================================================================*/

static void editor_update_editor(EditorState *editor, Platform *platform) {
    /* Middle-click pan */
    int raw_mx = editor->mouse_x;
    int raw_my = editor->mouse_y;

    if (editor->mouse_middle_down && !editor->panning) {
        editor->panning = true;
        editor->drag_start_x = raw_mx;
        editor->drag_start_y = raw_my;
    }
    if (editor->panning && editor->mouse_middle_down) {
        float dx = (float)(raw_mx - editor->drag_start_x) / editor->zoom;
        float dy = (float)(raw_my - editor->drag_start_y) / editor->zoom;
        editor->camera_x -= dx;
        editor->camera_y -= dy;
        editor->drag_start_x = raw_mx;
        editor->drag_start_y = raw_my;
    }
    if (!editor->mouse_middle_down) {
        editor->panning = false;
    }

    /* Smooth zoom (stage-centered: stage always stays centered in viewport) */
    if (editor->zoom != editor->target_zoom) {
        float diff = editor->target_zoom - editor->zoom;
        editor->zoom += diff * 0.2f;
        if (fabsf(diff) < 0.01f) editor->zoom = editor->target_zoom;

        /* Re-center stage in viewport after each zoom step */
        editor->needs_center_view = true;
    }

    /* Keyboard shortcuts (edge-detected) */
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    static Uint8 prev_keys[SDL_NUM_SCANCODES] = {0};

    #define KEY_PRESSED(sc) (keys[(sc)] && !prev_keys[(sc)])

    if (KEY_PRESSED(SDL_SCANCODE_G)) {
        editor->snap_to_grid = !editor->snap_to_grid;
        editor->show_grid = editor->snap_to_grid;
    }
    if (KEY_PRESSED(SDL_SCANCODE_C)) {
        editor->show_collision_overlay = !editor->show_collision_overlay;
    }
    if (KEY_PRESSED(SDL_SCANCODE_B)) {
        editor->show_object_bounds = !editor->show_object_bounds;
    }
    if (KEY_PRESSED(SDL_SCANCODE_H) && editor->combined_view) {
        editor->hide_buffer_rows = !editor->hide_buffer_rows;
        editor->needs_center_view = true;  /* Re-center since height changed */
    }
    if (KEY_PRESSED(SDL_SCANCODE_S) && !(keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])) {
        editor->current_tool = TOOL_SELECT;
        editor->palette_selection = COMP_NONE;
    }
    if (KEY_PRESSED(SDL_SCANCODE_P)) {
        editor->current_tool = TOOL_PLACE;
        if (editor->palette_selection == COMP_NONE)
            editor->palette_selection = COMP_BUMPER;
    }
    if (KEY_PRESSED(SDL_SCANCODE_HOME)) {
        editor->needs_center_view = true;
        editor->target_zoom = 2.0f;
    }
    if (KEY_PRESSED(SDL_SCANCODE_T)) {
        editor->current_tool = TOOL_TILE_PAINT;
        editor->palette_selection = COMP_NONE;
    }
    if (KEY_PRESSED(SDL_SCANCODE_X)) {
        editor->current_tool = TOOL_COLL_PAINT;
        editor->palette_selection = COMP_NONE;
    }
    if (KEY_PRESSED(SDL_SCANCODE_E)) {
        editor->current_tool = TOOL_ERASE;
        editor->palette_selection = COMP_NONE;
    }

    /* Number keys 0-9 change collision attribute */
    if (editor->current_tool == TOOL_COLL_PAINT) {
        for (int k = 0; k <= 9; k++) {
            if (KEY_PRESSED(SDL_SCANCODE_0 + k)) {
                editor->paint_coll_attr = (uint8_t)(k * 0x10);
            }
        }
    }

    /* [ and ] change tile index */
    if (editor->current_tool == TOOL_TILE_PAINT) {
        if (KEY_PRESSED(SDL_SCANCODE_LEFTBRACKET)) {
            if (editor->paint_tile_index > 0) editor->paint_tile_index--;
        }
        if (KEY_PRESSED(SDL_SCANCODE_RIGHTBRACKET)) {
            editor->paint_tile_index++;
        }
    }

    /* Ctrl+S saves the table */
    if (KEY_PRESSED(SDL_SCANCODE_S) && (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])) {
        /* Save to the source folder if available, else _editor_output */
        const char *save_path = editor->table.source_folder[0]
            ? editor->table.source_folder : "tables/_editor_output";
        editor_serialize_table(editor, save_path);
        editor->save_flash_timer = 120;  /* ~2 seconds at 60fps */
        editor->table.dirty = false;
    }

    /* Ctrl+Z undoes last action */
    if (KEY_PRESSED(SDL_SCANCODE_Z) && (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])) {
        editor_undo(editor);
    }

    /* Ctrl+D duplicates selected object */
    if (KEY_PRESSED(SDL_SCANCODE_D) && (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])) {
        if (editor->selected_object >= 0 && editor->table.num_objects < MAX_EDITOR_OBJECTS) {
            editor_push_undo(editor);
            EditorObject *src = &editor->table.objects[editor->selected_object];
            EditorObject *dst = &editor->table.objects[editor->table.num_objects];
            *dst = *src;
            dst->x += 16;
            editor->table.num_objects++;
            editor->selected_object = editor->table.num_objects - 1;
            editor->table.dirty = true;
        }
    }

    memcpy(prev_keys, keys, SDL_NUM_SCANCODES);
    #undef KEY_PRESSED

    /* Arrow key panning */
    float pan_speed = 4.0f / editor->zoom;
    if (keys[SDL_SCANCODE_LEFT])  editor->camera_x -= pan_speed;
    if (keys[SDL_SCANCODE_RIGHT]) editor->camera_x += pan_speed;
    if (keys[SDL_SCANCODE_UP])    editor->camera_y -= pan_speed;
    if (keys[SDL_SCANCODE_DOWN])  editor->camera_y += pan_speed;

    /* Convert mouse to world coordinates */
    float world_x = editor->camera_x + (float)raw_mx / editor->zoom;
    float world_y = editor->camera_y + (float)raw_my / editor->zoom;

    /* Determine sidebar width for click exclusion */
    int sidebar_w = 200;  /* Must match render code */

    /* Handle object placement (only if clicking in the viewport area) */
    if (editor->current_tool == TOOL_PLACE && editor->mouse_left_clicked) {
        int win_w, win_h;
        SDL_Window *win = SDL_GetMouseFocus();
        if (win) SDL_GetWindowSize(win, &win_w, &win_h);
        else { win_w = 640; win_h = 576; }

        if (raw_mx < win_w - sidebar_w) {  /* Not clicking sidebar */
            if (editor->palette_selection != COMP_NONE &&
                editor->table.num_objects < MAX_EDITOR_OBJECTS) {

                editor_push_undo(editor);

                EditorObject *obj = &editor->table.objects[editor->table.num_objects];
                memset(obj, 0, sizeof(EditorObject));
                obj->type = editor->palette_selection;

                if (editor->snap_to_grid) {
                    obj->x = ((int)world_x / editor->grid_size) * editor->grid_size;
                    obj->y = ((int)world_y / editor->grid_size) * editor->grid_size;
                } else {
                    obj->x = (uint8_t)world_x;
                    obj->y = (uint8_t)world_y;
                }

                editor_component_default_bbox(obj->type, &obj->x_thresh, &obj->y_thresh);
                obj->score = editor_component_default_score(obj->type);
                obj->bounce_force = component_defaults[obj->type].default_force;
                obj->sfx_id = component_defaults[obj->type].default_sfx;

                editor->table.num_objects++;
                editor->table.dirty = true;
                editor->selected_object = editor->table.num_objects - 1;
            }
        }
    }

    /* Handle object selection */
    if (editor->current_tool == TOOL_SELECT && editor->mouse_left_clicked) {
        editor->selected_object = -1;
        for (int i = editor->table.num_objects - 1; i >= 0; i--) {
            EditorObject *obj = &editor->table.objects[i];
            int dx = abs((int)world_x - (int)obj->x);
            int dy = abs((int)world_y - (int)obj->y);
            if (dx <= obj->x_thresh && dy <= obj->y_thresh) {
                editor->selected_object = i;
                editor->dragging_object = true;
                editor->drag_offset_x = (int)world_x - obj->x;
                editor->drag_offset_y = (int)world_y - obj->y;
                editor_push_undo(editor);  /* Undo before drag starts */
                break;
            }
        }
    }

    /* Handle object dragging */
    if (editor->dragging_object && editor->mouse_left_down && editor->selected_object >= 0) {
        EditorObject *obj = &editor->table.objects[editor->selected_object];
        int new_x = (int)world_x - editor->drag_offset_x;
        int new_y = (int)world_y - editor->drag_offset_y;
        if (editor->snap_to_grid) {
            new_x = (new_x / editor->grid_size) * editor->grid_size;
            new_y = (new_y / editor->grid_size) * editor->grid_size;
        }
        if (new_x >= 0 && new_x <= 255) obj->x = (uint8_t)new_x;
        if (new_y >= 0 && new_y <= 255) obj->y = (uint8_t)new_y;
        editor->table.dirty = true;
    }
    if (!editor->mouse_left_down) {
        editor->dragging_object = false;
        editor->dragging_coll_tile = false;
    }

    /* Delete key removes selected object (edge-detected via mouse_left_clicked as proxy for "just pressed") */
    {
        static bool delete_was_down = false;
        bool delete_down = keys[SDL_SCANCODE_DELETE] != 0;
        if (delete_down && !delete_was_down && editor->selected_object >= 0) {
            editor_push_undo(editor);
            int idx = editor->selected_object;
            for (int i = idx; i < editor->table.num_objects - 1; i++) {
                editor->table.objects[i] = editor->table.objects[i + 1];
            }
            editor->table.num_objects--;
            editor->selected_object = -1;
            editor->table.dirty = true;
        }
        delete_was_down = delete_down;
    }

    /* Undo push on paint/erase stroke start */
    if ((editor->current_tool == TOOL_TILE_PAINT || editor->current_tool == TOOL_COLL_PAINT) &&
        (editor->mouse_left_clicked || editor->mouse_right_clicked)) {
        editor->undo_pushed_this_stroke = false;
    }

    int disp_rows = editor_display_rows(editor);
    int max_cols = editor->table.tilemap_cols > 0 ? editor->table.tilemap_cols : 32;

    /* === Collision tile hover detection === */
    editor->hovered_coll_col = -1;
    editor->hovered_coll_row = -1;
    if (editor->show_collision_overlay) {
        int hover_col = (int)(world_x / 8.0f);
        int hover_disp_row = (int)(world_y / 8.0f);
        int hover_data_row = editor_display_to_data_row(editor, hover_disp_row);
        if (hover_col >= 0 && hover_col < max_cols &&
            hover_disp_row >= 0 && hover_disp_row < disp_rows && hover_data_row < 64) {
            uint8_t attr = editor->table.collision_map[hover_data_row][hover_col];
            if (attr != 0) {
                editor->hovered_coll_col = hover_col;
                editor->hovered_coll_row = hover_disp_row;
                editor->hovered_coll_attr = attr;
            }
        }
    }

    /* === Collision tile selection and drag-move === */
    if (editor->show_collision_overlay && editor->current_tool == TOOL_SELECT) {
        /* Click-to-select collision tile (only if no object was hit) */
        if (editor->mouse_left_clicked && editor->selected_object < 0 && !editor->dragging_object) {
            if (editor->hovered_coll_col >= 0) {
                /* Select the hovered collision tile */
                editor->selected_coll_col = editor->hovered_coll_col;
                editor->selected_coll_row = editor->hovered_coll_row;
                editor->selected_coll_attr = editor->hovered_coll_attr;
                /* Start drag */
                editor->dragging_coll_tile = true;
                editor->drag_coll_src_col = editor->hovered_coll_col;
                editor->drag_coll_src_row = editor->hovered_coll_row;
                editor_push_undo(editor);
            } else {
                /* Clicked empty space — deselect */
                editor->selected_coll_col = -1;
                editor->selected_coll_row = -1;
                editor->dragging_coll_tile = false;
            }
        }

        /* Drag-move collision tile */
        if (editor->dragging_coll_tile && editor->mouse_left_down && editor->selected_coll_col >= 0) {
            int dest_col = (int)(world_x / 8.0f);
            int dest_disp_row = (int)(world_y / 8.0f);
            int dest_data_row = editor_display_to_data_row(editor, dest_disp_row);
            if (dest_col >= 0 && dest_col < max_cols &&
                dest_disp_row >= 0 && dest_disp_row < disp_rows && dest_data_row < 64) {
                /* Only move if destination changed */
                if (dest_col != editor->selected_coll_col || dest_disp_row != editor->selected_coll_row) {
                    /* Clear current position */
                    int cur_data_row = editor_display_to_data_row(editor, editor->selected_coll_row);
                    if (cur_data_row < 64) {
                        editor->table.collision_map[cur_data_row][editor->selected_coll_col] = 0;
                    }
                    /* Set new position */
                    editor->table.collision_map[dest_data_row][dest_col] = editor->selected_coll_attr;
                    editor->selected_coll_col = dest_col;
                    editor->selected_coll_row = dest_disp_row;
                    editor->table.dirty = true;
                }
            }
        }
    }

    /* Deselect collision tile when switching tools or toggling overlay off */
    if (!editor->show_collision_overlay || editor->current_tool != TOOL_SELECT) {
        editor->selected_coll_col = -1;
        editor->selected_coll_row = -1;
        editor->dragging_coll_tile = false;
    }

    /* Tile painting */
    if (editor->current_tool == TOOL_TILE_PAINT && editor->mouse_left_down) {
        if (!editor->undo_pushed_this_stroke) {
            editor_push_undo(editor);
            editor->undo_pushed_this_stroke = true;
        }
        int tile_col = (int)(world_x / 8.0f);
        int disp_row = (int)(world_y / 8.0f);
        int data_row = editor_display_to_data_row(editor, disp_row);
        if (tile_col >= 0 && tile_col < max_cols && disp_row >= 0 && disp_row < disp_rows && data_row < 64) {
            editor->table.tilemap[data_row][tile_col] = editor->paint_tile_index;
            editor->table.tilemap_attrs[data_row][tile_col] =
                (editor->paint_tile_palette & 0x07);
            editor->table.dirty = true;
        }
    }

    /* Collision painting */
    if (editor->current_tool == TOOL_COLL_PAINT && editor->mouse_left_down) {
        if (!editor->undo_pushed_this_stroke) {
            editor_push_undo(editor);
            editor->undo_pushed_this_stroke = true;
        }
        int tile_col = (int)(world_x / 8.0f);
        int disp_row = (int)(world_y / 8.0f);
        int data_row = editor_display_to_data_row(editor, disp_row);
        if (tile_col >= 0 && tile_col < max_cols && disp_row >= 0 && disp_row < disp_rows && data_row < 64) {
            editor->table.collision_map[data_row][tile_col] = editor->paint_coll_attr;
            editor->table.dirty = true;
        }
    }

    /* Erasing: right-click clears collision */
    if (editor->current_tool == TOOL_COLL_PAINT && editor->mouse_right_down) {
        if (!editor->undo_pushed_this_stroke) {
            editor_push_undo(editor);
            editor->undo_pushed_this_stroke = true;
        }
        int tile_col = (int)(world_x / 8.0f);
        int disp_row = (int)(world_y / 8.0f);
        int data_row = editor_display_to_data_row(editor, disp_row);
        if (tile_col >= 0 && tile_col < max_cols && disp_row >= 0 && disp_row < disp_rows && data_row < 64) {
            editor->table.collision_map[data_row][tile_col] = 0;
            editor->table.dirty = true;
        }
    }

    /* Reset stroke flag when mouse released */
    if (!editor->mouse_left_down && !editor->mouse_right_down) {
        editor->undo_pushed_this_stroke = false;
    }

    /* Decrement save flash timer */
    if (editor->save_flash_timer > 0)
        editor->save_flash_timer--;

    /* No restrictive camera clamp — stage floats centered in viewport */
}

/*=============================================================================
 * Editor Update (input processing - dispatches based on screen)
 *===========================================================================*/

void editor_update(EditorState *editor, Platform *platform, GameState *state) {
    if (!editor || !editor->active) return;

    /* Poll SDL mouse state directly */
    int raw_mx, raw_my;
    uint32_t buttons = SDL_GetMouseState(&raw_mx, &raw_my);

    bool left_was_down = editor->mouse_left_down;
    bool right_was_down = editor->mouse_right_down;
    bool middle_was_down = editor->mouse_middle_down;

    editor->mouse_x = raw_mx;
    editor->mouse_y = raw_my;
    editor->mouse_left_down = (buttons & SDL_BUTTON_LMASK) != 0;
    editor->mouse_right_down = (buttons & SDL_BUTTON_RMASK) != 0;
    editor->mouse_middle_down = (buttons & SDL_BUTTON_MMASK) != 0;

    /* Edge detection */
    editor->mouse_left_clicked = editor->mouse_left_down && !left_was_down;
    editor->mouse_right_clicked = editor->mouse_right_down && !right_was_down;

    /* Dispatch based on screen */
    switch (editor->screen) {
        case EDITOR_SCREEN_PICKER: {
            bool confirmed = editor_update_picker(editor, platform);
            if (confirmed && editor->picker_num_tables > 0) {
                editor_open_table(editor, editor->picker_cursor, state);
            }
            break;
        }
        case EDITOR_SCREEN_EDITOR:
            editor_update_editor(editor, platform);
            break;
    }
}

/*=============================================================================
 * Editor Render (delegates to editor_render.c)
 *===========================================================================*/

void editor_render(EditorState *editor, Renderer *renderer, Platform *platform) {
    if (!editor || !editor->active) return;
    editor_render_viewport(editor, renderer, platform);
}
