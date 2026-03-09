/*
 * Audio Engine - GBC APU Synthesis + Bytecode Interpreter
 *
 * Faithfully reconstructs the Pokemon Pinball GBC audio engine from
 * engine_0f.asm (and identical copies in banks 10-13).
 *
 * Architecture:
 * - 8 software channels (0-3 music, 4-7 SFX) mapped to 4 hardware channels
 * - Bytecode interpreter processes commands $D0-$FF
 * - GBC APU synthesis: 2 square wave, 1 wave, 1 noise channel
 * - Output: 44100 Hz stereo S16 via SDL2 SDL_QueueAudio
 */

#include "audio/audio.h"
#include "audio/audio_data.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* --- GBC APU constants --- */
#define SAMPLE_RATE      44100
#define GBC_FRAME_RATE   59.7275
#define SAMPLES_PER_FRAME ((int)(SAMPLE_RATE / GBC_FRAME_RATE))
#define GBC_CPU_CLOCK    4194304

/* Channel state size (matches ASM wChannel structure: $32 = 50 bytes) */
#define CHANNEL_STATE_SIZE 0x32
#define NUM_CHANNELS 8

/* Channel state offsets (matching ASM hl+offset addressing) */
#define CH_TEMPO_LO       0x00  /* 16-bit tempo */
#define CH_TEMPO_HI       0x01
#define CH_FLAGS1         0x02  /* bit 0=active, bit 1=has_return, bit 2=loop, bit 3=sfx,
                                   bit 4=drum, bit 5=cry */
#define CH_FLAGS2         0x03  /* bit 0=vibrato, bit 1=pitch_slide, bit 2=duty_cycle_pattern,
                                   bit 3=note_delay, bit 4=cry_pitch */
#define CH_FLAGS3         0x04  /* bit 0=vibrato_dir, bit 1=slide_dir */
#define CH_PC_LO          0x05  /* program counter (pointer into data) */
#define CH_PC_HI          0x06
#define CH_RET_PC_LO      0x07  /* return address for sound_call */
#define CH_RET_PC_HI      0x08
#define CH_UNUSED_09      0x09
#define CH_UNUSED_0A      0x0A
#define CH_REG_FLAGS       0x0B  /* bits that flag which registers to update */
#define CH_LOOP_COUNTER   0x0C
#define CH_DUTY           0x0D  /* duty cycle / NRx1 high bits */
#define CH_VOL_ENV        0x0E  /* volume envelope NRx2 */
#define CH_FREQ_LO        0x0F  /* frequency low */
#define CH_FREQ_HI        0x10  /* frequency high */
#define CH_NOTE_SPEED     0x11  /* note speed multiplier */
#define CH_OCTAVE         0x12  /* octave (packed: high=base, low=current) */
#define CH_TRANSPOSE_HI   0x13  /* transpose octave adjust */
#define CH_COUNTDOWN      0x14  /* note duration countdown */
#define CH_FRAC_COUNT     0x15  /* fractional countdown accumulator */
#define CH_LOOP_CNT2      0x16  /* sound_loop counter */
#define CH_TEMPO_VAL_LO   0x17  /* current tempo value */
#define CH_TEMPO_VAL_HI   0x18
#define CH_PANNING        0x19  /* stereo panning mask */
#define CH_DUTY_PAT       0x1A  /* duty cycle pattern */
#define CH_VIB_DELAY      0x1B  /* vibrato delay counter (current) */
#define CH_VIB_DELAY_INIT 0x1C  /* vibrato delay initial */
#define CH_VIB_DEPTH      0x1D  /* vibrato depth/rate */
#define CH_VIB_RATE       0x1E  /* vibrato rate counter */
#define CH_SLIDE_TARGET_LO 0x1F /* pitch slide target low */
#define CH_SLIDE_TARGET_HI 0x20 /* pitch slide target high */
#define CH_SLIDE_STEP      0x21 /* pitch slide step */
#define CH_SLIDE_FRAC      0x22 /* pitch slide fractional */
#define CH_SLIDE_FRAC2     0x23 /* pitch slide fractional 2 */
#define CH_NOTE_DELAY_CNT  0x24 /* note delay countdown */
#define CH_NOTE_DELAY_INIT 0x25 /* note delay initial */
#define CH_CRY_PITCH_LO    0x26 /* cry pitch adjustment low */
#define CH_CRY_PITCH_HI    0x27 /* cry pitch adjustment high */
#define CH_NOTE_LENGTH     0x28 /* note length for speed calculation */

/* GBC hardware register shadows */
typedef struct {
    uint8_t nr10;  /* CH1 sweep */
    uint8_t nr11;  /* CH1 duty/length */
    uint8_t nr12;  /* CH1 volume envelope */
    uint8_t nr13;  /* CH1 freq low */
    uint8_t nr14;  /* CH1 freq high + trigger */
    uint8_t nr21;  /* CH2 duty/length */
    uint8_t nr22;  /* CH2 volume envelope */
    uint8_t nr23;  /* CH2 freq low */
    uint8_t nr24;  /* CH2 freq high + trigger */
    uint8_t nr30;  /* CH3 enable */
    uint8_t nr31;  /* CH3 length */
    uint8_t nr32;  /* CH3 volume */
    uint8_t nr33;  /* CH3 freq low */
    uint8_t nr34;  /* CH3 freq high + trigger */
    uint8_t nr41;  /* CH4 length */
    uint8_t nr42;  /* CH4 volume envelope */
    uint8_t nr43;  /* CH4 frequency */
    uint8_t nr44;  /* CH4 trigger */
    uint8_t nr50;  /* Master volume */
    uint8_t nr51;  /* Stereo panning */
    uint8_t nr52;  /* Sound on/off */
    uint8_t wave_ram[16]; /* Wave pattern RAM */
} APURegisters;

/* GBC APU state for synthesis */
typedef struct {
    /* Square channels (0, 1) */
    struct {
        double phase;
        uint8_t duty;      /* 0-3 */
        uint8_t volume;
        int8_t  env_dir;   /* +1 or -1 */
        uint8_t env_period;
        uint8_t env_timer;
        uint16_t freq;     /* 11-bit frequency register */
        bool active;
        bool triggered;
    } square[2];

    /* Wave channel */
    struct {
        double phase;
        uint8_t volume_shift; /* 0=mute, 1=100%, 2=50%, 3=25% */
        uint16_t freq;
        bool active;
        bool triggered;
    } wave;

    /* Noise channel */
    struct {
        double timer;
        uint16_t lfsr;
        uint8_t volume;
        int8_t  env_dir;
        uint8_t env_period;
        uint8_t env_timer;
        uint8_t freq_code;
        bool active;
        bool triggered;
    } noise;

    /* Global envelope timer (GBC envelope clocks at 64 Hz) */
    double env_clock;
} APUState;

struct AudioEngine {
    Platform *platform;
    bool enabled;

    /* Software channel state (8 channels x 50 bytes each) */
    uint8_t channels[NUM_CHANNELS][CHANNEL_STATE_SIZE];

    /* Global audio state (matching ASM WRAM variables) */
    uint8_t current_channel;   /* wde97: current channel being processed */
    uint8_t master_volume;     /* wde98: NR50 value */
    uint8_t stereo_panning;    /* wde99: NR51 value */
    uint8_t nr10_shadow;       /* wde9a: NR10 shadow */
    uint16_t song_data_base;   /* wde9b/c: base pointer for current song */
    uint16_t drum_ptr;         /* wde9d/e: current drum pattern pointer */
    uint8_t drum_countdown;    /* wde9f: drum note countdown */
    uint8_t drum_kit;          /* wdea1: current drum kit index */
    uint8_t fade_control;      /* wdea2: volume fade control */
    uint8_t fade_counter;      /* wdea3: volume fade counter */
    uint16_t fade_song;        /* wdea4/5: song to play after fade */
    uint8_t note_byte;         /* wde96: last read note byte */
    uint8_t slide_speed;       /* wde95: pitch slide speed */
    uint8_t cry_active;        /* wdead: cry is playing */
    uint8_t saved_volume;      /* wdeac: saved NR50 during cry */
    uint8_t channel_idx;       /* wdeae: channel index for init */
    bool audio_enabled;        /* wdd00: audio update enabled */
    uint8_t sfx_timer;         /* wSFXTimer: SFX suppression timer */
    uint8_t sfx_timer_tick;    /* wd801: 4-frame sub-counter for timer decrement */

    /* Register shadows for output */
    uint8_t reg_duty;          /* wde91: duty cycle to write */
    uint8_t reg_vol_env;       /* wde92: volume envelope to write */
    uint8_t reg_freq_lo;       /* wde93: freq low to write */
    uint8_t reg_freq_hi;       /* wde94: freq high to write */

    /* Hardware register state */
    APURegisters regs;
    APUState apu;

    /* Current song/SFX data pointers */
    const uint8_t *current_song_data;
    uint32_t current_song_size;
    const uint8_t *current_drum_data;
    uint32_t current_drum_size;

