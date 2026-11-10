-- yama_duo: Yama with two seats, each playing from its own client (RSProx 4498 is this duo):
-- seat 1 creates the domain and takes the west Judge, seat 2 joins through the Voice's Join
-- (each adds the other as a friend first) and takes the east one. The play is the solver's
-- (script/plugins/quest_driver/yama_solver.lua, QD.raid.yama_solve), its party handling the
-- relay's (barriers, ::synctimers behind a ready barrier). From the Scripts tab: Play runs
-- seat 1 in this client and the launch service starts seat 2.
--
-- GREEN: Yama killed, both seats alive, each seat's drop on its loot tile.
--
--   python3 tools/raid_gate/run.py yama_duo --party 2 --name yd1 --no-publish
return QD_ROOT.raid.yama_test({
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: yama_test's own fields are not seen
    fixture = "yama_kit.ini",
    party = 2,
    max_frames = 400000,
})
