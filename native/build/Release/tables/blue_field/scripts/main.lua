--[[
  Blue Field - Main Script
  Original: native/src/game/blue_field.c (init_blue_field, ball init/loss)
  ASM: engine/pinball_game/stage_init/blue_field_stage_init.asm
       engine/pinball_game/ball_init/blue_field_ball_init.asm
       engine/pinball_game/ball_loss/blue_field_ball_loss.asm

  This script handles stage initialization, ball start, and ball loss
  for the Blue Field (stages 4 and 5).
]]

--============================================================
-- STAGE INITIALIZATION
-- NOTE: The C init_blue_field() always runs first and sets up ALL
-- required state (40+ variables, collision tables, indicators, music,
-- ball saver timer, etc.). This Lua hook runs AFTER as an override
-- layer — only set values you want to customize.
--============================================================
function on_stage_init(stage_id)
    -- Example: override the starting bonus stage order
    -- pinball.set_state("next_bonus_stage", 7)  -- 7=Gengar, 9=Mewtwo, etc.

    pinball.log("Blue field stage initialized")
end

--============================================================
-- BALL INITIALIZATION
-- NOTE: The C init_ball_blue_field() always runs first and handles
-- normal start, bonus return, per-ball resets, collision attribute
-- loading, and music restart. This Lua hook runs AFTER as an override.
--============================================================
function on_ball_init(stage_id)
    -- Example: override starting position
    -- pinball.set_ball_x(0xA700)
    -- pinball.set_ball_y(0x9800)
end

--============================================================
-- BALL LOSS
--============================================================
function on_ball_loss(stage_id)
    -- Blue field ball loss handled by engine
end
