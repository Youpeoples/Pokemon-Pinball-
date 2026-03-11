--[[
  Blue Field - Slot Machine State Machine
  Original: native/src/game/blue_field.c (resolve_slot function, ~600 lines)
  ASM: engine/pinball_game/slot.asm

  The slot machine is a 10-state FSM that handles:
  - Opening the slot when ball enters (force field mechanics)
  - Spinning 3 reels with configurable symbols
  - Matching logic (all 3 match = bonus stage, catch mode, etc.)
  - Roulette reward selection
  - Billboard display during slot sequence

  Slot States:
    0: Idle (slot closed, force field active)
    1: Ball entered slot — start sequence
    2: First reel spinning
    3: First reel stopped
    4: Second reel spinning
    5: Second reel stopped
    6: Third reel spinning
    7: Third reel stopped — check match
    8: Match found — determine reward
    9: Reward display / transition
   10: Cleanup / return to play

  Blue Field Differences from Red Field:
    - Force field barrier controls access to slot
    - Force field direction alternates (left/right push)
    - Slot entrance requires CAVE lights to be lit
    - Different visual theme (water-based)

  NOTE: This is a TEMPLATE. The slot machine logic is one of the most
  complex state machines in the game. The C fallback handles it.
  Modders: Override to add custom slot symbols, rewards, or behaviors.
]]

--============================================================
-- SLOT SYMBOLS
-- Original symbols and their reward mappings
--============================================================
local SLOT_SYMBOLS = {
    [0] = "Pokeball",      -- Match: Catch'em mode
    [1] = "Item",          -- Match: Evolution mode
    [2] = "Gengar",        -- Match: Gengar bonus stage
    [3] = "Seel",          -- Match: Seel bonus stage
    [4] = "Shellder",      -- Match: Score bonus
}

--============================================================
-- SLOT REWARDS
-- What happens when all 3 reels match
--============================================================
local SLOT_REWARDS = {
    pokeball = "Start Catch'em mode (catch a Pokemon)",
    item     = "Start Evolution mode (evolve a Pokemon)",
    gengar   = "Enter Gengar bonus stage",
    seel     = "Enter Seel bonus stage",
    shellder = "Score bonus (100,000 points)",
}

--============================================================
-- SLOT UPDATE HOOK
-- Not currently active - the C fallback handles the slot machine.
-- Uncomment to override slot behavior.
--============================================================
--[[
function on_slot_update(slot_state)
    -- Called each frame while the slot machine is active.
    -- slot_state is the current state (0-10).

    if slot_state == 0 then
        -- Idle: force field active, slot closed
    elseif slot_state == 1 then
        -- Ball entered: start spinning
        pinball.play_sfx("slot_start")
    elseif slot_state == 7 then
        -- All reels stopped: check for match
        -- Use pinball.get_state() to read reel values
    elseif slot_state == 8 then
        -- Match found: show reward on billboard
    end
end
]]
