--[[
  Red Field - Main Script
  Original: native/src/game/red_field.c (init_red_field, ball init/loss)
  ASM: engine/pinball_game/stage_init/red_field_stage_init.asm
       engine/pinball_game/ball_init/red_field_ball_init.asm
       engine/pinball_game/ball_loss/red_field_ball_loss.asm

  This script handles stage initialization, ball start, and ball loss
  for the Red Field (stages 0 and 1).

  Modders: You can change starting positions, ball saver durations,
  initial music, and any per-stage variable initialization here.
]]

-- Load sub-scripts (collision, sprites, slot, etc.)
-- These define the hook functions that the engine will call.

--============================================================
-- STAGE INITIALIZATION
-- NOTE: The C init_red_field() always runs first and sets up ALL
-- required state (40+ variables, collision tables, indicators, music,
-- ball saver timer, animations, etc.). This Lua hook runs AFTER as
-- an override layer — only set values you want to customize.
--============================================================
function on_stage_init(stage_id)
    -- Example overrides (uncomment to customize):
    -- pinball.set_state("num_ball_lives", 5)           -- extra lives
    -- pinball.set_state("next_bonus_stage", 3)         -- change bonus order
    -- pinball.set_state("cur_bonus_multiplier", 2)     -- start with 2x

    pinball.log("Red field stage initialized")
end

--============================================================
-- BALL INITIALIZATION
-- NOTE: The C init_ball_red_field() always runs first and handles
-- normal start, bonus return, per-ball resets, collision attribute
-- loading, and music restart. This Lua hook runs AFTER as an override.
--============================================================
function on_ball_init(stage_id)
    -- Example: custom starting position
    -- pinball.set_ball_x(0xA700)
    -- pinball.set_ball_y(0x9800)
end

--============================================================
-- BALL LOSS
-- Called when ball drains (handled in check_ball_lost in C).
-- For main field stages, the C engine handles ball saver logic.
-- This hook is for any additional per-table ball loss behavior.
--============================================================
function on_ball_loss(stage_id)
    -- Red field ball loss is mostly handled by the engine.
    -- Stage-specific behavior could be added here.
    -- For example: custom drain animation, special mode cleanup, etc.
end
