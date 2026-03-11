--[[
  Red Field - Object Collision Responses (ACTIVE)
  Original: native/src/game/red_field.c (resolve_* functions)
  ASM: engine/pinball_game/object_collision/red_stage_resolve_collision.asm

  This script implements ALL resolve/update functions for the Red Field.
  The check phase (bounding-box detection) is handled by the C engine.
  This script replaces the resolve phase (game logic responses).

  The dispatch order matches the original ASM exactly:
    Top:    ResolveRedFieldTopGameObjectCollisions (0x1460e)
    Bottom: ResolveRedFieldBottomGameObjectCollisions (0x14652)
]]

--============================================================
-- CONSTANTS
--============================================================

local STAGE_TOP    = 0x0  -- STAGE_RED_FIELD_TOP
local STAGE_BOTTOM = 0x1  -- STAGE_RED_FIELD_BOTTOM

-- Special collision IDs passed to check_special_mode_collision
local SPECIAL_NOTHING          = 0
local SPECIAL_LEFT_ALLEY       = 1
local SPECIAL_RIGHT_ALLEY      = 2
local SPECIAL_STARYU_ALLEY     = 3
local SPECIAL_VOLTORB          = 4
local SPECIAL_BELLSPROUT        = 5
local SPECIAL_STARYU           = 6
local SPECIAL_LEFT_DIGLETT     = 7
local SPECIAL_RIGHT_DIGLETT    = 8
local SPECIAL_LEFT_BONUS_MULT  = 9
local SPECIAL_RIGHT_BONUS_MULT = 10
local SPECIAL_BALL_UPGRADE     = 11
local SPECIAL_SPINNER          = 12
local SPECIAL_SLOT_HOLE        = 13

-- Ball types
local POKE_BALL   = 0
local GREAT_BALL  = 2
local ULTRA_BALL  = 3
local MASTER_BALL = 5

-- Ball type progression (upgrade table)
local BallTypeProgression = { [0]=2, [1]=2, [2]=3, [3]=5, [4]=5, [5]=5 }

-- Ball type degradation table: MASTER->ULTRA, ULTRA->GREAT
local BallTypeDegradation = { [0]=0, [1]=0, [2]=0, [3]=GREAT_BALL, [4]=0, [5]=ULTRA_BALL }

-- Max values
local MAX_PIKACHU_SAVER_CHARGE = 16
local MAX_BONUS_MULTIPLIER = 99
local MAX_EXTRA_BALLS = 9
local MAP_MOVE_FRAMES_COUNTER = 480

-- Bumper angle deltas (from red_field.c bumper_angle_deltas)
local BUMPER_ANGLE_DELTAS = { [0]=-10, [1]=10 }

-- Spinner charging SFX IDs
local SPINNER_SFX_IDS = {
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x11
}

-- Billboard picture IDs (from billboard.c pic array)
-- IDs 0-12: Slot rewards
local BILLBOARD_BALL_SAVER_30   = 0   -- 30sec ball saver
local BILLBOARD_BALL_SAVER_60   = 1   -- 60sec ball saver
local BILLBOARD_BALL_SAVER_90   = 2   -- 90sec ball saver
local BILLBOARD_PIKACHU_SAVER   = 3   -- pikachu saver
local BILLBOARD_EXTRA_BALL      = 4   -- extra ball
local BILLBOARD_SMALL_REWARD    = 5   -- small point reward
local BILLBOARD_BIG_REWARD      = 6   -- big point reward
local BILLBOARD_CATCHEM_MODE    = 7   -- catch'em mode
local BILLBOARD_EVOLUTION_MODE  = 8   -- evolution mode
local BILLBOARD_GREAT_BALL      = 9   -- great ball upgrade
local BILLBOARD_ULTRA_BALL      = 10  -- ultra ball upgrade
local BILLBOARD_MASTER_BALL     = 11  -- master ball upgrade
local BILLBOARD_BONUS_MULTIPLIER = 12 -- bonus multiplier
-- IDs 13-17: Bonus stages (next_bonus_stage + 13)
local BILLBOARD_GENGAR_BONUS    = 13
local BILLBOARD_MEWTWO_BONUS    = 14
local BILLBOARD_MEOWTH_BONUS    = 15
local BILLBOARD_DIGLETT_BONUS   = 16
local BILLBOARD_SEEL_BONUS      = 17
-- IDs 41-58: Map locations
local BILLBOARD_PALLET_TOWN_PIC = 41

-- Slot reward modes
local CATCHEM_MODE_SLOT_REWARD    = 1
local EVOLUTION_MODE_SLOT_REWARD  = 2

-- Scrolling text headers (matching C constants)
local FIELD_MULT_HEADER          = { 5, 0x54, 0x40, 20, 0x00, 60 }
local FIELD_MULT_SPECIAL_HEADER  = { 7, 0x54, 0, 0, 0, 51 }
local DIGITS_1_8_HEADER          = { 7, 0x73, 0x46, 20, 0x20, 80 }
local BONUS_MULT_TEXT_HEADER     = { 5, 0x54, 0x40, 20, 0x00, 61 }

-- Red field initial maps for ChooseInitialMap
local RED_INITIAL_MAPS = { 0, 2, 3, 5, 6, 8, 9 }  -- 0-indexed map IDs

--============================================================
-- ANIMATION SYSTEM (Lua implementation)
--============================================================

-- Animation data: tables of {duration, sprite_frame} pairs
local BellsproutAnimData = {
    {0x08, 0x01}, {0x06, 0x02}, {0x20, 0x03}, {0x06, 0x02},
    {0x08, 0x01}, {0x01, 0x00},
    -- Idle breathing loop (index 6+)
    {0x29, 0x00}, {0x28, 0x01}, {0x2A, 0x00}, {0x27, 0x01},
    {0x29, 0x00}, {0x28, 0x01}, {0x2B, 0x00}, {0x28, 0x01},
}

local PikachuSaverAnimData = {
    {0x0C, 0x02}, {0x05, 0x03}, {0x05, 0x02}, {0x05, 0x04},
    {0x05, 0x05}, {0x05, 0x02}, {0x06, 0x06}, {0x06, 0x07},
    {0x06, 0x08}, {0x06, 0x02}, {0x06, 0x05}, {0x06, 0x08},
    {0x06, 0x07}, {0x06, 0x02}, {0x06, 0x08}, {0x06, 0x07},
    {0x06, 0x02}, {0x01, 0x00},
}

local PikachuSaverAnim2Data = {
    {0x0C, 0x02}, {0x01, 0x00},
}

-- Initialize an animation (set first frame from data)
local function init_anim(name, data)
    local entry = data[1]
    if entry then
        pinball.set_anim(name, entry[1], entry[2], 0)
    end
end

-- Update an animation, returns true when a step completes (index advances)
local function update_anim(name, data)
    local a = pinball.get_anim(name)
    local fc, frame, idx = a.frame_counter, a.frame, a.index

    if fc > 0 then
        fc = fc - 1
        pinball.set_anim(name, fc, frame, idx)
        return false
    end

    -- Frame counter expired: advance to next entry
    idx = idx + 1
    local entry = data[idx + 1]  -- Lua is 1-indexed
    if not entry then
        -- End of animation data
        return true
    end

    pinball.set_anim(name, entry[1], entry[2], idx)
    return true
end

