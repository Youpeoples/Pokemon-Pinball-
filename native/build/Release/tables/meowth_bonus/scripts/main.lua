--[[
  Meowth Bonus Stage
  Original: native/src/game/meowth_bonus.c (~1400 lines)
  ASM: engine/pinball_game/object_collision/meowth_bonus_object_collision.asm

  Jewel collection bonus stage. Meowth walks across the top producing
  jewels that fall down. Hit jewels with the ball for points.
  Speed increases as more jewels are collected.

  Stage Flow:
    1. Meowth appears at top, walks left/right
    2. Meowth drops jewels (up to 6 active: 3 bottom, 3 top)
    3. Ball hits jewels to collect them (jewels bounce on hit)
    4. Collecting jewels increases score multiplier (2x-6x)
    5. After 20 jewels or timeout: stage complete

  Meowth AI:
    - Walks left/right across top of screen
    - Periodically moves to produce position and drops jewel
    - Hit by ball: briefly stunned, drops bonus jewel

  Jewel System:
    - 6 jewel slots (indices 0-2=bottom row, 3-5=top row)
    - Each has: state, x/y position, direction, bounce counter
    - States: 0=inactive, 1=falling, 2=active, 3=hit, 4=cleanup

  Scoring:
    - Jewel hit: config "meowth_jewel" × multiplier
    - Meowth hit: config "meowth_hit"
    - Multiplier increases every 4 jewels (2x, 3x, 4x, 5x, 6x)

  NOTE: Template only. C fallback handles all logic.
]]

--============================================================
-- MEOWTH CONFIGURATION
--============================================================
local MEOWTH_CONFIG = {
    max_jewels_bottom = 3,
    max_jewels_top = 3,
    jewels_to_complete = 20,
    multiplier_thresholds = {4, 8, 12, 16},  -- Jewel counts for 2x, 3x, 4x, 5x
    meowth_walk_speed = 1,
}

--============================================================
-- STAGE INITIALIZATION
-- NOTE: The C init_meowth_bonus() always runs first and sets up ALL
-- required state (jewel slots, Meowth AI, score counters, etc.).
-- This Lua hook runs AFTER as an override layer — only set values
-- you want to customize.
--============================================================
function on_stage_init(stage_id)
    -- C init always runs first, this runs as override
    -- Example overrides (uncomment to customize):
    -- pinball.set_state("meowth_stage_score", 0)
    -- pinball.set_state("meowth_state", 0)

    pinball.log("Meowth bonus stage initialized")
end

--============================================================
-- HOOKS (Not active - C fallback used)
--============================================================
--[[
function on_bonus_frame()
    -- Update Meowth movement, jewel physics, check collisions
end
]]
