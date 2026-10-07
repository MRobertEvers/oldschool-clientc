-- What Lies Below. Guide: Quest Helper WhatLiesBelow (ladder.py whatliesbelow).
-- Scripts: OSRS-Content/osrs239-content/server/scripts/quests/quest_whatliesbelow/scripts/
-- Brought along (guide item list): bowl, 15 chaos runes, chaos talisman (the Chaos Altar entry),
-- a weapon and food for King Roald. Everything else is gathered in game.
return {
    id = "whatliesbelow",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::whatliesbelow", -- stages: varb3523 = 0, Rune Mysteries done, runecraft 99 (whatliesbelow.rs2:248; the quest only checks < 35), beside Rat
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 40",
        "::setlevel hitpoints 60",
        "::give rune_scimitar 1",
        "::give shark 10",
        "::give bowl_empty 1",
        "::give chaosrune 15",
        "::give chaos_talisman 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb3523_surok_quest",
            constants = {
                not_started = 0, collect_papers = 10, letter_to_surok = 20, wand_task = 30,
                letter_to_rat = 50, see_zaff = 60, arrest = 70, report_rat = 80, complete = 150,
            },
            display = "What Lies Below",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")

        local function tile_text(r, tt)
            if r == "ok" and type(tt) == "table" then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return tostring(r)
        end
        -- A walk on one floor, graded on the exact tile it reached.
        local function walk_check(name, x, z, level, why, ticks)
            local wr, wd = t.player.walk_to(x, z, ticks or 40)
            local tr, tt = t.world.tile()
            t.check(name, tr == "ok" and type(tt) == "table" and tt.x == x and tt.z == z and tt.level == level,
                "walk_to " .. x .. "," .. z .. " (" .. why .. ") -> " .. tostring(wr) .. " " .. tostring(wd)
                    .. "; at " .. tile_text(tr, tt) .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- Varrock Palace library (Surok Magis, King Roald's fight): from the open street south of
        -- the palace arch (3212,3466) through fai_varrock_castle_door 3215,3477 (south edge),
        -- 3214,3486 (north edge) and 3210,3490 (south edge, the library z >= 3490) -- maps/m50_54.jl2;
        -- reach.py NEEDS-DOOR at margins 30/80/160 for every goto into the library. Same doors and
        -- tiles as squire.lua's Reldo trip. Every visit presses all three, in and out, and closes
        -- each behind it (close = true): a door left open reverts after 500 ticks, and on the wlb2
        -- account that revert landed mid-walk on the way out (talkToRatToFinish.palaceDoorOut1
        -- read "stands open", then the walk stalled on the shut leaf at 3215,3477). Every far tile
        -- is one step clear of the tile the open leaf swings onto (3215,3476 / 3214,3487 /
        -- 3210,3489 / Zaff's 3204,3432): the close press from ON that tile steps the player back
        -- through the doorway.
        local CASTLE_DOOR, CASTLE_DOOR_OPEN = "fai_varrock_castle_door", "fai_varrock_castle_door_open"
        local function palace_in(prefix)
            t.exec(prefix .. ".goto-palaceStreet", t.player.goto_tile, 3212, 3466, 0)
            t.exec(prefix .. ".palaceDoorIn1", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3215, 3477, 0 }, near = { 3215, 3476 }, far = { 3215, 3478 }, close = true })
            t.exec(prefix .. ".palaceDoorIn2", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3214, 3486, 0 }, near = { 3214, 3486 }, far = { 3214, 3488 }, close = true })
            t.exec(prefix .. ".libraryDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3210, 3490, 0 }, near = { 3210, 3489 }, far = { 3210, 3491 }, close = true })
        end
        local function palace_out(prefix)
            t.exec(prefix .. ".libraryDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3210, 3490, 0 }, near = { 3210, 3491 }, far = { 3210, 3488 }, close = true })
            t.exec(prefix .. ".palaceDoorOut2", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3214, 3486, 0 }, near = { 3214, 3487 }, far = { 3214, 3486 }, close = true })
            t.exec(prefix .. ".palaceDoorOut1", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3215, 3477, 0 }, near = { 3215, 3478 }, far = { 3215, 3475 }, close = true })
            walk_check(prefix .. ".palaceStreet", 3212, 3466, 0, "the street south of the palace arch")
        end

        -- Fights: every kill wait eats a shark under 25 and its detail carries "lowest hp N/";
        -- a fight's margin row needs the lowest reading >= 15 (a quarter of the staged 60) AND
        -- sharks left.
        local function note_low(fight, low)
            if low ~= nil and (fight.low == nil or low < fight.low) then
                fight.low = low
            end
        end
        local function margin_row(name, fight, what)
            local fr, food = t.inv.count("shark")
            t.check(name, fight.low ~= nil and fight.low >= 15 and fr == "ok" and type(food) == "number" and food >= 1,
                what .. ": lowest hp " .. tostring(fight.low) .. "/60, sharks left " .. tostring(food)
                    .. " of 10 staged (" .. tostring(fr) .. ") (margin: lowest hp >= 15, a quarter of 60, AND food left)")
        end

        -- ------------------------------------------------------------ talkToRat
        t.exec("goto-talkToRat", t.player.goto_tile, 3267, 3333, 0)
        t.exec("talkToRat", t.player.talk_to, "surok_rat", 1)
        t.exec("talkToRat.dialog", t.chat.play, {
            "player:Hello there",
            "npc:Oh, hello. I'm Rat",
            "player:You're a what",
            "npc:No, no. My name is Rat",
            "player:Ohhhh",
            "npc:It's Rat, thank you",
            "player:Why, what seems",
            "npc:Well, I'm a trader",
            "choose:Shall I get them back for you?",
            "player:Shall I get them back",
            "npc:You mean you want to help",
            "choose:Yes.",
            "player:Of course! Tell me",
            "npc:Right, now I heard",
            "npc:Kill the outlaws",
            "npc:When you find all 5",
            "player:Don't worry, Ratty",
            "npc:...",
        })
        t.ticks(2)
        t.expect("quest.stage.collect_papers", t.quest.expect_stage("collect_papers"))
        t.exec("talkToRat.folder", t.inv.await, "surok_rat_emptyfolder", 1, 6)

        -- ------------------------------------------------------------ killOutlaws
        t.exec("goto-killOutlaws", t.player.goto_tile, 3118, 3472, 0)
        local outlaws = { "surok_outlaw1", "surok_outlaw2", "surok_outlaw3" }
        local outlaw_fight = {}
        for i = 1, 5 do
            local sym = outlaws[((i - 1) % 3) + 1]
            t.exec("killOutlaws-" .. i, t.player.attack, sym, 2, 20)
            local kr, kd = t.npc.await_dead_engaged(80, 8, { eat = { item = "shark", below = 25 } })
            t.step("killOutlaws-" .. i .. ".dead", kr == "ok" and "PASS" or "FAIL", tostring(kr) .. " " .. tostring(kd))
            note_low(outlaw_fight, tonumber(string.match(tostring(kd), "lowest hp (%d+)/") or ""))
            t.ticks(4)
            t.exec("killOutlaws-" .. i .. ".page", t.player.click_obj, "surok_paper", 3)
            t.exec("killOutlaws-" .. i .. ".page.held", t.inv.await, "surok_paper", 1, 8)
            t.exec("killOutlaws-" .. i .. ".folder", t.player.use_item_on_item, "surok_paper",
                (i == 1) and "surok_rat_emptyfolder" or "surok_rat_halffolder")
            t.ticks(2)
        end
        t.exec("killOutlaws.fullfolder", t.inv.await, "surok_rat_fullfolder", 1, 6)
        margin_row("killOutlaws.margin", outlaw_fight, "five outlaws (level 32)")

        -- ------------------------------------------------------------ bringFolderToRat
        t.exec("goto-bringFolderToRat", t.player.goto_tile, 3267, 3333, 0)
        t.exec("bringFolderToRat", t.player.talk_to, "surok_rat", 1)
        t.exec("bringFolderToRat.dialog", t.chat.play, {
            "npc:Hello again",
            "player:Hey, Rat! I got your pages",
            "npc:Excellent!",
            "npc:Now, I liked the way",
            "player:Wait! Wait!",
            "npc:Uhhh",
            "npc:What I want you to do",
            "npc:Take it to a wizard",
            "player:Letter. Wizard.",
            "npc:Yes, good luck",
        })
        t.ticks(2)
        t.expect("quest.stage.letter_to_surok", t.quest.expect_stage("letter_to_surok"))
        t.exec("bringFolderToRat.letter", t.inv.await, "surok_letter1", 1, 6)

        -- ------------------------------------------------------------ talkToSurok
        palace_in("goto-talkToSurok")
        t.exec("talkToSurok", t.player.talk_to, "surok_surok", 1)
        t.exec("talkToSurok.dialog", t.chat.play, {
            "player:Hello.",
            "npc:Hah! Come for my Aphro",
            "player:I didn't come here to be insulted",
            "player:No, look. I have a letter",
            "npc:Really? Well then",
            "player:Here it is",
            "npc:Of all the luck",
            "player:Why did you destroy",
            "npc:None of your business",
            "npc:However, I could let you in",
            "npc:I have uncovered",
            "npc:I would gladly share",
            "player:Okay, what do you need",
            "npc:An ordinary bowl",
            "npc:Take this metal wand",
            "npc:Bring the infused wand",
            "npc:I have also given you a copy",
        })
        t.ticks(2)
        t.expect("quest.stage.wand_task", t.quest.expect_stage("wand_task"))
        t.exec("talkToSurok.wand", t.inv.await, "surok_metalwand", 1, 6)

        -- ------------------------------------------------------------ enterChaosAltar
        -- Out of the palace door by door, overland to the open ground south of the Wilderness
        -- strip, then north on foot: wilderness_warning.rs2 zones 0_47_54_*_56 (z 3512-3519)
        -- stop the walk with three pages while %varp5753_wilderness is unset (fresh session),
        -- and the Wilderness Ditch (ditch_wilderness_cover 3060,3521 angle 0, 1x2 over z
        -- 3521-3522; wilderness_ditch.rs2 ~wilderness_ditch_cross jumps z-1 <-> z+2) is crossed
        -- by its own Cross op. North of it the ruins are open Wilderness ground (reach.py
        -- 3060,3523 -> 3060,3589 REACH closed-doors).
        palace_out("goto-enterChaosAltar")
        t.exec("goto-enterChaosAltar.ditchSouth", t.player.goto_tile, 3060, 3505, 0)
        t.player.walk_to(3060, 3520, 30)
        t.exec("enterChaosAltar.wildernessWarning", t.chat.play, {
            "mesbox:WARNING! Proceed with caution",
            "mesbox:The further north you go",
            "mesbox:In the wilderness an indicator",
        })
        walk_check("enterChaosAltar.ditchSide", 3060, 3520, 0, "the ditch's south side", 20)
        t.exec("enterChaosAltar.crossDitch", t.player.cross_trap, { loc = "ditch_wilderness_cover", op_name = "Cross",
            at = { 3060, 3521, 0 }, src = { 3060, 3520 }, dest = { 3060, 3523 }, attempts = 1 })
        t.exec("goto-enterChaosAltar", t.player.goto_tile, 3060, 3589, 0)
        local ruins = t.player.by_symbol("loc", "chaostemple_ruined")
        t.check("enterChaosAltar.ruins", ruins ~= nil, "chaostemple_ruined resolved: " .. tostring(ruins and ruins.id))
        t.exec("enterChaosAltar", t.player.use_on, "chaos_talisman", ruins)
        t.ticks(6)
        local _, altar_tile = t.world.tile()
        t.check("enterChaosAltar.inside", altar_tile ~= nil and altar_tile.x > 2200 and altar_tile.x < 2300,
            "tile " .. tostring(altar_tile and altar_tile.x) .. "," .. tostring(altar_tile and altar_tile.z) .. " level " .. tostring(altar_tile and altar_tile.level))

        -- ------------------------------------------------------------ enterChaosAltar (the maze)
        -- The ruins land you on the TOP floor of a four-level maze (runecraft.dbrow runecraft_chaos
        -- enter_coord 3_35_75_35_47 = LostCity runecraft.dbrow:113); the altar is on level 0
        -- (m35_75.jl2 `0 30 41: 34769` = LostCity m35_75.jm2 `0 30 41: 2487`). OSRS wiki Chaos Altar
        -- oldid 15350445: "players must navigate four levels of a chaotic maze to reach the altar".
        -- Plain ladders, no quest var: travel (section 2).
        t.exec("enterChaosAltar.L3-down", t.player.click_loc, "laddertop", 1, { at = { 2255, 4829, 3 } })
        t.ticks(4)
        local _, l2 = t.world.tile()
        t.check("enterChaosAltar.L2", l2 ~= nil and l2.level == 2, "tile " .. tostring(l2 and l2.x) .. "," .. tostring(l2 and l2.z) .. " level " .. tostring(l2 and l2.level))
        t.exec("enterChaosAltar.L2-down", t.player.click_loc, "laddertop", 1, { at = { 2275, 4834, 2 } })
        t.ticks(4)
        local _, l1 = t.world.tile()
        t.check("enterChaosAltar.L1", l1 ~= nil and l1.level == 1, "tile " .. tostring(l1 and l1.x) .. "," .. tostring(l1 and l1.z) .. " level " .. tostring(l1 and l1.level))
        t.exec("enterChaosAltar.L1-down", t.player.click_loc, "laddertop", 1, { at = { 2259, 4845, 1 } })
        t.ticks(4)
        local _, l0 = t.world.tile()
        t.check("enterChaosAltar.L0", l0 ~= nil and l0.level == 0, "tile " .. tostring(l0 and l0.x) .. "," .. tostring(l0 and l0.z) .. " level " .. tostring(l0 and l0.level))

        -- ------------------------------------------------------------ useWandOnAltar
        local altar = t.player.by_symbol("loc", "chaos_altar")
        t.exec("useWandOnAltar", t.player.use_on, "surok_metalwand", altar)
        t.exec("useWandOnAltar.glowing", t.inv.await, "surok_glowingwand", 1, 8)
        local _, runes = t.inv.count("chaosrune")
        t.check("useWandOnAltar.runes_spent", runes == 0, "chaosrune now " .. tostring(runes))
        t.chat.continue_()
        t.ticks(2)

        -- ------------------------------------------------------------ bringWandToSurok
        -- Leave the maze by its exit portal (chaostemple_exit_portal 2282,4837 on level 0,
        -- maps/m35_75.jl2): runecraft.rs2 [oploc1,_rc_exit_portal] p_telejumps to a random
        -- runecraft_chaos exit_coord (runecraft.dbrow, eight tiles, minrange/maxrange 0:
        -- 3060,3585 3063,3587 3066,3591 3063,3596 3060,3597 3056,3594 3055,3591 3055,3588),
        -- all round the ruins: the landing is graded on that box, x 3055-3066 z 3585-3597. Then south over the open Wilderness to the ditch,
        -- Cross it south (no warning on a south jump), overland to the palace street and in.
        local function by_ruins(tt)
            return tt.level == 0 and tt.x >= 3055 and tt.x <= 3066 and tt.z >= 3585 and tt.z <= 3597
        end
        t.exec("bringWandToSurok.exitPortal", t.player.climb, { loc = "chaostemple_exit_portal", op = 1, op_name = "Use",
            at = { 2282, 4837, 0 }, dest = { 3060, 3591, 0 }, slack = 6, ticks = 25,
            landed_ok = by_ruins, landed_desc = "an exit_coord round the chaos temple ruins, x 3055-3066 z 3585-3597 level 0",
            same_level = "runecraft.rs2 [oploc1,_rc_exit_portal] p_telejump(map_findsquare(runecraft_chaos exit_coord))" })
        t.exec("goto-bringWandToSurok.ditchNorth", t.player.goto_tile, 3060, 3523, 0)
        t.exec("bringWandToSurok.crossDitch", t.player.cross_trap, { loc = "ditch_wilderness_cover", op_name = "Cross",
            at = { 3060, 3521, 0 }, src = { 3060, 3523 }, dest = { 3060, 3520 }, attempts = 1 })
        local dk = t.chat.kind()
        t.check("bringWandToSurok.crossDitch.noWarning", dk == "none",
            "chat.kind() after the south jump -> " .. tostring(dk) .. " (want none: the ditch warns only on a jump north)")
        palace_in("goto-bringWandToSurok")
        t.exec("bringWandToSurok", t.player.talk_to, "surok_surok", 1)
        t.exec("bringWandToSurok.dialog", t.chat.play, {
            "npc:Ah! You're back",
            "player:I have the things you wanted",
            "npc:Excellent! Well done",
            "player:So...about this gold",
            "npc:All in good time",
            "player:Okay, but I'll be back",
            "npc:Yes, yes, yes",
        })
        t.ticks(2)
        t.expect("quest.stage.letter_to_rat", t.quest.expect_stage("letter_to_rat"))
        t.exec("bringWandToSurok.letter", t.inv.await, "surok_letter2", 1, 6)

        -- ------------------------------------------------------------ talkToRatAfterSurok
        palace_out("talkToRatAfterSurok")
        t.exec("goto-talkToRatAfterSurok", t.player.goto_tile, 3267, 3333, 0)
        t.exec("talkToRatAfterSurok", t.player.talk_to, "surok_rat", 1)
        t.exec("talkToRatAfterSurok.dialog", t.chat.play, {
            "npc:Ah! You've returned",
            "choose:Yes! I have a letter for you.",
            "player:Yes! I have a letter",
            "npc:A letter for me",
            "npc:This letter is treasonous",
            "player:Okay. Go on",
            "npc:I am not really a trader",
            "npc:A short while ago",
            "npc:Okay, here's what I need",
            "npc:His name is Zaff",
            "player:Yes, sir",
        })
        t.ticks(2)
        t.expect("quest.stage.see_zaff", t.quest.expect_stage("see_zaff"))

        -- ------------------------------------------------------------ talkToZaff
        -- Zaff's staff shop (x 3201-3204 z 3431-3437, maps/m50_53.jl2) is shut by fai_varrock_door
        -- on the west edge of 3205,3432: from the open street east of it, in through the door.
        t.exec("goto-talkToZaff", t.player.goto_tile, 3207, 3432, 0)
        t.exec("talkToZaff.shopDoorIn", t.player.pass_door, { closed = "fai_varrock_door", open = "fai_varrock_door_open",
            at = { 3205, 3432, 0 }, near = { 3205, 3432 }, far = { 3203, 3432 }, close = true })
        t.exec("talkToZaff", t.player.talk_to, "zaff", 1)
        t.exec("talkToZaff.dialog", t.chat.play, {
            "player:Rat Burgiss sent me",
            "npc:Ah, yes. Rat sent word",
            "player:Okay, so what's the plan",
            "npc:Listen carefully",
            "npc:Then and ONLY then",
            "npc:Take this beacon ring",
            "npc:Once you have read",
            "player:Won't he refuse",
            "npc:I very much expect so",
            "player:Okay, thanks, Zaff",
        })
        t.ticks(2)
        t.expect("quest.stage.arrest", t.quest.expect_stage("arrest"))
        t.exec("talkToZaff.ring", t.inv.await, "surok_ring", 1, 6)

        -- ------------------------------------------------------------ talkToSurokToFight
        t.exec("talkToSurokToFight.shopDoorOut", t.player.pass_door, { closed = "fai_varrock_door", open = "fai_varrock_door_open",
            at = { 3205, 3432, 0 }, near = { 3204, 3432 }, far = { 3205, 3432 }, close = true })
        palace_in("goto-talkToSurokToFight")
        t.exec("talkToSurokToFight", t.player.talk_to, "surok_surok", 1)
        t.exec("talkToSurokToFight.dialog", t.chat.play, {
            "player:Surok!! Your plans",
            "npc:So! You're with the Secret Guard",
            "player:Give yourself up",
            "npc:Never!",
            "player:The place is surrounded",
            "npc:Do you really wish to die",
            "choose:Bring it on!",
            "player:Bring it on!",
            "npc:I am a Dagon'hai",
            "mesbox:The room grows dark",
        })
        t.ticks(2)

        -- ------------------------------------------------------------ fightRoald
        t.exec("fightRoald", t.player.attack, "surok_king", 2, 20)
        -- whatliesbelow_king.rs2 [ai_queue2/3,surok_king] sets %varb3526_surok_spoken = 1 when the
        -- king is beaten down; hitpoints sampled every 2 ticks for the margin row, a shark under 25.
        local roald_fight = {}
        do
            local wv, rounds = nil, 0
            while rounds < 60 do
                rounds = rounds + 1
                t.ticks(2)
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and type(hp) == "table" and hp.level then
                    note_low(roald_fight, hp.level)
                    if hp.level < 25 then t.player.inv_op("shark", 1) end
                end
                local vr, vv = t.var.server("varb3526_surok_spoken")
                wv = vv
                if vr == "ok" and vv == 1 then break end
            end
            t.check("fightRoald.weakened", wv == 1,
                "varb3526_surok_spoken = " .. tostring(wv) .. " after " .. rounds .. " rounds of 2 ticks (want 1: the king beaten down)")
        end
        margin_row("fightRoald.margin", roald_fight, "King Roald (level 47), hitpoints sampled every 2 ticks")
        t.exec("fightRoald.ring", t.player.inv_op, "surok_ring", 3)
        t.ticks(2)
        t.exec("fightRoald.dialog", t.chat.play, {
            "mesbox:You summon Zaff",
            "npc:The king's mind has been restored",
            "npc:Your teleport spell has been corrupted",
            "npc:You will remain here",
            "npc:Thank you for your help",
        })
        t.ticks(2)
        t.expect("quest.stage.report_rat", t.quest.expect_stage("report_rat"))

        -- ------------------------------------------------------------ talkToRatToFinish
        palace_out("talkToRatToFinish")
        t.exec("goto-talkToRatToFinish", t.player.goto_tile, 3267, 3333, 0)
        local snap_result, snap = t.skill.snapshot()
        t.check("talkToRatToFinish-snapshot", snap_result == "ok", "skill.snapshot before hand-in -> " .. tostring(snap_result))
        t.exec("talkToRatToFinish", t.player.talk_to, "surok_rat", 1)
        t.exec("talkToRatToFinish.dialog", t.chat.play, {
            "npc:Well, how did it go",
            "player:The mission was accomplished",
            "npc:I take it that it went alright",
            "npc:Zaff has already briefed me",
            "npc:You've done very well",
            "mesbox:Continuing and completing",
            "choose:Yes, give me the experience.",
        })
        t.ticks(3)
        local rc_result, rc_detail = t.skill.expect_gain("runecraft", 8000, snap)
        t.check("reward.runecraft_xp", rc_result == "ok", "runecraft +8000 -> " .. tostring(rc_result) .. " " .. tostring(rc_detail))
        local df_result, df_detail = t.skill.expect_gain("defence", 2000, snap)
        t.check("reward.defence_xp", df_result == "ok", "defence +2000 -> " .. tostring(df_result) .. " " .. tostring(df_detail))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
