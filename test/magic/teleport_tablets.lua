-- teleport_tablets: breaking every tablet the 2026-10-29 recordings break
-- (index 4404..4430), for the tick log. Every break is the same (4394, 4424,
-- 4426, 4427, ...): on the click poh_smash_magic_tablet (4069) delay 16 and
-- poh_tablet_break_teleport delay 15; two ticks on poh_absorb_tablet_magic
-- and poh_absorb_tablet_teleport (4071) with the tablet gone; two more, the
-- teleport, onto the 3x3 around the tablet's tile (teleport_tablet.rs2's
-- header has the landings behind each tile). The Lumberyard scroll reads
-- (teleport_scroll.rs2): three ticks to 3302,3486 (4401) or 3303,3486 (4420).
local TABLETS = {
    { obj = "poh_tablet_varrockteleport", x = 3213, z = 3424 },
    { obj = "poh_tablet_ardougneteleport", x = 2661, z = 3302 },
    { obj = "poh_tablet_faladorteleport", x = 2965, z = 3378 },
    { obj = "poh_tablet_lumbridgeteleport", x = 3221, z = 3218 },
    { obj = "poh_tablet_camelotteleport", x = 2757, z = 3478 },
    { obj = "poh_tablet_kourendteleport", x = 1641, z = 3673 },
    { obj = "lunar_tablet_catherby_teleport", x = 2801, z = 3449 },
    { obj = "teletab_battlefront", x = 1348, z = 3739 },
    { obj = "teleportscroll_lumberyard", x = 3302, z = 3486 },
}

return {
    id = "teleport_tablets",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give poh_tablet_varrockteleport 1", "::give poh_tablet_ardougneteleport 1",
        "::give poh_tablet_faladorteleport 1", "::give poh_tablet_lumbridgeteleport 1",
        "::give poh_tablet_camelotteleport 1", "::give poh_tablet_kourendteleport 1",
        "::give lunar_tablet_catherby_teleport 1", "::give teletab_battlefront 1",
        "::give teleportscroll_lumberyard 2",
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
            local reach = 1
            local landed = r == "ok" and type(v) == "table" and v.level == 0
                and math.abs(v.x - tab.x) <= reach and math.abs(v.z - tab.z) <= reach
            t.check(tab.obj .. ".landed", landed, "want within " .. reach .. " of " .. tab.x .. "," .. tab.z
                .. ", read " .. tostring(r) .. " " .. ((type(v) == "table" and v.x) and (v.x .. "," .. v.z .. "," .. v.level) or "?"))
            local after_r, after = t.inv.count(tab.obj)
            t.check(tab.obj .. ".used", before_r == "ok" and after_r == "ok" and after == before - 1,
                tab.obj .. " " .. tostring(before) .. " -> " .. tostring(after))
        end
        t.finish(0)
    end,
}
