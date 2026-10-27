-- equipment_side: equipping from the equipment-stats screen keeps it open.
--
--   "View equipment stats" mounts `equipment` (84) and its own backpack,
--   `equipment_side` (85), in sidemodal (index 1102 [5689], index 4289 [725]).
--   Equip on a cell there wields the item and the screen stays up, repainted;
--   Jagex's stats screen closes only when the player closes it.
--
--   ./src/build_<x>_opt/torirsserver --scriptrun test/gear/equipment_side.lua --bots 1 \
--       --name es0 --ticks 300 --session /tmp/es --fixture test/raids/fixtures/fresh_lumbridge.ini
return {
    id = "equipment_side",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::goto 3222 3218 0",
    },
    run = function(t)
        t.ticks(2)
        t.cheat("::give bronze_dagger 1", false)
        t.cheat("::give wooden_shield 1", false)
        t.exec("give", t.inv.await_all, { bronze_dagger = 1, wooden_shield = 1 }, 5)

        t.check("worn.tab", t.ui.tab("equipment") == "ok", "t.ui.tab(equipment)")
        t.ticks(1)
        local sr, stats = t.ui.widget("wornitems:equipment")
        t.check("stats.widget", sr == "ok", "wornitems:equipment " .. tostring(sr))
        t.ui.invoke(stats, 1)
        t.exec("stats.open", t.ui.await_open, "equipment", 5)
        t.exec("side.open", t.ui.await_open, "equipment_side", 5)

        local function equip_from_side(name)
            local slot
            for i = 0, 27 do
                local r, row = t.inv.slot(i)
                if r == "ok" and type(row) == "table" and row.name == name then slot = i end
            end
            local wr, cell = t.ui.widget("equipment_side:items", slot)
            t.check(name .. ".cell", wr == "ok", "equipment_side:items " .. tostring(slot) .. " " .. tostring(wr))
            t.ui.invoke(cell, 1)
            t.ticks(2)
            local _, worn = t.inv.count_in("worn", name)
            t.check(name .. ".worn", worn == 1, name .. " worn " .. tostring(worn))
            t.check(name .. ".still_open", t.ui.await_open("equipment", 1) == "ok", "the stats screen closed")
        end
        equip_from_side("bronze_dagger")
        equip_from_side("wooden_shield")
        t.exec("stats.text", t.ui.expect_text, "equipment:stabatt", "Stab: +4", 2)
    end,
}
