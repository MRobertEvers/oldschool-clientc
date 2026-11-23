-- Desert Treasure I: the Stranger (dt_stranger.rs2), at the Grand Exchange where RSProx index 4628
-- [2962..3170] recorded him: the recorder on 3165,3487 beside banker1_east (3166,3489).
--   busy:   the owner's bank is open on his first tick, so he never goes for the owner (4628 [2963]):
--           with the bank shut again and the owner standing by him for 20 ticks he does not swing
--           ([2979..2995], 16 such ticks recorded).
--   fight:  the owner attacks, he fights back (every 4 ticks), dies; fd_killed_ninja and the page
--           "I wonder what that was all about?" ([3170]).
--   rules:  an attempt after the kill brings nobody; with the kill undone, an attempt with no
--           diamond in the pack, one upstairs and one underground bring nobody; one on the ground
--           with the Smoke diamond brings him.
--   free:   an owner with no modal up on his first tick is attacked.
-- ::dt_stranger spawns him now; ::dt_stranger_attempt runs one attempt next tick without the wait
-- or the roll (every other rule kept).
local GE = "0_49_54_29_31"           -- 3165,3487, the recorder's tile at [2962]

return {
    id = "dt_stranger",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::setvar varb358_deserttreasure 11",
        "::setvar varb375_fd_killed_ninja 0",
        "::setlevel hitpoints 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel prayer 99",
        "::clearinv",
        "::give dragon_scimitar 1",
        "::give fd_icediamond 1",
        "::give shark 10",
        "::tele " .. GE,
    },

    run = function(t)
        t.ticklog.start()
        local SYM = "fourdiamonds_assasin"
        local function var(name) local _, v = t.var.server(name) return v end
        local function stranger(radius)
            local r, row = t.npc.nearest(SYM, radius or 6)
            if r == "ok" and type(row) == "table" and row.x then return row end
            return nil
        end
        local function hp()
            local _, s = t.skill.read("hitpoints")
            return type(s) == "table" and s.level or nil
        end
        local function swung(ticks)
            local r = t.npc.await_anim(SYM, nil, ticks)
            return r == "ok"
        end
        local function protect_melee()
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            if now ~= 1 then
                t.ui.tab("prayer")
                t.ticks(1)
                local _, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(1)
            end
        end
        t.ticks(2)
        t.player.inv_op("dragon_scimitar", 2)
        t.ticks(2)

        -- busy: the bank open when he is added and on his first tick
        t.ticklog.mark("leg.busy")
        local br, bd = t.player.talk_to("banker1_east", 3)
        t.ticks(2)
        t.cheat("::dt_stranger ice")
        t.ticks(2)
        local s = stranger()
        t.check("busy.spawn", s ~= nil, "Stranger " .. (s and (s.x .. "," .. s.z) or "absent") .. " (bank: " .. tostring(br) .. " " .. tostring(bd) .. ")")
        t.ui.close_modal()
        t.ticks(1)
        local before = hp()
        local hit = swung(20)
        t.check("busy.no_attack", not hit and hp() == before,
            "no swing in 20 ticks beside an owner who was in the bank on his first tick (hp " .. tostring(before) .. " -> " .. tostring(hp()) .. ")")

        -- fight: the owner attacks him; he fights back and dies. His max is 115% of ours (~31 with
        -- 99 Strength and a dragon scimitar, twice on a special): the recorder prayed Protect from
        -- Melee from [3129] and took 0 on all 13 swings after, so the test prays too.
        t.ticklog.mark("leg.fight")
        t.player.attack(SYM, 2, 20)
        t.check("fight.swings", swung(16), "he swings back once attacked (4628 [3053] -> [3058])")
        for _ = 1, 60 do
            if var("varb375_fd_killed_ninja") == 1 or stranger(10) == nil then break end
            protect_melee()
            local h = hp()
            if h ~= nil and h < 65 then
                t.player.inv_op("shark", 1)
                t.ticks(2)
            end
            t.player.attack(SYM, 2, 10)
            t.ticks(4)
        end
        t.ticks(4)
        t.check("fight.killed", var("varb375_fd_killed_ninja") == 1, "fd_killed_ninja " .. tostring(var("varb375_fd_killed_ninja")))
        local _, raw = t.chat.text()
        t.check("fight.page", tostring(raw):find("I wonder what that was all about?", 1, true) ~= nil, "page: " .. tostring(raw))
        t.chat.drain()
        t.ticks(2)

        -- rules
        t.ticklog.mark("leg.rules")
        t.cheat("::dt_stranger_attempt ice")
        t.ticks(4)
        t.check("rules.killed", stranger(10) == nil, "no Stranger after the kill")
        t.cheat("::setvar varb375_fd_killed_ninja 0")
        t.cheat("::clearinv")
        t.cheat("::dt_stranger_attempt ice")
        t.ticks(4)
        t.check("rules.no_diamond", stranger(10) == nil, "no Stranger with no diamond in the pack")
        t.cheat("::give fd_diamond_fire 1")
        t.cheat("::tele 1_49_54_29_31")
        t.ticks(2)
        t.cheat("::dt_stranger_attempt smoke")
        t.ticks(4)
        t.check("rules.upstairs", stranger(10) == nil, "no Stranger on level 1")
        t.cheat("::tele 0_49_154_29_31")
        t.ticks(2)
        t.cheat("::dt_stranger_attempt smoke")
        t.ticks(4)
        t.check("rules.underground", stranger(10) == nil, "no Stranger underground")
        t.cheat("::tele " .. GE)
        t.ticks(3)
        t.cheat("::dt_stranger_attempt smoke")
        t.ticks(4)
        s = stranger()
        t.check("rules.spawn", s ~= nil, "a Stranger for the Smoke diamond on the ground")

        -- free: an owner with nothing open on his first tick is attacked
        t.ticklog.mark("leg.free")
        t.check("free.swings", swung(16), "he goes for a free owner (the wiki: aggressive)")
        t.ticks(2)
    end,
}
