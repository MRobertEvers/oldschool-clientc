-- tob_sotetseg: Sotetseg, Normal trio, played by its solver
-- (script/plugins/quest_driver/tob2_sotetseg.lua) in the relay's kit: the relay
-- (script/plugins/quest_driver/tob_relay.lua) run for this one room, the party
-- landed at its door through t.raid.enter and stopped after it.
--
-- GREEN: the room cleared with no raider dead.
--
--   python3 tools/raid_gate/run.py tob_sotetseg --party 3 --name sa --no-publish
return QD_ROOT.raid.tob_relay_test("normal", {
    from = "sotetseg",
    to = "sotetseg",
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: tob_relay_test's own fields are not seen
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 90000,
    detail = 3000,
})
