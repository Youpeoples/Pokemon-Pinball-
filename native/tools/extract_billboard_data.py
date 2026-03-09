#!/usr/bin/env python3
"""
Extract billboard palette and palette map data from ASM files and generate C arrays.
"""

import re
import os

BASE = r"C:\Users\ville\Documents\GitHub\Comp Proj"

# Species order from MonBillboardPalettePointers (same for palette map pointers)
SPECIES = [
    "Bulbasaur", "Ivysaur", "Venusaur",
    "Charmander", "Charmeleon", "Charizard",
    "Squirtle", "Wartortle", "Blastoise",
    "Caterpie", "Metapod", "Butterfree",
    "Weedle", "Kakuna", "Beedrill",
    "Pidgey", "Pidgeotto", "Pidgeot",
    "Rattata", "Raticate",
    "Spearow", "Fearow",
    "Ekans", "Arbok",
    "Pikachu", "Raichu",
    "Sandshrew", "Sandslash",
    "NidoranF", "Nidorina", "Nidoqueen",
    "NidoranM", "Nidorino", "Nidoking",
    "Clefairy", "Clefable",
    "Vulpix", "Ninetales",
    "Jigglypuff", "Wigglytuff",
    "Zubat", "Golbat",
    "Oddish", "Gloom", "Vileplume",
    "Paras", "Parasect",
    "Venonat", "Venomoth",
    "Diglett", "Dugtrio",
    "Meowth", "Persian",
    "Psyduck", "Golduck",
    "Mankey", "Primeape",
    "Growlithe", "Arcanine",
    "Poliwag", "Poliwhirl", "Poliwrath",
    "Abra", "Kadabra", "Alakazam",
    "Machop", "Machoke", "Machamp",
    "Bellsprout", "Weepinbell", "Victreebel",
    "Tentacool", "Tentacruel",
    "Geodude", "Graveler", "Golem",
    "Ponyta", "Rapidash",
    "Slowpoke", "Slowbro",
    "Magnemite", "Magneton",
    "Farfetchd", "Doduo", "Dodrio",
    "Seel", "Dewgong",
    "Grimer", "Muk",
    "Shellder", "Cloyster",
    "Gastly", "Haunter", "Gengar",
    "Onix",
    "Drowzee", "Hypno",
    "Krabby", "Kingler",
    "Voltorb", "Electrode",
    "Exeggcute", "Exeggutor",
    "Cubone", "Marowak",
    "Hitmonlee", "Hitmonchan",
    "Lickitung",
    "Koffing", "Weezing",
    "Rhyhorn", "Rhydon",
    "Chansey",
    "Tangela",
    "Kangaskhan",
    "Horsea", "Seadra",
    "Goldeen", "Seaking",
    "Staryu", "Starmie",
    "MrMime", "Scyther",
    "Jynx", "Electabuzz",
    "Magmar", "Pinsir",
    "Tauros",
    "Magikarp", "Gyarados",
    "Lapras",
    "Ditto",
    "Eevee",
    "Vaporeon", "Jolteon", "Flareon",
    "Porygon",
    "Omanyte", "Omastar",
    "Kabuto", "Kabutops",
    "Aerodactyl",
    "Snorlax",
    "Articuno", "Zapdos", "Moltres",
    "Dratini", "Dragonair", "Dragonite",
    "Mewtwo",
    "Mew",
]

assert len(SPECIES) == 151, f"Expected 151, got {len(SPECIES)}"

# Map from ASM label prefix to species name
# The ASM uses labels like BulbasaurBillboardBGPalette1, etc.
# Some have slight naming variations
ASM_NAME_MAP = {
    "NidoranF": "NidoranF",
    "NidoranM": "NidoranM",
    "Nidoran_F": "NidoranF",
    "Nidoran_M": "NidoranM",
    "Farfetch_d": "Farfetchd",
    "Farfetchd": "Farfetchd",
    "Mr_Mime": "MrMime",
    "MrMime": "MrMime",
}

def parse_rgb(line):
    """Parse 'RGB r, g, b' and return uint16_t GBC color."""
    m = re.match(r'\s*RGB\s+(\d+),\s*(\d+),\s*(\d+)', line)
    if m:
        r, g, b = int(m.group(1)), int(m.group(2)), int(m.group(3))
        return r | (g << 5) | (b << 10)
    return None

