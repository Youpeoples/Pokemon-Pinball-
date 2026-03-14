#ifndef GAME_STATE_H
#define GAME_STATE_H

#include "types.h"
#include "constants.h"

/* Forward declarations */
typedef struct VirtualVRAM VirtualVRAM;
typedef struct AudioEngine AudioEngine;
typedef struct ConfigData ConfigData;
typedef struct ScriptEngine ScriptEngine;
typedef struct EditorState EditorState;

/*=============================================================================
 * Field Select - Dynamic Table Discovery
 *===========================================================================*/
#define MAX_FIELD_SELECT_TABLES 16

/* Preview region constants (tiles) */
#define FS_PREVIEW_START_ROW   2    /* Start at border top (row 2) */
#define FS_PREVIEW_NUM_COLS    9    /* 72px wide (border + 7 content + border) */
#define FS_PREVIEW_LEFT_COL    1    /* Left slot border-left column */
#define FS_PREVIEW_RIGHT_COL   10   /* Right slot border-left column */
#define FS_SAVE_NUM_ROWS       14   /* Saved area: rows 2-15 (border + preview + label) */
#define FS_SAVE_NUM_TILES      126  /* 9 x 14 */

typedef struct FieldSelectEntry {
    char name[32];           /* Display name from manifest */
    char folder[64];         /* Folder name (e.g., "my_table") */
    uint8_t starting_stage;  /* Bottom stage ID to start playing */
    bool is_builtin;         /* true for Red/Blue Field */
    bool has_preview;        /* true if preview PNG exists */
    char preview_path[260];  /* Full path to preview PNG */
    bool has_palette;        /* true if custom palette specified */
    uint16_t palette[4];     /* RGB555 custom preview palette */
} FieldSelectEntry;

typedef struct FieldSelectState {
    FieldSelectEntry tables[MAX_FIELD_SELECT_TABLES];
    uint8_t num_tables;
    uint8_t cursor_index;      /* Currently selected table index */
    uint8_t visible_offset;    /* Index of table in left visible slot */
    bool needs_reload;         /* True when visible slots changed */
    bool scanned;              /* True after initial scan */
    bool has_custom_tables;    /* True if any non-builtin tables found */

    /* Saved original tilemap/bgattr for preview + border + label regions */
    uint8_t orig_left_tilemap[FS_SAVE_NUM_TILES];
    uint8_t orig_left_bgattr[FS_SAVE_NUM_TILES];
    uint8_t orig_right_tilemap[FS_SAVE_NUM_TILES];
    uint8_t orig_right_bgattr[FS_SAVE_NUM_TILES];
    bool originals_saved;
} FieldSelectState;

/*=============================================================================
 * HRAM State
 * Corresponds to hram.asm ($FF80-$FFFE)
 *===========================================================================*/
typedef struct HRAMState {
    /* Joypad (FF98-FF9C) */
    uint8_t joypad_state;            /* hJoypadState: current buttons */
    uint8_t newly_pressed_buttons;   /* hNewlyPressedButtons */
    uint8_t pressed_buttons;         /* hPressedButtons */
    uint8_t prev_previous_joypad;    /* hPrevPreviousJoypadState */
    uint8_t previous_joypad;         /* hPreviousJoypadState */
    uint8_t joy_repeat_delay;        /* hJoyRepeatDelay */

    /* LCD shadow registers (FF9E-FFB0) */
    uint8_t lcdc;
    uint8_t stat;
    uint8_t scy;
    uint8_t scx;
    uint8_t lyc;
    uint8_t bgp;
    uint8_t obp0;
    uint8_t obp1;
    uint8_t wy;
    uint8_t wx;
    uint8_t last_lyc;
    uint8_t next_lyc_sub;
    uint8_t lyc_sub;
    uint8_t next_frame_hblank_scx;
    uint8_t hblank_scx;
    uint8_t next_frame_hblank_scy;
    uint8_t hblank_scy;
    uint8_t lcdc_mask;
    uint8_t stat_intr_routine;
    uint8_t stat_lcdc_xor;       /* hFFB1: XOR mask applied to LCDC at LYC scanline (0=none) */

    /* Timing (FFB2-FFB5) */
    uint8_t num_frames_since_last_vblank;
    uint8_t frame_counter;
    uint8_t vblank_count;
    uint8_t stat_intr_fired;

    /* Math temps (FFB6-FFB7) */
    uint8_t signed_math_sign;
    uint8_t signed_math_sign2;

    /* Ball position copies in HRAM (FFBA-FFBD) */
    uint16_t ball_x_pos;
    uint16_t ball_y_pos;

    /* Flipper temps (FFBF-FFC4) */
    uint8_t flipper_collision_radius;
    uint16_t flipper_state_change;
    uint8_t previous_flipper_state;
    uint8_t flipper_state;

    /* System (FFF8-FFFE) */
    uint8_t loaded_rom_bank;     /* hLoadedROMBank - irrelevant in C */
    uint8_t rom_bank_buffer;
    uint8_t sgb_flag;
    uint8_t sgb_init;
    uint8_t gbc_flag_backup;
    uint8_t gbc_flag;            /* hGameBoyColorFlag: always 1 for GBC */
} HRAMState;

/*=============================================================================
 * Audio Channel State (0x32 bytes each in original)
 *===========================================================================*/
typedef struct AudioChannelState {
    uint8_t data[0x32];
} AudioChannelState;

/*=============================================================================
 * Main Game State
 * Corresponds to wram.asm (WRAM Bank 0 + Bank 1)
 * This is the master struct holding ALL game variables
 *===========================================================================*/
