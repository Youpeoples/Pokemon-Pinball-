/*
 * Main Game Loop & Screen State Machine
 *
 * Translated from Main (home.asm 0x1FFC) and DoScreenLogic (0x2043).
 *
 * The game uses a two-level state machine:
 * - wCurrentScreen selects which screen handler runs (title, game, options, etc.)
 * - wScreenState is a sub-state within each screen handler
 *
 * Each frame: TickRumble → DoScreenLogic → CleanSprites → ClearPersistentJoypad
 */

#include "game/main_loop.h"
#include "game/joypad.h"
#include "game/pinball.h"
#include "game/red_field.h"
#include "game/save.h"
#include "game/sprite_data.h"
#include "game/pokedex_data.h"
#include "game/rng.h"
#include "game/config_data.h"
#include "audio/audio.h"
#include "renderer/stage_assets.h"
#include "renderer/stage_palettes.h"
#include "renderer/vram.h"
#include "renderer/vwf.h"
#include "renderer/tile_loader.h"
#include "stb_image.h"
#include "stb_image.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
/* Prevent windows.h from redefining RGB (conflicts with stage_palettes.h) */
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#endif

/* Music bank/ID constants (from constants/song_constants.asm) */
#define MUSIC_BANK_0F  0x0F
#define MUSIC_BANK_10  0x10
#define MUSIC_BANK_11  0x11
#define MUSIC_BANK_12  0x12
#define MUSIC_BANK_13  0x13

#define MUSIC_TITLE_SCREEN   0x04  /* Bank 0x11 */
#define MUSIC_OPTIONS        0x02  /* Bank 0x12 */
#define MUSIC_FIELD_SELECT   0x03  /* Bank 0x12 */
#define MUSIC_HI_SCORE       0x04  /* Bank 0x10 */
#define MUSIC_RED_FIELD      0x01  /* Bank 0x0F */
#define MUSIC_BLUE_FIELD     0x01  /* Bank 0x10 */
#define MUSIC_NOTHING        0x00

/* SFX IDs (from constants/sound_effect_constants.asm) */
#define SFX_CONFIRM    0x01  /* A/B button confirm, menu selection */
#define SFX_CURSOR     0x03  /* Cursor move (up/down navigation) */
#define SFX_FIELD_LEFT 0x3C  /* Field select cursor left */
#define SFX_FIELD_RIGHT 0x3D /* Field select cursor right */

/* SongBanks (0xc77e): Maps sound test BGM index to {bank, song_id}.
 * Used by sound test to look up which bank and song to play. */
static const struct { uint8_t bank; uint8_t id; } song_banks[NUM_SONGS] = {
    { 0x0F, 0x00 }, /*  0: MUSIC_NOTHING */
    { 0x0F, 0x01 }, /*  1: MUSIC_RED_FIELD */
    { 0x0F, 0x02 }, /*  2: MUSIC_CATCH_EM_RED */
    { 0x0F, 0x03 }, /*  3: MUSIC_HURRY_UP_RED */
    { 0x0F, 0x04 }, /*  4: MUSIC_POKEDEX */
    { 0x0F, 0x05 }, /*  5: MUSIC_GASTLY_GRAVEYARD */
    { 0x0F, 0x06 }, /*  6: MUSIC_HAUNTER_GRAVEYARD */
    { 0x0F, 0x07 }, /*  7: MUSIC_GENGAR_GRAVEYARD */
    { 0x10, 0x01 }, /*  8: MUSIC_BLUE_FIELD */
    { 0x10, 0x02 }, /*  9: MUSIC_CATCH_EM_BLUE */
    { 0x10, 0x03 }, /* 10: MUSIC_HURRY_UP_BLUE */
    { 0x10, 0x04 }, /* 11: MUSIC_HI_SCORE */
    { 0x10, 0x05 }, /* 12: MUSIC_GAME_OVER */
    { 0x11, 0x01 }, /* 13: MUSIC_WHACK_DIGLETT */
    { 0x11, 0x02 }, /* 14: MUSIC_WHACK_DUGTRIO */
    { 0x11, 0x03 }, /* 15: MUSIC_SEEL_STAGE */
    { 0x11, 0x04 }, /* 16: MUSIC_TITLE_SCREEN */
    { 0x12, 0x01 }, /* 17: MUSIC_MEWTWO_STAGE */
    { 0x12, 0x02 }, /* 18: MUSIC_OPTIONS */
    { 0x12, 0x03 }, /* 19: MUSIC_FIELD_SELECT */
    { 0x12, 0x04 }, /* 20: MUSIC_MEOWTH_STAGE */
    { 0x13, 0x01 }, /* 21: MUSIC_END_CREDITS */
    { 0x13, 0x02 }, /* 22: MUSIC_NAME_ENTRY */
};

static const char *screen_names[] = {
    "SELECT_GAMEBOY", "ERASE_DATA", "COPYRIGHT", "TITLESCREEN",
    "PINBALL_GAME", "POKEDEX", "OPTIONS", "HIGH_SCORES", "FIELD_SELECT"
};

static uint8_t prev_screen = 0xFF;
static uint8_t prev_state = 0xFF;

/* Forward declarations for screen handlers */
static void handle_erase_all_data(GameState *state);
static void handle_copyright_screen(GameState *state);
static void handle_titlescreen(GameState *state);
static void handle_pokedex_screen(GameState *state);
static void handle_options_screen(GameState *state);
static void handle_high_scores_screen(GameState *state);
static void handle_field_select_screen(GameState *state);

/*
 * TickRumbleDuration (0x2034):
 * Decrements rumble duration, turns off when it reaches 0.
 */
static void tick_rumble_duration(GameState *state) {
    if (state->rumble_duration > 0) {
        state->rumble_duration--;
    } else {
        state->rumble_pattern = 0;
    }
}

/*
 * FadeIn/FadeOut palette transitions.
 * Translated from home/palettes.asm FadeIn_GameboyColor (0xc19) and
 * FadeOut_GameboyColor (0xcee).
 *
 * FadeIn: fade_bg/obj_palettes start at white ($7FFF), each frame subtracts 2
 * from each RGB5 component, clamping to the target (bg/obj_palettes).
 * After 16 frames, faded palettes equal the targets.
 *
 * FadeOut: fade_bg/obj_palettes start as current palette values, each frame
 * adds 2 to each RGB5 component, clamping to 31 (white = $7FFF).
 * After 16 frames, all colors are white.
 *
 * The renderer uses fade_bg/obj_palettes when fade_counter > 0.
 */
static void update_palette_fade(GameState *state) {
    if (state->fade_counter == 0) return;

    if (state->fade_direction == 0) {
        /* Fade In: subtract 2 from each component of faded palette,
         * clamp to target (bg/obj_palettes) value. */
        for (int i = 0; i < 8; i++) {
            for (int c = 0; c < 4; c++) {
                uint16_t faded = state->fade_bg_palettes[i].colors[c];
                uint16_t target = state->bg_palettes[i].colors[c];
                int fr = (faded & 0x1F), fg = ((faded >> 5) & 0x1F), fb = ((faded >> 10) & 0x1F);
                int tr = (target & 0x1F), tg = ((target >> 5) & 0x1F), tb = ((target >> 10) & 0x1F);
                fr = (fr - 2 < tr) ? tr : fr - 2;
                fg = (fg - 2 < tg) ? tg : fg - 2;
                fb = (fb - 2 < tb) ? tb : fb - 2;
                state->fade_bg_palettes[i].colors[c] =
                    (uint16_t)(fr | (fg << 5) | (fb << 10));

                faded = state->fade_obj_palettes[i].colors[c];
                target = state->obj_palettes[i].colors[c];
                fr = (faded & 0x1F); fg = ((faded >> 5) & 0x1F); fb = ((faded >> 10) & 0x1F);
                tr = (target & 0x1F); tg = ((target >> 5) & 0x1F); tb = ((target >> 10) & 0x1F);
                fr = (fr - 2 < tr) ? tr : fr - 2;
                fg = (fg - 2 < tg) ? tg : fg - 2;
                fb = (fb - 2 < tb) ? tb : fb - 2;
                state->fade_obj_palettes[i].colors[c] =
                    (uint16_t)(fr | (fg << 5) | (fb << 10));
            }
        }
    } else {
        /* Fade Out: add 2 to each component of faded palette, clamp to 31. */
        for (int i = 0; i < 8; i++) {
            for (int c = 0; c < 4; c++) {
                uint16_t faded = state->fade_bg_palettes[i].colors[c];
                int fr = (faded & 0x1F), fg = ((faded >> 5) & 0x1F), fb = ((faded >> 10) & 0x1F);
                fr = (fr + 2 > 31) ? 31 : fr + 2;
                fg = (fg + 2 > 31) ? 31 : fg + 2;
                fb = (fb + 2 > 31) ? 31 : fb + 2;
                state->fade_bg_palettes[i].colors[c] =
                    (uint16_t)(fr | (fg << 5) | (fb << 10));

                faded = state->fade_obj_palettes[i].colors[c];
                fr = (faded & 0x1F); fg = ((faded >> 5) & 0x1F); fb = ((faded >> 10) & 0x1F);
                fr = (fr + 2 > 31) ? 31 : fr + 2;
                fg = (fg + 2 > 31) ? 31 : fg + 2;
                fb = (fb + 2 > 31) ? 31 : fb + 2;
                state->fade_obj_palettes[i].colors[c] =
                    (uint16_t)(fr | (fg << 5) | (fb << 10));
            }
        }
    }

    state->fade_counter--;
}

/* N5: Helper to initiate a 16-frame fade to white for screen transitions.
 * Copies current palettes to fade slots, then starts the fade-out. */
static void start_screen_fade_out(GameState *state) {
    for (int i = 0; i < 8; i++) {
        for (int c = 0; c < 4; c++) {
            state->fade_bg_palettes[i].colors[c] = state->bg_palettes[i].colors[c];
            state->fade_obj_palettes[i].colors[c] = state->obj_palettes[i].colors[c];
        }
    }
    state->fade_counter = 16;
    state->fade_direction = 1;
}

/*
 * CleanSpriteBuffer:
 * In the original, this cleans unused OAM entries.
 * We clear the sprite buffer count so the renderer knows how many to draw.
 */
static void clean_sprite_buffer(GameState *state) {
    /* CleanSpriteBuffer (home.asm:926):
     * Zero out unused sprite slots, then reset size for next frame. */
    for (int i = state->sprite_buffer_size; i < GBC_OAM_ENTRIES; i++) {
        state->sprite_buffer[i].y = 0;
        state->sprite_buffer[i].x = 0;
    }
    state->sprite_buffer_size = 0;
}

void main_loop_init(GameState *state) {
    /*
     * Main setup (0x1FFC):
     * Already handled by game_state_reset(), but we can do any
     * additional runtime initialization here.
     */
    (void)state;
}

void main_loop_update(GameState *state) {
    /*
     * .master_loop (home.asm):
     * call TickRumbleDuration
     * call DoScreenLogic
     * call CleanSpriteBuffer
     * call ClearPersistentJoypadStates
     * rst AdvanceFrame (wait for VBlank - handled by our fixed timestep)
     */
    tick_rumble_duration(state);
    update_palette_fade(state);
    do_screen_logic(state);
    clean_sprite_buffer(state);
    joypad_clear_persistent(state);
}

void do_screen_logic(GameState *state) {
    /*
     * DoScreenLogic (0x2043):
     * Dispatch to the appropriate screen handler based on wCurrentScreen.
     *
     * CallTable_2049:
     *   0 = HandleSelectGameboyTargetMenu (unreachable debug menu)
     *   1 = HandleEraseAllDataMenu
     *   2 = HandleCopyrightScreen
     *   3 = HandleTitlescreen
     *   4 = HandlePinballGame
     *   5 = HandlePokedexScreen
     *   6 = HandleOptionsScreen
     *   7 = HandleHighScoresScreen
     *   8 = HandleFieldSelectScreen
     */
    /* Log screen/state transitions to console */
    if (state->current_screen != prev_screen || state->screen_state != prev_state) {
        const char *name = (state->current_screen < 9) ? screen_names[state->current_screen] : "???";
        printf("[Screen] %s (state %d)\n", name, state->screen_state);
        prev_screen = state->current_screen;
        prev_state = state->screen_state;
    }

    switch (state->current_screen) {
        case SCREEN_SELECT_GAMEBOY_TARGET:
            /* Debug menu - skip straight to erase data check */
            state->current_screen = SCREEN_ERASE_ALL_DATA;
            break;
        case SCREEN_ERASE_ALL_DATA:
            handle_erase_all_data(state);
            break;
        case SCREEN_COPYRIGHT:
            handle_copyright_screen(state);
            break;
        case SCREEN_TITLESCREEN:
            handle_titlescreen(state);
            break;
        case SCREEN_PINBALL_GAME:
            handle_pinball_game(state);
            break;
        case SCREEN_POKEDEX:
            handle_pokedex_screen(state);
            break;
        case SCREEN_OPTIONS:
            handle_options_screen(state);
            break;
        case SCREEN_HIGH_SCORES:
            handle_high_scores_screen(state);
            break;
        case SCREEN_FIELD_SELECT:
            handle_field_select_screen(state);
            break;
    }
}

/*=============================================================================
 * Screen Handlers
 *===========================================================================*/

static void handle_erase_all_data(GameState *state) {
    /*
     * HandleEraseAllDataMenu (0x815d):
     * 3-state machine from engine/erase_all_data_menu.asm.
     *   State 0: CheckForResetButtonCombo — check if UP+RIGHT+START+SELECT held
     *   State 1: HandleEraseAllDataInput — wait for A (erase) or B (cancel)
     *   State 2: ExitEraseAllDataMenu — fade out and go to copyright
     */
    switch (state->screen_state) {
        case 0:
            /* CheckForResetButtonCombo (0x8167):
             * If the specific 4-button combo (UP+RIGHT+START+SELECT) is held,
             * load the erase confirmation UI. Otherwise, skip to copyright. */
            if (state->hram.joypad_state ==
                (BTN_UP | BTN_RIGHT | BTN_START | BTN_SELECT)) {
                /* Load erase all data graphics (ASM: EraseAllDataGfx_GameBoyColor) */
                if (state->vram && !state->gfx_loaded) {
                    memset(state->vram, 0, sizeof(*state->vram));
                    printf("Loading erase all data screen graphics...\n");
                    load_screen_assets(SCREEN_ERASE_ALL_DATA, state->vram,
                                       state, state->asset_base_path);
                    state->hram.lcdc = 0x41;
                    state->hram.scx = 0;
                    state->hram.scy = 0;
                    state->hram.stat_lcdc_xor = 0;
                    state->sprite_buffer_size = 0;

                    /* SetAllPalettesWhite then FadeIn.
                     * Set all palettes to white, then start 16-frame fade in. */
                    for (int i = 0; i < 8; i++) {
                        for (int c = 0; c < 4; c++) {
                            state->fade_bg_palettes[i].colors[c] = 0x7FFF;
                            state->fade_obj_palettes[i].colors[c] = 0x7FFF;
                        }
                    }
                    state->fade_counter = 16;
                    state->fade_direction = 0;  /* Fade in */
                    state->gfx_loaded = 1;
                }
                state->screen_state = 1;
            } else {
                /* Combo not held: skip straight to copyright screen */
                state->gfx_loaded = 0;
                state->current_screen = SCREEN_COPYRIGHT;
                state->screen_state = 0;
            }
            break;

        case 1:
            /* HandleEraseAllDataInput (0x81d4):
             * A button: erase all save data (clear SRAM equivalent).
             * B button: cancel and exit. */
            if (state->hram.newly_pressed_buttons & BTN_A) {
                /* Erase all save files (ASM: zeroes all of $A000-$BFFF SRAM) */
                remove("save_scores.dat");
                remove("save_pokedex.dat");
                remove("save_keyconfig.dat");
                remove("save_game.dat");

                /* Clear in-memory high scores, pokedex, and key config */
                memset(state->red_high_scores, 0, sizeof(state->red_high_scores));
                memset(state->blue_high_scores, 0, sizeof(state->blue_high_scores));
                memset(state->pokedex_flags, 0, sizeof(state->pokedex_flags));
                memset(&state->key_config_menu, 0, sizeof(state->key_config_menu));
                state->saved_game = 0;

                printf("All save data erased.\n");
                state->screen_state = 2;
            } else if (state->hram.newly_pressed_buttons & BTN_B) {
                /* Cancel: skip to exit */
                state->screen_state = 2;
            }
            break;

        case 2:
            /* ExitEraseAllDataMenu (0x820f):
             * FadeOut, then advance to copyright screen.
             * Use fade_direction==1 as indicator that fade-out has been initiated. */
            if (state->fade_direction != 1) {
                /* Initiate fade-out (first time entering state 2) */
                for (int i = 0; i < 8; i++) {
                    for (int c = 0; c < 4; c++) {
                        state->fade_bg_palettes[i].colors[c] =
                            state->bg_palettes[i].colors[c];
                        state->fade_obj_palettes[i].colors[c] =
                            state->obj_palettes[i].colors[c];
                    }
                }
                state->fade_counter = 16;
                state->fade_direction = 1;  /* Fade out */
                break;
            }
            /* Wait for fade-out to complete before transitioning */
            if (state->fade_counter > 0)
                break;

            state->gfx_loaded = 0;
            state->current_screen = SCREEN_COPYRIGHT;
            state->screen_state = 0;
            break;
    }
}

static void handle_copyright_screen(GameState *state) {
    /*
     * HandleCopyrightScreen (0x821e):
     *   State 0: FadeInCopyrightScreen — load gfx, 80-frame white delay, then fade in
     *   State 1: DisplayCopyrightScreen — show for 0x5A (90) frames, skippable after 0x2D (45)
     *   State 2: FadeOutCopyrightScreenAndLoadData — transition to title
     */
    static uint8_t copyright_frame_counter = 0;
    switch (state->screen_state) {
        case 0:
            /* FadeInCopyrightScreen (0x8228): load graphics + 80-frame delay */
            if (state->vram && !state->gfx_loaded) {
                memset(state->vram, 0, sizeof(*state->vram));
                printf("Loading copyright screen graphics...\n");
                load_screen_assets(SCREEN_COPYRIGHT, state->vram,
                                   state, state->asset_base_path);
                state->hram.lcdc = 0x41;
                state->gfx_loaded = 1;
            }
            /* N4: ASM has LCD disabled during loading, then 80-frame delay with
             * white palettes before FadeIn. Save target palettes, set to white. */
            memcpy(state->fade_bg_palettes, state->bg_palettes, sizeof(state->bg_palettes));
            memcpy(state->fade_obj_palettes, state->obj_palettes, sizeof(state->obj_palettes));
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 4; c++) {
                    state->bg_palettes[i].colors[c] = 0x7FFF;
                    state->obj_palettes[i].colors[c] = 0x7FFF;
                }
            }
            copyright_frame_counter = 80;
            state->screen_state = 1;
            break;
        case 1:
            /* 80-frame white-screen delay (ASM: AdvanceFrames before FadeIn) */
            if (copyright_frame_counter > 0) {
                copyright_frame_counter--;
                /* During delay, show white screen by keeping fade active */
                if (copyright_frame_counter == 0) {
                    /* N4: Restore target palettes (saved in fade slots during state 0),
                     * then set fade slots to white as the FadeIn source. */
                    GBCPalette saved_bg[8], saved_obj[8];
                    memcpy(saved_bg, state->fade_bg_palettes, sizeof(saved_bg));
                    memcpy(saved_obj, state->fade_obj_palettes, sizeof(saved_obj));
                    memcpy(state->bg_palettes, saved_bg, sizeof(saved_bg));
                    memcpy(state->obj_palettes, saved_obj, sizeof(saved_obj));
                    for (int i = 0; i < 8; i++) {
                        for (int c = 0; c < 4; c++) {
                            state->fade_bg_palettes[i].colors[c] = 0x7FFF;
                            state->fade_obj_palettes[i].colors[c] = 0x7FFF;
                        }
                    }
                    state->fade_counter = 16;
                    state->fade_direction = 0;  /* Fade in */
                }
                break;
            }
            /* FadeIn started, start display timer */
            copyright_frame_counter = 0x5A; /* 90 frames total display time */
            state->screen_state = 2;
            break;
        case 2:
            /* DisplayCopyrightScreen (0x8290): show for 0x5A frames,
             * skippable with A once counter < 0x2D (45 frames remaining) */
            if (copyright_frame_counter < 0x2D) {
                if (state->hram.newly_pressed_buttons & BTN_A) {
                    /* L7: Start fade-out before transition */
                    start_screen_fade_out(state);
                    state->screen_state = 3;
                    break;
                }
            }
            if (copyright_frame_counter > 0) {
                copyright_frame_counter--;
            } else {
                /* L7: Start fade-out before transition */
                start_screen_fade_out(state);
                state->screen_state = 3;
            }
            break;
        case 3:
            /* L7: FadeOut sub-state — wait for 16-frame fade to complete */
            if (state->fade_counter == 0) {
                /* FadeOutCopyrightScreenAndLoadData (0x82a8):
                 * Load all save blocks before advancing to title screen. */
                load_game(state);
                state->gfx_loaded = 0;
                state->current_screen = SCREEN_TITLESCREEN;
                state->screen_state = 0;
            }
            break;
    }
}

/*
 * Move cursor up/down within [0, max_pos].
 * Translated from Func_c1fc (0xc1fc) in titlescreen.asm.
 * Uses hPressedButtons (includes held-repeat) for navigation.
 */
static void move_menu_cursor(GameState *state, uint8_t *cursor, uint8_t max_pos) {
    uint8_t buttons = state->hram.pressed_buttons;
    if (buttons & BTN_UP) {
        if (*cursor > 0) {
            (*cursor)--;
        }
    } else if (buttons & BTN_DOWN) {
        if (*cursor < max_pos) {
            (*cursor)++;
        }
    }
}

/*
 * Data_c1e4: maps cursor position to screen constant.
 *   0 = Game Start  -> SCREEN_FIELD_SELECT
 *   1 = Pokedex     -> SCREEN_POKEDEX
 *   2 = Options     -> SCREEN_OPTIONS
 */
