--[[
  Gold Field - Sprite Rendering
  Based on: Red Field sprites.lua
  Modifications:
    - Gold palette override applied every frame (survives C palette reloads)
    - Ball speed boost at end of on_draw_sprites

  Palette overrides are applied every frame in on_draw_sprites because
  C code reloads palettes during load_stage_assets (state 1) and
  reload_stage_data (stage transitions), both of which run AFTER
  on_stage_init and on_ball_init.
]]

--============================================================
-- CONSTANTS
--============================================================
local STAGE_RED_FIELD_TOP    = 0x00
local STAGE_RED_FIELD_BOTTOM = 0x01

--============================================================
-- WORLD POSITIONS
--============================================================

-- Voltorb world positions (3 bumpers on top stage)
local voltorb_positions = {
    {0x3A, 0x4E},  -- Voltorb 1: x, y
    {0x53, 0x44},  -- Voltorb 2
    {0x4D, 0x60},  -- Voltorb 3
}

-- Evolution arrow positions (top: 6 arrows, bottom: 8 arrows)
local evo_arrow_top_pos = {
    {0x0D, 0x37}, {0x46, 0x22}, {0x8A, 0x4A},
    {0x41, 0x81}, {0x3D, 0x65}, {0x73, 0x74},
}
local evo_arrow_bot_pos = {
    {0x2D, 0x13}, {0x6A, 0x13}, {0x25, 0x2D}, {0x73, 0x2D},
    {0x0F, 0x40}, {0x1F, 0x40}, {0x79, 0x40}, {0x89, 0x40},
}

-- Evolution trinket positions (top: 12 slots, bottom: 6 slots)
local evo_trinket_top_pos = {
    {0x4C, 0x0C}, {0x32, 0x12}, {0x66, 0x12}, {0x19, 0x25},
    {0x7F, 0x25}, {0x1E, 0x36}, {0x7F, 0x36}, {0x0E, 0x65},
    {0x8B, 0x65}, {0x49, 0x7A}, {0x59, 0x7A}, {0x71, 0x7A},
}
local evo_trinket_bot_pos = {
    {0x3D, 0x13}, {0x5B, 0x13}, {0x31, 0x17},
    {0x67, 0x17}, {0x2E, 0x2C}, {0x6A, 0x2C},
}

-- Pikachu saver positions (left, right)
local pikachu_saver_x = {0x0F, 0x92}  -- [0]=left, [1]=right
local PIKACHU_SAVER_Y = 0x7E

-- Flipper positions
local LEFT_FLIPPER_X  = 0x38
local LEFT_FLIPPER_Y  = 0x7B
local RIGHT_FLIPPER_X = 0x68
local RIGHT_FLIPPER_Y = 0x7B

--============================================================
-- VOLTORB ANIMATION DATA
--============================================================
local voltorb_anim_data = {
    {0x1E, 0},  -- idle 30 frames
    {0x02, 1},  -- shake 2 frames
    {0x03, 0},  -- idle 3 frames
    {0x02, 1},  -- shake 2 frames
    {0x03, 0},  -- idle 3 frames
    {0x02, 1},  -- shake 2 frames
}

-- Staryu animation durations
local staryu_anim_durations = {0x14, 0x13, 0x15, 0x12, 0x14, 0x13, 0x16, 0x13}

--============================================================
-- HELPER: Get scroll-adjusted screen coordinates
--============================================================
local function world_to_screen(wx, wy)
    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")
    return (wx - scx) & 0xFF, (wy - scy) & 0xFF
end

--============================================================
-- DRAW PINBALL (ball sprite)
--============================================================
local function draw_pinball()
    if pinball.get_state("pinball_is_visible") == 0 then return end

    local rotation = pinball.get_state("ball_rotation")
    local spin = pinball.get_state("ball_spin")
    rotation = (rotation + spin) & 0xFF
    pinball.set_state("ball_rotation", rotation)

    local ball_x = pinball.get_state("ball_x_pos") >> 8
    local ball_y = pinball.get_state("ball_y_pos") >> 8
    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")
    local sx = (ball_x + 1 - scx) & 0xFF
    local sy = (ball_y + 1 - 0x10 - scy) & 0xFF

    local frame = (rotation >> 4) & 7
    pinball.load_sprite_data("ball_spin", frame, sy, sx)
