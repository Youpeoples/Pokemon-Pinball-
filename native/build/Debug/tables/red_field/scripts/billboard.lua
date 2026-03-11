--[[
  Red Field - Billboard Configuration
  Original: native/src/game/billboard.c, red_field.c billboard calls
  ASM: engine/pinball_game/billboard.asm

  The billboard is the 6x5 tile display area in the center of the field.
  It shows Pokemon pictures, slot results, score bonuses, and map icons.

  Billboard Picture IDs:
    0-2:   Ball saver (30/60/90 second)
    3:     Pikachu saver
    4:     Extra ball
    5:     Small reward
    6:     Big reward
    7:     Catch'em mode
    8:     Evolution mode
    9:     Great Ball
   10:     Ultra Ball
   11:     Master Ball
   12:     Bonus multiplier
   13-17:  Bonus stage destinations (Gengar, Mewtwo, Meowth, Diglett, Seel)
   18-26:  Small point values (100-900)
   27-35:  Big point values (1M-9M)
   36-40:  Bonus multiplier X1-X5
   41-58:  Map location pictures (Pallet Town through Indigo Plateau)

  Billboard Tile Data Indices:
    0-17:  Map locations (used by LoadBillboardTileData)
   18-19:  HurryUp messages (map move mode)
   20:     GoToNext (map move, slot open)
   21-25:  Bonus stages
   26:     Slot (CAVE lights)

  Palette Mapping:
    Each picture ID maps to a palette index for colored display.
    Grey palette used for silhouettes (pre-reveal).

  NOTE: Template only. Billboard logic is handled in C.
  Modders: Override billboard pictures with custom PNGs (see examples below).
]]

--============================================================
-- BILLBOARD PICTURE IDS
-- Reference table for modders
--============================================================
local BILLBOARD = {
    -- Slot rewards
    BALL_SAVER_30   = 0,
    BALL_SAVER_60   = 1,
    BALL_SAVER_90   = 2,
    PIKACHU_SAVER   = 3,
    EXTRA_BALL      = 4,
    SMALL_REWARD    = 5,
    BIG_REWARD      = 6,
    CATCHEM_MODE    = 7,
    EVOLUTION_MODE  = 8,
    GREAT_BALL      = 9,
    ULTRA_BALL      = 10,
    MASTER_BALL     = 11,
    BONUS_MULTIPLIER = 12,

    -- Bonus stage destinations
    GENGAR_BONUS    = 13,
    MEWTWO_BONUS    = 14,
    MEOWTH_BONUS    = 15,
    DIGLETT_BONUS   = 16,
    SEEL_BONUS      = 17,

    -- Map pictures (base + map_index)
    MAP_BASE        = 41,

    -- Tile data indices
    TD_HURRYUP2     = 18,
    TD_HURRYUP      = 19,
    TD_GOTONEXT     = 20,
    TD_GENGAR       = 21,
    TD_MEWTWO       = 22,
    TD_MEOWTH       = 23,
    TD_DIGLETT      = 24,
    TD_SEEL         = 25,
    TD_SLOT         = 26,
}

--============================================================
-- ASSET PATH OVERRIDE EXAMPLES
--
-- These functions let you replace any billboard image with
-- custom PNGs. Paths are relative to the asset base path.
-- Use TABLE_PATH to reference files in this table's folder.
--
-- Billboard PNGs must be 48x32 pixels (6x4 tiles, 2bpp).
-- The override replaces the full path including _on/_off suffix,
-- so provide the complete filename.
--============================================================

--[[ EXAMPLE: Override billboard pictures in on_stage_init
-- Place custom PNGs in tables/red_field/assets/billboard/

-- Override the Catch'em mode billboard picture
pinball.set_billboard_pic_path(BILLBOARD.CATCHEM_MODE,
    TABLE_PATH .. "assets/billboard/custom_catchem_on.png")

-- Override a map location picture
pinball.set_billboard_pic_path(BILLBOARD.MAP_BASE + 0,
    TABLE_PATH .. "assets/billboard/custom_pallettown.png")

-- Override billboard tile data (used by LoadBillboardTileData)
pinball.set_billboard_td_path(BILLBOARD.TD_GENGAR,
    TABLE_PATH .. "assets/billboard/custom_gengar_bonus.png")

-- Reset all billboard overrides back to defaults
-- pinball.clear_billboard_overrides()
--]]

--[[ EXAMPLE: Override collision data in on_stage_init
-- Place custom collision PNGs/binaries in tables/red_field/assets/collision/

-- Override collision masks (1bpp PNG, 8x8 tiles)
pinball.set_collision_mask_path(
    TABLE_PATH .. "assets/collision/custom_masks.png")

-- Override collision map (binary, 0x300 bytes)
pinball.set_collision_map_path(
    TABLE_PATH .. "assets/collision/custom_map.collision")

-- Apply the overrides (reloads collision data from new paths)
pinball.reload_collision()

-- Reset collision overrides back to defaults
-- pinball.clear_collision_overrides()
-- pinball.reload_collision()
--]]

--[[ EXAMPLE: Load custom tiles and tilemaps
-- Load custom tile graphics with explicit VRAM bank
pinball.load_tiles(TABLE_PATH .. "assets/custom_tiles.png", 0x8000, 0)

-- Load 8x16 sprite tiles with interleave (32px wide source PNG)
pinball.load_tiles(TABLE_PATH .. "assets/sprites.png", 0x8400, 0, 32)

-- Load a binary tilemap into VRAM bg_map
pinball.load_tilemap(TABLE_PATH .. "assets/custom_bg.tilemap", 0x9800, 0, 0x240)
--]]