static const uint8_t titlescreen_cursor_to_screen[] = {
    SCREEN_FIELD_SELECT,
    SCREEN_POKEDEX,
    SCREEN_OPTIONS,
};

/*=============================================================================
 * Title Screen Animation Handlers
 * Translated from titlescreen.asm (0xc0f7, 0xc21e, 0xc278)
 *===========================================================================*/

/*
 * HandleTitlescreenPikachuBlinkingAnimation (0xc21e):
 * Overlay pikachu blinking eyes.  BLINK_0 (eyes open) is drawn by the BG
 * tilemap so no sprite overlay is needed; only BLINK_1/2 load sprites.
 */
static void handle_titlescreen_pikachu_blink(GameState *state) {
    uint8_t frame = state->title_screen_blink_anim_frame;
    const SpriteAnimFrame *entry = &titlescreen_blink_anim[frame];

    /* Load overlay sprite unless BLINK_0 (NULL = eyes open, no overlay) */
    if (entry->sprite_data != NULL) {
        /* lb bc, $38, $10 → offset_y=0x10 (c), offset_x=0x38 (b) */
        load_sprite_data(state, entry->sprite_data, 0x10, 0x38);
    }

    /* Decrement counter; if non-zero, keep current frame */
    uint8_t counter = state->title_screen_blink_anim_counter;
    counter--;
    if (counter != 0) {
        state->title_screen_blink_anim_counter = counter;
        return;
    }

    /* Counter reached 0: advance to next frame */
    if (titlescreen_blink_anim[frame + 1].duration != 0) {
        frame++;
    } else {
        frame = 0;  /* terminator reached, loop */
    }
    state->title_screen_blink_anim_frame = frame;

    /* Get new duration; add random jitter for long pauses (>= $3c) */
    uint8_t duration = titlescreen_blink_anim[frame].duration;
    if (duration >= 0x3c) {
        duration += gen_random(state) & 0x1f;
    }
    state->title_screen_blink_anim_counter = duration;
}

/*
 * HandleTitlescreenPokeballAnimation (0xc278):
 * Bouncing pokeball cursor next to selected menu item.
 * In screen_state != 1, always display frame 0 (static pokeball).
 * Animation counter runs regardless of screen_state.
 */
static void handle_titlescreen_pokeball_anim(GameState *state) {
    /* Position offset depends on cursor selection */
    uint8_t cursor = state->title_screen_cursor_selection;
    if (cursor > 2) cursor = 0;
    uint8_t offset_y = titlescreen_pokeball_offsets[cursor][0];
    uint8_t offset_x = titlescreen_pokeball_offsets[cursor][1];

    /* Display: use animated frame only in state 1, else static frame 0 */
    uint8_t display_frame = 0;
    if (state->screen_state == 1) {
        display_frame = state->title_screen_bouncing_ball_anim_frame;
    }
    load_sprite_data(state, titlescreen_pokeball_anim[display_frame].sprite_data,
                     offset_y, offset_x);

    /* Advance animation counter (runs even when not in state 1) */
    uint8_t counter = state->title_screen_pokeball_anim_counter;
    counter--;
    if (counter != 0) {
        state->title_screen_pokeball_anim_counter = counter;
        return;
    }

    /* Counter reached 0: advance to next frame */
    uint8_t frame = state->title_screen_bouncing_ball_anim_frame;
    if (titlescreen_pokeball_anim[frame + 1].duration != 0) {
        frame++;
    } else {
        frame = 0;  /* loop */
    }
    state->title_screen_bouncing_ball_anim_frame = frame;
    state->title_screen_pokeball_anim_counter = titlescreen_pokeball_anim[frame].duration;
}

/*
 * HandleTitlescreenAnimations (0xc0f7):
 * Called each frame during the title screen.
 */
static void handle_titlescreen_animations(GameState *state) {
    /* GBC blank sprite overlay (lb bc, $20, $40 → offset_y=$40, offset_x=$20) */
    load_sprite_data(state, sprite_titlescreen_blank, 0x40, 0x20);
    handle_titlescreen_pikachu_blink(state);
    handle_titlescreen_pokeball_anim(state);
}

/*=============================================================================
 * Field Select Border Animation
 * Translated from AnimateBlinkingFieldSelectBorder (field_select_screen.asm:152)
 *===========================================================================*/

/*
 * AnimateBlinkingFieldSelectBorder (0xd7fb):
 * Blinks the border around the selected field.
 * confirming=false uses the normal choosing animation (grey/white/grey/black).
 * confirming=true uses the fast confirmation animation (white/black/white/black).
 */
static void animate_field_select_border(GameState *state, bool confirming) {
    const SpriteAnimFrame *anim = confirming
        ? field_select_confirm_anim : field_select_border_anim;

    /* Pixel offsets based on selected field */
    uint8_t idx = state->selected_field_index;
    if (idx > 1) idx = 0;
    uint8_t offset_y = field_select_border_offsets[idx][0];
    uint8_t offset_x = field_select_border_offsets[idx][1];

    /* Load current step's border sprite */
    uint8_t step = state->field_select_border_anim_step;
    load_sprite_data(state, anim[step].sprite_data, offset_y, offset_x);

    /* Decrement frame counter */
    uint8_t frame = state->field_select_blinking_border_frame;
    frame--;
    if (frame != 0) {
        state->field_select_blinking_border_frame = frame;
        return;
    }

    /* Frame counter reached 0: advance to next step */
    if (anim[step + 1].duration != 0) {
        step++;
    } else {
        step = 0;  /* loop */
    }
    state->field_select_border_anim_step = step;
    state->field_select_blinking_border_frame = anim[step].duration;
}

/*
 * HandleTitleScreenContinuePromptAnimation (0xc2df):
 * Draws and advances the continue prompt animation.
 * Sprite offset: bc=$4446 → offset_y=$46, offset_x=$44
 */
static void handle_continue_prompt_animation(GameState *state) {
    uint8_t frame = state->titlescreen_continue_prompt_anim_frame;
    const uint8_t *sprite;

    if (frame == 6) {
        /* Selection mode: draw cursor-dependent sprite */
        uint8_t sel = state->title_screen_game_start_cursor_selection[0];
        if (sel > 1) sel = 1;
        sprite = continue_prompt_selection[sel];
    } else {
        sprite = continue_prompt_anim[frame].sprite;
        if (!sprite) sprite = sprite_continue_prompt_0; /* fallback */
    }
    load_sprite_data(state, sprite, 0x46, 0x44);

    /* Timer countdown */
    uint8_t timer = state->titlescreen_continue_prompt_anim_timer;
    timer--;
    if (timer != 0) {
        state->titlescreen_continue_prompt_anim_timer = timer;
        return;
    }

    /* Timer expired: check if next frame exists (sprite != NULL) */
    if (continue_prompt_anim[frame + 1].sprite != NULL) {
        frame++;
        state->titlescreen_continue_prompt_anim_frame = frame;
    }
    /* else: stay on current frame (hold) */

    /* Load new frame's duration */
    state->titlescreen_continue_prompt_anim_timer =
        continue_prompt_anim[frame].duration;
    if (state->titlescreen_continue_prompt_anim_timer == 0)
        state->titlescreen_continue_prompt_anim_timer = 2;
}

/*
 * Continue prompt per-frame rendering (Func_c1b1):
 * Draw continue prompt animation + GBC blank sprites.
 */
static void draw_continue_prompt(GameState *state) {
    handle_continue_prompt_animation(state);
    load_sprite_data(state, sprite_titlescreen_blank, 0x40, 0x20);
}

static void handle_titlescreen(GameState *state) {
    /*
     * HandleTitlescreen (0xc000):
     * Main menu with Game Start, Pokedex, Options.
     *
     * State 0: FadeInTitlescreen - load graphics, initialize
     * State 1: TitlescreenLoop - cursor movement + A/B input
     * State 2: Func_c10e - Continue prompt (saved game)
     * State 3: Func_c1cb - Transition to selected screen
     * State 4: GoToHighScoresFromTitlescreen
     * State 5: Game Start SFX + delay before transition
     */
    static uint8_t game_start_sfx_delay = 0;
    static uint8_t game_start_next_state = 3; /* where to go after delay */
    switch (state->screen_state) {
        case 0:
            /* FadeInTitlescreen (0xc00e) */
            if (state->vram && !state->gfx_loaded) {
                memset(state->vram, 0, sizeof(*state->vram));
                printf("Loading title screen graphics...\n");
                load_screen_assets(SCREEN_TITLESCREEN, state->vram,
                                   state, state->asset_base_path);
                state->hram.lcdc = 0x43;
                /* FadeInTitlescreen: xor a; ldh [hSCX], a; ldh [hSCY], a */
                state->hram.scx = 0;
                state->hram.scy = 0;
                state->gfx_loaded = 1;
            }
            state->title_screen_cursor_selection = 0;
            state->title_screen_blink_anim_frame = 0;
            state->title_screen_blink_anim_counter = 0xc8;
            state->title_screen_bouncing_ball_anim_frame = 0;
            state->title_screen_pokeball_anim_counter = 2;
            /* Play title screen music (Music_Title: bank $11, id $04) */
            PLAY_MUSIC(state, "title_screen", 0x11, 0x04);
            state->screen_state = 1;
            break;
        case 1:
            /* TitlescreenLoop (0xc089):
             * Move cursor, handle animations, check A/B buttons */
            {
                uint8_t old_cursor = state->title_screen_cursor_selection;
                move_menu_cursor(state, &state->title_screen_cursor_selection, 2);
                if (state->title_screen_cursor_selection != old_cursor)
                    PLAY_SFX(state, "cursor_move", 0x00, 0x03);
            }
            handle_titlescreen_animations(state);

            if (state->hram.newly_pressed_buttons & BTN_A) {
                if (state->title_screen_cursor_selection == 0) {
                    /* Player chose "Game Start" */
                    if (state->saved_game) {
                        /* Saved game exists: show continue prompt (state 2)
                         * ASM: plays SFX_CONFIRM ($00/$01) here */
                        PLAY_SFX(state, "confirm", 0x00, 0x01);
                        state->titlescreen_continue_prompt_anim_frame = 0;
                        state->titlescreen_continue_prompt_anim_timer = 2;
                        state->title_screen_game_start_cursor_selection[0] = 1; /* default to CONTINUE */
                        state->screen_state = 2;
                    } else {
                        /* No saved game: ASM plays MUSIC_NOTHING, 1 frame,
                         * SFX $00/$27, then waits $37 (55) frames before state 3.
                         * Does NOT play SFX_CONFIRM in this path. */
                        PLAY_MUSIC(state, "nothing", 0x0F, 0x00);
                        game_start_sfx_delay = 0x38; /* 56 frames (ASM: 1 AdvanceFrame + 55) */
                        game_start_next_state = 3;   /* go to Func_c1cb after delay */
                        state->screen_state = 5;
                    }
                } else {
                    /* Player chose Pokedex or Options: SFX confirm + direct transition */
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    state->screen_state = 3;
                }
            } else if (state->hram.newly_pressed_buttons & BTN_B) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                /* B button goes to High Scores (state 4) */
                state->screen_state = 4;
            }
            break;
        case 2:
            /* Func_c10e (0xc10e): Continue prompt (New Game / Continue)
             * Animation plays first, then cursor input is accepted. */
            handle_titlescreen_animations(state);
            {
                uint8_t anim_frame = state->titlescreen_continue_prompt_anim_frame;

                /* Handle cursor movement only when animation is at selection frame */
                if (anim_frame == 6) {
                    /* D-05: ASM Func_c1fc plays SFX $00/$03 on cursor move */
                    uint8_t old_cursor = state->title_screen_game_start_cursor_selection[0];
                    move_menu_cursor(state,
                        &state->title_screen_game_start_cursor_selection[0], 1);
                    if (state->title_screen_game_start_cursor_selection[0] != old_cursor)
                        PLAY_SFX(state, "cursor_move", 0x00, 0x03);
                }

                /* Draw the continue prompt */
                draw_continue_prompt(state);

                /* Only accept input when animation has reached selection frame */
                if (anim_frame != 6)
                    break;

                if (state->hram.newly_pressed_buttons & BTN_A) {
                    /* ASM Func_c10e: MUSIC_NOTHING, 1 frame, SFX $00/$27,
                     * AdvanceFrames $0041 (65 frames), then check selection.
                     * Does NOT play SFX_CONFIRM. */
                    PLAY_MUSIC(state, "nothing", 0x0F, 0x00);
                    game_start_sfx_delay = 0x42; /* 66 frames (ASM: 1 AdvanceFrame + 65) */
                    uint8_t sel = state->title_screen_game_start_cursor_selection[0];
                    if (sel == 0) {
                        /* NEW GAME selected: after delay, go to state 3 (Func_c1cb) */
                        state->loading_saved_game = 0;
                        game_start_next_state = 3;
                        state->screen_state = 5;
                    } else {
                        /* CONTINUE selected: after delay, load saved game.
                         * Use state 6 to handle post-delay save load. */
                        game_start_next_state = 6;
                        state->screen_state = 5;
                    }
                } else if (state->hram.newly_pressed_buttons & BTN_B) {
                    /* B: dismiss continue prompt, return to state 1 */
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    state->titlescreen_continue_prompt_anim_frame = 8;
                    state->titlescreen_continue_prompt_anim_timer = 2;
                }
            }
            /* Check if B-dismiss animation has completed (frame 14) */
            if (state->titlescreen_continue_prompt_anim_frame >= 14) {
                state->screen_state = 1;
            }
            break;
        case 3:
            /* Func_c1cb (0xc1cb): Transition to selected screen.
             * N5: ASM calls FadeOut + DisableLCD. */
            handle_titlescreen_animations(state);
            if (state->fade_direction != 1) {
                audio_stop_all(state->audio);
                start_screen_fade_out(state);
                break;
            }
            if (state->fade_counter > 0) break;
            {
                uint8_t cursor = state->title_screen_cursor_selection;
                if (cursor > 2) cursor = 0;
                state->gfx_loaded = 0;
                state->current_screen = titlescreen_cursor_to_screen[cursor];
                state->screen_state = 0;
                state->fade_direction = 0;
            }
            break;
        case 4:
            /* GoToHighScoresFromTitlescreen (0xc1e7)
             * N5: ASM calls FadeOut + DisableLCD. */
            handle_titlescreen_animations(state);
            if (state->fade_direction != 1) {
                start_screen_fade_out(state);
                break;
            }
            if (state->fade_counter > 0) break;
            state->gfx_loaded = 0;
            state->current_screen = SCREEN_HIGH_SCORES;
            state->screen_state = 1;
            state->high_score_is_entering_name = 0;
            state->fade_direction = 0;
            break;
        case 5:
            /* Game Start SFX + delay state.
             * ASM: MUSIC_NOTHING played on entry (state 1 or 2), then
             * 1 frame passes (the entry frame counts as that), SFX $00/$27
             * is played on the first frame here, then we count down. */
            handle_titlescreen_animations(state);
            if (game_start_sfx_delay == 0x37 || game_start_sfx_delay == 0x41) {
                /* First frame: play the Game Start SFX */
                PLAY_SFX(state, "new_ball", 0x00, 0x27);
            }
            if (game_start_sfx_delay > 0) {
                game_start_sfx_delay--;
            } else {
                /* Delay complete: transition to target state */
                if (game_start_next_state == 6) {
                    /* Continue path: load saved game */
                    state->screen_state = 6;
                } else {
                    /* Normal path: go to Func_c1cb (state 3) */
                    state->screen_state = 3;
                }
            }
            break;
        case 6:
            /* Post-delay continue game load (from Func_c10e).
             * ASM: FadeOut, DisableLCD, then load saved game. */
            if (load_saved_game_state(state)) {
                state->saved_game = 0;
                delete_saved_game(); /* ASM clears SRAM after load */
                state->loading_saved_game = 1;
                state->gfx_loaded = 0;
                state->current_screen = SCREEN_PINBALL_GAME;
                state->screen_state = 0;
            } else {
                /* Load failed: treat as new game, go to state 3 */
                state->loading_saved_game = 0;
                state->screen_state = 3;
            }
            break;
    }
}

/*=============================================================================
 * Pokedex Screen
 * Translated from engine/pokedex.asm (0x28000)
 *
 * State 0: LoadPokedexScreen - load graphics, init cursor
 * State 1: MainPokedexScreen - browse list, Up/Down/Left/Right/A/B
 * State 2: MonInfoPokedexScreen - description view (VWF rendered)
 * State 3: (unused)
 * State 4: ExitPokedexScreen
 *===========================================================================*/

/* Pokedex mon name table: 151 11-char names for display.
 * Unseen Pokemon show "----------" (0xFF fill).
 * Names are ASCII, rendered via char_to_tile mapping. */
/* Non-static: also used by red_field.c for evolution selection menu (H4) */
const char pokedex_names[151][12] = {
    "BULBASAUR  ", "IVYSAUR    ", "VENUSAUR   ", "CHARMANDER ",
    "CHARMELEON ", "CHARIZARD  ", "SQUIRTLE   ", "WARTORTLE  ",
    "BLASTOISE  ", "CATERPIE   ", "METAPOD    ", "BUTTERFREE ",
    "WEEDLE     ", "KAKUNA     ", "BEEDRILL   ", "PIDGEY     ",
    "PIDGEOTTO  ", "PIDGEOT    ", "RATTATA    ", "RATICATE   ",
    "SPEAROW    ", "FEAROW     ", "EKANS      ", "ARBOK      ",
    "PIKACHU    ", "RAICHU     ", "SANDSHREW  ", "SANDSLASH  ",
    "NIDORAN F  ", "NIDORINA   ", "NIDOQUEEN  ", "NIDORAN M  ",
    "NIDORINO   ", "NIDOKING   ", "CLEFAIRY   ", "CLEFABLE   ",
    "VULPIX     ", "NINETALES  ", "JIGGLYPUFF ", "WIGGLYTUFF ",
    "ZUBAT      ", "GOLBAT     ", "ODDISH     ", "GLOOM      ",
    "VILEPLUME  ", "PARAS      ", "PARASECT   ", "VENONAT    ",
    "VENOMOTH   ", "DIGLETT    ", "DUGTRIO    ", "MEOWTH     ",
    "PERSIAN    ", "PSYDUCK    ", "GOLDUCK    ", "MANKEY     ",
    "PRIMEAPE   ", "GROWLITHE  ", "ARCANINE   ", "POLIWAG    ",
    "POLIWHIRL  ", "POLIWRATH  ", "ABRA       ", "KADABRA    ",
    "ALAKAZAM   ", "MACHOP     ", "MACHOKE    ", "MACHAMP    ",
    "BELLSPROUT ", "WEEPINBELL ", "VICTREEBEL ", "TENTACOOL  ",
    "TENTACRUEL ", "GEODUDE    ", "GRAVELER   ", "GOLEM      ",
    "PONYTA     ", "RAPIDASH   ", "SLOWPOKE   ", "SLOWBRO    ",
    "MAGNEMITE  ", "MAGNETON   ", "FARFETCH'D ", "DODUO      ",
    "DODRIO     ", "SEEL       ", "DEWGONG    ", "GRIMER     ",
    "MUK        ", "SHELLDER   ", "CLOYSTER   ", "GASTLY     ",
    "HAUNTER    ", "GENGAR     ", "ONIX       ", "DROWZEE    ",
    "HYPNO      ", "KRABBY     ", "KINGLER    ", "VOLTORB    ",
    "ELECTRODE  ", "EXEGGCUTE  ", "EXEGGUTOR  ", "CUBONE     ",
    "MAROWAK    ", "HITMONLEE  ", "HITMONCHAN ", "LICKITUNG  ",
    "KOFFING    ", "WEEZING    ", "RHYHORN    ", "RHYDON     ",
    "CHANSEY    ", "TANGELA    ", "KANGASKHAN ", "HORSEA     ",
    "SEADRA     ", "GOLDEEN    ", "SEAKING    ", "STARYU     ",
    "STARMIE    ", "MR.MIME    ", "SCYTHER    ", "JYNX       ",
    "ELECTABUZZ ", "MAGMAR     ", "PINSIR     ", "TAUROS     ",
    "MAGIKARP   ", "GYARADOS   ", "LAPRAS     ", "DITTO      ",
    "EEVEE      ", "VAPOREON   ", "JOLTEON    ", "FLAREON    ",
    "PORYGON    ", "OMANYTE    ", "OMASTAR    ", "KABUTO     ",
    "KABUTOPS   ", "AERODACTYL ", "SNORLAX    ", "ARTICUNO   ",
    "ZAPDOS     ", "MOLTRES    ", "DRATINI    ", "DRAGONAIR  ",
    "DRAGONITE  ", "MEWTWO     ", "MEW        ",
};

/*
 * CountNumSeenOwnedMons: Count seen/caught Pokemon.
 * Updates num_pokemon_seen and num_pokemon_owned.
 */
static void count_seen_owned_mons(GameState *state) {
    uint16_t seen = 0, owned = 0;
    for (int i = 0; i < NUM_POKEMON; i++) {
        if (state->pokedex_flags[i] & (1 << BIT_POKEDEX_MON_SEEN))
            seen++;
        if (state->pokedex_flags[i] & (1 << BIT_POKEDEX_MON_CAUGHT))
            owned++;
    }
    state->num_pokemon_seen = seen;
    state->num_pokemon_owned = owned;
}

/*
 * DrawPokedexMonNames: Render 6 visible names using VWF into tile data.
 * Translated from DrawPokedexMonNames (0x28972) / DrawPokedexListMonName (0x28993).
 *
 * The ASM uses VWF to render name GRAPHICS into tile data at $8000+,
 * NOT by modifying tilemap indices. The tilemap stays fixed with indices
 * pointing to the tile data area where names are rendered.
 *
 * PokedexMonNamesTileLocations: vTilesOB tile $0,$A,$14,$1E,$28,$32,$3C,$46
 * Each name: bc=$500A (80 pixels wide, 10 tiles), bg_color=0x00 (normal).
 * Name chars mapped via ASM Func_295e1: ASCII - 0x20 = glyph index.
 */