    /* Per-channel data source (each channel reads from its own data block) */
    const uint8_t *channel_data[NUM_CHANNELS];
    uint32_t channel_data_size[NUM_CHANNELS];

    /* DC-blocking high-pass filter state (removes DC offset clicks) */
    double dc_prev_in_l;
    double dc_prev_out_l;
    double dc_prev_in_r;
    double dc_prev_out_r;

    /* Audio output buffer */
    int16_t output_buffer[SAMPLES_PER_FRAME * 2]; /* stereo */

    /* PCM Pikachu sound clips (loaded from WAV files) */
    int16_t *pcm_buffers[2];    /* 0 = "pi-ka-chu", 1 = "piiiiikaaaa" */
    uint32_t pcm_lengths[2];    /* length in stereo sample pairs */
    uint32_t pcm_position;      /* current playback position */
    int      pcm_active_clip;   /* -1 = none, 0 or 1 = playing */
};

/* --- Forward declarations --- */
static void engine_init(AudioEngine *e);
static void engine_update_frame(AudioEngine *e);
static void channel_read_note(AudioEngine *e, uint8_t *ch);
static uint8_t channel_read_byte(AudioEngine *e, uint8_t *ch);
static void channel_dispatch_command(AudioEngine *e, uint8_t *ch);
static void channel_process_effects(AudioEngine *e, uint8_t *ch);
static void channel_output_registers(AudioEngine *e, uint8_t *ch);
static void synthesize_frame(AudioEngine *e);
static void apply_registers_to_apu(AudioEngine *e);

/* --- Square wave duty cycle tables --- */
/* GBC duty cycles: 12.5%, 25%, 50%, 75% */
static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},  /* 12.5% */
    {1, 0, 0, 0, 0, 0, 0, 1},  /* 25% */
    {1, 0, 0, 0, 0, 1, 1, 1},  /* 50% */
    {0, 1, 1, 1, 1, 1, 1, 0},  /* 75% */
};

/* --- Frequency calculation (matching Func_3ca2b) --- */
static void calc_frequency(AudioEngine *e, uint8_t *ch) {
    uint8_t octave_data = ch[CH_OCTAVE];
    uint8_t transpose = ch[CH_TRANSPOSE_HI];

    uint8_t base_octave = (transpose >> 4) & 0xF;
    uint8_t pitch_idx = transpose & 0xF;

    uint8_t note_speed = ch[CH_NOTE_SPEED];
    uint8_t combined = base_octave + ((octave_data >> 4) & 0xF);

    /* Look up base frequency from table */
    uint16_t idx = (pitch_idx + note_speed);
    if (idx >= 25) idx = 24;
    uint16_t freq = audio_freq_table[idx];

    /* Shift by octave */
    uint8_t octave = combined;
    while (octave < 7) {
        freq = (int16_t)freq >> 1; /* arithmetic shift */
        octave++;
    }

    /* Mask to 11 bits */
    ch[CH_FREQ_LO] = freq & 0xFF;
    ch[CH_FREQ_HI] = (freq >> 8) & 0x07;
}

/* --- Duration calculation (matching Func_3ca5b) --- */
static void calc_duration(AudioEngine *e, uint8_t *ch) {
    uint8_t note_byte = e->note_byte;
    uint8_t length = (note_byte & 0xF) + 1;
    uint8_t speed = ch[CH_NOTE_LENGTH];

    /* Multiply length * speed */
    uint16_t duration = (uint16_t)length * speed;

    /* Apply tempo */
    uint16_t tempo = (uint16_t)ch[CH_TEMPO_VAL_LO] | ((uint16_t)ch[CH_TEMPO_VAL_HI] << 8);
    uint8_t frac = ch[CH_FRAC_COUNT];

    uint32_t total = (uint32_t)duration * tempo + frac;
    ch[CH_FRAC_COUNT] = total & 0xFF;
    ch[CH_COUNTDOWN] = (total >> 8) & 0xFF;
    if (ch[CH_COUNTDOWN] == 0) ch[CH_COUNTDOWN] = 1;
}

/* --- Public API --- */

AudioEngine *audio_init(Platform *platform) {
    AudioEngine *e = calloc(1, sizeof(AudioEngine));
    if (!e) return NULL;
    e->platform = platform;
    e->enabled = true;
    e->pcm_active_clip = -1;
    engine_init(e);
    return e;
}

void audio_shutdown(AudioEngine *e) {
    if (e) {
        audio_cleanup_pcm(e);
        free(e);
    }
}

void audio_update(AudioEngine *e) {
    if (!e || !e->enabled) return;

    /* Run the bytecode interpreter for one frame */
    engine_update_frame(e);

    /* Apply register writes to APU state */
    apply_registers_to_apu(e);

    /* Synthesize audio samples for this frame */
    synthesize_frame(e);

    /* Submit to SDL */
    platform_audio_submit(e->platform, e->output_buffer, SAMPLES_PER_FRAME);

    /* UpdateSFX (0x504) — SFX timer decrement:
     * ASM: wd801 increments each frame; every 4th frame (and $3 == 0),
     * wSFXTimer is decremented if non-zero. */
    e->sfx_timer_tick++;
    if ((e->sfx_timer_tick & 3) == 0) {
        if (e->sfx_timer > 0) {
            e->sfx_timer--;
        }
    }
}

void audio_play_music(AudioEngine *e, uint8_t bank, uint8_t id) {
    if (!e) return;

    int bank_idx = audio_bank_to_index(bank);
    if (bank_idx < 0 || bank_idx >= AUDIO_NUM_BANKS) return;
    if (id >= AUDIO_MAX_SONGS_PER_BANK) return;

    const AudioDataEntry *entry = &audio_song_table[bank_idx][id];
    if (!entry->data || entry->size == 0) return;

    printf("[Audio] Playing music: bank=0x%02X id=%d size=%d\n", bank, id, entry->size);

    /* Clear SDL audio queue to avoid latency from buffered old audio */
    platform_audio_clear(e->platform);

    /* Reset engine (matching Func_3c000) */
    engine_init(e);
    e->audio_enabled = true;

    e->current_song_data = entry->data;
    e->current_song_size = entry->size;

    /* Set drum data for this bank */
    switch (bank_idx) {
        case 0: e->current_drum_data = audio_drums_bank0; e->current_drum_size = sizeof(audio_drums_bank0); break;
        case 1: e->current_drum_data = audio_drums_bank1; e->current_drum_size = sizeof(audio_drums_bank1); break;
        case 2: e->current_drum_data = audio_drums_bank2; e->current_drum_size = sizeof(audio_drums_bank2); break;
        case 3: e->current_drum_data = audio_drums_bank3; e->current_drum_size = sizeof(audio_drums_bank3); break;
        case 4: e->current_drum_data = audio_drums_bank4; e->current_drum_size = sizeof(audio_drums_bank4); break;
    }

    /* Parse song header (matching PlaySong_BankF) */
    const uint8_t *data = entry->data;
    uint8_t first_byte = data[0];

    /* Extract channel count: rotate left twice, mask bottom 2 bits, add 1 */
    uint8_t num_channels = (((first_byte >> 6) & 0x3)) + 1;

    uint16_t ptr = 0; /* Pointer into song data */

    for (int i = 0; i < num_channels; i++) {
        uint8_t ch_byte = data[ptr++];
        uint8_t ch_id = ch_byte & 0x07;
        uint16_t ch_addr = data[ptr] | ((uint16_t)data[ptr+1] << 8);
        ptr += 2;

        if (ch_id >= NUM_CHANNELS) continue;

        uint8_t *ch = e->channels[ch_id];
        memset(ch, 0, CHANNEL_STATE_SIZE);

        /* Set program counter to channel data offset */
        ch[CH_PC_LO] = ch_addr & 0xFF;
        ch[CH_PC_HI] = (ch_addr >> 8) & 0xFF;

        /* Set per-channel data source */
        e->channel_data[ch_id] = entry->data;
        e->channel_data_size[ch_id] = entry->size;

        /* Set tempo (stored in channel bytes 0-1) */
        ch[CH_TEMPO_VAL_LO] = 0x00;
        ch[CH_TEMPO_VAL_HI] = 0x01; /* Default tempo = 256 */
        ch[CH_NOTE_LENGTH] = 0x01;  /* Default note length multiplier */

        /* Set panning mask */
        ch[CH_PANNING] = audio_channel_panning[ch_id & 3];

        /* Mark channel as active */
        ch[CH_FLAGS1] |= 0x01;
    }

    e->master_volume = 0x77;
    e->nr10_shadow = 0x08;
    e->audio_enabled = true;
}