--============================================================
-- EXAMPLE: Voltorb Combo System (disabled by default)
-- Uncomment this block and the call sites in resolve_voltorb()
-- to replace default Voltorb behavior with a combo challenge.
--
-- Rules:
--   - Hitting any Voltorb starts a 3-second combo timer (~180 frames).
--   - Each unique Voltorb hit is tracked (3 total: IDs 0, 1, 2).
--   - If all 3 are hit before the timer expires:
--       * Awards 1,000,000 points
--       * Plays the Pikachu cry
--       * Fully charges the Pikachu saver
--   - Individual hits still award 500 points and bounce normally.
--   - If the timer expires before all 3 are hit, the combo resets.
--============================================================
--[[ VOLTORB COMBO - uncomment to activate
local voltorb_combo_timer = 0       -- frames remaining (0 = inactive)
local voltorb_combo_hits = {false, false, false}  -- which voltorbs hit

local function reset_voltorb_combo()
    voltorb_combo_timer = 0
    voltorb_combo_hits = {false, false, false}
    pinball.log("[COMBO] Reset")
end

local function on_voltorb_combo_hit(voltorb_index)
    pinball.log("[COMBO] Voltorb " .. voltorb_index .. " hit!")

    -- Start or continue combo
    if voltorb_combo_timer == 0 then
        reset_voltorb_combo()
        pinball.log("[COMBO] New combo started")
    end
    voltorb_combo_timer = 180  -- 3 seconds at ~60fps

    voltorb_combo_hits[voltorb_index + 1] = true  -- Lua 1-indexed

    pinball.log("[COMBO] Hits: "
        .. (voltorb_combo_hits[1] and "1" or "-") .. " "
        .. (voltorb_combo_hits[2] and "2" or "-") .. " "
        .. (voltorb_combo_hits[3] and "3" or "-")
        .. " | Timer: " .. voltorb_combo_timer)

    -- Check if all 3 hit
    if voltorb_combo_hits[1] and voltorb_combo_hits[2] and voltorb_combo_hits[3] then
        pinball.log("[COMBO] ALL 3 HIT! Awarding 1,000,000 points!")
        pinball.add_score("score_1000000")
        pinball.play_pcm(0)  -- Pikachu cry
        pinball.set_state("pikachu_saver_charge", MAX_PIKACHU_SAVER_CHARGE)
        pinball.set_state("pikachu_saver_sound_cooldown", 0x64)
        pinball.update_spinner_charge_graphics()
        reset_voltorb_combo()
    end
end

local function update_voltorb_combo_timer()
    if voltorb_combo_timer > 0 then
        voltorb_combo_timer = voltorb_combo_timer - 1
        if voltorb_combo_timer == 0 then
            pinball.log("[COMBO] Timer expired! Combo failed.")
            reset_voltorb_combo()
        end
    end
end
--]] -- END VOLTORB COMBO

--============================================================
-- RESOLVE FUNCTIONS - Top stage objects
--============================================================

-- ResolveVoltorbCollision (0x145a8)
local function resolve_voltorb()
    local which = pinball.get_state("which_voltorb")
    if which ~= 0 then
        local id = pinball.get_state("which_voltorb_id")
        pinball.set_state("which_voltorb", 0)

        -- Flipper force system
        pinball.set_state("flipper_y_force", 0x0200)
        pinball.set_state("flipper_collision", 0x80)
        pinball.rumble(0xFF, 3)

        -- 16-frame hit animation
        pinball.set_state("voltorb_hit_anim_duration", 0x10)
        pinball.set_state("which_animated_voltorb", id - 3)

        -- 500 points
        pinball.add_score("score_500")
        pinball.play_sfx_raw(0x00, 0x0E)
        pinball.check_special_mode_collision(SPECIAL_VOLTORB)

        -- EXAMPLE: Voltorb combo system (uncomment to activate)
        -- on_voltorb_combo_hit(id - 3)

        return
    end

    -- Per-frame animation update
    local dur = pinball.get_state("voltorb_hit_anim_duration")
    if dur > 0 then
        dur = dur - 1
        pinball.set_state("voltorb_hit_anim_duration", dur)
        if dur == 0 then
            pinball.set_state("which_animated_voltorb", 0xFF)
        end
    end

    -- EXAMPLE: Combo timer countdown (uncomment to activate)
    -- update_voltorb_combo_timer()
end

-- ResolveRedStageSpinnerCollision (0x14dea) + UpdateRedStageSpinner (0x14e10)
local function resolve_spinner()
    local collision = pinball.get_state("spinner_collision")
    if collision ~= 0 then
        pinball.set_state("spinner_collision", 0)
        -- Transfer ball Y velocity to spinner
        local vel = pinball.get_ball_y_velocity()
        pinball.set_state("spinner_velocity", vel)
        pinball.check_special_mode_collision(SPECIAL_SPINNER)
    end

    -- Per-frame spinner update
    local vel = pinball.get_state("spinner_velocity")
    if vel == 0 then return end

    -- Friction: subtract/add 7 toward zero
    if vel > 0 then
        vel = vel - 7
        if vel < 0 then vel = 0 end
    else
        vel = vel + 7
        if vel > 0 then vel = 0 end
    end
    pinball.set_state("spinner_velocity", vel)

    -- Add velocity to spinner state (16-bit accumulator)
    local lo = pinball.get_spinner_state(0)
    local hi = pinball.get_spinner_state(1)
    local state16 = hi * 256 + lo + vel

    lo = state16 % 256
    if lo < 0 then lo = lo + 256 end
    hi = math.floor(state16 / 256)

    -- Handle sign for hi byte
    local rotated = false
    if hi < 0 then
        hi = hi + 0x18
        rotated = true
    elseif hi >= 0x18 then
        hi = hi - 0x18
        rotated = true
    end

    -- Wrap to byte range
    hi = hi % 256
    if hi >= 128 then hi = hi - 256 end
    if hi < 0 then
        hi = hi + 0x18
        rotated = true
    end

    pinball.set_spinner_state(0, lo % 256)
    pinball.set_spinner_state(1, hi % 256)

    if rotated then
        -- 10 points per rotation
        pinball.add_score("score_10")
        local turns = pinball.get_state("num_spinner_turns")
        if turns < 100 then
            pinball.set_state("num_spinner_turns", turns + 1)
        end

        -- Charge Pikachu saver
        local charge = pinball.get_state("pikachu_saver_charge")
        if charge < MAX_PIKACHU_SAVER_CHARGE then
            charge = charge + 1
            pinball.set_state("pikachu_saver_charge", charge)
            if charge >= MAX_PIKACHU_SAVER_CHARGE then
                pinball.set_state("pikachu_saver_sound_cooldown", 0x64)
            end
        end

        -- Play spinner charging SFX
        local cooldown = pinball.get_state("pikachu_saver_sound_cooldown")
        if cooldown == 0 then
            local idx = charge
            if idx > 15 then idx = 15 end
            pinball.play_sfx_raw(0x00, SPINNER_SFX_IDS[idx + 1])
        end

        -- Update spinner charge graphics on top stage only
        local stage = pinball.get_current_stage()
        if stage % 2 == 0 then
            pinball.update_spinner_charge_graphics()
        end
    end
end