static const uint16_t pokedex_name_tile_locations[8] = {
    0x8000, 0x80A0, 0x8140, 0x81E0, 0x8280, 0x8320, 0x83C0, 0x8460
};

/*
 * Func_28ad1: Compute the HBlank SCY for the Pokedex STAT split.
 * The STAT interrupt at LYC=$3B switches the BG tilemap and addressing,
 * and sets SCY to this value to scroll the name list.
 * Formula: (pokedexOffset & 0x0F) * 16 - 60
 */
static void update_pokedex_scroll(GameState *state) {
    uint8_t offset = state->pokedex_offset;
    state->hram.next_frame_hblank_scx =
        (uint8_t)(((offset & 0x0F) << 4) - 0x3C);
}

static void draw_pokedex_mon_names(GameState *state) {
    if (!state->vram || !state->vwf_font_gfx) return;

    for (int i = 0; i < 6; i++) {
        int mon_idx = state->pokedex_offset + i;
        /* ASM: c starts at pokedex_offset, sla a; and $e → slot=(c&7)*2 as word index */
        int slot = (state->pokedex_offset + i) & 7;
        uint16_t dest_addr = pokedex_name_tile_locations[slot];

        /* Initialize VWF: 80px wide, 10 tiles, normal colors (bg=0x00) */
        VWFState vwf;
        vwf_init(&vwf, 0x50, 0x0A, state->vwf_font_gfx,
                 state->vwf_font_gfx_size, 0x00);

        const char *name_text;
        if (mon_idx < NUM_POKEMON &&
            (state->pokedex_flags[mon_idx] & (1 << BIT_POKEDEX_MON_SEEN))) {
            name_text = pokedex_names[mon_idx];
        } else {
            name_text = " ";  /* BlankDexName2: space + terminator */
        }

        /* Render each character: ASM Func_295e1 maps ASCII via (c - 0x20) */
        for (int c = 0; name_text[c] != '\0'; c++) {
            uint8_t glyph = (uint8_t)((unsigned char)name_text[c] - 0x20);
            uint8_t width = character_widths[glyph];
            if (vwf_render_char(&vwf, glyph, width))
                break;
        }

        /* Copy rendered name tiles to VRAM tile data */
        vwf_copy_to_vram(&vwf, state->vram, dest_addr, 0);
    }
}

/*
 * Func_28a8a / Func_28aaa: Write Pokedex numbers to the $9C00 window tilemap.
 * BGMapLocations_287c7: 16 tilemap positions, one per slot.
 * Each number is 3 BCD digits at column 7 of the slot's first row.
 * Character mapping: ASCII digit + 0x23 offset = tile index.
 * Space (0x20) → tile 0xFF (blank).
 */
static void draw_pokedex_numbers(GameState *state) {
    if (!state->vram) return;

    for (int i = 0; i < 6; i++) {
        int mon_idx = state->pokedex_offset + i;
        int num_slot = (state->pokedex_offset + i) & 0x0F;
        /* BGMapLocations_287c7: vBGWin + slot * $40 + $07 */
        uint16_t map_addr = 0x9C00 + (uint16_t)(num_slot * 0x40 + 0x07);

        if (mon_idx < NUM_POKEMON) {
            int num = mon_idx + 1; /* 1-indexed Pokedex number */
            uint8_t d100 = (uint8_t)((num / 100) % 10);
            uint8_t d10  = (uint8_t)((num / 10) % 10);
            uint8_t d1   = (uint8_t)(num % 10);
            /* Digit tile = ASCII '0' + digit + 0x23 offset = 0x53 + digit */
            put_tile_in_vram(state->vram, 0, map_addr,     (uint8_t)(0x53 + d100));
            put_tile_in_vram(state->vram, 0, map_addr + 1, (uint8_t)(0x53 + d10));
            put_tile_in_vram(state->vram, 0, map_addr + 2, (uint8_t)(0x53 + d1));
        } else {
            /* Past 151: blank tiles */
            put_tile_in_vram(state->vram, 0, map_addr,     0xFF);
            put_tile_in_vram(state->vram, 0, map_addr + 1, 0xFF);
            put_tile_in_vram(state->vram, 0, map_addr + 2, 0xFF);
        }
    }
}

/*
 * DrawSummaryWindowMonName (0x28931):
 * Renders the selected Pokemon's name via VWF into tile data at signed offset
 * for tile $50 = tile_data[0][0x1500]. The info panel tilemap has tiles $50-$59
 * at row 3 cols 8-17. Rendering: 80px wide, 10 tiles, bg_color=$FF (white).
 * ASM char mapping: (ASCII - 0x20) = glyph index (same as name list).
 */
static void draw_summary_window_mon_name(GameState *state) {
    if (!state->vram || !state->vwf_font_gfx) return;

    uint8_t idx = state->cur_pokedex_index;
    const char *name_text;

    if (idx < NUM_POKEMON &&
        (state->pokedex_flags[idx] & (1 << BIT_POKEDEX_MON_SEEN))) {
        name_text = pokedex_names[idx];
    } else {
        name_text = " ";
    }

    VWFState vwf;
    vwf_init(&vwf, 0x50, 0x0A, state->vwf_font_gfx,
             state->vwf_font_gfx_size, 0xFF);

    for (int c = 0; name_text[c] != '\0'; c++) {
        uint8_t glyph = (uint8_t)((unsigned char)name_text[c] - 0x20);
        uint8_t width = character_widths[glyph];
        if (vwf_render_char(&vwf, glyph, width))
            break;
    }

    /* Copy to signed-mode tile $50 location: tile_data offset 0x1500.
     * Short chars (< $80) use 16-byte columns, so buffer is max_tiles * 16
     * bytes packed sequentially. ASM: LoadOrCopyVRAMData copies max_tiles*16. */
    uint16_t dest_offset = 0x1500; /* signed tile $50: 0x50 * 16 + 0x1000 */
    uint16_t size = (uint16_t)10 * 16;
    if (dest_offset + size <= VRAM_TILE_DATA_SIZE) {
        memcpy(&state->vram->tile_data[0][dest_offset], vwf.buffer, size);
    }
}

/*
 * DrawSummaryWindowMonSpecies (0x289c8):
 * Renders the species name (e.g. "SEED", "FLAME") via VWF into tile data
 * for tiles $5A-$6F in signed mode. The info panel tilemap has these tiles
 * arranged as interleaved rows 4-5. Only shown if Pokemon is CAUGHT.
 * Rendering: 88px wide, 22 tiles, bg_color=$FF (white).
 * ASM uses LoadPokemonNameVWFCharacterTiles with wd861=4 for species font.
 */

/* Species names table - extracted from text/pokedex_species.asm */
static const char *species_names[] = {
    /* Matches text/pokedex_species.asm order exactly (97 entries, 0-96) */
    "SEED", "LIZARD", "FLAME", "TINYTURTLE", "TURTLE",            /* $00-$04 */
    "SHELLFISH", "WORM", "COCOON", "BUTTERFLY", "HAIRY BUG",      /* $05-$09 */
    "POISON BEE", "TINY BIRD", "BIRD", "MOUSE", "BEAK",           /* $0A-$0E */
    "SNAKE", "COBRA", "POISON PIN", "DRILL", "FAIRY",             /* $0F-$13 */
    "FOX", "BALLOON", "BAT", "WEED", "FLOWER",                    /* $14-$18 */
    "MUSHROOM", "INSECT", "POISONMOTH", "MOLE", "SCRATCHCAT",     /* $19-$1D */
    "CLASSY CAT", "DUCK", "PIG MONKEY", "PUPPY", "LEGENDARY",     /* $1E-$22 */
    "TADPOLE", "PSI", "SUPERPOWER", "FLYCATCHER", "JELLYFISH",    /* $23-$27 */
    "ROCK", "MEGATON", "FIRE HORSE", "DOPEY", "HERMITCRAB",       /* $28-$2C */
    "MAGNET", "WILD DUCK", "TWIN BIRD", "TRIPLEBIRD", "SEA LION", /* $2D-$31 */
    "SLUDGE", "BIVALVE", "GAS", "SHADOW", "ROCK SNAKE",           /* $32-$36 */
    "HYPNOSIS", "RIVER CRAB", "PINCER", "BALL", "EGG",            /* $37-$3B */
    "COCONUT", "LONELY", "BONEKEEPER", "KICKING", "PUNCHING",     /* $3C-$40 */
    "LICKING", "POISON GAS", "SPIKES", "VINE", "PARENT",          /* $41-$45 */
    "DRAGON", "GOLDFISH", "STARSHAPE", "MYSTERIOUS", "BARRIER",    /* $46-$4A */
    "MANTIS", "HUMANSHAPE", "ELECTRIC", "SPITFIRE", "STAGBEETLE", /* $4B-$4F */
    "WILD BULL", "FISH", "ATROCIOUS", "TRANSPORT", "TRANSFORM",   /* $50-$54 */
    "EVOLUTION", "BUBBLE JET", "LIGHTNING", "FLAME", "VIRTUAL",    /* $55-$59 */
    "SPIRAL", "FOSSIL", "SLEEPING", "FREEZE", "GENETIC",          /* $5A-$5E */
    "NEW SPECIE", "RAT",                                           /* $5F-$60 */
};

/* Maps Pokemon index (0-150) to species name index in species_names[] above.
   Derived from data/mon_species.asm + constants/species_constants.asm */
static const uint8_t mon_species_index[151] = {
     0,  0,  0,  1,  2,  2,  3,  4,  5,  6,  /* Bulbasaur-Caterpie */
     7,  8,  9,  7, 10, 11, 12, 12, 96, 96,  /* Metapod-Raticate */
    11, 14, 15, 16, 13, 13, 13, 13, 17, 17,  /* Spearow-Nidorina */
    18, 17, 17, 18, 19, 19, 20, 20, 21, 21,  /* Nidoqueen-Wigglytuff */
    22, 22, 23, 23, 24, 25, 25, 26, 27, 28,  /* Zubat-Diglett */
    28, 29, 30, 31, 31, 32, 32, 33, 34, 35,  /* Dugtrio-Poliwag */
    35, 35, 36, 36, 36, 37, 37, 37, 24, 38,  /* Poliwhirl-Weepinbell */
    38, 39, 39, 40, 40, 41, 42, 42, 43, 44,  /* Victreebel-Slowbro */
    45, 45, 46, 47, 48, 49, 49, 50, 50, 51,  /* Magnemite-Cloyster */
    51, 52, 52, 53, 54, 55, 55, 56, 57, 58,  /* Gastly-Voltorb */
    58, 59, 60, 61, 62, 63, 64, 65, 66, 66,  /* Electrode-Weezing */
    67, 18, 59, 68, 69, 70, 70, 71, 71, 72,  /* Rhyhorn-Staryu */
    73, 74, 75, 76, 77, 78, 79, 80, 81, 82,  /* Starmie-Gyarados */
    83, 84, 85, 86, 87, 88, 89, 90, 90,  5,  /* Lapras-Kabuto */
     5, 91, 92, 93, 77,  2, 70, 70, 70, 94,  /* Kabutops-Mewtwo */
    95,                                        /* Mew */
};

static void draw_summary_window_mon_species(GameState *state) {
    if (!state->vram || !state->vwf_font_gfx) return;

    uint8_t idx = state->cur_pokedex_index;
    const char *species_text = " ";

    if (idx < NUM_POKEMON &&
        (state->pokedex_flags[idx] & (1 << BIT_POKEDEX_MON_CAUGHT))) {
        uint8_t sp_idx = mon_species_index[idx];
        if (sp_idx < sizeof(species_names) / sizeof(species_names[0])) {
            species_text = species_names[sp_idx];
        }
    }

    VWFState vwf;
    vwf_init(&vwf, 0x58, 0x16, state->vwf_font_gfx,
             state->vwf_font_gfx_size, 0xFF);
    /* Species uses 8x16 tall rendering (wd861=4 in ASM) so the interleaved
     * tilemap ($5A,$5C,$5E... on row 4; $5B,$5D,$5F... on row 5) correctly
     * shows top/bottom halves of each character across two tile rows. */
    vwf.force_tall = true;

    for (int c = 0; species_text[c] != '\0'; c++) {
        uint8_t glyph = (uint8_t)((unsigned char)species_text[c] - 0x20);
        uint8_t width = character_widths[glyph];
        if (vwf_render_char(&vwf, glyph, width))
            break;
    }

    /* Copy to signed-mode tile $5A location: tile_data offset 0x15A0.
     * With force_tall, each column is 32 bytes (2 tiles), so the sequential
     * copy produces tile pairs that match the interleaved tilemap layout. */
    uint16_t size = (uint16_t)vwf.max_tiles * 16;
    if (size > VWF_BUFFER_SIZE) size = VWF_BUFFER_SIZE;
    uint16_t dest_offset = 0x15A0; /* signed tile $5A: 0x5A * 16 + 0x1000 */
    if (dest_offset + size <= VRAM_TILE_DATA_SIZE) {
        memcpy(&state->vram->tile_data[0][dest_offset], vwf.buffer, size);
    }
}

/*
 * DrawSummaryWindowMonAttributes (0x28a15):
 * Writes dex number, height, weight tile indices to the info panel tilemap ($9800).
 * Func_28d71: reads attribute bytes, converts spaces ($20) to tile $FF,
 * other chars written as tile index (value + wd865 offset).
 * wd865 = 0 for all attribute rendering.
 *
 * Tilemap positions (hlCoord = vBGMap + row*32 + col):
 *   Dex #:  row 2, col 4  = $9844
 *   Height: row 6, col 8  = $98C8
 *   Weight: row 6, col 14 = $98CE
 *   Lbs:    row 7, col 16 = $98F0
 */
static void draw_summary_window_mon_attributes(GameState *state) {
    if (!state->vram) return;

    uint8_t idx = state->cur_pokedex_index;
    if (idx >= NUM_POKEMON) return;

    int num = idx + 1; /* 1-indexed dex number */
    uint8_t caught = (state->pokedex_flags[idx] & (1 << BIT_POKEDEX_MON_CAUGHT));

    /* Write dex number at (4, 2): 3 digits as tile indices $30-$39 */
    {
        uint16_t addr = 0x9800 + 2 * 32 + 4; /* $9844 */
        uint8_t d100 = (uint8_t)((num / 100) % 10);
        uint8_t d10  = (uint8_t)((num / 10) % 10);
        uint8_t d1   = (uint8_t)(num % 10);
        put_tile_in_vram(state->vram, 0, addr,     (uint8_t)(0x30 + d100));
        put_tile_in_vram(state->vram, 0, addr + 1, (uint8_t)(0x30 + d10));
        put_tile_in_vram(state->vram, 0, addr + 2, (uint8_t)(0x30 + d1));
    }

    if (caught) {
        uint8_t feet = pokedex_attributes[idx].feet;
        uint8_t inches = pokedex_attributes[idx].inches;
        uint16_t weight = pokedex_attributes[idx].weight;
        uint8_t wt_dec = pokedex_attributes[idx].weight_decimal;

        /* Height at (8, 6): [feet_tens_or_space][feet_ones][$70_or_$72][inches_ones] */
        {
            uint16_t addr = 0x9800 + 6 * 32 + 8; /* $98C8 */
            uint8_t ft_tens = (uint8_t)((feet / 10) % 10);
            uint8_t ft_ones = (uint8_t)(feet % 10);
            uint8_t in_ones = (uint8_t)(inches % 10);
            uint8_t in_tens = (uint8_t)((inches / 10) % 10);

            put_tile_in_vram(state->vram, 0, addr,
                             ft_tens ? (uint8_t)(0x30 + ft_tens) : 0xFF);
            put_tile_in_vram(state->vram, 0, addr + 1,
                             (uint8_t)(0x30 + ft_ones));
            put_tile_in_vram(state->vram, 0, addr + 2,
                             in_tens > 0 ? (uint8_t)0x70 : (uint8_t)0x72);
            put_tile_in_vram(state->vram, 0, addr + 3,
                             (uint8_t)(0x30 + in_ones));
        }

        /* Weight at (14, 6): up to 4 digits */
        {
            uint16_t addr = 0x9800 + 6 * 32 + 14; /* $98CE */

            if (wt_dec) {
                /* Decimal weight (Gastly/Haunter: 0.2 lbs) */
                int x = weight * 10;
                put_tile_in_vram(state->vram, 0, addr,     0xFF); /* space */
                put_tile_in_vram(state->vram, 0, addr + 1, 0xFF); /* space */
                put_tile_in_vram(state->vram, 0, addr + 2,
                                 (uint8_t)(0x30 + (x % 10)));
                put_tile_in_vram(state->vram, 0, addr + 3,
                                 (uint8_t)(0x30 + (wt_dec % 10)));
            } else {
                uint8_t d1000 = (uint8_t)((weight / 1000) % 10);
                uint8_t d100  = (uint8_t)((weight / 100) % 10);
                uint8_t d10   = (uint8_t)((weight / 10) % 10);
                uint8_t d1    = (uint8_t)(weight % 10);

                put_tile_in_vram(state->vram, 0, addr,
                                 weight >= 1000 ? (uint8_t)(0x30 + d1000) : 0xFF);
                put_tile_in_vram(state->vram, 0, addr + 1,
                                 weight >= 100 ? (uint8_t)(0x30 + d100) : 0xFF);
                put_tile_in_vram(state->vram, 0, addr + 2,
                                 weight >= 10 ? (uint8_t)(0x30 + d10) : 0xFF);
                put_tile_in_vram(state->vram, 0, addr + 3,
                                 (uint8_t)(0x30 + d1));
            }
        }

        /* Lbs indicator tile at (16, 7) */
        {
            uint16_t addr = 0x9800 + 7 * 32 + 16; /* $98F0 */
            put_tile_in_vram(state->vram, 0, addr,
                             wt_dec ? (uint8_t)0xFC : (uint8_t)0x83);
        }
    } else {
        /* Not caught: blank height/weight (BlankPokemonTileData_28a7f) */
        /* Height at (8, 6): $FF $FF $72 $FF */
        {
            uint16_t addr = 0x9800 + 6 * 32 + 8;
            put_tile_in_vram(state->vram, 0, addr,     0xFF);
            put_tile_in_vram(state->vram, 0, addr + 1, 0xFF);
            put_tile_in_vram(state->vram, 0, addr + 2, 0x72);
            put_tile_in_vram(state->vram, 0, addr + 3, 0xFF);
        }
        /* Weight at (14, 6): $FF $FF $FF $FF */
        {
            uint16_t addr = 0x9800 + 6 * 32 + 14;
            put_tile_in_vram(state->vram, 0, addr,     0xFF);
            put_tile_in_vram(state->vram, 0, addr + 1, 0xFF);
            put_tile_in_vram(state->vram, 0, addr + 2, 0xFF);
            put_tile_in_vram(state->vram, 0, addr + 3, 0xFF);
        }
        /* Lbs indicator tile at (16, 7): $83 */
        {
            uint16_t addr = 0x9800 + 7 * 32 + 16;
            put_tile_in_vram(state->vram, 0, addr, 0x83);
        }
    }
}

/* Billboard image filenames indexed by Pokedex number (0-150) */
static const char *billboard_filenames[151] = {
    "bulbasaur", "ivysaur", "venusaur", "charmander", "charmeleon",
    "charizard", "squirtle", "wartortle", "blastoise", "caterpie",
    "metapod", "butterfree", "weedle", "kakuna", "beedrill",
    "pidgey", "pidgeotto", "pidgeot", "rattata", "raticate",
    "spearow", "fearow", "ekans", "arbok", "pikachu",
    "raichu", "sandshrew", "sandslash", "nidoran_f", "nidorina",
    "nidoqueen", "nidoran_m", "nidorino", "nidoking", "clefairy",
    "clefable", "vulpix", "ninetales", "jigglypuff", "wigglytuff",
    "zubat", "golbat", "oddish", "gloom", "vileplume",
    "paras", "parasect", "venonat", "venomoth", "diglett",
    "dugtrio", "meowth", "persian", "psyduck", "golduck",
    "mankey", "primeape", "growlithe", "arcanine", "poliwag",
    "poliwhirl", "poliwrath", "abra", "kadabra", "alakazam",
    "machop", "machoke", "machamp", "bellsprout", "weepinbell",
    "victreebel", "tentacool", "tentacruel", "geodude", "graveler",
    "golem", "ponyta", "rapidash", "slowpoke", "slowbro",
    "magnemite", "magneton", "farfetch_d", "doduo", "dodrio",
    "seel", "dewgong", "grimer", "muk", "shellder",
    "cloyster", "gastly", "haunter", "gengar", "onix",
    "drowzee", "hypno", "krabby", "kingler", "voltorb",
    "electrode", "exeggcute", "exeggutor", "cubone", "marowak",
    "hitmonlee", "hitmonchan", "lickitung", "koffing", "weezing",
    "rhyhorn", "rhydon", "chansey", "tangela", "kangaskhan",
    "horsea", "seadra", "goldeen", "seaking", "staryu",
    "starmie", "mr_mime", "scyther", "jynx", "electabuzz",
    "magmar", "pinsir", "tauros", "magikarp", "gyarados",
    "lapras", "ditto", "eevee", "vaporeon", "jolteon",
    "flareon", "porygon", "omanyte", "omastar", "kabuto",
    "kabutops", "aerodactyl", "snorlax", "articuno", "zapdos",
    "moltres", "dratini", "dragonair", "dragonite", "mewtwo",
    "mew",
};

/*
 * DrawSummaryWindowMonImage (0x28add):
 * Loads Pokemon billboard picture into tile data at $8000.
 * Loads PNG from gfx/billboard/mon_pics/ and converts to 2bpp.
 */