void audio_play_sfx(AudioEngine *e, uint8_t bank, uint8_t id) {
    if (!e) return;
    if (e->cry_active) return; /* Don't play SFX during cries (wdead check) */

    /* PlaySoundEffect (0x4af) — SFX timer suppression:
     * ASM: if wSFXTimer != 0 AND d (duration param, mapped to 'bank' arg) == 0, suppress.
     * The 'bank' parameter is actually the ASM 'd' register = duration/timer value. */
    if (e->sfx_timer != 0 && bank == 0) {
        return; /* Timer active and no new duration → suppress */
    }
    e->sfx_timer = bank; /* Store duration as new timer value */

    if (id >= AUDIO_NUM_SFX) return;

    const AudioDataEntry *entry = &audio_sfx_table[id];
    if (!entry->data || entry->size == 0) return;

    const uint8_t *data = entry->data;
    uint8_t first_byte = data[0];
    uint8_t num_channels = ((first_byte >> 6) & 0x3) + 1;

    uint16_t ptr = 0;

    for (int i = 0; i < num_channels; i++) {
        uint8_t ch_byte = data[ptr++];
        uint8_t ch_id = ch_byte & 0x07;
        uint16_t ch_addr = data[ptr] | ((uint16_t)data[ptr+1] << 8);
        ptr += 2;

        if (ch_id >= NUM_CHANNELS) continue;

        uint8_t *ch = e->channels[ch_id];
        memset(ch, 0, CHANNEL_STATE_SIZE);

        ch[CH_PC_LO] = ch_addr & 0xFF;
        ch[CH_PC_HI] = (ch_addr >> 8) & 0xFF;

        /* Set per-channel data source to the SFX data block */
        e->channel_data[ch_id] = entry->data;
        e->channel_data_size[ch_id] = entry->size;

        ch[CH_TEMPO_VAL_LO] = 0x00;
        ch[CH_TEMPO_VAL_HI] = 0x01;
        ch[CH_NOTE_LENGTH] = 0x01;
        ch[CH_PANNING] = audio_channel_panning[ch_id & 3];
        ch[CH_FLAGS1] |= 0x01; /* active */
        ch[CH_FLAGS1] |= 0x08; /* SFX flag */
    }

    e->audio_enabled = true;
}

/*
 * PlaySFXIfNoneActive (0x4d8):
 * Only plays SFX if no SFX channel is currently active.
 * ASM: checks wChannel4+2, wChannel5+2, wChannel6+2 (flags1 bit 0).
 * ORs them together; if result is 0 (no channel active), calls PlaySoundEffect.
 */
void audio_play_sfx_if_none_active(AudioEngine *e, uint8_t bank, uint8_t id) {
    if (!e) return;
    /* Check SFX channels 4, 5, 6 for active flag (bit 0 of CH_FLAGS1) */
    uint8_t active = e->channels[4][CH_FLAGS1]
                   | e->channels[5][CH_FLAGS1]
                   | e->channels[6][CH_FLAGS1];
    if (active & 0x01) return;  /* At least one SFX channel active — don't play */
    audio_play_sfx(e, bank, id);
}

/*
 * PlayCry (0x4ef) + PlayCry_BankF (0x3c0f0):
 * Plays a Pokemon cry using SFX channels with pitch/length modifiers.
 * Input: mon_id = 1-151 (species number, 1-based like ASM's 'e' register).
 */
void audio_play_cry(AudioEngine *e, uint8_t mon_id) {
    if (!e) return;
    if (mon_id == 0 || mon_id > AUDIO_NUM_POKEMON) return;

    /* Look up CryData for this Pokemon (0-based index) */
    const CryDataEntry *cd = &audio_cry_data[mon_id - 1];
    uint8_t cry_id = cd->cry_id;
    uint16_t cry_pitch = cd->pitch;
    uint16_t cry_length = cd->length;

    if (cry_id >= AUDIO_NUM_CRIES) return;

    /* All cry data lives in one shared blob (audio_cry_blob).
     * audio_cry_offsets[cry_id] gives the header offset within it. */
    const AudioDataEntry *entry = &audio_cry_table[cry_id];
    if (!entry->data || entry->size == 0) return;

    const uint8_t *blob = entry->data;
    uint16_t header_offset = audio_cry_offsets[cry_id];
    const uint8_t *data = blob + header_offset;

    uint8_t first_byte = data[0];
    uint8_t num_channels = ((first_byte >> 6) & 0x3) + 1;

    uint16_t ptr = 0;

    /* Initialize each channel (same as SFX but with cry flags) */
    for (int i = 0; i < num_channels; i++) {
        uint8_t ch_byte = data[ptr++];
        uint8_t ch_id = ch_byte & 0x07;
        /* Channel address is an absolute offset into the blob */
        uint16_t ch_addr = data[ptr] | ((uint16_t)data[ptr + 1] << 8);
        ptr += 2;

        if (ch_id >= NUM_CHANNELS) continue;

        uint8_t *ch = e->channels[ch_id];
        memset(ch, 0, CHANNEL_STATE_SIZE);

        /* PC points into the blob using the absolute offset */
        ch[CH_PC_LO] = ch_addr & 0xFF;
        ch[CH_PC_HI] = (ch_addr >> 8) & 0xFF;

        /* All channels share the same blob as their data source */
        e->channel_data[ch_id] = blob;
        e->channel_data_size[ch_id] = entry->size;

        ch[CH_TEMPO_VAL_LO] = 0x00;
        ch[CH_TEMPO_VAL_HI] = 0x01;
        ch[CH_NOTE_LENGTH] = 0x01;
        ch[CH_PANNING] = audio_channel_panning[ch_id & 3];
        ch[CH_FLAGS1] |= 0x01; /* active */
        ch[CH_FLAGS1] |= 0x08; /* SFX flag — cry uses square_note/noise_note raw format */
        ch[CH_FLAGS1] |= 0x20; /* cry flag (bit 5) */
        ch[CH_FLAGS2] |= 0x10; /* cry_pitch enabled (bit 4) */

        /* Set cry pitch adjustment */
        ch[CH_CRY_PITCH_LO] = cry_pitch & 0xFF;
        ch[CH_CRY_PITCH_HI] = (cry_pitch >> 8) & 0xFF;

        /* For non-noise channels (ch_id & 3 < 3), set cry length as tempo */
        if ((ch_id & 3) < 3) {
            ch[CH_TEMPO_VAL_LO] = cry_length & 0xFF;
            ch[CH_TEMPO_VAL_HI] = (cry_length >> 8) & 0xFF;
        }
    }

    /* Save current volume if not already saved (wdeac check) */
    if (e->saved_volume == 0) {
        e->saved_volume = e->master_volume;
        e->master_volume = 0x77;
    }

    /* Set cry active flag */
    e->cry_active = 1;
    e->audio_enabled = true;
}

void audio_stop_all(AudioEngine *e) {
    if (!e) return;
    engine_init(e);
}

void audio_set_volume(AudioEngine *e, uint8_t left, uint8_t right) {
    if (!e) return;
    e->master_volume = ((left & 0x7) << 4) | (right & 0x7);
}

/* --- Engine internals --- */

static void engine_init(AudioEngine *e) {
    memset(e->channels, 0, sizeof(e->channels));
    for (int i = 0; i < NUM_CHANNELS; i++) {
        e->channel_data[i] = NULL;
        e->channel_data_size[i] = 0;
    }
    e->current_channel = 0;
    e->master_volume = 0x77;
    e->stereo_panning = 0;
    e->nr10_shadow = 0x08;
    e->cry_active = 0;
    e->saved_volume = 0;
    e->audio_enabled = false;
    e->fade_control = 0;
    e->current_song_data = NULL;
    e->current_song_size = 0;
    e->current_drum_data = NULL;
    e->current_drum_size = 0;

    memset(&e->regs, 0, sizeof(e->regs));
    e->regs.nr50 = 0x77;
    e->regs.nr52 = 0x80;

    /* Initialize APU */
    memset(&e->apu, 0, sizeof(e->apu));
    e->apu.noise.lfsr = 0x7FFF;
}

