/*
 * Core Pinball Game Logic
 *
 * Translated from engine/pinball_game.asm.
 *
 * HandlePinballGame dispatches via wScreenState to 5 sub-functions:
 *   0 = LoadGFX         - Load stage graphics
 *   1 = StartBall       - Initialize ball and LCD settings
 *   2 = HandleBallPhysics - Main gameplay frame
 *   3 = HandleBallLoss  - Ball drain animation
 *   4 = EndBall         - End of ball / game over transition
 *
 * The physics frame (state 2) is the heart of the game:
 *   ApplyGravity → LimitVelocity → HandleTilts → HandleFlippers →
 *   CheckStageCollision → CheckObjectCollisions → ResolveCollisions →
 *   CheckMenu → MoveBall → CheckStageTransition → DrawSprites →
 *   UpdateScoreboard → DecrementTimer
 */

#include "game/pinball.h"
#include "game/tilt.h"
#include "game/config_data.h"
#include "game/scripting.h"
#include "game/editor.h"
#include <stdlib.h>
#include <stdio.h>
#include "game/joypad.h"
#include "game/billboard.h"
#include "game/collision.h"
#include "game/flippers.h"
#include "game/physics_math.h"
#include "game/red_field.h"
#include "game/draw_red_field.h"
#include "game/gengar_bonus.h"
#include "game/mewtwo_bonus.h"
#include "game/meowth_bonus.h"
#include "game/diglett_bonus.h"
#include "game/seel_bonus.h"
#include "game/blue_field.h"
#include "game/draw_blue_field.h"
#include "game/score.h"
#include "game/timer.h"
#include "game/ball_gfx.h"
#include "game/save.h"
#include "audio/audio.h"
#include "renderer/stage_assets.h"
#include "renderer/tile_loader.h"
#include "renderer/vram.h"
#include <SDL.h>

/* Gravity constant: default 0x000B per frame (now configurable via config/physics.json) */

/*
 * Scrolling text headers (from text/scrolling_text.asm).
 * Format: { delay, start_off+0x40, stop_off+0x40, pause_dur, text_off*0x10, total_steps }
 * scrolling_text_normal MACRO: delay, stop_dur, text_slot, text_len
 *   start = 0x14 (20), stop = delay + 0x40 = start (start scrolls from off-screen)
 *   total_steps = text_len + stop_dur + (start - stop)
 */
static const uint8_t BALL_SAVED_HEADER[6]  = { 5, 0x54, 0x45, 20, 0x00, 51 };
static const uint8_t SHOOT_AGAIN_HEADER[6] = { 4, 0x54, 0x44, 20, 0x00, 51 };
static const uint8_t END_BONUS_HEADER[6]   = { 1, 0x54, 0x41, 20, 0x00, 53 };

/*
 * In-game pause menu tile map constants.
 * Menu renders to the window layer, WY=$60 shows it on screen.
 * ASM text encoding: char + 0xBF = tile index.
 * We write directly to win_map since we own the window layer.
 */
static const uint8_t MENU_TILE_BLANK  = 0x81;
static const uint8_t MENU_TILE_CURSOR = 0x86;  /* Arrow cursor from menu_symbols */

/* Saved rumble setting during pause menu (ASM saves wd917 to stack) */
static uint8_t saved_rumble_setting = 0;

/* Convert char to menu tile index (ASM: char + 0xBF) */
static uint8_t menu_char(char c) {
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 0xBF);
    if (c >= '0' && c <= '9') return (uint8_t)(0x86 + (c - '0'));
    return MENU_TILE_BLANK;
}

/*
 * Open the pause menu.
 * From HandleInGameMenu (0x86d7): sets WY=$60, draws SAVE/CANCEL to window.
 */
static void open_pause_menu(GameState *state) {
    state->in_game_menu_active = 1;
    state->in_game_menu_index = 1;  /* Default: CANCEL selected */
    state->in_game_menu_cleanup = 0;
    /* ASM menu.asm: save rumble setting and disable during menu */
    saved_rumble_setting = state->options_rumble_setting;
    state->options_rumble_setting = 1;  /* 1 = disabled */
    /* ASM menu.asm lines 7-9: clear bottom text before opening menu */
    fill_bottom_message_buffer_with_black_tile(state);
    state->draw_bottom_message_box = 0;
    /* ASM menu.asm lines 26-30: Load InGameMenuSymbolsGfx+$50 (1 tile, 16 bytes)
     * to vTilesSH+$60 ($8860). This is the arrow cursor tile (tile ID $86). */
    if (state->vram) {
        char path[260];
        snprintf(path, sizeof(path), "%s/gfx/stage/menu_symbols.png",
                 state->asset_base_path);
        size_t data_size = 0;
        uint8_t *tile_data = tiles_from_png(path, &data_size);
        if (tile_data && data_size > 0x60) {
            vram_write(state->vram, 0, 0x8860, tile_data + 0x50, 0x10);
        }
        free(tile_data);
    }
    /* ASM menu.asm lines 36-41: WY=$60, LYC=$5F, LCDCMask=$FD */
    state->hram.wy = 0x60;
    state->hram.lyc = 0x5F;
    state->hram.lcdc_mask = 0xFD;  /* Disable OBJ in window area */
    /* D-04: ASM HandleInGameMenu (0x86d7) plays no SFX on menu open */
}

/*
 * Draw the pause menu to the window tilemap.
 * Two rows: "  SAVE  " and " CANCEL ", with cursor on selected row.
 */
static void draw_pause_menu(GameState *state) {
    if (!state->vram) return;
    VirtualVRAM *vram = state->vram;

    /* Clear window tilemap rows 0-7 (8 rows × 32 tiles = 256 bytes).
     * WY=$60 shows ~6 rows of window; clear extra to prevent garbage. */
    for (int i = 0; i < 256; i++) {
        vram->win_map[0][i] = MENU_TILE_BLANK;
        vram->win_map[1][i] = 0x00;
    }

    /* Row 1 (offset 32): " [>] S A V E " */
    int row1 = 32;
    if (state->in_game_menu_index == 0)
        vram->win_map[0][row1 + 3] = MENU_TILE_CURSOR;
    vram->win_map[0][row1 + 4] = menu_char('S');
    vram->win_map[0][row1 + 5] = menu_char('A');
    vram->win_map[0][row1 + 6] = menu_char('V');
    vram->win_map[0][row1 + 7] = menu_char('E');

    /* Row 3 (offset 96): " [>] C A N C E L " */
    int row3 = 96;
    if (state->in_game_menu_index == 1)
        vram->win_map[0][row3 + 3] = MENU_TILE_CURSOR;
    vram->win_map[0][row3 + 4] = menu_char('C');
    vram->win_map[0][row3 + 5] = menu_char('A');
    vram->win_map[0][row3 + 6] = menu_char('N');
    vram->win_map[0][row3 + 7] = menu_char('C');
    vram->win_map[0][row3 + 8] = menu_char('E');
    vram->win_map[0][row3 + 9] = menu_char('L');
}

/*
 * Per-frame pause menu update.
 * Returns true while menu is active (physics should be skipped).
 */
static bool update_pause_menu(GameState *state) {
    if (!state->in_game_menu_active)
        return false;

    /* Cleanup phase: wait before resuming (ASM menu.asm lines 56-86) */
    if (state->in_game_menu_cleanup > 0) {
        state->in_game_menu_cleanup--;
        if (state->in_game_menu_cleanup == 0) {
            /* Restore normal state */
            state->in_game_menu_active = 0;
            /* ASM lines 58-64: restore WY, LYC, hLastLYC, LCDCMask */
            state->hram.wy = 0x86;
            state->hram.lyc = 0x83;
            state->hram.last_lyc = 0x83;
            state->hram.lcdc_mask = 0xFF;
            /* ASM lines 76-80: reload status bar tile at $8860 (1 tile, 16 bytes)
             * from StageRedFieldTopStatusBarSymbolsGfx_GameBoyColor + $60
             * (or StageBlueFieldTopStatusBarSymbolsGfx_GameBoyColor for blue field) */
            if (state->vram) {
                char path[260];
                const char *field_dir = (state->current_stage == STAGE_BLUE_FIELD_TOP ||
                                         state->current_stage == STAGE_BLUE_FIELD_BOTTOM)
                                        ? "blue_top" : "red_top";
                snprintf(path, sizeof(path), "%s/gfx/stage/%s/status_bar_symbols_gameboycolor.png",
                         state->asset_base_path, field_dir);
                size_t data_size = 0;
                uint8_t *tile_data = tiles_from_png(path, &data_size);
                if (tile_data && data_size > 0x70) {
                    vram_write(state->vram, 0, 0x8860, tile_data + 0x60, 0x10);
                }
                free(tile_data);
            }
            /* ASM lines 82-86: FillBottomMessageBufferWithBlackTile, then wDrawBottomMessageBox=1 */
            fill_bottom_message_buffer_with_black_tile(state);
            state->draw_bottom_message_box = 1;
            /* ASM menu.asm: restore rumble setting saved when menu opened */
            state->options_rumble_setting = saved_rumble_setting;

            /* ASM: HandleInGameMenu returns with Z flag based on wInGameMenuIndex.
             * If SAVE (index 0): caller transitions to SCREEN_TITLESCREEN.
             * menu.asm → pinball_game.asm: stop music, set screen to titlescreen. */
            if (state->in_game_menu_index == 0) {
                audio_stop_all(state->audio);
                state->gfx_loaded = 0;
                free_flipper_collision_data();
                billboard_free_cache();
                state->current_screen = SCREEN_TITLESCREEN;
                state->screen_state = 0;
            }
        }
        return true;
    }

    /* D-pad: move cursor (ASM MoveInGameMenuCursor 0x87c5) */
    if (state->hram.newly_pressed_buttons & BTN_UP) {
        if (state->in_game_menu_index > 0) {
            state->in_game_menu_index--;
            PLAY_SFX(state, "cursor_move", 0x00, 0x03);
        }
    }
    if (state->hram.newly_pressed_buttons & BTN_DOWN) {
        if (state->in_game_menu_index < 1) {
            state->in_game_menu_index++;
            PLAY_SFX(state, "cursor_move", 0x00, 0x03);
        }
    }

    /* A button: confirm selection */
    if (state->hram.newly_pressed_buttons & BTN_A) {
        PLAY_SFX(state, "confirm", 0x00, 0x01);
        if (state->in_game_menu_index == 0) {
            /* SAVE selected: save game state to file (ASM: SaveData to SRAM).
             * Titlescreen transition happens after cleanup delay completes. */
            state->saved_game = 1;
            state->draw_bottom_message_box = 0;
            /* N2: ASM clears rumble state on SAVE (menu.asm) */
            state->rumble_pattern = 0;
            state->rumble_duration = 0;
            save_game(state);
        }
        /* Both SAVE and CANCEL: start cleanup delay */
        state->in_game_menu_cleanup = 60;  /* 1 second */
    }

    /* Draw menu every frame */
    draw_pause_menu(state);
    return true;
}

/* Velocity and stage transition constants now read from config/physics.json */

/*
 * Apply gravity to ball velocity.
 * Translated from ApplyGravityToBall (0x2168).
 */
static void apply_gravity(GameState *state) {
    /* ASM: only checks wEnableBallGravityAndTilt (no pinball_is_visible check) */
    if (!state->enable_ball_gravity_and_tilt) {
        return;
    }
    state->ball_y_velocity += state->config->physics.gravity;
}

/*
 * Clamp ball velocity to maximum.
 * Translated from LimitBallVelocity (0x2180).
 * ASM checks only the high byte: positive cp 8 → cap at 7, negative cp -7 → cap at -7.
 */
