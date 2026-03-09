#!/usr/bin/env python3
"""
convert_audio.py - Convert Pokemon Pinball GBC ASM audio data to C byte arrays.

Parses RGBDS ASM music/SFX/drum files from the disassembly and generates
a C header file (audio_data.h) with byte arrays for the native audio engine.

Usage: python convert_audio.py <project_root> <output_file>
"""

import re
import os
import sys
from pathlib import Path

# --- Note pitch constants (from constants/audio_constants.asm) ---
PITCHES = {
    'C_': 1, 'C#': 2, 'D_': 3, 'D#': 4, 'E_': 5, 'F_': 6,
    'F#': 7, 'G_': 8, 'G#': 9, 'A_': 10, 'A#': 11, 'B_': 12
}

# --- Bank definitions ---
BANKS = [
    {
        'id': 0x0F, 'index': 0, 'suffix': 'BankF',
        'songs': ['Nothing0F', 'RedField', 'CatchEmRed', 'HurryUpRed',
                  'Pokedex', 'GastlyInTheGraveyard', 'HaunterInTheGraveyard',
                  'GengarInTheGraveyard'],
        'music_files': ['nothing0f', 'redfield', 'catchemred', 'hurryupred',
                        'pokedex', 'gastlyinthegraveyard', 'haunterinthegraveyard',
                        'gengarinthegraveyard'],
        'drumkit_file': 'drumkits_0f.asm',
    },
    {
        'id': 0x10, 'index': 1, 'suffix': 'Bank10',
        'songs': ['Nothing10', 'BlueField', 'CatchEmBlue', 'HurryUpBlue',
                  'HiScore', 'GameOver'],
        'music_files': ['nothing10', 'bluefield', 'catchemblue', 'hurryupblue',
                        'hiscore', 'gameover'],
        'drumkit_file': 'drumkits_10.asm',
    },
    {
        'id': 0x11, 'index': 2, 'suffix': 'Bank11',
        'songs': ['Nothing11', 'WhackTheDiglett', 'WhackTheDugtrio',
                  'SeelStage', 'Title'],
        'music_files': ['nothing11', 'whackthediglett', 'whackthedugtrio',
                        'seelstage', 'title'],
        'drumkit_file': 'drumkits_11.asm',
    },
    {
        'id': 0x12, 'index': 3, 'suffix': 'Bank12',
        'songs': ['Nothing12', 'MewtwoStage', 'Options', 'FieldSelect',
                  'MeowthStage'],
        'music_files': ['nothing12', 'mewtwostage', 'options', 'fieldselect',
                        'meowthstage'],
        'drumkit_file': 'drumkits_12.asm',
    },
    {
        'id': 0x13, 'index': 4, 'suffix': 'Bank13',
        'songs': ['Nothing13', 'EndCredits', 'NameEntry'],
        'music_files': ['nothing13', 'endcredits', 'nameentry'],
        'drumkit_file': 'drumkits_13.asm',
    },
]


def parse_int(s):
    """Parse integer from ASM format ($hex, 0xhex, %bin, decimal, negative, TRUE/FALSE)."""
    s = s.strip()
    if not s:
        return 0
    # Handle boolean constants
    if s.upper() == 'TRUE':
        return 1
    if s.upper() == 'FALSE':
        return 0
    neg = False
    if s.startswith('-'):
        neg = True
        s = s[1:].strip()
    if s.startswith('$'):
        val = int(s[1:], 16)
    elif s.startswith('0x') or s.startswith('0X'):
        val = int(s, 16)
    elif s.startswith('%'):
        val = int(s[1:], 2)
    else:
        val = int(s)
    return -val if neg else val


def encode_vol_env(vol, env):
    """Encode volume/envelope nybble pair with sign handling."""
    if env < 0:
        return ((vol & 0xF) << 4) | (0x8 | ((-env) & 0x7))
    else:
        return ((vol & 0xF) << 4) | (env & 0xF)


