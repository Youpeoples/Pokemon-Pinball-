/*
 * config_data.c — JSON configuration loader for Pokemon Pinball+
 *
 * Loads physics, scores, table objects, and Pokemon data from external
 * JSON files in the config/ directory. All values have hardcoded defaults
 * matching the original GBC game. Missing files or fields gracefully
 * fall back to defaults.
 *
 * Config file layout:
 *   config/physics.json    — gravity, velocity, flipper, collision constants
 *   config/scores.json     — all BCD score values by category
 *   config/tables/         — per-stage object definitions (4 files)
 *   config/pokemon.json    — species data, encounters, evolutions
 */

#include "game/config_data.h"
#include "audio/audio.h"
#include "third_party/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Helper: Read entire file into malloc'd string
 *===========================================================================*/
static char *read_file_to_string(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

/*=============================================================================
 * int_to_bcd4 — Convert decimal integer to 4-byte BCD (little-endian)
 *
 * Each byte holds two decimal digits as nibbles.
 * byte[0] = ones and tens, byte[1] = hundreds and thousands, etc.
 * E.g. 10000000 -> {0x00, 0x00, 0x00, 0x10}
 *===========================================================================*/
void int_to_bcd4(uint32_t value, uint8_t out[4]) {
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t lo = value % 10;
        value /= 10;
        uint8_t hi = value % 10;
        value /= 10;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

/*=============================================================================
 * Helper: Parse a JSON value as hex string ("0x1234") or integer
 *===========================================================================*/
static int parse_hex_or_int(const cJSON *item) {
    if (!item) return 0;

    if (cJSON_IsString(item)) {
        const char *s = item->valuestring;
        if (s && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
            return (int)strtol(s, NULL, 16);
        }
        return (int)strtol(s, NULL, 0);
    }

    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }

    return 0;
}

/*=============================================================================
 * Helper: Parse a JSON integer and convert to 4-byte BCD, with default
 *===========================================================================*/
static void parse_bcd_score(const cJSON *obj, const char *key,
                            uint8_t out[4], const uint8_t default_val[4]) {
    if (!obj) {
        memcpy(out, default_val, 4);
        return;
    }

    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) {
        uint32_t val = (uint32_t)item->valuedouble;
        int_to_bcd4(val, out);
    } else {
        memcpy(out, default_val, 4);
    }
}

/*=============================================================================
 * config_data_create — Allocate and fill with hardcoded defaults
 *===========================================================================*/
ConfigData *config_data_create(void) {
    ConfigData *config = (ConfigData *)calloc(1, sizeof(ConfigData));
    if (!config) {
        fprintf(stderr, "config_data_create: failed to allocate ConfigData\n");
        /* The header says "never returns NULL" so we abort */
        abort();
    }

    /* ---- Physics defaults ---- */
    PhysicsConfig *p = &config->physics;
    p->gravity                  = 0x000B;
    p->max_velocity_hi          = 7;
    p->position_clamp_positive  = 0x04FF;
    p->position_clamp_negative  = (int16_t)0xFB01;
    p->stage_transition_up_y    = 8;
    p->stage_transition_down_y  = 168;
    p->stage_transition_y_offset = 0x8800;
    p->flipper_delta            = 0x0333;
    p->flipper_max              = 0x0F00;

    /* Flipper radius magnitudes (32 entries) */
    static const uint16_t default_flipper_radius[32] = {
        0x0000, 0x000C, 0x001C, 0x0030,
        0x0038, 0x0048, 0x005C, 0x006C,
        0x0070, 0x0080, 0x0094, 0x00A4,
        0x00B4, 0x00C4, 0x00D4, 0x00E4,
        0x00F8, 0x00FC, 0x00FC, 0x00FC,
        0x00FC, 0x00FC, 0x00FC, 0x00FC,
        0x00FC, 0x00FC, 0x00FC, 0x00FC,
        0x00FC, 0x00FC, 0x00FC, 0x00FC
    };
    memcpy(p->flipper_radius_magnitudes, default_flipper_radius, sizeof(default_flipper_radius));

    p->flipper_collision_x_min   = 43;
    p->flipper_collision_x_range = 48;
    p->flipper_collision_y_min   = 123;
    p->flipper_collision_y_range = 32;
    p->damping_shift             = 2;
    p->amplification_shift       = 3;
    p->spin_transfer_shift       = 1;
    p->spin_recalc_shift         = 2;

    /* ---- Score defaults (convert integer to 4-byte BCD) ---- */
    ScoreConfig *s = &config->scores;
    int_to_bcd4(5,        s->score_5);
    int_to_bcd4(10,       s->score_10);
    int_to_bcd4(100,      s->score_100);
    int_to_bcd4(400,      s->score_400);
    int_to_bcd4(500,      s->score_500);
    int_to_bcd4(5000,     s->score_5000);
    int_to_bcd4(10000,    s->score_10000);
    int_to_bcd4(100000,   s->score_100000);
    int_to_bcd4(300000,   s->score_300000);
    int_to_bcd4(1000000,  s->score_1000000);
    int_to_bcd4(10000000, s->score_10000000);

    /* Gengar bonus */
    int_to_bcd4(100000,   s->gastly_hit);
    int_to_bcd4(500000,   s->haunter_hit);
    int_to_bcd4(5000000,  s->gengar_hit);
    int_to_bcd4(100,      s->gravestone);

    /* Mewtwo bonus */
    int_to_bcd4(5000000,  s->mewtwo_hit);
    int_to_bcd4(100000,   s->mewtwo_orb_hit);

    /* Meowth bonus */
    int_to_bcd4(1000,     s->meowth_jewel);
    int_to_bcd4(100000,   s->meowth_hit);

    /* Diglett bonus */
    int_to_bcd4(100000,   s->diglett_hit);
    int_to_bcd4(5000000,  s->dugtrio_hit);

    /* Seel bonus */
    int_to_bcd4(100000,   s->seel_hit);
    int_to_bcd4(5000000,  s->seel_dive);

    /* Jackpot values (original BCD: voltorb=0x00010000, spinner=0x00001000, etc.) */
    int_to_bcd4(10000,    s->jackpot_catch_voltorb);
    int_to_bcd4(1000,     s->jackpot_catch_spinner);
    int_to_bcd4(50000,    s->jackpot_catch_bellsprout);
    int_to_bcd4(15000,    s->jackpot_evo_voltorb);
    int_to_bcd4(75000,    s->jackpot_evo_bellsprout);
    int_to_bcd4(1500,     s->jackpot_evo_spinner);

    /* ---- Table defaults: empty (num_groups=0, use static data) ---- */
    config->red_field_top.num_groups    = 0;
    config->red_field_bottom.num_groups = 0;
    config->blue_field_top.num_groups   = 0;
    config->blue_field_bottom.num_groups = 0;

    /* ---- Pokemon defaults: zeroed by calloc, filled by pokemon.json ---- */

    /* ---- Audio volume defaults: -1 = not set (use engine defaults) ---- */
    config->audio_config.master_volume = -1;
    config->audio_config.music_volume = -1;
    config->audio_config.sfx_volume = -1;

    return config;
}