static void limit_velocity_component(int16_t *vel, int8_t max_hi) {
    int8_t hi = (int8_t)(*vel >> 8);
    if (hi >= 0) {
        if (hi > max_hi) {
            *vel = (int16_t)(max_hi << 8) | (*vel & 0xFF);
        }
    } else {
        if (hi < -max_hi) {
            *vel = (int16_t)((int8_t)(-max_hi) << 8) | (*vel & 0xFF);
        }
    }
}

static void limit_velocity(GameState *state) {
    int8_t max_hi = state->config->physics.max_velocity_hi;
    limit_velocity_component(&state->ball_x_velocity, max_hi);
    limit_velocity_component(&state->ball_y_velocity, max_hi);
}

/*
 * AddVelocityToPosition (0x21c3): Clamps large velocities before applying.
 * If high byte >= 5, use $04FF. If high byte <= -4, use -$04FF.
 */
static void add_velocity_to_position(ufixed8_8 *pos, int16_t vel,
                                     int16_t clamp_pos, int16_t clamp_neg) {
    int8_t hi = (int8_t)(vel >> 8);
    int16_t effective_vel;
    if (hi >= 0) {
        effective_vel = (hi >= 5) ? clamp_pos : vel;
    } else {
        effective_vel = (hi < -4) ? clamp_neg : vel;
    }
    *pos = (ufixed8_8)((uint16_t)*pos + (uint16_t)effective_vel);
}

/*
 * Move ball position by velocity.
 * Translated from MoveBallPosition (0x219c).
 */
static void move_ball_position(GameState *state) {
    /* ASM: MoveBallPosition has no conditional guard — always runs */

    /* Save previous position */
    state->prev_ball_x_pos = state->ball_x_pos;
    state->prev_ball_y_pos = state->ball_y_pos;

    /* Apply clamped velocity to position */
    int16_t clamp_pos = state->config->physics.position_clamp_positive;
    int16_t clamp_neg = state->config->physics.position_clamp_negative;
    add_velocity_to_position(&state->ball_x_pos, state->ball_x_velocity, clamp_pos, clamp_neg);
    add_velocity_to_position(&state->ball_y_pos, state->ball_y_velocity, clamp_pos, clamp_neg);

    /* Copy to HRAM equivalents */
    state->hram.ball_x_pos = state->ball_x_pos;
    state->hram.ball_y_pos = state->ball_y_pos;
}

/*
 * Reload all VRAM, collision, and palettes for the current stage.
 * Translated from FieldVerticalTransition / LoadStageData (0xe674 / 0xe6c2).
 */
static void reload_stage_data(GameState *state) {
    /* ASM: Clear rumble on stage transition (FieldVerticalTransition) */
    state->rumble_pattern = 0;
    /* ASM: ClearSpriteBuffer before loading new stage data.
     * DMG palette blackout (BGP/OBP0/OBP1 = 0) is a no-op on GBC. */
    memset(state->sprite_buffer, 0, sizeof(state->sprite_buffer));
    state->sprite_buffer_size = 0;

    /* ASM: Func_1129 — clear animation states before stage load */
    state->which_animated_voltorb = 0xFF;
    state->which_bumper_gfx = 0xFF;

    /* Reload graphics into VRAM */
    load_stage_assets(state->current_stage, state->vram,
                      state, state->asset_base_path);

    /* Reload collision data */
    load_stage_collision_attributes(state);

    /* ASM: xor a / ld [wd7f2], a — clear before _LoadStageData */
    state->previous_field_structure_state = 0;

    /* Call stage-specific load functions (matches ASM _LoadStageData dispatch) */
    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        load_stage_data_red_field_top(state);
    } else if (state->current_stage == STAGE_RED_FIELD_BOTTOM) {
        load_stage_data_red_field_bottom(state);
    } else if (state->current_stage == STAGE_BLUE_FIELD_TOP) {
        load_stage_data_blue_field_top(state);
    } else if (state->current_stage == STAGE_BLUE_FIELD_BOTTOM) {
        load_stage_data_blue_field_bottom(state);
    } else if (state->current_stage == STAGE_GENGAR_BONUS) {
        load_stage_data_gengar_bonus(state);
    } else if (state->current_stage == STAGE_MEWTWO_BONUS) {
        load_stage_data_mewtwo_bonus(state);
    } else if (state->current_stage == STAGE_MEOWTH_BONUS) {
        load_stage_data_meowth_bonus(state);
    } else if (state->current_stage == STAGE_DIGLETT_BONUS) {
        load_stage_data_diglett_bonus(state);
    } else if (state->current_stage == STAGE_SEEL_BONUS) {
        load_stage_data_seel_bonus(state);
    }

    /* Reload flipper collision data if on a bottom stage */
    if (STAGE_HAS_FLIPPERS(state->current_stage)) {
        load_flipper_collision_data(state);
    }

    /* Set WY based on stage type (LoadStageData 0xe6c2):
     * Top stages: WY=$86
     * Bottom stages: WY=$86 if bottom text enabled, else $90 */
    if (state->current_stage & 1) {
        /* Bottom stage */
        state->hram.wy = state->bottom_text_enabled ? 0x86 : 0x90;
    } else {
        state->hram.wy = 0x86;
    }
}

/*
 * Check for stage transition (ball moving between top/bottom halves).
 * Translated from CheckStageTransition (0xece9) / vertical_screen_transition.asm.
 */
static void check_stage_transition(GameState *state) {
    uint8_t ball_y = UFIXED_TO_INT(state->ball_y_pos);
    const PhysicsConfig *phys = &state->config->physics;

    /* ASM CheckStageTransition (0xece9): uses lookup tables for stage transitions.
     * $FF entries mean ball loss (youLose). Valid entries mean stage transition. */

    /* Ball moving down (ball_y + $10 >= $B8, i.e. ball_y >= $A8 = 168) */
    if (ball_y >= phys->stage_transition_down_y) {
        /* BallMovingDownStageTransitions table */
        switch (state->current_stage) {
            case STAGE_RED_FIELD_TOP:
                state->current_stage = STAGE_RED_FIELD_BOTTOM;
                state->ball_y_pos = (ufixed8_8)((uint16_t)state->ball_y_pos -
                                                 phys->stage_transition_y_offset);
                reload_stage_data(state);
                return;
            case STAGE_BLUE_FIELD_TOP:
                state->current_stage = STAGE_BLUE_FIELD_BOTTOM;
                state->ball_y_pos = (ufixed8_8)((uint16_t)state->ball_y_pos -
                                                 phys->stage_transition_y_offset);
                reload_stage_data(state);
                return;
            default:
                /* $FF entry: ball loss (bottom stages, bonus stages) */
                state->ball_lost_from_transition = 1;
                return;
        }
    }

    /* Ball moving up (ball_y + $10 < $18, i.e. ball_y < 8) */
    if (ball_y < phys->stage_transition_up_y) {
        /* BallMovingUpStageTransitions table */
        switch (state->current_stage) {
            case STAGE_RED_FIELD_BOTTOM:
                state->current_stage = STAGE_RED_FIELD_TOP;
                state->ball_y_pos = (ufixed8_8)((uint16_t)state->ball_y_pos +
                                                 phys->stage_transition_y_offset);
                reload_stage_data(state);
                return;
            case STAGE_BLUE_FIELD_BOTTOM:
                state->current_stage = STAGE_BLUE_FIELD_TOP;
                state->ball_y_pos = (ufixed8_8)((uint16_t)state->ball_y_pos +
                                                 phys->stage_transition_y_offset);
                reload_stage_data(state);
                return;
            default:
                /* $FF entry: ball loss (top stages, bonus stages going up) */
                state->ball_lost_from_transition = 1;
                return;
        }
    }
}

/*
 * Check if ball has fallen below the bottom of the stage.
 * Returns true if ball is lost.
 */
/*
 * Check if ball has fallen below the bottom of the stage.
 * Translated from HandleBallLossRedField (0xdd76).
 * If ball saver is active: show "BALL SAVED" text, play SFX, return false.
 * If saver expired: start 20-second timer for next ball, return true.
 */
static bool check_ball_lost(GameState *state) {
    /* ASM CheckStageTransition + HandleBallLoss has NO pinball_is_visible guard.
     * The ball position is checked every frame regardless of visibility.
     * This is critical for bonus stages: after gengar death hides the ball,
     * the handler still needs to fire to detect gengar_defeated and return
     * to the main field. Removing the old guard that caused a softlock. */

    /* Ball loss is now triggered by check_stage_transition setting ball_lost_from_transition
     * when the ASM transition table has $FF (youLose). This covers ALL stages:
     * - RED/BLUE_FIELD_BOTTOM going down
     * - RED/BLUE_FIELD_TOP going up
     * - All bonus stages going up or down */
    if (!state->ball_lost_from_transition) return false;
    printf("[BALL_LOST] ball_lost_from_transition! stage=0x%02X ball_y=0x%04X slot_counter=%d roulette=%d\n",
        state->current_stage, state->ball_y_pos, state->slot_enter_or_exit_counter, state->slot_roulette_active);
    fflush(stdout);
    state->ball_lost_from_transition = 0;

    /* Bonus stage ball loss: dispatch to stage-specific handler.
     * ASM: CheckStageTransition .youLose sets wMoveToNextScreenState=1 BEFORE
     * calling HandleBallLoss. Bonus handlers can CLEAR it back to 0 when the
     * ball should respawn (e.g., completion animation not yet played). (#82) */
    if (state->current_stage >= FIRST_BONUS_STAGE) {
        state->move_to_next_screen_state = 1;
        if (state->current_stage == STAGE_GENGAR_BONUS) {
            handle_ball_loss_gengar_bonus(state);
        } else if (state->current_stage == STAGE_MEWTWO_BONUS) {
            handle_ball_loss_mewtwo_bonus(state);
        } else if (state->current_stage == STAGE_MEOWTH_BONUS) {
            handle_ball_loss_meowth_bonus(state);
        } else if (state->current_stage == STAGE_DIGLETT_BONUS) {
            handle_ball_loss_diglett_bonus(state);
        } else if (state->current_stage == STAGE_SEEL_BONUS) {
            handle_ball_loss_seel_bonus(state);
        }
        return state->move_to_next_screen_state != 0;
    }

    /* On main field stages (red/blue, top or bottom), check ball saver before confirming loss.
     * ASM: HandleBallLossRedField/BlueField ALWAYS checks saver regardless of
     * top/bottom stage. Removing STAGE_HAS_FLIPPERS guard. (#95) */
    if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
        /* Ball saver check (ASM: or [hl] / jr z, .youLose) */
        if (state->ball_saver_timer_seconds || state->ball_saver_timer_frames) {
            /* BALL SAVED - ASM: bit 7, a / jr nz, .skip_save_text
             * If bit 7 set: skip text display and decrement entirely.
             * Otherwise: dec unconditionally (wraps 0→0xFF setting bit 7),
             * show text, and if result is 0 set timer to expire. */
            if (!(state->num_times_ball_saved_text_will_display & 0x80)) {
                state->num_times_ball_saved_text_will_display--;
                show_ball_loss_text(state, BALL_SAVED_HEADER, "BALL SAVED");
                if (state->num_times_ball_saved_text_will_display == 0) {
                    /* Last display: force timer to expire soon */
                    state->ball_saver_timer_frames = 1;
                    state->ball_saver_timer_seconds = 1;
                }
            }
            /* ASM: lb de, $15, $02 / call PlaySoundEffect (always, even when bit 7 set) */
            PLAY_SFX(state, "ball_launch_spring", 0x15, 0x02);
            /* ASM: wMoveToNextScreenState = 1 → advance through HandleBallLoss →
             * EndBall → StartBall to reinitialize ball position at launcher. */
            state->move_to_next_screen_state = 1;
            return true;
        }
    }

    /* Ball lost - ASM HandleBallLossRedField .youLose (0xdd83):
     * 1. Stop music (PlaySong MUSIC_NOTHING — stops music only, not SFX)
     * 2. Wait 30 frames (AdvanceFrames $001E) — silence before loss SFX
     * 3. Play loss SFX (bank $25, id $24)
     * 4. Restart saver timer
     * 5. Set lost_ball flag
     * 6. Clear pinball_launched
     * 7. Conclude special mode
     * 8. Check extra balls / advance life */
    audio_stop_all(state->audio);           /* Stop music (ASM: PlaySong MUSIC_NOTHING) */
    state->ball_loss_sfx_delay = 30;        /* 30-frame delay before loss SFX */
    start_20_second_saver_timer(state);
    state->lost_ball = 1;                   /* ASM sets before ConcludeSpecialMode */
    state->pinball_launched = 0;
    state->wd4df = 0;                       /* ASM line 36: ld [wd4df], a */

    /* ConcludeSpecialMode: dispatch based on field */
    if (state->current_stage <= STAGE_RED_FIELD_BOTTOM) {
        conclude_special_mode_red_field(state);
    } else if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
        conclude_special_mode_blue_field(state);
    }

    /* Extra ball / life progression (ASM 0xddca-0xddfc).
     * ASM: ShowBallLossText BEFORE setting wGameOver (line 63-64). */
    if (state->extra_balls > 0) {
        state->extra_balls--;
        state->extra_ball_state = 1;
        show_ball_loss_text(state, END_BONUS_HEADER, "END OF BALL BONUS");
    } else if (state->cur_ball_life >= state->num_ball_lives) {
        /* Game over: show text FIRST, then set flag (ASM order) */
        show_ball_loss_text(state, END_BONUS_HEADER, "END OF BALL BONUS");
        state->game_over[0] = 1;
    } else {
        state->cur_ball_life++;
        show_ball_loss_text(state, END_BONUS_HEADER, "END OF BALL BONUS");
    }
    /* ASM: ld a, $1 / ld [wMoveToNextScreenState], a */
    state->move_to_next_screen_state = 1;
    return true;
}

