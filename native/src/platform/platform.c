#include "platform/platform.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

struct Platform {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioDeviceID audio_device;
    int screen_w;
    int screen_h;
    int scale;
    uint8_t joypad_state;  /* Current button state mapped from keyboard */
    bool f1_pressed;       /* Edge-detected F1 toggle for debug overlay */
    bool fullscreen_toggle; /* Edge-detected F11 toggle for fullscreen */
    bool fullscreen;       /* Current fullscreen state */
    bool mouse_clicked;    /* Left mouse button was clicked */
    int mouse_gbc_x;       /* Click position in GBC logical coords */
    int mouse_gbc_y;
};

/* Default keyboard mapping (matches original GBC controls) */
typedef struct {
    SDL_Scancode key;
    uint8_t button;
} KeyMapping;

static const KeyMapping default_keymap[] = {
    { SDL_SCANCODE_Z,      0x01 },  /* A button */
    { SDL_SCANCODE_X,      0x02 },  /* B button */
    { SDL_SCANCODE_RSHIFT, 0x04 },  /* Select */
    { SDL_SCANCODE_RETURN, 0x08 },  /* Start */
    { SDL_SCANCODE_RIGHT,  0x10 },  /* D-Right */
    { SDL_SCANCODE_LEFT,   0x20 },  /* D-Left */
    { SDL_SCANCODE_UP,     0x40 },  /* D-Up */
    { SDL_SCANCODE_DOWN,   0x80 },  /* D-Down */
};
#define NUM_KEY_MAPPINGS (sizeof(default_keymap) / sizeof(default_keymap[0]))

Platform *platform_init(int screen_w, int screen_h, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return NULL;
    }

    Platform *p = calloc(1, sizeof(Platform));
    if (!p) return NULL;

    p->screen_w = screen_w;
    p->screen_h = screen_h;
    p->scale = scale;

    p->window = SDL_CreateWindow(
        "Pokemon Pinball",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screen_w * scale, screen_h * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!p->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(p);
        SDL_Quit();
        return NULL;
    }

    p->renderer = SDL_CreateRenderer(p->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!p->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(p->window);
        free(p);
        SDL_Quit();
        return NULL;
    }

    /* Nearest-neighbor scaling for crisp pixels */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    /* Initialize audio */
    SDL_AudioSpec desired, obtained;
    SDL_memset(&desired, 0, sizeof(desired));
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = NULL;  /* We'll use SDL_QueueAudio */

    p->audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (p->audio_device > 0) {
        SDL_PauseAudioDevice(p->audio_device, 0);  /* Start audio */
    }

    return p;
}

void platform_shutdown(Platform *p) {
    if (!p) return;
    if (p->audio_device > 0) SDL_CloseAudioDevice(p->audio_device);
    if (p->renderer) SDL_DestroyRenderer(p->renderer);
    if (p->window) SDL_DestroyWindow(p->window);
    free(p);
    SDL_Quit();
}

bool platform_poll_events(Platform *p) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            return false;
        }
        if (event.type == SDL_KEYDOWN && !event.key.repeat &&
            event.key.keysym.scancode == SDL_SCANCODE_F1) {
            p->f1_pressed = true;
        }
        if (event.type == SDL_KEYDOWN && !event.key.repeat &&
            event.key.keysym.scancode == SDL_SCANCODE_F11) {
            p->fullscreen_toggle = true;
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            /* Translate click position using viewport rect for correct coords
             * in fullscreen/resized window modes */
            int vx, vy, vw, vh;
            platform_get_viewport_rect(p, p->screen_w, p->screen_h, &vx, &vy, &vw, &vh);
            if (vw > 0 && vh > 0) {
                p->mouse_gbc_x = (event.button.x - vx) * p->screen_w / vw;
                p->mouse_gbc_y = (event.button.y - vy) * p->screen_h / vh;
                /* Clamp to valid range */
                if (p->mouse_gbc_x < 0) p->mouse_gbc_x = 0;
                if (p->mouse_gbc_x >= p->screen_w) p->mouse_gbc_x = p->screen_w - 1;
                if (p->mouse_gbc_y < 0) p->mouse_gbc_y = 0;
                if (p->mouse_gbc_y >= p->screen_h) p->mouse_gbc_y = p->screen_h - 1;
            } else {
                p->mouse_gbc_x = event.button.x / p->scale;
                p->mouse_gbc_y = event.button.y / p->scale;
            }
            p->mouse_clicked = true;
        }
    }

    /* Update joypad state from keyboard */
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    p->joypad_state = 0;
    for (int i = 0; i < (int)NUM_KEY_MAPPINGS; i++) {
        if (keys[default_keymap[i].key]) {
            p->joypad_state |= default_keymap[i].button;
        }
    }

    return true;
}

uint8_t platform_get_joypad_state(Platform *p) {
    return p->joypad_state;
}

void platform_present_frame(Platform *p, const uint32_t *framebuffer) {
    SDL_Texture *tex = SDL_CreateTexture(p->renderer,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
        p->screen_w, p->screen_h);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, framebuffer, p->screen_w * sizeof(uint32_t));
        SDL_RenderClear(p->renderer);
        SDL_RenderCopy(p->renderer, tex, NULL, NULL);
        SDL_RenderPresent(p->renderer);
        SDL_DestroyTexture(tex);
    }
}

double platform_get_time_ms(Platform *p) {
    (void)p;
    return (double)SDL_GetPerformanceCounter() /
           (double)SDL_GetPerformanceFrequency() * 1000.0;
}

void platform_audio_submit(Platform *p, const int16_t *samples, int num_samples) {
    if (p->audio_device > 0) {
        SDL_QueueAudio(p->audio_device, samples, num_samples * sizeof(int16_t) * 2);
    }
}

void platform_audio_clear(Platform *p) {
    if (p->audio_device > 0) {
        SDL_ClearQueuedAudio(p->audio_device);
    }
}

void *platform_get_sdl_renderer(Platform *p) {
    return p->renderer;
}

int platform_get_scale(Platform *p) {
    return p->scale;
}

bool platform_consume_f1_toggle(Platform *p) {
    bool was = p->f1_pressed;
    p->f1_pressed = false;
    return was;
}

bool platform_consume_mouse_click(Platform *p, int *out_x, int *out_y) {
    if (!p->mouse_clicked) return false;
    p->mouse_clicked = false;
    if (out_x) *out_x = p->mouse_gbc_x;
    if (out_y) *out_y = p->mouse_gbc_y;
    return true;
}

bool platform_consume_fullscreen_toggle(Platform *p) {
    bool was = p->fullscreen_toggle;
    p->fullscreen_toggle = false;
    return was;
}

void platform_toggle_fullscreen(Platform *p) {
    p->fullscreen = !p->fullscreen;
    SDL_SetWindowFullscreen(p->window,
        p->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void platform_get_viewport_rect(Platform *p, int logical_w, int logical_h,
                                int *out_x, int *out_y, int *out_w, int *out_h) {
    int win_w, win_h;
    SDL_GetRendererOutputSize(p->renderer, &win_w, &win_h);

    /* Integer pixel-perfect scale */
    int scale_x = win_w / logical_w;
    int scale_y = win_h / logical_h;
    int s = (scale_x < scale_y) ? scale_x : scale_y;
    if (s < 1) s = 1;

    int dest_w = logical_w * s;
    int dest_h = logical_h * s;

    /* Center in the window */
    *out_x = (win_w - dest_w) / 2;
    *out_y = (win_h - dest_h) / 2;
    *out_w = dest_w;
    *out_h = dest_h;
}
