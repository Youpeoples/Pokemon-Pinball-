--[[
  Blue Field - Evolution Mode State Machine
  Original: native/src/game/blue_field.c (check_special_mode_collision state 1)
  ASM: engine/pinball_game/evolution_mode.asm

  Evolution mode is activated by matching 3 Item symbols on the slot machine.
  The player selects a party Pokemon and collects evolution trinkets.

  States (managed by special_mode_state):
    0: Collecting trinkets - trinkets appear on field, collect 3
    1: Evolution check - does the Pokemon evolve with this type?
    2: Evolution animation - show evolution sequence
    3: Cleanup - restore normal state

  Evolution Types:
    Thunder Stone, Moon Stone, Fire Stone, Leaf Stone,
    Water Stone, Link Cable, Experience

  Trinket System:
    - Trinkets appear at fixed positions on the field
    - Ball passes through trinket to collect (point-based collision)
    - Collecting 3 triggers the evolution check
    - Recovery triggers (IDs 1/2) restore 10K points and indicators
    - Blue Field uses OBJ palettes 6/7 for trinket graphics
    - Stage transition palette loading can overwrite these;
      workaround: re-apply trinket palette when active

  Blue Field Specific Behavior:
    - Indicator backup differs from Red Field:
      Red Field: wd558←[2], wd559←[3]
      Blue Field: wd558←[0], wd559←[3], indicator_state_2_backup←[2]
    - After collection/recovery: restore stage palette to OBJ pal 6

  NOTE: Template only. C fallback handles the actual logic.
]]

--============================================================
-- EVOLUTION MODE HOOK
-- Not currently active.
--============================================================
--[[
function on_evolution_update(evo_state)
    -- Called each frame during evolution mode
end
]]
