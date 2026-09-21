/*
 * Stage Builder / Editor Mode - Core Implementation
 *
 * Handles editor lifecycle (create, enter, exit, free),
 * input processing, table picker, and object manipulation.
 */

#include "game/editor.h"
#include "game/editor_mask.h"
#include "game/game_state.h"
#include "game/config_data.h"
#include "platform/platform.h"
#include "game/editor_render.h"
#include "game/editor_serialize.h"
#include "game/scripting.h"
#include "game/collision.h"
#include "game/flippers.h"
#include "game/billboard.h"
#include "audio/audio.h"
#include "renderer/stage_assets.h"
#include "renderer/tile_loader.h"
#include "cJSON.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/stat.h>

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

    /* Link mode defaults */
    editor->link_mode_source = -1;
    editor->linking = false;
    editor->selected_link = -1;
    editor->show_link_overlay = true;  /* Default ON so links are visible */
    editor->template_picker_open = false;
    editor->template_placing = false;

    /* Collision tile selection/hover */
    editor->hovered_coll_col = -1;
    editor->hovered_coll_row = -1;
    editor->selected_coll_col = -1;
    editor->selected_coll_row = -1;
    editor->dragging_coll_tile = false;

    /* Undo stack */
    editor->undo_stack = calloc(MAX_EDITOR_UNDO, sizeof(EditorTable));
    editor->undo_count = 0;

    /* Mask editor */
    editor->mask_editor = mask_editor_create();

    return editor;
}

void editor_free(EditorState *editor) {
    if (!editor) return;
    mask_editor_free(editor->mask_editor);
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

    /* Add [+ New Table] entry at the end */
    if (editor->picker_num_tables < MAX_PICKER_TABLES) {
        PickerEntry *entry = &editor->picker_tables[editor->picker_num_tables];
        strncpy(entry->name, "[+ New Table]", sizeof(entry->name) - 1);
        entry->folder[0] = '\0';
        entry->folder_name[0] = '\0';
        entry->is_builtin = false;
        editor->picker_num_tables++;
    }

    editor->picker_scanned = true;
    printf("[EDITOR] Scan complete: %d tables found (including New Table option)\n",
           editor->picker_num_tables);
}

/*=============================================================================
 * Config Object Auto-Import
 *===========================================================================*/

/* Map config group name to editor ComponentType */
static ComponentType config_group_to_component_type(const char *name) {
    if (strcmp(name, "voltorb") == 0)           return COMP_BUMPER;
    if (strcmp(name, "shellder") == 0)          return COMP_BUMPER;
    if (strcmp(name, "bumpers") == 0)           return COMP_BUMPER;
    if (strcmp(name, "spinner") == 0)           return COMP_SPINNER;
    if (strcmp(name, "board_triggers") == 0)    return COMP_BOARD_TRIGGER;
    if (strcmp(name, "top_staryu") == 0)        return COMP_STARYU;
    if (strcmp(name, "bottom_staryu") == 0)     return COMP_STARYU;
    if (strcmp(name, "bellsprout") == 0)        return COMP_FIELD_CREATURE;
    if (strcmp(name, "slowpoke") == 0)          return COMP_FIELD_CREATURE;
    if (strcmp(name, "cloyster") == 0)          return COMP_FIELD_CREATURE;
    if (strcmp(name, "psyduck_poliwag") == 0)   return COMP_FIELD_CREATURE;
    if (strcmp(name, "upgrade_triggers") == 0)  return COMP_BALL_UPGRADE;
    if (strcmp(name, "wild_mon") == 0)          return COMP_WILD_POKEMON;
    if (strcmp(name, "pikachu") == 0)           return COMP_PIKACHU_SAVER;
    if (strcmp(name, "cave_lights") == 0)       return COMP_CAVE_LIGHT;
    if (strcmp(name, "diglett") == 0)           return COMP_DIGLETT;
    if (strcmp(name, "slot") == 0)              return COMP_SLOT_MACHINE;
    if (strcmp(name, "launch_alley") == 0)      return COMP_LAUNCH_ALLEY;
    if (strcmp(name, "bonus_multipliers") == 0) return COMP_RAILING;
    if (strcmp(name, "ditto_slot") == 0)        return COMP_DITTO_SLOT;
    return COMP_BOARD_TRIGGER; /* Fallback for unknown groups */
}

/* Import objects from a TableConfig into EditorObjects.
 * y_offset: pixel offset for bottom stage in combined view (144 = 18 rows * 8px). */
static void editor_import_config_objects(EditorState *editor, const TableConfig *table, int y_offset) {
    for (int g = 0; g < table->num_groups; g++) {
        const TableObjectGroup *grp = &table->groups[g];
        ComponentType comp_type = config_group_to_component_type(grp->name);

        for (int o = 0; o < grp->num_objects; o++) {
            if (editor->table.num_objects >= MAX_EDITOR_OBJECTS) {
                printf("[EDITOR] Warning: MAX_EDITOR_OBJECTS reached during config import\n");
                return;
            }

            EditorObject *obj = &editor->table.objects[editor->table.num_objects++];
            memset(obj, 0, sizeof(EditorObject));
            obj->type = comp_type;
            obj->x = grp->objects[o].x;
            obj->y = (uint16_t)(grp->objects[o].y + y_offset);
            obj->x_thresh = grp->x_thresh;
            obj->y_thresh = grp->y_thresh;
            obj->attribute_gated = grp->attribute_gated;
            if (grp->attribute_gated) {
                int a;
                for (a = 0; a < CONFIG_MAX_ATTRS && grp->collision_attrs[a] != 0xFF; a++) {
                    if (a >= 16) break;  /* EditorObject attrs[16] limit */
                    obj->attrs[a] = grp->collision_attrs[a];
                }
                obj->num_attrs = a;
            }
            obj->score = component_defaults[comp_type].default_score;
            obj->bounce_force = component_defaults[comp_type].default_force;
            obj->sfx_id = component_defaults[comp_type].default_sfx;
        }
    }
}

/* Get the TableConfig for a given stage ID, or NULL if not a builtin stage */
static const TableConfig *config_for_stage(GameState *state, uint8_t stage_id) {
    if (!state || !state->config) return NULL;
    switch (stage_id) {
        case 0x0: return &state->config->red_field_top;
        case 0x1: return &state->config->red_field_bottom;
        case 0x4: return &state->config->blue_field_top;
        case 0x5: return &state->config->blue_field_bottom;
        default:  return NULL;
    }
}

/*=============================================================================
 * Open Table from Picker
 *===========================================================================*/

