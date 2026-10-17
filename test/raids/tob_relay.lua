-- tob_relay: the whole Theatre of Blood, Normal, a trio, lobby to lobby, every
-- room played by its solver (script/plugins/quest_driver/tob_*.lua and
-- raid_solve_verzik_p1/p2/p3.lua) in ONE Learner/Void kit
-- (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 6, 6.1). The relay itself
-- is the library's (script/plugins/quest_driver/tob_relay.lua), shared with
-- test/raids/tob_relay_hard_v2.lua.
--
-- GREEN: every room cleared with no raider dead, the party back in the lobby.
--
-- scriptrun (the fast lane):
--   ./src/build_rooms_opt/torirsserver --scriptrun test/raids/tob_relay.lua --bots 3 \
--       --name sa --session /tmp/relay --fixture tests/raids/fixtures/fresh_lumbridge.ini
-- live client:
--   python3 tools/raid_gate/run.py tob_relay --party 3 --name sa --no-publish
return QD_ROOT.raid.tob_relay_test("normal", {
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: tob_relay_test's own fields are not seen
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 300000,
})
