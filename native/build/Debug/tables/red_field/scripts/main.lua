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
-- Called once when the stage loads (pinball state 0: LoadGFX).
-- ASM: InitializeCurrentStage -> CallTable_8348 -> init_red_field
--============================================================
function on_stage_init(stage_id)
    -- Red field init sets up default state for a new game.
    -- Bulk reset (ResetDataForStageInitialization) already zeroed everything.
    -- This sets the non-zero defaults that init_red_field (0x30000) sets.

    -- Skip if loading saved game (ASM line 2-4: ret nz)
    if pinball.get_state("loading_saved_game") ~= 0 then return end

    -- Score, party, extras (already zeroed by bulk reset)
    pinball.set_state("ball_type", 0)       -- POKE_BALL
    pinball.set_state("ball_size", 0)
    pinball.set_state("previous_num_pokeballs", 0)
    pinball.set_state("num_pokeballs", 0)
    pinball.set_state("flippers_disabled", 0)

    -- Map (Pallet Town = 0)
    pinball.set_state("current_map", 0)

    -- Ball lives
    pinball.set_state("cur_ball_life", 1)
    pinball.set_state("num_ball_lives", 3)

    -- Bonus multiplier starts at 1 (ASM: ld a, 1)
    pinball.set_state("cur_bonus_multiplier", 1)
    pinball.set_state("bonus_multiplier_tens_digit", 0)
    pinball.set_state("bonus_multiplier_ones_digit", 1)

    -- Right alley count starts at 2 (ASM: ld a, 2)
    pinball.set_state("right_alley_count", 2)

    -- Bonus stage order: INDEX into BonusStages_RedField array (0-4)
    -- Index 3 = STAGE_DIGLETT_BONUS (ASM: ld a, 3)
    pinball.set_state("next_bonus_stage", 3)
    pinball.set_state("initial_next_bonus_stage", 3)
    pinball.set_state("wd610", 3)

    -- Stage collision state = 4 (ASM: ld a, 4 / ld [wStageCollisionState], a)
    pinball.set_state("stage_collision_state", 4)
    pinball.set_state("red_stage_structure_backup", 4)
    pinball.set_state("previous_field_structure_state", 0)

    -- Indicator states (ASM init_red_field.asm lines 38-42)
    pinball.set_indicator(0, 0x80)
    pinball.set_indicator(1, 0x82)
    pinball.set_indicator(2, 0)
    pinball.set_indicator(3, 0x80)
    -- indicators 4-18 already zeroed by bulk reset

    -- Ball saver: Start20SecondSaverTimer (ASM line 43)
    -- 0x23 = 35 seconds BCD, 3 display attempts
    pinball.set_state("ball_saver_timer_seconds", 0x23)
    pinball.set_state("ball_saver_timer_frames", 0)
    pinball.set_state("ball_saver_icon_on", 1)
    pinball.set_state("num_times_ball_saved_text_will_display", 3)
    pinball.set_state("ball_saver_timer_seconds_backup", 0x23)
    pinball.set_state("ball_saver_timer_frames_backup", 0)
    pinball.set_state("num_times_ball_saved_text_will_display_backup", 3)

    -- Initialize animations to idle states so per-frame updates
    -- don't accidentally trigger game logic on first frame.
    -- Bellsprout: indices 0-5 = bite sequence, 6+ = idle breathing.
    -- Without this, update_anim advances from (0,0,0) to index 1,
    -- which triggers catch mode and hides the ball immediately.
    pinball.set_anim("bellsprout_anim", 0x19, 0, 6)

    -- Current stage (ASM: ld a, STAGE_RED_FIELD_BOTTOM)
    pinball.set_state("current_stage", 1)

    -- Play red field music
    pinball.play_music("red_field")

    pinball.log("Red field stage initialized")
end

--============================================================
-- BALL INITIALIZATION
-- Called once when ball starts (pinball state 1: StartBall).
-- ASM: InitBallForStage -> InitBallRedField
--============================================================
function on_ball_init(stage_id)
    local returning = pinball.get_state("returning_from_bonus_stage")

    if returning ~= 0 then
        -- Returning from bonus stage: position at top of field
        -- ASM: StartBallAfterBonusStageRedField
        pinball.set_state("ball_size", 0)
        pinball.set_ball_x(0x5000)   -- X = $50.00
        pinball.set_ball_y(0x1600)   -- Y = $16.00
        pinball.set_state("returning_from_bonus_stage", 0)
        pinball.set_state("scx", 0)
        pinball.set_scroll(0, 0)
        pinball.set_state("flippers_disabled", 0)

        -- Restore ball type from backup
        local backup_type = pinball.get_state("ball_type_backup")
        pinball.set_ball_type(backup_type)

        -- Restart red field music
        pinball.play_music("red_field")
    else
        -- Normal start: ball in launcher area
        -- ASM: InitBallRedField - X=$00A7, Y=$0098
        pinball.set_ball_x(0xA700)   -- X = $A7.00
        pinball.set_ball_y(0x9800)   -- Y = $98.00
        pinball.set_state("enable_ball_gravity_and_tilt", 0)
        pinball.set_state("pinball_launched", 0)
    end
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
