--[[
  Seel Bonus Stage
  Original: native/src/game/seel_bonus.c (~1200 lines)
  ASM: engine/pinball_game/object_collision/seel_bonus_object_collision.asm

  Swimming Seel bonus stage. 3 Seels swim across the water surface.
  Hit them as they peek/swim. Score multiplier increases with streak.

  Stage Flow:
    1. 3 Seels start in submerged state
    2. Seels cycle: submerged → peeking → swimming → submerging
    3. Hit Seels while swimming or peeking for points
    4. Consecutive hits build streak multiplier (2x-9x)
    5. After scoring enough or timeout: stage complete

  Seel AI (per seel):
    States:
      0: Submerged (invisible, timer countdown)
      1: Peeking (partially visible, short window)
      2: Swimming (fully visible, moving left/right)
      3: Submerging (going back under)
      4: Hit (score animation, then submerge)

    Movement:
      - Direction: 0=right, 1=left
      - Speed varies by state and difficulty
      - Position: x_lo/x_hi (16-bit), y_lo/y_hi (16-bit)
      - Timer controls duration of each state

  Scoring:
    - Seel hit: config "seel_hit" × streak multiplier
    - Dive bonus: config "seel_dive"
    - Streak multiplier: 1x, 2x, 3x, ... up to 9x
    - Resets to 1x on miss (ball touches water without hitting)

  NOTE: Template only. C fallback handles all logic.
]]

--============================================================
-- SEEL CONFIGURATION
--============================================================
local SEEL_CONFIG = {
    num_seels = 3,
    max_multiplier = 9,
    submerge_duration_min = 30,
    submerge_duration_max = 90,
    peek_duration = 20,
    swim_duration = 60,
    swim_speed = 2,
}

--============================================================
-- HOOKS (Not active - C fallback used)
--============================================================
--[[
function on_stage_init(stage_id)
    pinball.set_state("seel_bonus_closed_gate", 0)
    pinball.set_state("seel_stage_streak", 0)
    pinball.set_state("seel_stage_score", 0)
    pinball.set_state("seel_stage_state", 0)
    pinball.set_state("seel_completion_state", 0)
    pinball.log("Seel bonus stage initialized")
end

function on_bonus_frame()
    -- Update seel positions, AI states, check collisions
end
]]
