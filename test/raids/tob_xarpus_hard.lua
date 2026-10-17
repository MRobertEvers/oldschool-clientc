-- tob_xarpus_hard: Xarpus, HARD MODE, five seats, played by its v2 solver
-- (docs/minigames/theater_of_blood/TOB2_DESIGN.md) in the relay's kit: the
-- relay (script/plugins/quest_driver/tob_relay.lua) run for this one room,
-- the party landed at its door through t.raid.enter and stopped after it.
--
-- GREEN: the room cleared with no raider dead.
--
--   python3 tools/raid_gate/run.py tob_xarpus_hard --party 5 --name f1 --no-publish
return QD_ROOT.raid.tob_relay_test("hard", {
    from = "xarpus",
    to = "xarpus",
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: tob_relay_test's own fields are not seen
    fixture = "tob_normal_done.ini",
    party = 5,
    max_frames = 90000,
    detail = 3000,
    v2 = { maiden = true, bloat = true, nylocas = true, sotetseg = true, xarpus = true,
           verzik_p1 = true, verzik_p2 = true, verzik_p3 = true },
})