static void draw_summary_window_mon_image(GameState *state) {
    if (!state->vram) return;

    uint8_t idx = state->cur_pokedex_index;
    if (idx >= NUM_POKEMON) return;

    uint8_t flags = state->pokedex_flags[idx];

    /* In signed addressing mode (info panel), tile $00 maps to $9000 =
     * tile_data offset 0x1000. The tilemap has tile indices $00-$17 for
     * the billboard image area. */
    uint16_t img_base = 0x1000; /* Signed mode offset for tile $00 */

    if (!flags) {
        /* Not seen: clear the image area to blank tiles */
        memset(&state->vram->tile_data[0][img_base], 0x00, 0x180);
        return;
    }

    /* Load billboard PNG for this Pokemon */
    char pic_path[400];
    snprintf(pic_path, sizeof(pic_path), "%sgfx/billboard/mon_pics/%s.png",
             state->asset_base_path, billboard_filenames[idx]);

    int width, height, channels;
    unsigned char *pixels = stbi_load(pic_path, &width, &height, &channels, 1);
    if (!pixels) {
        /* No image available: clear area */
        memset(&state->vram->tile_data[0][img_base], 0x00, 0x180);
        return;
    }

    /* Convert to 2bpp tiles and load to VRAM at $8000 (vTilesBG tile $00).
     * Billboard images are 48x32px = 6x4 tiles = 24 tiles = 384 bytes. */
    int tiles_per_row = width / 8;
    int tiles_per_col = height / 8;

    for (int ty = 0; ty < tiles_per_col && ty < 4; ty++) {
        for (int tx = 0; tx < tiles_per_row && tx < 6; tx++) {
            int tile_idx = ty * tiles_per_row + tx;

            uint16_t dest = img_base + (uint16_t)(tile_idx * 16);
            if (dest + 16 > VRAM_TILE_DATA_SIZE) break;

            for (int row = 0; row < 8; row++) {
                uint8_t lo = 0, hi = 0;
                for (int col = 0; col < 8; col++) {
                    int px = tx * 8 + col;
                    int py = ty * 8 + row;
                    if (px >= width || py >= height) continue;
                    uint8_t gray = pixels[py * width + px];
                    uint8_t color;
                    if (gray >= 192) color = 0;
                    else if (gray >= 128) color = 1;
                    else if (gray >= 64) color = 2;
                    else color = 3;
                    lo |= ((color & 1) << (7 - col));
                    hi |= (((color >> 1) & 1) << (7 - col));
                }
                state->vram->tile_data[0][dest + row * 2] = lo;
                state->vram->tile_data[0][dest + row * 2 + 1] = hi;
            }
        }
    }

    /* If seen but not caught, make it a silhouette (all non-white pixels → black) */
    if (!(flags & (1 << BIT_POKEDEX_MON_CAUGHT))) {
        for (int i = 0; i < 0x180; i += 2) {
            uint8_t lo = state->vram->tile_data[0][img_base + i];
            uint8_t hi = state->vram->tile_data[0][img_base + i + 1];
            uint8_t mask = lo | hi;
            state->vram->tile_data[0][img_base + i] = mask;
            state->vram->tile_data[0][img_base + i + 1] = mask;
        }
    }

    stbi_image_free(pixels);
}

/*
 * DrawCornerInfoPokedexScreen (0x2868b):
 * If SELECT held: show seen/owned counts as "XXX/YYY" in top-right corner.
 * Otherwise: show "POKeDEX" text sprite.
 */
static void draw_corner_info_pokedex_screen(GameState *state) {
    if (state->hram.joypad_state & BTN_SELECT) {
        /* Show seen/owned counts */
        uint16_t seen = state->num_pokemon_seen;
        uint16_t owned = state->num_pokemon_owned;

        /* Seen count: 3 digits at Y=$6d, X starting at $03, spaced 7px apart */
        uint8_t x = 0x03;
        load_sprite_data(state, sprite_digits[(seen / 100) % 10], 0x6d, x); x += 7;
        load_sprite_data(state, sprite_digits[(seen / 10) % 10], 0x6d, x); x += 7;
        load_sprite_data(state, sprite_digits[seen % 10], 0x6d, x);

        /* Slash separator at Y=$82, X=$02 */
        load_sprite_data(state, sprite_slash, 0x82, 0x02);

        /* Owned count: 3 digits at Y=$87, X starting at $03, spaced 7px apart */
        x = 0x03;
        load_sprite_data(state, sprite_digits[(owned / 100) % 10], 0x87, x); x += 7;
        load_sprite_data(state, sprite_digits[(owned / 10) % 10], 0x87, x); x += 7;
        load_sprite_data(state, sprite_digits[owned % 10], 0x87, x);
    } else {
        /* Show "POKeDEX" text sprite: ASM ld bc, $6800 → b=$68=x, c=$00=y */
        load_sprite_data(state, sprite_pokedex_text, 0x00, 0x68);
    }
}

/*
 * Func_28815: Cycle animated mon sprite frame.
 * Translated from engine/pokedex.asm Func_28815 (0x28815).
 * Uses the same frame cycling as catch mode: decrement timer, when 0 switch frames.
 * frame alternates between sprite_type and sprite_type+2 (idle frames),
 * with the hit frame on counter reset.
 */
static void cycle_animated_mon_sprite_frame(GameState *state) {
    state->loops_until_next_catch_sprite_anim_change--;
    if (state->loops_until_next_catch_sprite_anim_change != 0)
        return;

    /* ASM: inc [wBallHitWildMon]; and $7 */
    state->ball_hit_wild_mon++;
    state->ball_hit_wild_mon &= 0x07;

    if (state->ball_hit_wild_mon == 0) {
        /* Hit frame: wCurrentCatchMonHitFrameDuration timer, frame = type+2 */
        state->loops_until_next_catch_sprite_anim_change =
            state->current_catch_mon_hit_frame_duration;
        state->catch_mode_mon_update_timer = 0;
        state->current_animated_mon_sprite_frame =
            state->current_animated_mon_sprite_type + 2;
    } else {
        /* Idle frame: alternate between type+0 and type+1 */
        uint8_t type = state->current_animated_mon_sprite_type;
        uint8_t frame = state->current_animated_mon_sprite_frame;
        uint8_t c = (frame - type >= 1) ? 0 : 1;
        state->loops_until_next_catch_sprite_anim_change =
            (c == 0)
                ? state->current_catch_mon_idle_frame1_duration
                : state->current_catch_mon_idle_frame2_duration;
        state->catch_mode_mon_update_timer = 0;
        state->current_animated_mon_sprite_frame = type + c;
    }
}

/*
 * AnimateMonSpriteIfStartIsPressed (0x287e7):
 * When Start is held and the current mon is caught, show animated sprite
 * cycling through frames. Uses MonAnimatedSpriteTypes to determine the
 * animation type, and catches species with bit 7 set have no animation.
 */
static void animate_mon_sprite_if_start_pressed(GameState *state) {
    if (!state->pokedex_start_button_is_pressed)
        return;
    if (state->pokedex_cursor_was_moved)
        return;

    uint8_t idx = state->cur_pokedex_index;
    uint8_t sprite_type = get_mon_animated_sprite_type(state, idx);
    if (sprite_type & 0x80)
        return;  /* No animation for this species */

    state->current_animated_mon_sprite_type = sprite_type;
    cycle_animated_mon_sprite_frame(state);

    /* Load the animated sprite at position (0x30, 0x20) — ASM: ld bc, $2030 */
    uint8_t frame = state->current_animated_mon_sprite_frame;
    if (frame > 11) frame = 11;
    load_sprite_data(state, animated_mon_sprites[frame], 0x30, 0x20);
}

/*
 * Func_2885c: Description view sprites - only TOPPER_2 and POKeDEX text.
 * Much simpler than browse mode: no cursor, no animated scrollbar, no corner info.
 */
static void display_pokedex_description_sprites(GameState *state) {
    /* ASM Func_2885c: Check if current mon is caught, animate if Start held */
    uint8_t idx = state->cur_pokedex_index;
    if (idx < NUM_POKEMON && (state->pokedex_flags[idx] & (1 << BIT_POKEDEX_MON_CAUGHT))) {
        animate_mon_sprite_if_start_pressed(state);
    }
    /* ASM: ld bc, $8888; SPRITE_DEX_SCROLLBAR_TOPPER_2 */
    load_sprite_data(state, sprite_dex_scrollbar_topper2, 0x88, 0x88);
    /* ASM: ld bc, $6800; SPRITE_POKEDEX_TEXT (b=$68=x, c=$00=y) */
    load_sprite_data(state, sprite_pokedex_text, 0x00, 0x68);
}

/*
 * DisplayPokedexScrollBarAndCursor (0x285db): Show cursor arrow, animated
 * scrollbar, all 3 toppers, and corner info. Browse mode (state 1) only.
 */
static void display_pokedex_scroll_bar_and_cursor(GameState *state) {
    /* ASM: check if caught, animate sprite if Start held */
    uint8_t idx = state->cur_pokedex_index;
    if (idx < NUM_POKEMON && (state->pokedex_flags[idx] & (1 << BIT_POKEDEX_MON_CAUGHT))) {
        animate_mon_sprite_if_start_pressed(state);
    }

    /* All 3 scrollbar topper sprites at fixed positions */
    load_sprite_data(state, sprite_dex_scrollbar_topper0, 0x38, 0x8c);
    load_sprite_data(state, sprite_dex_scrollbar_topper1, 0x40, 0x88);
    load_sprite_data(state, sprite_dex_scrollbar_topper2, 0x88, 0x88);

    /* Corner info display (POKeDEX text or seen/owned counts) */
    draw_corner_info_pokedex_screen(state);

    /* ASM reads counter BEFORE modification for scrollbar frame display */
    uint8_t counter = state->pokedex_blinking_cursor_counter;

    uint8_t scroll_frame = (counter >> 2) & 3;
    uint8_t scroll_y = dex_scroll_bar_offsets[state->cur_pokedex_index] + 0x49;
    load_sprite_data(state, dex_scrollbar_sprites[scroll_frame], scroll_y, 0x90);

    /* Compute cursor position within visible window */
    int vis_pos = state->cur_pokedex_index - state->pokedex_offset;
    if (vis_pos < 0) vis_pos = 0;
    if (vis_pos > 4) vis_pos = 4;

    /* ASM: if joypad held, reset counter to 0 (cursor stays visible);
     * if no joypad, keep current counter (cursor blinks on cycle).
     * Then always increment. Post-increment value used for cursor blink. */
    if (state->hram.joypad_state)
        counter = 0;
    counter++;

    /* Blinking cursor: visible when bit 3 of post-increment counter is CLEAR (0-7),
     * hidden when SET (8-15). */
    if (!(counter & 0x08)) {
        /* ASM: swap c; add $40 → Y = vis_pos*16 + $40; adjusted to $3C for
         * correct vertical centering with list entries after STAT split. */
        uint8_t cursor_y = (uint8_t)(vis_pos * 0x10 + 0x3C);
        load_sprite_data(state, sprite_pokedex_arrow, cursor_y, 0x10);
    }

    /* Store counter */
    state->pokedex_blinking_cursor_counter = counter;
}

static void handle_pokedex_screen(GameState *state) {
    /*
     * HandlePokedexScreen (0x28000):
     * State 0: LoadPokedexScreen
     * State 1: MainPokedexScreen
     * State 2: MonInfoPokedexScreen (description - VWF)
     * State 3: (unused)
     * State 4: ExitPokedexScreen
     */
    switch (state->screen_state) {
        case 0:
            /* LoadPokedexScreen (0x2800e) */
            if (state->vram && !state->gfx_loaded) {
                memset(state->vram, 0, sizeof(*state->vram));
                printf("Loading pokedex screen graphics...\n");
                load_screen_assets(SCREEN_POKEDEX, state->vram,
                                   state, state->asset_base_path);
                state->hram.lcdc = 0x23;  /* signed tile addressing, BG+OBJ */
                state->hram.scy = 0x09;
                state->hram.scx = 0x00;
                state->hram.wy = 0x8C;
                state->hram.wx = 0x07;
                /* STAT interrupt: at LYC=$3B, XOR LCDC bits 3,4 to switch
                 * BG tilemap $9800→$9C00 and signed→unsigned addressing.
                 * This makes VWF name tiles at $8000 visible in the list area.
                 * hStatIntrRoutine=2 selects StatIntrTogglePokedexWindow. */
                state->hram.lyc = 0x3B;
                /* N3: ASM also sets LYC_SUB to 0x3B for browse mode */
                state->hram.lyc_sub = 0x3B;
                state->hram.next_lyc_sub = 0x3B;
                state->hram.stat_lcdc_xor = 0x18;
                state->hram.stat_intr_routine = 2;
                state->gfx_loaded = 1;

                /* Load VWF font if not already loaded */
                if (!state->vwf_font_gfx) {
                    char font_path[300];
                    snprintf(font_path, sizeof(font_path), "%sgfx/pokedex/characters.interleave.png",
                             state->asset_base_path);
                    state->vwf_font_gfx = vwf_load_font(font_path, &state->vwf_font_gfx_size);
                    if (state->vwf_font_gfx)
                        printf("Loaded VWF font: %zu bytes\n", state->vwf_font_gfx_size);
                    else
                        printf("WARNING: Failed to load VWF font\n");
                }
            }
            state->cur_pokedex_index = 0;
            state->pokedex_offset = 0;
            state->pokedex_blinking_cursor_counter = 0;
            state->pokedex_input_delay = 0;
            state->pokedex_start_button_is_pressed = 0;
            state->pokedex_input_accum = 0;
            state->pokedex_cursor_was_moved = 0;

            count_seen_owned_mons(state);
            draw_pokedex_mon_names(state);
            draw_pokedex_numbers(state);
            update_pokedex_scroll(state);
            draw_summary_window_mon_name(state);
            draw_summary_window_mon_species(state);
            draw_summary_window_mon_attributes(state);
            draw_summary_window_mon_image(state);
            display_pokedex_scroll_bar_and_cursor(state);

            /* Play pokedex music (Music_Pokedex: bank $0F, id $04) */
            PLAY_MUSIC(state, "pokedex", 0x0F, 0x04);

            state->screen_state = 1;
            break;

        case 1: {
            /* MainPokedexScreen (0x280fe) */
            uint8_t old_index = state->cur_pokedex_index;
            uint8_t old_offset = state->pokedex_offset;
            state->pokedex_cursor_was_moved = 0;

            /* ASM: Mew (index 150, Pokemon #151) is hidden unless seen.
             * Max index is 149 (150 Pokemon, 0-indexed) if Mew not seen,
             * or 150 if Mew has been seen. Check SEEN bit specifically. */
            uint8_t max_index = (state->pokedex_flags[150] & (1 << BIT_POKEDEX_MON_SEEN))
                                ? (NUM_POKEMON - 1) : (NUM_POKEMON - 2);

            /* D-06: ASM HandlePokedexDirectionalInput (0x28513) OR-accumulates
             * hPressedButtons into wd95e every frame, even during cooldown.
             * When cooldown expires, the accumulated input is processed. */
            state->pokedex_input_accum |= state->hram.pressed_buttons;
            if (state->pokedex_input_delay > 0) {
                state->pokedex_input_delay--;
            } else {
                uint8_t buttons = state->pokedex_input_accum;
                state->pokedex_input_accum = 0;
                if (buttons & BTN_UP) {
                    if (state->cur_pokedex_index > 0) {
                        state->cur_pokedex_index--;
                        state->pokedex_cursor_was_moved = 1;
                    }
                    state->pokedex_input_delay = 4;
                } else if (buttons & BTN_DOWN) {
                    if (state->cur_pokedex_index < max_index) {
                        state->cur_pokedex_index++;
                        state->pokedex_cursor_was_moved = 1;
                    }
                    state->pokedex_input_delay = 4;
                } else if (buttons & BTN_LEFT) {
                    /* Page up by 5.
                     * ASM uses 5-frame scroll with hNextFrameHBlankSCX -=4 each frame. */
                    if (state->cur_pokedex_index >= 5)
                        state->cur_pokedex_index -= 5;
                    else
                        state->cur_pokedex_index = 0;
                    state->pokedex_cursor_was_moved = 1;
                    state->pokedex_window_was_shifted = 1;
                    state->pokedex_input_delay = 4;
                } else if (buttons & BTN_RIGHT) {
                    /* Page down by 5.
                     * ASM uses 5-frame scroll with hNextFrameHBlankSCX +=4 each frame. */
                    if (state->cur_pokedex_index + 5 <= max_index)
                        state->cur_pokedex_index += 5;
                    else
                        state->cur_pokedex_index = max_index;
                    state->pokedex_cursor_was_moved = 1;
                    state->pokedex_window_was_shifted = 1;
                    state->pokedex_input_delay = 4;
                }
            }

            /* Adjust visible window offset (ASM: keep vis_pos in 0-4 range) */
            if (state->cur_pokedex_index < state->pokedex_offset)
                state->pokedex_offset = state->cur_pokedex_index;
            if (state->cur_pokedex_index > state->pokedex_offset + 4)
                state->pokedex_offset = state->cur_pokedex_index - 4;

            /* Redraw names if offset changed */
            if (state->pokedex_offset != old_offset) {
                draw_pokedex_mon_names(state);
                draw_pokedex_numbers(state);
                update_pokedex_scroll(state);
            }

            /* Update info panel when cursor moves to new Pokemon */
            if (state->cur_pokedex_index != old_index) {
                draw_summary_window_mon_name(state);
                draw_summary_window_mon_species(state);
                draw_summary_window_mon_attributes(state);
                draw_summary_window_mon_image(state);
            }

            if (state->pokedex_cursor_was_moved)
                PLAY_SFX(state, "cursor_move", 0x00, 0x03);

            /* A button: play cry for SEEN mons, view description if CAUGHT.
             * ASM (0x280fe): checks any flag set first (PlayCry for all seen),
             * then checks BIT_POKEDEX_MON_CAUGHT for description entry. */
            uint8_t newly = state->hram.newly_pressed_buttons;
            if ((newly & BTN_A) && !state->pokedex_cursor_was_moved) {
                uint8_t flags = state->pokedex_flags[state->cur_pokedex_index];
                if (flags) {
                    /* PlayCry (0x4ef): mon_id = cur_pokedex_index + 1 */
                    audio_play_cry(state->audio, (uint8_t)(state->cur_pokedex_index + 1));

                    if (flags & (1 << BIT_POKEDEX_MON_CAUGHT)) {
                        /* LoadPokemonDescriptionIntoVRAM (0x288c6):
                         * Render first page of description using VWF */
                        if (state->vwf_font_gfx) {
                            const char *desc = pokedex_descriptions[state->cur_pokedex_index];
                            VWFState vwf;
                            vwf_init(&vwf, 0x90, 0x6C, state->vwf_font_gfx,
                                     state->vwf_font_gfx_size, 0x00);
                            const char *next = vwf_render_description(&vwf, desc, character_widths);
                            /* Copy rendered tiles to VRAM at vTilesSH tile $10 = $8900.
                             * In signed mode, tile $90 maps to $8900. The pokedex_2.map
                             * tilemap already has the correct interleaved tile indices
                             * ($90,$92,$94... / $91,$93,$95...) at rows 10-15. */
                            vwf_copy_to_vram(&vwf, state->vram, 0x8900, 0);
                            state->pokedex_description_page_flag = (next != NULL) ? 1 : 0;
                            state->pokedex_desc_next = next;
                        }

                        /* ScrollPokemonDescriptionDown (0x2887c):
                         * Start 17-frame LYC-based scroll animation to reveal description.
                         * hLYC is set to $3f, and hNextLYCSub progressively moves downward
                         * from $47 to $77, expanding the XORed LCDC region. */
                        state->hram.lyc = 0x3f;
                        state->hram.lyc_sub = 0x47;
                        state->hram.next_lyc_sub = 0x47;
                        state->hram.stat_lcdc_xor = 0x18;
                        state->pokedex_desc_scroll_counter = 0x33;
                        state->pokedex_desc_scroll_dir = 0;  /* down */
                        state->screen_state = 2;
                        break;
                    }
                    /* SEEN but not CAUGHT: cry plays but no description (ASM: jp .done) */
                }
            }

            /* B button: exit */
            if (newly & BTN_B) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                state->screen_state = 4;
                break;
            }

            /* Start button: toggle mon image mode (GBC only).
             * ASM (.checkIfGameboyColorAndIfStartIsPressed 0x28155):
             * When Start is first pressed, call DrawSummaryWindowMonImage and
             * initialize animation state for the current mon's sprite. */
            if (state->hram.joypad_state & BTN_START) {
                if (!state->pokedex_start_button_is_pressed) {
                    state->pokedex_start_button_is_pressed = 0xFF;
                    draw_summary_window_mon_image(state);
                    /* Initialize animation frame durations for this mon */
                    uint8_t mon_idx = state->cur_pokedex_index;
                    uint8_t stype = get_mon_animated_sprite_type(state, mon_idx);
                    state->current_animated_mon_sprite_type = stype;
                    state->current_animated_mon_sprite_frame = stype;
                    get_catch_sprite_frame_durations_for_mon(state, mon_idx,
                        &state->current_catch_mon_idle_frame1_duration,
                        &state->current_catch_mon_idle_frame2_duration,
                        &state->current_catch_mon_hit_frame_duration);
                    state->loops_until_next_catch_sprite_anim_change =
                        state->current_catch_mon_idle_frame1_duration;
                    state->ball_hit_wild_mon = 0;
                    state->catch_mode_mon_update_timer = 0;
                }
            } else {
                if (state->pokedex_start_button_is_pressed) {
                    state->pokedex_start_button_is_pressed = 0;
                }
            }

            display_pokedex_scroll_bar_and_cursor(state);
            break;
        }

        case 2:
            /* MonInfoPokedexScreen (0x28178) - description view */

            /* Per-frame scroll animation update.
             * ScrollPokemonDescriptionDown (0x2887c): b counts down from $33 by 3.
             * Each frame sets hNextLYCSub = $7a - b (scroll down, dir=0).
             * Func_288a2: hNextLYCSub = $44 + b (scroll up, dir=1).
             * When counter reaches 0, scroll is complete. */
            if (state->pokedex_desc_scroll_counter > 0) {
                if (state->pokedex_desc_scroll_dir == 0) {
                    /* Scrolling down (entering description) */
                    state->hram.next_lyc_sub = (uint8_t)(0x7a - state->pokedex_desc_scroll_counter);
                    state->hram.lyc_sub = state->hram.next_lyc_sub;
                } else {
                    /* Scrolling up (exiting description) */
                    state->hram.next_lyc_sub = (uint8_t)(0x44 + state->pokedex_desc_scroll_counter);
                    state->hram.lyc_sub = state->hram.next_lyc_sub;
                }
                if (state->pokedex_desc_scroll_counter >= 3)
                    state->pokedex_desc_scroll_counter -= 3;
                else
                    state->pokedex_desc_scroll_counter = 0;

                if (state->pokedex_desc_scroll_counter == 0) {
                    if (state->pokedex_desc_scroll_dir == 1) {
                        /* Scroll up complete: restore browse mode LYC */
                        state->hram.lyc = 0x3b;
                        state->hram.lyc_sub = 0x3b;
                        state->hram.next_lyc_sub = 0x3b;
                        display_pokedex_scroll_bar_and_cursor(state);
                        state->screen_state = 1;
                        break;
                    }
                    /* Scroll down complete: disable STAT split for full description view */
                    state->hram.stat_lcdc_xor = 0;
                }
                display_pokedex_description_sprites(state);
                break;
            }

            if (state->pokedex_description_page_flag & 1) {
                /* More pages available */
                if (state->hram.newly_pressed_buttons & BTN_A) {
                    /* Render next page (Func_28912) */
                    if (state->vwf_font_gfx && state->pokedex_desc_next) {
                        VWFState vwf;
                        vwf_init(&vwf, 0x90, 0x6C, state->vwf_font_gfx,
                                 state->vwf_font_gfx_size, 0x00);
                        const char *next = vwf_render_description(
                            &vwf, state->pokedex_desc_next, character_widths);
                        vwf_copy_to_vram(&vwf, state->vram, 0x8900, 0);
                        state->pokedex_description_page_flag = (next != NULL) ? 1 : 0;
                        state->pokedex_desc_next = next;
                    }
                } else if (state->hram.newly_pressed_buttons & BTN_B) {
                    /* Start scroll-up animation to exit description view.
                     * Func_288a2 (0x288a2): reverse scroll, then restore browse mode. */
                    state->hram.stat_lcdc_xor = 0x18;
                    state->pokedex_desc_scroll_counter = 0x33;
                    state->pokedex_desc_scroll_dir = 1;  /* up */
                    draw_pokedex_mon_names(state);
                    draw_pokedex_numbers(state);
                    update_pokedex_scroll(state);
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    break;
                }
            } else {
                /* Last page - A or B exits */
                if (state->hram.newly_pressed_buttons & (BTN_A | BTN_B)) {
                    /* Start scroll-up animation to exit description view */
                    state->hram.stat_lcdc_xor = 0x18;
                    state->pokedex_desc_scroll_counter = 0x33;
                    state->pokedex_desc_scroll_dir = 1;  /* up */
                    draw_pokedex_mon_names(state);
                    draw_pokedex_numbers(state);
                    update_pokedex_scroll(state);
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    break;
                }
            }
            /* Handle Start button for animated sprite in description view
             * (ASM MonInfoPokedexScreen 0x28178 .displayMonImage) */
            if (state->hram.joypad_state & BTN_START) {
                if (!state->pokedex_start_button_is_pressed) {
                    state->pokedex_start_button_is_pressed = 0xFF;
                    draw_summary_window_mon_image(state);
                    uint8_t mon_idx = state->cur_pokedex_index;
                    uint8_t stype = get_mon_animated_sprite_type(state, mon_idx);
                    state->current_animated_mon_sprite_type = stype;
                    state->current_animated_mon_sprite_frame = stype;
                    get_catch_sprite_frame_durations_for_mon(state, mon_idx,
                        &state->current_catch_mon_idle_frame1_duration,
                        &state->current_catch_mon_idle_frame2_duration,
                        &state->current_catch_mon_hit_frame_duration);
                    state->loops_until_next_catch_sprite_anim_change =
                        state->current_catch_mon_idle_frame1_duration;
                    state->ball_hit_wild_mon = 0;
                    state->catch_mode_mon_update_timer = 0;
                }
            } else {
                if (state->pokedex_start_button_is_pressed) {
                    state->pokedex_start_button_is_pressed = 0;
                    draw_summary_window_mon_image(state);
                }
            }

            /* Description mode: only draw topper2 + POKeDEX text (Func_2885c) */
            display_pokedex_description_sprites(state);
            break;

        case 3:
            /* Unused state (Func_282e9) */
            state->screen_state = 1;
            break;

        case 4:
            /* ExitPokedexScreen (0x284f9):
             * N5: ASM calls FadeOut + DisableLCD before transition. */
            if (state->fade_direction != 1) {
                start_screen_fade_out(state);
                break;
            }
            if (state->fade_counter > 0) break;
            state->gfx_loaded = 0;
            state->hram.stat_lcdc_xor = 0;
            state->hram.stat_intr_routine = 0;
            state->current_screen = SCREEN_TITLESCREEN;
            state->screen_state = 0;
            state->fade_direction = 0;
            break;
    }
}

