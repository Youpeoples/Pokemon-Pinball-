--[[
  Blue Field - Sprite Rendering
  Original: native/src/game/draw_blue_field.c
  ASM: engine/pinball_game/draw_sprites/draw_blue_field_sprites.asm

  This script renders all stage-specific sprites each frame.
  Sprites include: Shellder bumpers, spinner, Psyduck/Poliwag,
  Slowpoke, Cloyster, force field, Pikachu savers, ball,
  indicators, bumpers, and more.

  NOTE: This is a TEMPLATE. The actual sprite rendering logic is
  1500+ lines of coordinate calculations, animation frame selection,
  and conditional sprite pushing. The C fallback handles this
  automatically until this script is fully implemented.

  Modders: Override on_draw_sprites to add custom sprites, change
  positions, or add new visual elements to the field.
]]

--============================================================
-- SPRITE CONSTANTS
-- OAM attribute flags
--============================================================
local ATTR = {
    PALETTE_0 = 0x00,
    PALETTE_1 = 0x01,
    PALETTE_2 = 0x02,
    PALETTE_3 = 0x03,
    PALETTE_4 = 0x04,
    PALETTE_5 = 0x05,
    PALETTE_6 = 0x06,
    PALETTE_7 = 0x07,
    BANK_1    = 0x08,    -- Tile from VRAM bank 1
    X_FLIP    = 0x20,    -- Horizontal flip
    Y_FLIP    = 0x40,    -- Vertical flip
    PRIORITY  = 0x80,    -- Behind BG (priority bit)
}

--============================================================
-- DRAW SPRITES
-- Called every frame to render stage-specific sprites.
-- Not currently active - uncomment on_draw_sprites to enable.
--============================================================
--[[
function on_draw_sprites(stage_id)
    -- The engine clears the sprite buffer before calling this.
    -- Use pinball.push_sprite(y, x, tile, attr) to add sprites.

    -- Example: Draw a custom sprite at a fixed position
    -- pinball.push_sprite(80, 80, 0x00, ATTR.PALETTE_0)

    -- Ball sprite is drawn by the engine after this callback.
    -- Only stage-specific decorative sprites need to be drawn here.
end
]]