-- ResolveBallUpgradeTriggersCollision_RedField (0x1535d)
local function resolve_upgrade_triggers()
    local which = pinball.get_state("which_pinball_upgrade_trigger")
    if which == 0 then
        pinball.load_upgrade_triggers_graphics()
        return
    end

    local id = pinball.get_state("which_pinball_upgrade_trigger_id")
    pinball.set_state("which_pinball_upgrade_trigger", 0)

    -- Only active when stage collision state is odd (bit 0 set)
    local scs = pinball.get_state("stage_collision_state")
    if scs % 2 == 0 then
        pinball.load_upgrade_triggers_graphics()
        return
    end

    -- Skip if already blinking
    if pinball.get_state("ball_upgrade_triggers_blinking") ~= 0 then
        pinball.load_upgrade_triggers_graphics()
        return
    end

    -- Clear alley triggers + update field structures
    pinball.set_state("right_alley_trigger", 0)
    pinball.set_state("left_alley_trigger", 0)
    pinball.set_secondary_alley(0, 0)
    pinball.update_field_structures()
    pinball.check_special_mode_collision(SPECIAL_BALL_UPGRADE)

    local trig_idx = id - 0x0E
    if trig_idx >= 3 then
        pinball.load_upgrade_triggers_graphics()
        return
    end

    -- Set trigger state (skip if already on)
    if pinball.get_upgrade_trigger_state(trig_idx) ~= 0 then
        pinball.load_upgrade_triggers_graphics()
        return
    end

    pinball.set_upgrade_trigger_state(trig_idx, 1)
    pinball.add_score("score_100")

    -- Check all 3 triggers
    if pinball.get_upgrade_trigger_state(0) ~= 0 and
       pinball.get_upgrade_trigger_state(1) ~= 0 and
       pinball.get_upgrade_trigger_state(2) ~= 0 then
        -- All 3 triggered: start blinking, upgrade ball
        pinball.set_state("ball_upgrade_triggers_blinking", 1)
        pinball.set_state("ball_upgrade_triggers_blinking_frames_remaining", 0x80)
        pinball.set_state("ball_type_counter", 3600)
        pinball.add_score("score_400")

        local ball_type = pinball.get_ball_type()
        if ball_type >= MASTER_BALL then
            -- Master ball: SFX + 1M points + special bonus text
            pinball.play_sfx_raw(0x0F, 0x4D)
            pinball.add_score_no_mult("score_1000000")
            pinball.fill_bottom_message_black()
            pinball.enable_bottom_text()
            pinball.show_scrolling_text_bcd(1, DIGITS_1_8_HEADER, {0x01, 0x00, 0x00, 0x00})
            pinball.show_scrolling_text_full(0, FIELD_MULT_SPECIAL_HEADER,
                "FIELD MULTIPLIER SPECIAL BONUS")
        else
            -- Normal upgrade: SFX + text + upgrade
            pinball.play_sfx_raw(0x06, 0x3A)
            pinball.fill_bottom_message_black()
            pinball.enable_bottom_text()
            pinball.show_scrolling_text_full(0, FIELD_MULT_HEADER, "FIELD MULTIPLIER x0")
            local new_type = BallTypeProgression[ball_type] or ball_type
            pinball.set_ball_type(new_type)
            pinball.set_bottom_message_byte(18, 0x86 + new_type)
        end
        pinball.load_ball_gfx()
    else
        -- Single trigger
        pinball.play_sfx_raw(0x00, 0x09)
    end

    pinball.load_upgrade_triggers_graphics()
end

-- UpdatePinballUpgradeBlinkingAnimation_RedField (0x15270)
local function update_upgrade_blinking()
    local blinking = pinball.get_state("ball_upgrade_triggers_blinking")
    if blinking == 0 then
        -- Flipper key rotation of trigger states
        if pinball.is_key_pressed("left_flipper") then
            local a = pinball.get_upgrade_trigger_state(0)
            local b = pinball.get_upgrade_trigger_state(1)
            local c = pinball.get_upgrade_trigger_state(2)
            pinball.set_upgrade_trigger_state(0, b)
            pinball.set_upgrade_trigger_state(1, c)
            pinball.set_upgrade_trigger_state(2, a)
        elseif pinball.is_key_pressed("right_flipper") then
            local a = pinball.get_upgrade_trigger_state(0)
            local b = pinball.get_upgrade_trigger_state(1)
            local c = pinball.get_upgrade_trigger_state(2)
            pinball.set_upgrade_trigger_state(0, c)
            pinball.set_upgrade_trigger_state(1, a)
            pinball.set_upgrade_trigger_state(2, b)
        end
        return
    end

    local remaining = pinball.get_state("ball_upgrade_triggers_blinking_frames_remaining")
    remaining = remaining - 1
    pinball.set_state("ball_upgrade_triggers_blinking_frames_remaining", remaining)

    if remaining == 0 then
        pinball.set_state("ball_upgrade_triggers_blinking", 0)
        pinball.set_upgrade_trigger_state(0, 0)
        pinball.set_upgrade_trigger_state(1, 0)
        pinball.set_upgrade_trigger_state(2, 0)
        return
    end

    -- Blink toggle every 8 frames
    if remaining % 8 == 0 then
        local show = 0
        if math.floor(remaining / 8) % 2 == 1 then show = 1 end
        pinball.set_upgrade_trigger_state(0, show)
        pinball.set_upgrade_trigger_state(1, show)
        pinball.set_upgrade_trigger_state(2, show)
    end
end

-- UpdateBallTypeCounter (0x153e9) - degrades ball type after timeout
local function update_ball_type_counter()
    if pinball.get_state("capturing_mon") ~= 0 then return end
    local counter = pinball.get_state("ball_type_counter")
    if counter == 0 then return end

    counter = counter - 1
    pinball.set_state("ball_type_counter", counter)
    if counter ~= 0 then return end

    -- Counter expired: degrade ball type
    local bt = pinball.get_ball_type()
    if bt > 0 and bt < 6 then
        local new_type = BallTypeDegradation[bt] or 0
        pinball.set_ball_type(new_type)
        pinball.load_ball_gfx()
        if new_type > 0 then
            pinball.set_state("ball_type_counter", 3600)
        end
    end
end

-- ResolveRedStageBoardTriggerCollision (0x1581f)
local function resolve_board_triggers()
    if pinball.get_state("which_board_trigger") == 0 then return end

    local id = pinball.get_state("which_board_trigger_id")
    pinball.set_state("which_board_trigger", 0)

    -- 5 points
    pinball.add_score("score_5")

    -- Set collided trigger flag
    local trig_idx = id - 0x11
    if trig_idx >= 0 and trig_idx < 8 then
        pinball.set_alley_trigger(trig_idx, 1)
    end

    -- Trigger 0 ($11): HandleSecondaryLeftAlleyTrigger
    if pinball.get_alley_trigger(0) ~= 0 then
        pinball.set_alley_trigger(0, 0)
        if pinball.get_state("left_alley_trigger") ~= 0 then
            pinball.set_state("left_alley_trigger", 0)
            if not pinball.check_special_mode_collision(SPECIAL_LEFT_ALLEY) then
                local count = pinball.get_state("left_alley_count")
                if count < 3 then
                    count = count + 1
                    pinball.set_state("left_alley_count", count)
                    local ind_val = count
                    if count < 3 then ind_val = count + 0x80 end
                    pinball.set_indicator(0, ind_val)
                    if count == 3 then
                        -- Seal with collision state 6/7
                        local scs = pinball.get_state("stage_collision_state")
                        pinball.set_state("stage_collision_state", (scs % 2) + 6)
                        pinball.load_stage_collision_attributes()
                        pinball.load_field_structure_graphics()
                    end
                end
            end
        end
    end

    -- Trigger 1 ($12): HandleThirdLeftAlleyTrigger (same logic as trigger 0)
    if pinball.get_alley_trigger(1) ~= 0 then
        pinball.set_alley_trigger(1, 0)
        if pinball.get_state("left_alley_trigger") ~= 0 then
            pinball.set_state("left_alley_trigger", 0)
            if not pinball.check_special_mode_collision(SPECIAL_LEFT_ALLEY) then
                local count = pinball.get_state("left_alley_count")
                if count < 3 then
                    count = count + 1
                    pinball.set_state("left_alley_count", count)
                    local ind_val = count
                    if count < 3 then ind_val = count + 0x80 end
                    pinball.set_indicator(0, ind_val)
                    if count == 3 then
                        local scs = pinball.get_state("stage_collision_state")
                        pinball.set_state("stage_collision_state", (scs % 2) + 6)
                        pinball.load_stage_collision_attributes()
                        pinball.load_field_structure_graphics()
                    end
                end
            end
        end
    end

    -- Trigger 2 ($13): HandleSecondaryStaryuAlleyTrigger
    if pinball.get_alley_trigger(2) ~= 0 then
        pinball.set_alley_trigger(2, 0)
        if pinball.get_secondary_alley(0) ~= 0 then
            pinball.set_secondary_alley(0, 0)
            pinball.check_special_mode_collision(SPECIAL_STARYU_ALLEY)
        end
    end

    -- Trigger 3 ($14): HandleLeftAlleyTrigger
    if pinball.get_alley_trigger(3) ~= 0 then
        pinball.set_alley_trigger(3, 0)
        pinball.set_state("right_alley_trigger", 0)
        pinball.set_secondary_alley(0, 0)
        pinball.set_state("left_alley_trigger", 1)
        pinball.update_field_structures()
    end

    -- Trigger 4 ($15): HandleStaryuAlleyTrigger
    if pinball.get_alley_trigger(4) ~= 0 then
        pinball.set_alley_trigger(4, 0)
        pinball.set_state("right_alley_trigger", 0)
        pinball.set_state("left_alley_trigger", 0)
        pinball.set_secondary_alley(0, 1)
        pinball.update_field_structures()
    end

    -- Trigger 5 ($16): HandleSecondaryRightAlleyTrigger
    if pinball.get_alley_trigger(5) ~= 0 then
        pinball.set_alley_trigger(5, 0)
        if pinball.get_state("right_alley_trigger") ~= 0 then
            pinball.set_state("right_alley_trigger", 0)
            if not pinball.check_special_mode_collision(SPECIAL_RIGHT_ALLEY) then
                local count = pinball.get_state("right_alley_count")
                if count < 3 then
                    count = count + 1
                    pinball.set_state("right_alley_count", count)
                    if count == 3 then
                        pinball.set_indicator(1, count)
                    else
                        pinball.set_indicator(1, count + 0x80)
                    end
                    if count >= 2 then
                        pinball.set_indicator(3, 0x80)
                    end
                end
            end
        end
    end

    -- Trigger 6 ($17): HandleRightAlleyTrigger
    if pinball.get_alley_trigger(6) ~= 0 then
        pinball.set_alley_trigger(6, 0)
        pinball.set_state("left_alley_trigger", 0)
        pinball.set_secondary_alley(0, 0)
        pinball.set_state("right_alley_trigger", 1)
        pinball.update_field_structures()
    end

    -- Trigger 7 ($18): HandleThirdRightAlleyTrigger (same logic as trigger 5)
    if pinball.get_alley_trigger(7) ~= 0 then
        pinball.set_alley_trigger(7, 0)
        if pinball.get_state("right_alley_trigger") ~= 0 then
            pinball.set_state("right_alley_trigger", 0)
            if not pinball.check_special_mode_collision(SPECIAL_RIGHT_ALLEY) then
                local count = pinball.get_state("right_alley_count")
                if count < 3 then
                    count = count + 1
                    pinball.set_state("right_alley_count", count)
                    if count == 3 then
                        pinball.set_indicator(1, count)
                    else
                        pinball.set_indicator(1, count + 0x80)
                    end
                    if count >= 2 then
                        pinball.set_indicator(3, 0x80)
                    end
                end
            end
        end
    end