class Assembler:
    """Two-pass assembler for sound data bytecode."""

    def __init__(self):
        self.data = bytearray()
        self.labels = {}
        self.fixups = []  # (byte_offset, label_name)
        self._num_channels = 0

    @property
    def offset(self):
        return len(self.data)

    def label(self, name):
        self.labels[name] = self.offset

    def byte(self, b):
        self.data.append(b & 0xFF)

    def word_le(self, value):
        self.byte(value & 0xFF)
        self.byte((value >> 8) & 0xFF)

    def word_be(self, value):
        self.byte((value >> 8) & 0xFF)
        self.byte(value & 0xFF)

    def addr_fixup(self, label_name):
        self.fixups.append((self.offset, label_name))
        self.byte(0)
        self.byte(0)

    def resolve(self):
        for off, lbl in self.fixups:
            if lbl not in self.labels:
                raise KeyError(f"Unresolved label: {lbl}")
            addr = self.labels[lbl]
            self.data[off] = addr & 0xFF
            self.data[off + 1] = (addr >> 8) & 0xFF

    def process_line(self, line):
        """Process one line of ASM source."""
        # Strip comments
        ci = line.find(';')
        if ci >= 0:
            line = line[:ci]
        line = line.rstrip()
        if not line.strip():
            return

        stripped = line.strip()

        # Label detection: starts at column 0 (no leading whitespace) and has ':'
        if line and not line[0].isspace() and ':' in line:
            colon = line.index(':')
            label_name = line[:colon].strip()
            if label_name and not label_name.startswith('.'):
                self.label(label_name)
            rest = line[colon+1:].strip()
            if rest:
                self._process_instruction(rest)
            return

        # Local label (starts with .)
        if stripped.startswith('.') and ':' in stripped:
            colon = stripped.index(':')
            label_name = stripped[:colon].strip()
            self.label(label_name)
            rest = stripped[colon+1:].strip()
            if rest:
                self._process_instruction(rest)
            return

        self._process_instruction(stripped)

    def _process_instruction(self, instr):
        parts = instr.split(None, 1)
        if not parts:
            return
        mnemonic = parts[0].strip().lower()
        args_str = parts[1] if len(parts) > 1 else ''

        # Parse comma-separated args
        args = []
        if args_str.strip():
            args = [a.strip() for a in args_str.split(',')]

        # Dispatch
        handler = {
            'channel_count': self._h_channel_count,
            'channel': self._h_channel,
            'note': self._h_note,
            'drum_note': self._h_drum_note,
            'rest': self._h_rest,
            'octave': self._h_octave,
            'note_type': self._h_note_type,
            'drum_speed': self._h_drum_speed,
            'transpose': self._h_transpose,
            'tempo': self._h_tempo,
            'duty_cycle': self._h_duty_cycle,
            'volume_envelope': self._h_volume_envelope,
            'pitch_sweep': self._h_pitch_sweep,
            'duty_cycle_pattern': self._h_duty_cycle_pattern,
            'toggle_sfx': self._h_toggle_sfx,
            'pitch_slide': self._h_pitch_slide,
            'vibrato': self._h_vibrato,
            'unknownmusic0xe2': self._h_unknown_e2,
            'toggle_noise': self._h_toggle_noise,
            'force_stereo_panning': self._h_force_stereo_panning,
            'volume': self._h_volume,
            'pitch_offset': self._h_pitch_offset,
            'unknownmusic0xe7': self._h_unknown_1byte,
            'unknownmusic0xe8': self._h_unknown_1byte,
            'tempo_relative': self._h_tempo_relative,
            'restart_channel': self._h_restart_channel,
            'new_song': self._h_new_song,
            'sfx_priority_on': self._h_sfx_priority_on,
            'sfx_priority_off': self._h_sfx_priority_off,
            'unknownmusic0xee': self._h_unknown_ee,
            'stereo_panning': self._h_stereo_panning,
            'sfx_toggle_noise': self._h_sfx_toggle_noise,
            'music0xf1': self._h_nop_cmd,
            'music0xf2': self._h_nop_cmd,
            'music0xf3': self._h_nop_cmd,
            'music0xf4': self._h_nop_cmd,
            'music0xf5': self._h_nop_cmd,
            'music0xf6': self._h_nop_cmd,
            'music0xf7': self._h_nop_cmd,
            'music0xf8': self._h_nop_cmd,
            'unknownmusic0xf9': self._h_nop_cmd,
            'set_condition': self._h_set_condition,
            'sound_jump_if': self._h_sound_jump_if,
            'sound_jump': self._h_sound_jump,
            'sound_loop': self._h_sound_loop,
            'sound_call': self._h_sound_call,
            'sound_ret': self._h_sound_ret,
            'square_note': self._h_square_note,
            'noise_note': self._h_noise_note,
        }.get(mnemonic)

        if handler:
            handler(args, mnemonic)

    # --- Macro handlers ---

    def _h_channel_count(self, args, _):
        self._num_channels = parse_int(args[0]) - 1

    def _h_channel(self, args, _):
        ch_id = parse_int(args[0]) - 1
        label = args[1].strip()
        self.byte(((self._num_channels << 2) << 4) | (ch_id & 0xF))
        self._num_channels = 0
        self.addr_fixup(label)

    def _h_note(self, args, _):
        pitch = PITCHES.get(args[0].strip(), 0)
        length = parse_int(args[1])
        self.byte((pitch << 4) | ((length - 1) & 0xF))

    def _h_drum_note(self, args, _):
        inst = parse_int(args[0])
        length = parse_int(args[1])
        self.byte((inst << 4) | ((length - 1) & 0xF))

    def _h_rest(self, args, _):
        length = parse_int(args[0])
        self.byte((0 << 4) | ((length - 1) & 0xF))

    def _h_octave(self, args, _):
        n = parse_int(args[0])
        self.byte(0xD0 | (8 - n))

    def _h_note_type(self, args, _):
        self.byte(0xD8)
        self.byte(parse_int(args[0]) & 0xFF)
        if len(args) >= 3:
            self.byte(encode_vol_env(parse_int(args[1]), parse_int(args[2])))

    def _h_drum_speed(self, args, _):
        self.byte(0xD8)
        self.byte(parse_int(args[0]) & 0xFF)

    def _h_transpose(self, args, _):
        self.byte(0xD9)
        octaves = parse_int(args[0])
        pitches = parse_int(args[1])
        self.byte(((octaves & 0xF) << 4) | (pitches & 0xF))

    def _h_tempo(self, args, _):
        self.byte(0xDA)
        self.word_be(parse_int(args[0]))

    def _h_duty_cycle(self, args, _):
        self.byte(0xDB)
        self.byte(parse_int(args[0]) & 0xFF)

    def _h_volume_envelope(self, args, _):
        self.byte(0xDC)
        self.byte(encode_vol_env(parse_int(args[0]), parse_int(args[1])))

    def _h_pitch_sweep(self, args, _):
        self.byte(0xDD)
        self.byte(encode_vol_env(parse_int(args[0]), parse_int(args[1])))

    def _h_duty_cycle_pattern(self, args, _):
        self.byte(0xDE)
        a, b, c, d = [parse_int(x) for x in args[:4]]
        self.byte(((a & 3) << 6) | ((b & 3) << 4) | ((c & 3) << 2) | (d & 3))

    def _h_toggle_sfx(self, args, _):
        self.byte(0xDF)

    def _h_pitch_slide(self, args, _):
        self.byte(0xE0)
        duration = parse_int(args[0])
        octave = parse_int(args[1])
        pitch = parse_int(args[2])
        self.byte((duration - 1) & 0xFF)
        self.byte(((8 - octave) << 4) | (pitch % 12))

    def _h_vibrato(self, args, _):
        self.byte(0xE1)
        self.byte(parse_int(args[0]) & 0xFF)
        if len(args) >= 3:
            extent = parse_int(args[1])
            rate = parse_int(args[2])
            self.byte(((extent & 0xF) << 4) | (rate & 0xF))
        else:
            self.byte(parse_int(args[1]) & 0xFF)

    def _h_unknown_e2(self, args, _):
        self.byte(0xE2)
        self.byte(parse_int(args[0]) & 0xFF)

    def _h_toggle_noise(self, args, _):
        self.byte(0xE3)
        if args:
            self.byte(parse_int(args[0]) & 0xFF)

    def _h_force_stereo_panning(self, args, _):
        self.byte(0xE4)
        left = parse_int(args[0])
        right = parse_int(args[1])
        self.byte((0xF0 if left else 0) | (0x0F if right else 0))

    def _h_volume(self, args, _):
        self.byte(0xE5)
        if len(args) >= 2:
            left = parse_int(args[0])
            right = parse_int(args[1])
            self.byte(((left & 0xF) << 4) | (right & 0xF))
        else:
            self.byte(parse_int(args[0]) & 0xFF)

    def _h_pitch_offset(self, args, _):
        self.byte(0xE6)
        self.word_be(parse_int(args[0]))

    def _h_unknown_1byte(self, args, mnemonic):
        # unknownmusic0xe7, unknownmusic0xe8
        cmd = int(mnemonic[-2:], 16)
        self.byte(cmd)
        self.byte(parse_int(args[0]) & 0xFF)

    def _h_tempo_relative(self, args, _):
        self.byte(0xE9)
        val = parse_int(args[0])
        self.word_be(val & 0xFFFF)

    def _h_restart_channel(self, args, _):
        self.byte(0xEA)
        self.addr_fixup(args[0].strip())

    def _h_new_song(self, args, _):
        self.byte(0xEB)
        self.word_be(parse_int(args[0]))

    def _h_sfx_priority_on(self, args, _):
        self.byte(0xEC)

    def _h_sfx_priority_off(self, args, _):
        self.byte(0xED)

    def _h_unknown_ee(self, args, _):
        self.byte(0xEE)
        self.addr_fixup(args[0].strip())

    def _h_stereo_panning(self, args, _):
        self.byte(0xEF)
        left = parse_int(args[0])
        right = parse_int(args[1])
        self.byte((0xF0 if left else 0) | (0x0F if right else 0))

    def _h_sfx_toggle_noise(self, args, _):
        self.byte(0xF0)
        if args:
            self.byte(parse_int(args[0]) & 0xFF)

    def _h_nop_cmd(self, args, mnemonic):
        # music0xf1 through music0xf8, unknownmusic0xf9
        if mnemonic.startswith('music0x'):
            cmd = int(mnemonic[7:], 16)
        elif mnemonic.startswith('unknownmusic0x'):
            cmd = int(mnemonic[14:], 16)
        else:
            return
        self.byte(cmd)

    def _h_set_condition(self, args, _):
        self.byte(0xFA)
        self.byte(parse_int(args[0]) & 0xFF)

    def _h_sound_jump_if(self, args, _):
        self.byte(0xFB)
        self.byte(parse_int(args[0]) & 0xFF)
        self.addr_fixup(args[1].strip())

    def _h_sound_jump(self, args, _):
        self.byte(0xFC)
        self.addr_fixup(args[0].strip())

    def _h_sound_loop(self, args, _):
        self.byte(0xFD)
        self.byte(parse_int(args[0]) & 0xFF)
        self.addr_fixup(args[1].strip())

    def _h_sound_call(self, args, _):
        self.byte(0xFE)
        self.addr_fixup(args[0].strip())

    def _h_sound_ret(self, args, _):
        self.byte(0xFF)

    def _h_square_note(self, args, _):
        length = parse_int(args[0])
        vol = parse_int(args[1])
        env = parse_int(args[2])
        freq = parse_int(args[3])
        self.byte(length & 0xFF)
        self.byte(encode_vol_env(vol, env))
        self.word_le(freq & 0xFFFF)

    def _h_noise_note(self, args, _):
        length = parse_int(args[0])
        vol = parse_int(args[1])
        env = parse_int(args[2])
        freq = parse_int(args[3])
        self.byte(length & 0xFF)
        self.byte(encode_vol_env(vol, env))
        self.byte(freq & 0xFF)


