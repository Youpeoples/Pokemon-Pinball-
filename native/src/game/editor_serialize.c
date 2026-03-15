/*
 * Stage Builder - Lua Serializer Implementation
 *
 * Generates human-readable Lua files from EditorTable data.
 * Each placed object produces collision handler + sprite draw code.
 */

#include "game/editor_serialize.h"
#include "game/editor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/*=============================================================================
 * Helper: ensure directory exists
 *===========================================================================*/
static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return true;
    #ifdef _WIN32
    return _mkdir(path) == 0;
    #else
    return mkdir(path, 0755) == 0;
    #endif
}

/*=============================================================================
 * Helper: write string to file
 *===========================================================================*/
static bool write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("[EDITOR] Failed to write: %s\n", path);
        return false;
    }
    fputs(content, f);
    fclose(f);
    printf("[EDITOR] Wrote: %s\n", path);
    return true;
}

/*=============================================================================
 * Helper: write binary data to file
 *===========================================================================*/
static bool write_binary_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("[EDITOR] Failed to write binary: %s\n", path);
        return false;
    }
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    if (written != size) {
        printf("[EDITOR] Short write on binary: %s (%zu/%zu)\n", path, written, size);
        return false;
    }
    printf("[EDITOR] Wrote binary: %s (%zu bytes)\n", path, size);
    return true;
}

/*=============================================================================
 * Component Lua code-name (for variable naming)
 *===========================================================================*/
static const char *comp_lua_name(ComponentType type) {
    switch (type) {
        case COMP_BUMPER:        return "bumper";
        case COMP_SPINNER:       return "spinner";
        case COMP_BALL_UPGRADE:  return "ball_upgrade";
        case COMP_PIKACHU_SAVER: return "pikachu_saver";
        case COMP_CAVE_LIGHT:    return "cave_light";
        case COMP_DIGLETT:       return "diglett";
        case COMP_FIELD_CREATURE:return "field_creature";
        case COMP_STARYU:        return "staryu";
        case COMP_SLOT_MACHINE:  return "slot_machine";
        case COMP_RAILING:       return "railing";
        case COMP_LAUNCH_ALLEY:  return "launch_alley";
        case COMP_WILD_POKEMON:  return "wild_pokemon";
        case COMP_BOARD_TRIGGER: return "board_trigger";
        case COMP_DITTO_SLOT:    return "ditto_slot";
        default: return "unknown";
    }
}

/*=============================================================================
 * Generate: manifest.json
 *===========================================================================*/
static bool generate_manifest(EditorTable *table, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/manifest.json", dir);

    char buf[4096];
    snprintf(buf, sizeof(buf),
        "{\n"
        "    \"name\": \"%s\",\n"
        "    \"type\": \"main_field\",\n"
        "    \"description\": \"Custom table created with Stage Builder\",\n"
        "    \"stages\": {\n"
        "        \"top\":    { \"id\": %d, \"has_flippers\": false },\n"
        "        \"bottom\": { \"id\": %d, \"has_flippers\": %s }\n"
        "    },\n"
        "    \"scripts\": {\n"
        "        \"main\": \"scripts/main.lua\",\n"
        "        \"collision\": \"scripts/collision.lua\",\n"
        "        \"sprites\": \"scripts/sprites.lua\",\n"
        "        \"slot\": \"scripts/slot.lua\",\n"
        "        \"catchem\": \"scripts/catchem.lua\",\n"
        "        \"evolution\": \"scripts/evolution.lua\",\n"
        "        \"map_move\": \"scripts/map_move.lua\",\n"
        "        \"billboard\": \"scripts/billboard.lua\"\n"
        "    },\n"
        "    \"data\": {\n"
        "        \"collision_top\": \"data/top.collision\",\n"
        "        \"collision_bottom\": \"data/bottom.collision\",\n"
        "        \"tilemap_top\": \"data/top.map\",\n"
        "        \"tilemap_top_attr\": \"data/top.attr\",\n"
        "        \"tilemap_bottom\": \"data/bottom.map\",\n"
        "        \"tilemap_bottom_attr\": \"data/bottom.attr\",\n"
        "        \"tileset_top\": \"data/top_tiles.png\",\n"
        "        \"tileset_bottom\": \"data/bottom_tiles.png\"\n"
        "    }\n"
        "}\n",
        table->name,
        table->stage_id & ~1,  /* top stage = even */
        table->stage_id | 1,   /* bottom stage = odd */
        table->has_flippers ? "true" : "false"
    );

    return write_file(path, buf);
}

