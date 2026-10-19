-- spellbook_filter: the spellbook's Filters panel (skill_magic/scripts/spellbook_filter.rs2).
-- Each row is a cc_create child of magic_spellbook:filtermenu (subid 0..6) whose op1
-- "Change" the server must arm on the spellbook's if_open and answer by flipping one
-- varbit. Reads the CLIENT's copy, so a carrier left untransmitted fails here too.
--   ./src/build_<you>_opt/torirsserver --scriptrun test/magic/spellbook_filter.lua --name s1 \
--       --ticks 200 --session $S/run --fixture test/quests/fixtures/fresh_lumbridge.ini
local ROWS = {
    { sub = 0, varbit = "varb6605_magic_filter_blockcombat" },
    { sub = 1, varbit = "varb6609_magic_filter_blockteleport" },
    { sub = 2, varbit = "varb6606_magic_filter_blockutility" },
    { sub = 3, varbit = "varb6607_magic_filter_blocklacklevel" },
    { sub = 4, varbit = "varb6608_magic_filter_blocklackrunes" },
    { sub = 5, varbit = "varb12137_magic_filter_blocklocked" },
    { sub = 6, varbit = "varb6548_magic_spellbook_blockresizing" },
}

local function press(t, row, want)
    local wr, w = t.ui.widget("magic_spellbook:filtermenu", row.sub)
    local pr, pd = t.ui.invoke(w, 1)
    t.ticks(2)
    local vr, v = t.var.varbit(row.varbit)
    t.check("row" .. row.sub .. "=" .. want, wr == "ok" and pr == "ok" and vr == "ok" and v == want,
        "filtermenu[" .. row.sub .. "] " .. tostring(wr) .. " press " .. tostring(pr) .. " "
        .. tostring(pd) .. "; " .. row.varbit .. " " .. tostring(vr) .. " " .. tostring(v)
        .. " (want " .. want .. ")")
end

return {
    id = "spellbook_filter",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    run = function(t)
        t.ui.tab("magic")
        t.ticks(2)
        for _, row in ipairs(ROWS) do
            press(t, row, 1)
        end
        for _, row in ipairs(ROWS) do
            press(t, row, 0)
        end
    end,
}
