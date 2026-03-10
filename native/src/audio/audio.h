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

/* Set configurable volume scales (0.0 = silent, 1.0 = full, >1.0 = amplified) */
void audio_set_volume_scale(AudioEngine *audio, float master);
void audio_set_music_volume_scale(AudioEngine *audio, float scale);
void audio_set_sfx_volume_scale(AudioEngine *audio, float scale);

/* Load Pikachu PCM sound clips from WAV/OGG files.
 * clip_paths[0] and clip_paths[1] override the default filenames when non-NULL
 * and non-empty. Pass NULL to use defaults for both clips. */
void audio_init_pcm(AudioEngine *audio, const char *base_path,
                    const char *clip_paths[2]);

/* Play a Pikachu PCM clip (0 = "pi-ka-chu", 1 = "piiiiikaaaa" thundershock) */
void audio_play_pcm(AudioEngine *audio, int clip_index);

/* Free PCM sound clip buffers */
void audio_cleanup_pcm(AudioEngine *audio);

/* --- Custom WAV/OGG audio file support --- */

/* Load a single WAV or OGG file to S16 stereo 44100Hz PCM.
 * Returns clip index (>=0) on success, or -1 on failure. */
int audio_load_custom_clip(AudioEngine *audio, const char *file_path);

/* Load all custom clips referenced in config audio entries.
 * Called after config_load_all() to resolve file paths. */
struct ConfigData;
void audio_load_custom_clips(AudioEngine *audio, struct ConfigData *config,
                             const char *base_path);

/* Play a custom clip as music (looping, silences bytecode music) */
void audio_play_custom_music(AudioEngine *audio, int clip_index);

/* Play a custom clip as SFX (one-shot, mixed over current audio) */
void audio_play_custom_sfx(AudioEngine *audio, int clip_index);

/* Stop custom music playback (resumes bytecode music on next play) */
void audio_stop_custom_music(AudioEngine *audio);

/* Free all custom clip buffers */
void audio_cleanup_custom(AudioEngine *audio);

#endif /* AUDIO_H */
