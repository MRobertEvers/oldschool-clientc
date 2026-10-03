-- Dream Mentor. Relay test: leg 1 (goDownToCyrisus .. talkToCyrisus4) written; legs 2-3 follow.
-- Guide: Quest Helper DreamMentor.java via tools/quest_gate/ladder.py dreammentor.
-- Content notes: docs/quests/ladders/dreammentor.notes.md.

return {
    id = "dreammentor",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the food fits
        "::give shark 7", -- guide: 14 pieces of food, three kinds (feed4Food, feed4Food2, feed6Food)
        "::give lobster 7",
        "::give swordfish 6", -- 20 pieces in all: leg 1 feeds 14, leg 2 (useFood3) feeds the other 6
        "::give lunar_seal_of_passage 1", -- guide item requirement (later legs)
        "::give eadgar_goutweed_herb 1",
        "::give astralrune 1",
        "::give pestle_and_mortar 1",
        "::give tinderbox 1",
        "::give hammer 1", -- guide: hammer for the astral rune (leg 3)
        "::setlevel attack 99", -- combat level 85+ to start (dreammentor_cyrisus.rs2 dreammentor_start)
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99", -- ranged gear for the dream fights (melee misses the 240-defence forms)
        "::give magic_shortbow 1",
        "::give rune_arrow 800",
        "::wield magic_shortbow",
        "::wield rune_arrow", -- guide: combat gear for the dream fight, worn from the start
        "::give rune_full_helm 1",
        "::wield rune_full_helm",
        "::give rune_chainbody 1",
        "::wield rune_chainbody",
        "::give rune_platelegs 1",
        "::wield rune_platelegs",
        "::complete quest_lunardiplomacy", -- guide requirement: Lunar Diplomacy
        "::complete quest_eadgarsruse", -- guide requirement: Eadgar's Ruse
    },
    bind = {
        varp = "varb3618_dream_prog",
        constants = {
            not_started = 0,
            found_cyrisus = 4,
            stage2_feeding = 6,
            feeding_second = 8,
            stage3_feeding = 12,
            need_gear = 16,
            gear_given = 18,
            met_oneiromancer = 20,
            dream_ready = 24,
            bosses_defeated = 26,
            complete = 28,
        },
        row = "quest_dreammentor",
        display = "Dream Mentor",
        points = 2,
    },
    legs = {
        { name = "cyrisus", run = function(t)
        -- ---- goDownToCyrisus: the ladder pair into the Lunar mine (dreammentor_cyrisus.rs2:17) ----
        t.exec("goto-goDownToCyrisus", t.player.goto_tile, 2142, 3944, 0)
        t.exec("goDownToCyrisus", t.player.click_loc, "lunar_mine_slanty_ladder_down", 1)
        t.ticks(3)
        do local _, tile = t.world.tile(); local _, level = t.world.level()
            t.check("goDownToCyrisus-landed", level == 2, "after the ladder: tile " .. tostring(tile and (tile.x .. "," .. tile.z)) .. " level " .. tostring(level)) end

        -- ---- enterCyrisusCave: wall A crawls in (dreammentor_cyrisus.rs2:22) ----
        t.exec("enterCyrisusCave", t.player.click_loc, "dream_cave_wall_entrance", 1)
        t.ticks(3)
        do local _, tile = t.world.tile()
            t.check("enterCyrisusCave-landed", tile ~= nil, "after the crawl: tile " .. tostring(tile and (tile.x .. "," .. tile.z))) end

        -- ---- talkToCyrisus: the offer (dreammentor_start, dreammentor_cyrisus.rs2:~160) ----
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("talkToCyrisus", t.player.talk_to, "dream_cyrisus_unconscious", 1)
        t.exec("talkToCyrisus-offer", t.chat.drain, { stop_at = "options", max_pages = 6 })
        t.exec("talkToCyrisus-yes", t.chat.choose, "Yes.")
        t.exec("talkToCyrisus-pages", t.chat.play, {
            "mesbox:Torn clothes, covered in bruises and cuts",
            "mesbox:He seems to have a pulse.",
            "mesbox:And he's definitely breathing",
            "mesbox:Let's have a closer inspection",
            "mesbox:The following screen will show you the man's stats",
        })
        t.ticks(2)
        do local ok = t.ui.await_open("dream_cyrisus", 10); t.check("talkToCyrisus-stats-screen", ok == "ok", "the Fallen Man's status screen (interface dream_cyrisus) open: " .. tostring(ok)) end
        t.key("escape")
        t.ticks(2)
        t.expect("quest.stage.found_cyrisus", t.quest.expect_stage("found_cyrisus"))

        -- ---- feed4Food: four pieces, three kinds, never the same twice running ----
        do local cy = t.player.by_symbol("npc", "dream_cyrisus_unconscious")
            t.exec("feed4Food-1-shark", t.player.use_on, "shark", cy)
            t.exec("feed4Food-1-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed4Food-2-lobster", t.player.use_on, "lobster", cy)
            t.exec("feed4Food-2-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed4Food-3-swordfish", t.player.use_on, "swordfish", cy)
            t.exec("feed4Food-3-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed4Food-4-shark", t.player.use_on, "shark", cy)
            t.exec("feed4Food-4-page", t.chat.drain, { max_pages = 20 })
        end
        t.ticks(2)
        do local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("feed4Food-bars", true, "Cyrisus bars read from the client: Health " .. tostring(hv) .. "%, Spirit " .. tostring(sv) .. "%") end
        t.expect("quest.stage.stage2_feeding", t.quest.expect_stage("stage2_feeding"))

        -- ---- talkToCyrisus2: his first conversation (stage 6 -> 8) ----
        t.exec("talkToCyrisus2", t.player.talk_to, "dream_cyrisus_barely_conscious", 1)
        t.exec("talkToCyrisus2-r1-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus2-r1-first", t.chat.choose, "Just don't worry.")
        t.exec("talkToCyrisus2-r1-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus2-r1-second", t.chat.choose, "Of course.")
        t.exec("talkToCyrisus2-r1-tail", t.chat.drain, { max_pages = 40 })
        t.ticks(2)
        do local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("talkToCyrisus2-bars", true, "Cyrisus bars read from the client: Health " .. tostring(hv) .. "%, Spirit " .. tostring(sv) .. "%") end
        t.expect("quest.stage.feeding_second", t.quest.expect_stage("feeding_second"))

        -- ---- feed4Food2: four more, Health 20 -> 40 ----
        do local cy = t.player.by_symbol("npc", "dream_cyrisus_barely_conscious")
            t.exec("feed4Food2-1-lobster", t.player.use_on, "lobster", cy)
            t.exec("feed4Food2-1-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed4Food2-2-swordfish", t.player.use_on, "swordfish", cy)
            t.exec("feed4Food2-2-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed4Food2-3-shark", t.player.use_on, "shark", cy)
            t.exec("feed4Food2-3-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed4Food2-4-lobster", t.player.use_on, "lobster", cy)
            t.exec("feed4Food2-4-page", t.chat.drain, { max_pages = 20 })
        end
        t.ticks(2)
        do local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("feed4Food2-bars", true, "Cyrisus bars read from the client: Health " .. tostring(hv) .. "%, Spirit " .. tostring(sv) .. "%") end

        -- ---- talkToCyrisus3: rounds 2-4 until Spirit 32 (stage 8 -> 12) ----
        t.exec("talkToCyrisus3-1", t.player.talk_to, "dream_cyrisus_barely_conscious", 1)
        t.exec("talkToCyrisus3-1-r2-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus3-1-r2-first", t.chat.choose, "You're looking better now.")
        t.exec("talkToCyrisus3-1-r2-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus3-1-r2-second", t.chat.choose, "Well, you look and sound more lively.")
        t.exec("talkToCyrisus3-1-r2-tail", t.chat.drain, { max_pages = 40 })
        t.exec("talkToCyrisus3-2", t.player.talk_to, "dream_cyrisus_barely_conscious", 1)
        t.exec("talkToCyrisus3-2-r3-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus3-2-r3-first", t.chat.choose, "Are you looking forward to getting out?")
        t.exec("talkToCyrisus3-2-r3-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus3-2-r3-second", t.chat.choose, "That's the spirit!")
        t.exec("talkToCyrisus3-2-r3-tail", t.chat.drain, { max_pages = 40 })
        t.exec("talkToCyrisus3-3", t.player.talk_to, "dream_cyrisus_barely_conscious", 1)
        t.exec("talkToCyrisus3-3-r4-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus3-3-r4-first", t.chat.choose, "You seem like a nice guy.")
        t.exec("talkToCyrisus3-3-r4-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus3-3-r4-second", t.chat.choose, "Just being honest.")
        t.exec("talkToCyrisus3-3-r4-tail", t.chat.drain, { max_pages = 40 })
        t.ticks(2)
        do local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("talkToCyrisus3-bars", true, "Cyrisus bars read from the client: Health " .. tostring(hv) .. "%, Spirit " .. tostring(sv) .. "%") end
        t.expect("quest.stage.stage3_feeding", t.quest.expect_stage("stage3_feeding"))

        -- ---- feed6Food: six pieces, Health 40 -> 70 (Cyrisus sits up now) ----
        do local cy = t.player.by_symbol("npc", "dream_cyrisus_sitting")
            t.exec("feed6Food-1-swordfish", t.player.use_on, "swordfish", cy)
            t.exec("feed6Food-1-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed6Food-2-shark", t.player.use_on, "shark", cy)
            t.exec("feed6Food-2-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed6Food-3-lobster", t.player.use_on, "lobster", cy)
            t.exec("feed6Food-3-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed6Food-4-swordfish", t.player.use_on, "swordfish", cy)
            t.exec("feed6Food-4-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed6Food-5-shark", t.player.use_on, "shark", cy)
            t.exec("feed6Food-5-page", t.chat.drain, { max_pages = 20 })
            t.exec("feed6Food-6-lobster", t.player.use_on, "lobster", cy)
            t.exec("feed6Food-6-page", t.chat.drain, { max_pages = 20 })
        end
        t.ticks(2)
        do local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("feed6Food-bars", true, "Cyrisus bars read from the client: Health " .. tostring(hv) .. "%, Spirit " .. tostring(sv) .. "%") end

        -- ---- talkToCyrisus4: rounds 5-9 until Spirit 72 (stage 12 -> 16) ----
        t.exec("talkToCyrisus4-1", t.player.talk_to, "dream_cyrisus_sitting", 1)
        t.exec("talkToCyrisus4-1-r5-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-1-r5-first", t.chat.choose, "When we get out of here I'll buy you a drink!")
        t.exec("talkToCyrisus4-1-r5-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-1-r5-second", t.chat.choose, "Whatever and wherever you want - my treat.")
        t.exec("talkToCyrisus4-1-r5-tail", t.chat.drain, { max_pages = 40 })
        t.exec("talkToCyrisus4-2", t.player.talk_to, "dream_cyrisus_sitting", 1)
        t.exec("talkToCyrisus4-2-r6-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-2-r6-first", t.chat.choose, "I'm very impressed you managed to get into this cave.")
        t.exec("talkToCyrisus4-2-r6-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-2-r6-second", t.chat.choose, "I would have given up personally.")
        t.exec("talkToCyrisus4-2-r6-tail", t.chat.drain, { max_pages = 40 })
        t.exec("talkToCyrisus4-3", t.player.talk_to, "dream_cyrisus_sitting", 1)
        t.exec("talkToCyrisus4-3-r7-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-3-r7-first", t.chat.choose, "You'll survive this easily.")
        t.exec("talkToCyrisus4-3-r7-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-3-r7-second", t.chat.choose, "Think of all the places you can visit when you get out!")
        t.exec("talkToCyrisus4-3-r7-tail", t.chat.drain, { max_pages = 40 })
        t.exec("talkToCyrisus4-4", t.player.talk_to, "dream_cyrisus_sitting", 1)
        t.exec("talkToCyrisus4-4-r8-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-4-r8-first", t.chat.choose, "What are you going to do when you get out of here?")
        t.exec("talkToCyrisus4-4-r8-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-4-r8-second", t.chat.choose, "That's up to you. You could travel with me!")
        t.exec("talkToCyrisus4-4-r8-tail", t.chat.drain, { max_pages = 40 })
        t.exec("talkToCyrisus4-5", t.player.talk_to, "dream_cyrisus_sitting", 1)
        t.exec("talkToCyrisus4-5-r9-menu1", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-5-r9-first", t.chat.choose, "It's a good thing you have me to look after you.")
        t.exec("talkToCyrisus4-5-r9-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisus4-5-r9-second", t.chat.choose, "Not that I'm bragging or anything.")
        t.exec("talkToCyrisus4-5-r9-tail", t.chat.drain, { max_pages = 40 })
        t.ticks(2)
        do local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("talkToCyrisus4-bars", true, "Cyrisus bars read from the client: Health " .. tostring(hv) .. "%, Spirit " .. tostring(sv) .. "%") end
        t.expect("quest.stage.need_gear", t.quest.expect_stage("need_gear"))

        do local _, tile = t.world.tile(); local _, level = t.world.level(); local _, stage = t.var.server("varb3618_dream_prog")
            local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("leg.1.end", true, "quiet: tile " .. tostring(tile and (tile.x .. "," .. tile.z)) .. " level " .. tostring(level) .. ", dream_prog read from the server " .. tostring(stage) .. " (need_gear), Health " .. tostring(hv) .. " Spirit " .. tostring(sv) .. "; food all spent (14 of 14), tinderbox, pestle and mortar, astral rune, seal of passage and goutweed still carried") end
        end },
        { name = "armament", run = function(t)
        -- LEG 2 of 3: leaveCave .. talkToOneiromancer. Starts at stage 16 (need_gear), Health 70, Spirit 72, Armament 0.
        -- ---- leaveCave: wall B crawls out (dreammentor_cyrisus.rs2:22) ----
        t.exec("leaveCave", t.player.click_loc, "dream_cave_wall_entrance", 1, { at = { 2341, 10355 } })
        t.ticks(3)
        do local _, tile = t.world.tile(); local _, level = t.world.level()
            t.check("leaveCave-landed", tile ~= nil and tile.x < 2340, "after the crawl out: tile " .. tostring(tile and (tile.x .. "," .. tile.z)) .. " level " .. tostring(level)) end

        -- ---- goUpToSurface: the mine ladder (dreammentor_cyrisus.rs2:19) ----
        t.exec("goUpToSurface", t.player.click_loc, "lunar_mine_slanty_ladder_up", 1)
        t.ticks(3)
        do local _, tile = t.world.tile(); local _, level = t.world.level()
            t.check("goUpToSurface-landed", level == 0, "after the ladder: tile " .. tostring(tile and (tile.x .. "," .. tile.z)) .. " level " .. tostring(level)) end

        -- ---- talkToJack: 'Cyrisus in the mine' gives the chest and opens Cyrisus's bank (dreammentor_armour.rs2:273) ----
        t.exec("goto-talkToJack", t.player.goto_tile, 2099, 3921, 0)
        t.exec("talkToJack", t.player.talk_to, "dream_birds_eye_jack", 1)
        t.exec("talkToJack-menu", t.chat.drain, { stop_at = "options", max_pages = 6 })
        t.exec("talkToJack-topic", t.chat.choose, "Cyrisus in the mine")
        do local pages = {}
            for i = 1, 33 do pages[i] = "*" end -- 34 chat pages (dreammentor_armour.rs2 banker_intro branch); the last one opens the bank
            pages[34] = "npc:I suppose you had better take this chest"
            t.exec("talkToJack-pages", t.chat.play, pages)
        end
        do local ok = t.ui.await_open("dream_armour", 15)
            t.check("talkToJack-bank-open", ok == "ok", "Cyrisus's bank screen (interface dream_armour) open: " .. tostring(ok)) end
        -- the melee set (attack+strength 80 beats ranged and magic, dreammentor_shared.rs2:186): helm 15, body 2, legs 27, boots 30, weapon 19
        do local picks = { { 15, "dragon med helm", "varb3627_dream_arma_item1" }, { 2, "Ahrim's robetop", "varb3628_dream_arma_item2" }, { 27, "Ahrim's robeskirt", "varb3629_dream_arma_item3" }, { 30, "ranger boots", "varb3630_dream_arma_item4" }, { 19, "abyssal whip", "varb3631_dream_arma_item5" } }
            for _, p in ipairs(picks) do
                local _, w = t.ui.widget("dream_armour:bank_layer", p[1])
                t.ui.invoke(w, 1) -- hollow verb: the row below reads the chest varbit back
                t.ticks(2)
                local _, got = t.var.varbit(p[3])
                t.check("talkToJack-withdraw-" .. p[1], got == p[1], "bank slot " .. p[1] .. " (" .. p[2] .. ") clicked; " .. p[3] .. " reads " .. tostring(got))
            end
        end
        t.ticks(2)
        do local _, h = t.var.varbit("varb3627_dream_arma_item1"); local _, b = t.var.varbit("varb3628_dream_arma_item2")
            local _, l = t.var.varbit("varb3629_dream_arma_item3"); local _, f = t.var.varbit("varb3630_dream_arma_item4"); local _, wp = t.var.varbit("varb3631_dream_arma_item5")
            t.check("talkToJack-chest-filled", h == 15 and b == 2 and l == 27 and f == 30 and wp == 19, "chest slots read from the varbits: helm " .. tostring(h) .. " body " .. tostring(b) .. " legs " .. tostring(l) .. " boots " .. tostring(f) .. " weapon " .. tostring(wp)) end
        t.key("escape")
        t.ticks(2)
        t.expect("talkToJack-chest-held", t.inv.expect_has("dream_chest", 1))

        -- ---- goBackDownToCyrisus / enterCyrisusCaveAgain ----
        t.exec("goto-goBackDownToCyrisus", t.player.goto_tile, 2142, 3944, 0)
        t.exec("goBackDownToCyrisus", t.player.click_loc, "lunar_mine_slanty_ladder_down", 1)
        t.ticks(3)
        do local _, tile = t.world.tile(); local _, level = t.world.level()
            t.check("goBackDownToCyrisus-landed", level == 2, "after the ladder: tile " .. tostring(tile and (tile.x .. "," .. tile.z)) .. " level " .. tostring(level)) end
        t.exec("enterCyrisusCaveAgain", t.player.click_loc, "dream_cave_wall_entrance", 1, { at = { 2335, 10346 } })
        t.ticks(3)
        do local _, tile = t.world.tile()
            t.check("enterCyrisusCaveAgain-landed", tile ~= nil and tile.x > 2338, "after the crawl in: tile " .. tostring(tile and (tile.x .. "," .. tile.z))) end

        -- ---- giveCyrisusGear: 'Talk about the Armament' (dreammentor_armour.rs2:~222) ----
        t.exec("giveCyrisusGear-present", t.npc.await_present, "dream_cyrisus", 20, 20)
        t.exec("giveCyrisusGear", t.player.talk_to, "dream_cyrisus", 1)
        t.exec("giveCyrisusGear-menu", t.chat.drain, { stop_at = "options", max_pages = 6 })
        t.exec("giveCyrisusGear-topic", t.chat.choose, "Talk about the Armament")
        t.exec("giveCyrisusGear-pages", t.chat.drain, { max_pages = 40 })
        t.ticks(2)
        do local _, av = t.var.varbit("varb3623_dream_armament")
            t.check("giveCyrisusGear-armament", av == 100, "Armament read from the client: " .. tostring(av) .. "%") end

        t.exec("giveCyrisusGear-dressed", t.npc.await_present, "dream_cyrisus_melee", 20, 20)

        -- ---- useFood3: six pieces, three kinds, never the same twice running (Health 70 -> 100) ----
        do local cy = t.player.by_symbol("npc", "dream_cyrisus_melee")
            t.exec("useFood3-1-swordfish", t.player.use_on, "swordfish", cy)
            t.exec("useFood3-1-page", t.chat.drain, { max_pages = 20 })
            t.exec("useFood3-2-shark", t.player.use_on, "shark", cy)
            t.exec("useFood3-2-page", t.chat.drain, { max_pages = 20 })
            t.exec("useFood3-3-lobster", t.player.use_on, "lobster", cy)
            t.exec("useFood3-3-page", t.chat.drain, { max_pages = 20 })
            t.exec("useFood3-4-swordfish", t.player.use_on, "swordfish", cy)
            t.exec("useFood3-4-page", t.chat.drain, { max_pages = 20 })
            t.exec("useFood3-5-shark", t.player.use_on, "shark", cy)
            t.exec("useFood3-5-page", t.chat.drain, { max_pages = 20 })
            t.exec("useFood3-6-lobster", t.player.use_on, "lobster", cy)
            t.exec("useFood3-6-page", t.chat.drain, { max_pages = 20 })
        end
        t.ticks(2)
        do local _, hv = t.var.varbit("varb3621_dream_health"); local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check("useFood3-bars", hv == 100, "Cyrisus bars read from the client: Health " .. tostring(hv) .. "%, Spirit " .. tostring(sv) .. "%") end

        -- ---- supportCyrisusToRecovery: rounds 10-13 until Spirit 100 (each right answer is 8) ----
        local rounds = {
            { "Not long now and you'll be back on your feet!", "On whether you mind me helping you further." },
            { "You're sounding much better.", "If you need anything, just let me know." },
            { "It's quite cosy in here.", "The perfect environment for getting back on your feet!" },
            { "You're very safe in this little cave.", "The suqah will never fit through that tunnel." },
        }
        for i, r in ipairs(rounds) do
            local n = "supportCyrisusToRecovery-" .. i
            t.exec(n, t.player.talk_to, "dream_cyrisus_melee", 1)
            t.exec(n .. "-menu0", t.chat.drain, { stop_at = "options", max_pages = 12 })
            if i == 1 then
                -- the first talk of the stage still offers the topic menu (dreammentor_gear_talk, armament now full -> straight to the round)
            end
            t.exec(n .. "-first", t.chat.choose, r[1])
            t.exec(n .. "-menu2", t.chat.drain, { stop_at = "options", max_pages = 12 })
            t.exec(n .. "-second", t.chat.choose, r[2])
            t.exec(n .. "-tail", t.chat.drain, { max_pages = 40 })
            t.ticks(2)
            local _, sv = t.var.varbit("varb3622_dream_spirit")
            t.check(n .. "-spirit", sv == 72 + 8 * i or sv == 100, "Spirit read from the client after round " .. (9 + i) .. ": " .. tostring(sv) .. "%")
        end
        t.expect("quest.stage.gear_given", t.quest.expect_stage("gear_given"))

        -- ---- talkAfterHelping: stage 18, 'Ready?' sends him to the Oneiromancer (dreammentor_cyrisus.rs2:~125) ----
        t.exec("talkAfterHelping", t.player.talk_to, "dream_cyrisus_melee", 1)
        t.exec("talkAfterHelping-pages", t.chat.drain, { max_pages = 20 })
        t.ticks(2)
        t.expect("quest.stage.met_oneiromancer", t.quest.expect_stage("met_oneiromancer"))

        -- ---- talkToOneiromancer: 'Cyrisus.' (dragonslayer2.rs2:1547 -> dreammentor_dream.rs2:23) ----
        t.exec("goto-talkToOneiromancer", t.player.goto_tile, 2151, 3867, 0)
        t.exec("talkToOneiromancer", t.player.talk_to, "lunar_oneiromancer", 1)
        t.exec("talkToOneiromancer-menu", t.chat.drain, { stop_at = "options", max_pages = 6 })
        t.exec("talkToOneiromancer-topic", t.chat.choose, "Cyrisus.")
        t.exec("talkToOneiromancer-pages", t.chat.drain, { max_pages = 60 })
        t.ticks(2)
        t.expect("quest.stage.dream_ready", t.quest.expect_stage("dream_ready"))
        t.expect("talkToOneiromancer-vial", t.inv.expect_has("dream_vial_empty", 1))

        do local _, tile = t.world.tile(); local _, level = t.world.level(); local _, stage = t.var.server("varb3618_dream_prog")
            t.check("leg.2.end", true, "quiet: tile " .. tostring(tile and (tile.x .. "," .. tile.z)) .. " level " .. tostring(level) .. ", dream_prog read from the server " .. tostring(stage) .. " (dream_ready), dream vial, goutweed, astral rune, pestle and mortar, tinderbox and seal of passage carried; food all spent") end
        end },
        { name = "dream", run = function(t)
        -- LEG 3 of 3: fillVialWithWater .. returnToOneiromancer. Starts at stage 24 (dream_ready) beside the Oneiromancer.
        -- ---- fillVialWithWater: the vial on the Moon Clan sink (dreammentor_dream.rs2:237) ----
        t.exec("goto-fillVialWithWater", t.player.goto_tile, 2091, 3922, 0)
        do
            local sink = t.player.by_symbol("loc", "lunar_moonclan_sink")
            t.exec("fillVialWithWater", t.player.use_on, "dream_vial_empty", sink)
        end
        t.expect("fillVialWithWater-water", t.inv.await("dream_vial_water", 1, 10))
        t.check("fillVialWithWater-held", select(1, t.inv.has("dream_vial_water")) == "ok", "dream_vial_water in the backpack after the sink")

        -- ---- addGoutweed (dreammentor_dream.rs2:243) ----
        t.exec("addGoutweed", t.player.use_item_on_item, "eadgar_goutweed_herb", "dream_vial_water")
        t.expect("addGoutweed-weed", t.inv.await("dream_vial_weed", 1, 10))

        -- ---- useHammerOnAstralRune (hammer.rs2 shared trigger -> dreammentor_hammer_astral) ----
        t.exec("useHammerOnAstralRune", t.player.use_item_on_item, "hammer", "astralrune")
        t.expect("useHammerOnAstralRune-shards", t.inv.await("dream_astral_shards", 1, 10))

        -- ---- usePestleOnShards (grind_ingredient.rs2 -> dreammentor_grind_shards) ----
        t.exec("usePestleOnShards", t.player.use_item_on_item, "pestle_and_mortar", "dream_astral_shards")
        t.expect("usePestleOnShards-ground", t.inv.await("dream_groundastral", 1, 10))

        -- ---- useGroundAstralOnVial (dreammentor_dream.rs2:254) ----
        t.exec("useGroundAstralOnVial", t.player.use_item_on_item, "dream_groundastral", "dream_vial_weed")
        t.expect("useGroundAstralOnVial-full", t.inv.await("dream_vial_full", 1, 10))

        -- ---- lightBrazier: the tinderbox on the brazier (dreammentor_dream.rs2:298); combat gear worn from setup ----
        t.exec("goto-lightBrazier", t.player.goto_tile, 2073, 3913, 0)
        do
            local brazier = t.player.by_symbol("loc", "lunar_moonclan_brazier_multi")
            t.exec("lightBrazier", t.player.use_on, "tinderbox", brazier)
        end
        t.ticks(2)
        t.expect("lightBrazier-lit", t.msg.expect("You light the brazier"))

        -- the fight needs food: 25 sharks (guide: combat equipment, food) given now that the earlier legs' food is spent
        -- the spent tools go on the floor so the backpack holds more food (28 slots)
        for _, it in ipairs({ "pestle_and_mortar", "tinderbox", "hammer" }) do
            t.exec("drop-" .. it, t.player.drop, it)
        end
        t.ticks(2)
        t.cheat("::give shark 25")
        t.expect("fight-food", t.inv.await("shark", 25, 10))

        -- ---- talkToCyrisusForDream: 'Yes, let's go!' (dreammentor_dream.rs2:225) ----
        t.exec("talkToCyrisusForDream-present", t.npc.await_present, "dream_cyrisus_outsidebraziermulti", 20, 20)
        t.exec("talkToCyrisusForDream", t.player.talk_to, "dream_cyrisus_outsidebraziermulti", 1)
        t.exec("talkToCyrisusForDream-menu", t.chat.drain, { stop_at = "options", max_pages = 12 })
        t.exec("talkToCyrisusForDream-yes", t.chat.choose, "Yes, let's go!")
        t.exec("talkToCyrisusForDream-pages", t.chat.drain, { max_pages = 60 })
        t.ticks(3)
        do local _, tile = t.world.tile()
            t.check("talkToCyrisusForDream-entered", tile ~= nil and tile.x > 6000, "in the dream instance: tile " .. tostring(tile and (tile.x .. "," .. tile.z))) end

        -- ---- the four bosses, fought for real ----
        local names = { { "killInadaquacy", "dream_inadequacy" }, { "killEverlasting", "dream_everlasting" }, { "killUntouchable", "dream_untouchable" }, { "killIllusive", "dream_illusive" } }
        local lowest = 99
        for _, b in ipairs(names) do
            t.exec(b[1] .. "-present", t.npc.await_present, b[2], 40, 40)
            t.exec(b[1], t.player.attack, b[2], 2, 30) -- shot from the magic shortbow worn from setup
            local _, dead_detail = t.exec(b[1] .. "-dead", t.npc.await_dead_engaged, 600, 80, { eat = { item = "shark", below = 75 } })
            local _, hp = t.skill.read("hitpoints")
            local _, sharks = t.inv.count("shark")
            t.check(b[1] .. "-margin", true, "after the kill: lowest hp " .. tostring(string.match(tostring(dead_detail), "lowest hp (%d+/%d+)")) .. ", hitpoints now " .. tostring(hp and hp.level) .. "/" .. tostring(hp and hp.base_level) .. ", sharks left " .. tostring(sharks))
            t.ticks(2)
        end
        t.exec("bosses-pages", t.chat.drain, { max_pages = 60 })
        t.ticks(4)
        t.expect("quest.stage.bosses_defeated", t.quest.expect_stage("bosses_defeated"))

        -- ---- returnToOneiromancer: 'Cyrisus.' finishes (dreammentor_dream.rs2:134) ----
        t.exec("goto-returnToOneiromancer", t.player.goto_tile, 2151, 3867, 0)
        t.exec("returnToOneiromancer", t.player.talk_to, "lunar_oneiromancer", 1)
        t.exec("returnToOneiromancer-menu", t.chat.drain, { stop_at = "options", max_pages = 6 })
        t.exec("returnToOneiromancer-topic", t.chat.choose, "Cyrisus.")
        local _, snapshot = t.skill.snapshot()
        t.exec("returnToOneiromancer-pages", t.chat.drain, { max_pages = 60 })
        t.ticks(3)
        t.expect("reward.hitpoints_xp", t.skill.expect_gain("hitpoints", 15000, snapshot))
        t.expect("reward.magic_xp", t.skill.expect_gain("magic", 10000, snapshot))
        t.expect("reward.lamp", t.inv.expect_has("thosf_reward_lamp", 1))
        t.quest.expect_complete()
        t.finish(0)
        end },
    },
}