end

-- SetPikachuSaverSide_RedField (0x16766)
local function set_pikachu_saver_side()
    if pinball.is_key_pressed("left_flipper") then
        pinball.set_state("which_pikachu_saver_side", 0)
    elseif pinball.is_key_pressed("right_flipper") then
        pinball.set_state("which_pikachu_saver_side", 1)
    end
end

-- UpdatePikachuSaverAnimation_RedField (0x1669e)
local function update_pikachu_saver_animation()
    local pstate = pinball.get_state("pikachu_saver_state")

    if pstate == 1 then
        -- Full save animation
        local step_done = update_anim("pikachu_saver_anim", PikachuSaverAnimData)
        if not step_done then return end

        local idx = pinball.get_anim("pikachu_saver_anim").index
        if idx == 1 then
            -- Pikachu cry, heavy rumble, increment saves
            pinball.rumble(0xFF, 0x60)
            local saves = pinball.get_state("num_pikachu_saves")
            saves = saves + 1
            pinball.set_state("num_pikachu_saves", saves)
            if saves % 10 == 0 then
                pinball.add_extra_ball()
            end
            pinball.play_pcm(1)
            pinball.play_sfx_raw(0x16, 0x10)
        elseif idx == 17 then
            -- Animation complete: launch ball upward
            pinball.set_ball_y_velocity(-0x0400)
            pinball.set_state("enable_ball_gravity_and_tilt", 1)
            pinball.add_score("score_5000")
            pinball.set_state("pikachu_saver_state", 0)
        end
    elseif pstate == 2 then
        -- Partial bounce animation
        local step_done = update_anim("pikachu_saver_anim", PikachuSaverAnim2Data)
        if not step_done then return end

        local idx = pinball.get_anim("pikachu_saver_anim").index
        if idx == 1 then
            pinball.set_state("pikachu_saver_state", 0)
        end
    else
        -- Idle: cycle sprite frame based on frame counter
        local fc = pinball.get_hram("frame_counter")
        local frame = math.floor(fc / 16) % 2
        local a = pinball.get_anim("pikachu_saver_anim")
        pinball.set_anim("pikachu_saver_anim", a.frame_counter, frame, a.index)
    end
end

-- ResolveRedStagePikachuCollision (0x1660c)
local function resolve_pikachu()
    local which = pinball.get_state("which_pikachu")
    if which ~= 0 then
        pinball.set_state("which_pikachu", 0)

        local pstate = pinball.get_state("pikachu_saver_state")
        if pstate ~= 0 then
            goto per_frame
        end

        -- Hoist locals before goto to avoid scoping error
        local id = pinball.get_state("which_pikachu_id")
        local side = id - 0x1C

        -- Check slot reward (bypasses side/charge check)
        if pinball.get_state("pikachu_saver_slot_reward_active") ~= 0 then
            goto full_save
        end

        -- Check side match
        if side ~= pinball.get_state("which_pikachu_saver_side") then
            goto per_frame
        end

        -- Check fully charged
        if pinball.get_state("pikachu_saver_charge") >= MAX_PIKACHU_SAVER_CHARGE then
            goto full_save
        end

        -- Not fully charged: partial bounce
        init_anim("pikachu_saver_anim", PikachuSaverAnim2Data)
        pinball.set_state("pikachu_saver_state", 2)
        pinball.play_sfx_raw(0x00, 0x3B)
        goto per_frame

        ::full_save::
        pinball.fill_bottom_message_black()
        init_anim("pikachu_saver_anim", PikachuSaverAnimData)
        if pinball.get_state("pikachu_saver_slot_reward_active") == 0 then
            pinball.set_state("pikachu_saver_charge", 0)
        end
        pinball.set_state("pikachu_saver_state", 1)

        -- Freeze ball
        pinball.set_ball_x_velocity(0)
        pinball.set_ball_y_velocity(0)
        pinball.set_ball_spin(0)
        pinball.set_state("ball_rotation", 0)
        pinball.set_state("enable_ball_gravity_and_tilt", 0)
    end

    ::per_frame::
    -- Side selection only when idle
    if pinball.get_state("pikachu_saver_state") == 0 then
        set_pikachu_saver_side()
    end

    -- Animation state machine update
    update_pikachu_saver_animation()

    -- Sound cooldown for fully-charged indicator
    if pinball.get_state("pikachu_saver_charge") >= MAX_PIKACHU_SAVER_CHARGE then
        local cooldown = pinball.get_state("pikachu_saver_sound_cooldown")
        if cooldown > 0 then
            cooldown = cooldown - 1
            pinball.set_state("pikachu_saver_sound_cooldown", cooldown)
            if cooldown == 0x5A then
                pinball.play_sfx_raw(0x0F, 0x22)
            end
        end
    end
end

-- ResolveStaryuCollision (0x16781 / 0x167ff)
local function resolve_staryu()
    local stage = pinball.get_current_stage()
    local collision = pinball.get_state("staryu_collision")

    if collision ~= 0 then
        pinball.set_state("staryu_collision", 0)
        local timer = pinball.get_state("staryu_timer")

        if timer ~= 0 then
            timer = timer - 1
            pinball.set_state("staryu_timer", timer)
            if timer ~= 0 then return end
            -- Timer just expired: fall through
        else
            -- New collision: score, toggle, set timer
            pinball.add_score("score_5000")
            local side = pinball.get_state("staryu_side")
            side = (side == 0) and 1 or 0
            pinball.set_state("staryu_side", side)

            if stage == STAGE_TOP then
                pinball.set_state("staryu_anim_active", 1)
                pinball.load_staryu_graphics_top()
                pinball.check_special_mode_collision(SPECIAL_STARYU)
            else
                pinball.load_staryu_graphics_bottom()
                pinball.check_special_mode_collision(SPECIAL_STARYU)
            end
            pinball.set_state("staryu_timer", 0x14)
            return
        end
    else
        -- Per-frame timer countdown
        local timer = pinball.get_state("staryu_timer")
        if timer == 0 then return end
        timer = timer - 1
        pinball.set_state("staryu_timer", timer)
        if timer ~= 0 then return end
        -- Timer just expired: fall through
    end

    -- Timer-expired path
    pinball.set_state("staryu_anim_active", 0)
    local side = pinball.get_state("staryu_side")
    local scs = pinball.get_state("stage_collision_state")
    pinball.set_state("stage_collision_state", (scs - scs % 2) + (side % 2))

    if stage == STAGE_TOP then
        pinball.load_staryu_graphics_top()
        pinball.load_stage_collision_attributes()
        pinball.load_field_structure_graphics()
        pinball.play_sfx_raw(0x00, 0x07)
        pinball.load_upgrade_triggers_graphics()
    else
        pinball.play_sfx_raw(0x00, 0x07)
    end
