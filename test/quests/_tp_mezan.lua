-- Twilight's Promise: Mezan, the Knight of Varlamore, his training bout alone
-- (twilightspromise_colosseum.rs2) against RSProx index 4444 [1396..1646] (rev 241)
-- and 1679 (rsprox.net 1695, rev 226): docs/rsprox-analysis/quests/
-- twilights_promise.md, "Mezan (Colosseum)". The quest test walks the same bout
-- inside its knights leg; this is the bout with the tick log kept for the wire audit
-- (ticklog_tells.py, rsprox_player_hits.py --ours, rsprox_hit_causes.py).
--
-- Kit as 4444's recorder where it changes his hits: Defence 31, an amulet of glory,
-- a rune scimitar, an elemental shield (slash defence 4); Hitpoints 99, not 25, so
-- the first two legs stand unprayed through his swings. The knights' stage is set
-- with the other three groups done, so his last page moves the stage 20 -> 22.
--
-- Legs: the lobby door, the first talk (with the crest) and the way in; melee,
-- unprayed, until he prays melee; Earth Strike, unprayed, until he prays magic;
-- melee under Protect from Melee until he prays melee again; then the style he is
-- not praying until he yields; the way out, his page at H+8, the stage; the
-- lobby's door back to the street.

local KNIGHT = "vmq2_knight_6_combat"
local VIS = "vmq2_knight_6_colosseum"

local function knight_var(t)
    local _, v = t.var.server("varb9832_vmq2_colosseum_knight")
    return v or -1
end

local function says_since(t, since)
    local _, rows = t.ticklog.rows({ kind = "npc_say", since = since })
    return #(rows or {})
end

-- Swing / cast until he says one more prayer line (or yields): melee presses
-- Attack once and waits, magic casts one at a time.
local function until_prayer(t, style, cap)
    local before = says_since(t, 0)
    for i = 1, cap do
        if knight_var(t) >= 2 then
            return "yielded"
        end
        if says_since(t, 0) > before then
            return "ok"
        end
        if style == "melee" then
            if i == 1 then
                t.player.attack(KNIGHT, 2, 4)
            else
                t.ticks(4)
            end
        else
            t.player.cast("earth_strike", KNIGHT, 5)
        end
        local _, snap = t.skill.snapshot()
        local hp = snap and snap.hitpoints and snap.hitpoints.level
        if hp and hp < 45 then
            t.player.eat("shark")
        end
    end
    if says_since(t, 0) > before then
        return "ok"
    end
    return "cap"
end