/*
 * Helper: dispatch draw_sprites for the current stage.
 * Checks Lua hook first, falls back to C per-stage draw functions.
 */
static void dispatch_draw_sprites(GameState *state) {
    if (state->script_engine && state->script_engine->has_on_draw_sprites) {
        script_call_on_draw_sprites(state->script_engine, state->current_stage);
    } else if (state->current_stage <= STAGE_RED_FIELD_BOTTOM) {
        draw_red_field_sprites(state);
    } else if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
        draw_blue_field_sprites(state);
    } else if (state->current_stage == STAGE_GENGAR_BONUS) {
        draw_gengar_bonus_sprites(state);
    } else if (state->current_stage == STAGE_MEWTWO_BONUS) {
        draw_mewtwo_bonus_sprites(state);
    } else if (state->current_stage == STAGE_MEOWTH_BONUS) {
        draw_meowth_bonus_sprites(state);
    } else if (state->current_stage == STAGE_DIGLETT_BONUS) {
        draw_diglett_bonus_sprites(state);
    } else if (state->current_stage == STAGE_SEEL_BONUS) {
        draw_seel_bonus_sprites(state);
    }
}

/*
 * Scroll the screen to track the ball.
 * Translated from ScrollScreenToShowPinball (0xed5e).
 * ASM: if ball_x >= $9A, SCX += 2; else SCX -= 2; clamped [0, $21].
 */
static void scroll_screen_to_ball(GameState *state) {
    if (state->disable_horizontal_scroll_for_ball_start)
        goto apply_offsets;

    {
        uint8_t ball_x = UFIXED_TO_INT(state->ball_x_pos);
        int delta = (ball_x >= 0x9A) ? 2 : -2;
        int new_scx = (int)state->scx + delta;

        /* ASM: cp $22 / jr z (don't update if exactly $22) */
        if (new_scx == 0x22) goto apply_offsets;
        /* ASM: bit 7, a / jr nz (don't update if negative) */
        if (new_scx < 0) goto apply_offsets;
        state->scx = (uint8_t)new_scx;
    }

apply_offsets:
    /* ASM: SCX = wSCX - wLeftAndRightTiltPixelsOffset */
    state->hram.scx = (uint8_t)(state->scx - state->left_and_right_tilt_pixels_offset);
    /* ASM: SCY = 0 - wUpperTiltPixelsOffset */
    state->hram.scy = (uint8_t)(0 - state->upper_tilt_pixels_offset);
}

/*=============================================================================
 * Screen State Functions
 *===========================================================================*/

static void pinball_load_gfx(GameState *state) {
    /*
     * GameScreenFunction_LoadGFX (0xD861):
     * ASM: xor a / ld [wd908], a
     *      callba InitializeCurrentStage
     *      call FillBottomMessageBufferWithBlackTile
     *      ld a, $1 / ld [wAudioEngineEnabled], a / ld [wDrawBottomMessageBox], a
     *      inc [wScreenState]
     *
     * InitializeCurrentStage (0x8311) does:
     *   1. ClearData $C000-$C9FF (transient WRAM — handled differently in C)
     *   2. ResetDataForStageInitialization (bulk field state clear)
     *   3. Stage-specific init via CallTable_8348 (e.g. init_red_field)
     *
     * Asset/collision/stage data loading belongs in StartBall, not here.
     */

    /* ResetDataForStageInitialization (0x8388): bulk clear of per-game state.
     * ASM clears wPartyMons (0x170 bytes), wHighScoreId area (0x39 bytes),
     * and wCurrentStageBackup area (0x34d bytes) for non-bonus stages.
     * When loading a saved game, ASM only clears wSubTileBallXPos ($37 bytes). */
    if (state->loading_saved_game) {
        /* Loading saved game: skip bulk clear, only clear transient sub-pixel data */
        state->prev_ball_x_pos = 0;
        state->prev_ball_y_pos = 0;
        goto skip_stage_init;
    }
    if (state->current_stage < 6) {  /* Non-bonus stages only (ASM: cp FIRST_BONUS_STAGE) */
        /* Range 1: Party mons + score queue */
        memset(state->party_mons, 0, sizeof(state->party_mons));
        clear_score_queue(state);

        /* Range 2: Score tracking / ball type / bonus / extra balls / ball saver */
        memset(state->high_score_id, 0, sizeof(state->high_score_id));
        state->add_score_queue_offset = 0;
        memset(state->current_jackpot, 0, sizeof(state->current_jackpot));
        state->ball_type = 0;
        state->ball_type_counter = 0;
        state->ball_type_backup = 0;
        state->cur_bonus_multiplier = 0;
        memset(state->end_of_ball_bonus_category_score, 0, 6);
        memset(state->end_of_ball_bonus_subtotal, 0, 6);
        memset(state->end_of_ball_bonus_total_score, 0, 6);
        state->going_to_bonus_stage = 0;
        state->returning_from_bonus_stage = 0;
        state->next_stage = 0;
        state->next_bonus_stage = 0;
        state->initial_next_bonus_stage = 0;
        state->completed_bonus_stage = 0;
        state->extra_balls = 0;
        state->extra_ball_state = 0;
        state->cur_ball_life = 0;
        state->num_ball_lives = 0;
        state->ball_saver_icon_on = 0;
        state->ball_saver_flash_rate = 0;
        state->ball_saver_timer_frames = 0;
        state->ball_saver_timer_seconds = 0;
        state->num_times_ball_saved_text_will_display = 0;
        state->ball_saver_timer_frames_backup = 0;
        state->ball_saver_timer_seconds_backup = 0;
        state->num_times_ball_saved_text_will_display_backup = 0;
        state->extra_ball = 0;
        state->draw_bottom_message_box = 0;
        state->ball_bonus_wait_for_button_press = 0;

        /* Range 3: Stage state (collision, objects, text, special modes, etc.) */
        state->current_stage_backup = 0;
        state->move_to_next_screen_state = 0;
        state->stage_collision_state = 0;
        state->stage_collision_state_backup = 0;
        state->ball_x_pos = 0;
        state->ball_y_pos = 0;
        state->prev_ball_x_pos = 0;
        state->prev_ball_y_pos = 0;
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
        state->ball_size = 0;
        state->lost_ball = 0;
        state->show_extra_ball_text = 0;
        state->which_voltorb = 0;
        state->which_voltorb_id = 0;
        memset(&state->voltorb1_anim, 0, sizeof(Animation));
        memset(&state->voltorb2_anim, 0, sizeof(Animation));
        memset(&state->voltorb3_anim, 0, sizeof(Animation));
        state->voltorb_hit_anim_duration = 0;
        state->which_animated_voltorb = 0;
        state->which_bumper = 0;
        state->which_bumper_id = 0;
        state->bumper_light_up_duration = 0;
        state->which_bumper_gfx = 0;
        state->pinball_launch_collision = 0;
        state->pinball_launched = 0;
        state->wd4df = 0;
        state->chose_initial_map = 0;
        state->initial_map_selection_index = 0;
        state->num_map_moves = 0;
        memset(state->visited_maps, 0, sizeof(state->visited_maps));
        state->triggered_game_object = 0;
        state->triggered_game_object_index = 0;
        state->previous_triggered_game_object = 0;
        state->which_diglett = 0;
        state->which_diglett_id = 0;
        state->left_diglett_anim_controller = 0;
        state->left_map_move_counter = 0;
        state->right_diglett_anim_controller = 0;
        state->right_map_move_counter = 0;
        state->bellsprout_collision = 0;
        memset(&state->bellsprout_anim, 0, sizeof(Animation));
        state->staryu_collision = 0;
        state->staryu_anim_active = 0;
        state->staryu_timer = 0;
        memset(&state->staryu_anim, 0, sizeof(Animation));
        state->spinner_collision = 0;
        memset(state->spinner_state, 0, sizeof(state->spinner_state));
        state->spinner_velocity = 0;
        state->which_cave_light = 0;
        state->which_cave_light_id = 0;
        memset(state->cave_light_states, 0, sizeof(state->cave_light_states));
        state->cave_lights_blinking = 0;
        state->cave_lights_blinking_frames_remaining = 0;
        state->which_pikachu = 0;
        state->which_pikachu_id = 0;
        state->pikachu_saver_charge = 0;
        state->which_pikachu_saver_side = 0;
        memset(&state->pikachu_saver_anim, 0, sizeof(Animation));
        state->pikachu_saver_state = 0;
        state->pikachu_saver_slot_reward_active = 0;
        state->pikachu_saver_sound_cooldown = 0;
        state->which_board_trigger = 0;
        state->which_board_trigger_id = 0;
        memset(state->indicator_states, 0, sizeof(state->indicator_states));
        state->left_alley_trigger = 0;
        state->left_alley_count = 0;
        state->right_alley_trigger = 0;
        state->right_alley_count = 0;
        memset(state->secondary_left_alley_trigger, 0, sizeof(state->secondary_left_alley_trigger));
        state->pinball_is_visible = 0;
        state->enable_ball_gravity_and_tilt = 0;
        state->in_special_mode = 0;
        state->special_mode = 0;
        state->bottom_text_enabled = 0;
        state->disable_draw_scoreboard_info = 0;
        memset(state->scrolling_text, 0, sizeof(state->scrolling_text));
        memset(state->stationary_text, 0, sizeof(state->stationary_text));
        state->which_pinball_upgrade_trigger = 0;
        state->which_pinball_upgrade_trigger_id = 0;
        memset(state->ball_upgrade_trigger_states, 0, sizeof(state->ball_upgrade_trigger_states));
        state->ball_upgrade_triggers_blinking = 0;
        state->ball_upgrade_triggers_blinking_frames_remaining = 0;
        state->ditto_slot_collision = 0;
        state->ditto_enter_or_exit_counter = 0;
        state->slot_collision = 0;
        state->slot_enter_or_exit_counter = 0;
        state->slot_is_open = 0;
        state->slot_glowing_anim_counter = 0;
        state->frames_until_slot_cave_opens = 0;
        state->which_bonus_multiplier_railing = 0;
        state->which_bonus_multiplier_railing_id = 0;
        state->bonus_multiplier_tens_digit = 0;
        state->bonus_multiplier_ones_digit = 0;
        state->wd610 = 0;
        state->wd611 = 0;
        state->wd612 = 0;
        state->show_bonus_multiplier_bottom_message = 0;
        memset(state->game_over, 0, sizeof(state->game_over));
        state->previous_num_pokeballs = 0;
        state->num_pokeballs = 0;
        state->pokeball_blinking_counter = 0;
        state->num_pokemon_caught_in_ball_bonus = 0;
        state->num_pokemon_evolved_in_ball_bonus = 0;
        state->num_bellsprout_entries = 0;
        state->num_dugtrio_triples = 0;
        state->num_cave_completions = 0;
        state->num_spinner_turns = 0;
        state->num_pikachu_saves = 0;
        state->timer_seconds = 0;
        state->timer_minutes = 0;
        state->timer_frames = 0;
        state->timer_active = 0;
        state->time_ran_out = 0;
        state->pause_timer = 0;
        state->wd580 = 0;
    }

    printf("[LOAD_GFX] stage=0x%02X loading_saved=%d\n", state->current_stage, state->loading_saved_game);
    fflush(stdout);

    /* Try to load Lua table scripts for this stage */
    if (state->script_engine) {
        printf("[LOAD_GFX] Loading Lua scripts for stage 0x%02X...\n", state->current_stage);
        fflush(stdout);
        script_load_table_for_stage(state->script_engine, state->current_stage);
        printf("[LOAD_GFX] Lua scripts loaded. has_on_stage_init=%d\n",
            state->script_engine->has_on_stage_init);
        fflush(stdout);
    }

    /* Stage-specific initialization: C init always runs first to set up all
     * required state. Lua on_stage_init runs AFTER as an override layer,
     * allowing scripts to customize specific values without replicating
     * the full C initialization (40+ variables, collision tables, music, etc.). */
    printf("[LOAD_GFX] Stage init dispatch for 0x%02X...\n", state->current_stage);
    fflush(stdout);
    if (state->current_stage <= STAGE_RED_FIELD_BOTTOM) {
        init_red_field(state);
    } else if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
        init_blue_field(state);
    }
    else if (state->current_stage == STAGE_GENGAR_BONUS) {
        init_gengar_bonus(state);
    }
    else if (state->current_stage == STAGE_MEWTWO_BONUS) {
        init_mewtwo_bonus(state);
    }
    else if (state->current_stage == STAGE_MEOWTH_BONUS) {
        init_meowth_bonus(state);
    }
    else if (state->current_stage == STAGE_DIGLETT_BONUS) {
        init_diglett_bonus(state);
    }
    else if (state->current_stage == STAGE_SEEL_BONUS) {
        init_seel_bonus(state);
    }

    /* Lua on_stage_init runs AFTER C init as an override layer */
    if (state->script_engine && state->script_engine->has_on_stage_init) {
        script_call_on_stage_init(state->script_engine, state->current_stage);
    }