/*=============================================================================
 * Generate: collision.lua
 *===========================================================================*/
static bool generate_collision_lua(EditorTable *table, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/scripts/collision.lua", dir);

    /* Build the file in a large buffer */
    char *buf = malloc(32768);
    if (!buf) return false;
    int pos = 0;

    pos += snprintf(buf + pos, 32768 - pos,
        "-- Auto-generated by Pokemon Pinball+ Stage Builder\n"
        "-- Table: \"%s\"\n"
        "-- Do not edit above this line (regenerated on save)\n\n",
        table->name);

    /* Generate resolver functions per object */
    int counts[COMP_COUNT] = {0};
    for (int i = 0; i < table->num_objects; i++) {
        EditorObject *obj = &table->objects[i];
        if (obj->type == COMP_NONE) continue;
        counts[obj->type]++;
        int num = counts[obj->type];
        const char *name = comp_lua_name(obj->type);

        pos += snprintf(buf + pos, 32768 - pos,
            "-- %s #%d at (%d, %d)\n"
            "local function resolve_%s_%d()\n",
            editor_component_name(obj->type), num, obj->x, obj->y, name, num);

        /* Generate collision response based on component type */
        switch (obj->type) {
        case COMP_BUMPER:
            pos += snprintf(buf + pos, 32768 - pos,
                "    pinball.set_state(\"flipper_y_force\", 0x%04X)\n"
                "    pinball.rumble(0xFF, 3)\n"
                "    pinball.add_score(\"score_%d\")\n"
                "    pinball.play_sfx_raw(0x00, 0x%02X)\n",
                obj->bounce_force, obj->score, obj->sfx_id);
            break;
        case COMP_SPINNER:
            pos += snprintf(buf + pos, 32768 - pos,
                "    pinball.add_score(\"score_%d\")\n"
                "    pinball.play_sfx_raw(0x00, 0x%02X)\n",
                obj->score, obj->sfx_id);
            break;
        case COMP_BALL_UPGRADE:
            pos += snprintf(buf + pos, 32768 - pos,
                "    -- Ball upgrade trigger\n"
                "    pinball.play_sfx_raw(0x00, 0x%02X)\n",
                obj->sfx_id);
            break;
        case COMP_CAVE_LIGHT:
            pos += snprintf(buf + pos, 32768 - pos,
                "    -- CAVE light trigger\n"
                "    pinball.play_sfx_raw(0x00, 0x%02X)\n",
                obj->sfx_id);
            break;
        case COMP_FIELD_CREATURE:
            pos += snprintf(buf + pos, 32768 - pos,
                "    pinball.add_score(\"score_%d\")\n"
                "    pinball.play_sfx_raw(0x00, 0x%02X)\n",
                obj->score, obj->sfx_id);
            break;
        case COMP_BOARD_TRIGGER:
            pos += snprintf(buf + pos, 32768 - pos,
                "    -- Board trigger: increment bonus multiplier\n"
                "    pinball.play_sfx_raw(0x00, 0x%02X)\n",
                obj->sfx_id);
            break;
        default:
            pos += snprintf(buf + pos, 32768 - pos,
                "    -- TODO: implement %s collision handler\n",
                editor_component_name(obj->type));
            break;
        }

        /* Custom Lua if provided */
        if (obj->custom_lua[0] != '\0') {
            pos += snprintf(buf + pos, 32768 - pos,
                "    -- USER CODE\n    %s\n    -- END USER CODE\n",
                obj->custom_lua);
        }

        pos += snprintf(buf + pos, 32768 - pos, "end\n\n");
    }

    /* Generate main collision dispatcher */
    pos += snprintf(buf + pos, 32768 - pos,
        "function on_object_collision(collision_id, ball_x, ball_y)\n"
        "    local stage = pinball.get_current_stage()\n");

    /* Top stage (even ID) */
    pos += snprintf(buf + pos, 32768 - pos,
        "    if stage == %d then\n", table->stage_id & ~1);
    memset(counts, 0, sizeof(counts));
    for (int i = 0; i < table->num_objects; i++) {
        EditorObject *obj = &table->objects[i];
        if (obj->type == COMP_NONE) continue;
        counts[obj->type]++;
        pos += snprintf(buf + pos, 32768 - pos,
            "        resolve_%s_%d()\n",
            comp_lua_name(obj->type), counts[obj->type]);
    }

    /* Bottom stage (odd ID) */
    pos += snprintf(buf + pos, 32768 - pos,
        "    elseif stage == %d then\n"
        "        -- Bottom stage objects (add here)\n",
        table->stage_id | 1);

    pos += snprintf(buf + pos, 32768 - pos,
        "    end\n"
        "end\n");

    bool ok = write_file(path, buf);
    free(buf);
    return ok;
}

