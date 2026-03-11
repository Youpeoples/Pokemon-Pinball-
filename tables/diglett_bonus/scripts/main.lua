--[[
  Diglett Bonus Stage
  Original: native/src/game/diglett_bonus.c (~900 lines)
  ASM: engine/pinball_game/object_collision/diglett_bonus_object_collision.asm

  Whack-a-mole bonus stage. 31 Digletts pop up from holes.
  Hit them with the ball before they go back down.
  After hitting enough, Dugtrio appears as a boss.

  Stage Flow:
    1. Digletts initialize sequentially (one per few frames)
    2. Each Diglett cycles: underground → peeking → up → going down
    3. Ball hitting a Diglett while "up" scores points
    4. After all 31 Digletts handled, Dugtrio appears
    5. Hit Dugtrio for bonus points
    6. Stage complete after Dugtrio

  Diglett AI:
    - 31 fixed positions on the field
    - Each has an independent state machine:
      0=underground, 1=emerging, 2=up (vulnerable), 3=going down, 4=hit
    - Timing varies: faster Digletts appear as stage progresses
    - Some Digletts stay up longer than others (RNG)

  Dugtrio Boss:
    - Appears after all 31 Digletts
    - Larger collision area, takes 3 hits
    - Animation: emerge → vulnerable → retreat → repeat

  Scoring:
    - Diglett hit: config "diglett_hit"
    - Dugtrio hit: config "dugtrio_hit"

  NOTE: Template only. C fallback handles all logic.
]]

--============================================================
-- DIGLETT CONFIGURATION
--============================================================
local DIGLETT_CONFIG = {
    total_digletts = 31,
    dugtrio_hits_needed = 3,
    init_delay = 8,           -- Frames between each Diglett initialization
    up_duration_min = 30,     -- Minimum frames a Diglett stays up
    up_duration_max = 90,     -- Maximum frames
}

--============================================================
-- HOOKS (Not active - C fallback used)
--============================================================
--[[
function on_stage_init(stage_id)
    pinball.set_state("diglett_bonus_closed_gate", 0)
    pinball.set_state("dugtrio_state", 0)
    pinball.log("Diglett bonus stage initialized")
end

function on_bonus_frame()
    -- Update diglett states, check collisions, manage dugtrio
end
]]