/*=============================================================================
 * Options Screen Animation Handlers
 * Translated from options_screen.asm (0xc7ac, 0xc80b, 0xc88a, etc.)
 *===========================================================================*/

/*
 * HandleOptionsPsyduckAnimation (0xc7ac):
 * Psyduck only DISPLAYS animated frame when: state != 1, wd916 == 0,
 * wd917 == 0, and pikachu is at frame 4.  Otherwise shows frame 0.
 * Internal timer/frame always advance regardless.  Loops.
 */
static void handle_options_psyduck_anim(GameState *state) {
    uint8_t display_frame = 0;
    if (state->screen_state != 1 &&
        state->options_menu_selection == 0 &&
        state->options_rumble_setting == 0 &&
        state->options_pikachu_anim_frame == 4) {
        display_frame = state->options_psyduck_anim_frame;
    }

    /* lb bc, $50, $50 → offset_y=0x50 (c), offset_x=0x50 (b) */
    load_sprite_data(state, options_psyduck_anim[display_frame].sprite_data,
                     0x50, 0x50);

    uint8_t timer = state->options_psyduck_anim_timer;
    timer--;
    if (timer != 0) {
        state->options_psyduck_anim_timer = timer;
        return;
    }

    uint8_t frame = state->options_psyduck_anim_frame;
    if (options_psyduck_anim[frame + 1].duration != 0) {
        frame++;
    } else {
        frame = 0;  /* loop */
    }
    state->options_psyduck_anim_frame = frame;
    state->options_psyduck_anim_timer = options_psyduck_anim[frame].duration;
}

/*
 * HandleOptionsPikachuAnimation (0xc80b):
 * Pikachu only DISPLAYS animated frame when: state != 1, wd916 == 0,
 * wd917 == 0.  Otherwise shows frame 0.
 * Non-looping: 0 → 1 → 2 → 3 → 4 → stays at 4.
 * Internal timer/frame always advance regardless.
 */
static void handle_options_pikachu_anim(GameState *state) {
    uint8_t display_frame = 0;
    if (state->screen_state != 1 &&
        state->options_menu_selection == 0 &&
        state->options_rumble_setting == 0) {
        display_frame = state->options_pikachu_anim_frame;
    }

    /* lb bc, $78, $70 → offset_y=0x70 (c), offset_x=0x78 (b) */
    load_sprite_data(state, options_pikachu_anim[display_frame].sprite_data,
                     0x70, 0x78);

    uint8_t timer = state->options_pikachu_anim_timer;
    timer--;
    if (timer != 0) {
        state->options_pikachu_anim_timer = timer;
        return;
    }

    /* Non-looping: advance only if next entry exists, else stay at current */
    uint8_t frame = state->options_pikachu_anim_frame;
    if (options_pikachu_anim[frame + 1].duration != 0) {
        frame++;
    }
    state->options_pikachu_anim_frame = frame;
    state->options_pikachu_anim_timer = options_pikachu_anim[frame].duration;
}

/*
 * HandleOptionsPokeballAnimation (0xc88a):
 * Position depends on wd916.  Only DISPLAYS animated frame in state 1
 * (main menu); shows frame 0 otherwise.  Loops.
 */
static void handle_options_pokeball_anim(GameState *state) {
    uint8_t sel = state->options_menu_selection;
    if (sel > 2) sel = 0;
    uint8_t offset_y = options_pokeball_offsets[sel][0];
    uint8_t offset_x = options_pokeball_offsets[sel][1];

    uint8_t display_frame = 0;
    if (state->screen_state == 1) {
        display_frame = state->options_pokeball_anim_frame;
    }
    load_sprite_data(state, options_pokeball_anim[display_frame].sprite_data,
                     offset_y, offset_x);

    uint8_t timer = state->options_pokeball_anim_timer;
    timer--;
    if (timer != 0) {
        state->options_pokeball_anim_timer = timer;
        return;
    }

    uint8_t frame = state->options_pokeball_anim_frame;
    if (options_pokeball_anim[frame + 1].duration != 0) {
        frame++;
    } else {
        frame = 0;  /* loop */
    }
    state->options_pokeball_anim_frame = frame;
    state->options_pokeball_anim_timer = options_pokeball_anim[frame].duration;
}

/*
 * Func_c92e: Faded arrow showing current rumble status (main menu only).
 */
static void handle_options_faded_arrow(GameState *state) {
    uint8_t sel = state->options_rumble_setting;
    if (sel > 1) sel = 0;
    load_sprite_data(state, sprite_options_arrow_faded,
                     options_arrow_rumble_offsets[sel][0],
                     options_arrow_rumble_offsets[sel][1]);
}

/*
 * Func_c8f1: Active arrow for sub-menu.
 * mode: 0 = rumble, 1 = key config, 2 = BGM/SFX.
 */
static void handle_options_active_arrow(GameState *state, uint8_t mode) {
    if (mode > 2) return;
    uint8_t selection;
    switch (mode) {
        case 0: selection = state->options_rumble_setting; break;
        case 1: selection = state->options_key_config_selection; break;
        case 2: selection = state->options_bgm_sfx_selection; break;
        default: return;
    }
    if (selection > options_arrow_max_selection[mode])
        selection = 0;
    const uint8_t (*offsets)[2] = options_arrow_offset_tables[mode];
    load_sprite_data(state, sprite_options_arrow,
                     offsets[selection][0], offsets[selection][1]);
}

/*
 * Func_c4f4: Reset pikachu and psyduck animation state.
 * Called when entering rumble sub-menu or switching rumble to enabled.
 */
static void reset_options_pikachu_psyduck_anim(GameState *state) {
    state->options_psyduck_anim_frame = 0;
    state->options_pikachu_anim_frame = 0;
    state->options_psyduck_anim_timer = 0x02;
    state->options_pikachu_anim_timer = 0x09;
}

/*
 * Func_c869: Trigger rumble at pikachu frame 3, timer 1,
 * only when wd916 == 0 and wd917 == 0 (rumble enabled on rumble row).
 */
static void handle_options_rumble_trigger(GameState *state) {
    if (state->options_menu_selection != 0) return;
    if (state->options_rumble_setting != 0) return;
    if (state->options_pikachu_anim_frame != 3) return;
    if (state->options_pikachu_anim_timer != 1) return;
    state->rumble_pattern = 0x55;
    state->rumble_duration = 0x40;
}

/*
 * Func_c43a: Run all three animations + faded arrow (state 1 combo).
 */
static void options_run_animations_with_faded_arrow(GameState *state) {
    handle_options_psyduck_anim(state);
    handle_options_pikachu_anim(state);
    handle_options_pokeball_anim(state);
    handle_options_faded_arrow(state);
}

/*=============================================================================
 * Sound Test Helpers
 * Translated from RedrawSoundTestID (0xc76c)
 *===========================================================================*/

/*
 * RedrawSoundTestID (0xc76c):
 * Writes a 2-digit hex ID to the BG tilemap.
 * Hex digit tiles start at tile $B7 in the options tileset.
 */
static void redraw_sound_test_id(GameState *state, uint16_t vram_addr, uint8_t id) {
    if (!state->vram) return;
    uint8_t high_nybble = (id >> 4) & 0x0F;
    uint8_t low_nybble = id & 0x0F;
    put_tile_in_vram(state->vram, 0, vram_addr, 0xB7 + high_nybble);
    put_tile_in_vram(state->vram, 0, vram_addr + 1, 0xB7 + low_nybble);
}

/*=============================================================================
 * Key Config Helpers
 * Translated from Func_c95f, Func_c948, Func_c621, SaveDefaultKeyConfigs
 *===========================================================================*/

/* Get pointer to key config array (contiguous in GameState) */
static KeyConfig *get_key_configs(GameState *state) {
    return &state->key_config_ball_start;
}

/*
 * Func_c95f (0xc95f): Render a button mask as display tiles.
 * Writes up to 8 tiles to win_map at the specified VRAM address.
 * Button-to-tile mapping from Data_c9ae.
 * "+" separator = $1A between multiple buttons. Blank fill = $81.
 */
static void render_key_config_text(GameState *state, uint16_t vram_addr, uint8_t button_mask) {
    if (!state->vram) return;

    /* Build 7-tile display buffer (matching ASM wd922) */
    uint8_t display[7] = {0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81};
    int pos = 0;
    bool first = true;

    for (int bit = 0; bit < 8 && pos < 7; bit++) {
        if (!(button_mask & (1 << bit))) continue;

        /* Add "+" separator between buttons */
        if (!first && pos < 7) {
            display[pos++] = 0x1A;
        }
        first = false;

        /* Write tile(s) for this button */
        if (key_config_button_tiles[bit][0] && pos < 7) {
            display[pos++] = key_config_button_tiles[bit][0];
        }
        if (key_config_button_tiles[bit][1] && pos < 7) {
            display[pos++] = key_config_button_tiles[bit][1];
        }
    }

    /* Write the 8 tiles to VRAM (matching ASM LoadOrCopyVRAMData with bc=$0008) */
    for (int i = 0; i < 7; i++) {
        put_tile_in_vram(state->vram, 0, vram_addr + (uint16_t)i, display[i]);
    }
    put_tile_in_vram(state->vram, 0, vram_addr + 7, 0x81);
}

/*
 * Func_c948 (0xc948): Initialize key config display.
 * Loops through all 7 key configs and renders each row's primary and
 * secondary keys in the window tilemap.
 * vBGWin coord(13, 3) = $9C00 + 3*32 + 13 = $9C6D
 */
static void init_key_config_display(GameState *state) {
    KeyConfig *configs = get_key_configs(state);
    for (int i = 0; i < 7; i++) {
        /* Primary key display */
        render_key_config_text(state, key_config_vram_addrs[i], configs[i].primary);
        /* Secondary key display (+$20 = next tilemap row) */
        render_key_config_text(state, key_config_vram_addrs[i] + 0x20, configs[i].secondary);
    }
}

/*
 * SaveDefaultKeyConfigs (0xca3a): Reset all key configs to defaults.
 */
static void save_default_key_configs(GameState *state) {
    state->key_config_ball_start    = (KeyConfig){BTN_A, 0};
    state->key_config_left_flipper  = (KeyConfig){BTN_LEFT, 0};
    state->key_config_right_flipper = (KeyConfig){BTN_A, 0};
    state->key_config_left_tilt     = (KeyConfig){BTN_DOWN, 0};
    state->key_config_right_tilt    = (KeyConfig){BTN_B, 0};
    state->key_config_upper_tilt    = (KeyConfig){BTN_SELECT, 0};
    state->key_config_menu          = (KeyConfig){BTN_START, 0};
    init_key_config_display(state);
}

/*
 * Func_c644 (0xc644): Clear a key config entry and blank its display row.
 * index: byte offset into key config array (row*2 for primary, row*2+1 for secondary)
 * vram_addr: window tilemap address for the display row
 */
static void clear_key_config_row(GameState *state, int index, uint16_t vram_addr) {
    /* Clear the key config byte */
    uint8_t *configs = (uint8_t *)get_key_configs(state);
    configs[index] = 0;

    /* Write 8 blank tiles (Data_c689) to the display */
    if (state->vram) {
        for (int i = 0; i < 8; i++) {
            put_tile_in_vram(state->vram, 0, vram_addr + (uint16_t)i, 0x81);
        }
    }
}

/*
 * Func_c621 (0xc621): Key config blink effect.
 * During detection phases, flash a solid white sprite at the current
 * row's position. Only visible when frame_counter bit 2 is set.
 * blink_index: index into SpritePixelOffsetData_c66d (row*2 + column)
 */
static void key_config_blink(GameState *state, int blink_index) {
    if (blink_index < 0 || blink_index >= 14) return;
    uint8_t offset_y = key_config_blink_offsets[blink_index][0];
    uint8_t offset_x = key_config_blink_offsets[blink_index][1];

    if (state->hram.frame_counter & 0x04) {
        load_sprite_data(state, sprite_options_solid_white, offset_y, offset_x);
    }
}

/*
 * Func_c9be (0xc9be): Resolve combined button mask from joypad state.
 *
 * The ASM version expands current and previous button states into 8-byte arrays,
 * counts total active buttons, and if more than 3 are held simultaneously,
 * removes excess newly-pressed entries (Func_ca15) to limit to 3 concurrent
 * buttons. It then ORs newly-pressed into held and converts back to a bitmask.
 *
 * For key config detection (phases 2/4), this ensures that if a user
 * accidentally presses multiple buttons, only the most relevant subset
 * is captured. For single-button presses (the normal case), the output
 * equals the input. For multi-button combos, we limit to at most 3 buttons
 * by removing the highest-bit (least-significant direction) buttons first.
 */
static uint8_t resolve_key_config_buttons(uint8_t current, uint8_t previous) {
    /* Newly pressed = buttons in current that weren't in previous */
    uint8_t newly = (current ^ previous) & current;
    /* Held = buttons that were already pressed before */
    uint8_t held = previous & current;

    /* Count held buttons */
    int held_count = 0;
    for (int i = 0; i < 8; i++) {
        if (held & (1 << i)) held_count++;
    }

    /* If held count >= 3, no room for new buttons — return held only */
    if (held_count >= 3) {
        return held;
    }

    /* Count newly pressed buttons */
    int new_count = 0;
    for (int i = 0; i < 8; i++) {
        if (newly & (1 << i)) new_count++;
    }

    /* If total (held + new) > 3, trim newly pressed from bit 7 down */
    int total = held_count + new_count;
    if (total > 3) {
        int excess = total - 3;
        /* Remove excess newly-pressed buttons starting from highest bit
         * (ASM: Func_ca15 removes from array[0] = bit 7 first) */
        for (int i = 7; i >= 0 && excess > 0; i--) {
            if (newly & (1 << i)) {
                newly &= ~(1 << i);
                excess--;
            }
        }
    }

    return held | newly;
}

