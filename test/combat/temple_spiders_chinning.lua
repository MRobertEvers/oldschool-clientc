-- temple_spiders_chinning: chinchompas at the Forthos Dungeon's temple spiders,
-- as the owner chinned them in RSProx index 4517..4528 (rev 242).
-- Content: areas/area_forthos (the Temple Door, the spider's sounds),
-- drop_tables/scripts/temple_spider.rs2, the chinchompa throw in
-- skill_combat/scripts/player/player_ranged.rs2, maps/multiway.csv (the den).
-- Rules and counts: docs/rsprox-analysis/temple_spiders.md.
--
-- Walks through the Temple Door from the west, then throws red chinchompas into
-- the den until a few spiders have died and one has come back. The tick log is
-- the audit's "ours" (ticklog_tells.py, temple_spiders.md "Wire audit").
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--     ./src/build_s08_opt/torirsserver --scriptrun test/combat/temple_spiders_chinning.lua \
--     --bots 1 --name ts0 --ticks 900 --session <dir> --fixture test/fixtures/fresh_lumbridge.ini

local DOOR = { 1825, 9973, 0 }

local function server_var(t, name)
    local r, v = t.var.server(name)
    if r ~= "ok" then
        return nil
    end
    return v
end

local function run(t)
    t.ticks(3)
    t.check("ts.ticklog", t.ticklog.start() == "ok", "")

    -- The door, from the west: onto its tile, "slams shut" a tick later.
    t.ticklog.mark("leg.door")
    t.player.click_loc("hosdun_east_door", 1, { at = DOOR })
    local mr, md = t.msg.await("The door slams shut and locks behind you.", 12)
    t.check("ts.door_line", mr == "ok", tostring(md))
    local r, here = t.world.tile()
    t.check("ts.door_tile", r == "ok" and here.x == 1825 and here.z == 9973,
        "at " .. tostring(here and here.x) .. "," .. tostring(here and here.z))

    -- The den is multi: every blast splashes the spiders round its target.
    t.ticks(2)
    t.check("ts.multi", server_var(t, "varbit4605_multiway_indicator") == 1
        or server_var(t, "varb4605_multiway_indicator") == 1, "multiway indicator")

    -- Chinning: keep throwing until several spiders have died.
    t.ticklog.mark("leg.chin")
    -- The recorder chinned under Protect from Melee (2,917 of 2,923 swings).
    t.prayer.set("protectfrommelee", true)
    t.player.walk_to(1836, 9960, 30)
    local attacked = 0
    for _ = 1, 30 do
        local ar = t.player.attack("hosdun_spider", 2, 12)
        if ar == "ok" then
            attacked = attacked + 1
        end
        t.ticks(12)
    end
    t.check("ts.attacks", attacked >= 15, "attacks started " .. tostring(attacked))
    t.ticklog.mark("leg.end")
    t.ticks(2)
end

return {
    id = "temple_spiders_chinning",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::setlevel ranged 75",
        "::setlevel hitpoints 80",
        "::setlevel defence 70",
        "::setlevel prayer 99",
        "::give chinchompa_big_captured 2000",
        "::wield chinchompa_big_captured",
        "::goto 1821 9973 0",
    },
    run = run,
}