/*=============================================================================
 * Generate: sprites.lua
 *===========================================================================*/
static bool generate_sprites_lua(EditorTable *table, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/scripts/sprites.lua", dir);

    char *buf = malloc(16384);
    if (!buf) return false;
    int pos = 0;

    pos += snprintf(buf + pos, 16384 - pos,
        "-- Auto-generated by Pokemon Pinball+ Stage Builder\n"
        "-- Table: \"%s\"\n\n",
        table->name);

    /* Top stage draw function */
    pos += snprintf(buf + pos, 16384 - pos,
        "local function draw_top_sprites()\n"
        "    pinball.clear_sprites()\n"
        "    local scx = pinball.get_hram(\"scx\")\n"
        "    local scy = pinball.get_hram(\"scy\")\n\n");

    int counts[COMP_COUNT] = {0};
    for (int i = 0; i < table->num_objects; i++) {
        EditorObject *obj = &table->objects[i];
        if (obj->type == COMP_NONE) continue;
        counts[obj->type]++;
        const char *name = comp_lua_name(obj->type);

        pos += snprintf(buf + pos, 16384 - pos,
            "    -- %s #%d at world (%d, %d)\n"
            "    pinball.load_sprite_data(\"%s_idle\", 0, %d - scy, %d - scx)\n\n",
            editor_component_name(obj->type), counts[obj->type],
            obj->x, obj->y, name, obj->y, obj->x);
    }

    /* Ball sprite */
    pos += snprintf(buf + pos, 16384 - pos,
        "    -- Ball\n"
        "    local bx = (pinball.get_ball_x() >> 8) - scx\n"
        "    local by = (pinball.get_ball_y() >> 8) - scy\n"
        "    pinball.load_sprite_data(\"ball_spin\", pinball.get_ball_spin() >> 5, by, bx)\n");

    pos += snprintf(buf + pos, 16384 - pos, "end\n\n");

    /* Bottom stage draw function */
    pos += snprintf(buf + pos, 16384 - pos,
        "local function draw_bottom_sprites()\n"
        "    pinball.clear_sprites()\n"
        "    local scx = pinball.get_hram(\"scx\")\n"
        "    local scy = pinball.get_hram(\"scy\")\n"
        "    -- TODO: Add bottom stage sprites\n"
        "    -- Ball\n"
        "    local bx = (pinball.get_ball_x() >> 8) - scx\n"
        "    local by = (pinball.get_ball_y() >> 8) - scy\n"
        "    pinball.load_sprite_data(\"ball_spin\", pinball.get_ball_spin() >> 5, by, bx)\n"
        "end\n\n");

    /* Dispatcher */
    pos += snprintf(buf + pos, 16384 - pos,
        "function on_draw_sprites(stage_id)\n"
        "    if stage_id == %d then draw_top_sprites()\n"
        "    elseif stage_id == %d then draw_bottom_sprites() end\n"
        "end\n",
        table->stage_id & ~1, table->stage_id | 1);

    bool ok = write_file(path, buf);
    free(buf);
    return ok;
}

/*=============================================================================
 * Generate: main.lua
 *===========================================================================*/
static bool generate_main_lua(EditorTable *table, const char *dir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/scripts/main.lua", dir);

    char *buf = malloc(4096);
    if (!buf) return false;
    int pos = 0;

    pos += snprintf(buf + pos, 4096 - pos,
        "-- Auto-generated by Pokemon Pinball+ Stage Builder\n"
        "-- Table: \"%s\"\n\n"
        "function on_stage_init(stage_id)\n"
        "    -- Set palettes\n",
        table->name);

    /* Write palette setup for all 8 BG palettes */
    for (int p = 0; p < 8; p++) {
        pos += snprintf(buf + pos, 4096 - pos,
            "    pinball.set_bg_palette(%d, {0x%04X, 0x%04X, 0x%04X, 0x%04X})\n",
            p,
            table->bg_palettes[p].colors[0],
            table->bg_palettes[p].colors[1],
            table->bg_palettes[p].colors[2],
            table->bg_palettes[p].colors[3]);
    }

    pos += snprintf(buf + pos, 4096 - pos,
        "    pinball.log(\"Custom table '%s' initialized\")\n"
        "end\n\n"
        "function on_ball_init(stage_id)\n"
        "    -- Ball initialization (position, velocity)\n"
        "end\n\n"
        "-- function on_ball_loss(stage_id)\n"
        "--     -- Custom ball loss handler\n"
        "-- end\n",
        table->name);

    bool ok = write_file(path, buf);
    free(buf);
    return ok;
}

