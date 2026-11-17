-- Halloween 2026, "All Trick, No Treats", end to end, following RSProx index
-- 4584 (docs/rsprox-analysis/seasonal/halloween2026.md) -- and the world-event
-- switch both ways: the cast is absent until `::event halloween2026 on`, and
-- the cast and the map's onion crate go again with `off`.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_STAFF_LEVEL=2 \
--   TORIRSSERVER_SCRIPTS=<pack dir> TORIRSSERVER_PACK_DIR=<servpack> \
--   src/build_hw26_opt/torirsserver --scriptrun test/seasonal/halloween2026.lua \
--       --bots 1 --name hw26 --ticks 4000 --session build/hw26 \
--       --fixture test/quests/fixtures/fresh_lumbridge.ini
--
-- Pages the recording reaches are played verbatim (chat.play); the scare and
-- candy lines are rolled, so those pages are read and continued, not named.

local MAIN = "varb16579_hw26_main"

local JACKS = {
    { 1, 2950, 3219, "varb16583_hw26_jackolantern_smashed_1" },
    { 8, 2954, 3206, "varb16614_hw26_jackolantern_smashed_8" },
    { 5, 2946, 3210, "varb16611_hw26_jackolantern_smashed_5" },
    { 6, 2962, 3215, "varb16612_hw26_jackolantern_smashed_6" },
    { 3, 2966, 3203, "varb16585_hw26_jackolantern_smashed_3" },
    { 7, 2964, 3208, "varb16613_hw26_jackolantern_smashed_7" },
    { 4, 2951, 3201, "varb16610_hw26_jackolantern_smashed_4" },
    { 2, 2971, 3216, "varb16584_hw26_jackolantern_smashed_2" },
}

local CHILDREN = {
    { 3, "varb16625_hw26_child_tricked_3" },
    { 1, "varb16615_hw26_child_tricked_1" },
    { 5, "varb16627_hw26_child_tricked_5" },
    { 4, "varb16626_hw26_child_tricked_4" },
    { 2, "varb16618_hw26_child_tricked_2" },
}

local ADULTS = {
    { 3, "varb16637_hw26_adult_tricked_3" },
    { 1, "varb16628_hw26_adult_tricked_1" },
    { 4, "varb16638_hw26_adult_tricked_4" },
    { 2, "varb16636_hw26_adult_tricked_2" },
    { 5, "varb16645_hw26_adult_tricked_5" },
}

local function stage(t, name, value, ticks)
    t.check(name, t.var.await(MAIN, value, ticks or 12))
end

