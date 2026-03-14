/*
 * config_data.h — External JSON configuration system
 *
 * Makes Pokemon Pinball+ fully moddable by externalizing:
 *   - Physics constants (gravity, velocity, flippers, collision response)
 *   - Score values (all BCD score constants)
 *   - Table object definitions (positions, bounding boxes per stage)
 *   - Pokemon data (species, evolutions, encounters)
 *
 * All data has hardcoded defaults matching the original game.
 * JSON files in config/ override any subset of values.
 * Missing files or fields gracefully fall back to defaults.
 */

#ifndef CONFIG_DATA_H
#define CONFIG_DATA_H

#include <stdint.h>
#include <stdbool.h>

#define CONFIG_MAX_OBJECTS_PER_GROUP 16
#define CONFIG_MAX_GROUPS_PER_TABLE 24
#define CONFIG_MAX_ATTRS 64
#define CONFIG_MAX_WILD_MONS 32
#define CONFIG_MAX_MAPS 18
#define CONFIG_NUM_POKEMON 151
#define CONFIG_MAX_SLOT_REWARD_SETS 25
#define CONFIG_MAX_SLOT_PERMUTATIONS 256

/*=============================================================================
 * Physics Config
 *===========================================================================*/
typedef struct PhysicsConfig {
    /* Gravity & velocity */
    int16_t  gravity;                     /* Default: 0x000B */
    int8_t   max_velocity_hi;             /* Default: 7 */
    int16_t  position_clamp_positive;     /* Default: 0x04FF */
    int16_t  position_clamp_negative;     /* Default: (int16_t)0xFB01 */

    /* Stage transitions */
    uint8_t  stage_transition_up_y;       /* Default: 8 */
    uint8_t  stage_transition_down_y;     /* Default: 168 */
    uint16_t stage_transition_y_offset;   /* Default: 0x8800 */

    /* Flippers */
    uint16_t flipper_delta;               /* Default: 0x0333 */
    uint16_t flipper_max;                 /* Default: 0x0F00 */
    uint16_t flipper_radius_magnitudes[32];

    /* Flipper collision bounds */
    uint8_t  flipper_collision_x_min;     /* Default: 43 */
    uint8_t  flipper_collision_x_range;   /* Default: 48 */
    uint8_t  flipper_collision_y_min;     /* Default: 123 */
    uint8_t  flipper_collision_y_range;   /* Default: 32 */

    /* Collision response */
    uint8_t  damping_shift;               /* Default: 2 (divide by 4) */
    uint8_t  amplification_shift;         /* Default: 3 (divide by 8) */
    uint8_t  spin_transfer_shift;         /* Default: 1 (divide by 2) */
    uint8_t  spin_recalc_shift;           /* Default: 2 (multiply by 4) */
} PhysicsConfig;

/*=============================================================================
 * Score Config — all values stored as 4-byte BCD (little-endian)
 *===========================================================================*/
typedef struct ScoreConfig {
    /* Common score values (shared across fields) */
    uint8_t score_5[4];
    uint8_t score_10[4];
    uint8_t score_100[4];
    uint8_t score_400[4];
    uint8_t score_500[4];
    uint8_t score_5000[4];
    uint8_t score_10000[4];
    uint8_t score_100000[4];
    uint8_t score_300000[4];
    uint8_t score_1000000[4];
    uint8_t score_10000000[4];

    /* Gengar bonus */
    uint8_t gastly_hit[4];
    uint8_t haunter_hit[4];
    uint8_t gengar_hit[4];
    uint8_t gravestone[4];

    /* Mewtwo bonus */
    uint8_t mewtwo_hit[4];
    uint8_t mewtwo_orb_hit[4];

    /* Meowth bonus */
    uint8_t meowth_jewel[4];
    uint8_t meowth_hit[4];

    /* Diglett bonus */
    uint8_t diglett_hit[4];
    uint8_t dugtrio_hit[4];

    /* Seel bonus */
    uint8_t seel_hit[4];
    uint8_t seel_dive[4];

    /* Jackpot values (red field catch mode) */
    uint8_t jackpot_catch_voltorb[4];
    uint8_t jackpot_catch_spinner[4];
    uint8_t jackpot_catch_bellsprout[4];
    uint8_t jackpot_evo_voltorb[4];
    uint8_t jackpot_evo_bellsprout[4];
    uint8_t jackpot_evo_spinner[4];
} ScoreConfig;