end

--============================================================
-- DRAW FLIPPERS (bottom stage only)
--============================================================
local function draw_flipper_sprites()
    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")

    local lx = (LEFT_FLIPPER_X - scx) & 0xFF
    local ly = (LEFT_FLIPPER_Y - scy) & 0xFF
    local left_angle = pinball.get_state("left_flipper_state") >> 8
    if left_angle > 20 then left_angle = 20 end
    pinball.load_sprite_data("left_flipper", left_angle, ly, lx)

    local rx = (RIGHT_FLIPPER_X - scx) & 0xFF
    local ry = (RIGHT_FLIPPER_Y - scy) & 0xFF
    local right_angle = pinball.get_state("right_flipper_state") >> 8
    if right_angle > 20 then right_angle = 20 end
    pinball.load_sprite_data("right_flipper", right_angle, ry, rx)
end

--============================================================
-- DRAW PIKACHU SAVERS (bottom stage only)
--============================================================
local function draw_pikachu_savers()
    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")

    local side
    if pinball.get_state("pikachu_saver_slot_reward_active") == 0 then
        side = pinball.get_state("which_pikachu_saver_side")
    elseif pinball.get_state("pikachu_saver_state") == 0 then
        side = (pinball.get_hram("frame_counter") >> 3) & 1
    else
        local ball_x = pinball.get_state("ball_x_pos") >> 8
        side = (ball_x >= 0x50) and 1 or 0
    end

    local world_x = pikachu_saver_x[side + 1]
    local sx = (world_x - scx) & 0xFF
    local sy = (PIKACHU_SAVER_Y - scy) & 0xFF

    local anim = pinball.get_anim("pikachu_saver_anim")
    local frame = anim.frame
    if frame >= 9 then frame = 0 end
    pinball.load_sprite_data("pikachu_saver", frame, sy, sx)
end

--============================================================
-- DRAW VOLTORB SPRITES (top stage only)
--============================================================
local voltorb_anim_names = {"voltorb1_anim", "voltorb2_anim", "voltorb3_anim"}

local function draw_voltorb_sprites()
    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")

    for i = 1, 3 do
        local sx = (voltorb_positions[i][1] - scx) & 0xFF
        local sy = (voltorb_positions[i][2] - scy) & 0xFF

        local hit_dur = pinball.get_state("voltorb_hit_anim_duration")
        local which_anim = pinball.get_state("which_animated_voltorb")
        if hit_dur > 0 and which_anim == (i - 1) then
            pinball.draw_named_sprite("voltorb_collision", sy, sx)
        else
            local anim = pinball.get_anim(voltorb_anim_names[i])
            local fc = anim.frame_counter
            local idx = anim.index
            local fr = anim.frame

            if fc > 0 then
                fc = fc - 1
                if fc == 0 then
                    local next_idx = idx + 1
                    if next_idx < #voltorb_anim_data then
                        idx = next_idx
                        fc = voltorb_anim_data[idx + 1][1]
                        fr = voltorb_anim_data[idx + 1][2]
                    else
                        idx = 0
                        fc = 0
                    end
                end
            end

            if fc == 0 then
                fc = (pinball.random_raw() & 7) + 0x1E
                fr = 0
                idx = 0
            end

            pinball.set_anim(voltorb_anim_names[i], fc, fr, idx)

            if fr == 1 then
                pinball.load_sprite_data("voltorb_shake", i - 1, sy, sx)
            else
                pinball.draw_named_sprite("voltorb_stationary", sy, sx)
            end
        end
    end
end

--============================================================
-- DRAW BELLSPROUT (top stage only)
--============================================================
local function draw_bellsprout()
    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")

    local hx = (0x74 - scx) & 0xFF
    local hy = (0x52 - scy) & 0xFF
    local anim = pinball.get_anim("bellsprout_anim")
    local head_frame = anim.frame
    if head_frame >= 4 then head_frame = 0 end
    pinball.load_sprite_data("bellsprout_head", head_frame, hy, hx)

    local bx = (0x67 - scx) & 0xFF
    local by = (0x54 - scy) & 0xFF
    pinball.draw_named_sprite("bellsprout_body", by, bx)
