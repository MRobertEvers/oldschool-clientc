-- Demon Slayer, driven end to end from Quest Helper's demonslayer/DemonSlayer.java
-- (steps Aris -> Prysin -> Rovin -> bucket/sink/drain -> manhole -> sewer key ->
-- Traiborn's bones -> Silverlight -> Delrith and the incantation).
-- Brought along (getItemRequirements): coins, 25 bones, combat stats. The bucket,
-- the water and every key are obtained by playing the quest.

return {
    id = "demon",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give coins 1",
        "::give bones 25",
        "::setlevel attack 45",
        "::setlevel strength 45",
        "::setlevel defence 45",
        "::setlevel hitpoints 60",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb2561_demonslayer_main",
            constants = {
                not_started = 0,
                talked_aris = 1,
                key_hunt = 2,
                complete = 3,
            },
            display = "Demon Slayer",
            points = 3,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- talkToAris: Aris in her tent, Varrock Square (costs a coin)
        t.exec("goto-talkToAris", t.player.goto_tile, 3204, 3422, 0)
        t.exec("talkToAris", t.player.talk_to, "aris", 1)
        t.exec("talkToAris-dialog", t.chat.play, {
            "npc:Hello young one. Cross my palm",
            "choose:Ok, here you go.",
            "player:Ok, here you go.",
            "npc:Come closer",
            "npc:I can see images forming",
            "npc:very impressive looking sword",
            "npc:big dark shadow",
            "npc:Aaargh",
            "choose:Aaargh?",
            "player:Aaargh?",
            "npc:It's Delrith",
            "choose:Who's Delrith?",
            "player:Who's Delrith?",
            "npc:Delrith...",
            "npc:powerful demon",
            "npc:really hope he didn't see me",
            "npc:tried to destroy this city",
            "npc:Using his magic sword",
            "npc:Ye gods",
            "choose:Okay, where is he? I'll kill him for you!",
            "player:Okay, where is he",
            "npc:can't just go and fight",
            "npc:Wally managed",
            "npc:By reciting the correct magical incantation",
            "npc:Delrith will come forth",
            "npc:evil sorcerer",
            "choose:What is the magical incantation?",
            "player:What is the magical incantation?",
            "npc:let me think a second",
        })
        t.expect("quest.stage.talked_aris", t.quest.expect_stage("talked_aris"))
        -- the incantation is rolled per player: read its five words off the page
        local ok, txt = t.chat.text()
        local chant_words = {}
        if txt then
            local tail = txt:match("goes%.+%s*(.-)%.%s*Have") or ""
            for w in tail:gmatch("%a+") do chant_words[#chant_words + 1] = w end
        end
        t.check("aris.chant_words", #chant_words == 5, table.concat(chant_words, ","))
        t.exec("talkToAris-dialog2", t.chat.play, {
            "npc:Alright, I think I've got it",
            "player:I think so, yes.",
            "choose:Okay, thanks. I'll do my best to stop the demon.",
            "player:Okay, thanks",
            "npc:Good luck",
        })
        t.chat.close()

        -- talkToPrysin: south west corner of Varrock Castle
        t.exec("goto-talkToPrysin", t.player.goto_tile, 3204, 3471, 0)
        t.exec("talkToPrysin", t.player.talk_to, "sir_prysin", 1)
        t.exec("talkToPrysin-dialog", t.chat.play, {
            "npc:Hello, who are you?",
            "choose:Gypsy Aris said I should come and talk to you.",
            "player:Gypsy Aris said",
            "npc:Gypsy Aris? Is she still alive?",
            "choose:I need to find Silverlight.",
            "player:I need to find Silverlight.",
            "npc:What do you need to find that for?",
            "player:I need it to fight Delrith.",
            "npc:Delrith? I thought the world",
            "choose:He's back and unfortunately I've got to deal with him.",
            "player:He's back",
            "npc:You don't look up to much",
            "npc:The problem is getting Silverlight.",
            "player:You mean you don't have it?",
            "npc:Oh I do have it",
            "choose:So give me the keys!",
            "player:So give me the keys!",
            "npc:Um, well it's not so easy.",
            "npc:I kept one of the keys",
            "npc:One I gave to Rovin",
            "npc:I gave the other to the wizard Traiborn",
            "choose:Where can I find Captain Rovin?",
            "player:Where can I find Captain Rovin?",
            "npc:Captain Rovin lives at the top",
            "choose:Well I'd better go key hunting.",
            "player:Well I'd better go key hunting.",
            "npc:Ok, goodbye.",
        })
        t.chat.close()
        t.expect("quest.stage.key_hunt", t.quest.expect_stage("key_hunt"))

        -- talkToRovin: the stairs (goUpToRovin, goUpToRovin2) are travel; he is on the top floor
        t.exec("goto-talkToRovin", t.player.goto_tile, 3204, 3496, 2)
        t.exec("talkToRovin", t.player.talk_to, "captain_rovin", 1)
        t.exec("talkToRovin-dialog", t.chat.play, {
            "npc:What are you doing up here?",
            "choose:Yes I know, but this is important.",
            "player:Yes, I know, but this is important.",
            "npc:Ok, I'm listening",
            "choose:There's a demon who wants to invade this city.",
            "player:There's a demon who wants to invade",
            "npc:Is it a powerful demon?",
            "player:Yes, very.",
            "npc:As good as the palace guards",
            "player:It's not them who are going to fight",
            "npc:What, all by yourself?",
            "player:I'm going to use the powerful sword Silverlight",
            "npc:Yes you are right. Here you go.",
        })
        t.exec("talkToRovin-key", t.inv.await, "silverlight_key_2", 1, 8)

        -- pickupBucket: the bucket above the kitchen (goUpToBucket is the stairs)
        t.exec("goto-pickupBucket", t.player.goto_tile, 3221, 3496, 1)
        t.exec("pickupBucket", t.player.click_obj, "bucket_empty", 3)
        t.exec("pickupBucket-inv", t.inv.await, "bucket_empty", 1, 8)

        -- fillBucket: the sink beside the kitchen (goDownFromBucket is the stairs)
        t.exec("goto-fillBucket", t.player.goto_tile, 3224, 3494, 0)
        t.exec("fillBucket", t.player.use_on, "bucket_empty", t.player.by_symbol("loc", "fai_varrock_posh_sink"))
        t.exec("fillBucket-inv", t.inv.await, "bucket_water", 1, 8)

        -- drain: examine (Search), then pour the bucket
        t.exec("goto-drain", t.player.goto_tile, 3224, 3496, 0)
        local dr = t.player.by_symbol("loc", "questdrain")
        t.exec("drain.use_bucket", t.player.use_on, "bucket_water", dr)
        t.exec("drain.var", t.var.await_server, "varb2568_delrith_drain_key", 1, 8)

        -- sewer key
        -- goDownManhole: the manhole south east of the palace (Quest Helper WorldPoint 3237,3458)
        t.exec("goto-goDownManhole", t.player.goto_tile, 3237, 3457, 0)
        t.exec("goDownManhole-open", t.player.click_loc, "manholeclosed", 1)
        t.ticks(3)
        t.exec("goDownManhole", t.player.click_loc, "manholeopen", 1)
        t.ticks(4)
        local _, sewer_tile = t.world.tile()
        t.check("goDownManhole.landing", sewer_tile ~= nil and sewer_tile.z > 9000, "in the sewer at " .. tostring(sewer_tile and sewer_tile.x) .. "," .. tostring(sewer_tile and sewer_tile.z))
        t.exec("sewer.key", t.player.click_loc, "qip_ds_sewer_key", 1)
        t.exec("sewer.key.inv", t.inv.await, "silverlight_key_3", 1, 8)

        -- Traiborn
        t.exec("goto-traiborn", t.player.goto_tile, 3114, 3163, 1)
        t.exec("traiborn.talk", t.player.talk_to, "traiborn", 1)
        t.exec("traiborn.dialog", t.chat.play, {
            "npc:Ello young thingummywut.",
            "choose:I need to get a key given to you by Sir Prysin.",
            "player:I need to get a key",
            "npc:Sir Prysin? Who's that?",
            "choose:Well, have you got any keys knocking around?",
            "player:Well, have you got any keys knocking around?",
            "npc:Now you come to mention it",
            "npc:I sealed it using one of my magic rituals",
            "player:So do you know what ritual to use?",
            "npc:Let me think a second.",
            "npc:Yes a simple drazier",
            "choose:I'll get the bones for you.",
            "player:I'll help get the bones",
            "npc:Ooh that would be very good",
            "player:Okay, I'll speak to you",
        })
        t.chat.close()
        t.exec("traiborn.talk2", t.player.talk_to, "traiborn", 1)
        t.exec("traiborn.dialog2", t.chat.play, {
            "npc:How are you doing finding bones?",
            "player:I have some bones.",
            "npc:Give 'em here then.",
        })
        t.exec("traiborn.bones", t.var.await_server, "varp7143_demon_bones_given", 25, 60)
        t.exec("traiborn.ritual", t.chat.play, {
            "npc:Hurrah! That's all 25 sets of bones.",
            "mesbox:Traiborn places the bones in a circle",
            "mesbox:Traiborn waves his arms about",
            "npc:Wings of dark and colour too",
            "mesbox:The wizard waves his arms some more",
        })
        t.exec("traiborn.key", t.inv.await, "silverlight_key_1", 1, 12)
        t.ticks(4)
        t.exec("traiborn.thanks", t.chat.play, {
            "player:Thank you very much.",
            "npc:Not a problem for a friend",
        })
        t.chat.close()
        t.expect("bones.consumed", t.inv.expect_absent("bones"))

        -- Prysin: hand in the three keys
        t.exec("goto-prysin2", t.player.goto_tile, 3204, 3471, 0)
        t.exec("prysin2.talk", t.player.talk_to, "sir_prysin", 1)
        t.exec("prysin2.dialog", t.chat.play, {
            "npc:So how are you doing with getting the keys?",
            "player:I've got all three keys!",
            "npc:Excellent! Now I can give you Silverlight.",
        })
        t.exec("silverlight.got", t.inv.await, "silverlight", 1, 12)
        t.expect("keys.gone", t.inv.expect_absent("silverlight_key_1"))
        t.exec("case.var", t.var.await_server, "varb2567_delrith_silverlight_case", 1, 5)

        -- ================= BOSS =================
        t.exec("equip.silverlight", t.player.equip, "silverlight")
        t.exec("goto-circle-edge", t.player.goto_tile, 3221, 3366, 0)
        t.player.walk_to(3226, 3366)
        t.ticks(4)
        t.exec("summon.mesbox", t.chat.play, { "mesbox:The dark wizards complete their ritual" })
        t.chat.close()
        t.exec("delrith.present", t.npc.await_present, "delrith", 12, 12)
                t.exec("delrith.attack", t.player.attack, "delrith", 2, 15)
        t.exec("delrith.weakened", t.npc.await_present, "delrith_weakened", 20, 60)
        t.ticks(2)
        t.exec("banish.press", t.player.press, "delrith_weakened", 1, 8)
        -- wrong incantation first: fixed alphabetical order, unless it happens to be the real one
        local wrong = { "Aber", "Camerinthum", "Carlem", "Gabindo", "Purchai" }
        local same = true
        for i = 1, 5 do if wrong[i] ~= chant_words[i] then same = false end end
        if same then wrong = { "Purchai", "Gabindo", "Carlem", "Camerinthum", "Aber" } end
        local pages = { "player:Now what was that incantation again?" }
        for i = 1, 5 do pages[#pages + 1] = "choose:" .. wrong[i] end
        pages[#pages + 1] = "player:" .. table.concat(wrong, " ")
        pages[#pages + 1] = "mesbox:As you chant, Delrith is sucked"
        pages[#pages + 1] = "mesbox:The vortex collapses"
        t.exec("chant.wrong", t.chat.play, pages)
        t.chat.close()
        t.expect("wrong.stage_unchanged", t.quest.expect_stage("key_hunt"))
        t.exec("delrith.restored", t.npc.await_present, "delrith", 12, 12)
                t.exec("delrith2.attack", t.player.attack, "delrith", 2, 15)
        t.exec("delrith2.weakened", t.npc.await_present, "delrith_weakened", 20, 60)
        t.ticks(2)
        t.exec("banish2.press", t.player.press, "delrith_weakened", 1, 8)
        local rpages = { "player:Now what was that incantation again?" }
        for i = 1, 5 do rpages[#rpages + 1] = "choose:" .. chant_words[i] end
        rpages[#rpages + 1] = "player:" .. table.concat(chant_words, " ")
        rpages[#rpages + 1] = "mesbox:Delrith is sucked into the vortex"
        rpages[#rpages + 1] = "mesbox:back into the dark dimension"
        t.exec("chant.right", t.chat.play, rpages)
        t.chat.close()
        t.ticks(6)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