/*=============================================================================
 * Table Object Config — collision objects per stage
 *===========================================================================*/
typedef struct TableObject {
    uint8_t id;
    uint8_t x;
    uint8_t y;
} TableObject;

typedef struct TableObjectGroup {
    char name[32];
    uint8_t x_thresh;
    uint8_t y_thresh;
    bool attribute_gated;
    uint8_t collision_attrs[CONFIG_MAX_ATTRS]; /* 0xFF-terminated */
    TableObject objects[CONFIG_MAX_OBJECTS_PER_GROUP];
    uint8_t num_objects;
} TableObjectGroup;

typedef struct TableConfig {
    TableObjectGroup groups[CONFIG_MAX_GROUPS_PER_TABLE];
    uint8_t num_groups;
} TableConfig;

/*=============================================================================
 * Pokemon Config — species data, encounters, evolutions
 *===========================================================================*/
typedef struct PokemonEntry {
    uint8_t catchem_id;               /* Index into catch sprite frame table */
    uint8_t animated_sprite_type;     /* Sprite type for catch animation */
    char collision_mask_name[48];     /* Filename for collision mask */
    char animated_pic_name[48];       /* Filename for animated billboard pic */
    uint8_t catch_frame_durations[3]; /* idle1, idle2, hit frame durations */
    uint8_t evolution_data[6];        /* Evolution targets and types */
    uint8_t evolution_object_count;   /* Trinkets needed (minus 2) */
} PokemonEntry;

typedef struct PokemonConfig {
    bool pokemon_loaded;              /* true if loaded from JSON */
    PokemonEntry species[CONFIG_NUM_POKEMON];

    /* Wild encounter tables: [map_index][up to 32 pokemon] */
    uint8_t red_wild_mons[CONFIG_MAX_MAPS][CONFIG_MAX_WILD_MONS];
    uint8_t blue_wild_mons[CONFIG_MAX_MAPS][CONFIG_MAX_WILD_MONS];
    uint8_t red_map_indices[CONFIG_MAX_MAPS];   /* map_id -> wild_mons index */
    uint8_t blue_map_indices[CONFIG_MAX_MAPS];
    uint8_t num_red_wild_tables;
    uint8_t num_blue_wild_tables;
} PokemonConfig;

/*=============================================================================
 * Palette Config — stage and screen palette sets
 *===========================================================================*/
#define CONFIG_NUM_STAGE_PALETTES  9
#define CONFIG_NUM_SCREEN_PALETTES 7

typedef struct PaletteSet {
    uint16_t bg[32];   /* 8 palettes x 4 colors, RGB555 */
    uint16_t obj[32];  /* 8 palettes x 4 colors, RGB555 */
} PaletteSet;

typedef struct PaletteConfig {
    bool palettes_loaded;
    PaletteSet stages[CONFIG_NUM_STAGE_PALETTES];
    PaletteSet screens[CONFIG_NUM_SCREEN_PALETTES];
} PaletteConfig;

/*=============================================================================
 * Audio Config — music and SFX bank/id mappings
 *===========================================================================*/
#define CONFIG_MAX_MUSIC 32
#define CONFIG_MAX_SFX   96

typedef struct AudioEntry {
    char name[32];
    uint8_t bank;
    uint8_t id;
    char file[128];      /* Optional file path (empty = use bytecode) */
    int clip_index;      /* Index into AudioEngine custom_clips[], -1 = none */
} AudioEntry;

typedef struct AudioConfig {
    bool audio_loaded;
    AudioEntry music[CONFIG_MAX_MUSIC];
    uint8_t num_music;
    AudioEntry sfx[CONFIG_MAX_SFX];
    uint8_t num_sfx;
    /* Volume levels (0-100 percent, -1 = not set / use default) */
    int master_volume;   /* Overall volume scale */
    int music_volume;    /* Music channel volume scale */
    int sfx_volume;      /* SFX channel volume scale */
    /* Pikachu PCM clip paths (empty = use defaults) */
    char pikachu_clip_0[128];  /* "pi-ka-chu" — default: audio/sound_clips/pi_ka_chu.wav */
    char pikachu_clip_1[128];  /* "piiiiikaaaa" — default: audio/sound_clips/piiiiikaaaa.wav */
} AudioConfig;

