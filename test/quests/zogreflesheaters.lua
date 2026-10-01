-- Zogre Flesh Eaters. Content: OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_zogreflesheaters/scripts/{zogreflesheaters,zogre_finish}.rs2.
-- Guide: quest-helper ZogreFleshEaters.java (getPanels() is the step ladder
-- driven below; the guide text is paraphrase, the .rs2 dialogue quoted here
-- is verbatim). Progress varp %zogre (0..9, complete=14), side bits
-- %thzfe_prismsearch (coffin 0 locked/1 unlocked/3 open),
-- %thzfe_sithik_transformed, %thzfe_makecompozogrebow.
--
-- Slash Bash (npc 882) takes 25% damage from ordinary attacks, 50% (capped
-- 7) from Crumble Undead, and full damage from brutal arrows fired from a
-- comp ogre bow (zogre_finish.rs2 zfe_slash_bash_prepare_hit; wiki). Crumble
-- Undead alone drove him 30/30 -> 3/30 in 490 ticks and he left at his
-- 500-tick stay (seam25 runs s25sithik2/s25sithik4), so this file fletches
-- the bow and brutal arrows live after Grish's "easier way" unlock, from
-- pre-quest materials given in setup (Quest Helper combatGear: "Either
-- brutal arrows or Crumble Undead for fighting Slash Bash").

return {
    id = "zogreflesheaters",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- fourteen tutorial slots, so the quest's own drops fit
        "::setlevel smithing 4", -- getGeneralRequirements()
        "::setlevel herblore 8",
        "::setlevel ranged 75", -- 30 is the quest's; the comp ogre bow fight (seam25)
        "::setlevel fletching 30", -- comp ogre bow (ogre_arrows.rs2 make_unstrung_comp_bow)
        "::setlevel magic 70", -- Crumble Undead needs 39; Yanille guild gate needs 66 to enter
        "::setlevel hitpoints 99", -- Slash Bash: wiki infobox atk100/str120 -- survive the grind
        "::setlevel defence 75",
        "::setlevel attack 60",
        "::setlevel strength 60",
        -- quest_chompybird has no `[debugproc,quest_chompybird]` arm in
        -- quests/scripts/quest_cheat.rs2's ~170-quest completion table (grepped;
        -- absent), so `::complete quest_chompybird` answers questcheat_unknown
        -- and does nothing -- a content gap in that cheat table, not this file.
        -- %chompybird is an ordinary varp (chompybird_complete=65), so ::setvar
        -- reaches it directly; this is setup-phase prerequisite staging, not the
        -- run()-time varp write rule (e) forbids.
        "::setvar varp293_chompybird 65",
        "::complete quest_junglepotion",
        "::give rune_scimitar 1", -- combat prerequisite for the Brentle zombie fight
        "::give shark 6", -- combat prerequisite: food for the Slash Bash grind (few: the backpack must also hold the quest's clue items)
        -- seam25: Crumble Undead alone drove Slash Bash to 3/30 in 490 ticks and
        -- he left at his 500-tick stay (s25sithik2/4). The wiki's full-damage
        -- weapon is the comp ogre bow + brutal arrows, which the player may
        -- only fletch after Grish's "easier way" line (%thzfe_makecompozogrebow).
        -- These are its pre-quest materials (achey logs + wolf bones + bow
        -- string; headless ogre arrows from Big Chompy Bird Hunting; iron nails);
        -- the bow and the arrows are fletched live below.
        "::give achey_tree_logs 1",
        "::give wolf_bones 1",
        "::give bow_string 1",
        "::give ogre_headless_arrow 60",
        "::give nails_iron 60",
        "::give chaosrune 300", -- Crumble Undead: 1 chaos + 2 air + 2 earth per cast
        "::give airrune 300",
        "::give earthrune 300",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb487_zogre",
            constants = {
                not_started = 0,
                investigate = 1,
                crypt = 2,
                zavistic = 3,
                sithik = 4,
                potion = 5,
                potion_tea = 6,
                sithik_ogre = 7,
                grish_key = 8,
                slash_bash = 9,
                complete = 14,
            },
            row = "quest_zogreflesheaters",
            display = "Zogre Flesh Eaters",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.check("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("prep.equip_scimitar", t.player.equip, "rune_scimitar")

        -- ---- Starting off: Grish, the guard, the barricade -------------
        t.exec("goto-talkToGrish", t.player.goto_tile, 2447, 3049, 0)
        t.exec("talkToGrish", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("talkToGrish-dialog", t.chat.play, {
            "player:Hello there, what's going on here?",
            "npc:Hey yous creature...wha's yous doing here?",
            "choose:What do you mean sickies?",
            "player:What do you mean sickies?",
            "npc:Da zogries comin wiv da sickies",
            "player:Sorry, I just don't understand",
            "npc:Da sickies is when yous creature goes like orange",
            "choose:Can I help in any way?",
            "player:Can I help in any way?",
            "npc:Yes creatures...yous does good fings for Grish",
            "player:Oh, so you want me to find out why the Zogres have appeared",
            "npc:Is what Grish says!",
            "choose:Ok, I'll check things out then and report back.",
            "player:Ok, I'll check things out then and report back.",
            "npc:Is yous creatures really, really sure",
            "choose:Yes, I'm really sure!",
            "player:Yes, I'm really sure!",
            "npc:Dats da good fing yous creature",
            "mesbox:Grish hands you some food and two potions",
            "npc:Der's yous go creatures",
            "end",
        })
        t.check("quest.stage.investigate", t.quest.expect_stage("investigate"))

        t.exec("goto-talkToGuard", t.player.goto_tile, 2454, 3048, 0)
        t.exec("talkToGuard", t.player.talk_to, "zogre_ogre_guard", 1)
        t.exec("talkToGuard-dialog", t.chat.play, {
            "npc:Yous needs ta stay away from dis place",
            "player:But Grish has asked me to look into this place",
            "npc:Ok, dat is da big, big scary, danger fing!",
            "player:Yes, I'm sure.",
            "npc:Ok, I opens da stoppa's for yous creature.",
            "npc:Ok der' yous goes!",
            "end",
        })
        t.check("quest.stage.crypt", t.quest.expect_stage("crypt"))

        -- The collapsed barricade's p_teleport is a 2-tile hop (barricade
        -- east_n/west_n are 2 tiles apart) -- too short to trip
        -- _settle_after_click's teleport arm, so the click regrades
        -- "refused: settle_after_click" even though it landed (trap: "A
        -- SHORT hop does not trip that arm... grade the row on a
        -- t.world.tile() read of the far side"). Grade on the tile move.
        local _, barricade_before = t.world.tile()
        local cb_r, cb_d = t.player.click_loc("zogre_multi_blocking_barricade_l", 1)
        t.ticks(1)
        local _, barricade_after = t.world.tile()
        t.check("climbBarricade", barricade_after.x ~= barricade_before.x or barricade_after.z ~= barricade_before.z,
            tostring(cb_r) .. " " .. tostring(cb_d) .. " -- tile " .. barricade_before.x .. "," .. barricade_before.z
                .. " -> " .. barricade_after.x .. "," .. barricade_after.z)

        -- ---- Starting off: crypt (tomb F2) clues ------------------------
        t.exec("goto-goDownStairs", t.player.goto_tile, 2442, 9459, 2)
        t.ticks(2)

        t.exec("searchSkeleton", t.player.click_loc, "zogre_brentle_skeleton", 1)
        local kz_present = t.npc.await_present("zogre_human_brentle_vahn", 5, 10)
        t.check("killZombie.present", kz_present == "ok", tostring(kz_present))
        t.exec("killZombie.attack", t.player.attack, "zogre_human_brentle_vahn", 2, 15)
        t.exec("killZombie", t.npc.await_dead_engaged, 60, 10)

        t.exec("openBackpack", t.player.inv_op, "zogre_brentle_vahn_backpack", 1)
        t.exec("openBackpack-dialog", t.chat.play, {
            "mesbox:Just before you open the backpack",
            "mesbox:You find an interesting looking tankard",
            "mesbox:You find a knife and some rotten food",
            "end",
        })
        t.exec("openBackpack.knife", t.inv.await, "knife", 1, 10)
        -- rotten food is dead weight; the backpack must hold the later clue items
        t.exec("dropRottenFood", t.player.drop, "rotten_food")

        t.exec("searchLectern", t.player.click_loc, "zogre_lecturn", 1)
        t.exec("searchLectern-dialog", t.chat.play, { "mesbox:You find a half torn page", "end" })

        t.exec("searchCoffin", t.player.click_loc, "zogre_coffin_special_entity", 1)
        t.exec("searchCoffin-dialog", t.chat.play, {
            "mesbox:You search the coffin and find a small geometrically",
            "mesbox:The lock looks quite crude",
            "end",
        })

        t.exec("useKnifeOnCoffin", t.player.use_on, "knife", t.player.by_symbol("loc", "zogre_coffin_special_entity"))
        t.exec("useKnifeOnCoffin-dialog", t.chat.play, {
            "mesbox:With some skill you manage to slide the blade",
            "end",
        })

        t.exec("openCoffin", t.player.click_loc, "zogre_coffin_special_entity", 1)
        t.exec("openCoffin-dialog", t.chat.play, {
            "player:Urrrgggg.",
            "player:Aarrrgghhh!",
            "player:Raarrrggggg! Yes!",
            "mesbox:You eventually manage to lift the lid",
            "end",
        })

        t.exec("searchCoffinProperly", t.player.click_loc, "zogre_coffin_special_entity", 1)
        t.exec("searchCoffinProperly-dialog", t.chat.play, {
            "mesbox:You find a creepy looking black prism",
            "end",
        })
        t.exec("searchCoffinProperly.prism", t.inv.await, "zogre_black_prism", 1, 10)

        -- ---- Investigating: Zavistic, Sithik's house, the Dragon Inn ---
        -- Guild magic level is 66 to enter (measured run 4: "I can't reach
        -- that!" at 62 magic). The guide's alternate -- ring the bell
        -- outside instead of entering -- turned out to be a content gap:
        -- zogre_outdoor_bell's [oploc1,...] calls ~zfe_zavistic_talk with no
        -- npc bound (a bare loc trigger), and that proc's crypt-stage branch
        -- opens with ~chatplayer/~mesbox (no npc needed) then ~chatnpc
        -- (needs one) -- the exact chatnpc-without-npc shape trap 22
        -- documents (measured run 5: dialogue silently closed after the
        -- mesbox, no npc page, matching check-chatnpc-without-npc's "10
        -- known open" case count). Raised magic to 70 (>=66) and talk to
        -- Zavistic directly instead, which binds him as speaker naturally.
        --
        -- Still "I can't reach that!" at 70 magic (measured run 6): the real
        -- blocker is area_yanille/scripts/magic_guild.rs2's own entrance --
        -- magicguild_door_l/r (map m40_48.jl2: two door pairs, one at
        -- localx37 z15/16 = worldx 2597, worldz 3087/3088, almost exactly
        -- our goto tile) is a closed door the ::goto teleport walks the
        -- PLAYER through but the route-finder to the npc still treats as
        -- blocking. Back off outside it, probe which leaf is near (l or r --
        -- two separate objects, not a multiloc pair), and click it before
        -- talking; its own p_teleport (a door-width hop) is graded on the
        -- tile actually moving, same short-hop reasoning as the barricade.
        t.exec("goto-talkToZavistic", t.player.goto_tile, 2597, 3084, 0)
        t.ticks(2)
        local dl_probe = t.world.loc_near("magicguild_door_l", 6)
        local door_sym = (dl_probe == "ok") and "magicguild_door_l" or "magicguild_door_r"
        local _, guild_door_before = t.world.tile()
        local gd_r, gd_d = t.player.click_loc(door_sym, 1)
        t.ticks(1)
        local _, guild_door_after = t.world.tile()
        t.check("openGuildDoor",
            gd_r == "ok" or guild_door_after.x ~= guild_door_before.x or guild_door_after.z ~= guild_door_before.z,
            door_sym .. ": " .. tostring(gd_r) .. " " .. tostring(gd_d) .. " -- tile "
                .. guild_door_before.x .. "," .. guild_door_before.z .. " -> " .. guild_door_after.x .. "," .. guild_door_after.z)
        t.ticks(1)
        t.exec("talkToZavistic", t.player.talk_to, "zogre_human_zavistic_rarve", 1)
        t.exec("talkToZavistic-dialog", t.chat.play, {
            "player:There's some undead ogre activity over at Jiggig",
            "mesbox:You show the prism and the necromantic half page",
            "npc:Hmmm, now this is interesting",
            "player:I got them from a nearby Ogre tomb",
            "npc:This is very troubling",
            "player:Do you have any leads",
            "npc:Well a wizard by the name of 'Sithik Ints'",
            "npc:Why not go and talk to him",
            "end",
        })
        t.check("quest.stage.zavistic", t.quest.expect_stage("zavistic"))

        t.exec("goto-goUpToSith", t.player.goto_tile, 2591, 3104, 1)
        t.ticks(2)
        -- The bed sits crowded against the wardrobe/cupboard/drawers
        -- (measured run 7: first press hovered "Search Cupboard" instead --
        -- a covered/mis-hover pixel-hunt miss, trap 21) -- retry once if no
        -- dialogue actually opened, rather than trusting one settle.
        -- zogre_sithik_bed_entity IS placed here: maps/m40_48.jl2 line 3902,
        -- "1 31 31: 6887 10 2" decodes to level 1, worldx 2591, worldz 3103
        -- (40*64+31, 48*64+31), shape 10 (a centrepiece, per pointer.lua's
        -- own WALL/CENTREPIECE list) -- an EXACT match for the symbol
        -- click_loc resolves, not a multiloc indirection (ogre_bedman_loc/
        -- ogre_bedogre_loc, 6888/6889, are never placed directly; they are
        -- the multiloc children this wrapper swaps between). So this is not
        -- an unplaced-symbol content_bug (trap 29): the loc is really there,
        -- one tile from the goto, and two live presses this run (run 8, the
        -- last of the budget) both timed out with no hittest at all -- not
        -- "covered", not "menu has no row", a bare timeout -- across two
        -- attempts three ticks apart. A driver seam on this shape-10
        -- centrepiece in a room crowded with the wardrobe/cupboard/drawers,
        -- not a content gap and not a click op guess: click_loc's own retry
        -- (walk to another side, re-aim) already ran inside each attempt.
        -- seam24: [oploc1,ogre_bedman_loc] names Sithik (~chatnpc_specific), so the
        -- click opens his page instead of aborting on npc_type.
        t.exec("talkToSith.press", t.player.click_loc, "zogre_sithik_bed_entity", 1)
        t.exec("talkToSith", t.chat.play, {
            "npc:who gave you permission",
            "player:Zavistic Rarve said",
            "npc:why would he send you to me",
            "choose:Do you mind if I look around?",
            "player:Do you mind if I look around",
            "npc:actually yes I do mind",
            "player:going to have a look around anyway",
        })
        t.check("quest.stage.sithik", t.quest.expect_stage("sithik"))

        -- ---- Investigating: the three searches, the sketch, the Dragon Inn
        -- searchWardrobe/searchCupboard/searchDrawers grant only at
        -- %zogre 3..4 (zogre_finish.rs2 sithiks_*); stage is 4 here.
        t.exec("searchCupboard", t.player.click_loc, "sithiks_cupboard", 1)
        t.exec("searchCupboard-dialog", t.chat.play, { "mesbox:You find a book on Necromancy", "end" })
        t.exec("searchCupboard.book", t.inv.await, "zogre_necrobook", 1, 10)

        t.exec("searchWardrobe", t.player.click_loc, "sithiks_wardrobe", 1)
        t.exec("searchWardrobe-dialog", t.chat.play, { "mesbox:You find a book on Philosophy", "end" })
        t.exec("searchWardrobe.book", t.inv.await, "zogre_hambook", 1, 10)

        t.exec("searchDrawers", t.player.click_loc, "sithiks_drawers", 1)
        t.exec("searchDrawers-dialog", t.chat.play, {
            "mesbox:You find some papyrus",
            "mesbox:You find some charcoal",
            "mesbox:You also find a book on portraiture",
            "end",
        })
        t.exec("searchDrawers.papyrus", t.inv.await, "papyrus", 1, 10)
        t.exec("searchDrawers.charcoal", t.inv.await, "charcoal", 1, 10)

        t.exec("usePapyrusOnSith", t.player.use_on, "papyrus", t.player.by_symbol("loc", "zogre_sithik_bed_entity"))
        t.exec("usePapyrusOnSith-dialog", t.chat.play, {
            "npc:Oh lovely",
            "mesbox:You begin sketching",
            "mesbox:You get a portrait of Sithik",
            "end",
        })
        t.exec("usePapyrusOnSith.portrait", t.inv.await, "zogre_sithik_portrait_good", 1, 10)

        t.exec("goto-useTankardOnBartender", t.player.goto_tile, 2555, 3080, 0)
        t.ticks(2)
        t.exec("useTankardOnBartender", t.player.use_on, "zogre_dragon_tankard", t.player.by_symbol("npc", "dragon_bartender"))
        t.exec("useTankardOnBartender-dialog", t.chat.play, {
            "player:I found this tankard",
            "npc:this is Brentle's mug",
            "player:Brentle you say",
            "npc:Brentle Vahn",
            "player:Brentle Vahn is dead",
            "npc:Noooo",
            "end",
        })
        t.exec("usePortraitOnBartender", t.player.use_on, "zogre_sithik_portrait_good", t.player.by_symbol("npc", "dragon_bartender"))
        t.exec("usePortraitOnBartender-dialog", t.chat.play, {
            "mesbox:You show the portrait to the Inn keeper",
            "npc:that's the guy",
            "player:bring him to justice",
            "npc:I can and I will",
            "mesbox:The Dragon Inn bartender signs the portrait",
            "end",
        })
        t.exec("usePortraitOnBartender.signed", t.inv.await, "zogre_sithik_portrait_signed", 1, 10)

        -- ---- Zavistic again: the four pieces of evidence -> the potion ---
        t.exec("goto-bringSignedPortraitToZavistic", t.player.goto_tile, 2597, 3084, 0)
        t.ticks(2)
        local bs_probe = t.world.loc_near("magicguild_door_l", 6)
        local bs_door = (bs_probe == "ok") and "magicguild_door_l" or "magicguild_door_r"
        t.exec("openGuildDoorAgain", t.player.click_loc, bs_door, 1)
        t.ticks(2)
        local bs_r, bs_d = t.player.talk_to("zogre_human_zavistic_rarve", 1)
        t.check("bringSignedPortraitToZavistic", bs_r == "ok", tostring(bs_r) .. " " .. tostring(bs_d))
        t.exec("bringSignedPortraitToZavistic-dialog", t.chat.play, {
            "options",
            "choose:I have evidence I'd like you to look at.",
            "player:I have evidence",
            "mesbox:You show all the evidence",
            "npc:starting to think that Sithik may be involved",
            "mesbox:Zavistic hands you a strange looking potion",
            "end",
        })
        t.exec("bringSignedPortraitToZavistic.potion", t.inv.await, "zogre_ogre_trans_potion", 1, 10)
        t.check("quest.stage.potion", t.quest.expect_stage("potion"))

        -- ---- Discover the truth: potion in the tea, leave, come back ----
        t.exec("goto-goUpToSithAgain", t.player.goto_tile, 2593, 3104, 1)
        t.ticks(2)
        t.exec("usePotionOnTea", t.player.use_on, "zogre_ogre_trans_potion", t.player.by_symbol("obj", "zogre_cup_of_tea_sithix"))
        t.exec("usePotionOnTea-dialog", t.chat.play, { "mesbox:You pour some of the potion into the cup", "end" })
        t.check("quest.stage.potion_tea", t.quest.expect_stage("potion_tea"))

        -- seam25 sithik_ladder_stage: the guide's "go down the ladder and
        -- back up" (docs/quests/zogre_flesh_eaters.md stage 6 -> 7), both
        -- climbs real clicks. Down: laddertop2 (16681, m40_48.jl2:4087
        -- "1 37 35"); up: ladder (16683, m40_48.jl2:4088 "0 37 35").
        -- Sithik's room opens north onto the ladder landing through a
        -- poordoor (1535, m40_48.jl2:1938 "1 31 33" = 2591,3105).
        t.exec("openSithDoor", t.player.click_loc, "poordoor", 1)
        t.ticks(2)
        local down_r, down_d = t.player.click_loc("laddertop2", 1)
        t.ticks(3)
        local _, down_tile = t.world.tile()
        t.check("goDownstairsFromSith", down_tile.level == 0,
            tostring(down_r) .. " " .. tostring(down_d) .. " -- tile " .. down_tile.x .. "," .. down_tile.z .. "," .. down_tile.level)
        t.check("goDownstairsFromSith.stage_still_potion_tea", t.quest.expect_stage("potion_tea"))
        local lad_r, lad_d = t.player.click_loc("ladder", 1)
        t.ticks(3)
        local _, lad_tile = t.world.tile()
        t.check("goUpToOgreSith.climbed", lad_tile.level == 1,
            tostring(lad_r) .. " " .. tostring(lad_d) .. " -- tile " .. lad_tile.x .. "," .. lad_tile.z .. "," .. lad_tile.level)
        t.check("quest.stage.sithik_ogre", t.quest.expect_stage("sithik_ogre"))
        local tr_r, tr_v = t.var.server("varb495_thzfe_sithik_transformed")
        t.check("goUpToOgreSith.transformed", tr_r == "ok" and tr_v == 1, "thzfe_sithik_transformed=" .. tostring(tr_v))

        -- ---- askSithQuestions / askAboutDiseaseAndOgres -----------------
        t.exec("talkToOgreSith.press", t.player.click_loc, "zogre_sithik_bed_entity", 1)
        t.exec("talkToOgreSith", t.chat.play, {
            "npc:what's happened to me? You must help me!",
            "player:The potion won't wear off",
            "npc:Alright! Alright!",
            "options",
            "choose:How do I remove the effects of the spell from the area?",
            "player:How do I remove the effects",
            "npc:The spell is permanent",
            "options",
            "choose:How do I get rid of the undead ogres?",
            "player:How do I get rid of the undead ogres?",
            "npc:Brutal arrows work well against zogres.",
            "options",
            "choose:How do I get rid of the disease?",
            "player:How do I get rid of the disease?",
            "npc:Two jungle based herbs",
            "options",
            "choose:Sorry, I have to go.",
            "player:Sorry, I have to go.",
            "player:I'll tell Grish.",
            "end",
        })
        local ba_r, ba_v = t.var.server("varb499_thzfe_makebrutalarrow")
        local cd_r, cd_v = t.var.server("varb498_thzfe_makecuredisease")
        t.check("askAboutDiseaseAndOgres.bits", ba_v == 1 and cd_v == 1,
            "thzfe_makebrutalarrow=" .. tostring(ba_v) .. " thzfe_makecuredisease=" .. tostring(cd_v))

        -- ---- Tell Grish; the key; the easier way -------------------------
        t.exec("goto-talkToGrishAgain", t.player.goto_tile, 2447, 3049, 0)
        t.exec("talkToGrishAgain", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("talkToGrishAgain-dialog", t.chat.play, {
            "npc:Yous creature dun da fing yet?",
            "player:I found who's responsible",
            "npc:Where is da creature?",
            "player:The person responsible is a wizard",
            "player:I'm sorry to say",
            "npc:Dat is da bad fing creature",
            "player:Yes, that's right",
            "npc:Urghhh...not good fing creature",
            "mesbox:Grish gives you a crudely crafted key.",
            "player:Oh, so you want me to go back in there",
            "npc:Yeah creature",
            "end",
        })
        t.exec("talkToGrishAgain.key", t.inv.await, "zogre_tomb_artefact_key", 1, 10)
        t.check("quest.stage.grish_key", t.quest.expect_stage("grish_key"))
        t.exec("talkToGrishForBow", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("talkToGrishForBow-dialog", t.chat.play, {
            "npc:Hey, you's creature got da old fings?",
            "choose:There must be an easier way to kill these zogres!",
            "player:There must be an easier way to kill these zogres!",
            "npc:Yeah creature, yous needs da comp'zit bow",
            "mesbox:You can now fletch composite ogre bows and brutal arrows.",
            "end",
        })
        local bow_r, bow_v = t.var.server("varb500_thzfe_makecompozogrebow")
        t.check("talkToGrishForBow.bit", bow_v == 1, "thzfe_makecompozogrebow=" .. tostring(bow_v))

        -- ---- Fletch the comp ogre bow + brutal arrows (now unlocked) -----
        t.exec("fletchBow", t.player.use_item_on_item, "achey_tree_logs", "wolf_bones")
        t.exec("fletchBow.inv", t.inv.await, "unstrung_zogre_bow", 1, 5)
        t.exec("stringBow", t.player.use_item_on_item, "bow_string", "unstrung_zogre_bow")
        t.exec("stringBow.inv", t.inv.await, "zogre_bow", 1, 5)
        for i = 1, 10 do
            t.exec("fletchBrutal." .. i, t.player.use_item_on_item, "ogre_headless_arrow", "nails_iron")
            t.ticks(2)
        end
        local br_r, br_c = t.inv.count("zogre_brutal_iron")
        t.check("fletchBrutal.count", br_r == "ok" and (br_c or 0) >= 30, "zogre_brutal_iron=" .. tostring(br_c))
        t.exec("equip.bow", t.player.equip, "zogre_bow")
        t.exec("equip.brutal", t.player.equip, "zogre_brutal_iron")

        -- ---- goKillBash: the locked tomb door, the stand, Slash Bash -----
        -- The barricade is already down (%thzfe_blocking_barricade); plain
        -- travel back to the crypt floor, then the real door click.
        -- climbBarricadeForBoss: back over the crushed barricade, a real
        -- click graded on the tile move exactly as the first crossing above.
        t.exec("goto-climbBarricadeForBoss", t.player.goto_tile, 2454, 3048, 0)
        local _, bb_before = t.world.tile()
        local bb_r, bb_d = t.player.click_loc("zogre_multi_blocking_barricade_l", 1)
        t.ticks(1)
        local _, bb_after = t.world.tile()
        t.check("climbBarricadeForBoss", bb_after.x ~= bb_before.x or bb_after.z ~= bb_before.z,
            tostring(bb_r) .. " " .. tostring(bb_d) .. " -- tile " .. bb_before.x .. "," .. bb_before.z
                .. " -> " .. bb_after.x .. "," .. bb_after.z)
        -- North of the first pair of tomb doors (m38_147.jl2:6408/6410,
        -- ogre_cavedoorr/l at 2441/2442,9433 level 2): the key opens them.
        t.exec("goto-goDownStairsForBoss", t.player.goto_tile, 2441, 9435, 2)
        t.ticks(2)
        local door_r, door_d = t.player.click_loc("ogre_cavedoorl", 1)
        t.ticks(3)
        local _, door_tile = t.world.tile()
        t.check("enterDoors", door_tile.level == 0 and door_tile.x == 2480 and door_tile.z == 9446,
            tostring(door_r) .. " " .. tostring(door_d) .. " -- tile " .. door_tile.x .. "," .. door_tile.z .. "," .. door_tile.level)
        t.exec("openTombDoor.mes", t.msg.expect, "You use the Ogre Tomb Key to unlock the door.")

        t.exec("searchStand", t.player.click_loc, "zogre_stand", 1)
        t.exec("searchStand.mes", t.msg.expect, "Something stirs behind you!")
        local sb_r, sb_d = t.npc.await_present("zogre_slash_bash", 5, 10)
        t.check("slashBash.present", sb_r == "ok", "zogre_slash_bash within 5: " .. tostring(sb_r) .. " " .. tostring(sb_d))
        local atk_r, atk_d
        for _ = 1, 6 do
            atk_r, atk_d = t.player.attack("zogre_slash_bash", 2, 15)
            if atk_r == "ok" then break end
            t.ticks(5)
        end
        t.check("slashBash.attack", atk_r == "ok", "comp ogre bow + brutal: " .. tostring(atk_r) .. " " .. tostring(atk_d))
        t.exec("slashBash.dead", t.npc.await_dead_engaged, 480, 60)
        t.check("quest.stage.slash_bash", t.quest.expect_stage("slash_bash"))
        -- He drops the artefact (zogre_finish.rs2 [ai_queue3,zogre_slash_bash],
        -- Quest Helper pickUpOgreArtefact): take it off the floor.
        t.ticks(2)
        local art0 = select(2, t.inv.count("zogre_artifacts")) or 0
        local pk_r, pk_d = t.player.click_obj("zogre_artifacts")
        t.ticks(1)
        local art1 = select(2, t.inv.count("zogre_artifacts")) or 0
        t.check("pickUpOgreArtefact", pk_r == "ok" and art0 == 0 and art1 == 1,
            string.format("click_obj(zogre_artifacts) -> %s (%s); zogre_artifacts %d -> %d",
                tostring(pk_r), tostring(pk_d), art0, art1))

        -- ---- returnRelic ------------------------------------------------
        t.exec("goto-returnRelic", t.player.goto_tile, 2447, 3049, 0)
        t.exec("returnRelic", t.player.talk_to, "zogre_ogre_shaman", 1)
        t.exec("returnRelic-dialog", t.chat.play, {
            "npc:Hey, you's creature got da old fings?",
            "player:Yeah, I have them here!",
            "npc:Dat is da goodly fing",
            "player:Thanks, that's very nice of you!",
            "end",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.finish(0)
        return
    end,
}
