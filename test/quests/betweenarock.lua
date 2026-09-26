-- Between a Rock... -- hand-driven from the generated scaffold, rewritten
-- against the quest's own scripts (server/scripts/quests/quest_betweenarock/)
-- and docs/quests/between_a_rock.md. Tier 1.
--
-- Real prerequisite (betweenarock_shared.rs2's own `dwarfrock_real_prereqs_met`)
-- is ONLY `%fishingcompo >= ^fishingcompo_complete` -- the cache dbrow's two
-- `requirement_quests` values decode to the wrong rows (sheep herder / mage
-- arena 1), and Dwarf Cannon is deliberately soft-skipped in this tree
-- because `%mcannon` never advances past 0 anywhere in the tree (the audit
-- doc's own P0 #1 finding). So setup completes only Fishing Contest.
--
-- Travel between areas is via t.player.goto_tile (::goto) throughout: the
-- troll-stronghold/cave tunnels and both ferrymen are this quest's own
-- flavour transport, but they write no quest state at all (grep-confirmed
-- in betweenarock_travel.rs2), so clicking them proves nothing about the
-- quest and is skipped -- goto_tile lands directly at each NPC/loc that
-- DOES advance state, same convention as the ladder/stairs rule in
-- QUEST_AUTHORING.md section 2.
--
-- The four schematic pieces: Dondakan (from firing the golden cannonball),
-- the lore book's last page (read the book a SECOND time, at stage 80 --
-- entirely missing from the scaffold), the Dwarven Engineer, and Khorvak.
-- "Assemble" opens the REAL per-piece 2D position puzzle (interfaces
-- 113/114, betweenarock_schematics.rs2's parity1e rewrite -- content no
-- longer click-solves it in one opheld1 press): each piece is scrambled
-- off its border target on open, and dwarfrock_puzzle_dx{1,2,3}/dy{1,2,3}
-- (pack/varp.alloc, no client copy) are read back through t.var.server's
-- server-content fallback (landed 03947ac3a) and driven within the 4px
-- tolerance with the dr_move_* buttons -- see the block below for the
-- worked pattern (tools/quest_gate/_scratch/parity1e_betweenarock_puzzle
-- .lua proved the mechanic first). The scaffold's own unresolved
-- PuzzleWrapperStep marker is resolved by driving it, not narrating past
-- it.
--
-- The Arzinian Avatar's category is chosen by the player's strongest
-- combat stat (dwarfrock_realm.rs2's own dwarfrock_spawn_avatar) -- this
-- character is built melee-heavy (attack/strength boosted, ranged/magic
-- left at 1), so the branch taken is the "mage" category, and its COLOUR
-- is picked by gold ore held at the flame (parity1c fix, not random):
-- <15 ore spawns dwarf_rock_avatar_mage_green (level 125), >=15 spawns
-- _mage_yellow (level 75) -- mineGoldOre below stops at 6, so green is
-- tried first, with yellow and the base row as fallbacks.

return {
    id = "betweenarock",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99", -- exceeds the quest's own req_defence=30 gate
        "::setlevel hitpoints 99",
        "::setlevel mining 40", -- exactly the quest's req; real ore/page mining is boostable but this is simplest
        "::setlevel smithing 50", -- exactly the quest's req_smithing=50 gate
        "::complete quest_fishingcontest", -- quest_cheat.rs2's dispatch row is `quest_fishingcontest`, not the quest_fishingcompo folder name; the ONLY prereq dwarfrock_real_prereqs_met checks
        "::give rune_scimitar 1", -- combat prerequisite (Quest Helper: bring a weapon), not the quest's own deliverable
        "::give adamant_pickaxe 1", -- prerequisite for real mining (page 3, realm gold ore); rate 3 vs bronze's 7
                                     -- (skill_mining/configs/pickaxes.obj) -- mining level 40 cannot wield rune (req 41),
                                     -- adamant (req 31) is the fastest this character qualifies for -- RETRY: bronze
                                     -- alone left the realm's 6-ore floor at 2/6 after 6 budgeted attempts
        "::give hammer 1", -- prerequisite for smithing the golden helmet
        "::give gold_bar 4", -- prerequisite: 1 smelted into the golden cannonball, 3 smithed into the golden helmet -- the quest's own use of them is driven for real below
        "::give ammo_mould 1", -- prerequisite for casting the golden cannonball
    },

    run = function(t)
        t.quest.bind({
            varp = "dwarfrock_quest",
            constants = {
                not_started = 0,
                told_of_rock = 10,
                engineer_confirmed = 20,
                gathering_pages = 30,
                book_ready = 40,
                returned_with_book = 50,
                gold_bar_shown = 60,
                fired_into_rock = 70,
                assembling_schematics = 80,
                in_the_realm = 90,
                avatar_defeated = 100,
                complete = 110,
            },
            row = "quest_betweenarock",
            display = "Between a Rock...", -- all.quest.compack's own display text, confirmed by grep
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("equip.scimitar", t.player.equip, "rune_scimitar")

        -- ============================================================
        -- Dondakan #1 -- accept the quest (betweenarock_dondakan.rs2's
        -- dwarfrock_dondakan_talk, %dwarfrock_quest = not_started branch;
        -- real_prereqs_met is true, so the Fishing-Contest refusal branch
        -- above it is skipped).
        -- ============================================================
        t.exec("goto-talkToDondakan", t.player.goto_tile, 2824, 10168, 0)
        t.exec("talkToDondakan", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakan-dialog", t.chat.play, {
            "player:What are you doing firing a cannon at that wall?",
            "npc:Ha! I'm trying to blast my way into the rock",
            "player:So why were you trying to get through the rock again?",
            "npc:I've heard tales of a lost dwarven realm",
            "player:Sounds interesting! Can I help?",
            "npc:You certainly can! Head to Keldagrim",
        })
        t.expect("quest.stage.told_of_rock", t.quest.expect_stage("told_of_rock"))

        -- ============================================================
        -- Dwarven Engineer -- points at Rolad (betweenarock_schematics.rs2).
        -- ============================================================
        t.exec("goto-talkToEngineer", t.player.goto_tile, 2871, 10198, 0)
        t.exec("talkToEngineer", t.player.talk_to, "dwarfrock_engineer1", 1)
        t.exec("talkToEngineer-dialog", t.chat.play, {
            "player:Dondakan sent me -- he's trying to blast his way into a rock.",
            "npc:Ha! A cannonball alone won't crack solid rock like that.",
        })
        t.expect("quest.stage.engineer_confirmed", t.quest.expect_stage("engineer_confirmed"))

        -- ============================================================
        -- Rolad #1 -- accepts the three-page hunt (betweenarock_pages.rs2).
        -- ============================================================
        t.exec("goto-talkToRolad", t.player.goto_tile, 3022, 3452, 0)
        t.exec("talkToRolad", t.player.talk_to, "dwarfrock_rolad", 1)
        t.exec("talkToRolad-dialog", t.chat.play, {
            "player:The Engineer sent me to see you about some old dwarven lore.",
            "npc:Ah, you'll be wanting to know about the old realm",
            "player:I'll be back later.",
            "npc:Bring me back three pages",
        })
        t.expect("quest.stage.gathering_pages", t.quest.expect_stage("gathering_pages"))

        -- ============================================================
        -- Dwarven Mine -- page 3 (mine tin/copper/clay/iron), page 1
        -- (kill a scorpion), page 2 (search the mine cart). All three
        -- auto-combine into dwarf_rock_pagex3 the instant they are all
        -- held (betweenarock_pages.rs2's dwarfrock_try_combine_pages).
        -- m47_153.spawn's own scorpion row (2637..3042,9793..9822) sits
        -- inside the quest's coordinate-gated bounds -- confirmed by grep,
        -- not guessed.
        -- ============================================================
        t.exec("goto-dwarvenMine", t.player.goto_tile, 3042, 9793, 0)

        local tinrock_result, tinrock = t.world.loc_near("tinrock2", 15)
        t.check("locate.tinrock2", tinrock_result == "ok",
            string.format("world.loc_near(tinrock2,15) -> %s %s", tostring(tinrock_result), tostring(tinrock)))
        if tinrock_result == "ok" then
            t.exec("goto-tinrock2", t.player.goto_tile, tinrock.tile_x, tinrock.tile_z, tinrock.level)
        end
        t.exec("mineRock", t.player.click_loc, "tinrock2", 1)
        local page3_await_result, page3_await_detail = t.inv.await("dwarf_rock_page3", 1, 40)
        local page3r, page3c = t.inv.count("dwarf_rock_page3")
        local pagex3r_a, pagex3c_a = t.inv.count("dwarf_rock_pagex3")
        t.check("gotPage3", page3_await_result == "ok" or pagex3c_a == 1,
            string.format("inv.await(dwarf_rock_page3,1,40) -> %s (%s); page3=%s(%s) pagex3=%s(%s)",
                tostring(page3_await_result), tostring(page3_await_detail),
                tostring(page3c), tostring(page3r), tostring(pagex3c_a), tostring(pagex3r_a)))

        local attack1_result, attack1_detail = t.player.attack("scorpion", 2, 20)
        t.check("attackScorpion", attack1_result == "ok" or attack1_result == "timeout", attack1_detail)
        t.exec("killScorpion", t.npc.await_dead, "scorpion", 60)
        local page1_await_result, page1_await_detail = t.inv.await("dwarf_rock_page1", 1, 20)
        local page1r, page1c = t.inv.count("dwarf_rock_page1")
        local pagex3r_b, pagex3c_b = t.inv.count("dwarf_rock_pagex3")
        t.check("gotPage1", page1_await_result == "ok" or pagex3c_b == 1,
            string.format("inv.await(dwarf_rock_page1,1,20) -> %s (%s); page1=%s(%s) pagex3=%s(%s)",
                tostring(page1_await_result), tostring(page1_await_detail),
                tostring(page1c), tostring(page1r), tostring(pagex3c_b), tostring(pagex3r_b)))

        local cart_result, cart = t.world.loc_near("dwarfrock_book_cart", 50)
        t.check("locate.cart", cart_result == "ok",
            string.format("world.loc_near(dwarfrock_book_cart,50) -> %s %s", tostring(cart_result), tostring(cart)))
        if cart_result == "ok" then
            t.exec("goto-cart", t.player.goto_tile, cart.tile_x, cart.tile_z, cart.level)
        end
        t.exec("searchCart", t.player.click_loc, "dwarfrock_book_cart", 1)
        local page2_await_result, page2_await_detail = t.inv.await("dwarf_rock_page2", 1, 20)
        local page2r, page2c = t.inv.count("dwarf_rock_page2")
        local pagex3r_c, pagex3c_c = t.inv.count("dwarf_rock_pagex3")
        t.check("gotPage2", page2_await_result == "ok" or pagex3c_c == 1,
            string.format("inv.await(dwarf_rock_page2,1,20) -> %s (%s); page2=%s(%s) pagex3=%s(%s)",
                tostring(page2_await_result), tostring(page2_await_detail),
                tostring(page2c), tostring(page2r), tostring(pagex3c_c), tostring(pagex3r_c)))

        local pagex3_final_result, pagex3_final_detail = t.inv.await("dwarf_rock_pagex3", 1, 10)
        local pagex3fr, pagex3fc = t.inv.count("dwarf_rock_pagex3")
        t.check("pagesCombined", pagex3_final_result == "ok" and pagex3fc == 1,
            string.format("inv.await(dwarf_rock_pagex3,1,10) -> %s (%s); count=%s(%s)",
                tostring(pagex3_final_result), tostring(pagex3_final_detail), tostring(pagex3fc), tostring(pagex3fr)))

        -- ============================================================
        -- Rolad #2 -- hand in the three pages, get the restored book.
        -- ============================================================
        t.exec("goto-talkToRoladWithPages", t.player.goto_tile, 3022, 3452, 0)
        t.exec("talkToRoladWithPages", t.player.talk_to, "dwarfrock_rolad", 1)
        t.exec("talkToRoladWithPages-dialog", t.chat.play, {
            "player:I found all three pages.",
            "npc:Wonderful! Let me bind them back into the book for you.",
            "mesbox:Rolad hands you the restored dwarven lore book.",
        })
        t.expect("quest.stage.book_ready", t.quest.expect_stage("book_ready"))

        -- ============================================================
        -- Read the book -- first read (opheld1, stage book_ready): a
        -- two-option choice, then the stage-advancing mesbox. The book
        -- is a fresh inv_add from the dialogue that just closed -- await
        -- the backpack sync rather than trust the client copy is already
        -- current.
        -- ============================================================
        local book_sync_result, book_sync_detail = t.inv.await("dwarf_rock_book", 1, 10)
        t.check("bookSynced", book_sync_result == "ok", "inv.await(dwarf_rock_book,1,10) -> " .. tostring(book_sync_result) .. " " .. tostring(book_sync_detail))
        local read1_result, read1_detail = t.player.inv_op("dwarf_rock_book", 1)
        t.check("readBook", read1_result == "ok", "inv_op(dwarf_rock_book,1) -> " .. tostring(read1_result) .. " " .. tostring(read1_detail))
        t.exec("readBook-dialog", t.chat.play, {
            "choose:Read the book.",
            "mesbox:You read through the dwarven lore book.",
        })
        t.expect("quest.stage.returned_with_book", t.quest.expect_stage("returned_with_book"))

        -- inv_op's own op-1 press ALSO fires the backpack cell's shift-
        -- click-drop chain (app_minimenu.c's op_index==1 hook, same seam
        -- elemental_workshop.lua's readBook documents) -- the read itself
        -- landed (the stage row above is its proof), but the book is now
        -- on the ground. Pick it back up: Dondakan #2 below needs it held.
        local pickup1_result, pickup1_detail = t.player.click_obj("dwarf_rock_book", 3)
        t.check("pickBookBackUp", pickup1_result == "ok", "click_obj(dwarf_rock_book,3) -> "
            .. tostring(pickup1_result) .. " " .. tostring(pickup1_detail))
        local book2_await_result, book2_await_detail = t.inv.await("dwarf_rock_book", 1, 10)
        local book2_count_result, book2_count = t.inv.count("dwarf_rock_book")
        t.check("gotBookBack", book2_await_result == "ok" and book2_count == 1,
            "inv.await(dwarf_rock_book,1,10) after pickup -> " .. tostring(book2_await_result) .. " " .. tostring(book2_await_detail)
                .. "; count=" .. tostring(book2_count_result == "ok" and book2_count or book2_count_result))

        -- ============================================================
        -- Dondakan #2 -- the book report (betweenarock_dondakan.rs2's
        -- returned_with_book branch, restored to a real split since
        -- parity1: this talk alone writes stage 60, dwarfrock_gold_cannonball
        -- is a SEPARATE item-on-npc leg below).
        -- ============================================================
        t.exec("goto-talkToDondakanWithBook", t.player.goto_tile, 2824, 10168, 0)
        t.exec("talkToDondakanWithBook", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakanWithBook-dialog", t.chat.play, {
            "player:I've read the whole book. It talks about a fortune in gold",
            "npc:Gold, is it? Ha! That'd explain why I've never managed to crack this rock with a pick",
            "player:Your boots?",
            "npc:Solid granite, these. I could kick a hole clean through a castle wall",
            "npc:If you've got a bit of gold on you, show me.",
        })
        t.expect("quest.stage.gold_bar_shown", t.quest.expect_stage("gold_bar_shown"))

        -- ============================================================
        -- useGoldBarOnDondakan -- item-on-npc, the ONLY writer of
        -- %dwarfrock_gold_cannonball (betweenarock_dondakan.rs2's
        -- [opnpcu,dwarfrock_dondakan] -> dwarfrock_dondakan_gold_bar_shown).
        -- This five-page exchange used to hang the whole client process
        -- dismissing its fourth page (^chat_shock) -- FIXED 2026-09-24
        -- (9bf6b97c5d2): the freeze was a cyclic scenery chain in
        -- painters.c's bucket_paint_world, reached because a runtime-
        -- spawned multiloc child stayed linked into the high-water
        -- painter chain across its SECOND loc_change with the scene still
        -- loaded; release/reset now unlink through
        -- tile_unlink_scenery_element(high_water) instead of leaving a
        -- cycle. Proved fixed end to end before this rewrite --
        -- build/seam_state/seam11/scratch_chatshock/betweenarock_copy.lua
        -- (s11cs_copy1.run.log) drove the identical five-page list and the
        -- whole rest of the quest to 138/0 at the completion scroll with
        -- no t.blocked anywhere in the file.
        -- ============================================================
        local dondakan_book_target = t.player.by_symbol("npc", "dwarfrock_dondakan")
        t.exec("useGoldBarOnDondakan", t.player.use_on, "gold_bar", dondakan_book_target)
        t.exec("useGoldBarOnDondakan-dialog", t.chat.play, {
            "player:Here, take a look at this.",
            "npc:Haha, what am I meant to do with that? Gold's heavy and soft as butter",
            "player:The book said there's gold inside the rock.",
            "npc:Now that's not a bad thought",
            "mesbox:Dondakan agrees to try firing",
        })
        t.exec("useGoldBarOnDondakan.flag", t.var.await_server, "dwarfrock_gold_cannonball", 1, 5)

        -- ============================================================
        -- Furnace -- smelt a gold bar into the golden cannonball
        -- (smelting.rs2's shared switch, intercepted by
        -- dwarfrock_gold_bar_or_menu while this quest is waiting on it).
        -- Keldagrim's smithing quarter (Quest Helper's own WorldPoint for
        -- this step) is where both the anvil and the furnace live -- go
        -- there first, loc_near was searching from Dondakan's alcove.
        -- ============================================================
        t.exec("goto-keldagrimSmithing1", t.player.goto_tile, 2869, 10202, 0)
        local furnace_result, furnace = t.world.loc_near("dwarf_keldagrim_furnace", 60)
        t.check("locate.furnace", furnace_result == "ok",
            string.format("world.loc_near(dwarf_keldagrim_furnace,60) -> %s %s", tostring(furnace_result), tostring(furnace)))
        if furnace_result ~= "ok" then
            t.blocked("test/quests/betweenarock.lua:smeltCannonball -- dwarf_keldagrim_furnace (skill_smithing/configs/"
                .. "smithing_sources.loc) cannot be located by world.loc_near within 60 tiles of Keldagrim's smithing "
                .. "quarter (2869,10202,0, Quest Helper's own WorldPoint for this step) or of the Dwarven Engineer "
                .. "(2871,10198,0) -- no *.loc placement file exists to read its real tile from (trap 20), and every "
                .. "candidate tried answered not_found.")
            return
        end
        t.exec("goto-furnace", t.player.goto_tile, furnace.tile_x, furnace.tile_z, furnace.level)
        local furnace_target = t.player.by_symbol("loc", "dwarf_keldagrim_furnace")
        t.exec("smeltCannonball", t.player.use_on, "gold_bar", furnace_target)
        t.exec("smeltCannonball-dialog", t.chat.play, {
            "mesbox:You melt the gold bar and pour it into the mould",
        })
        local ball_sync_result, ball_sync_detail = t.inv.await("dwarf_rock_cannonball_gold", 1, 10)
        local ball_result, ball_count = t.inv.count("dwarf_rock_cannonball_gold")
        t.check("gotCannonball", ball_sync_result == "ok" and ball_result == "ok" and ball_count == 1,
            "inv.await(dwarf_rock_cannonball_gold,1,10) -> " .. tostring(ball_sync_result) .. " " .. tostring(ball_sync_detail)
                .. "; count=" .. tostring(ball_result == "ok" and ball_count or ball_result))

        -- ============================================================
        -- Use the golden cannonball on Dondakan -- opnpcu, then a
        -- SEPARATE talk_to to actually advance the stage (the opnpcu
        -- handler only sets %dwarfrock_fired_gold_cannonball; the regular
        -- opnpc1 branch reads that flag on the NEXT click). The furnace
        -- above left the player in Keldagrim's smithing quarter, well
        -- outside Dondakan's alcove -- go back first.
        -- ============================================================
        t.exec("goto-dondakanForCannonball", t.player.goto_tile, 2824, 10168, 0)
        local dondakan_target = t.player.by_symbol("npc", "dwarfrock_dondakan")
        t.exec("useCannonballOnDondakan", t.player.use_on, "dwarf_rock_cannonball_gold", dondakan_target)
        -- if_close fires for BOTH branches of this p_choice2 (dondakan.rs2's
        -- dwarfrock_dondakan_fire_cannonball), so the dialogue genuinely
        -- CLOSES after the choice; a new one-page mesbox opens separately
        -- after p_delay(1) -- two chat.play calls, not one list across the
        -- close.
        t.exec("useCannonballOnDondakan-choice", t.chat.play, {
            "choose:Yes, I'm sure this will crack open the rock.",
        })
        -- if_close, THEN inv_del/p_delay(1), THEN the mesbox opens -- a
        -- real one-tick gap between the close above and the next page
        -- mounting, not an immediate continuation.
        t.await({
            level = function() return t.chat.kind() ~= "none" end,
            note = "useCannonballOnDondakan.mesbox_open",
        }, 5)
        t.exec("useCannonballOnDondakan-dialog", t.chat.play, {
            "mesbox:Dondakan loads the golden cannonball and fires it point blank",
        })

        t.exec("talkToDondakanAfterShot", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakanAfterShot-dialog", t.chat.play, {
            "player:So you want to... fire me into the rock?",
            "npc:Precisely! If I can survive being fired through",
            "player:I can't argue with that, shoot me in!",
            "npc:Right then, brace yourself!",
        })
        t.expect("quest.stage.fired_into_rock", t.quest.expect_stage("fired_into_rock"))

        t.exec("talkToDondakanForSchematic", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("talkToDondakanForSchematic-dialog", t.chat.play, {
            "player:What did you find on the other side?",
            "npc:A whole realm, hidden behind the rock!",
            "npc:Head back to the Engineer, and see if Rolad's book",
        })
        t.expect("quest.stage.assembling_schematics", t.quest.expect_stage("assembling_schematics"))
        t.inv.await("dwarf_rock_schematic1", 1, 10)
        local schematic1_result, schematic1_count = t.inv.count("dwarf_rock_schematic1")
        t.check("gotSchematic1", schematic1_result == "ok" and schematic1_count == 1,
            "inv.count(dwarf_rock_schematic1) -> " .. tostring(schematic1_result) .. " " .. tostring(schematic1_count))

        -- ============================================================
        -- Read the book a SECOND time -- the base schematic (stage
        -- assembling_schematics, opheld1's second branch: straight
        -- mesbox, no choice this time).
        -- ============================================================
        local read2_result, read2_detail = t.player.inv_op("dwarf_rock_book", 1)
        t.check("readBookAgain", read2_result == "ok", "inv_op(dwarf_rock_book,1) -> " .. tostring(read2_result) .. " " .. tostring(read2_detail))
        t.exec("readBookAgain-dialog", t.chat.play, {
            "mesbox:You turn to the last page of the book again.",
        })
        t.inv.await("dwarf_rock_base_schematic", 1, 10)
        local base_result, base_count = t.inv.count("dwarf_rock_base_schematic")
        t.check("gotBaseSchematic", base_result == "ok" and base_count == 1,
            "inv.count(dwarf_rock_base_schematic) -> " .. tostring(base_result) .. " " .. tostring(base_count))

        -- ============================================================
        -- Engineer's schematic piece.
        -- ============================================================
        t.exec("goto-talkToEngineerForSchematic", t.player.goto_tile, 2871, 10198, 0)
        t.exec("talkToEngineerForSchematic", t.player.talk_to, "dwarfrock_engineer1", 1)
        t.exec("talkToEngineerForSchematic-dialog", t.chat.play, {
            "player:I need your help piecing together an old dwarven schematic.",
            "npc:Ah, I recognise this work!",
            "mesbox:The Dwarven Engineer hands you a schematic fragment.",
        })
        t.inv.await("dwarf_rock_schematic2", 1, 10)
        local schematic2_result, schematic2_count = t.inv.count("dwarf_rock_schematic2")
        t.check("gotSchematic2", schematic2_result == "ok" and schematic2_count == 1,
            "inv.count(dwarf_rock_schematic2) -> " .. tostring(schematic2_result) .. " " .. tostring(schematic2_count))

        -- ============================================================
        -- Khorvak's schematic piece -- refusing the stout still hands
        -- it over (betweenarock_schematics.rs2's else branch).
        -- ============================================================
        t.exec("goto-talkToKhorvak", t.player.goto_tile, 2864, 9876, 0)
        t.exec("talkToKhorvak", t.player.talk_to, "dwarfrock_engineer2", 1)
        t.exec("talkToKhorvak-dialog", t.chat.play, {
            "player:I'm told you might have a piece of an old dwarven schematic.",
            "npc:Maybe I do, maybe I don't.",
            "choose:No, I've had enough of buying drinks for people!",
            "mesbox:Khorvak laughs and hands over his schematic fragment regardless.",
        })
        t.inv.await("dwarf_rock_schematic3", 1, 10)
        local schematic3_result, schematic3_count = t.inv.count("dwarf_rock_schematic3")
        t.check("gotSchematic3", schematic3_result == "ok" and schematic3_count == 1,
            "inv.count(dwarf_rock_schematic3) -> " .. tostring(schematic3_result) .. " " .. tostring(schematic3_count))

        -- ============================================================
        -- Assemble the four schematic pieces: opheld1 on the held
        -- schematic1 fragment opens the REAL per-piece 2D position
        -- puzzle (interfaces 113/114, betweenarock_schematics.rs2's
        -- parity1e rewrite) -- no more one-click auto-solve. Each piece
        -- is knocked off its border target by a random 12-40px offset
        -- per axis on open (dwarfrock_puzzle_dx{1,2,3}/dy{1,2,3},
        -- betweenarock.varp, scope=temp, allocated in pack/varp.alloc
        -- above the client wire's addressable ceiling), readable
        -- through t.var.server since 03947ac3a landed the driver's
        -- server-content fallback for that shape of varp. The move step
        -- and the solve tolerance are both 4px (betweenarock.constant's
        -- dwarfrock_puzzle_step/tolerance), so floor(|delta|/4) clicks
        -- of the axis button that reduces |delta| toward zero always
        -- lands the piece within tolerance (the remainder is always
        -- 0-3, never >= the 4px tolerance) -- read each axis back and
        -- react to the real value, never force it with ::setvar (trap
        -- 16: solving the puzzle IS the quest's own work).
        -- dwarfrock_schematics_check_solved (the .rs2's own proc)
        -- re-checks all three pieces after every single nudge, so the
        -- solve fires on whichever piece's last correcting click
        -- happens to complete the set -- no separate "confirm" press.
        -- ============================================================
        -- No t.shot/t.check (auto-shooting) rows while this modal is
        -- open: interfaces/dwarf_rock_schematics.if's own
        -- dwarf_rock_black_background is a literal `type=3 fill=yes`
        -- 512x334 BLACK rectangle at the interface's own origin, so the
        -- top-left 64x64 corner of EVERY frame this modal is mounted
        -- reads solid black and matches gate.py's pre_login fingerprint
        -- (measured distance 2.18, threshold 12.0) -- a real content
        -- shape, not a boot stall, and t.drive.camera cannot change a
        -- fully opaque 2D modal's own fill. Evidence for this section is
        -- the real var/inv reads below (t.step, no shot) instead; the
        -- section's one shot is taken once the modal is closed again.
        local assemble_result, assemble_detail = t.player.inv_op("dwarf_rock_schematic1", 1)
        t.step("assembleSchematic", assemble_result == "ok" and "PASS" or "FAIL",
            "inv_op(dwarf_rock_schematic1,1) -> " .. tostring(assemble_result) .. " " .. tostring(assemble_detail))
        local puzzle_open_result, puzzle_open_detail = t.ui.await_open("dwarf_rock_schematics")
        t.step("schematicPuzzle-open", puzzle_open_result == "ok" and "PASS" or "FAIL",
            "ui.await_open(dwarf_rock_schematics) -> " .. tostring(puzzle_open_result) .. " " .. tostring(puzzle_open_detail))

        local sel1_r, w_select1 = t.ui.widget("dwarf_rock_schematics_control:dr_select1")
        local sel2_r, w_select2 = t.ui.widget("dwarf_rock_schematics_control:dr_select2")
        local sel3_r, w_select3 = t.ui.widget("dwarf_rock_schematics_control:dr_select3")
        local up_r, w_up = t.ui.widget("dwarf_rock_schematics_control:dr_move_up")
        local down_r, w_down = t.ui.widget("dwarf_rock_schematics_control:dr_move_down")
        local left_r, w_left = t.ui.widget("dwarf_rock_schematics_control:dr_move_left")
        local right_r, w_right = t.ui.widget("dwarf_rock_schematics_control:dr_move_right")
        local rot_r, w_rotate = t.ui.widget("dwarf_rock_schematics_control:dr_rotate_button")
        t.step("schematicPuzzle-widgets",
            (sel1_r == "ok" and sel2_r == "ok" and sel3_r == "ok"
                and up_r == "ok" and down_r == "ok" and left_r == "ok" and right_r == "ok"
                and rot_r == "ok")
                and "PASS" or "FAIL",
            string.format("select1=%s select2=%s select3=%s up=%s down=%s left=%s right=%s rotate=%s",
                tostring(sel1_r), tostring(sel2_r), tostring(sel3_r),
                tostring(up_r), tostring(down_r), tostring(left_r), tostring(right_r), tostring(rot_r)))

        -- dr_move_left/right subtract/add ^dwarfrock_puzzle_step from dx;
        -- dr_move_up/down subtract/add it from dy
        -- (dwarfrock_puzzle_nudge) -- so a positive delta needs the
        -- "decreasing" button (left / up) and a negative one needs the
        -- "increasing" button (right / down). dr_rotate_button adds 1
        -- (mod 4) to rot for every SELECTED piece (dwarfrock_puzzle_rotate,
        -- betweenarock_schematics.rs2:309-326) -- rotation must reach 0
        -- BEFORE the position matters, since dwarfrock_schematics_check_
        -- solved (:332) refuses on any nonzero rot before it even looks at
        -- dx/dy. Both nudge and rotate act on EVERY currently-selected
        -- piece at once (testbit loops over the whole %dwarfrock_puzzle_
        -- select bitmask), so a piece already finished MUST be deselected
        -- (dr_selectN toggles, it does not just set) before the next
        -- piece is selected, or moving/rotating piece 2 also drags piece
        -- 1's already-correct rot/dx/dy back off zero.
        local puzzle_pieces = {
            { n = 1, select = w_select1, dx = "dwarfrock_puzzle_dx1", dy = "dwarfrock_puzzle_dy1", rot = "dwarfrock_puzzle_rot1" },
            { n = 2, select = w_select2, dx = "dwarfrock_puzzle_dx2", dy = "dwarfrock_puzzle_dy2", rot = "dwarfrock_puzzle_rot2" },
            { n = 3, select = w_select3, dx = "dwarfrock_puzzle_dx3", dy = "dwarfrock_puzzle_dy3", rot = "dwarfrock_puzzle_rot3" },
        }
        for _, piece in ipairs(puzzle_pieces) do
            t.ui.invoke(piece.select, 1) -- select ON (togglebit)

            local before_rot_result, before_rot = t.var.server(piece.rot)
            if before_rot_result == "ok" and before_rot ~= 0 then
                local rot_clicks = (4 - before_rot) % 4
                for _ = 1, rot_clicks do
                    t.ui.invoke(w_rotate, 1)
                end
            end
            -- if_click queues a packet the embedded transport only
            -- delivers on a server tick (net_transport_embed.c) --
            -- QUEST_AUTHORING.md's own budget note and the mourningsend
            -- parti still-minigame precedent both tick between a batch
            -- of ui.invoke presses and the read that grades them;
            -- reading right after the raw clicks (no tick at all)
            -- measured as a stale before==after read on run 1.
            t.ticks(2)
            local after_rot_result, after_rot = t.var.server(piece.rot)

            local before_dx_result, before_dx = t.var.server(piece.dx)
            local before_dy_result, before_dy = t.var.server(piece.dy)
            if before_dx_result == "ok" and before_dx ~= 0 then
                local dx_clicks = math.floor(math.abs(before_dx) / 4)
                local dx_widget = before_dx > 0 and w_left or w_right
                for _ = 1, dx_clicks do
                    t.ui.invoke(dx_widget, 1)
                end
            end
            if before_dy_result == "ok" and before_dy ~= 0 then
                local dy_clicks = math.floor(math.abs(before_dy) / 4)
                local dy_widget = before_dy > 0 and w_up or w_down
                for _ = 1, dy_clicks do
                    t.ui.invoke(dy_widget, 1)
                end
            end
            t.ticks(2)
            local after_dx_result, after_dx = t.var.server(piece.dx)
            local after_dy_result, after_dy = t.var.server(piece.dy)
            t.step("schematicPuzzle-piece" .. piece.n,
                (after_rot_result == "ok" and after_rot == 0
                    and after_dx_result == "ok" and after_dy_result == "ok"
                    and math.abs(after_dx) <= 4 and math.abs(after_dy) <= 4)
                    and "PASS" or "FAIL",
                string.format("select dr_select%d, rotate piece %d: rot %s -> %s, move dx %s -> %s, dy %s -> %s (tolerance 4)",
                    piece.n, piece.n, tostring(before_rot), tostring(after_rot),
                    tostring(before_dx), tostring(after_dx), tostring(before_dy), tostring(after_dy)))

            t.ui.invoke(piece.select, 1) -- select OFF (toggle back) before the next piece
        end

        local solved_result, solved_value = t.var.server("dwarfrock_schematics_solved")
        t.step("schematicPuzzle-solved", (solved_result == "ok" and solved_value == 1) and "PASS" or "FAIL",
            "var.server(dwarfrock_schematics_solved) -> " .. tostring(solved_result) .. " " .. tostring(solved_value))

        -- dwarf_rock_close_button (interfaces/dwarf_rock_schematics.if)
        -- is buttontype=3 (REVCONFIG_BUTTON_TYPE_CLOSE): its if_click
        -- answers ok every time without unmounting the interface (trap
        -- 33), so ESCAPE is the real close -- the same
        -- app->host.close_modal_requested path t.shop.close uses. Left
        -- open, the schematics modal stayed mounted through every later
        -- goto/dialogue row in run 1's ledger (110-... through
        -- 118-goto-keldagrimSmithing2 all still showed it on screen).
        local close_key_result = t.key("escape")
        t.step("schematicPuzzle-closeKey", close_key_result == "ok" and "PASS" or "FAIL",
            "key(escape) -> " .. tostring(close_key_result))
        local closed_result, closed_detail = t.ui.await_close("dwarf_rock_schematics")
        t.check("schematicPuzzle-closed", closed_result == "ok",
            "ui.await_close(dwarf_rock_schematics) -> " .. tostring(closed_result) .. " " .. tostring(closed_detail))

        t.inv.await("dwarf_rock_schematic_assembled", 1, 10)
        local assembled_result, assembled_count = t.inv.count("dwarf_rock_schematic_assembled")
        t.check("gotAssembledSchematic", assembled_result == "ok" and assembled_count == 1,
            "inv.count(dwarf_rock_schematic_assembled) -> " .. tostring(assembled_result) .. " " .. tostring(assembled_count))

        -- ============================================================
        -- Golden helmet -- 3 gold bars on the anvil (oplocu). Back to
        -- Keldagrim's smithing quarter first -- Khorvak's talk above left
        -- the player under White Wolf Mountain, nowhere near it.
        -- ============================================================
        t.exec("goto-keldagrimSmithing2", t.player.goto_tile, 2869, 10202, 0)
        local anvil_result, anvil = t.world.loc_near("dwarf_keldagrim_anvil", 60)
        t.check("locate.anvil", anvil_result == "ok",
            string.format("world.loc_near(dwarf_keldagrim_anvil,60) -> %s %s", tostring(anvil_result), tostring(anvil)))
        if anvil_result ~= "ok" then
            t.blocked("test/quests/betweenarock.lua:smithHelmet -- dwarf_keldagrim_anvil (skill_smithing/configs/"
                .. "smithing_sources.loc) cannot be located by world.loc_near within 60 tiles of Keldagrim's smithing "
                .. "quarter (2869,10202,0, Quest Helper's own WorldPoint for this step) -- no *.loc placement file "
                .. "exists to read its real tile from (trap 20), and every candidate tried answered not_found.")
            return
        end
        t.exec("goto-anvil", t.player.goto_tile, anvil.tile_x, anvil.tile_z, anvil.level)
        local anvil_target = t.player.by_symbol("loc", "dwarf_keldagrim_anvil")
        t.exec("smithHelmet", t.player.use_on, "gold_bar", anvil_target)
        t.exec("smithHelmet-dialog", t.chat.play, {
            "mesbox:You carefully hammer three gold bars into a golden helmet.",
        })
        local helmet_sync_result, helmet_sync_detail = t.inv.await("dwarf_goldrock_helmet", 1, 10)
        t.check("helmetSynced", helmet_sync_result == "ok", "inv.await(dwarf_goldrock_helmet,1,10) -> " .. tostring(helmet_sync_result) .. " " .. tostring(helmet_sync_detail))

        -- ============================================================
        -- Dondakan #3 -- fire into the realm. dondakan.rs2's own stage-80
        -- branch checks `inv_total(inv, dwarf_goldrock_helmet) = 0` (the
        -- BACKPACK container) before it checks `inv_total(worn, ...)` --
        -- equipping first would empty that backpack copy and could make
        -- the "smith one" refusal fire again even though one is worn, so
        -- probe with the helmet still held (unworn) first, THEN equip.
        -- ============================================================
        t.exec("goto-talkToDondakanForRealm", t.player.goto_tile, 2824, 10168, 0)
        t.exec("talkToDondakanForRealm-probe", t.player.talk_to, "dwarfrock_dondakan", 1)
        local probe_drain_result, probe_drain_detail = t.chat.drain({ stop_at = "none" })
        t.expect("talkToDondakanForRealm-probe-drain", probe_drain_result, probe_drain_detail)

        t.exec("equip.helmet", t.player.equip, "dwarf_goldrock_helmet")

        t.exec("talkToDondakanForRealm", t.player.talk_to, "dwarfrock_dondakan", 1)
        local realm_kind = t.chat.kind()
        if realm_kind ~= "player" then
            local unexpected_text_result, unexpected_text = t.chat.text()
            t.check("talkToDondakanForRealm-unexpected", true,
                "chat.kind() -> " .. tostring(realm_kind) .. "; chat.text() -> " .. tostring(unexpected_text_result)
                    .. " " .. tostring(unexpected_text))
            t.blocked("test/quests/betweenarock.lua:talkToDondakanForRealm -- betweenarock_dondakan.rs2's stage-80 "
                .. "branch (dwarfrock_dondakan_talk) did not reach its final \"Ready as I'll ever be.\" case with "
                .. "dwarf_rock_schematic_assembled held, dwarf_goldrock_helmet worn (equip.helmet answered ok) and "
                .. "Defence 99 >= the 30 gate -- it opened a '" .. tostring(realm_kind) .. "' page instead ('"
                .. tostring(unexpected_text) .. "'). The two guards immediately above that line, "
                .. "`inv_total(inv, dwarf_goldrock_helmet) = 0` (line ~373, the BACKPACK container) and "
                .. "`inv_total(worn, dwarf_goldrock_helmet) = 0` (line ~381, the EQUIPMENT container), are "
                .. "mutually exclusive for the same item -- equipping the helmet empties the backpack copy the "
                .. "first guard wants, so no single state can satisfy both.")
            return
        end
        t.exec("talkToDondakanForRealm-dialog", t.chat.play, {
            "player:Ready as I'll ever be.",
            "npc:You may fire when ready!",
        })
        t.expect("quest.stage.in_the_realm", t.quest.expect_stage("in_the_realm"))
        t.exec("enterRealm-dialog", t.chat.play, {
            "mesbox:Dondakan fires you clean through the rock!",
        })

        -- ============================================================
        -- The Arzinian realm -- mine at least 6 gold ore, then face the
        -- guardian at the central wall of flame.
        -- ============================================================
        t.settle()
        local goldrock_sym = "goldrock1"
        local goldrock_result, goldrock = t.world.loc_near("goldrock1", 60)
        if goldrock_result ~= "ok" then
            goldrock_sym = "goldrock2"
            goldrock_result, goldrock = t.world.loc_near("goldrock2", 60)
        end
        t.check("locate.goldrock", goldrock_result == "ok",
            string.format("world.loc_near(%s,60) -> %s %s", goldrock_sym, tostring(goldrock_result), tostring(goldrock)))
        if goldrock_result ~= "ok" then
            t.blocked("test/quests/betweenarock.lua:mineGoldOre -- neither goldrock1 nor goldrock2 "
                .. "(server/scripts/skill_mining/scripts/mining.rs2's generic mining rocks) can be located by "
                .. "world.loc_near within 60 tiles of the realm landing tile -- no *.loc placement file exists to "
                .. "read a real tile from (trap 20), and both candidates answered not_found.")
            return
        end
        t.exec("goto-goldrock", t.player.goto_tile, goldrock.tile_x, goldrock.tile_z, goldrock.level)

        local ore_attempts = 0
        local ore_result, ore_count = t.inv.count("gold_ore")
        while (ore_result ~= "ok" or ore_count < 6) and ore_attempts < 30 do
            ore_attempts = ore_attempts + 1
            -- await ONE MORE ore than we currently hold, not a flat 6 -- the realm's
            -- 8-minute budget (^dwarfrock_realm_start_timeleft=16 * ^dwarfrock_realm_tick_period=50)
            -- cannot absorb six attempts each burning the full 60-tick timeout waiting on a
            -- target this single click can never reach in one swing.
            local before_ore_result, before_ore_count = t.inv.count("gold_ore")
            t.exec("mineGoldOre-" .. ore_attempts, t.player.click_loc, goldrock_sym, 1)
            t.inv.await("gold_ore", (before_ore_count or 0) + 1, 30)
            ore_result, ore_count = t.inv.count("gold_ore")
        end
        t.check("gotGoldOre", ore_result == "ok" and ore_count >= 6,
            "inv.count(gold_ore) after " .. tostring(ore_attempts) .. " mineGoldOre attempt(s) -> "
                .. tostring(ore_result) .. " " .. tostring(ore_count))

        -- The "central wall of flame" is dozens of placements of both
        -- dwarf_firewall_centre_straight AND dwarf_firewall_centre_diagonal
        -- (all.loc.compack 5979/5980, m36_77.jl2) sharing the one oploc1
        -- body (@dwarfrock_face_avatar). The nearest STRAIGHT copy the
        -- resolve picks is a wall face this driver's pixel hunt never lands
        -- a press on from inside the ring -- measured twice, ~438 ticks of
        -- retried sides/poses before giving up, which alone blows most of
        -- the realm's own 8-minute budget (^dwarfrock_realm_start_timeleft=16
        -- * ^dwarfrock_realm_tick_period=50 = ~800 ticks) and got the
        -- player ejected mid-fight on the run that tried it first. So try
        -- the nearest DIAGONAL copy FIRST (it lands cleanly every time
        -- measured), STRAIGHT only as a fallback, then t.drive.op
        -- (section 3/8's documented last resort for a press that has
        -- failed from every side/pose already tried) as the final one --
        -- the op itself is real for every copy of either symbol.
        local firewall_result, firewall = t.world.loc_near("dwarf_firewall_centre_diagonal", 60)
        t.check("locate.firewall", firewall_result == "ok",
            string.format("world.loc_near(dwarf_firewall_centre_diagonal,60) -> %s %s", tostring(firewall_result), tostring(firewall)))
        if firewall_result ~= "ok" then
            firewall_result, firewall = t.world.loc_near("dwarf_firewall_centre_straight", 60)
        end
        if firewall_result ~= "ok" then
            t.blocked("test/quests/betweenarock.lua:approachFlame -- neither dwarf_firewall_centre_diagonal nor "
                .. "dwarf_firewall_centre_straight "
                .. "(server/scripts/quests/quest_betweenarock/scripts/betweenarock_realm.rs2) can be located by "
                .. "world.loc_near within 60 tiles of the realm's gold-ore area -- no *.loc placement file exists "
                .. "to read a real tile from (trap 20), and both candidates answered not_found.")
            return
        end
        t.exec("goto-firewall", t.player.goto_tile, firewall.tile_x, firewall.tile_z, firewall.level)

        local approach_loc = firewall
        local approach_result, approach_detail = t.exec("approachFlame", t.player.click_loc, "dwarf_firewall_centre_diagonal", 1)
        if approach_result ~= "ok" then
            local straight_result, straight = t.world.loc_near("dwarf_firewall_centre_straight", 60)
            t.check("locate.firewall-straight", straight_result == "ok",
                string.format("world.loc_near(dwarf_firewall_centre_straight,60) -> %s %s", tostring(straight_result), tostring(straight)))
            if straight_result == "ok" then
                approach_loc = straight
                t.exec("goto-firewall-straight", t.player.goto_tile, straight.tile_x, straight.tile_z, straight.level)
                approach_result, approach_detail = t.exec("approachFlame-straight", t.player.click_loc, "dwarf_firewall_centre_straight", 1)
            end
        end
        if approach_result ~= "ok" then
            approach_result, approach_detail = t.exec("approachFlame-bypass", t.drive.op, approach_loc, 1)
        end
        t.exec("approachFlame-dialog", t.chat.play, {
            "mesbox:The flames roar and a guardian of the realm steps forth",
        })

        -- dwarfrock_spawn_avatar's own npc_add always lands the Avatar at the
        -- FIXED coord 0_37_77_7_25 (section 6's ^*_coord decode: level 0,
        -- region 37,77, local 7,25 -> worldX 37*64+7=2375, worldZ 77*64+25=4953),
        -- independent of which wall face (straight or diagonal, tiles apart
        -- around the ring) actually triggered the spawn -- stand there before
        -- polling for it rather than trusting whichever tile the successful
        -- approachFlame press happened to leave us on.
        t.exec("goto-avatarSpawn", t.player.goto_tile, 2375, 4953, 0)

        -- The avatar's colour is random(3) across three symbols in the
        -- melee-countered "mage" category (this character is built
        -- melee-heavy) -- try each until one is present.
        local avatar_candidates = { "dwarf_rock_avatar_mage", "dwarf_rock_avatar_mage_green", "dwarf_rock_avatar_mage_yellow" }
        local avatar_sym = nil
        local avatar_present_result = "not_found"
        for _, candidate in ipairs(avatar_candidates) do
            avatar_present_result = t.npc.await_present(candidate, 10, 10)
            if avatar_present_result == "ok" then
                avatar_sym = candidate
                break
            end
        end
        t.check("avatarPresent", avatar_sym ~= nil,
            "tried " .. table.concat(avatar_candidates, ", ") .. " -> resolved " .. tostring(avatar_sym)
                .. " (" .. tostring(avatar_present_result) .. ")")

        if avatar_sym == nil then
            t.blocked("test/quests/betweenarock.lua:approachFlame -- dwarf_firewall_centre_straight spawned the "
                .. "Arzinian Avatar (dwarfrock_realm.rs2's dwarfrock_spawn_avatar), but none of the three "
                .. "melee-countered variants (dwarf_rock_avatar_mage/_green/_yellow) are present in the npc pool "
                .. "within 10 tiles/10 ticks of the click -- npc.await_present tried all three and every one "
                .. "answered " .. tostring(avatar_present_result) .. ".")
            return
        end

        -- t.npc.await_dead credited a false kill twice on this build --
        -- both runs it answered "dead" within 9 ticks while the Avatar was
        -- still fully visible on screen at nonzero hp (killAvatar shots,
        -- both runs; dwarfrock_quest stayed at 90 forever afterward). The
        -- seam pass's own working proof (build/quest_gate/seam6_realm_fight2,
        -- 13/13 PASS) polls the QUEST VARP directly instead of trusting
        -- npc-pool presence for this hand-spawned, type-specific-op2-binding
        -- boss, so do the same: keep pressing Attack (content's own
        -- [label,player_melee_attack] re-arms the swing loop every
        -- attackrate from one click) and poll dwarfrock_quest for
        -- ^dwarfrock_avatar_defeated=100 after each, rather than asking the
        -- npc pool whether it is still there.
        local kill_attempts = 0
        local kill_stage_result = "refused"
        while kill_stage_result ~= "ok" and kill_attempts < 6 do
            kill_attempts = kill_attempts + 1
            local atk_result, atk_detail = t.player.attack(avatar_sym, 2, 30)
            t.check("attackAvatar-" .. kill_attempts, atk_result == "ok" or atk_result == "timeout", atk_detail)
            kill_stage_result = t.var.await_server("dwarfrock_quest", 100, 15)
        end
        t.check("killAvatar", kill_stage_result == "ok",
            "dwarfrock_quest var.await_server(...,100,15) after " .. tostring(kill_attempts)
                .. " attackAvatar attempt(s) -> " .. tostring(kill_stage_result))

        if kill_stage_result ~= "ok" then
            t.blocked("test/quests/betweenarock.lua:killAvatar -- dwarfrock_quest never reached "
                .. "^dwarfrock_avatar_defeated=100 after " .. tostring(kill_attempts) .. " attackAvatar attempt(s) "
                .. "polling var.await_server, though the seam pass proved this exact fight winnable "
                .. "(build/quest_gate/seam6_realm_fight2, 13/13 PASS) and t.npc.await_dead credited a kill twice on "
                .. "this quest file while the Avatar was still visibly standing at nonzero hp on screen -- a driver "
                .. "seam in how await_dead tracks this hand-spawned, type-specific-op2-binding npc, not a content "
                .. "bug.")
            return
        end

        t.expect("quest.stage.avatar_defeated", t.quest.expect_stage("avatar_defeated"))
        t.exec("avatarDefeated-dialog", t.chat.play, {
            "mesbox:The guardian collapses!",
        })

        -- ============================================================
        -- Reward snapshot before the hand-in, then finish the quest.
        -- ============================================================
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local rune_pickaxe_before_result, rune_pickaxe_before = t.inv.count("rune_pickaxe")

        t.exec("goto-finishQuest", t.player.goto_tile, 2824, 10168, 0)
        t.exec("finishQuest", t.player.talk_to, "dwarfrock_dondakan", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:It's done -- the guardian is defeated.",
            "npc:By my beard! You actually did it!",
        })

        t.quest.expect_complete()

        t.check("reward.defence", t.skill.expect_gain("defence", 5000, reward_before))
        t.check("reward.mining", t.skill.expect_gain("mining", 5000, reward_before))
        t.check("reward.smithing", t.skill.expect_gain("smithing", 5000, reward_before))
        local rune_pickaxe_after_result, rune_pickaxe_after = t.inv.count("rune_pickaxe")
        t.check("reward.rune_pickaxe",
            rune_pickaxe_before_result == "ok" and rune_pickaxe_after_result == "ok"
                and rune_pickaxe_after == rune_pickaxe_before + 1,
            string.format("rune_pickaxe %s -> %s (want +1), reads %s/%s",
                tostring(rune_pickaxe_before), tostring(rune_pickaxe_after),
                tostring(rune_pickaxe_before_result), tostring(rune_pickaxe_after_result)))

        t.finish(0)
    end,
}
