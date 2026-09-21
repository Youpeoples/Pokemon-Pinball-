--[[
  Blue Field - Map Move State Machine
  Original: native/src/game/blue_field.c (resolve_psyduck_poliwag + map move logic)
  ASM: engine/pinball_game/map_move.asm

  Map move progresses through 18 map locations, changing which wild
  Pokemon are available and which billboard art is shown.

  Map Locations (0-17):
     0: Pallet Town         9: Celadon City
     1: Viridian City      10: Fuchsia City
     2: Viridian Forest    11: Safari Zone
     3: Pewter City        12: Saffron City
     4: Mt. Moon           13: Seafoam Islands
     5: Cerulian City      14: Cinnabar Island
     6: Vermilion City     15: Indigo Plateau
     7: Rock Tunnel        16: Unknown Dungeon
     8: Lavender Town      17: (wraps to 0)

  Blue Field Initial Maps (launch screen selection):
    Viridian City, Viridian Forest, Mt. Moon, Cerulean City,
    Vermilion City, Rock Tunnel, Celadon City

  Triggers: Psyduck (left) and Poliwag (right)
    - Located in upper half of bottom screen (Y < 0x56)
    - Hit left Psyduck to decrement left counter, right Poliwag for right
    - When a counter reaches 0, map moves in that direction
    - Counter starts at 3, decrements per hit

  Differences from Red Field:
    - Red Field uses Diglett (left/right) for map move
    - Blue Field uses Psyduck/Poliwag instead
    - Same counter mechanics and map progression

  NOTE: Template only. C fallback handles the actual logic.
]]

--============================================================
-- MAP MOVE HOOK
-- Not currently active.
--============================================================
--[[
function on_map_move_update(map_state)
    -- Called each frame during map move sequence
end
]]
