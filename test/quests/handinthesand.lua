-- The Hand in the Sand (handinthesand). Quest Helper:
-- helpers/quests/thehandinthesand/TheHandInTheSand.java. Sources:
-- quests/quest_handinthesand/scripts/*.rs2 (bert, guard, rarve, betty, sandy, mazion).
--
-- Travel (door rule, b71): every closed space is entered and left by its own door, every island
-- by its boat, every members' gate pressed (reach.py / comp.py / locs_near.py, maps of b71):
--   * Lumbridge -> Yanille: the only walk is through the Taverley members' gate (arena.lua):
--     overland to 2934,3318 (REACH 387), membergatel 2934,3320 pressed, overland on (REACH 1042).
--   * Bert's house is a 12-tile room behind poordoor 2551,3098 (south wall; outside 2551,3097),
--     the pub (Guard Captain) a room behind poshdoor 2551,3082 (north wall; outside 2551,3083):
--     pass_door in and out on every visit.
--   * The bell zogre_outdoor_bell 2598,3085 is a solid tile: rung from 2598,3084, which comp.py puts
--     in the open component of Yanille's streets (2551,3083 -> 2598,3084 REACH 62).
--   * Yanille -> Brimhaven: captain_barnaby at Ardougne 2678,3275 (REACH 315 from Yanille, 30 coins,
--     deck 2775,3234,1, ardougneshipplank_off ashore; legends.lua). Brimhaven -> Ardougne:
--     captain_barnaby_karamja 2772,3229 (30 coins, deck 2683,3263,1, brimhavenshipplank_off; totem.lua).
--   * Sandy's office (handsand_desk 2788,3174 is solid) is entered by its doorway: poshdooropen
--     2790,3177, placed open by the map (m43_49), pass_door reads it standing open.
--   * Port Sarim (Rarve's teleport, handsand_rarve.rs2:160) -> Brimhaven: seaman_lorris 3028,3221
--     (30 coins, deck 2956,3143,1, sarimshipplank_off), the Karamja members' gate membergatel 2816,3182.
--   * Entrana: Yanille -> the Taverley gate southward (membergater 2933,3320, seaslug.lua) -> the
--     Port Sarim shipmonk 3045,3236 (deck 2834,3331,1, ship_from_entrana_off), Mazion 2818,3342
--     (REACH 25); back by shipmonk2 2832,3336 (deck 3048,3231,1, ship_to_entrana_off) and the gate
--     northward to Yanille. Nothing in the pack is a weapon or armour (monk_of_entrana.rs2 search).
-- Setup's 120 coins pay four 30-coin fares (Barnaby, Barnaby Karamja twice, Lorris).
return {
    id = "handinthesand",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel thieving 40",
        "::setlevel crafting 49",
        "::give beer 1",
        "::give vial_empty 2",
        "::give redberries 1",
        "::give white_berries 1",
        "::give bullseye_lantern_lens 1",
        "::give earthrune 5",
        "::give bucket_sand 1",
        "::give coins 120", -- four 30-coin fares: Ardougne->Brimhaven, Brimhaven->Ardougne x2, Port Sarim->Karamja
    },
    run = function(t)
        t.quest.bind({
            varp = "varb1527_handsand_quest",
            constants = { not_started = 0, have_hand = 10, beer_given = 20, hand_given_to_rarve = 30,
                have_berts_rota = 40, have_both_rotas = 50, have_scroll = 60, orb_given = 70,
                have_truth_serum = 80, sandy_distracted = 90, serum_used = 100, orb_activated = 110,
                interrogation_done = 120, orb_handed_in = 130, pit_enchanted = 140,
                have_wizards_head = 150, complete = 160 },
            display = "The Hand in the Sand",
            points = 1,
        })
        t.ticks(3)

        local function tile_text(r, tt)
            if r ~= "ok" or type(tt) ~= "table" then
                return tostring(r) .. " " .. tostring(tt)
            end
            return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
        end
        local function coins_now()
            local _, c = t.inv.count("coins")
            return c or 0
        end
        local function door(name, closed, open, at, near, far)
            t.exec(name, t.player.pass_door, { closed = closed, open = open, at = at, near = near, far = far })
        end
        -- Bert's house: poordoor 2551,3098 (south wall), outside 2551,3097, inside 2551,3099.
        local function bert_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2551, 3097, 0)
            door(name, "poordoor", "poordooropen", { 2551, 3098, 0 }, { 2551, 3097 }, { 2551, 3099 })
        end
        local function bert_out(name)
            door(name, "poordoor", "poordooropen", { 2551, 3098, 0 }, { 2551, 3099 }, { 2551, 3097 })
        end
        -- Sandy's office: its doorway's leaf poshdooropen 2790,3177 is placed open by the map.
        local function sandy_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2790, 3179, 0)
            door(name, "poshdoor", "poshdooropen", { 2790, 3177, 0 }, { 2790, 3178 }, { 2790, 3175 })
        end
        local function sandy_out(name)
            door(name, "poshdoor", "poshdooropen", { 2790, 3177, 0 }, { 2790, 3175 }, { 2790, 3179 })
        end
        -- A paid sailing: talk, the fare's chat, the arrival mesbox after p_delay(2) + telejump.
        local function deck_check(name, x, z, coins_before)
            local dr, deck = t.world.tile()
            local coins = coins_now()
            t.check(name, dr == "ok" and deck.level == 1 and math.abs(deck.x - x) <= 2 and math.abs(deck.z - z) <= 2
                    and coins_before - coins == 30,
                "tile " .. tile_text(dr, deck) .. " (want the deck " .. x .. "," .. z .. ",1), coins "
                    .. tostring(coins_before) .. " -> " .. tostring(coins) .. " (want -30)")
        end
        local function sail_wait(name)
            local r, d = t.await({
                level = function() return t.chat.kind() == "mesbox" end,
                note = name .. ": the arrival mesbox after p_delay(2) + telejump",
            }, 15)
            t.step(name .. ".sail", r == "ok" and "PASS" or "FAIL",
                "await(chat.kind() == mesbox) -> " .. tostring(r) .. " " .. tostring(d))
        end
        -- Ardougne -> Brimhaven: captain_barnaby (ardougne_east_thin.rs2, legends.lua).
        local function barnaby_to_brimhaven(name)
            t.exec("goto-" .. name .. ".barnaby", t.player.goto_tile, 2678, 3275, 0)
            local c0 = coins_now()
            t.exec(name .. ".barnaby", t.player.talk_to, "captain_barnaby", 1)
            t.exec(name .. ".barnaby-dialog", t.chat.play, {
                "npc:Do you want to go on a trip to Brimhaven?",
                "npc:The trip will cost you 30 coins.",
                "options",
                "choose:Yes please.",
                "player:Yes please.",
            })
            t.expect(name .. ".barnaby.paid", t.msg.expect("pay the 30 coins and board the ship"))
            sail_wait(name .. ".barnaby")
            t.exec(name .. ".barnaby.arrive", t.chat.play, { "mesbox:The ship arrives at Brimhaven." })
            deck_check(name .. ".barnaby.onDeck", 2775, 3234, c0)
            t.exec(name .. ".disembark", t.player.climb, { loc = "ardougneshipplank_off", op_name = "Cross",
                at = { 2774, 3234, 1 }, dest = { 2772, 3234, 0 }, slack = 2 })
        end
        -- Brimhaven -> Ardougne: captain_barnaby_karamja (totem.lua).
        local function barnaby_to_ardougne(name)
            t.exec("goto-" .. name .. ".barnabyKaramja", t.player.goto_tile, 2772, 3229, 0)
            local c0 = coins_now()
            t.exec(name .. ".barnabyKaramja", t.player.talk_to, "captain_barnaby_karamja", 1)
            t.exec(name .. ".barnabyKaramja.d1", t.chat.drain, { stop_at = "options" })
            t.exec(name .. ".barnabyKaramja.c1", t.chat.choose, "I'd like to go to Ardougne.")
            t.exec(name .. ".barnabyKaramja.d2", t.chat.drain, {})
            t.expect(name .. ".barnabyKaramja.sailMessage", t.msg.expect("You board the ship and sail to Ardougne."))
            t.ticks(4)
            deck_check(name .. ".barnabyKaramja.onDeck", 2683, 3263, c0)
            t.exec(name .. ".disembark", t.player.climb, { loc = "brimhavenshipplank_off", op_name = "Cross",
                at = { 2683, 3264, 1 }, dest = { 2683, 3266, 0 }, slack = 2 })
        end
        -- The Taverley members' gate membergater 2933,3320 (gates.rs2 walk-through, seaslug.lua).
        local function taverley_gate(name, northward)
            if northward then
                t.exec("goto-" .. name, t.player.goto_tile, 2933, 3317, 0)
                t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
                    near = { 2933, 3319 }, far_ok = function(tile) return tile.z >= 3320 end,
                    far_desc = "north of the members' gate, z >= 3320" })
            else
                t.exec("goto-" .. name, t.player.goto_tile, 2933, 3323, 0)
                t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
                    near = { 2933, 3321 }, far_ok = function(tile) return tile.z <= 3319 end,
                    far_desc = "south of the members' gate, z <= 3319" })
            end
        end

        -- Bert: Lumbridge -> the Taverley gate -> Yanille, into Bert's house
        taverley_gate("talkToBert.memberGate", true)
        bert_in("talkToBert.houseIn")
        t.exec("talkToBert", t.player.talk_to, "handsand_bert", 1)
        t.exec("bert.o1", t.chat.drain, { stop_at = "options" })
        t.exec("choose-What have you found?", t.chat.choose, "What have you found?")
        t.exec("bert.o2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Why haven't you told", t.chat.choose, "Why haven't you told the authorities?")
        t.exec("bert.o3", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Sure-give-hand", t.chat.choose, "Sure, I'll give you a hand.")
        t.exec("bert.o4", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Yes.", t.chat.choose, "Yes.")
        t.exec("bert.tail", t.chat.drain, {})
        t.expect("quest.stage.have_hand", t.quest.expect_stage("have_hand"))
        do
            t.inv.await("handsand_sandyhand", 1, 6)
            local hr, hc = t.inv.count("handsand_sandyhand")
            t.check("bert.hand", hr == "ok" and hc == 1, "handsand_sandyhand" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- lose the hand, get it back from Bert
        t.ticks(2)
        t.exec("bert.dropHand", t.player.drop, "handsand_sandyhand")
        t.ticks(2)
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_sandyhand")
            t.check("bert.hand_dropped", hr == "ok" and hc == 0, "handsand_sandyhand" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.exec("bert.again", t.player.talk_to, "handsand_bert", 1)
        t.exec("bert.lost.o", t.chat.drain, { stop_at = "options" })
        t.exec("choose-It seems to have sli", t.chat.choose, "It seems to have slipped through my fingers!")
        t.exec("bert.lost.tail", t.chat.drain, {})
        do
            t.inv.await("handsand_sandyhand", 1, 6)
            local hr, hc = t.inv.count("handsand_sandyhand")
            t.check("bert.hand_regiven", hr == "ok" and hc == 1, "handsand_sandyhand" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- Guard captain: out of Bert's house, into the pub by poshdoor 2551,3082 (north wall)
        bert_out("giveCaptainABeer.houseOut")
        t.exec("goto-giveCaptainABeer.pubIn", t.player.goto_tile, 2551, 3083, 0)
        door("giveCaptainABeer.pubIn", "poshdoor", "poshdooropen", { 2551, 3082, 0 }, { 2551, 3083 }, { 2551, 3081 })
        t.exec("giveCaptainABeer", t.player.talk_to, "handsand_guard_captain", 1)
        t.exec("guard.tail", t.chat.drain, {})
        t.expect("quest.stage.beer_given", t.quest.expect_stage("beer_given"))
        do
            t.inv.await("handsand_beerhand", 1, 6)
            local hr, hc = t.inv.count("handsand_beerhand")
            t.check("guard.beerhand", hr == "ok" and hc == 1, "handsand_beerhand" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- Bell, Rarve: out of the pub, the bell rung from 2598,3084 (the bell's own tile is solid)
        door("ringBell.pubOut", "poshdoor", "poshdooropen", { 2551, 3082, 0 }, { 2551, 3081 }, { 2551, 3083 })
        t.exec("goto-bell", t.player.goto_tile, 2598, 3084, 0)
        t.exec("ringBell", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBell.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBell.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBell.tail", t.chat.drain, {})
        t.expect("quest.stage.hand_given_to_rarve", t.quest.expect_stage("hand_given_to_rarve"))

        bert_in("talkToBertAboutRota.houseIn")
        t.exec("talkToBertAboutRota", t.player.talk_to, "handsand_bert", 1)
        t.exec("rota.tail", t.chat.drain, {})
        t.expect("quest.stage.have_berts_rota", t.quest.expect_stage("have_berts_rota"))
        bert_out("talkToBertAboutRota.houseOut")

        -- Sandy: Ardougne's boat to Brimhaven; first talk with Bert's rota, then desk, then pickpocket
        barnaby_to_brimhaven("searchSandysDesk")
        sandy_in("searchSandysDesk.officeIn")
        t.exec("sandy.firstTalk", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.firstTalk.tail", t.chat.drain, {})
        t.exec("searchSandysDesk", t.player.click_loc, "handsand_desk", 1)
        t.ticks(2)
        t.expect("quest.stage.have_both_rotas", t.quest.expect_stage("have_both_rotas"))
        do
            t.inv.await("handsand_rota_sandy", 1, 6)
            local hr, hc = t.inv.count("handsand_rota_sandy")
            t.check("desk.rota", hr == "ok" and hc == 1, "handsand_rota_sandy" .. " count=" .. tostring(hc) .. " want 1")
        end
        local tries = 0
        local r, c = t.inv.count("handsand_sand")
        while c == 0 and tries < 30 do
            tries = tries + 1
            t.exec("pickpocketSandy." .. tries, t.player.press, "handsand_sandy", 3, 6)
            t.ticks(4)
            r, c = t.inv.count("handsand_sand")
        end
        do
            t.inv.await("handsand_sand", 1, 6)
            local hr, hc = t.inv.count("handsand_sand")
            t.check("pickpocketSandy.sand", hr == "ok" and hc == 1, "handsand_sand" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.note("pickpocket attempts=" .. tries)

        -- Back to Bert: out of the office, Barnaby's boat to Ardougne, overland to Yanille
        sandy_out("talkToBertAboutScroll.officeOut")
        barnaby_to_ardougne("talkToBertAboutScroll")
        bert_in("talkToBertAboutScroll.houseIn")
        t.exec("talkToBertAboutScroll", t.player.talk_to, "handsand_bert", 1)
        t.exec("scroll.tail", t.chat.drain, {})
        t.expect("quest.stage.have_scroll", t.quest.expect_stage("have_scroll"))
        do
            t.inv.await("handsand_scroll_magic", 1, 6)
            local hr, hc = t.inv.count("handsand_scroll_magic")
            t.check("scroll.item", hr == "ok" and hc == 1, "handsand_scroll_magic" .. " count=" .. tostring(hc) .. " want 1")
        end
        bert_out("talkToBertAboutScroll.houseOut")

        t.exec("goto-bell2", t.player.goto_tile, 2598, 3084, 0)
        t.exec("ringBellAgain", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellAgain.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellAgain.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellAgain.tail", t.chat.drain, {})
        t.expect("quest.stage.orb_given", t.quest.expect_stage("orb_given"))
        do
            t.inv.await("handsand_orb_storage", 1, 6)
            local hr, hc = t.inv.count("handsand_orb_storage")
            t.check("orb.item", hr == "ok" and hc == 1, "handsand_orb_storage" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- Rarve: lost orb option + help (teleport)
        t.exec("rarve.dropOrb", t.player.drop, "handsand_orb_storage")
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_orb_storage")
            t.check("rarve.orb_gone", hr == "ok" and hc == 0, "handsand_orb_storage" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.exec("rarve.lostorb", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("rarve.lostorb.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("rarve.lostorb.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("rarve.lostorb.o", t.chat.drain, { stop_at = "options" })
        t.exec("choose-I've lost my magical", t.chat.choose, "I've lost my magical scrying orb!")
        t.exec("rarve.lostorb.tail", t.chat.drain, {})
        do
            t.inv.await("handsand_orb_storage", 1, 6)
            local hr, hc = t.inv.count("handsand_orb_storage")
            t.check("rarve.orb_regiven", hr == "ok" and hc == 1, "handsand_orb_storage" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("rarve.help", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("rarve.help.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("rarve.help.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("rarve.help.o", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Can you help me more", t.chat.choose, "Can you help me more?")
        t.exec("rarve.help.o2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Yes-that-would-be-great", t.chat.choose, "Yes, that would be great!")
        t.exec("rarve.help.tail", t.chat.drain, {})
        t.ticks(3)
        local rx, tile = t.world.tile()
        t.check("rarve.teleported", rx == "ok" and tile.x > 2990 and tile.x < 3030, "tile " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))

        -- Betty
        t.exec("talkToBetty", t.player.talk_to, "betty", 1)
        t.exec("betty.offer", t.chat.drain, {})
        do
            t.inv.await("handsand_bottle_water", 1, 6)
            local hr, hc = t.inv.count("handsand_bottle_water")
            t.check("betty.water", hr == "ok" and hc == 1, "handsand_bottle_water" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("addRedberries", t.player.use_item_on_item, "redberries", "handsand_bottle_water")
        do
            t.inv.await("handsand_redberry_juice", 1, 6)
            local hr, hc = t.inv.count("handsand_redberry_juice")
            t.check("addRedberries.juice", hr == "ok" and hc == 1, "handsand_redberry_juice" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("addWhiteberries", t.player.use_item_on_item, "white_berries", "handsand_redberry_juice")
        do
            t.inv.await("handsand_pink_dye", 1, 6)
            local hr, hc = t.inv.count("handsand_pink_dye")
            t.check("addWhiteberries.dye", hr == "ok" and hc == 1, "handsand_pink_dye" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("useDyeOnLanternLens", t.player.use_item_on_item, "handsand_pink_dye", "bullseye_lantern_lens")
        do
            t.inv.await("handsand_rose_lens", 1, 6)
            local hr, hc = t.inv.count("handsand_rose_lens")
            t.check("lens.rose", hr == "ok" and hc == 1, "handsand_rose_lens" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("talkToBettyAgain", t.player.talk_to, "betty", 1)
        t.exec("betty.vial", t.chat.drain, {})
        t.var.expect("varb1537_handsand_counter_multi", 1)
        -- Betty's open doorway (^handsand_doorway_coord 0_47_50_8_59 = 3016,3259), walked to inside the shop
        t.exec("walk-doorway", t.player.walk_to, 3016, 3259, 20)
        t.ticks(2)
        local lt = t.player.by_symbol("loc", "handsand_counter_multiloc")
        -- NOTE: use_on walks into walk_near range (2) of the counter 3 tiles away and leaves the doorway 3016,3259; the press is composed from arm + click_minimenu so the player holds the doorway tile (handsand_betty.rs2:243 aplocu allows range 4)
        local inv_before_r, inv_before = t.player._inv_contents()
        t.player._show_backpack_painted()
        local ar, acell = t.player._inv_cell("handsand_rose_lens")
        local arm_r, arm_d = t.player._arm_held("handsand_rose_lens", acell)
        t.check("useLensOnCounter.arm", arm_r == "ok", "arm " .. tostring(arm_r) .. " " .. tostring(arm_d))
        t.exec("useLensOnCounter", t.drive.click_minimenu, lt, "select")
        t.ticks(3)
        t.exec("useLensOnCounter.tail", t.chat.drain, {})
        do
            local pr, pt = t.world.tile()
            t.check("doorway.tile", pr == "ok" and pt.x == 3016 and pt.z == 3259,
                "after the press the player stands at " .. tile_text(pr, pt) .. " (doorway is 3016,3259)")
        end
        do
            -- The composed press's effect, read the way use_on reads its own (_inv_contents_diff):
            -- handsand_counter_use takes the lens and gives the truth serum.
            t.inv.await("handsand_truthserum", 1, 6)
            local inv_after_r, inv_after = t.player._inv_contents()
            local diff = (inv_before_r == "ok" and inv_after_r == "ok")
                and t.player._inv_contents_diff(inv_before, inv_after) or ""
            t.check("useLensOnCounter.effect", diff:find("lost handsand_rose_lens 1%->0") ~= nil
                    and diff:find("handsand_truthserum 0%->1") ~= nil,
                "[backpack: " .. diff .. "] (want lost handsand_rose_lens 1->0, gained handsand_truthserum 0->1)")
        end
        t.exec("lens.serum", t.inv.await, "handsand_truthserum", 1, 6)
        do
            t.inv.await("handsand_truthserum", 1, 6)
            local hr, hc = t.inv.count("handsand_truthserum")
            t.check("lens.serum.count", hr == "ok" and hc == 1, "handsand_truthserum" .. " count=" .. tostring(hc) .. " want 1")
        end
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_rose_lens")
            t.check("lens.gone", hr == "ok" and hc == 0, "handsand_rose_lens" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.var.expect("varb1537_handsand_counter_multi", 2)
        t.exec("talkToBettyOnceMore", t.player.talk_to, "betty", 1)
        t.exec("betty.sand", t.chat.drain, {})
        t.expect("quest.stage.have_truth_serum", t.quest.expect_stage("have_truth_serum"))
        t.var.expect("varb1532_handsand_serum", 5)

        -- Sandy: out of Betty's doorway, seaman_lorris's boat to Musa Point (sailors.rs2
        -- karamja_sailor_pay, deck 2956,3143,1), the gangplank, the Karamja members' gate, the office
        t.exec("talkToSandyWithPotion.shopOut", t.player.walk_to, 3018, 3259, 20)
        t.exec("goto-talkToSandyWithPotion.seaman", t.player.goto_tile, 3028, 3221, 0)
        do
            local c0 = coins_now()
            t.exec("talkToSandyWithPotion.seaman", t.player.talk_to, "seaman_lorris", 1)
            t.exec("talkToSandyWithPotion.seaman-dialog", t.chat.play, {
                "npc:Do you want to go on a trip to Karamja?",
                "npc:The trip will cost you 30 coins.",
                "options",
                "choose:Yes please.",
                "player:Yes please.",
            })
            t.expect("talkToSandyWithPotion.seaman.paid", t.msg.expect("pay the 30 coins and board the ship"))
            sail_wait("talkToSandyWithPotion.seaman")
            t.exec("talkToSandyWithPotion.seaman.arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
            deck_check("talkToSandyWithPotion.seaman.onDeck", 2956, 3143, c0)
        end
        t.exec("talkToSandyWithPotion.disembark", t.player.climb, { loc = "sarimshipplank_off", op_name = "Cross",
            at = { 2956, 3144, 1 }, src = { 2956, 3143 }, dest = { 2956, 3146, 0 }, slack = 1 })
        t.exec("goto-talkToSandyWithPotion.karamjaGate", t.player.goto_tile, 2818, 3182, 0)
        t.exec("talkToSandyWithPotion.karamjaGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
            near = { 2817, 3182 }, far_ok = function(tile) return tile.x <= 2815 end,
            far_desc = "west of the gate on the Brimhaven side, x <= 2815" })
        sandy_in("talkToSandyWithPotion.officeIn")
        local n = 0
        local rs, st = t.quest.stage()
        while st < 90 and n < 25 do
            n = n + 1
            t.exec("talkToSandyWithPotion." .. n, t.player.talk_to, "handsand_sandy", 1)
            t.exec("sandy.d" .. n, t.chat.drain, { stop_at = "options" })
            t.exec("choose-But the pygmy shrews", t.chat.choose, "But the pygmy shrews have eaten all the sand!")
            t.exec("sandy.d" .. n .. ".tail", t.chat.drain, {})
            rs, st = t.quest.stage()
        end
        t.note("distraction attempts=" .. n)
        t.expect("quest.stage.sandy_distracted", t.quest.expect_stage("sandy_distracted"))
        local ct = t.player.by_symbol("loc", "handsand_coffee_multiloc")
        t.exec("useSerumOnCoffee", t.player.use_on, "handsand_truthserum", ct)
        t.ticks(2)
        t.expect("quest.stage.serum_used", t.quest.expect_stage("serum_used"))
        t.exec("activateMagicalOrb", t.player.inv_op, "handsand_orb_storage", 1)
        t.ticks(2)
        t.expect("quest.stage.orb_activated", t.quest.expect_stage("orb_activated"))
        do
            t.inv.await("handsand_orb_recording", 1, 6)
            local hr, hc = t.inv.count("handsand_orb_recording")
            t.check("orb.recording", hr == "ok" and hc == 1, "handsand_orb_recording" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("interrogateSandy", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.q1", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Why is Bert's rota d", t.chat.choose, "Why is Bert's rota different from the original?")
        t.exec("sandy.q1.t", t.chat.drain, {})
        t.exec("interrogateSandy2", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.q2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Why doesn't Bert rem", t.chat.choose, "Why doesn't Bert remember the change in his hours?")
        t.exec("sandy.q2.t", t.chat.drain, {})
        t.exec("interrogateSandy3", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.q3", t.chat.drain, { stop_at = "options" })
        t.exec("choose-What happened to the", t.chat.choose, "What happened to the wizard?")
        t.exec("sandy.q3.t", t.chat.drain, {})
        t.expect("quest.stage.interrogation_done", t.quest.expect_stage("interrogation_done"))

        -- Rarve: out of the office, Barnaby's boat to Ardougne, overland to the bell; orb, runes, sand
        sandy_out("ringBellAfterInterrogation.officeOut")
        barnaby_to_ardougne("ringBellAfterInterrogation")
        t.exec("goto-bell3", t.player.goto_tile, 2598, 3084, 0)
        t.exec("ringBellAfterInterrogation", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellAfterInterrogation.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellAfterInterrogation.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellAfterInterrogation.tail", t.chat.drain, {})
        t.expect("quest.stage.orb_handed_in", t.quest.expect_stage("orb_handed_in"))
        t.exec("ringBellWithItems", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellWithItems.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellWithItems.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellWithItems.tail", t.chat.drain, {})
        t.expect("quest.stage.pit_enchanted", t.quest.expect_stage("pit_enchanted"))

        -- Entrana: south through the Taverley gate to Port Sarim, the shipmonk's boat (monk_of_entrana.rs2
        -- search: nothing in the pack is a weapon or armour), the gangplank ashore, Mazion
        taverley_gate("talkToMazion.memberGate", false)
        t.exec("goto-talkToMazion.shipmonk", t.player.goto_tile, 3045, 3236, 0)
        t.exec("talkToMazion.shipmonk", t.player.talk_to, "shipmonk", 1)
        t.exec("talkToMazion.shipmonk-dialog", t.chat.play, {
            "npc:Do you seek passage to holy Entrana?",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes, okay, I'm ready to go.",
            "npc:Very well. One moment please.",
            "mesbox:The monk quickly searches you.",
        })
        t.await({ level = function()
            local r, tt = t.world.tile()
            return r == "ok" and tt.level == 1 and tt.x < 2900
        end, note = "talkToMazion: the sail to Entrana" }, 12)
        do
            local dr, dk = t.world.tile()
            t.check("talkToMazion.onDeckAtEntrana", dr == "ok" and dk.level == 1 and math.abs(dk.x - 2834) <= 2
                    and math.abs(dk.z - 3331) <= 2,
                "t.world.tile() -> " .. tile_text(dr, dk) .. " (want the deck, p_telejump(1_44_52_18_3) = 2834,3331,1)")
        end
        t.exec("talkToMazion.gangplank", t.player.climb, { loc = "ship_from_entrana_off", op_name = "Cross",
            at = { 2834, 3333, 1 }, dest = { 2834, 3335, 0 }, slack = 1 })
        t.exec("goto-mazion", t.player.goto_tile, 2819, 3341, 0)
        t.exec("talkToMazion", t.player.talk_to, "handsand_naziom", 1)
        t.exec("mazion.o1", t.chat.drain, { stop_at = "options" })
        t.exec("choose-What did you find?", t.chat.choose, "What did you find?")
        t.exec("mazion.o2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Could I take a look ", t.chat.choose, "Could I take a look at it?")
        t.exec("mazion.o3", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Thank you.", t.chat.choose, "Thank you.")
        t.exec("mazion.tail", t.chat.drain, {})
        t.expect("quest.stage.have_wizards_head", t.quest.expect_stage("have_wizards_head"))
        do
            t.inv.await("handsand_wizhead", 1, 6)
            local hr, hc = t.inv.count("handsand_wizhead")
            t.check("mazion.head", hr == "ok" and hc == 1, "handsand_wizhead" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("mazion.dropHead", t.player.drop, "handsand_wizhead")
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_wizhead")
            t.check("mazion.head_gone", hr == "ok" and hc == 0, "handsand_wizhead" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.exec("mazion.again", t.player.talk_to, "handsand_naziom", 1)
        t.exec("mazion.lost", t.chat.drain, { stop_at = "options" })
        t.exec("choose-I've lost my head!", t.chat.choose, "I've lost my head!")
        t.exec("mazion.lost.tail", t.chat.drain, {})
        do
            t.inv.await("handsand_wizhead", 1, 6)
            local hr, hc = t.inv.count("handsand_wizhead")
            t.check("mazion.head_back", hr == "ok" and hc == 1, "handsand_wizhead" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- Off Entrana by shipmonk2 (areas/entrana/scripts/monk_of_entrana.rs2: deck 3048,3231,1 at Port
        -- Sarim), the gangplank, north through the Taverley gate, overland to the bell
        t.exec("goto-ringBellEnd.shipmonk2", t.player.goto_tile, 2832, 3336, 0)
        t.exec("ringBellEnd.leaveEntrana", t.player.talk_to, "shipmonk2", 1)
        t.exec("ringBellEnd.leaveEntrana-dialog", t.chat.play, {
            "npc:Do you wish to leave holy Entrana?",
            "choose:Yes, I'm ready to go.",
            "player:Yes, I'm ready to go.",
            "npc:Okay, let's board",
        })
        t.await({ level = function()
            local r, tt = t.world.tile()
            return r == "ok" and tt.level == 1 and tt.x > 3000
        end, note = "ringBellEnd: the sail to Port Sarim" }, 12)
        do
            local sr, st = t.world.tile()
            t.check("ringBellEnd.onDeckAtPortSarim", sr == "ok" and st.level == 1
                    and math.abs(st.x - 3048) <= 2 and math.abs(st.z - 3231) <= 2,
                "t.world.tile() -> " .. tile_text(sr, st) .. " (want the deck, p_telejump(1_47_50_40_31) = 3048,3231,1)")
        end
        t.exec("ringBellEnd.gangplank", t.player.climb, { loc = "ship_to_entrana_off", op_name = "Cross",
            at = { 3048, 3232, 1 }, dest = { 3048, 3234, 0 }, slack = 1 })
        taverley_gate("ringBellEnd.memberGate", true)
        t.exec("goto-bell4", t.player.goto_tile, 2598, 3084, 0)
        local snap_r, snap = t.skill.snapshot()
        t.exec("ringBellEnd", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellEnd.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellEnd.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellEnd.tail", t.chat.drain, {})
        t.quest.expect_complete()
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1000, snap))
        t.check("reward.crafting", t.skill.expect_gain("crafting", 9000, snap))
        t.finish(0)
    end,
}
