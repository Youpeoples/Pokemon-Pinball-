#!/usr/bin/env python3
"""
convert_pokedex_data.py

Extracts Pokedex data from ASM source files and generates a C header file:
  - 151 Pokemon descriptions from text/pokedex_descriptions.asm
  - VWF character widths from data/vwf_character_widths.asm
  - Dex scroll bar offsets from data/dex_scroll_offsets.asm

Output: native/src/game/pokedex_data.h

Usage (from project root):
    python native/tools/convert_pokedex_data.py
"""

import os
import re
import sys

# Resolve project root (two levels up from this script)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))

DESCRIPTIONS_ASM = os.path.join(PROJECT_ROOT, "text", "pokedex_descriptions.asm")
VWF_WIDTHS_ASM = os.path.join(PROJECT_ROOT, "data", "vwf_character_widths.asm")
DEX_SCROLL_ASM = os.path.join(PROJECT_ROOT, "data", "dex_scroll_offsets.asm")
OUTPUT_HEADER = os.path.join(PROJECT_ROOT, "native", "src", "game", "pokedex_data.h")


def read_file(path):
    """Read a file and return its lines."""
    with open(path, "r", encoding="utf-8") as f:
        return f.readlines()


def extract_pointer_table(lines):
    """
    Extract the ordered list of label names from PokedexDescriptionPointers.
    Each entry is like:  dw BulbasaurPokedexDescription
    Returns a list of 151 label names in Pokedex order.
    """
    labels = []
    in_table = False
    for line in lines:
        stripped = line.strip()
        if "PokedexDescriptionPointers:" in stripped:
            in_table = True
            continue
        if in_table:
            # Stop when we hit a non-dw line (empty line or a label definition)
            match = re.match(r"dw\s+(\w+)", stripped)
            if match:
                labels.append(match.group(1))
            elif stripped == "" or (not stripped.startswith(";") and not stripped.startswith("dw")):
                # End of pointer table
                if labels:
                    break
    return labels


def extract_descriptions(lines, label_order):
    """
    Parse all description labels and their text content.
    Returns a dict mapping label_name -> description string.

    Macros:
      dex_text "..."  -> just the string text (first line)
      dex_line "..."  -> newline + string text (subsequent lines)
      dex_end          -> end of description
    """
    # Build a map of label -> line index
    label_line = {}
    for i, line in enumerate(lines):
        stripped = line.strip()
        # Match label definitions like "BulbasaurPokedexDescription:"
        if stripped.endswith(":") or ":" in stripped:
            # Extract label name (before the colon, ignoring comments/addresses)
            m = re.match(r"(\w+):", stripped)
            if m:
                label_line[m.group(1)] = i

    descriptions = {}
    for label in label_order:
        if label not in label_line:
            print(f"WARNING: Label '{label}' not found in descriptions file", file=sys.stderr)
            descriptions[label] = ""
            continue

        start = label_line[label]
        text_parts = []
        for j in range(start + 1, len(lines)):
            stripped = lines[j].strip()

            # Skip empty lines and comments
            if stripped == "" or stripped.startswith(";"):
                continue

            # Check for dex_end
            if stripped == "dex_end":
                break

            # Check for dex_text "..."
            m = re.match(r'dex_text\s+"(.*)"', stripped)
            if m:
                text_parts.append(m.group(1))
                continue

            # Check for dex_line "..."
            m = re.match(r'dex_line\s+"(.*)"', stripped)
            if m:
                text_parts.append("\n" + m.group(1))
                continue

            # If we hit another label, stop
            if re.match(r"\w+:", stripped):
                break

        descriptions[label] = "".join(text_parts)

    return descriptions


def escape_c_string(s):
    """Escape a string for use in a C string literal."""
    result = []
    for ch in s:
        if ch == "\\":
            result.append("\\\\")
        elif ch == '"':
            result.append('\\"')
        elif ch == "\n":
            result.append("\\n")
        elif ch == "`":
            # Backtick is used as apostrophe in the ASM text
            result.append("`")
        else:
            result.append(ch)
    return "".join(result)


def pokemon_name_from_label(label):
    """Extract a Pokemon name from its description label, e.g. BulbasaurPokedexDescription -> Bulbasaur."""
    return label.replace("PokedexDescription", "")


def extract_character_widths(lines):
    """
    Extract the 256-entry CharacterWidths table.
    Starts at label 'CharacterWidths:' and reads 256 consecutive db $XX entries.
    """
    widths = []
    in_table = False
    for line in lines:
        stripped = line.strip()
        if "CharacterWidths:" in stripped:
            in_table = True
            continue
        if in_table:
            # Match db $XX or db $X
            m = re.match(r"db\s+\$([0-9A-Fa-f]+)", stripped)
            if m:
                widths.append(int(m.group(1), 16))
            elif stripped == "" or stripped.startswith(";"):
                # Skip blank lines and comments within the table
                continue
            else:
                # Hit something else (another label, etc.) - stop
                if widths:
                    break
    return widths


