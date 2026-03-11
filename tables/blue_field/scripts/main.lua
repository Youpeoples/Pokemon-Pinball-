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
--============================================================
function on_stage_init(stage_id)
    -- Blue field init
    pinball.set_state("stage_collision_state", 0)

    -- Initialize slot/cave state
    pinball.set_state("slot_is_open", 0)
    pinball.set_state("slot_collision", 0)

    -- Initialize map (Pallet Town = 0)
    pinball.set_state("current_map", 0)
    pinball.set_state("num_map_moves", 0)

    -- Ball lives
    pinball.set_state("num_ball_lives", 3)
    pinball.set_state("cur_ball_life", 1)

    -- Ball saver: 35 seconds
    pinball.set_state("ball_saver_timer_seconds", 0x23)
    pinball.set_state("ball_saver_timer_frames", 0)
    pinball.set_state("ball_saver_icon_on", 1)
    pinball.set_state("num_times_ball_saved_text_will_display", 3)
    pinball.set_state("ball_saver_timer_seconds_backup", 0x23)
    pinball.set_state("ball_saver_timer_frames_backup", 0)
    pinball.set_state("num_times_ball_saved_text_will_display_backup", 3)

    -- Initial bonus stage (Gengar)
    pinball.set_state("next_bonus_stage", 7)
    pinball.set_state("initial_next_bonus_stage", 7)

    -- Blue field specific: force field starts pointing up
    pinball.set_state("blue_stage_force_field_direction", 0)

    pinball.log("Blue field stage initialized")
end

--============================================================
-- BALL INITIALIZATION
--============================================================
function on_ball_init(stage_id)
    local returning = pinball.get_state("returning_from_bonus_stage")

    if returning ~= 0 then
        -- Returning from bonus: position at top
        pinball.set_state("ball_size", 0)
        pinball.set_ball_x(0x5000)
        pinball.set_ball_y(0x1600)
        pinball.set_state("returning_from_bonus_stage", 0)
        pinball.set_state("scx", 0)
        pinball.set_scroll(0, 0)
        pinball.set_state("flippers_disabled", 0)
        local backup_type = pinball.get_state("ball_type_backup")
        pinball.set_ball_type(backup_type)
        pinball.play_music("blue_field")
    else
        -- Normal start: launcher position
        -- ASM: InitBallBlueField - X=$00A7, Y=$0098
        pinball.set_ball_x(0xA700)
        pinball.set_ball_y(0x9800)
        pinball.set_state("enable_ball_gravity_and_tilt", 0)
        pinball.set_state("pinball_launched", 0)
    end
end

--============================================================
-- BALL LOSS
--============================================================
function on_ball_loss(stage_id)
    -- Blue field ball loss handled by engine
end