void editor_open_table(EditorState *editor, int picker_index, GameState *state) {
    if (!editor || picker_index < 0 || picker_index >= editor->picker_num_tables) return;

    PickerEntry *entry = &editor->picker_tables[picker_index];

    /* Close mask editor and reset all transient state from previous table */
    if (editor->mask_editor && editor->mask_editor->active) {
        editor->mask_editor->active = false;  /* Discard without applying to old table */
    }
    editor->selected_object = -1;
    editor->selected_coll_col = -1;
    editor->hovered_coll_col = -1;
    editor->dragging_object = false;
    editor->dragging_coll_tile = false;
    editor->fill_active = false;
    editor->show_checklist = false;
    editor->current_tool = TOOL_SELECT;
    editor->palette_selection = COMP_NONE;
    editor->undo_count = 0;
    editor->undo_pushed_this_stroke = false;
    editor->has_custom_tiles_top = false;
    editor->has_custom_tiles_bottom = false;
    editor->linking = false;
    editor->link_mode_source = -1;
    editor->selected_link = -1;
    editor->template_picker_open = false;
    editor->template_placing = false;

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

                /* Parse links array */
                cJSON *links_arr = cJSON_GetObjectItem(mroot, "links");
                if (links_arr && cJSON_IsArray(links_arr)) {
                    editor->table.num_links = 0;
                    cJSON *link_item;
                    cJSON_ArrayForEach(link_item, links_arr) {
                        if (editor->table.num_links >= MAX_TABLE_LINKS) break;
                        ObjectLink *link = &editor->table.links[editor->table.num_links];
                        cJSON *src = cJSON_GetObjectItem(link_item, "source");
                        cJSON *tgt = cJSON_GetObjectItem(link_item, "target");
                        cJSON *typ = cJSON_GetObjectItem(link_item, "type");
                        cJSON *thr = cJSON_GetObjectItem(link_item, "threshold");
                        cJSON *tmr = cJSON_GetObjectItem(link_item, "timer");

                        link->source_idx = src && cJSON_IsNumber(src) ? src->valueint : 0;
                        link->target_idx = tgt && cJSON_IsNumber(tgt) ? tgt->valueint : 0;
                        link->threshold = thr && cJSON_IsNumber(thr) ? thr->valueint : 0;
                        link->timer_frames = tmr && cJSON_IsNumber(tmr) ? tmr->valueint : 0;

                        /* Parse link type string */
                        link->type = LINK_NONE;
                        if (typ && cJSON_IsString(typ)) {
                            const char *ts = typ->valuestring;
                            if (strcmp(ts, "triggers") == 0) link->type = LINK_TRIGGERS;
                            else if (strcmp(ts, "charges") == 0) link->type = LINK_CHARGES;
                            else if (strcmp(ts, "toggles") == 0) link->type = LINK_TOGGLES;
                            else if (strcmp(ts, "sequence") == 0) link->type = LINK_SEQUENCE;
                        }

                        if (link->type != LINK_NONE) {
                            editor->table.num_links++;
                        }
                    }
                    printf("[EDITOR] Loaded %d links from manifest\n", editor->table.num_links);
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
             * The GBC collision map has 2 rows of solid padding above the visible area.
             * Collision source row 2 aligns with tilemap row 0. Shift by -2 during import
             * so collision_map[R][C] visually aligns with tilemap[R][C] in the editor. */
            for (int i = 0; i < 0x300; i++) {
                int coll_row = i / 32;
                int col = i % 32;
                int dest_row = coll_row - 2;
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
            /* Import bottom collision into rows 32+ (same 2-row offset as top) */
            for (int i = 0; i < 0x300; i++) {
                int coll_row = i / 32;
                int col = i % 32;
                int dest_row = coll_row - 2;
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

            /* Auto-import game objects from config data for builtin tables */
            {
                const TableConfig *top_cfg = config_for_stage(state, top_stage_id);
                const TableConfig *bot_cfg = config_for_stage(state, bottom_stage_id);
                if (top_cfg && top_cfg->num_groups > 0) {
                    editor_import_config_objects(editor, top_cfg, 0);
                    printf("[EDITOR] Imported %d config groups from top stage 0x%02X\n",
                           top_cfg->num_groups, top_stage_id);
                }
                if (bot_cfg && bot_cfg->num_groups > 0) {
                    /* Bottom stage objects offset by 18 visible rows * 8px = 144 */
                    editor_import_config_objects(editor, bot_cfg, 18 * 8);
                    printf("[EDITOR] Imported %d config groups from bottom stage 0x%02X\n",
                           bot_cfg->num_groups, bottom_stage_id);
                }
            }

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

    /* Load any persisted custom data (collision, tilemaps, tilesets) */
    editor_load_custom_data(editor);

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

    /* Stop all music/SFX when entering editor */
    audio_stop_all(state->audio);

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
        /* Restore windowed mode if we forced fullscreen on enter */
        if (editor->forced_fullscreen && platform_is_fullscreen(platform)) {
            platform_toggle_fullscreen(platform);
            editor->forced_fullscreen = false;
        }
    } else {
        /* Enter editor from any screen — table picker will be shown */
        editor_enter(editor, state);
        state->editor_mode = 1;
        /* Prevent ESC from quitting while editor is open */
        platform_set_esc_quits(platform, false);
        /* Force fullscreen for editor workspace */
        if (!platform_is_fullscreen(platform)) {
            platform_toggle_fullscreen(platform);
            editor->forced_fullscreen = true;
        } else {
            editor->forced_fullscreen = false;
        }
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

    /* Copy collision map with 2-row alignment offset.
     * GBC collision rows 0-1 are above-screen border padding;
     * collision row 2 aligns with tilemap row 0. */
    memset(t->collision_map, 0, sizeof(t->collision_map));
    for (int i = 0; i < (int)sizeof(state->stage_collision_map); i++) {
        int coll_row = i / 32;
        int col = i % 32;
        int dest_row = coll_row - 2;
        if (dest_row >= 0 && dest_row < 64) {
            t->collision_map[dest_row][col] = state->stage_collision_map[i];
        }
    }

    printf("[EDITOR] Imported stage 0x%02X (addressing: %s)\n",
           t->stage_id, t->unsigned_addressing ? "unsigned" : "signed");
}

/*=============================================================================
 * Load Custom Data (collision, tilemaps, tilesets) from table's data/ folder
 *===========================================================================*/

void editor_load_custom_data(EditorState *editor) {
    if (!editor || !editor->table.source_folder[0]) return;

    char path[512];
    FILE *f;

    /* --- Collision: data/top.collision and data/bottom.collision --- */
    snprintf(path, sizeof(path), "%s/data/top.collision", editor->table.source_folder);
    f = fopen(path, "rb");
    if (f) {
        uint8_t buf[1024];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n >= 1024) {
            /* Reverse the +2 offset: disk row 2 → display row 0 */
            for (int r = 0; r < 22; r++) {
                for (int c = 0; c < 32; c++) {
                    editor->table.collision_map[r][c] = buf[(r + 2) * 32 + c];
                }
            }
            printf("[EDITOR] Loaded custom collision: top (%zu bytes)\n", n);
        }
    }

    snprintf(path, sizeof(path), "%s/data/bottom.collision", editor->table.source_folder);
    f = fopen(path, "rb");
    if (f) {
        uint8_t buf[1024];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n >= 1024) {
            for (int r = 0; r < 22; r++) {
                int data_row = r + 32;  /* Bottom half starts at data row 32 */
                for (int c = 0; c < 32; c++) {
                    editor->table.collision_map[data_row][c] = buf[(r + 2) * 32 + c];
                }
            }
            printf("[EDITOR] Loaded custom collision: bottom (%zu bytes)\n", n);
        }
    }

    /* --- Tilemaps: data/top.map + .attr, data/bottom.map + .attr --- */
    snprintf(path, sizeof(path), "%s/data/top.map", editor->table.source_folder);
    f = fopen(path, "rb");
    if (f) {
        uint8_t buf[1024];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n >= 1024) {
            for (int r = 0; r < 32; r++)
                for (int c = 0; c < 32; c++)
                    editor->table.tilemap[r][c] = buf[r * 32 + c];
            printf("[EDITOR] Loaded custom tilemap: top\n");
        }
    }
    snprintf(path, sizeof(path), "%s/data/top.attr", editor->table.source_folder);
    f = fopen(path, "rb");
    if (f) {
        uint8_t buf[1024];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n >= 1024) {
            for (int r = 0; r < 32; r++)
                for (int c = 0; c < 32; c++)
                    editor->table.tilemap_attrs[r][c] = buf[r * 32 + c];
        }
    }

    snprintf(path, sizeof(path), "%s/data/bottom.map", editor->table.source_folder);
    f = fopen(path, "rb");
    if (f) {
        uint8_t buf[1024];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n >= 1024) {
            for (int r = 0; r < 32; r++)
                for (int c = 0; c < 32; c++)
                    editor->table.tilemap[r + 32][c] = buf[r * 32 + c];
            printf("[EDITOR] Loaded custom tilemap: bottom\n");
        }
    }
    snprintf(path, sizeof(path), "%s/data/bottom.attr", editor->table.source_folder);
    f = fopen(path, "rb");
    if (f) {
        uint8_t buf[1024];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n >= 1024) {
            for (int r = 0; r < 32; r++)
                for (int c = 0; c < 32; c++)
                    editor->table.tilemap_attrs[r + 32][c] = buf[r * 32 + c];
        }
    }

    /* --- Palettes: data/palettes.bin (128 bytes = 8 palettes x 4 colors x 2 bytes) --- */
    snprintf(path, sizeof(path), "%s/data/palettes.bin", editor->table.source_folder);
    f = fopen(path, "rb");
    if (f) {
        uint8_t buf[128];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (n >= 128) {
            for (int p = 0; p < 8; p++) {
                for (int c = 0; c < 4; c++) {
                    uint16_t val = buf[(p * 4 + c) * 2] | (buf[(p * 4 + c) * 2 + 1] << 8);
                    editor->table.bg_palettes[p].colors[c] = val;
                    editor->palettes_top[p].colors[c] = val;
                    editor->palettes_bottom[p].colors[c] = val;
                }
            }
            printf("[EDITOR] Loaded custom palettes (128 bytes)\n");
        }
    }

    /* --- Tilesets: data/top_tiles.png and data/bottom_tiles.png --- */
    editor->has_custom_tiles_top = false;
    editor->has_custom_tiles_bottom = false;

    snprintf(path, sizeof(path), "%s/data/top_tiles.png", editor->table.source_folder);
    {
        size_t tile_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &tile_size);
        if (tile_data && tile_size > 0) {
            size_t copy_size = tile_size < 6144 ? tile_size : 6144;
            memcpy(editor->vram_top[0], tile_data, copy_size);
            editor->has_custom_tiles_top = true;
            printf("[EDITOR] Loaded custom tileset: top (%zu bytes)\n", tile_size);
        }
        free(tile_data);
    }

    snprintf(path, sizeof(path), "%s/data/bottom_tiles.png", editor->table.source_folder);
    {
        size_t tile_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &tile_size);
        if (tile_data && tile_size > 0) {
            size_t copy_size = tile_size < 6144 ? tile_size : 6144;
            memcpy(editor->vram_bottom[0], tile_data, copy_size);
            editor->has_custom_tiles_bottom = true;
            printf("[EDITOR] Loaded custom tileset: bottom (%zu bytes)\n", tile_size);
        }
        free(tile_data);
    }
}