static void handle_options_screen(GameState *state) {
    /*
     * HandleOptionsScreen (0xc34a):
     *
     * State 0: Func_c35a  - Init (load gfx, LCDC $47, start animations)
     * State 1: Func_c400  - Main menu (Up/Down cursor, A to enter, B to exit)
     * State 2: Func_c483  - Exit to title screen
     * State 3: Func_c493  - Rumble toggle (Left/Right, pikachu/psyduck anim)
     * State 4: Func_c506  - Key Config (stub - navigate arrow, B to return)
     * State 5: Func_c691  - BGM/SFX Test (stub - navigate arrow, B to return)
     */
    switch (state->screen_state) {
        case 0:
            /* Func_c35a: Init */
            if (state->vram && !state->gfx_loaded) {
                memset(state->vram, 0, sizeof(*state->vram));
                printf("Loading options screen graphics...\n");
                load_screen_assets(SCREEN_OPTIONS, state->vram,
                                   state, state->asset_base_path);
                state->hram.lcdc = 0x47;  /* 8x16 sprites enabled */
                /* Func_c35a: xor a; ldh [hSCX/hSCY], a */
                state->hram.scx = 0;
                state->hram.scy = 0;
                state->gfx_loaded = 1;
            }
            state->options_pokeball_anim_timer = 0x02;
            state->options_psyduck_anim_timer = 0x02;
            state->options_pikachu_anim_timer = 0x09;
            options_run_animations_with_faded_arrow(state);
            /* Func_c948: initialize key config display in window tilemap */
            init_key_config_display(state);
            /* Play options music (Music_Options: bank $12, id $02) */
            PLAY_MUSIC(state, "options", 0x12, 0x02);
            /* Draw initial sound test IDs (Func_c35a lines 40-45) */
            /* hlCoord 7, 11, vBGMap = $9800 + 11*32 + 7 = $9967 */
            redraw_sound_test_id(state, 0x9967, state->sound_test_current_bgm);
            /* hlCoord 7, 13, vBGMap = $9800 + 13*32 + 7 = $99A7 */
            redraw_sound_test_id(state, 0x99A7, state->sound_test_current_sfx);
            state->screen_state = 1;
            break;

        case 1: {
            /* Func_c400: Main menu loop */
            /* Func_c41a: navigate wd916 with UP/DOWN (hPressedButtons) */
            uint8_t buttons = state->hram.pressed_buttons;
            uint8_t sel = state->options_menu_selection;
            uint8_t old_sel = sel;
            if (buttons & BTN_UP) {
                if (sel > 0) sel--;
            } else if (buttons & BTN_DOWN) {
                if (sel < 2) sel++;
            }
            state->options_menu_selection = sel;
            if (sel != old_sel) PLAY_SFX(state, "cursor_move", 0x00, 0x03);

            /* Func_c43a: animations + faded arrow */
            options_run_animations_with_faded_arrow(state);

            /* Func_c447: A button → enter sub-menu */
            if (state->hram.newly_pressed_buttons & BTN_A) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                if (sel == 0) {
                    /* Rumble sub-menu */
                    reset_options_pikachu_psyduck_anim(state);
                    state->screen_state = 3;
                } else if (sel == 1) {
                    /* Key Config */
                    state->sprite_buffer_size = 0;
                    state->hram.lcdc |= 0x08;  /* enable window */
                    state->screen_state = 4;
                } else {
                    /* BGM/SFX Test */
                    state->screen_state = 5;
                }
            } else if (state->hram.newly_pressed_buttons & BTN_B) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                /* Exit to title screen */
                state->screen_state = 2;
            }
            break;
        }

        case 2:
            /* Func_c483: Exit options screen. N5: ASM calls FadeOut. */
            if (state->fade_direction != 1) {
                save_game(state); /* persist key config changes */
                start_screen_fade_out(state);
                break;
            }
            if (state->fade_counter > 0) break;
            state->gfx_loaded = 0;
            state->current_screen = SCREEN_TITLESCREEN;
            state->screen_state = 0;
            state->fade_direction = 0;
            break;

        case 3: {
            /* Func_c493: Rumble toggle */
            /* Func_c4b4: navigate wd917 with LEFT/RIGHT (hNewlyPressedButtons) */
            uint8_t newly = state->hram.newly_pressed_buttons;
            uint8_t rum = state->options_rumble_setting;
            if (newly & BTN_LEFT) {
                if (rum > 0) {
                    rum--;
                    state->options_rumble_setting = rum;
                    reset_options_pikachu_psyduck_anim(state);
                    PLAY_SFX(state, "cursor_move", 0x00, 0x03);
                }
            } else if (newly & BTN_RIGHT) {
                if (rum < 1) {
                    rum++;
                    state->options_rumble_setting = rum;
                    state->rumble_pattern = 0;
                    state->rumble_duration = 0;
                    PLAY_SFX(state, "cursor_move", 0x00, 0x03);
                }
            }

            /* Func_c4e6: animations + active arrow (mode 0) */
            handle_options_psyduck_anim(state);
            handle_options_pikachu_anim(state);
            handle_options_pokeball_anim(state);
            handle_options_active_arrow(state, 0);

            /* Func_c869: rumble trigger check */
            handle_options_rumble_trigger(state);

            /* B → clear rumble, return to main menu */
            if (newly & BTN_B) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                state->rumble_pattern = 0;
                state->rumble_duration = 0;
                state->screen_state = 1;
            }
            break;
        }

        case 4: {
            /* Func_c506: Key Config
             * Phase 0: Navigate rows, A to select, B to exit
             * Phase 1-4: Key detection multi-frame state machine */
            uint8_t phase = state->key_config_detect_phase;

            if (phase == 0) {
                /* Func_c534: navigate wd918 with UP/DOWN (hNewlyPressedButtons) */
                uint8_t newly = state->hram.newly_pressed_buttons;
                uint8_t ksel = state->options_key_config_selection;
                uint8_t old_ksel = ksel;
                if (newly & BTN_UP) {
                    if (ksel > 0) ksel--;
                } else if (newly & BTN_DOWN) {
                    if (ksel < 7) ksel++;
                }
                state->options_key_config_selection = ksel;
                if (ksel != old_ksel) PLAY_SFX(state, "cursor_move", 0x00, 0x03);

                /* Func_c554: active arrow (mode 1) */
                handle_options_active_arrow(state, 1);

                /* Func_c55a: A button actions */
                if (newly & BTN_A) {
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    if (ksel == 0) {
                        /* Row 0 = "Default" → reset all key configs */
                        save_default_key_configs(state);
                        init_key_config_display(state);
                    } else {
                        /* Rows 1-7: start key detection for this row */
                        uint8_t row = ksel - 1;  /* 0-6 */
                        state->key_config_detect_row = row;
                        state->key_config_previous_capture = 0;  /* Func_c9be: reset previous */
                        /* Clear primary key and blank its display */
                        int primary_index = row * 2;
                        clear_key_config_row(state, primary_index,
                                             key_config_vram_addrs[row]);
                        state->key_config_detect_phase = 1;
                    }
                }

                /* B → save key configs to SRAM, clear sprites, disable window, return.
                 * ASM Func_c506: SaveData for key configs on B-press. */
                if (newly & BTN_B) {
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    save_game(state);  /* persist key config changes immediately */
                    state->sprite_buffer_size = 0;
                    state->hram.lcdc &= (uint8_t)~0x08;  /* disable window */
                    state->screen_state = 1;
                }
            } else if (phase == 1) {
                /* Wait for all buttons released before detecting primary */
                int blink_idx = state->key_config_detect_row * 2;
                key_config_blink(state, blink_idx);
                handle_options_active_arrow(state, 1);

                if (state->hram.joypad_state == 0) {
                    /* L8: 30-frame delay before primary detection (ASM: AdvanceFrames $1E) */
                    state->key_config_detect_timeout = 30;
                    state->key_config_detect_phase = 10; /* delay sub-phase */
                }
            } else if (phase == 2) {
                /* Detect primary key: wait for any button press */
                int blink_idx = state->key_config_detect_row * 2;
                key_config_blink(state, blink_idx);
                handle_options_active_arrow(state, 1);

                if (state->hram.joypad_state != 0) {
                    /* Capture the pressed button(s) as primary (Func_c9be) */
                    uint8_t captured = resolve_key_config_buttons(
                        state->hram.joypad_state,
                        state->key_config_previous_capture);
                    state->key_config_previous_capture = captured;
                    state->key_config_captured_primary = captured;

                    /* Save to key config and update display */
                    uint8_t row = state->key_config_detect_row;
                    uint8_t *configs = (uint8_t *)get_key_configs(state);
                    configs[row * 2] = captured;
                    render_key_config_text(state, key_config_vram_addrs[row], captured);

                    /* Clear secondary key and blank its display */
                    int sec_index = row * 2 + 1;
                    clear_key_config_row(state, sec_index,
                                         key_config_vram_addrs[row] + 0x20);

                    state->key_config_detect_phase = 3;
                }
            } else if (phase == 3) {
                /* Wait for release before secondary detection */
                int blink_idx = state->key_config_detect_row * 2 + 1;
                key_config_blink(state, blink_idx);
                handle_options_active_arrow(state, 1);

                if (state->hram.joypad_state == 0) {
                    /* L8: 30-frame delay before secondary detection (ASM: AdvanceFrames $1E) */
                    state->key_config_detect_timeout = 30;
                    state->key_config_detect_phase = 11; /* delay sub-phase */
                }
            } else if (phase == 4) {
                /* Detect secondary key: wait for press OR timeout */
                int blink_idx = state->key_config_detect_row * 2 + 1;
                key_config_blink(state, blink_idx);
                handle_options_active_arrow(state, 1);

                bool done = false;
                if (state->hram.joypad_state != 0) {
                    /* Capture secondary key (Func_c9be) */
                    uint8_t captured = resolve_key_config_buttons(
                        state->hram.joypad_state,
                        state->key_config_previous_capture);
                    state->key_config_previous_capture = captured;
                    uint8_t row = state->key_config_detect_row;
                    uint8_t *configs = (uint8_t *)get_key_configs(state);
                    configs[row * 2 + 1] = captured;
                    render_key_config_text(state, key_config_vram_addrs[row] + 0x20, captured);
                    done = true;
                } else if (state->key_config_detect_timeout == 0) {
                    /* Timeout - no secondary key */
                    done = true;
                } else {
                    state->key_config_detect_timeout--;
                }

                if (done) {
                    /* L9: Wait for all buttons released before finishing
                     * (ASM: resets timeout on press, loops until released) */
                    state->key_config_detect_phase = 12; /* wait-for-release */
                }
            } else if (phase == 10) {
                /* L8: 30-frame delay before primary detection */
                int blink_idx = state->key_config_detect_row * 2;
                key_config_blink(state, blink_idx);
                handle_options_active_arrow(state, 1);
                if (state->key_config_detect_timeout > 0) {
                    state->key_config_detect_timeout--;
                } else {
                    state->key_config_detect_phase = 2;
                }
            } else if (phase == 11) {
                /* L8: 30-frame delay before secondary detection */
                int blink_idx = state->key_config_detect_row * 2 + 1;
                key_config_blink(state, blink_idx);
                handle_options_active_arrow(state, 1);
                if (state->key_config_detect_timeout > 0) {
                    state->key_config_detect_timeout--;
                } else {
                    state->key_config_detect_timeout = 0x5A;  /* 90 frames */
                    state->key_config_detect_phase = 4;
                }
            } else if (phase == 12) {
                /* L9: Wait for all buttons released after secondary capture */
                int blink_idx = state->key_config_detect_row * 2 + 1;
                key_config_blink(state, blink_idx);
                handle_options_active_arrow(state, 1);
                if (state->hram.joypad_state != 0) {
                    /* L9: Accumulate button state (ASM resets timeout + loops) */
                    uint8_t captured = resolve_key_config_buttons(
                        state->hram.joypad_state,
                        state->key_config_previous_capture);
                    state->key_config_previous_capture = captured;
                    uint8_t row = state->key_config_detect_row;
                    uint8_t *configs = (uint8_t *)get_key_configs(state);
                    configs[row * 2 + 1] = captured;
                    render_key_config_text(state, key_config_vram_addrs[row] + 0x20, captured);
                } else {
                    state->key_config_detect_phase = 0;
                }
            }
            break;
        }

        case 5: {
            /* Func_c691: BGM/SFX Test */
            /* Func_c6bf: navigate wd919 with UP/DOWN (hNewlyPressedButtons) */
            uint8_t newly = state->hram.newly_pressed_buttons;
            uint8_t bsel = state->options_bgm_sfx_selection;
            if (newly & BTN_UP) {
                if (bsel > 0) bsel--;
            } else if (newly & BTN_DOWN) {
                if (bsel < 1) bsel++;
            }
            state->options_bgm_sfx_selection = bsel;

            /* Func_c6d9: animations + active arrow (mode 2) */
            handle_options_psyduck_anim(state);
            handle_options_pikachu_anim(state);
            handle_options_pokeball_anim(state);
            handle_options_active_arrow(state, 2);

            /* Func_c6e8: handle sound test selection */
            if (bsel == 0) {
                /* BGM row selected */
                /* A button: play BGM via SongBanks lookup (0xc6e8).
                 * ASM: MUSIC_NOTHING, 3x AdvanceFrame, then PlaySong. */
                if (newly & BTN_A) {
                    uint8_t idx = state->sound_test_current_bgm;
                    if (idx < NUM_SONGS) {
                        PLAY_MUSIC(state, "nothing", 0x0F, 0x00);  /* MUSIC_NOTHING */
                        state->sound_test_bgm_pending_bank = song_banks[idx].bank;
                        state->sound_test_bgm_pending_id = song_banks[idx].id;
                        state->sound_test_bgm_delay = 3;
                        state->screen_state = 7;  /* 3-frame delay state */
                        break;
                    }
                }
                /* UpdateSoundTestBackgroundMusicSelection (0xc715):
                 * LEFT/RIGHT with hPressedButtons to change BGM ID */
                uint8_t buttons = state->hram.pressed_buttons;
                uint8_t bgm = state->sound_test_current_bgm;
                if (buttons & BTN_LEFT) {
                    if (bgm == 0) bgm = NUM_SONGS - 1;
                    else bgm--;
                    state->sound_test_current_bgm = bgm;
                    redraw_sound_test_id(state, 0x9967, bgm);
                } else if (buttons & BTN_RIGHT) {
                    bgm++;
                    if (bgm >= NUM_SONGS) bgm = 0;
                    state->sound_test_current_bgm = bgm;
                    redraw_sound_test_id(state, 0x9967, bgm);
                }
            } else {
                /* SFX row selected */
                /* A button: play SFX (0xc73a) */
                if (newly & BTN_A) {
                    audio_play_sfx(state->audio, 0,
                                   state->sound_test_current_sfx);
                }
                /* UpdateSoundTestSoundEffectSelection (0xc73a):
                 * LEFT/RIGHT with hPressedButtons to change SFX ID */
                uint8_t buttons = state->hram.pressed_buttons;
                uint8_t sfx = state->sound_test_current_sfx;
                if (buttons & BTN_LEFT) {
                    if (sfx == 0) sfx = NUM_SOUND_EFFECTS - 1;
                    else sfx--;
                    state->sound_test_current_sfx = sfx;
                    redraw_sound_test_id(state, 0x99A7, sfx);
                } else if (buttons & BTN_RIGHT) {
                    sfx++;
                    if (sfx >= NUM_SOUND_EFFECTS) sfx = 0;
                    state->sound_test_current_sfx = sfx;
                    redraw_sound_test_id(state, 0x99A7, sfx);
                }
            }

            /* Func_c691: B → stop current music, 3-frame delay, restart options music,
             * THEN play SFX. ASM: PlaySong MUSIC_NOTHING, 3x AdvanceFrame,
             * PlaySong MUSIC_OPTIONS, PlaySoundEffect SFX_CONFIRM. */
            if (newly & BTN_B) {
                PLAY_MUSIC(state, "nothing", 0x0F, 0x00);  /* MUSIC_NOTHING: stop current music */
                /* SFX_CONFIRM is played AFTER the delay in state 6, not here (M8 fix) */
                state->sound_test_exit_delay = 3;
                state->screen_state = 6;  /* use state 6 for delay */
            }
            break;
        }

        case 6: {
            /* 3-frame delay after stopping music in sound test B-exit (ASM: 3x AdvanceFrame).
             * ASM Func_c691: stop music, 3 frames, restart options music, THEN play SFX. */
            if (state->sound_test_exit_delay > 0) {
                state->sound_test_exit_delay--;
                break;
            }
            PLAY_MUSIC(state, "options", 0x12, 0x02);
            PLAY_SFX(state, "confirm", 0x00, 0x01);  /* SFX after delay (M8 fix) */
            state->screen_state = 1;
            break;
        }

        case 7: {
            /* 3-frame delay before playing BGM in sound test (ASM Func_c6e8:
             * MUSIC_NOTHING, 3x AdvanceFrame, then PlaySong). */
            if (state->sound_test_bgm_delay > 0) {
                state->sound_test_bgm_delay--;
                break;
            }
            audio_play_music(state->audio,
                             state->sound_test_bgm_pending_bank,
                             state->sound_test_bgm_pending_id);
            state->screen_state = 5;  /* return to sound test */
            break;
        }
    }
}

/*=============================================================================
 * High Scores Screen
 * Translated from engine/high_scores_screen.asm (0xca7f)
 *
 * State 0: Func_ca8f - Score check & init (compare player score to top 5)
 * State 1: Func_cb14 - Load graphics & music, render all scores
 * State 2: Func_ccac - Name entry (character cycling + asterisk blink)
 * State 3: Func_ccb6 - View mode (arrow anim, Start to switch fields)
 * State 4: Func_cd6c - Print/Send UI (non-functional)
 * State 5: ExitHighScoresScreen
 *===========================================================================*/

/*
 * Render a single BCD score + name entry to the tilemap.
 * Translated from Func_d2cb (0xd2cb).
 *
 * Score: 6-byte little-endian BCD (points[0] is LSB, points[5] is MSB).
 * ASM reads from points[5] backward to points[0] for MSB-first display.
 * Each byte has 2 BCD digits (high nibble, low nibble).
 * Digit tile formula: digit * 2 + base_tile
 *   base_tile = $6C for comma positions (b=12,9,6,3), $58 otherwise
 * Each tile is written as a 2-row pair: tile at hl, tile+1 at hl+$20.
 *
 * Name: 3 chars, each stored as (ASCII - 0x37).
 * Name tile formula: char_value * 2 + 0x90
 * Each tile is a 2-row pair like digits.
 *
 * base_addr: tilemap address for this score row.
 */
static void render_high_score_entry(GameState *state, uint16_t base_addr,
                                     const HighScore *hs) {
    if (!state->vram) return;

    /* Name: 3 chars at base_addr + 5, going right to left
     * ASM: de starts at name[2], reads backward to name[0] */
    for (int i = 0; i < 3; i++) {
        uint16_t addr = base_addr + 5 - (uint16_t)i;
        uint8_t ch = hs->name[2 - i];
        uint8_t top_tile = (uint8_t)(ch * 2 + 0x90);
        uint8_t bot_tile = (top_tile == 0xFE) ? 0xFE : (uint8_t)(top_tile + 1);
        put_tile_in_vram(state->vram, 0, addr, top_tile);
        put_tile_in_vram(state->vram, 0, addr + 0x20, bot_tile);
    }

    /* Score: 6 LE-BCD bytes = 12 digits, rendered at base_addr + 6 onwards.
     * ASM reads from points[5] (MSB) backward to points[0] (LSB).
     * b starts at 12 and decrements. c starts at 1 (suppress leading zeros).
     * Comma base tile ($6C) at b=12,9,6,3; normal ($58) otherwise. */
    int b = 12;
    int suppress = 1; /* c register: 1=suppress leading zeros, 0=render all */
    int digit_pos = 0;

    for (int byte_idx = 5; byte_idx >= 0; byte_idx--) {
        uint8_t bcd_byte = hs->points[byte_idx];

        /* High nibble first (ASM: swap a; and $f) */
        uint8_t digit = (bcd_byte >> 4) & 0x0F;
        uint8_t base_tile = (b == 12 || b == 9 || b == 6 || b == 3) ? 0x6C : 0x58;

        /* Func_d30e logic: render if digit!=0, or if last digit (b==1),
         * or if past leading zeros (suppress==0) */
        bool render = (digit != 0) || (b == 1) || (suppress == 0);
        if (render) {
            suppress = 0;
            uint16_t addr = base_addr + 6 + (uint16_t)digit_pos;
            uint8_t top = (uint8_t)(digit * 2 + base_tile);
            uint8_t bot = (top == 0xFE) ? 0xFE : (uint8_t)(top + 1);
            put_tile_in_vram(state->vram, 0, addr, top);
            put_tile_in_vram(state->vram, 0, addr + 0x20, bot);
        }
        b--;
        digit_pos++;

        /* Low nibble (ASM: and $f) */
        digit = bcd_byte & 0x0F;
        base_tile = (b == 12 || b == 9 || b == 6 || b == 3) ? 0x6C : 0x58;

        render = (digit != 0) || (b == 1) || (suppress == 0);
        if (render) {
            suppress = 0;
            uint16_t addr = base_addr + 6 + (uint16_t)digit_pos;
            uint8_t top = (uint8_t)(digit * 2 + base_tile);
            uint8_t bot = (top == 0xFE) ? 0xFE : (uint8_t)(top + 1);
            put_tile_in_vram(state->vram, 0, addr, top);
            put_tile_in_vram(state->vram, 0, addr + 0x20, bot);
        }
        b--;
        digit_pos++;
    }

    /* ASM renders one final zero digit after the loop (xor a; call Func_d317) */
    {
        uint16_t addr = base_addr + 6 + (uint16_t)digit_pos;
        uint8_t base_tile = (b == 12 || b == 9 || b == 6 || b == 3) ? 0x6C : 0x58;
        uint8_t top = base_tile; /* digit 0: 0*2 + base = base */
        uint8_t bot = (top == 0xFE) ? 0xFE : (uint8_t)(top + 1);
        put_tile_in_vram(state->vram, 0, addr, top);
        put_tile_in_vram(state->vram, 0, addr + 0x20, bot);
    }
}

/*
 * Render all 5 high scores for one field to the tilemap.
 * Scores appear at rows 2, 5, 8, 11, 14 (3 rows apart).
 * Row N base addr = vBGMap + N*32 = $9800 + N*$20.
 *
 * ASM starts from score 5 at row 14 and works backwards:
 *   hlCoord(0, 14, vBGMap) = $9800 + 14*32 = $99C0
 *   Each score row is 3*$20 = $60 bytes apart.
 */
static void render_all_high_scores(GameState *state, const HighScore *scores,
                                    uint16_t tilemap_base) {
    for (int i = 4; i >= 0; i--) {
        uint16_t row = (uint16_t)(14 - (4 - i) * 3);
        uint16_t addr = tilemap_base + row * 32;
        render_high_score_entry(state, addr, &scores[i]);
    }
}

/*
 * AnimateHighScoresArrow (0xd24f):
 * Bouncing arrow at bottom corner. Counter 0-39.
 */
static void animate_high_scores_arrow(GameState *state) {
    uint8_t counter = state->high_scores_arrow_anim_counter;
    counter++;
    if (counter >= 0x28) counter = 0;
    state->high_scores_arrow_anim_counter = counter;

    uint8_t c = 0x77;  /* Y position */
    if (state->high_scores_stage == 0) {
        /* Red: right arrow */
        uint8_t x = hs_right_arrow_x_offsets[counter];
        load_sprite_data(state, sprite_hs_arrow_right, c, x);
    } else {
        /* Blue: left arrow */
        uint8_t x = hs_left_arrow_x_offsets[counter];
        load_sprite_data(state, sprite_hs_arrow_left, c, x);
    }
}

/*
 * Func_d4cf: Handle Left/Right to switch Red<->Blue field.
 * Translated from engine/high_scores_screen.asm (0xd4cf).
 *
 * The ASM uses a 39-frame ($27) sliding animation:
 *   Red→Blue: WX decrements by 4/frame (window slides left), SCX increments by 4/frame
 *   Blue→Red: WX increments by 4/frame (window slides right), SCX decrements by 4/frame
 * Every other frame, TransitionHighScoresPalettes interpolates palettes.
 * After animation: SCX=0, window disabled, LCDC bit 3 set/cleared for the new field.
 */