skip_stage_init:
    printf("[LOAD_GFX] Stage init complete. Filling bottom msg, advancing state.\n");
    fflush(stdout);
    fill_bottom_message_buffer_with_black_tile(state);
    state->audio_engine_enabled = 1;
    state->draw_bottom_message_box = 1;
    state->screen_state++;
}

static void pinball_start_ball(GameState *state) {
    /*
     * GameScreenFunction_StartBall (0xD87F):
     * Set up LCD registers, initialize ball position for the current stage.
     * Translated from InitBallForStage (ball_init.asm) + InitBallRedField (0x3007d).
     */

    /* LCD shadow register setup - ASM uses $67 for pinball stages:
     * Bit 0: BG enable, Bit 1: OBJ enable, Bit 2: OBJ 8x16,
     * Bit 5: Window enable, Bit 6: Window tilemap $9C00 */
    state->hram.lcdc = 0x67;
    state->hram.lcdc_mask = 0xFF;
    state->hram.stat_intr_routine = 1;
    state->hram.lyc = 0x83;  /* STAT fires at scanline $83 to switch LCDC for window */
    state->hram.last_lyc = 0x83;  /* ASM line 40: ldh [hLastLYC], a */

    /* Window position: WY=$86 places status bar at bottom.
     * ASM LoadStageData (0xe6c2): bottom stages without bottom text use WY=$90
     * to hide the window layer (no scoreboard visible). */
    state->hram.wy = 0x86;
    if ((state->current_stage & 1) && !state->bottom_text_enabled) {
        state->hram.wy = 0x90;
    }
    state->hram.wx = 0x07;

    /* InitBallForStage (0x83ba): ASM checks wLoadingSavedGame first.
     * If loading saved game, skip velocity/flipper clear (state preserved from save). */
    if (!state->loading_saved_game) {
        state->ball_x_velocity = 0;
        state->ball_y_velocity = 0;
        state->left_flipper_state = 0;      /* ASM lines 15-22 */
        state->right_flipper_state = 0;
        state->left_flipper_state_change = 0;
        state->right_flipper_state_change = 0;
        state->ball_spin = 0;
        state->ball_rotation = 0;
        state->pinball_is_visible = 1;
        state->enable_ball_gravity_and_tilt = 1;  /* ASM line 27 (overridden below for normal start) */
        state->scx = 0x20;                 /* ASM line 28-29: ld a, $20 / ld [wSCX], a */
        state->hram.scx = 0x20;
        state->hram.scy = 0;
    }

    /* Stage-specific ball position and state initialization.
     * ASM: When wLoadingSavedGame is set, InitBallForStage returns immediately
     * after TryLoadWildMonCollisionMask + RestartStageMusic — the stage-specific
     * ball init dispatch NEVER runs, preserving saved ball position. (#83/#84) */
    if (state->loading_saved_game) {
        /* #84: TryLoadWildMonCollisionMask — restore wild mon collision area if in catch-em mode.
         * ASM checks wInSpecialMode && wSpecialMode==0 && wWildMonIsHittable, then loads
         * per-species collision mask data from MonAnimatedCollisionMaskPointers. */
        if (state->in_special_mode && state->special_mode == 0 &&
            state->wild_mon_is_hittable) {
            load_wild_mon_collision_mask(state);
        }
        /* #84: RestartStageMusic — resume the correct music track from saved state */
        if (state->stage_song_bank) {
            audio_play_music(state->audio, state->stage_song_bank, state->stage_song);
        }
        goto skip_ball_init;
    }

    /* Stage-specific ball init: C init always runs first, Lua overrides after */
    if (state->current_stage <= STAGE_RED_FIELD_BOTTOM) {
        if (state->returning_from_bonus_stage) {
            /* StartBallAfterBonusStageRedField (ASM line 72-95):
             * Position at top of field, clear flags, restore ball type, restart music.
             * Reset ball_size to 0 (normal) — ASM sets wBallSize=2 in bonus ball_loss
             * but InitializeCurrentStage (which normally resets it) is skipped when
             * returning from bonus (screen_state=1, not 0). */
            state->ball_size = 0;
            state->ball_x_pos = MAKE_UFIXED(0x50, 0);
            state->ball_y_pos = MAKE_UFIXED(0x16, 0);
            state->returning_from_bonus_stage = 0;
            state->scx = 0;
            state->hram.scx = 0;
            state->flippers_disabled = 0;
            state->ball_type = state->ball_type_backup;
            PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        } else {
            /* Normal start: ball in launcher area.
             * From InitBallRedField: X=$00A7, Y=$0098 */
            state->ball_x_pos = MAKE_UFIXED(0xA7, 0);
            state->ball_y_pos = MAKE_UFIXED(0x98, 0);
            state->enable_ball_gravity_and_tilt = 0;
            state->pinball_launched = 0;
            state->wd580 = 0;
        }
    } else if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
        /* Blue field ball init — delegates to blue_field.c which handles
         * both normal start and bonus return, plus per-ball resets,
         * collision attribute init, and music restart. */
        init_ball_blue_field(state);
    } else if (state->current_stage == STAGE_GENGAR_BONUS) {
        init_ball_gengar_bonus(state);
    } else if (state->current_stage == STAGE_MEWTWO_BONUS) {
        init_ball_mewtwo_bonus(state);
    } else if (state->current_stage == STAGE_MEOWTH_BONUS) {
        init_ball_meowth_bonus(state);
    } else if (state->current_stage == STAGE_DIGLETT_BONUS) {
        init_ball_diglett_bonus(state);
    } else if (state->current_stage == STAGE_SEEL_BONUS) {
        init_ball_seel_bonus(state);
    }

    /* Lua on_ball_init runs AFTER C init as an override layer */
    if (state->script_engine && state->script_engine->has_on_ball_init) {
        script_call_on_ball_init(state->script_engine, state->current_stage);
    }