end

--============================================================
-- DRAW STARYU (top and bottom stages)
--============================================================
local function draw_staryu(world_x, world_y)
    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")
    local sx = (world_x - scx) & 0xFF
    local sy = (world_y - scy) & 0xFF

    local anim = pinball.get_anim("staryu_anim")
    local fc = anim.frame_counter
    local fr = anim.frame
    local idx = anim.index

    if fc > 0 then
        fc = fc - 1
        if fc == 0 then
            local next_idx = idx + 1
            if next_idx < 8 then
                fc = staryu_anim_durations[next_idx + 1]
                idx = next_idx
                fr = fr ~ 1
            end
        end
    end

    if fc == 0 then
        fc = 0x13
        fr = 0
        idx = 0
    end

    pinball.set_anim("staryu_anim", fc, fr, idx)

    local frame = fr & 1
    pinball.load_sprite_data("staryu", frame, sy, sx)
end

--============================================================
-- DRAW SPINNER (top stage only)
--============================================================
local function draw_spinner()
    local sx, sy = world_to_screen(0x88, 0x5A)
    local spinner_byte = pinball.get_spinner_state(1)
    local frame = (spinner_byte >> 2) % 6
    pinball.load_sprite_data("spinner", frame, sy, sx)
end

--============================================================
-- DRAW DITTO (top stage only)
--============================================================
local function draw_ditto()
    local sx, sy = world_to_screen(0x00, 0x10)
    local collision_state = pinball.get_state("stage_collision_state")
    if collision_state > 7 then collision_state = 7 end
    pinball.load_sprite_data("ditto", collision_state, sy, sx)
end

--============================================================
-- DRAW TIMER (top and bottom stages)
--============================================================
local function draw_timer(x, y)
    if pinball.get_state("timer_active") == 0 then return end

    local m = pinball.get_state("timer_minutes") & 0x0F
    if m > 9 then m = 9 end
    pinball.load_sprite_data("timer_digit", m, y, x)
    x = (x + 8) & 0xFF

    pinball.load_sprite_data("timer_digit", 10, y, x)
    x = (x + 8) & 0xFF

    local tens = (pinball.get_state("timer_seconds") >> 4) & 0x0F
    if tens > 9 then tens = 9 end
    pinball.load_sprite_data("timer_digit", tens, y, x)
    x = (x + 8) & 0xFF

    local ones = pinball.get_state("timer_seconds") & 0x0F
    if ones > 9 then ones = 9 end
    pinball.load_sprite_data("timer_digit", ones, y, x)
end

--============================================================
-- DRAW MON CAPTURE ANIMATION (bottom stage only)
--============================================================
local function draw_mon_capture_animation()
    if pinball.get_state("capturing_mon") == 0 then return end
    local sx, sy = world_to_screen(0x50, 0x38)
    local frame = pinball.get_anim("ball_capture_anim").frame
    if frame > 12 then frame = 12 end
    pinball.load_sprite_data("ball_capture", frame, sy, sx)
end

--============================================================
-- DRAW ANIMATED MON - RED STAGE (bottom stage only)
--============================================================
local function draw_animated_mon_red_stage()
    if pinball.get_state("wild_mon_is_hittable") == 0 then return end
    local sx, sy = world_to_screen(0x50, 0x3E)
    local frame = pinball.get_state("current_animated_mon_sprite_frame")
    if frame > 11 then frame = 11 end
    pinball.load_sprite_data("animated_mon", frame, sy, sx)
end

