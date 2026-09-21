--[[
  Gold Field - Main Script
  A custom table that reuses Red Field's stage IDs (0/1) but applies
  gold-tinted palettes and increased ball speed.

  Palette overrides are applied every frame in on_draw_sprites (sprites.lua)
  because C code reloads palettes during load_stage_assets (state 1) and
  reload_stage_data (stage transitions), both of which run AFTER on_stage_init
  and on_ball_init.
]]

--============================================================
-- GOLD PALETTE DEFINITIONS (shared with sprites.lua)
-- GBC RGB555 format: r | (g << 5) | (b << 10)
-- Each palette has 4 colors: [lightest, light, dark, darkest]
--============================================================

-- Helper: convert (r, g, b) in 0-31 range to RGB555
function gold_rgb(r, g, b)
    return r + (g * 32) + (b * 1024)
end

-- Deep gold/amber BG palettes - dramatically different from red field
GOLD_BG_PALETTES = {
    -- BG 0: Main field background - deep amber
    { gold_rgb(31,28,8),  gold_rgb(28,20,0),  gold_rgb(16,10,0),  gold_rgb(4,2,0)   },
    -- BG 1: Field structures - warm gold
    { gold_rgb(31,27,6),  gold_rgb(24,16,0),  gold_rgb(14,8,0),   gold_rgb(3,1,0)   },
    -- BG 2: Billboard area - bronze
    { gold_rgb(31,26,10), gold_rgb(22,14,0),  gold_rgb(12,6,0),   gold_rgb(2,1,0)   },
    -- BG 3: Scoreboard/text - bright gold on dark
    { gold_rgb(31,31,16), gold_rgb(31,24,0),  gold_rgb(18,10,0),  gold_rgb(0,0,0)   },
    -- BG 4: Field details - copper
    { gold_rgb(31,24,6),  gold_rgb(22,14,0),  gold_rgb(14,8,0),   gold_rgb(4,2,0)   },
    -- BG 5: Indicators/lights - bright amber flash
    { gold_rgb(31,30,10), gold_rgb(31,22,0),  gold_rgb(20,12,0),  gold_rgb(5,2,0)   },
    -- BG 6: Stage structure alt - antique gold
    { gold_rgb(31,26,8),  gold_rgb(22,14,0),  gold_rgb(12,6,0),   gold_rgb(3,1,0)   },
    -- BG 7: Dark accent - deep bronze
    { gold_rgb(31,25,6),  gold_rgb(20,12,0),  gold_rgb(10,5,0),   gold_rgb(2,0,0)   },
}

-- Deep gold/amber OBJ palettes
GOLD_OBJ_PALETTES = {
    -- OBJ 0: Ball / main sprites - golden ball
    { gold_rgb(31,28,8),  gold_rgb(31,22,0),  gold_rgb(18,10,0),  gold_rgb(0,0,0)   },
    -- OBJ 1: Pikachu - warm yellow
    { gold_rgb(31,30,10), gold_rgb(31,26,0),  gold_rgb(20,14,0),  gold_rgb(0,0,0)   },
    -- OBJ 2: Bumpers/flippers - bronze
    { gold_rgb(31,28,10), gold_rgb(26,18,0),  gold_rgb(16,8,0),   gold_rgb(0,0,0)   },
    -- OBJ 3: Highlights - bright gold flash
    { gold_rgb(31,31,16), gold_rgb(31,24,0),  gold_rgb(22,14,0),  gold_rgb(0,0,0)   },
    -- OBJ 4: Timer/UI sprites - amber digits
    { gold_rgb(31,28,8),  gold_rgb(31,20,0),  gold_rgb(16,8,0),   gold_rgb(0,0,0)   },
    -- OBJ 5: Wild mon / billboard - warm tint
    { gold_rgb(31,27,10), gold_rgb(24,16,0),  gold_rgb(14,6,0),   gold_rgb(0,0,0)   },
    -- OBJ 6: Evolution trinkets - gold tokens
    { gold_rgb(31,30,10), gold_rgb(31,22,0),  gold_rgb(18,10,0),  gold_rgb(0,0,0)   },
    -- OBJ 7: Slot glow / special - warm glow
    { gold_rgb(31,28,8),  gold_rgb(31,26,4),  gold_rgb(22,16,0),  gold_rgb(0,0,0)   },
}

-- Apply gold palettes (called from sprites.lua every frame)
function apply_gold_palettes()
    for i = 0, 7 do
        pinball.set_bg_palette(i, GOLD_BG_PALETTES[i + 1])
    end
    for i = 0, 7 do
        pinball.set_obj_palette(i, GOLD_OBJ_PALETTES[i + 1])
    end
end

--============================================================
-- STAGE INITIALIZATION
--============================================================
function on_stage_init(stage_id)
    apply_gold_palettes()
    pinball.log("Gold field stage " .. stage_id .. " initialized")
end

--============================================================
-- BALL INITIALIZATION
--============================================================
function on_ball_init(stage_id)
    apply_gold_palettes()
end

--============================================================
-- BALL LOSS
--============================================================
function on_ball_loss(stage_id)
    -- Gold field ball loss handled by engine
end
