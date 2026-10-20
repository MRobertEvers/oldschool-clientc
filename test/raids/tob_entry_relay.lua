-- tob_entry_relay: the whole Theatre of Blood in ENTRY MODE, SOLO, lobby to
-- lobby: the "carry the Stranger" leg of A Night at the Theatre. Every room is
-- played by its v2 solver (script/plugins/quest_driver/tob2_<room>.lua) for one
-- seat, the relay is the library's (script/plugins/quest_driver/tob_relay.lua,
-- mode "entry"); the quest test calls the same raid through
-- QD.raid.tob_entry_solo (tob_relay.lua). Plan and results:
-- docs/minigames/theater_of_blood/ENTRY_SOLO.md.
--
-- GREEN: every room cleared with no death, back in the lobby.
--
--   ./src/build_natt_entry_opt/torirsserver --scriptrun test/raids/tob_entry_relay.lua --bots 1 \
--       --name e1 --ticks 16000 --session /tmp/te_relay
return QD_ROOT.raid.tob_relay_test("entry", {
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: tob_relay_test's own fields are not seen
    fixture = "fresh_lumbridge.ini",
    party = 1,
    max_frames = 480000,
})
