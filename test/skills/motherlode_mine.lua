-- Motherlode Mine against RSProx index 4658..4674
-- (docs/rsprox-analysis/skilling/motherlode_mine.md): mine pay-dirt off a
-- vein, put it in the hopper, wait out the channel, empty the sack, repair a
-- strut, search the crate for a hammer and clear a rockfall. The tick log is
-- what the wire audit diffs.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_skl_opt/torirsserver --scriptrun test/skills/motherlode_mine.lua --bots 1 \
--       --name mlm0 --ticks 2400 --session /tmp/mlm --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end

local function sackvar(t)
    local _, v = t.var.server("varb5558_motherlode_sack_transmit")
    return tonumber(v) or -1
end

return {
    id = "motherlode_mine",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv",
        -- 4665 [0]: the recorder was 44..45 Mining, 37 Smithing.
        "::setlevel mining 45",
        "::setlevel smithing 37",
        "::setlevel hitpoints 99",
        "::give rune_pickaxe 1",
        -- 4665 [9]: the recorder mined 3748,5646 from 3748,5647.
        "::goto 3748 5647 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        t.cheat("::~mlmstruts 1")
        t.ticks(1)

        -- The vein: pay-dirt until three have landed.
        t.ticklog.mark("vein")
        -- The click shows only the swing on its tick (the line comes a tick later).
        local vr, vd = t.player.click_loc("motherlode_ore_middle", 1, { at = { 3748, 5646, 0 } })
        t.note("vein.click: " .. tostring(vr) .. " " .. tostring(vd))
        for _ = 1, 120 do
            if count(t, "paydirt") >= 3 then break end
            t.ticks(1)
        end
        local dirt = count(t, "paydirt")
        t.check("vein.paydirt", dirt >= 3, "paydirt " .. tostring(dirt))
        t.check("vein.msg", t.msg.expect("You manage to mine some pay-dirt."))

        -- The hopper: everything in, the sack 12 ticks later.
        t.ticklog.mark("hopper")
        t.exec("hopper.click", t.player.click_loc, "motherlode_hopper", 1, { at = { 3748, 5672, 0 } })
        for _ = 1, 40 do
            if count(t, "paydirt") == 0 then break end
            t.ticks(1)
        end
        local inhand = count(t, "paydirt")
        t.check("hopper.emptied", inhand == 0, "paydirt left " .. tostring(inhand))
        t.ticks(14)
        local sack = sackvar(t)
        t.check("sack.arrived", sack >= dirt, "varbit 5558 = " .. tostring(sack) .. ", deposited " .. tostring(dirt))
        t.check("sack.msg", t.msg.expect("Some ore is ready to be collected from the sack."))

        -- The sack.
        t.ticklog.mark("sack")
        -- The sack and the struts are bridged locs: the selector names the map's
        -- level 1, the server answers on the plane they are walked from.
        t.exec("sack.click", t.player.click_loc, "motherlode_sack", 1, { at = { 3748, 5659, 1 } })
        t.ticks(12)
        local ore = count(t, "coal") + count(t, "gold_ore") + count(t, "motherlode_nugget")
        t.check("sack.collected", ore > 0 and sackvar(t) == 0, "ores " .. tostring(ore) .. ", varbit " .. tostring(sackvar(t)))

        -- The crate: a hammer (4666 [770]).
        t.ticklog.mark("crate")
        t.exec("crate.click", t.player.click_loc, "crate2_old", 1, { at = { 3752, 5664, 0 } })
        t.ticks(10)
        t.check("crate.hammer", count(t, "hammer") == 1, "hammers " .. tostring(count(t, "hammer")))

        -- Both struts broken, then the south one repaired.
        t.cheat("::~mlmstruts 0")
        t.ticks(2)
        t.ticklog.mark("strut")
        local since = now(t)
        t.exec("strut.click", t.player.click_loc, "motherlode_wheel_strut_broken", 1, { at = { 3742, 5663, 1 } })
        local fixed = false
        for _ = 1, 120 do
            local _, rows = t.ticklog.rows({ kind = "loc_set", where = function(r) return r.x == 3742 and r.z == 5663 and r.tick > since end })
            if rows and #rows >= 1 then fixed = true break end
            t.ticks(1)
        end
        t.check("strut.fixed", fixed, "strut 3742,5663")

        -- A rockfall (4658 [5089]).
        t.ticklog.mark("rockfall")
        t.exec("rockfall.click", t.player.click_loc, "motherlode_rockfall_1", 1, { at = { 3733, 5680, 0 } })
        t.ticks(14)
        local _, rrows = t.ticklog.rows({ kind = "loc_set", where = function(r) return r.x == 3733 and r.z == 5680 end })
        t.check("rockfall.cleared", rrows and #rrows >= 1, "loc rows " .. tostring(rrows and #rrows))
        -- ...and falls back 75..101 ticks later.
        t.ticks(110)
        _, rrows = t.ticklog.rows({ kind = "loc_set", where = function(r) return r.x == 3733 and r.z == 5680 end })
        t.check("rockfall.back", rrows and #rrows >= 2, "loc rows " .. tostring(rrows and #rrows))
        t.ticklog.mark("end")
    end,
}
