-- tob_entry_maiden: Theatre of Blood ENTRY MODE, SOLO, the maiden room alone, played
-- by its v2 solver in the solo Entry kit: the relay
-- (script/plugins/quest_driver/tob_relay.lua, mode "entry", one seat) run for
-- this one room, landed at its door through t.raid.enter and stopped after it.
-- Plan and results: docs/minigames/theater_of_blood/ENTRY_SOLO.md.
--
-- GREEN: the room cleared, no death.
--
--   ./src/build_natt_entry_opt/torirsserver --scriptrun test/raids/tob_entry_maiden.lua --bots 1 \
--       --name e1 --ticks 6000 --session /tmp/te_maiden
return QD_ROOT.raid.tob_relay_test("entry", {
    from = "maiden",
    to = "maiden",
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    -- (the Scripts tab) by regex: tob_relay_test's own fields are not seen
    fixture = "fresh_lumbridge.ini",
    party = 1,
    max_frames = 120000,
    detail = 3000,
})