/*=============================================================================
 * Create New Blank Table
 *===========================================================================*/

void editor_create_new_table(EditorState *editor, GameState *state) {
    if (!editor) return;

    /* Auto-generate name: custom_table_1, custom_table_2, etc. */
    char folder_name[64];
    char folder_path[512];
    int suffix = 1;
    while (suffix < 100) {
        snprintf(folder_name, sizeof(folder_name), "custom_table_%d", suffix);
        snprintf(folder_path, sizeof(folder_path), "%stables/%s",
                 editor->asset_base_path, folder_name);
        struct stat st;
        if (stat(folder_path, &st) != 0) break;  /* Folder doesn't exist, use this name */
        suffix++;
    }

    /* Create directory structure */
    char scripts_dir[512], data_dir[512];
    snprintf(scripts_dir, sizeof(scripts_dir), "%s/scripts", folder_path);
    snprintf(data_dir, sizeof(data_dir), "%s/data", folder_path);

    #ifdef _WIN32
    _mkdir(folder_path);
    _mkdir(scripts_dir);
    _mkdir(data_dir);
    #else
    mkdir(folder_path, 0755);
    mkdir(scripts_dir, 0755);
    mkdir(data_dir, 0755);
    #endif

    /* Initialize editor table */
    memset(&editor->table, 0, sizeof(EditorTable));
    snprintf(editor->table.name, sizeof(editor->table.name), "Custom Table %d", suffix);
    strncpy(editor->table.source_folder, folder_path, sizeof(editor->table.source_folder) - 1);
    editor->table.stage_id = 0x01;  /* Red field bottom (odd = flippers) */
    editor->table.has_flippers = true;
    editor->table.tilemap_rows = 64;
    editor->table.tilemap_cols = 32;
    editor->table.unsigned_addressing = false;
    editor->table.has_vram = false;  /* No VRAM tiles yet */
    editor->table.default_scx = 0x20;  /* Standard bottom stage offset */

    /* Set up combined view */
    editor->combined_view = true;
    editor->hide_buffer_rows = true;
    editor->top_stage_id = 0x00;
    editor->bottom_stage_id = 0x01;

    /* Default palettes (grayscale) */
    for (int p = 0; p < 8; p++) {
        editor->table.bg_palettes[p].colors[0] = 0x7FFF;  /* White */
        editor->table.bg_palettes[p].colors[1] = 0x5294;  /* Light gray */
        editor->table.bg_palettes[p].colors[2] = 0x294A;  /* Dark gray */
        editor->table.bg_palettes[p].colors[3] = 0x0000;  /* Black */
        editor->table.obj_palettes[p] = editor->table.bg_palettes[p];
    }
    memcpy(editor->palettes_top, editor->table.bg_palettes, sizeof(editor->palettes_top));
    memcpy(editor->palettes_bottom, editor->table.bg_palettes, sizeof(editor->palettes_bottom));

    /* Generate template collision data.
     * In the editor, rows 0-17 are top visible, rows 32-49 are bottom visible
     * (display rows 0-17 and 18-35 when hide_buffer_rows is on). */

    /* --- Top half (display rows 0-17, data rows 0-17) --- */
    /* Row 0: solid top border (including launch alley cap) */
    for (int c = 0; c < 24; c++)
        editor->table.collision_map[0][c] = 0x01;

    /* Rows 1-15: walls on sides, passable interior + launch alley channel */
    for (int r = 1; r <= 15; r++) {
        editor->table.collision_map[r][0] = 0x01;     /* Left wall */
        editor->table.collision_map[r][19] = 0x01;    /* Right wall (main field) */
        for (int c = 1; c < 19; c++)
            editor->table.collision_map[r][c] = 0x00; /* Passable */
        /* Launch alley channel continues through top field */
        editor->table.collision_map[r][20] = 0x01;    /* Left alley wall */
        editor->table.collision_map[r][21] = 0x00;    /* Alley interior */
        editor->table.collision_map[r][22] = 0x00;    /* Alley interior */
        editor->table.collision_map[r][23] = 0x01;    /* Right alley wall */
    }

    /* Rows 16-17: transition zone (including alley passthrough) */
    for (int c = 0; c < 20; c++) {
        editor->table.collision_map[16][c] = 0xFF;
        editor->table.collision_map[17][c] = 0xFF;
    }
    /* Alley continues through transition zone */
    editor->table.collision_map[16][20] = 0x01;
    editor->table.collision_map[16][21] = 0x00;
    editor->table.collision_map[16][22] = 0x00;
    editor->table.collision_map[16][23] = 0x01;
    editor->table.collision_map[17][20] = 0x01;
    editor->table.collision_map[17][21] = 0x00;
    editor->table.collision_map[17][22] = 0x00;
    editor->table.collision_map[17][23] = 0x01;

    /* --- Bottom half (display rows 18-35, data rows 32-49) --- */
    /* Rows 0-1 (data 32-33): transition zone (scroll to top) + alley passthrough */
    for (int c = 0; c < 20; c++) {
        editor->table.collision_map[32][c] = 0xFF;
        editor->table.collision_map[33][c] = 0xFF;
    }
    /* Alley continues through bottom transition zone */
    editor->table.collision_map[32][20] = 0x01;
    editor->table.collision_map[32][21] = 0x00;
    editor->table.collision_map[32][22] = 0x00;
    editor->table.collision_map[32][23] = 0x01;
    editor->table.collision_map[33][20] = 0x01;
    editor->table.collision_map[33][21] = 0x00;
    editor->table.collision_map[33][22] = 0x00;
    editor->table.collision_map[33][23] = 0x01;

    /* Rows 2-11 (data 34-43): walls on sides, passable interior */
    for (int r = 34; r <= 43; r++) {
        editor->table.collision_map[r][0] = 0x01;
        editor->table.collision_map[r][19] = 0x01;
        for (int c = 1; c < 19; c++)
            editor->table.collision_map[r][c] = 0x00;
    }

    /* Rows 12-13 (data 44-45): flipper zones */
    for (int c = 0; c < 20; c++) {
        if (c < 10) {
            editor->table.collision_map[44][c] = 0xE0;  /* Left flipper */
            editor->table.collision_map[45][c] = 0xE0;
        } else {
            editor->table.collision_map[44][c] = 0xF0;  /* Right flipper */
            editor->table.collision_map[45][c] = 0xF0;
        }
    }

    /* Rows 14-15 (data 46-47): funnel walls guiding to drain */
    for (int c = 0; c < 20; c++) {
        editor->table.collision_map[46][c] = 0x01;
        editor->table.collision_map[47][c] = 0x01;
    }
    /* Leave center open for drain */
    for (int c = 8; c < 12; c++) {
        editor->table.collision_map[46][c] = 0x00;
        editor->table.collision_map[47][c] = 0x00;
    }

    /* Rows 16-17 (data 48-49): drain zone */
    for (int c = 0; c < 20; c++) {
        editor->table.collision_map[48][c] = 0xFF;
        editor->table.collision_map[49][c] = 0xFF;
    }

    /* --- Launch alley channel (cols 20-23, bottom play area) ---
     * The ball spawns at x=0xA7 (col ~20) with SCX=0x20. The alley in the
     * top half is already set up above. Here we fill the remaining bottom
     * play area rows (data 34-49) that weren't covered by the transition. */
    for (int r = 34; r <= 49; r++) {
        editor->table.collision_map[r][20] = 0x01;  /* Left wall of alley */
        editor->table.collision_map[r][23] = 0x01;  /* Right wall of alley */
        editor->table.collision_map[r][21] = 0x00;  /* Interior passable */
        editor->table.collision_map[r][22] = 0x00;
    }
    /* Bottom cap of alley */
    editor->table.collision_map[49][21] = 0x01;
    editor->table.collision_map[49][22] = 0x01;

    /* --- Default objects --- */
    editor->table.num_objects = 0;

    /* Launch alley trigger (matches Red field: position 0xA8,0x98, bounds 8x8)
     * In editor coords: x=0xA8=168, but in bottom-half display the Y needs
     * adjustment. The ball spawns at y=0x98=152 which is row 19 in screen
     * coords. In bottom-half editor data rows, that's data_row = 32+19 = 51,
     * but in display coords (with hide_buffer) it's display_row = 18+19 = 37.
     * The object world position = display_row * 8 for Y. However, objects use
     * the game's absolute coordinate space, so place at the matching position. */
    {
        EditorObject *obj = &editor->table.objects[editor->table.num_objects++];
        memset(obj, 0, sizeof(EditorObject));
        obj->type = COMP_LAUNCH_ALLEY;
        obj->x = 0xA8;   /* Same as Red field launch alley */
        obj->y = 0x98;   /* Same as Red field ball spawn area */
        editor_component_default_bbox(COMP_LAUNCH_ALLEY, &obj->x_thresh, &obj->y_thresh);
        obj->score = editor_component_default_score(COMP_LAUNCH_ALLEY);
        obj->bounce_force = component_defaults[COMP_LAUNCH_ALLEY].default_force;
        obj->sfx_id = component_defaults[COMP_LAUNCH_ALLEY].default_sfx;
    }

    /* Default bumper in center of bottom play area */
    {
        EditorObject *obj = &editor->table.objects[editor->table.num_objects++];
        memset(obj, 0, sizeof(EditorObject));
        obj->type = COMP_BUMPER;
        obj->x = 80;    /* Center of 160px visible area */
        obj->y = 64;    /* Upper-mid area of bottom stage (game coords) */
        editor_component_default_bbox(COMP_BUMPER, &obj->x_thresh, &obj->y_thresh);
        obj->score = editor_component_default_score(COMP_BUMPER);
        obj->bounce_force = component_defaults[COMP_BUMPER].default_force;
        obj->sfx_id = component_defaults[COMP_BUMPER].default_sfx;
    }

    /* Initialize blank VRAM tile data so playtest doesn't show Red field tiles.
     * Zero-filled tiles render as color index 0 (white/first palette color). */
    memset(editor->vram_top, 0, sizeof(editor->vram_top));
    memset(editor->vram_bottom, 0, sizeof(editor->vram_bottom));
    editor->has_custom_tiles_top = true;
    editor->has_custom_tiles_bottom = true;

    /* Serialize the initial table (creates manifest + scripts + data) */
    editor_serialize_table(editor, folder_path);

    /* Set up editor state for the new table */
    editor->vram_ref = state->vram;
    editor->selected_object = -1;
    editor->current_tool = TOOL_SELECT;
    editor->screen = EDITOR_SCREEN_EDITOR;
    editor->needs_center_view = true;
    editor->undo_count = 0;
    editor->undo_pushed_this_stroke = false;

    printf("[EDITOR] Created new table: %s at %s\n", editor->table.name, folder_path);
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
               32 * 32);  /* 32 remaining rows x 32 columns */
    }
    editor->playtest_has_collision = true;

    /* Flag custom tiles for VRAM injection after stage asset loading */
    editor->playtest_has_custom_tiles =
        (editor->has_custom_tiles_top || editor->has_custom_tiles_bottom);

    /* Snapshot editor palettes for injection after stage asset loading */
    memcpy(editor->playtest_palettes_top, editor->palettes_top,
           sizeof(editor->playtest_palettes_top));
    memcpy(editor->playtest_palettes_bottom, editor->palettes_bottom,
           sizeof(editor->playtest_palettes_bottom));
    editor->playtest_has_custom_palettes = true;

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

