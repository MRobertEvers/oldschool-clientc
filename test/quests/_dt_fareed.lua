-- Desert Treasure I: Fareed, fought the way RSProx index 4623 [1956..2120] recorded it: through
-- the unlocked gate, Protect from Melee, darts (disarmed when he comes beside the thrower), a wind
-- blast (lands as 0), then water blasts with no weapon to the kill; the diamond goes straight
-- into the pack at death + 2. dt_fareed.rs2 has the recorded ticks;
-- docs/rsprox-analysis/quests/desert_treasure_i_fareed.md the tables.
return {
    id = "dt_fareed",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::setvar varb358_deserttreasure 11",
        "::setvar varb386_fd_firewargate 1",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel magic 75",
        "::setlevel ranged 70",
        "::setlevel defence 60",
        "::clearinv",
        "::give adamant_dart 300",
        "::give waterrune 300",
        "::give airrune 400",
        "::give deathrune 120",
        "::give shark 10",
        "::give gasmask 1",
        "::tele 0_51_146_39_32",
    },

    run = function(t)
        t.ticklog.start()
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function var(name) local _, v = t.var.server(name) return v end
        local function said(text, n)
            local r, lines = t.msg.last(n or 8)
            if r ~= "ok" or type(lines) ~= "table" then return false end
            for _, l in ipairs(lines) do
                if tostring(l.text):find(text, 1, true) ~= nil then return true end
            end
            return false
        end
        t.ticks(2)
        t.exec("wear-mask", t.player.inv_op, "gasmask", 2)
        t.ticks(1)
        -- Protect from Melee before the gate ([1961] in the recording, 2 ticks before his first swing)
        t.ui.tab("prayer")
        t.ticks(2)
        local _, pw = t.ui.widget("prayerbook:prayer15")
        t.ui.invoke(pw, 1)
        t.ticks(2)

        -- the gate ([1955]) and his spawn at gate + 1 ([1956])
        t.ticklog.mark("leg.gate")
        t.exec("gate", t.player.click_loc, "fd_fw_metalgateclosed_r", 1)
        t.ticks(3)
        local _, w = t.world.tile()
        t.check("gate.crossed", type(w) == "table" and w.x >= 3305, "player at x " .. tostring(w and w.x))

        -- darts: the throw at range is not disarmed, the tick he stands beside the thrower is
        t.ticklog.mark("leg.darts")
        t.exec("darts.wield", t.player.inv_op, "adamant_dart", 2)
        t.ticks(1)
        local darts0 = count("adamant_dart")
        -- not a row: the attack verb waits for a hit, and the recorded answer beside him is no throw
        t.player.attack("firediamond_firewarrior", 2, 20)
        t.ticks(8)
        t.check("disarm.message", said("The heat from the warrior causes you to drop your weapon."),
            "the disarm line in chat")
        t.check("disarm.backpack", count("adamant_dart") > 0, "darts in the backpack after the disarm: "
            .. count("adamant_dart") .. " (held in the pack before wielding " .. darts0 .. ")")

        -- a wind blast lands as 0 ([2016]..[2018])
        t.ticklog.mark("leg.wind")
        t.exec("wind.cast", t.player.cast, "wind_blast", "firediamond_firewarrior", 14)
        t.ticks(4)

        -- water blasts with no weapon to the kill ([2053]..[2117])
        t.ticklog.mark("leg.water")
        t.exec("water.cast", t.player.cast, "water_blast", "firediamond_firewarrior", 14)
        t.exec("kill", t.npc.await_dead_engaged, 500, 80, { eat = { item = "shark", below = 50 } })
        t.ticks(6)
        t.ticklog.mark("leg.after")
        t.check("kill.varbit", var("varb376_fd_killed_firewarrior") == 1,
            "fd_killed_firewarrior " .. tostring(var("varb376_fd_killed_firewarrior")))
        t.check("kill.diamond", count("fd_diamond_fire") == 1, "smoke diamonds held: " .. count("fd_diamond_fire"))
        t.check("kill.message", said("You take the Diamond of Smoke from the ashes of the warrior.", 12),
            "the diamond line in chat")
        t.ticks(4)
    end,
}
