return {
    id = "priestperil",
    fixture = "fresh_lumbridge.ini",
        setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_scimitar 1",
        "::give shark 12",
        "::give bucket_empty 1",
    },

    run = function(t)
        local r, d = t.quest.bind({
            varp = "varp302_priestperil",
            constants = {
                not_started = 0, started = 1, agree_to_kill_dog = 2, killed_dog = 3,
                return_to_drezel = 4, find_drezel_key = 5, unlocked_drezel = 6,
                poured_blessed_water = 7, meet_in_mausoleum = 8,
                begin_bring_essence = 10, end_bring_essence = 60, complete = 60,
                access_holy_barrier = 61,
            },
            row = "quest_priestinperil",
            display = "Priest in Peril",
            points = 1,
        })
        t.step("quest.bind", r == "ok" and "PASS" or "FAIL", d)
        local function cheat(command)
            local cheat_result, cheat_detail = t.cheat(command)
            t.step("cheat " .. command, cheat_result == "ok" and "PASS" or "FAIL",
                command .. " -> " .. tostring(cheat_detail or cheat_result))
        end
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")

        -- LEG 1: King Roald (LC text, king_roald.rs2)
        t.exec("tele-varrock", t.player.teleport, "varrock")
        t.exec("goto-roald", t.player.goto_tile, 3222, 3473, 0)
        t.exec("talkToRoald", t.player.talk_to, "king_roald", 1)
        t.exec("roald-1", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "player:I'm looking for a quest!",
            "npc:A quest you say?",
            "npc:Are you aware of the temple east of here.",
            "player:No, I don't think I know it...",
            "npc:Hmm, how strange that you don't.",
            "npc:Be a sport and go make sure",
            "choose:No, that sounds boring.",
            "player:No. That sounds boring.",
            "npc:Yes, I dare say it does.",
        })
        t.exec("expect_stage-not-started", t.quest.expect_stage, "not_started")
        t.exec("talkRoald2", t.player.talk_to, "king_roald", 1)
        t.exec("roald-2", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "player:I'm looking for a quest!",
            "npc:A quest you say?",
            "npc:Are you aware of the temple east of here.",
            "player:No, I don't think I know it...",
            "npc:Hmm, how strange that you don't.",
            "npc:Be a sport and go make sure",
            "choose:Sure.",
            "player:Sure. I don't have anything better to do right now.",
            "npc:Many thanks adventurer!",
        })
        t.exec("expect_stage-started", t.quest.expect_stage, "started")
        t.exec("talkRoald3", t.player.talk_to, "king_roald", 1)
        t.exec("roald-3", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "npc:You have news of Drezel for me?",
            "player:Where am I supposed to go again?",
            "npc:The temple east of here where Drezel lives.",
            "npc:Don't worry, you can't miss it.",
        })

        -- LEG 2: temple door knock
        t.exec("tele-temple", t.player.teleport, "0_53_54_16_30")
        t.exec("goto-templedoor", t.player.goto_tile, 3407, 3488, 0)
        t.exec("goToTemple", t.player.click_loc, "priestperiltempledoorr", 1)
        t.exec("knock1-open", t.chat.play, {
            "mesbox:You knock at the door",
            "player:Ummmm.....",
            "choose:Roald sent me to check on Drezel.",
            "player:Roald sent me to check on Drezel.",
            "mesbox:Psst",
            "player:Well, as I say, the King sent me",
            "mesbox:And, uh, what would you do",
            "player:I'm not sure.",
            "mesbox:Ah, good, well",
            "choose:Nope.",
            "player:Nope. Something about all this is very suspicious",
            "mesbox:Get lost then!",
        })
        t.exec("expect_stage-still-started", t.quest.expect_stage, "started")
        t.exec("knock2", t.player.click_loc, "priestperiltempledoorr", 1)
        t.exec("knock2-open", t.chat.play, {
            "mesbox:You knock at the door",
            "player:Ummmm.....",
            "choose:Roald sent me to check on Drezel.",
            "player:Roald sent me to check on Drezel.",
            "mesbox:Psst",
            "player:Well, as I say, the King sent me",
            "mesbox:And, uh, what would you do",
            "player:I'm not sure.",
            "mesbox:Ah, good, well",
            "choose:Sure.",
            "player:Sure. I'm a helpful person!",
            "mesbox:HAHAHAHA! Really?",
            "mesbox:It's been really bugging me!",
            "player:Okey-dokey, one dead dog coming up.",
        })
        t.exec("expect_stage-agree", t.quest.expect_stage, "agree_to_kill_dog")

        -- LEG 3: the Temple Guardian dog (instanced, killed for real)
        t.exec("goto-dogtrapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("dogtrapdoor-open", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.ticks(3)
        t.exec("goDownToDog", t.player.click_loc, "trapdoor_open", 1, { at = { 3405, 3507, 0 } })
        t.ticks(3)
        t.exec("killTheDog", t.player.attack, "priestperilguarddog", 2, 30)
        t.exec("attackDog.dead", t.npc.await_dead_engaged, 300, 40, { eat = { item = "shark", below = 50 } })
        t.ticks(3)
        t.exec("expect_stage-killed_dog", t.quest.expect_stage, "killed_dog")
        t.exec("dogLadderUp", t.player.click_loc, "ladder_from_cellar", 1, { at = { 3405, 9907, 0 } })
        t.exec("tele-roald2", t.player.teleport, "varrock")
        t.exec("goto-roald2", t.player.goto_tile, 3222, 3473, 0)
        t.exec("returnToKingRoald", t.player.talk_to, "king_roald", 1)
        t.exec("roald-4", t.chat.play, {
            "player:Greetings, your majesty.",
            "npc:Well hello there. What do you want?",
            "npc:You have news of Drezel for me?",
            "player:Yeah, I spoke to the guys at the temple",
            "npc:YOU DID WHAT???",
            "npc:Are you mentally deficient???",
            "player:Did I make a mistake?",
            "npc:YES YOU DID!!!!!",
            "player:B-but Drezel TOLD me to...!",
            "npc:No, you absolute cretin!",
            "npc:You get back there",
            "player:Y-yes your highness.",
        })
        t.exec("expect_stage-return_to_drezel", t.quest.expect_stage, "return_to_drezel")

        -- LEG 4: the gold key from a level-30 Monk of Zamorak (real fight)
        t.exec("tele-temple2", t.player.teleport, "0_53_54_16_30")
        t.exec("goto-templedoor2", t.player.goto_tile, 3407, 3488, 0)
        t.exec("returnToTemple", t.player.click_loc, "priestperiltempledoorr", 1)
        t.ticks(3)
        t.exec("killMonk", t.player.attack, "priestperilevilmonk3", 2, 40)
        t.exec("attackMonk.dead", t.npc.await_dead_engaged, 400, 40, { eat = { item = "shark", below = 50 } })
        t.ticks(3)
        t.exec("goldKeyDropped", t.player.click_obj, "pipkey_gold", 3)
        t.exec("goldKeyHeld", t.inv.await, "pipkey_gold", 1)

        -- LEG 5: upstairs, Drezel behind the cell gate (talk-through)
        t.exec("goto-spiralstairs", t.player.goto_tile, 3417, 3493, 0)
        t.exec("stairsUp", t.player.click_loc, "paterdomus_spiralstairs", 1)
        t.exec("goto-ladder1", t.player.goto_tile, 3410, 3486, 1)
        t.exec("ladderUp", t.player.click_loc, "ladder", 1, { at = { 3410, 3485, 1 } })
        t.exec("goto-cellgate", t.player.goto_tile, 3416, 3489, 2)
        t.exec("talkToDrezel", t.player.click_loc, "pip_prisondoor", 1)
        t.exec("talkThrough-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Oh! You do not appear to be one of those Zamorakians",
            "player:My name's",
            "npc:That is right! Oh, praise be to Saradomin!",
            "npc:me up here",
            "player:How is a river a good defence then?",
            "npc:Well, it is a long tale",
            "choose:You're right, we don't.",
            "player:You're right, we don't.",
            "npc:Well, let's just say",
        })
        t.exec("expect_stage-still-return", t.quest.expect_stage, "return_to_drezel")
        t.exec("talkThrough2", t.player.click_loc, "pip_prisondoor", 1)
        t.exec("talkThrough2-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Oh! You do not appear to be one of those Zamorakians",
            "player:My name's",
            "npc:That is right! Oh, praise be to Saradomin!",
            "npc:me up here",
            "player:How is a river a good defence then?",
            "npc:Well, it is a long tale",
            "choose:Tell me anyway.",
        })
        t.exec("talkThrough2-tale", t.chat.drain, { stop_at = "options" })
        t.exec("talkThrough2-yes", t.chat.choose, "Yes.")
        t.exec("talkThrough2-tail", t.chat.drain, {})
        t.exec("expect_stage-find_drezel_key", t.quest.expect_stage, "find_drezel_key")

        -- LEG 6: back down through the temple, into the underground, the first gate
        t.exec("goto-laddertop", t.player.goto_tile, 3410, 3486, 2)
        t.exec("ladderDown", t.player.click_loc, "laddertop", 1)
        t.exec("stairsDown", t.player.click_loc, "spiralstairstop", 1)
        t.exec("goto-doorinside", t.player.goto_tile, 3409, 3488, 0)
        t.exec("leaveTemple", t.player.click_loc, "priestperiltempledoorr", 1)
        t.exec("goto-dogtrapdoor2", t.player.goto_tile, 3405, 3506, 0)
        local open_r = t.world.loc_near("trapdoor_open", 6)
        if open_r ~= "ok" then
            t.exec("dogtrapdoor2-open", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
            t.ticks(3)
        end
        t.exec("enterUnderground", t.player.click_loc, "trapdoor_open", 1, { at = { 3405, 3507, 0 } })
        t.ticks(2)
        t.exec("goto-gate1", t.player.goto_tile, 3405, 9896, 0)
        t.exec("gate1-open", t.player.click_loc, "pip_underground_door1", 1)
        t.exec("gate1-msg", t.msg.expect, "The golden key unlocks the gate")
        t.ticks(3)
        t.exec("gate1-through", t.player.goto_tile, 3405, 9893, 0)

        -- LEG 7: the monuments -- study to find the key monument, swap the golden key
        t.exec("goto-monument1", t.player.goto_tile, 3417, 9892, 0)
        t.exec("study1", t.player.click_loc, "priestperil_grave_base1", 1)
        t.exec("study1-open", t.ui.await_open, "priestperil_gravemonument")
        local seed_r, mausoleum_bits = t.var.server("varp6733_priestperil_mausoleum")
        t.step("monument.seed-initialised", seed_r == "ok" and mausoleum_bits > 0 and "PASS" or "FAIL",
            "priestperil_mausoleum = " .. tostring(mausoleum_bits))
        local seed = math.floor(mausoleum_bits / 4194304) % 128
        local key_grave = 0
        for g = 1, 7 do
            if (seed + g * 17) % 7 + 1 == 3 then key_grave = g end
        end
        t.note("seed " .. seed .. " -> the key monument is grave " .. key_grave)
        t.key("escape")
        t.ticks(2)
        t.exec("goto-keygrave", t.player.goto_tile, 3417, 9892, 0)
        local key_target = t.player.by_symbol("loc", "priestperil_grave_base" .. key_grave)
        t.exec("useKeyForKey", t.player.use_on, "pipkey_gold", key_target)
        t.exec("swapKey-iron", t.inv.await, "pipkey_iron", 1, 10)

        -- LEG 8: the well -- fill the bucket with murky water
        local well = t.player.by_symbol("loc", "priestperil_well")
        t.exec("fillBucket", t.player.use_on, "bucket_empty", well)
        t.exec("fillBucket-murky", t.inv.await, "bucket_murkywater", 1, 10)

        -- back up: gate, ladder, temple, stairs, ladder to the cell floor
        t.exec("goto-gate1-back", t.player.goto_tile, 3405, 9897, 0)
        t.exec("gate1-back-through", t.player.goto_tile, 3405, 9906, 0)
        t.exec("cellarLadderUp", t.player.click_loc, "ladder_from_cellar", 1, { at = { 3405, 9907, 0 } })
        t.ticks(2)
        t.exec("goto-templedoor-back", t.player.goto_tile, 3407, 3488, 0)
        t.exec("enterTemple-back", t.player.click_loc, "priestperiltempledoorr", 1)
        t.exec("goto-spiralstairs-back", t.player.goto_tile, 3417, 3493, 0)
        t.exec("stairsUp-back", t.player.click_loc, "paterdomus_spiralstairs", 1)
        t.exec("goto-ladder1-back", t.player.goto_tile, 3410, 3486, 1)
        t.exec("goUpWithWaterToSecondFloor", t.player.click_loc, "ladder", 1, { at = { 3410, 3485, 1 } })
        t.exec("goto-cellgate-back", t.player.goto_tile, 3416, 3489, 2)

        -- LEG 9: the iron key on the cell gate (stage 6), Drezel blesses the water
        local cell_door = t.player.by_symbol("loc", "pip_prisondoor")
        t.exec("openDoor", t.player.use_on, "pipkey_iron", cell_door)
        t.exec("unlockCell-dialog", t.chat.play, { "npc:Oh! Thank you! You have found the key!" })
        t.exec("expect_stage-unlocked_drezel", t.quest.expect_stage, "unlocked_drezel")

        -- LEG 10: Drezel blesses the murky water (use the bucket on him), then talk
        local drezel_cell = t.player.by_symbol("npc", "priestperiltrappedmonk")
        t.exec("blessWater", t.player.use_on, "bucket_murkywater", drezel_cell)
        t.exec("blessWater-dialog", t.chat.play, {
            "player:I have some water from the Salve. It seems to have been desecrated though.",
            "npc:Yes, good thinking adventurer! Give it to me, I will bless it!",
        })
        t.exec("blessWater-blessed", t.inv.await, "bucket_blessedwater", 1, 10)
        t.exec("talkFreed", t.player.talk_to, "priestperiltrappedmonk", 1)
        t.exec("talkFreed-dialog", t.chat.play, {
            "player:The key fitted the lock! You're free to leave now!",
            "npc:Well excellent work adventurer!",
            "player:I have some blessed water from the Salve in this bucket.",
            "npc:Yes! Great idea!",
        })

        -- LEG 11: pour it on the vampire's coffin (stage 7), tell Drezel (stage 8)
        t.exec("enterCell", t.player.click_loc, "pip_prisondoor", 1)
        t.ticks(3)
        local coffin = t.player.by_symbol("loc", "priestperil_coffin_noanim")
        t.exec("useBlessedWater", t.player.use_on, "bucket_blessedwater", coffin)
        t.exec("expect_stage-poured_blessed_water", t.quest.expect_stage, "poured_blessed_water")
        t.exec("bucket-returned", t.inv.await, "bucket_empty", 1, 10)
        t.exec("leaveCell", t.player.click_loc, "pip_prisondoor", 1)
        t.ticks(3)
        t.exec("talkToDrezelAfterFreeing", t.player.talk_to, "priestperiltrappedmonk", 1)
        t.exec("talkPoured-dialog", t.chat.play, {
            "player:I poured the blessed water over the vampires coffin.",
            "npc:Excellent work adventurer! I am free at last!",
            "npc:Look for me down there.",
        })
        t.exec("expect_stage-meet_in_mausoleum", t.quest.expect_stage, "meet_in_mausoleum")

        -- LEG 12: out of the temple, the east trapdoor, Drezel at the monument (stage 10)
        t.exec("goto-laddertop-2", t.player.goto_tile, 3410, 3486, 2)
        t.exec("ladderDown-2", t.player.click_loc, "laddertop", 1)
        t.exec("goto-stairstop-2", t.player.goto_tile, 3417, 3493, 1)
        t.exec("stairsDown-2", t.player.click_loc, "spiralstairstop", 1)
        t.exec("goto-doorinside-2", t.player.goto_tile, 3409, 3488, 0)
        t.exec("leaveTemple-2", t.player.click_loc, "priestperiltempledoorr", 1)
        t.exec("goto-easttrapdoor", t.player.goto_tile, 3422, 3484, 0)
        t.exec("easttrapdoor-open", t.player.click_loc, "pipeastsidetrapdoor", 1, { at = { 3422, 3485, 0 } })
        t.ticks(3)
        t.exec("easttrapdoor-descend", t.player.click_loc, "pipeastsidetrapdoor_open", 1, { at = { 3422, 3485, 0 } })
        t.ticks(2)
        t.exec("talkToDrezelUnderground", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("meetDrezel-dialog", t.chat.play, {
            "npc:Ah,",
            "player:Why, what's happened?",
            "npc:From what I can tell",
            "npc:and have used some kind",
            "npc:it will spread along",
            "player:What can we do",
            "npc:Well, as you can see",
            "npc:here focussing",
            "npc:find some kind of way",
            "player:Couldn't you bless",
            "npc:No, that would not work",
            "npc:I have only one idea",
            "player:What's that?",
            "npc:I have heard rumours",
            "npc:Should you be able",
            "player:Kind of like a filter",
            "npc:Well I have no knowledge",
        })
        t.exec("expect_stage-begin_bring_essence", t.quest.expect_stage, "begin_bring_essence")

        -- LEG 13: the holy barrier while the Salve is still polluted
        t.exec("barrierBlocked", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("barrierBlocked-dialog", t.chat.play, {
            "npc:STOP!",
            "player:Can't I go through here?",
            "npc:No, you cannot!",
        })

        -- LEG 14: the essence -- noted essence is refused, then three hand-ins
        cheat("::give cert_blankrune 5")
        t.ticks(2)
        local drezel_m = t.player.by_symbol("npc", "priestperiltrappedmonk2")
        t.exec("certRefused", t.player.use_on, "cert_blankrune", drezel_m)
        t.exec("certRefused-dialog", t.chat.play, {
            "player:I brought you some Rune Essence.",
            "npc:You have brought me notes",
        })
        t.exec("expect_stage-still-10", t.quest.expect_stage, "begin_bring_essence")
        cheat("::clearinv")
        cheat("::give blankrune 10")
        cheat("::give blankrune_high 8")
        t.ticks(2)
        t.exec("handIn1", t.player.use_on, "blankrune", drezel_m)
        t.ticks(2)
        t.exec("expect_stage-28", t.var.expect, "varp302_priestperil", 28)
        t.exec("moreQuestion", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("moreQuestion-dialog", t.chat.play, {
            "player:How many more essences do I need to bring you?",
            "npc:I need 32 more",
        })
        cheat("::give blankrune 9")
        cheat("::give blankrune_high 9")
        t.ticks(2)
        t.exec("handIn2", t.player.use_on, "blankrune_high", drezel_m)
        t.ticks(2)
        t.exec("expect_stage-46", t.var.expect, "varp302_priestperil", 46)
        local snapshot_result, xp_before = t.skill.snapshot()
        t.check("reward.snapshot", snapshot_result, "prayer xp snapshot before the hand-in -> " .. tostring(snapshot_result))
        cheat("::give blankrune 14")
        t.ticks(2)
        t.exec("bringDrezelEssence", t.player.use_on, "blankrune", drezel_m)
        t.exec("handIn3-dialog", t.chat.play, {
            "npc:Excellent! That should do it!",
            "npc:Please take this dagger",
            "npc:it has the power to prevent werewolves",
        })
        t.quest.expect_complete()
        local gain_result, gain_detail = t.skill.expect_gain("prayer", 1406, xp_before)
        t.check("reward.prayer_xp", gain_result, "prayer gain 1406 xp -> " .. tostring(gain_detail))
        local dagger_result, dagger_detail = t.inv.expect_has("dagger_wolfbane", 1)
        t.check("reward.dagger", dagger_result, "wolfbane dagger in backpack -> " .. tostring(dagger_detail))

        -- LEG 15: the barrier at 60 says to speak to Drezel first; his advice grants passage (61)
        t.exec("barrierAdvice", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("barrierAdvice-dialog", t.chat.play, {
            "npc:STOP!",
            "player:Can't I go through here?",
            "npc:Yes, now the Salve is restored you may, but speak to me first",
        })
        t.exec("talkAdvice", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("talkAdvice-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("expect_stage-access_holy_barrier", t.quest.expect_stage, "access_holy_barrier")

        -- the free-space guard (invented): a full backpack cannot reclaim the dagger; an empty one can
        cheat("::clearinv")
        t.exec("reclaimDagger", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("reclaimDagger-dialog", t.chat.play, {
            "player:I've lost my wolfbane dagger.",
            "npc:Yes, I know! Luckily for you it washed up",
            "npc:It's a family heirloom after all!",
            "player:Thanks for that!",
        })
        t.exec("reclaimDagger-held", t.inv.await, "dagger_wolfbane", 1, 10)

        -- LEG 16: through the barrier
        t.exec("barrierPass", t.player.click_loc, "pip_underground_wall_side_withportal", 1)
        t.exec("barrierPass-msg", t.msg.expect, "You pass through the holy barrier")
        t.finish(0)
    end,
}
