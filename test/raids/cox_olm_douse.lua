-- cox_olm_douse: a fire-wall flame blocks the way and a Water Strike from a
-- rune pouch puts it out (OLM_SPEC 7; cox_olm_douse). The flame is a
-- blockwalk=all npc: a walk north stops short of it; after the cast it is
-- gone and the same walk goes through.
--
--   ./src/build_cox_opt/torirsserver --scriptrun test/raids/cox_olm_douse.lua --bots 1 \
--       --name d1 --ticks 400 --session /tmp/douse --fixture test/raids/fixtures/fresh_lumbridge.ini

local function run(t)
    local r0, at = t.world.tile()
    t.check("start", r0 == "ok", string.format("%d,%d", at.x, at.z))
    t.cheat("::coxolmflame", false)
    t.ticks(2)
    -- blocked: two tiles north is beyond the flame
    t.player.walk_to(at.x, at.z + 2, 6)
    local _, a1 = t.world.tile()
    t.check("flame.blocks", a1.z < at.z + 1 or a1.x ~= at.x, string.format("walked to %d,%d", a1.x, a1.z))
    t.cheat(string.format("::tele %d_%d_%d_%d_%d", at.level, at.x // 64, at.z // 64, at.x % 64, at.z % 64), false)
    t.ticks(2)
    -- (the solver's own call: api_drive.cast_npc, both lanes)
    local cr = QD_ROOT.COXO.cast_on_flame("water_strike")
    if cr ~= "ok" then
        t.ticks(1)
        cr = QD_ROOT.COXO.cast_on_flame("water_strike")
    end
    t.check("douse.cast", cr == "ok", tostring(cr))
    t.ticks(3)
    t.player.walk_to(at.x, at.z + 2, 6)
    local _, a2 = t.world.tile()
    t.check("douse.opens", a2.x == at.x and a2.z == at.z + 2, string.format("walked to %d,%d", a2.x, a2.z))
    t.finish(0)
end

return {
    id = "cox_olm_douse",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::maxstats", "::give bh_rune_pouch 1",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 50",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 50",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 50" },
    run = run,
}
