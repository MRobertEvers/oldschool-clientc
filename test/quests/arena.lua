-- Fight Arena (arena), the route RSProx index 4395 and 4396 record (rev 241,
-- 2026-10-27): Lady Servil; the chest's helmet and platebody, worn; Sammy in his
-- cell; the Head Guard's Khali brew; the barman's; the brew for the keys (and the
-- spare set when they are lost); the keys on Sammy's gate -> the shift change,
-- Sammy into the arena, the ogre; the General and the cells; Hengrad; the
-- scorpion; Bouncer; the Servils freed; the General fought and beaten; his
-- return; out of the compound and back to Lady Servil (the ending with him
-- beaten). test/quests/arena_escape.lua plays the other ending.
-- Sources: quests/quest_arena/scripts/*.rs2; arena_cutscenes.rs2 has the scenes.
--
-- Every round is fought in a private copy of the arena (as recorded), so a
-- scene's camera is checked by op and height, never by tile, and the fighters
-- are found by type in the copy the player stands in.
--
-- The walls (maps/m40_49.jl2, doors closed): Lady Servil, the chest (2613,3189, in
-- the guards' house behind poordoor 2609,3191) and the roads to the bar and the
-- compound are one open component. The compound's ways in on foot are the two
-- fightarena_door1 walk-through doors (north 2617,3171 into the prison corridor,
-- west 2585,3141 into the yard); inside, the corridor, the guard room (poshdoor
-- 2616,3147 / poordoor 2609,3143) and the yard are three rooms; the bar is behind
-- poordoor 2569,3150.

return {
    id = "arena",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99", -- the guide's combat gear: Bouncer is level 137, the General 142
        "::give coins 5", -- the Khali brew
        "::give rune_scimitar 1",
        "::give shark 15",
    },

    run = function(t)
        t.ticklog.start()
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp17_arenaquest",
            constants = {
                -- This route ends with the General beaten: 15 is its complete
                -- (arena_escape.lua binds 14, the other ending).
                complete = 15,
                complete_escaped = 14,
                defeated_bouncer = 11,
                defeated_genkhazard = 13,
                defeated_ogre = 8,
                defeated_scorpion = 10,
                entered_ogre_fight = 6,
                freed_servils = 12,
                given_khali_brew = 5,
                not_started = 0,
                obtained_armour = 2,
                sent_jail = 9,
                spoken_drunkguard = 3,
                started = 1,
            },
            row = "quest_fightarena",
            display = "Fight Arena",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_text(tile)
            if type(tile) ~= "table" then
                return tostring(tile)
            end
            return tile.x .. "," .. tile.z .. "," .. tile.level
        end

        -- A plain door: pressed on its exact tile, or found standing open.
        local function door(name, closed, at, near, far)
            t.exec(name, t.player.pass_door, { closed = closed, open = closed .. "open",
                at = at, near = near, far = far })
        end

        -- The compound's walk-through doors (arena_locs.rs2): in with the disguise, out always.
        local function compound_door(name, at, near, far_ok, far_desc)
            t.exec(name, t.player.cross_gate, { loc = "fightarena_door1", at = at, near = near,
                far_ok = far_ok, far_desc = far_desc })
        end

        -- A scene's pages, in order: each page with a continue is read against the next
        -- fragment and clicked; the empty box a scene puts up while its cast moves has
        -- nothing to click and is waited out. Done when every fragment has been seen and
        -- the chat has been empty for six ticks, or at max_ticks.
        local function scene(name, fragments, max_ticks)
            local seen, idle, waited, last_text = 0, 0, 0, nil
            local misses = {}
            while waited < max_ticks do
                local kind = t.chat.kind()
                local text = nil
                if kind == "npc" or kind == "player" or kind == "mesbox" or kind == "objbox" then
                    local r, d = t.chat.text()
                    if r == "ok" and type(d) == "string" and d ~= "" then
                        text = d
                    end
                end
                if text ~= nil then
                    idle = 0
                    if text ~= last_text then
                        seen = seen + 1
                        local want = fragments[seen]
                        if want == nil or not string.find(text, want, 1, true) then
                            misses[#misses + 1] = seen .. ":" .. text:sub(1, 40)
                        end
                        last_text = text
                    end
                    t.chat.continue_()
                elseif kind == "options" then
                    break
                else
                    idle = idle + 1
                    if seen >= #fragments and idle >= 6 then
                        break
                    end
                    t.ticks(1)
                end
                waited = waited + 1
            end
            t.check(name, seen == #fragments and #misses == 0,
                seen .. "/" .. #fragments .. " page(s) over " .. waited .. " step(s)"
                .. (#misses > 0 and (": " .. table.concat(misses, " | ")) or ""))
        end

        -- A scene's camera, by op and height (the copy's tiles differ per run). The
        -- camera is not observable on every lane: there the row says so and fails.
        local function camera(name, mark, expect)
            if mark == nil then
                t.check(name, false, "no camera state on this lane (scriptrun); check on a live run")
                return
            end
            t.exec(name, t.cutscene.await, name, { since = mark, quiet = 120, expect = expect })
        end

        -- A fight's margin: lowest hp at least a quarter of the maximum AND food left.
        local function fight_margin(name, detail, food_before)
            local lowest = tonumber(tostring(detail):match("lowest hp (%d+)/"))
            local _, hitpoints = t.skill.read("hitpoints")
            local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
            local food_result, food_left = t.inv.count("shark")
            t.check(name, lowest ~= nil and max_hp ~= nil and food_result == "ok"
                and lowest * 4 >= max_hp and food_left >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", sharks "
                .. tostring(food_before) .. " -> " .. tostring(food_left))
        end

        local function fight(name, npc, ticks)
            t.exec(name, t.player.attack, npc, 2, 20)
            local _, food = t.inv.count("shark")
            local _, detail = t.exec(name .. ".dead", t.npc.await_dead_engaged, ticks, 60,
                { eat = { item = "shark", below = 60 } })
            fight_margin(name .. ".margin", detail, food)
        end

        -- ------------------------------------------------- Lady Servil (4395 [111..127])
        t.exec("goto-startQuest.memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("startQuest.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320" })
        t.exec("goto-startQuest", t.player.goto_tile, 2566, 3198, 0)
        t.exec("startQuest", t.player.talk_to, "lady_servil", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "player:Hi there. Looks like you're in some trouble.",
            "npc:The cart? That's the least of my problems.",
            "choose:Yes.",
            "player:Your family? Can I help?",
            "npc:You'd be willing to?",
            "player:Of course. Tell me what happened.",
            "npc:Well, I'm Lady Servil.",
            "npc:Some of General Khazard's men",
            "player:General Khazard?",
            "npc:He's a menace!",
            "player:That's horrible!",
            "npc:The authorities don't care.",
            "player:Well they won't stop me.",
            "npc:Thank you. They'll be somewhere inside the arena.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ------------------------------------------------- the chest (4395 [182..190])
        t.exec("goto-searchChest", t.player.goto_tile, 2609, 3192, 0)
        door("searchChest.houseIn", "poordoor", { 2609, 3191, 0 }, { 2609, 3192 }, { 2609, 3190 })
        t.exec("searchChest", t.player.click_loc, "arena_guard_chest_shut", 1)
        t.exec("searchChest.helmet", t.inv.await, "khazard_helmet", 1, 15)
        t.exec("searchChest-dialog", t.chat.play, { "objbox:You search the chest and find a helmet and platebody." })
        t.exec("searchChest.platemail", t.inv.expect_has, "khazard_platemail", 1)
        t.expect("quest.stage.obtained_armour", t.quest.expect_stage("obtained_armour"))
        door("searchChest.houseOut", "poordoor", { 2609, 3191, 0 }, { 2609, 3190 }, { 2609, 3192 })

        t.exec("equip.helmet", t.player.equip, "khazard_helmet")
        t.exec("equip.platemail", t.player.equip, "khazard_platemail")
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")

        -- ------------------------------------------------- Sammy (4395 [218..226])
        t.exec("goto-northDoor", t.player.goto_tile, 2617, 3173, 0)
        compound_door("talkToSammy.northDoorIn", { 2617, 3171, 0 }, { 2617, 3172 },
            function(tile) return tile.z <= 3171 and tile.x >= 2613 and tile.x <= 2619 end,
            "in the prison corridor, z <= 3171")
        -- 4395 [221..222]: the player at his gate, then the talk.
        -- He walks his cell (4395: 10 steps over his lives); from the corridor our engine
        -- finds him only while he stands in its line of sight from the gate (at 2616,3166, the
        -- south end, the ray through the gate is refused: OPEN), so the talk is retried.
        t.exec("goto-sammyGate", t.player.walk_to, 2617, 3167)
        do
            local talk_result, talk_detail
            for _ = 1, 30 do
                t.player.walk_to(2617, 3167)
                talk_result, talk_detail = t.player.talk_to("sammy_servil", 1)
                if talk_result == "ok" then
                    break
                end
                t.ticks(3)
            end
            t.check("talkToSammy", talk_result == "ok", tostring(talk_detail))
        end
        t.exec("talkToSammy-dialog", t.chat.play, {
            "player:Hello. Are you Sammy?",
            "npc:Please... don't hurt me.",
            "player:Don't worry, this armour is a disguise.",
            "npc:The Head Guard has them.",
            "player:Don't lose heart, I'll be back.",
        })
        local _, met = t.var.varbit("varb14717_arenaquest_met_sammy")
        t.check("talkToSammy.met", met == 1, "varbit arenaquest_met_sammy = " .. tostring(met) .. " (want 1, [225])")

        -- ------------------------------------------------- the Head Guard (4395 [246..256])
        door("talkToGuard.guardRoomIn", "poshdoor", { 2616, 3147, 0 }, { 2616, 3147 }, { 2616, 3146 })
        t.exec("talkToGuard", t.player.talk_to, "arena_guard2", 1)
        t.exec("talkToGuard-dialog", t.chat.play, {
            "player:Long live General Khazard!",
            "npc:Er... yes... quite right.",
            "player:Just checking if you have any orders for me.",
            "npc:Not right now.",
            "player:Khali brew? Can't say I've ever had one.",
            "npc:Oh, nothing beats them!",
            "player:Good to know, thanks.",
        })
        t.expect("quest.stage.spoken_drunkguard", t.quest.expect_stage("spoken_drunkguard"))

        -- ------------------------------------------------- the bar (4395 [416..450])
        door("buyKhaliBrew.guardRoomOut", "poordoor", { 2609, 3143, 0 }, { 2609, 3143 }, { 2608, 3143 })
        t.exec("goto-westDoor", t.player.goto_tile, 2585, 3141, 0)
        compound_door("buyKhaliBrew.westDoorOut", { 2585, 3141, 0 }, { 2585, 3141 },
            function(tile) return tile.x <= 2584 end, "outside the compound, x <= 2584")
        t.exec("goto-bar", t.player.goto_tile, 2569, 3151, 0)
        door("buyKhaliBrew.barIn", "poordoor", { 2569, 3150, 0 }, { 2569, 3151 }, { 2569, 3150 })
        local _, coins_before_brew = t.inv.count("coins")
        t.exec("buyKhaliBrew", t.player.talk_to, "khazard_barman", 1)
        t.exec("buyKhaliBrew-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Hi, what can I get you?",
            "choose:I'd like a Khali Brew please.",
            "player:I'd like a Khali Brew please.",
            "npc:That'll be five coins. I suggest lying down",
            "objbox:The barman gives you a Khali brew in exchange for five coins.",
        })
        t.exec("buyKhaliBrew.brew", t.inv.await, "khali_brew", 1, 10)
        local _, coins_after_brew = t.inv.count("coins")
        t.check("buyKhaliBrew.paid", coins_before_brew ~= nil and coins_after_brew ~= nil
            and coins_before_brew - coins_after_brew == 5,
            "coins " .. tostring(coins_before_brew) .. " -> " .. tostring(coins_after_brew))

        -- ------------------------------------------------- the keys (4395 [486..500])
        door("giveKhaliBrew.barOut", "poordoor", { 2569, 3150, 0 }, { 2569, 3150 }, { 2569, 3151 })
        t.exec("goto-westDoorIn", t.player.goto_tile, 2584, 3141, 0)
        compound_door("giveKhaliBrew.westDoorIn", { 2585, 3141, 0 }, { 2584, 3141 },
            function(tile) return tile.x >= 2585 end, "inside the compound yard, x >= 2585")
        t.exec("goto-guardRoomWest", t.player.goto_tile, 2608, 3143, 0)
        door("giveKhaliBrew.guardRoomIn", "poordoor", { 2609, 3143, 0 }, { 2608, 3143 }, { 2609, 3143 })
        t.exec("giveKhaliBrew", t.player.talk_to, "arena_guard2", 1)
        t.exec("giveKhaliBrew-dialog", t.chat.play, {
            "player:Hello again.",
            "npc:What a boring day.",
            "player:Do you still fancy a drink?",
            "npc:I really shouldn't...",
            "player:Are you sure?",
            "npc:Oh... okay then, but just the one.",
            "objbox:You hand a bottle of Khali brew to the Head Guard",
            "npc:That is some gooood stuff",
            "player:Are you alright?",
            "npc:Yeesshh! Oooh",
            "player:Good idea. I'll look after the prisoners.",
            "npc:Yeesh... that shounds reasonable.",
            "objbox:The Head Guard gives you some keys.",
            "player:Don't worry, I'll keep them in line.",
        })
        t.expect("quest.stage.given_khali_brew", t.quest.expect_stage("given_khali_brew"))
        t.exec("giveKhaliBrew.keys", t.inv.await, "khazard_cellkeys", 1, 10)
        local _, brew_left = t.inv.count("khali_brew")
        t.check("giveKhaliBrew.brewHandedOver", brew_left == 0, "khali_brew left: " .. tostring(brew_left))

        -- The spare set (the wiki, "Talking to the Head Guard after losing the keys").
        t.exec("getCellKeys.drop", t.player.drop, "khazard_cellkeys")
        t.exec("getCellKeys", t.player.talk_to, "arena_guard2", 1)
        t.exec("getCellKeys-dialog", t.chat.play, {
            "player:Hi, er... I lost the keys.",
            "npc:What? You're foolish...",
            "objbox:The Head Guard gives you some keys.",
        })
        t.exec("getCellKeys.keys", t.inv.await, "khazard_cellkeys", 1, 10)

        -- ------------------------------------------------- Sammy's release (4395 [576..621])
        door("openCell.guardRoomOut", "poshdoor", { 2616, 3147, 0 }, { 2616, 3146 }, { 2616, 3147 })
        do
            local walk_result, walk_detail = t.player.walk_to(2617, 3166)
            local tile_result, tile = t.world.tile()
            t.check("openCell.walk", tile_result == "ok" and tile.x == 2617 and tile.z == 3166 and tile.level == 0,
                "walk_to 2617,3166 -> " .. tostring(walk_result) .. " " .. tostring(walk_detail)
                .. "; at " .. tile_text(tile) .. " (want 2617,3166,0 beside Sammy's cell)")
        end
        local release_mark = t.cutscene.mark()
        t.exec("openCell", t.player.use_on, "khazard_cellkeys", t.player.by_symbol("loc", "arena_jeremydoor"))
        scene("openCell.scene", {
            "Sammy look, I have the keys.",
            "Wow! Please hurry and set me free.",
            "We'll need to wait for the guards to change shift",
            "Alright, the coast is clear.",
            "Now, let's get you out of here.",
            "Not yet! We need to find my dad!",
            "Sammy, wait!",
            "Quick, help him!",
        }, 120)
        camera("openCell.cutscene", release_mark, {
            { op = "moveto", height = 500 },
            { op = "lookat", height = 220 },
            { op = "moveto", height = 490 },
            { op = "lookat", height = 210 },
            { op = "moveto", height = 490 },
            { op = "lookat", height = 15 },
            { op = "moveto", height = 490 },
            { op = "lookat", height = 200 },
            { op = "reset" },
        })
        t.expect("quest.stage.entered_ogre_fight", t.quest.expect_stage("entered_ogre_fight"))
        local _, keys_left = t.inv.count("khazard_cellkeys")
        t.check("openCell.keysTaken", keys_left == 0, "khazard_cellkeys left: " .. tostring(keys_left) .. " (taken at [617])")

        -- ------------------------------------------------- the ogre, the General, Hengrad
        fight("killOgre", "arena_ogre", 240)
        local general_mark = t.cutscene.mark()
        scene("generalScene", {
            "Well, well, well, you seem to be in quite the rush.",
            "Oh no...",
            "General Khazard, I presume?",
            "You presume correct.",
            "I've seen worse.",
            "How arrogant.",
            "Now, despite your entertaining show",
            "They belong to nobody!",
            "Ha! Well if their freedom is so valuable to you",
            "And what if I refuse your offer?",
            "Well... I wasn't actually giving you a choice.",
            "Hello there. So Khazard got his hands on you too?",
            "Unfortunately so.",
            "Well if you're lucky, you might last as long as me.",
            "How long have you been here?",
            "As long as I can remember.",
            "Don't give up.",
            "Thanks, friend.",
            "Wait... sounds like the guard is coming.",
            "Now watch closely, friends.",
            "Ignore him! You can do it!",
        }, 200)
        camera("generalScene.cutscene", general_mark, {
            { op = "moveto", height = 600 },
            { op = "lookat", height = 300 },
            { op = "reset" },
        })
        t.expect("quest.stage.sent_jail", t.quest.expect_stage("sent_jail"))
        local _, scorp_cut = t.var.varbit("varb14718_arenaquest_scorpion_cutscene")
        t.check("scorpionScene.varbit", scorp_cut == 1, "arenaquest_scorpion_cutscene = " .. tostring(scorp_cut))

        -- ------------------------------------------------- the scorpion, Bouncer
        fight("killScorpion", "arena_scorpion", 240)
        scene("bouncerScene", {
            "Impressive, but now for a proper challenge.",
        }, 60)
        t.expect("quest.stage.defeated_scorpion", t.quest.expect_stage("defeated_scorpion"))
        fight("killBouncer", "arena_bouncer", 400)
        scene("freedScene", {
            "Bouncer! No!",
            "You monster! You'll pay for what you've done!",
            "I only did what you asked.",
            "Fine! I am not a coward who makes false promises.",
            "Guard, escort the Servils out.",
            "Don't try to fight him! Just run!",
        }, 120)
        t.expect("quest.stage.freed_servils", t.quest.expect_stage("freed_servils"))

        -- ------------------------------------------------- the General (4396 [203..525])
        fight("killGeneral", "general_khazard", 600)
        scene("returnScene", {
            "Going somewhere?",
            "But... I killed you!",
            "Ha! Arrogant fool!",
            "Now, part of me would very much like to kill you",
            "You proved yourself in battle against me.",
            "Now go. I have business to attend to elsewhere",
        }, 120)
        t.expect("quest.stage.defeated_genkhazard", t.quest.expect_stage("defeated_genkhazard"))
        local yard_result, yard = t.world.tile()
        t.check("returnScene.yard", yard_result == "ok" and yard.x == 2607 and yard.z == 3151 and yard.level == 0,
            "after the scene at " .. tile_text(yard) .. " (want 2607,3151,0: 4396 [523])")

        -- ------------------------------------------------- Lady Servil (4396 [575..602])
        t.exec("goto-westDoorOut", t.player.goto_tile, 2585, 3141, 0)
        compound_door("endQuest.westDoorOut", { 2585, 3141, 0 }, { 2585, 3141 },
            function(tile) return tile.x <= 2584 end, "outside the compound, x <= 2584")
        t.exec("goto-endQuest", t.player.goto_tile, 2566, 3198, 0)
        local _, snapshot = t.skill.snapshot()
        local _, coins_before = t.inv.count("coins")
        t.exec("endQuest", t.player.talk_to, "lady_servil", 1)
        t.exec("endQuest-dialog", t.chat.play, {
            "player:Lady Servil.",
            "npc:You're alive! I feared General Khazard would have killed you!",
            "player:He tried, but I was able to defeat him.",
            "npc:You killed General Khazard?",
            "player:I thought I had at first",
            "npc:Well, at least you're alive.",
            "npc:Now, you'll be pleased to know",
            "player:It's not a problem.",
            "npc:Please, take this payment.",
        })
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.quest.expect_complete()
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 12175, snapshot))
        t.expect("reward.thieving_xp", t.skill.expect_gain("thieving", 2175, snapshot))
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", coins_before ~= nil and coins_after ~= nil and coins_after - coins_before == 1000,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after))
        t.finish(0)
    end,
}
