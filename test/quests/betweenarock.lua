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
-- "Assemble" is a single opheld1 use on the held schematic1 fragment
-- (betweenarock_schematics.rs2's own documented simplification of the
-- native drag/rotate puzzle interface, not a puzzle this engine can drive) --
-- the scaffold's own unresolved PuzzleWrapperStep marker is resolved by
-- that comment, not a missing verb.
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
        -- ============================================================
        -- Stops at THREE entries deliberately (not the whole five-page
        -- exchange): chat.play's own post-loop wait -- run after ANY call
        -- whose last entry was actually clicked, waiting for that click's
        -- own answer (chat.lua's QD.chat._await_page_ready, called at
        -- line ~1123 regardless of whether the list "ends" mid-exchange or
        -- not) -- hangs the WHOLE CLIENT PROCESS the moment its target
        -- transition is "the fourth page closes and the mesbox opens".
        -- Proved identically four separate times (sonnet-b17 runs 1-4,
        -- notebook): --timeout 400, 2000, 250, and 250-with-
        -- TORIRSSERVER_VERBOSE=1 all killed the process at the BYTE-
        -- IDENTICAL last log line. The verbose run shows the SERVER did
        -- its job -- RESUME_PAUSEBUTTON 231:5 -> VARP_LARGE (the
        -- %dwarfrock_gold_cannonball write) + 2x IF_SETTEXT + IF_OPENSUB +
        -- RUNCLIENTSCRIPT + IF_SETEVENTS + SERVER_TICK_END, the exact same
        -- packet shape this same run's talkToRoladWithPages-dialog drives
        -- cleanly a few rows earlier for the identical "chatplayer ->
        -- chatnpc_anim -> [var writes, no if_close] -> mesbox" content
        -- shape (betweenarock_pages.rs2's book-ready branch) -- but the
        -- CLIENT never processes it: TIMEOUT.png shows the fourth page
        -- still on screen with its own "Click here to continue" prompt,
        -- and chat.lua's own wait is tick-bounded at 8 ticks and never
        -- even times out, meaning the tick clock itself stops advancing.
        -- That rules out a Lua-level retry/workaround -- nothing after the
        -- freeze point can run, including a bare t.chat.kind()/continue_()
        -- read tried directly (run 4). Not fixable from this file
        -- (script/plugins/quest_driver/chat.lua and src/ are out of
        -- scope) -- reported as a driver/engine seam, first exercised by
        -- this quest's own brand-new useGoldBarOnDondakan content leg
        -- (OSRS-Content parity1, 0ee0ee5e9d).
        local dondakan_book_target = t.player.by_symbol("npc", "dwarfrock_dondakan")
        t.exec("useGoldBarOnDondakan", t.player.use_on, "gold_bar", dondakan_book_target)
        t.exec("useGoldBarOnDondakan-dialog", t.chat.play, {
            "player:Here, take a look at this.",
            "npc:Haha, what am I meant to do with that? Gold's heavy and soft as butter",
            "player:The book said there's gold inside the rock.",
        })
        local blocking_page_kind = t.chat.kind()
        local blocking_page_text_result, blocking_page_text = t.chat.text()
        t.check("useGoldBarOnDondakan-npc-p4", blocking_page_kind == "npc"
            and blocking_page_text_result == "ok" and type(blocking_page_text) == "string"
            and blocking_page_text:find("Now that's not a bad thought", 1, true) ~= nil,
            "chat.kind() -> " .. tostring(blocking_page_kind) .. "; chat.text() -> "
                .. tostring(blocking_page_text_result) .. " " .. tostring(blocking_page_text))
        t.blocked("test/quests/betweenarock.lua:useGoldBarOnDondakan -- betweenarock_dondakan.rs2's "
            .. "[opnpcu,dwarfrock_dondakan] -> dwarfrock_dondakan_gold_bar_shown label's fourth page "
            .. "(\"...Now that's not a bad thought...\", ^chat_shock) is up and reads correctly (row above: "
            .. "kind=npc, text matches), but dismissing it -- t.chat.continue_ directly, or any chat.play call "
            .. "whose last list entry is this page -- hangs the whole client process indefinitely (reproduced "
            .. "identically across four separate runs; see the file:line comment just above and this quest's "
            .. "author notebook, build/author_state/sonnet-b17/betweenarock.author.progress.md, runs 1-4, for "
            .. "the full client.log evidence). No further row of this quest can be driven past this point.")
        return
    end,
}