skip_ball_init:
    state->draw_bottom_message_box = 1;

    /* Load stage assets into VRAM (ASM: done in InitBallForStage → _LoadStageData).
     * Guarded by gfx_loaded to avoid redundant PNG loading on normal ball loss. */
    if (state->vram && !state->gfx_loaded) {
        printf("[START_BALL] Loading stage assets for 0x%02X...\n", state->current_stage);
        fflush(stdout);
        load_stage_assets(state->current_stage, state->vram,
                          state, state->asset_base_path);
        state->gfx_loaded = 1;
        printf("[START_BALL] Stage assets loaded.\n");
        fflush(stdout);
    }

    /* Load flipper collision data if on a bottom stage */
    if (STAGE_HAS_FLIPPERS(state->current_stage)) {
        load_flipper_collision_data(state);
    }

    /* Red field ball init: structure backup + per-ball resets */
    if (state->current_stage <= STAGE_RED_FIELD_BOTTOM) {
        /* Ball init: restore structure backup, then reset collision state.
         * From InitBallRedField lines 16-25. */
        if (state->red_stage_structure_backup & 0x80) {
            state->red_stage_structure_backup =
                state->stage_collision_state & 0xFE;
        }
        state->stage_collision_state &= 1;

        /* Per-ball variable resets (InitBallRedField lines 26-70).
         * ASM: check wLostBall → if 0 (first ball), skip resets.
         * If set (balls 2+), clear it and reset per-ball state. */
        if (state->lost_ball) {
            state->lost_ball = 0;
            state->spinner_velocity = 0;
            state->pikachu_saver_slot_reward_active = 0;
            state->pikachu_saver_charge = 0;
            state->pikachu_saver_sound_cooldown = 0;
            memset(state->cave_light_states, 0, sizeof(state->cave_light_states));
            state->left_map_move_counter = 0;
            state->right_map_move_counter = 0;
            memset(state->ball_upgrade_trigger_states, 0, 3);
            state->ball_type = POKE_BALL;
            state->num_bellsprout_entries = 0;
            state->num_cave_completions = 0;
            state->num_spinner_turns = 0;
            state->num_pikachu_saves = 0;
            state->cur_bonus_multiplier = 1;
            state->left_diglett_anim_controller = 1;
            state->right_diglett_anim_controller = 1;
            state->wd611 = 0;
            state->wd612 = 0;
            state->num_pokemon_caught_in_ball_bonus = 0;
            state->num_pokemon_evolved_in_ball_bonus = 0;
            state->num_dugtrio_triples = 0;
            state->show_bonus_multiplier_bottom_message = 0;
            state->wd610 = 3;
            state->bonus_multiplier_tens_digit = 0;
            state->bonus_multiplier_ones_digit = state->cur_bonus_multiplier;
            PLAY_MUSIC(state, "red_field", 0x0F, 0x01);
        }
    }

    /* ASM StartBall: always calls LoadStageCollisionAttributes + LoadStageData.
     * Reload collision data and stage graphics for current state. */
    /* Blue field per-ball resets (parallel to red field block above) */
    if (state->current_stage >= STAGE_BLUE_FIELD_TOP &&
        state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
        state->stage_collision_state &= 1;
        if (state->lost_ball) {
            state->lost_ball = 0;
            state->spinner_velocity = 0;
            state->pikachu_saver_slot_reward_active = 0;
            state->pikachu_saver_charge = 0;
            state->pikachu_saver_sound_cooldown = 0;
            memset(state->cave_light_states, 0, sizeof(state->cave_light_states));
            state->left_map_move_counter = 0;
            state->right_map_move_counter = 0;
            memset(state->ball_upgrade_trigger_states, 0, 3);
            state->ball_type = POKE_BALL;
            state->num_bellsprout_entries = 0;
            state->num_cave_completions = 0;
            state->num_spinner_turns = 0;
            state->num_pikachu_saves = 0;
            state->cur_bonus_multiplier = 1;
            state->left_diglett_anim_controller = 1;
            state->right_diglett_anim_controller = 1;
            state->wd611 = 0;
            state->wd612 = 0;
            state->num_pokemon_caught_in_ball_bonus = 0;
            state->num_pokemon_evolved_in_ball_bonus = 0;
            state->num_dugtrio_triples = 0;
            state->show_bonus_multiplier_bottom_message = 0;
            state->wd610 = 3;
            state->bonus_multiplier_tens_digit = 0;
            state->bonus_multiplier_ones_digit = state->cur_bonus_multiplier;
            PLAY_MUSIC(state, "blue_field", 0x10, 0x01);
        }
    }

    load_stage_collision_attributes(state);

    /* If playtesting from editor, override collision with editor's custom data.
     * Editor rows map to collision rows offset by 3 (first 3 rows are buffer). */
    if (state->editor_state && state->editor_state->playtesting &&
        state->editor_state->playtest_has_collision) {
        EditorState *ed = state->editor_state;
        uint8_t (*src)[32] = (state->current_stage & 1) ?
            ed->playtest_collision_bottom : ed->playtest_collision_top;
        for (int r = 0; r < 21; r++) {
            for (int c = 0; c < 32; c++) {
                state->stage_collision_map[(r + 3) * 32 + c] = src[r][c];
            }
        }
    }

    if (state->current_stage == STAGE_RED_FIELD_TOP) {
        load_stage_data_red_field_top(state);
    } else if (state->current_stage == STAGE_RED_FIELD_BOTTOM) {
        load_stage_data_red_field_bottom(state);
    } else if (state->current_stage == STAGE_BLUE_FIELD_TOP) {
        load_stage_data_blue_field_top(state);
    } else if (state->current_stage == STAGE_BLUE_FIELD_BOTTOM) {
        load_stage_data_blue_field_bottom(state);
    } else if (state->current_stage == STAGE_GENGAR_BONUS) {
        load_stage_data_gengar_bonus(state);
    } else if (state->current_stage == STAGE_MEWTWO_BONUS) {
        load_stage_data_mewtwo_bonus(state);
    } else if (state->current_stage == STAGE_MEOWTH_BONUS) {
        load_stage_data_meowth_bonus(state);
    } else if (state->current_stage == STAGE_DIGLETT_BONUS) {
        load_stage_data_diglett_bonus(state);
    } else if (state->current_stage == STAGE_SEEL_BONUS) {
        load_stage_data_seel_bonus(state);
    }

    /* ASM: wLoadingSavedGame cleared AFTER LoadStageData (pinball_game.asm line 61).
     * Stage data load functions check this flag for extra restoration. */
    state->loading_saved_game = 0;

    /* ASM: ScrollScreenToShowPinball during StartBall (before first physics frame) */
    scroll_screen_to_ball(state);

    /* Clear sprite buffer and draw initial sprites (ASM lines 53-54) */
    memset(state->sprite_buffer, 0, sizeof(state->sprite_buffer));
    state->sprite_buffer_size = 0;
    dispatch_draw_sprites(state);

    /* ASM: SetAllPalettesWhite + EnableLCD + FadeIn (16 frames) before advancing.
     * Set fade palettes to white ($7FFF), start 16-frame fade-in toward target palettes.
     * Physics will block until fade completes (checked at top of HandleBallPhysics). */
    for (int i = 0; i < 8; i++) {
        for (int c = 0; c < 4; c++) {
            state->fade_bg_palettes[i].colors[c] = 0x7FFF;
            state->fade_obj_palettes[i].colors[c] = 0x7FFF;
        }
    }
    state->fade_direction = 0; /* fade in (white → target) */
    state->fade_counter = 16;

    state->screen_state++;
}

static void pinball_handle_physics(GameState *state) {
    /*
     * GameScreenFunction_HandleBallPhysics (0xD909):
     * This is the main gameplay frame, running every VBlank.
     * Follows the exact order from the ASM physics frame.
     */

    /* ASM: FadeIn blocks for 16 frames before physics starts.
     * Wait for the fade-in to complete before running any physics. */
    if (state->fade_counter > 0) return;

    /* H4: Evolution menu blocks physics while active (ASM: SelectPokemonToEvolveMenu blocks) */
    if (state->evolution_menu_active) {
        update_evolution_menu(state);
        return;
    }

    /* Pause menu blocks physics while active (ASM: HandleInGameMenu blocks in loop) */
    if (update_pause_menu(state)) {
        /* Still draw sprites while paused */
        dispatch_draw_sprites(state);
        return;
    }

    /* ASM: the physics frame ALWAYS runs, even before ball is launched.
     * Gravity is disabled via enable_ball_gravity_and_tilt=0 in the launcher.
     * The ball launch button press is handled inside resolve_launch_alley().
     * All resolve handlers, collision, sprites etc. still execute every frame. */

    /* 1. Clear per-frame collision state */
    state->flipper_collision = 0;
    state->collision_force_amplification = 0;

    /* 2. Apply gravity */
    apply_gravity(state);

    /* 3. Clamp velocity */
    limit_velocity(state);

    /* ASM: is_ball_colliding cleared AFTER LimitBallVelocity (not before gravity) */
    state->is_ball_colliding = 0;

    /* 4. Handle tilt input */
    handle_tilts(state);

    /* 5. Handle flippers (only on bottom stages) */
    if (STAGE_HAS_FLIPPERS(state->current_stage)) {
        handle_flippers(state);
    }

    /* 6. Check stage tile collision.
     * ASM critical detail: if flipper was hit, its collision angle must override
     * the tile collision angle. Save flipper state before tile collision. */
    {
        uint8_t flipper_hit = state->flipper_collision;
        uint8_t flipper_angle = state->collision_normal_angle;

        check_stage_collision(state);

        /* Restore flipper angle if flipper was hit (ASM: flipper angle takes priority) */
        if (flipper_hit) {
            state->collision_normal_angle = flipper_angle;
        }
    }

    /* 7. Check game object collisions.
     * ASM: CheckGameObjectCollisions (0x2720) wrapper sets wTriggeredGameObject=$FF
     * before dispatch and copies to wPreviousTriggeredGameObject after CHECK but
     * before RESOLVE. Red/blue field handle init+copy internally in their check
     * functions. Diglett bonus handles it in check_diglett_bonus_object_collisions.
     *
     * Lua hook: on_object_collision replaces BOTH check and resolve for the stage.
     * The C check functions do bounding-box detection; the resolve functions handle
     * game logic responses. A Lua script handles both in one callback. */
    /* Split check/resolve: C always runs the check phase (sets which_* flags),
     * then Lua handles resolve if on_object_collision is defined, else C resolves.
     * For bonus stages, Lua replaces both check+resolve if the hook exists. */
    if (state->current_stage <= STAGE_RED_FIELD_BOTTOM) {
        check_red_field_object_collisions(state);
        if (state->script_engine && state->script_engine->has_on_object_collision) {
            script_call_on_object_collision(state->script_engine,
                                            state->current_stage,
                                            state->ball_x_pos, state->ball_y_pos);
        } else {
            resolve_red_field_object_collisions(state);
        }
    } else if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
        check_blue_field_object_collisions(state);
        if (state->script_engine && state->script_engine->has_on_object_collision) {
            script_call_on_object_collision(state->script_engine,
                                            state->current_stage,
                                            state->ball_x_pos, state->ball_y_pos);
        } else {
            resolve_blue_field_object_collisions(state);
        }
    } else if (state->current_stage == STAGE_GENGAR_BONUS) {
        if (state->script_engine && state->script_engine->has_on_object_collision) {
            script_call_on_object_collision(state->script_engine,
                                            state->current_stage,
                                            state->ball_x_pos, state->ball_y_pos);
        } else {
            check_gengar_bonus_object_collisions(state);
            resolve_gengar_bonus_object_collisions(state);
        }
    } else if (state->current_stage == STAGE_MEWTWO_BONUS) {
        if (state->script_engine && state->script_engine->has_on_object_collision) {
            script_call_on_object_collision(state->script_engine,
                                            state->current_stage,
                                            state->ball_x_pos, state->ball_y_pos);
        } else {
            check_mewtwo_bonus_object_collisions(state);
            resolve_mewtwo_bonus_object_collisions(state);
        }
    } else if (state->current_stage == STAGE_MEOWTH_BONUS) {
        if (state->script_engine && state->script_engine->has_on_object_collision) {
            script_call_on_object_collision(state->script_engine,
                                            state->current_stage,
                                            state->ball_x_pos, state->ball_y_pos);
        } else {
            check_meowth_bonus_object_collisions(state);
            resolve_meowth_bonus_object_collisions(state);
        }
    } else if (state->current_stage == STAGE_DIGLETT_BONUS) {
        if (state->script_engine && state->script_engine->has_on_object_collision) {
            script_call_on_object_collision(state->script_engine,
                                            state->current_stage,
                                            state->ball_x_pos, state->ball_y_pos);
        } else {
            check_diglett_bonus_object_collisions(state);
            resolve_diglett_bonus_object_collisions(state);
        }
    } else if (state->current_stage == STAGE_SEEL_BONUS) {
        if (state->script_engine && state->script_engine->has_on_object_collision) {
            script_call_on_object_collision(state->script_engine,
                                            state->current_stage,
                                            state->ball_x_pos, state->ball_y_pos);
        } else {
            check_seel_bonus_object_collisions(state);
            resolve_seel_bonus_object_collisions(state);
        }
    }

    /* Check for in-game menu (ASM: after object collisions, before collision response) */
    if (joypad_is_key_pressed(state, &state->key_config_menu) && !state->in_game_menu_active) {
        open_pause_menu(state);
    }

    /* DEBUG: Press C to force catch'em mode on a main field stage */
    {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        static uint8_t debug_c_prev = 0;
        uint8_t debug_c_cur = keys[SDL_SCANCODE_C];
        if (debug_c_cur && !debug_c_prev) {
            if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
                start_catchem_mode(state);
            }
        }
        debug_c_prev = debug_c_cur;
    }

    /* DEBUG: Press V to force evolution mode on a main field stage */
    {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        static uint8_t debug_v_prev = 0;
        uint8_t debug_v_cur = keys[SDL_SCANCODE_V];
        if (debug_v_cur && !debug_v_prev) {
            if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
                /* Ensure at least one party mon (Charmander) so evolution can start */
                if (state->num_party_mons == 0) {
                    state->party_mons[0] = 4; /* Charmander */
                    state->num_party_mons = 1;
                }
                start_evolution_mode(state);
            }
        }
        debug_v_prev = debug_v_cur;
    }

    /* DEBUG: Press B to set 3 pokeballs (triggers slot open → billboard → bonus) */
    {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        static uint8_t debug_b_prev = 0;
        uint8_t debug_b_cur = keys[SDL_SCANCODE_B];
        if (debug_b_cur && !debug_b_prev) {
            if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM) {
                /* Set 3 pokeballs to trigger the natural pokeball→slot→billboard
                 * →bonus stage flow, rather than bypassing directly. */
                state->num_pokeballs = 3;
            }
        }
        debug_b_prev = debug_b_cur;
    }

    /* 8. Collision response */
    if (state->is_ball_colliding) {
        /* ApplyTiltForces */
        apply_tilt_forces(state);

        /* LoadBallVelocity → RotateVector → ApplyCollisionForces →
         * HandleFlipperForce → NegateAndRotate → SetBallVelocity */
        int16_t vx = state->ball_x_velocity;
        int16_t vy = state->ball_y_velocity;

        /* Rotate velocity into collision-normal-aligned coordinates */
        rotate_vector(&vx, &vy, state->collision_normal_angle);

        /* Apply damping, bounce, and spin transfer */
        apply_collision_forces(state, &vx, &vy);

        /* ASM: two separate paths after ApplyCollisionForces.
         * Path 1: flipper_collision set → apply forces → rotate back (always).
         * Path 2: no flipper → only rotate back if no_collision_applied is clear. */
        if (state->flipper_collision) {
            vy -= state->flipper_y_force;
            vx += state->flipper_x_force;
            negate_and_rotate_vector(&vx, &vy, state->collision_normal_angle);
            state->ball_x_velocity = vx;
            state->ball_y_velocity = vy;
        } else if (!state->no_collision_applied) {
            negate_and_rotate_vector(&vx, &vy, state->collision_normal_angle);
            state->ball_x_velocity = vx;
            state->ball_y_velocity = vy;
        }
    }

    /* 9. Move ball */
    move_ball_position(state);

    /* 10. Scroll screen + check stage transition (ASM CheckStageTransition
     *     calls ScrollScreenToShowPinball first, then checks for transition) */
    scroll_screen_to_ball(state);
    check_stage_transition(state);

    /* Ball loss check (ASM: CheckStageTransition .youLose when transition table = $FF).
     * check_stage_transition sets ball_lost_from_transition, then check_ball_lost handles it.
     * move_to_next_screen_state is set inside check_ball_lost (matching ASM order). */
    check_ball_lost(state);

    /* 11. Draw sprites */
    dispatch_draw_sprites(state);

    /* 12. ASM frame order (lines 152-166):
     *   UpdateBottomText              (always)
     *   if (!wDisableDrawScoreboardInfo):
     *     Func_85c7                   (process_score_queue)
     *     HideScoreIfBallLow          (adjust WY)
     *     Func_8645 + Func_dba9 +     (update_scoreboard — includes all three)
     *       DrawNumPartyMonsIcon +
     *       DrawPikachuSaverLightningBoltIcon
     *   DecrementTimer                (if active, always) */
    update_bottom_text(state);

    if (!state->disable_draw_scoreboard_info) {
        /* Func_85c7: drain one score queue entry every 4 frames */
        process_score_queue(state);

        /* HideScoreIfBallLow (0x8650): adjust WY based on ball Y on bottom stages.
         * ASM: INSIDE the disable_draw_scoreboard_info conditional. */
        if (!(state->current_stage & 1)) {
            /* Top stage: WY always $86 */
            if (!state->in_game_menu_active)
                state->hram.wy = 0x86;
        } else if (!state->in_game_menu_active) {
            /* Bottom stage: adjust based on ball position */
            uint8_t by = UFIXED_TO_INT(state->ball_y_pos);
            if (by >= 0x84) {
                uint8_t wy = state->hram.wy + 3;
                if (wy >= 0x90) wy = 0x90;
                state->hram.wy = wy;
            } else {
                uint8_t wy = state->hram.wy;
                if (wy >= 3 + 0x86) {
                    wy -= 3;
                } else {
                    wy = 0x86;
                }
                state->hram.wy = wy;
            }
        }

        /* Func_8645 + Func_dba9 + DrawNumPartyMonsIcon +
         * DrawPikachuSaverLightningBoltIcon (all in update_scoreboard) */
        update_scoreboard(state);
    }

    /* DecrementTimer (ASM line 164-166: called if wTimerActive != 0) */
    decrement_timer(state);

    /* Check for screen state advance */
    if (state->move_to_next_screen_state) {
        state->move_to_next_screen_state = 0;
        state->screen_state++;
    }
}