end

-- ResolveBellsproutCollision (0x15e93)
local function resolve_bellsprout()
    if pinball.get_state("bellsprout_collision") ~= 0 then
        pinball.set_state("bellsprout_collision", 0)

        pinball.add_score("score_10000")
        pinball.play_sfx_raw(0x00, 0x05)

        -- Init animation
        init_anim("bellsprout_anim", BellsproutAnimData)

        -- Freeze ball at (0x7C, 0x78)
        pinball.set_ball_x_velocity(0)
        pinball.set_ball_y_velocity(0)
        pinball.set_ball_x(0x7C00)
        pinball.set_ball_y(0x7800)
        pinball.set_state("enable_ball_gravity_and_tilt", 0)
    end

    -- Per-frame animation update
    local step_done = update_anim("bellsprout_anim", BellsproutAnimData)

    -- Reset idle breathing loop
    local a = pinball.get_anim("bellsprout_anim")
    if a.frame_counter == 0 and a.index > 5 then
        pinball.set_anim("bellsprout_anim", 0x19, 0, 6)
    end

    if not step_done then return end

    -- Animation step completed - check index for game state changes
    local idx = pinball.get_anim("bellsprout_anim").index

    if idx == 1 then
        -- Mouth closing complete: ball disappears into Bellsprout
        pinball.set_state("pinball_is_visible", 0)
        local right_count = pinball.get_state("right_alley_count")
        if right_count >= 2 then
            local rare = 0
            if right_count > 2 then rare = 8 end
            pinball.set_state("rare_mons_flag", rare)
            pinball.start_catchem_mode()
        end
        local entries = pinball.get_state("num_bellsprout_entries")
        if entries < 100 then
            entries = entries + 1
            pinball.set_state("num_bellsprout_entries", entries)
            if entries % 25 == 0 then
                pinball.add_extra_ball()
            end
        end
    elseif idx == 4 then
        -- Mouth open again: ball reappears
        pinball.set_state("pinball_is_visible", 1)
    elseif idx == 5 then
        -- Spitting: re-enable gravity, push ball down
        pinball.set_state("enable_ball_gravity_and_tilt", 1)
        -- Clear high byte of X velocity (preserve low byte)
        local xvel = pinball.get_ball_x_velocity()
        pinball.set_ball_x_velocity(xvel % 256)
        pinball.set_ball_y_velocity(0x0200)
        pinball.play_sfx_raw(0x00, 0x06)
        pinball.check_special_mode_collision(SPECIAL_BELLSPROUT)
    end
end

-- ResolveDittoSlotCollision (0x160f0)
local function resolve_ditto_slot()
    if pinball.get_state("ditto_slot_collision") ~= 0 then
        pinball.set_state("ditto_slot_collision", 0)

        pinball.add_score("score_10000")
        pinball.play_sfx_raw(0x00, 0x21)

        -- Freeze ball at ditto slot position
        pinball.set_ball_x_velocity(0)
        pinball.set_ball_y_velocity(0)
        pinball.set_state("enable_ball_gravity_and_tilt", 0)
        pinball.set_ball_x(0x1100)
        pinball.set_ball_y(0x2300)
        pinball.set_state("ditto_enter_or_exit_counter", 0x10)
        pinball.rumble(0x05, 0x08)
    end

    -- Per-frame counter update
    local counter = pinball.get_state("ditto_enter_or_exit_counter")
    if counter > 0 then
        counter = counter - 1
        pinball.set_state("ditto_enter_or_exit_counter", counter)

        if counter == 0x0F then
            pinball.load_mini_ball_gfx()
        elseif counter == 0x0C then
            pinball.load_super_mini_ball_gfx()
        elseif counter == 0x09 then
            pinball.set_state("pinball_is_visible", 0)
            pinball.set_ball_spin(0)
            pinball.set_state("ball_rotation", 0)
        elseif counter == 0x06 then
            pinball.start_evolution_mode()
            pinball.set_state("pinball_is_visible", 1)
            pinball.set_state("enable_ball_gravity_and_tilt", 1)
            pinball.rumble(0x05, 0x08)
        elseif counter == 0x03 then
            pinball.load_mini_ball_gfx()
        elseif counter == 0x00 then
            pinball.load_ball_gfx()
            pinball.set_ball_y_velocity(0x0200)
        end
    end
end

--============================================================
-- RESOLVE FUNCTIONS - Bottom stage objects
--============================================================

-- ResolveWildMonCollision_RedField (0x14795)
local function resolve_wild_mon()
    if pinball.get_state("wild_mon_collision") == 0 then return end
    pinball.set_state("wild_mon_collision", 0)
    pinball.set_state("ball_hit_wild_mon", 1)
    pinball.play_sfx_raw(0x00, 0x06)
end

-- ResolveBumpersCollision_RedField (0x15f86)
local function resolve_bumpers()
    if pinball.get_state("which_bumper") ~= 0 then
        -- Load current (previous) graphics first
        pinball.load_bumper_graphics()

        -- LightUpBumper
        local id = pinball.get_state("which_bumper_id")
        local bumper_idx = id - 0x06
        pinball.set_state("bumper_light_up_duration", 0x10)
        pinball.set_state("which_bumper_gfx", bumper_idx)
        -- Load lit graphics immediately
        pinball.load_bumper_graphics()

        -- Clear flag after graphics load
        pinball.set_state("which_bumper", 0)

        -- Apply bumper collision force
        pinball.set_state("flipper_y_force", 0x0200)
        pinball.set_state("flipper_collision", 0x80)
        pinball.rumble(0xFF, 3)

        -- Angle delta
        if bumper_idx < 2 then
            local angle = pinball.get_state("collision_normal_angle")
            local delta = BUMPER_ANGLE_DELTAS[bumper_idx] or 0
            pinball.set_state("collision_normal_angle", (angle + delta) % 256)
        end

        pinball.play_sfx_raw(0x00, 0x0B)
        return
    end

    -- Per-frame: decrement light-up duration
    local dur = pinball.get_state("bumper_light_up_duration")
    if dur > 0 then
        dur = dur - 1
        pinball.set_state("bumper_light_up_duration", dur)
        if dur == 0 then
            pinball.load_bumper_graphics()
        end
    end
end

