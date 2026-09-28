-- Family Crest. Hand-authored against OSRS-Content/osrs239-content/server/
-- scripts/quests/quest_crest/scripts/*.rs2 (crest_dimintheis.rs2, crest_caleb.rs2,
-- crest_avan.rs2, crest_boot.rs2, crest_witchaven.rs2, ...), areas/alkharid/
-- scripts/gem_trader.rs2 (the Family Crest branch, ported 39e774122) and
-- quests/quest_theslugmenace/scripts/slugmenace_witchaven.rs2 (the Witchaven
-- ruin entrance).
--
-- The five cooked fish are ::give'd in setup: Quest Helper's
-- getItemRequirements() lists shrimp/salmon/tuna/bass/swordfish as brought
-- along, and the guide has no step that fishes for them (its "Caleb's piece"
-- panel only has the three Caleb talks). Driven for real from there: the
-- Gem Trader's Avan question, Avan's whole intro, and Boot.
--
-- THE SEAM: the guide's enterWitchavenDungeon step (ruin entrance west of
-- Witchaven, slug2_ruin_entrance) is a dead end in this port for a player
-- who has not progressed The Slug Menace: [oploc1,slug2_ruin_entrance]
-- (slugmenace_witchaven.rs2:151) answers "There's nothing of interest down
-- there yet." while %slug2_main < ^slug_told_dungeon. Family Crest needs no
-- Slug Menace progress, so everything after this step (levers, perfect
-- gold, furnace, Johnathon, Chronozon) is unreachable through the client.
-- A second gap sits behind it: smelting.rs2's [label,use_furnace] has no
-- `case perfect_gold_ore` (smelting.dbrow:8 "Deferred: perfect gold").

return {
    id = "crest",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        -- Bring-along items: Quest Helper getItemRequirements() lists all five cooked fish.
        "::give shrimp 1",
        "::give salmon 1",
        "::give tuna 1",
        "::give bass 1",
        "::give swordfish 1",
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
        -- Al Kharid Gem Trader (gem_trader.rs2 spawn m51_50.spawn:14,
        -- 3288,3212,0). At %crestquest = caleb_where the menu has a third
        -- row; [label,gem_trader_crest] sets spoken_gem_trader.
        -- ---------------------------------------------------------------
        t.exec("goto-gemTrader", t.player.goto_tile, 3288, 3212, 0)
        t.exec("gemTrader.greet", t.player.talk_to, "gem_trader")
        t.exec("gemTrader.askAvan", t.chat.play, {
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
        t.exec("avan.greet", t.player.talk_to, "avan")
        t.exec("avan.intro", t.chat.play, {
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
        t.exec("boot.greet", t.player.talk_to, "boot_the_dwarf")
        t.exec("boot.gold", t.chat.play, {
            "npc:Hello tall person.",
            "choose:Hello. I'm in search of very high quality gold.",
            "player:Hello. I'm in search of very high quality gold.",
            "npc:High quality gold eh?",
            "npc:I don't believe it's exactly easy to get to though",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_boot", t.quest.expect_stage("spoken_boot"))

        -- ---------------------------------------------------------------
        -- enterWitchavenDungeon: the old ruin entrance west of Witchaven.
        -- ---------------------------------------------------------------
        t.exec("goto-witchavenRuin", t.player.goto_tile, 2696, 3282, 0)
        t.ticks(2)
        t.exec("enterWitchavenDungeon", t.player.click_loc, "slug2_ruin_entrance", 1)
        t.ticks(2)
        local _, tile = t.world.tile()
        local msg_result, msg_detail = t.msg.expect("nothing of interest down there yet")
        t.check("enterWitchavenDungeon.refused", true,
            "click_loc slug2_ruin_entrance op1 -> msg.expect " .. tostring(msg_result) .. " " .. tostring(msg_detail)
                .. "; player still at " .. tostring(tile and (tile.x .. "," .. tile.z .. "," .. tile.level)))
        t.blocked("content_bug: slugmenace_witchaven.rs2:151-154 [oploc1,slug2_ruin_entrance] refuses \"There's nothing of interest down there yet.\" while %slug2_main < ^slug_told_dungeon, so Family Crest's enterWitchavenDungeon step (the only way into the Hobgoblin dungeon with the levers leverg/leverh/leveri, the perfect gold rocks goldrock2 and the goldrock2 gate famcrest_doori2h1) cannot be entered by a player who has not progressed The Slug Menace; in the game the ruin is open to everyone. A second gap sits behind it: smelting.rs2 [label,use_furnace] (:44-67) has no case perfect_gold_ore, so fai_falador_furnace cannot turn perfect gold ore into perfect_gold_bar (smelting.dbrow:8 says \"Deferred: perfect gold\").")
        return
    end,
}