static void engine_update_frame(AudioEngine *e) {
    if (!e->audio_enabled) return;

    e->current_channel = 0;
    e->stereo_panning = 0;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        uint8_t *ch = e->channels[i];
        e->current_channel = i;

        if (!(ch[CH_FLAGS1] & 0x01)) {
            /* Channel not active, skip */
            goto next_channel;
        }

        /* Check countdown */
        if (ch[CH_COUNTDOWN] >= 2) {
            ch[CH_COUNTDOWN]--;
        } else {
            /* Reset vibrato/delay counters from initial values */
            ch[CH_VIB_DELAY] = ch[CH_VIB_DELAY_INIT];
            ch[CH_NOTE_DELAY_CNT] = ch[CH_NOTE_DELAY_INIT];
            ch[CH_FLAGS2] &= ~0x02; /* clear slide direction */

            /* Read next note/command */
            channel_read_note(e, ch);
        }

        /* Copy register values to temp */
        e->reg_duty = ch[CH_DUTY];
        e->reg_vol_env = ch[CH_VOL_ENV];
        e->reg_freq_lo = ch[CH_FREQ_LO];
        e->reg_freq_hi = ch[CH_FREQ_HI];

        /* Process effects (vibrato, pitch slide, etc.) */
        channel_process_effects(e, ch);

        /* Process drum pattern if active */
        if (ch[CH_FLAGS1] & 0x10) { /* drum mode */
            /* Drum pattern processing (Func_3c632) */
            if (e->drum_countdown > 0) {
                e->drum_countdown--;
            } else if (e->current_drum_data && e->drum_ptr < e->current_drum_size) {
                uint8_t drum_byte = e->current_drum_data[e->drum_ptr++];
                if (drum_byte != 0xFF) {
                    e->drum_countdown = (drum_byte & 0xF) + 1;
                    if (e->drum_ptr < e->current_drum_size)
                        e->reg_vol_env = e->current_drum_data[e->drum_ptr++];
                    if (e->drum_ptr < e->current_drum_size)
                        e->reg_freq_lo = e->current_drum_data[e->drum_ptr++];
                    e->reg_freq_hi = 0;
                    ch[CH_REG_FLAGS] |= 0x10; /* flag for full note init */
                }
            }
        }

        /* Check if SFX is overriding music channel */
        if (e->cry_active && i < 4) {
            /* During cry, mute music channels if SFX channel is active */
            if (e->channels[i + 4][CH_FLAGS1] & 0x01) {
                ch[CH_REG_FLAGS] |= 0x20; /* mute */
            }
        }
        if (i < 4 && (e->channels[i + 4][CH_FLAGS1] & 0x01)) {
            /* SFX channel is active, mute corresponding music channel */
            goto clear_flags;
        }

        /* Output to hardware registers */
        channel_output_registers(e, ch);

        /* Update stereo panning */
        e->stereo_panning |= ch[CH_PANNING];

clear_flags:
        ch[CH_REG_FLAGS] = 0;

next_channel:
        ;
    }

    /* Check if cry has finished (all cry channels inactive → restore volume) */
    if (e->cry_active) {
        uint8_t any_cry = 0;
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if ((e->channels[i][CH_FLAGS1] & 0x21) == 0x21) {
                /* Channel is active (bit 0) and is a cry (bit 5) */
                any_cry = 1;
                break;
            }
        }
        if (!any_cry) {
            e->cry_active = 0;
            if (e->saved_volume) {
                e->master_volume = e->saved_volume;
                e->saved_volume = 0;
            }
        }
    }

    /* Handle volume fading */
    if (e->fade_control) {
        if (e->fade_counter > 0) {
            e->fade_counter--;
        } else {
            uint8_t speed = e->fade_control & 0x7F;
            e->fade_counter = speed;
            uint8_t vol = e->master_volume & 0x07;
            if (e->fade_control & 0x80) {
                /* Fading in */
                if (vol < 7) vol++;
                else { e->fade_control = 0; }
            } else {
                /* Fading out */
                if (vol > 0) vol--;
                else {
                    /* Fade complete, play next song */
                    e->fade_control |= 0x80;
                }
            }
            e->master_volume = (vol << 4) | vol;
        }
    }

    /* Write master volume and panning to registers */
    e->regs.nr50 = e->master_volume;
    e->regs.nr51 = e->stereo_panning;
}

/* Read next note or command from channel data */
static void channel_read_note(AudioEngine *e, uint8_t *ch) {
    int ch_idx = (int)(ch - e->channels[0]) / CHANNEL_STATE_SIZE;
    const uint8_t *data = e->channel_data[ch_idx];

    if (!data) {
        ch[CH_FLAGS1] &= ~0x01; /* deactivate */
        return;
    }

read_again:
    ;
    uint8_t byte = channel_read_byte(e, ch);
    e->note_byte = byte;

    if (byte == 0xFF) {
        /* sound_ret or end of data */
        if (ch[CH_FLAGS1] & 0x02) {
            /* Has return address, pop it */
            ch[CH_FLAGS1] &= ~0x02;
            ch[CH_PC_LO] = ch[CH_RET_PC_LO];
            ch[CH_PC_HI] = ch[CH_RET_PC_HI];
            goto read_again;
        }

        /* Channel finished, handle cry end if needed */
        if (ch[CH_FLAGS1] & 0x20) {
            /* Cry channel ending */
            if (e->current_channel == 4) {
                e->regs.nr10 = 0;
                e->nr10_shadow = 0;
            }
        }

        /* Deactivate channel */
        ch[CH_FLAGS1] &= ~0x01;
        ch[CH_TEMPO_LO] = 0;
        ch[CH_TEMPO_HI] = 0;
        return;
    }

    if (byte >= 0xD0) {
        /* It's a command */
        channel_dispatch_command(e, ch);
        goto read_again;
    }

    /* It's a note */
    if (ch[CH_FLAGS1] & 0x08) {
        /* SFX note: byte is the raw length for square_note/noise_note */
        ch[CH_REG_FLAGS] |= 0x10; /* flag for full note init */
        calc_duration(e, ch);

        /* Read vol_env */
        uint8_t vol_env = channel_read_byte(e, ch);
        ch[CH_VOL_ENV] = vol_env;

        /* Read freq_lo */
        uint8_t freq_lo = channel_read_byte(e, ch);
        ch[CH_FREQ_LO] = freq_lo;

        /* Read freq_hi (except for noise channel) */
        if ((e->current_channel & 3) != 3) {
            uint8_t freq_hi = channel_read_byte(e, ch);
            ch[CH_FREQ_HI] = freq_hi;
        }
        return;
    }

    if (ch[CH_FLAGS1] & 0x10) {
        /* Drum channel (channel 3/noise): note byte encodes instrument + length */
        if ((e->current_channel & 3) != 3) return;

        /* Duration from low nybble */
        calc_duration(e, ch);

        /* Instrument from high nybble */
        uint8_t instrument = (e->note_byte >> 4) & 0xF;
        if (instrument == 0) return;

        /* Look up drum pattern from drum kit */
        if (e->current_drum_data && e->current_drum_size > 0) {
            /* The drum kit table is at the start of drum data.
               Each kit is 16 dw entries (32 bytes). Kit index is in drum_kit.
               Instrument index into the kit gives the pattern offset. */
            uint16_t kits_base = 0; /* First 6 dw entries = kit pointers */
            uint16_t kit_ptr_off = kits_base + e->drum_kit * 2;
            if (kit_ptr_off + 1 < e->current_drum_size) {
                uint16_t kit_off = e->current_drum_data[kit_ptr_off] |
                                   ((uint16_t)e->current_drum_data[kit_ptr_off + 1] << 8);
                /* kit_off is offset to the kit's 16-entry table */
                uint16_t pat_ptr_off = kit_off + instrument * 2;
                if (pat_ptr_off + 1 < e->current_drum_size) {
                    uint16_t pat_off = e->current_drum_data[pat_ptr_off] |
                                       ((uint16_t)e->current_drum_data[pat_ptr_off + 1] << 8);
                    e->drum_ptr = pat_off;
                    e->drum_countdown = 0;
                }
            }
        }
        return;
    }

    /* Normal music note: pitch in high nybble, length in low nybble */
    uint8_t pitch = (byte >> 4) & 0xF;
    calc_duration(e, ch);

    if (pitch == 0) {
        /* Rest note */
        ch[CH_REG_FLAGS] |= 0x20; /* mute */
        return;
    }

    /* Look up frequency */
    ch[CH_NOTE_SPEED] = pitch;
    uint8_t octave = ch[CH_OCTAVE] & 0x0F;
    uint8_t transpose_oct = (ch[CH_TRANSPOSE_HI] >> 4) & 0xF;
    uint8_t transpose_pitch = ch[CH_TRANSPOSE_HI] & 0xF;

    /* Combine pitch + transpose */
    uint16_t total_pitch = pitch + transpose_pitch;
    uint8_t total_octave = octave + transpose_oct;
    if (total_pitch > 12) {
        total_pitch -= 12;
        total_octave++;
    }

    /* Look up base frequency from table */
    if (total_pitch > 0 && total_pitch <= 24) {
        uint16_t freq = audio_freq_table[total_pitch];

        /* Shift right by (7 - octave) positions */
        int shift = 7 - total_octave;
        while (shift > 0) {
            freq = (uint16_t)((int16_t)freq >> 1);
            shift--;
        }

        ch[CH_FREQ_LO] = freq & 0xFF;
        ch[CH_FREQ_HI] = (freq >> 8) & 0x07;
    }

    /* Set register update flags */
    ch[CH_REG_FLAGS] |= 0x14; /* freq update + vol update */
}

/* Read one byte from channel's program counter */
static uint8_t channel_read_byte(AudioEngine *e, uint8_t *ch) {
    uint16_t pc = (uint16_t)ch[CH_PC_LO] | ((uint16_t)ch[CH_PC_HI] << 8);

    /* Use per-channel data source */
    int ch_idx = (int)(ch - e->channels[0]) / CHANNEL_STATE_SIZE;
    const uint8_t *data = e->channel_data[ch_idx];
    uint32_t size = e->channel_data_size[ch_idx];

    uint8_t val = 0;
    if (data && pc < size) {
        val = data[pc];
    }

    pc++;
    ch[CH_PC_LO] = pc & 0xFF;
    ch[CH_PC_HI] = (pc >> 8) & 0xFF;

    e->note_byte = val;
    return val;
}