def assemble_file(filepath):
    """Assemble a single ASM file into bytecode."""
    asm = Assembler()
    with open(filepath, 'r') as f:
        for line in f:
            asm.process_line(line)
    asm.resolve()
    return asm


def assemble_files(filepaths):
    """Assemble multiple ASM files into one bytecode block (for cross-file refs)."""
    asm = Assembler()
    for fp in filepaths:
        with open(fp, 'r') as f:
            for line in f:
                asm.process_line(line)
    asm.resolve()
    return asm


def assemble_drum_file(filepath):
    """Parse a drumkit ASM file, return dict of label→bytecode for each drum pattern,
    plus the kit table structure."""
    # We need to assemble the whole file as one unit since drum kits reference
    # drum patterns by label within the same file
    asm = Assembler()
    with open(filepath, 'r') as f:
        for line in f:
            asm.process_line(line)
    asm.resolve()
    return asm


def format_bytes(data, indent='    ', per_line=16):
    """Format a byte array as C hex literals."""
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i+per_line]
        hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f'{indent}{hex_str},')
    return '\n'.join(lines)


def c_name(s):
    """Convert a song/SFX name to a C identifier."""
    return re.sub(r'[^a-zA-Z0-9_]', '_', s).lower()


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <project_root> <output_file>")
        sys.exit(1)

    root = Path(sys.argv[1])
    output = Path(sys.argv[2])
    audio_dir = root / 'audio'
    music_dir = audio_dir / 'music'

    out_lines = []
    out_lines.append('/* AUTO-GENERATED by convert_audio.py - DO NOT EDIT */')
    out_lines.append('#ifndef AUDIO_DATA_H')
    out_lines.append('#define AUDIO_DATA_H')
    out_lines.append('')
    out_lines.append('#include <stdint.h>')
    out_lines.append('')

    # --- Wave patterns (from audio/wave_samples.asm, shared across all banks) ---
    out_lines.append('/* Wave patterns (5 x 16 bytes) */')
    out_lines.append('static const uint8_t audio_wave_patterns[5][16] = {')
    wave_data = [
        [0x02,0x46,0x8A,0xCE,0xFF,0xFE,0xED,0xDC,0xCB,0xA9,0x87,0x65,0x44,0x33,0x22,0x11],
        [0x02,0x46,0x8A,0xCE,0xEF,0xFF,0xFE,0xEE,0xDD,0xCB,0xA9,0x87,0x65,0x43,0x22,0x11],
        [0x01,0x23,0x43,0x21,0xFE,0xCA,0x8A,0xCE,0x01,0x23,0x43,0x21,0xFE,0xCA,0x8A,0xCE],
        [0x00,0x11,0x22,0x33,0x44,0x33,0x22,0x11,0xFF,0xEE,0xCC,0xAA,0x88,0xAA,0xCC,0xEE],
        [0x00,0x11,0x22,0x33,0x44,0x33,0x22,0x11,0xFF,0xEE,0xCC,0xAA,0x88,0xAA,0xCC,0xEE],
    ]
    for i, wp in enumerate(wave_data):
        hex_str = ', '.join(f'0x{b:02x}' for b in wp)
        comma = ',' if i < len(wave_data)-1 else ''
        out_lines.append(f'    {{{hex_str}}}{comma}')
    out_lines.append('};')
    out_lines.append('')

    # --- Frequency table (from Data_3cb20, shared across all banks) ---
    freq_table = [
        0x0000, 0xf82c, 0xf89d, 0xf907, 0xf96b, 0xf9ca,
        0xfa23, 0xfa77, 0xfac7, 0xfb12, 0xfb58, 0xfb9b,
        0xfbda, 0xfc16, 0xfc4e, 0xfc83, 0xfcb5, 0xfce5,
        0xfd11, 0xfd3b, 0xfd63, 0xfd89, 0xfdac, 0xfdcd,
        0xfded,
    ]
    out_lines.append('/* Frequency lookup table (25 entries) */')
    out_lines.append(f'static const uint16_t audio_freq_table[{len(freq_table)}] = {{')
    for i in range(0, len(freq_table), 6):
        chunk = freq_table[i:i+6]
        hex_str = ', '.join(f'0x{v:04x}' for v in chunk)
        out_lines.append(f'    {hex_str},')
    out_lines.append('};')
    out_lines.append('')

    # --- Channel panning table (from Data_3cc8e) ---
    out_lines.append('/* Channel panning masks */')
    out_lines.append('static const uint8_t audio_channel_panning[4] = { 0x11, 0x22, 0x44, 0x88 };')
    out_lines.append('')

    # --- Process drum data (from bank 0F - shared across all banks) ---
    out_lines.append('/* ============== Drum pattern data ============== */')
    out_lines.append('')

    # Process each bank's drum file and store results
    all_drum_data = {}
    for bank in BANKS:
        drum_file = audio_dir / bank['drumkit_file']
        if drum_file.exists():
            asm = assemble_drum_file(str(drum_file))
            all_drum_data[bank['index']] = asm
            var_name = f"audio_drums_bank{bank['index']}"
            out_lines.append(f'/* Drum data for bank 0x{bank["id"]:02X} ({len(asm.data)} bytes) */')
            out_lines.append(f'static const uint8_t {var_name}[] = {{')
            out_lines.append(format_bytes(asm.data))
            out_lines.append('};')
            out_lines.append('')

            # Output the drum kit and pattern offset tables
            # Find DrumkitN labels and DrumN labels
            kit_labels = []
            pattern_labels = []
            for lbl, off in sorted(asm.labels.items(), key=lambda x: x[1]):
                if lbl.startswith('Drumkit') and '_' in lbl:
                    kit_labels.append((lbl, off))
                elif lbl.startswith('Drum') and '_' in lbl:
                    pattern_labels.append((lbl, off))

            # Output kit table (each kit is 16 2-byte pointers = offsets into drum data)
            num_kits = len(kit_labels)
            out_lines.append(f'/* Drum kit tables for bank 0x{bank["id"]:02X}: {num_kits} kits */')
            # Each kit entry in the ASM is a dw (2-byte pointer) to a pattern.
            # In our assembled data, these are already resolved offsets stored as
            # little-endian 16-bit values in the byte array.
            # The kit table starts at the offset of the Drumkits label.
            drumkits_label = f'Drumkits_{bank["suffix"]}'
            if drumkits_label in asm.labels:
                kits_offset = asm.labels[drumkits_label]
                out_lines.append(f'#define AUDIO_DRUMS_BANK{bank["index"]}_KITS_OFFSET {kits_offset}')
            out_lines.append('')

    # --- Process SFX (from bank 0F, identical across banks) ---
    out_lines.append('/* ============== Sound Effects ============== */')
    out_lines.append('')

    sfx_file = audio_dir / 'sfx_0f.asm'
    if sfx_file.exists():
        # Read the SFX file and split into individual SFX
        with open(sfx_file, 'r') as f:
            sfx_content = f.read()

        # Find all SFX entry points: Sfx_SoundEffectN_BankF:
        sfx_entries = re.findall(r'^(Sfx_SoundEffect\d+_BankF):', sfx_content, re.MULTILINE)

        # Assemble the entire SFX file as one unit
        sfx_asm = Assembler()
        for line in sfx_content.split('\n'):
            sfx_asm.process_line(line)
        sfx_asm.resolve()

        # Find the byte range for each SFX
        sfx_offsets = []
        for entry in sfx_entries:
            if entry in sfx_asm.labels:
                sfx_offsets.append((entry, sfx_asm.labels[entry]))

        # Sort by offset
        sfx_offsets.sort(key=lambda x: x[1])

        # Output each SFX as its own array
        for i, (name, start) in enumerate(sfx_offsets):
            if i + 1 < len(sfx_offsets):
                end = sfx_offsets[i + 1][1]
            else:
                end = len(sfx_asm.data)
            sfx_data = sfx_asm.data[start:end]
            sfx_num = int(re.search(r'(\d+)', name).group(1))
            # Adjust internal addresses: subtract start offset
            adjusted = adjust_offsets(sfx_asm, start, end)
            var_name = f'audio_sfx_{sfx_num:02d}'
            out_lines.append(f'static const uint8_t {var_name}[] = {{ /* {name} */')
            out_lines.append(format_bytes(adjusted))
            out_lines.append('};')
            out_lines.append('')

        # SFX index table
        num_sfx = len(sfx_offsets)
        out_lines.append(f'#define AUDIO_NUM_SFX {num_sfx}')
        out_lines.append('')
        out_lines.append('typedef struct { const uint8_t *data; uint16_t size; } AudioDataEntry;')
        out_lines.append('')
        out_lines.append(f'static const AudioDataEntry audio_sfx_table[{num_sfx}] = {{')
        for i in range(num_sfx):
            out_lines.append(f'    {{ audio_sfx_{i:02d}, sizeof(audio_sfx_{i:02d}) }},')
        out_lines.append('};')
        out_lines.append('')

    # --- Process Cry data (from bank 0F, identical across banks) ---
    # NOTE: Unlike SFX where each entry's channel data follows its header,
    # cries have ALL headers at the top and ALL channel data at the bottom.
    # So we emit ONE shared data blob and per-cry offset/size entries.
    out_lines.append('/* ============== Pokemon Cry Data ============== */')
    out_lines.append('')

    cry_file = audio_dir / 'cries_0f.asm'
    if cry_file.exists():
        # Assemble the entire cry file as one unit
        cry_asm = Assembler()
        with open(cry_file, 'r') as f:
            for line in f:
                cry_asm.process_line(line)
        cry_asm.resolve()

        # Find all Cry_XX_BankF entry points (38 patterns: 0x00-0x25)
        cry_entries = []
        for i in range(0x26):
            label = f'Cry_{i:02X}_BankF'
            if label in cry_asm.labels:
                cry_entries.append((label, cry_asm.labels[label]))
        cry_entries.sort(key=lambda x: x[1])

        # Output the ENTIRE assembled cry data as one shared array.
        # Channel addresses are already correct as offsets into this blob.
        total_data = bytes(cry_asm.data)
        out_lines.append(f'static const uint8_t audio_cry_blob[] = {{ /* all cry data, {len(total_data)} bytes */')
        out_lines.append(format_bytes(total_data))
        out_lines.append('};')
        out_lines.append('')

        # Cry index table: each entry is an offset into the shared blob
        num_cries = len(cry_entries)
        out_lines.append(f'#define AUDIO_NUM_CRIES {num_cries}')
        out_lines.append('')
        out_lines.append(f'/* Each cry entry: offset into audio_cry_blob, size of the entire blob */')
        out_lines.append(f'static const AudioDataEntry audio_cry_table[{num_cries}] = {{')
        for i, (name, start) in enumerate(cry_entries):
            out_lines.append(f'    {{ audio_cry_blob, sizeof(audio_cry_blob) }}, /* {name} at offset {start} */')
        out_lines.append('};')
        out_lines.append('')
        # Per-cry offset table so audio_play_cry knows where each header starts
        out_lines.append(f'static const uint16_t audio_cry_offsets[{num_cries}] = {{')
        for i, (name, start) in enumerate(cry_entries):
            out_lines.append(f'    {start}, /* {name} */')
        out_lines.append('};')
        out_lines.append('')

        # CryData lookup table (151 Pokemon -> cry pattern + pitch + length)
        # Parse from engine_0f.asm CryData_BankF section
        engine_file = audio_dir / 'engine_0f.asm'
        if engine_file.exists():
            cry_data_entries = []
            with open(engine_file, 'r') as f:
                in_cry_data = False
                for line in f:
                    stripped = line.strip()
                    if 'CryData_BankF:' in line:
                        in_cry_data = True
                        continue
                    if in_cry_data:
                        if stripped.startswith('dw '):
                            # Parse: dw $XXXX, $YYYY, $ZZZZ  ; COMMENT
                            comment_idx = stripped.find(';')
                            comment = stripped[comment_idx+1:].strip() if comment_idx >= 0 else ''
                            data_part = stripped[:comment_idx] if comment_idx >= 0 else stripped
                            data_part = data_part[3:].strip()  # Remove 'dw '
                            values = [parse_int(v.strip()) for v in data_part.split(',')]
                            if len(values) == 3:
                                cry_data_entries.append((values[0], values[1], values[2], comment))
                        elif stripped and not stripped.startswith(';') and not stripped.startswith('dw'):
                            break  # End of CryData section

            out_lines.append(f'/* Pokemon cry lookup table ({len(cry_data_entries)} entries) */')
            out_lines.append('typedef struct { uint8_t cry_id; uint16_t pitch; uint16_t length; } CryDataEntry;')
            out_lines.append('')
            out_lines.append(f'static const CryDataEntry audio_cry_data[{len(cry_data_entries)}] = {{')
            for cry_id, pitch, length, comment in cry_data_entries:
                cmt = f' /* {comment} */' if comment else ''
                out_lines.append(f'    {{ 0x{cry_id:02x}, 0x{pitch:04x}, 0x{length:04x} }},{cmt}')
            out_lines.append('};')
            out_lines.append('')
            out_lines.append(f'#define AUDIO_NUM_POKEMON {len(cry_data_entries)}')
            out_lines.append('')

    # --- Process music for each bank ---
    out_lines.append('/* ============== Music Data ============== */')
    out_lines.append('')

    # Cross-file dependencies: some songs reference labels in other songs
    # These must be assembled together as one unit, then extracted by entry label
    CROSS_FILE_DEPS = {
        # song_file -> [prerequisite files that must be assembled first]
        'haunterinthegraveyard': ['gastlyinthegraveyard'],
    }

    max_songs_per_bank = max(len(b['songs']) for b in BANKS)
    song_arrays = {}  # (bank_index, song_index) -> var_name

    for bank in BANKS:
        out_lines.append(f'/* --- Bank 0x{bank["id"]:02X} songs --- */')
        for song_idx, (song_name, music_file) in enumerate(
                zip(bank['songs'], bank['music_files'])):
            filepath = music_dir / f'{music_file}.asm'
            if not filepath.exists():
                print(f"Warning: {filepath} not found, skipping")
                continue

            try:
                if music_file in CROSS_FILE_DEPS:
                    # Assemble prerequisite files first, then this file
                    prereq_files = [str(music_dir / f'{dep}.asm')
                                    for dep in CROSS_FILE_DEPS[music_file]]
                    all_files = prereq_files + [str(filepath)]
                    asm = assemble_files(all_files)
                    # The song entry point is the label for this specific song
                    entry_label = f'Music_{song_name}'
                    if entry_label in asm.labels:
                        entry_offset = asm.labels[entry_label]
                        # Extract from entry_offset to end, adjust offsets
                        # Actually we need the whole blob since sound_call
                        # can reference the prerequisite data
                        # Just use the full assembled data as-is
                    # Use full data - the engine will use the entry offset
                else:
                    asm = assemble_file(str(filepath))
            except Exception as e:
                print(f"Error assembling {filepath}: {e}")
                # Create a minimal "nothing" song
                asm = Assembler()
                asm.data = bytearray([0xC0, 0x03, 0x00, 0xFF])
                continue

            var_name = f'audio_song_b{bank["index"]}_{c_name(song_name)}'
            song_arrays[(bank['index'], song_idx)] = var_name
            out_lines.append(f'/* {song_name} ({len(asm.data)} bytes) */')
            out_lines.append(f'static const uint8_t {var_name}[] = {{')
            out_lines.append(format_bytes(asm.data))
            out_lines.append('};')

            # If cross-file, also emit the entry offset
            if music_file in CROSS_FILE_DEPS:
                entry_label = f'Music_{song_name}'
                if entry_label in asm.labels:
                    offset = asm.labels[entry_label]
                    out_lines.append(f'#define {var_name.upper()}_ENTRY_OFFSET {offset}')
            out_lines.append('')

    # Song index table
    out_lines.append(f'#define AUDIO_NUM_BANKS 5')
    out_lines.append(f'#define AUDIO_MAX_SONGS_PER_BANK {max_songs_per_bank}')
    out_lines.append('')
    out_lines.append(f'static const AudioDataEntry audio_song_table[AUDIO_NUM_BANKS][AUDIO_MAX_SONGS_PER_BANK] = {{')
    for bank in BANKS:
        out_lines.append(f'    {{ /* Bank 0x{bank["id"]:02X} */')
        for song_idx in range(max_songs_per_bank):
            key = (bank['index'], song_idx)
            if key in song_arrays:
                vn = song_arrays[key]
                out_lines.append(f'        {{ {vn}, sizeof({vn}) }},')
            else:
                out_lines.append(f'        {{ NULL, 0 }},')
        out_lines.append('    },')
    out_lines.append('};')
    out_lines.append('')

    # Bank ID to index mapping
    out_lines.append('/* Map ROM bank ID to array index */')
    out_lines.append('static inline int audio_bank_to_index(uint8_t bank) {')
    out_lines.append('    switch (bank) {')
    for b in BANKS:
        out_lines.append(f'        case 0x{b["id"]:02X}: return {b["index"]};')
    out_lines.append('        default: return 0;')
    out_lines.append('    }')
    out_lines.append('}')
    out_lines.append('')

    out_lines.append('#endif /* AUDIO_DATA_H */')
    out_lines.append('')

    # Write output
    output.parent.mkdir(parents=True, exist_ok=True)
    with open(output, 'w') as f:
        f.write('\n'.join(out_lines))

    print(f"Generated {output} successfully")
    print(f"  Songs: {len(song_arrays)}")
    if sfx_file.exists():
        print(f"  SFX: {num_sfx}")
    print(f"  Drum banks: {len(all_drum_data)}")


def adjust_offsets(asm, start, end):
    """Extract a sub-range of assembled data and adjust internal offsets.

    When we split a larger assembled block into individual songs/SFX,
    the internal address fixups need to be adjusted relative to the
    sub-block's start.
    """
    sub_data = bytearray(asm.data[start:end])

    # Find fixups that fall within our range
    for fix_offset, label_name in asm.fixups:
        if start <= fix_offset < end:
            if label_name in asm.labels:
                # Original absolute offset
                abs_addr = asm.labels[label_name]
                # Relative offset within this sub-block
                rel_addr = abs_addr - start
                local_off = fix_offset - start
                sub_data[local_off] = rel_addr & 0xFF
                sub_data[local_off + 1] = (rel_addr >> 8) & 0xFF

    return sub_data


if __name__ == '__main__':
    main()