def extract_scroll_offsets(lines):
    """
    Extract the 151-entry DexScrollBarOffsets table.
    Starts at label 'DexScrollBarOffsets:' and reads 151 consecutive db $XX entries.
    """
    offsets = []
    in_table = False
    for line in lines:
        stripped = line.strip()
        if "DexScrollBarOffsets:" in stripped:
            in_table = True
            continue
        if in_table:
            m = re.match(r"db\s+\$([0-9A-Fa-f]+)", stripped)
            if m:
                offsets.append(int(m.group(1), 16))
            elif stripped == "" or stripped.startswith(";"):
                continue
            else:
                if offsets:
                    break
    return offsets


def generate_header(descriptions_ordered, pokemon_names, widths, scroll_offsets):
    """Generate the C header file content."""
    lines = []
    lines.append("#ifndef POKEDEX_DATA_H")
    lines.append("#define POKEDEX_DATA_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")

    # Descriptions
    lines.append("/* 151 Pokedex descriptions - ASCII text with \\n for line breaks */")
    lines.append("static const char *pokedex_descriptions[151] = {")
    for i, (desc, name) in enumerate(zip(descriptions_ordered, pokemon_names)):
        escaped = escape_c_string(desc)
        comma = "," if i < 150 else ""
        lines.append(f'    "{escaped}"{comma}  /* {name} */'.rstrip())
    lines.append("};")
    lines.append("")

    # Character widths
    lines.append("/* VWF character pixel widths (256 entries) */")
    lines.append("static const uint8_t character_widths[256] = {")
    for i in range(0, len(widths), 16):
        chunk = widths[i:i+16]
        hex_vals = ", ".join(f"0x{v:02X}" for v in chunk)
        comma = "," if i + 16 < len(widths) else ""
        if i + 16 >= len(widths):
            # Last row - no trailing comma after last element
            lines.append(f"    {hex_vals}")
        else:
            lines.append(f"    {hex_vals},")
    lines.append("};")
    lines.append("")

    # Scroll offsets
    lines.append("/* Dex scroll bar Y offsets (151 entries) */")
    lines.append("static const uint8_t dex_scroll_bar_offsets_data[151] = {")
    for i in range(0, len(scroll_offsets), 16):
        chunk = scroll_offsets[i:i+16]
        hex_vals = ", ".join(f"0x{v:02X}" for v in chunk)
        if i + 16 >= len(scroll_offsets):
            lines.append(f"    {hex_vals}")
        else:
            lines.append(f"    {hex_vals},")
    lines.append("};")
    lines.append("")

    lines.append("#endif")
    lines.append("")

    return "\n".join(lines)


def main():
    # Check input files exist
    for path, name in [(DESCRIPTIONS_ASM, "pokedex_descriptions.asm"),
                       (VWF_WIDTHS_ASM, "vwf_character_widths.asm"),
                       (DEX_SCROLL_ASM, "dex_scroll_offsets.asm")]:
        if not os.path.isfile(path):
            print(f"ERROR: Cannot find {name} at {path}", file=sys.stderr)
            sys.exit(1)

    # --- 1. Pokedex descriptions ---
    desc_lines = read_file(DESCRIPTIONS_ASM)

    # Get ordered label list from pointer table
    label_order = extract_pointer_table(desc_lines)
    if len(label_order) != 151:
        print(f"WARNING: Expected 151 pointer table entries, got {len(label_order)}", file=sys.stderr)

    # Extract each description by label
    desc_map = extract_descriptions(desc_lines, label_order)

    # Build ordered descriptions and Pokemon names
    descriptions_ordered = []
    pokemon_names = []
    for label in label_order:
        descriptions_ordered.append(desc_map.get(label, ""))
        pokemon_names.append(pokemon_name_from_label(label))

    print(f"Extracted {len(descriptions_ordered)} Pokedex descriptions")

    # --- 2. VWF character widths ---
    vwf_lines = read_file(VWF_WIDTHS_ASM)
    widths = extract_character_widths(vwf_lines)
    if len(widths) != 256:
        print(f"WARNING: Expected 256 character width entries, got {len(widths)}", file=sys.stderr)
    print(f"Extracted {len(widths)} character width entries")

    # --- 3. Dex scroll bar offsets ---
    scroll_lines = read_file(DEX_SCROLL_ASM)
    scroll_offsets = extract_scroll_offsets(scroll_lines)
    if len(scroll_offsets) != 151:
        print(f"WARNING: Expected 151 scroll offset entries, got {len(scroll_offsets)}", file=sys.stderr)
    print(f"Extracted {len(scroll_offsets)} scroll bar offset entries")

    # --- 4. Generate header ---
    header_content = generate_header(descriptions_ordered, pokemon_names, widths, scroll_offsets)

    # Ensure output directory exists
    os.makedirs(os.path.dirname(OUTPUT_HEADER), exist_ok=True)

    with open(OUTPUT_HEADER, "w", encoding="utf-8") as f:
        f.write(header_content)

    print(f"Generated {OUTPUT_HEADER}")


if __name__ == "__main__":
    main()
