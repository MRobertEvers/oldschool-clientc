-- tob_entry_solo: QD.raid.tob_entry_solo as the quest test of A Night at the
-- Theatre calls it (script/plugins/quest_driver/tob_relay.lua): the solo Entry
-- kit as the setup (QD.raid.tob_entry_kit()), the player put inside an Entry
-- raid at the Maiden's walkway first (here t.raid.enter; the quest gets there
-- through the lobby door), then the one call, lobby to lobby.
-- docs/minigames/theater_of_blood/ENTRY_SOLO.md.
--
-- GREEN: the call answers ok (every room cleared, no death, back in the lobby).
--
--   ./src/build_natt_entry_opt/torirsserver --scriptrun test/raids/tob_entry_solo.lua --bots 1 \
--       --name e1 --ticks 16000 --session /tmp/te_solo
return {
    id = "tob_entry_solo",
    -- read by tools/quest_gate/run.py and tools/raid_gate/prepare_scripts.py
    fixture = "fresh_lumbridge.ini",
    party = 1,
    max_frames = 480000,
    setup = QD_ROOT.raid.tob_entry_kit(),
    run = function(t)
        t.check("ticklog", t.ticklog.start() == "ok", "")
        local r, d = t.raid.enter("tob", "maiden", { mode = "entry" })
        t.expect("enter", r, tostring(d))
        local ok, detail = t.raid.tob_entry_solo(t)
        t.check("tob_entry_solo", ok, tostring(detail))
        t.finish(ok and 0 or 1)
    end,
}