/*=============================================================================
 * EndOfBallBonus (0xf533): Multi-phase scoring screen
 *
 * Translated from engine/pinball_game/end_of_ball_bonus.asm.
 * ASM uses blocking AdvanceFrame loops; C uses non-blocking state machine.
 * Shows categories with counting animation, multiplier, total, game over.
 *===========================================================================*/

/* EndOfBallBonus phases */
#define EOBB_INIT            0
#define EOBB_SHOW_CATEGORY   1
#define EOBB_COUNT_CATEGORY  2
#define EOBB_WAIT_CATEGORY   3
#define EOBB_SCROLL_TO_MULT  4
#define EOBB_SHOW_MULT       5
#define EOBB_MULT_COUNT      6
#define EOBB_WAIT_MULT       7
#define EOBB_SCROLL_TO_SCORE 8
#define EOBB_SHOW_SCORE      9
#define EOBB_ADD_TO_SCORE    10
#define EOBB_WAIT_SCORE      11
#define EOBB_WAIT_SCORE_2    12
#define EOBB_GAME_OVER       13
#define EOBB_GAME_OVER_WAIT  14
#define EOBB_CLEANUP         15

/* BCD point values per category (6-byte little-endian BCD) */
static const uint8_t BCD_50000[6] = {0x00, 0x00, 0x05, 0x00, 0x00, 0x00}; /* Pokemon caught */
static const uint8_t BCD_75000[6] = {0x00, 0x50, 0x07, 0x00, 0x00, 0x00}; /* Pokemon evolved */
static const uint8_t BCD_7500[6]  = {0x00, 0x75, 0x00, 0x00, 0x00, 0x00}; /* Bellsprout/Cloyster/Slowpoke */
static const uint8_t BCD_5000[6]  = {0x00, 0x50, 0x00, 0x00, 0x00, 0x00}; /* Dugtrio/Psyduck/Poliwag */
static const uint8_t BCD_2500[6]  = {0x00, 0x25, 0x00, 0x00, 0x00, 0x00}; /* CAVE completions */
static const uint8_t BCD_1000[6]  = {0x00, 0x10, 0x00, 0x00, 0x00, 0x00}; /* Spinner turns */

/* Number of bonus categories per field */
#define NUM_RED_BONUS_CATEGORIES  6
#define NUM_BLUE_BONUS_CATEGORIES 8

/* PlaceTextAlphanumericOnly equivalent: convert string to tile indices */
static void bonus_place_text(uint8_t *buf, uint16_t offset, const char *text) {
    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        uint8_t tile;
        if (c >= 'A' && c <= 'Z')      tile = (uint8_t)(c + 0xBF);
        else if (c >= '0' && c <= '9')  tile = (uint8_t)(0x86 + (c - '0'));
        else if (c == 'e')              tile = 0x83;  /* é (acute e) */
        else                            tile = 0x81;  /* space */
        if (offset + i < 256) buf[offset + i] = tile;
    }
}

/* Func_f78e: Convert byte value to 3 decimal digit tiles, with leading zero suppression */
static void bonus_place_count(uint8_t *buf, uint16_t offset, uint8_t value) {
    uint8_t h = value / 100;
    uint8_t t = (value / 10) % 10;
    uint8_t o = value % 10;
    bool leading = true;
    if (h > 0) { buf[offset] = 0x86 + h; leading = false; }
    offset++;
    if (t > 0 || !leading) { buf[offset] = 0x86 + t; }
    offset++;
    buf[offset] = 0x86 + o;  /* always show ones digit */
}

/* Func_f8bd: Convert 6-byte BCD to tile display with commas.
 * Writes 12 digit tile positions starting at text_offset, with leading zero suppression.
 * Commas placed one row below (+$20) at positions b=12, 9, 6, 3. */
static void bonus_display_bcd6(uint8_t *buf, const uint8_t bcd[6], uint16_t text_offset) {
    int out = text_offset;
    bool leading = true;
    int b = 12;

    for (int byte_idx = 5; byte_idx >= 0; byte_idx--) {
        uint8_t val = bcd[byte_idx];

        /* High nibble (ASM: swap a then AND $F) */
        uint8_t nibble = (val >> 4) & 0xF;
        if (nibble != 0 || (b == 1) || !leading) {
            if (out < 256) buf[out] = 0x86 + nibble;
            leading = false;
            if (b == 12 || b == 9 || b == 6 || b == 3) {
                if (out + 0x20 < 256) buf[out + 0x20] = 0x82;
            }
        }
        out++;
        b--;

        /* Low nibble */
        nibble = val & 0xF;
        if (nibble != 0 || (b == 1) || !leading) {
            if (out < 256) buf[out] = 0x86 + nibble;
            leading = false;
            if (b == 12 || b == 9 || b == 6 || b == 3) {
                if (out + 0x20 < 256) buf[out + 0x20] = 0x82;
            }
        }
        out++;
        b--;
    }

    /* ASM Func_f8bd: append a "0" tile ($86) after the score digits */
    if (out < 256) buf[out] = 0x86;
}

/* Func_f80d: Copy from bottom_message_text to window VRAM */
static void bonus_copy_to_vram(GameState *state, uint16_t offset, uint16_t count) {
    if (!state->vram) return;
    for (uint16_t i = 0; i < count && (offset + i) < 256; i++) {
        state->vram->win_map[0][offset + i] = state->bottom_message_text[offset + i];
        state->vram->win_map[1][offset + i] = 0x00;
    }
}

/* Get category info for red field */
static void get_red_category(GameState *state, int cat_idx,
                             const char **text, uint8_t *text_offset,
                             uint8_t *count, const uint8_t **points) {
    switch (cat_idx) {
    case 0: *text = "  0 POKeMON CAUGHT";  *text_offset = 0x01;
            *count = state->num_pokemon_caught_in_ball_bonus; *points = BCD_50000; break;
    case 1: *text = "  0 POKeMON EVOLVED"; *text_offset = 0x00;
            *count = state->num_pokemon_evolved_in_ball_bonus; *points = BCD_75000; break;
    case 2: *text = "  0 BELLSPROUT";      *text_offset = 0x03;
            *count = state->num_bellsprout_entries; *points = BCD_7500; break;
    case 3: *text = "  0 DUGTRIO";         *text_offset = 0x04;
            *count = state->num_dugtrio_triples; *points = BCD_5000; break;
    case 4: *text = "  0 CAVE SHOTS";      *text_offset = 0x03;
            *count = state->num_cave_completions; *points = BCD_2500; break;
    case 5: *text = "  0 SPINNER TURNS";   *text_offset = 0x01;
            *count = state->num_spinner_turns; *points = BCD_1000; break;
    default: *text = ""; *text_offset = 0; *count = 0; *points = BCD_1000; break;
    }
}

