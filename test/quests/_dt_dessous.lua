-- Desert Treasure I: Dessous, both forms, the way RSProx index 4629 recorded them.
--   1. the UNBLESSED pot on the tomb ([1475]): the unkillable Dessous; taken to 0 hp twice, each
--      time he heals to full and fights on; no diamond, fdvw_subquest stays 2 (the owner's
--      requirement); then the player leaves and he goes back to his grave ([1685]..[1693]).
--   2. the BLESSED pot ([3284]): the real one; bats against Protect from Melee, claws without it;
--      the kill ([3401]..[3409]): fdvw_subquest 2 -> 3 and the player's two lines.
-- dt_dessous.rs2 has the recorded ticks; docs/rsprox-analysis/quests/desert_treasure_i_dessous.md the tables.
local UNKILLABLE = "blooddiamond_vampirewarrior_unkillable"
local REAL = "blooddiamond_vampirewarrior"

return {
    id = "dt_dessous",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::setvar varb358_deserttreasure 11",
        "::setvar varb373_fdvw_subquest 2",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::clearinv",
        "::give rune_scimitar",
        "::give fd_silver_pot_blood_garlic_spiced",
        "::give fd_silver_pot_blood_garlic_spiced_blessed",
        "::give shark 12",
        "::tele 0_55_53_50_12",
    },

    run = function(t)
        t.ticklog.start()
        local function stage(name, var, want)
            local _, v = t.var.server(var)
            t.check(name, v == want, var .. " " .. tostring(v) .. " want " .. tostring(want))
        end
        local function count(obj)
            local _, n = t.inv.count(obj)
            return n or 0
        end
        -- the server's own reading of the caller's Dessous (::dt_dessous_state): a row
        -- { hitpoints = n } when the asked-for form is up, else nil. The client sees only his bar.
        local FORM = { [UNKILLABLE] = 1, [REAL] = 2 }
        local function dessous(sym)
            t.cheat("::dt_dessous_state")
            t.ticks(2) -- the answer is a tick behind the cheat; one tick reads the previous one
            local msgs = t.raid.cox_messages(6) -- newest first
            for i = 1, #msgs do
                local f, hp = string.match(msgs[i], "dt_dessous_state: (%-?%d+) (%-?%d+)")
                if f then
                    if tonumber(f) == FORM[sym] then return { hitpoints = tonumber(hp) } end
                    return nil
                end
            end
            return nil
        end
        local function topup() t.cheat("::setlevel hitpoints 99") end
        local tomb = t.player.by_symbol("loc", "vampire_big_grave_noblood")

        t.exec("wield", t.player.inv_op, "rune_scimitar", 2)
        t.ticks(2)

        -- 1. the unblessed pot: the unkillable one
        t.ticklog.mark("leg.unkillable")
        t.exec("pour-unblessed", t.player.use_on, "fd_silver_pot_blood_garlic_spiced", tomb)
        t.ticks(9)
        t.check("unkillable.pot-back", count("fd_silver_pot") == 1 and count("fd_silver_pot_blood_garlic_spiced") == 0,
            "fd_silver_pot " .. count("fd_silver_pot"))
        local d = dessous(UNKILLABLE)
        t.check("unkillable.up", d ~= nil, "the unkillable Dessous in view")
        t.player.attack(UNKILLABLE, 2, 20)
        for round = 1, 2 do
            topup()
            t.cheat("::dt_dessous_hp 1")
            local healed, low = false, false
            for i = 1, 30 do
                if i % 8 == 0 then topup() end
                d = dessous(UNKILLABLE)
                if d and d.hitpoints ~= nil then
                    if d.hitpoints <= 1 then low = true end
                    if low and d.hitpoints >= 190 then healed = true break end
                end
            end
            t.check("unkillable.heal" .. round, healed, "to 0 and back to full: " .. tostring(d and d.hitpoints))
            t.check("unkillable.alive" .. round, dessous(UNKILLABLE) ~= nil, "still fighting after 0 hp")
            stage("unkillable.stage" .. round, "varb373_fdvw_subquest", 2)
            t.check("unkillable.no-diamond" .. round, count("fd_blood_diamond") == 0, "no diamond")
            t.player.attack(UNKILLABLE, 2, 20)
        end
        -- a stretch of his fight as Jagex sent it: bats every 10 ticks
        topup()
        t.ticks(25)
        topup()

        -- leaving: back to his grave 7 ticks on ([1685] -> [1692])
        t.ticklog.mark("leg.leave")
        t.cheat("::tele 0_50_50_32_32")
        t.ticks(12)
        t.cheat("::tele 0_55_53_50_12")
        t.ticks(4)
        t.check("unkillable.gone", dessous(UNKILLABLE) == nil, "back to his grave after the player left")

        -- 2. the blessed pot: the real one
        t.ticklog.mark("leg.real")
        t.exec("pour-blessed", t.player.use_on, "fd_silver_pot_blood_garlic_spiced_blessed", tomb)
        t.ticks(9)
        t.check("real.pot-back", count("fd_silver_pot_blessed") == 1, "fd_silver_pot_blessed " .. count("fd_silver_pot_blessed"))
        t.check("real.up", dessous(REAL) ~= nil, "the real Dessous in view")
        t.player.attack(REAL, 2, 20)
        -- Protect from Melee: bats (5 + 5, three ticks on); then off: a teleport and his claws
        t.cheat("::setvar varb4118_prayer_protectfrommelee 1")
        t.ticks(24)
        topup()
        t.cheat("::setvar varb4118_prayer_protectfrommelee 0")
        t.ticks(8)
        topup()
        t.ticklog.mark("leg.kill")
        t.cheat("::dt_dessous_hp 3")
        t.player.attack(REAL, 2, 20)
        local _, detail = t.exec("real.dead", t.npc.await_dead_engaged, 200, 40, { eat = { item = "shark", below = 40 } })
        t.note("kill: " .. tostring(detail))
        t.ticks(4)
        stage("real.stage", "varb373_fdvw_subquest", 3)
        t.exec("real.pages", t.chat.play, {
            "player:Well that's Dessous dead, but where is the Diamond he was supposed to have?",
            "player:If Malak lied to me about it, he is going to pay!",
        })
        t.ticks(10)
        -- the empty blessed pot on the tomb ([3416])
        t.exec("empty-pot", t.player.use_on, "fd_silver_pot_blessed", tomb)
        t.ticks(3)
        t.check("empty-pot.kept", count("fd_silver_pot_blessed") == 1, "the empty pot stays")
    end,
}