-- ResolveDiglettCollision (0x147aa)
local function resolve_diglett()
    if pinball.get_state("which_diglett") ~= 0 then
        local id = pinball.get_state("which_diglett_id")
        pinball.set_state("which_diglett", 0)

        local side = id - 1  -- 0=left, 1=right

        -- Get side-specific fields
        local counter_name = side == 0 and "left_map_move_counter" or "right_map_move_counter"
        local decay_name = side == 0 and "left_map_move_counter_frames_until_decrease"
                                      or "right_map_move_counter_frames_until_decrease"
        local ctrl_name = side == 0 and "left_diglett_anim_controller"
                                     or "right_diglett_anim_controller"

        local counter = pinball.get_state(counter_name)

        -- Already at max (3)? Skip to animation-only path
        if counter >= 3 then
            goto no_collision
        end

        counter = counter + 1
        pinball.set_state(counter_name, counter)
        pinball.set_state(ctrl_name, 0x50)  -- 80 frames hit animation
        pinball.set_state(decay_name, MAP_MOVE_FRAMES_COUNTER)

        if side == 1 then
            -- Right Diglett: update collision map attrs to hit state
            pinball.set_collision_map(0xF0, 0x6A)
            pinball.set_collision_map(0x110, 0x6B)
            pinball.load_diglett_graphics(5)
            pinball.load_diglett_number_graphics(pinball.get_state("right_map_move_counter") + 4)
            pinball.check_special_mode_collision(SPECIAL_RIGHT_DIGLETT)
            if pinball.get_state("right_map_move_counter") >= 3 then
                local triples = pinball.get_state("num_dugtrio_triples")
                if triples < 100 then
                    triples = triples + 1
                    pinball.set_state("num_dugtrio_triples", triples)
                    if triples % 10 == 0 then
                        pinball.add_extra_ball()
                    end
                end
                pinball.set_state("map_move_direction", 1)
                pinball.start_map_move_mode()
            end
        else
            -- Left Diglett: update collision map attrs to hit state
            pinball.set_collision_map(0xE3, 0x66)
            pinball.set_collision_map(0x103, 0x67)
            pinball.load_diglett_graphics(2)
            pinball.load_diglett_number_graphics(pinball.get_state("left_map_move_counter"))
            pinball.check_special_mode_collision(SPECIAL_LEFT_DIGLETT)
            if pinball.get_state("left_map_move_counter") >= 3 then
                local triples = pinball.get_state("num_dugtrio_triples")
                if triples < 100 then
                    triples = triples + 1
                    pinball.set_state("num_dugtrio_triples", triples)
                    if triples % 10 == 0 then
                        pinball.add_extra_ball()
                    end
                end
                pinball.set_state("map_move_direction", 0)
                pinball.start_map_move_mode()
            end
        end

        -- Score + force
        pinball.add_score("score_500")
        pinball.set_collision_force(2)
        pinball.rumble(0x55, 4)
        pinball.play_sfx_raw(0x00, 0x0F)
        return
    end

    ::no_collision::
    -- No collision path: decrement controllers, restore attrs

    -- Left Diglett controller
    local left_ctrl = pinball.get_state("left_diglett_anim_controller")
    if left_ctrl > 0 then
        left_ctrl = left_ctrl - 1
        pinball.set_state("left_diglett_anim_controller", left_ctrl)
        if left_ctrl == 0 then
            if pinball.get_state("left_map_move_counter") == 3 then
                pinball.set_state("left_map_move_counter", 0)
                pinball.load_diglett_number_graphics(0)
            end
            pinball.set_collision_map(0xE3, 0x64)
            pinball.set_collision_map(0x103, 0x65)
        end
    end

    -- Right Diglett controller
    local right_ctrl = pinball.get_state("right_diglett_anim_controller")
    if right_ctrl > 0 then
        right_ctrl = right_ctrl - 1
        pinball.set_state("right_diglett_anim_controller", right_ctrl)
        if right_ctrl == 0 then
            if pinball.get_state("right_map_move_counter") == 3 then
                pinball.set_state("right_map_move_counter", 0)
                pinball.load_diglett_number_graphics(4)
            end
            pinball.set_collision_map(0xF0, 0x68)
            pinball.set_collision_map(0x110, 0x69)
        end
    end

    -- Idle animation toggle
    if pinball.get_state("left_diglett_anim_controller") == 0 then
        local ac = pinball.get_state("left_map_move_diglett_anim_counter")
        if ac > 0 then
            pinball.set_state("left_map_move_diglett_anim_counter", ac - 1)
        else
            pinball.set_state("left_map_move_diglett_anim_counter", 0x14)
            local frame = pinball.get_state("left_map_move_diglett_frame")
            frame = (frame == 0) and 1 or 0
            pinball.set_state("left_map_move_diglett_frame", frame)
            pinball.load_diglett_graphics(frame)
        end
    end

    if pinball.get_state("right_diglett_anim_controller") == 0 then
        local ac = pinball.get_state("right_map_move_diglett_anim_counter")
        if ac > 0 then
            pinball.set_state("right_map_move_diglett_anim_counter", ac - 1)
        else
            pinball.set_state("right_map_move_diglett_anim_counter", 0x14)
            local frame = pinball.get_state("right_map_move_diglett_frame")
            frame = (frame == 0) and 1 or 0
            pinball.set_state("right_map_move_diglett_frame", frame)
            pinball.load_diglett_graphics(frame + 3)
        end
    end
end

-- UpdateMapMoveCounters (0x14880 / 0x148cf)
local function update_map_move_counters()
    local stage = pinball.get_current_stage()

    -- Left counter decay
    local left_timer = pinball.get_state("left_map_move_counter_frames_until_decrease")
    if left_timer > 0 then
        left_timer = left_timer - 1
        pinball.set_state("left_map_move_counter_frames_until_decrease", left_timer)
        if left_timer == 0 then
            pinball.set_state("left_map_move_counter_frames_until_decrease", MAP_MOVE_FRAMES_COUNTER)
            local counter = pinball.get_state("left_map_move_counter")
            if counter > 0 and counter < 3 then
                counter = counter - 1
                pinball.set_state("left_map_move_counter", counter)
                if stage % 2 == 1 then
                    pinball.load_diglett_number_graphics(counter)
                end
            end
        end
    end

    -- Right counter decay
    local right_timer = pinball.get_state("right_map_move_counter_frames_until_decrease")
    if right_timer > 0 then
        right_timer = right_timer - 1
        pinball.set_state("right_map_move_counter_frames_until_decrease", right_timer)
        if right_timer == 0 then
            pinball.set_state("right_map_move_counter_frames_until_decrease", MAP_MOVE_FRAMES_COUNTER)
            local counter = pinball.get_state("right_map_move_counter")
            if counter > 0 and counter < 3 then
                counter = counter - 1
                pinball.set_state("right_map_move_counter", counter)
                if stage % 2 == 1 then
                    pinball.load_diglett_number_graphics(counter + 4)
                end
            end
        end
    end
end

-- ResolveCAVELightCollision_RedField (0x151cb)
local function resolve_cave_lights()
    local which = pinball.get_state("which_cave_light")

    if which ~= 0 then
        local id = pinball.get_state("which_cave_light_id")
        pinball.set_state("which_cave_light", 0)

        -- If blinking, skip collision handling
        if pinball.get_state("cave_lights_blinking") == 0 then
            -- Toggle light on (id $0A-$0D -> index 0-3)
            local light_idx = id - 0x0A
            if light_idx >= 0 and light_idx < 4 then
                local prev = pinball.get_cave_light(light_idx)
                pinball.set_cave_light(light_idx, 1)

                if prev == 0 then
                    pinball.add_score("score_100")

                    -- Check all 4 lit
                    if pinball.get_cave_light(0) ~= 0 and
                       pinball.get_cave_light(1) ~= 0 and
                       pinball.get_cave_light(2) ~= 0 and
                       pinball.get_cave_light(3) ~= 0 then
                        pinball.set_state("cave_lights_blinking", 1)
                        pinball.set_state("cave_lights_blinking_frames_remaining", 0x80)
                        pinball.add_score("score_400")
                        pinball.play_sfx_raw(0x00, 0x09)
                        local completions = pinball.get_state("num_cave_completions")
                        if completions < 100 then
                            pinball.set_state("num_cave_completions", completions + 1)
                        end
                    end
                end
            end
            pinball.load_cave_lights_graphics()
            return
        end
        -- Blinking active during collision: fall through to update
    end

    -- UpdateCAVELightsBlinking
    if pinball.get_state("cave_lights_blinking") ~= 0 then
        local remaining = pinball.get_state("cave_lights_blinking_frames_remaining")
        remaining = remaining - 1
        pinball.set_state("cave_lights_blinking_frames_remaining", remaining)

        if remaining == 0 then
            pinball.set_state("cave_lights_blinking", 0)
            pinball.set_state("opened_slot_by_cave_lights", 1)
            pinball.set_state("frames_until_slot_cave_opens", 3)
            for i = 0, 3 do pinball.set_cave_light(i, 0) end
        end

        -- Blink toggle every 8 frames
        if remaining % 8 == 0 then
            local val = math.floor(remaining / 8) % 2
            for i = 0, 3 do pinball.set_cave_light(i, val) end
        end

        pinball.load_cave_lights_graphics()
        return
    end

    -- Not blinking: flipper rotation
    if pinball.is_key_pressed("left_flipper") then
        local c0 = pinball.get_cave_light(0)
        local c1 = pinball.get_cave_light(1)
        local c2 = pinball.get_cave_light(2)
        local c3 = pinball.get_cave_light(3)
        pinball.set_cave_light(0, c1)
        pinball.set_cave_light(1, c2)
        pinball.set_cave_light(2, c3)
        pinball.set_cave_light(3, c0)
        pinball.load_cave_lights_graphics()
        return
    end
    if pinball.is_key_pressed("right_flipper") then
        local c0 = pinball.get_cave_light(0)
        local c1 = pinball.get_cave_light(1)
        local c2 = pinball.get_cave_light(2)
        local c3 = pinball.get_cave_light(3)
        pinball.set_cave_light(0, c3)
        pinball.set_cave_light(1, c0)
        pinball.set_cave_light(2, c1)
        pinball.set_cave_light(3, c2)
        pinball.load_cave_lights_graphics()
        return
    end