/* Forward declarations */
static void editor_place_template(EditorState *editor, int tmpl_idx, int cx, int cy);

/*=============================================================================
 * Editor Update - Main Editor Screen
 *===========================================================================*/

static void editor_update_editor(EditorState *editor, Platform *platform, GameState *state) {
    /* If mask editor panel is open, it consumes all input */
    if (editor->mask_editor && editor->mask_editor->active) {
        int me_win_w = 640, me_win_h = 576;
        SDL_Window *me_win = SDL_GetMouseFocus();
        if (me_win) SDL_GetWindowSize(me_win, &me_win_w, &me_win_h);
        if (mask_editor_handle_input(editor->mask_editor, editor,
                editor->mouse_x, editor->mouse_y,
                editor->mouse_left_clicked, editor->mouse_right_clicked,
                editor->mouse_left_down, editor->mouse_right_down,
                me_win_w, me_win_h)) {
            /* ESC is handled in main.c before editor_update runs */
            /* Ctrl+Z for mask undo */
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            static Uint8 prev_z = 0;
            if (keys[SDL_SCANCODE_Z] && !prev_z &&
                (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])) {
                mask_editor_undo(editor->mask_editor);
            }
            /* Ctrl+Y for mask redo */
            static Uint8 prev_y = 0;
            if (keys[SDL_SCANCODE_Y] && !prev_y &&
                (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])) {
                mask_editor_redo(editor->mask_editor);
            }
            prev_z = keys[SDL_SCANCODE_Z];
            prev_y = keys[SDL_SCANCODE_Y];
            return;
        }
    }

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
        if (editor->current_tool == TOOL_SELECT && editor->selected_object < 0 && !editor->linking) {
            /* Toggle template picker */
            editor->template_picker_open = !editor->template_picker_open;
            editor->template_placing = false;
            editor->template_cursor = 0;
        } else if (!editor->linking) {
            editor->current_tool = TOOL_TILE_PAINT;
            editor->palette_selection = COMP_NONE;
        }
    }
    if (KEY_PRESSED(SDL_SCANCODE_X)) {
        editor->current_tool = TOOL_COLL_PAINT;
        editor->palette_selection = COMP_NONE;
    }
    if (KEY_PRESSED(SDL_SCANCODE_E)) {
        editor->current_tool = TOOL_ERASE;
        editor->palette_selection = COMP_NONE;
    }
    /* L key: link mode (select tool + object selected), or palette tool otherwise.
     * Link mode takes priority so L never accidentally switches to palette
     * when you're trying to link objects. Use K for palette instead. */
    if (KEY_PRESSED(SDL_SCANCODE_L)) {
        if (editor->linking) {
            /* Cycle link type with repeated L presses */
            editor->link_type_cycle++;
            if (editor->link_type_cycle >= LINK_COUNT) editor->link_type_cycle = LINK_TRIGGERS;
        } else if (editor->selected_object >= 0) {
            /* Enter link creation mode (works from any tool) */
            editor->current_tool = TOOL_SELECT;
            editor->linking = true;
            editor->link_mode_source = editor->selected_object;
            editor->link_type_cycle = LINK_TRIGGERS;
        }
    }
    if (KEY_PRESSED(SDL_SCANCODE_K)) {
        editor->current_tool = TOOL_PALETTE;
        editor->palette_selection = COMP_NONE;
    }
    if (KEY_PRESSED(SDL_SCANCODE_I)) {
        editor->show_link_overlay = !editor->show_link_overlay;
    }
    /* Q toggles ball test point overlay (collision mode visual aid) */
    if (KEY_PRESSED(SDL_SCANCODE_Q) && editor->mask_editor) {
        editor->mask_editor->show_test_points = !editor->mask_editor->show_test_points;
    }
    /* F toggles flipper sweep visualizer (requires collision overlay) */
    if (KEY_PRESSED(SDL_SCANCODE_F)) {
        editor->show_flipper_sweep = !editor->show_flipper_sweep;
    }
    /* ` (grave) cycles left panel tab */
    if (KEY_PRESSED(SDL_SCANCODE_GRAVE)) {
        editor->left_panel_tab = (editor->left_panel_tab + 1) % 2;
    }
    /* M opens the mask pixel editor for the hovered collision attribute */
    if (KEY_PRESSED(SDL_SCANCODE_M) && editor->mask_editor && state &&
        editor->show_collision_overlay && editor->hovered_coll_col >= 0 &&
        editor->hovered_coll_attr != 0) {
        mask_editor_open(editor->mask_editor, state, editor->hovered_coll_attr);
    }

    /* Tab toggles table readiness checklist (not in palette mode where Tab cycles scope) */
    if (KEY_PRESSED(SDL_SCANCODE_TAB) && editor->current_tool != TOOL_PALETTE) {
        editor->show_checklist = !editor->show_checklist;
    }

    /* Number keys 0-9 change collision attribute */
    if (editor->current_tool == TOOL_COLL_PAINT) {
        for (int k = 0; k <= 9; k++) {
            if (KEY_PRESSED(SDL_SCANCODE_0 + k)) {
                editor->paint_coll_attr = (uint8_t)(k * 0x10);
            }
        }
    }

    /* Palette editor: number keys 0-7 select palette, [/] select color slot */
    if (editor->current_tool == TOOL_PALETTE) {
        for (int k = 0; k <= 7; k++) {
            if (KEY_PRESSED(SDL_SCANCODE_0 + k)) {
                editor->pal_selected_palette = k;
            }
        }
        if (KEY_PRESSED(SDL_SCANCODE_LEFTBRACKET)) {
            editor->pal_selected_color = (editor->pal_selected_color + 3) % 4;  /* wrap left */
        }
        if (KEY_PRESSED(SDL_SCANCODE_RIGHTBRACKET)) {
            editor->pal_selected_color = (editor->pal_selected_color + 1) % 4;
        }
        /* Tab cycles scope: Both → Top → Bottom → Both */
        if (KEY_PRESSED(SDL_SCANCODE_TAB) && editor->combined_view) {
            editor->pal_edit_scope = (editor->pal_edit_scope + 1) % 3;
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
        /* R key: reload custom tileset PNGs */
        if (KEY_PRESSED(SDL_SCANCODE_R) && editor->table.source_folder[0]) {
            char path[512];
            snprintf(path, sizeof(path), "%s/data/top_tiles.png", editor->table.source_folder);
            size_t tile_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &tile_size);
            if (tile_data && tile_size > 0) {
                size_t copy_size = tile_size < 6144 ? tile_size : 6144;
                memcpy(editor->vram_top[0], tile_data, copy_size);
                editor->has_custom_tiles_top = true;
                printf("[EDITOR] Reloaded top tileset (%zu bytes)\n", tile_size);
            }
            free(tile_data);

            snprintf(path, sizeof(path), "%s/data/bottom_tiles.png", editor->table.source_folder);
            tile_data = tiles_from_png(path, &tile_size);
            if (tile_data && tile_size > 0) {
                size_t copy_size = tile_size < 6144 ? tile_size : 6144;
                memcpy(editor->vram_bottom[0], tile_data, copy_size);
                editor->has_custom_tiles_bottom = true;
                printf("[EDITOR] Reloaded bottom tileset (%zu bytes)\n", tile_size);
            }
            free(tile_data);

            editor->table.has_vram = (editor->has_custom_tiles_top || editor->has_custom_tiles_bottom);
        }
    }

    /* Ctrl+S saves the table */
    if (KEY_PRESSED(SDL_SCANCODE_S) && (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL])) {
        /* Save to the source folder if available, else _editor_output */
        const char *save_path = editor->table.source_folder[0]
            ? editor->table.source_folder : "tables/_editor_output";
        editor_serialize_table(editor, save_path);

        /* Export modified collision masks to atlas PNG + Lua snippet */
        if (editor->mask_editor &&
            (editor->mask_editor->any_modified || editor->mask_editor->any_flipper_modified)) {
            mask_editor_export_atlas(editor->mask_editor, state);
            mask_editor_export_lua_snippet(editor->mask_editor, save_path);
        }

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

    /* Flipper sweep animation tick */
    if (editor->show_flipper_sweep) {
        editor->flipper_anim_timer++;
        if (editor->flipper_anim_timer >= 4) {
            editor->flipper_anim_timer = 0;
            editor->flipper_anim_angle = (editor->flipper_anim_angle + 1) % 16;
        }
    }

    /* Template picker input: arrow keys navigate, Enter selects */
    if (editor->template_picker_open && !editor->template_placing) {
        static bool tp_up_prev = false, tp_down_prev = false, tp_enter_prev = false;
        bool tp_up = keys[SDL_SCANCODE_UP] != 0;
        bool tp_down = keys[SDL_SCANCODE_DOWN] != 0;
        bool tp_enter = keys[SDL_SCANCODE_RETURN] != 0;

        if (tp_up && !tp_up_prev && editor->template_cursor > 0) {
            editor->template_cursor--;
        }
        if (tp_down && !tp_down_prev && editor->template_cursor < NUM_BEHAVIOR_TEMPLATES - 1) {
            editor->template_cursor++;
        }
        if (tp_enter && !tp_enter_prev) {
            editor->template_selected = editor->template_cursor;
            editor->template_placing = true;
            editor->template_picker_open = false;
        }
        tp_up_prev = tp_up;
        tp_down_prev = tp_down;
        tp_enter_prev = tp_enter;
    }

    /* Link inspector: arrow keys cycle link type, +/- adjust threshold */
    if (editor->selected_link >= 0 && editor->selected_link < editor->table.num_links) {
        ObjectLink *link = &editor->table.links[editor->selected_link];
        static bool li_left_prev = false, li_right_prev = false;
        static bool li_up_prev = false, li_down_prev = false;
        bool li_left = keys[SDL_SCANCODE_LEFT] != 0;
        bool li_right = keys[SDL_SCANCODE_RIGHT] != 0;
        bool li_up = keys[SDL_SCANCODE_UP] != 0;
        bool li_down = keys[SDL_SCANCODE_DOWN] != 0;

        if (li_left && !li_left_prev && editor->current_tool == TOOL_SELECT) {
            int t = (int)link->type - 1;
            if (t < LINK_TRIGGERS) t = LINK_SEQUENCE;
            link->type = (LinkType)t;
            editor->table.dirty = true;
        }
        if (li_right && !li_right_prev && editor->current_tool == TOOL_SELECT) {
            int t = (int)link->type + 1;
            if (t >= LINK_COUNT) t = LINK_TRIGGERS;
            link->type = (LinkType)t;
            editor->table.dirty = true;
        }
        if (li_up && !li_up_prev && editor->current_tool == TOOL_SELECT &&
            link->type == LINK_CHARGES) {
            link->threshold++;
            editor->table.dirty = true;
        }
        if (li_down && !li_down_prev && editor->current_tool == TOOL_SELECT &&
            link->type == LINK_CHARGES && link->threshold > 1) {
            link->threshold--;
            editor->table.dirty = true;
        }
        li_left_prev = li_left;
        li_right_prev = li_right;
        li_up_prev = li_up;
        li_down_prev = li_down;
    }

    /* Palette editor: arrow keys adjust channel value and cycle channels */
    if (editor->current_tool == TOOL_PALETTE) {
        int pal = editor->pal_selected_palette;
        int slot = editor->pal_selected_color;
        int ch = editor->pal_edit_channel;
        int scope = editor->pal_edit_scope;  /* 0=both, 1=top, 2=bottom */

        /* Read current color from the correct source based on scope */
        uint16_t color;
        if (scope == 1) {
            color = editor->palettes_top[pal].colors[slot];
        } else if (scope == 2) {
            color = editor->palettes_bottom[pal].colors[slot];
        } else {
            color = editor->table.bg_palettes[pal].colors[slot];
        }
        int r = color & 0x1F;
        int g = (color >> 5) & 0x1F;
        int b = (color >> 10) & 0x1F;

        /* Use a repeat-rate timer for comfortable held-key adjustment */
        static int arrow_repeat_timer = 0;
        bool up_down = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN];
        bool up_edge = false, down_edge = false;
        static bool prev_up = false, prev_down = false;
        bool cur_up = keys[SDL_SCANCODE_UP] != 0;
        bool cur_down = keys[SDL_SCANCODE_DOWN] != 0;
        if (cur_up && !prev_up) { up_edge = true; arrow_repeat_timer = 15; }
        if (cur_down && !prev_down) { down_edge = true; arrow_repeat_timer = 15; }
        if (up_down && !up_edge && !down_edge) {
            arrow_repeat_timer--;
            if (arrow_repeat_timer <= 0) {
                arrow_repeat_timer = 3;
                if (cur_up) up_edge = true;
                if (cur_down) down_edge = true;
            }
        }
        prev_up = cur_up;
        prev_down = cur_down;

        bool changed = false;
        if (up_edge) {
            int *val = (ch == 0) ? &r : (ch == 1) ? &g : &b;
            if (*val < 31) { (*val)++; changed = true; }
        }
        if (down_edge) {
            int *val = (ch == 0) ? &r : (ch == 1) ? &g : &b;
            if (*val > 0) { (*val)--; changed = true; }
        }

        /* Left/Right cycle R/G/B channel */
        static bool prev_left_pal = false, prev_right_pal = false;
        bool cur_left = keys[SDL_SCANCODE_LEFT] != 0;
        bool cur_right = keys[SDL_SCANCODE_RIGHT] != 0;
        if (cur_left && !prev_left_pal) {
            editor->pal_edit_channel = (editor->pal_edit_channel + 2) % 3;
        }
        if (cur_right && !prev_right_pal) {
            editor->pal_edit_channel = (editor->pal_edit_channel + 1) % 3;
        }
        prev_left_pal = cur_left;
        prev_right_pal = cur_right;

        if (changed) {
            uint16_t new_color = (uint16_t)(r | (g << 5) | (b << 10));
            /* Write to correct targets based on scope */
            if (scope == 0) {
                /* Both: write to table + top + bottom */
                editor->table.bg_palettes[pal].colors[slot] = new_color;
                editor->palettes_top[pal].colors[slot] = new_color;
                editor->palettes_bottom[pal].colors[slot] = new_color;
            } else if (scope == 1) {
                /* Top only */
                editor->palettes_top[pal].colors[slot] = new_color;
                editor->table.bg_palettes[pal].colors[slot] = new_color;
            } else {
                /* Bottom only */
                editor->palettes_bottom[pal].colors[slot] = new_color;
                editor->table.bg_palettes[pal].colors[slot] = new_color;
            }
            editor->table.dirty = true;
        }
    } else {
        /* Arrow key panning (non-palette modes) */
        float pan_speed = 4.0f / editor->zoom;
        if (keys[SDL_SCANCODE_LEFT])  editor->camera_x -= pan_speed;
        if (keys[SDL_SCANCODE_RIGHT]) editor->camera_x += pan_speed;
        if (keys[SDL_SCANCODE_UP])    editor->camera_y -= pan_speed;
        if (keys[SDL_SCANCODE_DOWN])  editor->camera_y += pan_speed;
    }

    /* Convert mouse to world coordinates */
    float world_x = editor->camera_x + (float)raw_mx / editor->zoom;
    float world_y = editor->camera_y + (float)raw_my / editor->zoom;

    /* Determine sidebar width for click exclusion */
    int sidebar_w = EDITOR_SIDEBAR_W;  /* Must match render code */

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
                    obj->x = (uint16_t)(((int)world_x / editor->grid_size) * editor->grid_size);
                    obj->y = (uint16_t)(((int)world_y / editor->grid_size) * editor->grid_size + OBJECT_Y_DISPLAY_OFFSET);
                } else {
                    obj->x = (uint16_t)world_x;
                    obj->y = (uint16_t)((int)world_y + OBJECT_Y_DISPLAY_OFFSET);
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

    /* Handle link mode click (target selection) */
    if (editor->linking && editor->mouse_left_clicked) {
        int clicked_obj = -1;
        for (int i = editor->table.num_objects - 1; i >= 0; i--) {
            EditorObject *obj = &editor->table.objects[i];
            int dx = abs((int)world_x - (int)obj->x);
            int dy = abs((int)world_y - (int)(obj->y - OBJECT_Y_DISPLAY_OFFSET));
            if (dx <= obj->x_thresh && dy <= obj->y_thresh) {
                clicked_obj = i;
                break;
            }
        }
        if (clicked_obj >= 0 && clicked_obj != editor->link_mode_source) {
            editor_push_undo(editor);
            int link_idx = editor_add_link(&editor->table, editor->link_mode_source,
                                           clicked_obj, (LinkType)editor->link_type_cycle);
            if (link_idx >= 0) {
                editor->selected_link = link_idx;
            }
            editor->linking = false;
            editor->link_mode_source = -1;
        }
    }
    /* Handle template placement click */
    else if (editor->template_placing && editor->mouse_left_clicked) {
        int cx = (int)world_x;
        int cy = (int)world_y;
        if (editor->snap_to_grid) {
            cx = (cx / editor->grid_size) * editor->grid_size;
            cy = (cy / editor->grid_size) * editor->grid_size;
        }
        editor_place_template(editor, editor->template_selected, cx, cy);
        editor->template_placing = false;
    }
    /* Handle object selection (normal mode) */
    else if (editor->current_tool == TOOL_SELECT && editor->mouse_left_clicked) {
        editor->selected_object = -1;
        editor->selected_link = -1;
        for (int i = editor->table.num_objects - 1; i >= 0; i--) {
            EditorObject *obj = &editor->table.objects[i];
            int display_y = (int)obj->y - OBJECT_Y_DISPLAY_OFFSET;
            int dx = abs((int)world_x - (int)obj->x);
            int dy = abs((int)world_y - display_y);
            if (dx <= obj->x_thresh && dy <= obj->y_thresh) {
                editor->selected_object = i;
                editor->dragging_object = true;
                editor->drag_offset_x = (int)world_x - obj->x;
                editor->drag_offset_y = (int)world_y - display_y;
                editor_push_undo(editor);  /* Undo before drag starts */
                break;
            }
        }
    }

    /* Handle object dragging */
    if (editor->dragging_object && editor->mouse_left_down && editor->selected_object >= 0) {
        EditorObject *obj = &editor->table.objects[editor->selected_object];
        int new_x = (int)world_x - editor->drag_offset_x;
        int new_display_y = (int)world_y - editor->drag_offset_y;
        if (editor->snap_to_grid) {
            new_x = (new_x / editor->grid_size) * editor->grid_size;
            new_display_y = (new_display_y / editor->grid_size) * editor->grid_size;
        }
        int new_game_y = new_display_y + OBJECT_Y_DISPLAY_OFFSET;
        if (new_x >= 0 && new_x < 512) obj->x = (uint16_t)new_x;
        if (new_game_y >= 0 && new_game_y < 512) obj->y = (uint16_t)new_game_y;
        editor->table.dirty = true;
    }
    if (!editor->mouse_left_down) {
        editor->dragging_object = false;
        editor->dragging_coll_tile = false;
    }

    /* Delete key removes selected object or selected link */
    {
        static bool delete_was_down = false;
        bool delete_down = keys[SDL_SCANCODE_DELETE] != 0;
        if (delete_down && !delete_was_down) {
            if (editor->selected_link >= 0) {
                /* Delete selected link */
                editor_push_undo(editor);
                editor_remove_link(&editor->table, editor->selected_link);
                editor->selected_link = -1;
            } else if (editor->selected_object >= 0) {
                /* Delete selected object + its links */
                editor_push_undo(editor);
                int idx = editor->selected_object;
                editor_remove_links_for_object(&editor->table, idx);
                for (int i = idx; i < editor->table.num_objects - 1; i++) {
                    editor->table.objects[i] = editor->table.objects[i + 1];
                }
                editor->table.num_objects--;
                editor_fix_link_indices_after_delete(&editor->table, idx);
                editor->selected_object = -1;
                editor->selected_link = -1;
                editor->linking = false;
                editor->link_mode_source = -1;
                editor->table.dirty = true;
            }
        }
        delete_was_down = delete_down;
    }

    /* Modifier key flags */
    bool alt_held = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
    bool shift_held = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];

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
            /* Show hover for any tile in collision mode, only non-zero in other modes */
            if (attr != 0 || editor->current_tool == TOOL_COLL_PAINT) {
                editor->hovered_coll_col = hover_col;
                editor->hovered_coll_row = hover_disp_row;
                editor->hovered_coll_attr = attr;
            }
        }
    }

    /* === Eyedropper: Alt+Click samples collision attribute === */
    if (editor->mouse_left_clicked && alt_held && editor->show_collision_overlay) {
        int sample_col = (int)(world_x / 8.0f);
        int sample_disp_row = (int)(world_y / 8.0f);
        int sample_data_row = editor_display_to_data_row(editor, sample_disp_row);
        if (sample_col >= 0 && sample_col < max_cols &&
            sample_disp_row >= 0 && sample_disp_row < disp_rows && sample_data_row < 64) {
            uint8_t sampled = editor->table.collision_map[sample_data_row][sample_col];
            editor->paint_coll_attr = sampled;
            editor->sampled_coll_attr = sampled;
            editor->has_sampled_attr = true;
            if (editor->current_tool != TOOL_COLL_PAINT) {
                editor->current_tool = TOOL_COLL_PAINT;
                editor->palette_selection = COMP_NONE;
            }
        }
    }

    /* === Fill Rectangle: Shift+Drag === */
    if (editor->current_tool == TOOL_COLL_PAINT && shift_held && !editor->fill_active) {
        if (editor->mouse_left_clicked || editor->mouse_right_clicked) {
            int fc = (int)(world_x / 8.0f);
            int fr = (int)(world_y / 8.0f);
            if (fc >= 0 && fc < max_cols && fr >= 0 && fr < disp_rows) {
                editor->fill_start_col = fc;
                editor->fill_start_row = fr;
                editor->fill_active = true;
                editor->fill_erasing = editor->mouse_right_clicked;
                editor_push_undo(editor);
                editor->undo_pushed_this_stroke = true;
            }
        }
    }

    if (editor->fill_active) {
        if (!shift_held) {
            /* Shift released mid-drag: cancel fill, undo the push */
            editor->fill_active = false;
            editor_undo(editor);
        } else if (!editor->mouse_left_down && !editor->mouse_right_down) {
            /* Mouse released: commit fill */
            int cur_col = (int)(world_x / 8.0f);
            int cur_row = (int)(world_y / 8.0f);

            int c0 = editor->fill_start_col < cur_col ? editor->fill_start_col : cur_col;
            int c1 = editor->fill_start_col > cur_col ? editor->fill_start_col : cur_col;
            int r0 = editor->fill_start_row < cur_row ? editor->fill_start_row : cur_row;
            int r1 = editor->fill_start_row > cur_row ? editor->fill_start_row : cur_row;

            if (c0 < 0) c0 = 0;
            if (r0 < 0) r0 = 0;
            if (c1 >= max_cols) c1 = max_cols - 1;
            if (r1 >= disp_rows) r1 = disp_rows - 1;

            uint8_t fill_val = editor->fill_erasing ? 0x00 : editor->paint_coll_attr;
            for (int r = r0; r <= r1; r++) {
                int dr = editor_display_to_data_row(editor, r);
                if (dr < 0 || dr >= 64) continue;
                for (int c = c0; c <= c1; c++) {
                    editor->table.collision_map[dr][c] = fill_val;
                }
            }
            editor->table.dirty = true;
            editor->fill_active = false;
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

    /* Collision painting (guarded: skip if Alt or Shift held, or fill active) */
    if (editor->current_tool == TOOL_COLL_PAINT && editor->mouse_left_down &&
        !alt_held && !shift_held && !editor->fill_active) {
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

    /* Erasing: right-click clears collision (guarded: skip if Alt or Shift held, or fill active) */
    if (editor->current_tool == TOOL_COLL_PAINT && editor->mouse_right_down &&
        !alt_held && !shift_held && !editor->fill_active) {
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

/*=============================================================================
 * Object Link CRUD Operations
 *===========================================================================*/

static const char *link_type_names[] = {
    "None", "Triggers", "Charges", "Toggles", "Sequence"
};

const char *editor_link_type_name(LinkType type) {
    if (type >= 0 && type < LINK_COUNT) return link_type_names[type];
    return "Unknown";
}

int editor_add_link(EditorTable *table, int source, int target, LinkType type) {
    if (!table || source < 0 || target < 0 || source == target) return -1;
    if (table->num_links >= MAX_TABLE_LINKS) return -1;

    /* Check for duplicate */
    for (int i = 0; i < table->num_links; i++) {
        if (table->links[i].source_idx == source &&
            table->links[i].target_idx == target) {
            return -1;
        }
    }

    /* Count outgoing links from source */
    int out_count = 0;
    for (int i = 0; i < table->num_links; i++) {
        if (table->links[i].source_idx == source) out_count++;
    }
    if (out_count >= MAX_OBJECT_LINKS) return -1;

    ObjectLink *link = &table->links[table->num_links];
    link->source_idx = source;
    link->target_idx = target;
    link->type = type;
    link->threshold = (type == LINK_CHARGES) ? 3 : 0;
    link->timer_frames = 0;
    table->dirty = true;

    return table->num_links++;
}

void editor_remove_link(EditorTable *table, int link_idx) {
    if (!table || link_idx < 0 || link_idx >= table->num_links) return;
    for (int i = link_idx; i < table->num_links - 1; i++) {
        table->links[i] = table->links[i + 1];
    }
    table->num_links--;
    table->dirty = true;
}

void editor_remove_links_for_object(EditorTable *table, int obj_idx) {
    if (!table) return;
    for (int i = table->num_links - 1; i >= 0; i--) {
        if (table->links[i].source_idx == obj_idx ||
            table->links[i].target_idx == obj_idx) {
            editor_remove_link(table, i);
        }
    }
}

void editor_fix_link_indices_after_delete(EditorTable *table, int deleted_idx) {
    if (!table) return;
    for (int i = 0; i < table->num_links; i++) {
        if (table->links[i].source_idx > deleted_idx) table->links[i].source_idx--;
        if (table->links[i].target_idx > deleted_idx) table->links[i].target_idx--;
    }
}

/*=============================================================================
 * Behavior Templates (Phase 5)
 *===========================================================================*/

static const BehaviorTemplate builtin_templates[NUM_BEHAVIOR_TEMPLATES] = {
    {
        "N-Hit Combo",
        "3 bumpers that charge a reward trigger",
        4, /* num_objects */
        {
            { COMP_BUMPER, -24, -16 },
            { COMP_BUMPER,  24, -16 },
            { COMP_BUMPER,   0,  16 },
            { COMP_BOARD_TRIGGER, 0, -32 },
        },
        3, /* num_links */
        {
            { 0, 3, LINK_CHARGES, 1 },
            { 1, 3, LINK_CHARGES, 1 },
            { 2, 3, LINK_CHARGES, 1 },
        }
    },
    {
        "Charge Saver",
        "Spinner charges Pikachu saver (5 hits)",
        2,
        {
            { COMP_SPINNER, -16, 0 },
            { COMP_PIKACHU_SAVER, 16, 0 },
        },
        1,
        {
            { 0, 1, LINK_CHARGES, 5 },
        }
    },
    {
        "Toggle Pair",
        "2 field creatures that alternate on/off",
        2,
        {
            { COMP_FIELD_CREATURE, -16, 0 },
            { COMP_FIELD_CREATURE,  16, 0 },
        },
        2,
        {
            { 0, 1, LINK_TOGGLES, 0 },
            { 1, 0, LINK_TOGGLES, 0 },
        }
    },
    {
        "CAVE Sequence",
        "4 lights in sequence unlock a reward",
        5,
        {
            { COMP_CAVE_LIGHT, -24, 0 },
            { COMP_CAVE_LIGHT,  -8, 0 },
            { COMP_CAVE_LIGHT,   8, 0 },
            { COMP_CAVE_LIGHT,  24, 0 },
            { COMP_BOARD_TRIGGER, 0, -16 },
        },
        4,
        {
            { 0, 1, LINK_SEQUENCE, 0 },
            { 1, 2, LINK_SEQUENCE, 0 },
            { 2, 3, LINK_SEQUENCE, 0 },
            { 3, 4, LINK_TRIGGERS, 0 },
        }
    },
    {
        "Timed Gate",
        "Trigger activates target for 5 seconds",
        2,
        {
            { COMP_BOARD_TRIGGER, -16, 0 },
            { COMP_BOARD_TRIGGER,  16, 0 },
        },
        1,
        {
            { 0, 1, LINK_TRIGGERS, 300 },  /* timer_frames stored in threshold field; will be moved to timer on placement */
        }
    },
};

const BehaviorTemplate *editor_get_templates(void) {
    return builtin_templates;
}

static void editor_place_template(EditorState *editor, int tmpl_idx, int cx, int cy) {
    if (tmpl_idx < 0 || tmpl_idx >= NUM_BEHAVIOR_TEMPLATES) return;
    const BehaviorTemplate *tmpl = &builtin_templates[tmpl_idx];

    if (editor->table.num_objects + tmpl->num_objects > MAX_EDITOR_OBJECTS) return;
    if (editor->table.num_links + tmpl->num_links > MAX_TABLE_LINKS) return;

    editor_push_undo(editor);

    int base_obj = editor->table.num_objects;

    /* Place objects at relative offsets from click point */
    for (int i = 0; i < tmpl->num_objects; i++) {
        EditorObject *obj = &editor->table.objects[base_obj + i];
        memset(obj, 0, sizeof(EditorObject));
        obj->type = tmpl->objects[i].type;
        int ox = cx + tmpl->objects[i].dx;
        int oy = cy + tmpl->objects[i].dy + OBJECT_Y_DISPLAY_OFFSET;
        obj->x = (uint16_t)(ox < 0 ? 0 : (ox > 511 ? 511 : ox));
        obj->y = (uint16_t)(oy < 0 ? 0 : (oy > 511 ? 511 : oy));
        editor_component_default_bbox(obj->type, &obj->x_thresh, &obj->y_thresh);
        obj->score = editor_component_default_score(obj->type);
        obj->bounce_force = component_defaults[obj->type].default_force;
        obj->sfx_id = component_defaults[obj->type].default_sfx;
    }
    editor->table.num_objects += tmpl->num_objects;

    /* Wire up links */
    for (int i = 0; i < tmpl->num_links; i++) {
        int src = base_obj + tmpl->links[i].src;
        int dst = base_obj + tmpl->links[i].dst;
        int link_idx = editor_add_link(&editor->table, src, dst, tmpl->links[i].type);
        if (link_idx >= 0 && tmpl->links[i].type == LINK_CHARGES) {
            editor->table.links[link_idx].threshold = tmpl->links[i].threshold;
        }
        /* For the "Timed Gate" template, threshold is actually timer_frames */
        if (link_idx >= 0 && tmpl_idx == 4 && tmpl->links[i].type == LINK_TRIGGERS) {
            editor->table.links[link_idx].timer_frames = tmpl->links[i].threshold;
            editor->table.links[link_idx].threshold = 0;
        }
    }

    editor->table.dirty = true;
    editor->selected_object = base_obj;  /* Select first placed object */
}

void editor_update(EditorState *editor, Platform *platform, GameState *state) {
    if (!editor || !editor->active) return;

    /* Store GameState reference for render pass */
    editor->game_state_ref = state;

    /* Sync editor stage context to mask editor.
     * In combined view, use top_stage_id for rows 0-17, bottom_stage_id for 18+.
     * This ensures the correct mask PNG is loaded for each half. */
    if (editor->mask_editor) {
        if (editor->combined_view && editor->hovered_coll_row >= 0 &&
            editor->hovered_coll_row < 18) {
            editor->mask_editor->editor_stage_id = editor->top_stage_id;
        } else {
            editor->mask_editor->editor_stage_id = editor->table.stage_id;
        }
    }

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
                /* Check if the [+ New Table] entry was selected (last entry, empty folder) */
                PickerEntry *sel = &editor->picker_tables[editor->picker_cursor];
                if (sel->folder[0] == '\0') {
                    editor_create_new_table(editor, state);
                } else {
                    editor_open_table(editor, editor->picker_cursor, state);
                }
            }
            break;
        }
        case EDITOR_SCREEN_EDITOR:
            editor_update_editor(editor, platform, state);
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
