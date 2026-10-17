-- tob_relay_hard: the whole Theatre of Blood in HARD MODE, five seats, lobby to
-- lobby, every room played by its solver (script/plugins/quest_driver/
-- tob2_<room>.lua) in the committed void kit with burning claws.
--
-- GREEN: every room cleared with no raider dead, the party back in the lobby.
--
-- scriptrun (the fast lane):
--   ./src/build_tob2_opt/torirsserver --scriptrun test/raids/tob_relay_hard.lua --bots 5 \
--       --name f1 --ticks 14000 --session /tmp/relay --fixture test/raids/fixtures/tob_normal_done.ini
-- live client (the same seed must produce the same tick log):
--   python3 tools/raid_gate/run.py tob_relay_hard --party 5 --name f1 --no-publish
return QD_ROOT.raid.tob_relay_test("hard", {
    fixture = "tob_normal_done.ini",
    party = 5,
    detail = 3000,
    -- read by tools/quest_gate/run.py (TORIRS_MAX_FRAMES, the live lane's frame
    -- budget): the relay's own 300000 sits in tob_relay.lua, which run.py
    -- does not read, so without this a live relay stopped at 60000 frames
    max_frames = 300000,
})
