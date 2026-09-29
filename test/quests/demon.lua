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
            varp = "demonslayer_main",
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
        -- The pack has no [oplocu,...] for fai_varrock_posh_sink (category 175 is not `watersource`
        -- 244, so general_use/scripts/water_sources.rs2:17 never fires): "Nothing interesting happens."
        t.blocked("content_bug: fillBucket -- using bucket_empty on fai_varrock_posh_sink (Quest Helper FAI_VARROCK_POSH_SINK, 3224,3495) does nothing; no [oplocu,fai_varrock_posh_sink] exists and its category 175 is not watersource (general_use/scripts/water_sources.rs2:17), so bucket_water, the drain key and the rest of the quest are unreachable without a cheat")
        return
    end,
}