/* Get category info for blue field (8 categories from ASM EndOfBallBonus_BlueField) */
static void get_blue_category(GameState *state, int cat_idx,
                              const char **text, uint8_t *text_offset,
                              uint8_t *count, const uint8_t **points) {
    switch (cat_idx) {
    case 0: *text = "  0 POKeMON CAUGHT";  *text_offset = 0x01;
            *count = state->num_pokemon_caught_in_ball_bonus; *points = BCD_50000; break;
    case 1: *text = "  0 POKeMON EVOLVED"; *text_offset = 0x00;
            *count = state->num_pokemon_evolved_in_ball_bonus; *points = BCD_75000; break;
    case 2: *text = "  0 CLOYSTER";        *text_offset = 0x04;
            *count = state->num_cloyster_entries; *points = BCD_7500; break;
    case 3: *text = "  0 SLOWPOKE";        *text_offset = 0x04;
            *count = state->num_slowpoke_entries; *points = BCD_7500; break;
    case 4: *text = "  0 POLIWAG";         *text_offset = 0x04;
            *count = state->num_poliwag_triples; *points = BCD_5000; break;
    case 5: *text = "  0 PSYDUCK";         *text_offset = 0x04;
            *count = state->num_psyduck_triples; *points = BCD_5000; break;
    case 6: *text = "  0 CAVE SHOTS";      *text_offset = 0x03;
            *count = state->num_cave_completions; *points = BCD_2500; break;
    case 7: *text = "  0 SPINNER TURNS";   *text_offset = 0x01;
            *count = state->num_spinner_turns; *points = BCD_1000; break;
    default: *text = ""; *text_offset = 0; *count = 0; *points = BCD_1000; break;
    }
}

/* Get number of bonus categories for current field */
static int get_num_bonus_categories(GameState *state) {
    if (state->current_stage <= STAGE_RED_FIELD_BOTTOM)
        return NUM_RED_BONUS_CATEGORIES;
    if (state->current_stage <= STAGE_BLUE_FIELD_BOTTOM)
        return NUM_BLUE_BONUS_CATEGORIES;
    return 0;
}

/*
 * Process EndOfBallBonus: non-blocking state machine.
 * Returns true when complete, false while still running.
 * Called each frame from pinball_handle_ball_loss.
 */
static bool process_end_of_ball_bonus(GameState *state) {
    uint8_t *buf = state->bottom_message_text;

    switch (state->end_of_ball_bonus_state) {
    case EOBB_INIT:
        /* ASM end_of_ball_bonus.asm line 3: load é tile before display */
        load_e_acute_character_gfx(state);
        /* Func_f57f: clear draw_bottom_message_box, fill buffer, copy to VRAM */
        state->draw_bottom_message_box = 0;
        memset(buf, 0x81, 256);
        /* Place permanent labels: " BONUS" at row 2, " SUBTOTAL" at row 4 */
        bonus_place_text(buf, 0x40, " BONUS");
        bonus_place_text(buf, 0x80, " SUBTOTAL");
        /* Clear BCD buffers */
        memset(state->end_of_ball_bonus_category_score, 0, 6);
        memset(state->end_of_ball_bonus_subtotal, 0, 6);
        memset(state->end_of_ball_bonus_total_score, 0, 6);
        /* Set WY=$60 to show 6-row window, LYC=$5F, LCDC mask $FD (disable OBJ below LYC) */
        state->hram.wy = 0x60;
        state->hram.lyc = 0x5F;
        state->hram.lcdc_mask = 0xFD;
        /* Copy all 6 rows to VRAM */
        bonus_copy_to_vram(state, 0x00, 0xC0);
        /* Start first category */
        state->end_of_ball_bonus_step = 0;
        state->ball_bonus_wait_for_button_press = 1;
        state->end_of_ball_bonus_state = EOBB_SHOW_CATEGORY;
        return false;

    case EOBB_SHOW_CATEGORY: {
        int cat = state->end_of_ball_bonus_step;
        int num_cats = get_num_bonus_categories(state);
        if (cat >= num_cats) {
            /* All categories done, scroll to multiplier */
            state->end_of_ball_bonus_step = 0;
            state->end_of_ball_bonus_state = EOBB_SCROLL_TO_MULT;
            return false;
        }
        const char *text; uint8_t text_off, count; const uint8_t *points;
        if (state->current_stage <= STAGE_RED_FIELD_BOTTOM)
            get_red_category(state, cat, &text, &text_off, &count, &points);
        else
            get_blue_category(state, cat, &text, &text_off, &count, &points);
        /* Place category text */
        bonus_place_text(buf, text_off, text);
        /* Place count number (overwrites "  0" placeholder) */
        bonus_place_count(buf, text_off, count);
        /* Copy row 0 to VRAM */
        bonus_copy_to_vram(state, 0x00, 0x40);
        /* Clear category score for counting */
        memset(state->end_of_ball_bonus_category_score, 0, 6);
        state->end_of_ball_bonus_count = count;
        state->end_of_ball_bonus_state = EOBB_COUNT_CATEGORY;
        return false;
    }

    case EOBB_COUNT_CATEGORY: {
        /* ASM Func_f853: display score FIRST (starting from 0), then check/decrement.
         * M05: C was checking/decrementing first, skipping the initial 0 display. */
        /* Display running category score at text+$46.
         * ASM Func_f853 does NOT clear the buffer before writing — Func_f8bd
         * skips leading-zero positions, preserving existing content. */
        bonus_display_bcd6(buf, state->end_of_ball_bonus_category_score, 0x46);
        bonus_copy_to_vram(state, 0x40, 0x40);
        /* Play counting SFX */
        PLAY_SFX(state, "score_tally", 0x00, 0x3E);
        /* Check A button to skip waits */
        if (state->ball_bonus_wait_for_button_press &&
            (state->hram.newly_pressed_buttons & BTN_A)) {
            state->ball_bonus_wait_for_button_press = 0;
        }
        /* Now check if done (after display, matching ASM) */
        if (state->end_of_ball_bonus_count == 0) {
            /* Done counting: add category to subtotal, display subtotal */
            bcd6_add(state->end_of_ball_bonus_subtotal,
                     state->end_of_ball_bonus_category_score);
            /* Display subtotal at $86. ASM .asm_f899 does NOT clear before
             * writing — Func_f8bd skips leading-zero positions, so "TAL"
             * from "SUBTOTAL" at $86-$88 is preserved. */
            bonus_display_bcd6(buf, state->end_of_ball_bonus_subtotal, 0x86);
            bonus_copy_to_vram(state, 0x80, 0x40);
            state->end_of_ball_bonus_timer = 0;
            state->end_of_ball_bonus_state = EOBB_WAIT_CATEGORY;
            return false;
        }
        /* Prepare next frame: decrement count and add points */
        state->end_of_ball_bonus_count--;
        int cat = state->end_of_ball_bonus_step;
        const char *text; uint8_t text_off, count; const uint8_t *points;
        if (state->current_stage <= STAGE_RED_FIELD_BOTTOM)
            get_red_category(state, cat, &text, &text_off, &count, &points);
        else
            get_blue_category(state, cat, &text, &text_off, &count, &points);
        bcd6_add(state->end_of_ball_bonus_category_score, points);
        return false;
    }

    case EOBB_WAIT_CATEGORY:
        /* Func_f83a: wait $46 (70) frames or until A.
         * Func_f824: then clear category text rows. */
        if (state->ball_bonus_wait_for_button_press) {
            if (state->hram.newly_pressed_buttons & BTN_A) {
                state->ball_bonus_wait_for_button_press = 0;
            } else {
                state->end_of_ball_bonus_timer++;
                if (state->end_of_ball_bonus_timer < 70)
                    return false;
            }
        }
        /* Clear row 0 and parts of row 2 (Func_f824) */
        for (int i = 0x00; i < 0x40; i++) buf[i] = 0x81;
        for (int i = 0x48; i < 0x80; i++) buf[i] = 0x81;
        bonus_copy_to_vram(state, 0x00, 0xC0);
        /* Advance to next category */
        state->end_of_ball_bonus_step++;
        state->end_of_ball_bonus_state = EOBB_SHOW_CATEGORY;
        return false;

    case EOBB_SCROLL_TO_MULT:
        /* Func_f676: scroll up 4 times (one per frame) */
        if (state->end_of_ball_bonus_step < 4) {
            memmove(buf, buf + 0x20, 0xE0);
            memset(buf + 0xC0, 0x81, 0x20);
            bonus_copy_to_vram(state, 0x00, 0xC0);
            /* Check A during scroll */
            if (state->ball_bonus_wait_for_button_press &&
                (state->hram.newly_pressed_buttons & BTN_A)) {
                state->ball_bonus_wait_for_button_press = 0;
            }
            state->end_of_ball_bonus_step++;
            return false;
        }
        state->end_of_ball_bonus_state = EOBB_SHOW_MULT;
        return false;

    case EOBB_SHOW_MULT:
        /* Place " MULTIPLIER" at row 2, " TOTAL" at row 4 */
        bonus_place_text(buf, 0x40, " MULTIPLIER");
        bonus_place_text(buf, 0x80, " TOTAL");
        /* Show multiplier value at text+$50 */
        bonus_place_count(buf, 0x50, state->cur_bonus_multiplier);
        bonus_copy_to_vram(state, 0x40, 0x80);
        /* Reset wait flag for multiplier phase */
        state->ball_bonus_wait_for_button_press = 1;
        state->end_of_ball_bonus_count = state->cur_bonus_multiplier;
        state->end_of_ball_bonus_state = EOBB_MULT_COUNT;
        return false;

    case EOBB_MULT_COUNT:
        /* ASM Func_f676: display total BEFORE each addition.
         * M06: C was adding first then displaying, skipping initial 0 display. */
        /* Display total at text+$86. No clearing — matches ASM .asm_f6c7. */
        bonus_display_bcd6(buf, state->end_of_ball_bonus_total_score, 0x86);
        bonus_copy_to_vram(state, 0x80, 0x40);
        /* Play SFX */
        PLAY_SFX(state, "score_tally", 0x00, 0x3E);
        /* Check A */
        if (state->ball_bonus_wait_for_button_press &&
            (state->hram.newly_pressed_buttons & BTN_A)) {
            state->ball_bonus_wait_for_button_press = 0;
        }
        /* Now check if done (after display, matching ASM) */
        if (state->end_of_ball_bonus_count == 0) {
            state->end_of_ball_bonus_timer = 0;
            state->end_of_ball_bonus_state = EOBB_WAIT_MULT;
            return false;
        }
        /* Prepare next frame: decrement and add subtotal */
        state->end_of_ball_bonus_count--;
        state->cur_bonus_multiplier = state->end_of_ball_bonus_count;
        bcd6_add(state->end_of_ball_bonus_total_score,
                 state->end_of_ball_bonus_subtotal);
        return false;

    case EOBB_WAIT_MULT:
        if (state->ball_bonus_wait_for_button_press) {
            if (state->hram.newly_pressed_buttons & BTN_A) {
                state->ball_bonus_wait_for_button_press = 0;
            } else {
                state->end_of_ball_bonus_timer++;
                if (state->end_of_ball_bonus_timer < 70)
                    return false;
            }
        }
        /* Scroll to score phase */
        state->end_of_ball_bonus_step = 0;
        state->ball_bonus_wait_for_button_press = 1;
        state->end_of_ball_bonus_state = EOBB_SCROLL_TO_SCORE;
        return false;

    case EOBB_SCROLL_TO_SCORE:
        /* Scroll up 4 times (Func_f70d) */
        if (state->end_of_ball_bonus_step < 4) {
            memmove(buf, buf + 0x20, 0xE0);
            memset(buf + 0xC0, 0x81, 0x20);
            bonus_copy_to_vram(state, 0x00, 0xC0);
            if (state->ball_bonus_wait_for_button_press &&
                (state->hram.newly_pressed_buttons & BTN_A)) {
                state->ball_bonus_wait_for_button_press = 0;
            }
            state->end_of_ball_bonus_step++;
            return false;
        }
        state->end_of_ball_bonus_state = EOBB_SHOW_SCORE;
        return false;

    case EOBB_SHOW_SCORE:
        /* Place " SCORE" at row 3 + display current score */
        bonus_place_text(buf, 0x60, " SCORE");
        for (int i = 0x66; i < 0x80; i++) buf[i] = 0x81;
        for (int i = 0x80; i < 0xA0; i++) buf[i] = 0x81;
        bonus_display_bcd6(buf, state->score, 0x66);
        bonus_copy_to_vram(state, 0x60, 0x40);
        PLAY_SFX(state, "score_tally", 0x00, 0x3E);
        /* Check A */
        if (state->ball_bonus_wait_for_button_press &&
            (state->hram.newly_pressed_buttons & BTN_A)) {
            state->ball_bonus_wait_for_button_press = 0;
        }
        state->end_of_ball_bonus_state = EOBB_ADD_TO_SCORE;
        return false;

    case EOBB_ADD_TO_SCORE:
        /* Add total bonus to score */
        bcd6_add(state->score, state->end_of_ball_bonus_total_score);
        state->score_changed = 1;
        /* Redisplay score */
        for (int i = 0x66; i < 0x80; i++) buf[i] = 0x81;
        for (int i = 0x80; i < 0xA0; i++) buf[i] = 0x81;
        bonus_display_bcd6(buf, state->score, 0x66);
        bonus_copy_to_vram(state, 0x60, 0x40);
        state->end_of_ball_bonus_timer = 0;
        state->end_of_ball_bonus_state = EOBB_WAIT_SCORE;
        return false;

    case EOBB_WAIT_SCORE:
        /* First wait (Func_f83a) */
        if (state->ball_bonus_wait_for_button_press) {
            if (state->hram.newly_pressed_buttons & BTN_A) {
                state->ball_bonus_wait_for_button_press = 0;
            } else {
                state->end_of_ball_bonus_timer++;
                if (state->end_of_ball_bonus_timer < 70)
                    return false;
            }
        }
        state->end_of_ball_bonus_timer = 0;
        state->end_of_ball_bonus_state = EOBB_WAIT_SCORE_2;
        return false;

    case EOBB_WAIT_SCORE_2:
        /* Second wait (ASM calls Func_f83a twice) */
        if (state->ball_bonus_wait_for_button_press) {
            if (state->hram.newly_pressed_buttons & BTN_A) {
                state->ball_bonus_wait_for_button_press = 0;
            } else {
                state->end_of_ball_bonus_timer++;
                if (state->end_of_ball_bonus_timer < 70)
                    return false;
            }
        }
        /* Check game over */
        if (state->game_over[0]) {
            state->end_of_ball_bonus_state = EOBB_GAME_OVER;
        } else {
            state->end_of_ball_bonus_state = EOBB_CLEANUP;
        }
        return false;

    case EOBB_GAME_OVER:
        /* Play game over music: Bank(Music_GameOver)=0x10, MUSIC_GAME_OVER=0x05 */
        PLAY_MUSIC(state, "game_over", 0x10, 0x05);
        /* Clear and place "GAME OVER" text */
        memset(buf, 0x81, 0x40);
        bonus_place_text(buf, 0x20, "     GAME  OVER     ");
        bonus_copy_to_vram(state, 0x00, 0x40);
        state->end_of_ball_bonus_state = EOBB_GAME_OVER_WAIT;
        return false;

    case EOBB_GAME_OVER_WAIT:
        /* Wait for A press */
        if (state->hram.newly_pressed_buttons & BTN_A) {
            state->end_of_ball_bonus_state = EOBB_CLEANUP;
        }
        return false;

    case EOBB_CLEANUP:
        /* Restore window state (ASM: hWY=$90, hLYC=$83, hLastLYC=$83, hLCDCMask=$FF) */
        state->hram.wy = 0x90;
        state->hram.lyc = 0x83;
        state->hram.last_lyc = 0x83;
        state->hram.lcdc_mask = 0xFF;
        fill_bottom_message_buffer_with_black_tile(state);
        return true;  /* Done! */

    default:
        return true;
    }
}