/* Dispatch bytecode command ($D0-$FE) */
static void channel_dispatch_command(AudioEngine *e, uint8_t *ch) {
    uint8_t cmd = e->note_byte;

    if (cmd >= 0xD0 && cmd <= 0xD7) {
        /* Octave command (Func_3c9b6): store cmd & 7 at ch[0x12] */
        ch[CH_OCTAVE] = cmd & 0x07;
        return;
    }

    switch (cmd) {
    case 0xD8: { /* note_type: length [, vol, env] */
        uint8_t len = channel_read_byte(e, ch);
        ch[CH_NOTE_LENGTH] = len;
        /* Check if there's a vol_env byte (music channels only, not noise) */
        /* In practice, note_type always has 2 or 3 args. We read the vol_env
           if the next byte is available and the channel expects it. */
        /* Actually, the ASM note_type macro conditionally emits the vol_env byte.
           We can check: if the channel is noise (ch3), note_type = drum_speed with
           only 1 arg. For others, read vol_env. But we can't tell from here.
           The safest approach: peek at next byte. If it looks like a command or
           note, don't read it. But this is fragile.
           Actually: in the original, note_type ALWAYS has the vol_env byte
           when called with 3 args. The macro handles it. In our assembled data,
           the converter already emitted the correct bytes. So we should check
           the size of the emitted data. But we don't have that info here.
           Let's check: for drum_speed (noise channel), only 1 extra byte.
           For regular note_type, 2 extra bytes (length + vol_env).
           The converter emits 2 bytes for drum_speed (0xD8 + length) and
           3 bytes for note_type with vol_env (0xD8 + length + vol_env).
           Since we already read the length, if this is a drum channel we stop.
           Otherwise we read vol_env. */
        /* Check if we're on a noise channel (3 or 7) */
        if ((e->current_channel & 3) == 3) {
            /* Noise channel, no vol_env byte */
        } else {
            /* Read vol_env */
            uint8_t vol_env = channel_read_byte(e, ch);
            ch[CH_VOL_ENV] = vol_env;
            ch[CH_REG_FLAGS] |= 0x04; /* vol update */
        }
        break;
    }
    case 0xD9: { /* transpose */
        uint8_t val = channel_read_byte(e, ch);
        ch[CH_TRANSPOSE_HI] = val;
        break;
    }
    case 0xDA: { /* tempo (big-endian 16-bit) */
        uint8_t hi = channel_read_byte(e, ch);
        uint8_t lo = channel_read_byte(e, ch);
        /* Set tempo for all channels in same group (music or SFX) */
        uint16_t tempo = ((uint16_t)hi << 8) | lo;
        int base = (e->current_channel >= 4) ? 4 : 0;
        for (int i = base; i < base + 4; i++) {
            e->channels[i][CH_TEMPO_VAL_LO] = lo;
            e->channels[i][CH_TEMPO_VAL_HI] = hi;
        }
        break;
    }
    case 0xDB: { /* duty_cycle */
        uint8_t duty = channel_read_byte(e, ch);
        ch[CH_DUTY] = (duty & 3) << 6;
        ch[CH_REG_FLAGS] |= 0x01; /* duty update */
        break;
    }
    case 0xDC: { /* volume_envelope */
        uint8_t vol_env = channel_read_byte(e, ch);
        ch[CH_VOL_ENV] = vol_env;
        ch[CH_REG_FLAGS] |= 0x04; /* vol update */
        break;
    }
    case 0xDD: { /* pitch_sweep (channel 0/4 only) */
        uint8_t val = channel_read_byte(e, ch);
        e->nr10_shadow = val;
        ch[CH_REG_FLAGS] |= 0x08; /* sweep update */
        break;
    }
    case 0xDE: { /* duty_cycle_pattern (Func_3c939) */
        uint8_t pat = channel_read_byte(e, ch);
        /* ASM rotates right by 2 (rrca; rrca) before storing */
        pat = (pat >> 2) | (pat << 6);
        ch[CH_DUTY_PAT] = pat;
        ch[CH_FLAGS2] |= 0x04; /* enable duty pattern rotation */
        /* Set initial duty from top 2 bits of rotated pattern */
        ch[CH_DUTY] = pat & 0xC0;
        break;
    }
    case 0xDF: { /* toggle_sfx */
        ch[CH_FLAGS1] ^= 0x08; /* toggle SFX flag */
        break;
    }
    case 0xE0: { /* pitch_slide */
        uint8_t duration = channel_read_byte(e, ch);
        e->slide_speed = duration;
        uint8_t target = channel_read_byte(e, ch);
        /* target: high nybble = octave, low nybble = pitch */
        uint8_t octave = (target >> 4) & 0xF;
        uint8_t pitch = target & 0xF;
        /* Calculate target frequency */
        if (pitch <= 24) {
            uint16_t freq = audio_freq_table[pitch > 0 ? pitch : 1];
            int shift = 7 - octave;
            while (shift > 0) {
                freq = (uint16_t)((int16_t)freq >> 1);
                shift--;
            }
            ch[CH_SLIDE_TARGET_LO] = freq & 0xFF;
            ch[CH_SLIDE_TARGET_HI] = (freq >> 8) & 0xFF;
        }
        ch[CH_FLAGS2] |= 0x02; /* enable pitch slide */
        break;
    }
    case 0xE1: { /* vibrato (Func_3c8c4) */
        ch[CH_FLAGS2] |= 0x01; /* enable vibrato */
        ch[CH_FLAGS3] &= ~0x01; /* reset vibrato direction */
        uint8_t delay = channel_read_byte(e, ch);
        ch[CH_VIB_DELAY] = delay;
        ch[CH_VIB_DELAY_INIT] = delay;
        uint8_t params = channel_read_byte(e, ch);
        /* Process depth: high nybble → split into up/down halves (matching ASM) */
        uint8_t depth = (params >> 4) & 0xF;
        uint8_t half = depth >> 1;
        uint8_t carry = depth & 1;
        ch[CH_VIB_DEPTH] = ((half + carry) << 4) | half;
        /* Process rate: low nybble → duplicate into both nybbles */
        uint8_t rate = params & 0xF;
        ch[CH_VIB_RATE] = (rate << 4) | rate;
        break;
    }
    case 0xE2: { /* unknown_e2 */
        channel_read_byte(e, ch); /* skip 1 byte */
        break;
    }
    case 0xE3: { /* toggle_noise */
        ch[CH_FLAGS1] ^= 0x10; /* toggle drum/noise mode */
        if (ch[CH_FLAGS1] & 0x10) {
            uint8_t kit = channel_read_byte(e, ch);
            e->drum_kit = kit;
        }
        break;
    }
    case 0xE4: { /* force_stereo_panning */
        uint8_t pan = channel_read_byte(e, ch);
        ch[CH_PANNING] &= 0; /* Clear current panning */
        ch[CH_PANNING] = pan;

        /* Apply to all channels in group */
        int ch_hw = e->current_channel & 3;
        if (e->current_channel >= 4) {
            e->channels[e->current_channel][CH_PANNING] = pan;
        } else {
            e->channels[e->current_channel][CH_PANNING] = pan;
        }
        break;
    }
    case 0xE5: { /* volume */
        uint8_t vol = channel_read_byte(e, ch);
        e->master_volume = vol;
        break;
    }
    case 0xE6: { /* pitch_offset (cry pitch adjustment, big-endian) */
        /* Func_3c926: sets cry pitch at ch[0x26..0x27] and enables cry_pitch flag */
        uint8_t hi = channel_read_byte(e, ch);
        uint8_t lo = channel_read_byte(e, ch);
        ch[CH_CRY_PITCH_HI] = hi;
        ch[CH_CRY_PITCH_LO] = lo;
        ch[CH_FLAGS2] |= 0x10; /* enable cry pitch effect */
        break;
    }
    case 0xE7: /* N8: no-op in ASM (Func_3c7f7: ret), 0 params */
        break;
    case 0xE8: /* N8: no-op in ASM (Func_3c7f7: ret), 0 params */
        break;
    case 0xE9: { /* tempo_relative */
        uint8_t hi = channel_read_byte(e, ch);
        uint8_t lo = channel_read_byte(e, ch);
        /* Sign-extend and add to tempo */
        int16_t delta = (int16_t)((uint16_t)hi << 8 | lo);
        uint16_t tempo = ((uint16_t)ch[CH_TEMPO_VAL_HI] << 8) | ch[CH_TEMPO_VAL_LO];
        tempo = (uint16_t)((int16_t)tempo + delta);
        int base = (e->current_channel >= 4) ? 4 : 0;
        for (int i = base; i < base + 4; i++) {
            e->channels[i][CH_TEMPO_VAL_LO] = tempo & 0xFF;
            e->channels[i][CH_TEMPO_VAL_HI] = (tempo >> 8) & 0xFF;
        }
        break;
    }
    case 0xEA: { /* restart_channel — L14: stub, unused in game data */
        uint8_t lo = channel_read_byte(e, ch);
        uint8_t hi = channel_read_byte(e, ch);
        (void)lo; (void)hi;
        break;
    }
    case 0xEB: { /* new_song — L14: stub, unused in game data */
        uint8_t hi = channel_read_byte(e, ch);
        uint8_t lo = channel_read_byte(e, ch);
        (void)hi; (void)lo;
        break;
    }
    case 0xEC: /* sfx_priority_on */
        break;
    case 0xED: /* sfx_priority_off */
        break;
    case 0xEE: { /* unknown_ee */
        channel_read_byte(e, ch);
        channel_read_byte(e, ch);
        break;
    }
    case 0xEF: { /* stereo_panning */
        uint8_t pan = channel_read_byte(e, ch);
        ch[CH_PANNING] = pan & audio_channel_panning[e->current_channel & 3];
        break;
    }
    case 0xF0: { /* sfx_toggle_noise */
        ch[CH_FLAGS1] ^= 0x10;
        if (ch[CH_FLAGS1] & 0x10) {
            uint8_t kit = channel_read_byte(e, ch);
            e->drum_kit = kit;
        }
        break;
    }
    case 0xFA: { /* set_condition */
        uint8_t cond = channel_read_byte(e, ch);
        ch[CH_LOOP_COUNTER] = cond;
        break;
    }
    case 0xFB: { /* sound_jump_if */
        uint8_t cond = channel_read_byte(e, ch);
        uint8_t lo = channel_read_byte(e, ch);
        uint8_t hi = channel_read_byte(e, ch);
        if (ch[CH_LOOP_COUNTER] == cond) {
            ch[CH_PC_LO] = lo;
            ch[CH_PC_HI] = hi;
        }
        break;
    }
    case 0xFC: { /* sound_jump */
        uint8_t lo = channel_read_byte(e, ch);
        uint8_t hi = channel_read_byte(e, ch);
        ch[CH_PC_LO] = lo;
        ch[CH_PC_HI] = hi;
        break;
    }
    case 0xFD: { /* sound_loop */
        uint8_t count = channel_read_byte(e, ch);
        uint8_t lo = channel_read_byte(e, ch);
        uint8_t hi = channel_read_byte(e, ch);

        if (count == 0) {
            /* Infinite loop */
            ch[CH_PC_LO] = lo;
            ch[CH_PC_HI] = hi;
        } else {
            if (!(ch[CH_FLAGS1] & 0x04)) {
                /* Not in loop yet, start it */
                ch[CH_FLAGS1] |= 0x04;
                ch[CH_LOOP_CNT2] = count - 1;
            }
            if (ch[CH_LOOP_CNT2] > 0) {
                ch[CH_LOOP_CNT2]--;
                ch[CH_PC_LO] = lo;
                ch[CH_PC_HI] = hi;
            } else {
                /* Loop finished */
                ch[CH_FLAGS1] &= ~0x04;
            }
        }
        break;
    }
    case 0xFE: { /* sound_call */
        uint8_t lo = channel_read_byte(e, ch);
        uint8_t hi = channel_read_byte(e, ch);

        /* Save return address */
        ch[CH_RET_PC_LO] = ch[CH_PC_LO];
        ch[CH_RET_PC_HI] = ch[CH_PC_HI];
        ch[CH_FLAGS1] |= 0x02; /* has return address */

        /* Jump to subroutine */
        ch[CH_PC_LO] = lo;
        ch[CH_PC_HI] = hi;
        break;
    }
    default:
        /* Unknown command (0xF1-0xF9 are no-ops) */
        break;
    }
}

