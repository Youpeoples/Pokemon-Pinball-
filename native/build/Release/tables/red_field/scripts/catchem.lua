--[[
  Red Field - Catch'em Mode State Machine
  Original: native/src/game/red_field.c (check_special_mode_collision state 0)
  ASM: engine/pinball_game/catchem_mode.asm

  Catch'em mode is activated by matching 3 Pokeball symbols on the slot machine.
  A wild Pokemon appears on the billboard and the player must hit it to catch it.

  States (managed by special_mode_state):
    0: Initializing - select wild Pokemon, load graphics
    1: Pokemon appeared - show on billboard, start timer
    2: Active - ball can hit wild Pokemon
    3: Hit registered - check catch success
    4: Caught! - add to party, show animation
    5: Escaped - Pokemon fled, end mode
    6: Timer expired - end mode
    7: Cleanup - restore normal state

  Catch Mechanics:
    - Ball type affects catch difficulty:
      Poke Ball: narrow hit window
      Great Ball: wider hit window
      Ultra Ball: high catch rate
      Master Ball: guaranteed catch
    - Number of hits increases catch chance
    - Mew: special encounter (requires 2+ Mewtwo bonus completions)

  Wild Pokemon Availability:
    - Different Pokemon per map location
    - See data/red_wild_mons.asm for availability tables

  NOTE: Template only. C fallback handles the actual logic.
]]

--============================================================
-- CATCH MODE HOOK
-- Not currently active.
--============================================================
--[[
function on_catchem_update(catchem_state)
    -- Called each frame during catch'em mode
end
]]
