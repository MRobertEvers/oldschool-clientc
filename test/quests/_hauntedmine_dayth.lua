-- Haunted Mine: Treus Dayth, the way RSProx index 4647 recorded him (both fights).
--   1. stage 7, the key: "What the... ?", the voice's mesbox, its continue is his rise (stage 8);
--      a stretch of the fight (summons, fades, glides, carts and cranes); then the player
--      teleports out mid-fight ([6053]): he is gone, the stage stays 8.
--   2. back at the key at stage 8 ([6713]): the same pages and a new rise; the fight to the kill
--      ([6944]): stage 9 and "As you make the killing strike, the machinery within the room falls
--      silent." on the killing tick.
-- hauntedmine_dayth_fight.rs2 has the recorded ticks; docs/rsprox-analysis/quests/
-- haunted_mine_dayth.md the tables.
local DAYTH = "hauntedmine_boss_ghost"
local KEY = "hauntedmine_boss_key"
local VOICE = "mesbox:You hear a disembodied voice laughing at you"

return {
    id = "hauntedmine_dayth",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::setvar varp302_priestperil 60",
        "::setvar varp382_hauntedmine 7",
        "::setlevel ranged 70",
        "::setlevel defence 70",
        "::setlevel hitpoints 99",
        "::setlevel prayer 70",
        "::clearinv",
        "::give magic_shortbow",
        "::give rune_arrow 600",
        "::give shark 20",
        "::tele 0_43_69_36_38",
    },

    run = function(t)
        t.ticklog.start()
        local function stage(name, want)
            local _, v = t.var.server("varp382_hauntedmine")
            t.check(name, v == want, "varp382_hauntedmine " .. tostring(v) .. " want " .. tostring(want))
        end
        -- the server's reading of the caller's Dayth (::hmq_dayth_state): nil when none, else
        -- { form = 1 Treus Dayth | 2 the ghost, hp, phase, x, z }.
        local function dayth()
            t.cheat("::hmq_dayth_state")
            t.ticks(2)
            local msgs = t.raid.cox_messages(6)
            for i = 1, #msgs do
                local f, hp, ph, x, z = string.match(msgs[i], "hmq_dayth_state: (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+)")
                if f then
                    if tonumber(f) == 0 then return nil end
                    return { form = tonumber(f), hp = tonumber(hp), phase = tonumber(ph), x = tonumber(x), z = tonumber(z) }
                end
            end
            return nil
        end
        local function topup() t.cheat("::setlevel hitpoints 99") end
        local function take_key(name)
            t.exec(name .. ".key", t.player.talk_to, KEY, 1)
            t.exec(name .. ".pages", t.chat.play, { "player:What the... ?", VOICE })
        end
        -- Fight for `ticks`, re-pressing Attack whenever he is seen again (he fades on hits).
        local function fight(name, ticks)
            local spent = 0
            local seen, faded = 0, 0
            while spent < ticks do
                local d = dayth()
                spent = spent + 2
                if d == nil then break end
                if d.form == 1 then
                    seen = seen + 1
                    t.player.attack(DAYTH, 2, 6)
                    spent = spent + 6
                else
                    faded = faded + 1
                    t.ticks(3)
                    spent = spent + 3
                end
                if spent % 30 < 9 then topup() end
            end
            t.note(name .. ": seen " .. seen .. ", faded " .. faded)
            return seen, faded
        end

        t.exec("wield-bow", t.player.inv_op, "magic_shortbow", 2)
        t.exec("wield-arrows", t.player.inv_op, "rune_arrow", 2)
        t.ticks(2)

        -- 1. the first rise, a stretch of the fight, the teleport out
        t.ticklog.mark("leg.rise1")
        take_key("rise1")
        stage("rise1.stage", 8)
        t.ticks(14)
        local d = dayth()
        t.check("rise1.up", d ~= nil and d.form == 1, "Treus Dayth seen after the rise: " .. tostring(d and d.form))
        t.ticklog.mark("leg.fight1")
        local seen1, faded1 = fight("fight1", 160)
        t.check("fight1.fades", faded1 > 0, "he faded at least once")
        topup()
        t.ticklog.mark("leg.leave")
        t.cheat("::tele lumbridge")
        t.ticks(20)
        t.check("leave.gone", dayth() == nil, "gone once the player left")
        stage("leave.stage", 8)

        -- 2. back: the key at stage 8, a new rise, the kill
        t.ticklog.mark("leg.rise2")
        t.cheat("::tele 0_43_69_36_38")
        t.ticks(4)
        take_key("rise2")
        stage("rise2.stage", 8)
        t.ticks(14)
        d = dayth()
        t.check("rise2.up", d ~= nil and d.form == 1, "a new Treus Dayth: " .. tostring(d and d.form))
        t.ticklog.mark("leg.fight2")
        fight("fight2", 120)
        t.ticklog.mark("leg.kill")
        t.cheat("::hmq_dayth_hp 4")
        local dead = false
        for i = 1, 30 do
            d = dayth()
            if d == nil or d.phase == 5 then dead = true break end
            if d.form == 1 then t.player.attack(DAYTH, 2, 6) else t.ticks(3) end
            if i % 6 == 0 then topup() end
        end
        t.check("kill.dead", dead, "Treus Dayth killed")
        stage("kill.stage", 9)
        t.exec("kill.mesbox", t.chat.play, { "mesbox:As you make the killing strike, the machinery within the room falls silent." })
        t.ticks(8)
        t.check("kill.gone", dayth() == nil, "his [del] after the death seq")
    end,
}