static void pinball_handle_ball_loss(GameState *state) {
    /*
     * GameScreenFunction_HandleBallLoss (0xDA36):
     * Clear input, disable ball, animate tilts/flippers, wait for text.
     * When text finishes, call EndOfBallBonus, then check extra balls.
     * Translated from pinball_game.asm lines 178-224.
     */

    /* Always clear collision state */
    state->flipper_collision = 0;
    state->collision_force_amplification = 0;
    state->is_ball_colliding = 0;
    state->pinball_is_visible = 0;
    state->enable_ball_gravity_and_tilt = 0;

    /* 30-frame delay before playing ball loss SFX (ASM: AdvanceFrames $001E).
     * Music already stopped in check_ball_lost. */
    if (state->ball_loss_sfx_delay > 0) {
        state->ball_loss_sfx_delay--;
        if (state->ball_loss_sfx_delay == 0) {
            PLAY_SFX(state, "ball_loss", 0x25, 0x24);  /* Ball loss SFX */
        }
    }

    /* If EndOfBallBonus is active: process it each frame.
     * Don't clear input (need A button), don't run tilts/flippers/text
     * (ASM blocks in EndOfBallBonus with rst AdvanceFrame). */
    if (state->end_of_ball_bonus_active) {
        process_score_queue(state);
        if (!process_end_of_ball_bonus(state)) {
            return;  /* Still running */
        }
        /* EndOfBallBonus done */
        state->end_of_ball_bonus_active = 0;

        /* Post-bonus: check extra ball (ASM 0xdaa0) */
        if (state->extra_ball_state == 1) {
            state->extra_ball_state = 2;
            state->draw_bottom_message_box = 2;  /* ASM: ld a, $2 */
            fill_bottom_message_buffer_with_black_tile(state);
            enable_bottom_text(state);
            load_scrolling_text(state, 2, SHOOT_AGAIN_HEADER, "SHOOT AGAIN");
            return;  /* Stay in this state while text scrolls */
        }

        /* All done, advance to EndBall */
        state->extra_ball_state = 0;
        state->screen_state++;
        return;
    }

    /* Normal ball loss frame: clear input, run tilts/flippers for animation */
    state->hram.joypad_state = 0;
    state->hram.newly_pressed_buttons = 0;
    state->hram.pressed_buttons = 0;

    handle_tilts(state);
    if (STAGE_HAS_FLIPPERS(state->current_stage)) {
        handle_flippers(state);
    }

    /* Draw sprites (ASM calls DrawSpritesForStage here) */
    dispatch_draw_sprites(state);

    /* Update bottom text + process score queue (ASM calls both) */
    update_bottom_text(state);
    process_score_queue(state);

    /* Wait for bottom text to finish scrolling */
    if (state->bottom_text_enabled) return;

    printf("[BALL_LOSS] Text done. stage=0x%02X lost_ball=%d extra_ball=%d going_bonus=%d\n",
        state->current_stage, state->lost_ball, state->extra_ball_state, state->going_to_bonus_stage);
    fflush(stdout);

    /* Bonus stages: skip EndOfBallBonus, go straight to EndBall */
    if (state->current_stage >= FIRST_BONUS_STAGE) {
        state->screen_state++;
        return;
    }

    /* ASM: check wLostBall and wExtraBallState (0xda73-0xdaa8) */
    if (state->lost_ball && state->extra_ball_state != 2) {
        /* Start EndOfBallBonus (ASM: blocking call at 0xda7a).
         * Lock draw_bottom_message_box to 0 immediately to prevent flicker
         * during the frame before EOBB_INIT processes. */
        state->draw_bottom_message_box = 0;
        state->end_of_ball_bonus_active = 1;
        state->end_of_ball_bonus_state = EOBB_INIT;
        state->ball_bonus_wait_for_button_press = 1;
        return;
    }

    /* All text done, advance to EndBall state */
    state->extra_ball_state = 0;
    state->screen_state++;
}

static void pinball_end_ball(GameState *state) {
    /*
     * GameScreenFunction_EndBall (0xDAB2):
     * Check for game over, bonus stage transitions, or restart ball.
     * Life progression is handled in HandleBallLossRedField (check_ball_lost).
     */
    state->rumble_pattern = 0;

    if (state->game_over[0]) {
        /* TransitionToHighScoresScreen (ASM 0xdaea):
         * Clear game_over, stop music, map stage to high score stage ID. */
        memset(state->game_over, 0, sizeof(state->game_over));
        state->draw_bottom_message_box = 0;
        audio_stop_all(state->audio);  /* Stop music */
        /* ASM HighScoresStageMapping table (0xdb99):
         * Red (0-3) → 0, Blue (4-5) → 1, ALL bonus stages (6+) → 0. */
        {
            static const uint8_t HighScoresStageMapping[16] = {
                0, 0, 0, 0,  /* Red field (stages 0-3) */
                1, 1,        /* Blue field (stages 4-5) */
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* All bonus stages → Red */
            };
            uint8_t idx = state->current_stage;
            state->high_scores_stage = (idx < 16) ? HighScoresStageMapping[idx] : 0;
        }
        state->gfx_loaded = 0;
        free_flipper_collision_data();
        billboard_free_cache();
        state->current_screen = SCREEN_HIGH_SCORES;
        state->screen_state = 0;
        return;
    }

    if (state->going_to_bonus_stage) {
        printf("[END_BALL] Going to bonus! cur_stage=0x%02X next_stage=0x%02X\n",
            state->current_stage, state->next_stage);
        fflush(stdout);
        /* ASM lines 251-276: backup stage, set bonus stage, stop music */
        state->current_stage_backup = state->current_stage;
        state->stage_collision_state_backup = state->stage_collision_state;
        state->current_stage = state->next_stage;
        /* ASM: xor a / ld [wReturningFromBonusStage], a / ld [wGoingToBonusStage], a */
        state->returning_from_bonus_stage = 0;
        state->going_to_bonus_stage = 0;
        audio_stop_all(state->audio);  /* Stop music */
        state->gfx_loaded = 0;
        state->screen_state = 0;  /* LoadGFX for bonus stage */
        printf("[END_BALL] Bonus transition complete. screen_state=0, stage=0x%02X\n",
            state->current_stage);
        fflush(stdout);
        return;
    }

    if (state->returning_from_bonus_stage) {
        /* ASM lines 278-298: restore stage from backup, go to StartBall.
         * Do NOT clear returning_from_bonus_stage — StartBall needs it. */
        state->current_stage = state->current_stage_backup;
        state->stage_collision_state = state->stage_collision_state_backup;
        audio_stop_all(state->audio);  /* Stop bonus stage music before returning */
        state->gfx_loaded = 0;
        state->screen_state = 1;  /* ASM: wScreenState = 1 (StartBall, skip LoadGFX) */
        return;
    }

    /* Normal end of ball: go to StartBall (skip LoadGFX — graphics already loaded).
     * ASM: FadeOut then wScreenState = 1. */
    state->screen_state = 1;
}

void handle_pinball_game(GameState *state) {
    /*
     * HandlePinballGame (0xD853):
     * Dispatch via wScreenState jump table.
     */
    /* DEBUG: log state transitions */
    static uint8_t prev_screen_state = 0xFF;
    if (state->screen_state != prev_screen_state) {
        printf("[PINBALL] screen_state: %d -> %d (stage=0x%02X going_bonus=%d ret_bonus=%d lost=%d)\n",
            prev_screen_state, state->screen_state, state->current_stage,
            state->going_to_bonus_stage, state->returning_from_bonus_stage, state->lost_ball);
        fflush(stdout);
        prev_screen_state = state->screen_state;
    }

    switch (state->screen_state) {
        case 0: pinball_load_gfx(state); break;
        case 1: pinball_start_ball(state); break;
        case 2: pinball_handle_physics(state); break;
        case 3: pinball_handle_ball_loss(state); break;
        case 4: pinball_end_ball(state); break;
    }
}