/* Process per-frame effects: duty cycle pattern, vibrato, pitch slide */
static void channel_process_effects(AudioEngine *e, uint8_t *ch) {
    /* Duty cycle pattern rotation */
    if (ch[CH_FLAGS2] & 0x04) {
        uint8_t pat = ch[CH_DUTY_PAT];
        pat = (pat << 2) | (pat >> 6); /* rotate left 2 bits */
        ch[CH_DUTY_PAT] = pat;
        e->reg_duty = pat & 0xC0;
        ch[CH_REG_FLAGS] |= 0x01; /* duty update */
    }

    /* Cry pitch adjustment */
    if (ch[CH_FLAGS2] & 0x10) {
        int16_t cry_pitch = (int16_t)((uint16_t)ch[CH_CRY_PITCH_LO] |
                                       ((uint16_t)ch[CH_CRY_PITCH_HI] << 8));
        int16_t freq = (int16_t)((uint16_t)e->reg_freq_lo |
                                  ((uint16_t)e->reg_freq_hi << 8));
        freq += cry_pitch;
        e->reg_freq_lo = freq & 0xFF;
        e->reg_freq_hi = (freq >> 8) & 0xFF;
    }

    /* Vibrato */
    if (ch[CH_FLAGS2] & 0x01) {
        if (ch[CH_VIB_DELAY] > 0) {
            ch[CH_VIB_DELAY]--;
        } else {
            /* Decrement rate counter */
            uint8_t rate_cnt = ch[CH_VIB_RATE] & 0xF;
            if (rate_cnt > 0) {
                ch[CH_VIB_RATE] = (ch[CH_VIB_RATE] & 0xF0) | (rate_cnt - 1);
            } else {
                /* Reset rate counter from init value (high nybble) */
                uint8_t rate_init = (ch[CH_VIB_RATE] >> 4) & 0xF;
                ch[CH_VIB_RATE] = (ch[CH_VIB_RATE] & 0xF0) | rate_init;
                ch[CH_FLAGS3] ^= 0x01; /* toggle direction */
            }

            /* Apply vibrato: depth has up amount in high nybble, down in low */
            if (ch[CH_FLAGS3] & 0x01) {
                /* Going down */
                uint8_t sub = ch[CH_VIB_DEPTH] & 0xF;
                int16_t freq = (int16_t)((uint16_t)e->reg_freq_lo |
                                          ((uint16_t)(e->reg_freq_hi & 0x07) << 8));
                freq -= sub;
                if (freq < 0) freq = 0;
                e->reg_freq_lo = freq & 0xFF;
                e->reg_freq_hi = (freq >> 8) & 0x07;
            } else {
                /* Going up */
                uint8_t add = (ch[CH_VIB_DEPTH] >> 4) & 0xF;
                uint16_t freq = (uint16_t)e->reg_freq_lo |
                                ((uint16_t)(e->reg_freq_hi & 0x07) << 8);
                freq += add;
                e->reg_freq_lo = freq & 0xFF;
                e->reg_freq_hi = (freq >> 8) & 0x07;
            }
            ch[CH_REG_FLAGS] |= 0x40; /* freq update from vibrato */
        }
    }

    /* Note delay (for volume fading) */
    if (ch[CH_FLAGS2] & 0x08) {
        if (ch[CH_NOTE_DELAY_CNT] > 0) {
            ch[CH_NOTE_DELAY_CNT]--;
        } else {
            ch[CH_REG_FLAGS] |= 0x20; /* mute */
        }
    }
}

