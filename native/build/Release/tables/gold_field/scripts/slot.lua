--[[
  Gold Field - Slot Machine State Machine
  Based on: Red Field slot.lua (template)

  NOTE: Template only. The C fallback handles the slot machine.
  Modders: Override to add custom slot symbols, rewards, or behaviors.
]]

--============================================================
-- SLOT SYMBOLS
--============================================================
local SLOT_SYMBOLS = {
    [0] = "Pokeball",
    [1] = "Item",
    [2] = "Diglett",
    [3] = "Pikachu",
    [4] = "Voltorb",
}

--============================================================
-- SLOT REWARDS
--============================================================
local SLOT_REWARDS = {
    pokeball = "Start Catch'em mode (catch a Pokemon)",
    item     = "Start Evolution mode (evolve a Pokemon)",
    diglett  = "Enter bonus stage",
    pikachu  = "Map move + bonus stage",
    voltorb  = "Score bonus (100,000 points)",
}

--============================================================
-- SLOT UPDATE HOOK
-- Not currently active - the C fallback handles the slot machine.
--============================================================
--[[
function on_slot_update(slot_state)
    if slot_state == 0 then
    elseif slot_state == 1 then
        pinball.play_sfx("slot_start")
    elseif slot_state == 7 then
    elseif slot_state == 8 then
    end
end
]]