static void handle_high_scores_field_switch(GameState *state) {
    /* If animation is active, update it per-frame */
    if (state->high_scores_field_switch_counter > 0) {
        state->high_scores_field_switch_counter--;
        uint8_t remaining = state->high_scores_field_switch_counter;

        if (state->high_scores_field_switch_dir == 0) {
            /* Red→Blue: WX -= 4, SCX += 4 each frame */
            state->hram.wx = (uint8_t)(state->hram.wx - 4);
            state->hram.scx = (uint8_t)(state->hram.scx + 4);
        } else {
            /* Blue→Red: WX += 4, SCX -= 4 each frame */
            state->hram.wx = (uint8_t)(state->hram.wx + 4);
            state->hram.scx = (uint8_t)(state->hram.scx - 4);
        }

        /* Palette interpolation on odd frames (ASM: bit 0, b; call nz) */
        if (remaining & 1) {
            /* Simple palette interpolation: blend toward target field palettes.
             * ASM uses TransitionHighScoresPalettes with 15 intermediate steps.
             * For simplicity, we interpolate linearly between source and target. */
            uint8_t target_stage = state->high_scores_field_switch_dir == 0 ? 1 : 0;
            const uint16_t *src_bg = get_high_scores_bg_palettes(target_stage ^ 1);
            const uint16_t *dst_bg = get_high_scores_bg_palettes(target_stage);
            const uint16_t *src_obj = get_high_scores_obj_palettes(target_stage ^ 1);
            const uint16_t *dst_obj = get_high_scores_obj_palettes(target_stage);
            /* Config palette overrides for high scores */
            if (state->config && state->config->palettes.palettes_loaded) {
                /* Config index 4 = high_scores_red, 5 = high_scores_blue */
                src_bg = state->config->palettes.screens[4 + (target_stage ^ 1)].bg;
                dst_bg = state->config->palettes.screens[4 + target_stage].bg;
                src_obj = state->config->palettes.screens[4 + (target_stage ^ 1)].obj;
                dst_obj = state->config->palettes.screens[4 + target_stage].obj;
            }

            /* Progress fraction: 0 at start (counter=38), 1 at end (counter=0) */
            uint8_t progress = (uint8_t)(0x27 - remaining);
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 4; c++) {
                    uint16_t s = src_bg[i * 4 + c];
                    uint16_t d = dst_bg[i * 4 + c];
                    int sr = s & 0x1F, sg = (s >> 5) & 0x1F, sb = (s >> 10) & 0x1F;
                    int dr = d & 0x1F, dg = (d >> 5) & 0x1F, db = (d >> 10) & 0x1F;
                    int r = sr + (dr - sr) * progress / 0x27;
                    int g = sg + (dg - sg) * progress / 0x27;
                    int b = sb + (db - sb) * progress / 0x27;
                    state->bg_palettes[i].colors[c] = (uint16_t)(r | (g << 5) | (b << 10));

                    s = src_obj[i * 4 + c];
                    d = dst_obj[i * 4 + c];
                    sr = s & 0x1F; sg = (s >> 5) & 0x1F; sb = (s >> 10) & 0x1F;
                    dr = d & 0x1F; dg = (d >> 5) & 0x1F; db = (d >> 10) & 0x1F;
                    r = sr + (dr - sr) * progress / 0x27;
                    g = sg + (dg - sg) * progress / 0x27;
                    b = sb + (db - sb) * progress / 0x27;
                    state->obj_palettes[i].colors[c] = (uint16_t)(r | (g << 5) | (b << 10));
                }
            }
        }

        if (remaining == 0) {
            /* Animation complete: finalize the switch */
            state->hram.scx = 0;
            state->hram.lcdc &= (uint8_t)~0x20; /* Disable window */

            if (state->high_scores_field_switch_dir == 0) {
                /* Red→Blue complete */
                state->hram.lcdc |= 0x08;  /* Set bit 3: use $9C00 tilemap */
                state->high_scores_stage = 1;
            } else {
                /* Blue→Red complete */
                state->hram.lcdc &= (uint8_t)~0x08;  /* Clear bit 3: use $9800 tilemap */
                state->high_scores_stage = 0;
            }

            /* Set final palettes (config override if available) */
            const uint16_t *bg_pals = get_high_scores_bg_palettes(state->high_scores_stage);
            const uint16_t *obj_pals = get_high_scores_obj_palettes(state->high_scores_stage);
            if (state->config && state->config->palettes.palettes_loaded) {
                bg_pals = state->config->palettes.screens[4 + state->high_scores_stage].bg;
                obj_pals = state->config->palettes.screens[4 + state->high_scores_stage].obj;
            }
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 4; c++) {
                    state->bg_palettes[i].colors[c] = bg_pals[i * 4 + c];
                    state->obj_palettes[i].colors[c] = obj_pals[i * 4 + c];
                }
            }
        }
        return;
    }

    /* Check for input to start animation */
    uint8_t newly = state->hram.newly_pressed_buttons;
    if (newly & BTN_RIGHT) {
        if (state->high_scores_stage != 0) return;
        PLAY_SFX(state, "cursor_move", 0x00, 0x03);
        /* Start Red→Blue animation */
        state->sprite_buffer_size = 0;  /* ClearSpriteBuffer */
        state->hram.wx = 0xa5;
        state->hram.wy = 0;
        state->hram.scx = 2;
        state->hram.lcdc |= 0x20;  /* Enable window */
        state->high_scores_field_switch_counter = 0x27;
        state->high_scores_field_switch_dir = 0;
    } else if (newly & BTN_LEFT) {
        if (state->high_scores_stage == 0) return;
        PLAY_SFX(state, "cursor_move", 0x00, 0x03);
        /* Start Blue→Red animation */
        state->sprite_buffer_size = 0;  /* ClearSpriteBuffer */
        state->hram.wx = 0x07;
        state->hram.wy = 0;
        state->hram.scx = 0xa0;
        state->hram.lcdc |= 0x20;  /* Enable window */
        state->hram.lcdc &= (uint8_t)~0x08; /* Clear bit 3 to show red field BG */
        state->high_scores_field_switch_counter = 0x27;
        state->high_scores_field_switch_dir = 1;
    }
}

/*
 * Func_d46f: Update a single name character tile in the tilemap.
 * Called during name entry when the character changes.
 */
static void update_name_entry_tile(GameState *state) {
    if (!state->vram) return;

    uint8_t row = state->high_score_name_row;
    uint8_t col = state->high_score_name_column;
    HighScore *scores = state->high_scores_stage
        ? state->blue_high_scores : state->red_high_scores;

    uint8_t char_val = scores[row].name[col];

    /* Calculate tilemap position: hlCoord(3, 2, vBGMap/vBGWin) + row*3*$20 + col */
    uint16_t base = state->high_scores_stage ? 0x9C00 : 0x9800;
    uint16_t addr = base + 2 * 32 + 3 + (uint16_t)(row * 3 * 32) + col;

    uint8_t top_tile = (uint8_t)(char_val * 2 + 0x90);
    uint8_t bot_tile = (top_tile == 0xFE) ? 0xFE : (uint8_t)(top_tile + 1);
    put_tile_in_vram(state->vram, 0, addr, top_tile);
    put_tile_in_vram(state->vram, 0, addr + 0x20, bot_tile);
}

static void handle_high_scores_screen(GameState *state) {
    /*
     * HandleHighScoresScreen (0xca7f):
     * 6-state machine for high score display, name entry, and field switching.
     */
    switch (state->screen_state) {
        case 0: {
            /* Func_ca8f: Score check & init
             * Generate random ID, check if player score qualifies.
             * In our native version, we skip the actual score comparison
             * (no game has been played yet when coming from title screen).
             * When coming from a finished game, score comparison would happen here. */
            state->high_score_id[0] = gen_random(state);
            state->high_score_id[1] = gen_random(state);
            state->high_score_id[2] = gen_random(state);
            state->high_score_id[3] = gen_random(state);

            /* Compare player score against current top 5
             * ASM (0xca8f): iterates bottom-up (score 5 to 1), advancing while
             * player score is strictly greater. Stops at first equal-or-lower.
             * This places equal scores below existing ones of the same value. */
            HighScore *scores = state->high_scores_stage
                ? state->blue_high_scores : state->red_high_scores;
            int insert_pos = 5;  /* 5 = didn't qualify */
            for (int i = 4; i >= 0; i--) {
                /* Compare 6-byte LE BCD: compare from MSB (index 5) to LSB (index 0) */
                bool higher = false;
                for (int b = 5; b >= 0; b--) {
                    if (state->score[b] > scores[i].points[b]) {
                        higher = true; break;
                    } else if (state->score[b] < scores[i].points[b]) {
                        break;
                    }
                }
                if (higher) {
                    insert_pos = i;  /* beat this score, keep advancing upward */
                } else {
                    break;  /* equal or lower: stop here */
                }
            }

            state->high_score_name_row = (uint8_t)insert_pos;
            state->high_score_name_column = 0;

            /* Shift down existing scores if qualified */
            if (insert_pos < 5) {
                for (int i = 4; i > insert_pos; i--) {
                    scores[i] = scores[i - 1];
                }
                /* Insert player score. N6: ASM only overwrites points+id,
                 * leaving the name inherited from the displaced entry. */
                memcpy(scores[insert_pos].points, state->score, 6);
                memcpy(scores[insert_pos].id, state->high_score_id, 4);
                state->high_score_is_entering_name = 1;
            } else {
                state->high_score_is_entering_name = 0;
            }

            state->screen_state = 1;
            break;
        }

        case 1:
            /* Func_cb14: Load graphics, set LCDC, render scores */
            if (state->vram && !state->gfx_loaded) {
                memset(state->vram, 0, sizeof(*state->vram));
                printf("Loading high scores screen graphics...\n");
                load_screen_assets(SCREEN_HIGH_SCORES, state->vram,
                                   state, state->asset_base_path);
                /* Overlay header/footer decorations (ASM VideoData extra entries).
                 * The full tilemap has decoration art in rows 20-21 and 30-31.
                 * These are copied over the base tilemap at rows 16-17 and 0-1.
                 * This is what creates the Red/Blue field banners. */
                /* Header: tilemap rows 30-31 → vBGMap rows 0-1 */
                memcpy(&state->vram->bg_map[0][0x000],
                       &state->vram->bg_map[0][0x3C0], 0x40);
                memcpy(&state->vram->win_map[0][0x000],
                       &state->vram->win_map[0][0x3C0], 0x40);
                /* Footer: tilemap rows 20-21 → vBGMap rows 16-17 */
                memcpy(&state->vram->bg_map[0][0x200],
                       &state->vram->bg_map[0][0x280], 0x40);
                memcpy(&state->vram->win_map[0][0x200],
                       &state->vram->win_map[0][0x280], 0x40);

                state->hram.lcdc = 0x43;
                state->hram.scx = 0;
                state->hram.scy = 0;
                state->hram.stat_lcdc_xor = 0;  /* No LCDC XOR for high scores */
                /* STAT interrupt for 3-section layout (Func_cb14):
                 * LYC=0x0E (scanline 14): header→scores SCY switch
                 * LYC_SUB=0x82 (scanline 130): scores→footer SCY switch
                 * hStatIntrRoutine=3 (StatIntrToggleHighScoresWindow) */
                state->hram.lyc = 0x0E;
                state->hram.lyc_sub = 0x82;
                state->hram.stat_intr_routine = 3;
                state->hram.next_frame_hblank_scx = 0;
                state->hram.next_frame_hblank_scy = 0;
                state->gfx_loaded = 1;
            }

            state->high_score_name_entry_blink_counter = 0x20;
            state->high_scores_arrow_anim_counter = 0;
            state->high_scores_print_send_selection = 0;

            /* Render scores for both fields into their tilemaps */
            render_all_high_scores(state, state->red_high_scores, 0x9800);
            render_all_high_scores(state, state->blue_high_scores, 0x9C00);

            /* Func_d68a: Show Pokedex completion crown if all caught.
             * Crown tiles ($56, $57) placed at row 0 col 9 (header). */
            {
                int caught = 0;
                for (int i = 0; i < NUM_POKEMON; i++) {
                    if (state->pokedex_flags[i] & (1 << BIT_POKEDEX_MON_CAUGHT))
                        caught++;
                }
                if (caught == NUM_POKEMON) {
                    put_tile_in_vram(state->vram, 0, 0x9800 + 0x09, 0x56);
                    put_tile_in_vram(state->vram, 0, 0x9800 + 0x0A, 0x57);
                    put_tile_in_vram(state->vram, 0, 0x9C00 + 0x09, 0x56);
                    put_tile_in_vram(state->vram, 0, 0x9C00 + 0x0A, 0x57);
                }
            }

            /* Switch tilemap area if Blue */
            if (state->high_scores_stage)
                state->hram.lcdc |= 0x08;

            /* Choose music */
            if (state->high_score_is_entering_name) {
                if (state->high_score_name_row == 0) {
                    PLAY_MUSIC(state, "end_credits", 0x13, 0x01); /* End Credits */
                } else {
                    PLAY_MUSIC(state, "name_entry", 0x13, 0x02); /* Name Entry */
                }
                state->screen_state = 2;  /* Go to name entry */
            } else {
                PLAY_MUSIC(state, "hi_score", 0x10, 0x04);
                state->screen_state = 3;  /* Go to view mode */
            }
            break;

        case 2: {
            /* Func_ccac: Name entry
             * Left/Right: cycle character value (0x0A-0x37, wrapping)
             * A: advance column (0→1→2→done)
             * B: go back one column */
            uint8_t buttons = state->hram.pressed_buttons;
            uint8_t row = state->high_score_name_row;
            HighScore *scores = state->high_scores_stage
                ? state->blue_high_scores : state->red_high_scores;

            /* Character cycling with Left/Right */
            uint8_t *cur_char = &scores[row].name[state->high_score_name_column];
            if (buttons & BTN_RIGHT) {
                uint8_t c = *cur_char;
                c++;
                if (c >= 0x38) c = 0x0A;
                *cur_char = c;
                update_name_entry_tile(state);
                PLAY_SFX(state, "cursor_move", 0x00, 0x03);
            } else if (buttons & BTN_LEFT) {
                uint8_t c = *cur_char;
                if (c <= 0x09 || c == 0x0A) c = 0x37;
                else c--;
                *cur_char = c;
                update_name_entry_tile(state);
                PLAY_SFX(state, "cursor_move", 0x00, 0x03);
            }

            /* A button: advance column */
            uint8_t newly = state->hram.newly_pressed_buttons;
            if (newly & BTN_A) {
                uint8_t col = state->high_score_name_column;
                col++;
                if (col >= 3) {
                    /* Done entering name */
                    PLAY_SFX(state, "high_scores_enter", 0x07, 0x45);
                    state->high_score_is_entering_name = 0;
                    state->screen_state = 3;
                    break;
                }
                state->high_score_name_column = col;
                state->high_score_name_entry_blink_counter = 0x20;
                PLAY_SFX(state, "confirm", 0x00, 0x01);
            }

            /* B button: go back one column */
            if (newly & BTN_B) {
                if (state->high_score_name_column > 0) {
                    state->high_score_name_column--;
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                }
            }

            /* Asterisk blink (Func_d211) */
            if (state->hram.joypad_state & (BTN_RIGHT | BTN_LEFT)) {
                /* Hide asterisk when character is changing */
                state->high_score_name_entry_blink_counter = 0;
            } else {
                state->high_score_name_entry_blink_counter++;
                if (state->high_score_name_entry_blink_counter & 0x20) {
                    /* Show asterisk */
                    uint8_t y = hs_name_y_offsets[row];
                    uint8_t x = hs_name_x_offsets[state->high_score_name_column];
                    load_sprite_data(state, sprite_hs_asterisk, y, x);
                }
            }
            break;
        }

        case 3:
            /* Func_ccb6: View mode
             * Arrow animation, Start to switch fields, A/B to print/send or exit */

            /* Handle Start to switch Red↔Blue */
            handle_high_scores_field_switch(state);

            /* Animate bouncing arrow */
            animate_high_scores_arrow(state);

            {
                uint8_t newly = state->hram.newly_pressed_buttons;
                if (newly & BTN_A) {
                    /* A: exit (inc state twice: 3→5) */
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    state->screen_state = 5;
                } else if (newly & BTN_B) {
                    /* B: exit (inc state twice: 3→5) */
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    state->screen_state = 5;
                } else if (newly & BTN_START) {
                    /* Start: go to print/send UI (inc state once: 3→4) */
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    state->screen_state = 4;
                } else if (state->hram.joypad_state == (BTN_SELECT | BTN_UP) &&
                           (newly & (BTN_SELECT | BTN_UP))) {
                    /* ASM (.asm_ccfb): SELECT+UP held AND at least one newly pressed
                     * → show delete data confirmation dialog */
                    PLAY_SFX(state, "confirm", 0x00, 0x01);
                    state->sprite_buffer_size = 0;
                    state->screen_state = 6;  /* delete confirmation sub-state */
                }
            }
            break;

        case 4: {
            /* Func_cd6c: Print/Send UI (non-functional)
             * Show dialog, Left/Right to select Print/Send, A to confirm, B to exit */

            /* Left/Right to toggle selection */
            uint8_t newly = state->hram.newly_pressed_buttons;
            if (newly & BTN_UP) {
                if (state->high_scores_print_send_selection > 0) {
                    state->high_scores_print_send_selection--;
                    PLAY_SFX(state, "cursor_move", 0x00, 0x03);
                }
            } else if (newly & BTN_DOWN) {
                if (state->high_scores_print_send_selection < 1) {
                    state->high_scores_print_send_selection++;
                    PLAY_SFX(state, "cursor_move", 0x00, 0x03);
                }
            }

            /* Load print/send dialog sprites */
            load_sprite_data(state, sprite_hs_print_send_dialog, 0x3B, 0x47);

            /* Load disabled indicator (neither print nor send available) */
            load_sprite_data(state, sprite_hs_disabled_neither, 0x3B, 0x47);

            /* Load selection cursor */
            if (state->high_scores_print_send_selection < 2) {
                load_sprite_data(state,
                    hs_selection_sprites[state->high_scores_print_send_selection],
                    0x3B, 0x47);
            }

            /* A button: acknowledge (non-functional) */
            if (newly & BTN_A) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                state->screen_state = 3;
            }

            /* B button: go back to view mode */
            if (newly & BTN_B) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                state->screen_state = 3;
            }
            break;
        }

        case 5:
            /* ExitHighScoresScreen (0xd171). N5: ASM calls FadeOut. */
            if (state->fade_direction != 1) {
                save_game(state);
                start_screen_fade_out(state);
                break;
            }
            if (state->fade_counter > 0) break;
            state->gfx_loaded = 0;
            state->current_screen = SCREEN_TITLESCREEN;
            state->screen_state = 0;
            state->fade_direction = 0;
            break;

        case 6: {
            /* Delete Data confirmation dialog (ASM .asm_ccfb, 0xccfb):
             * Show "Delete Data? (A) Okay / (B) Cancel" sprite overlay.
             * Wait for A (confirm delete) or B (cancel).
             * Position: Y=$47, X=$3B (from ASM: ld bc, $473b). */
            load_sprite_data(state, sprite_hs_delete_data, 0x3B, 0x47);

            uint8_t newly = state->hram.newly_pressed_buttons;
            if (newly & BTN_B) {
                /* Cancel: play SFX, return to view mode */
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                state->screen_state = 3;
            } else if (newly & BTN_A) {
                /* Confirm: reset high scores to defaults (CopyInitialHighScores) */
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                reset_high_scores_to_defaults(state);

                /* Reload high score tilemaps (ASM reloads HighScoresTilemap and
                 * HighScoresTilemap2 then re-renders all scores via Func_d361).
                 * We re-render by going through state 1 which reloads everything. */
                state->high_score_is_entering_name = 0;
                state->screen_state = 1;

                /* Save the reset scores immediately (ASM: call SaveData) */
                save_game(state);
            }
            break;
        }
    }
}

/*
 * Starting stages for field select.
 * Index 0 = Red Field, Index 1 = Blue Field.
 * From StartingStages (0xd7d1) in field_select_screen.asm.
 */
static const uint8_t starting_stages[] = {
    STAGE_RED_FIELD_BOTTOM,
    STAGE_BLUE_FIELD_BOTTOM,
};

/*=============================================================================
 * Dynamic Field Select - Table Discovery and Preview Loading
 *
 * Scans tables/{name}/manifest.json for type=="main_field" entries.
 * Builtins (Red/Blue Field) are always at indices 0/1.
 * Custom tables append at index 2+.
 *===========================================================================*/

/*
 * Scan the tables/ directory for main_field manifests.
 * Populates state->field_select with discovered tables.
 */
