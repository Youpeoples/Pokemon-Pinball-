--[[
  Blue Field - Billboard Configuration
  Original: native/src/game/billboard.c, blue_field.c billboard calls
  ASM: engine/pinball_game/billboard.asm

  The billboard is the 6x5 tile display area in the center of the field.
  It shows Pokemon pictures, slot results, score bonuses, and map icons.

  Billboard Picture IDs:
    0-7:   Pokemon animated sprites (catch mode)
    8:     Pokeball icon
    9:     Great Ball icon
   10:     Ultra Ball icon
   11:     Master Ball icon
   12-40:  Various game state pictures
   41-58:  Map location pictures (one per map)

  Palette Mapping:
    Each picture ID maps to a palette index for colored display.
    Grey palette used for silhouettes (pre-reveal).

  Blue Field Specific:
    - Initial map selection at launch: cycles through 7 maps
      (Viridian City, Viridian Forest, Mt. Moon, Cerulean City,
       Vermilion City, Rock Tunnel, Celadon City)
    - Map cycling timer: 32 frames per map
    - Ball start key finalizes selection
    - Billboard formula: BILLBOARD_PALLET_TOWN_PIC + current_map

  NOTE: Template only. Billboard logic is handled in C.
  Modders: Could override to add custom billboard pictures.
]]

--============================================================
-- BILLBOARD PICTURE IDS
-- Reference table for modders
--============================================================
local BILLBOARD = {
    -- Ball type upgrade pictures
    GREAT_BALL  = 9,
    ULTRA_BALL  = 10,
    MASTER_BALL = 11,

    -- Map pictures (base + map_index)
    MAP_BASE    = 41,

    -- Special
    QUESTION_MARK = 8,
}

--============================================================
-- BLUE FIELD MAP PICTURE IDS
-- Map index values used for billboard display
--============================================================
local MAP_IDS = {
    PALLET_TOWN      = 0,
    VIRIDIAN_CITY    = 1,
    VIRIDIAN_FOREST  = 2,
    PEWTER_CITY      = 3,
    MT_MOON          = 4,
    CERULEAN_CITY    = 5,
    VERMILION_CITY   = 6,
    ROCK_TUNNEL      = 7,
    LAVENDER_TOWN    = 8,
    CELADON_CITY     = 9,
    FUCHSIA_CITY     = 10,
    SAFARI_ZONE      = 11,
    SAFFRON_CITY     = 12,
    SEAFOAM_ISLANDS  = 13,
    CINNABAR_ISLAND  = 14,
    INDIGO_PLATEAU   = 15,
    UNKNOWN_DUNGEON  = 16,
}