typedef struct GameState {
    /* --- HRAM --- */
    HRAMState hram;

    /* --- Sprite OAM shadow buffer (D000-D09F) --- */
    OAMEntry sprite_buffer[GBC_OAM_ENTRIES];
    uint8_t sprite_buffer_size;  /* wSpriteBufferSize */

    /* --- Palette data (D200-D2FF) --- */
    GBCPalette bg_palettes[GBC_NUM_BG_PALETTES];    /* wPaletteData BG */
    GBCPalette obj_palettes[GBC_NUM_OBJ_PALETTES];  /* wPaletteData OBJ */
    GBCPalette fade_bg_palettes[GBC_NUM_BG_PALETTES];
    GBCPalette fade_obj_palettes[GBC_NUM_OBJ_PALETTES];
    uint8_t fade_counter;              /* 0=idle, 1-16=active fade frame */
    uint8_t fade_direction;            /* 0=fade in (white→target), 1=fade out (current→white) */

    /* --- Party Pokemon (D300-D3FF) --- */
    uint8_t party_mons[256];       /* wPartyMons */
    uint8_t num_party_mons;        /* wNumPartyMons */
    uint8_t cur_selected_party_mon;
    uint8_t cur_selected_party_mon_scroll_offset;
    uint8_t party_selection_cursor_counter;
    uint8_t evolution_menu_active;       /* H4: 1=menu is shown, 0=not */

    /* --- Score system (D400-D495) --- */
    uint8_t add_score_queue[0x60]; /* wAddScoreQueue: circular buffer, 16 entries × 6 bytes */
    uint8_t score_to_add[6];      /* wScoreToAdd: 6-byte BCD */
    uint8_t score[6];             /* wScore: 6-byte BCD */
    uint8_t player_name[3];       /* wPlayerName */
    uint8_t high_score_id[4];     /* wHighScoreId */
    uint8_t add_score_queue_offset; /* Write pointer into add_score_queue (wd477) */
    uint8_t score_queue_read_offset;  /* wd478: read pointer for Func_85c7 */
    uint8_t score_queue_caught_up;    /* wd479: snapshot of write offset when caught up */
    uint8_t score_changed;            /* wd49f: set to 1 when score needs redraw */
    uint8_t score_changed_pad;        /* wd49f is ds $2 in ASM; second byte unused */
    uint8_t current_jackpot[4];   /* wCurrentJackpot: 4-byte BCD */

    /* --- Ball state (D47E-D4C9) --- */
    uint8_t ball_type;             /* wBallType */
    uint16_t ball_type_counter;    /* wBallTypeCounter */
    uint8_t ball_type_backup;
    uint8_t cur_bonus_multiplier;  /* wCurBonusMultiplier */

    /* End-of-ball bonus */
    uint8_t end_of_ball_bonus_category_score[6];
    uint8_t end_of_ball_bonus_subtotal[6];
    uint8_t end_of_ball_bonus_total_score[6];

    /* Stage transitions */
    uint8_t going_to_bonus_stage;
    uint8_t returning_from_bonus_stage;
    uint8_t next_stage;
    uint8_t next_bonus_stage;
    uint8_t initial_next_bonus_stage;
    uint8_t completed_bonus_stage;

    /* Lives */
    uint8_t extra_balls;
    uint8_t extra_ball_state;
    uint8_t cur_ball_life;
    uint8_t num_ball_lives;

    /* Ball saver */
    uint8_t ball_saver_icon_on;
    uint8_t ball_saver_flash_rate;
    uint8_t ball_saver_timer_frames;
    uint8_t ball_saver_timer_seconds;
    uint8_t num_times_ball_saved_text_will_display;
    uint8_t ball_saver_timer_frames_backup;
    uint8_t ball_saver_timer_seconds_backup;
    uint8_t num_times_ball_saved_text_will_display_backup;
    uint8_t extra_ball;              /* wExtraBall */

    uint8_t draw_bottom_message_box;
    uint8_t ball_bonus_wait_for_button_press;
    uint8_t end_of_ball_bonus_active;  /* 1 = EndOfBallBonus is running */
    uint8_t end_of_ball_bonus_state;   /* Sub-state machine for EndOfBallBonus screen */
    uint8_t end_of_ball_bonus_step;    /* Which category/scroll step is being displayed */
    uint8_t end_of_ball_bonus_timer;   /* Frame timer for display pacing */
    uint8_t end_of_ball_bonus_count;   /* Remaining count for current category */

    /* --- Stage state (D4AC-) --- */
    uint8_t current_stage;           /* wCurrentStage */
    uint8_t current_stage_backup;
    uint8_t move_to_next_screen_state;
    uint8_t stage_collision_state;
    uint8_t stage_collision_state_backup;

    /* --- Ball physics (D4B3-D4C8) --- */
    ufixed8_8 ball_x_pos;           /* wBallXPos: unsigned 8.8 fixed-point */
    ufixed8_8 ball_y_pos;           /* wBallYPos */
    ufixed8_8 prev_ball_x_pos;     /* wPreviousBallXPos */
    ufixed8_8 prev_ball_y_pos;     /* wPreviousBallYPos */
    fixed8_8 ball_x_velocity;       /* wBallXVelocity */
    fixed8_8 ball_y_velocity;       /* wBallYVelocity */
    uint8_t ball_spin;              /* wBallSpin */
    uint8_t ball_rotation;          /* wBallRotation */
    uint8_t ball_size;              /* wBallSize: 0=default, 1=mini, 2=super mini */
    uint8_t lost_ball;
    uint8_t show_extra_ball_text;
    uint8_t ball_loss_sfx_delay;    /* Countdown for 30-frame silence before loss SFX */
    uint8_t ball_lost_from_transition; /* Set by check_stage_transition for $FF table entries */

    /* --- Field object collision state --- */
    /* Voltorb/Shellder bumpers */
    uint8_t which_voltorb;
    uint8_t which_voltorb_id;
    Animation voltorb1_anim;
    Animation voltorb2_anim;
    Animation voltorb3_anim;
    uint8_t voltorb_hit_anim_duration;
    uint8_t which_animated_voltorb;

    /* Bumpers */
    uint8_t which_bumper;
    uint8_t which_bumper_id;
    uint8_t bumper_light_up_duration;
    uint8_t which_bumper_gfx;

    /* Pinball launch */
    uint8_t pinball_launch_collision;
    uint8_t pinball_launched;
    uint8_t wd4df;                       /* Write-only, cleared on ball loss (ASM 0xd4df) */
    uint8_t chose_initial_map;
    uint8_t initial_map_selection_index;
    uint8_t map_cycling_frames;           /* Countdown timer for ChooseInitialMap billboard cycling */

    /* Map move */
    uint8_t num_map_moves;
    uint8_t visited_maps[7];

    /* Triggered objects */
    uint8_t triggered_game_object;
    uint8_t triggered_game_object_index;
    uint8_t previous_triggered_game_object;

    /* Diglett / Psyduck+Poliwag */
    uint8_t which_diglett;
    uint8_t which_diglett_id;
    uint8_t left_diglett_anim_controller;
    uint8_t left_map_move_counter;
    uint8_t right_diglett_anim_controller;
    uint8_t right_map_move_counter;
    uint8_t left_map_move_diglett_anim_counter;
    uint8_t left_map_move_diglett_frame;
    uint8_t right_map_move_diglett_anim_counter;
    uint8_t right_map_move_diglett_frame;
    uint16_t left_map_move_counter_frames_until_decrease;
    uint16_t right_map_move_counter_frames_until_decrease;

    /* Bellsprout */
    uint8_t bellsprout_collision;
    Animation bellsprout_anim;

    /* Staryu (wd502-wd503) */
    uint8_t staryu_collision;
    uint8_t staryu_side;             /* wd502 bit 0: which side staryu is on */
    uint8_t staryu_anim_active;      /* wd502 bit 1: animation playing */
    uint8_t staryu_timer;            /* wd503: frames until side change */
    Animation staryu_anim;

    /* Spinner */
    uint8_t spinner_collision;
    uint8_t spinner_state[2];
    int16_t spinner_velocity;

    /* CAVE lights */
    uint8_t which_cave_light;
    uint8_t which_cave_light_id;
    uint8_t cave_light_states[4];
    uint8_t cave_lights_blinking;
    uint8_t cave_lights_blinking_frames_remaining;

    /* Pikachu saver */
    uint8_t which_pikachu;
    uint8_t which_pikachu_id;
    uint8_t pikachu_saver_charge;
    uint8_t which_pikachu_saver_side;
    Animation pikachu_saver_anim;
    uint8_t pikachu_saver_state;             /* wd51c: 0=idle, 1=full save anim, 2=partial bounce */
    uint8_t pikachu_saver_slot_reward_active;
    uint8_t pikachu_saver_sound_cooldown;    /* wd51e: countdown for charge-full sound */

    /* Board triggers / alley triggers */
    uint8_t which_board_trigger;
    uint8_t which_board_trigger_id;
    uint8_t collided_alley_triggers[8];

    /* Indicator states */
    uint8_t indicator_states[0x13];
    uint8_t left_alley_trigger;
    uint8_t left_alley_count;
    uint8_t right_alley_trigger;
    uint8_t right_alley_count;
    uint8_t secondary_left_alley_trigger[2];

    /* Ball visibility / physics flags */
    uint8_t pinball_is_visible;
    uint8_t enable_ball_gravity_and_tilt;

    /* Current map and special mode */
    uint8_t current_map;
    uint8_t in_special_mode;
    uint8_t special_mode_collision_id;
    uint8_t special_mode_state;
    uint8_t special_mode;

    /* Evolution mode */
    uint8_t evolution_objects_disabled;
    uint8_t current_evolution_mon;
    uint8_t current_evolution_type;
    uint8_t num_evolution_trinkets;
    uint8_t num_possible_evolution_objects;
    uint16_t evolution_trinket_cooldown_frames;
    uint8_t evolution_object_states[10];
    uint8_t active_evolution_trinkets[18];
    uint8_t collided_point_index;

    /* Catch'em mode */
    uint8_t current_catchem_mon;
    uint8_t wd558;                   /* Backup indicator state (red:[2], blue:[0]) */
    uint8_t wd559;                   /* Backup indicator state ([3] for both) */
    uint8_t indicator_state_2_backup; /* Blue field only: backup of indicators[2] */

    /* Timer */
    uint8_t timer_seconds;
    uint8_t timer_minutes;
    uint8_t timer_frames;
    uint8_t timer_active;
    uint8_t time_ran_out;
    uint8_t pause_timer;
    uint8_t timer_digits[4];
    uint8_t wd580;                   /* Timer-related state, cleared on ball init */

    /* Billboard */
    uint8_t billboard_tiles_illumination_states[0x30];
    uint8_t number_of_catch_mode_tiles_flipped;
    uint8_t billboard_reveal_frame_counter;  /* wd54e: frames until next flicker toggle */
    uint8_t billboard_reveal_flicker_count;  /* wd54f: remaining flicker cycles */

    /* Wild mon state */
    uint8_t wild_mon_is_hittable;
    uint8_t current_animated_mon_sprite_type;
    uint8_t current_animated_mon_sprite_frame;
    uint8_t loops_until_next_catch_sprite_anim_change;
    uint8_t ball_hit_wild_mon;
    uint8_t num_mon_hits;
    uint8_t current_catch_mon_idle_frame1_duration;
    uint8_t current_catch_mon_idle_frame2_duration;
    uint8_t current_catch_mon_hit_frame_duration;
    uint8_t catch_mode_mon_update_timer;
    uint8_t num_mew_hits;
    uint8_t wd5c6;                   /* Catch mode state flag (cleared by ConcludeCatchEmMode) */
    uint8_t wild_mon_collision;

    /* Bottom text system */
    uint8_t bottom_text_enabled;
    uint8_t disable_draw_scoreboard_info;
    ScrollingText scrolling_text[3];
    StationaryText stationary_text[3];

    /* Ball capture animation */
    uint8_t capturing_mon;
    Animation ball_capture_anim;

    /* Ball upgrade triggers */
    uint8_t which_pinball_upgrade_trigger;
    uint8_t which_pinball_upgrade_trigger_id;
    uint8_t ball_upgrade_trigger_states[3];
    uint8_t ball_upgrade_triggers_blinking;
    uint8_t ball_upgrade_triggers_blinking_frames_remaining;

    /* Ditto slot */
    uint8_t ditto_slot_collision;
    uint8_t ditto_enter_or_exit_counter;

    /* Slot */
    uint8_t slot_collision;
    uint8_t slot_enter_or_exit_counter;
    uint8_t slot_is_open;
    uint8_t slot_glowing_anim_counter;
    uint8_t frames_until_slot_cave_opens;
    uint8_t opened_slot_by_cave_lights;
    uint8_t opened_slot_by_pokeballs;

    /* Bonus multiplier railings */
    uint8_t which_bonus_multiplier_railing;
    uint8_t which_bonus_multiplier_railing_id;
    uint8_t bonus_multiplier_tens_digit;
    uint8_t bonus_multiplier_ones_digit;
    uint8_t show_bonus_multiplier_bottom_message;
    uint8_t wd610;                   /* Bonus mult railing blinking state (3=idle, 1=hit, 2=animating) */
    uint8_t wd614;                   /* Backup of tens digit for bottom message */
    uint8_t wd615;                   /* Backup of ones digit for bottom message */

    /* Game over */
    uint8_t game_over[3];

    /* Slot reward/roulette */
    uint8_t wd611;                   /* Bonus mult railing ones-side state (3=idle, 1=hit, 2=animating) */
    uint8_t wd612;                   /* Bonus mult railing animation timer (0x80 countdown) */
    uint8_t slot_reward_progress;
    uint8_t cur_slot_reward_roulette_index;
    uint8_t slot_roulette_counter;
    uint8_t slot_roulette_billboard_picture;
    uint8_t slot_roulette_slowed;
    uint8_t cur_slot_bonus;
    uint8_t slot_any_pokemon_caught;
    uint8_t slot_ball_increase;
    uint8_t catchem_or_evolution_slot_reward_active;
    uint8_t bonus_stage_slot_reward_active;
    uint8_t slot_roulette_active;     /* Non-zero while roulette state machine runs */
    uint8_t slot_roulette_state;      /* Sub-state for roulette */
    uint8_t slot_roulette_anim_counter; /* Frame counter for blink/display animations */

    /* Pokeballs under billboard */
    uint8_t previous_num_pokeballs;
    uint8_t num_pokeballs;
    uint8_t pokeball_blinking_counter;

    /* Per-ball bonus counters */
    uint8_t num_pokemon_caught_in_ball_bonus;
    uint8_t num_pokemon_evolved_in_ball_bonus;
    uint8_t num_bellsprout_entries;
    uint8_t num_dugtrio_triples;
    uint8_t num_cave_completions;
    uint8_t num_spinner_turns;
    uint8_t num_pikachu_saves;
    uint8_t num_mewtwo_bonus_completions;

    /* Slowpoke / Cloyster (blue field top) */
    uint8_t slowpoke_collision;
    Animation slowpoke_anim;
    uint8_t slowpoke_anim_frame;           /* wSlowpokeAnimationFrame: sprite frame index */
    uint8_t slowpoke_anim_frame_counter;   /* wSlowpokeAnimationFrameCounter */
    uint8_t slowpoke_anim_index;           /* wSlowpokeAnimationIndex */
    uint8_t cloyster_collision;
    Animation cloyster_anim;
    uint8_t cloyster_anim_frame;           /* wCloysterAnimationFrame: sprite frame index */
    uint8_t cloyster_anim_frame_counter;   /* wCloysterAnimationFrameCounter */
    uint8_t cloyster_anim_index;           /* wCloysterAnimationIndex */
    uint8_t num_slowpoke_entries;
    uint8_t num_cloyster_entries;
    uint8_t num_psyduck_triples;
    uint8_t num_poliwag_triples;

    /* Shellder (blue field top) */
    uint8_t which_shellder;
    uint8_t which_shellder_id;
    uint8_t which_animated_shellder;       /* wWhichAnimatedShellder: 0xFF=none */
    uint8_t shellder_hit_anim_duration;    /* wShellderHitAnimationDuration */

    /* Psyduck / Poliwag (blue field bottom) */
    uint8_t which_psyduck_poliwag;
    uint8_t which_psyduck_poliwag_id;
    uint8_t psyduck_state;                 /* wPsyduckState: 0/1/2/3 */
    uint8_t poliwag_state;                 /* wPoliwagState: 0/1/2 */
    uint8_t left_map_move_poliwag_anim_counter;  /* wLeftMapMovePoliwagAnimationCounter */
    uint8_t left_map_move_poliwag_frame;         /* wLeftMapMovePoliwagFrame */
    uint8_t right_map_move_psyduck_anim_counter; /* wRightMapMovePsyduckAnimationCounter */
    uint8_t right_map_move_psyduck_frame;        /* wRightMapMovePsyduckFrame */

    /* Blue field specific */
    uint8_t blue_stage_force_field_direction;     /* 0=up, 1=right, 2=down, 3=left */
    uint8_t blue_stage_force_field_gfx_needs_loading;
    uint8_t blue_stage_force_field_flipped_down;
    uint8_t wd642;                         /* Evolution mode slowpoke flag */
    uint8_t wd643;                         /* Force field direction logic */
    uint8_t wd644;                         /* Map move direction / psyduck flag */
    uint8_t wd648;                         /* Force field indicator left */
    uint8_t wd649;                         /* Force field indicator right */
    uint8_t wd64a;                         /* Force field indicator up */
    uint8_t wd64b;                         /* Force field last direction */
    uint8_t bonus_multiplier_railing_end_light_duration;
    uint8_t blue_field_force_field_frame_counter;
    uint8_t blue_field_force_field_seconds_counter;

    /* --- Bonus stage state --- */

    /* Gengar bonus — entity struct: 9 bytes per ghost
     * [0]=enabled [1-3]=Animation [4]=extra [5-6]=x_pos [7-8]=y_pos */
    uint8_t gengar_bonus_closed_gate;
    uint8_t which_gravestone;
    uint8_t gastly1_enabled;
    Animation gastly1_anim;
    uint8_t gastly1_in_hit_anim;
    uint16_t gastly1_x_pos;
    uint16_t gastly1_y_pos;
    uint8_t gastly2_enabled;
    Animation gastly2_anim;
    uint8_t gastly2_in_hit_anim;
    uint16_t gastly2_x_pos;
    uint16_t gastly2_y_pos;
    uint8_t gastly3_enabled;
    Animation gastly3_anim;
    uint8_t gastly3_in_hit_anim;
    uint16_t gastly3_x_pos;
    uint16_t gastly3_y_pos;
    uint8_t num_gastly_hits;
    uint8_t gastly_y_offset_counter[3];   /* wd675/wd679/wd67d — floating bob */
    uint8_t gastly_x_direction[3];        /* horizontal float direction (0=right, 1=left) */
    uint8_t gastly_x_offset_counter[3];   /* horizontal float offset counter */
    uint8_t haunter1_enabled;
    Animation haunter1_anim;
    uint8_t haunter1_in_hit_anim;
    uint16_t haunter1_x_pos;
    uint16_t haunter1_y_pos;
    uint8_t haunter2_enabled;
    Animation haunter2_anim;
    uint8_t haunter2_in_hit_anim;
    uint16_t haunter2_x_pos;
    uint16_t haunter2_y_pos;
    uint8_t num_haunter_hits;
    uint8_t haunter_y_offset_counter[2];  /* wd691/wd693 — floating bob */
    uint8_t haunter_x_direction[2];       /* horizontal float direction (0=right, 1=left) */
    uint8_t haunter_x_offset_counter[2];  /* horizontal float offset counter */
    uint8_t gengar_enabled;
    Animation gengar_anim;
    uint8_t gengar_phase;                 /* wd69c — descent/ascent phase */
    uint16_t gengar_x_pos;
    uint16_t gengar_y_pos;
    uint8_t num_gengar_hits;
    uint8_t gengar_defeated;              /* wd6a8 — stage complete flag */
    uint8_t gengar_returning;             /* wd6a7 — ball-loss return flag */
    uint8_t gengar_gate_state;            /* wd656 — 0=open, 1=closed */
    uint8_t gengar_prev_anim_state;       /* wd6a3 — previous gengar animation frame for descent */
    uint8_t gengar_upper_tilt_active;     /* wd6a4 */
    uint8_t gengar_upper_tilt_counter;    /* wd6a5 */
    uint8_t gengar_upper_tilt_cooldown;   /* wd6a6 — cooldown after tilt push */
    uint8_t gengar_collision_trigger;     /* wd657 — gastly collision trigger flag */
    uint8_t gengar_collision_object;      /* wd658 — gastly collision object ID */
    uint8_t haunter_collision_trigger;    /* wd67c — haunter collision trigger flag */
    uint8_t haunter_collision_object;     /* wd67d — haunter collision object ID */
    uint8_t gengar_hit_trigger;           /* wd696 — gengar collision trigger flag */
    uint8_t gengar_hit_object;            /* wd697 — gengar collision object ID */
    uint8_t gastly_gfx_countdown;         /* wd674 */
    uint8_t haunter_gfx_countdown;        /* wd690 */
    uint8_t gengar_gfx_countdown;         /* wd6a1 */

    /* Mewtwo bonus */
    uint8_t mewtwo_bonus_closed_gate;       /* wd6a9 */
    uint8_t mewtwo_collision_trigger;       /* wd6aa — set when Mewtwo tile collision detected */
    uint8_t mewtwo_collision_object;        /* wd6ab — triggered object ID */
    Animation mewtwo_anim;                  /* wd6ac — 3-byte animation struct */
    uint8_t mewtwo_anim_index;              /* wd6af — index into MewtwoAnimations table (0-3) */
    uint8_t mewtwo_hit_counter;             /* wd6b0 — orb hits in current group (0-2) */
    uint8_t mewtwo_regen_phase;             /* wd6b1 — regeneration phase (0-8, 8=defeated) */
    uint8_t mewtwo_ball_loss_processed;     /* wd6b2 */
    uint8_t mewtwo_bonus_completed;         /* wd6b3 — stage cleared flag */
    uint8_t mewtwo_orb_collision;           /* wd6b4 — orbiting ball collision flag */
    uint8_t mewtwo_triggered_orb_index;     /* wd6b5 — which orb was hit (1-6) */
    OrbitingBall orbiting_balls[6];         /* wd6b6-wd6e5, 8 bytes each */

    /* Meowth bonus */
    uint8_t meowth_bonus_closed_gate;       /* wd6e6 */
    Animation meowth_anim;                  /* wMeowthAnimation (3 bytes) */
    uint8_t meowth_state;                   /* wMeowthState: 0=left walk,1=right walk,2=left hit,3=right hit,4=timeout */
    uint8_t meowth_x_position;              /* wMeowthXPosition (0-128) */
    uint8_t meowth_y_position;              /* wMeowthYPosition (16=top, 32=bottom) */
    int8_t  meowth_x_movement;              /* wMeowthXMovement (-1/+1) */
    int8_t  meowth_y_movement;              /* wMeowthYMovement (-1/0/+1) */
    uint8_t num_active_jewels_bottom;       /* wNumActiveJewelsBottom (0-3) */
    uint8_t num_active_jewels_top;          /* wNumActiveJewelsTop (0-3) */
    uint8_t meowth_stage_bonus_counter;     /* wMeowthStageBonusCounter (0-6, wraps at 7) */
    uint8_t meowth_stage_score;             /* wMeowthStageScore (0-20) */
    uint8_t disable_meowth_jewel_production; /* wDisableMeowthJewelProduction */
    uint8_t meowth_jewel_production_state;  /* wd6e7: 0=idle,1=produce,2=moving to produce */
    uint8_t meowth_jewel_spawn_flag;        /* wd6f3 */
    /* Jewel state (6 jewels: 0-2=bottom, 3-5=top) */
    uint8_t meowth_jewel_anim_index[6];
    uint8_t meowth_jewel_state[6];          /* 0=inactive,1=falling,2=active,3=hit,4=cleanup */
    uint8_t meowth_jewel_x_coord[6];
    uint8_t meowth_jewel_y_coord[6];
    uint8_t meowth_jewel_phase_counter[6];  /* wd6f5-wd6f7 + wd6ff-wd701: movement phase */
    uint8_t meowth_jewel_collision_ctr[6];  /* wd6f8-wd6fa + wd702-wd704 */
    uint8_t meowth_jewel_bounce_ctr[6];     /* wd6fb-wd6fd + wd705-wd707 */
    uint8_t meowth_jewel_direction[6];      /* wd72a-wd72c + wd734-wd736: 0=left,1=right */
    /* Multiplier display */
    uint8_t meowth_multiplier_active;       /* wd795 */
    Animation meowth_multiplier_anim;       /* wd797-wd799 */
    uint8_t meowth_multiplier_index;        /* wd79a: which multiplier 0-4 (2x-6x) */
    uint8_t meowth_multiplier_x;            /* wd79c */
    uint8_t meowth_multiplier_y;            /* wd79e */
    /* Progress sparkle */
    uint8_t meowth_sparkle_active;          /* wd64e */
    uint8_t meowth_sparkle_counter;         /* wd64f (0-19) */
    uint8_t meowth_sparkle_cycle;           /* wd650 (0-9, then disable) */
    uint8_t meowth_score_display_offset;    /* wd651 */
    uint8_t meowth_sparkle_x;              /* wd652: X position for sparkle sprite */
    /* Stage state */
    uint8_t meowth_completion_state;        /* wd712: completion/music counter */
    uint8_t meowth_transition_timer;        /* wd739 */
    uint8_t meowth_anim_update_flag;        /* wd710 */

    /* Diglett bonus */
    uint8_t diglett_bonus_closed_gate;     /* wd73a */
    uint8_t diglett_collision_trigger;     /* wd73b — set when diglett hit */
    uint8_t diglett_collision_object;      /* wd73c — which diglett was hit */
    uint8_t diglett_states[0x1F];          /* wd73d — 31 diglett states */
    uint8_t current_diglett;               /* wd75c — position in init/update order */
    uint8_t digletts_initialized_flag;     /* wd75d — set when all digletts initialized */
    uint8_t diglett_init_delay_counter;    /* wd75e — 8-bit accumulator for init timing */
    uint8_t dugtrio_collision_trigger;     /* wd75f — set when dugtrio hit */
    uint8_t dugtrio_collision_data;        /* wd760 — dugtrio collision data */
    Animation dugtrio_anim;                /* wd761 — 3-byte animation struct */
    uint8_t dugtrio_state;                 /* wd764 — dugtrio state machine (0-7) */
    uint8_t wd765;                         /* wd765 — dugtrio collision map refresh flag */

    /* Seel bonus */
    uint8_t seel_bonus_closed_gate;         /* wSeelBonusClosedGate */
    uint8_t seel_stage_streak;              /* wSeelStageStreak */
    uint8_t seel_stage_score;               /* wSeelStageScore */
    /* Per-seel state (3 seels, 10 bytes each: wd76b-wd789) */
    /* Each seel: [anim_data(3)] [state(1)] [x_lo(1)] [x_hi(1)] [y_lo(1)] [y_hi(1)] [timer(1)] [direction(1)] */
    Animation seel_anim[3];                 /* wd769/wd773/wd77d: 3-byte animation structs */
    uint8_t seel_state[3];                  /* wd76c/wd776/wd780: animation state ID */
    uint8_t seel_x_lo[3];                   /* wd76d/wd777/wd781: X position low byte */
    uint8_t seel_x_hi[3];                   /* wd76e/wd778/wd782: X position high byte */
    uint8_t seel_y_lo[3];                   /* wd76f/wd779/wd783: Y position low byte */
    uint8_t seel_y_hi[3];                   /* wd770/wd77a/wd784: Y position high byte */
    uint8_t seel_timer[3];                  /* wd771/wd77b/wd785: counter/timer */
    uint8_t seel_direction[3];              /* wd772/wd77c/wd786: 0=right, 1=left */
    /* Collision */
    uint8_t seel_collision_flag;            /* wd767: 1 if collision occurred */
    uint8_t seel_collision_index;           /* wd768: which seel (0-2) was hit */
    /* Completion state */
    uint8_t seel_stage_state;               /* wd791: 0=normal, 1=one_emerged, 2=two_emerged, 3=timeout */
    uint8_t seel_completion_state;          /* wd794: 0=running, 1=timeout, 2=just_cleared, 5=cleared_playing */
    /* Multiplier display */
    uint8_t seel_multiplier_active;         /* wd795: 0=inactive, 0xFF=active */
    Animation seel_multiplier_anim;         /* wd797-wd799: multiplier animation struct */
    uint8_t seel_multiplier_index;          /* wd79a: multiplier level (0-8, 0xFF=none) */
    uint8_t seel_multiplier_x;              /* wd79c: sprite X position */
    uint8_t seel_multiplier_y;              /* wd79e: sprite Y position */
    /* Progress sparkle */
    uint8_t seel_sparkle_active;            /* wd64e */
    uint8_t seel_sparkle_frame;             /* wd64f (0-19) */
    uint8_t seel_sparkle_cycle;             /* wd650 (0-9, then disable) */
    uint8_t seel_score_display_val;         /* wd651: score to display */
    uint8_t seel_sparkle_x;                 /* wd652: X position for sparkle sprite */
    uint8_t seel_transition_timer;          /* wd739 */

    /* --- Tilt state (D79F-D7AC) --- */
    uint8_t left_and_right_tilt_pixels_offset;
    uint8_t upper_tilt_pixels_offset;
    uint8_t left_tilt_counter;
    uint8_t left_tilt_reset;
    uint8_t right_tilt_counter;
    uint8_t right_tilt_reset;
    uint8_t upper_tilt_counter;
    uint8_t upper_tilt_reset;
    uint8_t left_tilt_pushing;
    uint8_t right_tilt_pushing;
    uint8_t upper_tilt_pushing;

    /* --- Scroll state --- */
    uint8_t scx;                     /* wSCX */
    uint8_t disable_horizontal_scroll_for_ball_start;
    uint8_t red_stage_structure_backup;
    uint8_t previous_field_structure_state; /* wd7f2: previous stage_collision_state for structure graphics */

    /* --- Flipper state (D7AE-D7BE) --- */
    uint16_t left_flipper_state;
    uint16_t left_flipper_state_change;
    uint16_t right_flipper_state;
    uint16_t right_flipper_state_change;
    uint8_t previous_left_flipper_state;
    uint8_t previous_right_flipper_state;
    uint8_t flipper_collision_normal_angle;
    uint8_t flipper_collision;
    int16_t flipper_x_force;
    int16_t flipper_y_force;
    uint8_t flippers_disabled;

    /* --- Collision system (D7C3-D7F8) --- */
    uint8_t sub_tile_ball_x_pos;
    uint8_t sub_tile_ball_y_pos;
    uint8_t upper_left_collision_attr;
    uint8_t lower_left_collision_attr;
    uint8_t upper_right_collision_attr;
    uint8_t lower_right_collision_attr;
    uint8_t collision_point_tests[16];
    uint8_t is_ball_colliding;
    uint8_t collision_normal_angle;
    uint8_t collision_force_amplification;
    uint16_t stage_collision_map_pointer;  /* In C: index into collision data */
    uint8_t stage_collision_map_bank;
    uint16_t stage_collision_masks_pointer;
    uint8_t stage_collision_masks_bank;
    uint16_t ball_position_tile_offset;
    uint8_t cur_collision_attribute;
    uint16_t cur_collision_tile_offset;
    uint8_t no_collision_applied;

    /* --- Collision map (C700-C9FF) --- */
    uint8_t stage_collision_map[0x300];

    /* --- In-game menu --- */
    uint8_t in_game_menu_active;     /* 1 = pause menu is open, physics frozen */
    uint8_t in_game_menu_index;      /* 0 = SAVE, 1 = CANCEL */
    uint8_t in_game_menu_cleanup;    /* countdown frames for exit cleanup */

    /* --- Audio control --- */
    uint8_t sfx_timer;
    uint8_t stage_song;
    uint8_t stage_song_bank;
    uint8_t update_audio_using_timer_interrupt;
    uint8_t toggle_audio_update_method;
    uint8_t audio_engine_enabled;

    /* --- Rumble --- */
    uint8_t rumble_pattern;
    uint8_t rumble_duration;

    /* --- Persistent joypad states (D808-D80A) --- */
    uint8_t joypad_state_persistent;
    uint8_t newly_pressed_persistent;
    uint8_t pressed_persistent;

    /* --- DMG palette shadows (D80C-D80E) --- */
    uint8_t bgp_shadow;
    uint8_t obp0_shadow;
    uint8_t obp1_shadow;

    /* --- RNG state (D80F-D848) --- */
    uint8_t rng_sub;
    uint8_t rng_modulus;
    uint8_t rng_pointer;
    uint8_t rng_values[54];
    uint8_t rng_sub2;
    uint8_t rng_sram_seed;      /* sRNGMod (SRAM $AFFF) — seed for ResetRNG */

    /* --- Screen state machine (D8F1-D8F2) --- */
    uint8_t current_screen;     /* wCurrentScreen */
    uint8_t screen_state;       /* wScreenState */

    /* --- Field select --- */
    uint8_t field_select_pressed_button;

    /* --- Title screen --- */
    uint8_t title_screen_cursor_selection;
    uint8_t title_screen_game_start_cursor_selection[2];
    uint8_t title_screen_blink_anim_frame;
    uint8_t title_screen_blink_anim_counter;
    uint8_t title_screen_bouncing_ball_anim_frame;
    uint8_t title_screen_pokeball_anim_counter;
    uint8_t titlescreen_continue_prompt_anim_frame;
    uint8_t titlescreen_continue_prompt_anim_timer;

    /* --- Field select screen --- */
    uint8_t field_select_blinking_border_timer;
    uint8_t selected_field_index;
    uint8_t field_select_blinking_border_frame;
    uint8_t field_select_border_anim_step;   /* wd915: index in border anim table */

    /* --- Options screen --- */
    uint8_t options_menu_selection;        /* wd916: 0=Rumble, 1=KeyConfig, 2=BGM/SFX */
    uint8_t options_rumble_setting;        /* wd917: 0=Enabled, 1=Disabled */
    uint8_t options_key_config_selection;  /* wd918: 0-7 key config cursor */
    uint8_t options_bgm_sfx_selection;     /* wd919: 0=BGM, 1=SFX */
    uint8_t sound_test_current_bgm;
    uint8_t sound_test_current_sfx;
    uint8_t sound_test_exit_delay;         /* 3-frame delay on B button exit */
    uint8_t sound_test_bgm_delay;          /* 3-frame delay before BGM play (Func_c6e8) */
    uint8_t sound_test_bgm_pending_bank;   /* pending BGM bank to play after delay */
    uint8_t sound_test_bgm_pending_id;     /* pending BGM id to play after delay */
    uint8_t options_psyduck_anim_frame;
    uint8_t options_psyduck_anim_timer;
    uint8_t options_pikachu_anim_frame;
    uint8_t options_pikachu_anim_timer;
    uint8_t options_pokeball_anim_frame;
    uint8_t options_pokeball_anim_timer;

    /* Key config detection state machine (native-only, replaces ASM blocking loop) */
    uint8_t key_config_detect_phase;    /* 0=navigate, 1=wait release, 2=detect primary,
                                           3=wait release2, 4=detect secondary */
    uint8_t key_config_detect_row;      /* which row (0-6) being configured */
    uint8_t key_config_detect_timeout;  /* countdown for secondary key (90 frames) */
    uint8_t key_config_captured_primary; /* captured primary button mask */
    uint8_t key_config_previous_capture; /* Func_c9be: previous resolved button state */

    /* --- Key configs (D948-D955) --- */
    KeyConfig key_config_ball_start;
    KeyConfig key_config_left_flipper;
    KeyConfig key_config_right_flipper;
    KeyConfig key_config_left_tilt;
    KeyConfig key_config_right_tilt;
    KeyConfig key_config_upper_tilt;
    KeyConfig key_config_menu;

    /* --- Pokedex (D956-D961) --- */
    uint8_t pokedex_description_page_flag;
    uint8_t cur_pokedex_index;
    uint8_t pokedex_offset;
    uint8_t pokedex_blinking_cursor_counter;
    uint8_t pokedex_window_was_shifted;
    uint8_t pokedex_cursor_was_moved;
    uint8_t pokedex_start_button_is_pressed;
    uint8_t pokedex_flags[NUM_POKEMON];  /* wPokedexFlags: per-pokemon seen/caught */
    uint16_t num_pokemon_seen;
    uint16_t num_pokemon_owned;
    uint8_t pokedex_input_delay;         /* wd95c: repeat timer for directional input */
    uint8_t pokedex_input_accum;         /* wd95e: persistent button accumulator */
    uint8_t pokedex_prev_mon_state;      /* wd961: previous mon display state */
    uint8_t pokedex_desc_scroll_counter; /* ScrollPokemonDescriptionDown frame counter */
    uint8_t pokedex_desc_scroll_dir;     /* 0=scroll down (enter), 1=scroll up (exit) */

    /* --- High scores (D9FD-DA7E) --- */
    HighScore red_high_scores[5];
    HighScore blue_high_scores[5];
    uint8_t high_score_is_entering_name;
    uint8_t high_score_name_column;
    uint8_t high_score_name_row;
    uint8_t high_score_name_entry_blink_counter;
    uint8_t high_scores_stage;
    uint8_t high_scores_arrow_anim_counter;
    uint8_t high_scores_print_send_selection;
    uint8_t high_scores_field_switch_counter;  /* 0=idle, >0=animating switch */
    uint8_t high_scores_field_switch_dir;      /* 0=red→blue, 1=blue→red */

    /* --- Save state --- */
    uint8_t loading_saved_game;
    uint8_t saved_game;

    /* --- Bottom message buffer (C500-C6FF) --- */
    uint8_t bottom_message_text[256];     /* wBottomMessageText */
    uint8_t bottom_message_buffer[256];   /* wBottomMessageBuffer */

    /* --- Mon collision mask (C400-C47F) --- */
    uint8_t mon_animated_collision_mask[128];

    /* --- Joypad repeat delay settings --- */
    uint8_t joy_initial_delay;   /* wd806 */
    uint8_t joy_repeat_rate;     /* wd807 */

    /* --- Map move direction --- */
    uint8_t map_move_direction;
    uint8_t rare_mons_flag;

    /* --- Slot force field data (loaded from ball_physics_f0000.bin, 9216 bytes) --- */
    uint8_t *slot_force_field_data;
    size_t slot_force_field_data_size;

    /* --- External config data (not in original GBC) --- */
    ConfigData *config;             /* JSON-loaded config (physics, scores, tables, pokemon) */

    /* --- Dynamic field select (not in original GBC) --- */
    FieldSelectState field_select;  /* Dynamic table discovery for field select screen */

    /* --- Lua scripting engine (not in original GBC) --- */
    ScriptEngine *script_engine;    /* Lua scripting engine for moddable table logic */
    char active_table_folder[64];   /* Custom table folder override for scripting */

    /* --- Stage builder / editor mode (not in original GBC) --- */
    uint8_t editor_mode;            /* F12 toggles: 0=off, 1=editor active */
    EditorState *editor_state;      /* Allocated on first use, persists across resets */

    /* --- Debug overlay (not in original GBC) --- */
    uint8_t debug_mode;             /* F1 toggles: 0=off, 1=text overlay */
    float debug_fps;                /* Measured FPS (updated once per second) */
    uint8_t debug_sprite_count;     /* Number of active OAM sprites this frame */

    /* --- Native renderer state (not in original GBC) --- */
    VirtualVRAM *vram;              /* Virtual VRAM for tile/map data */
    AudioEngine *audio;             /* Audio engine pointer for music/SFX calls */
    char asset_base_path[260];      /* Path to project root (where gfx/ lives) */
    uint8_t gfx_loaded;            /* Flag: stage graphics loaded into VRAM */

    /* --- VWF font data (loaded once, reused) --- */
    uint8_t *vwf_font_gfx;         /* Interleaved 2bpp font data (malloc'd) */
    size_t vwf_font_gfx_size;      /* Size of font data in bytes */
    const char *pokedex_desc_next;  /* wd957:wd958 - continuation ptr for multi-page */

} GameState;

/*=============================================================================
 * Game state lifecycle
 *===========================================================================*/
GameState *game_state_init(void);
void game_state_free(GameState *state);
void game_state_reset(GameState *state);
void reset_high_scores_to_defaults(GameState *state);

#endif /* GAME_STATE_H */