/* Write channel state to hardware register shadows */
static void channel_output_registers(AudioEngine *e, uint8_t *ch) {
    uint8_t hw_ch = e->current_channel & 3; /* 0-3 hardware channel */
    uint8_t flags = ch[CH_REG_FLAGS];

    if (flags & 0x20) {
        /* Mute: set volume to 0, retrigger */
        switch (hw_ch) {
        case 0:
            e->regs.nr12 = 0x08;
            e->regs.nr14 = (e->reg_freq_hi & 0x07) | 0x80;
            break;
        case 1:
            e->regs.nr22 = 0x08;
            e->regs.nr24 = (e->reg_freq_hi & 0x07) | 0x80;
            break;
        case 2:
            e->regs.nr30 = 0; /* disable wave */
            break;
        case 3:
            e->regs.nr42 = 0x08;
            e->regs.nr44 = 0x80;
            break;
        }
        return;
    }

    if (flags & 0x10) {
        /* Full note init (SFX-style or fresh note) */
        switch (hw_ch) {
        case 0:
            e->regs.nr11 = e->reg_duty | 0x3F;
            e->regs.nr12 = e->reg_vol_env;
            e->regs.nr13 = e->reg_freq_lo;
            e->regs.nr14 = (e->reg_freq_hi & 0x07) | 0x80;
            break;
        case 1:
            e->regs.nr21 = e->reg_duty | 0x3F;
            e->regs.nr22 = e->reg_vol_env;
            e->regs.nr23 = e->reg_freq_lo;
            e->regs.nr24 = (e->reg_freq_hi & 0x07) | 0x80;
            break;
        case 2:
            e->regs.nr31 = 0x3F;
            e->regs.nr30 = 0;
            /* Load wave pattern */
            {
                uint8_t wave_idx = e->reg_vol_env & 0xF;
                if (wave_idx < 5) {
                    memcpy(e->regs.wave_ram, audio_wave_patterns[wave_idx], 16);
                }
                /* NR32 volume: (vol_env & 0xF0) << 1, matching ASM sla instruction */
                e->regs.nr32 = (uint8_t)((e->reg_vol_env & 0xF0) << 1);
            }
            e->regs.nr30 = 0x80;
            e->regs.nr33 = e->reg_freq_lo;
            e->regs.nr34 = (e->reg_freq_hi & 0x07) | 0x80;
            break;
        case 3:
            e->regs.nr41 = 0x3F;
            e->regs.nr42 = e->reg_vol_env;
            e->regs.nr43 = e->reg_freq_lo;
            e->regs.nr44 = 0x80;
            break;
        }
        return;
    }

    /* Incremental register updates */
    switch (hw_ch) {
    case 0:
        if (flags & 0x08) e->regs.nr10 = e->nr10_shadow;
        if (flags & 0x02) {
            e->regs.nr13 = e->reg_freq_lo;
            e->regs.nr14 = e->reg_freq_hi & 0x07;
        }
        if (flags & 0x04) {
            e->regs.nr12 = e->reg_vol_env;
            e->regs.nr14 = (e->reg_freq_hi & 0x07) | 0x80;
        }
        if (flags & 0x40) {
            e->regs.nr13 = e->reg_freq_lo;
        }
        if (flags & 0x01) {
            uint8_t cur = e->regs.nr11;
            e->regs.nr11 = (cur & 0x3F) | e->reg_duty;
        }
        break;
    case 1:
        if (flags & 0x02) {
            e->regs.nr23 = e->reg_freq_lo;
            e->regs.nr24 = e->reg_freq_hi & 0x07;
        }
        if (flags & 0x04) {
            e->regs.nr22 = e->reg_vol_env;
            e->regs.nr24 = (e->reg_freq_hi & 0x07) | 0x80;
        }
        if (flags & 0x40) {
            e->regs.nr23 = e->reg_freq_lo;
        }
        if (flags & 0x01) {
            uint8_t cur = e->regs.nr21;
            e->regs.nr21 = (cur & 0x3F) | e->reg_duty;
        }
        break;
    case 2:
        if (flags & 0x02) {
            e->regs.nr33 = e->reg_freq_lo;
            e->regs.nr34 = e->reg_freq_hi & 0x07;
        }
        if (flags & 0x04) {
            e->regs.nr30 = 0;
            {
                uint8_t wave_idx = e->reg_vol_env & 0xF;
                if (wave_idx < 5) {
                    memcpy(e->regs.wave_ram, audio_wave_patterns[wave_idx], 16);
                }
                /* NR32 volume: (vol_env & 0xF0) << 1, matching ASM sla instruction */
                e->regs.nr32 = (uint8_t)((e->reg_vol_env & 0xF0) << 1);
            }
            e->regs.nr30 = 0x80;
            e->regs.nr33 = e->reg_freq_lo;
            e->regs.nr34 = (e->reg_freq_hi & 0x07) | 0x80;
        }
        if (flags & 0x40) {
            e->regs.nr33 = e->reg_freq_lo;
        }
        break;
    case 3:
        if (flags & 0x02) {
            e->regs.nr43 = e->reg_freq_lo;
        }
        if (flags & 0x04) {
            e->regs.nr42 = e->reg_vol_env;
            e->regs.nr44 = 0x80;
        }
        break;
    }
}

/* --- APU Synthesis --- */

static void apply_registers_to_apu(AudioEngine *e) {
    APUState *apu = &e->apu;
    APURegisters *r = &e->regs;

    /* Square channel 0 */
    apu->square[0].duty = (r->nr11 >> 6) & 3;
    apu->square[0].freq = ((uint16_t)(r->nr14 & 0x07) << 8) | r->nr13;
    if (r->nr14 & 0x80) {
        /* Trigger: reload volume and envelope from register */
        apu->square[0].volume = (r->nr12 >> 4) & 0xF;
        apu->square[0].env_dir = (r->nr12 >> 3) & 1 ? 1 : -1;
        apu->square[0].env_period = r->nr12 & 7;
        apu->square[0].env_timer = apu->square[0].env_period;
        apu->square[0].triggered = true;
        apu->square[0].active = true;
        r->nr14 &= ~0x80;
    }

    /* Square channel 1 */
    apu->square[1].duty = (r->nr21 >> 6) & 3;
    apu->square[1].freq = ((uint16_t)(r->nr24 & 0x07) << 8) | r->nr23;
    if (r->nr24 & 0x80) {
        /* Trigger: reload volume and envelope from register */
        apu->square[1].volume = (r->nr22 >> 4) & 0xF;
        apu->square[1].env_dir = (r->nr22 >> 3) & 1 ? 1 : -1;
        apu->square[1].env_period = r->nr22 & 7;
        apu->square[1].env_timer = apu->square[1].env_period;
        apu->square[1].triggered = true;
        apu->square[1].active = true;
        r->nr24 &= ~0x80;
    }

    /* Wave channel */
    apu->wave.freq = ((uint16_t)(r->nr34 & 0x07) << 8) | r->nr33;
    {
        uint8_t vol_code = (r->nr32 >> 5) & 3;
        apu->wave.volume_shift = vol_code;
    }
    apu->wave.active = (r->nr30 & 0x80) != 0;
    if (r->nr34 & 0x80) {
        apu->wave.triggered = true;
        apu->wave.phase = 0;
        r->nr34 &= ~0x80;
    }

    /* Noise channel */
    apu->noise.freq_code = r->nr43;
    if (r->nr44 & 0x80) {
        /* Trigger: reload volume and envelope from register */
        apu->noise.volume = (r->nr42 >> 4) & 0xF;
        apu->noise.env_dir = (r->nr42 >> 3) & 1 ? 1 : -1;
        apu->noise.env_period = r->nr42 & 7;
        apu->noise.env_timer = apu->noise.env_period;
        apu->noise.triggered = true;
        apu->noise.active = true;
        apu->noise.lfsr = 0x7FFF;
        r->nr44 &= ~0x80;
    }
}