end

-- ResolveRedStagePinballLaunchCollision (0x1652d)
local function resolve_launch_alley()
    if pinball.get_state("pinball_launch_collision") == 0 then return end
    pinball.set_state("pinball_launch_collision", 0)

    if pinball.get_state("pinball_launched") ~= 0 then
        -- Launched: apply launch velocity
        pinball.set_state("right_alley_trigger", 0)
        pinball.set_state("left_alley_trigger", 0)
        pinball.set_secondary_alley(0, 0)
        pinball.set_secondary_alley(1, 0)
        pinball.set_ball_x_velocity(0)
        pinball.set_ball_y_velocity(-0x0580)  -- -5.5
        pinball.set_ball_spin(0)
        pinball.set_state("ball_rotation", 0)
        pinball.set_state("enable_ball_gravity_and_tilt", 1)
        pinball.play_sfx_raw(0x00, 0x0A)
    end

    -- previous_triggered_game_object = $FF unconditionally
    pinball.set_state("previous_triggered_game_object", 0xFF)

    if pinball.get_state("pinball_launched") ~= 0 then return end

    -- Not yet launched
    if pinball.get_state("chose_initial_map") == 0 then
        -- ChooseInitialMap: non-blocking state machine
        local cycling = pinball.get_state("map_cycling_frames")
        if cycling == 0 then
            pinball.load_grey_billboard_palette()
            local idx = pinball.get_state("initial_map_selection_index") + 1
            if idx >= 7 then idx = 0 end
            pinball.set_state("initial_map_selection_index", idx)
            pinball.set_state("current_map", RED_INITIAL_MAPS[idx + 1])
            pinball.play_sfx_raw(0x00, 0x48)
            pinball.show_billboard(BILLBOARD_PALLET_TOWN_PIC + RED_INITIAL_MAPS[idx + 1])
            pinball.set_state("map_cycling_frames", 32)
            return
        end

        if pinball.is_key_pressed("ball_start") then
            pinball.load_map_billboard_tile_data()
            pinball.load_scrolling_map_name_text(0)
            local map = pinball.get_state("current_map")
            pinball.set_visited_map(0, map)
            pinball.set_state("num_map_moves", 0)
            pinball.set_state("chose_initial_map", 1)
            pinball.set_state("pinball_launched", 1)
            return
        else
            pinball.set_state("map_cycling_frames", cycling - 1)
            return
        end
    end

    -- Wait for A button to launch
    if pinball.is_key_pressed("ball_start") then
        pinball.set_state("pinball_launched", 1)
    end
end

-- GetBCDForNextBonusMultiplier (0x16f95)
local function get_bcd_for_next_bonus_multiplier()
    local val = pinball.get_state("cur_bonus_multiplier") + 1
    if val > MAX_BONUS_MULTIPLIER then val = MAX_BONUS_MULTIPLIER end
    pinball.set_state("bonus_multiplier_tens_digit", math.floor(val / 10))
    pinball.set_state("bonus_multiplier_ones_digit", val % 10)
end

-- ShowBonusMultiplierMessage_RedField (0x16ef5)
local function show_bonus_multiplier_message()
    if pinball.get_state("bottom_text_enabled") ~= 0 then return end
    if pinball.get_state("show_bonus_multiplier_bottom_message") == 0 then return end
    pinball.set_state("show_bonus_multiplier_bottom_message", 0)

    pinball.fill_bottom_message_black()
    pinball.enable_bottom_text()
    pinball.show_scrolling_text_full(0, BONUS_MULT_TEXT_HEADER, "BONUS MULTIPLIER x0  ")

    local tens = pinball.get_state("wd614") % 128
    local ones = pinball.get_state("wd615") % 128
    if tens ~= 0 then
        pinball.set_bottom_message_byte(18, 0x86 + tens)
        pinball.set_bottom_message_byte(19, 0x86 + ones)
    else
        pinball.set_bottom_message_byte(18, 0x86 + ones)
    end
end

-- UpdateBonusMultiplierRailing_RedField (0x16e51)
local function update_bonus_multiplier_railing()
    show_bonus_multiplier_message()

    -- Timer countdown
    local timer = pinball.get_state("wd612")
    if timer ~= 0 then
        timer = timer - 1
        pinball.set_state("wd612", timer)
        if timer == 0x70 then
            pinball.set_state("wd610", 2)
            pinball.set_state("wd611", 2)
        elseif timer == 0 then
            pinball.set_state("wd610", 3)
            pinball.set_state("wd611", 0)
            get_bcd_for_next_bonus_multiplier()
            pinball.load_bonus_mult_railing_gfx(pinball.get_state("bonus_multiplier_tens_digit"))
            pinball.load_bonus_mult_railing_gfx(pinball.get_state("bonus_multiplier_ones_digit") + 0x14)
            return
        end
    end

    -- Tens digit blinking
    local wd610 = pinball.get_state("wd610")
    if wd610 >= 2 then
        local fc = pinball.get_hram("frame_counter")
        if wd610 >= 3 then
            fc = math.floor(fc / 4)
        end
        if fc % 4 == 0 then
            local tens = pinball.get_state("bonus_multiplier_tens_digit")
            if math.floor(fc / 4) % 2 == 1 then
                tens = tens + 0x80
                if tens >= 256 then tens = tens - 256 end
            else
                tens = tens % 128
            end
            pinball.set_state("bonus_multiplier_tens_digit", tens)
            pinball.load_bonus_mult_railing_gfx(tens)
        end
    end

    -- Ones digit blinking
    local wd611 = pinball.get_state("wd611")
    if wd611 >= 2 then
        local fc = pinball.get_hram("frame_counter")
        if wd611 >= 3 then
            fc = math.floor(fc / 4)
        end
        if fc % 4 == 0 then
            local ones = pinball.get_state("bonus_multiplier_ones_digit")
            if math.floor(fc / 4) % 2 == 1 then
                ones = ones + 0x80
                if ones >= 256 then ones = ones - 256 end
            else
                ones = ones % 128
            end
            pinball.set_state("bonus_multiplier_ones_digit", ones)
            pinball.load_bonus_mult_railing_gfx(ones + 0x14)
        end
    end
end

