-- tob_verzik: Verzik Vitur, Normal trio, all three phases, played by
-- tob2_verzik_p1/p2/p3.lua in the relay's Learner/Void kit: the relay
-- (script/plugins/quest_driver/tob_relay.lua) run for her room alone -- the
-- healing shared at her door, P1 to P3, the trapdoor, the reward chest and the
-- teleport out. Seat 1 is given the Dawnbringer (no Xarpus to take it from).
--
-- GREEN: Verzik dead with no raider dead, the party back in the lobby.
--
--   python3 tools/raid_gate/run.py tob_verzik --party 3 --name sa --no-publish
return QD_ROOT.raid.tob_relay_test("normal", {
    from = "verzik",
    to = "verzik",
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: tob_relay_test's own fields are not seen
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 120000,
})
