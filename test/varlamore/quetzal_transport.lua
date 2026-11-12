-- quetzal_transport: Renu's map from Civitas illa Fortis to Tal Teklan, the
-- roost's Renu waiting there, and an unlimited whistle's Signal to Aldarin.
-- Content: OSRS-Content/osrs-content/server/scripts/skill_hunter/scripts/
-- quetzal_transport.rs2 and quetzal_whistle.rs2; recordings and rules:
-- docs/rsprox-analysis/quetzal_transport.md.
--
-- Recorded (121/121 flights land on the quetzal dbrow's tile exactly):
--   Fortis -> Tal Teklan (sub 11): 1226,3091, the teleport 4 ticks after the
--     click (index 4530 [287] -> [291]; 4515, 4544, 4545, 4547, 4508 the same)
--   Signal -> the whistle's own map (949) -> W+8 the teleport (1131, 1139,
--     3980, 4031); Aldarin (sub 8) lands 1389,2901 (4479, 4480, 4536)
--
--   TORIRSSERVER_SCRIPTS=<pack dir> TORIRSSERVER_PACK_DIR=<servpack> \
--     ./src/build_<x>_opt/torirsserver --scriptrun test/varlamore/quetzal_transport.lua \
--     --bots 1 --name qz0 --ticks 400 --session <dir> --fixture test/fixtures/fresh_lumbridge.ini

local function tile_is(t, x, z)
    local r, here = t.world.tile()
    if r ~= "ok" or type(here) ~= "table" then
        return false, "no tile"
    end
    return here.x == x and here.z == z, here.x .. "," .. here.z .. "," .. tostring(here.level)
end

local function run(t)
    t.ticks(3)
    t.check("qz.ticklog", t.ticklog.start() == "ok", "")

    -- Renu at Fortis, walked to from the west as every recording does.
    t.ticklog.mark("leg.fortis")
    t.exec("goto-fortis", t.player.goto_tile, 1690, 3138, 0)
    t.exec("travel", t.player.talk_to, "quetzal_fortis", 1)
    local open = t.ui.await_open("quetzal_menu", 10)
    t.check("qz.map_open", open == "ok", tostring(open))
    local wr, wid = t.ui.widget("quetzal_menu:icons", 11)
    t.check("qz.icon", wr == "ok" and wid ~= nil, "Tal Teklan icon (sub 11 = id 12): " .. tostring(wr))
    t.ticklog.mark("leg.click")
    t.ui.invoke(wid, 1)
    t.ticks(3)
    local early, where3 = tile_is(t, 1226, 3091)
    t.check("qz.not_yet", not early, "still at Fortis 3 ticks after the click: " .. where3)
    t.ticks(3)
    local landed, where = tile_is(t, 1226, 3091)
    t.check("qz.landed_talteklan", landed, "landed at " .. where)
    local _, last = t.var.server("varb9950_quetzal_last_destination")
    t.check("qz.last_destination", last == 12, "last destination " .. tostring(last))

    -- The roost's own Renu, which nothing spawned before.
    t.check("qz.renu_talteklan", t.npc.await_present("quetzal_talteklan", 8, 4) == "ok", "Renu at Tal Teklan")

    -- The whistle: Signal opens 949, a destination blows it.
    t.ticklog.mark("leg.whistle")
    t.ticks(4)
    t.player.inv_op("hg_quetzalwhistle_perfected_infinite", 1)
    local wopen = t.ui.await_open("quetzalwhistle_menu", 6)
    t.check("qz.whistle_map_open", wopen == "ok", tostring(wopen))
    local ar, aid = t.ui.widget("quetzalwhistle_menu:icons", 8)
    t.check("qz.whistle_icon", ar == "ok" and aid ~= nil, "Aldarin icon (sub 8 = id 9): " .. tostring(ar))
    t.ui.invoke(aid, 1)
    t.ticks(6)
    local early2, w6 = tile_is(t, 1389, 2901)
    t.check("qz.whistle_not_yet", not early2, "not landed 6 ticks after the whistle: " .. w6)
    t.ticks(5)
    local landed2, where2 = tile_is(t, 1389, 2901)
    t.check("qz.landed_aldarin", landed2, "landed at " .. where2)
    t.ticks(2)
end

return {
    id = "quetzal_transport",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        "::setvar varb9649_vmq2 50",
        "::setvar varb9951_quetzal_fortis 2",
        "::setvar varb17755_quetzal_talteklan 1",
        "::setvar varb11377_quetzal_aldarin 1",
        "::give hg_quetzalwhistle_perfected_infinite 1",
        "::goto 1686 3136 0",
    },
    run = run,
}