return {
    id = "_tp_mezan",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 45",
        "::setlevel strength 36",
        "::setlevel defence 31",
        "::setlevel hitpoints 99",
        "::setlevel magic 25",
        "::setlevel prayer 43",
        "::give rune_scimitar 1",
        "::give amulet_of_glory_3 1",
        "::give elemental_shield 1",
        "::wield rune_scimitar",
        "::wield amulet_of_glory_3",
        "::wield elemental_shield",
        "::give air_rune 300",
        "::give mind_rune 300",
        "::give earth_rune 600",
        "::give shark 12",
        "::give vmq2_crest 1",
        "::setvar option_nodef 1",
        "::setvar varb9829_vmq2_bazaar_knights 3",
        "::setvar varb9830_vmq2_cothon_knight 3",
        "::setvar varb9831_vmq2_pub_knights 3",
        "::setvar varb9832_vmq2_colosseum_knight 0",
        "::setvar varb9649_vmq2 20",
        "::tele 0_28_48_3_34",

    },

    run = function(t)
        local log_result, log_detail = t.ticklog.start()
        t.check("ticklog", log_result, tostring(log_detail))
        t.ticks(3)

        -- The way in (4444 [1337..1345]): the lobby at 1799,9506, a walk from the room.
        t.ticklog.mark("leg.lobby")
        t.exec("lobby.enter", t.player.click_loc, "colosseum_entrance_outside", 1)
        t.ticks(9)
        local _, lobby = t.world.tile()
        t.check("lobby.arrived", (lobby ~= nil and lobby.x == 1799 and lobby.z == 9506) and "ok" or "hollow",
            "in the lobby at " .. tostring(lobby and (lobby.x .. "," .. lobby.z .. "," .. lobby.level)))

        -- The first talk, with the crest (4444 [1369..1380]): his var 1 goes out with
        -- the "Oh, and bring two attack styles" page.
        t.ticklog.mark("leg.talk")
        t.exec("talk", t.player.talk_to, VIS, 1)
        t.exec("talk.dialog", t.chat.play, {
            "player:Hi there.",
            "npc:Sorry, but I'm waiting",
            "player:Well, you might need",
            "npc:You don't work for the palace",
            "player:I'm working directly",
            "npc:If you are, then prove it",
            "*", -- objbox: the crest shown
            "npc:Brand new here",
            "player:Maybe, but like I say",
            "npc:And I'll return there",
            "npc:Hmm... You look like",
            "player:I can?",
            "npc:Yes! Join me",
            "npc:Oh, and bring two attack styles",
            "options",
            "choose:I'm ready. Let's do this.",
            "player:I'm ready",
            "npc:Kuani!",
        })
        t.ticklog.mark("leg.enter")
        t.ticks(8)
        local _, arena = t.world.tile()
        t.check("enter.arena", (arena ~= nil and arena.x > 6000) and "ok" or "hollow",
            "in the copy at " .. tostring(arena and (arena.x .. "," .. arena.z .. "," .. arena.level)))
        t.exec("enter.knight", t.npc.await_present, KNIGHT, 10, 15)

        t.ticklog.mark("leg.melee_unprayed")
        local r1 = until_prayer(t, "melee", 12)
        t.check("melee.prays", r1, "a melee run until his first prayer line")
        t.ticks(4)

        t.ticklog.mark("leg.magic_unprayed")
        local r2 = until_prayer(t, "magic", 14)
        t.check("magic.prays", r2, "Earth Strike until his second prayer line")

        t.ticklog.mark("leg.melee_prayed")
        t.exec("pray.melee", t.prayer.set, "protectfrommelee", true)
        local r3 = until_prayer(t, "melee", 12)
        t.check("melee2.prays", r3, "melee under Protect from Melee until his third line")
        t.ticks(4)

        t.ticklog.mark("leg.finish")
        -- He prays melee now: magic until magic is prayed, then melee, until he yields.
        local turns = 0
        while knight_var(t) < 2 and turns < 10 do
            if turns % 2 == 0 then
                until_prayer(t, "magic", 14)
            else
                until_prayer(t, "melee", 12)
                t.ticks(4)
            end
            turns = turns + 1
        end
        for _ = 1, 20 do
            if knight_var(t) >= 2 then break end
            t.ticks(1)
        end
        t.check("yield.var", knight_var(t) == 2 and "ok" or "hollow",
            "his var after the bout: " .. tostring(knight_var(t)))

        t.ticklog.mark("leg.out")
        for _ = 1, 20 do
            if t.chat.kind() ~= "none" then break end
            t.ticks(1)
        end
        local _, back = t.world.tile()
        t.check("out.room", (back ~= nil and back.x < 2000 and back.z > 9000) and "ok" or "hollow",
            "back at " .. tostring(back and (back.x .. "," .. back.z .. "," .. back.level)))
        t.exec("out.dialog", t.chat.play, {
            "npc:You fought well in there",
            "player:So you'll return",
            "npc:Yes, I'll head over",
            "player:Perfect! Thanks.",
        })
        t.ticks(2)
        local _, stage = t.var.server("varb9649_vmq2")
        t.check("out.stage", (stage == 22 and knight_var(t) == 3) and "ok" or "hollow",
            "stage " .. tostring(stage) .. ", his var " .. tostring(knight_var(t)))
        t.ticks(2)

        -- Out of the lobby (4444 [1661..1670]): the street at 1795,3106.
        t.ticklog.mark("leg.street")
        t.exec("street.exit", t.player.click_loc, "colosseum_exit_lobby", 1)
        t.ticks(9)
        local _, street = t.world.tile()
        t.check("street.arrived", (street ~= nil and street.x == 1795 and street.z == 3106) and "ok" or "hollow",
            "on the street at " .. tostring(street and (street.x .. "," .. street.z .. "," .. street.level)))
    end,
}
