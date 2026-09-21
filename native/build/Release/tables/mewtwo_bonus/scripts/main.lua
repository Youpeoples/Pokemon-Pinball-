--[[
  Mewtwo Bonus Stage
  Original: native/src/game/mewtwo_bonus.c (~1000 lines)
  ASM: engine/pinball_game/object_collision/mewtwo_bonus_object_collision.asm

  Orbiting balls surround Mewtwo. Hit orbiting balls to destroy them,
  then hit Mewtwo directly. Mewtwo regenerates orbs multiple times.

  Stage Flow:
    1. 6 orbiting balls appear around Mewtwo
    2. Hit orbiting balls to destroy them (each has angle/radius)
    3. When all orbs destroyed, Mewtwo becomes vulnerable
    4. Hit Mewtwo directly (multiple times per phase)
    5. Mewtwo regenerates orbs (up to 8 regen phases)
    6. After final phase: stage complete

  Orbiting Ball System:
    - 6 balls orbit at fixed radius
    - Each has: active flag, angle (0-255), radius, type
    - Angles advance each frame (circular motion)
    - Collision: ball radius check against orbit position

  Scoring:
    - Orb hit: config "mewtwo_orb_hit"
    - Mewtwo hit: config "mewtwo_hit"

  NOTE: Template only. C fallback handles all logic.
]]

--============================================================
-- ORBIT CONFIGURATION
--============================================================
local ORBIT_CONFIG = {
    num_orbs = 6,
    orbit_radius = 24,
    orbit_speed = 2,          -- Angle increment per frame
    mewtwo_hits_per_phase = 3,
    total_regen_phases = 8,
}

--============================================================
-- STAGE INITIALIZATION
-- NOTE: The C init_mewtwo_bonus() always runs first and sets up ALL
-- required state (orbiting balls, hit counters, regen phase, etc.).
-- This Lua hook runs AFTER as an override layer — only set values
-- you want to customize.
--============================================================
function on_stage_init(stage_id)
    -- C init always runs first, this runs as override
    -- Example overrides (uncomment to customize):
    -- pinball.set_state("mewtwo_hit_counter", 0)
    -- pinball.set_state("mewtwo_regen_phase", 0)

    pinball.log("Mewtwo bonus stage initialized")
end

--============================================================
-- HOOKS (Not active - C fallback used)
--============================================================
--[[
function on_bonus_frame()
    -- Update orbiting ball positions, check collisions
end
]]