/*=============================================================================
 * Generate: Template scripts (slot, catchem, evolution, map_move, billboard)
 *===========================================================================*/
static bool generate_template_script(const char *dir, const char *filename, const char *hook) {
    char path[512];
    snprintf(path, sizeof(path), "%s/scripts/%s", dir, filename);

    char buf[512];
    snprintf(buf, sizeof(buf),
        "-- Template: %s\n"
        "-- Uncomment function below to override C behavior.\n"
        "-- When commented out, the C engine handles this automatically.\n\n"
        "-- function %s(...)\n"
        "--     -- Custom implementation\n"
        "-- end\n",
        filename, hook);

    return write_file(path, buf);
}

/*=============================================================================
 * Serialize: Collision Maps (binary)
 *
 * Writes two 1024-byte .collision files (32 rows x 32 cols) per stage half.
 * On-disk format has +3 row offset: rows 0-2 are above-screen padding (0x01),
 * rows 3-20 are visible tilemap rows, rows 21-31 are padding (0x00).
 *===========================================================================*/

bool editor_serialize_collision(EditorState *editor, const char *output_path) {
    if (!editor || !output_path) return false;

    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s/data", output_path);
    ensure_dir(data_dir);

    uint8_t buf[1024];  /* 32 rows x 32 cols */
    bool ok = true;

    /* Top half: editor display rows 0-17 → disk rows 3-20 */
    memset(buf, 0, sizeof(buf));
    /* Rows 0-2: above-screen solid padding */
    for (int c = 0; c < 32; c++) {
        buf[0 * 32 + c] = 0x01;
        buf[1 * 32 + c] = 0x01;
        buf[2 * 32 + c] = 0x01;
    }
    /* Rows 3-20: visible area from editor */
    for (int r = 0; r < 18; r++) {
        int data_row = editor_display_to_data_row(editor, r);
        if (data_row >= 0 && data_row < 64) {
            for (int c = 0; c < 32; c++) {
                buf[(r + 3) * 32 + c] = editor->table.collision_map[data_row][c];
            }
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/data/top.collision", output_path);
    ok = ok && write_binary_file(path, buf, sizeof(buf));

    /* Bottom half: editor display rows 18-35 → disk rows 3-20 */
    memset(buf, 0, sizeof(buf));
    for (int c = 0; c < 32; c++) {
        buf[0 * 32 + c] = 0x01;
        buf[1 * 32 + c] = 0x01;
        buf[2 * 32 + c] = 0x01;
    }
    for (int r = 0; r < 18; r++) {
        int display_row = r + 18;  /* Bottom half display rows */
        int data_row = editor_display_to_data_row(editor, display_row);
        if (data_row >= 0 && data_row < 64) {
            for (int c = 0; c < 32; c++) {
                buf[(r + 3) * 32 + c] = editor->table.collision_map[data_row][c];
            }
        }
    }
    snprintf(path, sizeof(path), "%s/data/bottom.collision", output_path);
    ok = ok && write_binary_file(path, buf, sizeof(buf));

    return ok;
}

/*=============================================================================
 * Serialize: Tilemaps (binary)
 *
 * Writes four 1024-byte files per stage half:
 *   data/top.map + data/top.attr (tile indices + attributes)
 *   data/bottom.map + data/bottom.attr
 *===========================================================================*/

bool editor_serialize_tilemaps(EditorState *editor, const char *output_path) {
    if (!editor || !output_path) return false;

    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s/data", output_path);
    ensure_dir(data_dir);

    uint8_t map_buf[1024];   /* 32 rows x 32 cols */
    uint8_t attr_buf[1024];
    bool ok = true;
    char path[512];

    /* Top half: editor data rows 0-31 */
    memset(map_buf, 0, sizeof(map_buf));
    memset(attr_buf, 0, sizeof(attr_buf));
    for (int r = 0; r < 32; r++) {
        for (int c = 0; c < 32; c++) {
            map_buf[r * 32 + c] = editor->table.tilemap[r][c];
            attr_buf[r * 32 + c] = editor->table.tilemap_attrs[r][c];
        }
    }
    snprintf(path, sizeof(path), "%s/data/top.map", output_path);
    ok = ok && write_binary_file(path, map_buf, sizeof(map_buf));
    snprintf(path, sizeof(path), "%s/data/top.attr", output_path);
    ok = ok && write_binary_file(path, attr_buf, sizeof(attr_buf));

    /* Bottom half: editor data rows 32-63 */
    memset(map_buf, 0, sizeof(map_buf));
    memset(attr_buf, 0, sizeof(attr_buf));
    for (int r = 0; r < 32; r++) {
        for (int c = 0; c < 32; c++) {
            map_buf[r * 32 + c] = editor->table.tilemap[r + 32][c];
            attr_buf[r * 32 + c] = editor->table.tilemap_attrs[r + 32][c];
        }
    }
    snprintf(path, sizeof(path), "%s/data/bottom.map", output_path);
    ok = ok && write_binary_file(path, map_buf, sizeof(map_buf));
    snprintf(path, sizeof(path), "%s/data/bottom.attr", output_path);
    ok = ok && write_binary_file(path, attr_buf, sizeof(attr_buf));

    return ok;
}

/*=============================================================================
 * Serialize: Palettes (binary)
 *
 * Writes data/palettes.bin: 128 bytes = 8 palettes x 4 colors x 2 bytes (LE).
 *===========================================================================*/

bool editor_serialize_palettes(EditorState *editor, const char *output_path) {
    if (!editor || !output_path) return false;

    char data_dir[512];
    snprintf(data_dir, sizeof(data_dir), "%s/data", output_path);
    ensure_dir(data_dir);

    uint8_t buf[128];
    for (int p = 0; p < 8; p++) {
        for (int c = 0; c < 4; c++) {
            uint16_t val = editor->table.bg_palettes[p].colors[c];
            int idx = (p * 4 + c) * 2;
            buf[idx]     = val & 0xFF;         /* Low byte */
            buf[idx + 1] = (val >> 8) & 0xFF;  /* High byte */
        }
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/data/palettes.bin", output_path);
    return write_binary_file(path, buf, sizeof(buf));
}

/*=============================================================================
 * Main Serialization Entry Point
 *===========================================================================*/

bool editor_serialize_table(EditorState *editor, const char *output_path) {
    if (!editor || !output_path) return false;
    EditorTable *table = &editor->table;

    /* Create directory structure */
    char scripts_dir[512], data_dir[512];
    snprintf(scripts_dir, sizeof(scripts_dir), "%s/scripts", output_path);
    snprintf(data_dir, sizeof(data_dir), "%s/data", output_path);
    ensure_dir(output_path);
    ensure_dir(scripts_dir);
    ensure_dir(data_dir);

    bool ok = true;
    ok = ok && generate_manifest(table, output_path);
    ok = ok && generate_collision_lua(table, output_path);
    ok = ok && generate_sprites_lua(table, output_path);
    ok = ok && generate_main_lua(table, output_path);
    ok = ok && generate_template_script(output_path, "slot.lua", "on_slot_update");
    ok = ok && generate_template_script(output_path, "catchem.lua", "on_catchem_update");
    ok = ok && generate_template_script(output_path, "evolution.lua", "on_evolution_update");
    ok = ok && generate_template_script(output_path, "map_move.lua", "on_map_move_update");
    ok = ok && generate_template_script(output_path, "billboard.lua", "on_billboard_update");

    /* Serialize binary data (collision maps + tilemaps + palettes) */
    ok = ok && editor_serialize_collision(editor, output_path);
    ok = ok && editor_serialize_tilemaps(editor, output_path);
    ok = ok && editor_serialize_palettes(editor, output_path);

    if (ok) {
        table->dirty = false;
        printf("[EDITOR] Table saved to: %s\n", output_path);
    }
    return ok;
}

bool editor_serialize_scripts_only(EditorState *editor, const char *output_path) {
    if (!editor || !output_path) return false;
    EditorTable *table = &editor->table;

    char scripts_dir[512];
    snprintf(scripts_dir, sizeof(scripts_dir), "%s/scripts", output_path);
    ensure_dir(scripts_dir);

    bool ok = true;
    ok = ok && generate_collision_lua(table, output_path);
    ok = ok && generate_sprites_lua(table, output_path);
    ok = ok && generate_main_lua(table, output_path);
    return ok;
}
