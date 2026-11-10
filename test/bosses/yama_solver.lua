-- yama_solver: Yama, solo, a real kill with no cheats in the fight (docs/minigames/yama/solver.md),
-- played by its solver (script/plugins/quest_driver/yama_solver.lua, QD.raid.yama_solve) in
-- the solver's kit (the setup cheats build it, so the server must be staff: --dev).
-- test/bosses/yama_duo.lua is the same with two seats.
--
-- GREEN: Yama killed, the player alive, the drop on the loot tile.
--
--   python3 tools/quest_gate/run.py --script test/bosses/yama_solver.lua --name ys1 --no-publish
return QD_ROOT.raid.yama_test({
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: yama_test's own fields are not seen
    fixture = "yama_kit.ini",
    party = 1,
    max_frames = 400000,
})