/*=============================================================================
 * Master Config — holds all sub-configs
 *===========================================================================*/
typedef struct ConfigData {
    PhysicsConfig physics;
    ScoreConfig scores;
    TableConfig red_field_top;
    TableConfig red_field_bottom;
    TableConfig blue_field_top;
    TableConfig blue_field_bottom;
    PokemonConfig pokemon;
    PaletteConfig palettes;
    AudioConfig audio_config;
} ConfigData;

/*=============================================================================
 * Public API
 *===========================================================================*/

/* Create config with all hardcoded defaults. Never returns NULL. */
ConfigData *config_data_create(void);

/* Free config (safe to call with NULL). */
void config_data_free(ConfigData *config);

/* Load all config files from base_path/config/. Missing files use defaults. */
void config_load_all(ConfigData *config, const char *base_path);

/* Individual loaders (called by config_load_all) */
void config_load_physics(ConfigData *config, const char *base_path);
void config_load_scores(ConfigData *config, const char *base_path);
void config_load_tables(ConfigData *config, const char *base_path);
void config_load_pokemon(ConfigData *config, const char *base_path);
void config_load_palettes(ConfigData *config, const char *base_path);
void config_load_audio(ConfigData *config, const char *base_path);

/* Convert a decimal integer to 4-byte BCD (little-endian).
 * E.g. 10000000 -> {0x00, 0x00, 0x00, 0x10} */
void int_to_bcd4(uint32_t value, uint8_t out[4]);

/* Audio config lookup helpers.
 * Return true if found in config, writing bank/id and clip_index.
 * clip_index: -1 = no custom file (use bytecode), >=0 = custom clip index.
 * False = use hardcoded fallback. */
bool config_get_music(const ConfigData *config, const char *name,
                      uint8_t *bank, uint8_t *id, int *clip_index);
bool config_get_sfx(const ConfigData *config, const char *name,
                    uint8_t *bank, uint8_t *id, int *clip_index);

/* Palette config lookup helpers.
 * Return pointer to config palette data if loaded, NULL otherwise. */
const uint16_t *config_get_stage_bg_palettes(const ConfigData *config, uint8_t stage_id);
const uint16_t *config_get_stage_obj_palettes(const ConfigData *config, uint8_t stage_id);
const uint16_t *config_get_screen_bg_palettes(const ConfigData *config, uint8_t screen_id);
const uint16_t *config_get_screen_obj_palettes(const ConfigData *config, uint8_t screen_id);

/* Apply audio volume config to audio engine (call after config_load_all) */
struct AudioEngine;
void config_apply_audio_volume(const ConfigData *config, struct AudioEngine *audio);

/* Audio playback wrapper macros — check config first, fall back to hardcoded.
 * If the config entry has a custom audio file (clip_index >= 0), play that
 * instead of the GBC bytecode track.
 * If a Lua table has custom stage music loaded (via manifest.json "music"),
 * that takes priority over config and bytecode for "red_field"/"blue_field". */
struct GameState;
bool script_try_play_stage_music(struct GameState *state, const char *name);
#define PLAY_MUSIC(state, name, fallback_bank, fallback_id) do { \
    if (!script_try_play_stage_music((state), name)) { \
        uint8_t _b, _i; int _ci = -1; \
        if ((state)->config && config_get_music((state)->config, name, &_b, &_i, &_ci)) { \
            if (_ci >= 0) \
                audio_play_custom_music((state)->audio, _ci); \
            else \
                audio_play_music((state)->audio, _b, _i); \
        } else \
            audio_play_music((state)->audio, fallback_bank, fallback_id); \
    } \
} while(0)

#define PLAY_SFX(state, name, fallback_bank, fallback_id) do { \
    uint8_t _b, _i; int _ci = -1; \
    if ((state)->config && config_get_sfx((state)->config, name, &_b, &_i, &_ci)) { \
        if (_ci >= 0) \
            audio_play_custom_sfx((state)->audio, _ci); \
        else \
            audio_play_sfx((state)->audio, _b, _i); \
    } else \
        audio_play_sfx((state)->audio, fallback_bank, fallback_id); \
} while(0)

#endif /* CONFIG_DATA_H */