/*=============================================================================
 * config_data_free — Free the config struct
 *===========================================================================*/
void config_data_free(ConfigData *config) {
    if (config) {
        free(config);
    }
}

/*=============================================================================
 * config_load_physics — Load physics.json overrides
 *===========================================================================*/
void config_load_physics(ConfigData *config, const char *base_path) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config/physics.json", base_path);

    char *json_str = read_file_to_string(path);
    if (!json_str) {
        fprintf(stderr, "[config] physics.json: NOT FOUND - using defaults\n");
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        fprintf(stderr, "[config] physics.json: PARSE ERROR\n");
        free(json_str);
        return;
    }

    PhysicsConfig *p = &config->physics;
    cJSON *item;
    int fields = 0;

    item = cJSON_GetObjectItem(root, "gravity");
    if (item) { p->gravity = (int16_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "max_velocity_hi");
    if (item) { p->max_velocity_hi = (int8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "position_clamp_positive");
    if (item) { p->position_clamp_positive = (int16_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "position_clamp_negative");
    if (item) { p->position_clamp_negative = (int16_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "stage_transition_up_y");
    if (item) { p->stage_transition_up_y = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "stage_transition_down_y");
    if (item) { p->stage_transition_down_y = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "stage_transition_y_offset");
    if (item) { p->stage_transition_y_offset = (uint16_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "flipper_delta");
    if (item) { p->flipper_delta = (uint16_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "flipper_max");
    if (item) { p->flipper_max = (uint16_t)parse_hex_or_int(item); fields++; }

    /* Flipper radius magnitudes array */
    item = cJSON_GetObjectItem(root, "flipper_radius_magnitudes");
    if (item && cJSON_IsArray(item)) {
        int count = cJSON_GetArraySize(item);
        if (count > 32) count = 32;
        for (int i = 0; i < count; i++) {
            cJSON *elem = cJSON_GetArrayItem(item, i);
            if (elem) {
                p->flipper_radius_magnitudes[i] = (uint16_t)parse_hex_or_int(elem);
            }
        }
        fields++;
    }

    item = cJSON_GetObjectItem(root, "flipper_collision_x_min");
    if (item) { p->flipper_collision_x_min = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "flipper_collision_x_range");
    if (item) { p->flipper_collision_x_range = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "flipper_collision_y_min");
    if (item) { p->flipper_collision_y_min = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "flipper_collision_y_range");
    if (item) { p->flipper_collision_y_range = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "damping_shift");
    if (item) { p->damping_shift = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "amplification_shift");
    if (item) { p->amplification_shift = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "spin_transfer_shift");
    if (item) { p->spin_transfer_shift = (uint8_t)parse_hex_or_int(item); fields++; }

    item = cJSON_GetObjectItem(root, "spin_recalc_shift");
    if (item) { p->spin_recalc_shift = (uint8_t)parse_hex_or_int(item); fields++; }

    fprintf(stderr, "[config] physics.json: loaded (%d fields)\n", fields);

    cJSON_Delete(root);
    free(json_str);
}

/*=============================================================================
 * config_load_scores — Load scores.json overrides
 *===========================================================================*/
void config_load_scores(ConfigData *config, const char *base_path) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config/scores.json", base_path);

    char *json_str = read_file_to_string(path);
    if (!json_str) {
        fprintf(stderr, "[config] scores.json: NOT FOUND - using defaults\n");
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        fprintf(stderr, "[config] scores.json: PARSE ERROR\n");
        free(json_str);
        return;
    }

    ScoreConfig *s = &config->scores;
    int sections = 0;
    int fields = 0;

    /* Common scores */
    cJSON *common = cJSON_GetObjectItem(root, "common");
    if (common) { sections++;
    parse_bcd_score(common, "score_5",        s->score_5,        s->score_5);
    parse_bcd_score(common, "score_10",       s->score_10,       s->score_10);
    parse_bcd_score(common, "score_100",      s->score_100,      s->score_100);
    parse_bcd_score(common, "score_400",      s->score_400,      s->score_400);
    parse_bcd_score(common, "score_500",      s->score_500,      s->score_500);
    parse_bcd_score(common, "score_5000",     s->score_5000,     s->score_5000);
    parse_bcd_score(common, "score_10000",    s->score_10000,    s->score_10000);
    parse_bcd_score(common, "score_100000",   s->score_100000,   s->score_100000);
    parse_bcd_score(common, "score_300000",   s->score_300000,   s->score_300000);
    parse_bcd_score(common, "score_1000000",  s->score_1000000,  s->score_1000000);
    parse_bcd_score(common, "score_10000000", s->score_10000000, s->score_10000000);
    fields += 11; }

    /* Gengar bonus */
    cJSON *gengar = cJSON_GetObjectItem(root, "gengar_bonus");
    if (gengar) { sections++;
    parse_bcd_score(gengar, "gastly_hit",  s->gastly_hit,  s->gastly_hit);
    parse_bcd_score(gengar, "haunter_hit", s->haunter_hit, s->haunter_hit);
    parse_bcd_score(gengar, "gengar_hit",  s->gengar_hit,  s->gengar_hit);
    parse_bcd_score(gengar, "gravestone",  s->gravestone,  s->gravestone);
    fields += 4; }

    /* Mewtwo bonus */
    cJSON *mewtwo = cJSON_GetObjectItem(root, "mewtwo_bonus");
    if (mewtwo) { sections++;
    parse_bcd_score(mewtwo, "mewtwo_hit", s->mewtwo_hit,     s->mewtwo_hit);
    parse_bcd_score(mewtwo, "orb_hit",   s->mewtwo_orb_hit, s->mewtwo_orb_hit);
    fields += 2; }

    /* Meowth bonus */
    cJSON *meowth = cJSON_GetObjectItem(root, "meowth_bonus");
    if (meowth) { sections++;
    parse_bcd_score(meowth, "jewel",      s->meowth_jewel, s->meowth_jewel);
    parse_bcd_score(meowth, "meowth_hit", s->meowth_hit,   s->meowth_hit);
    fields += 2; }

    /* Diglett bonus */
    cJSON *diglett = cJSON_GetObjectItem(root, "diglett_bonus");
    if (diglett) { sections++;
    parse_bcd_score(diglett, "diglett_hit", s->diglett_hit, s->diglett_hit);
    parse_bcd_score(diglett, "dugtrio_hit", s->dugtrio_hit, s->dugtrio_hit);
    fields += 2; }

    /* Seel bonus */
    cJSON *seel = cJSON_GetObjectItem(root, "seel_bonus");
    if (seel) { sections++;
    parse_bcd_score(seel, "seel_hit",  s->seel_hit,  s->seel_hit);
    parse_bcd_score(seel, "dive_hit",  s->seel_dive, s->seel_dive);
    fields += 2; }

    /* Jackpot values */
    cJSON *jackpots = cJSON_GetObjectItem(root, "jackpots");
    if (jackpots) { sections++;
    parse_bcd_score(jackpots, "catch_voltorb",    s->jackpot_catch_voltorb,    s->jackpot_catch_voltorb);
    parse_bcd_score(jackpots, "catch_spinner",    s->jackpot_catch_spinner,    s->jackpot_catch_spinner);
    parse_bcd_score(jackpots, "catch_bellsprout",  s->jackpot_catch_bellsprout,  s->jackpot_catch_bellsprout);
    parse_bcd_score(jackpots, "evo_voltorb",      s->jackpot_evo_voltorb,      s->jackpot_evo_voltorb);
    parse_bcd_score(jackpots, "evo_bellsprout",    s->jackpot_evo_bellsprout,    s->jackpot_evo_bellsprout);
    parse_bcd_score(jackpots, "evo_spinner",      s->jackpot_evo_spinner,      s->jackpot_evo_spinner);
    fields += 6; }

    fprintf(stderr, "[config] scores.json: loaded (%d fields in %d sections)\n",
            fields, sections);

    cJSON_Delete(root);
    free(json_str);
}

/*=============================================================================
 * Helper: Parse a single table config file into a TableConfig struct
 * Returns: 0 = file not found, -1 = parse error, >0 = total objects loaded
 *===========================================================================*/
static int parse_table_file(TableConfig *table, const char *path) {
    char *json_str = read_file_to_string(path);
    if (!json_str) {
        return 0;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        free(json_str);
        return -1;
    }

    cJSON *groups_arr = cJSON_GetObjectItem(root, "groups");
    if (!groups_arr || !cJSON_IsArray(groups_arr)) {
        cJSON_Delete(root);
        free(json_str);
        return -1;
    }

    int num_groups = cJSON_GetArraySize(groups_arr);
    if (num_groups > CONFIG_MAX_GROUPS_PER_TABLE) {
        num_groups = CONFIG_MAX_GROUPS_PER_TABLE;
    }

    table->num_groups = (uint8_t)num_groups;

    for (int g = 0; g < num_groups; g++) {
        cJSON *group_obj = cJSON_GetArrayItem(groups_arr, g);
        if (!group_obj) continue;

        TableObjectGroup *grp = &table->groups[g];

        /* Name */
        cJSON *name_item = cJSON_GetObjectItem(group_obj, "name");
        if (name_item && cJSON_IsString(name_item)) {
            strncpy(grp->name, name_item->valuestring, sizeof(grp->name) - 1);
            grp->name[sizeof(grp->name) - 1] = '\0';
        }

        /* Thresholds */
        cJSON *item;
        item = cJSON_GetObjectItem(group_obj, "x_thresh");
        if (item && cJSON_IsNumber(item)) grp->x_thresh = (uint8_t)item->valueint;

        item = cJSON_GetObjectItem(group_obj, "y_thresh");
        if (item && cJSON_IsNumber(item)) grp->y_thresh = (uint8_t)item->valueint;

        /* Attribute gated */
        item = cJSON_GetObjectItem(group_obj, "attribute_gated");
        if (item) {
            grp->attribute_gated = cJSON_IsTrue(item) ? true : false;
        }

        /* Collision attributes array (0xFF terminated) */
        memset(grp->collision_attrs, 0xFF, sizeof(grp->collision_attrs));
        cJSON *attrs_arr = cJSON_GetObjectItem(group_obj, "collision_attrs");
        if (attrs_arr && cJSON_IsArray(attrs_arr)) {
            int attr_count = cJSON_GetArraySize(attrs_arr);
            if (attr_count > CONFIG_MAX_ATTRS - 1) {
                attr_count = CONFIG_MAX_ATTRS - 1;
            }
            for (int a = 0; a < attr_count; a++) {
                cJSON *attr_item = cJSON_GetArrayItem(attrs_arr, a);
                if (attr_item) {
                    grp->collision_attrs[a] = (uint8_t)parse_hex_or_int(attr_item);
                }
            }
            /* Ensure 0xFF termination */
            grp->collision_attrs[attr_count] = 0xFF;
        }

        /* Objects array */
        cJSON *objs_arr = cJSON_GetObjectItem(group_obj, "objects");
        if (objs_arr && cJSON_IsArray(objs_arr)) {
            int obj_count = cJSON_GetArraySize(objs_arr);
            if (obj_count > CONFIG_MAX_OBJECTS_PER_GROUP) {
                obj_count = CONFIG_MAX_OBJECTS_PER_GROUP;
            }
            grp->num_objects = (uint8_t)obj_count;

            for (int o = 0; o < obj_count; o++) {
                cJSON *obj_item = cJSON_GetArrayItem(objs_arr, o);
                if (!obj_item) continue;

                TableObject *tobj = &grp->objects[o];

                item = cJSON_GetObjectItem(obj_item, "id");
                if (item) tobj->id = (uint8_t)parse_hex_or_int(item);

                item = cJSON_GetObjectItem(obj_item, "x");
                if (item) tobj->x = (uint8_t)parse_hex_or_int(item);

                item = cJSON_GetObjectItem(obj_item, "y");
                if (item) tobj->y = (uint8_t)parse_hex_or_int(item);
            }
        }
    }

    int total_objects = 0;
    for (int g = 0; g < num_groups; g++)
        total_objects += table->groups[g].num_objects;

    cJSON_Delete(root);
    free(json_str);
    return total_objects;
}

/*=============================================================================
 * config_load_tables — Load table object definitions for all 4 stages
 *===========================================================================*/
static void log_table_result(const char *name, int result, const TableConfig *table) {
    if (result == 0)
        fprintf(stderr, "[config] tables/%s: NOT FOUND - using defaults\n", name);
    else if (result < 0)
        fprintf(stderr, "[config] tables/%s: PARSE ERROR\n", name);
    else
        fprintf(stderr, "[config] tables/%s: loaded (%d groups, %d objects)\n",
                name, table->num_groups, result);
}

void config_load_tables(ConfigData *config, const char *base_path) {
    char path[512];
    int r;

    snprintf(path, sizeof(path), "%s/config/tables/red_field_top.json", base_path);
    r = parse_table_file(&config->red_field_top, path);
    log_table_result("red_field_top.json", r, &config->red_field_top);

    snprintf(path, sizeof(path), "%s/config/tables/red_field_bottom.json", base_path);
    r = parse_table_file(&config->red_field_bottom, path);
    log_table_result("red_field_bottom.json", r, &config->red_field_bottom);

    snprintf(path, sizeof(path), "%s/config/tables/blue_field_top.json", base_path);
    r = parse_table_file(&config->blue_field_top, path);
    log_table_result("blue_field_top.json", r, &config->blue_field_top);

    snprintf(path, sizeof(path), "%s/config/tables/blue_field_bottom.json", base_path);
    r = parse_table_file(&config->blue_field_bottom, path);
    log_table_result("blue_field_bottom.json", r, &config->blue_field_bottom);
}

/*=============================================================================
 * config_load_pokemon — Load species data, encounters, evolutions
 *===========================================================================*/
void config_load_pokemon(ConfigData *config, const char *base_path) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config/pokemon.json", base_path);

    char *json_str = read_file_to_string(path);
    if (!json_str) {
        fprintf(stderr, "[config] pokemon.json: NOT FOUND - using defaults\n");
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        fprintf(stderr, "[config] pokemon.json: PARSE ERROR\n");
        free(json_str);
        return;
    }

    PokemonConfig *pk = &config->pokemon;
    int species_count = 0;
    int red_wild_count = 0;
    int blue_wild_count = 0;

    /* Parse species array */
    cJSON *species_arr = cJSON_GetObjectItem(root, "species");
    if (species_arr && cJSON_IsArray(species_arr)) {
        int count = cJSON_GetArraySize(species_arr);
        if (count > CONFIG_NUM_POKEMON) count = CONFIG_NUM_POKEMON;
        species_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *entry = cJSON_GetArrayItem(species_arr, i);
            if (!entry) continue;

            PokemonEntry *pe = &pk->species[i];
            cJSON *item;

            item = cJSON_GetObjectItem(entry, "catchem_id");
            if (item && cJSON_IsNumber(item)) pe->catchem_id = (uint8_t)item->valueint;

            item = cJSON_GetObjectItem(entry, "animated_sprite_type");
            if (item && cJSON_IsNumber(item)) pe->animated_sprite_type = (uint8_t)item->valueint;

            item = cJSON_GetObjectItem(entry, "collision_mask_name");
            if (item && cJSON_IsString(item)) {
                strncpy(pe->collision_mask_name, item->valuestring,
                        sizeof(pe->collision_mask_name) - 1);
                pe->collision_mask_name[sizeof(pe->collision_mask_name) - 1] = '\0';
            }

            item = cJSON_GetObjectItem(entry, "animated_pic_name");
            if (item && cJSON_IsString(item)) {
                strncpy(pe->animated_pic_name, item->valuestring,
                        sizeof(pe->animated_pic_name) - 1);
                pe->animated_pic_name[sizeof(pe->animated_pic_name) - 1] = '\0';
            }

            /* catch_frame_durations: array of 3 */
            item = cJSON_GetObjectItem(entry, "catch_frame_durations");
            if (item && cJSON_IsArray(item)) {
                int dur_count = cJSON_GetArraySize(item);
                if (dur_count > 3) dur_count = 3;
                for (int d = 0; d < dur_count; d++) {
                    cJSON *dur = cJSON_GetArrayItem(item, d);
                    if (dur && cJSON_IsNumber(dur)) {
                        pe->catch_frame_durations[d] = (uint8_t)dur->valueint;
                    }
                }
            }

            /* evolution_data: array of 6 */
            item = cJSON_GetObjectItem(entry, "evolution_data");
            if (item && cJSON_IsArray(item)) {
                int evo_count = cJSON_GetArraySize(item);
                if (evo_count > 6) evo_count = 6;
                for (int e = 0; e < evo_count; e++) {
                    cJSON *ev = cJSON_GetArrayItem(item, e);
                    if (ev && cJSON_IsNumber(ev)) {
                        pe->evolution_data[e] = (uint8_t)ev->valueint;
                    }
                }
            }

            item = cJSON_GetObjectItem(entry, "evolution_object_count");
            if (item && cJSON_IsNumber(item)) {
                pe->evolution_object_count = (uint8_t)item->valueint;
            }
        }
    }

    /* Parse red_wild_mons: array of arrays */
    cJSON *red_wild = cJSON_GetObjectItem(root, "red_wild_mons");
    if (red_wild && cJSON_IsArray(red_wild)) {
        int table_count = cJSON_GetArraySize(red_wild);
        if (table_count > CONFIG_MAX_MAPS) table_count = CONFIG_MAX_MAPS;
        pk->num_red_wild_tables = (uint8_t)table_count;
        red_wild_count = table_count;

        for (int t = 0; t < table_count; t++) {
            cJSON *mons_arr = cJSON_GetArrayItem(red_wild, t);
            if (!mons_arr || !cJSON_IsArray(mons_arr)) continue;

            int mon_count = cJSON_GetArraySize(mons_arr);
            if (mon_count > CONFIG_MAX_WILD_MONS) mon_count = CONFIG_MAX_WILD_MONS;

            for (int m = 0; m < mon_count; m++) {
                cJSON *mon = cJSON_GetArrayItem(mons_arr, m);
                if (mon && cJSON_IsNumber(mon)) {
                    pk->red_wild_mons[t][m] = (uint8_t)mon->valueint;
                }
            }
        }
    }

    /* Parse blue_wild_mons: array of arrays */
    cJSON *blue_wild = cJSON_GetObjectItem(root, "blue_wild_mons");
    if (blue_wild && cJSON_IsArray(blue_wild)) {
        int table_count = cJSON_GetArraySize(blue_wild);
        if (table_count > CONFIG_MAX_MAPS) table_count = CONFIG_MAX_MAPS;
        pk->num_blue_wild_tables = (uint8_t)table_count;
        blue_wild_count = table_count;

        for (int t = 0; t < table_count; t++) {
            cJSON *mons_arr = cJSON_GetArrayItem(blue_wild, t);
            if (!mons_arr || !cJSON_IsArray(mons_arr)) continue;

            int mon_count = cJSON_GetArraySize(mons_arr);
            if (mon_count > CONFIG_MAX_WILD_MONS) mon_count = CONFIG_MAX_WILD_MONS;

            for (int m = 0; m < mon_count; m++) {
                cJSON *mon = cJSON_GetArrayItem(mons_arr, m);
                if (mon && cJSON_IsNumber(mon)) {
                    pk->blue_wild_mons[t][m] = (uint8_t)mon->valueint;
                }
            }
        }
    }

    /* Parse red_map_indices */
    cJSON *red_maps = cJSON_GetObjectItem(root, "red_map_indices");
    if (red_maps && cJSON_IsArray(red_maps)) {
        int count = cJSON_GetArraySize(red_maps);
        if (count > CONFIG_MAX_MAPS) count = CONFIG_MAX_MAPS;
        for (int i = 0; i < count; i++) {
            cJSON *idx = cJSON_GetArrayItem(red_maps, i);
            if (idx && cJSON_IsNumber(idx)) {
                pk->red_map_indices[i] = (uint8_t)idx->valueint;
            }
        }
    }

    /* Parse blue_map_indices */
    cJSON *blue_maps = cJSON_GetObjectItem(root, "blue_map_indices");
    if (blue_maps && cJSON_IsArray(blue_maps)) {
        int count = cJSON_GetArraySize(blue_maps);
        if (count > CONFIG_MAX_MAPS) count = CONFIG_MAX_MAPS;
        for (int i = 0; i < count; i++) {
            cJSON *idx = cJSON_GetArrayItem(blue_maps, i);
            if (idx && cJSON_IsNumber(idx)) {
                pk->blue_map_indices[i] = (uint8_t)idx->valueint;
            }
        }
    }

    pk->pokemon_loaded = true;

    fprintf(stderr, "[config] pokemon.json: loaded (%d species, %d red wild tables, %d blue wild tables)\n",
            species_count, red_wild_count, blue_wild_count);

    cJSON_Delete(root);
    free(json_str);
}

/*=============================================================================
 * config_load_palettes — Load palettes.json overrides
 *
 * JSON format:
 *   { "stages": { "red_field_top": { "bg": [[r,g,b,...], ...], "obj": [...] } },
 *     "screens": { "copyright": { ... }, ... } }
 *
 * Each palette = array of 12 ints (4 colors x r,g,b components 0-31).
 * 8 palettes per bg/obj section.
 *===========================================================================*/

/* Stage name → config index mapping */
static const struct { const char *name; int index; } stage_palette_names[] = {
    { "red_field_top",      0 },
    { "red_field_bottom",   1 },
    { "blue_field_top",     2 },
    { "blue_field_bottom",  3 },
    { "gengar_bonus",       4 },
    { "mewtwo_bonus",       5 },
    { "meowth_bonus",       6 },
    { "diglett_bonus",      7 },
    { "seel_bonus",         8 },
    { NULL, -1 }
};

/* Screen name → config index mapping */
static const struct { const char *name; int index; } screen_palette_names[] = {
    { "copyright",          0 },
    { "titlescreen",        1 },
    { "field_select",       2 },
    { "options",            3 },
    { "high_scores_red",    4 },
    { "high_scores_blue",   5 },
    { "pokedex",            6 },
    { NULL, -1 }
};

/* Parse a palette array: array of 8 sub-arrays, each with 12 ints (4 colors x r,g,b) */
static int parse_palette_array(const cJSON *arr, uint16_t out[32]) {
    if (!arr || !cJSON_IsArray(arr)) return 0;
    int count = cJSON_GetArraySize(arr);
    if (count > 8) count = 8;
    int loaded = 0;

    for (int p = 0; p < count; p++) {
        cJSON *pal = cJSON_GetArrayItem(arr, p);
        if (!pal || !cJSON_IsArray(pal)) continue;
        int num_vals = cJSON_GetArraySize(pal);
        if (num_vals < 12) continue; /* need 4 colors x 3 components */

        for (int c = 0; c < 4; c++) {
            int r = 0, g = 0, b = 0;
            cJSON *rv = cJSON_GetArrayItem(pal, c * 3);
            cJSON *gv = cJSON_GetArrayItem(pal, c * 3 + 1);
            cJSON *bv = cJSON_GetArrayItem(pal, c * 3 + 2);
            if (rv && cJSON_IsNumber(rv)) r = rv->valueint & 0x1F;
            if (gv && cJSON_IsNumber(gv)) g = gv->valueint & 0x1F;
            if (bv && cJSON_IsNumber(bv)) b = bv->valueint & 0x1F;
            out[p * 4 + c] = (uint16_t)(r | (g << 5) | (b << 10));
        }
        loaded++;
    }
    return loaded;
}

void config_load_palettes(ConfigData *config, const char *base_path) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config/palettes.json", base_path);

    char *json_str = read_file_to_string(path);
    if (!json_str) {
        fprintf(stderr, "[config] palettes.json: NOT FOUND - using defaults\n");
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        fprintf(stderr, "[config] palettes.json: PARSE ERROR\n");
        free(json_str);
        return;
    }

    PaletteConfig *pc = &config->palettes;
    int stage_count = 0, screen_count = 0;

    /* Parse stages */
    cJSON *stages = cJSON_GetObjectItem(root, "stages");
    if (stages && cJSON_IsObject(stages)) {
        for (int i = 0; stage_palette_names[i].name; i++) {
            cJSON *stage_obj = cJSON_GetObjectItem(stages, stage_palette_names[i].name);
            if (!stage_obj) continue;

            int idx = stage_palette_names[i].index;
            cJSON *bg_arr = cJSON_GetObjectItem(stage_obj, "bg");
            cJSON *obj_arr = cJSON_GetObjectItem(stage_obj, "obj");

            int bg_loaded = parse_palette_array(bg_arr, pc->stages[idx].bg);
            int obj_loaded = parse_palette_array(obj_arr, pc->stages[idx].obj);
            if (bg_loaded > 0 || obj_loaded > 0) stage_count++;
        }
    }

    /* Parse screens */
    cJSON *screens = cJSON_GetObjectItem(root, "screens");
    if (screens && cJSON_IsObject(screens)) {
        for (int i = 0; screen_palette_names[i].name; i++) {
            cJSON *screen_obj = cJSON_GetObjectItem(screens, screen_palette_names[i].name);
            if (!screen_obj) continue;

            int idx = screen_palette_names[i].index;
            cJSON *bg_arr = cJSON_GetObjectItem(screen_obj, "bg");
            cJSON *obj_arr = cJSON_GetObjectItem(screen_obj, "obj");

            int bg_loaded = parse_palette_array(bg_arr, pc->screens[idx].bg);
            int obj_loaded = parse_palette_array(obj_arr, pc->screens[idx].obj);
            if (bg_loaded > 0 || obj_loaded > 0) screen_count++;
        }
    }

    if (stage_count > 0 || screen_count > 0) {
        pc->palettes_loaded = true;
        fprintf(stderr, "[config] palettes.json: loaded (%d stage sets, %d screen sets)\n",
                stage_count, screen_count);
    } else {
        fprintf(stderr, "[config] palettes.json: parsed but no valid palette data found\n");
    }

    cJSON_Delete(root);
    free(json_str);
}

/*=============================================================================
 * Palette config lookup helpers
 *
 * Map stage_id / screen_id to config palette index.
 * Return pointer to config palette data if loaded, NULL otherwise.
 *===========================================================================*/

/* Stage ID (from constants.h) → config palette index */
static int stage_id_to_palette_index(uint8_t stage_id) {
    switch (stage_id) {
        case 0x0: return 0;  /* RED_FIELD_TOP */
        case 0x1: return 1;  /* RED_FIELD_BOTTOM */
        case 0x4: return 2;  /* BLUE_FIELD_TOP */
        case 0x5: return 3;  /* BLUE_FIELD_BOTTOM */
        case 0x7: return 4;  /* GENGAR_BONUS */
        case 0x9: return 5;  /* MEWTWO_BONUS */
        case 0xB: return 6;  /* MEOWTH_BONUS */
        case 0xD: return 7;  /* DIGLETT_BONUS */
        case 0xF: return 8;  /* SEEL_BONUS */
        default:  return -1;
    }
}

/* Screen ID → config palette index */
static int screen_id_to_palette_index(uint8_t screen_id) {
    switch (screen_id) {
        case 2: return 0;  /* COPYRIGHT */
        case 3: return 1;  /* TITLESCREEN */
        case 8: return 2;  /* FIELD_SELECT */
        case 6: return 3;  /* OPTIONS */
        case 7: return -1; /* HIGH_SCORES — handled separately via stage param */
        case 5: return 6;  /* POKEDEX */
        case 1: return 4;  /* ERASE_ALL_DATA — uses high_scores_red palettes */
        default: return -1;
    }
}

const uint16_t *config_get_stage_bg_palettes(const ConfigData *config, uint8_t stage_id) {
    if (!config || !config->palettes.palettes_loaded) return NULL;
    int idx = stage_id_to_palette_index(stage_id);
    if (idx < 0) return NULL;
    return config->palettes.stages[idx].bg;
}

const uint16_t *config_get_stage_obj_palettes(const ConfigData *config, uint8_t stage_id) {
    if (!config || !config->palettes.palettes_loaded) return NULL;
    int idx = stage_id_to_palette_index(stage_id);
    if (idx < 0) return NULL;
    return config->palettes.stages[idx].obj;
}

const uint16_t *config_get_screen_bg_palettes(const ConfigData *config, uint8_t screen_id) {
    if (!config || !config->palettes.palettes_loaded) return NULL;
    int idx = screen_id_to_palette_index(screen_id);
    if (idx < 0) return NULL;
    return config->palettes.screens[idx].bg;
}

const uint16_t *config_get_screen_obj_palettes(const ConfigData *config, uint8_t screen_id) {
    if (!config || !config->palettes.palettes_loaded) return NULL;
    int idx = screen_id_to_palette_index(screen_id);
    if (idx < 0) return NULL;
    return config->palettes.screens[idx].obj;
}

/*=============================================================================
 * config_load_audio — Load audio.json overrides
 *
 * JSON format:
 *   { "music": { "red_field": { "bank": 15, "id": 1 }, ... },
 *     "sfx":   { "flipper":   { "bank": 0,  "id": 12 }, ... } }
 *===========================================================================*/
void config_load_audio(ConfigData *config, const char *base_path) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config/audio.json", base_path);

    char *json_str = read_file_to_string(path);
    if (!json_str) {
        fprintf(stderr, "[config] audio.json: NOT FOUND - using defaults\n");
        return;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        fprintf(stderr, "[config] audio.json: PARSE ERROR\n");
        free(json_str);
        return;
    }

    AudioConfig *ac = &config->audio_config;

    /* Parse music entries */
    cJSON *music = cJSON_GetObjectItem(root, "music");
    if (music && cJSON_IsObject(music)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, music) {
            if (ac->num_music >= CONFIG_MAX_MUSIC) break;
            if (!cJSON_IsObject(entry)) continue;

            AudioEntry *ae = &ac->music[ac->num_music];
            strncpy(ae->name, entry->string, sizeof(ae->name) - 1);
            ae->name[sizeof(ae->name) - 1] = '\0';
            ae->file[0] = '\0';
            ae->clip_index = -1;

            cJSON *bank = cJSON_GetObjectItem(entry, "bank");
            cJSON *id = cJSON_GetObjectItem(entry, "id");
            if (bank && cJSON_IsNumber(bank)) ae->bank = (uint8_t)bank->valueint;
            if (id && cJSON_IsNumber(id)) ae->id = (uint8_t)id->valueint;

            cJSON *file = cJSON_GetObjectItem(entry, "file");
            if (file && cJSON_IsString(file) && strlen(file->valuestring) > 0) {
                strncpy(ae->file, file->valuestring, sizeof(ae->file) - 1);
                ae->file[sizeof(ae->file) - 1] = '\0';
            }

            ac->num_music++;
        }
    }

    /* Parse sfx entries */
    cJSON *sfx = cJSON_GetObjectItem(root, "sfx");
    if (sfx && cJSON_IsObject(sfx)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, sfx) {
            if (ac->num_sfx >= CONFIG_MAX_SFX) break;
            if (!cJSON_IsObject(entry)) continue;

            AudioEntry *ae = &ac->sfx[ac->num_sfx];
            strncpy(ae->name, entry->string, sizeof(ae->name) - 1);
            ae->name[sizeof(ae->name) - 1] = '\0';
            ae->file[0] = '\0';
            ae->clip_index = -1;

            cJSON *bank = cJSON_GetObjectItem(entry, "bank");
            cJSON *id = cJSON_GetObjectItem(entry, "id");
            if (bank && cJSON_IsNumber(bank)) ae->bank = (uint8_t)bank->valueint;
            if (id && cJSON_IsNumber(id)) ae->id = (uint8_t)id->valueint;

            cJSON *file = cJSON_GetObjectItem(entry, "file");
            if (file && cJSON_IsString(file) && strlen(file->valuestring) > 0) {
                strncpy(ae->file, file->valuestring, sizeof(ae->file) - 1);
                ae->file[sizeof(ae->file) - 1] = '\0';
            }

            ac->num_sfx++;
        }
    }

    /* Parse volume settings */
    cJSON *volume = cJSON_GetObjectItem(root, "volume");
    if (volume && cJSON_IsObject(volume)) {
        cJSON *master = cJSON_GetObjectItem(volume, "master");
        if (master && cJSON_IsNumber(master)) {
            ac->master_volume = (int)master->valuedouble;
            if (ac->master_volume < 0) ac->master_volume = 0;
            if (ac->master_volume > 200) ac->master_volume = 200;
        }
        cJSON *mus = cJSON_GetObjectItem(volume, "music");
        if (mus && cJSON_IsNumber(mus)) {
            ac->music_volume = (int)mus->valuedouble;
            if (ac->music_volume < 0) ac->music_volume = 0;
            if (ac->music_volume > 200) ac->music_volume = 200;
        }
        cJSON *sfxv = cJSON_GetObjectItem(volume, "sfx");
        if (sfxv && cJSON_IsNumber(sfxv)) {
            ac->sfx_volume = (int)sfxv->valuedouble;
            if (ac->sfx_volume < 0) ac->sfx_volume = 0;
            if (ac->sfx_volume > 200) ac->sfx_volume = 200;
        }
    }

    /* Parse pikachu_clips section */
    cJSON *pika = cJSON_GetObjectItem(root, "pikachu_clips");
    if (pika && cJSON_IsObject(pika)) {
        cJSON *clip0 = cJSON_GetObjectItem(pika, "pi_ka_chu");
        if (clip0 && cJSON_IsString(clip0) && strlen(clip0->valuestring) > 0) {
            strncpy(ac->pikachu_clip_0, clip0->valuestring,
                    sizeof(ac->pikachu_clip_0) - 1);
            ac->pikachu_clip_0[sizeof(ac->pikachu_clip_0) - 1] = '\0';
        }
        cJSON *clip1 = cJSON_GetObjectItem(pika, "piiiiikaaaa");
        if (clip1 && cJSON_IsString(clip1) && strlen(clip1->valuestring) > 0) {
            strncpy(ac->pikachu_clip_1, clip1->valuestring,
                    sizeof(ac->pikachu_clip_1) - 1);
            ac->pikachu_clip_1[sizeof(ac->pikachu_clip_1) - 1] = '\0';
        }
    }

    if (ac->num_music > 0 || ac->num_sfx > 0 ||
        ac->master_volume >= 0 || ac->music_volume >= 0 || ac->sfx_volume >= 0) {
        ac->audio_loaded = true;
        fprintf(stderr, "[config] audio.json: loaded (%d music, %d sfx, vol=%d%%)\n",
                ac->num_music, ac->num_sfx,
                ac->master_volume >= 0 ? ac->master_volume : 100);
    } else {
        fprintf(stderr, "[config] audio.json: parsed but no valid entries found\n");
    }

    cJSON_Delete(root);
    free(json_str);
}

