-- Nature Spirit, driven end to end from the Quest Helper ladder (naturespirit/NatureSpirit.java).
-- Route facts: quest_druidspirit/scripts/{quest_druidspirit,filliman,druidspirit_drezel,ghast,swamp_decay}.rs2.
-- Brought along (guide getItemRequirements): amulet of ghostspeak and a silver sickle. The
-- rest -- pies, mirror, journal, bloom scroll, mushroom, pouch, blessed sickle -- is obtained in play.
return {
    id = "druidspirit",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give amulet_of_ghostspeak 1",
        "::give silver_sickle 1",
        "::give lobster 12",
        "::give dagger_wolfbane 1",
        "::complete quest_restlessghost",
        -- ::complete also sets Priest in Peril's golden-key gate bit (bit 20 of
        -- %priestperil_mausoleum, gates.rs2:15; quest_cheat.rs2:972, seam31).
        "::complete quest_priestinperil",
        "::setlevel crafting 18",
        "::setlevel prayer 40",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 30",
        "::setlevel hitpoints 45",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp307_druidspirit",
            constants = {
                not_started = 0, started = 5, entered_swamp = 10, failed_talk = 15,
                spoken_filliman = 20, shown_mirror = 25, given_journal = 30, received_spell = 35,
                blessed = 40, casted_spell = 45, picked_fungi = 50, spoken_filliman2 = 55,
                performed_ritual = 60, entered_grotto = 65, full_transform = 70, blessed_sickle = 75,
                casted_sickle_bloom = 80, picked_sickle = 85, added_pouch = 90,
                killed_ghast1 = 95, killed_ghast2 = 100, killed_ghast3 = 105, complete = 110,
            },
            row = "quest_naturespirit",
            display = "Nature Spirit",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("amulet.equip", t.player.equip, "amulet_of_ghostspeak")

        -- goDownToDrezel / talkToDrezel: the temple trapdoor, the two mausoleum gates, Drezel
        t.exec("goto-goDownToDrezel", t.player.goto_tile, 3405, 3506, 0)
        t.exec("goDownToDrezel", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.ticks(2)
        t.exec("goDownToDrezel-descend", t.player.click_loc, "trapdoor_open", 1, { at = { 3405, 3507, 0 } })
        t.ticks(3)
        local _, down = t.world.tile()
        t.check("goDownToDrezel-underground", down.z > 6400, "landed at " .. down.x .. "," .. down.z .. "," .. down.level)
        t.exec("goDownToDrezel-gate1", t.player.click_loc, "pip_underground_door1", 1)
        t.ticks(2)
        local _, g1 = t.world.tile()
        t.check("goDownToDrezel-gate1-through", g1.z < 9895, "past the gate at " .. g1.x .. "," .. g1.z .. "," .. g1.level)
        t.player.walk_to(3430, 9897, 40)
        local _, w2 = t.world.tile()
        t.check("goDownToDrezel-walk-gate2", w2.x >= 3428 and w2.x <= 3431, "walked to " .. w2.x .. "," .. w2.z .. "," .. w2.level)
        t.exec("goDownToDrezel-gate2", t.player.click_loc, "pip_underground_door2", 1)
        t.ticks(2)
        local _, g2 = t.world.tile()
        t.check("goDownToDrezel-gate2-through", g2.x > 3431, "past the gate at " .. g2.x .. "," .. g2.z .. "," .. g2.level)
        -- Priest in Peril's farewell advice (LostCity drezel.rs2:138-147): 60 -> 61, the barrier opens
        t.exec("talkToDrezel-advice", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkToDrezel-advice-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("talkToDrezel-advice-var", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkToDrezel-dialog", t.chat.play, {
            "npc:Greetings again adventurer",
            "choose:Is there anything else interesting to do around here?",
            "player:Is there anything else interesting",
            "npc:not a great deal",
            "choose:Well, what is it, I may be able to help?",
            "player:Well, what is it",
            "npc:There's a man called Filliman",
            "choose:Who is this Filliman?",
            "player:Who is this Filliman?",
            "npc:Filliman Tarlock is his full name",
            "npc:Most people that come this way",
            "choose:Yes, I'll go and look for him.",
            "player:Yes, I'll go and look for him.",
            "npc:That's great, but it is very dangerous",
            "choose:Yes, I'm sure.",
            "player:Yes, I'm sure.",
            "npc:That's great! Many thanks!",
            "npc:Just run from them",
        })
        t.exec("talkToDrezel-pies", t.inv.await_all, { meat_pie = 3, apple_pie = 3 }, 8)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        t.exec("talkToDrezel-box", t.chat.expect_text, "The cleric hands you some food")
        t.exec("talkToDrezel-dialog2", t.chat.play, {
            "*",
            "npc:Please take this food to Filliman",
            "player:I'll do my very best",
        })
        t.chat.close()

        -- leaveDrezel: through the holy barrier (mausoleum_interactions.rs2:26), out east of the Salve
        t.exec("leaveDrezel", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("leaveDrezel-msg", t.msg.expect, "You pass through the holy barrier")
        local _, out = t.world.tile()
        t.check("leaveDrezel-surface", out.z < 6400, "out at " .. out.x .. "," .. out.z .. "," .. out.level)
        -- enterSwamp: the north gate of Mort Myre, 5 -> 10
        t.exec("goto-enterSwamp", t.player.goto_tile, 3444, 3460, 0)
        t.exec("enterSwamp", t.player.click_loc, "mortmyre_metalgateclosed_l", 1)
        t.exec("enterSwamp-msg", t.msg.expect, "gloomy atmosphere of Mort Myre", 8)
        t.expect("quest.stage.entered_swamp", t.quest.expect_stage("entered_swamp"))

        -- tryToEnterGrotto / talkToFilliman: with the amulet worn Filliman answers, 10 -> 20
        t.exec("goto-tryToEnterGrotto", t.player.goto_tile, 3440, 3334, 0)
        t.exec("tryToEnterGrotto", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.exec("talkToFilliman", t.chat.play, {
            "mesbox:A shifting apparition appears in front of you.",
            "player:Hello?",
            "npc:Oh, I understand you!",
            "choose:How long have you been a ghost?",
            "player:How long have you been a ghost?",
            "npc:What?! Don't be preposterous",
            "player:But it's true",
            "npc:Don't be silly, I can see you",
            "choose:Ok, thanks.",
            "player:Ok, thanks.",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_filliman", t.quest.expect_stage("spoken_filliman"))

        -- takeWashingBowl / takeMirror: the mirror is under the washing bowl
        t.exec("goto-takeWashingBowl", t.player.goto_tile, 3437, 3336, 0)
        local bowl_result, bowl_detail = t.player.click_obj("bowl_empty_filliman", 3)
        t.ticks(2)
        t.exec("takeWashingBowl-inv", t.inv.await, "bowl_empty_filliman", 1, 8)
        t.step("takeWashingBowl", bowl_result == "ok" and "PASS" or "FAIL", "click_obj bowl_empty_filliman -> " .. tostring(bowl_result) .. " " .. tostring(bowl_detail))
        t.exec("takeWashingBowl-msg", t.msg.expect, "small mirror under the washing bowl")
        local mirror_result, mirror_detail = t.player.click_obj("mirror", 3)
        t.ticks(1)
        t.step("takeMirror", mirror_result == "ok" and "PASS" or "FAIL", "click_obj mirror -> " .. tostring(mirror_result) .. " " .. tostring(mirror_detail))
        t.exec("takeMirror-inv", t.inv.await, "mirror", 1, 8)

        -- useMirrorOnFilliman: 20 -> 25
        t.exec("goto-useMirrorOnFilliman", t.player.goto_tile, 3440, 3334, 0)
        t.exec("useMirrorOnFilliman-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.chat.close()
        local fil = t.player.by_symbol("npc", "filliman_tarlock_spirit")
        t.exec("useMirrorOnFilliman", t.player.use_on, "mirror", fil)
        t.exec("useMirrorOnFilliman-dialog", t.chat.play, {
            "mesbox:You use the mirror on the spirit",
            "player:Here take a look at this",
            "mesbox:The spirit of Filliman reaches forwards",
            "npc:Well, that is the most peculiar thing",
            "npc:visage apparent",
            "player:That's because you're dead!",
            "npc:I think you might be right my friend",
            "npc:It must be a sign",
        })
        t.chat.close()
        t.expect("quest.stage.shown_mirror", t.quest.expect_stage("shown_mirror"))

        -- searchGrotto: the journal in the knot hole, then useJournalOnFilliman: 25 -> 30 -> 35
        t.exec("goto-searchGrotto", t.player.goto_tile, 3440, 3339, 0)
        t.exec("searchGrotto", t.player.click_loc, "grotto_druidicspirit", 2)
        t.exec("searchGrotto-text", t.chat.expect_text, "Tarlock")
        t.chat.close()
        t.exec("searchGrotto-inv", t.inv.await, "filliman_journal", 1, 8)
        t.exec("goto-useJournalOnFilliman", t.player.goto_tile, 3440, 3334, 0)
        t.exec("useJournalOnFilliman-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.chat.close()
        fil = t.player.by_symbol("npc", "filliman_tarlock_spirit")
        t.exec("useJournalOnFilliman", t.player.use_on, "filliman_journal", fil)
        t.exec("useJournalOnFilliman-dialog", t.chat.play, {
            "mesbox:You give the journal to Filliman Tarlock.",
            "player:Here, I found this",
            "npc:My journal!",
            "mesbox:The spirit starts leafing through the journal",
            "npc:It's all coming back to me now",
        })
        t.expect("quest.stage.given_journal", t.quest.expect_stage("given_journal"))
        t.exec("useJournalOnFilliman-help", t.chat.play, {
            "choose:How can I help?",
            "player:How can I help?",
            "npc:Will you help me to become a nature spirit?",
            "player:I might be interested",
            "npc:Well, the book says",
            "player:Well, that does seem a bit vague.",
            "npc:Hmm, it does and I could understand",
            "mesbox:The druid produces a small sheet of papyrus",
            "npc:This spell needs to be cast in the swamp",
            "player:Blessed, what does that do?",
            "npc:It is required if you're to cast this druid spell",
        })
        t.chat.close()
        t.expect("quest.stage.received_spell", t.quest.expect_stage("received_spell"))
        t.exec("useJournalOnFilliman-spell", t.inv.await, "bloom_spell", 1, 8)

        -- goBackDownToDrezel / talkToDrezelForBlessing: out of the swamp's north gate, the east trapdoor, 35 -> 40
        t.exec("goto-leaveSwamp", t.player.goto_tile, 3444, 3457, 0)
        t.exec("leaveSwamp", t.player.click_loc, "mortmyre_metalgateclosed_l", 1)
        t.exec("leaveSwamp-msg", t.msg.expect, "You skip gladly out of murky Mort Myre")
        t.exec("goto-goBackDownToDrezel", t.player.goto_tile, 3422, 3484, 0)
        t.exec("goBackDownToDrezel", t.player.click_loc, "pipeastsidetrapdoor", 1, { at = { 3422, 3485, 0 } })
        t.ticks(2)
        t.exec("goBackDownToDrezel-descend", t.player.click_loc, "pipeastsidetrapdoor_open", 1, { at = { 3422, 3485, 0 } })
        t.ticks(3)
        local _, back = t.world.tile()
        t.check("goBackDownToDrezel-underground", back.z > 6400, "landed at " .. back.x .. "," .. back.z .. "," .. back.level)
        t.exec("talkToDrezelForBlessing", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkToDrezelForBlessing-dialog", t.chat.play, {
            "player:Hello again! I'm helping Filliman",
            "npc:But you haven't sneezed!",
            "player:You're so funny!",
            "player:But can you bless me?",
            "npc:Very well my friend, prepare yourself",
        })
        t.exec("talkToDrezelForBlessing-var", t.var.await_server, "varp307_druidspirit", 40, 12)
        t.exec("talkToDrezelForBlessing-dialog2", t.chat.play, {
            "npc:There you go my friend, you're now blessed",
            "player:Many thanks!",
        })
        t.chat.close()
        t.expect("quest.stage.blessed", t.quest.expect_stage("blessed"))

        -- back to the swamp the way Drezel's room lets out: the holy barrier, then the north gate
        t.exec("leaveDrezel2", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("leaveDrezel2-msg", t.msg.expect, "You pass through the holy barrier")
        t.exec("goto-reenterSwamp", t.player.goto_tile, 3444, 3460, 0)
        t.exec("reenterSwamp", t.player.click_loc, "mortmyre_metalgateclosed_l", 1)
        t.exec("reenterSwamp-msg", t.msg.expect, "You walk into the gloomy atmosphere of Mort Myre")

        -- castSpellAndGetMushroom: cast the scroll beside the rotting log, pick the fungus, 40 -> 45 -> 50
        t.exec("goto-castSpellAndGetMushroom", t.player.goto_tile, 3440, 3348, 0)
        t.ticks(2)
        t.exec("castSpellAndGetMushroom", t.player.inv_op, "bloom_spell", 1)
        t.exec("castSpellAndGetMushroom-msg", t.msg.expect, "You cast the spell in the swamp")
        t.expect("quest.stage.casted_spell", t.quest.expect_stage("casted_spell"))
        t.exec("castSpellAndGetMushroom-used", t.inv.await, "used_bloom_spell", 1, 8)
        t.ticks(3)
        t.exec("castSpellAndGetMushroom-pick", t.player.click_loc, "log_druidicspirit2", 2)
        t.exec("castSpellAndGetMushroom-inv", t.inv.await, "mortmyremushroom", 1, 8)
        t.expect("quest.stage.picked_fungi", t.quest.expect_stage("picked_fungi"))

        -- show the fungus (50 -> 55) and take a second bloom scroll
        t.exec("goto-showFungus", t.player.goto_tile, 3440, 3334, 0)
        t.exec("showFungus-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.exec("showFungus-dialog", t.chat.play, {
            "npc:Did you manage to get something from nature?",
            "mesbox:You show the fungus to Filliman.",
            "player:Yes, I have a fungus here that I picked.",
            "npc:Wonderful, the mushroom represents",
            "choose:What are the things that are needed?",
            "player:What are the things that are needed again?",
            "npc:The three things are",
            "player:Ok, and 'something from nature'",
            "npc:Yes, that's correct",
            "player:Do you have any ideas",
            "npc:I'm sorry my friend",
            "choose:What should I do when I have those things?",
            "player:What should we do when we have those things?",
            "npc:Ah yes, I looked this up",
            "player:Can we just place the components on any rock?",
            "npc:Well, the only thing the journal says",
            "choose:Could I have another bloom scroll please?",
            "player:Could I have another bloom scroll please?",
            "npc:Sure, but please look after this one.",
            "*",
        })
        t.exec("showFungus-scroll", t.inv.await, "bloom_spell", 1, 8)
        t.chat.close()
        t.expect("quest.stage.spoken_filliman2", t.quest.expect_stage("spoken_filliman2"))

        -- useMushroom / useSpellCard: the two stones outside the grotto
        t.exec("goto-useMushroom", t.player.goto_tile, 3440, 3334, 0)
        t.exec("useMushroom", t.player.use_on, "mortmyremushroom", t.player.by_symbol("loc", "stonedisc_ds_nature"))
        t.exec("useMushroom-msg", t.msg.expect, "The stone seems to absorb the fungus.")
        t.exec("useMushroom-bit", t.var.await_server, "varp6200_druidspirit_bits", 1, 10)
        t.exec("useSpellCard", t.player.use_on, "used_bloom_spell", t.player.by_symbol("loc", "stonedisc_ds_spirit"))
        t.exec("useSpellCard-msg", t.msg.expect, "The stone seems to absorb the used spell scroll.")
        t.exec("useSpellCard-bit", t.var.await_server, "varp6200_druidspirit_bits", 3, 10)

        -- tellFillimanToCast / standOnOrange: the ritual, 55 -> 60
        -- Filliman's spirit lives 100 ticks from the grotto door that summoned him (npc_add(...,
        -- filliman_tarlock_spirit, 100), quest_druidspirit.rs2:114, LostCity "100t osrs"); the door is
        -- the summon, so open it again before the ritual rather than count on the mirror-leg copy
        -- (seam32: takeMirror went 26 -> 1 tick and that copy expired at tick ~190, one row short).
        t.exec("tellFillimanToCast-door", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.chat.close()
        t.exec("goto-standOnOrange", t.player.goto_tile, 3440, 3335, 0)
        t.ticks(2)
        local tile_result, tile = t.world.tile()
        t.check("standOnOrange", tile_result == "ok" and tile.x == 3440 and tile.z == 3335, "standing on the orange stone at " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))
        t.exec("tellFillimanToCast", t.player.talk_to, "filliman_tarlock_spirit", 1)
        t.exec("tellFillimanToCast-dialog", t.chat.play, {
            "npc:Hello again! I don't suppose you've found out",
            "choose:I think I've solved the puzzle!",
            "player:I think I've solved the puzzle!",
            "npc:Oh really.. Have you placed all the items",
            "end",
        })
        t.expect("tellFillimanToCast-reopen", t.await({
            level = function() return t.chat.kind() == "npc" end,
            note = "ritual reopen",
        }, 15))
        t.exec("tellFillimanToCast-dialog2", t.chat.play, {
            "npc:Aha, everything seems to be in place!",
        })
        t.chat.close()
        t.expect("quest.stage.performed_ritual", t.quest.expect_stage("performed_ritual"))

        -- enterGrotto: 60 -> 65
        t.exec("enterGrotto", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.ticks(3)
        t.expect("quest.stage.entered_grotto", t.quest.expect_stage("entered_grotto"))

        -- talkToFillimanInGrotto: the altar, Filliman transforms, 65 -> 70
        t.exec("talkToFillimanInGrotto", t.player.click_loc, "druidic_spirit_grotto", 1)
        t.exec("talkToFillimanInGrotto-dialog", t.chat.play, {
            "npc:Well, hello there again, I was just enjoying the grotto.",
            "npc:I must complete the transformation now.",
            "end",
        })
        t.expect("talkToFillimanInGrotto-reopen", t.await({
            level = function() return t.chat.kind() == "npc" end,
            note = "transform reopen",
        }, 25))
        t.exec("talkToFillimanInGrotto-dialog2", t.chat.play, {
            "npc:Hmmm, good, the transformation is complete.",
            "player:A silver sickle? What's that?",
            "npc:The sickle is the symbol and weapon of the Druid",
            "choose:Where would I get a silver sickle?",
            "player:Where would I get a silver sickle?",
            "npc:You could make one yourself",
            "choose:What will you do to the silver sickle?",
            "player:What will you do to the silver sickle?",
            "npc:Why, I will give it my blessings",
            "choose:How can a blessed sickle help me to defeat the Ghasts?",
            "player:How can a blessed sickle help me to defeat the Ghasts?",
            "npc:My blessings will entice nature to bloom",
            "choose:Ok, thanks.",
            "player:Ok thanks.",
        })
        t.chat.close()
        t.expect("quest.stage.full_transform", t.quest.expect_stage("full_transform"))

        -- searchAltar / blessSickle: hand the silver sickle in, 70 -> 75
        t.exec("searchAltar", t.player.click_loc, "druidic_spirit_grotto", 1)
        t.exec("blessSickle", t.chat.play, {
            "npc:Have you brought me the silver sickle?",
            "player:Yes, here it is.",
            "npc:My friend, I will bless it for you",
            "end",
        })
        t.expect("blessSickle-reopen", t.await({
            level = function() return t.chat.kind() == "mesbox" end,
            note = "sickle reopen",
        }, 20))
        t.exec("blessSickle-dialog2", t.chat.play, {
            "mesbox:Your sickle has been blessed!",
            "npc:Now you can go forth and make the swamp bloom.",
            "npc:Before I can make this grotto into an Altar of Nature",
            "mesbox:The nature spirit gives you an empty pouch.",
            "npc:You'll need this in order to collect together",
        })
        t.chat.close()
        t.expect("quest.stage.blessed_sickle", t.quest.expect_stage("blessed_sickle"))
        t.exec("blessSickle-sickle", t.inv.await, "silver_sickle_blessed", 1, 8)
        t.exec("blessSickle-pouch", t.inv.await, "druid_pouch_empty", 1, 8)

        -- the empty pouch before the sickle has bloomed anything (quest_druidspirit.rs2:333), asked
        -- inside the grotto: out in the swamp a ghast's swing closes the mesbox a tick later
        t.exec("fillPouches-early", t.player.inv_op, "druid_pouch_empty", 1)
        t.exec("fillPouches-early-text", t.chat.play, { "mesbox:You've not been told how to use this item yet." })
        t.chat.close()

        -- fillPouches: bloom the swamp with the blessed sickle, pick three, fill the pouch, 75 -> 90
        t.exec("leaveGrotto", t.player.click_loc, "underground_rootwall_door", 1)
        t.ticks(3)
        t.exec("goto-fillPouches", t.player.goto_tile, 3414, 3360, 0)
        t.ticks(2)
        local casts = 0
        local total = 0
        while total < 3 and casts < 10 do
            casts = casts + 1
            t.player.inv_op("silver_sickle_blessed", 3)
            t.ticks(2)
            local names = { "log_druidicspirit2", "branch_druidicspirit2", "peartree_druidicspirit2" }
            for i = 1, 3 do
                if total < 3 then
                    t.player.click_loc(names[i], 2)
                    t.ticks(1)
                end
                local _, a = t.inv.count("mortmyremushroom")
                local _, b = t.inv.count("mortmyrebuddingstem")
                local _, c = t.inv.count("mortmyrepear")
                total = (a or 0) + (b or 0) + (c or 0)
            end
            t.ticks(1)
        end
        t.check("fillPouches-harvest", total >= 3, "harvested " .. tostring(total) .. " after " .. casts .. " cast(s)")
        t.expect("quest.stage.picked_sickle", t.quest.expect_stage("picked_sickle"))
        t.exec("fillPouches", t.player.inv_op, "druid_pouch_empty", 1)
        t.exec("fillPouches-msg", t.msg.expect, "natures harvests to your druid pouch")
        t.exec("fillPouches-inv", t.inv.await, "druid_pouch", 1, 8)
        t.expect("quest.stage.added_pouch", t.quest.expect_stage("added_pouch"))

        -- killGhasts: three real kills, 90 -> 105
        t.exec("killGhasts-equip", t.player.equip, "silver_sickle_blessed")
        t.exec("goto-killGhasts", t.player.goto_tile, 3414, 3360, 0)
        t.ticks(2)
        local stages = { "killed_ghast1", "killed_ghast2", "killed_ghast3" }
        for k = 1, 3 do
            local _, pouch = t.inv.count("druid_pouch")
            if (pouch or 0) == 0 then
                t.exec("killGhasts-unequip" .. k, t.player.inv_op, "silver_sickle_blessed", 1)
                local refill = 0
                local got = 0
                while got < 3 and refill < 10 do
                    refill = refill + 1
                    t.player.inv_op("silver_sickle_blessed", 3)
                    t.ticks(2)
                    local names = { "log_druidicspirit2", "branch_druidicspirit2", "peartree_druidicspirit2" }
                    for i = 1, 3 do
                        if got < 3 then
                            t.player.click_loc(names[i], 2)
                            t.ticks(1)
                        end
                        local _, a = t.inv.count("mortmyremushroom")
                        local _, b = t.inv.count("mortmyrebuddingstem")
                        local _, c = t.inv.count("mortmyrepear")
                        got = (a or 0) + (b or 0) + (c or 0)
                    end
                    t.ticks(1)
                end
                t.exec("killGhasts-refill" .. k, t.player.inv_op, "druid_pouch_empty", 1)
                t.exec("killGhasts-reequip" .. k, t.player.equip, "silver_sickle_blessed")
            end
            t.exec("killGhasts-visible" .. k, t.npc.await_present, "ghast_vis", 12, 90)
            -- Single-way combat: a second revealed ghast swinging at the player renews the player's
            -- claim, and every press on another copy answers "I'm already under attack." (engine
            -- %lastcombat+8, as LostCity; seam32 sourced_carry_overs traced it). So a refused press
            -- moves on to a copy not yet pressed -- the one fighting the player is among them.
            local attack_result, attack_detail
            local pressed_slots = {}
            for try = 1, 8 do
                local pick = nil
                if try > 1 then
                    local _, _, copies = t.npc.tiles("ghast_vis", 12)
                    for _, row in ipairs(copies or {}) do
                        if not pressed_slots[row.slot] then
                            pick = { slot = row.slot }
                            break
                        end
                    end
                    if pick == nil then
                        pressed_slots = {}
                    end
                end
                attack_result, attack_detail = t.player.attack("ghast_vis", 2, 15, pick)
                if attack_result == "ok" then break end
                local pressed = string.match(tostring(attack_detail), "pressed slot (%d+)")
                if pressed then
                    pressed_slots[tonumber(pressed)] = true
                end
                t.ticks(4)
            end
            t.check("killGhasts-attack" .. k, attack_result == "ok", tostring(attack_result) .. ": " .. tostring(attack_detail))
            t.exec("killGhasts-dead" .. k, t.npc.await_dead_engaged, 300, 4, { eat = { item = "lobster", below = 20 } })
            t.ticks(4)
            t.expect("quest.stage." .. stages[k], t.quest.expect_stage(stages[k]))
            t.ticks(8)
        end

        -- enterGrottoAgain / talkToNatureSpiritToFinish
        local snap_result, snapshot = t.skill.snapshot()
        t.step("reward.snapshot", snap_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(snap_result))
        t.exec("goto-enterGrottoAgain", t.player.goto_tile, 3440, 3334, 0)
        t.exec("enterGrottoAgain", t.player.click_loc, "grotto_door_druidicspirit", 1)
        t.ticks(3)
        t.exec("talkToNatureSpiritToFinish", t.player.click_loc, "druidic_spirit_grotto", 1)
        t.exec("talkToNatureSpiritToFinish-dialog", t.chat.play, {
            "npc:Hello again my friend, have you defeated three Ghasts",
            "player:Yes, I've killed all three",
            "npc:Many thanks my friend, you have completed your quest!",
            "end",
        })
        t.var.await_server("varp307_druidspirit", 110, 40)
        t.await({ level = function() return t.chat.kind() == "npc" end, note = "farewell chat" }, 60)
        t.exec("talkToNatureSpiritToFinish-farewell", t.chat.play, {
            "npc:Welcome to my Altar to Nature! Farewell my friend, and keep those Ghasts at bay!",
            "end",
        })
        t.ticks(3)
        t.quest.expect_complete()
        local cr = t.skill.expect_gain("crafting", 3000, snapshot)
        t.check("reward.crafting", cr == "ok", "crafting +3000 xp -> " .. tostring(cr))
        local dr = t.skill.expect_gain("defence", 2000, snapshot)
        t.check("reward.defence", dr == "ok", "defence +2000 xp -> " .. tostring(dr))
        local hr = t.skill.expect_gain("hitpoints", 2000, snapshot)
        t.check("reward.hitpoints", hr == "ok", "hitpoints +2000 xp -> " .. tostring(hr))
        t.finish(0)
    end,
}