--============================================================
-- DRAW EVOLUTION INDICATOR ARROWS
--============================================================
local function draw_evolution_indicator_arrows(stage_id)
    if pinball.get_state("evolution_objects_disabled") ~= 0 then return end
    if (pinball.get_hram("frame_counter") & 0x10) == 0 then return end

    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")

    if stage_id == STAGE_RED_FIELD_TOP then
        for i = 1, 6 do
            if pinball.get_indicator(5 + i - 1) ~= 0 then
                local sx = (evo_arrow_top_pos[i][1] - scx) & 0xFF
                local sy = (evo_arrow_top_pos[i][2] - scy) & 0xFF
                pinball.load_sprite_data("evo_arrow_top", i - 1, sy, sx)
            end
        end
    else
        for i = 1, 8 do
            if pinball.get_indicator(11 + i - 1) ~= 0 then
                local sx = (evo_arrow_bot_pos[i][1] - scx) & 0xFF
                local sy = (evo_arrow_bot_pos[i][2] - scy) & 0xFF
                pinball.load_sprite_data("evo_arrow_bot", i - 1, sy, sx)
            end
        end
    end
end

--============================================================
-- DRAW EVOLUTION TRINKET
--============================================================
local function draw_evolution_trinket(stage_id)
    if pinball.get_state("evolution_objects_disabled") == 0 then return end

    local scx = pinball.get_hram("scx")
    local scy = pinball.get_hram("scy")
    local bob = 0
    if (pinball.get_hram("frame_counter") & 0x0E) == 0 then bob = 1 end

    if stage_id == STAGE_RED_FIELD_TOP then
        local array_name = "evo_trinket_top"
        for i = 1, 12 do
            local trinket_type = pinball.get_trinket(i - 1)
            if trinket_type >= 1 and trinket_type <= 7 then
                local sx = (evo_trinket_top_pos[i][1] - scx) & 0xFF
                local sy = (evo_trinket_top_pos[i][2] - scy - bob) & 0xFF
                pinball.load_sprite_data(array_name, trinket_type, sy, sx)
            end
        end
    else
        local array_name = "evo_trinket_bot"
        for i = 1, 6 do
            local trinket_type = pinball.get_trinket(12 + i - 1)
            if trinket_type >= 1 and trinket_type <= 7 then
                local sx = (evo_trinket_bot_pos[i][1] - scx) & 0xFF
                local sy = (evo_trinket_bot_pos[i][2] - scy - bob) & 0xFF
                pinball.load_sprite_data(array_name, trinket_type, sy, sx)
            end
        end
    end
end

--============================================================
-- DRAW SLOT GLOW - RED FIELD (bottom stage only)
--============================================================
local function draw_slot_glow()
    if pinball.get_state("slot_is_open") == 0 then return end

    local counter = pinball.get_state("slot_glowing_anim_counter") + 1
    pinball.set_state("slot_glowing_anim_counter", counter & 0xFF)

    local sx, sy = world_to_screen(0x40, 0x01)

    local frame = (counter >> 3) & 3
    if frame < 3 then
        pinball.load_sprite_data("slot_glow", frame, sy, sx)
    end
end

--============================================================
-- MAIN SPRITE DISPATCH
-- Gold Field modification: ball speed boost at end
--============================================================
function on_draw_sprites(stage_id)
    -- Apply gold palettes every frame (C code overwrites palettes during
    -- load_stage_assets and reload_stage_data, so we must re-apply here)
    apply_gold_palettes()

    pinball.clear_sprites()

    if stage_id == STAGE_RED_FIELD_TOP then
        draw_timer(0x7F, 0x00)
        draw_voltorb_sprites()
        draw_ditto()
        draw_bellsprout()
        draw_staryu(0x2B, 0x69)
        draw_spinner()
        draw_pinball()
        draw_evolution_indicator_arrows(stage_id)
        draw_evolution_trinket(stage_id)

    elseif stage_id == STAGE_RED_FIELD_BOTTOM then
        draw_timer(0x7F, 0x00)
        draw_mon_capture_animation()
        draw_animated_mon_red_stage()
        draw_pikachu_savers()
        draw_flipper_sprites()
        draw_pinball()
        draw_evolution_indicator_arrows(stage_id)
        draw_evolution_trinket(stage_id)
        draw_slot_glow()
    end

    -- GOLD FIELD SPEED BOOST: extra pull each frame
    local vy = pinball.get_ball_y_velocity()
    if vy ~= 0 then
        pinball.set_ball_y_velocity(vy + ((vy > 0) and 6 or -3))
    end
end