def parse_palette_file(filepath):
    """Parse a palette ASM file and return dict of label -> [8 uint16_t colors]."""
    palettes = {}
    with open(filepath, 'r') as f:
        lines = f.readlines()

    current_label = None
    colors = []

    for line in lines:
        line = line.strip()
        # Check for label
        m = re.match(r'(\w+BillboardBGPalette[12]):', line)
        if m:
            label = m.group(1)
            # Get species name (remove BillboardBGPalette1/2)
            species_raw = re.sub(r'BillboardBGPalette[12]$', '', label)
            species = ASM_NAME_MAP.get(species_raw, species_raw)

            if label.endswith('1'):
                current_label = species
                colors = []

            continue

        c = parse_rgb(line)
        if c is not None and current_label:
            colors.append(c)
            if len(colors) == 8:  # 4 colors for pal1 + 4 for pal2
                palettes[current_label] = colors[:]
                current_label = None
                colors = []

    return palettes

def parse_palette_map_file(filepath):
    """Parse a palette map ASM file and return dict of label -> [24 uint8_t values]."""
    maps = {}
    with open(filepath, 'r') as f:
        lines = f.readlines()

    current_label = None
    values = []

    for line in lines:
        line = line.strip()
        # Check for label
        m = re.match(r'(\w+BillboardBGPaletteMap):', line)
        if m:
            if m.group(1).startswith("Unused"):
                continue
            label = m.group(1)
            species_raw = re.sub(r'BillboardBGPaletteMap$', '', label)
            species = ASM_NAME_MAP.get(species_raw, species_raw)
            current_label = species
            values = []
            continue

        # Parse db values
        if current_label and line.startswith('db '):
            nums = re.findall(r'\$([0-9a-fA-F]+)', line)
            for n in nums:
                values.append(int(n, 16))
            if len(values) >= 24:
                maps[current_label] = values[:24]
                current_label = None
                values = []

    return maps

# Parse all palette files
all_palettes = {}
for i in range(1, 7):
    f = os.path.join(BASE, "data", "mon_gfx", f"mon_billboard_palettes_{i}.asm")
    if os.path.exists(f):
        pals = parse_palette_file(f)
        all_palettes.update(pals)
        print(f"Parsed {f}: {len(pals)} palettes")

# Parse all palette map files
all_maps = {}
for i in range(1, 6):
    f = os.path.join(BASE, "data", "mon_gfx", f"mon_billboard_palette_maps_{i}.asm")
    if os.path.exists(f):
        maps = parse_palette_map_file(f)
        all_maps.update(maps)
        print(f"Parsed {f}: {len(maps)} maps")

# Verify we have data for all 151 species
print(f"\nTotal palettes: {len(all_palettes)}")
print(f"Total maps: {len(all_maps)}")

missing_pal = [s for s in SPECIES if s not in all_palettes]
missing_map = [s for s in SPECIES if s not in all_maps]
if missing_pal:
    print(f"Missing palettes: {missing_pal}")
if missing_map:
    print(f"Missing maps: {missing_map}")

# Generate C arrays
print("\n// ============================================================")
print("// Billboard palette data (BG palettes 6 and 7)")
print("// 8 colors per species: palette6[4] then palette7[4]")
print("// GBC RGB555 format: r | (g << 5) | (b << 10)")
print("// ============================================================")
print("static const uint16_t mon_billboard_palettes[151][8] = {")
for i, species in enumerate(SPECIES):
    if species in all_palettes:
        colors = all_palettes[species]
        hex_colors = ", ".join(f"0x{c:04X}" for c in colors)
        comment = f"/* {i:3d} {species:12s} */"
        print(f"    {comment} {{{hex_colors}}},")
    else:
        print(f"    /* {i:3d} {species:12s} */ {{0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x7FFF, 0x7FFF, 0x0000, 0x0000}}, /* MISSING */")
print("};")

print()
print("// ============================================================")
print("// Billboard palette attribute maps")
print("// 24 bytes per species (4 rows x 6 cols), values 0x06 or 0x07")
print("// ============================================================")
print("static const uint8_t mon_billboard_palette_maps[151][24] = {")
for i, species in enumerate(SPECIES):
    if species in all_maps:
        vals = all_maps[species]
        hex_vals = ", ".join(f"0x{v:02X}" for v in vals)
        comment = f"/* {i:3d} {species:12s} */"
        print(f"    {comment} {{{hex_vals}}},")
    else:
        print(f"    /* {i:3d} {species:12s} */ {{0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06,0x06}}, /* MISSING */")
print("};")
