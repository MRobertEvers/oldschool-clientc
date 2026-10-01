-- test/quests/_setup.lua -- the SETUP LIST CONTRACT, proved from a real client.
--
-- Not a quest (the leading underscore keeps it out of `run.py --all`). Run:
--   python3 tools/quest_gate/run.py --script test/quests/_setup.lua --name setup_contract --no-publish
--
-- Every line of a quest file's `setup = {...}` must LAND or end the run on a
-- FAIL row named `setup.<line>` (tools/quest_gate/run.py,
-- write_wrapper_script). This file states one line of each kind a tier 2
-- boss test arms itself with and reads every one back at the top of run():
-- if any row here is red, a setup line answered and did nothing.
--
-- Seam pass 19 (setlevel_in_setup_noop): parity2a measured `::setlevel` in
-- setup "not landing" three times -- because `run.py --script` ignored the
-- setup list outright (run_script_direct ran the file unwrapped), so none of
-- the lines were ever issued; the `::setvar` it believed had landed read its
-- DEFAULT. --script now runs a non-empty setup list through the same loop a
-- quest run does, and that loop no longer believes `ok` from a ::setlevel
-- (the engine answers ok to a bad stat name or a level outside 1..99 and
-- sets nothing): it waits for the client's base_level. Proved negative with
-- `::setlevel strenght 61` and `::setlevel defence 120`
-- (build/seam_state/seam19/, s19_setup_neg / s19_setup_neg2: one FAIL row
-- each, exit 1).
--
-- Seam pass 29 (setup_wield_text_read_and_reach_honesty): `::wield` took only
-- an obj id, printed its Usage line for a name and answered ok -- arthur's
-- `::wield rune_scimitar` wielded nothing behind a green setup. The ladder now
-- takes a name like ::give and answers FAILED on every miss, and the loop
-- reads the WORN container back (build/seam_state/seam29/wield/: s29w_before
-- caught the old binary's Usage line, s29w_neglevel the content's "You need
-- to have an Attack level of 40.", s29w_negladder "::wield: no rune_scimitar
-- (1333) in the backpack.").
return {
    id = "_setup",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 60",
        "::setlevel strength 61",
        "::setlevel defence 62",
        "::setlevel hitpoints 70",
        "::give rune_scimitar 2",
        "::wield rune_scimitar",
        "::give shark 10",
        "::setvar varp226_ballquest 1",
    },
    run = function(t)
        -- A level is the stat's BASE level, stated by the server (not the
        -- fresh-account table the client holds before its first stat packet).
        local function level_is(stat, want)
            local read_result, reading = t.skill.read(stat)
            if read_result ~= "ok" then
                return read_result, stat
            end
            if reading.stated and reading.base_level == want then
                return "ok", stat .. " base_level=" .. reading.base_level
                    .. " level=" .. reading.level
            end
            return "refused", stat .. " stated=" .. tostring(reading.stated)
                .. " base_level=" .. tostring(reading.base_level) .. ", setup asked for " .. want
        end
        t.expect("setup.setlevel_attack", level_is("attack", 60))
        t.expect("setup.setlevel_strength", level_is("strength", 61))
        t.expect("setup.setlevel_defence", level_is("defence", 62))
        t.expect("setup.setlevel_hitpoints", level_is("hitpoints", 70))
        t.expect("setup.give_scimitar", t.inv.expect_has("rune_scimitar", 1))
        local worn_result, worn = t.ui._worn_count("rune_scimitar")
        t.expect("setup.wield_scimitar", (worn_result == "ok" and worn == 1) and "ok" or "refused",
            "worn rune_scimitar " .. tostring(worn_result) .. " " .. tostring(worn)
                .. " (the second of two given; one stays in the backpack)")
        t.expect("setup.give_shark", t.inv.expect_has("shark", 10))
        t.expect("setup.setvar", t.var.await_server("varp226_ballquest", 1, 1))
        t.finish(0)
    end,
}
