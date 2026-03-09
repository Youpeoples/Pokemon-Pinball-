#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "platform/platform.h"

typedef struct AudioEngine AudioEngine;

/* Initialize the audio engine */
AudioEngine *audio_init(Platform *platform);

/* Shutdown and free audio resources */
void audio_shutdown(AudioEngine *audio);

/* Update the audio engine for one frame (call once per game frame ~59.7 Hz) */
void audio_update(AudioEngine *audio);

/* Play a sound effect (bank: 0x0F-0x13, id: 0-77) */
void audio_play_sfx(AudioEngine *audio, uint8_t bank, uint8_t id);

/* Play a music track (bank: 0x0F-0x13, id: 0-7) */
void audio_play_music(AudioEngine *audio, uint8_t bank, uint8_t id);

/* Play a sound effect only if no SFX channel is currently active.
 * ASM: PlaySFXIfNoneActive (0x4d8) — checks channels 4, 5, 6. */
void audio_play_sfx_if_none_active(AudioEngine *audio, uint8_t bank, uint8_t id);

/* Play a Pokemon cry. ASM: PlayCry (0x4ef) + PlayCry_BankF (0x3c0f0).
 * mon_id: 1-151 (species number). Uses SFX channels with cry pitch/length. */
void audio_play_cry(AudioEngine *audio, uint8_t mon_id);

/* Stop all audio */
void audio_stop_all(AudioEngine *audio);

/* Set master volume (0-7 for each channel) */
void audio_set_volume(AudioEngine *audio, uint8_t left, uint8_t right);

/* Load Pikachu PCM sound clips from WAV files.
 * The original uses raw GBC wave channel PCM streaming (PlayPikachuSoundClip
 * at bank 0x50) which can't be reproduced through the bytecode SFX system.
 * This is the best we can do: play the WAV files mixed into the audio output. */
void audio_init_pcm(AudioEngine *audio, const char *base_path);

/* Play a Pikachu PCM clip (0 = "pi-ka-chu", 1 = "piiiiikaaaa" thundershock) */
void audio_play_pcm(AudioEngine *audio, int clip_index);

/* Free PCM sound clip buffers */
void audio_cleanup_pcm(AudioEngine *audio);

#endif /* AUDIO_H */