static void field_select_scan_tables(GameState *state) {
    FieldSelectState *fs = &state->field_select;
    if (fs->scanned) return;

    memset(fs->tables, 0, sizeof(fs->tables));
    fs->num_tables = 0;

    /* Always add builtins at indices 0 and 1 */
    FieldSelectEntry *red = &fs->tables[0];
    strncpy(red->name, "Red Field", sizeof(red->name) - 1);
    strncpy(red->folder, "red_field", sizeof(red->folder) - 1);
    red->starting_stage = STAGE_RED_FIELD_BOTTOM;
    red->is_builtin = true;

    FieldSelectEntry *blue = &fs->tables[1];
    strncpy(blue->name, "Blue Field", sizeof(blue->name) - 1);
    strncpy(blue->folder, "blue_field", sizeof(blue->folder) - 1);
    blue->starting_stage = STAGE_BLUE_FIELD_BOTTOM;
    blue->is_builtin = true;

    fs->num_tables = 2;
    fs->has_custom_tables = false;

#ifdef _WIN32
    /* Scan tables/ directory for subfolders with manifest.json */
    char search_path[520];
    snprintf(search_path, sizeof(search_path), "%stables\\*",
             state->asset_base_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        fs->scanned = true;
        return;
    }

    do {
        /* Skip non-directories and . / .. */
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;

        /* Skip known builtins (already added) */
        if (strcmp(fd.cFileName, "red_field") == 0 ||
            strcmp(fd.cFileName, "blue_field") == 0) {
            /* Check if builtins have custom preview overrides */
            char manifest_path[520];
            snprintf(manifest_path, sizeof(manifest_path),
                     "%stables/%s/manifest.json",
                     state->asset_base_path, fd.cFileName);
            FILE *f = fopen(manifest_path, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *json = (char *)malloc(sz + 1);
                if (json) {
                    fread(json, 1, sz, f);
                    json[sz] = '\0';
                    cJSON *root = cJSON_Parse(json);
                    if (root) {
                        /* Check for preview override on builtin */
                        int idx = (strcmp(fd.cFileName, "red_field") == 0) ? 0 : 1;
                        FieldSelectEntry *entry = &fs->tables[idx];

                        cJSON *preview = cJSON_GetObjectItem(root, "preview");
                        if (preview && cJSON_IsString(preview)) {
                            snprintf(entry->preview_path,
                                     sizeof(entry->preview_path),
                                     "%stables/%s/%s",
                                     state->asset_base_path,
                                     fd.cFileName,
                                     preview->valuestring);
                            entry->has_preview = true;
                            fs->has_custom_tables = true;
                        }

                        cJSON *pal = cJSON_GetObjectItem(root, "preview_palette");
                        if (pal && cJSON_IsArray(pal) &&
                            cJSON_GetArraySize(pal) == 4) {
                            for (int c = 0; c < 4; c++) {
                                cJSON *color = cJSON_GetArrayItem(pal, c);
                                if (color && cJSON_IsArray(color) &&
                                    cJSON_GetArraySize(color) == 3) {
                                    int r = cJSON_GetArrayItem(color, 0)->valueint;
                                    int g = cJSON_GetArrayItem(color, 1)->valueint;
                                    int b = cJSON_GetArrayItem(color, 2)->valueint;
                                    entry->palette[c] = (uint16_t)(
                                        (r & 0x1F) |
                                        ((g & 0x1F) << 5) |
                                        ((b & 0x1F) << 10));
                                }
                            }
                            entry->has_palette = true;
                            fs->has_custom_tables = true;
                        }

                        cJSON_Delete(root);
                    }
                    free(json);
                }
                fclose(f);
            }
            continue;
        }

        /* Skip non-main_field bonus stage folders */
        if (strcmp(fd.cFileName, "gengar_bonus") == 0 ||
            strcmp(fd.cFileName, "mewtwo_bonus") == 0 ||
            strcmp(fd.cFileName, "meowth_bonus") == 0 ||
            strcmp(fd.cFileName, "diglett_bonus") == 0 ||
            strcmp(fd.cFileName, "seel_bonus") == 0)
            continue;

        if (fs->num_tables >= MAX_FIELD_SELECT_TABLES) {
            fprintf(stderr, "[FieldSelect] Too many tables (max %d), "
                    "ignoring '%s'\n", MAX_FIELD_SELECT_TABLES, fd.cFileName);
            continue;
        }

        /* Read this folder's manifest.json */
        char manifest_path[520];
        snprintf(manifest_path, sizeof(manifest_path),
                 "%stables/%s/manifest.json",
                 state->asset_base_path, fd.cFileName);

        FILE *f = fopen(manifest_path, "rb");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *json = (char *)malloc(sz + 1);
        if (!json) { fclose(f); continue; }
        fread(json, 1, sz, f);
        json[sz] = '\0';
        fclose(f);

        cJSON *root = cJSON_Parse(json);
        free(json);
        if (!root) continue;

        /* Only accept type == "main_field" */
        cJSON *type = cJSON_GetObjectItem(root, "type");
        if (!type || !cJSON_IsString(type) ||
            strcmp(type->valuestring, "main_field") != 0) {
            cJSON_Delete(root);
            continue;
        }

        FieldSelectEntry *entry = &fs->tables[fs->num_tables];
        memset(entry, 0, sizeof(*entry));

        /* Name */
        cJSON *name = cJSON_GetObjectItem(root, "name");
        if (name && cJSON_IsString(name))
            strncpy(entry->name, name->valuestring,
                    sizeof(entry->name) - 1);
        else
            strncpy(entry->name, fd.cFileName, sizeof(entry->name) - 1);

        strncpy(entry->folder, fd.cFileName, sizeof(entry->folder) - 1);

        /* Starting stage: read from stages.bottom.id */
        entry->starting_stage = STAGE_RED_FIELD_BOTTOM; /* fallback */
        cJSON *stages = cJSON_GetObjectItem(root, "stages");
        if (stages) {
            cJSON *bottom = cJSON_GetObjectItem(stages, "bottom");
            if (bottom) {
                cJSON *sid = cJSON_GetObjectItem(bottom, "id");
                if (sid && cJSON_IsNumber(sid))
                    entry->starting_stage = (uint8_t)sid->valueint;
            }
        }

        /* Preview PNG */
        cJSON *preview = cJSON_GetObjectItem(root, "preview");
        if (preview && cJSON_IsString(preview)) {
            snprintf(entry->preview_path, sizeof(entry->preview_path),
                     "%stables/%s/%s",
                     state->asset_base_path,
                     fd.cFileName,
                     preview->valuestring);
            entry->has_preview = true;
        }

        /* Preview palette */
        cJSON *pal = cJSON_GetObjectItem(root, "preview_palette");
        if (pal && cJSON_IsArray(pal) && cJSON_GetArraySize(pal) == 4) {
            for (int c = 0; c < 4; c++) {
                cJSON *color = cJSON_GetArrayItem(pal, c);
                if (color && cJSON_IsArray(color) &&
                    cJSON_GetArraySize(color) == 3) {
                    int r = cJSON_GetArrayItem(color, 0)->valueint;
                    int g = cJSON_GetArrayItem(color, 1)->valueint;
                    int b = cJSON_GetArrayItem(color, 2)->valueint;
                    entry->palette[c] = (uint16_t)(
                        (r & 0x1F) |
                        ((g & 0x1F) << 5) |
                        ((b & 0x1F) << 10));
                }
            }
            entry->has_palette = true;
        }

        entry->is_builtin = false;
        fs->num_tables++;
        fs->has_custom_tables = true;

        printf("[FieldSelect] Found custom table: '%s' (stage %d)\n",
               entry->name, entry->starting_stage);

        cJSON_Delete(root);
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
#endif /* _WIN32 */

    fs->scanned = true;
    printf("[FieldSelect] %d table(s) found, custom=%d\n",
           fs->num_tables, fs->has_custom_tables);
}

/*
 * Save the original tilemap/bgattr entries for both preview regions.
 * Called once after the baked field_select assets are loaded into VRAM.
 */
static void field_select_save_originals(GameState *state) {
    FieldSelectState *fs = &state->field_select;
    if (fs->originals_saved) return;

    VirtualVRAM *vram = state->vram;
    if (!vram) return;

    for (int row = 0; row < FS_SAVE_NUM_ROWS; row++) {
        for (int col = 0; col < FS_PREVIEW_NUM_COLS; col++) {
            int idx = row * FS_PREVIEW_NUM_COLS + col;

            /* Left preview + border + label */
            int left_off = (FS_PREVIEW_START_ROW + row) * 32 +
                           (FS_PREVIEW_LEFT_COL + col);
            fs->orig_left_tilemap[idx] = vram->bg_map[0][left_off];
            fs->orig_left_bgattr[idx]  = vram->bg_map[1][left_off];

            /* Right preview + border + label */
            int right_off = (FS_PREVIEW_START_ROW + row) * 32 +
                            (FS_PREVIEW_RIGHT_COL + col);
            fs->orig_right_tilemap[idx] = vram->bg_map[0][right_off];
            fs->orig_right_bgattr[idx]  = vram->bg_map[1][right_off];
        }
    }

    fs->originals_saved = true;
}

/*
 * Convert a signed tile index (for LCDC mode $43, bit 4=0) to a
 * VRAM address in the $8000-$97FF range.
 */
static uint16_t signed_tile_addr(uint8_t index) {
    return (uint16_t)(0x9000 + (int8_t)index * 16);
}

/*
 * Write a preview image's tiles into VRAM bank 1, remapping from
 * the PNG's tile layout (src_cols tiles per row) to the display
 * layout (FS_PREVIEW_NUM_COLS tiles per row).
 *
 * slot 0 = left preview (base index 0x00)
 * slot 1 = right preview (base index 0x7E)
 */
static void write_preview_tiles_to_vram(VirtualVRAM *vram, int slot,
                                         const uint8_t *tile_data,
                                         int src_cols, int src_rows) {
    uint8_t base_index = (slot == 0) ? 0x00 : 0x7E;
    int dst_cols = FS_PREVIEW_NUM_COLS;
    int dst_rows = FS_SAVE_NUM_ROWS;
    uint8_t blank[16] = {0};

    for (int row = 0; row < dst_rows; row++) {
        for (int col = 0; col < dst_cols; col++) {
            int dst_idx = row * dst_cols + col;
            uint8_t tile_idx = (uint8_t)(base_index + dst_idx);
            uint16_t addr = signed_tile_addr(tile_idx);

            if (row < src_rows && col < src_cols) {
                int src_idx = row * src_cols + col;
                vram_write(vram, 1, addr, tile_data + src_idx * 16, 16);
            } else {
                vram_write(vram, 1, addr, blank, 16);
            }
        }
    }
}

/*
 * Patch the tilemap for one preview slot to show custom tiles from bank 1.
 * Covers all 13 rows (preview + border + label, rows 3-15).
 */
static void patch_preview_custom(GameState *state, int slot, uint8_t bg_palette) {
    VirtualVRAM *vram = state->vram;
    int start_col = (slot == 0) ? FS_PREVIEW_LEFT_COL : FS_PREVIEW_RIGHT_COL;
    uint8_t base_index = (slot == 0) ? 0x00 : 0x7E;

    for (int row = 0; row < FS_SAVE_NUM_ROWS; row++) {
        for (int col = 0; col < FS_PREVIEW_NUM_COLS; col++) {
            int map_off = (FS_PREVIEW_START_ROW + row) * 32 +
                          (start_col + col);
            int idx = row * FS_PREVIEW_NUM_COLS + col;
            vram->bg_map[0][map_off] = (uint8_t)(base_index + idx);
            vram->bg_map[1][map_off] = 0x08 | (bg_palette & 0x07);
        }
    }
}

/*
 * Restore a slot's tilemap/bgattr from saved originals.
 * Covers all 13 rows (preview + border bottom + label, rows 3-15).
 * source_slot selects WHICH originals to use:
 *   0 = left originals (Red Field's native data)
 *   1 = right originals (Blue Field's native data)
 */
static void restore_preview_slot(GameState *state, int target_slot,
                                   int source_slot) {
    VirtualVRAM *vram = state->vram;
    FieldSelectState *fs = &state->field_select;
    int start_col = (target_slot == 0) ? FS_PREVIEW_LEFT_COL
                                        : FS_PREVIEW_RIGHT_COL;
    const uint8_t *src_tilemap = (source_slot == 0)
        ? fs->orig_left_tilemap : fs->orig_right_tilemap;
    const uint8_t *src_bgattr = (source_slot == 0)
        ? fs->orig_left_bgattr : fs->orig_right_bgattr;

    for (int row = 0; row < FS_SAVE_NUM_ROWS; row++) {
        for (int col = 0; col < FS_PREVIEW_NUM_COLS; col++) {
            int map_off = (FS_PREVIEW_START_ROW + row) * 32 +
                          (start_col + col);
            int idx = row * FS_PREVIEW_NUM_COLS + col;
            vram->bg_map[0][map_off] = src_tilemap[idx];
            vram->bg_map[1][map_off] = src_bgattr[idx];
        }
    }
}

/*
 * Load preview images for the two currently visible slots.
 * Called when visible_offset changes or on first load with custom tables.
 */
static void field_select_load_previews(GameState *state) {
    FieldSelectState *fs = &state->field_select;
    VirtualVRAM *vram = state->vram;
    if (!vram) return;

    for (int slot = 0; slot < 2; slot++) {
        int table_idx = fs->visible_offset + slot;

        if (table_idx >= fs->num_tables) {
            /* No table in this slot — show blank */
            write_preview_tiles_to_vram(vram, slot, NULL, 0, 0);
            patch_preview_custom(state, slot, 3);

            /* Set palette 3 to grayscale */
            state->bg_palettes[3].colors[0] = 0x7FFF;
            state->bg_palettes[3].colors[1] = 0x56B5;
            state->bg_palettes[3].colors[2] = 0x294A;
            state->bg_palettes[3].colors[3] = 0x0000;
            continue;
        }

        FieldSelectEntry *entry = &fs->tables[table_idx];

        if (entry->is_builtin && !entry->has_preview) {
            /* Builtin: restore from its native slot's originals.
             * Red (table 0) → native slot 0 (left originals)
             * Blue (table 1) → native slot 1 (right originals)
             * This handles the case where Blue is in the left slot
             * or Red is in the right slot after scrolling. */
            int native_slot = table_idx;  /* 0=Red, 1=Blue */
            restore_preview_slot(state, slot, native_slot);
        } else if (entry->has_preview) {
            /* Get PNG dimensions for tile remapping */
            int png_w = 0, png_h = 0, png_comp = 0;
            stbi_info(entry->preview_path, &png_w, &png_h, &png_comp);
            int src_cols = png_w / 8;
            int src_rows = png_h / 8;
            if (src_cols < 1) src_cols = FS_PREVIEW_NUM_COLS;
            if (src_rows < 1) src_rows = FS_SAVE_NUM_ROWS;

            /* Load custom preview PNG */
            size_t tile_size = 0;
            uint8_t *tile_data = tiles_from_png(entry->preview_path,
                                                &tile_size);
            if (tile_data) {
                write_preview_tiles_to_vram(vram, slot, tile_data,
                                            src_cols, src_rows);
                free(tile_data);
            } else {
                /* Preview load failed — fill with placeholder */
                fprintf(stderr, "[FieldSelect] Failed to load preview: %s\n",
                        entry->preview_path);
                write_preview_tiles_to_vram(vram, slot, NULL, 0, 0);
            }

            /* Use custom palette or default grayscale */
            uint8_t pal_idx = (slot == 0) ? 3 : 4;
            if (entry->has_palette) {
                for (int c = 0; c < 4; c++)
                    state->bg_palettes[pal_idx].colors[c] =
                        entry->palette[c];
            } else {
                /* Grayscale fallback */
                state->bg_palettes[pal_idx].colors[0] = 0x7FFF;
                state->bg_palettes[pal_idx].colors[1] = 0x56B5;
                state->bg_palettes[pal_idx].colors[2] = 0x294A;
                state->bg_palettes[pal_idx].colors[3] = 0x0000;
            }
            patch_preview_custom(state, slot, pal_idx);
        } else {
            /* Custom table without preview — blank placeholder */
            write_preview_tiles_to_vram(vram, slot, NULL, 0, 0);

            uint8_t pal_idx = (slot == 0) ? 3 : 4;
            state->bg_palettes[pal_idx].colors[0] = 0x7FFF;
            state->bg_palettes[pal_idx].colors[1] = 0x56B5;
            state->bg_palettes[pal_idx].colors[2] = 0x294A;
            state->bg_palettes[pal_idx].colors[3] = 0x0000;
            patch_preview_custom(state, slot, pal_idx);
        }
    }

    fs->needs_reload = false;
}

/*
 * Load the high-scores arrow tiles into VRAM and set OBJ palette 1 to yellow.
 * Tiles 0x7A-0x7D are the same indices used by the high scores screen arrows.
 * Called once during field select init when custom tables are present.
 */
static void field_select_load_arrow_tiles(GameState *state) {
    /* Load arrow tiles from the high scores base PNG into tile slots 0x7A-0x7D.
     * The high_scores_base_gameboy.png contains these arrow tiles. */
    char path[512];
    snprintf(path, sizeof(path), "%sgfx/high_scores/high_scores_base_gameboy.png",
             state->asset_base_path);
    size_t tile_count = 0;
    uint8_t *tiles = tiles_from_png(path, &tile_count);
    if (tiles && tile_count > 0x7D) {
        /* Write tiles 0x7A-0x7D (4 tiles, 64 bytes) */
        vram_write(state->vram, 0, 0x8000 + 0x7A * 16,
                   tiles + 0x7A * 16, 4 * 16);
        free(tiles);
    } else {
        if (tiles) free(tiles);
    }

    /* Set OBJ palette 1 to the high-scores yellow arrow palette */
    state->obj_palettes[1].colors[0] = 0x7FFF;  /* RGB(31,31,31) white */
    state->obj_palettes[1].colors[1] = 0x13BF;  /* RGB(31,29, 4) yellow */
    state->obj_palettes[1].colors[2] = 0x025D;  /* RGB(29,18, 0) dark gold */
    state->obj_palettes[1].colors[3] = 0x0000;  /* RGB( 0, 0, 0) black */
}

/*
 * Draw scroll indicator arrows using the 4-sprite high-scores arrow composites.
 * Bounces with a 40-frame cycle matching the high scores style.
 */
static void field_select_draw_scroll_arrows(GameState *state) {
    FieldSelectState *fs = &state->field_select;

    /* Advance bounce counter (40-frame cycle) */
    uint8_t counter = state->high_scores_arrow_anim_counter;
    counter++;
    if (counter >= 0x28) counter = 0;
    state->high_scores_arrow_anim_counter = counter;

    /* Y base centers the arrow vertically with the preview area (rows 3-12).
     * Arrow visual span = base to base+16, center = base+8.
     * Preview center = screen Y 60, so base = 52 = 0x34. */
    const uint8_t arrow_y = 0x34;

    /* X bases from the high-scores offset tables — same screen-edge positions
     * with the proper heartbeat bounce animation (40-frame cycle). */
    if (fs->visible_offset > 0) {
        uint8_t x = hs_left_arrow_x_offsets[counter];
        load_sprite_data(state, sprite_hs_arrow_left, arrow_y, x);
    }

    if (fs->visible_offset + 2 < fs->num_tables) {
        uint8_t x = hs_right_arrow_x_offsets[counter];
        load_sprite_data(state, sprite_hs_arrow_right, arrow_y, x);
    }
}

static void handle_field_select_screen(GameState *state) {
    /*
     * HandleFieldSelectScreen (0xd6d3):
     * Choose a field to play. Supports dynamic table discovery.
     *
     * When only 2 builtin tables with no custom previews: pixel-identical
     * to original. When custom tables exist: dynamic preview loading with
     * LEFT/RIGHT scrolling.
     *
     * State 0: LoadFieldSelectScreen - load graphics, scan tables, init
     * State 1: ChooseFieldToPlay - cursor + blinking border + scrolling
     * State 2: ExitFieldSelectScreen - confirmation blink, then transition
     */
    FieldSelectState *fs = &state->field_select;

    switch (state->screen_state) {
        case 0:
            /* LoadFieldSelectScreen (0xd6dd) */
            if (state->vram && !state->gfx_loaded) {
                memset(state->vram, 0, sizeof(*state->vram));
                printf("Loading field select screen graphics...\n");
                load_screen_assets(SCREEN_FIELD_SELECT, state->vram,
                                   state, state->asset_base_path);
                state->hram.lcdc = 0x43;
                state->hram.scx = 0;
                state->hram.scy = 0;
                state->gfx_loaded = 1;

                /* Scan for custom tables */
                field_select_scan_tables(state);

                /* Reset cursor/offset to defaults before loading previews */
                fs->cursor_index = 0;
                fs->visible_offset = 0;
                state->selected_field_index = 0;

                /* Re-save originals from the freshly-loaded baked tilemap */
                fs->originals_saved = false;
                field_select_save_originals(state);

                /* If custom tables exist, load preview images and arrow tiles */
                if (fs->has_custom_tables) {
                    field_select_load_previews(state);
                    field_select_load_arrow_tiles(state);
                }
            }
            state->field_select_blinking_border_frame = 8;
            state->field_select_border_anim_step = 0;
            state->high_scores_arrow_anim_counter = 0;
            PLAY_MUSIC(state, "field_select", 0x12, 0x03);
            state->screen_state = 1;
            break;
        case 1: {
            /* ChooseFieldToPlay (0xd74e):
             * L/R navigation with scrolling support for 3+ tables.
             * A or B pressed → go to state 2 with confirmation blink. */
            uint8_t buttons = state->hram.pressed_buttons;
            uint8_t old_cursor = fs->cursor_index;
            uint8_t old_offset = fs->visible_offset;

            if ((buttons & BTN_LEFT) && fs->cursor_index > 0) {
                fs->cursor_index--;
                PLAY_SFX(state, "field_select_left", 0x00, 0x3C);

                /* Scroll left if cursor moved before visible window */
                if (fs->cursor_index < fs->visible_offset)
                    fs->visible_offset = fs->cursor_index;
            } else if ((buttons & BTN_RIGHT) &&
                       fs->cursor_index < fs->num_tables - 1) {
                fs->cursor_index++;
                PLAY_SFX(state, "field_select_right", 0x00, 0x3D);

                /* Scroll right if cursor moved past visible window */
                if (fs->cursor_index > fs->visible_offset + 1)
                    fs->visible_offset = fs->cursor_index - 1;
            }

            /* Reload previews if visible window changed */
            if (fs->visible_offset != old_offset && fs->has_custom_tables) {
                fs->needs_reload = true;
            }

            if (fs->needs_reload) {
                field_select_load_previews(state);
            }

            /* Compute local slot (0 or 1) for border sprite position */
            uint8_t local_slot = fs->cursor_index - fs->visible_offset;
            if (local_slot > 1) local_slot = 1;
            state->selected_field_index = local_slot;

            /* Draw scroll arrows when more than 2 tables exist */
            if (fs->num_tables > 2) {
                field_select_draw_scroll_arrows(state);
            }

            animate_field_select_border(state, false);

            uint8_t newly = state->hram.newly_pressed_buttons;
            if (newly & (BTN_A | BTN_B)) {
                PLAY_SFX(state, "confirm", 0x00, 0x01);
                state->field_select_pressed_button = newly & (BTN_A | BTN_B);
                state->field_select_blinking_border_timer = 0x18;
                state->field_select_blinking_border_frame = 1;
                state->screen_state = 2;
            }
            break;
        }
        case 2:
            /* ExitFieldSelectScreen (0xd774) */
            if (state->field_select_pressed_button & BTN_A) {
                animate_field_select_border(state, true);
                if (state->field_select_blinking_border_timer > 0) {
                    state->field_select_blinking_border_timer--;
                    break;
                }
            }
            /* Transition with FadeOut */
            if (state->fade_direction != 1) {
                start_screen_fade_out(state);
                break;
            }
            if (state->fade_counter > 0) break;
            state->fade_direction = 0;
            if (state->field_select_pressed_button & BTN_A) {
                /* Use the selected table's starting stage */
                uint8_t table_idx = fs->cursor_index;
                if (table_idx >= fs->num_tables)
                    table_idx = 0;
                state->current_stage =
                    fs->tables[table_idx].starting_stage;
                state->saved_game = 0;
                save_game(state);
                state->loading_saved_game = 0;
                state->gfx_loaded = 0;
                state->current_screen = SCREEN_PINBALL_GAME;
                state->screen_state = 0;
            } else {
                /* B was pressed - go back to title */
                state->gfx_loaded = 0;
                state->current_screen = SCREEN_TITLESCREEN;
                state->screen_state = 0;
            }
            break;
    }
}
