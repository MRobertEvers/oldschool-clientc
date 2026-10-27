-- teleport_tablets: breaking the standard spellbook's tablets (poh_tablet_*),
-- for the tick log. RSProx index 4394 breaks three, Varrock [321] and
-- Ardougne [494] [636], each the same: on the click poh_smash_magic_tablet
-- (4069) delay 16 and poh_tablet_break_teleport delay 15; two ticks on
-- poh_absorb_tablet_magic and poh_absorb_tablet_teleport (4071) with the
-- tablet gone; two more, the teleport. Varrock lands on (3213,3424), Ardougne
-- beside (2661,3301).
local TABLETS = {
    { obj = "poh_tablet_varrockteleport", x = 3213, z = 3424 },
    { obj = "poh_tablet_ardougneteleport", x = 2661, z = 3301 },
}

return {
    id = "teleport_tablets",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", "::give poh_tablet_varrockteleport 2", "::give poh_tablet_ardougneteleport 2",
    },
    run = function(t)
        t.ticklog.start()
        t.ticks(3)
        for _, tab in ipairs(TABLETS) do
            local before_r, before = t.inv.count(tab.obj)
            t.ticklog.mark(tab.obj)
            t.exec(tab.obj .. ".break", t.player.inv_op, tab.obj, 1)
            t.ticks(6)
            local r, v = t.world.tile()
            local landed = r == "ok" and type(v) == "table" and v.level == 0
                and math.abs(v.x - tab.x) <= 2 and math.abs(v.z - tab.z) <= 2
            t.check(tab.obj .. ".landed", landed, "want within 2 of " .. tab.x .. "," .. tab.z
                .. ", read " .. tostring(r) .. " " .. ((type(v) == "table" and v.x) and (v.x .. "," .. v.z .. "," .. v.level) or "?"))
            local after_r, after = t.inv.count(tab.obj)
            t.check(tab.obj .. ".used", before_r == "ok" and after_r == "ok" and after == before - 1,
                tab.obj .. " " .. tostring(before) .. " -> " .. tostring(after))
        end
        t.finish(0)
    end,
}