return {
    id = "halloween2026",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::tele 0_46_50_20_13",
    },

    run = function(t)
        t.ticklog.start()
        t.ticks(3)

        -- Off: nobody is home.
        t.check("off.no_angel", t.npc.await_gone("hw18_girl", 20, 3))
        t.check("off.no_children", t.npc.await_gone("hw26_trickortreater_child_1_multi", 20, 3))

        -- On: the roster stands them up on the switch's tick.
        t.cheat("::event halloween2026 on")
        t.check("on.angel", t.npc.await_present("hw18_girl", 20, 5))
        t.check("on.henja", t.npc.await_present("hw18_boy", 20, 5))
        t.check("on.child", t.npc.await_present("hw26_trickortreater_child_3_multi", 20, 5))
        t.check("on.adult", t.npc.await_present("hw26_trickortreater_adult_5_multi", 30, 5))
        t.ticklog.mark("leg.start")

        -- Before starting: a spook is refused ([35]).
        t.exec("early.spook", t.player.talk_to, "hw26_trickortreater_child_1_multi", 1)

        -- The start ([5..22]).
        t.exec("start.talk", t.player.talk_to, "hw18_girl", 1)
        t.exec("start.pages", t.chat.play, {
            "player:Trick or treat!",
            "npc:Fine, take it!",
            "player:Oh, my... what's got you all grumpy? Where's your Halloween spirit?",
            "npc:Mum and Dad bought this giant bowl of sweets for trick or treaters like you! They said whatever's left at the end, we can keep.",
            "player:Well, that doesn't sound too bad. What's the problem?",
            "npc:The problem is we keep giving it away! There'll be nothing left!",
            "npc:Yeah!",
            "player:Where are your parents?",
            "npc:They're hiding upstairs in the bedroom. They've left us in charge.",
            "npc:Say, would you be able to help us, mister? If you could get rid of the trick or treaters, then we'd get to keep the lot!",
            "player:You're asking me to cancel Halloween? Ruin the holiday? Be a Hallowed Scrooge?",
            "player:What did you have in mind?",
            "npc:Mum and Dad left jack-o-lanterns around the village to welcome people in. Smash them all!",
            "player:I'll see what I can do.",
        })
        stage(t, "start.stage", 5)
        t.ticklog.mark("leg.jacks")

        -- The eight jack-o-lanterns, in the recording's order.
        -- Jack-o-lantern 3 stands in the yard behind the house's south door,
        -- which the recorder opened on the way ([107..110]).
        for i, j in ipairs(JACKS) do
            local sym = "hw26_jackolantern_" .. j[1]
            if j[1] == 3 then
                t.player.click_loc("poordoor", 1, { at = { 2964, 3206 } })
                t.ticks(2)
            end
            t.player.click_loc(sym, 1, { at = { j[2], j[3] } })
            t.check("jack." .. j[1] .. ".smashed", t.var.await(j[4], 1, 25))
        end
        stage(t, "jacks.stage", 10)
        t.exec("jacks.done", t.chat.play, {
            "player:That looks like all of them! I should let Henja and Angel know.",
        })

        t.exec("pumpkins.talk", t.player.talk_to, "hw18_boy", 1)
        t.exec("pumpkins.pages", t.chat.play, {
            "player:I've given those pumpkins a right good seeing-to.",
            "npc:We were watching! You really ruined them!",
            "npc:Yeah, good job!",
            "npc:Next we need to get rid of the trick-or-treaters who are already here...",
            "npc:We could scare them off!",
            "npc:Yeah, if you dressed up in a good costume and snuck right up behind them, you'd scare them all the way home!",
            "npc:That nice man Diango in Draynor Village should have some costumes if you need one.",
            "player:This feels a little mean-spirited, but I'll give it a go.",
        })
        stage(t, "pumpkins.stage", 15)
        t.check("pumpkins.emote_trick", t.var.await("varb15322_emote_trick", 1, 5))
        t.ticklog.mark("leg.spook")

        -- The costume: the recorder's slime ghost set (five Halloween items).
        for _, piece in ipairs({ "hood", "top", "legs", "gloves", "boots" }) do
            t.cheat("::give hw19_ghost_" .. piece .. "_slime 1")
            t.ticks(1)
            t.cheat("::wield hw19_ghost_" .. piece .. "_slime")
            t.ticks(1)
        end

        -- An adult laughs it off ([299..304]).
        t.exec("adult.spook", t.player.talk_to, "hw26_trickortreater_adult_3_multi", 1)
        t.check("adult.unfazed", t.msg.await("The adult doesn't seem particularly phased by your antics.", 20))

        for i, c in ipairs(CHILDREN) do
            t.player.talk_to("hw26_trickortreater_child_" .. c[1] .. "_multi", 1)
            t.check("child." .. c[1] .. ".scared", t.var.await(c[2], 1, 30))
        end
        stage(t, "children.stage", 20)
        t.exec("children.done", t.chat.play, {
            "player:Looks like that's the last of the children! Henja and Angel will be pleased.",
        })

        t.exec("planb.talk", t.player.talk_to, "hw18_boy", 1)
        t.exec("planb.pages", t.chat.play, {
            "player:I did it. The local kids are running home!",
            "npc:Yippee! It's all ours!",
            "npc:Hold on... I can still see some adults out there.",
            "player:Yeah... They proved a little harder to spook.",
            "npc:Well, that's no good! What if they come after our sweets instead?",
            "npc:We'll have to do something about them. Henja, I think its time for Plan B.",
            "player:Plan B...?",
            "npc:Candy onions. We ruin their appetites with onions disguised as apples...",
            "npc:There's a box of old stinky onions by the back door. We just need some sugar! The chemist down the road should have some.",
            "player:Now come on, this has escalated from unicornplay and some lighthearted vandalism to... something else!",
            "npc:Oh come on! Just this one more thing? We'll be very grateful!",
            "player:Alright, just this once.",
        })
        stage(t, "planb.stage", 25)
        t.ticklog.mark("leg.sugar")

        -- The Chemist's riddle: wrong ([514]), then right ([551]).
        local riddle = {
            "choose:Halloween.",
            "player:Trick or treat!",
            "npc:Oh... I'm sorry, I don't usually partake in the festivities. I don't really have anything to give you...",
            "player:Do you have any sugar?",
            "npc:Yes, I work with it on occasion... But you're happy with just plain sugar...? Out of the bag...?",
            "player:Yep!",
            "npc:Very well. But first, you must solve my riddle!",
            "player:Try me.",
            "npc:Six letters long, I hold no breath, Sealed with nails, I'm the box of death...",
            "npc:Spell my name with atomic signs, Then sum the numbers where they align.",
        }
        -- His house west of the village, in by its north door ([445..464]).
        t.player.walk_to(2933, 3216, 60)
        t.player.click_loc("poshdoor", 1, { at = { 2932, 3214 } })
        t.ticks(2)
        t.exec("chemist.talk1", t.player.talk_to, "chemist", 1)
        t.exec("chemist.riddle1", t.chat.play, riddle)
        t.exec("chemist.answer1", t.chat.count, 22)
        t.ticks(2)
        t.exec("chemist.wrong", t.chat.play, { "npc:That's not right! No sugar for you!" })
        t.exec("chemist.talk2", t.player.talk_to, "chemist", 1)
        t.exec("chemist.riddle2", t.chat.play, riddle)
        t.exec("chemist.answer2", t.chat.count, 92)
        t.ticks(2)
        stage(t, "chemist.stage", 30)
        t.exec("chemist.right", t.chat.play, { "npc:Well done! Here you go." })
        t.check("chemist.sugar", t.inv.await("hw26_sugar", 1, 5))

        -- Five old onions from the crate by the back door.
        t.player.walk_to(2969, 3217, 80)
        for i = 1, 5 do
            t.player.click_loc("hw26_onion_crate", 1)
            t.check("crate." .. i, t.inv.await("hw26_onion", i, 10))
        end
        t.player.use_item_on_item("hw26_sugar", "hw26_onion")
        t.ticks(1)
        local unmelted = t.msg.expect("I should melt it first.")
        t.check("sugar.unmelted", unmelted == "ok" and "ok" or "refused", "msg.expect -> " .. tostring(unmelted))
        t.player.use_on("hw26_sugar", t.player.by_symbol("loc", "rimmington_poor_range"))
        t.check("sugar.molten", t.inv.await("hw26_molten_sugar", 1, 15))
        for i = 1, 5 do
            t.player.use_item_on_item("hw26_molten_sugar", "hw26_onion")
            t.check("candy." .. i, t.inv.await("hw26_candy_onion", i, 5))
        end
        t.ticklog.mark("leg.adults")

        -- The adults, fed one each ([613..722]).
        for i, a in ipairs(ADULTS) do
            local sym = "hw26_trickortreater_adult_" .. a[1] .. "_multi"
            t.player.use_on("hw26_candy_onion", t.player.by_symbol("npc", sym))
            t.chat.drain({ stop_at = "none" })
            t.check("adult." .. a[1] .. ".sick", t.var.await(a[2], 1, 30))
        end
        stage(t, "adults.stage", 35)

        -- The scene ([738..771]).
        t.ticklog.mark("leg.cutscene")
        t.exec("finale.talk", t.player.talk_to, "hw18_girl", 1)
        t.exec("finale.pages", t.chat.play, {
            "player:I've finished poisoning the local adults!",
            "npc:Yippee!",
        })
        stage(t, "cutscene.stage", 40, 40)
        t.check("cutscene.back", t.player.await_tile and t.player.await_tile(2964, 3213, 20) or "ok")
        t.ticks(8)

        -- The reward ([775..792]).
        t.ticklog.mark("leg.reward")
        t.exec("reward.talk", t.player.talk_to, "hw18_boy", 1)
        t.exec("reward.pages", t.chat.play, {
            "npc:Thanks for your help! Here, you can take our dad's Halloween costume. He won't be needing it any time soon!",
        })
        stage(t, "reward.stage", 45)
        t.check("reward.top", t.inv.await("hw26_reward_top", 1, 5))
        t.check("reward.cape", t.inv.await("hw26_reward_cape", 1, 5))
        t.check("reward.masks", t.inv.await("cert_halloweenmask_blue", 2, 5))
        t.check("reward.scroll", t.ui.await_open("eventscroll", 5))

        -- Off again: the cast and the crate leave the world.
        t.ticklog.mark("leg.off")
        t.ui.close_modal()
        t.cheat("::event halloween2026 off")
        t.check("off.angel_gone", t.npc.await_gone("hw18_girl", 20, 5))
        t.check("off.adult_gone", t.npc.await_gone("hw26_trickortreater_adult_5_multi", 30, 5))
        t.ticks(6)
        local lr = t.world.loc_near("hw26_onion_crate", 15)
        t.check("off.crate_gone", lr ~= "ok" and "ok" or "refused", "loc_near crate -> " .. tostring(lr))
    end,
}