/*=============================================================================
 * Audio config lookup helpers
 *===========================================================================*/
bool config_get_music(const ConfigData *config, const char *name,
                      uint8_t *bank, uint8_t *id, int *clip_index) {
    if (!config || !config->audio_config.audio_loaded || !name) return false;
    const AudioConfig *ac = &config->audio_config;
    for (int i = 0; i < ac->num_music; i++) {
        if (strcmp(ac->music[i].name, name) == 0) {
            *bank = ac->music[i].bank;
            *id = ac->music[i].id;
            if (clip_index) *clip_index = ac->music[i].clip_index;
            return true;
        }
    }
    return false;
}

bool config_get_sfx(const ConfigData *config, const char *name,
                    uint8_t *bank, uint8_t *id, int *clip_index) {
    if (!config || !config->audio_config.audio_loaded || !name) return false;
    const AudioConfig *ac = &config->audio_config;
    for (int i = 0; i < ac->num_sfx; i++) {
        if (strcmp(ac->sfx[i].name, name) == 0) {
            *bank = ac->sfx[i].bank;
            *id = ac->sfx[i].id;
            if (clip_index) *clip_index = ac->sfx[i].clip_index;
            return true;
        }
    }
    return false;
}

/*=============================================================================
 * config_apply_audio_volume — Apply volume settings to audio engine
 *===========================================================================*/
void config_apply_audio_volume(const ConfigData *config, struct AudioEngine *audio) {
    if (!config || !audio) return;
    const AudioConfig *ac = &config->audio_config;

    if (ac->master_volume >= 0) {
        float scale = (float)ac->master_volume / 100.0f;
        audio_set_volume_scale(audio, scale);
    }
    if (ac->music_volume >= 0) {
        float scale = (float)ac->music_volume / 100.0f;
        audio_set_music_volume_scale(audio, scale);
    }
    if (ac->sfx_volume >= 0) {
        float scale = (float)ac->sfx_volume / 100.0f;
        audio_set_sfx_volume_scale(audio, scale);
    }
}

/*=============================================================================
 * config_load_all — Load all config files from base_path/config/
 *===========================================================================*/
void config_load_all(ConfigData *config, const char *base_path) {
    if (!config || !base_path) return;

    fprintf(stderr, "[config] Loading config files from %s/config/\n", base_path);
    config_load_physics(config, base_path);
    config_load_scores(config, base_path);
    config_load_tables(config, base_path);
    config_load_pokemon(config, base_path);
    config_load_palettes(config, base_path);
    config_load_audio(config, base_path);
    fprintf(stderr, "[config] Config loading complete (9 files)\n");
}
