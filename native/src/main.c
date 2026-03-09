/*
 * Pokemon Pinball - Native C Reconstruction
 * Main entry point
 *
 * Original: Game Boy Color (LR35902, RGBDS assembly)
 * This file: SDL2-based native Windows build
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "platform/platform.h"
#include "renderer/renderer.h"
#include "renderer/vram.h"
#include "audio/audio.h"
#include "game/game_state.h"
#include "game/main_loop.h"
#include "game/joypad.h"
#include "game/save.h"
#include <string.h>

/* GBC runs at ~59.7275 Hz */
#define GBC_FRAME_RATE 59.7275
#define FRAME_TIME_MS  (1000.0 / GBC_FRAME_RATE)

/* Native resolution */
#define GBC_SCREEN_W 160
#define GBC_SCREEN_H 144

/* Default scale factor */
#define DEFAULT_SCALE 4

int main(int argc, char *argv[]) {
    int scale = DEFAULT_SCALE;
    if (argc > 1) {
        scale = atoi(argv[1]);
        if (scale < 1 || scale > 10) scale = DEFAULT_SCALE;
    }

    /* Initialize platform (SDL2 window, audio, input) */
    Platform *platform = platform_init(GBC_SCREEN_W, GBC_SCREEN_H, scale);
    if (!platform) {
        fprintf(stderr, "Failed to initialize platform\n");
        return 1;
    }

    /* Initialize renderer */
    Renderer *renderer = renderer_init(platform, GBC_SCREEN_W, GBC_SCREEN_H);
    if (!renderer) {
        fprintf(stderr, "Failed to initialize renderer\n");
        platform_shutdown(platform);
        return 1;
    }

    /* Initialize audio engine */
    AudioEngine *audio = audio_init(platform);

    /* Load Pikachu PCM sound clips.
     * This is the best we can do — the original uses raw GBC wave channel PCM
     * streaming (PlayPikachuSoundClip at bank 0x50, writes directly to NR30-NR34
     * and wave RAM FF30-FF3F) which can't be reproduced through the bytecode
     * SFX system. We load the WAV files and mix them into the audio output. */
    {
        char *sdl_base = SDL_GetBasePath();
        if (sdl_base && audio) {
            char pcm_base[512];
            snprintf(pcm_base, sizeof(pcm_base), "%s..\\..\\..\\", sdl_base);
            audio_init_pcm(audio, pcm_base);
            SDL_free(sdl_base);
        }
    }

    /* Initialize game state (equivalent to clearing WRAM + HRAM) */
    GameState *state = game_state_init();
    if (!state) {
        fprintf(stderr, "Failed to initialize game state\n");
        renderer_shutdown(renderer);
        platform_shutdown(platform);
        return 1;
    }

    /* Create virtual VRAM and attach to game state */
    VirtualVRAM *vram = vram_create();
    if (!vram) {
        fprintf(stderr, "Failed to create virtual VRAM\n");
        game_state_free(state);
        renderer_shutdown(renderer);
        platform_shutdown(platform);
        return 1;
    }
    state->vram = vram;
    state->audio = audio;
    renderer_set_vram(renderer, vram);

    /* Compute asset base path: check if assets are next to the exe (deployed),
     * otherwise go up 3 levels for dev builds (native/build/<config>/). */
    {
        char *sdl_base = SDL_GetBasePath();
        char base_dir[260] = {0};
        if (sdl_base) {
            snprintf(base_dir, sizeof(base_dir), "%s", sdl_base);
            SDL_free(sdl_base);
        } else {
            /* Fallback: try argv[0] */
            const char *exe_path = argv[0];
            const char *last_sep = NULL;
            for (const char *p = exe_path; *p; p++) {
                if (*p == '/' || *p == '\\') last_sep = p;
            }
            if (last_sep) {
                size_t dir_len = (size_t)(last_sep - exe_path);
                if (dir_len >= sizeof(base_dir)) dir_len = sizeof(base_dir) - 1;
                memcpy(base_dir, exe_path, dir_len);
                base_dir[dir_len] = '\0';
            } else {
                base_dir[0] = '.';
                base_dir[1] = '\0';
            }
        }

        /* Check if gfx/ folder exists next to the exe (deployed/release layout) */
        char test_path[260];
        snprintf(test_path, sizeof(test_path), "%sgfx", base_dir);
        FILE *test_f = fopen(test_path, "r");
        if (test_f) {
            fclose(test_f);
            snprintf(state->asset_base_path, sizeof(state->asset_base_path),
                     "%s", base_dir);
        } else {
            /* Check if gfx/ is a directory by trying to open a known file in it */
            snprintf(test_path, sizeof(test_path), "%sgfx\\copyright_text.png", base_dir);
            test_f = fopen(test_path, "rb");
            if (test_f) {
                fclose(test_f);
                snprintf(state->asset_base_path, sizeof(state->asset_base_path),
                         "%s", base_dir);
            } else {
                /* Dev build: exe is at native/build/<config>/, go up 3 levels */
                snprintf(state->asset_base_path, sizeof(state->asset_base_path),
                         "%s..\\..\\..\\", base_dir);
            }
        }
        printf("Asset base path: %s\n", state->asset_base_path);
    }

    /* Save data is loaded during copyright screen exit (FadeOutCopyrightScreenAndLoadData),
     * matching ASM lifecycle ordering. See handle_copyright_screen state 2. */

    /* Main loop */
    double last_time = platform_get_time_ms(platform);
    double accumulator = 0.0;
    bool running = true;

    while (running) {
        double current_time = platform_get_time_ms(platform);
        double elapsed = current_time - last_time;
        last_time = current_time;
        accumulator += elapsed;

        /* Cap accumulator to prevent audio burst after slow frames (e.g. loading) */
        if (accumulator > FRAME_TIME_MS * 4)
            accumulator = FRAME_TIME_MS * 4;

        /* Poll input events */
        if (!platform_poll_events(platform)) {
            running = false;
            break;
        }

        /* Fixed timestep game update */
        while (accumulator >= FRAME_TIME_MS) {
            /* Read joypad (equivalent to ReadJoypad in home/joypad.asm) */
            joypad_update(state, platform);

            /* Increment frame counter before game logic (ASM: VBlank handler
             * increments hFrameCounter before game logic runs) */
            state->hram.frame_counter++;

            /* Run one frame of game logic (equivalent to Main in home.asm) */
            main_loop_update(state);

            /* Update audio engine (synthesize one frame of audio) */
            audio_update(audio);

            accumulator -= FRAME_TIME_MS;
        }

        /* Render current frame */
        renderer_begin_frame(renderer);
        renderer_draw_game(renderer, state);
        renderer_end_frame(renderer);
    }

    /* Cleanup */
    game_state_free(state);
    vram_free(vram);
    if (audio) audio_shutdown(audio);
    renderer_shutdown(renderer);
    platform_shutdown(platform);

    return 0;
}
