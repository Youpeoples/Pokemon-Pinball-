#!/usr/bin/env python3
"""Extract Pokedex height/weight attributes from ASM to C header data."""

import re
import sys

def parse_attributes(asm_path):
    entries = []
    current = {}

    with open(asm_path, 'r') as f:
        for line in f:
            line = line.strip()

            # Skip comments and empty lines
            if line.startswith(';') and not current:
                continue

            m = re.match(r'dex_number\s+(\d+)', line)
            if m:
                current['number'] = int(m.group(1))
                continue

            m = re.match(r'dex_height\s+(\d+),\s*(\d+)', line)
            if m:
                current['feet'] = int(m.group(1))
                current['inches'] = int(m.group(2))
                continue

            m = re.match(r'dex_weight\s+(\d+)', line)
            if m:
                current['weight'] = int(m.group(1))
                current['decimal'] = 0
                entries.append(current)
                current = {}
                continue

            m = re.match(r'dex_weight_decimal\s+(\d+),\s*(\d+)', line)
            if m:
                current['weight'] = int(m.group(1))
                current['decimal'] = int(m.group(2))
                entries.append(current)
                current = {}
                continue

    return entries

def main():
    asm_path = sys.argv[1] if len(sys.argv) > 1 else "text/pokedex_mon_attributes.asm"
    entries = parse_attributes(asm_path)

    print(f"/* Auto-generated from {asm_path} - {len(entries)} entries */")
    print("static const struct { uint8_t feet; uint8_t inches; uint16_t weight; uint8_t weight_decimal; } pokedex_attributes[151] = {")

    for i, e in enumerate(entries):
        comment = f"/* #{e['number']:>3d} */"
        if e['decimal']:
            print(f"    {{{e['feet']:>2d}, {e['inches']:>2d}, {e['weight']:>4d}, {e['decimal']}}}, {comment}")
        else:
            print(f"    {{{e['feet']:>2d}, {e['inches']:>2d}, {e['weight']:>4d}, 0}}, {comment}")

    print("};")

if __name__ == '__main__':
    main()
