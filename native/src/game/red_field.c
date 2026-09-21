/*
 * Red Field Game Objects
 *
 * Translated from:
 *   data/collision/game_objects/red_stage_game_object_collision.asm
 *   engine/pinball_game/object_collision/object_collision.asm (0x2720)
 *   engine/pinball_game/object_collision/red_stage_object_collision.asm (0x143e1)
 *   engine/pinball_game/object_collision/red_stage_resolve_collision.asm (0x1460e)
 *   engine/pinball_game/stage_init/init_red_field.asm (0x30000)
 *
 * Two-phase collision system:
 *   Phase 1 (check): Tests ball against objects in ASM dispatch order.
 *     Attribute-gated objects require matching tile collision attribute.
 *     Sets per-object which_xxx flags. Stops after first hit.
 *   Phase 2 (resolve): ALL handlers run every frame.
 *     Each checks its own which_xxx flag independently.
 *     Voltorb/bumper use flipper force system (NOT atan2 angles).
 */

#include "game/red_field.h"
#include "game/billboard.h"
#include "game/collision.h"
#include "game/config_data.h"
#include "game/score.h"
#include "game/joypad.h"
#include "game/animation.h"
#include "game/ball_gfx.h"
#include "game/draw_red_field.h"
#include "game/rng.h"
#include "game/timer.h"
#include "game/blue_field.h"
#include "audio/audio.h"
#include "renderer/vram.h"
#include "renderer/tile_loader.h"
#include "data/embedded_data.h"
#include "renderer/stage_assets.h"
#include <string.h>
#include <stdio.h>

/* Forward declarations */
static void load_scrolling_text_with_bcd(GameState *state, int slot_index,
                                          const uint8_t *header,
                                          const uint8_t bcd[4]);

/* Pokemon names from main_loop.c (also declared later for evolution mode) */
extern const char pokedex_names[151][12];

/* Score constants now read from state->config->scores (see config/scores.json) */

/* BallTypeProgressionRedField (0x15505): lookup table for ball upgrades.
 * Index by current ball_type, value is the next ball_type. */
static const uint8_t BallTypeProgressionRedField[6] = {
    GREAT_BALL,   /* 0: Pokeball → Great */
    GREAT_BALL,   /* 1: unused */
    ULTRA_BALL,   /* 2: Great → Ultra */
    MASTER_BALL,  /* 3: Ultra → Master */
    MASTER_BALL,  /* 4: unused */
    MASTER_BALL,  /* 5: Master → Master */
};

/*=============================================================================
 * BallTypeIncreases (0xef2f): how many billboard IDs to add for ball upgrade.
 * Index by ball_type. Result added to BILLBOARD_GREAT_BALL (9).
 *===========================================================================*/
static const uint8_t BallTypeIncreases[6] = {
    0, /* Pokeball → +0 = GREAT(9) */
    0, /* unused */
    1, /* Great → +1 = ULTRA(10) */
    2, /* Ultra → +2 = MASTER(11) */
    2, /* unused */
    2, /* Master → +2 = MASTER(11) */
};

/*=============================================================================
 * BonusStages_RedField: maps next_bonus_stage index to stage ID.
 *===========================================================================*/
static const uint8_t BonusStages_RedField[5] = {
    STAGE_GENGAR_BONUS,   /* 0 */
    STAGE_MEWTWO_BONUS,   /* 1 */
    STAGE_MEOWTH_BONUS,   /* 2 */
    STAGE_DIGLETT_BONUS,  /* 3 */
    STAGE_SEEL_BONUS,     /* 4 */
};

/*=============================================================================
 * SlotRewardRoulettePermutations (0xf339): 16 rows × 16 columns.
 * Each row is a random permutation of offsets (0,2,4,6,8) into SlotRewardSets.
 *===========================================================================*/
static const uint8_t SlotRewardRoulettePermutations[256] = {
    2, 6, 0, 8, 4, 2, 6, 8, 4, 0, 6, 2, 4, 8, 0, 2,
    6, 2, 4, 8, 0, 6, 4, 8, 2, 0, 6, 8, 2, 0, 6, 8,
    2, 4, 0, 8, 6, 4, 0, 2, 6, 4, 0, 8, 6, 4, 2, 8,
    0, 8, 2, 4, 0, 8, 6, 2, 4, 0, 6, 8, 4, 0, 6, 2,
    0, 8, 2, 4, 0, 8, 6, 4, 2, 8, 0, 6, 2, 8, 0, 6,
    2, 0, 6, 4, 2, 0, 6, 8, 2, 4, 0, 6, 8, 4, 2, 6,
    0, 2, 8, 4, 0, 2, 6, 4, 8, 2, 6, 0, 4, 8, 6, 2,
    4, 8, 6, 2, 0, 8, 4, 6, 0, 2, 4, 6, 0, 2, 4, 8,
    2, 0, 4, 6, 2, 0, 8, 4, 2, 0, 6, 4, 8, 0, 6, 4,
    4, 0, 2, 8, 4, 6, 0, 8, 2, 4, 6, 8, 0, 4, 6, 2,
    6, 8, 4, 2, 6, 0, 8, 2, 4, 0, 6, 2, 8, 4, 6, 2,
    4, 6, 2, 0, 8, 4, 6, 0, 8, 2, 6, 0, 8, 2, 4, 0,
    2, 0, 6, 4, 2, 8, 6, 0, 4, 8, 2, 0, 4, 6, 8, 0,
    8, 6, 4, 0, 8, 6, 2, 0, 8, 6, 4, 0, 8, 6, 4, 2,
    2, 0, 6, 4, 8, 2, 0, 4, 8, 2, 0, 4, 6, 2, 8, 0,
    4, 6, 8, 2, 0, 6, 4, 8, 2, 6, 0, 8, 4, 6, 2, 8,
};

/*=============================================================================
 * SlotRewardSets (0xf439): 25 groups × 5 pairs × 2 bytes.
 * Each pair = {billboard_id, weight}. $FF = empty slot (skipped).
 * Indexed by slot_reward_progress + permutation offset.
 *===========================================================================*/
static const uint8_t SlotRewardSets[250] = {
    /* Progress=0 */   5,0x19, 12,0x4C,  0,0x4C,  3,0x4C, 0xFF,0,
    /* Progress=10 */  5,0x19, 12,0x4C,  0,0x4C,  7,0x4C, 0xFF,0,
    /* Progress=20 */  5,0x19, 12,0x44,  0,0x44,  3,0x44,  6,0x16,
    /* Progress=30 */  5,0x19, 12,0x4C,  0,0x4C,  8,0x4C, 0xFF,0,
    /* Progress=40 */  1,0x4C,  6,0x66, 13,0x4C, 0xFF,0, 0xFF,0,
    /* Progress=50 */  5,0x19, 12,0x4C,  0,0x4C,  3,0x4C, 0xFF,0,
    /* Progress=60 */  5,0x19, 12,0x4C,  0,0x4C,  7,0x4C, 0xFF,0,
    /* Progress=70 */  5,0x19, 12,0x44,  0,0x44,  3,0x44,  6,0x16,
    /* Progress=80 */  5,0x19, 12,0x4C,  0,0x4C,  8,0x4C, 0xFF,0,
    /* Progress=90 */  1,0x3F,  6,0x3F, 13,0x3F,  9,0x3F, 0xFF,0,
    /* Progress=100*/  5,0x11, 12,0x4F,  0,0x4F,  3,0x4F, 0xFF,0,
    /* Progress=110*/  5,0x11, 12,0x4F,  1,0x4F,  7,0x4F, 0xFF,0,
    /* Progress=120*/  5,0x11, 12,0x44,  0,0x44,  3,0x44,  6,0x1E,
    /* Progress=130*/  5,0x11, 12,0x4F,  1,0x4F,  8,0x4F, 0xFF,0,
    /* Progress=140*/  2,0x66,  6,0x4C, 13,0x4C, 0xFF,0, 0xFF,0,
    /* Progress=150*/  5,0x0A, 12,0x51,  0,0x51,  3,0x51, 0xFF,0,
    /* Progress=160*/  5,0x0A, 12,0x51,  1,0x51,  7,0x51, 0xFF,0,
    /* Progress=170*/  5,0x0A, 12,0x44,  0,0x44,  3,0x44,  6,0x26,
    /* Progress=180*/  5,0x0A, 12,0x51,  1,0x51,  8,0x51, 0xFF,0,
    /* Progress=190*/  1,0x3F,  6,0x3F, 13,0x3F,  9,0x3F, 0xFF,0,
    /* Progress=200*/  5,0x0A, 12,0x51,  0,0x51,  3,0x51, 0xFF,0,
    /* Progress=210*/  5,0x0A, 12,0x51,  1,0x51,  7,0x51, 0xFF,0,
    /* Progress=220*/  5,0x0A, 12,0x44,  0,0x44,  3,0x44,  6,0x26,
    /* Progress=230*/  5,0x0A, 12,0x51,  1,0x51,  8,0x51, 0xFF,0,
    /* Progress=240*/  1,0x26,  6,0x26, 13,0x26,  4,0x8C, 0xFF,0,
};

/*=============================================================================
 * Object collision data (from red_stage_game_object_collision.asm)
 * Format per group: {x_thresh, y_thresh}, then {id, x, y}... terminated $FF
 *===========================================================================*/
typedef struct {
    uint8_t id;
    uint8_t x;
    uint8_t y;
} ObjectEntry;

/* Voltorb: id $03-$05, bounds 14x14, attribute-gated */
static const ObjectEntry voltorb_entries[] = {
    {0x03, 0x42, 0x66}, {0x04, 0x5A, 0x5C}, {0x05, 0x55, 0x78},
};
#define VOLTORB_X_THRESH 0x0E
#define VOLTORB_Y_THRESH 0x0E

/* Spinner: id $09, bounds 8x4, direct */
static const ObjectEntry spinner_entries[] = {
    {0x09, 0x90, 0x6C},
};
#define SPINNER_X_THRESH 0x08
#define SPINNER_Y_THRESH 0x04

/* Board Triggers: id $11-$18, bounds 9x9, direct */
static const ObjectEntry board_trigger_entries[] = {
    {0x11, 0x1C, 0x3C}, {0x12, 0x2A, 0x44},
    {0x13, 0x25, 0x63}, {0x14, 0x12, 0x7A},
    {0x15, 0x26, 0x84}, {0x16, 0x7C, 0x44},
    {0x17, 0x8E, 0x7A}, {0x18, 0x7F, 0x39},
};
#define BOARD_TRIG_X_THRESH 0x09
#define BOARD_TRIG_Y_THRESH 0x09

/* Top Staryu: id $19, bounds 8x6, attribute-gated */
static const ObjectEntry top_staryu_entries[] = {
    {0x19, 0x40, 0x90},
};
#define STARYU_X_THRESH 0x08
#define STARYU_Y_THRESH 0x06

/* Bottom Staryu: id $1A, bounds 8x6, attribute-gated */
static const ObjectEntry bottom_staryu_entries[] = {
    {0x1A, 0x40, 0x08},
};

/* Bellsprout: id $1B, bounds 6x5, direct */
static const ObjectEntry bellsprout_entries[] = {
    {0x1B, 0x7B, 0x76},
};
#define BELLSPROUT_X_THRESH 0x06
#define BELLSPROUT_Y_THRESH 0x05

/* Ditto Slot: id $1F, bounds 3x3, direct */
static const ObjectEntry ditto_slot_entries[] = {
    {0x1F, 0x12, 0x24},
};
#define DITTO_X_THRESH 0x03
#define DITTO_Y_THRESH 0x03

/* Pinball Upgrade Triggers: id $0E-$10, bounds 6x5, direct */
static const ObjectEntry upgrade_entries[] = {
    {0x0E, 0x3A, 0x53}, {0x0F, 0x50, 0x48}, {0x10, 0x66, 0x49},
};
#define UPGRADE_X_THRESH 0x06
#define UPGRADE_Y_THRESH 0x05

/* Wild Pokemon: id $1E, bounds 26x26, attribute-gated */
static const ObjectEntry wild_mon_entries[] = {
    {0x1E, 0x50, 0x40},
};
#define WILD_MON_X_THRESH 0x1A
#define WILD_MON_Y_THRESH 0x1A

/* Diglett: id $01-$02, bounds 8x12, attribute-gated */
static const ObjectEntry diglett_entries[] = {
    {0x01, 0x20, 0x40}, {0x02, 0x80, 0x40},
};
#define DIGLETT_X_THRESH 0x08
#define DIGLETT_Y_THRESH 0x0C

/* Bonus Multipliers: id $21-$22, bounds 7x7, attribute-gated */
static const ObjectEntry bonus_mult_entries[] = {
    {0x21, 0x2C, 0x20}, {0x22, 0x74, 0x20},
};
#define BONUS_MULT_X_THRESH 0x07
#define BONUS_MULT_Y_THRESH 0x07

/* Slot: id $20, bounds 4x4, direct */
static const ObjectEntry slot_entries[] = {
    {0x20, 0x50, 0x16},
};
#define SLOT_X_THRESH 0x04
#define SLOT_Y_THRESH 0x04

/* Bumpers: id $06-$07, bounds 6x11, attribute-gated */
static const ObjectEntry bumper_entries[] = {
    {0x06, 0x30, 0x66}, {0x07, 0x6F, 0x66},
};
#define BUMPER_X_THRESH 0x06
#define BUMPER_Y_THRESH 0x0B

/* Pikachu Savers: id $1C-$1D, bounds 3x5, direct */
static const ObjectEntry pikachu_entries[] = {
    {0x1C, 0x0E, 0x7C}, {0x1D, 0x92, 0x7C},
};
#define PIKACHU_X_THRESH 0x03
#define PIKACHU_Y_THRESH 0x05

/* CAVE Lights: id $0A-$0D, bounds 5x3, direct */
static const ObjectEntry cave_light_entries[] = {
    {0x0A, 0x0E, 0x65}, {0x0B, 0x1E, 0x65},
    {0x0C, 0x82, 0x65}, {0x0D, 0x92, 0x65},
};
#define CAVE_X_THRESH 0x05
#define CAVE_Y_THRESH 0x03

/* Launch Alley: id $08, bounds 8x8, direct */
static const ObjectEntry launch_entries[] = {
    {0x08, 0xA8, 0x98},
};
#define LAUNCH_X_THRESH 0x08
#define LAUNCH_Y_THRESH 0x08

/*=============================================================================
 * Collision attribute lists (from red_stage_game_object_collision.asm)
 * Each starts with $00 (flat list), attributes..., $FF terminator.
 * We skip the $00 prefix and just store the attribute values.
 *===========================================================================*/
static const uint8_t voltorb_attrs[] = {
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,
    0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5, 0xFF
};
static const uint8_t bumper_attrs[] = {
    0x32,0x3F,0x37,0x3C,0x34,0x31,0x3E,0x36,0x3B,0x3D, 0xFF
};
static const uint8_t diglett_attrs[] = {
    0x64,0x65,0x68,0x69, 0xFF
};
static const uint8_t wild_mon_attrs[] = {
    0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,
    0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, 0xFF
};
static const uint8_t top_staryu_attrs[] = {
    0x10,0x11,0x12, 0xFF
};
static const uint8_t bottom_staryu_attrs[] = {
    0x56,0x5B,0x5C, 0xFF
};
static const uint8_t bonus_mult_attrs[] = {
    0x4C,0x4B,0x48,0x47,0x4D,0x4A, 0xFF
};

/* Bumper collision angle deltas (from BumperCollisionAngleDeltas_RedField) */
static const int8_t bumper_angle_deltas[] = { -8, 8 };

/* Forward declarations for graphics loading (defined after resolve handlers) */
static void play_low_time_sfx(GameState *state);
void load_staryu_graphics_bottom(GameState *state);
void load_upgrade_triggers_graphics(GameState *state);
void load_pokeball_indicator_graphics(GameState *state);
void update_field_structures(GameState *state);
bool check_special_mode_collision(GameState *state, uint8_t collision_id);
void load_staryu_graphics_top(GameState *state);
void update_spinner_charge_graphics(GameState *state);
static void load_catch_progress_gfx(GameState *state, uint8_t hit_count);
/* These are now non-static (shared with blue field, declared in red_field.h) */
static void do_slot_logic_red_field(GameState *state);
static void load_billboard_graphics_red_field(GameState *state);
void load_billboard_status_bar_graphics(GameState *state);
void load_evolution_trinket_graphics(GameState *state);
void func_1414b_special_mode_gfx(GameState *state);
void start_catchem_mode(GameState *state);
void start_evolution_mode(GameState *state);
void start_map_move_mode(GameState *state);
static void handle_red_catchem_collision(GameState *state);
static void handle_red_evo_mode_collision(GameState *state);
static void handle_map_mode_collision(GameState *state);
uint8_t increment_max100(uint8_t *val);
void add_extra_ball(GameState *state);
static void clear_all_red_indicators(GameState *state);
static void load_mon_billboard_picture(GameState *state);
static void load_mon_billboard_palettes(GameState *state);
static void refresh_billboard_illumination(GameState *state);
static void load_animated_mon_tiles_and_palettes(GameState *state);

/*=============================================================================
 * IsCollisionInList (from object_collision.asm 0x27da)
 * Checks: is_ball_colliding != 0 AND cur_collision_attribute in list.
 *===========================================================================*/
static bool is_collision_in_list(GameState *state, const uint8_t *attr_list) {
    if (!state->is_ball_colliding)
        return false;
    uint8_t attr = state->cur_collision_attribute;
    for (int i = 0; attr_list[i] != 0xFF; i++) {
        if (attr_list[i] == attr)
            return true;
    }
    return false;
}

/*=============================================================================
 * CheckGameObjectCollision (from object_collision.asm 0x27a4)
 * Bounding box check: abs(ball - obj) < threshold for each entry.
 * Returns true on first hit, sets triggered_game_object and index.
 *===========================================================================*/
static bool check_bounding_box(
    GameState *state,
    const ObjectEntry *entries, uint8_t count,
    uint8_t x_thresh, uint8_t y_thresh)
{
    uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
    uint8_t ball_y = UFIXED_TO_INT(state->ball_y_pos);

    for (uint8_t i = 0; i < count; i++) {
        int dx = (int)ball_x - (int)entries[i].x;
        int dy = (int)ball_y - (int)entries[i].y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < x_thresh && dy < y_thresh) {
            state->triggered_game_object = entries[i].id;
            state->triggered_game_object_index = i + 1; /* 1-based like ASM */
            return true;
        }
    }
    return false;
}

/*=============================================================================
 * HandleGameObjectCollision (from object_collision.asm 0x2775)
 *
 * Core dispatch for one object group:
 *   1. Skip if already triggered this frame (triggered != $FF)
 *   2. Skip if which_flag has bit 7 set (handler busy)
 *   3. If attribute-gated: check IsCollisionInList first
 *   4. Check bounding box
 *   5. Skip if same object as previous frame
 *   6. Set which_flag = index, which_flag_id = id
 *
 * attr_list: collision attributes (NULL for direct/scf objects)
 * which_flag: pointer to per-object flag (e.g. &state->which_voltorb)
 * which_flag_id: pointer to per-object id (e.g. &state->which_voltorb_id)
 *                May be NULL for single-entry objects that don't need id.
 *===========================================================================*/
static bool handle_game_object_collision(
    GameState *state,
    const uint8_t *attr_list,
    const ObjectEntry *entries, uint8_t count,
    uint8_t x_thresh, uint8_t y_thresh,
    uint8_t *which_flag, uint8_t *which_flag_id)
{
    /* Already triggered this frame? */
    if (state->triggered_game_object != 0xFF)
        return false;

    /* Handler busy? (bit 7 of flag, from ASM bit 7 check at 0x277d) */
    if (*which_flag & 0x80)
        return false;

    /* Clear flag (ASM: ld [hl], $0 at 0x278f) */
    *which_flag = 0;

    /* Attribute-gated check (carry clear = check attributes) */
    if (attr_list) {
        if (!is_collision_in_list(state, attr_list))
            return false;
    }

    /* Bounding box check */
    if (!check_bounding_box(state, entries, count, x_thresh, y_thresh))
        return false;

    /* Previous trigger check (prevent re-trigger same object, ASM 0x2792) */
    if (state->previous_triggered_game_object == state->triggered_game_object)
        return false;

    /* Set per-object flags */
    *which_flag = state->triggered_game_object_index;
    if (which_flag_id)
        *which_flag_id = state->triggered_game_object;

    return true;
}

/*=============================================================================
 * Init Red Field (from init_red_field.asm 0x30000)
 *===========================================================================*/
void init_red_field(GameState *state) {
    /* InitRedField (0x30000) - matches ASM exactly */

    /* ASM line 2-4: ret nz if loading saved game */
    if (state->loading_saved_game) return;

    memset(state->score, 0, sizeof(state->score));
    state->num_party_mons = 0;
    state->extra_balls = 0;
    state->lost_ball = 0;
    state->ball_type = POKE_BALL;
    state->ball_size = 0;
    state->previous_num_pokeballs = 0;
    state->num_pokeballs = 0;
    state->pokeball_blinking_counter = 0;
    state->disable_horizontal_scroll_for_ball_start = 0;
    state->flippers_disabled = 0;
    state->current_map = MAP_PALLET_TOWN;
    state->cur_ball_life = 1;       /* ASM: ld a, 1 / ld [wCurBallLife], a */
    state->cur_bonus_multiplier = 1;
    state->right_alley_count = 2;   /* ASM: ld a, 2 */
    state->num_ball_lives = 3;      /* ASM: ld a, 3 */
    state->next_bonus_stage = 3;    /* BONUS_STAGE_ORDER_DIGLETT */
    state->initial_next_bonus_stage = 3;  /* ASM line 34 */
    state->wd610 = 3;                     /* ASM line 32 */

    state->stage_collision_state = 4;
    state->red_stage_structure_backup = 4;
    /* ASM does NOT set wd7f2 in InitRedField; LoadStageData clears it to 0 */
    state->previous_field_structure_state = 0;

    /* Indicator states: ASM init_red_field.asm lines 38-42 */
    memset(state->indicator_states, 0, sizeof(state->indicator_states));
    state->indicator_states[0] = 0x80;
    state->indicator_states[1] = 0x82;
    state->indicator_states[3] = 0x80; /* ASM: ld [wIndicatorStates + 3], a (where a=$80) */

    /* Ball saver: ASM DOES call Start20SecondSaverTimer at init (line 43) */
    start_20_second_saver_timer(state);

    /* GetBCDForNextBonusMultiplier_RedField (ASM line 44):
     * Computes wBonusMultiplierTensDigit/OnesDigit from cur_bonus_multiplier.
     * For initial multiplier of 1: tens=0, ones=1. */
    state->bonus_multiplier_tens_digit = 0;
    state->bonus_multiplier_ones_digit = state->cur_bonus_multiplier;

    state->current_stage = STAGE_RED_FIELD_BOTTOM;

    /* ASM: init_red_field.asm lines 45-48
     * ld a, Bank(Music_RedField)  ; bank $0F
     * call SetSongBank
     * ld de, MUSIC_RED_FIELD      ; id $01
     * call PlaySong */
    /* Initialize bellsprout animation to idle breathing loop.
     * Without this, Lua resolve_bellsprout() advances from (0,0,0) to index 1
     * on the first frame, which triggers pinball_is_visible = 0 (ball hidden). */
    state->bellsprout_anim.frame_counter = 0x19;
    state->bellsprout_anim.frame = 0;
    state->bellsprout_anim.index = 6;

    PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
}

/*=============================================================================
 * ConcludeSpecialMode_RedField (0xddfd)
 * Ends any active special mode and restores collision state from backup.
 *===========================================================================*/
void conclude_special_mode_red_field(GameState *state) {
    if (!state->in_special_mode)
        return;

    /* ASM: dispatch by wSpecialMode (0xddfd):
     * 0 = CatchEm, 1 = Evolution, 2 = MapMove */
    if (state->special_mode == 0) {
        /* ConcludeCatchEmMode (0x10157):
         * Clear all catch'em state, collision mask, and stop timer. */
        state->in_special_mode = 0;
        state->wild_mon_is_hittable = 0;
        state->wd5c6 = 0;
        state->number_of_catch_mode_tiles_flipped = 0;
        state->num_mon_hits = 0;
        memset(state->mon_animated_collision_mask, 0, 0x80);
        free_cached_billboard_data();
        /* RestoreBallSaverAfterCatchEmMode (ASM 0x10196) */
        state->ball_saver_timer_frames = state->ball_saver_timer_frames_backup;
        state->ball_saver_timer_seconds = state->ball_saver_timer_seconds_backup;
        state->num_times_ball_saved_text_will_display = state->num_times_ball_saved_text_will_display_backup;
        {
            uint8_t sec = state->ball_saver_timer_seconds;
            /* D-03: ASM uses cp $2 / cp $6 / cp $b on raw seconds value */
            if (sec < 2) state->ball_saver_flash_rate = 0x00;
            else if (sec < 6) state->ball_saver_flash_rate = 0x04;
            else if (sec < 11) state->ball_saver_flash_rate = 0x10;
            else state->ball_saver_flash_rate = 0xFF;
        }
        stop_timer(state);
        /* Func_108f5 (0x108f5): stage-specific tile cleanup after catch'em mode.
         * ResetIndicatorStates + OpenSlotCave + SetLeftAndRightAlleyArrowIndicatorStates
         * + Func_107e9, then bottom stage: ClearAllRedIndicators + billboard/slot gfx */
        {
            /* ResetIndicatorStates (0x107a5): zero all 19 indicator states */
            memset(state->indicator_states, 0, 19);
            /* OpenSlotCave (0x107c2): set reopen delay */
            state->frames_until_slot_cave_opens = 0x1E;
            /* SetLeftAndRightAlleyArrowIndicatorStates_RedField (0x107c8) */
            {
                uint8_t right = state->right_alley_count;
                if (right < 3)
                    state->indicator_states[1] = right | 0x80;
                else
                    state->indicator_states[1] = right;
                if (right >= 2)
                    state->indicator_states[3] = 0x80;
                state->indicator_states[0] = state->left_alley_count | 0x80;
            }
            /* Func_107e9: restore structure backup based on left alley */
            state->red_stage_structure_backup =
                (state->left_alley_count == 3) ? 6 : 4;
            /* Bottom stage: reload tile data (ASM Func_108f5 lines 1280-1301) */
            if (state->current_stage & 1) {
                clear_all_red_indicators(state);
                load_billboard_tilemap(state);
                load_map_billboard_tile_data(state);

                /* StageSharedBonusSlotGlowGfx → vTilesOB tile $1A, $160 bytes.
                 * ASM (catchem_mode.asm:1283-1287): full reload of glow tiles.
                 * Catch mode's animated mon overwrites $81A0-$82FF. */
                {
                    char glow_path[260];
                    snprintf(glow_path, sizeof(glow_path), "%s/gfx/stage/shared/bonus_slot_glow.png",
                             state->asset_base_path);
                    size_t glow_size = 0;
                    uint8_t *glow_data = tiles_from_png(glow_path, &glow_size);
                    if (glow_data && glow_size >= 0x0160) {
                        vram_write(state->vram, 0, 0x81A0, glow_data, 0x0160);
                    }
                    free(glow_data);
                }

                /* BonusSlotGlow2Gfx → vTilesOB tile $38 ($8380), $20 bytes.
                 * LoadShakeBallGfx during capture overwrites $8380-$83BF with
                 * ball shake tiles. Restore the slot glow frame 2 tile data. */
                {
                    char path[260];
                    snprintf(path, sizeof(path), "%s/gfx/stage/shared/bonus_slot_glow_2.png",
                             state->asset_base_path);
                    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
                    size_t data_size = 0;
                    uint8_t *tile_data = tiles_from_png(path, &data_size);
                    if (tile_data) {
                        uint16_t copy = (data_size < 0x20) ? (uint16_t)data_size : 0x20;
                        vram_write(state->vram, 0, 0x8380, tile_data, copy);
                        free(tile_data);
                    }
                }

                /* BlankSaverSpaceTileData: restore tiles at $8AE0, $8B00, $8B20
                 * from bottom field base GBC gfx (offsets $2E0, $300, $320).
                 * These VRAM addresses held the "CATCH!" text during catch mode. */
                {
                    const char *sub = (state->current_stage >= STAGE_BLUE_FIELD_TOP)
                        ? "blue_bottom/blue_bottom_base_gameboycolor"
                        : "red_bottom/red_bottom_base_gameboycolor";
                    char path[260];
                    snprintf(path, sizeof(path), "%s/gfx/stage/%s.png",
                             state->asset_base_path, sub);
                    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
                    size_t data_size = 0;
                    uint8_t *tile_data = tiles_from_png(path, &data_size);
                    if (tile_data) {
                        /* 3 entries: offset $2E0→$8AE0, $300→$8B00, $320→$8B20, each $20 bytes */
                        if (data_size >= 0x300) vram_write(state->vram, 0, 0x8AE0, tile_data + 0x2E0, 0x20);
                        if (data_size >= 0x320) vram_write(state->vram, 0, 0x8B00, tile_data + 0x300, 0x20);
                        if (data_size >= 0x340) vram_write(state->vram, 0, 0x8B20, tile_data + 0x320, 0x20);
                        free(tile_data);
                    }
                }

                /* CaughtPokeballTileData: load caught pokeball gfx at $8AE0 ($20 bytes) */
                {
                    char path[260];
                    snprintf(path, sizeof(path), "%s/gfx/stage/caught_pokeball.png",
                             state->asset_base_path);
                    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
                    size_t data_size = 0;
                    uint8_t *tile_data = tiles_from_png(path, &data_size);
                    if (tile_data) {
                        uint16_t copy = (data_size < 0x20) ? (uint16_t)data_size : 0x20;
                        vram_write(state->vram, 0, 0x8AE0, tile_data, copy);
                        free(tile_data);
                    }
                }

                /* Restore pokeball indicator tilemap entries */
                load_pokeball_indicator_graphics(state);
            }
        }
    } else if (state->special_mode == 1) {
        /* ConcludeEvolutionMode: slot cleanup + full state clear */
        state->slot_is_open = 0;
        state->frames_until_slot_cave_opens = 0x1E;
        state->in_special_mode = 0;
        state->bottom_text_enabled = 0;
        fill_bottom_message_buffer_with_black_tile(state);
        state->wild_mon_is_hittable = 0;
        state->number_of_catch_mode_tiles_flipped = 0;
        state->num_mon_hits = 0;
        state->evolution_objects_disabled = 0;
        state->num_evolution_trinkets = 0;
        memset(state->mon_animated_collision_mask, 0, 0x80);
        /* RestoreBallSaverAfterCatchEmMode (ASM 0x10196) */
        state->ball_saver_timer_frames = state->ball_saver_timer_frames_backup;
        state->ball_saver_timer_seconds = state->ball_saver_timer_seconds_backup;
        state->num_times_ball_saved_text_will_display = state->num_times_ball_saved_text_will_display_backup;
        {
            uint8_t sec = state->ball_saver_timer_seconds;
            /* D-03: ASM uses cp $2 / cp $6 / cp $b on raw seconds value */
            if (sec < 2) state->ball_saver_flash_rate = 0x00;
            else if (sec < 6) state->ball_saver_flash_rate = 0x04;
            else if (sec < 11) state->ball_saver_flash_rate = 0x10;
            else state->ball_saver_flash_rate = 0xFF;
        }
        stop_timer(state);
        /* ConcludeEvolutionMode_RedField (0x10fe3): stage-specific cleanup */
        {
            /* ResetIndicatorStates (0x107a5) */
            memset(state->indicator_states, 0, 19);
            /* OpenSlotCave (0x107c2) */
            state->frames_until_slot_cave_opens = 0x1E;
            /* SetLeftAndRightAlleyArrowIndicatorStates_RedField (0x107c8) */
            {
                uint8_t right = state->right_alley_count;
                if (right < 3)
                    state->indicator_states[1] = right | 0x80;
                else
                    state->indicator_states[1] = right;
                if (right >= 2)
                    state->indicator_states[3] = 0x80;
                state->indicator_states[0] = state->left_alley_count | 0x80;
            }
            /* Func_107e9: restore structure backup */
            state->red_stage_structure_backup =
                (state->left_alley_count == 3) ? 6 : 4;
            if (state->current_stage & 1) {
                /* Bottom stage: ClearAllRedIndicators + reload tile data */
                clear_all_red_indicators(state);
                load_slot_cave_cover_graphics(state);
                load_map_billboard_tile_data(state);
                /* StageSharedBonusSlotGlowGfx+$60 → vTilesOB tile $20, $E0 bytes.
                 * Catch mode's billboard/pokemon sprites overwrite slot glow
                 * tiles at $8200+. Reload them from the PNG. */
                if (state->vram) {
                    char path[260];
                    snprintf(path, sizeof(path), "%s/gfx/stage/shared/bonus_slot_glow.png",
                             state->asset_base_path);
                    size_t data_size = 0;
                    uint8_t *tile_data = tiles_from_png(path, &data_size);
                    if (tile_data && data_size >= 0x60 + 0xE0) {
                        vram_write(state->vram, 0, 0x8200, tile_data + 0x60, 0xE0);
                    }
                    free(tile_data);
                }
            } else {
                /* Top stage: LoadRedFieldTopGraphics (0x10aff)
                 * Reload StageRedFieldTopGfx3 → $8900, $E0 bytes */
            }
        }
    } else if (state->special_mode == 2) {
        /* ConcludeMapMoveMode: slot cleanup + full state clear */
        state->slot_is_open = 0;
        state->frames_until_slot_cave_opens = 0x1E;
        state->in_special_mode = 0;
        state->bottom_text_enabled = 0;
        fill_bottom_message_buffer_with_black_tile(state);
        state->special_mode = 0;
        stop_timer(state);
        /* Func_31234: stage-specific cleanup after map move mode */
        {
            /* ResetIndicatorStates (0x107a5) */
            memset(state->indicator_states, 0, 19);
            /* OpenSlotCave (0x107c2) */
            state->frames_until_slot_cave_opens = 0x1E;
            /* SetLeftAndRightAlleyArrowIndicatorStates_RedField (0x107c8) */
            {
                uint8_t right = state->right_alley_count;
                if (right < 3)
                    state->indicator_states[1] = right | 0x80;
                else
                    state->indicator_states[1] = right;
                if (right >= 2)
                    state->indicator_states[3] = 0x80;
                state->indicator_states[0] = state->left_alley_count | 0x80;
            }
            /* Func_107e9: restore structure backup */
            state->red_stage_structure_backup =
                (state->left_alley_count == 3) ? 6 : 4;
            if (state->current_stage & 1) {
                /* Bottom stage: ClearAllRedIndicators + reload tile data */
                clear_all_red_indicators(state);
                load_slot_cave_cover_graphics(state);
                load_map_billboard_tile_data(state);
            }
        }
    }

    state->special_mode_state = 0;

    /* Restore collision state from backup (ASM 0xde30-0xde4d):
     * wStageCollisionState = (wStageCollisionState & 0x01) | wRedStageStructureBackup */
    state->stage_collision_state =
        (state->stage_collision_state & 0x01) | state->red_stage_structure_backup;
}

/*=============================================================================
 * Start20SecondSaverTimer (0xdbba)
 * Called after ball loss to set up saver for next ball.
 *===========================================================================*/
void start_20_second_saver_timer(GameState *state) {
    state->ball_saver_icon_on = 1;
    state->ball_saver_flash_rate = 0xFF;
    state->ball_saver_timer_frames = 59;
    state->ball_saver_timer_seconds = 20;
    state->num_times_ball_saved_text_will_display = 2;
}

/*=============================================================================
 * Evolution Trinket Collision Data (PinballCollidesWithPoints)
 * From red_stage_game_object_collision.asm (0x156d6 / 0x145fb)
 * Format: {flag, x, y} triplets, terminated by flag=0
 *===========================================================================*/
static const uint8_t red_top_evolution_trinket_coords[] = {
    0x01, 0x44, 0x14,
    0x01, 0x2A, 0x1A,
    0x01, 0x5E, 0x1A,
    0x01, 0x11, 0x2D,
    0x01, 0x77, 0x2D,
    0x01, 0x16, 0x3E,
    0x01, 0x77, 0x3E,
    0x01, 0x06, 0x6D,
    0x01, 0x83, 0x6D,
    0x01, 0x41, 0x82,
    0x01, 0x51, 0x82,
    0x01, 0x69, 0x82,
    0x00  /* terminator */
};

static const uint8_t red_bottom_evolution_trinket_coords[] = {
    0x01, 0x35, 0x1B,
    0x01, 0x53, 0x1B,
    0x01, 0x29, 0x1F,
    0x01, 0x5F, 0x1F,
    0x01, 0x26, 0x34,
    0x01, 0x62, 0x34,
    0x00  /* terminator */
};

/* Pointers indexed by stage (0=top, 1=bottom) */
static const uint8_t *red_evolution_trinket_coord_ptrs[2] = {
    red_top_evolution_trinket_coords,
    red_bottom_evolution_trinket_coords
};

/* CheckRedStageEvolutionTrinketCollision (0x1441e)
 * PinballCollidesWithPoints (0x27fd): check ball against point array.
 * Collision if (point_x - ball_x) >= 0xE8 AND (point_y - ball_y) >= 0xE8. */
static void check_evolution_trinket_collision_red(GameState *state) {
    state->collided_point_index = 0;
    if (!state->evolution_objects_disabled) return;  /* ASM: ret z */

    const uint8_t *coords = red_evolution_trinket_coord_ptrs[state->current_stage & 1];
    uint8_t ball_x = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t ball_y = (uint8_t)(state->ball_y_pos >> 8);
    uint8_t index = 0;

    while (*coords) {
        coords++;  /* skip flag byte */
        index++;
        uint8_t px = *coords++;
        uint8_t py = *coords++;
        uint8_t dx = (uint8_t)(px - ball_x);
        uint8_t dy = (uint8_t)(py - ball_y);
        if (dx >= 0xE8 && dy >= 0xE8) {
            state->collided_point_index = index;
            return;
        }
    }
}

/*=============================================================================
 * Config-Aware Object Group Lookup
 * Finds a named collision group in a TableConfig. Returns NULL if not found.
 *===========================================================================*/
static const TableObjectGroup *find_config_group(const TableConfig *table, const char *name) {
    for (uint8_t i = 0; i < table->num_groups; i++) {
        if (strcmp(table->groups[i].name, name) == 0)
            return &table->groups[i];
    }
    return NULL;
}

/*
 * Config-aware collision check wrapper.
 * If a config group matches by name, use its positions/thresholds/attrs.
 * Otherwise fall back to the hardcoded defaults.
 * Note: TableObject and ObjectEntry have identical layout {id, x, y}.
 */
static bool check_object_group(GameState *state,
    const TableConfig *table, const char *group_name,
    const uint8_t *default_attrs,
    const ObjectEntry *default_entries, uint8_t default_count,
    uint8_t default_x_thresh, uint8_t default_y_thresh,
    uint8_t *which_flag, uint8_t *which_flag_id)
{
    const TableObjectGroup *grp = find_config_group(table, group_name);
    if (grp && grp->num_objects > 0) {
        return handle_game_object_collision(state,
            grp->attribute_gated ? grp->collision_attrs : NULL,
            (const ObjectEntry *)grp->objects,
            grp->num_objects, grp->x_thresh, grp->y_thresh,
            which_flag, which_flag_id);
    }
    return handle_game_object_collision(state, default_attrs,
        default_entries, default_count, default_x_thresh, default_y_thresh,
        which_flag, which_flag_id);
}

/*=============================================================================
 * Check Red Field Object Collisions
 * Matches ASM dispatch order exactly:
 *   Top:    Voltorb(attr) → Spinner(direct) → BoardTriggers(direct) →
 *           TopStaryu(attr) → Bellsprout(direct) → DittoSlot(direct) →
 *           UpgradeTriggers(direct)
 *   Bottom: split by ball_y < $56:
 *     Upper: WildMon(attr) → BottomStaryu(attr) → Diglett(attr) →
 *            BonusMult(attr) → Slot(direct)
 *     Lower: Bumpers(attr) → Pikachu(direct) → CAVE(direct) →
 *            LaunchAlley(direct)
 * From red_stage_object_collision.asm (0x143e1 / 0x143f9)
 *===========================================================================*/
void check_red_field_object_collisions(GameState *state) {
    /* ASM: CheckGameObjectCollisions (0x2720) */
    state->triggered_game_object = 0xFF;

    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        /* CheckRedStageTopGameObjectCollisions (0x143e1) */
        const TableConfig *t = &state->config->red_field_top;
        check_object_group(state, t, "voltorb", voltorb_attrs,
            voltorb_entries, 3, VOLTORB_X_THRESH, VOLTORB_Y_THRESH,
            &state->which_voltorb, &state->which_voltorb_id);
        check_object_group(state, t, "spinner", NULL,
            spinner_entries, 1, SPINNER_X_THRESH, SPINNER_Y_THRESH,
            &state->spinner_collision, NULL);
        check_object_group(state, t, "board_triggers", NULL,
            board_trigger_entries, 8, BOARD_TRIG_X_THRESH, BOARD_TRIG_Y_THRESH,
            &state->which_board_trigger, &state->which_board_trigger_id);
        check_object_group(state, t, "top_staryu", top_staryu_attrs,
            top_staryu_entries, 1, STARYU_X_THRESH, STARYU_Y_THRESH,
            &state->staryu_collision, NULL);
        check_object_group(state, t, "bellsprout", NULL,
            bellsprout_entries, 1, BELLSPROUT_X_THRESH, BELLSPROUT_Y_THRESH,
            &state->bellsprout_collision, NULL);
        check_object_group(state, t, "ditto_slot", NULL,
            ditto_slot_entries, 1, DITTO_X_THRESH, DITTO_Y_THRESH,
            &state->ditto_slot_collision, NULL);
        check_object_group(state, t, "upgrade_triggers", NULL,
            upgrade_entries, 3, UPGRADE_X_THRESH, UPGRADE_Y_THRESH,
            &state->which_pinball_upgrade_trigger, &state->which_pinball_upgrade_trigger_id);
        check_evolution_trinket_collision_red(state);

    } else if (state->current_stage == STAGE_RED_FIELD_BOTTOM) {
        /* CheckRedStageBottomGameObjectCollisions (0x143f9) */
        const TableConfig *t = &state->config->red_field_bottom;
        uint8_t ball_y = UFIXED_TO_INT(state->ball_y_pos);

        if (ball_y < 0x56) {
            /* Upper half of bottom stage */
            check_object_group(state, t, "wild_mon", wild_mon_attrs,
                wild_mon_entries, 1, WILD_MON_X_THRESH, WILD_MON_Y_THRESH,
                &state->wild_mon_collision, NULL);
            check_object_group(state, t, "bottom_staryu", bottom_staryu_attrs,
                bottom_staryu_entries, 1, STARYU_X_THRESH, STARYU_Y_THRESH,
                &state->staryu_collision, NULL);
            check_object_group(state, t, "diglett", diglett_attrs,
                diglett_entries, 2, DIGLETT_X_THRESH, DIGLETT_Y_THRESH,
                &state->which_diglett, &state->which_diglett_id);
            check_object_group(state, t, "bonus_multipliers", bonus_mult_attrs,
                bonus_mult_entries, 2, BONUS_MULT_X_THRESH, BONUS_MULT_Y_THRESH,
                &state->which_bonus_multiplier_railing, &state->which_bonus_multiplier_railing_id);
            check_object_group(state, t, "slot", NULL,
                slot_entries, 1, SLOT_X_THRESH, SLOT_Y_THRESH,
                &state->slot_collision, NULL);
            check_evolution_trinket_collision_red(state);
        } else {
            /* Lower half of bottom stage (ball_y >= $56) */
            check_object_group(state, t, "bumpers", bumper_attrs,
                bumper_entries, 2, BUMPER_X_THRESH, BUMPER_Y_THRESH,
                &state->which_bumper, &state->which_bumper_id);
            check_object_group(state, t, "pikachu", NULL,
                pikachu_entries, 2, PIKACHU_X_THRESH, PIKACHU_Y_THRESH,
                &state->which_pikachu, &state->which_pikachu_id);
            check_object_group(state, t, "cave_lights", NULL,
                cave_light_entries, 4, CAVE_X_THRESH, CAVE_Y_THRESH,
                &state->which_cave_light, &state->which_cave_light_id);
            check_object_group(state, t, "launch_alley", NULL,
                launch_entries, 1, LAUNCH_X_THRESH, LAUNCH_Y_THRESH,
                &state->pinball_launch_collision, NULL);
        }
    }

    /* Save for next frame's previous-trigger check (ASM 0x2726) */
    state->previous_triggered_game_object = state->triggered_game_object;
}

/*=============================================================================
 * Resolve Handlers - ALL run every frame (from red_stage_resolve_collision.asm)
 * Each handler checks its own which_xxx flag and acts only if set.
 *===========================================================================*/

/* ApplyVoltorbCollision (0x14dc9) + ResolveVoltorbCollision (0x14d85) */
static void resolve_voltorb(GameState *state) {
    if (state->which_voltorb) {
        uint8_t id = state->which_voltorb_id;
        state->which_voltorb = 0;

        /* Flipper force system - NOT atan2 angles */
        state->flipper_y_force = 0x0200;
        state->flipper_collision = 0x80;
        state->rumble_pattern = 0xFF;
        state->rumble_duration = 3;

        /* Animation: 16-frame hit effect */
        state->voltorb_hit_anim_duration = 0x10;
        state->which_animated_voltorb = id - 3;

        /* Score: 500 points */
        add_score_with_multiplier(state, state->config->scores.score_500);
        /* ASM: lb de, $00, $0e / call PlaySoundEffect */
        PLAY_SFX(state, "spinner", 0x00, 0x0E);
        check_special_mode_collision(state, 4); /* SPECIAL_COLLISION_VOLTORB */
        return;
    }

    /* Per-frame animation update (runs even without new collision) */
    if (state->voltorb_hit_anim_duration) {
        state->voltorb_hit_anim_duration--;
        if (state->voltorb_hit_anim_duration == 0)
            state->which_animated_voltorb = 0xFF;
    }
}

/* ResolveRedStageSpinnerCollision (0x14dea) + UpdateRedStageSpinner (0x14e10) */
static void resolve_spinner(GameState *state) {
    if (state->spinner_collision) {
        state->spinner_collision = 0;
        /* Transfer ball Y velocity to spinner (NOT bounce) */
        state->spinner_velocity = state->ball_y_velocity;
        check_special_mode_collision(state, 12); /* SPECIAL_COLLISION_SPINNER */
    }

    /* Per-frame spinner update: apply friction, update rotation */
    if (state->spinner_velocity == 0)
        return;

    /* Friction: subtract/add 7 toward zero */
    int16_t vel = state->spinner_velocity;
    if (vel > 0) {
        vel -= 7;
        if (vel < 0) vel = 0;
    } else {
        vel += 7;
        if (vel > 0) vel = 0;
    }
    state->spinner_velocity = vel;

    /* Add velocity to spinner state (16-bit accumulator) */
    int32_t new_state = (int32_t)((int16_t)((state->spinner_state[1] << 8) | state->spinner_state[0])) + vel;
    state->spinner_state[0] = (uint8_t)(new_state & 0xFF);
    uint8_t hi = (uint8_t)((new_state >> 8) & 0xFF);

    /* Check for full rotation (0x18 = 24 units per rotation) */
    bool rotated = false;
    if ((int8_t)hi < 0) {
        hi += 0x18;
        rotated = true;
    } else if (hi >= 0x18) {
        hi -= 0x18;
        rotated = true;
    }
    state->spinner_state[1] = hi;

    if (rotated) {
        /* 10 points per rotation */
        add_score_with_multiplier(state, state->config->scores.score_10);
        if (state->num_spinner_turns < 100) state->num_spinner_turns++;

        /* Charge Pikachu saver (ASM 0x14e7e..0x14e9d) */
        if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
            /* Already at max: just play sound */
        } else {
            state->pikachu_saver_charge++;
            /* Set sound cooldown when reaching max (ASM: ld a, $64 / ld [wd51e], a) */
            if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
                state->pikachu_saver_sound_cooldown = 0x64;
            }
        }

        /* PlaySpinnerChargingSoundEffect_RedField (0x14ea7):
         * Only plays if wd51e == 0, uses charge as index into SFX table */
        if (state->pikachu_saver_sound_cooldown == 0) {
            static const uint8_t spinner_sfx_ids[16] = {
                0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
                0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x11
            };
            uint8_t idx = state->pikachu_saver_charge;
            if (idx > 15) idx = 15;
            audio_play_sfx(state->audio, 0x00, spinner_sfx_ids[idx]);
        }

        /* ASM: only update spinner charge graphics on top stage (bit 0 == 0) */
        if (!(state->current_stage & 1)) {
            update_spinner_charge_graphics(state);
        }
    }
}

/* ResolveBallUpgradeTriggersCollision_RedField (0x1535d) */
/* FieldMultiplierText header: scrolling_text_normal 0, 20, 0, 20
 * → scrolling_text 5, 20, 0, 20, 0, 20 */
static const uint8_t FIELD_MULT_HEADER[6] = { 5, 0x54, 0x40, 20, 0x00, 60 };
/* FieldMultiplierSpecialBonusText header: scrolling_text_nopause 7, 51 */
static const uint8_t FIELD_MULT_SPECIAL_HEADER[6] = { 7, 0x54, 0, 0, 0, 51 };
/* DigitsText1to8 header: scrolling_text 7, 51, 6, 20, 2, 15
 * → { 7, 51+0x40, 6+0x40, 20, 2*0x10, 15+20+(51-6) } = { 7, 0x73, 0x46, 20, 0x20, 80 } */
static const uint8_t DIGITS_1_8_HEADER[6] = { 7, 0x73, 0x46, 20, 0x20, 80 };

/* BonusMultiplierText header: scrolling_text_normal 0, 20, 0, 21
 * → scrolling_text 5, 20, 0, 20, 0, 21 → { 5, 0x54, 0x40, 20, 0x00, 61 } */
static const uint8_t BONUS_MULT_TEXT_HEADER[6] = { 5, 0x54, 0x40, 20, 0x00, 61 };

/* StartFromMapText header: scrolling_text_nopause 5, 31 */
static const uint8_t START_FROM_TEXT_HEADER[6] = { 5, 0x54, 0, 0, 0, 31 };
/* ArrivedAtMapText header: scrolling_text_nopause 5, 31 */
static const uint8_t ARRIVED_AT_TEXT_HEADER[6] = { 5, 0x54, 0, 0, 0, 31 };

/* ShowCapturedPokemonText headers (0x106b6) */
/* "YOU GOT A " — scrolling_text_nopause 5, 30 */
static const uint8_t YOU_GOT_A_HEADER[6] = { 5, 0x54, 0, 0, 0, 30 };
/* "YOU GOT AN " — scrolling_text_nopause 5, 31 */
static const uint8_t YOU_GOT_AN_HEADER[6] = { 5, 0x54, 0, 0, 0, 31 };
/* Mon name (consonant prefix, offset 30): scrolling_text 5, 30, 0, 20, 2, 17
 * → { 5, 0x5E, 0x40, 20, 0x20, 67 }  (base before per-name adjustment) */
static const uint8_t MON_NAME_CONSONANT_HEADER[6] = { 5, 0x5E, 0x40, 20, 0x20, 67 };
/* Mon name (vowel prefix, offset 31): scrolling_text 5, 31, 0, 20, 2, 17
 * → { 5, 0x5F, 0x40, 20, 0x20, 68 }  (base before per-name adjustment) */
static const uint8_t MON_NAME_VOWEL_HEADER[6] = { 5, 0x5F, 0x40, 20, 0x20, 68 };

/* ShowJackpotText headers (0x10825) */
/* "JACKPOT" label: stationary_text 2, 0, 180 */
static const uint8_t JACKPOT_LABEL_HEADER[4] = { 0x42, 0x00, 0xB4, 0x00 };
/* Jackpot score digits: stationary_text 10, 1, 180 */
static const uint8_t JACKPOT_SCORE_HEADER[4] = { 0x4A, 0x10, 0xB4, 0x00 };

/* Map name scrolling text headers (scrolling_text 5, 31, \3, 20, 2, \6)
 * → { 5, 0x5F, \3+0x40, 20, 0x20, \6+20+(31-\3) }
 * Indexed by MAP_* constants (0-17). */
static const uint8_t MAP_NAME_HEADERS[18][6] = {
    { 5, 0x5F, 0x44, 20, 0x20, 63 }, /* PALLET_TOWN */
    { 5, 0x5F, 0x43, 20, 0x20, 65 }, /* VIRIDIAN_CITY */
    { 5, 0x5F, 0x42, 20, 0x20, 67 }, /* VIRIDIAN_FOREST */
    { 5, 0x5F, 0x44, 20, 0x20, 63 }, /* PEWTER_CITY */
    { 5, 0x5F, 0x46, 20, 0x20, 59 }, /* MT_MOON */
    { 5, 0x5F, 0x43, 20, 0x20, 65 }, /* CERULEAN_CITY */
    { 5, 0x5F, 0x40, 20, 0x20, 71 }, /* VERMILION_SEASIDE */
    { 5, 0x5F, 0x40, 20, 0x20, 71 }, /* VERMILION_STREETS */
    { 5, 0x5F, 0x43, 20, 0x20, 65 }, /* ROCK_MOUNTAIN */
    { 5, 0x5F, 0x43, 20, 0x20, 65 }, /* LAVENDER_TOWN */
    { 5, 0x5F, 0x44, 20, 0x20, 64 }, /* CELADON_CITY */
    { 5, 0x5F, 0x44, 20, 0x20, 64 }, /* CYCLING_ROAD */
    { 5, 0x5F, 0x44, 20, 0x20, 63 }, /* FUCHIA_CITY */
    { 5, 0x5F, 0x44, 20, 0x20, 63 }, /* SAFARI_ZONE */
    { 5, 0x5F, 0x44, 20, 0x20, 64 }, /* SAFFRON_CITY */
    { 5, 0x5F, 0x42, 20, 0x20, 67 }, /* SEAFOAM_ISLANDS */
    { 5, 0x5F, 0x42, 20, 0x20, 67 }, /* CINNABAR_ISLAND */
    { 5, 0x5F, 0x43, 20, 0x20, 66 }, /* INDIGO_PLATEAU */
};

static const char * const MAP_NAMES[18] = {
    "PALLET TOWN",        /* 0 */
    "VIRIDIAN CITY",      /* 1 */
    "VIRIDIAN FOREST",    /* 2 */
    "PEWTER CITY",        /* 3 */
    "MT.MOON",            /* 4 */
    "CERULEAN CITY",      /* 5 */
    "VERMILION : SEASIDE",/* 6 */
    "VERMILION : STREETS",/* 7 */
    "ROCK MOUNTAIN",      /* 8 */
    "LAVENDER TOWN",      /* 9 */
    "CELADON CITY",       /* 10 */
    "CYCLING ROAD",       /* 11 */
    "FUCHIA CITY",        /* 12 */
    "SAFARI ZONE",        /* 13 */
    "SAFFRON CITY",       /* 14 */
    "SEAFOAM ISLANDS",    /* 15 */
    "CINNABAR ISLAND",    /* 16 */
    "INDIGO PLATEAU",     /* 17 */
};

/* EvolutionFailedText header: scrolling_text_normal 2, 20, 0, 19
 * → scrolling_text 5, 20, 2, 20, 0, 19 → { 5, 0x54, 0x42, 20, 0x00, 57 } */
static const uint8_t EVOLUTION_FAILED_HEADER[6] = { 5, 0x54, 0x42, 20, 0x00, 57 };

/* "IT EVOLVED INTO A " — scrolling_text_nopause 5, 38 */
static const uint8_t IT_EVOLVED_A_HEADER[6] = { 5, 0x54, 0, 0, 0, 38 };
/* "IT EVOLVED INTO AN " — scrolling_text_nopause 5, 39 */
static const uint8_t IT_EVOLVED_AN_HEADER[6] = { 5, 0x54, 0, 0, 0, 39 };
/* Evolved mon name (consonant prefix): scrolling_text 5, 38, 0, 20, 2, 17
 * → { 5, 0x66, 0x40, 20, 0x20, 75 } */
static const uint8_t EVO_NAME_CONSONANT_HEADER[6] = { 5, 0x66, 0x40, 20, 0x20, 75 };
/* Evolved mon name (vowel prefix): scrolling_text 5, 39, 0, 20, 2, 17
 * → { 5, 0x67, 0x40, 20, 0x20, 76 } */
static const uint8_t EVO_NAME_VOWEL_HEADER[6] = { 5, 0x67, 0x40, 20, 0x20, 76 };

/* PokemonCaughtSpecialBonusText: scrolling_text_nopause 7, 49 */
static const uint8_t CAUGHT_SPECIAL_BONUS_HEADER[6] = { 7, 0x54, 0, 0, 0, 49 };
/* OneBillionText: scrolling_text 7, 46, 5, 20, 2, 19
 * → { 7, 0x6E, 0x45, 20, 0x20, 80 } */
static const uint8_t ONE_BILLION_HEADER[6] = { 7, 0x6E, 0x45, 20, 0x20, 80 };

/* EvolutionSpecialBonusText: scrolling_text_nopause 7, 44 */
static const uint8_t EVO_SPECIAL_BONUS_HEADER[6] = { 7, 0x54, 0, 0, 0, 44 };
/* Data_2b6b (evolution digits): scrolling_text 7, 44, 6, 20, 2, 15
 * → { 7, 0x6C, 0x46, 20, 0x20, 73 } */
static const uint8_t EVO_DIGITS_HEADER[6] = { 7, 0x6C, 0x46, 20, 0x20, 73 };

/* RedStageInitialMaps (0x16605) — 7 starting maps for ChooseInitialMap */
static const uint8_t red_stage_initial_maps[7] = {
    MAP_PALLET_TOWN, MAP_VIRIDIAN_FOREST, MAP_PEWTER_CITY,
    MAP_CERULEAN_CITY, MAP_VERMILION_SEASIDE, MAP_ROCK_MOUNTAIN,
    MAP_LAVENDER_TOWN
};

/* LoadScrollingMapNameText (0x3118f)
 * Loads two scrolling texts: prefix in slot 0, map name in slot 1.
 * prefix_type: 0 = "START FROM", 1 = "ARRIVED AT" */
void load_scrolling_map_name_text(GameState *state, int prefix_type) {
    uint8_t map = state->current_map;
    if (map >= 18) map = 0;

    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    load_scrolling_text(state, 1, MAP_NAME_HEADERS[map], MAP_NAMES[map]);
    load_scrolling_text(state, 0,
        prefix_type == 0 ? START_FROM_TEXT_HEADER : ARRIVED_AT_TEXT_HEADER,
        prefix_type == 0 ? "START FROM" : "ARRIVED AT");
}

/* ResolveBallUpgradeTriggersCollision_RedField (0x1535d)
 * [C05] Clear alley triggers + UpdateFieldStructures on each collision.
 * [C06] Use BallTypeProgressionRedField lookup table for ball type upgrades.
 * [C07] Show "FIELD MULTIPLIER xN" bottom text + call TransitionPinballUpgrade. */
static void resolve_upgrade_triggers(GameState *state) {
    if (!state->which_pinball_upgrade_trigger)
        goto load_gfx;

    uint8_t id = state->which_pinball_upgrade_trigger_id;
    state->which_pinball_upgrade_trigger = 0;

    /* Only active when stage collision state is odd (bit 0 set) */
    if (!(state->stage_collision_state & 1))
        goto load_gfx;

    /* Skip if already blinking */
    if (state->ball_upgrade_triggers_blinking)
        goto load_gfx;

    /* [C05] Clear alley triggers + update field structures (ASM: 0x15376) */
    state->right_alley_trigger = 0;
    state->left_alley_trigger = 0;
    state->secondary_left_alley_trigger[0] = 0;
    update_field_structures(state);
    check_special_mode_collision(state, 11); /* SPECIAL_COLLISION_BALL_UPGRADE */

    uint8_t trig_idx = id - 0x0E;
    if (trig_idx >= 3)
        goto load_gfx;

    /* Set trigger state (skip if already on) */
    if (state->ball_upgrade_trigger_states[trig_idx])
        goto load_gfx;

    state->ball_upgrade_trigger_states[trig_idx] = 1;

    /* 100 points */
    add_score_with_multiplier(state, state->config->scores.score_100);

    /* Check all 3 triggers */
    if (state->ball_upgrade_trigger_states[0] &&
        state->ball_upgrade_trigger_states[1] &&
        state->ball_upgrade_trigger_states[2]) {
        /* All 3 triggered: start blinking, upgrade ball */
        state->ball_upgrade_triggers_blinking = 1;
        state->ball_upgrade_triggers_blinking_frames_remaining = 0x80;

        /* Start degradation timer (~60 seconds) */
        state->ball_type_counter = 3600;

        /* 400 points bonus */
        add_score_with_multiplier(state, state->config->scores.score_400);

        if (state->ball_type >= MASTER_BALL) {
            /* .masterBall (0x154a9): SFX + 1,000,000 pts (no mult) + special text. */
            PLAY_SFX(state, "slot_start", 0x0F, 0x4D);
            add_score_no_multiplier(state, state->config->scores.score_1000000);
            static const uint8_t bcd_1m[4] = { 0x01, 0x00, 0x00, 0x00 };
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text_with_bcd(state, 1, DIGITS_1_8_HEADER, bcd_1m);
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER,
                                "FIELD MULTIPLIER SPECIAL BONUS");
        } else {
            /* .allTriggersOn (0x15491): SFX + text + upgrade */
            PLAY_SFX(state, "slot_reel_stop", 0x06, 0x3A);
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 0, FIELD_MULT_HEADER,
                                "FIELD MULTIPLIER x0");
            /* [C06] Lookup table progression (ASM: BallTypeProgressionRedField) */
            uint8_t new_type = BallTypeProgressionRedField[state->ball_type];
            state->ball_type = new_type;
            /* Replace the '0' digit at position 18 with the new ball type digit */
            state->bottom_message_text[18] = (uint8_t)(0x86 + new_type);
        }
        /* [C07] TransitionPinballUpgrade: reload ball sprite tiles + palette */
        load_ball_gfx(state);
    } else {
        /* Single trigger: lb de, $00, $09 */
        PLAY_SFX(state, "object_hit", 0x00, 0x09);
    }

load_gfx:
    /* ASM: always falls through to LoadPinballUpgradeTriggersGraphics */
    load_upgrade_triggers_graphics(state);
}

/* Update blinking animation for upgrade triggers */
static void update_upgrade_blinking(GameState *state) {
    /* ASM: UpdatePinballUpgradeBlinkingAnimation_RedField (0x15270)
     * This is STATE-ONLY — no graphics loading. Graphics are loaded by
     * resolve_upgrade_triggers (top stage only) via fall-through to
     * LoadPinballUpgradeTriggersGraphics. */
    if (!state->ball_upgrade_triggers_blinking) {
        /* Flipper key rotation of trigger states when not blinking */
        if (joypad_is_key_pressed(state, &state->key_config_left_flipper)) {
            uint8_t a = state->ball_upgrade_trigger_states[0];
            uint8_t b = state->ball_upgrade_trigger_states[1];
            uint8_t c = state->ball_upgrade_trigger_states[2];
            state->ball_upgrade_trigger_states[0] = b;
            state->ball_upgrade_trigger_states[1] = c;
            state->ball_upgrade_trigger_states[2] = a;
        } else if (joypad_is_key_pressed(state, &state->key_config_right_flipper)) {
            uint8_t a = state->ball_upgrade_trigger_states[0];
            uint8_t b = state->ball_upgrade_trigger_states[1];
            uint8_t c = state->ball_upgrade_trigger_states[2];
            state->ball_upgrade_trigger_states[0] = c;
            state->ball_upgrade_trigger_states[1] = a;
            state->ball_upgrade_trigger_states[2] = b;
        }
        return;
    }

    state->ball_upgrade_triggers_blinking_frames_remaining--;
    if (state->ball_upgrade_triggers_blinking_frames_remaining == 0) {
        state->ball_upgrade_triggers_blinking = 0;
        memset(state->ball_upgrade_trigger_states, 0, 3);
        return;
    }

    /* Blink toggle every 8 frames */
    if ((state->ball_upgrade_triggers_blinking_frames_remaining & 7) == 0) {
        uint8_t show = (state->ball_upgrade_triggers_blinking_frames_remaining & 8) ? 1 : 0;
        for (int i = 0; i < 3; i++)
            state->ball_upgrade_trigger_states[i] = show;
    }
}

/*=============================================================================
 * LoadFieldStructureGraphics_RedField (0x159f4)
 *
 * Updates BG tilemap tiles when the stage collision state changes.
 * Handles Ditto blocking/unblocking, lightning bolt guard rail, voltorb roof.
 *
 * The ASM uses a 64-entry pointer table indexed by:
 *   c = (current_state << 1) | (previous_state << 4)
 * Each entry chains sub-chunks that call Func_1198 (sequential tilemap writes)
 * or LoadTileLists (specific tile ID writes) to the BG map.
 *
 * The queue system skips byte[0] of each leaf chunk (size indicator),
 * so Func_1198/LoadTileLists data starts at byte[1].
 *===========================================================================*/

/* Leaf chunk 0: TileData_15db7 (Func_1198) - Ditto area state A */
static void write_ditto_a(VirtualVRAM *vram) {
    uint8_t *bg = vram->bg_map[0];
    bg[0x4C] = 0x80;
    /* $65-$68 at $6C-$6F */
    bg[0x6C] = 0x65; bg[0x6D] = 0x66; bg[0x6E] = 0x67; bg[0x6F] = 0x68;
    /* $69-$6C at $8D-$90 */
    bg[0x8D] = 0x69; bg[0x8E] = 0x6A; bg[0x8F] = 0x6B; bg[0x90] = 0x6C;
    /* $6D-$70 at $AE-$B1 */
    bg[0xAE] = 0x6D; bg[0xAF] = 0x6E; bg[0xB0] = 0x6F; bg[0xB1] = 0x70;
    /* $71-$72 at $D0-$D1 */
    bg[0xD0] = 0x71; bg[0xD1] = 0x72;
    /* $73-$74 at $F1-$F2 */
    bg[0xF1] = 0x73; bg[0xF2] = 0x74;
    /* $75-$76 at $111-$112 */
    bg[0x111] = 0x75; bg[0x112] = 0x76;
    /* $77 at $132 */
    bg[0x132] = 0x77;
}

/* Leaf chunk 1: TileData_15dd5 (Func_1198) - Ditto area state B */
static void write_ditto_b(VirtualVRAM *vram) {
    uint8_t *bg = vram->bg_map[0];
    bg[0x4C] = 0x51;
    bg[0x6C] = 0x52; bg[0x6D] = 0x53; bg[0x6E] = 0x54; bg[0x6F] = 0x55;
    bg[0x8D] = 0x56; bg[0x8E] = 0x57; bg[0x8F] = 0x58; bg[0x90] = 0x59;
    bg[0xAE] = 0x5A; bg[0xAF] = 0x5B; bg[0xB0] = 0x5C; bg[0xB1] = 0x5D;
    bg[0xD0] = 0x5E; bg[0xD1] = 0x5F;
    bg[0xF1] = 0x60; bg[0xF2] = 0x61;
    bg[0x111] = 0x62; bg[0x112] = 0x63;
    bg[0x132] = 0x64;
}

/* Leaf chunk 2: TileData_15df2 (LoadTileLists) - Guard rail state A (lightning bolt) */
static void write_guardrail_a(VirtualVRAM *vram) {
    uint8_t *bg = vram->bg_map[0];
    bg[0xA9] = 0x1E; bg[0xAA] = 0x1F; bg[0xAB] = 0x20; bg[0xAC] = 0x21; bg[0xAD] = 0x22;
    bg[0xC7] = 0x23; bg[0xC8] = 0x24; bg[0xC9] = 0x39; bg[0xCA] = 0x3A;
    bg[0xCB] = 0x25; bg[0xCC] = 0x3B; bg[0xCD] = 0x26;
    bg[0xE6] = 0x27; bg[0xE7] = 0x37; bg[0xE8] = 0x28; bg[0xE9] = 0x29;
    bg[0xEA] = 0x2A; bg[0xEB] = 0x2B; bg[0xEC] = 0x3C; bg[0xED] = 0x2C;
    bg[0x106] = 0x2D; bg[0x107] = 0x38; bg[0x108] = 0x2E;
    bg[0x10D] = 0x2F;
    bg[0x126] = 0x30;
}

/* Leaf chunk 3: TileData_15e21 (LoadTileLists) - Guard rail state B (no lightning bolt) */
static void write_guardrail_b(VirtualVRAM *vram) {
    uint8_t *bg = vram->bg_map[0];
    bg[0xA9] = 0x0B; bg[0xAA] = 0x0C; bg[0xAB] = 0x0D; bg[0xAC] = 0x0E; bg[0xAD] = 0x0F;
    bg[0xC7] = 0x10; bg[0xC8] = 0x11; bg[0xC9] = 0x33; bg[0xCA] = 0x34;
    bg[0xCB] = 0x12; bg[0xCC] = 0x35; bg[0xCD] = 0x13;
    bg[0xE6] = 0x14; bg[0xE7] = 0x31; bg[0xE8] = 0x15; bg[0xE9] = 0x16;
    bg[0xEA] = 0x17; bg[0xEB] = 0x18; bg[0xEC] = 0x36; bg[0xED] = 0x19;
    bg[0x106] = 0x1A; bg[0x107] = 0x32; bg[0x108] = 0x1B;
    bg[0x10D] = 0x1C;
    bg[0x126] = 0x1D;
}

/* Leaf chunk 4: TileData_15e50 (LoadTileLists) - Voltorb roof open */
static void write_roof_open(VirtualVRAM *vram) {
    uint8_t *bg = vram->bg_map[0];
    bg[0x100] = 0x45; bg[0x101] = 0x46; bg[0x102] = 0x22;
    bg[0x120] = 0x45; bg[0x121] = 0x46;
    bg[0x140] = 0x45; bg[0x141] = 0x46;
    bg[0x160] = 0x45; bg[0x161] = 0x46;
}

/* Leaf chunk 5: TileData_15e69 (LoadTileLists) - Voltorb roof variant */
static void write_roof_variant(VirtualVRAM *vram) {
    uint8_t *bg = vram->bg_map[0];
    bg[0x100] = 0x43; bg[0x101] = 0x44; bg[0x102] = 0x22;
    bg[0x120] = 0x45; bg[0x121] = 0x46;
    bg[0x140] = 0x45; bg[0x141] = 0x46;
    bg[0x160] = 0x45; bg[0x161] = 0x46;
}

/* Leaf chunk 6: TileData_15e82 (Func_1198) - Voltorb roof sequential */
static void write_roof_seq(VirtualVRAM *vram) {
    uint8_t *bg = vram->bg_map[0];
    bg[0x100] = 0x47; bg[0x101] = 0x48; bg[0x102] = 0x49;
    bg[0x120] = 0x4A; bg[0x121] = 0x4B;
    bg[0x140] = 0x4C; bg[0x141] = 0x4D;
    bg[0x160] = 0x4E; bg[0x161] = 0x4F;
}

/* Leaf chunk function pointer type */
typedef void (*StructureChunkFn)(VirtualVRAM *vram);

/* Intermediate chunks: arrays of leaf chunk functions, terminated by NULL.
 * Indices match the TileData_15d85..TileData_15db4 intermediate labels. */
static const StructureChunkFn chunk_ditto_a_guardrail_a[] = { write_ditto_a, write_guardrail_a, NULL };
static const StructureChunkFn chunk_guardrail_a_roof_seq[] = { write_guardrail_a, write_roof_seq, NULL };
static const StructureChunkFn chunk_ditto_a_guardrail_a_roof_open[] = { write_ditto_a, write_guardrail_a, write_roof_open, NULL };
static const StructureChunkFn chunk_ditto_a[] = { write_ditto_a, NULL };
static const StructureChunkFn chunk_roof_seq[] = { write_roof_seq, NULL };
static const StructureChunkFn chunk_ditto_a_roof_open[] = { write_ditto_a, write_roof_open, NULL };
static const StructureChunkFn chunk_ditto_b_roof_seq[] = { write_ditto_b, write_roof_seq, NULL };
static const StructureChunkFn chunk_roof_open[] = { write_roof_open, NULL };
static const StructureChunkFn chunk_ditto_a_roof_variant[] = { write_ditto_a, write_roof_variant, NULL };
static const StructureChunkFn chunk_roof_variant[] = { write_roof_variant, NULL };
static const StructureChunkFn chunk_guardrail_a[] = { write_guardrail_a, NULL };
static const StructureChunkFn chunk_guardrail_b[] = { write_guardrail_b, NULL };

/* Main pointer table: TileDataPointers_15d05, 64 entries.
 * Index = (prev_state * 8) + current_state.
 * NULL entries mean no tilemap change for that transition. */
static const StructureChunkFn *structure_pointer_table[64] = {
    /* prev=0: cur=0..7 */
    NULL, chunk_guardrail_a, chunk_ditto_a, chunk_ditto_a_guardrail_a,
    chunk_roof_seq, chunk_guardrail_a_roof_seq, chunk_ditto_a_roof_open, chunk_ditto_a_guardrail_a_roof_open,
    /* prev=1: cur=0..7 */
    NULL, NULL, NULL, chunk_ditto_a,
    NULL, chunk_roof_seq, NULL, chunk_ditto_a_roof_open,
    /* prev=2: cur=0..7 */
    NULL, NULL, NULL, chunk_guardrail_a,
    chunk_ditto_b_roof_seq, NULL, chunk_roof_open, NULL,
    /* prev=3: cur=0..7 */
    NULL, NULL, chunk_guardrail_b, NULL,
    NULL, chunk_ditto_b_roof_seq, NULL, chunk_roof_open,
    /* prev=4: cur=0..7 */
    NULL, NULL, chunk_ditto_a_roof_variant, NULL,
    NULL, chunk_guardrail_a, chunk_ditto_a_roof_open, NULL,
    /* prev=5: cur=0..7 */
    NULL, NULL, NULL, chunk_ditto_a_roof_variant,
    chunk_guardrail_b, NULL, NULL, chunk_ditto_a_roof_open,
    /* prev=6: cur=0..7 */
    NULL, NULL, chunk_roof_variant, NULL,
    chunk_ditto_b_roof_seq, NULL, NULL, chunk_guardrail_a,
    /* prev=7: cur=0..7 */
    NULL, NULL, NULL, chunk_roof_variant,
    NULL, chunk_ditto_b_roof_seq, chunk_guardrail_b, NULL,
};

/* LoadFieldStructureGraphics_RedField (0x159f4) */
void load_field_structure_graphics(GameState *state) {
    uint8_t prev = state->previous_field_structure_state;
    uint8_t cur = state->stage_collision_state;

    /* ASM: Sound effect plays when Ditto/guardrail state actually changes.
     * Strips bit 0 (staryu side) before comparing, and skips sound for
     * the 0↔1 transition (add==2). From ASM 0x159f4-0x15a12. */
    {
        uint8_t prev_stripped = prev & 0xFE;
        uint8_t cur_stripped = cur & 0xFE;
        if (cur_stripped != prev_stripped) {
            uint8_t sum = cur_stripped + prev_stripped;
            if (sum != 2) {
                PLAY_SFX(state, "silence", 0x00, 0x00);
            }
        }
    }

    /* Look up the transition in the pointer table */
    uint8_t idx = (prev & 7) * 8 + (cur & 7);
    const StructureChunkFn *chunks = structure_pointer_table[idx];
    if (!chunks) {
        /* ASM: LoadFieldStructureGraphics_RedField returns WITHOUT updating wd7f2
         * when the pointer table entry is NULL. This is critical — updating prev
         * for NULL entries causes wrong transitions on subsequent calls. */
        return;
    }

    /* Execute each leaf chunk function */
    while (*chunks) {
        (*chunks)(state->vram);
        chunks++;
    }

    state->previous_field_structure_state = cur;
}

/* UpdateFieldStructures_RedField (0x159c9)
 * Applies the stored structure backup to collision state and reloads collision data.
 * This seals the launch alley by changing to a collision map without the alley opening.
 * Only runs once per ball (backup bit 7 set to prevent re-calling). */
void update_field_structures(GameState *state) {
    if (state->red_stage_structure_backup & 0x80)
        return;  /* Already applied this ball */

    uint8_t backup = state->red_stage_structure_backup;
    state->stage_collision_state = (state->stage_collision_state & 1) | backup;
    state->red_stage_structure_backup = 0xFF;
    load_stage_collision_attributes(state);
    load_field_structure_graphics(state);
    /* ASM: ld a, $1 / ld [wd580], a then call LoadTimerGraphics */
    state->wd580 = 1;
    /* LoadTimerGraphics is a NOP on GBC (checks hGameBoyColorFlag, ret nz) */
}

/* ResolveRedStageBoardTriggerCollision (0x1581f)
 * Full dispatch matching ASM: each trigger ID maps to a specific handler.
 * All pending trigger flags are processed in sequence. */
static void resolve_board_triggers(GameState *state) {
    if (!state->which_board_trigger)
        return;

    uint8_t id = state->which_board_trigger_id;
    state->which_board_trigger = 0;

    /* 5 points */
    add_score_with_multiplier(state, state->config->scores.score_5);

    /* Set collided trigger flag */
    uint8_t trig_idx = id - 0x11;
    if (trig_idx < 8)
        state->collided_alley_triggers[trig_idx] = 1;

    /* Trigger 0 ($11): HandleSecondaryLeftAlleyTrigger (0x1587c) */
    if (state->collided_alley_triggers[0]) {
        state->collided_alley_triggers[0] = 0;
        if (state->left_alley_trigger) {
            state->left_alley_trigger = 0;
            if (!check_special_mode_collision(state, 1)) { /* ASM: ret c skips normal */
                if (state->left_alley_count < 3) {
                    state->left_alley_count++;
                    state->indicator_states[0] = state->left_alley_count | 0x80;
                    if (state->left_alley_count == 3) {
                        /* All 3 left alley hits: seal with collision state 6/7 */
                        state->stage_collision_state =
                            (state->stage_collision_state & 1) | 6;
                        load_stage_collision_attributes(state);
                        load_field_structure_graphics(state);
                    }
                }
            }
        }
    }

    /* Trigger 1 ($12): HandleThirdLeftAlleyTrigger (0x158c0) - same as trigger 0 */
    if (state->collided_alley_triggers[1]) {
        state->collided_alley_triggers[1] = 0;
        if (state->left_alley_trigger) {
            state->left_alley_trigger = 0;
            if (!check_special_mode_collision(state, 1)) { /* ASM: ret c */
                if (state->left_alley_count < 3) {
                    state->left_alley_count++;
                    state->indicator_states[0] = state->left_alley_count | 0x80;
                    if (state->left_alley_count == 3) {
                        state->stage_collision_state =
                            (state->stage_collision_state & 1) | 6;
                        load_stage_collision_attributes(state);
                        load_field_structure_graphics(state);
                    }
                }
            }
        }
    }

    /* Trigger 2 ($13): HandleSecondaryStaryuAlleyTrigger (0x15904) */
    if (state->collided_alley_triggers[2]) {
        state->collided_alley_triggers[2] = 0;
        if (state->secondary_left_alley_trigger[0]) {
            state->secondary_left_alley_trigger[0] = 0;
            check_special_mode_collision(state, 3); /* SPECIAL_COLLISION_STARYU_ALLEY_TRIGGER */
        }
    }

    /* Trigger 3 ($14): HandleLeftAlleyTrigger (0x1591e)
     * Ball exits left alley: set left_alley_trigger and seal alley via structures. */
    if (state->collided_alley_triggers[3]) {
        state->collided_alley_triggers[3] = 0;
        state->right_alley_trigger = 0;
        state->secondary_left_alley_trigger[0] = 0;
        state->left_alley_trigger = 1;
        update_field_structures(state);
    }

    /* Trigger 4 ($15): HandleStaryuAlleyTrigger (0x15931) */
    if (state->collided_alley_triggers[4]) {
        state->collided_alley_triggers[4] = 0;
        state->right_alley_trigger = 0;
        state->left_alley_trigger = 0;
        state->secondary_left_alley_trigger[0] = 1;
        update_field_structures(state);
    }

    /* Trigger 5 ($16): HandleSecondaryRightAlleyTrigger (0x15944) */
    if (state->collided_alley_triggers[5]) {
        state->collided_alley_triggers[5] = 0;
        if (state->right_alley_trigger) {
            state->right_alley_trigger = 0;
            if (!check_special_mode_collision(state, 2)) { /* ASM: ret c */
                if (state->right_alley_count < 3) {
                    state->right_alley_count++;
                    if (state->right_alley_count == 3) {
                        state->indicator_states[1] = state->right_alley_count;
                    } else {
                        state->indicator_states[1] = state->right_alley_count | 0x80;
                    }
                    if (state->right_alley_count >= 2)
                        state->indicator_states[3] = 0x80;
                }
            }
        }
    }

    /* Trigger 6 ($17): HandleRightAlleyTrigger (0x1597d) */
    if (state->collided_alley_triggers[6]) {
        state->collided_alley_triggers[6] = 0;
        state->left_alley_trigger = 0;
        state->secondary_left_alley_trigger[0] = 0;
        state->right_alley_trigger = 1;
        update_field_structures(state);
    }

    /* Trigger 7 ($18): HandleThirdRightAlleyTrigger (0x15990) - same as trigger 5 */
    if (state->collided_alley_triggers[7]) {
        state->collided_alley_triggers[7] = 0;
        if (state->right_alley_trigger) {
            state->right_alley_trigger = 0;
            if (!check_special_mode_collision(state, 2)) { /* ASM: ret c */
                if (state->right_alley_count < 3) {
                    state->right_alley_count++;
                    if (state->right_alley_count == 3) {
                        state->indicator_states[1] = state->right_alley_count;
                    } else {
                        state->indicator_states[1] = state->right_alley_count | 0x80;
                    }
                    if (state->right_alley_count >= 2)
                        state->indicator_states[3] = 0x80;
                }
            }
        }
    }
}

/* PikachuSaverAnimationData_RedField (0x1673c): 18 entries, [duration, sprite_id] */
static const uint8_t PikachuSaverAnimData[] = {
    0x0C, 0x02,  /* 0: 12 frames, sprite 2 */
    0x05, 0x03,  /* 1: 5 frames, sprite 3 */
    0x05, 0x02,  /* 2: 5 frames, sprite 2 */
    0x05, 0x04,  /* 3: 5 frames, sprite 4 */
    0x05, 0x05,  /* 4: 5 frames, sprite 5 */
    0x05, 0x02,  /* 5: 5 frames, sprite 2 */
    0x06, 0x06,  /* 6: 6 frames, sprite 6 */
    0x06, 0x07,  /* 7: 6 frames, sprite 7 */
    0x06, 0x08,  /* 8: 6 frames, sprite 8 */
    0x06, 0x02,  /* 9: 6 frames, sprite 2 */
    0x06, 0x05,  /* 10: 6 frames, sprite 5 */
    0x06, 0x08,  /* 11: 6 frames, sprite 8 */
    0x06, 0x07,  /* 12: 6 frames, sprite 7 */
    0x06, 0x02,  /* 13: 6 frames, sprite 2 */
    0x06, 0x08,  /* 14: 6 frames, sprite 8 */
    0x06, 0x07,  /* 15: 6 frames, sprite 7 */
    0x06, 0x02,  /* 16: 6 frames, sprite 2 */
    0x01, 0x00,  /* 17: 1 frame, sprite 0 */
    0x00,        /* Terminator */
};

/* PikachuSaverAnimation2Data_RedField (0x16761): 2 entries (partial bounce) */
static const uint8_t PikachuSaverAnim2Data[] = {
    0x0C, 0x02,  /* 0: 12 frames, sprite 2 */
    0x01, 0x00,  /* 1: 1 frame, sprite 0 */
    0x00,        /* Terminator */
};

/* SetPikachuSaverSide_RedField (0x16766) */
static void set_pikachu_saver_side(GameState *state) {
    if (joypad_is_key_pressed(state, &state->key_config_left_flipper))
        state->which_pikachu_saver_side = 0;
    else if (joypad_is_key_pressed(state, &state->key_config_right_flipper))
        state->which_pikachu_saver_side = 1;
}

/* UpdatePikachuSaverAnimation_RedField (0x1669e) */
static void update_pikachu_saver_animation(GameState *state) {
    if (state->pikachu_saver_state == 1) {
        /* State 1: full save animation */
        bool step_done = update_animation(
            &state->pikachu_saver_anim, PikachuSaverAnimData);
        if (!step_done) return;

        uint8_t idx = state->pikachu_saver_anim.index;
        if (idx == 1) {
            /* Pikachu cry, heavy rumble, increment saves */
            state->rumble_pattern = 0xFF;
            state->rumble_duration = 0x60;
            state->num_pikachu_saves++;
            /* Extra ball every 10 saves (ASM: DivideBy10 check) */
            if (state->num_pikachu_saves % 10 == 0) {
                add_extra_ball(state);
            }
            /* ASM: PlayPikachuSoundClip(a=$1) then lb de, $16, $10 / call PlaySoundEffect.
             * This is the best we can do — the original uses raw GBC wave channel
             * PCM streaming via NR32 volume modulation (PlayPikachuSoundClip at
             * bank 0x50) which can't be reproduced through the bytecode SFX system. */
            audio_play_pcm(state->audio, 1);
            PLAY_SFX(state, "extra_ball", 0x16, 0x10);
        } else if (idx == 0x11) {
            /* Animation complete: launch ball upward */
            state->ball_y_velocity = (int16_t)0xFC00;
            state->enable_ball_gravity_and_tilt = 1;
            add_score_with_multiplier(state, state->config->scores.score_5000);
            state->pikachu_saver_state = 0;
        }
    } else if (state->pikachu_saver_state == 2) {
        /* State 2: partial bounce animation */
        bool step_done = update_animation(
            &state->pikachu_saver_anim, PikachuSaverAnim2Data);
        if (!step_done) return;

        if (state->pikachu_saver_anim.index == 1) {
            state->pikachu_saver_state = 0;
        }
    } else {
        /* State 0: idle - cycle sprite frame based on frame counter */
        state->pikachu_saver_anim.frame =
            (state->hram.frame_counter >> 4) & 1;
    }
}

/* ResolveRedStagePikachuCollision (0x1660c) */
static void resolve_pikachu(GameState *state) {
    if (state->which_pikachu) {
        state->which_pikachu = 0;

        /* Skip if animation already playing */
        if (state->pikachu_saver_state != 0)
            goto per_frame;

        /* Check slot reward first (bypasses side/charge check) */
        if (state->pikachu_saver_slot_reward_active)
            goto full_save;

        /* Check side match: (id - 0x1C) must equal which_pikachu_saver_side */
        uint8_t side = state->which_pikachu_id - 0x1C;
        if (side != state->which_pikachu_saver_side)
            goto per_frame;

        /* Check if fully charged */
        if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE)
            goto full_save;

        /* Not fully charged: partial bounce animation */
        init_animation(&state->pikachu_saver_anim, PikachuSaverAnim2Data);
        state->pikachu_saver_state = 2;
        /* ASM: lb de, $00, $3b / call PlaySoundEffect */
        PLAY_SFX(state, "slot_trigger", 0x00, 0x3B);
        goto per_frame;

    full_save:
        /* Full save: freeze ball, start 18-frame animation */
        fill_bottom_message_buffer_with_black_tile(state);
        init_animation(&state->pikachu_saver_anim, PikachuSaverAnimData);
        if (!state->pikachu_saver_slot_reward_active)
            state->pikachu_saver_charge = 0;
        state->pikachu_saver_state = 1;

        /* Freeze ball */
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
        state->enable_ball_gravity_and_tilt = 0;
    }

per_frame:
    /* Side selection only when idle */
    if (state->pikachu_saver_state == 0)
        set_pikachu_saver_side(state);

    /* Animation state machine update */
    update_pikachu_saver_animation(state);

    /* Sound cooldown for fully-charged indicator (wd51e) */
    if (state->pikachu_saver_charge >= MAX_PIKACHU_SAVER_CHARGE) {
        if (state->pikachu_saver_sound_cooldown > 0) {
            state->pikachu_saver_sound_cooldown--;
            if (state->pikachu_saver_sound_cooldown == 0x5A) {
                /* ASM: lb de, $0f, $22 / call PlaySoundEffect */
                PLAY_SFX(state, "slot_spin", 0x0F, 0x22);
            }
        }
    }
}

/* ResolveStaryuCollision (0x16781 / 0x167ff) */
/*
 * ResolveStaryuCollision_Top (0x16781) / ResolveStaryuCollision_Bottom (0x167ff)
 *
 * wd502 bit 0 = staryu_side (toggled on each hit)
 * wd502 bit 1 = staryu_anim_active (set on hit, cleared when timer expires)
 * wd503 = staryu_timer (set to 0x14 on hit, decremented per frame)
 *
 * Top stage: when timer expires, reload collision attributes + structure graphics.
 * Bottom stage: when timer expires, only update collision state (no reload).
 * ASM: bottom Staryu does NOT call LoadStageCollisionAttributes or
 *       LoadFieldStructureGraphics_RedField.
 */
static void resolve_staryu(GameState *state) {
    if (state->staryu_collision) {
        state->staryu_collision = 0;

        /* If timer already running, decrement (ASM: jr nz, .asm_167c2).
         * [H02] If timer reaches 0 on this frame, fall through to timer-expired
         * path instead of returning (ASM: dec a / ret nz falls through). */
        if (state->staryu_timer) {
            state->staryu_timer--;
            if (state->staryu_timer)
                return;
            /* Timer just expired — fall through to timer-expired logic */
        } else {
            /* New collision: score, toggle, set timer */
            add_score_with_multiplier(state, state->config->scores.score_5000);

            /* Toggle staryu side (wd502 ^= 1).
             * [H01] Top: set bit 1 (anim_active). Bottom: only xor bit 0. */
            state->staryu_side ^= 1;
            if (state->current_stage == STAGE_RED_FIELD_TOP) {
                state->staryu_anim_active = 1;
                load_staryu_graphics_top(state);
                check_special_mode_collision(state, 6); /* SPECIAL_COLLISION_STARYU */
            } else {
                load_staryu_graphics_bottom(state);
                check_special_mode_collision(state, 6); /* SPECIAL_COLLISION_STARYU */
            }
            state->staryu_timer = 0x14; /* 20 frames */
            return;
        }
    } else {
        /* Per-frame timer countdown (ASM: .noCollision path) */
        if (!state->staryu_timer)
            return;
        state->staryu_timer--;
        if (state->staryu_timer)
            return;
        /* Timer just expired — fall through */
    }

    /* Timer-expired path (shared by collision+timer and no-collision+timer).
     * ASM: res 1, [wd502] + update collision state + reload (top only). */
    state->staryu_anim_active = 0;

    state->stage_collision_state =
        (state->stage_collision_state & 0xFE) | (state->staryu_side & 1);

    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        load_staryu_graphics_top(state);
        load_stage_collision_attributes(state);
        load_field_structure_graphics(state);
        PLAY_SFX(state, "spinner_hit", 0x00, 0x07);
        /* ASM: checks bit 0 of collision state for enabled/disabled triggers.
         * LoadDisabledPinballUpgradeTriggerGraphics is a no-op on GBC (ret nz). */
        load_upgrade_triggers_graphics(state);
    } else {
        PLAY_SFX(state, "spinner_hit", 0x00, 0x07);
    }
}

/* BellsproutAnimationData (0x15f69): [duration, sprite_id] pairs.
 * Sprite IDs index into BellsproutHeadAnimationSpriteIds:
 *   0=open mouth (normal), 1=opening, 2=closing, 3=closed (eating) */
static const uint8_t BellsproutAnimationData[] = {
    0x08, 0x01,  /* Index 0: 8 frames, mouth opening */
    0x06, 0x02,  /* Index 1: 6 frames, mouth closing */
    0x20, 0x03,  /* Index 2: 32 frames, closed/eating */
    0x06, 0x02,  /* Index 3: 6 frames, mouth opening */
    0x08, 0x01,  /* Index 4: 8 frames, mouth open */
    0x01, 0x00,  /* Index 5: 1 frame, spitting */
    0x29, 0x00,  /* Index 6+: idle breathing loop */
    0x28, 0x01,
    0x2A, 0x00,
    0x27, 0x01,
    0x29, 0x00,
    0x28, 0x01,
    0x2B, 0x00,
    0x28, 0x01,
    0x00,        /* Terminator */
};

/* ResolveBellsproutCollision (0x15e93) */
static void resolve_bellsprout(GameState *state) {
    if (state->bellsprout_collision) {
        state->bellsprout_collision = 0;

        /* 10,000 points */
        add_score_with_multiplier(state, state->config->scores.score_10000);
        /* ASM: lb de, $00, $05 / call PlaySoundEffect */
        PLAY_SFX(state, "bumper_small", 0x00, 0x05);

        /* Init animation (ASM: call InitAnimation) */
        init_animation(&state->bellsprout_anim, BellsproutAnimationData);

        /* Freeze ball at (0x7C, 0x78) */
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->ball_x_pos = MAKE_UFIXED(0x7C, 0);
        state->ball_y_pos = MAKE_UFIXED(0x78, 0);
        state->enable_ball_gravity_and_tilt = 0;
    }

    /* Per-frame animation update (ASM: call UpdateAnimation) */
    bool anim_step_done = update_animation(
        &state->bellsprout_anim, BellsproutAnimationData);

    /* Reset idle breathing loop (ASM 0x15eda):
     * When frame_counter reaches 0 outside of the eat sequence,
     * reset to index 6 with 25-frame cycle. */
    if (state->bellsprout_anim.frame_counter == 0 &&
        state->bellsprout_anim.index > 5) {
        state->bellsprout_anim.frame_counter = 0x19;
        state->bellsprout_anim.frame = 0;
        state->bellsprout_anim.index = 6;
    }

    if (!anim_step_done) return;

    /* Animation step completed - check index for game state changes */
    uint8_t idx = state->bellsprout_anim.index;

    if (idx == 1) {
        /* Mouth closing complete: ball disappears into Bellsprout */
        state->pinball_is_visible = 0;
        /* ASM 0x15f01: Check if Catch'em mode should start (right_alley_count >= 2) */
        if (state->right_alley_count >= 2) {
            /* count == 2 → rare_mons_flag = 0, count > 2 → rare_mons_flag = 8 */
            state->rare_mons_flag = (state->right_alley_count == 2) ? 0 : 8;
            start_catchem_mode(state);
        }
        /* Increment_Max100 + AddExtraBall every 25 entries */
        if (increment_max100(&state->num_bellsprout_entries)) {
            if ((state->num_bellsprout_entries % 25) == 0) {
                add_extra_ball(state);
            }
        }
    } else if (idx == 4) {
        /* Mouth open again: ball reappears */
        state->pinball_is_visible = 1;
    } else if (idx == 5) {
        /* Spitting: re-enable gravity, clear X velocity high byte, push ball down.
         * ASM: xor a / ld [wBallXVelocity + 1], a — only clears high byte. */
        state->enable_ball_gravity_and_tilt = 1;
        state->ball_x_velocity &= 0x00FF;  /* Clear high byte only, preserve low */
        state->ball_y_velocity = MAKE_FIXED(2, 0);
        /* ASM: lb de, $00, $06 / call PlaySoundEffect */
        PLAY_SFX(state, "bumper", 0x00, 0x06);
        check_special_mode_collision(state, 5); /* SPECIAL_COLLISION_BELLSPROUT */
    }
}

/*=============================================================================
 * ResolveDittoSlotCollision (0x160f0)
 *
 * Full 16-frame countdown:
 *   0x10: Freeze ball at ($11,$23), 10K points, SFX, rumble
 *   0x0F: LoadMiniBallGfx
 *   0x0C: LoadSuperMiniPinballGfx
 *   0x09: Hide ball, clear spin/rotation
 *   0x06: StartEvolutionMode, show ball, enable gravity, rumble
 *   0x03: LoadMiniBallGfx (growing back)
 *   0x00: LoadBallGfx (normal), eject Y=+2.0
 *===========================================================================*/
static void resolve_ditto_slot(GameState *state) {
    if (state->ditto_slot_collision) {
        state->ditto_slot_collision = 0;

        /* 10,000 points */
        add_score_with_multiplier(state, state->config->scores.score_10000);
        PLAY_SFX(state, "arrow_indicator", 0x00, 0x21);

        /* Freeze ball at ditto slot position */
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->ball_x_pos = MAKE_UFIXED(0x11, 0);
        state->ball_y_pos = MAKE_UFIXED(0x23, 0);
        state->ditto_enter_or_exit_counter = 0x10;
        state->rumble_pattern = 0x05;
        state->rumble_duration = 0x08;
    }

    /* Per-frame counter update */
    if (state->ditto_enter_or_exit_counter > 0) {
        state->ditto_enter_or_exit_counter--;
        uint8_t c = state->ditto_enter_or_exit_counter;

        if (c == 0x0F) {
            load_mini_ball_gfx(state);
        } else if (c == 0x0C) {
            load_super_mini_ball_gfx(state);
        } else if (c == 0x09) {
            state->pinball_is_visible = 0;
            state->ball_spin = 0;
            state->ball_rotation = 0;
        } else if (c == 0x06) {
            start_evolution_mode(state);
            state->pinball_is_visible = 1;
            state->enable_ball_gravity_and_tilt = 1;
            state->rumble_pattern = 0x05;
            state->rumble_duration = 0x08;
        } else if (c == 0x03) {
            load_mini_ball_gfx(state);
        } else if (c == 0x00) {
            load_ball_gfx(state);
            state->ball_y_velocity = MAKE_FIXED(2, 0);
        }
    }
}

/* LoadCAVELightsGraphics_RedField (0x1522d)
 * Updates BG tilemap tiles for each of the 4 CAVE lights based on their state.
 * GBC mode uses TileDataPointers_1531d (lit) / TileDataPointers_15325 (unlit).
 * Each light writes a single tile ID to a specific BG map position. */
void load_cave_lights_graphics(GameState *state) {
    uint8_t *bg = state->vram->bg_map[0];
    /* Light C (index 0): BG map 0x121 */
    bg[0x121] = state->cave_light_states[0] ? 0x27 : 0x26;
    /* Light A (index 1): BG map 0x123 */
    bg[0x123] = state->cave_light_states[1] ? 0x29 : 0x28;
    /* Light V (index 2): BG map 0x130 */
    bg[0x130] = state->cave_light_states[2] ? 0x7E : 0x7C;
    /* Light E (index 3): BG map 0x132 */
    bg[0x132] = state->cave_light_states[3] ? 0x7F : 0x7D;
}

/* ResolveCAVELightCollision_RedField (0x151cb)
 *
 * ASM flow:
 *   1. If collision: clear flag, skip if blinking, toggle light → LoadGraphics
 *   2. If no collision (or blinking during collision): call UpdateCAVELightsBlinking
 *      - If blinking: animate blink → LoadGraphics
 *      - If not blinking: check flipper rotation → if rotated, LoadGraphics
 */
static void resolve_cave_lights(GameState *state) {
    if (state->which_cave_light) {
        uint8_t id = state->which_cave_light_id;
        state->which_cave_light = 0;

        /* ASM: if blinking, skip collision handling entirely */
        if (!state->cave_lights_blinking) {
            /* Toggle light on (id $0A-$0D → index 0-3) */
            uint8_t light_idx = id - 0x0A;
            if (light_idx < 4) {
                uint8_t prev = state->cave_light_states[light_idx];
                state->cave_light_states[light_idx] = 1;

                /* Only score if light was previously off */
                if (!prev) {
                    add_score_with_multiplier(state, state->config->scores.score_100);

                    /* Check all 4 lit */
                    if (state->cave_light_states[0] && state->cave_light_states[1] &&
                        state->cave_light_states[2] && state->cave_light_states[3]) {
                        state->cave_lights_blinking = 1;
                        state->cave_lights_blinking_frames_remaining = 0x80;
                        add_score_with_multiplier(state, state->config->scores.score_400);
                        /* ASM: lb de, $00, $09 / call PlaySoundEffect */
                        PLAY_SFX(state, "object_hit", 0x00, 0x09);
                        if (state->num_cave_completions < 100) state->num_cave_completions++;
                    }
                }
            }
            /* ASM: jr LoadCAVELightsGraphics_RedField (skip update_blinking) */
            load_cave_lights_graphics(state);
            return;
        }
        /* Blinking active during collision: fall through to update_blinking */
    }

    /* UpdateCAVELightsBlinking_RedField (0x15270) - runs every frame */
    if (state->cave_lights_blinking) {
        /* Blinking active: animate */
        state->cave_lights_blinking_frames_remaining--;
        if (state->cave_lights_blinking_frames_remaining == 0) {
            /* Blinking done: reset lights, open slot cave */
            state->cave_lights_blinking = 0;
            state->opened_slot_by_cave_lights = 1;
            state->frames_until_slot_cave_opens = 3;
            memset(state->cave_light_states, 0, 4);
        }

        /* Blink effect: toggle lights every 8 frames */
        if ((state->cave_lights_blinking_frames_remaining & 7) == 0) {
            uint8_t val = (state->cave_lights_blinking_frames_remaining >> 3) & 1;
            state->cave_light_states[0] = val;
            state->cave_light_states[1] = val;
            state->cave_light_states[2] = val;
            state->cave_light_states[3] = val;
        }
        /* ASM: always falls through to LoadCAVELightsGraphics when blinking */
        load_cave_lights_graphics(state);
        return;
    }

    /* Not blinking: flipper rotation runs EVERY FRAME (ASM .notBlinking) */
    if (joypad_is_key_pressed(state, &state->key_config_left_flipper)) {
        /* Rotate left: [0]→[3], [1]→[0], [2]→[1], [3]→[2] */
        uint8_t tmp = state->cave_light_states[0];
        state->cave_light_states[0] = state->cave_light_states[1];
        state->cave_light_states[1] = state->cave_light_states[2];
        state->cave_light_states[2] = state->cave_light_states[3];
        state->cave_light_states[3] = tmp;
        load_cave_lights_graphics(state);
        return;
    }
    if (joypad_is_key_pressed(state, &state->key_config_right_flipper)) {
        /* Rotate right: [3]→[0], [0]→[1], [1]→[2], [2]→[3] */
        uint8_t tmp = state->cave_light_states[3];
        state->cave_light_states[3] = state->cave_light_states[2];
        state->cave_light_states[2] = state->cave_light_states[1];
        state->cave_light_states[1] = state->cave_light_states[0];
        state->cave_light_states[0] = tmp;
        load_cave_lights_graphics(state);
        return;
    }
    /* No change: ASM returns with Z set (no graphics load) */
}

/* ResolveWildMonCollision_RedField (0x14795) */
static void resolve_wild_mon(GameState *state) {
    if (!state->wild_mon_collision)
        return;
    state->wild_mon_collision = 0;
    state->ball_hit_wild_mon = 1;
    /* ASM: lb de, $00, $06 / call PlaySoundEffect */
    PLAY_SFX(state, "bumper", 0x00, 0x06);
}

/* LoadBumpersGraphics_RedField (0x15fb8)
 * Loads BG tilemap tiles for bumper display state.
 * GBC mode uses TileData_16080 pointer table (4 entries):
 *   Entry 0: left unlit, Entry 1: left lit
 *   Entry 2: right unlit, Entry 3: right lit
 * wWhichBumperGfx: 0=left, 1=right, 0xFF=don't load */
void load_bumper_graphics(GameState *state) {
    if (state->which_bumper_gfx == 0xFF)
        return;
    uint8_t *bg = state->vram->bg_map[0];
    /* wWhichBumperGfx: 0=left, 1=right
     * LoadBumpersGraphics doubles it: 0→0 (left unlit), 1→2 (right unlit)
     * LightUpBumper enters with (idx*2+1): 0→1 (left lit), 1→3 (right lit)
     * After light-up expires, LoadBumpersGraphics reloads unlit state */
    uint8_t bumper = state->which_bumper_gfx;
    uint8_t lit = (state->bumper_light_up_duration > 0) ? 1 : 0;

    if (bumper == 0) {
        /* Left bumper: BG offsets 0x124, 0x144-0x145, 0x164-0x165, 0x185-0x186 */
        if (lit) {
            bg[0x124] = 0x31;
            bg[0x144] = 0x32; bg[0x145] = 0x33;
            bg[0x164] = 0x34; bg[0x165] = 0x35;
            bg[0x185] = 0x36; bg[0x186] = 0x37;
        } else {
            bg[0x124] = 0x2A;
            bg[0x144] = 0x2B; bg[0x145] = 0x2C;
            bg[0x164] = 0x2D; bg[0x165] = 0x2E;
            bg[0x185] = 0x2F; bg[0x186] = 0x30;
        }
    } else {
        /* Right bumper: BG offsets 0x12F, 0x14E-0x14F, 0x16E-0x16F, 0x18D-0x18E */
        if (lit) {
            bg[0x12F] = 0x31;
            bg[0x14E] = 0x33; bg[0x14F] = 0x32;
            bg[0x16E] = 0x35; bg[0x16F] = 0x34;
            bg[0x18D] = 0x37; bg[0x18E] = 0x36;
        } else {
            bg[0x12F] = 0x2A;
            bg[0x14E] = 0x2C; bg[0x14F] = 0x2B;
            bg[0x16E] = 0x2E; bg[0x16F] = 0x2D;
            bg[0x18D] = 0x30; bg[0x18E] = 0x2F;
        }
    }
}

/* ResolveBumpersCollision_RedField (0x15f86) + ApplyBumperCollision (0x15fda)
 * ASM order: LoadBumpersGraphics → LightUpBumper → clear flag → ApplyCollision */
static void resolve_bumpers(GameState *state) {
    if (state->which_bumper) {
        /* ASM: LoadBumpersGraphics FIRST (loads current/previous state) */
        load_bumper_graphics(state);

        /* ASM: LightUpBumper_RedField (0x15fa6) */
        uint8_t id = state->which_bumper_id;
        uint8_t bumper_idx = id - 0x06;
        state->bumper_light_up_duration = 0x10;
        state->which_bumper_gfx = bumper_idx;
        /* Load lit graphics immediately */
        load_bumper_graphics(state);

        /* ASM: clear flag AFTER graphics load */
        state->which_bumper = 0;

        /* ASM: ApplyBumperCollision_RedField (0x15fda) */
        state->flipper_y_force = 0x0200;
        state->flipper_collision = 0x80;
        state->rumble_pattern = 0xFF;
        state->rumble_duration = 3;

        /* Angle delta (ASM 0x15ffa) */
        if (bumper_idx < 2) {
            state->collision_normal_angle = (uint8_t)(
                (int)state->collision_normal_angle + bumper_angle_deltas[bumper_idx]);
        }

        /* ASM: lb de, $00, $0b / call PlaySoundEffect */
        PLAY_SFX(state, "cave_light", 0x00, 0x0B);
        /* ASM: bumpers award 0 points (force only, no score) */
        return;
    }

    /* Per-frame: decrement light-up duration, reload graphics when expired */
    if (state->bumper_light_up_duration > 0) {
        state->bumper_light_up_duration--;
        if (state->bumper_light_up_duration == 0) {
            load_bumper_graphics(state);  /* Reload unlit state */
        }
    }
}

/* _LoadDiglettGraphics (0x149d9) - GBC version
 * Writes 4 tiles (2x2) to BG map for Diglett animation frame.
 * GBC uses TileListDataPointers_14a83 (LoadTileLists format).
 * Index: 0=left frame0, 1=left frame1, 2=left hit,
 *        3=right frame0, 4=right frame1, 5=right hit */
void load_diglett_graphics(GameState *state, uint8_t gfx_index) {
    uint8_t *bg = state->vram->bg_map[0];
    switch (gfx_index) {
    case 0: /* Left Diglett, frame 0: TileListData_14aa1 */
        bg[0xA3] = 0x54; bg[0xA4] = 0x55;
        bg[0xC3] = 0x56; bg[0xC4] = 0x57;
        break;
    case 1: /* Left Diglett, frame 1: TileListData_14aaf */
        bg[0xA3] = 0x58; bg[0xA4] = 0x59;
        bg[0xC3] = 0x5A; bg[0xC4] = 0x5B;
        break;
    case 2: /* Left Diglett, hit frame: TileListData_14abd */
        bg[0xA3] = 0x5C; bg[0xA4] = 0x80;
        bg[0xC3] = 0x5D; bg[0xC4] = 0x80;
        break;
    case 3: /* Right Diglett, frame 0: TileListData_14acb (mirrored) */
        bg[0xAF] = 0x55; bg[0xB0] = 0x54;
        bg[0xCF] = 0x57; bg[0xD0] = 0x56;
        break;
    case 4: /* Right Diglett, frame 1: TileListData_14ad9 (mirrored) */
        bg[0xAF] = 0x59; bg[0xB0] = 0x58;
        bg[0xCF] = 0x5B; bg[0xD0] = 0x5A;
        break;
    case 5: /* Right Diglett, hit frame: TileListData_14ae7 (mirrored) */
        bg[0xAF] = 0x80; bg[0xB0] = 0x5C;
        bg[0xCF] = 0x80; bg[0xD0] = 0x5D;
        break;
    }
}

/* LoadDiglettNumberGraphics (0x149f5) - GBC version
 * Writes 10 tiles to BG map for Diglett hit counter display.
 * GBC uses TileListDataPointers_14c8d (LoadTileLists format).
 * Index: 0-3 = left (0/1/2/3 hits), 4-7 = right (0/1/2/3 hits) */
void load_diglett_number_graphics(GameState *state, uint8_t state_index) {
    uint8_t *bg = state->vram->bg_map[0];
    switch (state_index) {
    case 0: /* Left, 0 hits: TileListData_14cb5 */
        bg[0x80] = 0x06; bg[0x81] = 0x07;
        bg[0xA0] = 0x08; bg[0xA1] = 0x09; bg[0xA2] = 0x0A;
        bg[0xC0] = 0x0B; bg[0xC1] = 0x0C; bg[0xC2] = 0x0D;
        bg[0xE0] = 0x0E; bg[0xE1] = 0x0F;
        break;
    case 1: /* Left, 1 hit: TileListData_14ccf */
        bg[0x80] = 0x06; bg[0x81] = 0x07;
        bg[0xA0] = 0x10; bg[0xA1] = 0x11; bg[0xA2] = 0x0A;
        bg[0xC0] = 0x12; bg[0xC1] = 0x13; bg[0xC2] = 0x0D;
        bg[0xE0] = 0x14; bg[0xE1] = 0x15;
        break;
    case 2: /* Left, 2 hits: TileListData_14ce9 */
        bg[0x80] = 0x06; bg[0x81] = 0x07;
        bg[0xA0] = 0x10; bg[0xA1] = 0x16; bg[0xA2] = 0x17;
        bg[0xC0] = 0x12; bg[0xC1] = 0x18; bg[0xC2] = 0x19;
        bg[0xE0] = 0x14; bg[0xE1] = 0x15;
        break;
    case 3: /* Left, 3 hits: TileListData_14d03 */
        bg[0x80] = 0x1A; bg[0x81] = 0x1B;
        bg[0xA0] = 0x1C; bg[0xA1] = 0x1D; bg[0xA2] = 0x17;
        bg[0xC0] = 0x12; bg[0xC1] = 0x18; bg[0xC2] = 0x19;
        bg[0xE0] = 0x14; bg[0xE1] = 0x15;
        break;
    case 4: /* Right, 0 hits: TileListData_14d1d */
        bg[0x92] = 0x07; bg[0x93] = 0x06;
        bg[0xB1] = 0x0A; bg[0xB2] = 0x1E; bg[0xB3] = 0x08;
        bg[0xD1] = 0x22; bg[0xD2] = 0x0C; bg[0xD3] = 0x24;
        bg[0xF2] = 0x0F; bg[0xF3] = 0x0E;
        break;
    case 5: /* Right, 1 hit: TileListData_14d37 */
        bg[0x92] = 0x07; bg[0x93] = 0x06;
        bg[0xB1] = 0x0A; bg[0xB2] = 0x1F; bg[0xB3] = 0x10;
        bg[0xD1] = 0x22; bg[0xD2] = 0x13; bg[0xD3] = 0x25;
        bg[0xF2] = 0x15; bg[0xF3] = 0x14;
        break;
    case 6: /* Right, 2 hits: TileListData_14d51 */
        bg[0x92] = 0x07; bg[0x93] = 0x06;
        bg[0xB1] = 0x17; bg[0xB2] = 0x20; bg[0xB3] = 0x10;
        bg[0xD1] = 0x23; bg[0xD2] = 0x18; bg[0xD3] = 0x25;
        bg[0xF2] = 0x15; bg[0xF3] = 0x14;
        break;
    case 7: /* Right, 3 hits: TileListData_14d6b */
        bg[0x92] = 0x1B; bg[0x93] = 0x1A;
        bg[0xB1] = 0x17; bg[0xB2] = 0x21; bg[0xB3] = 0x1C;
        bg[0xD1] = 0x23; bg[0xD2] = 0x18; bg[0xD3] = 0x25;
        bg[0xF2] = 0x15; bg[0xF3] = 0x14;
        break;
    }
}

/* UpdateDiglettAnimations (0x14990)
 * Idle animation: when controller == 0, toggle frame every ~20 frames.
 * [M01] ASM gates on Func_1130 (graphics queue caught up), NOT Random()&3.
 * C loads graphics synchronously, so the queue is always caught up — no gate needed. */
static void update_diglett_animations(GameState *state) {
    /* Left Diglett idle animation */
    if (state->left_diglett_anim_controller == 0) {
        if (state->left_map_move_diglett_anim_counter > 0) {
            state->left_map_move_diglett_anim_counter--;
        } else {
            state->left_map_move_diglett_anim_counter = 0x14; /* 20 frames */
            state->left_map_move_diglett_frame ^= 1;
            load_diglett_graphics(state, state->left_map_move_diglett_frame);
        }
    }

    /* Right Diglett idle animation */
    if (state->right_diglett_anim_controller == 0) {
        if (state->right_map_move_diglett_anim_counter > 0) {
            state->right_map_move_diglett_anim_counter--;
        } else {
            state->right_map_move_diglett_anim_counter = 0x14;
            state->right_map_move_diglett_frame ^= 1;
            load_diglett_graphics(state, state->right_map_move_diglett_frame + 3);
        }
    }
}

/* AddScoreForHittingDiglett (0x1488f) */
static void add_score_for_hitting_diglett(GameState *state) {
    add_score_with_multiplier(state, state->config->scores.score_500);
    state->collision_force_amplification = 2;
    state->rumble_pattern = 0x55;
    state->rumble_duration = 4;
    /* ASM: lb de, $00, $0f / call PlaySoundEffect */
    PLAY_SFX(state, "ball_drain", 0x00, 0x0F);
}

/* ResolveDiglettCollision (0x147aa) - matches ASM exactly.
 * If collision: handle hit, update collision map attrs, load graphics, RETURN.
 * If no collision: decrement controllers, restore attrs when expired,
 *                  then call UpdateDiglettAnimations. */
static void resolve_diglett(GameState *state) {
    if (state->which_diglett) {
        uint8_t id = state->which_diglett_id;
        state->which_diglett = 0;

        /* ASM: c = (id - 1) * 2, selects left(0) or right(2) */
        uint8_t side = id - 1; /* 0=left, 1=right */
        uint8_t c = side * 2;

        uint8_t *counter = (side == 0) ?
            &state->left_map_move_counter : &state->right_map_move_counter;
        uint16_t *decay_timer = (side == 0) ?
            &state->left_map_move_counter_frames_until_decrease :
            &state->right_map_move_counter_frames_until_decrease;
        uint8_t *anim_ctrl = (side == 0) ?
            &state->left_diglett_anim_controller :
            &state->right_diglett_anim_controller;

        /* Already at max (3)? Skip to animation-only path */
        if (*counter >= 3)
            goto no_collision;

        (*counter)++;
        *anim_ctrl = 0x50; /* 80 frames hit animation */
        *decay_timer = MAP_MOVE_FRAMES_COUNTER;

        if (side == 1) {
            /* Right Diglett: update collision map attrs to hit state */
            state->stage_collision_map[0xF0] = 0x6A;
            state->stage_collision_map[0x110] = 0x6B;
            /* Load hit frame graphics (index 5) */
            load_diglett_graphics(state, 5);
            /* Load right counter number graphics */
            load_diglett_number_graphics(state, state->right_map_move_counter + 4);
            check_special_mode_collision(state, 8); /* SPECIAL_COLLISION_RIGHT_DIGLETT */
            if (state->right_map_move_counter >= 3) {
                /* HitRightDiglett3Times (0x14920) */
                if (increment_max100(&state->num_dugtrio_triples)) {
                    if ((state->num_dugtrio_triples % 10) == 0)
                        add_extra_ball(state);
                }
                state->map_move_direction = 1;
                start_map_move_mode(state);
            }
        } else {
            /* Left Diglett: update collision map attrs to hit state */
            state->stage_collision_map[0xE3] = 0x66;
            state->stage_collision_map[0x103] = 0x67;
            /* Load hit frame graphics (index 2) */
            load_diglett_graphics(state, 2);
            /* Load left counter number graphics */
            load_diglett_number_graphics(state, state->left_map_move_counter);
            check_special_mode_collision(state, 7); /* SPECIAL_COLLISION_LEFT_DIGLETT */
            if (state->left_map_move_counter >= 3) {
                /* HitLeftDiglett3Times (0x14947) */
                if (increment_max100(&state->num_dugtrio_triples)) {
                    if ((state->num_dugtrio_triples % 10) == 0)
                        add_extra_ball(state);
                }
                state->map_move_direction = 0;
                start_map_move_mode(state);
            }
        }

        add_score_for_hitting_diglett(state);
        return; /* ASM returns here, does NOT fall through to animation */
    }

no_collision:
    /* .asm_14834: No collision path - decrement controllers, restore attrs */

    /* Left Diglett controller */
    if (state->left_diglett_anim_controller > 0) {
        state->left_diglett_anim_controller--;
        if (state->left_diglett_anim_controller == 0) {
            /* Controller expired: check if counter was 3 → reset to 0 */
            if (state->left_map_move_counter == 3) {
                state->left_map_move_counter = 0;
                load_diglett_number_graphics(state, 0);
            }
            /* Restore default collision map attributes */
            state->stage_collision_map[0xE3] = 0x64;
            state->stage_collision_map[0x103] = 0x65;
        }
    }

    /* Right Diglett controller */
    if (state->right_diglett_anim_controller > 0) {
        state->right_diglett_anim_controller--;
        if (state->right_diglett_anim_controller == 0) {
            if (state->right_map_move_counter == 3) {
                state->right_map_move_counter = 0;
                load_diglett_number_graphics(state, 4);
            }
            /* Restore default collision map attributes */
            state->stage_collision_map[0xF0] = 0x68;
            state->stage_collision_map[0x110] = 0x69;
        }
    }

    /* UpdateDiglettAnimations: idle frame toggle (only when controller == 0) */
    update_diglett_animations(state);
}

/* Update map move counter decay (called from resolve dispatch).
 * ASM: UpdateMapMoveCounters_RedFieldBottom (0x14880) /
 *      UpdateMapMoveCounters_RedFieldTop    (0x148cf).
 * Timer counts down from MAP_MOVE_FRAMES_COUNTER (480).  When it expires
 * it resets to 480 and decrements the map move counter (if 1 or 2).
 * Top stage skips if timer is already 0; bottom stage also reloads
 * Diglett number graphics on counter decrease. */
static void update_map_move_counters(GameState *state) {
    /* Left counter decay */
    if (state->left_map_move_counter_frames_until_decrease > 0) {
        state->left_map_move_counter_frames_until_decrease--;
        if (state->left_map_move_counter_frames_until_decrease == 0) {
            state->left_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;
            if (state->left_map_move_counter > 0 && state->left_map_move_counter < 3) {
                state->left_map_move_counter--;
                /* Bottom stage: reload diglett number graphics */
                if (state->current_stage & 1) {
                    load_diglett_number_graphics(state, state->left_map_move_counter);
                }
            }
        }
    }
    /* Right counter decay */
    if (state->right_map_move_counter_frames_until_decrease > 0) {
        state->right_map_move_counter_frames_until_decrease--;
        if (state->right_map_move_counter_frames_until_decrease == 0) {
            state->right_map_move_counter_frames_until_decrease = MAP_MOVE_FRAMES_COUNTER;
            if (state->right_map_move_counter > 0 && state->right_map_move_counter < 3) {
                state->right_map_move_counter--;
                /* Bottom stage: reload right counter graphics (index = counter + 4) */
                if (state->current_stage & 1) {
                    load_diglett_number_graphics(state, state->right_map_move_counter + 4);
                }
            }
        }
    }
}

/* ResolveRedStagePinballLaunchCollision (0x1652d) */
static void resolve_launch_alley(GameState *state) {
    /* ResolveRedStagePinballLaunchCollision (0x1652d) */
    if (!state->pinball_launch_collision)
        return;
    state->pinball_launch_collision = 0;

    if (state->pinball_launched) {
        /* === Launched: apply launch velocity === */
        state->right_alley_trigger = 0;
        state->left_alley_trigger = 0;
        memset(state->secondary_left_alley_trigger, 0, 2);
        state->ball_x_velocity = 0;
        state->ball_y_velocity = (int16_t)0xFA80;  /* -5.5 */
        state->ball_spin = 0;
        state->ball_rotation = 0;
        state->enable_ball_gravity_and_tilt = 1;
        PLAY_SFX(state, "pikachu_charge", 0x00, 0x0A);
        /* Fall through to .notLaunchedYet */
    }

    /* .notLaunchedYet: previous_triggered_game_object = $FF UNCONDITIONALLY */
    state->previous_triggered_game_object = 0xFF;

    /* If already launched (velocity was just applied above), done */
    if (state->pinball_launched)
        return;

    /* Not yet launched: ChooseInitialMap or button press check */
    if (!state->chose_initial_map) {
        /* ChooseInitialMap_RedField (0x1658f) — non-blocking state machine.
         * ASM blocks in a loop cycling maps every 32 frames on the billboard.
         * C: map_cycling_frames counts down; 0 triggers next map display. */
        if (state->map_cycling_frames == 0) {
            /* Timer expired or first entry: show next map */
            load_grey_billboard_palette(state);

            /* Advance index (ASM increments THEN wraps at 7) */
            uint8_t idx = state->initial_map_selection_index + 1;
            if (idx >= 7) idx = 0;
            state->initial_map_selection_index = idx;
            state->current_map = red_stage_initial_maps[idx];

            /* Play cycling SFX and load billboard picture */
            PLAY_SFX(state, "pikachu_full_charge", 0x00, 0x48);
            load_billboard_picture(state,
                (uint8_t)(BILLBOARD_PALLET_TOWN_PIC + state->current_map));
            state->map_cycling_frames = 32;
            return;
        }

        /* Check A button to finalize selection */
        if (joypad_is_key_pressed(state, &state->key_config_ball_start)) {
            /* .ballStartKeyPressed: finalize map selection */
            load_map_billboard_tile_data(state);
            load_scrolling_map_name_text(state, 0); /* "START FROM [map]" */
            state->visited_maps[0] = state->current_map;
            state->num_map_moves = 0;
            state->chose_initial_map = 1;
            /* ASM sets pinball_launched=1 immediately after map selection */
            state->pinball_launched = 1;
            return;
        } else {
            state->map_cycling_frames--;
            return;
        }
    }

    /* .checkPressedKeysToLaunchBall: wait for A button to launch */
    if (joypad_is_key_pressed(state, &state->key_config_ball_start)) {
        state->pinball_launched = 1;
    }
}

/*=============================================================================
 * GetBCDForNextBonusMultiplier_RedField (0x16f95)
 * Computes tens/ones BCD digits from cur_bonus_multiplier + 1.
 * ASM uses DAA-based binary→BCD conversion; C uses simple division.
 *===========================================================================*/
static void get_bcd_for_next_bonus_multiplier(GameState *state) {
    uint8_t val = state->cur_bonus_multiplier + 1;
    if (val > MAX_BONUS_MULTIPLIER) val = MAX_BONUS_MULTIPLIER;
    state->bonus_multiplier_tens_digit = val / 10;
    state->bonus_multiplier_ones_digit = val % 10;
}

/*=============================================================================
 * AddExtraBall (0x30164)
 * Increments extra_balls or awards 10,000,000 points if maxed.
 *===========================================================================*/
void add_extra_ball(GameState *state) {
    uint8_t balls = state->extra_balls + 1;
    if (balls > MAX_EXTRA_BALLS) {
        /* Maxed — award 10 million points instead */
        add_score_no_multiplier(state, state->config->scores.score_10000000);
        state->show_extra_ball_text = 2;
    } else {
        state->extra_balls = balls;
        state->show_extra_ball_text = 1;
    }
}

/*=============================================================================
 * _LoadBonusMultiplierRailingGraphics_RedField (0x16f28) — GBC path
 *
 * Writes digit tile IDs to BG tilemap for the railing display.
 * Input a: digit value (0-9 for tens, 0x14-0x1D for ones), bit 7 = lit flag.
 *
 * GBC tile layout (from BonusMultiplierRailingTileDataPointers_17228):
 *   Tens unlit: tiles $40+digit*2 / $41+digit*2 at bg_map[0x04] / bg_map[0x24]
 *   Tens lit:   tiles $2C+digit*2 / $2D+digit*2 at bg_map[0x04] / bg_map[0x24]
 *   Ones unlit: tiles $68+digit*2 / $69+digit*2 at bg_map[0x0F] / bg_map[0x2F]
 *   Ones lit:   tiles $54+digit*2 / $55+digit*2 at bg_map[0x0F] / bg_map[0x2F]
 *===========================================================================*/
void load_bonus_mult_railing_gfx(GameState *state, uint8_t a) {
    if (!state->vram) return;
    uint8_t lit = (a & 0x80) ? 1 : 0;
    a &= 0x7F;

    if (a < 0x14) {
        /* Tens digit: position bg_map[0x04] and bg_map[0x24] */
        uint8_t digit = a;
        uint8_t tile_top, tile_bot;
        if (lit) {
            tile_top = (uint8_t)(0x2C + digit * 2);
            tile_bot = (uint8_t)(0x2D + digit * 2);
        } else {
            tile_top = (uint8_t)(0x40 + digit * 2);
            tile_bot = (uint8_t)(0x41 + digit * 2);
        }
        state->vram->bg_map[0][0x04] = tile_top;
        state->vram->bg_map[0][0x24] = tile_bot;
    } else {
        /* Ones digit: position bg_map[0x0F] and bg_map[0x2F] */
        uint8_t digit = a - 0x14;
        uint8_t tile_top, tile_bot;
        if (lit) {
            tile_top = (uint8_t)(0x54 + digit * 2);
            tile_bot = (uint8_t)(0x55 + digit * 2);
        } else {
            tile_top = (uint8_t)(0x68 + digit * 2);
            tile_bot = (uint8_t)(0x69 + digit * 2);
        }
        state->vram->bg_map[0][0x0F] = tile_top;
        state->vram->bg_map[0][0x2F] = tile_bot;
    }
}

/*=============================================================================
 * ShowBonusMultiplierMessage_RedField (0x16ef5)
 * Shows "BONUS MULTIPLIER xNN" scrolling text when flag is set.
 *===========================================================================*/
static void show_bonus_multiplier_message(GameState *state) {
    if (state->bottom_text_enabled)
        return;
    if (!state->show_bonus_multiplier_bottom_message)
        return;
    state->show_bonus_multiplier_bottom_message = 0;

    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    load_scrolling_text(state, 0, BONUS_MULT_TEXT_HEADER,
                        "BONUS MULTIPLIER x0  ");

    /* Write actual digits at buffer position $12 (18).
     * Tens digit: leading-zero suppression (skip if 0). */
    uint8_t tens = state->wd614 & 0x7F;
    uint8_t ones = state->wd615 & 0x7F;
    if (tens != 0) {
        state->bottom_message_text[18] = (uint8_t)(0x86 + tens);
        state->bottom_message_text[19] = (uint8_t)(0x86 + ones);
    } else {
        state->bottom_message_text[18] = (uint8_t)(0x86 + ones);
    }
}

/*=============================================================================
 * UpdateBonusMultiplierRailing_RedField (0x16e51)
 * Per-frame handler: timer countdown, blinking animation, digit reload.
 *===========================================================================*/
static void update_bonus_multiplier_railing(GameState *state) {
    show_bonus_multiplier_message(state);

    /* Timer countdown from wd612 */
    if (state->wd612) {
        state->wd612--;
        if (state->wd612 == 0x70) {
            /* Transition to fast blink state */
            state->wd610 = 2;
            state->wd611 = 2;
        } else if (state->wd612 == 0) {
            /* Timer expired: return to idle, recalculate BCD digits */
            state->wd610 = 3;
            state->wd611 = 0;
            get_bcd_for_next_bonus_multiplier(state);
            load_bonus_mult_railing_gfx(state, state->bonus_multiplier_tens_digit);
            load_bonus_mult_railing_gfx(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
            return;
        }
    }

    /* Tens digit blinking (wd610 >= 2) */
    if (state->wd610 >= 2) {
        uint8_t b = state->hram.frame_counter;
        if (state->wd610 >= 3) {
            /* wd610>=3: srl a twice → slower blink */
            b >>= 2;
        }
        /* wd610==2: use frame_counter directly → faster blink */
        if ((b & 3) == 0) {
            if (b & 8) {
                state->bonus_multiplier_tens_digit |= 0x80;
            } else {
                state->bonus_multiplier_tens_digit &= 0x7F;
            }
            load_bonus_mult_railing_gfx(state, state->bonus_multiplier_tens_digit);
        }
    }

    /* Ones digit blinking (wd611 >= 2) */
    if (state->wd611 >= 2) {
        uint8_t b = state->hram.frame_counter;
        if (state->wd611 >= 3) {
            /* wd611>=3: srl a twice → slower blink */
            b >>= 2;
        }
        /* wd611==2: use frame_counter directly → faster blink */
        if ((b & 3) == 0) {
            if (b & 8) {
                state->bonus_multiplier_ones_digit |= 0x80;
            } else {
                state->bonus_multiplier_ones_digit &= 0x7F;
            }
            load_bonus_mult_railing_gfx(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
        }
    }
}

/*=============================================================================
 * ResolveRedStageBonusMultiplierCollision (0x16d9d)
 * Full state machine with left/right railing detection,
 * wd610/wd611/wd612 blinking system, and multiplier increment.
 *===========================================================================*/
static void resolve_bonus_multiplier(GameState *state) {
    if (!state->which_bonus_multiplier_railing) {
        /* No collision — run per-frame update */
        update_bonus_multiplier_railing(state);
        return;
    }

    /* Collision occurred */
    state->which_bonus_multiplier_railing = 0;
    PLAY_SFX(state, "ball_saver", 0x00, 0x0D);

    uint8_t railing_id = state->which_bonus_multiplier_railing_id;
    if (railing_id == 0x21) {
        /* Left railing hit */
        check_special_mode_collision(state, 9);  /* SPECIAL_COLLISION_LEFT_BONUS_MULTIPLIER */
        if (state->wd610 == 3) {
            state->wd610 = 1;
            state->wd611 = 3;
            state->bonus_multiplier_tens_digit |= 0x80;
        }
    } else {
        /* Right railing hit (id == 0x22) */
        check_special_mode_collision(state, 10); /* SPECIAL_COLLISION_RIGHT_BONUS_MULTIPLIER */
        if (state->wd611 == 3) {
            state->wd610 = 1;
            state->wd611 = 1;
            state->wd612 = 0x80;
            state->bonus_multiplier_ones_digit |= 0x80;

            /* Increment multiplier */
            uint8_t mult = state->cur_bonus_multiplier + 1;
            uint8_t capped = 0;
            if (mult > MAX_BONUS_MULTIPLIER) {
                mult = MAX_BONUS_MULTIPLIER;
                capped = 1;
            }
            state->cur_bonus_multiplier = mult;

            if (!capped) {
                /* Check for extra ball every 25 multipliers */
                if ((mult % 25) == 0) {
                    add_extra_ball(state);
                }
            }

            /* Save digit backups for bottom message */
            state->wd614 = state->bonus_multiplier_tens_digit;
            state->wd615 = state->bonus_multiplier_ones_digit;
            state->show_bonus_multiplier_bottom_message = 1;
        }
    }

    /* Common: add 10 points + reload railing graphics */
    add_score_with_multiplier(state, state->config->scores.score_10);
    load_bonus_mult_railing_gfx(state, state->bonus_multiplier_tens_digit);
    load_bonus_mult_railing_gfx(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
}

/*=============================================================================
 * Slot Roulette State Machine
 *
 * ASM DoSlotRewardRoulette (0xed8e) is a blocking function. In C, we
 * implement it as a non-blocking state machine that runs one step per frame.
 *===========================================================================*/
/* Slot roulette state enum is in red_field.h */

/*=============================================================================
 * ConvertSlotRewardBillboardPicture (0xeef9)
 *
 * Converts generic billboard IDs to specific ones based on game state:
 *   EVOLUTION_MODE → CATCHEM_MODE if no pokemon caught
 *   GREAT_BALL → +slot_ball_increase (GREAT/ULTRA/MASTER)
 *   GENGAR_BONUS → +next_bonus_stage (GENGAR/MEWTWO/MEOWTH/DIGLETT/SEEL)
 *===========================================================================*/
static uint8_t convert_slot_reward_billboard_picture(GameState *state, uint8_t id) {
    if (id == BILLBOARD_EVOLUTION_MODE) {
        return state->slot_any_pokemon_caught ? BILLBOARD_EVOLUTION_MODE : BILLBOARD_CATCHEM_MODE;
    }
    if (id == BILLBOARD_GREAT_BALL) {
        return (uint8_t)(id + state->slot_ball_increase);
    }
    if (id == BILLBOARD_GENGAR_BONUS) {
        return (uint8_t)(id + state->next_bonus_stage);
    }
    return id;
}

/*=============================================================================
 * ShowScrollingGoToBonusText_RedField (0x163f2)
 *
 * Displays "GO TO xxx STAGE" scrolling text and plays SFX.
 *===========================================================================*/
static void show_scrolling_go_to_bonus_text(GameState *state) {
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);

    const char *text;
    uint8_t header[6];
    uint8_t next = state->next_stage;

    /* ASM only checks Diglett and Gengar; everything else defaults to Mewtwo text */
    if (next == STAGE_DIGLETT_BONUS) {
        text = "GO TO DIGLETT STAGE";
        /* scrolling_text_normal 0, 20, 0, 20 */
        header[0]=5; header[1]=0x54; header[2]=0x40; header[3]=20; header[4]=0; header[5]=60;
    } else if (next == STAGE_GENGAR_BONUS) {
        text = "GO TO GENGAR STAGE";
        /* scrolling_text_normal 1, 20, 0, 20 */
        header[0]=5; header[1]=0x54; header[2]=0x41; header[3]=20; header[4]=0; header[5]=59;
    } else {
        text = "GO TO MEWTWO STAGE";
        /* scrolling_text_normal 1, 20, 0, 20 */
        header[0]=5; header[1]=0x54; header[2]=0x41; header[3]=20; header[4]=0; header[5]=59;
    }

    load_scrolling_text(state, 2, header, text);

    /* Stop music, play go-to-bonus SFX */
    PLAY_MUSIC(state, "nothing", 0, 0);
    PLAY_SFX(state, "bonus_stage_enter", 0x3C, 0x23);
}

/*=============================================================================
 * Slot reward handler helpers
 *===========================================================================*/
static void start_saver_timer(GameState *state, uint8_t seconds) {
    state->ball_saver_icon_on = 0;
    state->ball_saver_flash_rate = 0xFF;
    state->ball_saver_timer_frames = 59;
    state->ball_saver_timer_seconds = seconds;
    state->num_times_ball_saved_text_will_display = 2;
}

static void slot_reward_dispatch(GameState *state, uint8_t reward) {
    switch (reward) {
    case BILLBOARD_BALL_SAVER_30:
        start_saver_timer(state, 30);
        break;
    case BILLBOARD_BALL_SAVER_60:
        start_saver_timer(state, 60);
        break;
    case BILLBOARD_BALL_SAVER_90:
        start_saver_timer(state, 90);
        break;
    case BILLBOARD_PIKACHU_SAVER:
        state->pikachu_saver_slot_reward_active = 1;
        state->pikachu_saver_charge = MAX_PIKACHU_SAVER_CHARGE;
        /* ASM (SlotRewardPikachuSaver, 0xef83): disables audio engine, calls
         * PlayPikachuSoundClip(a=$0) at 0x50000, then re-enables audio.
         * This is the best we can do — the original uses raw GBC wave channel
         * PCM streaming via NR32 volume modulation which can't be reproduced
         * through the bytecode SFX system. */
        audio_play_pcm(state->audio, 0);
        break;
    case BILLBOARD_EXTRA_BALL:
        add_extra_ball(state);
        break;
    case BILLBOARD_SMALL_REWARD:
    case BILLBOARD_BIG_REWARD:
        /* Random index picked here; score added in slot_reward_finish() after
         * the 128-frame blink animation. ASM: SmallPoints=$10-$80 in low word,
         * BigPoints=$10-$80 in high word. */
        state->cur_slot_bonus = random_range(state, 8);
        break;
    case BILLBOARD_CATCHEM_MODE:
        state->catchem_or_evolution_slot_reward_active = CATCHEM_MODE_SLOT_REWARD;
        break;
    case BILLBOARD_EVOLUTION_MODE:
        state->catchem_or_evolution_slot_reward_active = EVOLUTION_MODE_SLOT_REWARD;
        break;
    case BILLBOARD_GREAT_BALL:
    case BILLBOARD_ULTRA_BALL:
    case BILLBOARD_MASTER_BALL: {
        /* Upgrade ball type + reset degradation timer */
        state->ball_type_counter = (uint16_t)(0x10 | (0x0E << 8));
        if (state->ball_type == MASTER_BALL) {
            /* Already master: 1M points + SFX + DigitsText1to8 in slot 1.
             * ASM: push bc=$0100/de=$0000, Func_32cc for slot 2 (wScrollingText2),
             * LoadScrollingText for slot 1 (wScrollingText1). */
            PLAY_SFX(state, "slot_start", 0x0F, 0x4D);
            add_score_no_multiplier(state, state->config->scores.score_1000000);
            static const uint8_t bcd_1m_slot[4] = { 0x01, 0x00, 0x00, 0x00 };
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text_with_bcd(state, 1, DIGITS_1_8_HEADER, bcd_1m_slot);
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER,
                                "FIELD MULTIPLIER SPECIAL BONUS");
        } else {
            PLAY_SFX(state, "slot_reel_stop", 0x06, 0x3A);
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 0, FIELD_MULT_HEADER,
                                "FIELD MULTIPLIER x0");
            uint8_t new_type = BallTypeProgressionRedField[state->ball_type];
            state->ball_type = new_type;
            state->bottom_message_text[18] = (uint8_t)(0x86 + new_type);
        }
        /* TransitionPinballUpgradePalette: reload ball GFX */
        load_ball_gfx(state);
        break;
    }
    case BILLBOARD_BONUS_MULTIPLIER:
        /* Random index picked here; multiplier added in slot_reward_finish()
         * after the 128-frame blink animation. */
        state->cur_slot_bonus = random_range(state, 4);
        break;
    default:
        /* BILLBOARD_GENGAR_BONUS..SEEL_BONUS: go-to-bonus handled in caller */
        if (reward >= BILLBOARD_GENGAR_BONUS && reward <= BILLBOARD_SEEL_BONUS)
            state->bonus_stage_slot_reward_active = 1;
        break;
    }
}

/*=============================================================================
 * Slot reward finish — called after 128-frame blink for animated rewards.
 * ASM: SmallPoints/BigPoints/BonusMultiplier add score/mult AFTER blink.
 *===========================================================================*/
static void slot_reward_finish(GameState *state, uint8_t reward) {
    switch (reward) {
    case BILLBOARD_SMALL_REWARD: {
        /* ASM: (rand(8)+1) * $10 → 100-800 BCD points */
        uint8_t r = state->cur_slot_bonus;
        uint8_t bcd_lo = (uint8_t)((r + 1) << 4);
        uint8_t score[4] = {0, bcd_lo, 0, 0};
        add_score_no_multiplier(state, score);
        break;
    }
    case BILLBOARD_BIG_REWARD: {
        /* ASM: (rand(8)+1) * $10 → 1M-8M BCD points */
        uint8_t r = state->cur_slot_bonus;
        uint8_t bcd_mid = (uint8_t)((r + 1) << 4);
        uint8_t score[4] = {0, 0, 0, bcd_mid};
        add_score_no_multiplier(state, score);
        break;
    }
    case BILLBOARD_BONUS_MULTIPLIER: {
        /* ASM: Add 1-4 to bonus multiplier, extra ball at 25/50/75 thresholds */
        uint8_t r = state->cur_slot_bonus;
        state->wd610 = 3;
        state->wd611 = 0;
        state->wd612 = 0;
        uint8_t old_div = 0;
        { uint8_t v = state->cur_bonus_multiplier; while (v >= 25) { v -= 25; old_div++; } }
        uint8_t new_mult = state->cur_bonus_multiplier + r + 1;
        if (new_mult >= 100) new_mult = 99;
        state->cur_bonus_multiplier = new_mult;
        uint8_t new_div = 0;
        { uint8_t v = new_mult; while (v >= 25) { v -= 25; new_div++; } }
        if (new_div != old_div)
            add_extra_ball(state);
        get_bcd_for_next_bonus_multiplier(state);
        load_bonus_mult_railing_gfx(state, state->bonus_multiplier_tens_digit);
        load_bonus_mult_railing_gfx(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
        break;
    }
    default:
        break;
    }
}

/*=============================================================================
 * Go to bonus stage — extracted helper from DoSlotLogic_RedField.
 *===========================================================================*/
static void slot_goto_bonus_stage(GameState *state) {
    if (!state->bonus_stage_slot_reward_active) {
        /* Triggered by pokeballs — clear them + start blink animation */
        state->num_pokeballs = 0;
        state->pokeball_blinking_counter = 0x40;
    }
    state->bonus_stage_slot_reward_active = 0;
    state->going_to_bonus_stage = 1;
    state->move_to_next_screen_state = 1;

    /* Look up bonus stage from next_bonus_stage index */
    uint8_t idx = state->next_bonus_stage;
    if (idx >= 5) idx = 4;
    state->next_stage = BonusStages_RedField[idx];

    show_scrolling_go_to_bonus_text(state);

    state->opened_slot_by_pokeballs = 0;
    state->catchem_or_evolution_slot_reward_active = 0;
    state->frames_until_slot_cave_opens = 30;
}

/*=============================================================================
 * StartSlotRoulette — called at slot counter 0x09.
 *
 * Handles the initial checks (special mode, pokeballs) then starts the
 * non-blocking roulette state machine.
 *===========================================================================*/
void start_slot_roulette(GameState *state) {
    /* Clear slot indicator */
    state->indicator_states[4] = 0;

    /* ASM: CheckSpecialModeColision with SLOT_HOLE (13). */
    check_special_mode_collision(state, 13);
    if (state->in_special_mode) {
        if (state->special_mode == 1 && state->special_mode_state >= 3) {
            /* Evolution completing: ASM blocks via MainLoopUntilTextIsClear
             * for text/jackpot. In C we handle it async — halt the slot exit
             * counter and keep ball hidden until state 5 releases it. */
            state->slot_enter_or_exit_counter = 0;
        } else {
            /* Catch'em / map move: restore ball, let counter continue */
            state->pinball_is_visible = 1;
            state->enable_ball_gravity_and_tilt = 1;
        }
        return;
    }

    /* Check if 3 pokeballs → go directly to bonus stage (no roulette) */
    if (state->previous_num_pokeballs == 3 && state->frames_until_slot_cave_opens == 0) {
        slot_goto_bonus_stage(state);
        return;
    }

    /* Initialize roulette state (ASM: DoSlotRewardRoulette lines 2-14) */
    state->rumble_pattern = 0;
    state->rumble_duration = 0;
    state->catchem_or_evolution_slot_reward_active = 0;
    state->slot_any_pokemon_caught = state->num_party_mons;
    state->slot_ball_increase = BallTypeIncreases[state->ball_type < 6 ? state->ball_type : 0];

    /* Start the roulette state machine */
    state->slot_roulette_active = 1;
    state->slot_roulette_state = SLOT_ROULETTE_WAIT_FLIPPERS;
}

/*=============================================================================
 * UpdateSlotRoulette — non-blocking state machine for DoSlotRewardRoulette.
 *
 * Called once per frame while slot_roulette_active is set. The slot counter
 * (slot_enter_or_exit_counter) is paused at 0x09 during the roulette.
 *
 * ASM flow: wait flippers → spin entries (with Delay1Frame) → stopped display
 * (40 frames) → load colored palette → blink (128 frames) → advance progress
 * → dispatch reward → [animated reward blink (128 frames)] → done.
 *===========================================================================*/
void update_slot_roulette(GameState *state) {
    bool continue_processing = true;
    while (continue_processing) {
        continue_processing = false;
        switch (state->slot_roulette_state) {

        case SLOT_ROULETTE_WAIT_FLIPPERS:
            /* ASM clears all input each frame so flippers return to rest.
             * In C, suppress flipper input bits so flippers release. */
            state->hram.joypad_state &= (uint8_t)~FLIPPERS;
            state->hram.newly_pressed_buttons &= (uint8_t)~FLIPPERS;
            state->hram.pressed_buttons &= (uint8_t)~FLIPPERS;
            /* Check both flippers at rest (high byte of 16-bit state = 0) */
            if ((state->left_flipper_state >> 8) == 0 &&
                (state->right_flipper_state >> 8) == 0) {
                state->slot_roulette_state = SLOT_ROULETTE_INIT;
                continue_processing = true; /* Don't consume a frame */
            }
            break;

        case SLOT_ROULETTE_INIT:
            /* ASM: LoadGreyBillboardPaletteData, random permutation row */
            load_grey_billboard_palette(state);
            state->cur_slot_reward_roulette_index = gen_random(state) & 0xF0;
            state->slot_roulette_counter = 0;
            state->slot_roulette_slowed = 0;
            state->slot_roulette_state = SLOT_ROULETTE_SPIN_ENTRY;
            continue_processing = true; /* Don't consume a frame */
            break;

        case SLOT_ROULETTE_SPIN_ENTRY: {
            /* Find next valid permutation entry, skipping $80+ (empty) */
            uint8_t idx = state->cur_slot_reward_roulette_index;
            int safety = 0;
            while (safety < 16) {
                safety++;
                uint8_t perm_offset = SlotRewardRoulettePermutations[idx];
                uint16_t set_offset = (uint16_t)state->slot_reward_progress + perm_offset;
                uint8_t reward_id = (set_offset < 250) ? SlotRewardSets[set_offset] : 0xFF;
                if (!(reward_id & 0x80)) {
                    /* Valid entry found */
                    uint8_t pic = convert_slot_reward_billboard_picture(state, reward_id);
                    state->slot_roulette_billboard_picture = pic;
                    PLAY_SFX(state, "object_hit", 0x00, 0x09);
                    load_billboard_off_picture(state, pic);
                    /* Delay = max(counter, 10) frames */
                    state->slot_roulette_anim_counter =
                        (state->slot_roulette_counter >= 10) ? state->slot_roulette_counter : 10;
                    state->cur_slot_reward_roulette_index = idx;
                    state->slot_roulette_state = SLOT_ROULETTE_SPIN_DELAY;
                    /* Don't consume frame — SPIN_DELAY's first tick is this frame */
                    continue_processing = true;
                    break;
                }
                /* Empty entry: advance index (low nibble wraps 0-15) */
                idx = (idx & 0xF0) | ((idx + 1) & 0x0F);
            }
            if (state->slot_roulette_state == SLOT_ROULETTE_SPIN_ENTRY) {
                /* Safety: all entries empty — shouldn't happen, force stop */
                state->slot_roulette_billboard_picture = BILLBOARD_SMALL_REWARD;
                state->slot_roulette_state = SLOT_ROULETTE_STOPPED_SFX;
                continue_processing = true;
            }
            break;
        }

        case SLOT_ROULETTE_SPIN_DELAY:
            /* ASM: Delay1Frame loop. Check flipper for slowdown. */
            if (!state->slot_roulette_slowed) {
                if (state->hram.newly_pressed_buttons & FLIPPERS) {
                    state->slot_roulette_slowed = 1;
                    state->slot_roulette_counter = 50;
                    PLAY_SFX(state, "evo_trinket", 0x07, 0x28);
                }
            }
            /* ASM Delay1Frame is a CPU busy-wait (~3597 M-cycles ≈ 3.43ms),
             * NOT a real frame wait. One real frame ≈ 16.7ms ≈ 5 Delay1Frames.
             * Decrement by 5 per real frame to match ASM timing. */
            if (state->slot_roulette_anim_counter > 5)
                state->slot_roulette_anim_counter -= 5;
            else
                state->slot_roulette_anim_counter = 0;
            if (state->slot_roulette_anim_counter == 0) {
                /* Delay done: increment counter, check stop */
                state->slot_roulette_counter++;
                if (state->slot_roulette_counter >= 60) {
                    state->slot_roulette_state = SLOT_ROULETTE_STOPPED_SFX;
                    continue_processing = true;
                } else {
                    /* Advance to next permutation entry */
                    uint8_t idx = state->cur_slot_reward_roulette_index;
                    idx = (idx & 0xF0) | ((idx + 1) & 0x0F);
                    state->cur_slot_reward_roulette_index = idx;
                    state->slot_roulette_state = SLOT_ROULETTE_SPIN_ENTRY;
                    continue_processing = true;
                }
            }
            break;

        case SLOT_ROULETTE_STOPPED_SFX:
            /* Play result SFX: small reward = $0C/$42, otherwise $0C/$43 */
            if (state->slot_roulette_billboard_picture == BILLBOARD_SMALL_REWARD)
                PLAY_SFX(state, "evo_stone_flash_1", 0x0C, 0x42);
            else
                PLAY_SFX(state, "evo_stone_flash_2", 0x0C, 0x43);
            state->slot_roulette_anim_counter = 40;
            state->slot_roulette_state = SLOT_ROULETTE_STOPPED_DISPLAY;
            /* Fall through — first display frame is this frame */
            continue_processing = true;
            break;

        case SLOT_ROULETTE_STOPPED_DISPLAY:
            /* 40-frame display, flipper press skips */
            if (state->hram.newly_pressed_buttons & FLIPPERS) {
                load_colored_billboard_palette(state, state->slot_roulette_billboard_picture);
                state->slot_roulette_anim_counter = 0x80;
                state->slot_roulette_state = SLOT_ROULETTE_STOPPED_BLINK;
                break;
            }
            state->slot_roulette_anim_counter--;
            if (state->slot_roulette_anim_counter == 0) {
                load_colored_billboard_palette(state, state->slot_roulette_billboard_picture);
                state->slot_roulette_anim_counter = 0x80;
                state->slot_roulette_state = SLOT_ROULETTE_STOPPED_BLINK;
            }
            break;

        case SLOT_ROULETTE_STOPPED_BLINK: {
            /* 128-frame blink: toggle billboard every 16 frames */
            uint8_t b = state->slot_roulette_anim_counter;
            if ((b & 0x0F) == 0) {
                if (b & 0x10)
                    load_billboard_picture(state, state->slot_roulette_billboard_picture);
                else
                    load_billboard_off_picture(state, state->slot_roulette_billboard_picture);
            }
            if (state->hram.newly_pressed_buttons & FLIPPERS) {
                /* Skip to advance progress */
                uint8_t prog = state->slot_reward_progress + 10;
                if (prog == 250) prog = 100;
                state->slot_reward_progress = prog;
                state->slot_roulette_state = SLOT_ROULETTE_REWARD_INIT;
                continue_processing = true;
                break;
            }
            state->slot_roulette_anim_counter--;
            if (state->slot_roulette_anim_counter == 0) {
                uint8_t prog = state->slot_reward_progress + 10;
                if (prog == 250) prog = 100;
                state->slot_reward_progress = prog;
                state->slot_roulette_state = SLOT_ROULETTE_REWARD_INIT;
                continue_processing = true;
            }
            break;
        }

        case SLOT_ROULETTE_REWARD_INIT: {
            /* Dispatch the reward. For animated rewards (SmallPoints/BigPoints/
             * BonusMultiplier), dispatch picks the random value; the blink
             * animation and score/mult application follow. */
            uint8_t reward = state->slot_roulette_billboard_picture;
            slot_reward_dispatch(state, reward);

            if (reward == BILLBOARD_SMALL_REWARD ||
                reward == BILLBOARD_BIG_REWARD ||
                reward == BILLBOARD_BONUS_MULTIPLIER) {
                /* Start 128-frame reward blink animation */
                state->slot_roulette_anim_counter = 0x80;
                state->slot_roulette_state = SLOT_ROULETTE_REWARD_BLINK;
            } else {
                state->slot_roulette_state = SLOT_ROULETTE_DONE;
                continue_processing = true;
            }
            break;
        }

        case SLOT_ROULETTE_REWARD_BLINK: {
            /* 128-frame blink of numbered billboard.
             * SmallPoints: BILLBOARD_SMALL_REWARD_100 + cur_slot_bonus
             * BigPoints:   BILLBOARD_BIG_REWARD_1000000 + cur_slot_bonus
             * Multiplier:  BILLBOARD_BONUS_MULTIPLIER_X1 + cur_slot_bonus */
            uint8_t b = state->slot_roulette_anim_counter;
            uint8_t reward = state->slot_roulette_billboard_picture;
            uint8_t base_id;
            if (reward == BILLBOARD_SMALL_REWARD)
                base_id = BILLBOARD_SMALL_REWARD_100;
            else if (reward == BILLBOARD_BIG_REWARD)
                base_id = BILLBOARD_BIG_REWARD_1000000;
            else
                base_id = BILLBOARD_BONUS_MULTIPLIER_X1;
            uint8_t anim_id = base_id + state->cur_slot_bonus;

            if ((b & 0x0F) == 0) {
                if (b & 0x10)
                    load_billboard_picture(state, anim_id);
                else
                    load_billboard_off_picture(state, anim_id);
            }
            if (state->hram.newly_pressed_buttons & FLIPPERS) {
                state->slot_roulette_state = SLOT_ROULETTE_REWARD_FINISH;
                continue_processing = true;
                break;
            }
            state->slot_roulette_anim_counter--;
            if (state->slot_roulette_anim_counter == 0) {
                state->slot_roulette_state = SLOT_ROULETTE_REWARD_FINISH;
                continue_processing = true;
            }
            break;
        }

        case SLOT_ROULETTE_REWARD_FINISH:
            /* Apply score/multiplier after blink animation */
            slot_reward_finish(state, state->slot_roulette_billboard_picture);
            state->slot_roulette_state = SLOT_ROULETTE_DONE;
            continue_processing = true;
            break;

        case SLOT_ROULETTE_DONE: {
            /* Post-roulette cleanup */
            uint8_t reward = state->slot_roulette_billboard_picture;
            state->opened_slot_by_cave_lights = 0;

            if (reward >= BILLBOARD_GENGAR_BONUS && reward <= BILLBOARD_SEEL_BONUS) {
                slot_goto_bonus_stage(state);
            } else {
                state->pinball_is_visible = 1;
                state->enable_ball_gravity_and_tilt = 1;
                if (state->catchem_or_evolution_slot_reward_active == EVOLUTION_MODE_SLOT_REWARD) {
                    start_evolution_mode(state);
                    uint8_t backup = state->red_stage_structure_backup;
                    state->stage_collision_state = (state->stage_collision_state & 1) | backup;
                    state->catchem_or_evolution_slot_reward_active = 0;
                }
            }
            state->slot_roulette_active = 0;
            break;
        }
        } /* switch */
    } /* while continue_processing */
}

/*=============================================================================
 * LoadSlotCaveCoverGraphics_RedField (0x16425) — GBC path
 *
 * Writes tile IDs to bg_map for the slot cave cover.
 * Index = (stage & 1) * 2 + slot_is_open:
 *   0 = Top closed:   bg_map[0x229] = {$D4, $D5}
 *   1 = Top open:     bg_map[0x229] = {$D6, $D7}
 *   2 = Bottom closed: bg_map[0x09] = {$38,$39}, bg_map[0x29] = {$3A,$3B}
 *   3 = Bottom open:   bg_map[0x09] = {$3C,$3D}, bg_map[0x29] = {$3E,$3F}
 *===========================================================================*/
void load_slot_cave_cover_graphics(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    uint8_t idx = (uint8_t)(((state->current_stage & 1) * 2) + (state->slot_is_open ? 1 : 0));

    switch (idx) {
    case 0: /* Top, closed */
        bg[0x229] = 0xD4;
        bg[0x22A] = 0xD5;
        break;
    case 1: /* Top, open */
        bg[0x229] = 0xD6;
        bg[0x22A] = 0xD7;
        break;
    case 2: /* Bottom, closed */
        bg[0x09] = 0x38; bg[0x0A] = 0x39;
        bg[0x29] = 0x3A; bg[0x2A] = 0x3B;
        break;
    case 3: /* Bottom, open */
        bg[0x09] = 0x3C; bg[0x0A] = 0x3D;
        bg[0x29] = 0x3E; bg[0x2A] = 0x3F;
        break;
    }
}

/*=============================================================================
 * ResolveSlotCollision_RedField (0x16279)
 *
 * Full 19-frame countdown:
 *   0x13: Freeze ball at ($50,$16), start countdown
 *   0x12: SFX + LoadMiniBallGfx
 *   0x0F: LoadSuperMiniPinballGfx
 *   0x0C: Hide ball, clear spin/rotation
 *   0x09: DoSlotLogic_RedField (roulette + reward dispatch)
 *   0x06: Close slot, rumble, LoadMiniBallGfx (exit begins)
 *   0x03: LoadBallGfx (normal), eject Y=+2.0 X=+0.5
 *   0x00: LoadSlotCaveCoverGraphics, start CatchEm if slot reward
 *===========================================================================*/
static void resolve_slot(GameState *state) {
    if (state->slot_collision) {
        state->slot_collision = 0;

        /* Only enter if slot is open */
        if (!state->slot_is_open)
            return;
        if (state->slot_enter_or_exit_counter)
            return;

        /* Freeze ball at slot position */
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->ball_x_pos = MAKE_UFIXED(0x50, 0);
        state->ball_y_pos = MAKE_UFIXED(0x16, 0);
        state->slot_enter_or_exit_counter = 0x13;
    }

    /* Per-frame slot counter update */
    if (state->slot_roulette_active) {
        /* Roulette in progress — keep glow alive and tick the state machine */
        state->slot_glowing_anim_counter = 0x18;
        update_slot_roulette(state);
        return;
    }

    if (state->slot_enter_or_exit_counter > 0) {
        state->slot_enter_or_exit_counter--;
        state->slot_glowing_anim_counter = 0x18;
        uint8_t c = state->slot_enter_or_exit_counter;

        if (c == 0x12) {
            PLAY_SFX(state, "arrow_indicator", 0x00, 0x21);
            load_mini_ball_gfx(state);
        } else if (c == 0x0F) {
            load_super_mini_ball_gfx(state);
        } else if (c == 0x0C) {
            state->pinball_is_visible = 0;
            state->ball_spin = 0;
            state->ball_rotation = 0;
        } else if (c == 0x09) {
            start_slot_roulette(state);
        } else if (c == 0x06) {
            state->slot_is_open = 0;
            state->rumble_pattern = 0x05;
            state->rumble_duration = 0x08;
            load_mini_ball_gfx(state);
        } else if (c == 0x03) {
            load_ball_gfx(state);
            state->ball_y_velocity = MAKE_FIXED(2, 0);
            state->ball_x_velocity = 0x0080;
        } else if (c == 0x00) {
            load_slot_cave_cover_graphics(state);
            /* ASM only checks CATCHEM here; evolution starts inside DoSlotLogic */
            if (state->catchem_or_evolution_slot_reward_active == CATCHEM_MODE_SLOT_REWARD) {
                /* Start Catch'Em mode from slot reward */
                uint8_t rnd = gen_random(state);
                state->rare_mons_flag = rnd & 0x08;
                start_catchem_mode(state);
                state->catchem_or_evolution_slot_reward_active = 0;
            }
        }
    }
}

/* UpdateBallSaverState (0x146a9) - runs every frame */
void update_ball_saver(GameState *state) {
    /* Skip if timer is already zero */
    if (!state->ball_saver_timer_frames && !state->ball_saver_timer_seconds)
        return;

    /* Don't decrement timer while ball is in launch area (x >= 154) */
    uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
    if (ball_x >= 154)
        goto update_icon;

    /* Decrement frames */
    state->ball_saver_timer_frames--;
    if (state->ball_saver_timer_frames & 0x80) {
        /* Frames underflowed: reset to 59, decrement seconds */
        state->ball_saver_timer_frames = 59;
        if (state->ball_saver_timer_seconds > 0)
            state->ball_saver_timer_seconds--;
    }

    /* Update flash rate based on remaining seconds.
     * ASM does inc a (on decremented sec) then cp $2/$6/$b:
     *   (sec+1) < 2 → sec < 1 → rate=0x00
     *   (sec+1) < 6 → sec < 5 → rate=0x04
     *   (sec+1) < 11→ sec < 10→ rate=0x10
     *   else         → rate=0xFF */
    {
        uint8_t sec = state->ball_saver_timer_seconds;
        if (sec < 1) {
            state->ball_saver_flash_rate = 0x00;
        } else if (sec < 5) {
            state->ball_saver_flash_rate = 0x04;
        } else if (sec < 10) {
            state->ball_saver_flash_rate = 0x10;
        } else {
            state->ball_saver_flash_rate = 0xFF;
        }
    }

update_icon:
    /* Update icon visibility */
    {
        uint8_t new_icon = 0;
        uint8_t rate = state->ball_saver_flash_rate;
        if (rate == 0) {
            new_icon = 0;
        } else if (rate == 0xFF) {
            new_icon = 1;
        } else {
            new_icon = ((state->hram.frame_counter & rate) == 0) ? 1 : 0;
        }
        state->ball_saver_icon_on = new_icon;
    }
}

/* Ball type degradation table: MASTER(5)→ULTRA(3), ULTRA(3)→GREAT(2)
 * ASM: BallTypeDegradationTable (0x1543e): {0,0,0,2,0,3} */
static const uint8_t ball_type_degradation[] = {0, 0, 0, GREAT_BALL, 0, ULTRA_BALL};

/* UpdateBallTypeCounter (0x153e9) - degrades ball type after timeout */
static void update_ball_type_counter(GameState *state) {
    if (state->capturing_mon) return;
    if (state->ball_type_counter == 0) return;

    state->ball_type_counter--;
    if (state->ball_type_counter != 0) return;

    /* Counter expired: degrade ball type */
    if (state->ball_type > 0 && state->ball_type < 6) {
        state->ball_type = ball_type_degradation[state->ball_type];
        /* ASM: TransitionPinballUpgrade — reload ball sprite graphics/palette */
        load_ball_gfx(state);
        /* Reload timer if still upgradeable */
        if (state->ball_type > 0)
            state->ball_type_counter = 3600; /* ~60 seconds */
    }
}

/* UpdatePokeballs (0x162f0) - track pokeball collection and blink when new */
/* UpdateBlinkingPokeballs_RedField (0x174ea)
 * [H07] ASM alternates old/new pokeball count display every 8 frames
 * using bit 3 of the blinking counter. C previously updated immediately. */
static void update_pokeballs(GameState *state) {
    if (state->previous_num_pokeballs == state->num_pokeballs)
        return;

    /* Counter not yet started: init to 0x40 (64 frames of blinking) */
    if (state->pokeball_blinking_counter == 0)
        state->pokeball_blinking_counter = 0x40;

    state->pokeball_blinking_counter--;
    if (state->pokeball_blinking_counter == 0) {
        /* Blink done: finalize — set previous = current */
        state->previous_num_pokeballs = state->num_pokeballs;
        load_pokeball_indicator_graphics(state);
        /* Check if 3 pokeballs collected → open slot */
        if (state->num_pokeballs >= 3) {
            state->opened_slot_by_pokeballs = 1;
            state->frames_until_slot_cave_opens = 3;
            /* ASM does NOT reset wNumPokeballs here — both previous and
             * current stay at 3 so UpdateBlinkingPokeballs exits early
             * on the next frame (ret z).  num_pokeballs is only reset
             * to 0 inside slot_goto_bonus_stage() when the ball actually
             * enters the bonus stage. */
        }
        return;
    }

    /* Still blinking: only update display every 8th frame */
    if ((state->pokeball_blinking_counter & 7) != 0)
        return;

    /* Bit 3 of counter alternates between old and new count display */
    if (state->pokeball_blinking_counter & 8) {
        /* Display NEW count (num_pokeballs already has it) */
        load_pokeball_indicator_graphics(state);
    } else {
        /* Display OLD count: temporarily swap in previous value */
        uint8_t saved = state->num_pokeballs;
        state->num_pokeballs = state->previous_num_pokeballs;
        load_pokeball_indicator_graphics(state);
        state->num_pokeballs = saved;
    }
}

/*=============================================================================
 * OpenSlotCave_RedField (0x164e3)
 *
 * Counts down frames_until_slot_cave_opens. When it reaches 0:
 *   - Check wInSpecialMode (skip if active)
 *   - Determine billboard ID (pokeballs → next_bonus_stage+0x15, CAVE → 0x1A)
 *   - Load billboard tile data on bottom stage
 *   - Set slot_is_open=1, indicator_states[4]=0x80
 *   - Load slot cave cover graphics on bottom stage
 *===========================================================================*/
void open_slot_cave(GameState *state) {
    if (state->frames_until_slot_cave_opens == 0)
        return;
    state->frames_until_slot_cave_opens--;
    if (state->frames_until_slot_cave_opens != 0)
        return;

    /* Don't open during special mode */
    if (state->in_special_mode)
        return;

    /* Determine billboard picture to show */
    uint8_t billboard_id = 0;
    if (state->opened_slot_by_pokeballs) {
        billboard_id = (uint8_t)(state->next_bonus_stage + 0x15);
    } else if (state->opened_slot_by_cave_lights) {
        billboard_id = 0x1A;
    } else {
        return; /* Neither pokeballs nor CAVE lights triggered */
    }

    /* Load billboard tile data on bottom stage */
    if (state->current_stage & 1)
        load_billboard_tile_data(state, billboard_id);

    /* Open the slot */
    if (state->slot_is_open)
        return; /* Already open */
    state->slot_is_open = 1;
    state->indicator_states[4] = 0x80; /* Activate slot indicator glow */

    /* Load cover graphics on bottom stage only */
    if (state->current_stage & 1)
        load_slot_cave_cover_graphics(state);
}

/*=============================================================================
 * ApplySlotForceField_RedField (0x161af / 0x161e0)
 *
 * Applies force from a 2D lookup table when ball is near the slot cave opening.
 * The table is 48x48 entries of 4-byte (vx_lo, vx_hi, vy_lo, vy_hi) deltas.
 * Index = dy * 192 + dx * 4 where dy/dx are pixel offsets from reference coords.
 *
 * Bottom: ref_y=0xFE, ref_x=0x38. Top: ref_y=0x86, ref_x=0x38.
 * Both stages use the same data file (ball_physics_f0000.bin, 9216 bytes).
 *===========================================================================*/
void apply_slot_force_field(GameState *state) {
    if (!state->slot_is_open)
        return;

    /* Load force field data lazily on first use */
    if (!state->slot_force_field_data) {
        state->slot_force_field_data = load_binary_data(state->asset_base_path,
            "data/collision/ball_physics_f0000.bin", &state->slot_force_field_data_size);
        if (!state->slot_force_field_data) {
            fprintf(stderr, "red_field: failed to load slot force field data\n");
            return;
        }
    }

    /* Reference Y: bottom=0xFE, top=0x86 */
    uint8_t ref_y = (state->current_stage == STAGE_RED_FIELD_BOTTOM) ? 0xFE : 0x86;
    uint8_t ball_y = UFIXED_TO_INT(state->ball_y_pos);
    uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);

    /* ASM: sub ref_y; cp $30; ret nc — unsigned 8-bit range check */
    uint8_t dy = ball_y - ref_y;
    if (dy >= 0x30)
        return;
    uint8_t dx = ball_x - 0x38;
    if (dx >= 0x30)
        return;

    /* ASM index calc:
     *   hl = dy * 256 (b=dy, c=0, h=dy, l=0)
     *   bc = dy * 64  (srl b:rr c twice → dy >> 2 in 16-bit = dy * 64)
     *   hl = dy * 128 (srl h:rr l once → dy >> 1 in 16-bit = dy * 128)
     *   hl += bc → dy * 192
     *   bc = dx * 4   (sla c twice)
     *   hl += bc → dy * 192 + dx * 4 */
    uint16_t offset = (uint16_t)dy * 192 + (uint16_t)dx * 4;
    if (offset + 4 > state->slot_force_field_data_size)
        return;

    const uint8_t *entry = &state->slot_force_field_data[offset];

    /* Read 16-bit signed force deltas (little-endian: lo, hi) */
    int16_t force_vx = (int16_t)((uint16_t)entry[0] | ((uint16_t)entry[1] << 8));
    int16_t force_vy = (int16_t)((uint16_t)entry[2] | ((uint16_t)entry[3] << 8));

    /* Add forces to ball velocity (16-bit add with carry, matching ASM) */
    state->ball_x_velocity = (int16_t)((uint16_t)state->ball_x_velocity + (uint16_t)force_vx);
    state->ball_y_velocity = (int16_t)((uint16_t)state->ball_y_velocity + (uint16_t)force_vy);

    /* ASM: magnitude check — abs(force_vy) + abs(force_vx), shifted left 1.
     * If magnitude < 0x200, don't trigger rumble. */
    int16_t abs_vx = (force_vx < 0) ? -force_vx : force_vx;
    int16_t abs_vy = (force_vy < 0) ? -force_vy : force_vy;
    uint16_t magnitude = (uint16_t)(abs_vx + abs_vy) << 1;

    if ((magnitude >> 8) < 2)
        return;
    if (state->rumble_duration)
        return;

    state->rumble_pattern = 0x05;
    state->rumble_duration = 0x08;
    PLAY_SFX(state, "map_move", 0x00, 0x04);
}

/*=============================================================================
 * UpdateAgainText (0x14733)
 *
 * Updates the "AGAIN" text tiles on the bottom stage to show solid (lit) when
 * extra balls are available, or faded when not. Checks if the display state
 * changed since last call and only reloads graphics on change.
 *
 * ASM loads different tile GFX data to VRAM tiles $38/$3A via QueueGraphicsToLoad.
 * In our C code, the bottom stage PNG already contains the default (off) tiles.
 * A full implementation would swap tile pixel data; for now we track the state
 * change so future tile-swap code can hook in here.
 *===========================================================================*/
void update_again_text(GameState *state) {
    uint8_t new_state = (state->extra_balls > 0) ? 1 : 0;
    if (state->extra_ball == new_state)
        return;
    state->extra_ball = new_state;

    /* LoadAgainTextGraphics: swap tile pixel data at VRAM tiles $38/$3A
     * between "on" (solid) and "off" (faded) versions. */
    load_again_text_graphics(state);
}

/*=============================================================================
 * ShowExtraBallMessage (0x30188)
 *
 * Displays "EXTRA BALL" scrolling text when show_extra_ball_text is set.
 * show_extra_ball_text == 1: "EXTRA BALL" in slot 0.
 * show_extra_ball_text == 2: "EXTRA BALL SPECIAL BONUS" in slot 0 +
 *                            formatted "10,000,000" in slot 1 (Func_32cc).
 *===========================================================================*/

/* ExtraBallText: scrolling_text_normal 5, 20, 0, 16 */
static const uint8_t EXTRA_BALL_HEADER[6] = { 5, 0x54, 0x45, 20, 0x00, 51 };

/* ExtraBallSpecialBonusText: scrolling_text_nopause 7, 45 */
static const uint8_t EXTRA_BALL_SPECIAL_HEADER[6] = { 7, 0x54, 0, 0, 0, 45 };

/* DigitsText1to9: scrolling_text 7, 45, 5, 20, 2, 15 */
static const uint8_t DIGITS_1_9_HEADER[6] = { 7, 0x6D, 0x45, 20, 0x20, 75 };

/*
 * Func_32cc equivalent: set up scrolling text slot from header, then
 * format a 4-byte BCD value into the bottom_message_text buffer.
 * ASM reads 4 bytes from stack (big-endian), formats 8 BCD digits with
 * commas at positions 6 and 3, then appends "0 " trailer.
 *
 * bcd[0] = highest byte, bcd[3] = lowest byte (matching ASM sp+5 down).
 */
static void load_scrolling_text_with_bcd(GameState *state, int slot_index,
                                          const uint8_t *header,
                                          const uint8_t bcd[4]) {
    if (slot_index < 0 || slot_index >= 3) return;

    /* Set up scrolling text struct from header (matches Func_32cc header copy) */
    ScrollingText *st = &state->scrolling_text[slot_index];
    st->enabled = 1;
    st->scroll_delay_counter = header[0];
    st->scroll_delay = header[0];
    st->message_box_offset = header[1];
    st->stop_offset = header[2];
    st->stop_duration = header[3];
    st->source_text_offset = header[4];
    st->scroll_steps_remaining = header[5];

    /* Format BCD digits into bottom_message_text at header's text_offset.
     * Matches Func_32cc + Func_3309: 8 digits with leading zero suppression,
     * commas at digit positions b==6 and b==3, then trailing "0 ".
     * ASM "set 7, e" trick: commas go to buf[pos + 0x80] (row 1),
     * NOT inline — they overlay the digit position below it. */
    uint8_t *buf = state->bottom_message_text;
    int pos = header[4];  /* text_offset */
    int b = 8;            /* digit position counter (8 down to 1) */
    bool suppressing = true;  /* c flag in ASM: leading zero suppression */

    for (int byte_idx = 0; byte_idx < 4; byte_idx++) {
        /* High nibble (ASM: swap a / and $f) */
        uint8_t nibble = (bcd[byte_idx] >> 4) & 0xF;
        if (nibble != 0 || b == 1 || !suppressing) {
            buf[pos] = (uint8_t)(0x86 + nibble);
            if (b == 6 || b == 3) buf[pos + 0x80] = 0x82;  /* comma in row 1 */
            pos++;
            suppressing = false;
        }
        b--;

        /* Low nibble (ASM: and $f) */
        nibble = bcd[byte_idx] & 0xF;
        if (nibble != 0 || b == 1 || !suppressing) {
            buf[pos] = (uint8_t)(0x86 + nibble);
            if (b == 6 || b == 3) buf[pos + 0x80] = 0x82;  /* comma in row 1 */
            pos++;
            suppressing = false;
        }
        b--;
    }

    /* ASM: append '0' tile and ' ' tile after formatted number */
    buf[pos++] = 0x86;  /* '0' */
    buf[pos++] = 0x81;  /* ' ' */
    buf[pos] = 0x00;    /* terminator */
}

/* Public wrapper for blue_field.c to use */
void load_scrolling_text_with_bcd_public(GameState *state, int slot_index,
                                          const uint8_t *header,
                                          const uint8_t bcd[4]) {
    load_scrolling_text_with_bcd(state, slot_index, header, bcd);
}

void show_extra_ball_message(GameState *state) {
    if (state->bottom_text_enabled)
        return;
    if (!state->show_extra_ball_text)
        return;

    if (state->show_extra_ball_text == 1) {
        /* ASM 0x30195: simple "EXTRA BALL" text in slot 0 (wScrollingText1) */
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        load_scrolling_text(state, 0, EXTRA_BALL_HEADER, "EXTRA BALL ");
    } else {
        /* ASM 0x301a7: "EXTRA BALL SPECIAL BONUS" + formatted "10,000,000"
         * BCD value $10,$00,$00,$00 = 10,000,000 (matches bc=$1000, de=$0000) */
        static const uint8_t bcd_10m[4] = { 0x10, 0x00, 0x00, 0x00 };
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        /* Func_32cc: format BCD into slot 1 (wScrollingText2) */
        load_scrolling_text_with_bcd(state, 1, DIGITS_1_9_HEADER, bcd_10m);
        /* LoadScrollingText: "EXTRA BALL SPECIAL BONUS" in slot 0 (wScrollingText1) */
        load_scrolling_text(state, 0, EXTRA_BALL_SPECIAL_HEADER,
                            "EXTRA BALL SPECIAL BONUS ");
    }

    state->show_extra_ball_text = 0;
}

/*=============================================================================
 * Increment_Max100 (0xe4a)
 * Increments *val, capped at 100. Returns 1 if incremented, 0 if at max.
 *===========================================================================*/
uint8_t increment_max100(uint8_t *val) {
    if (*val >= 100) return 0;
    (*val)++;
    return 1;
}

/*=============================================================================
 * CloseSlotCave (0x14f2c)
 * Closes the slot cave entrance and clears related state.
 *===========================================================================*/
static void close_slot_cave(GameState *state) {
    state->slot_is_open = 0;
    state->frames_until_slot_cave_opens = 0;
    state->slot_glowing_anim_counter = 0;
    /* ASM has two separate functions:
     * CloseSlotCave_ (0x107b0) → LoadSlotCaveCoverGraphics_RedField
     * CloseSlotCave  (0x1f2ed) → LoadSlotCaveCoverGraphics_BlueField */
    if (state->current_stage >= STAGE_BLUE_FIELD_TOP)
        load_slot_cave_cover_graphics_blue(state);
    else
        load_slot_cave_cover_graphics(state);
}

/*=============================================================================
 * CATCH'EM MODE (SPECIAL_MODE_CATCHEM = 0)
 *
 * From catchem_mode.asm + catchem_mode_red_field.asm.
 * 8-state machine: billboard animation → mon display → hit detection → capture.
 *===========================================================================*/

/*=============================================================================
 * WILD MON TABLES (from data/red_wild_mons.asm, data/blue_wild_mons.asm)
 *
 * Each row = 32 bytes: [0..15] common, [16..31] rare.
 * Species IDs are 1-based (matching PokemonId enum).
 * 12 active maps per field, indexed via map_index tables.
 *===========================================================================*/

/* Red field: 12 maps × 32 entries. Order: Pallet, VForest, Pewter, Cerulean,
 * VermSeaside, RockMtn, Lavender, Cycling, Safari, Seafoam, Cinnabar, Indigo */
static const uint8_t red_wild_mons[12][32] = {
    /* Pallet Town */
    { 1, 4, 4, 4, 4, 4, 4,16,16,16,19,19,19,32,60,72,
      1, 1, 1, 4,16,19,32,32,32,60,60,60,60,72,72,72},
    /* Viridian Forest */
    {13,13,13,13,13,16,16,16,16,16,19,19,19,19,19,25,
     10,10,13,13,13,16,16,19,19,25,25,25,25,25,25,25},
    /* Pewter City */
    {16,16,21,21,21,21,21,21,23,39,39,39,39,39,129,129,
     16,16,21,21,21,23,23,23,23,39,39,39,129,129,129,129},
    /* Cerulean City */
    {13,13,16,43,43,43,43,43,54,56,56,56,63,63,98,118,
     10,32,43,54,54,56,56,63,63,63,98,118,118,124,124,124},
    /* Vermilion Seaside */
    {16,21,23,23,43,43,56,56,90,90,90,96,96,98,98,98,
     23,23,23,23,43,56,83,83,83,83,90,90,96,96,98,98},
    /* Rock Mountain */
    {19,21,23,23,23,41,50,50,50,66,74,79,95,100,100,100,
     41,41,50,66,66,74,74,79,79,95,95,100,100,122,122,122},
    /* Lavender Town */
    {16,16,23,23,56,56,58,58,81,81,92,92,92,92,92,104,
     23,56,58,81,81,92,92,104,104,104,125,125,125,145,145,145},
    /* Cycling Road */
    {19,19,21,21,72,72,84,84,84,98,98,108,118,118,129,129,
     72,84,84,84,84,84,98,108,108,108,108,118,129,143,143,143},
    /* Safari Zone */
    {32,32,32,32,46,46,46,46,84,84,84,84,111,111,111,111,
     32,32,46,46,111,111,113,113,113,113,123,123,128,128,147,147},
    /* Seafoam Islands */
    {41,54,72,79,86,90,98,116,116,116,116,118,120,120,120,120,
     86,86,86,86,86,118,118,118,118,120,120,120,120,144,144,144},
    /* Cinnabar Island */
    {58,58,58,58,77,77,77,77,88,88,109,109,109,109,114,114,
     58,58,77,77,88,109,109,114,114,114,138,138,138,140,140,140},
    /* Indigo Plateau */
    {21,23,41,41,66,66,66,74,74,74,95,95,95,132,132,132,
     21,23,41,66,74,95,132,132,132,132,146,146,146,150,150,150},
};

/* Red field: map_id → index into red_wild_mons (0xFF = unused) */
static const uint8_t red_wild_mon_map_index[18] = {
    0, 0xFF, 1, 2, 0xFF, 3, 4, 0xFF, 5, 6, 0xFF, 7, 0xFF, 8, 0xFF, 9, 10, 11
};

/* Blue field: 12 maps × 32 entries. Order: ViridianCity, VForest, MtMoon,
 * Cerulean, VermStreets, RockMtn, Celadon, Fuchsia, Safari, Saffron,
 * Cinnabar, Indigo */
static const uint8_t blue_wild_mons[12][32] = {
    /* Viridian City */
    { 1, 7, 7, 7, 7, 7,21,29,29,29,32,32,32,60,72,118,
      1, 1, 1, 7,21,21,29,29,32,32,60,60,72,72,118,118},
    /* Viridian Forest */
    {10,10,10,10,10,16,16,16,16,16,19,19,19,19,19,25,
     10,10,10,13,13,16,16,19,19,25,25,25,25,25,25,25},
    /* Mt. Moon */
    {19,21,21,23,23,27,27,41,41,46,46,54,74,74,98,118,
     23,23,27,27,35,35,35,35,35,35,41,41,46,46,74,74},
    /* Cerulean City */
    {10,10,16,52,52,52,54,63,63,69,69,69,69,69,98,118,
     13,32,52,52,54,54,63,63,63,69,98,118,118,124,124,124},
    /* Vermilion Streets */
    {16,21,27,27,52,52,69,69,90,90,90,96,96,98,98,98,
     27,27,27,27,52,69,83,83,83,83,90,90,96,96,98,98},
    /* Rock Mountain */
    {19,21,27,27,41,50,50,50,50,66,74,79,95,100,100,100,
     41,41,50,66,66,74,74,79,79,95,95,100,100,122,122,122},
    /* Celadon City */
    {16,16,37,37,43,43,52,52,52,56,56,56,58,58,69,69,
     35,35,63,63,123,127,133,133,133,137,137,137,137,147,147,147},
    /* Fuchsia City */
    {48,48,98,98,98,102,102,115,115,118,118,118,129,129,129,129,
     48,48,48,48,98,102,102,102,102,115,115,115,115,118,129,129},
    /* Safari Zone */
    {29,29,29,29,46,46,46,46,84,84,84,84,111,111,111,111,
     29,29,46,46,111,111,113,113,113,113,127,127,128,128,147,147},
    /* Saffron City */
    {16,16,23,23,23,27,27,27,37,43,43,52,56,58,69,69,
     16,23,27,37,52,56,58,106,106,106,107,107,107,131,131,131},
    /* Cinnabar Island */
    {37,37,37,77,77,77,77,77,88,88,109,109,109,109,114,114,
     37,77,77,88,88,109,109,114,114,114,126,126,126,142,142,142},
    /* Indigo Plateau */
    {21,27,41,41,66,66,66,74,74,74,95,95,95,132,132,132,
     21,27,41,66,74,95,132,132,132,132,146,146,146,150,150,150},
};

/* Blue field: map_id → index into blue_wild_mons (0xFF = unused) */
static const uint8_t blue_wild_mon_map_index[18] = {
    0xFF, 0, 1, 0xFF, 2, 3, 0xFF, 4, 5, 0xFF, 6, 0xFF, 7, 8, 9, 0xFF, 10, 11
};

/*=============================================================================
 * CatchemMonIds (0x1161d): species (0-based) → catch sprite ID (0x00-0x4E).
 * Evolutions share the base form's sprite ID.
 *===========================================================================*/
static const uint8_t catchem_mon_ids[NUM_POKEMON] = {
    0x00,0x00,0x00, 0x01,0x01,0x01, 0x02,0x02,0x02,  /* Bulba,Ivy,Venu, Char,Mel,Zard, Squir,War,Blast */
    0x03,0x03,0x03, 0x04,0x04,0x04, 0x05,0x05,0x05,  /* Cater,Meta,Butter, Weed,Kaku,Beed, Pidg,Otto,Geot */
    0x06,0x06, 0x07,0x07, 0x08,0x08,                  /* Ratt,Ratic, Spear,Fear, Ekans,Arbok */
    0x09,0x09, 0x0A,0x0A,                              /* Pika,Raichu, Sandsh,Slash */
    0x0B,0x0B,0x0B, 0x0C,0x0C,0x0C,                  /* NidoF,Rina,Queen, NidoM,Rino,King */
    0x0D,0x0D, 0x0E,0x0E, 0x0F,0x0F,                  /* Clef,Clefa, Vulp,Nine, Jigg,Wigg */
    0x10,0x10, 0x11,0x11,0x11, 0x12,0x12,              /* Zubat,Golb, Odd,Gloom,Vile, Paras,Parasect */
    0x13,0x13, 0x14,0x14, 0x15,0x15,                  /* Veno,Moth, Dig,Dug, Meow,Pers */
    0x16,0x16, 0x17,0x17, 0x18,0x18,                  /* Psy,Gold, Mank,Prime, Grow,Arcan */
    0x19,0x19,0x19, 0x1A,0x1A,0x1A,                   /* Poli,Whirl,Wrath, Abra,Kada,Alak */
    0x1B,0x1B,0x1B, 0x1C,0x1C,0x1C,                   /* Mach,Choke,Champ, Bell,Weep,Victr */
    0x1D,0x1D, 0x1E,0x1E,0x1E,                        /* Tent,Cruel, Geo,Grav,Golem */
    0x1F,0x1F, 0x20,0x20, 0x21,0x21,                  /* Pony,Rapid, Slow,Bro, Magnem,Ton */
    0x22, 0x23,0x23, 0x24,0x24,                        /* Farfetchd, Doduo,Dodrio, Seel,Dewg */
    0x25,0x25, 0x26,0x26, 0x27,0x27,0x27,              /* Grim,Muk, Shell,Cloys, Gas,Haunt,Geng */
    0x28, 0x29,0x29, 0x2A,0x2A,                        /* Onix, Drow,Hypno, Krab,King */
    0x2B,0x2B, 0x2C,0x2C, 0x2D,0x2D,                  /* Volt,Elect, Exegg,Extor, Cubo,Maro */
    0x2E, 0x2F, 0x30,                                  /* Hitmonlee, Hitmonchan, Lickitung */
    0x31,0x31, 0x32,0x32,                              /* Koff,Weez, Rhy,Don */
    0x33, 0x34, 0x35,                                  /* Chansey, Tangela, Kangaskhan */
    0x36,0x36, 0x37,0x37, 0x38,0x38,                  /* Hors,Seadra, Gold,Seak, Star,Starm */
    0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,        /* MrMime,Scyther,Jynx,Electabuzz,Magmar,Pinsir,Tauros */
    0x40,0x40, 0x41, 0x42,                              /* Magik,Gyar, Lapras, Ditto */
    0x43,0x43,0x43,0x43,                                /* Eevee,Vap,Jolt,Flar */
    0x44, 0x45,0x45, 0x46,0x46,                        /* Porygon, Oman,Omastar, Kabuto,Kabutops */
    0x47, 0x48,                                         /* Aerodactyl, Snorlax */
    0x49,0x4A,0x4B,                                     /* Articuno,Zapdos,Moltres */
    0x4C,0x4C,0x4C,                                     /* Dratini,Dragonair,Dragonite */
    0x4D, 0x4E,                                         /* Mewtwo, Mew */
};

/*=============================================================================
 * CatchSpriteFrameDurations (0x13685): 79 entries × 3 bytes.
 * [idle_frame1, idle_frame2, hit_frame] per catch sprite ID.
 *===========================================================================*/
static const uint8_t catch_sprite_frame_durations[79][3] = {
    {0x12,0x12,0x10}, {0x10,0x10,0x10}, {0x12,0x12,0x0E}, /*00-02: Bulba,Char,Squirt*/
    {0x14,0x14,0x12}, {0x14,0x14,0x10}, {0x0A,0x0A,0x0E}, /*03-05: Cater,Weed,Pidg*/
    {0x11,0x13,0x10}, {0x0B,0x0B,0x10}, {0x12,0x12,0x0E}, /*06-08: Ratt,Spear,Ekans*/
    {0x12,0x14,0x0E}, {0x10,0x12,0x10}, {0x11,0x12,0x0E}, /*09-0B: Pika,Sandsh,NidoF*/
    {0x11,0x12,0x0E}, {0x12,0x13,0x10}, {0x11,0x11,0x10}, /*0C-0E: NidoM,Clef,Vulp*/
    {0x12,0x12,0x10}, {0x08,0x08,0x10}, {0x10,0x10,0x10}, /*0F-11: Jigg,Zubat,Odd*/
    {0x10,0x10,0x10}, {0x11,0x11,0x0E}, {0x10,0x10,0x0E}, /*12-14: Paras,Veno,Dig*/
    {0x14,0x14,0x0E}, {0x30,0x30,0x10}, {0x12,0x12,0x10}, /*15-17: Meow,Psy,Mank*/
    {0x12,0x12,0x10}, {0x10,0x10,0x10}, {0x10,0x10,0x10}, /*18-1A: Grow,Poli,Abra*/
    {0x12,0x14,0x10}, {0x10,0x12,0x10}, {0x0C,0x0C,0x12}, /*1B-1D: Mach,Bell,Tent*/
    {0x12,0x14,0x0C}, {0x12,0x14,0x0E}, {0x30,0x30,0x10}, /*1E-20: Geo,Pony,Slow*/
    {0x14,0x14,0x10}, {0x12,0x12,0x0E}, {0x12,0x12,0x0E}, /*21-23: Magnem,Farfetch,Dod*/
    {0x14,0x14,0x0E}, {0x12,0x12,0x10}, {0x14,0x14,0x0E}, /*24-26: Seel,Grim,Shell*/
    {0x10,0x10,0x0E}, {0x12,0x12,0x10}, {0x14,0x14,0x10}, /*27-29: Gas,Onix,Drow*/
    {0x14,0x12,0x10}, {0x02,0x02,0x10}, {0x12,0x12,0x10}, /*2A-2C: Krab,Volt,Exegg*/
    {0x12,0x12,0x10}, {0x14,0x10,0x10}, {0x14,0x10,0x10}, /*2D-2F: Cubo,HitLee,HitChan*/
    {0x14,0x12,0x10}, {0x11,0x11,0x10}, {0x14,0x14,0x10}, /*30-32: Licki,Koff,Rhy*/
    {0x12,0x12,0x10}, {0x10,0x10,0x10}, {0x12,0x12,0x10}, /*33-35: Chan,Tang,Kanga*/
    {0x0F,0x0F,0x0E}, {0x12,0x12,0x0E}, {0x23,0x23,0x10}, /*36-38: Hors,Gold,Star*/
    {0x13,0x13,0x10}, {0x13,0x13,0x10}, {0x12,0x12,0x10}, /*39-3B: MrMime,Scyth,Jynx*/
    {0x12,0x14,0x10}, {0x14,0x14,0x0E}, {0x12,0x12,0x0E}, /*3C-3E: Electa,Magm,Pins*/
    {0x12,0x14,0x10}, {0x18,0x18,0x0C}, {0x16,0x16,0x0C}, /*3F-41: Taur,Magik,Lapr*/
    {0x14,0x14,0x10}, {0x12,0x12,0x10}, {0x10,0x10,0x0E}, /*42-44: Ditto,Eevee,Pory*/
    {0x12,0x12,0x0E}, {0x12,0x12,0x0E}, {0x0C,0x0C,0x12}, /*45-47: Oman,Kabuto,Aero*/
    {0x26,0x36,0x12}, {0x13,0x13,0x10}, {0x13,0x13,0x10}, /*48-4A: Snorl,Artic,Zapdos*/
    {0x13,0x13,0x10}, {0x12,0x12,0x0E}, {0x14,0x14,0x0E}, /*4B-4D: Moltr,Drat,Mewtwo*/
    {0x14,0x14,0x0E},                                      /*4E: Mew*/
};

/*=============================================================================
 * MonAnimatedSpriteTypes (0x13429): species (0-based) → sprite type.
 * $03=Bulbasaur type, $00=Squirtle type, $06=generic, $09=Omanyte type.
 * $FF = no catch sprite (evolutions that are never directly catchable).
 *===========================================================================*/
static const uint8_t mon_animated_sprite_types[NUM_POKEMON] = {
    0x03, 0xFF, 0xFF,  /* Bulbasaur, Ivysaur, Venusaur */
    0x06, 0xFF, 0xFF,  /* Charmander, Charmeleon, Charizard */
    0x00, 0xFF, 0xFF,  /* Squirtle, Wartortle, Blastoise */
    0x06, 0xFF, 0xFF,  /* Caterpie, Metapod, Butterfree */
    0x06, 0xFF, 0xFF,  /* Weedle, Kakuna, Beedrill */
    0x06, 0xFF, 0xFF,  /* Pidgey, Pidgeotto, Pidgeot */
    0x06, 0xFF,        /* Rattata, Raticate */
    0x06, 0xFF,        /* Spearow, Fearow */
    0x06, 0xFF,        /* Ekans, Arbok */
    0x06, 0xFF,        /* Pikachu, Raichu */
    0x06, 0xFF,        /* Sandshrew, Sandslash */
    0x06, 0xFF, 0xFF,  /* Nidoran_F, Nidorina, Nidoqueen */
    0x06, 0xFF, 0xFF,  /* Nidoran_M, Nidorino, Nidoking */
    0x06, 0xFF,        /* Clefairy, Clefable */
    0x06, 0xFF,        /* Vulpix, Ninetales */
    0x06, 0xFF,        /* Jigglypuff, Wigglytuff */
    0x06, 0xFF,        /* Zubat, Golbat */
    0x06, 0xFF, 0xFF,  /* Oddish, Gloom, Vileplume */
    0x06, 0xFF,        /* Paras, Parasect */
    0x06, 0xFF,        /* Venonat, Venomoth */
    0x06, 0xFF,        /* Diglett, Dugtrio */
    0x06, 0xFF,        /* Meowth, Persian */
    0x06, 0xFF,        /* Psyduck, Golduck */
    0x06, 0xFF,        /* Mankey, Primeape */
    0x06, 0xFF,        /* Growlithe, Arcanine */
    0x06, 0xFF, 0xFF,  /* Poliwag, Poliwhirl, Poliwrath */
    0x06, 0xFF, 0xFF,  /* Abra, Kadabra, Alakazam */
    0x06, 0xFF, 0xFF,  /* Machop, Machoke, Machamp */
    0x06, 0xFF, 0xFF,  /* Bellsprout, Weepinbell, Victreebel */
    0x06, 0xFF,        /* Tentacool, Tentacruel */
    0x06, 0xFF, 0xFF,  /* Geodude, Graveler, Golem */
    0x06, 0xFF,        /* Ponyta, Rapidash */
    0x06, 0xFF,        /* Slowpoke, Slowbro */
    0x06, 0xFF,        /* Magnemite, Magneton */
    0x06,              /* Farfetch'd */
    0x06, 0xFF,        /* Doduo, Dodrio */
    0x06, 0xFF,        /* Seel, Dewgong */
    0x06, 0xFF,        /* Grimer, Muk */
    0x06, 0xFF,        /* Shellder, Cloyster */
    0x06, 0xFF, 0xFF,  /* Gastly, Haunter, Gengar */
    0x06,              /* Onix */
    0x06, 0xFF,        /* Drowzee, Hypno */
    0x06, 0xFF,        /* Krabby, Kingler */
    0x06, 0xFF,        /* Voltorb, Electrode */
    0x06, 0xFF,        /* Exeggcute, Exeggutor */
    0x06, 0xFF,        /* Cubone, Marowak */
    0x06,              /* Hitmonlee */
    0x06,              /* Hitmonchan */
    0x06,              /* Lickitung */
    0x06, 0xFF,        /* Koffing, Weezing */
    0x06, 0xFF,        /* Rhyhorn, Rhydon */
    0x06,              /* Chansey */
    0x06,              /* Tangela */
    0x06,              /* Kangaskhan */
    0x06, 0xFF,        /* Horsea, Seadra */
    0x06, 0xFF,        /* Goldeen, Seaking */
    0x06, 0xFF,        /* Staryu, Starmie */
    0x06,              /* Mr. Mime */
    0x06,              /* Scyther */
    0x06,              /* Jynx */
    0x06,              /* Electabuzz */
    0x06,              /* Magmar */
    0x06,              /* Pinsir */
    0x06,              /* Tauros */
    0x06, 0xFF,        /* Magikarp, Gyarados */
    0x06,              /* Lapras */
    0x06,              /* Ditto */
    0x06, 0xFF, 0xFF, 0xFF,  /* Eevee, Vaporeon, Jolteon, Flareon */
    0x06,              /* Porygon */
    0x09, 0xFF,        /* Omanyte, Omastar */
    0x06, 0xFF,        /* Kabuto, Kabutops */
    0x06,              /* Aerodactyl */
    0x06,              /* Snorlax */
    0x06,              /* Articuno */
    0x06,              /* Zapdos */
    0x06,              /* Moltres */
    0x06, 0xFF, 0xFF,  /* Dratini, Dragonair, Dragonite */
    0x06,              /* Mewtwo */
    0x06,              /* Mew */
};

/*=============================================================================
 * Pokedex animated sprite accessors.
 * These expose the static data tables for use by main_loop.c's pokedex
 * animated sprite feature (AnimateMonSpriteIfStartIsPressed 0x287e7).
 *===========================================================================*/
uint8_t get_mon_animated_sprite_type(GameState *state, uint8_t pokedex_index) {
    if (pokedex_index >= NUM_POKEMON) return 0xFF;
    if (state->config->pokemon.pokemon_loaded)
        return state->config->pokemon.species[pokedex_index].animated_sprite_type;
    return mon_animated_sprite_types[pokedex_index];
}

void get_catch_sprite_frame_durations_for_mon(GameState *state, uint8_t pokedex_index,
                                               uint8_t *idle1, uint8_t *idle2,
                                               uint8_t *hit) {
    if (pokedex_index >= NUM_POKEMON) {
        *idle1 = 0x10; *idle2 = 0x10; *hit = 0x10;
        return;
    }
    if (state->config->pokemon.pokemon_loaded) {
        const PokemonEntry *pe = &state->config->pokemon.species[pokedex_index];
        *idle1 = pe->catch_frame_durations[0];
        *idle2 = pe->catch_frame_durations[1];
        *hit   = pe->catch_frame_durations[2];
        return;
    }
    uint8_t cid = catchem_mon_ids[pokedex_index];
    if (cid >= 79) {
        *idle1 = 0x10; *idle2 = 0x10; *hit = 0x10;
        return;
    }
    *idle1 = catch_sprite_frame_durations[cid][0];
    *idle2 = catch_sprite_frame_durations[cid][1];
    *hit   = catch_sprite_frame_durations[cid][2];
}

/*=============================================================================
 * BallCaptureAnimationData (0x105e4): 22 entries of [duration, sprite_id].
 * Terminated by $00. Drives the pokeball-shake capture animation.
 *===========================================================================*/
static const uint8_t ball_capture_animation_data[45] = {
    0x05, 0x00,  /* frame 0 */
    0x05, 0x01,  /* frame 1 */
    0x05, 0x02,  /* frame 2 */
    0x04, 0x03,  /* frame 3 */
    0x06, 0x04,  /* frame 4 */
    0x08, 0x05,  /* frame 5 */
    0x07, 0x06,  /* frame 6 */
    0x05, 0x07,  /* frame 7 */
    0x04, 0x08,  /* frame 8 */
    0x04, 0x09,  /* frame 9 */
    0x04, 0x0A,  /* frame 10 */
    0x04, 0x0B,  /* frame 11 */
    0x24, 0x0A,  /* frame 12: long pause, ball closed */
    0x09, 0x0C,  /* frame 13: shake */
    0x09, 0x0A,  /* frame 14: ball closed */
    0x09, 0x0C,  /* frame 15: shake */
    0x27, 0x0A,  /* frame 16: long pause */
    0x09, 0x0C,  /* frame 17: shake */
    0x09, 0x0A,  /* frame 18: ball closed */
    0x09, 0x0C,  /* frame 19: shake */
    0x24, 0x0A,  /* frame 20: long pause */
    0x01, 0x0A,  /* frame 21: final frame */
    0x00,        /* terminator */
};

/*=============================================================================
 * MonAnimatedCollisionMaskPointers (0x134c0): species → mask filename.
 * Evolutions share the base form's collision mask.
 *===========================================================================*/
static const char *mon_collision_mask_names[NUM_POKEMON] = {
    "bulbasaur",  "bulbasaur",  "bulbasaur",   /*   0-  2: Bulbasaur/Ivysaur/Venusaur */
    "charmander", "bulbasaur",  "bulbasaur",   /*   3-  5: Charmander/Charmeleon/Charizard */
    "squirtle",   "bulbasaur",  "bulbasaur",   /*   6-  8: Squirtle/Wartortle/Blastoise */
    "caterpie",   "bulbasaur",  "bulbasaur",   /*   9- 11: Caterpie/Metapod/Butterfree */
    "weedle",     "bulbasaur",  "bulbasaur",   /*  12- 14: Weedle/Kakuna/Beedrill */
    "pidgey",     "bulbasaur",  "bulbasaur",   /*  15- 17: Pidgey/Pidgeotto/Pidgeot */
    "rattata",    "bulbasaur",                 /*  18- 19: Rattata/Raticate */
    "spearow",    "bulbasaur",                 /*  20- 21: Spearow/Fearow */
    "ekans",      "bulbasaur",                 /*  22- 23: Ekans/Arbok */
    "pikachu",    "bulbasaur",                 /*  24- 25: Pikachu/Raichu */
    "sandshrew",  "bulbasaur",                 /*  26- 27: Sandshrew/Sandslash */
    "nidoranf",   "bulbasaur",  "bulbasaur",   /*  28- 30: NidoranF/Nidorina/Nidoqueen */
    "nidoranm",   "bulbasaur",  "bulbasaur",   /*  31- 33: NidoranM/Nidorino/Nidoking */
    "clefairy",   "bulbasaur",                 /*  34- 35: Clefairy/Clefable */
    "vulpix",     "bulbasaur",                 /*  36- 37: Vulpix/Ninetales */
    "jigglypuff", "bulbasaur",                 /*  38- 39: Jigglypuff/Wigglytuff */
    "zubat",      "zubat",                     /*  40- 41: Zubat/Golbat */
    "oddish",     "zubat",      "zubat",       /*  42- 44: Oddish/Gloom/Vileplume */
    "paras",      "zubat",                     /*  45- 46: Paras/Parasect */
    "venonat",    "zubat",                     /*  47- 48: Venonat/Venomoth */
    "diglett",    "zubat",                     /*  49- 50: Diglett/Dugtrio */
    "meowth",     "zubat",                     /*  51- 52: Meowth/Persian */
    "psyduck",    "zubat",                     /*  53- 54: Psyduck/Golduck */
    "mankey",     "zubat",                     /*  55- 56: Mankey/Primeape */
    "growlithe",  "zubat",                     /*  57- 58: Growlithe/Arcanine */
    "poliwag",    "zubat",      "zubat",       /*  59- 61: Poliwag/Poliwhirl/Poliwrath */
    "abra",       "zubat",      "zubat",       /*  62- 64: Abra/Kadabra/Alakazam */
    "machop",     "zubat",      "zubat",       /*  65- 67: Machop/Machoke/Machamp */
    "bellsprout", "zubat",      "zubat",       /*  68- 70: Bellsprout/Weepinbell/Victreebel */
    "tentacool",  "zubat",                     /*  71- 72: Tentacool/Tentacruel */
    "geodude",    "zubat",      "zubat",       /*  73- 75: Geodude/Graveler/Golem */
    "ponyta",     "zubat",                     /*  76- 77: Ponyta/Rapidash */
    "slowpoke",   "slowpoke",                  /*  78- 79: Slowpoke/Slowbro */
    "magnemite",  "slowpoke",                  /*  80- 81: Magnemite/Magneton */
    "farfetchd",                               /*  82:     Farfetch'd */
    "doduo",      "slowpoke",                  /*  83- 84: Doduo/Dodrio */
    "seel",       "slowpoke",                  /*  85- 86: Seel/Dewgong */
    "grimer",     "slowpoke",                  /*  87- 88: Grimer/Muk */
    "shellder",   "slowpoke",                  /*  89- 90: Shellder/Cloyster */
    "gastly",     "slowpoke",  "slowpoke",     /*  91- 93: Gastly/Haunter/Gengar */
    "onix",                                    /*  94:     Onix */
    "drowzee",    "slowpoke",                  /*  95- 96: Drowzee/Hypno */
    "krabby",     "slowpoke",                  /*  97- 98: Krabby/Kingler */
    "voltorb",    "slowpoke",                  /*  99-100: Voltorb/Electrode */
    "exeggcute",  "slowpoke",                  /* 101-102: Exeggcute/Exeggutor */
    "cubone",     "slowpoke",                  /* 103-104: Cubone/Marowak */
    "hitmonlee",                               /* 105:     Hitmonlee */
    "hitmonchan",                              /* 106:     Hitmonchan */
    "lickitung",                               /* 107:     Lickitung */
    "koffing",    "lickitung",                 /* 108-109: Koffing/Weezing */
    "rhyhorn",    "lickitung",                 /* 110-111: Rhyhorn/Rhydon */
    "chansey",                                 /* 112:     Chansey */
    "tangela",                                 /* 113:     Tangela */
    "kangaskhan",                              /* 114:     Kangaskhan */
    "horsea",     "lickitung",                 /* 115-116: Horsea/Seadra */
    "goldeen",    "lickitung",                 /* 117-118: Goldeen/Seaking */
    "staryu",     "lickitung",                 /* 119-120: Staryu/Starmie */
    "mrmime",                                  /* 121:     Mr. Mime */
    "scyther",                                 /* 122:     Scyther */
    "jynx",                                    /* 123:     Jynx */
    "electabuzz",                              /* 124:     Electabuzz */
    "magmar",                                  /* 125:     Magmar */
    "pinsir",                                  /* 126:     Pinsir */
    "tauros",                                  /* 127:     Tauros */
    "magikarp",   "magikarp",                  /* 128-129: Magikarp/Gyarados */
    "lapras",                                  /* 130:     Lapras */
    "ditto",                                   /* 131:     Ditto */
    "eevee",      "magikarp",  "magikarp",     /* 132-134: Eevee/Vaporeon/Jolteon */
    "magikarp",                                /* 135:     Flareon */
    "porygon",                                 /* 136:     Porygon */
    "omanyte",    "magikarp",                  /* 137-138: Omanyte/Omastar */
    "kabuto",     "magikarp",                  /* 139-140: Kabuto/Kabutops */
    "aerodactyl",                              /* 141:     Aerodactyl */
    "snorlax",                                 /* 142:     Snorlax */
    "articuno",                                /* 143:     Articuno */
    "zapdos",                                  /* 144:     Zapdos */
    "moltres",                                 /* 145:     Moltres */
    "dratini",    "magikarp",  "magikarp",     /* 146-148: Dratini/Dragonair/Dragonite */
    "mewtwo",                                  /* 149:     Mewtwo */
    "mew",                                     /* 150:     Mew */
};

/*=============================================================================
 * MonAnimatedPicPointers (0x13264): species → animated pic filename.
 * Evolutions share the base form's animated pic.
 * Filenames differ from collision masks: farfetch_d, mr_mime, nidoran_f, nidoran_m
 *===========================================================================*/
static const char *mon_animated_pic_names[NUM_POKEMON] = {
    "bulbasaur",  "bulbasaur",  "bulbasaur",   /*   0-  2: Bulbasaur/Ivysaur/Venusaur */
    "charmander", "charmander", "charmander",  /*   3-  5: Charmander/Charmeleon/Charizard */
    "squirtle",   "squirtle",   "squirtle",    /*   6-  8: Squirtle/Wartortle/Blastoise */
    "caterpie",   "caterpie",   "caterpie",    /*   9- 11: Caterpie/Metapod/Butterfree */
    "weedle",     "weedle",     "weedle",      /*  12- 14: Weedle/Kakuna/Beedrill */
    "pidgey",     "pidgey",     "pidgey",      /*  15- 17: Pidgey/Pidgeotto/Pidgeot */
    "rattata",    "rattata",                   /*  18- 19: Rattata/Raticate */
    "spearow",    "spearow",                   /*  20- 21: Spearow/Fearow */
    "ekans",      "ekans",                     /*  22- 23: Ekans/Arbok */
    "pikachu",    "pikachu",                   /*  24- 25: Pikachu/Raichu */
    "sandshrew",  "sandshrew",                 /*  26- 27: Sandshrew/Sandslash */
    "nidoran_f",  "nidoran_f",  "nidoran_f",   /*  28- 30: NidoranF/Nidorina/Nidoqueen */
    "nidoran_m",  "nidoran_m",  "nidoran_m",   /*  31- 33: NidoranM/Nidorino/Nidoking */
    "clefairy",   "clefairy",                  /*  34- 35: Clefairy/Clefable */
    "vulpix",     "vulpix",                    /*  36- 37: Vulpix/Ninetales */
    "jigglypuff", "jigglypuff",                /*  38- 39: Jigglypuff/Wigglytuff */
    "zubat",      "zubat",                     /*  40- 41: Zubat/Golbat */
    "oddish",     "oddish",     "oddish",      /*  42- 44: Oddish/Gloom/Vileplume */
    "paras",      "paras",                     /*  45- 46: Paras/Parasect */
    "venonat",    "venonat",                   /*  47- 48: Venonat/Venomoth */
    "diglett",    "diglett",                   /*  49- 50: Diglett/Dugtrio */
    "meowth",     "meowth",                   /*  51- 52: Meowth/Persian */
    "psyduck",    "psyduck",                   /*  53- 54: Psyduck/Golduck */
    "mankey",     "mankey",                    /*  55- 56: Mankey/Primeape */
    "growlithe",  "growlithe",                 /*  57- 58: Growlithe/Arcanine */
    "poliwag",    "poliwag",    "poliwag",     /*  59- 61: Poliwag/Poliwhirl/Poliwrath */
    "abra",       "abra",       "abra",        /*  62- 64: Abra/Kadabra/Alakazam */
    "machop",     "machop",     "machop",      /*  65- 67: Machop/Machoke/Machamp */
    "bellsprout", "bellsprout", "bellsprout",   /*  68- 70: Bellsprout/Weepinbell/Victreebel */
    "tentacool",  "tentacool",                 /*  71- 72: Tentacool/Tentacruel */
    "geodude",    "geodude",    "geodude",     /*  73- 75: Geodude/Graveler/Golem */
    "ponyta",     "ponyta",                    /*  76- 77: Ponyta/Rapidash */
    "slowpoke",   "slowpoke",                  /*  78- 79: Slowpoke/Slowbro */
    "magnemite",  "magnemite",                 /*  80- 81: Magnemite/Magneton */
    "farfetch_d",                              /*  82:     Farfetch'd */
    "doduo",      "doduo",                     /*  83- 84: Doduo/Dodrio */
    "seel",       "seel",                      /*  85- 86: Seel/Dewgong */
    "grimer",     "grimer",                    /*  87- 88: Grimer/Muk */
    "shellder",   "shellder",                  /*  89- 90: Shellder/Cloyster */
    "gastly",     "gastly",     "gastly",      /*  91- 93: Gastly/Haunter/Gengar */
    "onix",                                    /*  94:     Onix */
    "drowzee",    "drowzee",                   /*  95- 96: Drowzee/Hypno */
    "krabby",     "krabby",                    /*  97- 98: Krabby/Kingler */
    "voltorb",    "voltorb",                   /*  99-100: Voltorb/Electrode */
    "exeggcute",  "exeggcute",                 /* 101-102: Exeggcute/Exeggutor */
    "cubone",     "cubone",                    /* 103-104: Cubone/Marowak */
    "hitmonlee",                               /* 105:     Hitmonlee */
    "hitmonchan",                              /* 106:     Hitmonchan */
    "lickitung",                               /* 107:     Lickitung */
    "koffing",    "koffing",                   /* 108-109: Koffing/Weezing */
    "rhyhorn",    "rhyhorn",                   /* 110-111: Rhyhorn/Rhydon */
    "chansey",                                 /* 112:     Chansey */
    "tangela",                                 /* 113:     Tangela */
    "kangaskhan",                              /* 114:     Kangaskhan */
    "horsea",     "horsea",                    /* 115-116: Horsea/Seadra */
    "goldeen",    "goldeen",                   /* 117-118: Goldeen/Seaking */
    "staryu",     "staryu",                    /* 119-120: Staryu/Starmie */
    "mr_mime",                                 /* 121:     Mr. Mime */
    "scyther",                                 /* 122:     Scyther */
    "jynx",                                    /* 123:     Jynx */
    "electabuzz",                              /* 124:     Electabuzz */
    "magmar",                                  /* 125:     Magmar */
    "pinsir",                                  /* 126:     Pinsir */
    "tauros",                                  /* 127:     Tauros */
    "magikarp",   "magikarp",                  /* 128-129: Magikarp/Gyarados */
    "lapras",                                  /* 130:     Lapras */
    "ditto",                                   /* 131:     Ditto */
    "eevee",      "eevee",     "eevee",        /* 132-134: Eevee/Vaporeon/Jolteon */
    "eevee",                                   /* 135:     Flareon */
    "porygon",                                 /* 136:     Porygon */
    "omanyte",    "omanyte",                   /* 137-138: Omanyte/Omastar */
    "kabuto",     "kabuto",                    /* 139-140: Kabuto/Kabutops */
    "aerodactyl",                              /* 141:     Aerodactyl */
    "snorlax",                                 /* 142:     Snorlax */
    "articuno",                                /* 143:     Articuno */
    "zapdos",                                  /* 144:     Zapdos */
    "moltres",                                 /* 145:     Moltres */
    "dratini",    "dratini",   "dratini",      /* 146-148: Dratini/Dragonair/Dragonite */
    "mewtwo",                                  /* 149:     Mewtwo */
    "mew",                                     /* 150:     Mew */
};

/*=============================================================================
 * MonAnimatedPalettePointers (0x1309f): species → animated OBJ palette data.
 * 8 colors per entry: palette1[4] → obj_palettes[3], palette2[4] → obj_palettes[5].
 * From data/mon_gfx/mon_animated_palettes_{1,2,3}.asm
 *===========================================================================*/
#define CGB_RGB(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

static const uint16_t mon_animated_palettes[NUM_POKEMON][8] = {
    /* Bulbasaur family: palette1 != palette2 */
    { CGB_RGB(31,31,31), CGB_RGB( 5,21,30), CGB_RGB( 1, 3,22), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB( 0,21,15), CGB_RGB( 0,12, 6), CGB_RGB( 0, 0, 0) }, /*   0: Bulbasaur */
    { CGB_RGB(31,31,31), CGB_RGB( 5,21,30), CGB_RGB( 1, 3,22), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB( 0,21,15), CGB_RGB( 0,12, 6), CGB_RGB( 0, 0, 0) }, /*   1: Ivysaur */
    { CGB_RGB(31,31,31), CGB_RGB( 5,21,30), CGB_RGB( 1, 3,22), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB( 0,21,15), CGB_RGB( 0,12, 6), CGB_RGB( 0, 0, 0) }, /*   2: Venusaur */
    /* Charmander family */
    { CGB_RGB(31,31,31), CGB_RGB(31,17, 0), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,17, 0), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0) }, /*   3: Charmander */
    { CGB_RGB(31,31,31), CGB_RGB(31,17, 0), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,17, 0), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0) }, /*   4: Charmeleon */
    { CGB_RGB(31,31,31), CGB_RGB(31,17, 0), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,17, 0), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0) }, /*   5: Charizard */
    /* Squirtle family: palette1 != palette2 */
    { CGB_RGB(31,31,31), CGB_RGB( 4,19,31), CGB_RGB( 1, 5,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(14, 9, 3), CGB_RGB( 0, 0, 0) }, /*   6: Squirtle */
    { CGB_RGB(31,31,31), CGB_RGB( 4,19,31), CGB_RGB( 1, 5,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(14, 9, 3), CGB_RGB( 0, 0, 0) }, /*   7: Wartortle */
    { CGB_RGB(31,31,31), CGB_RGB( 4,19,31), CGB_RGB( 1, 5,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(14, 9, 3), CGB_RGB( 0, 0, 0) }, /*   8: Blastoise */
    /* Caterpie family */
    { CGB_RGB(31,31,31), CGB_RGB( 0,25, 9), CGB_RGB(27,13, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB( 0,25, 9), CGB_RGB(27,13, 0), CGB_RGB( 0, 0, 0) }, /*   9: Caterpie */
    { CGB_RGB(31,31,31), CGB_RGB( 0,25, 9), CGB_RGB(27,13, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB( 0,25, 9), CGB_RGB(27,13, 0), CGB_RGB( 0, 0, 0) }, /*  10: Metapod */
    { CGB_RGB(31,31,31), CGB_RGB( 0,25, 9), CGB_RGB(27,13, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB( 0,25, 9), CGB_RGB(27,13, 0), CGB_RGB( 0, 0, 0) }, /*  11: Butterfree */
    /* Weedle family */
    { CGB_RGB(31,31,31), CGB_RGB(31,25, 3), CGB_RGB(25, 9, 7), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,25, 3), CGB_RGB(25, 9, 7), CGB_RGB( 0, 0, 0) }, /*  12: Weedle */
    { CGB_RGB(31,31,31), CGB_RGB(31,25, 3), CGB_RGB(25, 9, 7), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,25, 3), CGB_RGB(25, 9, 7), CGB_RGB( 0, 0, 0) }, /*  13: Kakuna */
    { CGB_RGB(31,31,31), CGB_RGB(31,25, 3), CGB_RGB(25, 9, 7), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,25, 3), CGB_RGB(25, 9, 7), CGB_RGB( 0, 0, 0) }, /*  14: Beedrill */
    /* Pidgey family */
    { CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(21,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(21,10, 4), CGB_RGB( 0, 0, 0) }, /*  15: Pidgey */
    { CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(21,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(21,10, 4), CGB_RGB( 0, 0, 0) }, /*  16: Pidgeotto */
    { CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(21,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(21,10, 4), CGB_RGB( 0, 0, 0) }, /*  17: Pidgeot */
    /* Rattata family */
    { CGB_RGB(31,31,31), CGB_RGB(30,12,23), CGB_RGB(20, 4, 8), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,12,23), CGB_RGB(20, 4, 8), CGB_RGB( 0, 0, 0) }, /*  18: Rattata */
    { CGB_RGB(31,31,31), CGB_RGB(30,12,23), CGB_RGB(20, 4, 8), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,12,23), CGB_RGB(20, 4, 8), CGB_RGB( 0, 0, 0) }, /*  19: Raticate */
    /* Spearow family */
    { CGB_RGB(31,31,31), CGB_RGB(31,22,14), CGB_RGB(24, 4, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,22,14), CGB_RGB(24, 4, 2), CGB_RGB( 0, 0, 0) }, /*  20: Spearow */
    { CGB_RGB(31,31,31), CGB_RGB(31,22,14), CGB_RGB(24, 4, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,22,14), CGB_RGB(24, 4, 2), CGB_RGB( 0, 0, 0) }, /*  21: Fearow */
    /* Ekans family */
    { CGB_RGB(31,31,31), CGB_RGB(30,26,12), CGB_RGB(20, 7,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,26,12), CGB_RGB(20, 7,12), CGB_RGB( 0, 0, 0) }, /*  22: Ekans */
    { CGB_RGB(31,31,31), CGB_RGB(30,26,12), CGB_RGB(20, 7,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,26,12), CGB_RGB(20, 7,12), CGB_RGB( 0, 0, 0) }, /*  23: Arbok */
    /* Pikachu family */
    { CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(23,10, 0), CGB_RGB( 3, 3, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(23,10, 0), CGB_RGB( 3, 3, 0) }, /*  24: Pikachu */
    { CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(23,10, 0), CGB_RGB( 3, 3, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(23,10, 0), CGB_RGB( 3, 3, 0) }, /*  25: Raichu */
    /* Sandshrew family */
    { CGB_RGB(31,31,31), CGB_RGB(30,25, 3), CGB_RGB(19,11, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,25, 3), CGB_RGB(19,11, 0), CGB_RGB( 0, 0, 0) }, /*  26: Sandshrew */
    { CGB_RGB(31,31,31), CGB_RGB(30,25, 3), CGB_RGB(19,11, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,25, 3), CGB_RGB(19,11, 0), CGB_RGB( 0, 0, 0) }, /*  27: Sandslash */
    /* NidoranF family */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,30), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(19,23,30), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0) }, /*  28: NidoranF */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,30), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(19,23,30), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0) }, /*  29: Nidorina */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,30), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(19,23,30), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0) }, /*  30: Nidoqueen */
    /* NidoranM family */
    { CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(20, 5,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(20, 5,12), CGB_RGB( 0, 0, 0) }, /*  31: NidoranM */
    { CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(20, 5,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(20, 5,12), CGB_RGB( 0, 0, 0) }, /*  32: Nidorino */
    { CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(20, 5,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(20, 5,12), CGB_RGB( 0, 0, 0) }, /*  33: Nidoking */
    /* Clefairy family */
    { CGB_RGB(31,31,31), CGB_RGB(31,20,20), CGB_RGB(23, 5, 6), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,20), CGB_RGB(23, 5, 6), CGB_RGB( 0, 0, 0) }, /*  34: Clefairy */
    { CGB_RGB(31,31,31), CGB_RGB(31,20,20), CGB_RGB(23, 5, 6), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20,20), CGB_RGB(23, 5, 6), CGB_RGB( 0, 0, 0) }, /*  35: Clefable */
    /* Vulpix family */
    { CGB_RGB(31,31,31), CGB_RGB(30,20,13), CGB_RGB(27, 8, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,20,13), CGB_RGB(27, 8, 0), CGB_RGB( 0, 0, 0) }, /*  36: Vulpix */
    { CGB_RGB(31,31,31), CGB_RGB(30,20,13), CGB_RGB(27, 8, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,20,13), CGB_RGB(27, 8, 0), CGB_RGB( 0, 0, 0) }, /*  37: Ninetales */
    /* Jigglypuff family */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,18), CGB_RGB( 7, 6,27), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,18), CGB_RGB( 7, 6,27), CGB_RGB( 0, 0, 0) }, /*  38: Jigglypuff */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,18), CGB_RGB( 7, 6,27), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,18), CGB_RGB( 7, 6,27), CGB_RGB( 0, 0, 0) }, /*  39: Wigglytuff */
    /* Zubat family */
    { CGB_RGB(31,31,31), CGB_RGB(15,19,31), CGB_RGB(14, 9,21), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(15,19,31), CGB_RGB(14, 9,21), CGB_RGB( 0, 0, 0) }, /*  40: Zubat */
    { CGB_RGB(31,31,31), CGB_RGB(15,19,31), CGB_RGB(14, 9,21), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(15,19,31), CGB_RGB(14, 9,21), CGB_RGB( 0, 0, 0) }, /*  41: Golbat */
    /* Oddish family */
    { CGB_RGB(31,31,31), CGB_RGB(27,29, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(27,29, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0) }, /*  42: Oddish */
    { CGB_RGB(31,31,31), CGB_RGB(27,29, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(27,29, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0) }, /*  43: Gloom */
    { CGB_RGB(31,31,31), CGB_RGB(27,29, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(27,29, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0) }, /*  44: Vileplume */
    /* Paras family */
    { CGB_RGB(31,31,31), CGB_RGB(31,15, 1), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,15, 1), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0) }, /*  45: Paras */
    { CGB_RGB(31,31,31), CGB_RGB(31,15, 1), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,15, 1), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0) }, /*  46: Parasect */
    /* Venonat family */
    { CGB_RGB(31,31,31), CGB_RGB(24,15,28), CGB_RGB(12, 5,18), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(24,15,28), CGB_RGB(12, 5,18), CGB_RGB( 0, 0, 0) }, /*  47: Venonat */
    { CGB_RGB(31,31,31), CGB_RGB(24,15,28), CGB_RGB(12, 5,18), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(24,15,28), CGB_RGB(12, 5,18), CGB_RGB( 0, 0, 0) }, /*  48: Venomoth */
    /* Diglett family */
    { CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0) }, /*  49: Diglett */
    { CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0) }, /*  50: Dugtrio */
    /* Meowth family */
    { CGB_RGB(31,31,31), CGB_RGB(30,25,16), CGB_RGB(23,12, 6), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,25,16), CGB_RGB(23,12, 6), CGB_RGB( 0, 0, 0) }, /*  51: Meowth */
    { CGB_RGB(31,31,31), CGB_RGB(30,25,16), CGB_RGB(23,12, 6), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,25,16), CGB_RGB(23,12, 6), CGB_RGB( 0, 0, 0) }, /*  52: Persian */
    /* Psyduck family */
    { CGB_RGB(31,31,31), CGB_RGB(31,31, 0), CGB_RGB(19,17, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,31, 0), CGB_RGB(19,17, 0), CGB_RGB( 0, 0, 0) }, /*  53: Psyduck */
    { CGB_RGB(31,31,31), CGB_RGB(31,31, 0), CGB_RGB(19,17, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,31, 0), CGB_RGB(19,17, 0), CGB_RGB( 0, 0, 0) }, /*  54: Golduck */
    /* Mankey family */
    { CGB_RGB(31,31,31), CGB_RGB(31,21,19), CGB_RGB(23, 8, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,21,19), CGB_RGB(23, 8, 4), CGB_RGB( 0, 0, 0) }, /*  55: Mankey */
    { CGB_RGB(31,31,31), CGB_RGB(31,21,19), CGB_RGB(23, 8, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,21,19), CGB_RGB(23, 8, 4), CGB_RGB( 0, 0, 0) }, /*  56: Primeape */
    /* Growlithe family */
    { CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0) }, /*  57: Growlithe */
    { CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18, 1), CGB_RGB(24, 9, 3), CGB_RGB( 0, 0, 0) }, /*  58: Arcanine */
    /* Poliwag family */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  59: Poliwag */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  60: Poliwhirl */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  61: Poliwrath */
    /* Abra family */
    { CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0) }, /*  62: Abra */
    { CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0) }, /*  63: Kadabra */
    { CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0) }, /*  64: Alakazam */
    /* Machop family */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  65: Machop */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  66: Machoke */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  67: Machamp */
    /* Bellsprout family */
    { CGB_RGB(31,31,31), CGB_RGB(29,26, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,26, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0) }, /*  68: Bellsprout */
    { CGB_RGB(31,31,31), CGB_RGB(29,26, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,26, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0) }, /*  69: Weepinbell */
    { CGB_RGB(31,31,31), CGB_RGB(29,26, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,26, 5), CGB_RGB( 5,16, 0), CGB_RGB( 0, 0, 0) }, /*  70: Victreebel */
    /* Tentacool family */
    { CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB( 0,11,22), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB( 0,11,22), CGB_RGB( 0, 0, 0) }, /*  71: Tentacool */
    { CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB( 0,11,22), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB( 0,11,22), CGB_RGB( 0, 0, 0) }, /*  72: Tentacruel */
    /* Geodude family */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,20), CGB_RGB( 8,11, 7), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(19,23,20), CGB_RGB( 8,11, 7), CGB_RGB( 0, 0, 0) }, /*  73: Geodude */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,20), CGB_RGB( 8,11, 7), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(19,23,20), CGB_RGB( 8,11, 7), CGB_RGB( 0, 0, 0) }, /*  74: Graveler */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,20), CGB_RGB( 8,11, 7), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(19,23,20), CGB_RGB( 8,11, 7), CGB_RGB( 0, 0, 0) }, /*  75: Golem */
    /* Ponyta family */
    { CGB_RGB(31,31,31), CGB_RGB(31,28,11), CGB_RGB(31, 6, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,28,11), CGB_RGB(31, 6, 0), CGB_RGB( 0, 0, 0) }, /*  76: Ponyta */
    { CGB_RGB(31,31,31), CGB_RGB(31,28,11), CGB_RGB(31, 6, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,28,11), CGB_RGB(31, 6, 0), CGB_RGB( 0, 0, 0) }, /*  77: Rapidash */
    /* Slowpoke family */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0) }, /*  78: Slowpoke */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0) }, /*  79: Slowbro */
    /* Magnemite family */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  80: Magnemite */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  81: Magneton */
    /* Farfetch'd */
    { CGB_RGB(31,31,31), CGB_RGB(31,22, 5), CGB_RGB(19,11, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,22, 5), CGB_RGB(19,11, 4), CGB_RGB( 0, 0, 0) }, /*  82: Farfetch'd */
    /* Doduo family */
    { CGB_RGB(31,31,31), CGB_RGB(30,20, 5), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,20, 5), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0) }, /*  83: Doduo */
    { CGB_RGB(31,31,31), CGB_RGB(30,20, 5), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,20, 5), CGB_RGB(22, 5, 2), CGB_RGB( 0, 0, 0) }, /*  84: Dodrio */
    /* Seel family */
    { CGB_RGB(31,31,31), CGB_RGB(20,24,29), CGB_RGB( 8,11,20), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(20,24,29), CGB_RGB( 8,11,20), CGB_RGB( 0, 0, 0) }, /*  85: Seel */
    { CGB_RGB(31,31,31), CGB_RGB(20,24,29), CGB_RGB( 8,11,20), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(20,24,29), CGB_RGB( 8,11,20), CGB_RGB( 0, 0, 0) }, /*  86: Dewgong */
    /* Grimer family */
    { CGB_RGB(31,31,31), CGB_RGB(27,15,31), CGB_RGB(16, 7,19), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(27,15,31), CGB_RGB(16, 7,19), CGB_RGB( 0, 0, 0) }, /*  87: Grimer */
    { CGB_RGB(31,31,31), CGB_RGB(27,15,31), CGB_RGB(16, 7,19), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(27,15,31), CGB_RGB(16, 7,19), CGB_RGB( 0, 0, 0) }, /*  88: Muk */
    /* Shellder family */
    { CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0) }, /*  89: Shellder */
    { CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0) }, /*  90: Cloyster */
    /* Gastly family: palette1 != palette2 */
    { CGB_RGB(31,31,31), CGB_RGB(25,17,28), CGB_RGB(12, 7,15), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,23,17), CGB_RGB(23, 8, 4), CGB_RGB( 0, 0, 0) }, /*  91: Gastly */
    { CGB_RGB(31,31,31), CGB_RGB(25,17,28), CGB_RGB(12, 7,15), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,23,17), CGB_RGB(23, 8, 4), CGB_RGB( 0, 0, 0) }, /*  92: Haunter */
    { CGB_RGB(31,31,31), CGB_RGB(25,17,28), CGB_RGB(12, 7,15), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,23,17), CGB_RGB(23, 8, 4), CGB_RGB( 0, 0, 0) }, /*  93: Gengar */
    /* Onix */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(17,19,23), CGB_RGB( 9,10,12), CGB_RGB( 0, 0, 0) }, /*  94: Onix */
    /* Drowzee family */
    { CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0) }, /*  95: Drowzee */
    { CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(30,24, 0), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0) }, /*  96: Hypno */
    /* Krabby family */
    { CGB_RGB(31,31,31), CGB_RGB(31,20, 8), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20, 8), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0) }, /*  97: Krabby */
    { CGB_RGB(31,31,31), CGB_RGB(31,20, 8), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,20, 8), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0) }, /*  98: Kingler */
    /* Voltorb family */
    { CGB_RGB(31,31,31), CGB_RGB(31,17,14), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,17,14), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  99: Voltorb */
    { CGB_RGB(31,31,31), CGB_RGB(31,17,14), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,17,14), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /* 100: Electrode */
    /* Exeggcute family */
    { CGB_RGB(31,31,31), CGB_RGB(31,17,16), CGB_RGB(20, 8, 5), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,17,16), CGB_RGB(20, 8, 5), CGB_RGB( 0, 0, 0) }, /* 101: Exeggcute */
    { CGB_RGB(31,31,31), CGB_RGB(31,17,16), CGB_RGB(20, 8, 5), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,17,16), CGB_RGB(20, 8, 5), CGB_RGB( 0, 0, 0) }, /* 102: Exeggutor */
    /* Cubone family: palette1 != palette2 */
    { CGB_RGB(31,31,31), CGB_RGB(30,15, 5), CGB_RGB(18, 9, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(20,22,29), CGB_RGB(13, 8, 6), CGB_RGB( 0, 0, 0) }, /* 103: Cubone */
    { CGB_RGB(31,31,31), CGB_RGB(30,15, 5), CGB_RGB(18, 9, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(20,22,29), CGB_RGB(13, 8, 6), CGB_RGB( 0, 0, 0) }, /* 104: Marowak */
    /* Hitmonlee */
    { CGB_RGB(31,31,31), CGB_RGB(26,13, 7), CGB_RGB(16,10, 7), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(26,13, 7), CGB_RGB(16,10, 7), CGB_RGB( 0, 0, 0) }, /* 105: Hitmonlee */
    /* Hitmonchan */
    { CGB_RGB(31,31,31), CGB_RGB(27,17,10), CGB_RGB(24, 4, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(27,17,10), CGB_RGB(24, 4, 2), CGB_RGB( 0, 0, 0) }, /* 106: Hitmonchan */
    /* Lickitung */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0) }, /* 107: Lickitung */
    /* Koffing family */
    { CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0) }, /* 108: Koffing */
    { CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(26,19,29), CGB_RGB(15,11,17), CGB_RGB( 0, 0, 0) }, /* 109: Weezing */
    /* Rhyhorn family */
    { CGB_RGB(31,31,31), CGB_RGB(26,13,24), CGB_RGB(15, 4,14), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(26,13,24), CGB_RGB(15, 4,14), CGB_RGB( 0, 0, 0) }, /* 110: Rhyhorn */
    { CGB_RGB(31,31,31), CGB_RGB(26,13,24), CGB_RGB(15, 4,14), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(26,13,24), CGB_RGB(15, 4,14), CGB_RGB( 0, 0, 0) }, /* 111: Rhydon */
    /* Chansey */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(31,11, 9), CGB_RGB( 0, 0, 0) }, /* 112: Chansey */
    /* Tangela */
    { CGB_RGB(31,31,31), CGB_RGB(13,19,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(13,19,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0) }, /* 113: Tangela */
    /* Kangaskhan */
    { CGB_RGB(31,31,31), CGB_RGB(28,21,11), CGB_RGB(16,10, 5), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(28,21,11), CGB_RGB(16,10, 5), CGB_RGB( 0, 0, 0) }, /* 114: Kangaskhan */
    /* Horsea family */
    { CGB_RGB(31,31,31), CGB_RGB(13,19,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(13,19,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0) }, /* 115: Horsea */
    { CGB_RGB(31,31,31), CGB_RGB(13,19,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(13,19,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0) }, /* 116: Seadra */
    /* Goldeen family */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(29, 0, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(29, 0, 0), CGB_RGB( 0, 0, 0) }, /* 117: Goldeen */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(29, 0, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(29, 0, 0), CGB_RGB( 0, 0, 0) }, /* 118: Seaking */
    /* Staryu family */
    { CGB_RGB(31,31,31), CGB_RGB(31,22, 5), CGB_RGB(19, 7, 1), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,22, 5), CGB_RGB(19, 7, 1), CGB_RGB( 0, 0, 0) }, /* 119: Staryu */
    { CGB_RGB(31,31,31), CGB_RGB(31,22, 5), CGB_RGB(19, 7, 1), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,22, 5), CGB_RGB(19, 7, 1), CGB_RGB( 0, 0, 0) }, /* 120: Starmie */
    /* Mr. Mime */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(29, 0, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,18,16), CGB_RGB(29, 0, 0), CGB_RGB( 0, 0, 0) }, /* 121: Mr. Mime */
    /* Scyther */
    { CGB_RGB(31,31,31), CGB_RGB(22,29, 5), CGB_RGB( 6,17, 1), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(22,29, 5), CGB_RGB( 6,17, 1), CGB_RGB( 0, 0, 0) }, /* 122: Scyther */
    /* Jynx */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,16), CGB_RGB(25, 1, 3), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,16,16), CGB_RGB(25, 1, 3), CGB_RGB( 0, 0, 0) }, /* 123: Jynx */
    /* Electabuzz */
    { CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(21,14, 1), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(21,14, 1), CGB_RGB( 0, 0, 0) }, /* 124: Electabuzz */
    /* Magmar */
    { CGB_RGB(31,31,31), CGB_RGB(31,23, 2), CGB_RGB(31, 3, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,23, 2), CGB_RGB(31, 3, 0), CGB_RGB( 0, 0, 0) }, /* 125: Magmar */
    /* Pinsir */
    { CGB_RGB(31,31,31), CGB_RGB(28,20,13), CGB_RGB(17,12, 6), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(28,20,13), CGB_RGB(17,12, 6), CGB_RGB( 0, 0, 0) }, /* 126: Pinsir */
    /* Tauros */
    { CGB_RGB(31,31,31), CGB_RGB(31,21, 5), CGB_RGB(20, 9, 3), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,21, 5), CGB_RGB(20, 9, 3), CGB_RGB( 0, 0, 0) }, /* 127: Tauros */
    /* Magikarp family */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,10), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,16,10), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0) }, /* 128: Magikarp */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,10), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,16,10), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0) }, /* 129: Gyarados */
    /* Lapras */
    { CGB_RGB(31,31,31), CGB_RGB(11,22,31), CGB_RGB( 0,10,30), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(11,22,31), CGB_RGB( 0,10,30), CGB_RGB( 0, 0, 0) }, /* 130: Lapras */
    /* Ditto */
    { CGB_RGB(31,31,31), CGB_RGB(25,18,28), CGB_RGB(15, 7,16), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(25,18,28), CGB_RGB(15, 7,16), CGB_RGB( 0, 0, 0) }, /* 131: Ditto */
    /* Eevee family */
    { CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0) }, /* 132: Eevee */
    { CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0) }, /* 133: Vaporeon */
    { CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0) }, /* 134: Jolteon */
    { CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,20,10), CGB_RGB(17, 9, 4), CGB_RGB( 0, 0, 0) }, /* 135: Flareon */
    /* Porygon */
    { CGB_RGB(31,31,31), CGB_RGB(29, 8,20), CGB_RGB( 0, 0,31), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29, 8,20), CGB_RGB( 0, 0,31), CGB_RGB( 0, 0, 0) }, /* 136: Porygon */
    /* Omanyte family */
    { CGB_RGB(31,31,31), CGB_RGB(13,18,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(13,18,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0) }, /* 137: Omanyte */
    { CGB_RGB(31,31,31), CGB_RGB(13,18,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(13,18,31), CGB_RGB( 1, 6,20), CGB_RGB( 0, 0, 0) }, /* 138: Omastar */
    /* Kabuto family */
    { CGB_RGB(31,31,31), CGB_RGB(29,21, 6), CGB_RGB(20, 7, 1), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,21, 6), CGB_RGB(20, 7, 1), CGB_RGB( 0, 0, 0) }, /* 139: Kabuto */
    { CGB_RGB(31,31,31), CGB_RGB(29,21, 6), CGB_RGB(20, 7, 1), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(29,21, 6), CGB_RGB(20, 7, 1), CGB_RGB( 0, 0, 0) }, /* 140: Kabutops */
    /* Aerodactyl */
    { CGB_RGB(31,31,31), CGB_RGB(25,20,29), CGB_RGB(10, 8,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(25,20,29), CGB_RGB(10, 8,17), CGB_RGB( 0, 0, 0) }, /* 141: Aerodactyl */
    /* Snorlax */
    { CGB_RGB(31,31,31), CGB_RGB(31,25, 9), CGB_RGB(17, 7, 2), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,25, 9), CGB_RGB(17, 7, 2), CGB_RGB( 0, 0, 0) }, /* 142: Snorlax */
    /* Articuno */
    { CGB_RGB(31,31,31), CGB_RGB(11,22,31), CGB_RGB( 0, 4,31), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(11,22,31), CGB_RGB( 0, 4,31), CGB_RGB( 0, 0, 0) }, /* 143: Articuno */
    /* Zapdos */
    { CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(22, 7, 3), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(22, 7, 3), CGB_RGB( 0, 0, 0) }, /* 144: Zapdos */
    /* Moltres */
    { CGB_RGB(31,31,31), CGB_RGB(31,26, 0), CGB_RGB(31, 3, 0), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,26, 0), CGB_RGB(31, 3, 0), CGB_RGB( 0, 0, 0) }, /* 145: Moltres */
    /* Dratini family */
    { CGB_RGB(31,31,31), CGB_RGB(20,22,31), CGB_RGB( 6, 8,18), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(20,22,31), CGB_RGB( 6, 8,18), CGB_RGB( 0, 0, 0) }, /* 146: Dratini */
    { CGB_RGB(31,31,31), CGB_RGB(20,22,31), CGB_RGB( 6, 8,18), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(20,22,31), CGB_RGB( 6, 8,18), CGB_RGB( 0, 0, 0) }, /* 147: Dragonair */
    { CGB_RGB(31,31,31), CGB_RGB(20,22,31), CGB_RGB( 6, 8,18), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(20,22,31), CGB_RGB( 6, 8,18), CGB_RGB( 0, 0, 0) }, /* 148: Dragonite */
    /* Mewtwo */
    { CGB_RGB(31,31,31), CGB_RGB(31,19,27), CGB_RGB(23, 8,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,19,27), CGB_RGB(23, 8,17), CGB_RGB( 0, 0, 0) }, /* 149: Mewtwo */
    /* Mew */
    { CGB_RGB(31,31,31), CGB_RGB(31,19,27), CGB_RGB(23, 8,17), CGB_RGB( 0, 0, 0),
      CGB_RGB(31,31,31), CGB_RGB(31,19,27), CGB_RGB(23, 8,17), CGB_RGB( 0, 0, 0) }, /* 150: Mew */
};

#undef CGB_RGB

/*=============================================================================
 * MonBillboardPicPointers (0x12b50): species -> billboard pic filename.
 * Each species has a unique billboard picture. Used by billboard illumination.
 *===========================================================================*/
static const char *mon_billboard_pic_names[NUM_POKEMON] = {
    "bulbasaur",         /*   0: Bulbasaur */
    "ivysaur",           /*   1: Ivysaur */
    "venusaur",          /*   2: Venusaur */
    "charmander",        /*   3: Charmander */
    "charmeleon",        /*   4: Charmeleon */
    "charizard",         /*   5: Charizard */
    "squirtle",          /*   6: Squirtle */
    "wartortle",         /*   7: Wartortle */
    "blastoise",         /*   8: Blastoise */
    "caterpie",          /*   9: Caterpie */
    "metapod",           /*  10: Metapod */
    "butterfree",        /*  11: Butterfree */
    "weedle",            /*  12: Weedle */
    "kakuna",            /*  13: Kakuna */
    "beedrill",          /*  14: Beedrill */
    "pidgey",            /*  15: Pidgey */
    "pidgeotto",         /*  16: Pidgeotto */
    "pidgeot",           /*  17: Pidgeot */
    "rattata",           /*  18: Rattata */
    "raticate",          /*  19: Raticate */
    "spearow",           /*  20: Spearow */
    "fearow",            /*  21: Fearow */
    "ekans",             /*  22: Ekans */
    "arbok",             /*  23: Arbok */
    "pikachu",           /*  24: Pikachu */
    "raichu",            /*  25: Raichu */
    "sandshrew",         /*  26: Sandshrew */
    "sandslash",         /*  27: Sandslash */
    "nidoran_f",         /*  28: Nidoran_F */
    "nidorina",          /*  29: Nidorina */
    "nidoqueen",         /*  30: Nidoqueen */
    "nidoran_m",         /*  31: Nidoran_M */
    "nidorino",          /*  32: Nidorino */
    "nidoking",          /*  33: Nidoking */
    "clefairy",          /*  34: Clefairy */
    "clefable",          /*  35: Clefable */
    "vulpix",            /*  36: Vulpix */
    "ninetales",         /*  37: Ninetales */
    "jigglypuff",        /*  38: Jigglypuff */
    "wigglytuff",        /*  39: Wigglytuff */
    "zubat",             /*  40: Zubat */
    "golbat",            /*  41: Golbat */
    "oddish",            /*  42: Oddish */
    "gloom",             /*  43: Gloom */
    "vileplume",         /*  44: Vileplume */
    "paras",             /*  45: Paras */
    "parasect",          /*  46: Parasect */
    "venonat",           /*  47: Venonat */
    "venomoth",          /*  48: Venomoth */
    "diglett",           /*  49: Diglett */
    "dugtrio",           /*  50: Dugtrio */
    "meowth",            /*  51: Meowth */
    "persian",           /*  52: Persian */
    "psyduck",           /*  53: Psyduck */
    "golduck",           /*  54: Golduck */
    "mankey",            /*  55: Mankey */
    "primeape",          /*  56: Primeape */
    "growlithe",         /*  57: Growlithe */
    "arcanine",          /*  58: Arcanine */
    "poliwag",           /*  59: Poliwag */
    "poliwhirl",         /*  60: Poliwhirl */
    "poliwrath",         /*  61: Poliwrath */
    "abra",              /*  62: Abra */
    "kadabra",           /*  63: Kadabra */
    "alakazam",          /*  64: Alakazam */
    "machop",            /*  65: Machop */
    "machoke",           /*  66: Machoke */
    "machamp",           /*  67: Machamp */
    "bellsprout",        /*  68: Bellsprout */
    "weepinbell",        /*  69: Weepinbell */
    "victreebel",        /*  70: Victreebel */
    "tentacool",         /*  71: Tentacool */
    "tentacruel",        /*  72: Tentacruel */
    "geodude",           /*  73: Geodude */
    "graveler",          /*  74: Graveler */
    "golem",             /*  75: Golem */
    "ponyta",            /*  76: Ponyta */
    "rapidash",          /*  77: Rapidash */
    "slowpoke",          /*  78: Slowpoke */
    "slowbro",           /*  79: Slowbro */
    "magnemite",         /*  80: Magnemite */
    "magneton",          /*  81: Magneton */
    "farfetch_d",        /*  82: Farfetch_d */
    "doduo",             /*  83: Doduo */
    "dodrio",            /*  84: Dodrio */
    "seel",              /*  85: Seel */
    "dewgong",           /*  86: Dewgong */
    "grimer",            /*  87: Grimer */
    "muk",               /*  88: Muk */
    "shellder",          /*  89: Shellder */
    "cloyster",          /*  90: Cloyster */
    "gastly",            /*  91: Gastly */
    "haunter",           /*  92: Haunter */
    "gengar",            /*  93: Gengar */
    "onix",              /*  94: Onix */
    "drowzee",           /*  95: Drowzee */
    "hypno",             /*  96: Hypno */
    "krabby",            /*  97: Krabby */
    "kingler",           /*  98: Kingler */
    "voltorb",           /*  99: Voltorb */
    "electrode",         /* 100: Electrode */
    "exeggcute",         /* 101: Exeggcute */
    "exeggutor",         /* 102: Exeggutor */
    "cubone",            /* 103: Cubone */
    "marowak",           /* 104: Marowak */
    "hitmonlee",         /* 105: Hitmonlee */
    "hitmonchan",        /* 106: Hitmonchan */
    "lickitung",         /* 107: Lickitung */
    "koffing",           /* 108: Koffing */
    "weezing",           /* 109: Weezing */
    "rhyhorn",           /* 110: Rhyhorn */
    "rhydon",            /* 111: Rhydon */
    "chansey",           /* 112: Chansey */
    "tangela",           /* 113: Tangela */
    "kangaskhan",        /* 114: Kangaskhan */
    "horsea",            /* 115: Horsea */
    "seadra",            /* 116: Seadra */
    "goldeen",           /* 117: Goldeen */
    "seaking",           /* 118: Seaking */
    "staryu",            /* 119: Staryu */
    "starmie",           /* 120: Starmie */
    "mr_mime",           /* 121: Mr_Mime */
    "scyther",           /* 122: Scyther */
    "jynx",              /* 123: Jynx */
    "electabuzz",        /* 124: Electabuzz */
    "magmar",            /* 125: Magmar */
    "pinsir",            /* 126: Pinsir */
    "tauros",            /* 127: Tauros */
    "magikarp",          /* 128: Magikarp */
    "gyarados",          /* 129: Gyarados */
    "lapras",            /* 130: Lapras */
    "ditto",             /* 131: Ditto */
    "eevee",             /* 132: Eevee */
    "vaporeon",          /* 133: Vaporeon */
    "jolteon",           /* 134: Jolteon */
    "flareon",           /* 135: Flareon */
    "porygon",           /* 136: Porygon */
    "omanyte",           /* 137: Omanyte */
    "omastar",           /* 138: Omastar */
    "kabuto",            /* 139: Kabuto */
    "kabutops",          /* 140: Kabutops */
    "aerodactyl",        /* 141: Aerodactyl */
    "snorlax",           /* 142: Snorlax */
    "articuno",          /* 143: Articuno */
    "zapdos",            /* 144: Zapdos */
    "moltres",           /* 145: Moltres */
    "dratini",           /* 146: Dratini */
    "dragonair",         /* 147: Dragonair */
    "dragonite",         /* 148: Dragonite */
    "mewtwo",            /* 149: Mewtwo */
    "mew",               /* 150: Mew */
};

/*=============================================================================
 * MonBillboardPalettePointers (0x12eda): species -> BG palette data.
 * 8 colors per species: palette6[4] + palette7[4]. Loaded into bg_palettes[6..7].
 *===========================================================================*/
#define CGB_RGB(r,g,b) ((uint16_t)((r)|((g)<<5)|((b)<<10)))

static const uint16_t mon_billboard_palettes[NUM_POKEMON][8] = {
    { CGB_RGB(31,31,31), CGB_RGB( 0,19,13), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB( 0,19,13), CGB_RGB( 0, 9, 0), CGB_RGB( 0, 0, 0) }, /*   0: Bulbasaur */
    { CGB_RGB(31,31,31), CGB_RGB( 0,19,13), CGB_RGB( 0,12, 6), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(25,17, 3), CGB_RGB( 0,12, 6), CGB_RGB( 0, 0, 0) }, /*   1: Ivysaur */
    { CGB_RGB(31,31,31), CGB_RGB( 0,19,13), CGB_RGB(26, 1, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB( 0,19,13), CGB_RGB( 5,15, 0), CGB_RGB( 0, 0, 0) }, /*   2: Venusaur */
    { CGB_RGB(31,31,31), CGB_RGB(31,17, 1), CGB_RGB(26, 0, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,17, 1), CGB_RGB(26, 0, 0), CGB_RGB( 3, 2, 0) }, /*   3: Charmander */
    { CGB_RGB(31,31,31), CGB_RGB(31,17, 1), CGB_RGB(26, 4, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,17, 1), CGB_RGB(26, 4, 0), CGB_RGB( 3, 2, 0) }, /*   4: Charmeleon */
    { CGB_RGB(31,31,31), CGB_RGB(31,17, 1), CGB_RGB(26, 4, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,17, 1), CGB_RGB(26, 4, 0), CGB_RGB( 3, 2, 0) }, /*   5: Charizard */
    { CGB_RGB(31,31,31), CGB_RGB(26,23, 0), CGB_RGB( 0,16,31), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(14,27,31), CGB_RGB( 0,16,31), CGB_RGB( 0, 1, 3) }, /*   6: Squirtle */
    { CGB_RGB(31,31,31), CGB_RGB(29,23, 0), CGB_RGB( 0,16,31), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(14,27,31), CGB_RGB( 0,16,31), CGB_RGB( 0, 1, 3) }, /*   7: Wartortle */
    { CGB_RGB(31,31,31), CGB_RGB(27,20,10), CGB_RGB(12, 6, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(11,18,31), CGB_RGB( 2, 6,19), CGB_RGB( 0, 0, 0) }, /*   8: Blastoise */
    { CGB_RGB(31,31,31), CGB_RGB(23,27, 5), CGB_RGB( 3,17, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(23,27, 5), CGB_RGB( 3,17, 0), CGB_RGB( 0, 0, 0) }, /*   9: Caterpie */
    { CGB_RGB(31,31,31), CGB_RGB(23,27, 5), CGB_RGB( 7,18, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(23,27, 5), CGB_RGB( 7,18, 0), CGB_RGB( 0, 0, 0) }, /*  10: Metapod */
    { CGB_RGB(31,31,31), CGB_RGB(31,15, 0), CGB_RGB(31, 0, 1), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(11,13,31), CGB_RGB( 9, 8,18), CGB_RGB( 0, 0, 0) }, /*  11: Butterfree */
    { CGB_RGB(31,31,31), CGB_RGB(29,25, 0), CGB_RGB(25, 6, 7), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(29,25, 0), CGB_RGB(25, 6, 7), CGB_RGB( 3, 2, 0) }, /*  12: Weedle */
    { CGB_RGB(31,31,31), CGB_RGB(28,24, 0), CGB_RGB(18,12, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(28,24, 0), CGB_RGB(18,12, 0), CGB_RGB( 3, 2, 0) }, /*  13: Kakuna */
    { CGB_RGB(31,31,31), CGB_RGB(30,27, 0), CGB_RGB(21, 7, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(30,27, 0), CGB_RGB(21, 7, 0), CGB_RGB( 3, 2, 0) }, /*  14: Beedrill */
    { CGB_RGB(31,31,31), CGB_RGB(30,25, 1), CGB_RGB(26, 9, 3), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(30,25, 1), CGB_RGB(26, 9, 3), CGB_RGB( 3, 2, 0) }, /*  15: Pidgey */
    { CGB_RGB(31,31,31), CGB_RGB(30,21, 0), CGB_RGB(28, 6, 1), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(30,21, 0), CGB_RGB(28, 6, 1), CGB_RGB( 3, 2, 0) }, /*  16: Pidgeotto */
    { CGB_RGB(31,31,31), CGB_RGB(26,23, 0), CGB_RGB(28, 6, 1), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(26,23, 0), CGB_RGB(28, 6, 1), CGB_RGB( 3, 2, 0) }, /*  17: Pidgeot */
    { CGB_RGB(31,31,31), CGB_RGB(30,16,24), CGB_RGB(21, 4, 7), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(30,16,24), CGB_RGB(21, 4, 7), CGB_RGB( 0, 0, 0) }, /*  18: Rattata */
    { CGB_RGB(31,31,31), CGB_RGB(30,24, 7), CGB_RGB(27, 7, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(30,24, 7), CGB_RGB(27, 7, 0), CGB_RGB( 3, 2, 0) }, /*  19: Raticate */
    { CGB_RGB(31,31,31), CGB_RGB(31,24, 2), CGB_RGB(30, 3, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,24, 2), CGB_RGB(30, 3, 0), CGB_RGB( 0, 0, 0) }, /*  20: Spearow */
    { CGB_RGB(31,31,31), CGB_RGB(31,24, 2), CGB_RGB(30, 3, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,24, 2), CGB_RGB(30, 3, 0), CGB_RGB( 0, 0, 0) }, /*  21: Fearow */
    { CGB_RGB(31,31,31), CGB_RGB(30,16,24), CGB_RGB(21, 4, 7), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(30,16,24), CGB_RGB(21, 4, 7), CGB_RGB( 0, 0, 0) }, /*  22: Ekans */
    { CGB_RGB(31,31,31), CGB_RGB(30,16,24), CGB_RGB(26, 1, 5), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(30,16,24), CGB_RGB(26, 1, 5), CGB_RGB( 0, 0, 0) }, /*  23: Arbok */
    { CGB_RGB(31,31,31), CGB_RGB(30,24, 4), CGB_RGB(27, 7, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(30,24, 4), CGB_RGB(27, 7, 0), CGB_RGB( 3, 2, 0) }, /*  24: Pikachu */
    { CGB_RGB(31,31,31), CGB_RGB(30,26, 3), CGB_RGB(29,16, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(30,26, 3), CGB_RGB(29,16, 0), CGB_RGB( 3, 2, 0) }, /*  25: Raichu */
    { CGB_RGB(31,31,31), CGB_RGB(31,25, 7), CGB_RGB(23,14, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,25, 7), CGB_RGB(23,14, 0), CGB_RGB( 0, 0, 0) }, /*  26: Sandshrew */
    { CGB_RGB(31,31,31), CGB_RGB(31,25, 7), CGB_RGB(25,10, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,25, 7), CGB_RGB(25,10, 0), CGB_RGB( 3, 2, 0) }, /*  27: Sandslash */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,31), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,23,31), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0) }, /*  28: NidoranF */
    { CGB_RGB(31,31,31), CGB_RGB(19,23,31), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,23,31), CGB_RGB( 8, 8,24), CGB_RGB( 0, 0, 0) }, /*  29: Nidorina */
    { CGB_RGB(31,31,31), CGB_RGB(10,18,31), CGB_RGB( 6, 5,23), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(10,18,31), CGB_RGB( 6, 5,23), CGB_RGB( 0, 0, 0) }, /*  30: Nidoqueen */
    { CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(17, 1,12), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(28,16,25), CGB_RGB(17, 1,12), CGB_RGB( 0, 0, 0) }, /*  31: NidoranM */
    { CGB_RGB(31,31,31), CGB_RGB(31,15,24), CGB_RGB(21, 3,15), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,15,24), CGB_RGB(21, 3,15), CGB_RGB( 0, 0, 0) }, /*  32: Nidorino */
    { CGB_RGB(31,31,31), CGB_RGB(25,14,31), CGB_RGB(17, 0,26), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(25,14,31), CGB_RGB(17, 0,26), CGB_RGB( 0, 0, 0) }, /*  33: Nidoking */
    { CGB_RGB(31,31,31), CGB_RGB(31,14,18), CGB_RGB(20, 8, 4), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,14,18), CGB_RGB(20, 8, 4), CGB_RGB( 0, 0, 0) }, /*  34: Clefairy */
    { CGB_RGB(31,31,31), CGB_RGB(31,14,18), CGB_RGB(20, 8, 4), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,14,18), CGB_RGB(20, 8, 4), CGB_RGB( 0, 0, 0) }, /*  35: Clefable */
    { CGB_RGB(31,31,31), CGB_RGB(31,17,13), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,17,13), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0) }, /*  36: Vulpix */
    { CGB_RGB(31,31,31), CGB_RGB(28,26, 0), CGB_RGB(23,12, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(28,26, 0), CGB_RGB(23,12, 3), CGB_RGB( 0, 0, 0) }, /*  37: Ninetales */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,19), CGB_RGB(22, 6,11), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,16,19), CGB_RGB(13, 2,21), CGB_RGB( 0, 0, 0) }, /*  38: Jigglypuff */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,19), CGB_RGB(22, 6,11), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,16,19), CGB_RGB(13, 5,19), CGB_RGB( 0, 0, 0) }, /*  39: Wigglytuff */
    { CGB_RGB(31,31,31), CGB_RGB(14,15,30), CGB_RGB(10, 5,26), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(14,15,30), CGB_RGB(10, 5,26), CGB_RGB( 0, 0, 0) }, /*  40: Zubat */
    { CGB_RGB(31,31,31), CGB_RGB(15,15,30), CGB_RGB(10, 5,26), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(15,15,30), CGB_RGB(10, 5,26), CGB_RGB( 0, 0, 0) }, /*  41: Golbat */
    { CGB_RGB(31,31,31), CGB_RGB(22,28, 2), CGB_RGB( 7,18, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31, 6, 0), CGB_RGB( 4, 8,14), CGB_RGB( 0, 0, 0) }, /*  42: Oddish */
    { CGB_RGB(31,31,31), CGB_RGB(30,19,15), CGB_RGB(28, 4, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,20,31), CGB_RGB( 4, 8,14), CGB_RGB( 0, 0, 0) }, /*  43: Gloom */
    { CGB_RGB(31,31,31), CGB_RGB(30,19,15), CGB_RGB(28, 4, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,20,31), CGB_RGB( 4, 8,14), CGB_RGB( 0, 0, 0) }, /*  44: Vileplume */
    { CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(23, 6, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(23, 6, 3), CGB_RGB( 0, 0, 0) }, /*  45: Paras */
    { CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(23, 6, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,20,11), CGB_RGB(23, 6, 3), CGB_RGB( 0, 0, 0) }, /*  46: Parasect */
    { CGB_RGB(31,31,31), CGB_RGB(24,15,28), CGB_RGB(12, 5,18), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(24,15,28), CGB_RGB(12, 5,18), CGB_RGB( 0, 0, 0) }, /*  47: Venonat */
    { CGB_RGB(31,31,31), CGB_RGB(27,17,29), CGB_RGB(17, 7,16), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(27,17,29), CGB_RGB(17, 7,16), CGB_RGB( 0, 0, 0) }, /*  48: Venomoth */
    { CGB_RGB(31,31,31), CGB_RGB(24,17, 5), CGB_RGB(15, 7, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(24,17, 5), CGB_RGB(23, 3, 0), CGB_RGB( 0, 0, 0) }, /*  49: Diglett */
    { CGB_RGB(31,31,31), CGB_RGB(24,17, 5), CGB_RGB(15, 7, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(24,17, 5), CGB_RGB(23, 3, 0), CGB_RGB( 0, 0, 0) }, /*  50: Dugtrio */
    { CGB_RGB(31,31,31), CGB_RGB(29,28, 7), CGB_RGB(19,10, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,28, 7), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  51: Meowth */
    { CGB_RGB(31,31,31), CGB_RGB(29,28, 7), CGB_RGB(19,10, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,28, 7), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  52: Persian */
    { CGB_RGB(31,31,31), CGB_RGB(31,31, 0), CGB_RGB(19,17, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,31, 0), CGB_RGB(19,17, 0), CGB_RGB( 0, 0, 0) }, /*  53: Psyduck */
    { CGB_RGB(31,31,31), CGB_RGB(26,25, 7), CGB_RGB(13,15,27), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31, 0, 0), CGB_RGB(13,15,27), CGB_RGB( 0, 0, 0) }, /*  54: Golduck */
    { CGB_RGB(31,31,31), CGB_RGB(28,20,17), CGB_RGB(22, 9, 5), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(28,20,17), CGB_RGB(22, 9, 5), CGB_RGB( 0, 0, 0) }, /*  55: Mankey */
    { CGB_RGB(31,31,31), CGB_RGB(28,20,17), CGB_RGB(22, 9, 5), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(15,15,15), CGB_RGB(22, 9, 5), CGB_RGB( 0, 0, 0) }, /*  56: Primeape */
    { CGB_RGB(31,31,31), CGB_RGB(28,27,10), CGB_RGB(26,12, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(28,27,10), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  57: Growlithe */
    { CGB_RGB(31,31,31), CGB_RGB(28,27,10), CGB_RGB(26,12, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(28,27,10), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  58: Arcanine */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,27), CGB_RGB(11,11,18), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,17,14), CGB_RGB(11,11,18), CGB_RGB( 0, 0, 0) }, /*  59: Poliwag */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,27), CGB_RGB(11,11,18), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,20,27), CGB_RGB(11,11,18), CGB_RGB( 0, 0, 0) }, /*  60: Poliwhirl */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,27), CGB_RGB(11,11,18), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,20,27), CGB_RGB(11,11,18), CGB_RGB( 0, 0, 0) }, /*  61: Poliwrath */
    { CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(19,11, 6), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(19,11, 6), CGB_RGB( 0, 0, 0) }, /*  62: Abra */
    { CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(19,11, 6), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(21,21,21), CGB_RGB(19,11, 6), CGB_RGB( 0, 0, 0) }, /*  63: Kadabra */
    { CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(19,11, 6), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(21,21,21), CGB_RGB(19,11, 6), CGB_RGB( 0, 0, 0) }, /*  64: Alakazam */
    { CGB_RGB(31,31,31), CGB_RGB(19,19,21), CGB_RGB(12,12,13), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,19,21), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  65: Machop */
    { CGB_RGB(31,31,31), CGB_RGB(18,18,22), CGB_RGB(11,11,14), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(18,18,22), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  66: Machoke */
    { CGB_RGB(31,31,31), CGB_RGB(18,21,22), CGB_RGB( 9,12,13), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(18,21,22), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  67: Machamp */
    { CGB_RGB(31,31,31), CGB_RGB(26,29, 7), CGB_RGB(11,20, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(26,29, 7), CGB_RGB(31,11, 8), CGB_RGB( 0, 0, 0) }, /*  68: Bellsprout */
    { CGB_RGB(31,31,31), CGB_RGB(26,29, 7), CGB_RGB(11,20, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(26,29, 7), CGB_RGB(31,11, 8), CGB_RGB( 0, 0, 0) }, /*  69: Weepinbell */
    { CGB_RGB(31,31,31), CGB_RGB(29,31, 9), CGB_RGB(11,20, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,31, 9), CGB_RGB(31,11, 8), CGB_RGB( 0, 0, 0) }, /*  70: Victreebel */
    { CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB( 0,11,22), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB(31, 5, 6), CGB_RGB( 0, 0, 0) }, /*  71: Tentacool */
    { CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB( 0,11,22), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(16,22,31), CGB_RGB(31, 5, 6), CGB_RGB( 0, 0, 0) }, /*  72: Tentacruel */
    { CGB_RGB(31,31,31), CGB_RGB(20,23,22), CGB_RGB(10,13,12), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,23,22), CGB_RGB(10,13,12), CGB_RGB( 0, 0, 0) }, /*  73: Geodude */
    { CGB_RGB(31,31,31), CGB_RGB(20,23,22), CGB_RGB(10,13,12), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,23,22), CGB_RGB(10,13,12), CGB_RGB( 0, 0, 0) }, /*  74: Graveler */
    { CGB_RGB(31,31,31), CGB_RGB(26,25,15), CGB_RGB(10,13,12), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(26,25,15), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  75: Golem */
    { CGB_RGB(31,31,31), CGB_RGB(27,26,11), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  76: Ponyta */
    { CGB_RGB(31,31,31), CGB_RGB(27,26,11), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,29, 0), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  77: Rapidash */
    { CGB_RGB(31,31,31), CGB_RGB(31,21,21), CGB_RGB(31,11,11), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,27,15), CGB_RGB(31,11,11), CGB_RGB( 0, 0, 0) }, /*  78: Slowpoke */
    { CGB_RGB(31,31,31), CGB_RGB(31,27,15), CGB_RGB(31,11,11), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(23,23,23), CGB_RGB(12,12,12), CGB_RGB( 0, 0, 0) }, /*  79: Slowbro */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,26), CGB_RGB(11,11,20), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,20,26), CGB_RGB(31, 0, 0), CGB_RGB( 0, 0, 0) }, /*  80: Magnemite */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,26), CGB_RGB(11,11,20), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,20,26), CGB_RGB(11,11,20), CGB_RGB( 0, 0, 0) }, /*  81: Magneton */
    { CGB_RGB(31,31,31), CGB_RGB(31,29,13), CGB_RGB(20,12, 9), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(21,31,17), CGB_RGB( 7,20, 6), CGB_RGB( 0, 0, 0) }, /*  82: Farfetchd */
    { CGB_RGB(31,31,31), CGB_RGB(29,26,14), CGB_RGB(26,16, 4), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,26,14), CGB_RGB(26,16, 4), CGB_RGB( 0, 0, 0) }, /*  83: Doduo */
    { CGB_RGB(31,31,31), CGB_RGB(29,26,14), CGB_RGB(26,16, 4), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,26,14), CGB_RGB(26,16, 4), CGB_RGB( 0, 0, 0) }, /*  84: Dodrio */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,26), CGB_RGB(11,11,20), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,26,14), CGB_RGB(31,11,10), CGB_RGB( 0, 0, 0) }, /*  85: Seel */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,26), CGB_RGB(11,11,20), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,20,26), CGB_RGB(11,11,20), CGB_RGB( 0, 0, 0) }, /*  86: Dewgong */
    { CGB_RGB(31,31,31), CGB_RGB(27,18,30), CGB_RGB(15, 7,19), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(27,18,30), CGB_RGB(15, 7,19), CGB_RGB( 0, 0, 0) }, /*  87: Grimer */
    { CGB_RGB(31,31,31), CGB_RGB(27,18,30), CGB_RGB(15, 7,19), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(27,18,30), CGB_RGB(15, 7,19), CGB_RGB( 0, 0, 0) }, /*  88: Muk */
    { CGB_RGB(31,31,31), CGB_RGB(24,21,25), CGB_RGB(13,11,15), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(24,21,25), CGB_RGB(31,13,13), CGB_RGB( 0, 0, 0) }, /*  89: Shellder */
    { CGB_RGB(31,31,31), CGB_RGB(25,21,26), CGB_RGB(14,11,16), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(25,21,26), CGB_RGB(14,11,16), CGB_RGB( 0, 0, 0) }, /*  90: Cloyster */
    { CGB_RGB(31,31,31), CGB_RGB(26,18,27), CGB_RGB(15, 8,16), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(26,18,27), CGB_RGB(26,10, 8), CGB_RGB( 0, 0, 0) }, /*  91: Gastly */
    { CGB_RGB(31,31,31), CGB_RGB(26,18,27), CGB_RGB(15, 8,16), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,13,13), CGB_RGB(14, 9,15), CGB_RGB( 0, 0, 0) }, /*  92: Haunter */
    { CGB_RGB(31,31,31), CGB_RGB(18,21,23), CGB_RGB(10,12,13), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,11, 6), CGB_RGB(10,12,13), CGB_RGB( 0, 0, 0) }, /*  93: Gengar */
    { CGB_RGB(31,31,31), CGB_RGB(20,20,24), CGB_RGB(10,10,14), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,20,24), CGB_RGB(10,10,14), CGB_RGB( 0, 0, 0) }, /*  94: Onix */
    { CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(21,19, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(21,19, 0), CGB_RGB( 0, 0, 0) }, /*  95: Drowzee */
    { CGB_RGB(31,31,31), CGB_RGB(31,30, 0), CGB_RGB(21,15, 5), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,23,31), CGB_RGB(21,15, 5), CGB_RGB( 0, 0, 0) }, /*  96: Hypno */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,17), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,16,17), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0) }, /*  97: Krabby */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,17), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,16,17), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0) }, /*  98: Kingler */
    { CGB_RGB(31,31,31), CGB_RGB(31,16,17), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,23,31), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0) }, /*  99: Voltorb */
    { CGB_RGB(31,31,31), CGB_RGB(31,15,12), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(19,23,31), CGB_RGB(25, 6, 0), CGB_RGB( 0, 0, 0) }, /* 100: Electrode */
    { CGB_RGB(31,31,31), CGB_RGB(31,15,12), CGB_RGB(18, 8, 6), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,15,12), CGB_RGB(18, 8, 6), CGB_RGB( 0, 0, 0) }, /* 101: Exeggcute */
    { CGB_RGB(31,31,31), CGB_RGB(31,27, 5), CGB_RGB( 7,18, 0), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,23, 5), CGB_RGB(20,10, 3), CGB_RGB( 0, 0, 0) }, /* 102: Exeggutor */
    { CGB_RGB(31,31,31), CGB_RGB(18,20,27), CGB_RGB(20,10, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,23,10), CGB_RGB(20,10, 3), CGB_RGB( 0, 0, 0) }, /* 103: Cubone */
    { CGB_RGB(31,31,31), CGB_RGB(18,20,27), CGB_RGB(20,10, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,23,10), CGB_RGB(20,10, 3), CGB_RGB( 0, 0, 0) }, /* 104: Marowak */
    { CGB_RGB(31,31,31), CGB_RGB(29,23,10), CGB_RGB(21,13, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,23,10), CGB_RGB(21,13, 3), CGB_RGB( 0, 0, 0) }, /* 105: Hitmonlee */
    { CGB_RGB(31,31,31), CGB_RGB(31,21,13), CGB_RGB(23, 3, 3), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(31,21,13), CGB_RGB(22, 3,25), CGB_RGB( 0, 0, 0) }, /* 106: Hitmonchan */
    { CGB_RGB(31,31,31), CGB_RGB(31,21,21), CGB_RGB(31, 9, 8), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,20,12), CGB_RGB(31, 9, 8), CGB_RGB( 0, 0, 0) }, /* 107: Lickitung */
    { CGB_RGB(31,31,31), CGB_RGB(21,13,28), CGB_RGB(10, 7,14), CGB_RGB( 3, 2, 0), CGB_RGB(31,31,31), CGB_RGB(21,13,28), CGB_RGB(21, 4, 7), CGB_RGB( 0, 0, 0) }, /* 108: Koffing */
    { CGB_RGB(31,31,31), CGB_RGB(21,13,28), CGB_RGB(10, 7,14), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(21,13,28), CGB_RGB(21, 4, 7), CGB_RGB( 3, 2, 0) }, /* 109: Weezing */
    { CGB_RGB(31,31,31), CGB_RGB(21,21,21), CGB_RGB(13,11,16), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(21,21,21), CGB_RGB(13,11,16), CGB_RGB( 0, 0, 0) }, /* 110: Rhyhorn */
    { CGB_RGB(31,31,31), CGB_RGB(21,21,21), CGB_RGB(13,11,16), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(21,21,21), CGB_RGB(13,11,16), CGB_RGB( 0, 0, 0) }, /* 111: Rhydon */
    { CGB_RGB(31,31,31), CGB_RGB(31,15,20), CGB_RGB(27, 5, 7), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,15,20), CGB_RGB(27, 5, 7), CGB_RGB( 0, 0, 0) }, /* 112: Chansey */
    { CGB_RGB(31,31,31), CGB_RGB(15,21,29), CGB_RGB( 4, 8,18), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(15,21,29), CGB_RGB( 4, 8,18), CGB_RGB( 0, 0, 0) }, /* 113: Tangela */
    { CGB_RGB(31,31,31), CGB_RGB(31,18, 9), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,18, 9), CGB_RGB(17,10, 4), CGB_RGB( 0, 0, 0) }, /* 114: Kangaskhan */
    { CGB_RGB(31,31,31), CGB_RGB(12,19,31), CGB_RGB( 4, 7,22), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(25,26, 3), CGB_RGB( 4, 7,22), CGB_RGB( 0, 0, 0) }, /* 115: Horsea */
    { CGB_RGB(31,31,31), CGB_RGB(12,19,31), CGB_RGB( 4, 7,22), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(12,19,31), CGB_RGB( 4, 7,22), CGB_RGB( 0, 0, 0) }, /* 116: Seadra */
    { CGB_RGB(31,31,31), CGB_RGB(30,16, 4), CGB_RGB(29, 3, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(30,16, 4), CGB_RGB(29, 3, 0), CGB_RGB( 0, 0, 0) }, /* 117: Goldeen */
    { CGB_RGB(31,31,31), CGB_RGB(29,17, 5), CGB_RGB(31, 5, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,17, 5), CGB_RGB(31, 5, 0), CGB_RGB( 0, 0, 0) }, /* 118: Seaking */
    { CGB_RGB(31,31,31), CGB_RGB(31,20, 0), CGB_RGB(19, 3, 6), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,16, 8), CGB_RGB(25, 3, 2), CGB_RGB( 0, 0, 0) }, /* 119: Staryu */
    { CGB_RGB(31,31,31), CGB_RGB(31,17, 0), CGB_RGB(15, 8,16), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,16, 8), CGB_RGB(25, 3, 2), CGB_RGB( 0, 0, 0) }, /* 120: Starmie */
    { CGB_RGB(31,31,31), CGB_RGB(31,17,19), CGB_RGB(28, 6, 4), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,17,19), CGB_RGB(28, 6, 4), CGB_RGB( 0, 0, 0) }, /* 121: MrMime */
    { CGB_RGB(31,31,31), CGB_RGB(17,31, 4), CGB_RGB( 4,16, 4), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(17,31, 4), CGB_RGB( 4,16, 4), CGB_RGB( 0, 0, 0) }, /* 122: Scyther */
    { CGB_RGB(31,31,31), CGB_RGB(29,13,15), CGB_RGB(30, 6, 1), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,21, 0), CGB_RGB(30, 6, 1), CGB_RGB( 0, 0, 0) }, /* 123: Jynx */
    { CGB_RGB(31,31,31), CGB_RGB(31,27, 0), CGB_RGB(19,11, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,27, 0), CGB_RGB(19,11, 0), CGB_RGB( 0, 0, 0) }, /* 124: Electabuzz */
    { CGB_RGB(31,31,31), CGB_RGB(31,27, 0), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,27, 0), CGB_RGB(28, 6, 0), CGB_RGB( 0, 0, 0) }, /* 125: Magmar */
    { CGB_RGB(31,31,31), CGB_RGB(17,23,10), CGB_RGB(21,10, 3), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,19, 8), CGB_RGB(21,10, 3), CGB_RGB( 0, 0, 0) }, /* 126: Pinsir */
    { CGB_RGB(31,31,31), CGB_RGB(31,18, 7), CGB_RGB(17, 9, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(14,16,20), CGB_RGB(17, 9, 0), CGB_RGB( 0, 0, 0) }, /* 127: Tauros */
    { CGB_RGB(31,31,31), CGB_RGB(29,28, 4), CGB_RGB(31, 7, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,14,12), CGB_RGB(31, 7, 0), CGB_RGB( 0, 0, 0) }, /* 128: Magikarp */
    { CGB_RGB(31,31,31), CGB_RGB(12,18,31), CGB_RGB( 3, 9,14), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,14,16), CGB_RGB(15, 3, 0), CGB_RGB( 0, 0, 0) }, /* 129: Gyarados */
    { CGB_RGB(31,31,31), CGB_RGB(12,19,31), CGB_RGB( 5, 8,19), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(12,19,31), CGB_RGB( 5, 8,19), CGB_RGB( 0, 0, 0) }, /* 130: Lapras */
    { CGB_RGB(31,31,31), CGB_RGB(26, 9,21), CGB_RGB(15, 2,10), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(26, 9,21), CGB_RGB(15, 2,10), CGB_RGB( 0, 0, 0) }, /* 131: Ditto */
    { CGB_RGB(31,31,31), CGB_RGB(25,16, 4), CGB_RGB(12, 7, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(25,16, 4), CGB_RGB(12, 7, 0), CGB_RGB( 0, 0, 0) }, /* 132: Eevee */
    { CGB_RGB(31,31,31), CGB_RGB(10,18,29), CGB_RGB( 4, 6,14), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,28, 7), CGB_RGB( 4, 6,14), CGB_RGB( 0, 0, 0) }, /* 133: Vaporeon */
    { CGB_RGB(31,31,31), CGB_RGB(31,26, 0), CGB_RGB(15,10, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,26, 0), CGB_RGB(15,10, 0), CGB_RGB( 0, 0, 0) }, /* 134: Jolteon */
    { CGB_RGB(31,31,31), CGB_RGB(31,27, 0), CGB_RGB(31, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,27, 0), CGB_RGB(31, 6, 0), CGB_RGB( 0, 0, 0) }, /* 135: Flareon */
    { CGB_RGB(31,31,31), CGB_RGB(29,12,13), CGB_RGB( 2,10,17), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB( 5,23,31), CGB_RGB( 2,10,17), CGB_RGB( 0, 0, 0) }, /* 136: Porygon */
    { CGB_RGB(31,31,31), CGB_RGB(22,21,14), CGB_RGB( 0,15,25), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(11,26,29), CGB_RGB( 0,15,25), CGB_RGB( 0, 0, 0) }, /* 137: Omanyte */
    { CGB_RGB(31,31,31), CGB_RGB(22,21,14), CGB_RGB( 0,15,25), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(11,26,29), CGB_RGB( 0,15,25), CGB_RGB( 0, 0, 0) }, /* 138: Omastar */
    { CGB_RGB(31,31,31), CGB_RGB(29,18, 0), CGB_RGB(14, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31, 6, 0), CGB_RGB(29,18, 0), CGB_RGB(14, 6, 0), CGB_RGB( 0, 0, 0) }, /* 139: Kabuto */
    { CGB_RGB(31,31,31), CGB_RGB(31,22,13), CGB_RGB(19,12, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(16,25,12), CGB_RGB(19,12, 0), CGB_RGB( 0, 0, 0) }, /* 140: Kabutops */
    { CGB_RGB(31,31,31), CGB_RGB(20,18,31), CGB_RGB( 8, 6,15), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(20,18,31), CGB_RGB(17, 3,25), CGB_RGB( 0, 0, 0) }, /* 141: Aerodactyl */
    { CGB_RGB(31,31,31), CGB_RGB(31,27, 9), CGB_RGB( 5, 6,14), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,27, 9), CGB_RGB(28, 6, 2), CGB_RGB( 0, 0, 0) }, /* 142: Snorlax */
    { CGB_RGB(31,31,31), CGB_RGB(13,27,29), CGB_RGB( 5,13,24), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(13,27,29), CGB_RGB( 5,13,24), CGB_RGB( 0, 0, 0) }, /* 143: Articuno */
    { CGB_RGB(31,31,31), CGB_RGB(29,27, 0), CGB_RGB(20, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(29,27, 0), CGB_RGB(20, 6, 0), CGB_RGB( 0, 0, 0) }, /* 144: Zapdos */
    { CGB_RGB(31,31,31), CGB_RGB(30,25, 0), CGB_RGB(30, 6, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(30,25, 0), CGB_RGB(30, 6, 0), CGB_RGB( 0, 0, 0) }, /* 145: Moltres */
    { CGB_RGB(31,31,31), CGB_RGB(17,19,24), CGB_RGB( 6,11,15), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(17,19,24), CGB_RGB( 6,11,15), CGB_RGB( 0, 0, 0) }, /* 146: Dratini */
    { CGB_RGB(31,31,31), CGB_RGB( 9,19,30), CGB_RGB( 2, 4,26), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB( 9,19,30), CGB_RGB( 2, 4,26), CGB_RGB( 0, 0, 0) }, /* 147: Dragonair */
    { CGB_RGB(31,31,31), CGB_RGB(31,23, 7), CGB_RGB(27,11, 0), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(13,22,16), CGB_RGB(27,11, 0), CGB_RGB( 0, 0, 0) }, /* 148: Dragonite */
    { CGB_RGB(31,31,31), CGB_RGB(28,23,28), CGB_RGB(13, 7,20), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(28,23,28), CGB_RGB(20, 5,18), CGB_RGB( 0, 0, 0) }, /* 149: Mewtwo */
    { CGB_RGB(31,31,31), CGB_RGB(31,18,24), CGB_RGB(31, 7,12), CGB_RGB( 0, 0, 0), CGB_RGB(31,31,31), CGB_RGB(31,18,24), CGB_RGB( 0,10,31), CGB_RGB( 0, 0, 0) }, /* 150: Mew */
};

#undef CGB_RGB

/*=============================================================================
 * MonBillboardPaletteMapPointers (0x12d15): species -> per-tile palette attrs.
 * 24 bytes per species (0x06 or 0x07). Written to bg_map bank 1 attributes.
 *===========================================================================*/
static const uint8_t mon_billboard_palette_maps[NUM_POKEMON][24] = {
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07 }, /*   0: Bulbasaur */
    { 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x07, 0x06, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07 }, /*   1: Ivysaur */
    { 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07 }, /*   2: Venusaur */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*   3: Charmander */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*   4: Charmeleon */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*   5: Charizard */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07 }, /*   6: Squirtle */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07 }, /*   7: Wartortle */
    { 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*   8: Blastoise */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*   9: Caterpie */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  10: Metapod */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  11: Butterfree */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  12: Weedle */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  13: Kakuna */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  14: Beedrill */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  15: Pidgey */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  16: Pidgeotto */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  17: Pidgeot */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  18: Rattata */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  19: Raticate */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  20: Spearow */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  21: Fearow */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  22: Ekans */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  23: Arbok */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  24: Pikachu */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  25: Raichu */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  26: Sandshrew */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  27: Sandslash */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  28: NidoranF */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  29: Nidorina */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  30: Nidoqueen */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  31: NidoranM */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  32: Nidorino */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  33: Nidoking */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  34: Clefairy */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  35: Clefable */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  36: Vulpix */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  37: Ninetales */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  38: Jigglypuff */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x07, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  39: Wigglytuff */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  40: Zubat */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  41: Golbat */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06 }, /*  42: Oddish */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /*  43: Gloom */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06 }, /*  44: Vileplume */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  45: Paras */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  46: Parasect */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  47: Venonat */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  48: Venomoth */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06 }, /*  49: Diglett */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  50: Dugtrio */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06 }, /*  51: Meowth */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  52: Persian */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  53: Psyduck */
    { 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  54: Golduck */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  55: Mankey */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  56: Primeape */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06 }, /*  57: Growlithe */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  58: Arcanine */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  59: Poliwag */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  60: Poliwhirl */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  61: Poliwrath */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  62: Abra */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  63: Kadabra */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  64: Alakazam */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  65: Machop */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  66: Machoke */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  67: Machamp */
    { 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07 }, /*  68: Bellsprout */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06 }, /*  69: Weepinbell */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  70: Victreebel */
    { 0x06, 0x07, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  71: Tentacool */
    { 0x06, 0x06, 0x07, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  72: Tentacruel */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  73: Geodude */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  74: Graveler */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06 }, /*  75: Golem */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  76: Ponyta */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06 }, /*  77: Rapidash */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06 }, /*  78: Slowpoke */
    { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  79: Slowbro */
    { 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  80: Magnemite */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  81: Magneton */
    { 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  82: Farfetchd */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  83: Doduo */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  84: Dodrio */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06 }, /*  85: Seel */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  86: Dewgong */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  87: Grimer */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  88: Muk */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06 }, /*  89: Shellder */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  90: Cloyster */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  91: Gastly */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  92: Haunter */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  93: Gengar */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  94: Onix */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  95: Drowzee */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07 }, /*  96: Hypno */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  97: Krabby */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /*  98: Kingler */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07 }, /*  99: Voltorb */
    { 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 100: Electrode */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 101: Exeggcute */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06 }, /* 102: Exeggutor */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06 }, /* 103: Cubone */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07 }, /* 104: Marowak */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 105: Hitmonlee */
    { 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 }, /* 106: Hitmonchan */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06 }, /* 107: Lickitung */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 108: Koffing */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 109: Weezing */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 110: Rhyhorn */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 111: Rhydon */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 112: Chansey */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 113: Tangela */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 114: Kangaskhan */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07 }, /* 115: Horsea */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 116: Seadra */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 117: Goldeen */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 118: Seaking */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 119: Staryu */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 120: Starmie */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 121: MrMime */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 122: Scyther */
    { 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06 }, /* 123: Jynx */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 124: Electabuzz */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 125: Magmar */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07 }, /* 126: Pinsir */
    { 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 127: Tauros */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06 }, /* 128: Magikarp */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06 }, /* 129: Gyarados */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 130: Lapras */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 131: Ditto */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 132: Eevee */
    { 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 133: Vaporeon */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 134: Jolteon */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 135: Flareon */
    { 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x07, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06 }, /* 136: Porygon */
    { 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06 }, /* 137: Omanyte */
    { 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x06, 0x06, 0x07, 0x06, 0x06, 0x07 }, /* 138: Omastar */
    { 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 139: Kabuto */
    { 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x06, 0x07, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06 }, /* 140: Kabutops */
    { 0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 141: Aerodactyl */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 142: Snorlax */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 143: Articuno */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 144: Zapdos */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 145: Moltres */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 146: Dratini */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 147: Dragonair */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07 }, /* 148: Dragonite */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 149: Mewtwo */
    { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 }, /* 150: Mew */
};


/* LoadWildMonCollisionMask (0x10464)
 * Loads 0x80 bytes of 1bpp collision mask data for the current catch'em species
 * into mon_animated_collision_mask[]. Used by tile collision for attrs 0xD0-0xDF. */
void load_wild_mon_collision_mask(GameState *state) {
    uint8_t species = state->current_catchem_mon;
    if (species >= NUM_POKEMON) return;

    const char *name;
    if (state->config->pokemon.pokemon_loaded && state->config->pokemon.species[species].collision_mask_name[0])
        name = state->config->pokemon.species[species].collision_mask_name;
    else
        name = mon_collision_mask_names[species];
    char path[260];
    snprintf(path, sizeof(path), "%s/data/collision/mon_masks/%s_collision.png",
             state->asset_base_path, name);
    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }

    size_t data_size = 0;
    uint8_t *mask_data = masks_from_png(path, &data_size);
    if (mask_data) {
        size_t copy_size = data_size < 0x80 ? data_size : 0x80;
        memcpy(state->mon_animated_collision_mask, mask_data, copy_size);
        free(mask_data);
    }
}

/* StartCatchEmMode (0x1003f) */
void start_catchem_mode(GameState *state) {
    if (state->in_special_mode) return;

    state->in_special_mode = 1;
    state->special_mode = 0; /* SPECIAL_MODE_CATCHEM */
    state->special_mode_state = 0;

    /* InitBallSaverForCatchEmMode: backup current saver, set infinite */
    state->ball_saver_timer_frames_backup = state->ball_saver_timer_frames;
    state->ball_saver_timer_seconds_backup = state->ball_saver_timer_seconds;
    state->num_times_ball_saved_text_will_display_backup = state->num_times_ball_saved_text_will_display;
    state->ball_saver_timer_frames = 59;
    state->ball_saver_timer_seconds = 60;
    state->num_times_ball_saved_text_will_display = 0xFF;

    /* Mon selection: multi-level table lookup matching ASM algorithm */
    {
        int is_blue = (state->current_stage >= STAGE_BLUE_FIELD_TOP);
        const uint8_t (*wild_table)[32];
        const uint8_t *map_idx_table;
        if (state->config->pokemon.pokemon_loaded) {
            wild_table = is_blue
                ? (const uint8_t (*)[32])state->config->pokemon.blue_wild_mons
                : (const uint8_t (*)[32])state->config->pokemon.red_wild_mons;
            map_idx_table = is_blue
                ? state->config->pokemon.blue_map_indices
                : state->config->pokemon.red_map_indices;
        } else {
            wild_table = is_blue ? blue_wild_mons : red_wild_mons;
            map_idx_table = is_blue ? blue_wild_mon_map_index : red_wild_mon_map_index;
        }

        uint8_t map_idx = map_idx_table[state->current_map];
        if (map_idx == 0xFF) map_idx = 0; /* fallback for unused maps */

        /* Random slot selection (equal probability, 16 entries) */
        uint8_t slot = gen_random(state) & 0x0F;

        /* CheckForMew: all 4 conditions must be true */
        if (slot == 0x0F &&
            state->current_map == MAP_INDIGO_PLATEAU &&
            state->rare_mons_flag == 0x08 &&
            state->num_mewtwo_bonus_completions >= 2) {
            state->current_catchem_mon = MON_MEW - 1; /* 0-based */
            state->num_mewtwo_bonus_completions = 0;
        } else {
            /* Apply rare offset: rare_mons_flag $00→0, $08→16 */
            uint8_t rare_offset = (state->rare_mons_flag & 0x08) ? 16 : 0;
            uint8_t species = wild_table[map_idx][slot + rare_offset];
            state->current_catchem_mon = species - 1; /* Convert to 0-based */
        }
    }

    /* Billboard illumination init: first N tiles ON, rest OFF */
    uint8_t flipped = state->number_of_catch_mode_tiles_flipped;
    for (int i = 0; i < 24; i++) {
        if ((uint8_t)i < flipped) {
            state->billboard_tiles_illumination_states[i * 2] = 1;
            state->billboard_tiles_illumination_states[i * 2 + 1] = 0;
        } else {
            state->billboard_tiles_illumination_states[i * 2] = 0;
            state->billboard_tiles_illumination_states[i * 2 + 1] = 1;
        }
    }

    /* Start 2-minute timer (all mons use 2:00 in shipped game) */
    start_timer(state, 2, 0);

    /* Display "LET'S GET POKEMON" text */
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);

    /* ASM: Load catch bar tiles on bottom stage (catchem_mode.asm lines 133-145).
     * StageRedFieldBottomBaseGameBoyColorGfx+$300 → vTilesSH tile $2E ($8AE0), 2 tiles.
     * CatchBarTiles → BG Map at coord (6, 8) = offset 0x106. */
    if (state->current_stage & 1) {
        /* Load 2 tiles ($20 bytes) from bottom field base GBC PNG offset $300 */
        const char *sub = (state->current_stage >= STAGE_BLUE_FIELD_TOP)
            ? "blue_bottom/blue_bottom_base_gameboycolor"
            : "red_bottom/red_bottom_base_gameboycolor";
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/%s.png",
                 state->asset_base_path, sub);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data && data_size >= 0x320) {
            vram_write(state->vram, 0, 0x8AE0, tile_data + 0x300, 0x20);
        }
        free(tile_data);

        /* CatchBarTiles: tilemap for the "CATCH!" bar under the wild pokemon */
        static const uint8_t catch_bar_tiles[8] = {
            0x80, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0x80
        };
        uint8_t *bg = state->vram->bg_map[0];
        for (int i = 0; i < 8; i++) {
            bg[0x106 + i] = catch_bar_tiles[i];
        }
    }

    /* Set sprite animation durations from per-species table */
    {
        uint8_t idle1, idle2, hit;
        get_catch_sprite_frame_durations_for_mon(state, state->current_catchem_mon,
                                                  &idle1, &idle2, &hit);
        state->current_catch_mon_idle_frame1_duration = idle1;
        state->loops_until_next_catch_sprite_anim_change = idle1;
        state->current_catch_mon_idle_frame2_duration = idle2;
        state->current_catch_mon_hit_frame_duration = hit;
    }

    /* ASM: SetPokemonSeenFlag — mark encountered mon as seen */
    if (state->current_catchem_mon < NUM_POKEMON) {
        state->pokedex_flags[state->current_catchem_mon] |= 0x01;  /* Seen flag = bit 0 */
    }

    /* Stage-specific initialization dispatch — ASM JumpTable at catchem_mode.asm line 149.
     * Red field: Func_10871 (0x10871). Blue field: Func_1098c (0x1098c). */
    {
        int is_blue = (state->current_stage >= STAGE_BLUE_FIELD_TOP);

        /* Load indicator states — all CatchEmModeInitialIndicatorStates entries are
         * identical: 19 bytes of zeros with byte[9] = 1 (data at 0x123ae) */
        memset(state->indicator_states, 0, 0x13);
        state->indicator_states[9] = 1;

        /* Clear right alley count (ASM lines 1250-1251 / 1385-1386) */
        state->right_alley_count = 0;

        /* CloseSlotCave (ASM line 1252 / 1387) */
        close_slot_cave(state);

        if (!is_blue) {
            /* Red field: wRedStageStructureBackup = 4 (ASM line 1253-1254) */
            state->red_stage_structure_backup = 4;
        }

        /* Play catch'em music — bank 0x0F id 0x02 (red), bank 0x10 id 0x02 (blue)
         * ASM lines 1255-1256 / 1388-1389 */
        audio_play_music(state->audio, is_blue ? 0x10 : 0x0F, 0x02);

        if (!(state->current_stage & 1)) {
            /* Top stage */
            if (!is_blue) {
                /* Red top: LoadStageCollisionAttributes + LoadFieldStructureGraphics_RedField
                 * (ASM lines 1260-1261) */
                load_stage_collision_attributes(state);
                load_field_structure_graphics(state);
            }
            /* Blue top: no extra init (ASM: ret z at line 1392) */
        } else {
            /* Bottom stage */
            if (!is_blue) {
                /* Red bottom: ClearAllRedIndicators (ASM line 1265) */
                clear_all_red_indicators(state);
            } else {
                /* Blue bottom: clear_all_blue_indicators / Func_1c2cb (ASM line 1393) */
                clear_all_blue_indicators(state);
            }
            /* Both fields bottom: Func_10184 (process_billboard_illumination) +
             * Func_102bc (load_mon_billboard_palettes) — ASM lines 1266-1269 / 1394-1397.
             * C needs load_mon_billboard_picture + load_billboard_tilemap first since
             * ASM reads tile data from ROM directly but C caches from PNG. */
            load_mon_billboard_picture(state);
            load_billboard_tilemap(state);
            process_billboard_illumination(state);
            load_mon_billboard_palettes(state);
        }
    }
}

/* HandleRedCatchEmCollision (0x20000) - collision dispatch */
/* Jackpot values loaded from config/scores.json */

/*=============================================================================
 * ShowCapturedPokemonText (0x106b6) — Two-phase Pokemon name display
 * Slot 0: "YOU GOT A[N] " scrolls on then off left (no pause)
 * Slot 1: Pokemon name scrolls on, pauses centered ~100 frames, scrolls off
 * ASM adjusts stop_offset and scroll_steps per name length for centering.
 *===========================================================================*/
static void show_captured_pokemon_text(GameState *state) {
    const char *name = "POKEMON    ";
    if (state->current_catchem_mon < NUM_POKEMON)
        name = pokedex_names[state->current_catchem_mon];

    /* Compute trimmed name length (strip trailing spaces) */
    int name_len = 0;
    for (int i = 0; i < 11 && name[i] != '\0'; i++) {
        if (name[i] != ' ') name_len = i + 1;
    }
    if (name_len == 0) name_len = 1;

    /* Vowel check on first character → "YOU GOT A" vs "YOU GOT AN" */
    char first = name[0];
    int is_vowel = (first == 'A' || first == 'E' || first == 'I' ||
                    first == 'O' || first == 'U');

    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);

    /* Slot 0: prefix text — scrolls on and off with no pause */
    if (is_vowel)
        load_scrolling_text(state, 0, YOU_GOT_AN_HEADER, "YOU GOT AN ");
    else
        load_scrolling_text(state, 0, YOU_GOT_A_HEADER, "YOU GOT A ");

    /* Slot 1: Pokemon name — scrolls on, pauses centered, scrolls off.
     * Per ASM (0x106b6): stop_offset += (20 - name_len) / 2 to center,
     * scroll_steps += name_len to account for name width. */
    const uint8_t *base = is_vowel ? MON_NAME_VOWEL_HEADER
                                   : MON_NAME_CONSONANT_HEADER;
    uint8_t adjusted[6];
    memcpy(adjusted, base, 6);
    adjusted[2] += (uint8_t)((20 - name_len) / 2);  /* center name */
    adjusted[5] += (uint8_t)name_len;                /* extend scroll */

    /* Write name tile indices into bottom_message_text at offset 0x20 */
    uint8_t text_offset = adjusted[4]; /* 0x20 */
    for (int i = 0; i < name_len; i++) {
        char c = name[i];
        uint8_t tile;
        if (c >= 'A' && c <= 'Z')
            tile = (uint8_t)(c - 'A');
        else if (c == '\'') {
            tile = 0x85;
            load_special_text_char_gfx(state, 2); /* apostrophe glyph */
        } else
            tile = 0x81; /* space */
        state->bottom_message_text[text_offset + i] = tile;
    }

    /* Set up scrolling text slot 1 struct directly */
    ScrollingText *st = &state->scrolling_text[1];
    st->enabled = 1;
    st->scroll_delay_counter = adjusted[0];
    st->scroll_delay = adjusted[0];
    st->message_box_offset = adjusted[1];
    st->stop_offset = adjusted[2];
    st->stop_duration = adjusted[3];
    st->source_text_offset = adjusted[4];
    st->scroll_steps_remaining = adjusted[5];
}

/*=============================================================================
 * ShowMonEvolvedText (0x10e0a) — Display "IT EVOLVED INTO A[N] [name]"
 * or "EVOLUTION SPECIAL BONUS" if current_evolution_mon == 0xFF.
 * Returns true if normal evolution text was shown, false if special bonus.
 * Mirrors show_captured_pokemon_text but for evolution mode.
 *===========================================================================*/
static bool show_evolved_pokemon_text(GameState *state) {
    if (state->current_evolution_mon == 0xFF) {
        /* EvolutionSpecialBonus (0x10e8b): all evolutions done.
         * Award 1,000,000 points and show "EVOLUTION SPECIAL BONUS" + digits. */
        add_score_no_multiplier(state, state->config->scores.score_1000000);
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        static const uint8_t bcd_1m[4] = { 0x01, 0x00, 0x00, 0x00 };
        load_scrolling_text_with_bcd(state, 1, EVO_DIGITS_HEADER, bcd_1m);
        load_scrolling_text(state, 0, EVO_SPECIAL_BONUS_HEADER,
                            "EVOLUTION SPECIAL BONUS ");
        return false;
    }

    /* Normal evolution: show "IT EVOLVED INTO A[N] [name]" */
    const char *name = "POKEMON    ";
    if (state->current_evolution_mon < NUM_POKEMON)
        name = pokedex_names[state->current_evolution_mon];

    /* Compute trimmed name length (strip trailing spaces) */
    int name_len = 0;
    for (int i = 0; i < 11 && name[i] != '\0'; i++) {
        if (name[i] != ' ') name_len = i + 1;
    }
    if (name_len == 0) name_len = 1;

    /* Vowel check on first character */
    char first = name[0];
    int is_vowel = (first == 'A' || first == 'E' || first == 'I' ||
                    first == 'O' || first == 'U');

    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);

    /* Slot 0: prefix text — scrolls on and off with no pause */
    if (is_vowel)
        load_scrolling_text(state, 0, IT_EVOLVED_AN_HEADER, "IT EVOLVED INTO AN ");
    else
        load_scrolling_text(state, 0, IT_EVOLVED_A_HEADER, "IT EVOLVED INTO A ");

    /* Slot 1: Pokemon name — scrolls on, pauses centered, scrolls off.
     * Same centering logic as show_captured_pokemon_text. */
    const uint8_t *base = is_vowel ? EVO_NAME_VOWEL_HEADER
                                   : EVO_NAME_CONSONANT_HEADER;
    uint8_t adjusted[6];
    memcpy(adjusted, base, 6);
    adjusted[2] += (uint8_t)((20 - name_len) / 2);
    adjusted[5] += (uint8_t)name_len;

    /* Write name tile indices into bottom_message_text at offset 0x20 */
    uint8_t text_offset = adjusted[4]; /* 0x20 */
    for (int i = 0; i < name_len; i++) {
        char c = name[i];
        uint8_t tile;
        if (c >= 'A' && c <= 'Z')
            tile = (uint8_t)(c - 'A');
        else if (c == '\'') {
            tile = 0x85;
            load_special_text_char_gfx(state, 2);
        } else
            tile = 0x81; /* space */
        state->bottom_message_text[text_offset + i] = tile;
    }

    /* Set up scrolling text slot 1 struct directly */
    ScrollingText *st = &state->scrolling_text[1];
    st->enabled = 1;
    st->scroll_delay_counter = adjusted[0];
    st->scroll_delay = adjusted[0];
    st->message_box_offset = adjusted[1];
    st->stop_offset = adjusted[2];
    st->stop_duration = adjusted[3];
    st->source_text_offset = adjusted[4];
    st->scroll_steps_remaining = adjusted[5];
    return true;
}

/*=============================================================================
 * ShowJackpotText (0x10825) — Payout jackpot as two stationary text entries
 * Slot 0: "JACKPOT" label at display offset 2, 180 frames
 * Slot 1: Formatted BCD score at display offset 10, 180 frames
 *===========================================================================*/
static void show_jackpot_text(GameState *state) {
    /* RetrieveJackpot + AddBCDEToCurBufferValue: add jackpot to score */
    add_score_no_multiplier(state, state->current_jackpot);
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);

    /* Stationary slot 0: "JACKPOT" label */
    load_stationary_text(state, 0, JACKPOT_LABEL_HEADER, "JACKPOT");

    /* Stationary slot 1: formatted jackpot score digits.
     * Format 4-byte BCD into bottom_message_text at offset 0x10,
     * matching LoadScoreTextFromStack (0x3372) digit formatting.
     * BCD is little-endian: byte[3]=MSB, byte[0]=LSB.
     * ASM "set 7, e" trick: commas go to buf[pos + 0x80] (row 1),
     * NOT inline — they overlay the digit position below it. */
    {
        uint8_t *buf = state->bottom_message_text;
        int pos = 0x10; /* source_text_offset from JACKPOT_SCORE_HEADER */
        const uint8_t *bcd = state->current_jackpot;
        int b = 8;
        bool suppressing = true;

        for (int byte_idx = 3; byte_idx >= 0; byte_idx--) {
            /* High nibble */
            uint8_t nibble = (bcd[byte_idx] >> 4) & 0xF;
            if (nibble != 0 || b == 1 || !suppressing) {
                buf[pos] = (uint8_t)(0x86 + nibble);
                if (b == 6 || b == 3) buf[pos + 0x80] = 0x82; /* comma in row 1 */
                pos++;
                suppressing = false;
            }
            b--;
            /* Low nibble */
            nibble = bcd[byte_idx] & 0xF;
            if (nibble != 0 || b == 1 || !suppressing) {
                buf[pos] = (uint8_t)(0x86 + nibble);
                if (b == 6 || b == 3) buf[pos + 0x80] = 0x82; /* comma in row 1 */
                pos++;
                suppressing = false;
            }
            b--;
        }
        /* Append '0' and space tiles (matches ASM Func_3309) */
        buf[pos++] = 0x86; /* '0' */
        buf[pos++] = 0x81; /* ' ' */

        /* Set up stationary text slot 1 struct directly */
        StationaryText *stt = &state->stationary_text[1];
        stt->enabled = 1;
        stt->message_box_offset = JACKPOT_SCORE_HEADER[0];
        stt->source_text_offset = JACKPOT_SCORE_HEADER[1];
        stt->duration_low = JACKPOT_SCORE_HEADER[2];
        stt->duration_high = JACKPOT_SCORE_HEADER[3];
    }
}

static void handle_red_catchem_collision(GameState *state) {
    uint8_t id = state->special_mode_collision_id;

    if (id == 4) { /* VOLTORB */
        /* Flip up to 4 billboard tiles per voltorb hit */
        uint8_t flipped = state->number_of_catch_mode_tiles_flipped;
        if (flipped >= 24) {
            /* All tiles already flipped — add to jackpot (0x0001_0000) */
            add_bcd_to_jackpot(state, state->config->scores.jackpot_catch_voltorb);
            return;
        }
        uint8_t new_flipped = flipped + 4;
        if (new_flipped > 24) new_flipped = 24;
        for (uint8_t i = flipped; i < new_flipped; i++) {
            state->billboard_tiles_illumination_states[i * 2] = 1;
            state->billboard_tiles_illumination_states[i * 2 + 1] = 0;
        }
        state->number_of_catch_mode_tiles_flipped = new_flipped;
        if (new_flipped >= 24)
            state->indicator_states[9] = 0; /* Clear voltorb indicator */
        /* Func_10184: Process illumination changes into VRAM tile writes */
        process_billboard_illumination(state);
        add_score_no_multiplier(state, state->config->scores.score_100000);

        /* ASM: Display "FLIPPED 100,000" stationary bottom text.
         * FlippedText header: stationary_text 2, 0, 64 → {0x42, 0x00, 0x40, 0x00}
         * CatchModeTileFlippedScoreStationaryTextHeader: stationary_text 10, 1, 64 → {0x4A, 0x10, 0x40, 0x00} */
        {
            static const uint8_t FLIPPED_TEXT_HDR[4] = {0x42, 0x00, 0x40, 0x00};
            static const uint8_t FLIPPED_SCORE_HDR[4] = {0x4A, 0x10, 0x40, 0x00};
            /* BCD score for display: $0010_0000 = 100,000 */
            static const uint8_t flipped_bcd[4] = {0x00, 0x00, 0x10, 0x00};

            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);

            /* Format BCD score into bottom_message_text at offset 0x10 */
            {
                uint8_t *buf = state->bottom_message_text;
                int pos = 0x10;
                int b = 8;
                bool suppressing = true;
                for (int byte_idx = 3; byte_idx >= 0; byte_idx--) {
                    uint8_t hi = (flipped_bcd[byte_idx] >> 4) & 0xF;
                    if (hi != 0 || b == 1 || !suppressing) {
                        buf[pos] = (uint8_t)(0x86 + hi);
                        if (b == 6 || b == 3) buf[pos + 0x80] = 0x82;
                        pos++;
                        suppressing = false;
                    }
                    b--;
                    uint8_t lo = flipped_bcd[byte_idx] & 0xF;
                    if (lo != 0 || b == 1 || !suppressing) {
                        buf[pos] = (uint8_t)(0x86 + lo);
                        if (b == 6 || b == 3) buf[pos + 0x80] = 0x82;
                        pos++;
                        suppressing = false;
                    }
                    b--;
                }
                buf[pos++] = 0x86; /* trailing '0' */
                buf[pos++] = 0x81; /* space */
            }

            /* Stationary text slot 1: score digits */
            state->stationary_text[1].enabled = 1;
            state->stationary_text[1].message_box_offset = FLIPPED_SCORE_HDR[0];
            state->stationary_text[1].source_text_offset = FLIPPED_SCORE_HDR[1];
            state->stationary_text[1].duration_low = FLIPPED_SCORE_HDR[2];
            state->stationary_text[1].duration_high = FLIPPED_SCORE_HDR[3];

            /* Stationary text slot 0: "FLIPPED" */
            load_stationary_text(state, 0, FLIPPED_TEXT_HDR, "FLIPPED");
        }

        /* ASM: .AllTilesFlipped — add to jackpot */
        add_bcd_to_jackpot(state, state->config->scores.jackpot_catch_voltorb);
        return;
    }
    if (id == 12) { /* SPINNER — add to jackpot (0x0000_1000) */
        add_bcd_to_jackpot(state, state->config->scores.jackpot_catch_spinner);
        return;
    }
    if (id == 5) { /* BELLSPROUT — add to jackpot (0x0005_0000) */
        add_bcd_to_jackpot(state, state->config->scores.jackpot_catch_bellsprout);
        return;
    }

    if (id != 0) return; /* Unknown collision: ignore */

    /* COLLISION_NOTHING (0): per-frame state machine dispatch */
    /* ASM: PlayLowTimeSfx before checking timer (M1) */
    play_low_time_sfx(state);
    /* CheckIfCatchemModeTimerExpired_RedField (0x201f2) */
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->special_mode_state = 7;
        /* ASM: Mew is automatically set as caught on timeout since you
         * cannot catch Mew (needs 256 hits which overflows). */
        if (state->current_catchem_mon == (MON_MEW - 1)) {
            if (state->current_catchem_mon < NUM_POKEMON)
                state->pokedex_flags[state->current_catchem_mon] |= 0x02;
        }
        stop_timer(state);
        /* Show "POKEMON RAN AWAY" text (Func_106a6) */
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "POKEMON RAN AWAY");
    }

    switch (state->special_mode_state) {
    case 0:
        /* Func_20041: Billboard reveal — wait for all 24 tiles flipped.
         * ASM: Only advance to flicker on bottom stage AND tiles_flipped == 24.
         * On top stage, ASM stays in state 0 (no billboard to animate).
         * Once player scrolls to bottom and tiles are flipped, state advances. */
        if (state->number_of_catch_mode_tiles_flipped >= 0x18 &&
            (state->current_stage & 1)) {
            state->special_mode_state = 1;
            state->billboard_reveal_frame_counter = 0x14;
            state->billboard_reveal_flicker_count = 5;
        }
        break;
    case 1:
        /* Func_2005f -> Func_10648: Billboard flicker animation.
         * ASM calls Func_10184 every frame, toggles illumination every 20 frames.
         * ASM: byte[0] = count & 1, byte[1] = (count & 1) ^ 1 — alternates on/off. */
        process_billboard_illumination(state);
        state->billboard_reveal_frame_counter--;
        if (state->billboard_reveal_frame_counter == 0) {
            state->billboard_reveal_frame_counter = 0x14;
            for (int i = 0; i < 24; i++) {
                uint8_t on_val = state->billboard_reveal_flicker_count & 1;
                state->billboard_tiles_illumination_states[i * 2] = on_val;
                state->billboard_tiles_illumination_states[i * 2 + 1] = on_val ^ 1;
            }
            state->billboard_reveal_flicker_count--;
            if (state->billboard_reveal_flicker_count == 0)
                state->special_mode_state = 2;
        }
        break;
    case 2:
        /* Func_2006b: LoadBillboardClearedTilemap + Func_10362 + Func_10301.
         * ASM loads animated mon tiles and palettes here (not just clearing). */
        if (state->current_stage & 1) {
            clear_billboard_tilemap(state);
            load_animated_mon_tiles_and_palettes(state);
        }
        state->wd5c6 = 1;
        state->special_mode_state = 3;
        break;
    case 3:
        /* ShowAnimatedCatchemPokemon_RedField (0x200a3):
         * ShowAnimatedWildMon: set sprite type, enable hittability, play cry. */
        if (state->current_catchem_mon < NUM_POKEMON) {
            uint8_t sprite_type = get_mon_animated_sprite_type(state, state->current_catchem_mon);
            if (sprite_type == 0xFF) sprite_type = 0x06;
            state->current_animated_mon_sprite_type = sprite_type;
            state->current_animated_mon_sprite_frame = sprite_type;
        } else {
            state->current_animated_mon_sprite_type = 0x06;
            state->current_animated_mon_sprite_frame = 0x06;
        }
        state->wild_mon_is_hittable = 1;
        state->ball_hit_wild_mon = 0;
        state->num_mon_hits = 0;
        state->catch_mode_mon_update_timer = 0;
        /* PlayCatchemPokemonCry (0x10732): PlayCry with mon_id = current_catchem_mon + 1 */
        audio_play_cry(state->audio, (uint8_t)(state->current_catchem_mon + 1));
        /* LoadWildMonCollisionMask (0x10464) */
        load_wild_mon_collision_mask(state);
        state->special_mode_state = 4;
        break;
    case 4: {
        /* UpdateMonState: hit detection every 4 frames */
        state->loops_until_next_catch_sprite_anim_change--;
        if (state->loops_until_next_catch_sprite_anim_change > 0) {
            state->catch_mode_mon_update_timer++;
            if ((state->catch_mode_mon_update_timer & 3) != 0)
                break;
        }
        if (state->ball_hit_wild_mon) {
            state->ball_hit_wild_mon = 0;
            state->loops_until_next_catch_sprite_anim_change =
                state->current_catch_mon_hit_frame_duration;
            state->catch_mode_mon_update_timer = 0;
            /* Mew special: unlimited hits, count tracked separately */
            if (state->current_catchem_mon != (MON_MEW - 1)) {
                if (state->num_mon_hits >= 3) {
                    /* 3 hits: caught! */
                    state->time_ran_out = 0;
                    state->pause_timer = 1;
                    state->special_mode_state = 5;
                    state->current_animated_mon_sprite_frame =
                        state->current_animated_mon_sprite_type + 2;
                    break;
                }
                state->num_mon_hits++;
            } else {
                state->num_mew_hits++;
                if (state->num_mew_hits == 0) {
                    /* ASM: 256 Mew hits overflow wraps to 0 */
                    state->num_mon_hits = 3;
                }
            }
            add_score_no_multiplier(state, state->config->scores.score_300000);
            /* ASM: ShowHitText — display "HIT" in bottom text area */
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "HIT");
            /* Func_10611: load catch progress indicator (1/2/3 circles) */
            load_catch_progress_gfx(state, state->num_mon_hits);
            state->current_animated_mon_sprite_frame =
                state->current_animated_mon_sprite_type + 2;
        } else {
            /* Idle animation toggle */
            if (state->loops_until_next_catch_sprite_anim_change == 0) {
                uint8_t type = state->current_animated_mon_sprite_type;
                uint8_t frame = state->current_animated_mon_sprite_frame;
                uint8_t c = (frame - type >= 1) ? 0 : 1;
                state->loops_until_next_catch_sprite_anim_change =
                    (c == 0) ? state->current_catch_mon_idle_frame1_duration
                             : state->current_catch_mon_idle_frame2_duration;
                state->catch_mode_mon_update_timer = 0;
                state->current_animated_mon_sprite_frame = type + c;
            }
        }
        break;
    }
    case 5: /* CatchPokemon_RedField (0x20193): trigger capture animation */
        if (state->wd580 != 0) {
            state->wd580 = 0;
            break;
        }
        /* BallCaptureInit (0x10496): zero velocity, hide ball, init animation */
        state->wd5c6 = 0;
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->pinball_is_visible = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->capturing_mon = 1;

        /* BallCaptureInit (ASM 0x10496 lines 697-707): Load smoke & shake tile
         * graphics into VRAM immediately so capture animation sprites are valid */
        /* BallCaptureSmoke2Gfx → vTilesOB tile $7E ($87E0), $20 bytes (2 tiles) */
        {
            char path[260];
            snprintf(path, sizeof(path), "%s/gfx/stage/ball_capture_smoke_2.png",
                     state->asset_base_path);
            for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
            size_t data_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &data_size);
            if (tile_data) {
                uint16_t copy = (data_size < 0x20) ? (uint16_t)data_size : 0x20;
                vram_write(state->vram, 0, 0x87E0, tile_data, copy);
                free(tile_data);
            }
        }
        /* BallCaptureSmokeGfx → vTilesSH tile $10 ($8900), $180 bytes (24 tiles) */
        {
            char path[260];
            snprintf(path, sizeof(path), "%s/gfx/stage/ball_capture_smoke.interleave.png",
                     state->asset_base_path);
            for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
            size_t data_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &data_size);
            if (tile_data) {
                interleave_tiles(tile_data, data_size, 1);
                uint16_t copy = (data_size < 0x180) ? (uint16_t)data_size : 0x180;
                vram_write(state->vram, 0, 0x8900, tile_data, copy);
                free(tile_data);
            }
        }
        /* LoadShakeBallGfx (0x104e2): Ball-type shake gfx → vTilesOB tile $38 ($8380), $40 bytes */
        {
            const char *shake_name;
            switch (state->ball_type) {
            case 1:  shake_name = "ball_greatball_shake.w16.interleave"; break;
            case 2:  shake_name = "ball_ultraball_shake.w16.interleave"; break;
            case 3:  shake_name = "ball_masterball_shake.w16.interleave"; break;
            default: shake_name = "ball_pokeball_shake.w16.interleave"; break;
            }
            char path[260];
            snprintf(path, sizeof(path), "%s/gfx/stage/%s.png",
                     state->asset_base_path, shake_name);
            for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
            size_t data_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &data_size);
            if (tile_data) {
                interleave_tiles(tile_data, data_size, 2);
                uint16_t copy = (data_size < 0x40) ? (uint16_t)data_size : 0x40;
                vram_write(state->vram, 0, 0x8380, tile_data, copy);
                free(tile_data);
            }
        }

        /* InitAnimation: load first entry from BallCaptureAnimationData */
        state->ball_capture_anim.frame_counter = ball_capture_animation_data[0];
        state->ball_capture_anim.frame = ball_capture_animation_data[1];
        state->ball_capture_anim.index = 0;
        /* Play capture start SFX (ASM: lb de, $00, $0b) */
        PLAY_SFX(state, "cave_light", 0x00, 0x0B);
        /* ShowCapturedPokemonText (0x106b6): two-slot display
         * Slot 0: "YOU GOT A[N]" scrolls on/off, Slot 1: name pauses centered */
        show_captured_pokemon_text(state);
        /* AddCaughtPokemonToParty (0x1073d) */
        if (state->num_party_mons < 255) {
            state->party_mons[state->num_party_mons] = state->current_catchem_mon;
            state->num_party_mons++;
        }
        state->special_mode_state = 6;
        break;
    case 6: {
        /* CapturePokemonAnimation (0x1052d): 22-frame ball capture animation.
         * Runs the pokeball shake sequence. When index reaches 21, handles all
         * capture side effects and concludes catch'em mode (does not use state 7). */

        /* Play SFX when animation frame is sprite $0C (tilted ball) on first tick
         * ASM checks wBallCaptureAnimationFrame == $C, not the table index */
        if (state->ball_capture_anim.frame == 0x0C &&
            state->ball_capture_anim.frame_counter == 1) {
            PLAY_SFX(state, "catch_fail", 0x00, 0x41);
        }

        /* UpdateAnimation: decrement frame_counter, advance when 0 */
        if (state->ball_capture_anim.frame_counter == 0)
            break;
        state->ball_capture_anim.frame_counter--;
        if (state->ball_capture_anim.frame_counter != 0)
            break;

        /* Frame counter reached 0: advance to next animation entry */
        {
            uint8_t next_idx = state->ball_capture_anim.index + 1;
            state->ball_capture_anim.index = next_idx;
            uint8_t data_offset = (uint8_t)(next_idx * 2);
            uint8_t next_duration = ball_capture_animation_data[data_offset];

            if (next_duration == 0)
                break; /* Terminator: safety fallback */
            state->ball_capture_anim.frame_counter = next_duration;
            state->ball_capture_anim.frame = ball_capture_animation_data[data_offset + 1];

            /* At index 1, clear wild mon hittable (ASM line 779-786) */
            if (next_idx == 1)
                state->wild_mon_is_hittable = 0;

            /* At index 21 ($15): animation complete, transition to post-capture
             * waiting states. ASM calls MainLoopUntilTextIsClear here which blocks
             * until scrolling text finishes — we use states 8/9 to do this
             * across frames while keeping the pokeball visible. */
            if (next_idx == 21) {
                state->special_mode_state = 8;
            }
        }
        break;
    }
    case 7: /* ConcludeCatchemMode_RedField (0x201ce): timer-expired conclusion.
             * Only reached when timer runs out. Capture-success concludes in state 6. */
        if (state->bottom_text_enabled) break;
        fill_bottom_message_buffer_with_black_tile(state);
        conclude_special_mode_red_field(state);
        /* Restart field music */
        PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        break;
    case 8:
        /* Wait for "YOU GOT A[N] <name>" text to finish scrolling.
         * ASM: MainLoopUntilTextIsClear (0x3475) call #1 at line 795.
         * Ball stays invisible, pokeball capture sprite keeps rendering. */
        if (state->bottom_text_enabled) break;
        /* Stop music, play capture fanfare SFX */
        PLAY_MUSIC(state, "nothing", 0x0F, 0x00); /* MUSIC_NOTHING */
        PLAY_SFX(state, "catch_attempt", 0x23, 0x29);
        /* ShowJackpotText (0x10825): payout jackpot as stationary text */
        show_jackpot_text(state);
        /* Func_10848 (0x10848): ASM checks wNumPartyMons == 0. Since
         * AddCaughtPokemonToParty already incremented in state 5, this is
         * effectively dead code in the catch path (matches ASM behavior). */
        if (state->num_party_mons == 0) {
            static const uint8_t hundred_million[6] = {
                0x00, 0x00, 0x00, 0x00, 0x01, 0x00
            };
            uint8_t off = state->add_score_queue_offset;
            bcd6_add(&state->add_score_queue[off], hundred_million);
            off += 6;
            if (off >= 0x60) off = 0;
            state->add_score_queue_offset = off;
        }
        state->special_mode_state = 9;
        break;
    case 9:
        /* Wait for JACKPOT stationary text to expire (180 frames / ~3s).
         * ASM: MainLoopUntilTextIsClear (0x3475) call #2 at line 802.
         * Pokeball stays on screen until this completes. */
        if (state->bottom_text_enabled) break;
        /* Func_10848 text portion: ASM checks wNumPartyMons == 0.
         * Effectively dead code since party was already incremented. */
        if (state->num_party_mons == 0) {
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 1, ONE_BILLION_HEADER, "1,000,000,000 ");
            load_scrolling_text(state, 0, CAUGHT_SPECIAL_BONUS_HEADER,
                                "POKeMON CAUGHT SPECIAL BONUS ");
            state->special_mode_state = 10;
            break;
        }
        state->special_mode_state = 10;
        break;
    case 10:
        /* Wait for special bonus text (if any), then restore ball. */
        if (state->bottom_text_enabled) break;
        /* Restore ball: X=$50, Y=$40, Xvel=$0080 (ASM line 807-818) */
        state->ball_x_pos = 0x5000;
        state->ball_y_pos = 0x4000;
        state->ball_x_velocity = 0x0080;
        state->ball_y_velocity = 0;
        state->capturing_mon = 0;
        state->pinball_is_visible = 1;
        state->enable_ball_gravity_and_tilt = 1;
        /* RestoreBallSaverAfterCatchEmMode + ConcludeCatchEmMode */
        conclude_special_mode_red_field(state);
        /* Restart field music (MUSIC_RED_FIELD) */
        PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        /* Increment_Max100 (0xe4a): increment caught count, cap 100.
         * AddExtraBall every 10 catches (ASM: Modulo_C c=10). */
        if (state->num_pokemon_caught_in_ball_bonus < 100) {
            state->num_pokemon_caught_in_ball_bonus++;
            if ((state->num_pokemon_caught_in_ball_bonus % 10) == 0)
                add_extra_ball(state);
        }
        /* SetPokemonOwnedFlag (0x1077c): BIT_POKEDEX_MON_CAUGHT = bit 1 */
        if (state->current_catchem_mon < NUM_POKEMON)
            state->pokedex_flags[state->current_catchem_mon] |= 0x02;
        /* Pokeball: if prev < 3, set num = prev+1 (ASM line 831-837) */
        if (state->previous_num_pokeballs < 3)
            state->num_pokeballs = state->previous_num_pokeballs + 1;
        /* wPokeballBlinkingCounter = $80 (ASM line 836-837) */
        state->pokeball_blinking_counter = 0x80;
        break;
    }
}

/*=============================================================================
 * EVOLUTION MODE (SPECIAL_MODE_EVOLUTION = 1)
 *
 * From evolution_mode.asm + evolution_mode_red_field.asm.
 * States: 0=active, 1=complete, 2=failed.
 * 10 objects shuffled, 3 are "correct" — hit correct ones to spawn trinkets.
 * 3 trinkets collected → slot cave opens → enter slot → evolution complete.
 *===========================================================================*/

/*=============================================================================
 * MonEvolutions (0x116B3): 151 entries × 6 bytes.
 * 3 slots per species: {target_species_1based, evo_type} × 3.
 * target=0 means empty slot. Types: 1=THUNDER,2=MOON,3=FIRE,4=LEAF,5=WATER,
 * 6=LINK_CABLE,7=EXPERIENCE.
 *===========================================================================*/
/* Evolution type constants from constants.h: EVO_THUNDER_STONE(1)..EVO_EXPERIENCE(7) */
#define E7 EVO_EXPERIENCE

static const uint8_t mon_evolutions[NUM_POKEMON][6] = {
    /* 0:Bulba*/  { 2,E7,  0,E7, 0,E7}, /* 1:Ivy*/    { 3,E7,  0,E7, 0,E7},
    /* 2:Venu*/   { 0,E7,  0,E7, 0,E7}, /* 3:Char*/   { 5,E7,  0,E7, 0,E7},
    /* 4:Melon*/  { 6,E7,  0,E7, 0,E7}, /* 5:Zard*/   { 0,E7,  0,E7, 0,E7},
    /* 6:Squir*/  { 8,E7,  0,E7, 0,E7}, /* 7:Wart*/   { 9,E7,  0,E7, 0,E7},
    /* 8:Blast*/  { 0,E7,  0,E7, 0,E7}, /* 9:Cater*/  {11,E7,  0,E7, 0,E7},
    /*10:Meta*/   {12,E7,  0,E7, 0,E7}, /*11:Butt*/   { 0,E7,  0,E7, 0,E7},
    /*12:Weed*/   {14,E7,  0,E7, 0,E7}, /*13:Kaku*/   {15,E7,  0,E7, 0,E7},
    /*14:Beed*/   { 0,E7,  0,E7, 0,E7}, /*15:Pidg*/   {17,E7,  0,E7, 0,E7},
    /*16:Otto*/   {18,E7,  0,E7, 0,E7}, /*17:Geot*/   { 0,E7,  0,E7, 0,E7},
    /*18:Ratt*/   {20,E7,  0,E7, 0,E7}, /*19:Ratic*/  { 0,E7,  0,E7, 0,E7},
    /*20:Spear*/  {22,E7,  0,E7, 0,E7}, /*21:Fear*/   { 0,E7,  0,E7, 0,E7},
    /*22:Ekans*/  {24,E7,  0,E7, 0,E7}, /*23:Arbok*/  { 0,E7,  0,E7, 0,E7},
    /*24:Pika*/   {26, 1,  0,E7, 0,E7}, /*25:Raichu*/ { 0,E7,  0,E7, 0,E7},
    /*26:Sandsh*/ {28,E7,  0,E7, 0,E7}, /*27:Slash*/  { 0,E7,  0,E7, 0,E7},
    /*28:NidoF*/  {30,E7,  0,E7, 0,E7}, /*29:Rina*/   {31, 2,  0,E7, 0,E7},
    /*30:Queen*/  { 0,E7,  0,E7, 0,E7}, /*31:NidoM*/  {33,E7,  0,E7, 0,E7},
    /*32:Rino*/   {34, 2,  0,E7, 0,E7}, /*33:King*/   { 0,E7,  0,E7, 0,E7},
    /*34:Clef*/   {36, 2,  0,E7, 0,E7}, /*35:Clefa*/  { 0,E7,  0,E7, 0,E7},
    /*36:Vulp*/   {38, 3,  0,E7, 0,E7}, /*37:Nine*/   { 0,E7,  0,E7, 0,E7},
    /*38:Jigg*/   {40, 2,  0,E7, 0,E7}, /*39:Wigg*/   { 0,E7,  0,E7, 0,E7},
    /*40:Zubat*/  {42,E7,  0,E7, 0,E7}, /*41:Golb*/   { 0,E7,  0,E7, 0,E7},
    /*42:Oddish*/ {44,E7,  0,E7, 0,E7}, /*43:Gloom*/  {45, 4,  0,E7, 0,E7},
    /*44:Vile*/   { 0,E7,  0,E7, 0,E7}, /*45:Paras*/  {47,E7,  0,E7, 0,E7},
    /*46:Parsct*/ { 0,E7,  0,E7, 0,E7}, /*47:Veno*/   {49,E7,  0,E7, 0,E7},
    /*48:Moth*/   { 0,E7,  0,E7, 0,E7}, /*49:Dig*/    {51,E7,  0,E7, 0,E7},
    /*50:Dug*/    { 0,E7,  0,E7, 0,E7}, /*51:Meow*/   {53,E7,  0,E7, 0,E7},
    /*52:Pers*/   { 0,E7,  0,E7, 0,E7}, /*53:Psy*/    {55,E7,  0,E7, 0,E7},
    /*54:Gold*/   { 0,E7,  0,E7, 0,E7}, /*55:Mank*/   {57,E7,  0,E7, 0,E7},
    /*56:Prime*/  { 0,E7,  0,E7, 0,E7}, /*57:Grow*/   {59, 3,  0,E7, 0,E7},
    /*58:Arcan*/  { 0,E7,  0,E7, 0,E7}, /*59:Poli*/   {61,E7,  0,E7, 0,E7},
    /*60:Whirl*/  {62, 5,  0,E7, 0,E7}, /*61:Wrath*/  { 0,E7,  0,E7, 0,E7},
    /*62:Abra*/   {64,E7,  0,E7, 0,E7}, /*63:Kada*/   {65, 6,  0,E7, 0,E7},
    /*64:Alak*/   { 0,E7,  0,E7, 0,E7}, /*65:Mach*/   {67,E7,  0,E7, 0,E7},
    /*66:Choke*/  {68, 6,  0,E7, 0,E7}, /*67:Champ*/  { 0,E7,  0,E7, 0,E7},
    /*68:Bell*/   {70,E7,  0,E7, 0,E7}, /*69:Weep*/   {71, 4,  0,E7, 0,E7},
    /*70:Victr*/  { 0,E7,  0,E7, 0,E7}, /*71:Tent*/   {73,E7,  0,E7, 0,E7},
    /*72:Cruel*/  { 0,E7,  0,E7, 0,E7}, /*73:Geo*/    {75,E7,  0,E7, 0,E7},
    /*74:Grav*/   {76, 6,  0,E7, 0,E7}, /*75:Golem*/  { 0,E7,  0,E7, 0,E7},
    /*76:Pony*/   {78,E7,  0,E7, 0,E7}, /*77:Rapid*/  { 0,E7,  0,E7, 0,E7},
    /*78:Slow*/   {80,E7,  0,E7, 0,E7}, /*79:Bro*/    { 0,E7,  0,E7, 0,E7},
    /*80:Magnem*/ {82,E7,  0,E7, 0,E7}, /*81:Ton*/    { 0,E7,  0,E7, 0,E7},
    /*82:Farfch*/ { 0,E7,  0,E7, 0,E7}, /*83:Doduo*/  {85,E7,  0,E7, 0,E7},
    /*84:Dodrio*/ { 0,E7,  0,E7, 0,E7}, /*85:Seel*/   {87,E7,  0,E7, 0,E7},
    /*86:Dewg*/   { 0,E7,  0,E7, 0,E7}, /*87:Grim*/   {89,E7,  0,E7, 0,E7},
    /*88:Muk*/    { 0,E7,  0,E7, 0,E7}, /*89:Shell*/  {91, 5,  0,E7, 0,E7},
    /*90:Cloys*/  { 0,E7,  0,E7, 0,E7}, /*91:Gas*/    {93,E7,  0,E7, 0,E7},
    /*92:Haunt*/  {94, 6,  0,E7, 0,E7}, /*93:Geng*/   { 0,E7,  0,E7, 0,E7},
    /*94:Onix*/   { 0,E7,  0,E7, 0,E7}, /*95:Drow*/   {97,E7,  0,E7, 0,E7},
    /*96:Hypno*/  { 0,E7,  0,E7, 0,E7}, /*97:Krab*/   {99,E7,  0,E7, 0,E7},
    /*98:King*/   { 0,E7,  0,E7, 0,E7}, /*99:Volt*/  {101,E7,  0,E7, 0,E7},
    /*100:Elect*/ { 0,E7,  0,E7, 0,E7}, /*101:Exegg*/{103, 4,  0,E7, 0,E7},
    /*102:Extor*/ { 0,E7,  0,E7, 0,E7}, /*103:Cubo*/ {105,E7,  0,E7, 0,E7},
    /*104:Maro*/  { 0,E7,  0,E7, 0,E7}, /*105:HitL*/  { 0,E7,  0,E7, 0,E7},
    /*106:HitC*/  { 0,E7,  0,E7, 0,E7}, /*107:Lick*/  { 0,E7,  0,E7, 0,E7},
    /*108:Koff*/ {110,E7,  0,E7, 0,E7}, /*109:Weez*/  { 0,E7,  0,E7, 0,E7},
    /*110:Rhy*/  {112,E7,  0,E7, 0,E7}, /*111:Don*/   { 0,E7,  0,E7, 0,E7},
    /*112:Chan*/  { 0,E7,  0,E7, 0,E7}, /*113:Tang*/  { 0,E7,  0,E7, 0,E7},
    /*114:Kanga*/ { 0,E7,  0,E7, 0,E7}, /*115:Hors*/ {117,E7,  0,E7, 0,E7},
    /*116:Seadr*/ { 0,E7,  0,E7, 0,E7}, /*117:Golde*/{119,E7,  0,E7, 0,E7},
    /*118:Seak*/  { 0,E7,  0,E7, 0,E7}, /*119:Star*/ {121, 5,  0,E7, 0,E7},
    /*120:Starm*/ { 0,E7,  0,E7, 0,E7}, /*121:MrMi*/  { 0,E7,  0,E7, 0,E7},
    /*122:Scyth*/ { 0,E7,  0,E7, 0,E7}, /*123:Jynx*/  { 0,E7,  0,E7, 0,E7},
    /*124:Elcta*/ { 0,E7,  0,E7, 0,E7}, /*125:Magm*/  { 0,E7,  0,E7, 0,E7},
    /*126:Pins*/  { 0,E7,  0,E7, 0,E7}, /*127:Taur*/  { 0,E7,  0,E7, 0,E7},
    /*128:Magik*/{130,E7,  0,E7, 0,E7}, /*129:Gyar*/  { 0,E7,  0,E7, 0,E7},
    /*130:Lapr*/  { 0,E7,  0,E7, 0,E7}, /*131:Ditto*/ { 0,E7,  0,E7, 0,E7},
    /*132:Eevee*/{134, 5,135, 1,136, 3}, /*133:Vapo*/  { 0,E7,  0,E7, 0,E7},
    /*134:Jolt*/  { 0,E7,  0,E7, 0,E7}, /*135:Flar*/  { 0,E7,  0,E7, 0,E7},
    /*136:Pory*/  { 0,E7,  0,E7, 0,E7}, /*137:Oman*/ {139,E7,  0,E7, 0,E7},
    /*138:Omast*/ { 0,E7,  0,E7, 0,E7}, /*139:Kabu*/ {141,E7,  0,E7, 0,E7},
    /*140:Kbtps*/ { 0,E7,  0,E7, 0,E7}, /*141:Aero*/  { 0,E7,  0,E7, 0,E7},
    /*142:Snorl*/ { 0,E7,  0,E7, 0,E7}, /*143:Artic*/ { 0,E7,  0,E7, 0,E7},
    /*144:Zapd*/  { 0,E7,  0,E7, 0,E7}, /*145:Molt*/  { 0,E7,  0,E7, 0,E7},
    /*146:Drat*/ {148,E7,  0,E7, 0,E7}, /*147:Dnair*/{149,E7,  0,E7, 0,E7},
    /*148:Dnite*/ { 0,E7,  0,E7, 0,E7}, /*149:Mwtwo*/ { 0,E7,  0,E7, 0,E7},
    /*150:Mew*/   { 0,E7,  0,E7, 0,E7},
};

#undef E7

/*=============================================================================
 * MonEvolutionObjectCounts (0x1298B): value + 2 = wNumPossibleEvolutionObjects.
 * Controls how many of the 10 field objects participate in the shuffle.
 *===========================================================================*/
static const uint8_t mon_evolution_object_counts[NUM_POKEMON] = {
    1,2,3, 1,2,3, 1,2,3, 1,2,3, 1,2,3, 1,2,3,  /* Bulba..Pidgeot */
    1,3, 1,3, 1,3,                                /* Rattata..Arbok */
    1,3, 1,3, 1,2,4, 1,2,4,                       /* Pikachu..Nidoking */
    2,3, 2,3, 2,3, 2,3,                            /* Clefairy..Wigglytuff */
    1,2,4, 2,3, 2,3, 2,3, 2,3, 2,3, 2,3,          /* Oddish..Arcanine */
    1,2,4, 1,2,4, 1,2,4, 1,2,4,                   /* Poliwag..Victreebel */
    2,3, 1,2,4, 2,3, 2,3, 2,3, 4,                  /* Tentacool..Farfetch'd */
    2,3, 2,3, 2,3, 2,3,                            /* Doduo..Cloyster */
    1,2,4, 4, 2,3, 2,3, 2,3, 2,3,                  /* Gastly..Exeggutor */
    2,3, 4,4,4, 2,3, 2,3,                          /* Cubone..Rhydon */
    4,4,4, 4,4, 2,3, 2,3,                          /* Chansey..Starmie */
    4,4,4,4,4,4,4,                                  /* MrMime..Tauros */
    2,3, 4,4,                                       /* Magikarp..Ditto */
    2,3,3,3, 4,                                     /* Eevee..Porygon */
    2,3, 2,3, 4,4,                                  /* Omanyte..Snorlax */
    4,4,4,                                          /* Articuno..Moltres */
    1,2,4, 4,6,                                     /* Dratini..Mew */
};

/*=============================================================================
 * H4: SelectPokemonToEvolve Menu (evolution_mode.asm:205-376)
 *
 * When num_party_mons > 1, shows a scrollable menu to choose which
 * Pokemon to evolve. Runs as a per-frame state machine.
 *===========================================================================*/

/* Pokemon names from main_loop.c */
extern const char pokedex_names[151][12];

/* Simple ASCII-to-tile conversion for evolution menu text */
static uint8_t evo_char_to_tile(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A');
    if (c >= '0' && c <= '9') return (uint8_t)(0x86 + (c - '0'));
    return 0x81; /* space */
}

/* LoadMonNamesIntoEvolutionSelectionList (0x10b8e):
 * Load up to 6 mon names from party[scroll_offset..] into bottom_message_text */
static void load_evo_selection_names(GameState *state) {
    /* Fill 192 bytes (6 rows × 32 cols) with space tile ($81) */
    memset(state->bottom_message_text, 0x81, 192);

    uint8_t scroll = state->cur_selected_party_mon_scroll_offset;
    int count = state->num_party_mons - scroll;
    if (count > 6) count = 6;

    for (int i = 0; i < count; i++) {
        uint8_t mon_id = state->party_mons[scroll + i];
        int row_offset = i * 32; /* 32 bytes per row */
        /* 4 leading spaces, then name */
        const char *name = (mon_id < NUM_POKEMON) ? pokedex_names[mon_id] : "           ";
        for (int c = 0; c < 11 && name[c] != '\0'; c++) {
            state->bottom_message_text[row_offset + 4 + c] = evo_char_to_tile(name[c]);
        }
    }

    /* Cursor blink: place cursor indicator ($88) at selected position */
    uint8_t local_pos = state->cur_selected_party_mon - scroll;
    state->party_selection_cursor_counter++;
    if (!(state->party_selection_cursor_counter & 0x08)) {
        int cursor_offset = local_pos * 32 + 3;
        if (cursor_offset < 192)
            state->bottom_message_text[cursor_offset] = 0x88; /* cursor tile */
    }

    /* Up/down scroll arrows */
    if (scroll > 0) {
        state->bottom_message_text[0x11] = 0x8A; /* up arrow */
    }
    if (scroll + 6 < state->num_party_mons) {
        state->bottom_message_text[0xB1] = 0x89; /* down arrow */
    }

    /* Copy to window tilemap ($9C00) — 192 bytes */
    uint8_t *win_map = state->vram->bg_map[1]; /* $9C00 = second tilemap */
    memcpy(win_map, state->bottom_message_text, 192);
}

/* Initialize evolution selection menu (InitEvolutionSelectionMenu, 0x10b59) */
static void init_evo_selection_menu(GameState *state) {
    state->cur_selected_party_mon = 0;
    state->cur_selected_party_mon_scroll_offset = 0;
    state->party_selection_cursor_counter = 0;
    state->draw_bottom_message_box = 0;

    load_evo_selection_names(state);

    /* Set window position: WY=$60, LYC=$5F, lcdc_mask=$FD */
    state->hram.wy = 0x60;
    state->hram.lyc = 0x5F;
    state->hram.lcdc_mask = 0xFD;
}

/* Finalize evolution selection — restore window and set current_catchem_mon */
static void finalize_evo_selection(GameState *state) {
    /* Restore normal pinball window state */
    state->hram.wy = 0x86;
    state->hram.lyc = 0x83;
    state->hram.last_lyc = 0x83;
    state->hram.lcdc_mask = 0xFF;

    /* Set current_catchem_mon from selected party mon (PlaceEvolutionInParty / SelectPokemonToEvolve) */
    uint8_t sel = state->cur_selected_party_mon;
    if (sel < state->num_party_mons) {
        state->current_catchem_mon = state->party_mons[sel];
    }

    state->evolution_menu_active = 0;

    /* Now fill bottom message buffer again for the scrolling text */
    fill_bottom_message_buffer_with_black_tile(state);
}

/* Per-frame evolution menu update (SelectPokemonToEvolveMenu, 0x10bea) */
void update_evolution_menu(GameState *state) {
    if (!state->evolution_menu_active) return;

    /* Cursor movement (MoveEvolutionSelectionCursor, 0x10c0c) */
    uint8_t pressed = state->hram.pressed_buttons;
    uint8_t sel = state->cur_selected_party_mon;

    if (pressed & BTN_UP) {
        if (sel > 0) {
            sel--;
            state->cur_selected_party_mon = sel;
            PLAY_SFX(state, "cursor_move", 0x00, 0x03);
        }
    } else if (pressed & BTN_DOWN) {
        if (sel + 1 < state->num_party_mons) {
            sel++;
            state->cur_selected_party_mon = sel;
            PLAY_SFX(state, "cursor_move", 0x00, 0x03);
        }
    }

    /* Adjust scroll offset (UpdateEvolutionSelectionList, 0x10c38) */
    uint8_t scroll = state->cur_selected_party_mon_scroll_offset;
    int local = (int)sel - (int)scroll;
    if (local < 0) {
        scroll = sel;
    } else if (local >= 6) {
        scroll = sel - 5;
    }
    state->cur_selected_party_mon_scroll_offset = scroll;

    /* Render names with cursor */
    load_evo_selection_names(state);

    /* Check A button to confirm */
    if (state->hram.newly_pressed_buttons & BTN_A) {
        PLAY_SFX(state, "confirm", 0x00, 0x01);
        finalize_evo_selection(state);
        /* Now the deferred start_evolution_mode_finish will be called */
        start_evolution_mode(state);
    }
}

/* StartEvolutionMode (0x10ab3) — ASM-accurate with table lookups */
void start_evolution_mode(GameState *state) {
    /* M5: ASM returns immediately if wNumPartyMons == 0 (evolution_mode.asm:610-612) */
    if (state->num_party_mons == 0) return;

    /* H4: If num_party_mons > 1 and menu not shown yet, enter menu */
    if (state->num_party_mons > 1 && !state->evolution_menu_active
        && !state->in_special_mode) {
        state->evolution_menu_active = 1;
        state->in_special_mode = 1; /* Block re-entry */
        state->special_mode = SPECIAL_MODE_EVOLUTION;
        init_evo_selection_menu(state);
        return; /* Menu will run per-frame; start_evolution_mode called again on confirm */
    }

    if (state->in_special_mode && !state->evolution_menu_active) {
        return; /* Already fully in evolution mode */
    }

    state->in_special_mode = 1;
    state->special_mode = 1; /* SPECIAL_MODE_EVOLUTION */
    state->special_mode_state = 0;

    /* InitBallSaverForCatchEmMode: backup current saver, set infinite */
    state->ball_saver_timer_frames_backup = state->ball_saver_timer_frames;
    state->ball_saver_timer_seconds_backup = state->ball_saver_timer_seconds;
    state->num_times_ball_saved_text_will_display_backup = state->num_times_ball_saved_text_will_display;
    state->ball_saver_timer_frames = 59;
    state->ball_saver_timer_seconds = 60;
    state->num_times_ball_saved_text_will_display = 0xFF;

    /* Initialize evolution state */
    state->num_evolution_trinkets = 0;
    state->evolution_trinket_cooldown_frames = 0;
    state->evolution_objects_disabled = 0;
    memset(state->active_evolution_trinkets, 0, 19); /* ASM clears 19 bytes (bug) */

    /* Billboard illumination: all pairs set to {1, 0} */
    for (int i = 0; i < 24; i++) {
        state->billboard_tiles_illumination_states[i * 2] = 1;
        state->billboard_tiles_illumination_states[i * 2 + 1] = 0;
    }

    /* Determine num_possible_evolution_objects from table */
    uint8_t mon_id = state->current_catchem_mon; /* 0-based */
    if (mon_id >= NUM_POKEMON) mon_id = 0;
    uint8_t evo_obj_count;
    if (state->config->pokemon.pokemon_loaded && state->config->pokemon.species[mon_id].evolution_data[0])
        evo_obj_count = state->config->pokemon.species[mon_id].evolution_object_count;
    else
        evo_obj_count = mon_evolution_object_counts[mon_id];
    uint8_t num_possible = evo_obj_count + 2;
    state->num_possible_evolution_objects = num_possible;

    /* Randomly select evolution target from MonEvolutions table */
    {
        const uint8_t *evo_entry;
        uint8_t config_evo[6];
        if (state->config->pokemon.pokemon_loaded && state->config->pokemon.species[mon_id].evolution_data[0]) {
            memcpy(config_evo, state->config->pokemon.species[mon_id].evolution_data, 6);
            evo_entry = config_evo;
        } else {
            evo_entry = mon_evolutions[mon_id];
        }
        /* Count valid evolution slots */
        int valid_count = 0;
        for (int i = 0; i < 3; i++) {
            if (evo_entry[i * 2] != 0) valid_count++;
        }

        uint8_t target = 0;
        uint8_t evo_type = EVO_EXPERIENCE;
        if (valid_count > 0) {
            /* Pick random slot among valid (non-zero) ones */
            uint8_t pick = random_range(state, valid_count - 1);
            /* Map pick to nth valid (non-zero) slot */
            int found = 0;
            for (int i = 0; i < 3; i++) {
                if (evo_entry[i * 2] != 0) {
                    if (found == pick) {
                        target = evo_entry[i * 2];
                        evo_type = evo_entry[i * 2 + 1];
                        break;
                    }
                    found++;
                }
            }
        }

        if (target == 0) {
            /* No valid evolution → evolution special bonus */
            state->current_evolution_mon = 0xFF;
            state->current_evolution_type = evo_type;
        } else {
            state->current_evolution_mon = target - 1; /* 0-based */
            state->current_evolution_type = evo_type;
        }
    }

    /* M4: SetPokemonSeenFlag — mark evolution target as seen in Pokedex
     * (evolution_mode.asm:639 / catchem_mode.asm:1065) */
    {
        uint8_t mon = (state->current_evolution_mon != 0xFF)
            ? state->current_evolution_mon : state->current_catchem_mon;
        if (mon < NUM_POKEMON) state->pokedex_flags[mon] |= 0x01;
    }

    /* Initialize evolution object states: 3 "correct" + 7 "wrong" */
    for (int i = 0; i < 10; i++)
        state->evolution_object_states[i] = (i < 3) ? 1 : 0;

    /* ASM forward shuffle: only within [0..numPossible] range */
    for (int i = 0; i <= (int)num_possible; i++) {
        uint8_t j = random_range(state, num_possible);
        uint8_t tmp = state->evolution_object_states[i];
        state->evolution_object_states[i] = state->evolution_object_states[j];
        state->evolution_object_states[j] = tmp;
    }

    /* Start 2-minute timer */
    start_timer(state, 2, 0);

    /* Backup structure state, close slot cave.
     * ASM: StartEvolutionMode_RedField (0x10ebb) sets wRedStageStructureBackup = $02
     * unconditionally, not derived from stage_collision_state. */
    int is_blue = (state->current_stage >= STAGE_BLUE_FIELD_TOP);
    if (!is_blue) {
        state->red_stage_structure_backup = 0x02;
        /* L3: ASM sets wLeftAlleyCount = 0 (evolution_mode.asm:632-633) */
        state->left_alley_count = 0;
    }
    close_slot_cave(state);

    /* H3: Set indicator states from per-mon tables (evolution_mode.asm:615-631).
     * Index = (num_possible_evolution_objects - 2), selects 19-byte table.
     * Red and blue fields have different tables. */
    {
        static const uint8_t initial_indicator_states_red[8][19] = {
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x80,0x80,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x80,0x80,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x80,0x80,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
        };
        static const uint8_t initial_indicator_states_blue[8][19] = {
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x80,0x80,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x00,0x00,0x80,0x80,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x80,0x00,0x80,0x80,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
            {0x80,0x00,0x80,0x80,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00},
        };
        int table_idx = (int)num_possible - 2;
        if (table_idx < 0) table_idx = 0;
        if (table_idx > 7) table_idx = 7;
        const uint8_t *src = is_blue
            ? initial_indicator_states_blue[table_idx]
            : initial_indicator_states_red[table_idx];
        memcpy(state->indicator_states, src, 19);
    }

    /* ShowStartEvolutionModeText (0x10b3f): display mode-specific text.
     * ASM chooses text based on evolution type:
     *   EVO_EXPERIENCE → "START TRAINING"
     *   Other types    → "FIND ITEMS" */
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    if (state->current_evolution_type == EVO_EXPERIENCE) {
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "START TRAINING");
    } else {
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "FIND ITEMS");
    }

    /* Play evolution mode music */
    PLAY_MUSIC(state, "hurry_up_red", 0x0F, 0x03);

    /* ASM: Load catch bar tiles on bottom stage (evolution_mode.asm lines 501-513).
     * Same as catch'em mode: 2 tiles from bottom field base GBC gfx → $8AE0,
     * CatchBarTiles → BG Map at coord (6, 8). */
    if (state->current_stage & 1) {
        const char *sub = (state->current_stage >= STAGE_BLUE_FIELD_TOP)
            ? "blue_bottom/blue_bottom_base_gameboycolor"
            : "red_bottom/red_bottom_base_gameboycolor";
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/%s.png",
                 state->asset_base_path, sub);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data && data_size >= 0x320) {
            vram_write(state->vram, 0, 0x8AE0, tile_data + 0x300, 0x20);
        }
        free(tile_data);

        static const uint8_t catch_bar_tiles[8] = {
            0x80, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0x80
        };
        uint8_t *bg = state->vram->bg_map[0];
        for (int i = 0; i < 8; i++) {
            bg[0x106 + i] = catch_bar_tiles[i];
        }

        if (state->current_stage >= STAGE_BLUE_FIELD_TOP)
            clear_all_blue_indicators(state);
        else
            clear_all_red_indicators(state);
    }
}

/* Evolution object index to indicator_states mapping */
static const uint8_t evo_obj_indicator_idx[10] = {
    9,   /* 0: Voltorb */
    13,  /* 1: Left Diglett */
    14,  /* 2: Right Diglett */
    11,  /* 3: Left Bonus Mult */
    12,  /* 4: Right Bonus Mult */
    8,   /* 5: Staryu */
    3,   /* 6: Bellsprout (also 10) */
    2,   /* 7: Staryu Alley Trigger */
    7,   /* 8: Spinner */
    6,   /* 9: Ball Upgrade */
};

/* Try to collect a trinket from hitting the given object index */
static void try_evo_object_hit(GameState *state, uint8_t obj_idx) {
    if (state->evolution_objects_disabled) return;
    if (obj_idx >= 10) return;

    uint8_t ind = evo_obj_indicator_idx[obj_idx];
    if (!state->indicator_states[ind]) return;

    /* Turn off indicator */
    state->indicator_states[ind] = 0;
    if (obj_idx == 6) /* Bellsprout: also clear attachment */
        state->indicator_states[10] = 0;

    /* Check if this is a "correct" object */
    uint8_t was_correct = state->evolution_object_states[obj_idx];
    state->evolution_object_states[obj_idx] = 0;

    if (was_correct) {
        /* CreateEvolutionTrinket (0x20977): spawn trinket at random location.
         * ASM ChooseNextEvolutionTrinketLocation picks 0-16 directly into
         * wActiveEvolutionTrinkets — NO stage offset adjustment. */
        PLAY_SFX(state, "catch_ball_hit", 0x07, 0x46);
        uint8_t pos = random_range(state, 17);
        state->active_evolution_trinkets[pos] = state->current_evolution_type;
        state->evolution_objects_disabled = state->current_evolution_type;

        /* Backup indicator states */
        state->wd558 = state->indicator_states[2];
        state->wd559 = state->indicator_states[3];
        state->indicator_states[2] = 0;
        state->indicator_states[3] = 0;
        state->indicator_states[10] = 0;

        if (state->current_stage & 1) {
            if (state->current_stage >= STAGE_BLUE_FIELD_TOP)
                clear_all_blue_indicators(state);
            else
                clear_all_red_indicators(state);
        }

        /* ASM: Load evolution trinket palettes into OBJ palettes 6 & 7 */
        {
            static const uint16_t trinket_pal1[4] = {0x7FFF, 0x03BF, 0x00DD, 0x0842};
            static const uint16_t trinket_pal2[4] = {0x7FFF, 0x1AC9, 0x7C64, 0x0842};
            for (int c = 0; c < 4; c++) {
                state->obj_palettes[6].colors[c] = trinket_pal1[c];
                state->obj_palettes[7].colors[c] = trinket_pal2[c];
            }
        }

        add_score_no_multiplier(state, state->config->scores.score_300000);

        /* ASM: Show "GET A [TYPE]" scrolling text */
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        {
            static const char *evo_get_texts[7] = {
                "GET A THUNDER STONE", "GET A MOON STONE", "GET A FIRE STONE",
                "GET A LEAF STONE", "GET A WATER STONE", "GET A LINK CABLE",
                "GET EXPERIENCE"
            };
            uint8_t type_idx = state->current_evolution_type;
            if (type_idx >= 1 && type_idx <= 7)
                load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER,
                                    evo_get_texts[type_idx - 1]);
        }
    } else {
        /* EvolutionTrinketNotFound (0x209eb): wrong object, cooldown penalty */
        PLAY_SFX(state, "catch_ball_escape", 0x07, 0x47);
        state->evolution_objects_disabled = 1;
        state->indicator_states[0] = 0x80; /* Flash left */
        state->indicator_states[1] = 0x80; /* Flash right */

        state->wd558 = state->indicator_states[2];
        state->wd559 = state->indicator_states[3];
        state->indicator_states[2] = 0;
        state->indicator_states[3] = 0;
        state->indicator_states[10] = 0;

        if (state->current_stage & 1) {
            if (state->current_stage >= STAGE_BLUE_FIELD_TOP)
                clear_all_blue_indicators(state);
            else
                clear_all_red_indicators(state);
        }

        /* 600-frame cooldown (~10 seconds) */
        state->evolution_trinket_cooldown_frames = 0x0258;

        add_score_no_multiplier(state, state->config->scores.score_300000);

        /* ASM: Show "POKEMON IS TIRED" or "ITEM NOT FOUND" text */
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        if (state->current_evolution_type == EVO_EXPERIENCE)
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "POKeMON IS TIRED");
        else
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "ITEM NOT FOUND");
    }
}

/* Forward declaration — defined in Blue Field Evolution section below */
static const uint8_t blue_evo_obj_indicator_idx[10];

/* Trinket collection: called per-frame from state 0 when ball collides with
 * trinket position. ASM: HandleEvolutionMode_RedField (0x205e0) /
 * HandleEvolutionMode_BlueField (0x20c08) */
static void collect_evolution_trinket(GameState *state) {
    if (!state->collided_point_index) return;

    uint8_t pos = state->collided_point_index - 1;
    if (state->current_stage & 1)
        pos += 12;
    if (pos >= 18) return;

    if (!state->active_evolution_trinkets[pos]) return;

    bool is_blue = (state->current_stage >= STAGE_BLUE_FIELD_TOP);

    /* Clear trinket from position */
    state->active_evolution_trinkets[pos] = 0;
    state->evolution_objects_disabled = 0;

    /* Increment trinket count (Func_20651 / ProgressEvolution) */
    state->num_evolution_trinkets++;

    /* SFX per trinket number */
    if (state->num_evolution_trinkets == 1)
        PLAY_SFX(state, "evo_trinket", 0x07, 0x28);
    else if (state->num_evolution_trinkets == 2)
        PLAY_SFX(state, "evo_start_check", 0x07, 0x44);
    else if (state->num_evolution_trinkets >= 3) {
        PLAY_SFX(state, "high_scores_enter", 0x07, 0x45);
        /* 3 trinkets: open slot cave! */
        state->slot_is_open = 1;
        state->indicator_states[4] = 0x80;
        /* Turn off all object indicators */
        const uint8_t *ind_idx = is_blue ? blue_evo_obj_indicator_idx
                                         : evo_obj_indicator_idx;
        for (int i = 0; i < 10; i++)
            state->indicator_states[ind_idx[i]] = 0;
        state->indicator_states[10] = 0;
        state->wd558 = 0;
        state->wd559 = 0;
        if (is_blue)
            state->indicator_state_2_backup = 0;
        if (state->current_stage & 1) {
            if (is_blue) {
                clear_all_blue_indicators(state);
                load_slot_cave_cover_graphics_blue(state);
            } else {
                clear_all_red_indicators(state);
                load_slot_cave_cover_graphics(state);
            }
        }
    }

    /* Restore backed up indicator states — differs by field.
     * Red: wd558→[2], wd559→[3],[10]. Blue: wd558→[0], wd559→[3], backup→[2]. */
    if (is_blue) {
        state->indicator_states[0] = state->wd558;
        state->indicator_states[3] = state->wd559;
        state->indicator_states[2] = state->indicator_state_2_backup;
        if (state->current_stage & 1)
            clear_all_blue_indicators(state);
    } else {
        state->indicator_states[2] = state->wd558;
        state->indicator_states[3] = state->wd559;
        state->indicator_states[10] = state->wd559;
        if (state->current_stage & 1)
            clear_all_red_indicators(state);
    }

    add_score_no_multiplier(state, state->config->scores.score_1000000);

    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "YEAH! YOU GOT IT!");

    /* ASM restores stage palette to OBJ palette 6 after trinket is collected
     * (trinket gfx no longer needed). Red: StageRedFieldBottomOBJPalette6,
     * Blue: StageBlueFieldBottomOBJPalette6. */
    if (is_blue) {
        static const uint16_t blue_stage_pal6[4] = {0x56B5, 0x63E0, 0x3A40, 0x0000};
        for (int c = 0; c < 4; c++)
            state->obj_palettes[6].colors[c] = blue_stage_pal6[c];
    } else {
        static const uint16_t red_stage_pal6[4] = {0x5294, 0x63E0, 0x3A40, 0x0000};
        for (int c = 0; c < 4; c++)
            state->obj_palettes[6].colors[c] = red_stage_pal6[c];
    }
}

/* RecoverPokemon_RedField (0x20a9c): early recovery via trigger collision.
 * Clears indicators, restores backups, restores stage palette, shows text. */
static void recover_pokemon_red_field(GameState *state) {
    state->indicator_states[0] = 0;
    state->indicator_states[1] = 0;
    state->evolution_objects_disabled = 0;
    state->indicator_states[2] = state->wd558;
    state->indicator_states[3] = state->wd559;
    state->indicator_states[10] = state->wd559;
    if (state->current_stage & 1)
        clear_all_red_indicators(state);
    /* Restore StageRedFieldBottomOBJPalette6 to OBJ palette 6 */
    static const uint16_t red_stage_pal6[4] = {0x5294, 0x63E0, 0x3A40, 0x0000};
    for (int c = 0; c < 4; c++)
        state->obj_palettes[6].colors[c] = red_stage_pal6[c];
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    if (state->current_evolution_type == EVO_EXPERIENCE)
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "POKeMON RECOVERED");
    else
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "TRY NEXT PLACE");
}

/* HandleRedEvoModeCollision (0x20581) */
static void handle_red_evo_mode_collision(GameState *state) {
    uint8_t id = state->special_mode_collision_id;

    /* Route collision to per-object handler.
     * ASM: Voltorb/Bellsprout/Spinner add to jackpot BEFORE evo object logic. */
    switch (id) {
    case 4:  add_bcd_to_jackpot(state, state->config->scores.jackpot_evo_voltorb);
             try_evo_object_hit(state, 0); return; /* Voltorb */
    case 3:  try_evo_object_hit(state, 7); return; /* Staryu alley (no jackpot) */
    case 5:  add_bcd_to_jackpot(state, state->config->scores.jackpot_evo_bellsprout);
             try_evo_object_hit(state, 6); return; /* Bellsprout */
    case 6:  try_evo_object_hit(state, 5); return; /* Staryu */
    case 7:  try_evo_object_hit(state, 1); return; /* Left Diglett */
    case 8:  try_evo_object_hit(state, 2); return; /* Right Diglett */
    case 9:  try_evo_object_hit(state, 3); return; /* Left Bonus Mult */
    case 10: try_evo_object_hit(state, 4); return; /* Right Bonus Mult */
    case 11: try_evo_object_hit(state, 9); return; /* Ball Upgrade */
    case 12: add_bcd_to_jackpot(state, state->config->scores.jackpot_evo_spinner);
             try_evo_object_hit(state, 8); return; /* Spinner */
    case 13: /* Slot hole: evolution complete! */
        if (state->num_evolution_trinkets >= 3) {
            /* ASM: HandleSlotCaveCollision_EvolutionMode_RedField (0x20b02)
             * loads the EVOLVED mon's billboard picture + palettes before
             * showing the "IT EVOLVED INTO" text. */
            {
                uint8_t evolved = state->current_evolution_mon;
                if (evolved == 0xFF)
                    evolved = state->current_catchem_mon;
                state->current_catchem_mon = evolved;
                load_mon_billboard_picture(state);
                load_billboard_tilemap(state);
                refresh_billboard_illumination(state);
            }
            state->special_mode_state = 3; /* COMPLETE → show evolved text */
            add_score_no_multiplier(state, state->config->scores.score_10000000);
            PLAY_SFX(state, "catch_success", 0x25, 0x25);
        }
        return;
    case 1: /* Left trigger: recovery if objects disabled + indicator lit */
        if (!state->evolution_objects_disabled) return;
        if (!state->indicator_states[0]) return;
        add_score_no_multiplier(state, state->config->scores.score_10000);
        recover_pokemon_red_field(state);
        return;
    case 2: /* Right trigger: recovery if objects disabled + indicator lit */
        if (!state->evolution_objects_disabled) return;
        if (!state->indicator_states[1]) return;
        add_score_no_multiplier(state, state->config->scores.score_10000);
        recover_pokemon_red_field(state);
        return;
    case 0: break; /* NOTHING: fall through to state machine */
    default: return;
    }

    /* Per-frame state machine (id == 0) */
    /* ASM: PlayLowTimeSfx before checking timer (M1) */
    play_low_time_sfx(state);

    /* Decrement trinket cooldown timer */
    if (state->evolution_trinket_cooldown_frames > 0) {
        state->evolution_trinket_cooldown_frames--;
        if (state->evolution_trinket_cooldown_frames == 0) {
            /* EndEvolutionTrinketCooldown (0x20a55): re-enable objects */
            if (state->evolution_objects_disabled &&
                state->indicator_states[1]) {
                state->indicator_states[0] = 0;
                state->indicator_states[1] = 0;
                state->evolution_objects_disabled = 0;
                state->indicator_states[2] = state->wd558;
                state->indicator_states[3] = state->wd559;
                state->indicator_states[10] = state->wd559;
                if (state->current_stage & 1) {
                    if (state->current_stage >= STAGE_BLUE_FIELD_TOP)
                        clear_all_blue_indicators(state);
                    else
                        clear_all_red_indicators(state);
                }
                /* ASM: Show "POKEMON RECOVERED" or "TRY NEXT PLACE" text */
                fill_bottom_message_buffer_with_black_tile(state);
                enable_bottom_text(state);
                if (state->current_evolution_type == EVO_EXPERIENCE)
                    load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "POKeMON RECOVERED");
                else
                    load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "TRY NEXT PLACE");
            }
        }
    }

    /* Check timer expiry */
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->special_mode_state = 2; /* FAIL */
        state->slot_is_open = 0;
        /* Clear all indicators */
        for (int i = 0; i < 10; i++)
            state->indicator_states[evo_obj_indicator_idx[i]] = 0;
        state->indicator_states[10] = 0;
        state->indicator_states[4] = 0;
        state->wd558 = 0;
        state->wd559 = 0;
        state->evolution_objects_disabled = 0;
        if (state->current_stage & 1) {
            if (state->current_stage >= STAGE_BLUE_FIELD_TOP) {
                clear_all_blue_indicators(state);
                load_slot_cave_cover_graphics_blue(state);
            } else {
                clear_all_red_indicators(state);
                load_slot_cave_cover_graphics(state);
            }
        }
        stop_timer(state);
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
    }

    /* State dispatch */
    switch (state->special_mode_state) {
    case 0: /* Active: trinket collection via point-based collision */
        collect_evolution_trinket(state);
        break;
    case 1: /* Legacy complete entry — redirect to text sequence */
        state->special_mode_state = 3;
        break;
    case 2: /* Failed: show "EVOLUTION FAILED" text, then conclude */
        load_scrolling_text(state, 0, EVOLUTION_FAILED_HEADER, "EVOLUTION FAILED");
        state->special_mode_state = 6;
        break;
    case 3: /* Complete: ShowMonEvolvedText (ASM 0x10e0a) */
        show_evolved_pokemon_text(state);
        state->special_mode_state = 4;
        break;
    case 4: /* Wait for evolved text, then show jackpot */
        if (state->bottom_text_enabled) break;
        PLAY_MUSIC(state, "nothing", 0x0F, 0x00); /* MUSIC_NOTHING */
        PLAY_SFX(state, "pokemon_evolve", 0x2D, 0x26);
        show_jackpot_text(state);
        state->special_mode_state = 5;
        break;
    case 5: /* Wait for jackpot text, then conclude */
        if (state->bottom_text_enabled) break;
        /* PlaceEvolutionInParty (0x10ca5): replace party mon with evolved form */
        if (state->current_evolution_mon != 0xFF &&
            state->cur_selected_party_mon < state->num_party_mons) {
            state->party_mons[state->cur_selected_party_mon] =
                state->current_evolution_mon;
        }
        conclude_special_mode_red_field(state);
        /* Resume field music (red or blue) */
        if (state->current_stage >= STAGE_BLUE_FIELD_TOP)
            PLAY_MUSIC(state, "blue_field", 0x0F, 0x02);
        else
            PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        state->num_pokemon_evolved_in_ball_bonus++;
        /* SetPokemonOwnedFlag (0x1077c): mark evolved mon as caught */
        {
            uint8_t mon = (state->current_evolution_mon != 0xFF)
                ? state->current_evolution_mon : state->current_catchem_mon;
            if (mon < NUM_POKEMON)
                state->pokedex_flags[mon] |= 0x02;
        }
        if (state->num_pokeballs < 3)
            state->num_pokeballs += 2;
        if (state->num_pokeballs > 3)
            state->num_pokeballs = 3;
        /* Release ball from slot.
         * ASM: after blocking text/jackpot, the slot exit counter naturally
         * continues and ejects the ball. In C we halted the counter, so
         * release the ball directly: restore visibility, gravity, eject
         * velocity, and reload the slot cave cover graphics. */
        state->pinball_is_visible = 1;
        state->enable_ball_gravity_and_tilt = 1;
        state->ball_y_velocity = MAKE_FIXED(2, 0);
        state->ball_x_velocity = 0x0080;
        load_ball_gfx(state);
        load_slot_cave_cover_graphics(state);
        state->slot_enter_or_exit_counter = 0;
        break;
    case 6: /* Wait for "EVOLUTION FAILED" text, then conclude */
        if (state->bottom_text_enabled) break;
        conclude_special_mode_red_field(state);
        PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        break;
    }
}

/*=============================================================================
 * MAP MOVE MODE (SPECIAL_MODE_MAP_MOVE = 2)
 *
 * From map_move.asm.
 * States: 0=active (waiting for trigger), 1=slot opened,
 *         2=success (map changed), 3=failed (timer expired).
 *===========================================================================*/

/* Red field map sets for ChooseNextMap */
static const uint8_t first_map_set_red[7] = {
    0,  /* PALLET_TOWN */
    2,  /* VIRIDIAN_FOREST */
    3,  /* PEWTER_CITY */
    5,  /* CERULEAN_CITY */
    6,  /* VERMILION_SEASIDE */
    8,  /* ROCK_MOUNTAIN */
    9,  /* LAVENDER_TOWN */
};
static const uint8_t second_map_set_red[4] = {
    11, /* CYCLING_ROAD */
    13, /* SAFARI_ZONE */
    15, /* SEAFOAM_ISLANDS */
    16, /* CINNABAR_ISLAND */
};

/* Blue field map sets for ChooseNextMap */
static const uint8_t first_map_set_blue[7] = {
    1,  /* VIRIDIAN_CITY */
    2,  /* VIRIDIAN_FOREST */
    4,  /* MT_MOON */
    5,  /* CERULEAN_CITY */
    7,  /* VERMILION_STREETS */
    8,  /* ROCK_MOUNTAIN */
    10, /* CELADON_CITY */
};
static const uint8_t second_map_set_blue[4] = {
    12, /* FUCHSIA_CITY */
    13, /* SAFARI_ZONE */
    14, /* SAFFRON_CITY */
    16, /* CINNABAR_ISLAND */
};

/* ChooseNextMap_RedField (0x3024b) — ASM-accurate with anti-repeat */
static void choose_next_map_red(GameState *state) {
    uint8_t count = state->num_map_moves + 1;
    if (count >= 6) {
        memset(state->visited_maps, 0xFF, 6);
        count = 0;
    }
    state->num_map_moves = count;

    uint8_t new_map;
    if (count < 3) {
        /* Pick from first set (7 maps) with anti-repeat check */
        int attempts = 0;
        for (;;) {
            uint8_t r = gen_random(state) & 7;
            if (r >= 7) continue;
            new_map = first_map_set_red[r];
            int dup = 0;
            for (int i = 0; i < (int)count && !dup; i++)
                if (state->visited_maps[i] == new_map) dup = 1;
            if (!dup) break;
            if (++attempts > 100) {
                printf("[MAP] WARNING: choose_next_map_red anti-repeat loop stuck! count=%d visited=[%d,%d,%d]\n",
                    count, state->visited_maps[0], state->visited_maps[1], state->visited_maps[2]);
                fflush(stdout);
                break;
            }
        }
    } else if (count < 5) {
        /* Pick from second set (4 maps) with anti-repeat */
        int attempts = 0;
        for (;;) {
            new_map = second_map_set_red[gen_random(state) & 3];
            int dup = 0;
            int offset = count - 3;
            for (int i = 0; i < offset && !dup; i++)
                if (state->visited_maps[3 + i] == new_map) dup = 1;
            if (!dup) break;
            if (++attempts > 100) {
                printf("[MAP] WARNING: choose_next_map_red set2 anti-repeat loop stuck! count=%d\n", count);
                fflush(stdout);
                break;
            }
        }
    } else {
        new_map = MAP_INDIGO_PLATEAU;
    }

    state->visited_maps[count] = new_map;
    state->current_map = new_map;
}

/* ChooseNextMap_BlueField (0x30419) — ASM-accurate with anti-repeat */
static void choose_next_map_blue(GameState *state) {
    uint8_t count = state->num_map_moves + 1;
    if (count >= 6) {
        memset(state->visited_maps, 0xFF, 6);
        count = 0;
    }
    state->num_map_moves = count;

    uint8_t new_map;
    if (count < 3) {
        int attempts = 0;
        for (;;) {
            uint8_t r = gen_random(state) & 7;
            if (r >= 7) continue;
            new_map = first_map_set_blue[r];
            int dup = 0;
            for (int i = 0; i < (int)count && !dup; i++)
                if (state->visited_maps[i] == new_map) dup = 1;
            if (!dup) break;
            if (++attempts > 100) {
                printf("[MAP] WARNING: choose_next_map_blue anti-repeat loop stuck! count=%d visited=[%d,%d,%d]\n",
                    count, state->visited_maps[0], state->visited_maps[1], state->visited_maps[2]);
                fflush(stdout);
                break;
            }
        }
    } else if (count < 5) {
        int attempts = 0;
        for (;;) {
            new_map = second_map_set_blue[gen_random(state) & 3];
            int dup = 0;
            int offset = count - 3;
            for (int i = 0; i < offset && !dup; i++)
                if (state->visited_maps[3 + i] == new_map) dup = 1;
            if (!dup) break;
            if (++attempts > 100) {
                printf("[MAP] WARNING: choose_next_map_blue set2 anti-repeat loop stuck! count=%d\n", count);
                fflush(stdout);
                break;
            }
        }
    } else {
        new_map = MAP_INDIGO_PLATEAU;
    }

    state->visited_maps[count] = new_map;
    state->current_map = new_map;
}

/* StartMapMoveMode (0x30207 red, 0x31326 blue) */
void start_map_move_mode(GameState *state) {
    if (state->in_special_mode) return;

    state->in_special_mode = 1;
    state->special_mode = 2; /* SPECIAL_MODE_MAP_MOVE */
    state->special_mode_state = 0;

    /* Start 30-second timer */
    start_timer(state, 0, 30);

    /* Load billboard based on direction */
    if (state->current_stage & 1)
        load_billboard_tile_data(state,
            (uint8_t)(state->map_move_direction + 0x12));

    /* Set direction-based indicators */
    if (state->map_move_direction == 0) {
        state->indicator_states[0] = 0x80;
        state->indicator_states[2] = 0x80;
        state->indicator_states[1] = 0;
        state->indicator_states[3] = 0;
    } else {
        state->indicator_states[1] = 0x80;
        state->indicator_states[3] = 0x80;
        state->indicator_states[0] = 0;
        state->indicator_states[2] = 0;
    }
    state->indicator_states[4] = 0;

    int is_blue = (state->current_stage >= STAGE_BLUE_FIELD_TOP);

    if (!is_blue) {
        /* Red field: ASM Func_311b4 loads diglett graphics and writes collision map.
         * _LoadDiglettGraphics(2) = left diglett hidden, _LoadDiglettGraphics(5) = right diglett hidden */
        load_diglett_graphics(state, 2);
        load_diglett_graphics(state, 5);
        /* Collision map writes: block the diglett tunnels */
        state->stage_collision_map[0xF0] = 0x6A;
        state->stage_collision_map[0x110] = 0x6B;
        state->stage_collision_map[0xE3] = 0x66;
        state->stage_collision_map[0x103] = 0x67;
        /* ASM: wRedStageStructureBackup = $04 unconditionally for map move */
        state->red_stage_structure_backup = 0x04;
    } else {
        state->wd644 = 1;
    }
    close_slot_cave(state);

    /* Play hurry-up music */
    PLAY_MUSIC(state, "gastly_graveyard", 0x0F, 0x05);

    /* Stage-specific indicator reload + graphics (ASM: ClearAllRedIndicators
     * for red, Func_1c2cb + Psyduck/Poliwag gfx for blue) */
    if (is_blue) {
        start_map_move_blue_init(state);
    } else if (state->current_stage & 1) {
        clear_all_red_indicators(state);
    }
}

/* HandleMapModeCollision (0x30427 red / 0x31389 blue) — unified handler */
static void handle_map_mode_collision(GameState *state) {
    uint8_t id = state->special_mode_collision_id;
    int is_blue = (state->current_stage >= STAGE_BLUE_FIELD_TOP);

    /* Route by collision ID — LEFT slot openers:
     * Red: LEFT_TRIGGER (1), STARYU_ALLEY (3)
     * Blue: LEFT_TRIGGER (1), SLOWPOKE (15) */
    if (id == 1 || id == 3 || id == 15) {
        if (state->map_move_direction != 0) return;
        if (!state->indicator_states[0]) return;
        state->indicator_states[0] = 0;
        state->indicator_states[2] = 0;
        state->slot_is_open = 1;
        state->indicator_states[4] = 0x80;
        state->special_mode_state = 1;
        if (state->current_stage & 1) {
            if (is_blue) load_slot_cave_cover_graphics_blue(state);
            else load_slot_cave_cover_graphics(state);
        }
        return;
    }
    /* RIGHT slot openers:
     * Red: RIGHT_TRIGGER (2), BELLSPROUT (5)
     * Blue: RIGHT_TRIGGER (2), CLOYSTER (14) */
    if (id == 2 || id == 5 || id == 14) {
        if (state->map_move_direction != 1) return;
        if (!state->indicator_states[1]) return;
        state->indicator_states[1] = 0;
        state->indicator_states[3] = 0;
        state->slot_is_open = 1;
        state->indicator_states[4] = 0x80;
        state->special_mode_state = 1;
        if (state->current_stage & 1) {
            if (is_blue) load_slot_cave_cover_graphics_blue(state);
            else load_slot_cave_cover_graphics(state);
        }
        return;
    }
    if (id == 13) {
        /* SLOT_HOLE: successful map move! */
        if (state->special_mode_state < 1) return;
        audio_stop_all(state->audio);
        if (is_blue)
            choose_next_map_blue(state);
        else
            choose_next_map_red(state);
        if (state->current_stage & 1)
            load_map_billboard_tile_data(state);
        PLAY_SFX(state, "catch_success", 0x25, 0x25);

        /* "ARRIVED AT [map name]" scrolling text */
        load_scrolling_map_name_text(state, 1);

        state->special_mode_state = 2;
        return;
    }

    if (id != 0) return; /* Unknown: ignore */

    /* COLLISION_NOTHING (0): per-frame update */
    /* ASM: animation suppression + PlayLowTimeSfx before timer check (M1, M3) */
    {
        bool is_blue_map = (state->current_stage >= STAGE_BLUE_FIELD_TOP);
        if (is_blue_map) {
            /* Blue: freeze Poliwag/Psyduck animation (map_move.asm:635-641) */
            state->left_map_move_poliwag_anim_counter = 0x50;
            state->right_map_move_psyduck_frame = 0x50;
            state->psyduck_state = 3;
            state->poliwag_state = 1;
        } else {
            /* Red: freeze Diglett animation controllers (map_move.asm:485-487) */
            state->left_diglett_anim_controller = 0x50;
            state->right_diglett_anim_controller = 0x50;
        }
        play_low_time_sfx(state);
    }

    /* Check timer expiry */
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->special_mode_state = 3; /* FAIL */
        state->slot_is_open = 0;
        state->indicator_states[0] = 0;
        state->indicator_states[1] = 0;
        state->indicator_states[2] = 0;
        state->indicator_states[3] = 0;
        state->indicator_states[4] = 0;
        /* L4: Bottom-stage graphics reload on map move fail (map_move.asm:503-509) */
        if (state->current_stage & 1) {
            bool is_blue_mm = (state->current_stage >= STAGE_BLUE_FIELD_TOP);
            if (is_blue_mm) {
                /* Blue: Func_1c2cb + slot cave + billboard (map_move.asm:660) */
                clear_all_blue_indicators(state);
                load_slot_cave_cover_graphics_blue(state);
                load_map_billboard_tile_data(state);
            } else {
                clear_all_red_indicators(state);
                load_slot_cave_cover_graphics(state);
                load_map_billboard_tile_data(state);
            }
        }
        stop_timer(state);

        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        /* M2: Load "MAP MOVE FAILED" scrolling text (map_move.asm:511-516) */
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "MAP MOVE FAILED");
    }

    /* State dispatch */
    switch (state->special_mode_state) {
    case 0: case 1:
        break;
    case 2: /* Success: conclude + restart field music */
        if (is_blue) {
            state->wd644 = 0;
            conclude_special_mode_blue_field(state);
            PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
        } else {
            conclude_special_mode_red_field(state);
            PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        }
        break;
    case 3: /* Failed: conclude + restart field music */
        if (is_blue) {
            state->wd644 = 0;
            conclude_special_mode_blue_field(state);
            PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
        } else {
            conclude_special_mode_red_field(state);
            PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        }
        break;
    }
}

/*=============================================================================
 * PlayLowTimeSfx (0xacc6) — plays warning SFX at 32, 16, and 5 seconds
 *===========================================================================*/
static void play_low_time_sfx(GameState *state) {
    if (state->timer_frames != 0) return;
    if (state->timer_minutes != 0) return;
    if (state->timer_seconds == 32) PLAY_SFX(state, "countdown_32sec", 0x07, 0x49);
    else if (state->timer_seconds == 16) PLAY_SFX(state, "countdown_16sec", 0x0A, 0x4A);
    else if (state->timer_seconds == 5) PLAY_SFX(state, "countdown_5sec", 0x0D, 0x4B);
}

/*=============================================================================
 * Blue Field Catch'em Handler (HandleBlueCatchEmCollision 0x21000)
 *
 * Blue field uses SHELLDER (4) for tile flipping (same ID as VOLTORB),
 * SLOWPOKE (15) and CLOYSTER (14) for jackpot (equivalent of BELLSPROUT).
 * Per-frame state machine is identical to red.
 *===========================================================================*/
static void handle_blue_catchem_collision(GameState *state) {
    uint8_t id = state->special_mode_collision_id;

    if (id == 4) { /* SHELLDER (same ID as VOLTORB): flip billboard tiles */
        uint8_t flipped = state->number_of_catch_mode_tiles_flipped;
        if (flipped >= 24) {
            /* All tiles flipped — add to jackpot */
            add_bcd_to_jackpot(state, state->config->scores.jackpot_catch_voltorb);
            return;
        }
        uint8_t new_flipped = flipped + 4;
        if (new_flipped > 24) new_flipped = 24;
        for (uint8_t i = flipped; i < new_flipped; i++) {
            state->billboard_tiles_illumination_states[i * 2] = 1;
            state->billboard_tiles_illumination_states[i * 2 + 1] = 0;
        }
        state->number_of_catch_mode_tiles_flipped = new_flipped;
        if (new_flipped >= 24)
            state->indicator_states[9] = 0;
        /* Func_10184: Process illumination changes into VRAM tile writes */
        process_billboard_illumination(state);
        add_score_no_multiplier(state, state->config->scores.score_100000);

        /* ASM: Display "FLIPPED 100,000" stationary bottom text.
         * Same headers as red field (shared text data). */
        {
            static const uint8_t FLIPPED_TEXT_HDR[4] = {0x42, 0x00, 0x40, 0x00};
            static const uint8_t FLIPPED_SCORE_HDR[4] = {0x4A, 0x10, 0x40, 0x00};
            static const uint8_t flipped_bcd[4] = {0x00, 0x00, 0x10, 0x00};

            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);

            {
                uint8_t *buf = state->bottom_message_text;
                int pos = 0x10;
                int b = 8;
                bool suppressing = true;
                for (int byte_idx = 3; byte_idx >= 0; byte_idx--) {
                    uint8_t hi = (flipped_bcd[byte_idx] >> 4) & 0xF;
                    if (hi != 0 || b == 1 || !suppressing) {
                        buf[pos] = (uint8_t)(0x86 + hi);
                        if (b == 6 || b == 3) buf[pos + 0x80] = 0x82;
                        pos++;
                        suppressing = false;
                    }
                    b--;
                    uint8_t lo = flipped_bcd[byte_idx] & 0xF;
                    if (lo != 0 || b == 1 || !suppressing) {
                        buf[pos] = (uint8_t)(0x86 + lo);
                        if (b == 6 || b == 3) buf[pos + 0x80] = 0x82;
                        pos++;
                        suppressing = false;
                    }
                    b--;
                }
                buf[pos++] = 0x86;
                buf[pos++] = 0x81;
            }

            state->stationary_text[1].enabled = 1;
            state->stationary_text[1].message_box_offset = FLIPPED_SCORE_HDR[0];
            state->stationary_text[1].source_text_offset = FLIPPED_SCORE_HDR[1];
            state->stationary_text[1].duration_low = FLIPPED_SCORE_HDR[2];
            state->stationary_text[1].duration_high = FLIPPED_SCORE_HDR[3];

            load_stationary_text(state, 0, FLIPPED_TEXT_HDR, "FLIPPED");
        }

        /* ASM: .AllTilesFlipped — add to jackpot */
        add_bcd_to_jackpot(state, state->config->scores.jackpot_catch_voltorb);
        return;
    }
    if (id == 12) { /* SPINNER — add to jackpot */
        add_bcd_to_jackpot(state, state->config->scores.jackpot_catch_spinner);
        return;
    }
    if (id == 15 || id == 14) {
        /* SLOWPOKE, CLOYSTER: ASM loads BCD but doesn't call AddBCDEToJackpot (original bug) */
        return;
    }

    if (id != 0) return;

    /* Per-frame state machine — identical to red */
    play_low_time_sfx(state);

    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->special_mode_state = 7;
    }

    switch (state->special_mode_state) {
    case 0:
        /* Func_20302: Wait for all 24 tiles flipped — same logic as red field.
         * ASM: Only advance on bottom stage AND tiles_flipped == 24.
         * On top stage, stays in state 0 until scrolled to bottom. */
        if (state->number_of_catch_mode_tiles_flipped >= 0x18 &&
            (state->current_stage & 1)) {
            state->special_mode_state = 1;
            state->billboard_reveal_frame_counter = 0x14;
            state->billboard_reveal_flicker_count = 5;
        }
        break;
    case 1:
        /* Func_20320: Billboard flicker animation (Func_10648).
         * ASM: byte[0] = count & 1, byte[1] = (count & 1) ^ 1 — alternates on/off. */
        process_billboard_illumination(state);
        state->billboard_reveal_frame_counter--;
        if (state->billboard_reveal_frame_counter == 0) {
            state->billboard_reveal_frame_counter = 0x14;
            for (int i = 0; i < 24; i++) {
                uint8_t on_val = state->billboard_reveal_flicker_count & 1;
                state->billboard_tiles_illumination_states[i * 2] = on_val;
                state->billboard_tiles_illumination_states[i * 2 + 1] = on_val ^ 1;
            }
            state->billboard_reveal_flicker_count--;
            if (state->billboard_reveal_flicker_count == 0)
                state->special_mode_state = 2;
        }
        break;
    case 2:
        /* Func_2032c: LoadBillboardClearedTilemap + Func_10362 + Func_10301.
         * ASM loads animated mon tiles and palettes here (not just clearing). */
        if (state->current_stage & 1) {
            clear_billboard_tilemap(state);
            load_animated_mon_tiles_and_palettes(state);
        }
        state->wd5c6 = 1;
        state->special_mode_state = 3;
        break;
    case 3:
        /* ShowAnimatedCatchemPokemon_BlueField: show mon, enable hittability. */
        if (state->current_catchem_mon < NUM_POKEMON) {
            uint8_t sprite_type = get_mon_animated_sprite_type(state, state->current_catchem_mon);
            if (sprite_type == 0xFF) sprite_type = 0x06;
            state->current_animated_mon_sprite_type = sprite_type;
            state->current_animated_mon_sprite_frame = sprite_type;
        } else {
            state->current_animated_mon_sprite_type = 0x06;
            state->current_animated_mon_sprite_frame = 0x06;
        }
        state->wild_mon_is_hittable = 1;
        state->ball_hit_wild_mon = 0;
        state->num_mon_hits = 0;
        state->catch_mode_mon_update_timer = 0;
        audio_play_cry(state->audio, (uint8_t)(state->current_catchem_mon + 1));
        load_wild_mon_collision_mask(state);
        state->special_mode_state = 4;
        break;
    case 4: {
        state->loops_until_next_catch_sprite_anim_change--;
        if (state->loops_until_next_catch_sprite_anim_change > 0) {
            state->catch_mode_mon_update_timer++;
            if ((state->catch_mode_mon_update_timer & 3) != 0)
                break;
        }
        if (state->ball_hit_wild_mon) {
            state->ball_hit_wild_mon = 0;
            state->loops_until_next_catch_sprite_anim_change =
                state->current_catch_mon_hit_frame_duration;
            state->catch_mode_mon_update_timer = 0;
            if (state->current_catchem_mon != 150) {
                if (state->num_mon_hits >= 3) {
                    state->time_ran_out = 0;
                    state->pause_timer = 1;
                    state->special_mode_state = 5;
                    break;
                }
                state->num_mon_hits++;
            } else {
                state->num_mew_hits++;
            }
            add_score_no_multiplier(state, state->config->scores.score_300000);
            /* ASM: ShowHitText — display "HIT" in bottom text area */
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "HIT");
            /* Func_10611: load catch progress indicator (1/2/3 circles) */
            load_catch_progress_gfx(state, state->num_mon_hits);
            state->current_animated_mon_sprite_frame =
                state->current_animated_mon_sprite_type + 2;
        } else {
            if (state->loops_until_next_catch_sprite_anim_change == 0) {
                uint8_t type = state->current_animated_mon_sprite_type;
                uint8_t frame = state->current_animated_mon_sprite_frame;
                uint8_t c = (frame - type >= 1) ? 0 : 1;
                state->loops_until_next_catch_sprite_anim_change =
                    (c == 0) ? state->current_catch_mon_idle_frame1_duration
                             : state->current_catch_mon_idle_frame2_duration;
                state->catch_mode_mon_update_timer = 0;
                state->current_animated_mon_sprite_frame = type + c;
            }
        }
        break;
    }
    case 5:
        /* CatchPokemon_BlueField (0x20454): wait one frame for wd580, then init capture anim */
        if (state->wd580 != 0) {
            state->wd580 = 0;
            break;
        }
        /* BallCaptureInit (0x10496): zero velocity, hide ball, init animation */
        state->wd5c6 = 0;
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->pinball_is_visible = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->capturing_mon = 1;

        /* BallCaptureInit (ASM 0x10496 lines 697-707): Load smoke & shake tile
         * graphics into VRAM immediately so capture animation sprites are valid */
        /* BallCaptureSmoke2Gfx → vTilesOB tile $7E ($87E0), $20 bytes (2 tiles) */
        {
            char path[260];
            snprintf(path, sizeof(path), "%s/gfx/stage/ball_capture_smoke_2.png",
                     state->asset_base_path);
            for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
            size_t data_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &data_size);
            if (tile_data) {
                uint16_t copy = (data_size < 0x20) ? (uint16_t)data_size : 0x20;
                vram_write(state->vram, 0, 0x87E0, tile_data, copy);
                free(tile_data);
            }
        }
        /* BallCaptureSmokeGfx → vTilesSH tile $10 ($8900), $180 bytes (24 tiles) */
        {
            char path[260];
            snprintf(path, sizeof(path), "%s/gfx/stage/ball_capture_smoke.interleave.png",
                     state->asset_base_path);
            for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
            size_t data_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &data_size);
            if (tile_data) {
                interleave_tiles(tile_data, data_size, 1);
                uint16_t copy = (data_size < 0x180) ? (uint16_t)data_size : 0x180;
                vram_write(state->vram, 0, 0x8900, tile_data, copy);
                free(tile_data);
            }
        }
        /* LoadShakeBallGfx (0x104e2): Ball-type shake gfx → vTilesOB tile $38 ($8380), $40 bytes */
        {
            const char *shake_name;
            switch (state->ball_type) {
            case 1:  shake_name = "ball_greatball_shake.w16.interleave"; break;
            case 2:  shake_name = "ball_ultraball_shake.w16.interleave"; break;
            case 3:  shake_name = "ball_masterball_shake.w16.interleave"; break;
            default: shake_name = "ball_pokeball_shake.w16.interleave"; break;
            }
            char path[260];
            snprintf(path, sizeof(path), "%s/gfx/stage/%s.png",
                     state->asset_base_path, shake_name);
            for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
            size_t data_size = 0;
            uint8_t *tile_data = tiles_from_png(path, &data_size);
            if (tile_data) {
                interleave_tiles(tile_data, data_size, 2);
                uint16_t copy = (data_size < 0x40) ? (uint16_t)data_size : 0x40;
                vram_write(state->vram, 0, 0x8380, tile_data, copy);
                free(tile_data);
            }
        }

        /* InitAnimation: load first entry from BallCaptureAnimationData */
        state->ball_capture_anim.frame_counter = ball_capture_animation_data[0];
        state->ball_capture_anim.frame = ball_capture_animation_data[1];
        state->ball_capture_anim.index = 0;
        /* Play capture start SFX (ASM: lb de, $00, $0b) */
        PLAY_SFX(state, "cave_light", 0x00, 0x0B);
        /* ShowCapturedPokemonText (0x106b6): two-slot display
         * Slot 0: "YOU GOT A[N]" scrolls on/off, Slot 1: name pauses centered */
        show_captured_pokemon_text(state);
        /* AddCaughtPokemonToParty (ASM catchem_mode_blue_field.asm) */
        if (state->num_party_mons < 255) {
            state->party_mons[state->num_party_mons] = state->current_catchem_mon + 1;
            state->num_party_mons++;
        }
        state->special_mode_state = 6;
        break;
    case 6: {
        /* CapturePokemonAnimation_BlueField (0x20483): calls shared CapturePokemonAnimation.
         * 22-frame pokeball shake animation, identical to red field state 6. */

        /* Play SFX when animation frame is sprite $0C (tilted ball) on first tick
         * ASM checks wBallCaptureAnimationFrame == $C, not the table index */
        if (state->ball_capture_anim.frame == 0x0C &&
            state->ball_capture_anim.frame_counter == 1) {
            PLAY_SFX(state, "catch_fail", 0x00, 0x41);
        }

        /* UpdateAnimation: decrement frame_counter, advance when 0 */
        if (state->ball_capture_anim.frame_counter == 0)
            break;
        state->ball_capture_anim.frame_counter--;
        if (state->ball_capture_anim.frame_counter != 0)
            break;

        /* Frame counter reached 0: advance to next animation entry */
        {
            uint8_t next_idx = state->ball_capture_anim.index + 1;
            state->ball_capture_anim.index = next_idx;
            uint8_t data_offset = (uint8_t)(next_idx * 2);
            uint8_t next_duration = ball_capture_animation_data[data_offset];

            if (next_duration == 0)
                break; /* Terminator: safety fallback */
            state->ball_capture_anim.frame_counter = next_duration;
            state->ball_capture_anim.frame = ball_capture_animation_data[data_offset + 1];

            /* At index 1, clear wild mon hittable (ASM line 779-786) */
            if (next_idx == 1)
                state->wild_mon_is_hittable = 0;

            /* At index 21 ($15): animation complete, transition to post-capture
             * waiting states (same pattern as red field states 8/9). */
            if (next_idx == 21) {
                state->special_mode_state = 8;
            }
        }
        break;
    }
    case 7: /* ConcludeCatchemMode_BlueField (0x2048f): timer-expired conclusion.
             * Only reached when timer runs out. Capture-success concludes in state 6. */
        if (state->bottom_text_enabled) break;
        fill_bottom_message_buffer_with_black_tile(state);
        conclude_special_mode_blue_field(state);
        PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
        break;
    case 8:
        /* Wait for "YOU GOT A[N] <name>" text to finish scrolling.
         * ASM: MainLoopUntilTextIsClear call #1. Pokeball stays on screen. */
        if (state->bottom_text_enabled) break;
        /* Stop music, play capture fanfare SFX */
        PLAY_MUSIC(state, "nothing", 0x0F, 0x00); /* MUSIC_NOTHING */
        PLAY_SFX(state, "catch_attempt", 0x23, 0x29);
        /* ShowJackpotText (0x10825): payout jackpot as stationary text */
        show_jackpot_text(state);
        /* Func_10848: if first Pokemon caught, award 100M bonus */
        if (state->num_party_mons == 0) {
            static const uint8_t hundred_million[6] = {
                0x00, 0x00, 0x00, 0x00, 0x01, 0x00
            };
            uint8_t off = state->add_score_queue_offset;
            bcd6_add(&state->add_score_queue[off], hundred_million);
            off += 6;
            if (off >= 0x60) off = 0;
            state->add_score_queue_offset = off;
        }
        state->special_mode_state = 9;
        break;
    case 9:
        /* Wait for JACKPOT stationary text to expire (180 frames / ~3s).
         * ASM: MainLoopUntilTextIsClear call #2. Pokeball stays on screen. */
        if (state->bottom_text_enabled) break;
        /* Func_10848 (0x10848): if first Pokemon caught (party was empty),
         * show "POKEMON CAUGHT SPECIAL BONUS" + "1,000,000,000" text.
         * Points already added in state 8. */
        if (state->num_party_mons == 0) {
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 1, ONE_BILLION_HEADER, "1,000,000,000 ");
            load_scrolling_text(state, 0, CAUGHT_SPECIAL_BONUS_HEADER,
                                "POKeMON CAUGHT SPECIAL BONUS ");
            state->special_mode_state = 10;
            break;
        }
        state->special_mode_state = 10;
        break;
    case 10:
        /* Wait for special bonus text (if any), then restore ball. */
        if (state->bottom_text_enabled) break;
        /* Restore ball: X=$50, Y=$40, Xvel=$0080 (ASM line 807-818) */
        state->ball_x_pos = 0x5000;
        state->ball_y_pos = 0x4000;
        state->ball_x_velocity = 0x0080;
        state->ball_y_velocity = 0;
        state->capturing_mon = 0;
        state->pinball_is_visible = 1;
        state->enable_ball_gravity_and_tilt = 1;
        /* RestoreBallSaverAfterCatchEmMode + ConcludeCatchEmMode */
        conclude_special_mode_blue_field(state);
        /* Restart field music (MUSIC_BLUE_FIELD) */
        PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
        /* Increment caught count, cap 100. AddExtraBall every 10. */
        if (state->num_pokemon_caught_in_ball_bonus < 100) {
            state->num_pokemon_caught_in_ball_bonus++;
            if ((state->num_pokemon_caught_in_ball_bonus % 10) == 0)
                add_extra_ball(state);
        }
        /* SetPokemonOwnedFlag: BIT_POKEDEX_MON_CAUGHT = bit 1 */
        if (state->current_catchem_mon < NUM_POKEMON)
            state->pokedex_flags[state->current_catchem_mon] |= 0x02;
        /* Pokeball: if prev < 3, set num = prev+1 */
        if (state->previous_num_pokeballs < 3)
            state->num_pokeballs = state->previous_num_pokeballs + 1;
        /* wPokeballBlinkingCounter = $80 */
        state->pokeball_blinking_counter = 0x80;
        break;
    }
}

/*=============================================================================
 * Blue Field Evolution Handler (HandleBlueEvoModeCollision 0x20BAE)
 *
 * Blue field uses different objects than red for evolution trinket discovery:
 *   SHELLDER (4)  → obj[0], CLOYSTER (14) → obj[6],
 *   SLOWPOKE (15) → obj[5], POLIWAG (7)   → obj[1],
 *   PSYDUCK (8)   → obj[2], LEFT_TRIGGER (1) → obj[7] or recovery.
 *===========================================================================*/

/* Blue field evolution object → indicator_states mapping */
static const uint8_t blue_evo_obj_indicator_idx[10] = {
    9,   /* 0: Shellder */
    13,  /* 1: Poliwag */
    14,  /* 2: Psyduck */
    11,  /* 3: Left Bonus Mult */
    12,  /* 4: Right Bonus Mult */
    8,   /* 5: Slowpoke */
    3,   /* 6: Cloyster (also 10) */
    0,   /* 7: Left Trigger (via indicator_states[0]) */
    7,   /* 8: Spinner */
    6,   /* 9: Ball Upgrade */
};

/* CreateEvolutionTrinket_BlueField (0x20f75) / EvolutionTrinketNotFound_BlueField.
 * Blue field backup pattern: wd558←[0], wd559←[3], indicator_state_2_backup←[2].
 * ASM uses wCurrentEvolutionType directly (no +1). Position is raw 0-16. */
static void try_blue_evo_object_hit(GameState *state, uint8_t obj_idx) {
    if (state->evolution_objects_disabled) return;
    if (obj_idx >= 10) return;

    uint8_t ind = blue_evo_obj_indicator_idx[obj_idx];
    if (!state->indicator_states[ind]) return;

    state->indicator_states[ind] = 0;
    if (obj_idx == 6) /* Cloyster: also clear attachment */
        state->indicator_states[10] = 0;
    if (obj_idx == 5) /* Slowpoke: also clear indicator[2] */
        state->indicator_states[2] = 0;

    uint8_t was_correct = state->evolution_object_states[obj_idx];
    state->evolution_object_states[obj_idx] = 0;

    if (was_correct) {
        PLAY_SFX(state, "catch_ball_hit", 0x07, 0x46);
        /* ASM ChooseNextEvolutionTrinketLocation_BlueField: raw 0-16, no offset */
        uint8_t pos = random_range(state, 17);
        state->active_evolution_trinkets[pos] = state->current_evolution_type;
        state->evolution_objects_disabled = state->current_evolution_type;

        /* Blue field backup: [0]→wd558, [3]→wd559, [2]→indicator_state_2_backup */
        state->wd558 = state->indicator_states[0];
        state->wd559 = state->indicator_states[3];
        state->indicator_state_2_backup = state->indicator_states[2];
        state->indicator_states[0] = 0;
        state->indicator_states[2] = 0;
        state->indicator_states[3] = 0;

        if (state->current_stage & 1)
            clear_all_blue_indicators(state);

        /* Load trinket palettes to OBJ palette 6/7 */
        {
            static const uint16_t trinket_pal1[4] = {0x7FFF, 0x03BF, 0x00DD, 0x0842};
            static const uint16_t trinket_pal2[4] = {0x7FFF, 0x1AC9, 0x7C64, 0x0842};
            for (int c = 0; c < 4; c++) {
                state->obj_palettes[6].colors[c] = trinket_pal1[c];
                state->obj_palettes[7].colors[c] = trinket_pal2[c];
            }
        }

        add_score_no_multiplier(state, state->config->scores.score_300000);

        /* Show "GET A [TYPE]" text */
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        {
            static const char *evo_get_texts[7] = {
                "GET A THUNDER STONE", "GET A MOON STONE", "GET A FIRE STONE",
                "GET A LEAF STONE", "GET A WATER STONE", "GET A LINK CABLE",
                "GET EXPERIENCE"
            };
            uint8_t type_idx = state->current_evolution_type;
            if (type_idx >= 1 && type_idx <= 7)
                load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER,
                                    evo_get_texts[type_idx - 1]);
        }
    } else {
        /* EvolutionTrinketNotFound_BlueField (0x20fef):
         * Must backup [0] BEFORE overwriting with $80.
         * ASM: read [0]→wd558, then set [0]=$80,[1]=$80,
         *      read [3]→wd559, [2]→backup, clear [2],[3]. */
        PLAY_SFX(state, "catch_ball_escape", 0x07, 0x47);
        state->evolution_objects_disabled = 1;

        state->wd558 = state->indicator_states[0]; /* backup BEFORE $80 */
        state->indicator_states[0] = 0x80;
        state->indicator_states[1] = 0x80;
        state->wd559 = state->indicator_states[3];
        state->indicator_state_2_backup = state->indicator_states[2];
        state->indicator_states[2] = 0;
        state->indicator_states[3] = 0;

        if (state->current_stage & 1)
            clear_all_blue_indicators(state);

        state->evolution_trinket_cooldown_frames = 0x0258;
        add_score_no_multiplier(state, state->config->scores.score_300000);

        /* Show "ITEM NOT FOUND" / "POKEMON IS TIRED" text */
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
        if (state->current_evolution_type == EVO_EXPERIENCE)
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "POKeMON IS TIRED");
        else
            load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "ITEM NOT FOUND");
    }
}

/* RecoverPokemon_BlueField (0x21097): early recovery via trigger collision.
 * Blue field restore: wd558→[0], wd559→[3], indicator_state_2_backup→[2]. */
static void recover_pokemon_blue_field(GameState *state) {
    state->indicator_states[1] = 0;
    state->evolution_objects_disabled = 0;
    state->indicator_states[0] = state->wd558;
    state->indicator_states[3] = state->wd559;
    state->indicator_states[2] = state->indicator_state_2_backup;
    if (state->current_stage & 1)
        clear_all_blue_indicators(state);
    /* Restore StageBlueFieldBottomOBJPalette6 to OBJ palette 6 */
    static const uint16_t blue_stage_pal6[4] = {0x56B5, 0x63E0, 0x3A40, 0x0000};
    for (int c = 0; c < 4; c++)
        state->obj_palettes[6].colors[c] = blue_stage_pal6[c];
    fill_bottom_message_buffer_with_black_tile(state);
    enable_bottom_text(state);
    if (state->current_evolution_type == EVO_EXPERIENCE)
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "POKeMON RECOVERED");
    else
        load_scrolling_text(state, 0, FIELD_MULT_SPECIAL_HEADER, "TRY NEXT PLACE");
}

static void handle_blue_evo_mode_collision(GameState *state) {
    uint8_t id = state->special_mode_collision_id;

    /* ASM: Shellder and Spinner add to jackpot BEFORE evo object logic.
     * Cloyster/Slowpoke/Poliwag/Psyduck/others do NOT add to jackpot. */
    switch (id) {
    case 4:  add_bcd_to_jackpot(state, state->config->scores.jackpot_evo_voltorb);
             try_blue_evo_object_hit(state, 0); return; /* Shellder */
    case 14: try_blue_evo_object_hit(state, 6); return; /* Cloyster (no jackpot) */
    case 15: try_blue_evo_object_hit(state, 5); return; /* Slowpoke (no jackpot) */
    case 7:  try_blue_evo_object_hit(state, 1); return; /* Poliwag */
    case 8:  try_blue_evo_object_hit(state, 2); return; /* Psyduck */
    case 9:  try_blue_evo_object_hit(state, 3); return; /* Left Bonus Mult */
    case 10: try_blue_evo_object_hit(state, 4); return; /* Right Bonus Mult */
    case 11: try_blue_evo_object_hit(state, 9); return; /* Ball Upgrade */
    case 12: add_bcd_to_jackpot(state, state->config->scores.jackpot_evo_spinner);
             try_blue_evo_object_hit(state, 8); return; /* Spinner */
    case 13: /* Slot hole: evolution complete! */
        if (state->num_evolution_trinkets >= 3) {
            /* Load evolved mon's billboard picture (same as red field) */
            {
                uint8_t evolved = state->current_evolution_mon;
                if (evolved == 0xFF)
                    evolved = state->current_catchem_mon;
                state->current_catchem_mon = evolved;
                load_mon_billboard_picture(state);
                load_billboard_tilemap(state);
                refresh_billboard_illumination(state);
            }
            state->special_mode_state = 3; /* COMPLETE → show evolved text */
            add_score_no_multiplier(state, state->config->scores.score_10000000);
            PLAY_SFX(state, "catch_success", 0x25, 0x25);
        }
        return;
    case 1: /* Left trigger: dual behavior (HandleLeftTriggerCollision 0x21089)
             * Not disabled: evo object hit for obj 7
             * Disabled: recovery if indicator[0] lit */
        if (!state->evolution_objects_disabled) {
            try_blue_evo_object_hit(state, 7);
        } else {
            if (!state->indicator_states[0]) return;
            add_score_no_multiplier(state, state->config->scores.score_10000);
            recover_pokemon_blue_field(state);
        }
        return;
    case 2: /* Right trigger: recovery only (HandleRightTriggerCollision 0x2105c) */
        if (!state->evolution_objects_disabled) return;
        if (!state->indicator_states[1]) return;
        add_score_no_multiplier(state, state->config->scores.score_10000);
        recover_pokemon_blue_field(state);
        return;
    case 0: break;
    default: return;
    }

    /* Per-frame state machine */
    play_low_time_sfx(state);

    /* Decrement trinket cooldown timer.
     * EndEvolutionTrinketCooldown_BlueField (0x21079): calls RecoverPokemon. */
    if (state->evolution_trinket_cooldown_frames > 0) {
        state->evolution_trinket_cooldown_frames--;
        if (state->evolution_trinket_cooldown_frames == 0) {
            if (state->evolution_objects_disabled &&
                state->indicator_states[1]) {
                recover_pokemon_blue_field(state);
            }
        }
    }

    /* Check timer expiry */
    if (state->time_ran_out) {
        state->time_ran_out = 0;
        state->special_mode_state = 2;
        state->slot_is_open = 0;
        for (int i = 0; i < 10; i++)
            state->indicator_states[blue_evo_obj_indicator_idx[i]] = 0;
        state->indicator_states[10] = 0;
        state->indicator_states[4] = 0;
        state->wd558 = 0;
        state->wd559 = 0;
        state->evolution_objects_disabled = 0;
        if (state->current_stage & 1) {
            clear_all_blue_indicators(state);
            load_slot_cave_cover_graphics_blue(state);
        }
        stop_timer(state);
        fill_bottom_message_buffer_with_black_tile(state);
        enable_bottom_text(state);
    }

    switch (state->special_mode_state) {
    case 0: /* Active: trinket collection via point-based collision */
        collect_evolution_trinket(state);
        break;
    case 1: /* Legacy complete entry — redirect to text sequence */
        state->special_mode_state = 3;
        break;
    case 2: /* Failed: show "EVOLUTION FAILED" text, then conclude */
        load_scrolling_text(state, 0, EVOLUTION_FAILED_HEADER, "EVOLUTION FAILED");
        state->special_mode_state = 6;
        break;
    case 3: /* Complete: ShowMonEvolvedText (ASM 0x10e0a) */
        show_evolved_pokemon_text(state);
        state->special_mode_state = 4;
        break;
    case 4: /* Wait for evolved text, then show jackpot */
        if (state->bottom_text_enabled) break;
        PLAY_MUSIC(state, "nothing", 0x0F, 0x00); /* MUSIC_NOTHING */
        PLAY_SFX(state, "pokemon_evolve", 0x2D, 0x26);
        show_jackpot_text(state);
        state->special_mode_state = 5;
        break;
    case 5: /* Wait for jackpot text, then conclude */
        if (state->bottom_text_enabled) break;
        /* PlaceEvolutionInParty (0x10ca5): replace party mon with evolved form */
        if (state->current_evolution_mon != 0xFF &&
            state->cur_selected_party_mon < state->num_party_mons) {
            state->party_mons[state->cur_selected_party_mon] =
                state->current_evolution_mon;
        }
        conclude_special_mode_blue_field(state);
        PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
        state->num_pokemon_evolved_in_ball_bonus++;
        /* SetPokemonOwnedFlag (0x1077c): mark evolved mon as caught */
        {
            uint8_t mon = (state->current_evolution_mon != 0xFF)
                ? state->current_evolution_mon : state->current_catchem_mon;
            if (mon < NUM_POKEMON)
                state->pokedex_flags[mon] |= 0x02;
        }
        if (state->num_pokeballs < 3)
            state->num_pokeballs += 2;
        if (state->num_pokeballs > 3)
            state->num_pokeballs = 3;
        break;
    case 6: /* Wait for "EVOLUTION FAILED" text, then conclude */
        if (state->bottom_text_enabled) break;
        conclude_special_mode_blue_field(state);
        PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
        break;
    }
}

/*=============================================================================
 * CheckSpecialModeCollision (0x10000)
 *
 * Dispatches collision events to the active special mode (Catch'em, Evolution,
 * or Map Move). Called from each resolve handler with a specific collision ID,
 * and at the end of both stage resolve lists with SPECIAL_COLLISION_NOTHING (0).
 * Field-aware: dispatches to blue-specific handlers for blue field stages.
 *===========================================================================*/
bool check_special_mode_collision(GameState *state, uint8_t collision_id) {
    /* ASM CheckSpecialModeColision (0x10000):
     * Returns carry clear if NOT in special mode.
     * Returns carry set (true) if in special mode — signals callers
     * to skip normal collision processing (ASM: ret c / jr nc). */
    if (!state->in_special_mode)
        return false;

    state->special_mode_collision_id = collision_id;
    int is_blue = (state->current_stage >= STAGE_BLUE_FIELD_TOP);

    switch (state->special_mode) {
    case 0: /* Catch'em */
        if (is_blue)
            handle_blue_catchem_collision(state);
        else
            handle_red_catchem_collision(state);
        break;
    case 1: /* Evolution */
        if (is_blue)
            handle_blue_evo_mode_collision(state);
        else
            handle_red_evo_mode_collision(state);
        break;
    case 2: /* Map Move — unified handler */
        handle_map_mode_collision(state);
        break;
    }
    return true;
}

/*=============================================================================
 * UpdateCAVELightsBlinking_RedField (0x15270) - extracted for TOP stage use
 *
 * ASM calls this as a separate handler on the TOP stage dispatch list (position 5).
 * On TOP stage, there is no CAVE light collision detection — only the blinking
 * animation and flipper rotation logic runs.
 *
 * On BOTTOM stage, this logic is integrated into resolve_cave_lights().
 *===========================================================================*/
void update_cave_lights_blinking(GameState *state) {
    /* ASM: UpdateCAVELightsBlinking_RedField (0x15270)
     * On the TOP stage, this is a PURE STATE MACHINE — it updates the
     * cave_light_states[] array but does NOT call LoadCAVELightsGraphics.
     * The bg_map offsets used by load_cave_lights_graphics (0x121, 0x123,
     * 0x130, 0x132) overlap with the voltorb roof/ditto area on the top
     * field's tilemap. Writing CAVE tiles there corrupts the top field.
     *
     * On the BOTTOM stage, resolve_cave_lights() handles both state updates
     * AND graphics loading (via its own calls to load_cave_lights_graphics). */

    /* Blinking animation */
    if (state->cave_lights_blinking) {
        state->cave_lights_blinking_frames_remaining--;
        if (state->cave_lights_blinking_frames_remaining == 0) {
            state->cave_lights_blinking = 0;
            state->opened_slot_by_cave_lights = 1;
            state->frames_until_slot_cave_opens = 3;
            memset(state->cave_light_states, 0, 4);
        }

        if ((state->cave_lights_blinking_frames_remaining & 7) == 0) {
            uint8_t val = (state->cave_lights_blinking_frames_remaining >> 3) & 1;
            state->cave_light_states[0] = val;
            state->cave_light_states[1] = val;
            state->cave_light_states[2] = val;
            state->cave_light_states[3] = val;
        }
        /* NO load_cave_lights_graphics here — top stage only updates state */
        return;
    }

    /* Flipper rotation (runs every frame when not blinking) */
    if (joypad_is_key_pressed(state, &state->key_config_left_flipper)) {
        uint8_t tmp = state->cave_light_states[0];
        state->cave_light_states[0] = state->cave_light_states[1];
        state->cave_light_states[1] = state->cave_light_states[2];
        state->cave_light_states[2] = state->cave_light_states[3];
        state->cave_light_states[3] = tmp;
        /* NO load_cave_lights_graphics here — top stage only updates state */
        return;
    }
    if (joypad_is_key_pressed(state, &state->key_config_right_flipper)) {
        uint8_t tmp = state->cave_light_states[3];
        state->cave_light_states[3] = state->cave_light_states[2];
        state->cave_light_states[2] = state->cave_light_states[1];
        state->cave_light_states[1] = state->cave_light_states[0];
        state->cave_light_states[0] = tmp;
        /* NO load_cave_lights_graphics here — top stage only updates state */
        return;
    }
}

/*=============================================================================
 * Arrow Indicator Graphics (GBC version from TileDataPointers_16bef)
 * LoadTileLists data: writes tile IDs to BG map positions.
 * 5 indicators, each with multiple states.
 *===========================================================================*/

/* Indicator 0 (left alley): 5 states, BG offsets 0x23, 0x43-0x44, 0x64-0x65, 0x85, 0xA5 */
static const uint8_t arrow_ind0_tiles[5][7] = {
    {0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64},  /* State 0: no arrows lit */
    {0x65, 0x66, 0x67, 0x61, 0x62, 0x63, 0x64},  /* State 1: 1 arrow lit */
    {0x65, 0x66, 0x67, 0x68, 0x69, 0x63, 0x64},  /* State 2: 2 arrows lit */
    {0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B},  /* State 3: 3 arrows lit */
    {0x5E, 0x5F, 0x60, 0x68, 0x69, 0x6A, 0x6B},  /* State 4: wrap-around */
};
static const uint16_t arrow_ind0_offsets[7] = {0x23, 0x43, 0x44, 0x64, 0x65, 0x85, 0xA5};

/* Indicator 1 (right alley): 5 states, BG offsets 0x30, 0x4F-0x50, 0x6E-0x6F, 0x8E, 0xAE */
static const uint8_t arrow_ind1_tiles[5][7] = {
    {0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72},
    {0x73, 0x74, 0x75, 0x6F, 0x70, 0x71, 0x72},
    {0x73, 0x74, 0x75, 0x76, 0x77, 0x71, 0x72},
    {0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79},
    {0x6C, 0x6D, 0x6E, 0x76, 0x77, 0x78, 0x79},
};
static const uint16_t arrow_ind1_offsets[7] = {0x30, 0x4F, 0x50, 0x6E, 0x6F, 0x8E, 0xAE};

/* Indicator 2: 2 states, BG offsets 0x06, 0x26-0x27 */
static const uint8_t arrow_ind2_tiles[2][3] = {
    {0x48, 0x49, 0x4A},
    {0x4B, 0x4C, 0x4D},
};
static const uint16_t arrow_ind2_offsets[3] = {0x06, 0x26, 0x27};

/* Indicator 3: 2 states, BG offsets 0x0D, 0x2C-0x2D */
static const uint8_t arrow_ind3_tiles[2][3] = {
    {0x4E, 0x4F, 0x50},
    {0x51, 0x52, 0x53},
};
static const uint16_t arrow_ind3_offsets[3] = {0x0D, 0x2C, 0x2D};

/* Indicator 4 (slot cave): 2 states, BG offsets 0x49-0x4A, 0x69-0x6A */
static const uint8_t arrow_ind4_tiles[2][4] = {
    {0x40, 0x41, 0x42, 0x43},
    {0x44, 0x45, 0x46, 0x47},
};
static const uint16_t arrow_ind4_offsets[4] = {0x49, 0x4A, 0x69, 0x6A};

/* LoadArrowIndicatorGraphics_RedField (0x169cd) */
void load_arrow_indicator_graphics(GameState *state, uint8_t indicator, uint8_t gfx_state) {
    uint8_t *bg = state->vram->bg_map[0];
    switch (indicator) {
    case 0:
        if (gfx_state > 4) gfx_state = 4;
        for (int i = 0; i < 7; i++)
            bg[arrow_ind0_offsets[i]] = arrow_ind0_tiles[gfx_state][i];
        break;
    case 1:
        if (gfx_state > 4) gfx_state = 4;
        for (int i = 0; i < 7; i++)
            bg[arrow_ind1_offsets[i]] = arrow_ind1_tiles[gfx_state][i];
        break;
    case 2:
        if (gfx_state > 1) gfx_state = 1;
        for (int i = 0; i < 3; i++)
            bg[arrow_ind2_offsets[i]] = arrow_ind2_tiles[gfx_state][i];
        break;
    case 3:
        if (gfx_state > 1) gfx_state = 1;
        for (int i = 0; i < 3; i++)
            bg[arrow_ind3_offsets[i]] = arrow_ind3_tiles[gfx_state][i];
        break;
    case 4:
        if (gfx_state > 1) gfx_state = 1;
        for (int i = 0; i < 4; i++)
            bg[arrow_ind4_offsets[i]] = arrow_ind4_tiles[gfx_state][i];
        break;
    }
}

/* UpdateArrowIndicators_RedField (0x169a6)
 * Updates every 32 frames. For each active indicator (bit 7 set),
 * alternates between counter and counter+1 graphics state based on
 * hFrameCounter bit 5. */
void update_arrow_indicators(GameState *state) {
    if ((state->hram.frame_counter & 0x1F) != 0)
        return;
    for (uint8_t c = 0; c < 5; c++) {
        uint8_t ind = state->indicator_states[c];
        if (!(ind & 0x80))
            continue;
        uint8_t counter = ind & 0x7F;
        if (state->hram.frame_counter & 0x20)
            counter++;
        load_arrow_indicator_graphics(state, c, counter);
    }
}

/*=============================================================================
 * LoadDiglettGraphics (0x140f9) + other indicator init for bottom stage.
 * Called from _LoadStageDataRedFieldBottom during stage load/transitions.
 * Restores all dynamic BG tilemap graphics that resolve handlers update.
 *===========================================================================*/
void load_red_field_bottom_graphics(GameState *state) {
    /* LoadDiglettGraphics (0x140f9): restore Diglett tiles and collision attrs.
     * ASM: when controller == 0, a = [controller] = 0 (idle frame),
     * NOT the current animation frame. Only controller != 0 uses hit frame 2. */
    uint8_t left_gfx = 0; /* idle frame (ASM: a from ld a,[controller] = 0) */
    if (state->left_diglett_anim_controller > 0) {
        /* Controller active: use hit frame and set hit collision attrs */
        state->stage_collision_map[0xE3] = 0x66;
        state->stage_collision_map[0x103] = 0x67;
        left_gfx = 2; /* Hit frame */
    }
    load_diglett_graphics(state, left_gfx);
    load_diglett_number_graphics(state, state->left_map_move_counter);

    uint8_t right_gfx = 0 + 3; /* idle frame + right offset (ASM: a=0, add $3) */
    if (state->right_diglett_anim_controller > 0) {
        state->stage_collision_map[0xF0] = 0x6A;
        state->stage_collision_map[0x110] = 0x6B;
        right_gfx = 2 + 3; /* Hit frame + right offset */
    }
    load_diglett_graphics(state, right_gfx);
    load_diglett_number_graphics(state, state->right_map_move_counter + 4);

    /* Restore CAVE light tiles */
    load_cave_lights_graphics(state);

    /* Restore bumper tiles */
    load_bumper_graphics(state);

    /* Restore arrow indicator tiles */
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t ind = state->indicator_states[i];
        if (ind & 0x80) {
            load_arrow_indicator_graphics(state, i, ind & 0x7F);
        }
    }

    /* Restore pokeball indicator tiles */
    load_pokeball_indicator_graphics(state);

    /* Restore staryu BG tiles */
    load_staryu_graphics_bottom(state);
}

/*=============================================================================
 * LoadStaryuGraphics_Bottom (0x16878)
 * Writes BG tilemap tiles showing staryu on left or right side.
 * From data/queued_tiledata/red_field/staryu_bumper.asm TileDataPointers_1695a.
 *===========================================================================*/
void load_staryu_graphics_bottom(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    if (state->staryu_side & 1) {
        /* State 1: right side (GBC TileData from TileDataPointers_16980[1]) */
        bg[0x40] = 0xC0; bg[0x41] = 0xC1;
        bg[0x60] = 0xC2; bg[0x61] = 0xC3;
    } else {
        /* State 0: left side (GBC TileData from TileDataPointers_16980[0]) */
        bg[0x40] = 0xBC; bg[0x41] = 0xBD;
        bg[0x60] = 0xBE; bg[0x61] = 0xBF;
    }
}

/*=============================================================================
 * LoadPinballUpgradeTriggersGraphics_RedField (0x15023)
 * Updates BG tilemap tiles showing on/off state for 3 upgrade triggers.
 * Only active when stage_collision_state bit 0 is set.
 * From data/queued_tiledata/red_field/ball_upgrade_triggers.asm.
 *===========================================================================*/
void load_upgrade_triggers_graphics(GameState *state) {
    if (!state->vram) return;
    if (!(state->stage_collision_state & 1)) return;

    uint8_t *bg = state->vram->bg_map[0];

    /* GBC tile IDs from TileDataPointers_15543 (on) / TileDataPointers_15549 (off).
     * Trigger 3 (index 2): ON=$41 at $CC, OFF=$3B */
    bg[0xCC] = state->ball_upgrade_trigger_states[2] ? 0x41 : 0x3B;

    /* Trigger 2 (index 1): ON=$3F,$40 at $C9, OFF=$39,$3A */
    if (state->ball_upgrade_trigger_states[1]) {
        bg[0xC9] = 0x3F; bg[0xCA] = 0x40;
    } else {
        bg[0xC9] = 0x39; bg[0xCA] = 0x3A;
    }

    /* Trigger 1 (index 0): ON=$3D at $E7, OFF=$37 */
    bg[0xE7] = state->ball_upgrade_trigger_states[0] ? 0x3D : 0x37;
}

/*=============================================================================
 * LoadPokeballIndicatorGraphics (from data/queued_tiledata/red_field/pokeballs.asm)
 * Writes 6 BG tilemap tiles at vBGMap+$107 showing 0-3 collected pokeballs.
 * Tile $AE,$AF = lit pokeball pair, $B0,$B1 = empty pair.
 *===========================================================================*/
void load_pokeball_indicator_graphics(GameState *state) {
    if (!state->vram) return;
    /* During catch mode, the pokeball tilemap positions ($107-$10C) are used
     * for the CATCH! progress bar instead. Don't overwrite them. */
    if (state->in_special_mode && state->special_mode == SPECIAL_MODE_CATCHEM) return;
    uint8_t *bg = state->vram->bg_map[0];
    uint8_t count = state->num_pokeballs;
    if (count > 3) count = 3;

    /* 3 pairs of tiles at consecutive positions */
    for (uint8_t i = 0; i < 3; i++) {
        uint16_t pos = 0x107 + i * 2;
        if (i < count) {
            bg[pos]     = 0xAE; /* lit */
            bg[pos + 1] = 0xAF;
        } else {
            bg[pos]     = 0xB0; /* empty */
            bg[pos + 1] = 0xB1;
        }
    }
}

/*=============================================================================
 * Resolve Red Field Object Collisions
 *
 * ALL handlers run every frame - each checks its own flag.
 * This is the critical difference from the old switch-based approach.
 *
 * Handler order matches ASM exactly:
 *   Top:    ResolveRedFieldTopGameObjectCollisions (0x1460e)
 *   Bottom: ResolveRedFieldBottomGameObjectCollisions (0x14652)
 *===========================================================================*/
void resolve_red_field_object_collisions(GameState *state) {
    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        /* ResolveRedFieldTopGameObjectCollisions (0x1460e) — ASM order */
        resolve_voltorb(state);                     /*  1: ResolveVoltorbCollision */
        resolve_spinner(state);                     /*  2: ResolveRedStageSpinnerCollision */
        resolve_upgrade_triggers(state);            /*  3: ResolveBallUpgradeTriggersCollision */
        update_upgrade_blinking(state);             /*  3b: (integrated in ASM's resolve func) */
        update_ball_type_counter(state);            /*  4: UpdateBallTypeUpgradeCounter */
        update_cave_lights_blinking(state);         /*  5: UpdateCAVELightsBlinking */
        resolve_board_triggers(state);              /*  6: ResolveRedStageBoardTriggerCollision */
        resolve_pikachu(state);                     /*  7: ResolveRedStagePikachuCollision */
        resolve_staryu(state);                      /*  8: ResolveStaryuCollision_Top */
        resolve_bellsprout(state);                  /*  9: ResolveBellsproutCollision */
        resolve_ditto_slot(state);                  /* 10: ResolveDittoSlotCollision */
        apply_slot_force_field(state);              /* 11: ApplySlotForceField_RedFieldTop */
        open_slot_cave(state);                      /* 12: OpenSlotCave_RedField */
        update_ball_saver(state);                   /* 13: UpdateBallSaverState */
        /* D-01: ASM top stage calls UpdateBallSaverState directly, NOT the
         * UpdateBallSaver wrapper (0x146a2) that includes DrawBallSaverIcon.
         * Drawing on top stage corrupts unrelated BG tiles at offset 0x1A8. */
        if (state->current_stage == STAGE_RED_FIELD_BOTTOM)
            draw_ball_saver_icon(state);            /* 13b: DrawBallSaverIcon (bottom only) */
        update_pokeballs(state);                    /* 14: UpdateBlinkingPokeballs */
        update_map_move_counters(state);            /* 15: UpdateMapMoveCounters_RedFieldTop */
        show_extra_ball_message(state);             /* 16: ShowExtraBallMessage */
        check_special_mode_collision(state, 0);     /* 17: CheckSpecialModeColision (NOTHING) */
    } else if (state->current_stage == STAGE_RED_FIELD_BOTTOM) {
        /* ResolveRedFieldBottomGameObjectCollisions (0x14652) — ASM order */
        resolve_wild_mon(state);                    /*  1: ResolveWildMonCollision */
        resolve_bumpers(state);                     /*  2: ResolveBumpersCollision */
        resolve_diglett(state);                     /*  3: ResolveDiglettCollision */
        update_map_move_counters(state);            /*  4: UpdateMapMoveCounters_RedFieldBottom */
        resolve_spinner(state);                     /*  5: UpdateRedStageSpinner */
        update_upgrade_blinking(state);             /*  6: UpdatePinballUpgradeBlinkingAnimation */
        update_ball_type_counter(state);            /*  7: UpdateBallTypeUpgradeCounter */
        resolve_cave_lights(state);                 /*  8: ResolveCAVELightCollision */
        resolve_launch_alley(state);                /*  9: ResolveRedStagePinballLaunchCollision */
        resolve_pikachu(state);                     /* 10: ResolveRedStagePikachuCollision */
        resolve_staryu(state);                      /* 11: ResolveStaryuCollision_Bottom */
        update_arrow_indicators(state);             /* 12: UpdateArrowIndicators */
        resolve_bonus_multiplier(state);            /* 13: ResolveRedStageBonusMultiplierCollision */
        resolve_slot(state);                        /* 14: ResolveSlotCollision */
        apply_slot_force_field(state);              /* 15: ApplySlotForceField_RedFieldBottom */
        open_slot_cave(state);                      /* 16: OpenSlotCave_RedField */
        update_again_text(state);                   /* 17: UpdateAgainText */
        update_ball_saver(state);                   /* 18: UpdateBallSaver */
        draw_ball_saver_icon(state);                /* 18b: DrawBallSaverIcon */
        update_pokeballs(state);                    /* 19: UpdatePokeballs_RedField */
        show_extra_ball_message(state);             /* 20: ShowExtraBallMessage */
        check_special_mode_collision(state, 0);     /* 21: CheckSpecialModeColision (NOTHING) */
    }
}

/*=============================================================================
 * Func_14091 (0x14091): Reset voltorb/bumper gfx indices + deferred staryu.
 * Called at the start of _LoadStageDataRedFieldTop and _LoadStageDataRedFieldBottom.
 * Resets animated voltorb/bumper gfx tracking, saves ball position copies,
 * and handles deferred staryu state change (if wd503 timer expired).
 *===========================================================================*/
static void func_14091(GameState *state) {
    /* ASM: ld a, $ff / ld [wWhichAnimatedVoltorb], a / ld [wWhichBumperGfx], a */
    state->which_animated_voltorb = 0xFF;
    state->which_bumper_gfx = 0xFF;

    /* ASM: save ball position copies to wd4c5-wd4c7 (not used in C) */

    /* Deferred staryu state change: if wd503 timer is non-zero, apply it */
    if (state->staryu_timer != 0) {
        state->staryu_timer = 0;
        /* ASM: res 1, [wd502] — clear animation active bit */
        state->staryu_anim_active = 0;
        /* ASM: and $1 / or with collision state — set bit 0 from staryu_side */
        uint8_t side = state->staryu_side & 1;
        state->stage_collision_state = (state->stage_collision_state & 0xFE) | side;
        /* Play staryu SFX */
        PLAY_SFX(state, "spinner_hit", 0x00, 0x07);
        /* If on top stage: reload collision attributes + structure graphics */
        if (!(state->current_stage & 1)) {
            load_stage_collision_attributes(state);
            load_field_structure_graphics(state);
        }
    }
}

/*=============================================================================
 * DrawBallSaverIcon (0x11725)
 * Writes 4 BG tile IDs at vBGMap row 13, cols 8-11.
 * Off: $AA, $AB, $AC, $AD. On: $B4, $B5, $B6, $B7.
 *===========================================================================*/
void draw_ball_saver_icon(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    /* Row 13, col 8 = offset 13*32 + 8 = 424 = 0x1A8 */
    uint16_t offset = 0x1A8;
    if (state->ball_saver_icon_on) {
        bg[offset]     = 0xB4;
        bg[offset + 1] = 0xB5;
        bg[offset + 2] = 0xB6;
        bg[offset + 3] = 0xB7;
    } else {
        bg[offset]     = 0xAA;
        bg[offset + 1] = 0xAB;
        bg[offset + 2] = 0xAC;
        bg[offset + 3] = 0xAD;
    }
}

/*=============================================================================
 * ClearAllRedIndicators (0x14135)
 * Clears bit 7 of each indicator state (stops blinking) and reloads graphics.
 * Called by _LoadStageDataRedFieldBottom.
 *===========================================================================*/
static void clear_all_red_indicators(GameState *state) {
    if (!state->vram) return;
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t ind = state->indicator_states[i] & 0x7F;
        load_arrow_indicator_graphics(state, i, ind);
    }
}

/*=============================================================================
 * LoadBonusMultiplierRailingGraphics_RedField (0x140e2)
 * Resets railing tracking state. Tile data loaded from PNG assets at stage init.
 *===========================================================================*/
static void load_bonus_multiplier_railing_graphics(GameState *state) {
    /* ASM: ld a, $ff / ld [wd60e], a / ld [wd60f], a (DMG-only tracking, not needed) */
    /* Load initial digit tiles to BG map for tens and ones */
    load_bonus_mult_railing_gfx(state, state->bonus_multiplier_tens_digit);
    load_bonus_mult_railing_gfx(state, (uint8_t)(state->bonus_multiplier_ones_digit + 0x14));
}

/*=============================================================================
 * LoadStaryuGraphics_Top (0x16859) — GBC path
 * Writes BG tilemap tiles showing staryu animation state on top field.
 * Index = wd502 (staryu_side | staryu_anim_active<<1), values 0-3.
 * From data/queued_tiledata/red_field/staryu_bumper.asm TileDataPointers_16910.
 *===========================================================================*/
void load_staryu_graphics_top(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    uint8_t idx = (state->staryu_side & 1) | (state->staryu_anim_active ? 2 : 0);

    /* GBC TileDataPointers_16910: 4 entries (states 2 and 3 share same data) */
    switch (idx) {
    case 0: /* State 0 (TileData_16921) */
        bg[0x1C6] = 0xC3; bg[0x1C7] = 0xC4; bg[0x1C8] = 0xC5;
        bg[0x1E7] = 0xC6; bg[0x1E8] = 0xC7;
        bg[0x207] = 0xC8;
        break;
    case 1: /* State 1 (TileData_16934) */
        bg[0x1C6] = 0xCD; bg[0x1C7] = 0xCE; bg[0x1C8] = 0xC5;
        bg[0x1E7] = 0xC6; bg[0x1E8] = 0xC7;
        bg[0x207] = 0xC8;
        break;
    default: /* States 2/3 (TileData_16947) */
        bg[0x1C6] = 0xC3; bg[0x1C7] = 0xC4; bg[0x1C8] = 0xC9;
        bg[0x1E7] = 0xCA; bg[0x1E8] = 0xCB;
        bg[0x207] = 0xCC;
        break;
    }
}

/*=============================================================================
 * UpdateSpinnerChargeGraphics_RedField (0x14ece) — GBC path
 * Updates BG tilemap tiles showing spinner charge level (0-15).
 * From data/queued_tiledata/red_field/spinner.asm TileDataPointers_1509b.
 * Each charge level writes 4 tiles to bg_map offsets 0x10E-0x10F and 0x12E-0x12F.
 *===========================================================================*/
void update_spinner_charge_graphics(GameState *state) {
    if (!state->vram) return;
    uint8_t *bg = state->vram->bg_map[0];
    uint8_t charge = state->pikachu_saver_charge;
    if (charge > 15) charge = 15;

    /* Top row (bg_map offset 0x10E): charges 0-8 always $5C,$5D; 9+ vary */
    /* Bottom row (bg_map offset 0x12E): charges 0-8 vary; 9-14 always $6E,$6F */
    if (charge <= 8) {
        bg[0x10E] = 0x5C;
        bg[0x10F] = 0x5D;
        bg[0x12E] = (uint8_t)(0x5E + charge * 2);
        bg[0x12F] = (uint8_t)(0x5F + charge * 2);
    } else if (charge <= 14) {
        bg[0x10E] = (uint8_t)(0x70 + (charge - 9) * 2);
        bg[0x10F] = (uint8_t)(0x71 + (charge - 9) * 2);
        bg[0x12E] = 0x6E;
        bg[0x12F] = 0x6F;
    } else {
        /* Charge 15: both rows change */
        bg[0x10E] = 0x7C;
        bg[0x10F] = 0x7D;
        bg[0x12E] = 0x7E;
        bg[0x12F] = 0x7F;
    }
}

/*=============================================================================
 * LoadAgainTextGraphics (0x14746)
 * Loads tile data for the "AGAIN" extra ball indicator on bottom field.
 * ON = original stage tiles at VRAM tile $38/$3a (restored from stage PNG).
 * OFF = blank tiles from gfx/stage/again_off.png.
 *===========================================================================*/
void load_again_text_graphics(GameState *state) {
    if (!state->vram) return;

    /* vTilesSH tile $38 = $8800 + $380 = $8B80, tile $3a = $8BA0 */
    /* Each entry is $20 bytes = 2 tiles */
    if (state->extra_balls > 0) {
        /* ON state: reload original stage tiles from bottom field GBC PNG.
         * Source offset in PNG = tile $38 * 16 = $380.
         * VRAM $8B80 = $8800 + $380, PNG loads at $8800. */
        const char *sub = (state->current_stage >= STAGE_BLUE_FIELD_TOP)
            ? "blue_bottom/blue_bottom_base_gameboycolor"
            : "red_bottom/red_bottom_base_gameboycolor";
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/%s.png",
                 state->asset_base_path, sub);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data && data_size >= 0x3C0) {
            vram_write(state->vram, 0, 0x8B80, tile_data + 0x380, 0x20);
            vram_write(state->vram, 0, 0x8BA0, tile_data + 0x3A0, 0x20);
        }
        free(tile_data);
    } else {
        /* OFF state: load blank tiles from again_off.png */
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/again_off.png",
                 state->asset_base_path);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data && data_size >= 0x40) {
            vram_write(state->vram, 0, 0x8B80, tile_data, 0x20);
            vram_write(state->vram, 0, 0x8BA0, tile_data + 0x20, 0x20);
        }
        free(tile_data);
    }
}

/*=============================================================================
 * LoadBillboardGraphics_RedField (0x14377)
 *
 * Decides which billboard picture to show and loads its tile data.
 * Called from _LoadStageDataRedFieldBottom.
 *
 * Priority:
 *   1. 3 pokeballs collected → bonus stage billboard (index 0x15 + bonus_stage)
 *   2. 4 CAVE lights → slot billboard (index 0x1A)
 *   3. In map move mode state 3 → current map
 *   4. In map move mode, slot open → "GO TO NEXT" (index 0x14)
 *   5. In map move mode, direction set → "HURRY UP" (index 0x12+dir)
 *   6. Default → current map
 *===========================================================================*/
static void load_billboard_graphics_red_field(GameState *state) {
    /* If slot roulette is active, restore the roulette's billboard state
     * instead of loading the map/slot billboard. Stage transition would
     * otherwise overwrite palette 6 with the map's colors. */
    if (state->slot_roulette_active) {
        uint8_t pic = state->slot_roulette_billboard_picture;
        if (state->slot_roulette_state >= SLOT_ROULETTE_STOPPED_BLINK) {
            load_billboard_picture(state, pic);
            load_grey_billboard_palette(state);  /* attrs → all palette 6 */
            load_colored_billboard_palette(state, pic); /* palette 6 → colored */
        } else {
            load_billboard_off_picture(state, pic);
            load_grey_billboard_palette(state);  /* attrs + palette 6 → grey */
        }
        return;
    }

    if (!state->in_special_mode) {
        /* Not in special mode: check pokeballs / CAVE / default map */
        if (state->opened_slot_by_pokeballs) {
            load_billboard_tile_data(state,
                (uint8_t)(state->next_bonus_stage + 0x15));
        } else if (state->opened_slot_by_cave_lights) {
            load_billboard_tile_data(state, 0x1A);
        } else {
            load_map_billboard_tile_data(state);
        }
        return;
    }

    /* In special mode: only handle map move (mode 2) */
    if (state->special_mode != SPECIAL_MODE_MAP_MOVE)
        return;

    if (state->special_mode_state == 3) {
        load_map_billboard_tile_data(state);
    } else if (state->slot_is_open) {
        load_billboard_tile_data(state, 0x14); /* GO TO NEXT */
    } else {
        load_billboard_tile_data(state,
            (uint8_t)(state->map_move_direction + 0x12)); /* HURRY UP */
    }
}

/*=============================================================================
 * LoadBillboardStatusBarGraphics_RedField (0x14282)
 *
 * Loads status bar tiles below the billboard area.
 * Normal mode: pokeball indicators + caught pokeball tile data.
 * Catch'em mode: catch progress indicators (stub).
 * Evolution mode: trinket icons (stub).
 *
 * Pokeball indicators at bg_map $9907 (row 8, col 7):
 *   $AE/$AF = filled pokeball  |  $B0/$B1 = empty pokeball
 * CaughtPokeballGfx (2 tiles) at vTilesSH tile $2E ($8AE0).
 *===========================================================================*/
/*=============================================================================
 * Func_10611 (0x10611): Load catch progress indicator graphics.
 *
 * Loads 2 tiles ($20 bytes) of CatchTextGfx into VRAM for each hit:
 *   hit 1: CatchTextGfx+$00 -> $8AE0 (vTilesSH tile $2E)
 *   hit 2: CatchTextGfx+$20 -> $8B00 (vTilesSH tile $30)
 *   hit 3: CatchTextGfx+$40 -> $8B20 (vTilesSH tile $32)
 *
 * Source PNG: gfx/stage/catch.w48.png (48px wide = 6 tiles = $60 bytes)
 *===========================================================================*/
static void load_catch_progress_gfx(GameState *state, uint8_t hit_count) {
    if (!state->vram || hit_count == 0 || hit_count > 3) return;

    char path[260];
    snprintf(path, sizeof(path), "%s/gfx/stage/catch.w48.png",
             state->asset_base_path);
    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (!tile_data) return;

    /* Each hit loads $20 bytes (2 tiles) from offset (hit-1)*$20 */
    static const uint16_t vram_addrs[3] = { 0x8AE0, 0x8B00, 0x8B20 };
    uint8_t idx = hit_count - 1;
    uint32_t src_offset = (uint32_t)idx * 0x20;
    if (data_size >= src_offset + 0x20) {
        vram_write(state->vram, 0, vram_addrs[idx], tile_data + src_offset, 0x20);
    }
    free(tile_data);
}

void load_billboard_status_bar_graphics(GameState *state) {
    if (!state->vram) return;

    if (state->in_special_mode) {
        if (state->special_mode == SPECIAL_MODE_CATCHEM) {
            /* Func_142b3 / Func_1c46d: Load all catch progress indicators
             * for current hit count. Loops from num_mon_hits down to 1. */
            for (uint8_t i = state->num_mon_hits; i >= 1; i--) {
                load_catch_progress_gfx(state, i);
            }
            return;
        }
        if (state->special_mode == SPECIAL_MODE_EVOLUTION) {
            /* Func_142c3: Load evolution trinket progress icons.
             * Each trinket loads 0x20 bytes (2 tiles) from EvolutionProgressIconsGfx
             * indexed by (current_evolution_type - 1) * 0x20. */
            if (state->num_evolution_trinkets > 0 && state->current_evolution_type > 0) {
                char path[260];
                snprintf(path, sizeof(path), "%s/gfx/stage/evolution_progress_icons.png",
                         state->asset_base_path);
                for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
                size_t data_size = 0;
                uint8_t *tile_data = tiles_from_png(path, &data_size);
                if (tile_data) {
                    static const uint16_t trinket_vram_addrs[3] = { 0x8AE0, 0x8B00, 0x8B20 };
                    uint32_t src_offset = (uint32_t)(state->current_evolution_type - 1) * 0x20;
                    uint8_t count = state->num_evolution_trinkets;
                    if (count > 3) count = 3;
                    for (uint8_t i = 0; i < count; i++) {
                        if (data_size >= src_offset + 0x20) {
                            vram_write(state->vram, 0, trinket_vram_addrs[i],
                                       tile_data + src_offset, 0x20);
                        }
                    }
                    free(tile_data);
                }
            }
            return;
        }
        /* MAP_MOVE: fall through to normal mode */
    }

    /* Normal mode: pokeball indicator tile IDs at bg_map offset $107 (row 8, col 7) */
    {
        uint8_t *bg = state->vram->bg_map[0];
        uint8_t count = state->previous_num_pokeballs;
        if (count > 3) count = 3;
        /* 3 pokeball slots, each 2 tiles wide: $AE/$AF (filled) or $B0/$B1 (empty) */
        for (int i = 0; i < 3; i++) {
            uint8_t base_tile = (i < count) ? 0xAE : 0xB0;
            bg[0x107 + i * 2]     = base_tile;
            bg[0x107 + i * 2 + 1] = (uint8_t)(base_tile + 1);
        }
    }

    /* Load CaughtPokeballGfx (2 tiles, $20 bytes) to vTilesSH tile $2E ($8AE0) */
    {
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/caught_pokeball.png",
                 state->asset_base_path);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data && data_size >= 0x20) {
            vram_write(state->vram, 0, 0x8AE0, tile_data, 0x20);
        }
        free(tile_data);
    }
}

/*=============================================================================
 * LoadEvolutionTrinketGraphics_RedField (0x14234)
 *
 * Loads evolution trinket sprite tiles into VRAM when in evolution mode.
 * Top stage: tiles at vTilesSH tile $10 ($8900), $E0 bytes (14 tiles).
 * Bottom stage: tiles at vTilesOB tile $20 ($8200), $E0 bytes (14 tiles).
 * Also loads 2 OBJ palettes (palette 7) for trinket colors.
 *
 * Exits early if:
 *   - Not in special mode
 *   - Not in evolution mode (special_mode != 1)
 *   - All 3 trinkets already collected
 *===========================================================================*/
void load_evolution_trinket_graphics(GameState *state) {
    if (!state->in_special_mode)
        return;
    if (state->special_mode != 1) /* SPECIAL_MODE_EVOLUTION */
        return;
    if (state->num_evolution_trinkets >= 3)
        return;

    char path[260];
    snprintf(path, sizeof(path), "%s/gfx/stage/shared/evolution_trinkets.png",
             state->asset_base_path);
    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (!tile_data || data_size < 0xE0) {
        free(tile_data);
        return;
    }

    /* Bottom (both fields): vTilesOB tile $20 = $8200.
     * Red top: vTilesSH tile $10 = $8900.
     * Blue top: vTilesOB tile $60 = $8600. */
    uint16_t dest;
    if (state->current_stage & 1) {
        dest = 0x8200; /* Bottom stage */
    } else if (state->current_stage >= STAGE_BLUE_FIELD_TOP) {
        dest = 0x8600; /* Blue field top: vTilesOB tile $60 */
    } else {
        dest = 0x8900; /* Red field top: vTilesSH tile $10 */
    }
    vram_write(state->vram, 0, dest, tile_data, 0xE0);
    free(tile_data);

    /* EvolutionTrinketPalette1: RGB(31,31,31), (31,29,0), (29,3,2), (2,2,2)
     * EvolutionTrinketPalette2: RGB(31,31,31), (9,22,6), (4,13,31), (2,2,2)
     * ASM loads to OBJ palette offset $0070 = OBJ palette 6. $10 bytes = 2 palettes. */
    static const uint16_t trinket_pal1[4] = {
        0x7FFF, /* RGB(31,31,31) */
        0x03BF, /* RGB(31,29,0) = 31 | (29<<5) | (0<<10) */
        0x00DD, /* RGB(29,3,2) = 29 | (3<<5) | (2<<10) */
        0x0842, /* RGB(2,2,2) = 2 | (2<<5) | (2<<10) */
    };
    static const uint16_t trinket_pal2[4] = {
        0x7FFF, /* RGB(31,31,31) */
        0x1AC9, /* RGB(9,22,6) = 9 | (22<<5) | (6<<10) */
        0x7C64, /* RGB(4,13,31) = 4 | (13<<5) | (31<<10) */
        0x0842, /* RGB(2,2,2) */
    };

    /* Always load trinket palettes — stage transition palette loading
     * overwrites OBJ palette 6/7, so re-apply every time this is called.
     * ASM writes directly to CGB hardware (unaffected by DMA), but our
     * C code shares the same obj_palettes[] array as stage palettes. */
    for (int c = 0; c < 4; c++) {
        state->obj_palettes[6].colors[c] = trinket_pal1[c];
        state->obj_palettes[7].colors[c] = trinket_pal2[c];
    }
}

/*=============================================================================
 * load_animated_mon_tiles_and_palettes — Func_10362 + Func_10301
 *
 * Loads animated catch'em mon sprite tiles into VRAM bank 1 and palette data
 * into OBJ palettes 3 and 5. Called from func_1414b_special_mode_gfx.
 *===========================================================================*/
static void load_animated_mon_tiles_and_palettes(GameState *state) {
    uint8_t species = state->current_catchem_mon;
    if (species >= NUM_POKEMON) return;

    /* Data_103c6 (0x103c6): 13 VRAM copy entries for animated mon tiles.
     * Each entry: { size, vram_dest, src_offset } — all to VRAM bank 1. */
    static const struct { uint16_t size; uint16_t vram_dest; uint16_t src_offset; }
    tile_copy_table[13] = {
        { 0x40, 0x8900, 0x000 },
        { 0x40, 0x8940, 0x040 },
        { 0x40, 0x8980, 0x080 },
        { 0x40, 0x89C0, 0x0C0 },
        { 0x40, 0x8A00, 0x100 },
        { 0x40, 0x8A40, 0x140 },
        { 0x20, 0x8A80, 0x180 },
        { 0x20, 0x81A0, 0x1A0 },
        { 0x40, 0x81C0, 0x1C0 },
        { 0x40, 0x8200, 0x200 },
        { 0x40, 0x8240, 0x240 },
        { 0x40, 0x8280, 0x280 },
        { 0x40, 0x82C0, 0x2C0 },
    };

    /* Func_10362: Load tile data from PNG */
    const char *name;
    if (state->config->pokemon.pokemon_loaded && species < NUM_POKEMON && state->config->pokemon.species[species].animated_pic_name[0])
        name = state->config->pokemon.species[species].animated_pic_name;
    else
        name = mon_animated_pic_names[species];
    char path[260];
    snprintf(path, sizeof(path), "%s/gfx/billboard/mon_animated/%s.w32.interleave.png",
             state->asset_base_path, name);
    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }

    size_t data_size = 0;
    uint8_t *tile_data = tiles_from_png(path, &data_size);
    if (tile_data) {
        interleave_tiles(tile_data, data_size, 4); /* 32px wide = 4 tiles/row */
        for (int i = 0; i < 13; i++) {
            uint16_t src_off = tile_copy_table[i].src_offset;
            uint16_t size = tile_copy_table[i].size;
            if (src_off + size <= (uint16_t)data_size) {
                vram_write(state->vram, 0, tile_copy_table[i].vram_dest,
                           tile_data + src_off, size);
            }
        }
        free(tile_data);
    }

    /* Func_10301: Load palette data → OBJ palettes 3 and 5 */
    const uint16_t *pal = mon_animated_palettes[species];
    for (int i = 0; i < 4; i++)
        state->obj_palettes[3].colors[i] = pal[i];     /* palette1 → OBJ palette 3 */
    for (int i = 0; i < 4; i++)
        state->obj_palettes[5].colors[i] = pal[4 + i]; /* palette2 → OBJ palette 5 */

    /* Combined view: also update the per-half palette snapshots so
     * render_sprites_combined() sees the Pokemon-specific colors. */
    if (state->combined_view_active) {
        state->obj_palettes_bottom[3] = state->obj_palettes[3];
        state->obj_palettes_bottom[5] = state->obj_palettes[5];
    }
}

/* Cached billboard tile data for the current catch'em species */
static uint8_t *cached_billboard_on_tiles = NULL;   /* "on" tiles from mon_pics */
static uint8_t *cached_billboard_off_tiles = NULL;  /* "off" tiles from mon_silhouettes */

/* Data_102a4 (0x102a4): Billboard tile flip order.
 * Maps sequential flip index (0-23) to physical tile position (0-23). */
static const uint8_t billboard_tile_order[24] = {
    0x00, 0x07, 0x06, 0x01, 0x0E, 0x15, 0x14, 0x0F,
    0x04, 0x0B, 0x0A, 0x05, 0x0C, 0x13, 0x12, 0x0D,
    0x02, 0x09, 0x08, 0x03, 0x10, 0x17, 0x16, 0x11
};

/* PointerTable_10274 (0x10274): Billboard bg_map addresses for each tile (0-23).
 * 6x4 grid at bg_map rows 4-7, cols 7-12. */
static const uint16_t billboard_bgmap_addrs[24] = {
    0x9887, 0x9888, 0x9889, 0x988A, 0x988B, 0x988C,
    0x98A7, 0x98A8, 0x98A9, 0x98AA, 0x98AB, 0x98AC,
    0x98C7, 0x98C8, 0x98C9, 0x98CA, 0x98CB, 0x98CC,
    0x98E7, 0x98E8, 0x98E9, 0x98EA, 0x98EB, 0x98EC
};

/*=============================================================================
 * Load billboard tile data for the current catch'em species.
 * Loads both "on" (colored) and "off" (silhouette) tile data from PNG files.
 * Cached for use by process_billboard_illumination.
 *===========================================================================*/
static void load_mon_billboard_picture(GameState *state) {
    free(cached_billboard_on_tiles);
    free(cached_billboard_off_tiles);
    cached_billboard_on_tiles = NULL;
    cached_billboard_off_tiles = NULL;

    uint8_t species = state->current_catchem_mon;
    if (species >= NUM_POKEMON) return;

    const char *name = mon_billboard_pic_names[species];
    char path[260];
    size_t data_size = 0;

    /* Load "on" tiles from mon_pics */
    snprintf(path, sizeof(path), "%s/gfx/billboard/mon_pics/%s.png",
             state->asset_base_path, name);
    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
    cached_billboard_on_tiles = tiles_from_png(path, &data_size);

    /* Load "off" tiles from mon_silhouettes */
    snprintf(path, sizeof(path), "%s/gfx/billboard/mon_silhouettes/%s.png",
             state->asset_base_path, name);
    for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
    cached_billboard_off_tiles = tiles_from_png(path, &data_size);
}

/*=============================================================================
 * Func_102bc (0x102bc): Load species billboard BG palette data.
 * Copies species palette data into bg_palettes[6] and bg_palettes[7].
 *===========================================================================*/
static void load_mon_billboard_palettes(GameState *state) {
    uint8_t species = state->current_catchem_mon;
    if (species >= NUM_POKEMON) return;

    const uint16_t *pal = mon_billboard_palettes[species];
    for (int i = 0; i < 4; i++)
        state->bg_palettes[6].colors[i] = pal[i];
    for (int i = 0; i < 4; i++)
        state->bg_palettes[7].colors[i] = pal[4 + i];

    /* Combined view: sync per-half BG palette snapshots */
    if (state->combined_view_active) {
        state->bg_palettes_bottom[6] = state->bg_palettes[6];
        state->bg_palettes_bottom[7] = state->bg_palettes[7];
    }
}

/*=============================================================================
 * Func_10184 (0x10184): Process billboard illumination state changes.
 * For each of 24 tiles, compares byte[0] (current) vs byte[1] (previous).
 * If different: copies byte[0] to byte[1], writes tile data to VRAM bank 0
 * at $8900, and updates palette attributes in bg_map bank 1.
 * Only runs on bottom stages (bit 0 of current_stage set).
 *===========================================================================*/
void process_billboard_illumination(GameState *state) {
    if (!(state->current_stage & 1)) return;
    if (!cached_billboard_on_tiles) return;

    uint8_t species = state->current_catchem_mon;
    if (species >= NUM_POKEMON) return;

    for (int c = 0; c < 24; c++) {
        uint8_t on_state = state->billboard_tiles_illumination_states[c * 2];
        uint8_t prev_state = state->billboard_tiles_illumination_states[c * 2 + 1];

        /* Copy current to previous (ASM: ld [hli], a) */
        state->billboard_tiles_illumination_states[c * 2 + 1] = on_state;

        if (on_state == prev_state) continue;

        uint8_t tile_index = billboard_tile_order[c];
        uint16_t src_offset = (uint16_t)tile_index * 16;

        /* Func_101d9: Write tile data to VRAM bank 0 at $8900 + tile_index*16 */
        const uint8_t *src;
        if (on_state) {
            src = cached_billboard_on_tiles + src_offset;
        } else {
            if (cached_billboard_off_tiles)
                src = cached_billboard_off_tiles + src_offset;
            else {
                static const uint8_t blank[16] = {0};
                src = blank;
            }
        }
        vram_write(state->vram, 0, 0x8900 + tile_index * 16, src, 16);

        /* Func_10230: Update palette attribute in bg_map bank 1 */
        uint8_t palette_attr;
        if (on_state) {
            palette_attr = mon_billboard_palette_maps[species][tile_index];
        } else {
            palette_attr = 0x05; /* "off" tiles use BG palette 5 */
        }
        uint16_t bgmap_addr = billboard_bgmap_addrs[tile_index];
        state->vram->bg_map[1][bgmap_addr - 0x9800] = palette_attr;
    }
}

/*=============================================================================
 * Func_14210 (0x14210): Refresh all billboard illumination.
 * Toggles all byte[1] values to force all tiles "dirty", then calls
 * process_billboard_illumination + load_mon_billboard_palettes.
 *===========================================================================*/
static void refresh_billboard_illumination(GameState *state) {
    for (int i = 0; i < 24; i++) {
        uint8_t val = state->billboard_tiles_illumination_states[i * 2];
        state->billboard_tiles_illumination_states[i * 2 + 1] = val ^ 1;
    }
    process_billboard_illumination(state);
    load_mon_billboard_palettes(state);
}

/* Free cached billboard tile data */
void free_cached_billboard_data(void) {
    free(cached_billboard_on_tiles);
    free(cached_billboard_off_tiles);
    cached_billboard_on_tiles = NULL;
    cached_billboard_off_tiles = NULL;
}

/*=============================================================================
 * Func_1414b — Special Mode Sprite/Tile Management
 *
 * Called during _LoadStageDataRedFieldBottom. Only active when in special mode
 * (catch'em or evolution, NOT map_move).
 *
 * When catching + wd5c6/capturing: clears billboard, loads animated mon sprites.
 * When catching + !wd5c6 + !capturing: Func_14210 refreshes billboard illumination.
 *===========================================================================*/
void func_1414b_special_mode_gfx(GameState *state) {
    if (!state->in_special_mode)
        return;
    if (state->special_mode == 2) /* MAP_MOVE: no graphics needed */
        return;

    /* Check if we have a capturing mon or wd5c6 state */
    if (!state->wd5c6 && !state->capturing_mon) {
        /* Func_14210: Load billboard pic data and refresh illumination.
         * Toggles all tiles dirty, processes into VRAM writes, loads palettes. */
        load_mon_billboard_picture(state);
        load_billboard_tilemap(state);
        refresh_billboard_illumination(state);
        return;
    }

    /* Active capture/display: clear billboard tilemap + load animated mon sprites */
    /* ASM .asm_14165: Func_141f2 (clear billboard area) */
    clear_billboard_tilemap(state);

    /* Func_10362 (0x10362): Load animated catch'em mon sprite tiles into VRAM.
     * Func_10301 (0x10301): Load animated mon palette data (OBJ palettes 3+5). */
    load_animated_mon_tiles_and_palettes(state);

    /* Load ball capture smoke + shake tiles if actively capturing */
    if (!state->capturing_mon)
        return;

    /* BallCaptureSmoke2Gfx → vTilesOB tile $7E ($87E0), $20 bytes (2 tiles) */
    {
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/ball_capture_smoke_2.png",
                 state->asset_base_path);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data) {
            uint16_t copy = (data_size < 0x20) ? (uint16_t)data_size : 0x20;
            vram_write(state->vram, 0, 0x87E0, tile_data, copy);
            free(tile_data);
        }
    }

    /* BallCaptureSmokeGfx → vTilesSH tile $10 ($8900), $180 bytes (24 tiles)
     * interleave format (8x16 sprite pairs) */
    {
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/ball_capture_smoke.interleave.png",
                 state->asset_base_path);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data) {
            /* PNG is 8px wide = 1 tile/row; interleave pairs adjacent rows */
            interleave_tiles(tile_data, data_size, 1);
            uint16_t copy = (data_size < 0x180) ? (uint16_t)data_size : 0x180;
            vram_write(state->vram, 0, 0x8900, tile_data, copy);
            free(tile_data);
        }
    }

    /* LoadShakeBallGfx (0x104e2): Ball-type-specific shake gfx → vTilesOB tile $38 ($8380), $40 bytes */
    {
        const char *shake_name;
        switch (state->ball_type) {
        case 1:  shake_name = "ball_greatball_shake.w16.interleave"; break;
        case 2:  shake_name = "ball_ultraball_shake.w16.interleave"; break;
        case 3:  shake_name = "ball_masterball_shake.w16.interleave"; break;
        default: shake_name = "ball_pokeball_shake.w16.interleave"; break;
        }
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/%s.png",
                 state->asset_base_path, shake_name);
        for (char *p = path; *p; p++) { if (*p == '/') *p = '\\'; }
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data) {
            interleave_tiles(tile_data, data_size, 2); /* w16 = 2 tiles/row */
            uint16_t copy = (data_size < 0x40) ? (uint16_t)data_size : 0x40;
            vram_write(state->vram, 0, 0x8380, tile_data, copy);
            free(tile_data);
        }
    }
}

/*=============================================================================
 * _LoadStageDataRedFieldTop (0x14000)
 * Complete stage data load for red field top stage.
 * Matches ASM call list exactly.
 *===========================================================================*/
void load_stage_data_red_field_top(GameState *state) {
    func_14091(state);
    load_field_structure_graphics(state);
    load_upgrade_triggers_graphics(state);
    load_staryu_graphics_top(state);
    update_spinner_charge_graphics(state);
    load_evolution_trinket_graphics(state);
    load_slot_cave_cover_graphics(state);
    /* LoadBallGraphics (ball_gfx.asm): load ball tiles + OBJ palette by type/size */
    switch (state->ball_size) {
    case 1:  load_mini_ball_gfx(state); break;
    case 2:  load_super_mini_ball_gfx(state); break;
    default: load_ball_gfx(state); break;
    }
    /* LoadTimerGraphics: NOP on GBC (ret nz after hGameBoyColorFlag) */
}

/*=============================================================================
 * _LoadStageDataRedFieldBottom (0x1401c)
 * Complete stage data load for red field bottom stage.
 * Matches ASM call list exactly.
 *===========================================================================*/
void load_stage_data_red_field_bottom(GameState *state) {
    func_14091(state);
    load_billboard_graphics_red_field(state);
    clear_all_red_indicators(state);
    /* LoadCAVELightsGraphics_RedField: called within load_red_field_bottom_graphics */
    load_billboard_status_bar_graphics(state);
    func_1414b_special_mode_gfx(state);
    load_evolution_trinket_graphics(state);
    load_again_text_graphics(state);
    draw_ball_saver_icon(state);
    load_red_field_bottom_graphics(state);
    load_staryu_graphics_bottom(state);
    load_bonus_multiplier_railing_graphics(state);
    load_slot_cave_cover_graphics(state);
    /* LoadBallGraphics (ball_gfx.asm): load ball tiles + OBJ palette by type/size */
    switch (state->ball_size) {
    case 1:  load_mini_ball_gfx(state); break;
    case 2:  load_super_mini_ball_gfx(state); break;
    default: load_ball_gfx(state); break;
    }
    /* LoadTimerGraphics: NOP on GBC */
}
