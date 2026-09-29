-- Family Crest. Hand-authored against OSRS-Content/osrs239-content/server/
-- scripts/quests/quest_crest/scripts/*.rs2 (crest_dimintheis.rs2,
-- crest_caleb.rs2, crest_avan.rs2, crest_boot.rs2, crest_witchaven.rs2,
-- crest_johnathon.rs2, crest_chronozon.rs2, crest_quest.rs2),
-- areas/alkharid/scripts/gem_trader.rs2 (the Family Crest branch),
-- quests/quest_theslugmenace/scripts/slugmenace_witchaven.rs2 (the Witchaven
-- ruin entrance, open to all since seam25), skill_smithing smelting.rs2 and
-- skill_crafting jewellery.rs2 / jewellery_if.rs2 (perfect gold).
--
-- Setup gives only Quest Helper getItemRequirements(): the five cooked
-- fish, a pickaxe, two rubies, ring + necklace moulds, antipoison and the
-- runes for the four blast spells; plus the skill requirements (Mining,
-- Smithing, Crafting 40, Magic 59 -- raised to 99 with Hitpoints/Defence 99,
-- rune armour and sharks for the level-170 Chronozon fight).

return {
    id = "crest",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        -- Bring-along items: Quest Helper getItemRequirements().
        "::give shrimp 1",
        "::give salmon 1",
        "::give tuna 1",
        "::give bass 1",
        "::give swordfish 1",
        "::give adamant_pickaxe 1",
        "::give ruby 2",
        "::give ring_mould 1",
        "::give necklace_mould 1",
        "::give 3doseantipoison 1",
        "::give airrune 200",
        "::give waterrune 60",
        "::give earthrune 60",
        "::give firerune 80",
        "::give deathrune 60",
        -- Chronozon (level 170) gear and food.
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 8",
        "::setlevel mining 40",
        "::setlevel smithing 40",
        "::setlevel crafting 40",
        "::setlevel magic 99",
        "::setlevel hitpoints 99",
        "::setlevel defence 99",
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
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equip.rune_full_helm", t.player.equip, "rune_full_helm")
        t.exec("equip.rune_chainbody", t.player.equip, "rune_chainbody")
        t.exec("equip.rune_platelegs", t.player.equip, "rune_platelegs")
        t.exec("equip.rune_kiteshield", t.player.equip, "rune_kiteshield")

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
        t.exec("talkToDimintheis", t.player.talk_to, "dimintheis")
        t.exec("talkToDimintheis-dialog", t.chat.play, {
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
        t.expect("quest.stage.spoken_dimintheis", t.quest.expect_stage("spoken_dimintheis"))

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
        t.exec("talkToCaleb", t.player.talk_to, "caleb_fitzharmon")
        t.exec("talkToCaleb-dialog", t.chat.play, {
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
        t.expect("quest.stage.spoken_caleb", t.quest.expect_stage("spoken_caleb"))

        -- ---------------------------------------------------------------
        -- talkToCalebWithFish: back to Caleb with the five fish.
        -- caleb_fitzharmon_fish (crest_caleb.rs2) hands over avan_crest and
        -- sets %crestquest=caleb_piece, then a 2-way; "Thank you very much!"
        -- ends this visit so the guide's talkToCalebOnceMore is its own talk.
        -- ---------------------------------------------------------------
        t.exec("goto-calebWithFish", t.player.goto_tile, 2819, 3451, 0)
        t.exec("talkToCalebWithFish", t.player.talk_to, "caleb_fitzharmon")
        t.exec("talkToCalebWithFish-dialog", t.chat.play, {
            "npc:How is the fish collecting going?",
            "player:Got them all with me.",
            "mesbox:You exchange the fish",
            "choose:Thank you very much!",
            "player:Thank you very much.",
            "npc:You're welcome.",
        })
        t.chat.close()
        local cg_r, cg_d = t.inv.expect_has("avan_crest", 1)
        t.check("caleb.gotPiece", cg_r, "Caleb's crest piece (avan_crest) held: " .. tostring(cg_r) .. " count " .. tostring(cg_d))
        t.expect("quest.stage.caleb_piece", t.quest.expect_stage("caleb_piece"))

        -- talkToCalebOnceMore: caleb_fitzharmon_salad -> "Uh... what happened
        -- to the rest of it?" -> caleb_fitzharmon_rest, which at caleb_piece
        -- names Avan and sets %crestquest=caleb_where.
        t.exec("talkToCalebOnceMore", t.player.talk_to, "caleb_fitzharmon")
        t.exec("talkToCalebOnceMore-dialog", t.chat.play, {
            "npc:finishing touches to my masterful salad",
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
        t.expect("quest.stage.caleb_where", t.quest.expect_stage("caleb_where"))

        -- ---------------------------------------------------------------
        -- Al Kharid Gem Trader (gem_trader.rs2 spawn m51_50.spawn:14,
        -- 3288,3212,0). At %crestquest = caleb_where the menu has a third
        -- row; [label,gem_trader_crest] sets spoken_gem_trader.
        -- ---------------------------------------------------------------
        t.exec("goto-gemTrader", t.player.goto_tile, 3288, 3212, 0)
        t.exec("talkToGemTrader", t.player.talk_to, "gem_trader")
        t.exec("talkToGemTrader-dialog", t.chat.play, {
            "npc:Good day to you traveller.",
            "choose:I'm in search of a man named Avan Fitzharmon.",
            "player:I'm in search of a man named Avan Fitzharmon.",
            "npc:Fitzharmon eh?",
            "npc:persuasion around here recently",
            "npc:from 'perfect gold'",
            "npc:theres gold out there",
            "npc:Maybe we'll all get lucky",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_gem_trader", t.quest.expect_stage("spoken_gem_trader"))

        -- ---------------------------------------------------------------
        -- Avan (crest_avan.rs2, spawn m51_51.spawn:9, 3295,3284,0):
        -- avan_intro's second option, then crest_man_avan sets spoken_avan
        -- before the closing player line.
        -- ---------------------------------------------------------------
        t.exec("goto-avan", t.player.goto_tile, 3295, 3284, 0)
        t.exec("talkToMan", t.player.talk_to, "avan")
        t.exec("talkToMan-dialog", t.chat.play, {
            "options",
            "choose:I'm looking for a man named Avan Fitzharmon.",
            "player:I'm looking for a man... his name is Avan Fitzharmon.",
            "npc:Then you have found him.",
            "player:You have a part of your family crest.",
            "npc:Ha! I suppose one of my worthless brothers",
            "player:No, it was your father",
            "npc:My... my father wishes this?",
            "npc:There is a certain lady",
            "npc:is a golden ring",
            "npc:not just any old gold",
            "npc:None of the gold around here",
            "npc:in finding it I am afraid",
            "npc:gladly hand over my fragment",
            "player:Can you give me any help on finding this 'perfect gold'?",
            "npc:I thought I had found a solid lead",
            "npc:Unfortunately he has apparently returned to his home",
            "player:Well, I'll see what I can do.",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_avan", t.quest.expect_stage("spoken_avan"))

        -- ---------------------------------------------------------------
        -- Boot the dwarf (crest_boot.rs2, spawn m46_153.spawn:14,
        -- 2985,9812,0). The guide's enterDwarvenMine step is the trapdoor
        -- at 3019,3450 -- plain travel, goto_tile to the mine below.
        -- ---------------------------------------------------------------
        t.exec("goto-boot", t.player.goto_tile, 2985, 9812, 0)
        t.ticks(2)
        t.exec("talkToBoot", t.player.talk_to, "boot_the_dwarf")
        t.exec("talkToBoot-dialog", t.chat.play, {
            "npc:Hello tall person.",
            "choose:Hello. I'm in search of very high quality gold.",
            "player:Hello. I'm in search of very high quality gold.",
            "npc:High quality gold eh?",
            "npc:I don't believe it's exactly easy to get to though",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_boot", t.quest.expect_stage("spoken_boot"))

        -- ---------------------------------------------------------------
        -- enterWitchavenDungeon: the old ruin entrance west of Witchaven
        -- (open to everyone since seam25; lands at 2696,9683). The
        -- dungeon's aggressive types (m42_151.spawn hobgoblins at the
        -- ladder, ogres at the levers, hellhounds on the gold rocks) are
        -- made passive with the documented ::passive cheat (QUEST_AUTHORING
        -- trap 26) so a Mining-40 character survives the walk.
        -- ---------------------------------------------------------------
        t.exec("goto-witchavenRuin", t.player.goto_tile, 2696, 3282, 0)
        t.ticks(2)
        local pr1, pd1 = t.cheat("::passive rimmington_hobgoblin_armed_1")
        t.check("passive.hobgoblin", pr1 == "ok", "::passive rimmington_hobgoblin_armed_1 -> " .. tostring(pr1) .. " " .. tostring(pd1))
        local pr2, pd2 = t.cheat("::passive ogre")
        t.check("passive.ogre", pr2 == "ok", "::passive ogre -> " .. tostring(pr2) .. " " .. tostring(pd2))
        local pr3, pd3 = t.cheat("::passive hellhound")
        t.check("passive.hellhound", pr3 == "ok", "::passive hellhound -> " .. tostring(pr3) .. " " .. tostring(pd3))
        t.exec("enterWitchavenDungeon", t.player.click_loc, "slug2_ruin_entrance", 1)
        t.ticks(3)
        local dr, dtile = t.world.tile()
        t.check("enterWitchavenDungeon.underground", dr == "ok" and dtile ~= nil and dtile.z > 9600,
            "after Climb-down: " .. tostring(dr) .. " " .. tostring(dtile and (dtile.x .. "," .. dtile.z .. "," .. dtile.level)))

        -- ---------------------------------------------------------------
        -- The lever puzzle (crest_witchaven.rs2): each [oploc1,lever*]
        -- swaps the lever for its pair; [oploc1,famcrest_doori2h1] opens
        -- only with leverh DOWN and leveri2 UP. Quest Helper order; the
        -- room doors (famcrest_doorg2h1 east, famcrest_doorh2 west,
        -- famcrest_doorh2g1 north room) are selfstage doors that stay open,
        -- so each is opened the first time the guide walks through it.
        -- ---------------------------------------------------------------
        t.exec("pullNorthLever", t.player.click_loc, "leverg", 1)
        t.ticks(2)
        local l1r, l1 = t.world.loc_near("leverg2", 6)
        t.check("pullNorthLever.pulled", l1r == "ok", "world.loc_near(leverg2,6) -> " .. tostring(l1r) .. " " .. tostring(l1 and (tostring(l1.tile_x) .. "," .. tostring(l1.tile_z))))

        t.exec("enterSouthRoomEast", t.player.click_loc, "famcrest_doorg2h1", 1)
        t.ticks(2)
        t.exec("pullSouthRoomLever", t.player.click_loc, "leverh", 1)
        t.ticks(2)
        local l2r, l2 = t.world.loc_near("leverh2", 6)
        t.check("pullSouthRoomLever.pulled", l2r == "ok", "world.loc_near(leverh2,6) -> " .. tostring(l2r) .. " " .. tostring(l2 and (tostring(l2.tile_x) .. "," .. tostring(l2.tile_z))))

        t.exec("exitSouthRoomWest", t.player.click_loc, "famcrest_doorh2", 1)
        t.ticks(2)
        local w3r, w3d = t.player.walk_to(2722, 9709, 60)
        local _, w3at = t.world.tile()
        t.check("walk-pullNorthLeverAgain", w3r == "ok",
            "walk_to 2722,9709 -> " .. tostring(w3r) .. " " .. tostring(w3d) .. "; at " .. tostring(w3at and (w3at.x .. "," .. w3at.z)))
        t.exec("pullNorthLeverAgain", t.player.click_loc, "leverg2", 1)
        t.ticks(2)
        local l3r, l3 = t.world.loc_near("leverg", 6)
        t.check("pullNorthLeverAgain.pulled", l3r == "ok", "world.loc_near(leverg,6) -> " .. tostring(l3r) .. " " .. tostring(l3 and (tostring(l3.tile_x) .. "," .. tostring(l3.tile_z))))

        t.exec("enterNorthRoom", t.player.click_loc, "famcrest_doorh2g1", 1)
        t.ticks(2)
        t.exec("pullNorthRoomLever", t.player.click_loc, "leveri", 1)
        t.ticks(2)
        local l4r, l4 = t.world.loc_near("leveri2", 6)
        t.check("pullNorthRoomLever.pulled", l4r == "ok", "world.loc_near(leveri2,6) -> " .. tostring(l4r) .. " " .. tostring(l4 and (tostring(l4.tile_x) .. "," .. tostring(l4.tile_z))))

        local w5r, w5d = t.player.walk_to(2722, 9709, 60)
        local _, w5at = t.world.tile()
        t.check("walk-pullNorthLever3", w5r == "ok",
            "walk_to 2722,9709 -> " .. tostring(w5r) .. " " .. tostring(w5d) .. "; at " .. tostring(w5at and (w5at.x .. "," .. w5at.z)))
        t.exec("pullNorthLever3", t.player.click_loc, "leverg", 1)
        t.ticks(2)
        local l5r, l5 = t.world.loc_near("leverg2", 6)
        t.check("pullNorthLever3.pulled", l5r == "ok", "world.loc_near(leverg2,6) -> " .. tostring(l5r) .. " " .. tostring(l5 and (tostring(l5.tile_x) .. "," .. tostring(l5.tile_z))))

        t.exec("pullSouthRoomLever2", t.player.click_loc, "leverh2", 1)
        t.ticks(2)
        local l6r, l6 = t.world.loc_near("leverh", 6)
        t.check("pullSouthRoomLever2.pulled", l6r == "ok", "world.loc_near(leverh,6) -> " .. tostring(l6r) .. " " .. tostring(l6 and (tostring(l6.tile_x) .. "," .. tostring(l6.tile_z))))

        -- followPathAroundEast (2721,9700), then the goldrock2 gate
        -- famcrest_doori2h1 (2727,9690), which reads the lever state.
        local er, ed = t.player.walk_to(2721, 9700, 60)
        local _, east = t.world.tile()
        t.check("followPathAroundEast", er == "ok",
            "walk_to 2721,9700 -> " .. tostring(er) .. " " .. tostring(ed) .. "; at " .. tostring(east and (east.x .. "," .. east.z)))
        t.exec("openGoldGate", t.player.click_loc, "famcrest_doori2h1", 1)
        t.ticks(2)
        t.expect("openGoldGate.open", t.msg.expect("The gate swings open"))

        -- mineGold: two 'perfect' gold ore from goldrock2 (2732,9680).
        t.exec("mineGold1", t.player.click_loc, "goldrock2", 1)
        t.exec("mineGold1.ore", t.inv.await, "perfect_gold_ore", 1, 250)
        t.exec("mineGold2", t.player.click_loc, "goldrock2", 1)
        t.exec("mineGold2.ore", t.inv.await, "perfect_gold_ore", 2, 250)

        -- ---------------------------------------------------------------
        -- smeltGold: Quest Helper's furnace WorldPoint 3273,3186 (Al
        -- Kharid) is fai_falador_furnace; smelting.rs2's use_furnace has
        -- `case perfect_gold_ore : @smelt_ore_single(perfect_gold_bar)`.
        -- ---------------------------------------------------------------
        t.exec("goto-alkharidFurnace", t.player.goto_tile, 3275, 3186, 0)
        t.ticks(2)
        t.exec("smeltGold1", t.player.use_on, "perfect_gold_ore", t.player.by_symbol("loc", "fai_falador_furnace"))
        t.exec("smeltGold1.bar", t.inv.await, "perfect_gold_bar", 1, 15)
        t.exec("smeltGold2", t.player.use_on, "perfect_gold_ore", t.player.by_symbol("loc", "fai_falador_furnace"))
        t.exec("smeltGold2.bar", t.inv.await, "perfect_gold_bar", 2, 15)

        -- makeNecklace / makeRing: a perfect gold bar on the furnace
        -- opens crafting_gold (jewellery.rs2 craft_gold_menu); its
        -- ruby_necklace / ruby_ring cells (jewellery_if.rs2) run
        -- craft_gold_once, which swaps in perfect_ruby_necklace/_ring
        -- while a perfect_gold_bar is held.
        t.exec("makeNecklace", t.player.use_on, "perfect_gold_bar", t.player.by_symbol("loc", "fai_falador_furnace"))
        local n_open = t.ui.await_open("crafting_gold", 10)
        local n_wr, n_widget = t.ui.widget("crafting_gold:ruby_necklace")
        t.check("makeNecklace.menu", n_open == "ok" and n_wr == "ok",
            "await_open crafting_gold -> " .. tostring(n_open) .. "; widget crafting_gold:ruby_necklace -> " .. tostring(n_wr) .. " " .. tostring(n_widget))
        local n_ir, n_id = "not_visible", "no crafting_gold:ruby_necklace widget"
        if n_widget then
            n_ir, n_id = t.ui.invoke(n_widget, 1)
        end
        local n_ar, n_ad = t.inv.await("perfect_ruby_necklace", 1, 10)
        t.check("makeNecklace.made", n_ar == "ok",
            "invoke ruby_necklace -> " .. tostring(n_ir) .. " " .. tostring(n_id) .. "; inv.await perfect_ruby_necklace -> " .. tostring(n_ar) .. " " .. tostring(n_ad))
        t.key("escape")
        t.ticks(2)

        t.exec("makeRing", t.player.use_on, "perfect_gold_bar", t.player.by_symbol("loc", "fai_falador_furnace"))
        local r_open = t.ui.await_open("crafting_gold", 10)
        local r_wr, r_widget = t.ui.widget("crafting_gold:ruby_ring")
        t.check("makeRing.menu", r_open == "ok" and r_wr == "ok",
            "await_open crafting_gold -> " .. tostring(r_open) .. "; widget crafting_gold:ruby_ring -> " .. tostring(r_wr) .. " " .. tostring(r_widget))
        local r_ir, r_id = "not_visible", "no crafting_gold:ruby_ring widget"
        if r_widget then
            r_ir, r_id = t.ui.invoke(r_widget, 1)
        end
        local r_ar, r_ad = t.inv.await("perfect_ruby_ring", 1, 10)
        t.check("makeRing.made", r_ar == "ok",
            "invoke ruby_ring -> " .. tostring(r_ir) .. " " .. tostring(r_id) .. "; inv.await perfect_ruby_ring -> " .. tostring(r_ar) .. " " .. tostring(r_ad))
        t.key("escape")
        t.ticks(2)

        -- ---------------------------------------------------------------
        -- returnToMan: crest_avan.rs2 @avan_jewelry at spoken_boot takes
        -- the ring and necklace, sets avan_piece, hands over caleb_crest,
        -- then @crest_avan_johnathon.
        -- ---------------------------------------------------------------
        t.exec("goto-avanReturn", t.player.goto_tile, 3295, 3284, 0)
        t.exec("returnToMan", t.player.talk_to, "avan")
        t.exec("returnToMan-dialog", t.chat.play, {
            "npc:So how are you doing getting me my perfect gold jewelry?",
            "player:I have the ring and necklace right here.",
            "mesbox:You hand Avan the perfect gold ring and necklace.",
            "npc:These... these are exquisite!",
            "npc:Now, I suppose you will be wanting to find my brother Johnathon",
            "player:That's correct.",
            "npc:he was studying the magical arts",
            "npc:Unsurprisingly, I do not believe",
            "npc:some tavern or other near the edge of The Wilderness",
            "player:Thanks Avan.",
        })
        t.chat.close()
        local ap_r, ap_d = t.inv.expect_has("caleb_crest", 1)
        t.check("returnToMan.piece", ap_r, "Avan's crest piece (caleb_crest) held: " .. tostring(ap_r) .. " count " .. tostring(ap_d))
        t.expect("quest.stage.avan_piece", t.quest.expect_stage("avan_piece"))

        -- ---------------------------------------------------------------
        -- goUpToJohnathon / talkToJohnathon: upstairs in the Jolly Boar
        -- (johnathon_fitzharmon, m51_54.spawn:26, 3279,3503,1).
        -- ---------------------------------------------------------------
        t.exec("goUpToJohnathon", t.player.goto_tile, 3277, 3504, 1)
        t.ticks(2)
        t.exec("talkToJohnathon", t.player.talk_to, "johnathon_fitzharmon")
        t.exec("talkToJohnathon-dialog", t.chat.play, {
            "player:Greetings. Would you happen to be Johnathon Fitzharmon?",
            "npc:That... I am...",
            "player:I am here to retrieve your fragment",
            "npc:The.. poison.. it is all.. too much",
            "mesbox:Sweat is pouring down",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_johnathon", t.quest.expect_stage("spoken_johnathon"))

        -- giveJohnathonAntipoison: [opnpcu,johnathon_fitzharmon] with any
        -- antipoison dose at spoken_johnathon -> cured_johnathon.
        t.exec("giveJohnathonAntipoison", t.player.use_on, "3doseantipoison", t.player.by_symbol("npc", "johnathon_fitzharmon"))
        t.exec("giveJohnathonAntipoison-dialog", t.chat.play, {
            "npc:That's completely cured me!",
            "npc:How can I reward you?",
            "player:I've come here for your piece of the Fitzharmon family crest.",
            "npc:Unfortunately I don't have it any more",
            "npc:our last battle when he bested me",
            "choose:Where can I find Chronozon?",
            "player:Where can I find Chronozon?",
            "npc:The fiend has made his lair in the Wilderness below the Obelisk of Air.",
            "choose:I will be on my way now.",
            "player:I will be on my way now.",
            "npc:My thanks for the assistance adventurer.",
        })
        t.chat.close()
        t.expect("quest.stage.cured_johnathon", t.quest.expect_stage("cured_johnathon"))

        -- ---------------------------------------------------------------
        -- killChronizon (m48_155.spawn:9, 3087,9937,0; the guide's
        -- goDownToChronizon is the Edgeville trapdoor -- plain travel).
        -- player_magic.rs2:285 calls ~chronozon_spell on every LANDED
        -- blast, which prints "Chronozon weakens..."; [ai_queue3,chronozon]
        -- regenerates him unless all four bits are set. Each blast is cast
        -- until its own new "weakens" line, then fire blast finishes him.
        -- ---------------------------------------------------------------
        local sp1, spd1 = t.cheat("::passive poisonspider")
        t.check("passive.poisonspider", sp1 == "ok", "::passive poisonspider -> " .. tostring(sp1) .. " " .. tostring(spd1))
        t.exec("goDownToChronizon", t.player.goto_tile, 3087, 9936, 0)
        t.ticks(2)
        local cz_r, cz = t.npc.nearest("chronozon", 15)
        t.check("chronozon.present", cz_r == "ok",
            "npc.nearest chronozon -> " .. tostring(cz_r) .. " " .. tostring(type(cz) == "table" and ("slot " .. tostring(cz.slot) .. " at " .. tostring(cz.x) .. "," .. tostring(cz.z)) or cz))
        for _, blast in ipairs({ "wind_blast", "water_blast", "earth_blast", "fire_blast" }) do
            local weakened = false
            local casts = 0
            local last = "none"
            for attempt = 1, 12 do
                local hr, hp = t.skill.read("hitpoints")
                if hr == "ok" and type(hp) == "table" and hp.level < 60 then
                    t.player.inv_op("shark", 1)
                    t.ticks(2)
                end
                local mark = -1
                local mr, mlist = t.msg.last(100)
                if mr == "ok" and type(mlist) == "table" then
                    for i = 1, #mlist do
                        if string.find(tostring(mlist[i].text), "Chronozon weakens", 1, true) and mlist[i].serial > mark then
                            mark = mlist[i].serial
                        end
                    end
                end
                local cr, cd = t.player.cast(blast, "chronozon", 14)
                casts = attempt
                last = tostring(cr) .. " " .. tostring(cd)
                t.ticks(2)
                local newest = -1
                local nr, nlist = t.msg.last(100)
                if nr == "ok" and type(nlist) == "table" then
                    for i = 1, #nlist do
                        if string.find(tostring(nlist[i].text), "Chronozon weakens", 1, true) and nlist[i].serial > newest then
                            newest = nlist[i].serial
                        end
                    end
                end
                if newest > mark then
                    weakened = true
                    break
                end
            end
            t.check("killChronizon." .. blast, weakened, "new 'Chronozon weakens...' line after " .. casts .. " cast(s); last cast: " .. last)
        end
        local bits_r, bits = t.var.server("crest_spells_levers_gauntlets")
        t.check("killChronizon.allFourBlasts", bits_r == "ok" and type(bits) == "number" and bits % 16 == 15,
            "crest_spells_levers_gauntlets=" .. tostring(bits) .. " (low four bits = ^crest_all_spells_cast 15)")
        -- With all four bits set, fire blast until his slot leaves the
        -- pool, eating a shark whenever hitpoints fall under 70 (a bare
        -- await_dead_engaged cannot eat; run 3 died in it). The kill is
        -- corroborated by the johnathon_crest drop below, not by absence.
        local kill_casts = 0
        local kill_gone = false
        local kill_last = "none"
        for attempt = 1, 30 do
            local khr, khp = t.skill.read("hitpoints")
            if khr == "ok" and type(khp) == "table" and khp.level < 70 then
                t.player.inv_op("shark", 1)
                t.ticks(2)
            end
            local kpr = t.npc.nearest("chronozon", 12)
            if kpr ~= "ok" then
                kill_gone = true
                break
            end
            local kr, kd = t.player.cast("fire_blast", "chronozon", 14)
            kill_casts = attempt
            kill_last = tostring(kr) .. " " .. tostring(kd)
            t.ticks(2)
        end
        local _, hp_end = t.skill.read("hitpoints")
        t.check("killChronizon", kill_gone,
            "chronozon left the pool after " .. kill_casts .. " fire blast(s) (gone=" .. tostring(kill_gone) .. "); hitpoints "
                .. tostring(type(hp_end) == "table" and hp_end.level or hp_end) .. "; last cast: " .. kill_last)

        -- pickUpCrest3: the drop is obj_add(npc_coord, johnathon_crest)
        -- at cured_johnathon.
        t.ticks(3)
        local cc_r, cc = t.world.obj_near("johnathon_crest", 8)
        t.check("pickUpCrest3.seen", cc_r == "ok",
            "world.obj_near johnathon_crest -> " .. tostring(cc_r) .. " " .. tostring(type(cc) == "table" and (tostring(cc.tile_x) .. "," .. tostring(cc.tile_z)) or cc))
        local tk_r, tk_d = t.player.click_obj("johnathon_crest", 3)
        local pk_r, pk_d = t.inv.await("johnathon_crest", 1, 20)
        t.check("pickUpCrest3", pk_r == "ok", "click_obj johnathon_crest -> " .. tostring(tk_r) .. " " .. tostring(tk_d) .. "; inv.await -> " .. tostring(pk_r) .. " " .. tostring(pk_d))

        -- repairCrest: [opheldu,avan_crest] with johnathon_crest while all
        -- three pieces are held -> family_crest (crest_quest.rs2).
        t.exec("repairCrest", t.player.use_item_on_item, "avan_crest", "johnathon_crest")
        t.exec("repairCrest.crest", t.inv.await, "family_crest", 1, 10)

        -- ---------------------------------------------------------------
        -- returnCrest: crest_dimintheis.rs2 at %crestquest > spoken_caleb
        -- with family_crest: steel_gauntlets, crest_complete,
        -- ~quest_complete_rewards(quest_familycrest, "Steel gauntlets").
        -- ---------------------------------------------------------------
        local sg_r, sg_before = t.inv.count("steel_gauntlets")
        t.check("reward.steel_gauntlets.before", sg_r == "ok", "steel_gauntlets before hand-in: " .. tostring(sg_before))
        t.exec("goto-dimintheisReturn", t.player.goto_tile, 3279, 3404, 0)
        t.exec("returnCrest", t.player.talk_to, "dimintheis")
        t.exec("returnCrest-dialog", t.chat.play, {
            "player:I have retrieved your crest.",
            "npc:Adventurer... I can only thank you",
            "npc:You are truly a hero",
            "npc:I know not how I can adequately reward you",
            "npc:I do have these mystical gauntlets",
            "npc:whenever lost, or if the owner has died",
            "npc:They can also be granted extra powers",
        })
        t.exec("quest.stage.complete", t.var.await_server, "crestquest", 11, 10)
        t.ticks(3)
        t.quest.expect_complete()
        local rg_r, rg_d = t.inv.expect_has("steel_gauntlets", (sg_before or 0) + 1)
        t.expect("reward.steel_gauntlets", rg_r, "scroll reward 'Steel gauntlets': steel_gauntlets " .. tostring(sg_before) .. " before hand-in, now " .. tostring(rg_d) .. " (expected " .. tostring((sg_before or 0) + 1) .. ")")
        t.finish(0)
        return
    end,
}
