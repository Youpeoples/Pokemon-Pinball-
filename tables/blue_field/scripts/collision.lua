--[[
  Blue Field - Object Collision Responses
  Original: native/src/game/blue_field.c (resolve_* functions)
  ASM: engine/pinball_game/object_collision/blue_stage_resolve_collision.asm

  This script defines how the game responds when the ball hits each
  game object on the Blue Field. Each resolve function runs every frame
  and checks its own collision flag.

  The check phase (bounding-box detection) is still handled by the C engine.
  This script replaces the resolve phase (game logic responses).

  IMPORTANT: This is a TEMPLATE showing the structure. The actual resolve
  functions are complex state machines (2000+ lines in C). To fully convert,
  each resolve_* function below would need to replicate the C logic.
  Until then, the C fallback handles these automatically.

  Modders: Override specific resolve functions to change behavior.
  Any function not defined here will use the C fallback.
]]

--============================================================
-- OBJECT COLLISION IDS
-- These map to the object groups defined in check_blue_field_object_collisions
--============================================================
local COLLISION_IDS = {
    -- Top stage objects
    SHELLDER     = {1, 2, 3},     -- 3 Shellder bumpers
    SPINNER      = 4,             -- Spinner (top stage)
    BOARD_TRIG   = {5,6,7,8,9,10,11,12}, -- 8 board triggers
    SLOWPOKE     = 13,            -- Slowpoke (directional bumper)
    CLOYSTER     = 14,            -- Cloyster (evolves from Shellder)
    UPGRADE_TRIG = {15, 16, 17},  -- 3 ball upgrade triggers

    -- Bottom stage objects
    WILD_MON     = 18,            -- Wild mon on billboard
    PSYDUCK      = {19, 20},      -- Psyduck/Poliwag map move triggers
    POLIWAG      = {19, 20},      -- Same IDs (left/right)
    BONUS_MULT   = {21, 22},      -- Bonus multiplier railings
    SLOT         = 23,            -- Slot entrance
    BUMPER       = {24, 25},      -- Left/right bumpers
    PIKACHU      = {26, 27},      -- Left/right Pikachu savers
    CAVE_LIGHT   = {28,29,30,31}, -- C-A-V-E lights
    LAUNCH_ALLEY = 32,            -- Launch alley
}

--============================================================
-- SCORE VALUES
-- Use pinball.add_score(name) with these config names from scores.json
--============================================================
local SCORES = {
    SHELLDER   = "score_100",      -- 100 points per Shellder hit
    SPINNER    = "score_100",      -- 100 points per spinner turn
    BUMPER     = "score_100",      -- 100 points per bumper hit
    CAVE       = "score_5000",     -- 5000 points per CAVE light
    SLOWPOKE   = "score_10000",    -- 10000 per Slowpoke entry
    CLOYSTER   = "score_10000",    -- 10000 per Cloyster entry
    PSYDUCK    = "score_5",        -- 5 points per Psyduck hit
    POLIWAG    = "score_5",        -- 5 points per Poliwag hit
    WILD_MON   = "score_100000",   -- 100000 per wild mon hit
}

--============================================================
-- BLUE FIELD SPECIAL MECHANICS
--============================================================
-- Force Field:
--   Directional barrier near the slot entrance. Direction alternates
--   based on collision count. When active, pushes the ball in a specific
--   direction (left or right depending on state).
--
-- Slowpoke:
--   Opens when hit, creates a path similar to Bellsprout on Red Field.
--   Sends ball toward the force field area.
--
-- Cloyster:
--   Evolved form of Shellder. When hit during special conditions,
--   Shellder transforms into Cloyster (larger, more valuable target).
--
-- Psyduck/Poliwag:
--   Blue Field's map move triggers (replaces Diglett from Red Field).
--   Left Psyduck and right Poliwag each decrement counters to move maps.
--   Located in upper half of bottom screen.
--
-- Shellder x3:
--   Blue Field equivalent of Voltorb. Three bumpers positioned on top stage.
--   Similar behavior but water-themed sprites.
--
--============================================================

--============================================================
-- EXAMPLE: Customized Shellder bumper response
-- Uncomment and modify to override the default C behavior.
--============================================================
--[[
function resolve_shellder_hit(shellder_index)
    -- Award score
    pinball.add_score(SCORES.SHELLDER)

    -- Play bumper sound
    pinball.play_sfx("bumper")

    -- Set collision force for bouncy response
    pinball.set_collision_force(0x10)

    -- Increment bumper hit counter for end-of-ball bonus
    local hits = pinball.get_state("num_bumper_hits")
    pinball.set_state("num_bumper_hits", hits + 1)
end
]]

--============================================================
-- MAIN COLLISION HANDLER
-- Called by the engine when any object collision is detected.
-- Dispatches to individual resolve functions.
--
-- NOTE: This hook is NOT currently active (on_object_collision is
-- not defined). The C code handles all collision resolution.
-- To activate Lua collision handling, uncomment the function below
-- and implement the resolve functions you want to customize.
--============================================================
--[[
function on_object_collision(collision_id, ball_x, ball_y)
    -- Dispatch based on collision_id
    -- Each handler should check if the collision actually applies
    -- (some objects have attribute-gated collisions)

    -- Example: Override just the Shellder response
    if collision_id >= 1 and collision_id <= 3 then
        resolve_shellder_hit(collision_id - 1)
    end

    -- All other collisions fall through to C code
    -- (This requires the C engine to support partial Lua overrides,
    -- which is a future enhancement)
end
]]

--============================================================
-- DOCUMENTATION: Blue Field Object Layout
--
-- TOP STAGE (stage 4):
--   Shellder x3: Upper area, circular water-themed bumpers
--   Spinner: Center-right, rotates on hit
--   Board triggers x8: Various positions, trigger map events
--   Slowpoke: Opens a path when hit (like Bellsprout on Red)
--   Cloyster: Evolved form of Shellder (special condition)
--   Force Field: Directional barrier near slot entrance
--   Upgrade triggers x3: Ball type upgrade progression
--   Evolution trinkets: Appear during evolution mode
--
-- BOTTOM STAGE (stage 5):
--   Wild mon: Billboard area, hittable during catch mode
--   Psyduck/Poliwag: Map move counters (left/right)
--   Bonus multiplier x2: Left/right railings
--   Slot: Main slot cave entrance (with force field)
--   Bumpers x2: Left/right bumpers near bottom
--   Pikachu x2: Ball saver charge positions
--   CAVE lights x4: Spell C-A-V-E to open slot
--   Launch alley: Ball launcher area
--   Evolution trinkets: Appear during evolution mode
--============================================================
