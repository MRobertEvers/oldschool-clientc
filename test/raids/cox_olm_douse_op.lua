-- cox_olm_douse_op: the fire wall's own right-click "Douse" (the cache's op1
-- on olm_firewall_npc) with a watering can -- the other half of the wiki's
-- "any water ... or a filled watering can" beside cox_olm_douse.lua's spell.
-- The flame blocks the walk; Douse takes a dose from the can and the same
-- walk goes through.
--
--   python3 tools/raid_gate/run.py cox_olm_douse_op --no-publish

local function run(t)
    local r0, at = t.world.tile()
    t.check("start", r0 == "ok", string.format("%d,%d", at.x, at.z))
    t.cheat("::coxolmflame", false)
    t.ticks(2)
    t.player.walk_to(at.x, at.z + 2, 6)
    local _, a1 = t.world.tile()
    t.check("flame.blocks", a1.z < at.z + 1 or a1.x ~= at.x, string.format("walked to %d,%d", a1.x, a1.z))
    t.cheat(string.format("::tele %d_%d_%d_%d_%d", at.level, at.x // 64, at.z // 64, at.x % 64, at.z % 64), false)
    t.ticks(2)
    local pr, pd = t.player.press("olm_firewall_npc", 1, 6)
    t.ticks(2)
    t.shot("doused")
    local _, full = t.inv.count("watering_can_8")
    local _, less = t.inv.count("watering_can_7")
    t.check("douse.can", full == 0 and less == 1,
        string.format("press %s %s; cans 8=%s 7=%s", tostring(pr), tostring(pd), tostring(full), tostring(less)))
    t.player.walk_to(at.x, at.z + 2, 6)
    local _, a2 = t.world.tile()
    t.check("douse.opens", a2.x == at.x and a2.z == at.z + 2, string.format("walked to %d,%d", a2.x, a2.z))
    t.finish(0)
end

return {
    id = "cox_olm_douse_op",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::maxstats", "::give watering_can_8 1" },
    run = run,
}
