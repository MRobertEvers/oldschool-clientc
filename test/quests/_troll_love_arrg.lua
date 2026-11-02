-- Arrg's duel (Troll Romance stage 35 -> 40) on our wire, against RSProx index 4456
-- [2071..2266] (rev 241, the only recorded fight with npc 643). Not a quest: the leading
-- underscore keeps it out of run.py --all. The fight is
-- OSRS-Content/.../quest_troll_love/scripts/trollromance_arrg_fight.rs2.
--
-- The recorder: Defence 31, a rune scimitar, elemental shield, glory and leather gloves;
-- Protect from Missiles up [2060..2099]; meleed him beside the arena's middle
-- [2072..2081], ran to the west gate (2897,3619) [2082..2089], where Arrg stuck on
-- 2901,3619 for 174 ticks without a swing, and killed him there [2095..2263].
--
-- Phases, each marked in the tick log:
--   arena     the last page's click (A): teleport, Arrg added, the attack armed for A+1
--   missiles  after 24 ticks beside him: Protect from Missiles up for 16 more
--   safespot  the walk to the west gate and 24 ticks standing there
--   kill      Attack, Strength and Defence 99, Protect from Melee, a dragon scimitar
--   dead      ten ticks for the drop and his del
--
-- Run (TORIRSSERVER_DEV=1 for the cheats; a private pack pair, see the fight's report):
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--     src/build_<x>_opt/torirsserver --scriptrun test/quests/_troll_love_arrg.lua --bots 1 \
--     --name arrg --ticks 2000 --session <dir> --fixture test/quests/fixtures/fresh_lumbridge.ini
return {
    id = "troll_love_arrg",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setvar varp385_troll_love 35",
        "::setlevel attack 45",
        "::setlevel strength 1",
        "::setlevel defence 31",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::give rune_scimitar 1",
        "::give elemental_shield 1",
        "::give amulet_of_glory_4 1",
        "::give leather_gloves 1",
        "::give dragon_scimitar 1",
        "::give shark 20",
        "::tele 1_44_157_13_49",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp385_troll_love",
            constants = { dispose_of_arrg = 35, defeated_arrg = 40 },
            row = "quest_trollromance",
            display = "Troll Romance",
            points = 2,
        })
        t.ticks(3)
        for _, item in ipairs({ "rune_scimitar", "elemental_shield", "amulet_of_glory_4", "leather_gloves" }) do
            t.exec("wear-" .. item, t.player.equip, item)
        end
        t.ticks(2)
        t.expect("stage.35", t.quest.expect_stage("dispose_of_arrg"))
        t.ticklog.start()

        local function eat_if_low()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level and hp.level < 50 then
                t.player.inv_op("shark", 1)
            end
        end
        local function hold(n)
            for _ = 1, n do
                t.ticks(1)
                eat_if_low()
            end
        end
        local function mark(label)
            local _, text = t.ticklog.mark(label)
            return tonumber(string.match(tostring(text), "at tick (%d+)"))
        end
        local function arrg_rows(kind)
            local r, rows = t.ticklog.rows({ kind = kind, type = 643 })
            if r ~= "ok" then
                return {}
            end
            return rows
        end

        -- 4456 [2048..2071]: the challenge, answered as the recorder did.
        t.exec("challenge", t.player.talk_to, "trollromance_arrg", 1)
        t.exec("challenge-dialog", t.chat.play, {
            "npc:Whaaaaaaaat?",
            "player:Ehh, Excuse me... Mr. Troll, sir,",
            "choose:Aga deserves better than you.",
            "player:Aga deserves better than you.",
            "npc:What you mean? Arrg strong troll",
            "player:But you don't love her!",
            "npc:This concept of 'love'",
            "player:Looks like I'm going to have to teach you",
            "player:But not in front of the lady",
        })
        mark("arena")
        hold(24)
        mark("missiles")
        t.prayer.set("protectfrommissiles", true)
        hold(16)
        local safe_tick = mark("safespot")
        -- Auto-retaliate off for the walk (his hits would turn the player back).
        t.cheat("::setvar varp172_option_nodef 1")
        t.player.walk_to(2897, 3619, 20)
        hold(24)
        do
            local r, tt = t.world.tile()
            t.check("safespot.tile", r == "ok" and tt.x == 2897 and tt.z == 3619,
                "the player at " .. tostring(r == "ok" and (tt.x .. "," .. tt.z) or r) .. " (want 2897,3619)")
        end
        t.prayer.set("protectfrommissiles", false)
        local kill_tick = mark("kill")
        t.cheat("::setvar varp172_option_nodef 0")
        t.cheat("::setlevel attack 99")
        t.cheat("::setlevel strength 99")
        t.cheat("::setlevel defence 99")
        t.prayer.set("protectfrommelee", true)
        t.exec("wield-dscim", t.player.equip, "dragon_scimitar")
        t.exec("kill.attack", t.player.attack, "trollromance_arrg_attackable", 2, 20)
        local r, d = t.npc.await_dead_engaged(300, 20, { eat = { item = "shark", below = 50 } })
        t.step("kill.dead", r == "ok" and "PASS" or "FAIL", tostring(r) .. " " .. tostring(d))
        mark("dead")
        t.ticks(10)
        t.expect("stage.40", t.quest.expect_stage("defeated_arrg"))

        -- The recorded shape, from the tick log.
        local spawn = arrg_rows("npc_spawn")[1]
        local swings = {}
        for _, row in ipairs(arrg_rows("npc_anim")) do
            -- troll_attack (284), troll_rock_throw_lefthand (1488)
            if row.seq == 284 or row.seq == 1488 then
                swings[#swings + 1] = row.tick
            end
        end
        local A = spawn and spawn.tick or -1
        -- 4456 [2072]: his first step and face on A+1, none on A.
        local first_step = nil
        for _, row in ipairs(arrg_rows("npc_tile")) do
            if row.tick > A then
                first_step = first_step or row
            end
        end
        t.check("arrg.first_step", first_step ~= nil and first_step.tick == A + 1 and first_step.x == 2910 and first_step.z == 3611,
            "added on tick " .. A .. "; first step " .. (first_step and (first_step.tick .. " to " .. first_step.x .. "," .. first_step.z) or "none")
                .. " (4456: A+1, 2910,3611)")
        -- 4456 [2076]: his first swing on A+5, then every 4 while the player stands beside him.
        local gaps = {}
        for i = 2, #swings do
            if safe_tick and swings[i] <= safe_tick + 2 then
                gaps[#gaps + 1] = swings[i] - swings[i - 1]
            end
        end
        local clock_ok = #gaps >= 5
        for _, gap in ipairs(gaps) do
            clock_ok = clock_ok and gap == 4
        end
        t.check("arrg.first_swing", swings[1] == A + 5, "first swing on tick " .. tostring(swings[1]) .. ", added " .. A .. " (4456: A+5)")
        t.check("arrg.clock", clock_ok, "gaps beside the player: " .. table.concat(gaps, ",") .. " (4456: 4, the cache's attackrate)")
        -- 4456 [2090..2263]: stuck on 2901,3619 below the west gate, no swing at 4 tiles.
        local stall, moved_late, swung_late = nil, false, false
        for _, row in ipairs(arrg_rows("npc_tile")) do
            if kill_tick and row.tick < kill_tick then
                stall = row
                if row.tick > kill_tick - 18 then
                    moved_late = true
                end
            end
        end
        for _, tick in ipairs(swings) do
            if kill_tick and tick > kill_tick - 18 and tick < kill_tick then
                swung_late = true
            end
        end
        t.check("arrg.safespot", stall ~= nil and stall.x == 2901 and stall.z == 3619 and not moved_late and not swung_late,
            "last tile before the kill " .. (stall and (stall.x .. "," .. stall.z .. " on " .. stall.tick) or "none")
                .. "; moved in the last 18 ticks " .. tostring(moved_late) .. "; swung " .. tostring(swung_late)
                .. " (4456: 2901,3619, still, no swing)")
        -- 4456 [2263] H the killing splat, [2264] H+1 troll_death and "Arrg!", [2266] H+3 the drop and his del.
        local death = arrg_rows("npc_death")[1]
        local H = death and death.tick or -100
        local say = arrg_rows("npc_say")[1]
        local free = arrg_rows("npc_free")[1]
        local death_seq = nil
        for _, row in ipairs(arrg_rows("npc_anim")) do
            if row.seq == 287 then
                death_seq = death_seq or row
            end
        end
        local bones = nil
        do
            local br, objs = t.ticklog.rows({ kind = "obj_add" })
            for _, row in ipairs(br == "ok" and objs or {}) do
                if row.obj == 526 and row.tick >= H then
                    bones = bones or row
                end
            end
        end
        t.check("arrg.death", death_seq ~= nil and death_seq.tick == H + 1 and say ~= nil and say.tick == H + 1
                and say.text == "Arrg!" and free ~= nil and free.tick == H + 3 and bones ~= nil and bones.tick == H + 3,
            "blow " .. H .. "; troll_death " .. tostring(death_seq and death_seq.tick) .. "; say "
                .. tostring(say and (say.tick .. " " .. tostring(say.text))) .. "; bones " .. tostring(bones and bones.tick)
                .. "; del " .. tostring(free and free.tick) .. " (4456: H+1, H+1 \"Arrg!\", H+3, H+3)")
        t.finish(0)
    end,
}