-- ResolveRedStageBonusMultiplierCollision (0x16d9d)
local function resolve_bonus_multiplier()
    if pinball.get_state("which_bonus_multiplier_railing") == 0 then
        update_bonus_multiplier_railing()
        return
    end

    pinball.set_state("which_bonus_multiplier_railing", 0)
    pinball.play_sfx_raw(0x00, 0x0D)

    local railing_id = pinball.get_state("which_bonus_multiplier_railing_id")

    if railing_id == 0x21 then
        -- Left railing hit
        pinball.check_special_mode_collision(SPECIAL_LEFT_BONUS_MULT)
        if pinball.get_state("wd610") == 3 then
            pinball.set_state("wd610", 1)
            pinball.set_state("wd611", 3)
            local tens = pinball.get_state("bonus_multiplier_tens_digit")
            pinball.set_state("bonus_multiplier_tens_digit", tens + 0x80)
        end
    else
        -- Right railing hit
        pinball.check_special_mode_collision(SPECIAL_RIGHT_BONUS_MULT)
        if pinball.get_state("wd611") == 3 then
            pinball.set_state("wd610", 1)
            pinball.set_state("wd611", 1)
            pinball.set_state("wd612", 0x80)
            local ones = pinball.get_state("bonus_multiplier_ones_digit")
            pinball.set_state("bonus_multiplier_ones_digit", ones + 0x80)

            -- Increment multiplier
            local mult = pinball.get_state("cur_bonus_multiplier") + 1
            local capped = false
            if mult > MAX_BONUS_MULTIPLIER then
                mult = MAX_BONUS_MULTIPLIER
                capped = true
            end
            pinball.set_state("cur_bonus_multiplier", mult)

            if not capped then
                if mult % 25 == 0 then
                    pinball.add_extra_ball()
                end
            end

            -- Save digit backups for bottom message
            pinball.set_state("wd614", pinball.get_state("bonus_multiplier_tens_digit"))
            pinball.set_state("wd615", pinball.get_state("bonus_multiplier_ones_digit"))
            pinball.set_state("show_bonus_multiplier_bottom_message", 1)
        end
    end

    -- Common: score + reload graphics
    pinball.add_score("score_10")
    pinball.load_bonus_mult_railing_gfx(pinball.get_state("bonus_multiplier_tens_digit"))
    pinball.load_bonus_mult_railing_gfx(pinball.get_state("bonus_multiplier_ones_digit") + 0x14)
end

-- ResolveSlotCollision_RedField (0x16279)
local function resolve_slot()
    if pinball.get_state("slot_collision") ~= 0 then
        pinball.set_state("slot_collision", 0)

        -- Only enter if slot is open
        if pinball.get_state("slot_is_open") == 0 then return end
        if pinball.get_state("slot_enter_or_exit_counter") ~= 0 then return end

        -- Freeze ball at slot position
        pinball.set_ball_x_velocity(0)
        pinball.set_ball_y_velocity(0)
        pinball.set_state("enable_ball_gravity_and_tilt", 0)
        pinball.set_ball_x(0x5000)
        pinball.set_ball_y(0x1600)
        pinball.set_state("slot_enter_or_exit_counter", 0x13)
    end

    -- Roulette in progress
    if pinball.get_state("slot_roulette_active") ~= 0 then
        pinball.set_state("slot_glowing_anim_counter", 0x18)
        pinball.update_slot_roulette()
        return
    end

    local counter = pinball.get_state("slot_enter_or_exit_counter")
    if counter > 0 then
        counter = counter - 1
        pinball.set_state("slot_enter_or_exit_counter", counter)
        pinball.set_state("slot_glowing_anim_counter", 0x18)

        if counter == 0x12 then
            pinball.play_sfx_raw(0x00, 0x21)
            pinball.load_mini_ball_gfx()
        elseif counter == 0x0F then
            pinball.load_super_mini_ball_gfx()
        elseif counter == 0x0C then
            pinball.set_state("pinball_is_visible", 0)
            pinball.set_ball_spin(0)
            pinball.set_state("ball_rotation", 0)
        elseif counter == 0x09 then
            pinball.start_slot_roulette()
        elseif counter == 0x06 then
            pinball.set_state("slot_is_open", 0)
            pinball.rumble(0x05, 0x08)
            pinball.load_mini_ball_gfx()
        elseif counter == 0x03 then
            pinball.load_ball_gfx()
            pinball.set_ball_y_velocity(0x0200)
            pinball.set_ball_x_velocity(0x0080)
        elseif counter == 0x00 then
            pinball.load_slot_cave_cover_graphics()
            if pinball.get_state("catchem_or_evolution_slot_reward_active") == CATCHEM_MODE_SLOT_REWARD then
                local rnd = pinball.random_raw()
                pinball.set_state("rare_mons_flag", rnd % 256 % 16 >= 8 and 8 or 0)
                pinball.start_catchem_mode()
                pinball.set_state("catchem_or_evolution_slot_reward_active", 0)
            end
        end
    end
end

-- UpdatePokeballs (0x162f0 / 0x174ea)
local function update_pokeballs()
    local prev = pinball.get_state("previous_num_pokeballs")
    local cur = pinball.get_state("num_pokeballs")
    if prev == cur then return end

    local blink = pinball.get_state("pokeball_blinking_counter")
    if blink == 0 then
        pinball.set_state("pokeball_blinking_counter", 0x40)
        blink = 0x40
    end

    blink = blink - 1
    pinball.set_state("pokeball_blinking_counter", blink)

    if blink == 0 then
        -- Blink done: finalize
        pinball.set_state("previous_num_pokeballs", cur)
        pinball.load_pokeball_indicator_graphics()
        if cur >= 3 then
            pinball.set_state("opened_slot_by_pokeballs", 1)
            pinball.set_state("frames_until_slot_cave_opens", 3)
        end
        return
    end

    -- Only update display every 8th frame
    if blink % 8 ~= 0 then return end

    if math.floor(blink / 8) % 2 == 1 then
        -- Display NEW count
        pinball.load_pokeball_indicator_graphics()
    else
        -- Display OLD count: temporarily swap
        pinball.set_state("num_pokeballs", prev)
        pinball.load_pokeball_indicator_graphics()
        pinball.set_state("num_pokeballs", cur)
    end
end

--============================================================
-- MAIN DISPATCH - on_object_collision
-- Called by the engine after the C check phase sets which_* flags.
-- stage_id is the current stage, ball_x/ball_y are positions.
--============================================================
function on_object_collision(stage_id, ball_x, ball_y)
    if stage_id == STAGE_TOP then
        -- ResolveRedFieldTopGameObjectCollisions (0x1460e)
        resolve_voltorb()                               --  1
        resolve_spinner()                               --  2
        resolve_upgrade_triggers()                      --  3
        update_upgrade_blinking()                       --  3b
        update_ball_type_counter()                      --  4
        pinball.update_cave_lights_blinking()            --  5
        resolve_board_triggers()                        --  6
        resolve_pikachu()                               --  7
        resolve_staryu()                                --  8
        resolve_bellsprout()                            --  9
        resolve_ditto_slot()                            -- 10
        pinball.apply_slot_force_field()                 -- 11
        pinball.open_slot_cave()                         -- 12
        pinball.update_ball_saver()                      -- 13
        -- D-01: top stage does NOT draw ball saver icon
        update_pokeballs()                              -- 14
        update_map_move_counters()                      -- 15
        pinball.show_extra_ball_message()                -- 16
        pinball.check_special_mode_collision(SPECIAL_NOTHING) -- 17

    elseif stage_id == STAGE_BOTTOM then
        -- ResolveRedFieldBottomGameObjectCollisions (0x14652)
        resolve_wild_mon()                              --  1
        resolve_bumpers()                               --  2
        resolve_diglett()                               --  3
        update_map_move_counters()                      --  4
        resolve_spinner()                               --  5
        update_upgrade_blinking()                       --  6
        update_ball_type_counter()                      --  7
        resolve_cave_lights()                           --  8
        resolve_launch_alley()                          --  9
        resolve_pikachu()                               -- 10
        resolve_staryu()                                -- 11
        pinball.update_arrow_indicators()                -- 12
        resolve_bonus_multiplier()                      -- 13
        resolve_slot()                                  -- 14
        pinball.apply_slot_force_field()                 -- 15
        pinball.open_slot_cave()                         -- 16
        pinball.update_again_text()                      -- 17
        pinball.update_ball_saver()                      -- 18
        pinball.draw_ball_saver_icon()                   -- 18b
        update_pokeballs()                              -- 19
        pinball.show_extra_ball_message()                -- 20
        pinball.check_special_mode_collision(SPECIAL_NOTHING) -- 21
    end
end