static void synthesize_frame(AudioEngine *e) {
    APUState *apu = &e->apu;
    APURegisters *r = &e->regs;

    uint8_t master_left = (r->nr50 >> 4) & 0x7;
    uint8_t master_right = r->nr50 & 0x7;
    uint8_t panning = r->nr51;

    for (int s = 0; s < SAMPLES_PER_FRAME; s++) {
        int32_t left_mix = 0;
        int32_t right_mix = 0;

        /* Tick volume envelope at 64 Hz (every ~689 samples at 44100 Hz) */
        apu->env_clock += 64.0 / SAMPLE_RATE;
        if (apu->env_clock >= 1.0) {
            apu->env_clock -= 1.0;
            for (int sq = 0; sq < 2; sq++) {
                if (apu->square[sq].active && apu->square[sq].env_period > 0) {
                    if (apu->square[sq].env_timer > 0) {
                        apu->square[sq].env_timer--;
                    } else {
                        apu->square[sq].env_timer = apu->square[sq].env_period;
                        int new_vol = apu->square[sq].volume + apu->square[sq].env_dir;
                        if (new_vol >= 0 && new_vol <= 15)
                            apu->square[sq].volume = (uint8_t)new_vol;
                    }
                }
            }
            if (apu->noise.active && apu->noise.env_period > 0) {
                if (apu->noise.env_timer > 0) {
                    apu->noise.env_timer--;
                } else {
                    apu->noise.env_timer = apu->noise.env_period;
                    int new_vol = apu->noise.volume + apu->noise.env_dir;
                    if (new_vol >= 0 && new_vol <= 15)
                        apu->noise.volume = (uint8_t)new_vol;
                }
            }
        }

        /* Square channel 0 */
        if (apu->square[0].active) {
            double freq_hz = GBC_CPU_CLOCK / (32.0 * (2048 - apu->square[0].freq));
            apu->square[0].phase += freq_hz / SAMPLE_RATE;
            while (apu->square[0].phase >= 1.0) apu->square[0].phase -= 1.0;

            int phase_idx = (int)(apu->square[0].phase * 8) & 7;
            int sample = duty_table[apu->square[0].duty][phase_idx] ?
                         apu->square[0].volume : 0;

            if (panning & 0x10) left_mix += sample;
            if (panning & 0x01) right_mix += sample;
        }

        /* Square channel 1 */
        if (apu->square[1].active) {
            double freq_hz = GBC_CPU_CLOCK / (32.0 * (2048 - apu->square[1].freq));
            apu->square[1].phase += freq_hz / SAMPLE_RATE;
            while (apu->square[1].phase >= 1.0) apu->square[1].phase -= 1.0;

            int phase_idx = (int)(apu->square[1].phase * 8) & 7;
            int sample = duty_table[apu->square[1].duty][phase_idx] ?
                         apu->square[1].volume : 0;

            if (panning & 0x20) left_mix += sample;
            if (panning & 0x02) right_mix += sample;
        }

        /* Wave channel */
        if (apu->wave.active) {
            double freq_hz = GBC_CPU_CLOCK / (64.0 * (2048 - apu->wave.freq));
            apu->wave.phase += freq_hz / SAMPLE_RATE;
            while (apu->wave.phase >= 1.0) apu->wave.phase -= 1.0;

            int wave_idx = (int)(apu->wave.phase * 32) & 31;
            uint8_t wave_byte = r->wave_ram[wave_idx / 2];
            uint8_t sample_4bit = (wave_idx & 1) ? (wave_byte & 0xF) : (wave_byte >> 4);

            /* Volume shift: 0=mute, 1=100%, 2=50%, 3=25% */
            int sample = 0;
            switch (apu->wave.volume_shift) {
                case 0: sample = 0; break;
                case 1: sample = sample_4bit; break;
                case 2: sample = sample_4bit >> 1; break;
                case 3: sample = sample_4bit >> 2; break;
            }

            if (panning & 0x40) left_mix += sample;
            if (panning & 0x04) right_mix += sample;
        }

        /* Noise channel */
        if (apu->noise.active) {
            uint8_t shift = (apu->noise.freq_code >> 4) & 0xF;
            uint8_t divisor_code = apu->noise.freq_code & 0x07;
            uint8_t width = (apu->noise.freq_code >> 3) & 1;

            double divisor = divisor_code == 0 ? 0.5 : (double)divisor_code;
            double noise_freq = GBC_CPU_CLOCK / (divisor * (1 << (shift + 1))) / 8.0;

            apu->noise.timer += noise_freq / SAMPLE_RATE;
            while (apu->noise.timer >= 1.0) {
                apu->noise.timer -= 1.0;
                /* Clock LFSR */
                uint16_t lfsr = apu->noise.lfsr;
                uint8_t bit = (lfsr ^ (lfsr >> 1)) & 1;
                lfsr >>= 1;
                lfsr |= (bit << 14);
                if (width) {
                    lfsr &= ~(1 << 6);
                    lfsr |= (bit << 6);
                }
                apu->noise.lfsr = lfsr;
            }

            int sample = (apu->noise.lfsr & 1) ? 0 : apu->noise.volume;

            if (panning & 0x80) left_mix += sample;
            if (panning & 0x08) right_mix += sample;
        }

        /* Apply master volume and convert to S16 */
        left_mix = (left_mix * (master_left + 1)) / 8;
        right_mix = (right_mix * (master_right + 1)) / 8;

        /* Scale from 0-15 range to S16 range */
        double left_raw = left_mix * 400.0;
        double right_raw = right_mix * 400.0;

        /* DC-blocking high-pass filter: y[n] = x[n] - x[n-1] + alpha * y[n-1]
         * Removes DC offset that causes clicks when channels toggle on/off.
         * alpha = 0.999 gives cutoff ~7 Hz at 44100 Hz */
        #define DC_BLOCK_ALPHA 0.999
        double out_l = left_raw - e->dc_prev_in_l + DC_BLOCK_ALPHA * e->dc_prev_out_l;
        double out_r = right_raw - e->dc_prev_in_r + DC_BLOCK_ALPHA * e->dc_prev_out_r;
        e->dc_prev_in_l = left_raw;
        e->dc_prev_out_l = out_l;
        e->dc_prev_in_r = right_raw;
        e->dc_prev_out_r = out_r;

        /* Clamp to S16 range */
        if (out_l > 32767.0) out_l = 32767.0;
        if (out_l < -32768.0) out_l = -32768.0;
        if (out_r > 32767.0) out_r = 32767.0;
        if (out_r < -32768.0) out_r = -32768.0;

        e->output_buffer[s * 2] = (int16_t)out_l;
        e->output_buffer[s * 2 + 1] = (int16_t)out_r;
    }

    /* PCM overlay: if a Pikachu sound clip is playing, replace APU output.
     * The original ASM disables the audio engine during PCM playback
     * (PlayPikachuSoundClip writes raw samples to wave channel registers). */
    if (e->pcm_active_clip >= 0 && e->pcm_active_clip < 2) {
        int clip = e->pcm_active_clip;
        int16_t *pcm = e->pcm_buffers[clip];
        uint32_t len = e->pcm_lengths[clip];
        if (pcm && len > 0) {
            for (int s = 0; s < SAMPLES_PER_FRAME; s++) {
                if (e->pcm_position < len) {
                    e->output_buffer[s * 2]     = pcm[e->pcm_position * 2];
                    e->output_buffer[s * 2 + 1] = pcm[e->pcm_position * 2 + 1];
                    e->pcm_position++;
                } else {
                    e->pcm_active_clip = -1;
                    break;
                }
            }
        } else {
            e->pcm_active_clip = -1;
        }
    }
}

/* --- PCM Pikachu sound clip support --- */

static const char *pcm_clip_files[2] = {
    "audio/sound_clips/pi_ka_chu.wav",
    "audio/sound_clips/piiiiikaaaa.wav"
};

void audio_init_pcm(AudioEngine *e, const char *base_path) {
    if (!e) return;

    e->pcm_active_clip = -1;
    e->pcm_position = 0;

    for (int i = 0; i < 2; i++) {
        e->pcm_buffers[i] = NULL;
        e->pcm_lengths[i] = 0;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", base_path, pcm_clip_files[i]);

        SDL_AudioSpec wav_spec;
        uint8_t *wav_buf = NULL;
        uint32_t wav_len = 0;

        if (!SDL_LoadWAV(path, &wav_spec, &wav_buf, &wav_len)) {
            printf("[Audio] Warning: Could not load PCM clip '%s': %s\n",
                   path, SDL_GetError());
            continue;
        }

        /* Convert to match engine format: S16, 44100Hz, stereo */
        SDL_AudioCVT cvt;
        int ret = SDL_BuildAudioCVT(&cvt,
            wav_spec.format, wav_spec.channels, wav_spec.freq,
            AUDIO_S16SYS, 2, SAMPLE_RATE);

        if (ret < 0) {
            printf("[Audio] Warning: Could not build audio CVT for '%s': %s\n",
                   pcm_clip_files[i], SDL_GetError());
            SDL_FreeWAV(wav_buf);
            continue;
        }

        if (ret == 0) {
            /* No conversion needed — already in target format */
            uint32_t num_samples = wav_len / (2 * sizeof(int16_t)); /* stereo S16 */
            e->pcm_buffers[i] = (int16_t *)malloc(wav_len);
            if (e->pcm_buffers[i]) {
                memcpy(e->pcm_buffers[i], wav_buf, wav_len);
                e->pcm_lengths[i] = num_samples;
            }
        } else {
            /* Conversion needed */
            cvt.len = (int)wav_len;
            cvt.buf = (uint8_t *)malloc((size_t)(wav_len * cvt.len_mult));
            if (!cvt.buf) {
                SDL_FreeWAV(wav_buf);
                continue;
            }
            memcpy(cvt.buf, wav_buf, wav_len);

            if (SDL_ConvertAudio(&cvt) < 0) {
                printf("[Audio] Warning: Audio conversion failed for '%s': %s\n",
                       pcm_clip_files[i], SDL_GetError());
                free(cvt.buf);
                SDL_FreeWAV(wav_buf);
                continue;
            }

            uint32_t converted_len = (uint32_t)cvt.len_cvt;
            uint32_t num_samples = converted_len / (2 * sizeof(int16_t)); /* stereo S16 */
            e->pcm_buffers[i] = (int16_t *)malloc(converted_len);
            if (e->pcm_buffers[i]) {
                memcpy(e->pcm_buffers[i], cvt.buf, converted_len);
                e->pcm_lengths[i] = num_samples;
            }
            free(cvt.buf);
        }

        SDL_FreeWAV(wav_buf);
        printf("[Audio] Loaded PCM clip '%s': %u samples\n",
               pcm_clip_files[i], e->pcm_lengths[i]);
    }
}

void audio_play_pcm(AudioEngine *e, int clip_index) {
    if (!e || clip_index < 0 || clip_index > 1) return;
    if (!e->pcm_buffers[clip_index] || e->pcm_lengths[clip_index] == 0) return;

    e->pcm_active_clip = clip_index;
    e->pcm_position = 0;
}

void audio_cleanup_pcm(AudioEngine *e) {
    if (!e) return;
    for (int i = 0; i < 2; i++) {
        if (e->pcm_buffers[i]) {
            free(e->pcm_buffers[i]);
            e->pcm_buffers[i] = NULL;
        }
        e->pcm_lengths[i] = 0;
    }
    e->pcm_active_clip = -1;
}
