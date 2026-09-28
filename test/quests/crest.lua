-- Family Crest. Hand-authored against OSRS-Content/osrs239-content/server/
-- scripts/quests/quest_crest/scripts/*.rs2 (crest_quest.rs2, crest_dimintheis.rs2,
-- crest_caleb.rs2, crest_avan.rs2, crest_boot.rs2, crest_witchaven.rs2,
-- crest_johnathon.rs2, crest_chronozon.rs2, crest_journal.rs2) and the real
-- shop/npc content that backs Caleb's fish-salad task (areas/area_catherby/
-- scripts/harry.rs2, shop/catherby/scripts/harrys_fishing_shop.rs2,
-- shop/fishing_guild/scripts/fishing_guild_shop.rs2, shop/jatizso/scripts/
-- keepa_kettilons_store.rs2). `new_quest.py`'s own scaffold guessed the wrong
-- progress varp (crest_spells_levers_gauntlets, which is only the spell/
-- lever/gauntlet BITFIELD) and a stale "::skipboss not landed" stop long
-- before the real seam -- discarded; every row below was read from the .rs2
-- source and the live shop tables, not guessed.
--
-- THE ACTUAL SEAM (found here, not in last_failure): the real OSRS quest has
-- you visit the Al-Kharid Gem Trader between Caleb and Avan -- his dialogue
-- there sets %crestquest to ^crest_spoken_gem_trader, which is the ONLY
-- thing that ever opens Avan's "busy" gate (crest_avan.rs2 avan_talk: `if
-- (%crestquest <= ^crest_caleb_where) { "What? Can't you see I'm busy?";
-- return; }` -- crest_spoken_gem_trader = 5 is one past crest_caleb_where =
-- 4, the only way past). This content pack's gem_trader.rs2
-- (areas/alkharid/scripts/gem_trader.rs2) is a stock stub -- "Would you be
-- interested in buying some gems?" / "The gem trader has nothing to trade
-- with yet." -- with NO %crestquest branch at all, and nothing else in the
-- tree ever writes ^crest_spoken_gem_trader (grep across every .rs2:
-- only crest_avan.rs2 READS it, crest_journal.rs2 reads it for the journal
-- text, and crest_selftest.rs2's debugproc MIRRORS the write by hand,
-- exactly the placeholder shape the selftest's own banner says is not real
-- content). So Avan is permanently stuck refusing the player past Caleb's
-- exchange, and nothing beyond that point (Boot, the perfect gold, the
-- Witchaven lever puzzle, Johnathon, Chronozon) can be reached through the
-- real client. Driven for real up to and including proving the gate is
-- shut on both sides (the gem trader's own dialogue and Avan's own refusal
-- at the live tile), then t.blocked.
--
-- Caleb's fish-salad task is driven for real too (rule c). Every shop
-- below was picked by checking wiki/shop_stock.csv's own stock column
-- first (a shop can list an item on its wiki page with baseline stock 0
-- -- the Fishing Guild Shop's own fish rows, raw AND cooked, are ALL 0;
-- only its tools have real stock -- and a buy against that refuses
-- "stocks 0 X" with no hint that the fix is a different shop): Bass
-- bought COOKED from the Warrior Guild Food Shop (stock 10), Swordfish
-- from Legends' Guild General Store/Fionella (stock 20, an upper floor),
-- Tuna and Salmon both from Keepa Kettilon's store on Jatizso (stock 20
-- each, one visit) -- all three real, wired ~openshop shops with
-- confirmed nonzero stock. Shrimp is the one fish with no cooked seller
-- anywhere in wiki/shop_stock.csv, so it is fished for real at Catherby's
-- own net spot
-- (0_44_53_saltfish, op1 Small Net) at the fixture's untouched Fishing
-- level 1 -- deliberately never raised above 15 before this catch, because
-- fishing_catch.dbrow authors the Anchovy row (level 15) ahead of Shrimp
-- (level 1) in the same family, so a higher level risks catching Anchovies
-- forever instead. Cooked on a real fire (tinderbox+logs, the hero.lua/
-- eadgar.lua idiom) after ::setlevel cooking so the one raw shrimp caught
-- does not burn (cooking_generic_shrimp's successchance is only 128/512 at
-- level 1).

return {
    id = "crest",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give coins 120000", -- bring-along currency: Fishing Guild Shop's swordfish/tuna/bass and Keepa Kettilon's salmon, all real purchases below
        "::give tinderbox 1", -- bring-along tool: lighting the fire that cooks the caught shrimp
        "::give logs 3",
        "::setlevel cooking 40", -- bring-along skill level (not an item): keeps the one raw shrimp caught below from burning (cooking_generic_shrimp.dbrow successchance 128/512 at level 1)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "crestquest",
            constants = {
                not_started = 0,
                spoken_dimintheis = 1,
                spoken_caleb = 2,
                caleb_piece = 3,
                caleb_where = 4,
                spoken_gem_trader = 5,
                spoken_avan = 6,
                spoken_boot = 7,
                avan_piece = 8,
                spoken_johnathon = 9,
                cured_johnathon = 10,
                complete = 11,
            },
            row = "quest_familycrest",
            display = "Family Crest",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats' effect is not client-side yet
        t.expect("crest.reset", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- Dimintheis, south east Varrock (crest_dimintheis.rs2 spawn row
        -- m51_53.spawn:21, 3279,3404,0). %crestquest=0 falls to the bottom
        -- default branch: "Hello, My name is Dimintheis..." -> a 3-way
        -- choice -> "Hi, I am a bold adventurer." -> crest_dimintheis_
        -- adventurer's own 3-way -> "So where is this crest?" ->
        -- crest_dimintheis_where's reveal (3 npc pages) -> a 2-way ->
        -- "Ok, I will help you." -> crest_dimintheis_accept sets
        -- %crestquest=spoken_dimintheis.
        -- ---------------------------------------------------------------
        t.exec("goto-dimintheis", t.player.goto_tile, 3279, 3404, 0)
        t.exec("dimintheis.greet", t.player.talk_to, "dimintheis")
        t.exec("dimintheis.accept", t.chat.play, {
            "npc:My name is Dimintheis, of the noble family Fitzharmon.",
            "choose:Hi, I am a bold adventurer.",
            "player:Hi, I am a bold adventurer.",
            "npc:An adventurer hmmm?",
            "choose:So where is this crest?",
            "player:So where is this crest?",
            "npc:my three sons took it with them",
            "npc:the battle to save Varrock",
            "npc:Caleb is alive and well",
            "choose:Ok, I will help you.",
            "player:Ok, I will help you.",
            "npc:I thank you greatly adventurer",
        })
        t.chat.close()
        t.expect("dimintheis.started", t.quest.expect_stage("spoken_dimintheis"))

        -- ---------------------------------------------------------------
        -- Caleb Fitzharmon, Catherby (crest_caleb.rs2 spawn row
        -- m44_53.spawn:10, 2819,3451,0). %crestquest=spoken_dimintheis ->
        -- caleb_fitzharmon_start: "Who are you?" -> 3-way -> "Are you Caleb
        -- Fitzharmon?" -> caleb_fitzharmon_areyou (npc+player narrative,
        -- no choice) -> a 2-way -> "So can I have your bit?" ->
        -- caleb_fitzharmon_bit (npc+player narrative) -> a 2-way ->
        -- "Ok, I will get those." sets %crestquest=spoken_caleb.
        -- ---------------------------------------------------------------
        t.exec("goto-caleb", t.player.goto_tile, 2819, 3451, 0)
        t.exec("caleb.greet", t.player.talk_to, "caleb_fitzharmon")
        t.exec("caleb.accept", t.chat.play, {
            "npc:Who are you? What are you after?",
            "choose:Are you Caleb Fitzharmon?",
            "player:Are you Caleb Fitzharmon?",
            "npc:Why... yes I am",
            "player:I have been sent by your father",
            "npc:Ah... well... hmmm",
            "choose:So can I have your bit?",
            "player:So can I have your bit?",
            "npc:I am the oldest son",
            "player:It's not really much use",
            "npc:Well that is true",
            "npc:so if you will assist me",
            "player:So what ingredients are you missing?",
            "npc:I require the following cooked fish",
            "choose:Ok, I will get those.",
            "player:Ok, I will get those.",
            "npc:You will? It would help me a lot!",
        })
        t.chat.close()
        t.expect("caleb.fishTaskGiven", t.quest.expect_stage("spoken_caleb"))

        -- ---------------------------------------------------------------
        -- Gather the five cooked fish for real. Harry's Fishing Shop
        -- (Catherby, harry.rs2/harrys_fishing_shop.rs2, spawn row
        -- m44_53.spawn:12, 2834,3445,0) for a Small fishing net.
        -- ---------------------------------------------------------------
        t.exec("goto-harry", t.player.goto_tile, 2834, 3445, 0)
        t.exec("harry.open", t.shop.open, "harry", 3, "fishingshop2")
        t.exec("harry.buyNet", t.shop.buy, "net", 1)
        t.check("harry.close", t.shop.close() == "ok", "shop.close()")

        -- Small Net the Catherby saltfish spot (0_44_53_saltfish, op1
        -- Small Net, spawn row m44_53.spawn:13, 2836,3431,0) -- Fishing
        -- level is still the fixture's untouched 1, so only the Shrimp row
        -- (levelrequired=1) of fishing_catch_shrimp/fishing_catch_anchovies'
        -- shared family is eligible; the catch always lands raw_shrimp.
        t.exec("goto-shrimpSpot", t.player.goto_tile, 2836, 3431, 0)
        t.exec("catchShrimp", t.player.talk_to, "0_44_53_saltfish")
        local shrimp_result, shrimp_detail = t.inv.await("raw_shrimp", 1, 60)
        t.step("catchShrimp.caught", shrimp_result == "ok" and "PASS" or "FAIL",
            "inv.await(raw_shrimp, 1, 60) -> " .. tostring(shrimp_result) .. " " .. tostring(shrimp_detail))

        -- Light a fire and cook the shrimp on it (hero.lua's own idiom).
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        local fire_msg_result, fire_msg_detail = t.msg.await("The fire catches", 15)
        t.step("fire.lit", fire_msg_result == "ok" and "PASS" or "FAIL",
            "msg.await('The fire catches', 15) -> " .. tostring(fire_msg_result) .. " " .. tostring(fire_msg_detail))
        t.ticks(2)
        local fire_lookup_result, fire_row = t.world.loc_near("fire", 10)
        t.check("lookup.fire", fire_lookup_result == "ok" and fire_row ~= nil,
            "world.loc_near(fire, 10) -> " .. tostring(fire_lookup_result))
        t.exec("cookShrimp", t.player.use_on, "raw_shrimp", fire_row)
        local _, shrimp_cooked_count = t.inv.count("shrimp")
        if shrimp_cooked_count < 1 then
            -- Burnt (cooking_generic_shrimp successchance 128/512 at level
            -- 40 is still not certain) -- no second raw shrimp on hand, so
            -- go fish one more and try again once.
            t.exec("catchShrimp2", t.player.talk_to, "0_44_53_saltfish")
            local shrimp2_result = t.inv.await("raw_shrimp", 1, 60)
            t.step("catchShrimp2.caught", shrimp2_result == "ok" and "PASS" or "FAIL", "inv.await(raw_shrimp, 1, 60)")
            t.exec("cookShrimp2", t.player.use_on, "raw_shrimp", fire_row)
        end
        t.expect("shrimp.cooked", t.inv.expect_has("shrimp", 1))

        -- RUN 1 correction (see notebook): the Fishing Guild Shop's own
        -- fish rows (raw AND cooked) all carry baseline stock 0 in
        -- wiki/shop_stock.csv -- only its tools (net, rod, harpoon...) have
        -- real stock; buying any fish there refuses "stocks 0". Re-picked
        -- three shops with confirmed NONZERO baseline stock instead:
        --
        -- Warrior Guild Food Shop (warrior_guild_food_shop.rs2, wired,
        -- spawn row m44_55.spawn:16, 2842,3551,0) -- cooked Bass, stock 10.
        t.exec("goto-warguild", t.player.goto_tile, 2842, 3551, 0)
        t.exec("warguild.open", t.shop.open, "warguild_food_shopkeeper", 3, "warguild_food_shop")
        t.exec("warguild.buyBass", t.shop.buy, "bass", 1)
        t.check("warguild.close", t.shop.close() == "ok", "shop.close()")

        -- Legends' Guild General Store (legends_guild_general_store.rs2,
        -- wired, owner Fionella, spawn row m42_52.spawn:21, 2725,3378,1 --
        -- upper floor) -- cooked Swordfish, stock 20.
        t.exec("goto-fionella", t.player.goto_tile, 2725, 3378, 1)
        t.exec("fionella.open", t.shop.open, "fionella", 3, "generallegends")
        t.exec("fionella.buySwordfish", t.shop.buy, "swordfish", 1)
        t.check("fionella.close", t.shop.close() == "ok", "shop.close()")

        -- Keepa Kettilon's store (Jatizso, keepa_kettilons_store.rs2,
        -- wired, spawn row m37_59.spawn:44, 2417,3817,0) -- cooked Tuna
        -- (stock 20) AND cooked Salmon (stock 20), both in one visit --
        -- nothing closer and wired sells cooked salmon (wiki/shop_stock.csv
        -- checked).
        t.exec("goto-keepa", t.player.goto_tile, 2417, 3817, 0)
        t.exec("keepa.open", t.shop.open, "frisd_cook", 3, "frisd_cook")
        t.exec("keepa.buyTuna", t.shop.buy, "tuna", 1)
        t.exec("keepa.buySalmon", t.shop.buy, "salmon", 1)
        t.check("keepa.close", t.shop.close() == "ok", "shop.close()")

        t.check("fish.allFiveHeld",
            select(2, t.inv.has("swordfish")) and select(2, t.inv.has("bass"))
                and select(2, t.inv.has("tuna")) and select(2, t.inv.has("salmon"))
                and select(2, t.inv.has("shrimp")),
            "swordfish/bass/tuna/salmon/shrimp all held before returning to Caleb")

        -- ---------------------------------------------------------------
        -- Back to Caleb with the five fish. caleb_fitzharmon_fish hands
        -- over avan_crest and sets %crestquest=caleb_piece; choosing "Uh...
        -- what happened to the rest of it?" on THIS visit re-enters
        -- caleb_fitzharmon_rest, which (now that %crestquest=caleb_piece)
        -- reveals Avan's location and sets %crestquest=caleb_where in the
        -- same conversation.
        -- ---------------------------------------------------------------
        t.exec("goto-calebWithFish", t.player.goto_tile, 2819, 3451, 0)
        t.exec("caleb.handIn.greet", t.player.talk_to, "caleb_fitzharmon")
        t.exec("caleb.handIn", t.chat.play, {
            "npc:How is the fish collecting going?",
            "player:Got them all with me.",
            "mesbox:You exchange the fish",
            "choose:Uh... what happened to the rest of it?",
            "player:Uh... what happened to the rest of it?",
            "npc:my brothers and I had a slight disagreement",
            "npc:None of us wanted to give up",
            "npc:We each went our seperate ways",
            "player:So do you know where I could find any of your brothers?",
            "npc:we haven't really kept in touch",
            "npc:He said he was on some kind of search for treasure",
            "npc:Avan always did have expensive tastes",
        })
        t.chat.close()
        t.expect("caleb.gotPiece", t.inv.expect_has("avan_crest", 1))
        t.expect("caleb.avanLocationKnown", t.quest.expect_stage("caleb_where"))

        -- ---------------------------------------------------------------
        -- THE SEAM. Al Kharid gem trader (areas/alkharid/scripts/
        -- gem_trader.rs2, spawn row m51_50.spawn:14, 3288,3212,0): the real
        -- quest's dialogue with him is what sets %crestquest to
        -- crest_spoken_gem_trader -- this port's gem_trader.rs2 is a plain
        -- gem-trading stub with no Family Crest branch at all. Driven for
        -- real to prove it: the stage does not move.
        -- ---------------------------------------------------------------
        t.exec("goto-gemTrader", t.player.goto_tile, 3288, 3212, 0)
        t.exec("gemTrader.greet", t.player.talk_to, "gem_trader")
        t.exec("gemTrader.decline", t.chat.play, {
            "npc:Good day to you traveller.",
            "choose:No thank you.",
            "player:No thank you.",
            "npc:Eh, suit yourself.",
        })
        t.chat.close()
        t.expect("gemTrader.stageUnchanged", t.quest.expect_stage("caleb_where"))

        -- Avan (crest_avan.rs2 spawn row m51_51.spawn:9, 3295,3284,0):
        -- avan_talk's busy gate is `%crestquest <= crest_caleb_where` --
        -- true here, and nothing real ever moves %crestquest to
        -- crest_spoken_gem_trader, so this is permanent.
        t.exec("goto-avan", t.player.goto_tile, 3295, 3284, 0)
        t.exec("avan.greet", t.player.talk_to, "avan")
        t.exec("avan.busy", t.chat.play, {
            "npc:What? Can't you see I'm busy?",
            "player:Well, sooooorry",
        })
        t.chat.close()
        t.expect("avan.stillBusy", t.quest.expect_stage("caleb_where"))

        t.blocked("crest.avan_gem_trader_gap: gem_trader.rs2 (OSRS-Content/osrs239-content/server/scripts/areas/alkharid/scripts/gem_trader.rs2:8-17) has no Family Crest branch -- its only two options are buying gems or declining, and it never writes %crestquest. Avan's busy gate (crest_avan.rs2:25 avan_talk, `if (%crestquest <= ^crest_caleb_where) return busy`) can only be opened by %crestquest reaching crest_spoken_gem_trader (=5), and grep across the whole OSRS-Content tree shows nothing real ever writes that value -- only crest_avan.rs2 reads it and crest_selftest.rs2's debugproc mirrors the write by hand. Driven for real above: Caleb hands over avan_crest and reveals Avan's location (stage caleb_where=4), the gem trader's live dialogue leaves the stage at caleb_where, and Avan then refuses with \"What? Can't you see I'm busy?\" every time. Everything past this point is unreachable through the real client until gem_trader.rs2 gets its crest_caleb_where -> crest_spoken_gem_trader branch, naming every leg by its own content symbol for the record: boot_the_dwarf, the perfect-gold mine goldrock2, the ring/necklace/bar furnace fai_falador_furnace, the Witchaven ruin entrance slug2_ruin_entrance and its levers leverg/leverh/leveri/leverg2/leverh2, Johnathon (johnathon_fitzharmon), and the Chronozon fight (chronozon, trapdoor_open).")
        return
    end,
}
