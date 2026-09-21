--[[
  Gengar Bonus Stage
  Original: native/src/game/gengar_bonus.c (~1500 lines)
  ASM: engine/pinball_game/object_collision/gengar_bonus_object_collision.asm
       engine/pinball_game/draw_sprites/gengar_bonus_draw_sprites.asm

  Ghost-hunting bonus stage. Ball starts at bottom, ghosts float around.
  Hit all ghosts in order: 3 Gastly → 2 Haunter → 1 Gengar.

  Stage Flow:
    1. 3 Gastly appear, floating with bobbing motion
    2. Hit each Gastly (3-frame hit animation, then disabled)
    3. After all 3 Gastly: 2 Haunter appear
    4. Hit each Haunter
    5. After both Haunter: Gengar appears (descent from top)
    6. Hit Gengar (takes multiple hits, regenerates)
    7. After defeating Gengar: stage complete, return to main field

  Ghost AI:
    - Gastly: Float vertically (bob) + horizontal drift
    - Haunter: Similar but faster, wider range
    - Gengar: Descends from top, ascends on hit, larger collision area

  Scoring:
    - Gastly hit: config "gastly_hit" (default 10,000,000)
    - Haunter hit: config "haunter_hit" (default 10,000,000)
    - Gengar hit: config "gengar_hit" (default 10,000,000)
    - Gravestone: config "gravestone" (default 100,000)

  Ball Loss:
    - Ball can exit top/bottom of stage
    - On loss: if Gengar defeated, play completion; else respawn ball

  NOTE: This is a TEMPLATE. The C fallback handles all logic.
  Modders: Override hooks to change ghost count, behavior, scoring, etc.
]]

--============================================================
-- GHOST CONFIGURATION
-- Modify these to change the bonus stage behavior
--============================================================
local GHOST_CONFIG = {
    num_gastly  = 3,
    num_haunter = 2,
    gengar_hits_needed = 3,   -- Hits to defeat Gengar

    -- Gastly movement parameters
    gastly_bob_speed = 1,     -- Vertical bob speed
    gastly_drift_speed = 1,   -- Horizontal drift speed

    -- Haunter movement parameters
    haunter_bob_speed = 2,
    haunter_drift_speed = 1,

    -- Gengar movement
    gengar_descent_speed = 1,
}

--============================================================
-- STAGE INITIALIZATION
-- NOTE: The C init_gengar_bonus() always runs first and sets up ALL
-- required state (ghost entities, gate, counters, animations, etc.).
-- This Lua hook runs AFTER as an override layer — only set values
-- you want to customize.
--============================================================
function on_stage_init(stage_id)
    -- C init always runs first, this runs as override
    -- Example overrides (uncomment to customize):
    -- pinball.set_state("gengar_bonus_closed_gate", 1)
    -- pinball.set_state("num_gastly_hits", 0)

    pinball.log("Gengar bonus stage initialized")
end

--============================================================
-- HOOKS (Not active - C fallback used)
--============================================================
--[[
function on_bonus_frame()
    -- Called every frame during bonus stage
    -- Update ghost positions, check collisions, animate
end

function on_draw_sprites(stage_id)
    -- Draw ghost sprites at their current positions
end
]]
