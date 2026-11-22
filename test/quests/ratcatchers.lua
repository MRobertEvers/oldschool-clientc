-- Ratcatchers (quest_ratcatchers), rebuilt 2026-11-22 from RSProx index 4650, the owner's own
-- playthrough of the whole quest (docs/rsprox-analysis/quests/ratcatchers.md). Each leg is one
-- recorded section, entered at its stage (`::setvar`) and its place (`::tele`): the travel between
-- them is generic content the recording also walks (teleports, doors, gates, the carpet) and is
-- not this test's subject. Every page prefix below is the recording's; every stage is checked on
-- the click the recording writes it with.
--
-- What each leg proves:
--   start    Gertrude's offer (the No branch too), stage 5 and the line ([280..347]);
--   sewer    the sisters' task (stage 10), the cat's Chase Vermin by the recorded shape, the
--            rat-o-meter, the eighth rat's stage 15, the rat pole and Jimmy (stage 20);
--   jimmy    the map and the spell (stage 25); the Directions refused outside Ardougne and
--            followed inside it ([1001], [1074]);
--   mansion  a guard's catch to the garden gate ([2126..2131]); the trellis (stage 30);
--   kingrat  the warehouse copy at stage 80, the cat through the wall, fed, the King Rat dead
--            (stage 85, [7577..7759]);
--   smoke    Smokin' Joe's hole: the failed smoke, the cat's plan, the ambush (stage 100);
--   finale   the charm's tune in Port Sarim (stage 125, the Pied Piper) and Felkrash (complete).
local TUNE = { "rc_flute_d", "rc_flute_g", "rc_flute_e", "rc_flute_fsharp", "raised_d",
    "rc_flute_b", "rc_flute_csharp", "rc_flute_a" }

local function stage(t)
    local _, v = t.var.server("varb1404_ratcatch_var")
    return v
end

local function count(t, item)
    local r, n = t.inv.count(item)
    if r ~= "ok" then return -1 end
    return n
end

local function tile(t)
    local r, tl = t.world.tile()
    if r ~= "ok" or not tl then return nil end
    return tl
end

-- An item used on a loc by its symbol (the nearest one).
local function use_on_loc(t, name, item, sym)
    local target, why = t.player.by_symbol("loc", sym)
    if not target then
        t.check(name, false, "no " .. sym .. " in reach: " .. tostring(why))
        return
    end
    t.exec(name, t.player.use_on, item, target)
end

-- The cat out as a follower (it is held in the pack between legs).
local function cat_out(t, name)
    if t.npc.nearest("growncat", 6) == "ok" or count(t, "growncatobject") == 0 then return end
    local dr, dd = t.player.drop("growncatobject")
    t.ticks(2)
    t.check(name .. ".catOut", t.npc.nearest("growncat", 3) == "ok",
        "drop(growncatobject) -> " .. tostring(dr) .. " " .. tostring(dd))
end

return {
    id = "ratcatchers",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::complete quest_gertrudescat",
        "::complete quest_icthlarinslittlehelper",
        "::complete quest_giantdwarf",
        "::setlevel magic 51",
        "::give growncatobject 1",
        "::give ics_little_amulet_of_catspeak 1",
        "::give kwuarm 1", "::give red_spiders_eggs 1", "::give vial_empty 1",
        "::give cheese 4", "::give bucket_milk 1", "::give unicorn_horn_dust 1", "::give marentill 1",
        "::give tbwt_raw_karambwanji 10", "::give pot_empty 1", "::give weeds 1", "::give tinderbox 1",
        "::give coins 200", "::give beer 1", "::give snake_flute 1",
    },
    run = function(t)
        t.ticklog.start()
        t.quest.bind({
            varp = "varb1404_ratcatch_var",
            constants = { not_started = 0, started = 5, sewer_task = 10, sewer_done = 15, sent_to_jimmy = 20,
                mansion_task = 25, mansion_in = 30, kingrat_task = 80, kingrat_dead = 85, smoke_task = 95,
                smoke_done = 100, charm_bought = 120, tune_played = 125, complete = 127 },
            row = "quest_ratcatchers",
            display = "Ratcatchers",
            points = 2,
        })
        t.ticks(3)
        t.exec("equipCatspeak", t.player.equip, "ics_little_amulet_of_catspeak")

        -- start: Gertrude, west of Varrock ([280..331]). The No branch first, as recorded.
        t.ticklog.mark("leg.start")
        t.exec("start.tele", t.cheat, "::tele 0_49_53_15_22")
        t.exec("start.doorIn", t.player.pass_door, { closed = "fai_varrock_door", open = "fai_varrock_door_open",
            at = { 3151, 3412, 0 }, near = { 3151, 3412 }, far = { 3151, 3411 } })
        t.exec("start.no", t.player.talk_to, "gertrude", 1)
        t.exec("start.no-dialog", t.chat.play, {
            "player:Hello again.", "npc:Oh yes, before I forget - are you still in the adventuring business?",
            "player:I am! Do you have another quest for me?", "npc:Well, I don't", "player:Who's that then?",
            "npc:Well, there are these two ladies", "npc:They called themselves 'The Ratcatchers'.",
            "choose:No.", "player:Interesting, but I'm a bit busy right now.", "npc:Very well.",
        })
        t.chat.close()
        t.expect("start.stage0", t.quest.expect_stage("not_started"))
        t.exec("start.yes", t.player.talk_to, "gertrude", 1)
        t.exec("start.yes-dialog", t.chat.play, {
            "player:Hello again.", "npc:Oh yes, before I forget", "player:I am!", "npc:Well, I don't",
            "player:Who's that then?", "npc:Well, there are these two ladies", "npc:They called themselves",
            "choose:Yes.", "player:Ratcatchers, eh? Where would I find them?", "npc:They told me they generally hang about",
            "player:I wouldn't have thought", "npc:I've had to drag Shilop and Wilough", "npc:I don't know which is harder",
            "player:Thank you Gertrude, I'll be sure to have a look for these ratcatchers.",
        })
        t.expect("start.stage5", t.quest.expect_stage("started"))
        t.chat.close()

        -- sewer: the sisters and the eight rats ([471..778]).
        t.ticklog.mark("leg.sewer")
        t.exec("sewer.tele", t.cheat, "::tele 0_50_154_43_9")
        cat_out(t, "sewer")
        t.exec("sewer.task", t.player.talk_to, "vc_phingspet", 1)
        t.exec("sewer.task-dialog", t.chat.play, {
            "npc:Who are you?", "player:And what do you want?", "player:I'm Gertrude's friend.", "npc:Ratcatchers?",
            "player:Oh yes, we know lots about ratcatching.", "npc:But why should we tell you?",
            "npc:These are our trade secrets, you know.", "player:My cat and I are adventurers.",
            "player:We'll have to confer.", "npc:What do you think?", "player:I don't think cats even do tricks.",
            "npc:Don't be stupid!", "player:I don't know. Seems a bit weird.", "player:I can hear you, you know!",
            "player:We'll set", "npc:Great idea!", "player:Right, if you catch 8 ratsies", "npc:We'll tell you what we know.",
            "player:Sound fair?", "player:I suppose so.", "player:We'll smell your cat, of course!", "player:Um... alright.",
            "npc:Great! Let's have a sniff then.", "mesbox:The sisters pick up your cat", "player:Are you alright, cat?",
            "npc:I'll live.", "npc:Yup. Definitely a cat!", "npc:A real pongy one, too.",
            "npc:Go on then, go splat them ratsies!", "npc:Splat 'em! Squash 'em!", "player:Er, will do.",
        })
        t.expect("sewer.stage10", t.quest.expect_stage("sewer_task"))
        -- Chase Vermin until the eighth: each order is an attempt (the cat can miss, a rat can be
        -- out of reach); the meter and the stage are the grade.
        local orders = 0
        while stage(t) == 10 and orders < 40 do
            orders = orders + 1
            cat_out(t, "sewer.chase" .. orders)
            local r, d = t.player.press("growncat", 4, 10)
            t.ticks(9)
            local _, m = t.var.server("varb1423_ratcatch_ometer")
            t.note("sewer.chase" .. orders .. ": press(growncat, 4) -> " .. tostring(r) .. " "
                .. string.sub(tostring(d), 1, 160) .. "; meter " .. tostring(m))
            if t.chat.kind() ~= "none" then
                t.chat.play({ "player:Good job puss", "npc:What for what now?", "player:Oh never mind",
                    "npc:Speaking of turning faces", "player:Suppose so." })
            end
        end
        t.expect("sewer.stage15", t.quest.expect_stage("sewer_done"))
        local _, meter = t.var.server("varb1423_ratcatch_ometer")
        t.check("sewer.meterReset", meter == 0, "ratcatch_ometer = " .. tostring(meter) .. " after the eighth, in " .. orders .. " orders")
        t.exec("sewer.handback", t.player.talk_to, "vc_grimesquit", 1)
        t.exec("sewer.handback-dialog", t.chat.play, {
            "player:I splatted the ratsies for you.", "npc:...", "player:Why are you talking like that?",
            "npc:Are you making fun of us?", "player:Sorry, I just thought...", "player:So did you splat them ratsies or what?",
            "player:Er, yes, I did... catch the rats.", "npc:Haha, nice!", "player:Splat! Splat!",
            "player:Can you tell me about ratcatching now?", "npc:Ha! Seems you already know plenty!",
            "player:Oh yes, you're a real ratcatching prodigy.", "npc:Reminds me of us when we were little.",
            "player:I'm not sure that's a compliment...", "player:Well, there's nothing more we can teach you.",
            "player:Not much at all!", "player:What? So we've just caught all these rats for nothing?",
            "player:Isn't there anything you can teach me?", "npc:Here, have a rat pole.",
            "mesbox:Grimesquit hands you a rat pole.", "player:Um, thanks. What's this for?",
            "player:You can stick the ratsies your cat splats on it.", "player:Oh. Just what I wanted.",
            "npc:You're welcome!", "npc:Now now, keep your shirt on.", "player:We'll send you to Jimmy.",
            "npc:Ooh, yes, Jimmy will know how to help.", "player:Where do I find this... Jimmy?",
            "player:Jimmy Dazzler hangs out in Ardougne.", "npc:Ooh, and he is dazzling!", "player:Shut up, Grimesquit!",
            "npc:Say hello to Jimmikins from me, won't you?", "player:Er, sure.",
        })
        t.expect("sewer.stage20", t.quest.expect_stage("sent_to_jimmy"))
        t.check("sewer.pole", count(t, "vc_rat_pole") == 1, "rat poles held: " .. count(t, "vc_rat_pole"))

        -- jimmy: the back room of the Flying Horse Inn ([910..947]) and the Directions.
        t.ticklog.mark("leg.jimmy")
        t.exec("jimmy.tele", t.cheat, "::tele 0_40_51_2_58")
        t.exec("jimmy.talk", t.player.talk_to, "vc_jimmy_dazzler", 1)
        t.exec("jimmy.talk-dialog", t.chat.play, {
            "player:Hello.", "npc:Ah, you must be", "player:You... have?", "npc:I have. The Sisters Grime",
            "player:They said you could teach me", "npc:Well, I could, but I need a little more motivation",
            "npc:I didn't get this far", "player:Maybe I could help you out", "player:I am an adventurer after all.",
            "npc:Ah, that's mincemeat to my ears!", "player:Music?", "npc:No thank you!", "npc:How about I test",
            "npc:Take a good long look at this map.", "mesbox:Jimmy hands you a map.", "npc:A client of mine",
            "player:A what?", "npc:A soy - a fancy party.", "npc:This client of mine has a problem",
            "player:He'd be better off calling a doctor", "npc:No, you fool! A rat problem.", "player:So you want me",
            "npc:Ah, but it's not that simple.", "npc:Our wealthier clients", "player:Oh, I see. So I have to do this covertly?",
            "npc:Precisely!", "npc:Now just in case", "player:Are you going to cast an invisibility spell",
            "npc:Not quite.", "player:That's really useful!", "mesbox:Jimmy waves his hands about",
            "player:Where'd you learn that spell, anyway?", "npc:That's a trade secret, m'afraid.",
            "player:Well, can you teach me?", "npc:Oh my, no.", "npc:We already have a serious crime problem.",
            "player:Oh, I see.", "npc:But there's plenty more I can teach you", "player:Do you have any tips for me now?",
            "npc:Of course! Use a cat to catch the rats.", "player:That's it?", "npc:Don't look a gilt horse",
            "player:You mean a gift horse?", "npc:I'll gift you no such thing!", "player:Off I go then...",
        })
        t.expect("jimmy.stage25", t.quest.expect_stage("mansion_task"))
        t.chat.close()
        -- Refused outside Ardougne ([1001]: Varrock's square), followed inside it ([1074]).
        t.exec("jimmy.outside", t.cheat, "::tele 0_50_53_28_30")
        t.exec("jimmy.readOutside", t.player.inv_op, "ratcatchers_party_directions", 1)
        t.exec("jimmy.readOutside-dialog", t.chat.play, { "mesbox:The scroll contains directions",
            "choose:Follow the directions to the house.", "player:These directions are from Ardougne." })
        t.chat.close()
        t.exec("jimmy.inside", t.cheat, "::tele 0_41_51_63_42")
        local from = tile(t)
        t.exec("jimmy.read", t.player.inv_op, "ratcatchers_party_directions", 1)
        t.exec("jimmy.read-dialog", t.chat.play, { "mesbox:The scroll contains directions",
            "choose:Follow the directions to the house." })
        t.ticks(8)
        local g = tile(t)
        t.check("jimmy.garden", g ~= nil and g.x == 2848 and g.z == 5064 and g.level == 0,
            "after the Directions from " .. (from and (from.x .. "," .. from.z) or "?") .. ": "
                .. (g and (g.x .. "," .. g.z .. "," .. g.level) or "?") .. " (the gate, 2848,5064)")

        -- mansion: walk into the left mid guard's sight (his loop is x = 2840, z 5076..5084), be
        -- caught and sent back to the gate; then the trellis.
        t.ticklog.mark("leg.mansion")
        t.exec("mansion.intoSight", t.cheat, "::tele 0_44_79_28_24")
        local caught = t.await({ level = function()
            local _, c = t.var.server("varb14605_ratcatch_guards_caught")
            return c == 1
        end, note = "a guard's catch within 6 tiles" }, 50)
        t.ticks(8)
        local back = tile(t)
        t.check("mansion.caught", back ~= nil and back.x == 2848 and back.z == 5064,
            "after the catch: " .. (back and (back.x .. "," .. back.z) or "?") .. " (the gate checkpoint); await "
                .. tostring(caught))
        t.exec("mansion.toTrellis", t.cheat, "::tele 0_44_79_28_50")
        t.exec("mansion.trellis", t.player.click_loc, "vc_trellis_base", 1, { at = { 2844, 5105, 0 } })
        t.ticks(6)
        local up = tile(t)
        t.check("mansion.upstairs", up ~= nil and up.level == 1, "after the trellis: "
            .. (up and (up.x .. "," .. up.z .. "," .. up.level) or "?"))
        t.expect("mansion.stage30", t.quest.expect_stage("mansion_in"))

        -- kingrat: the warehouse loft copy at stage 80 ([7563..7759]).
        t.ticklog.mark("leg.kingrat")
        t.exec("kingrat.stage", t.cheat, "::setvar varb1404_ratcatch_var 80")
        t.exec("kingrat.tele", t.cheat, "::tele 0_51_52_3_51")
        -- The cat back in the pack (Pick-up, op 1): it follows the tele a tick or two late.
        t.ticks(4)
        if count(t, "growncatobject") == 0 then
            t.player.press("growncat", 1, 10)
            t.inv.await("growncatobject", 1, 10)
        end
        t.check("kingrat.catHeld", count(t, "growncatobject") == 1, "cats held: " .. count(t, "growncatobject"))
        t.exec("kingrat.ladder", t.player.click_loc, "fai_varrock_ladder", 1, { at = { 3268, 3379, 0 } })
        t.ticks(5)
        local loft = tile(t)
        t.check("kingrat.copy", loft ~= nil and loft.x > 6400 and loft.level == 1,
            "up the ladder at stage 80: " .. (loft and (loft.x .. "," .. loft.z .. "," .. loft.level) or "?") .. " (a copy)")
        t.check("kingrat.king", t.npc.nearest("vc_kingrat", 10) == "ok", "the King Rat in the copy")
        use_on_loc(t, "kingrat.catOnWall", "growncatobject", "vc_blank_walldecor")
        t.exec("kingrat.catOnWall-dialog", t.chat.play, {
            "mesbox:The rat inside the room looks vicious.", "choose:Yes", "mesbox:Your cat squeezes through the broken wall.",
            "mesbox:Perhaps you could also supply your cat with food", "choose:Be careful in there, cat!",
            "player:Be careful in there, cat!", "npc:Hisss!", "npc:Pfssst!",
        })
        local fed = 0
        for i = 1, 60 do
            if stage(t) == 85 then break end
            t.ticks(4)
            local _, hurt = t.var.server("varp7879_ratcatch_kr_cat_hurt")
            if hurt ~= nil and hurt >= 3 and stage(t) == 80 and count(t, "tbwt_raw_karambwanji") > 0 then
                -- An attempt, noted: the fight can end on the tick the food lands.
                local wall = t.player.by_symbol("loc", "vc_blank_walldecor")
                local fr, fd = t.player.use_on("tbwt_raw_karambwanji", wall)
                t.note("kingrat.feed" .. fed .. ": " .. tostring(fr) .. " " .. string.sub(tostring(fd), 1, 120))
                fed = fed + 1
            end
        end
        t.expect("kingrat.stage85", t.quest.expect_stage("kingrat_dead"))
        t.note("kingrat: fed " .. fed .. " time(s)")

        -- smoke: Smokin' Joe's hole in Keldagrim ([8109..8334]).
        t.ticklog.mark("leg.smoke")
        t.exec("smoke.stage", t.cheat, "::setvar varb1404_ratcatch_var 95")
        t.exec("smoke.leave", t.player.click_loc, "fai_varrock_laddertop", 1, {})
        t.ticks(4)
        t.exec("smoke.tele", t.cheat, "::tele 0_45_159_51_36")
        t.exec("smoke.weeds", t.player.use_item_on_item, "weeds", "pot_empty")
        t.exec("smoke.pot", t.player.use_item_on_item, "tinderbox", "ratcatchers_weedpot")
        t.check("smoke.potLit", count(t, "ratcatchers_smokey_weedpot") == 1, "lit pots: " .. count(t, "ratcatchers_smokey_weedpot"))
        use_on_loc(t, "smoke.fail", "ratcatchers_smokey_weedpot", "ratcatchers_rathole5")
        t.ticks(26)
        t.exec("smoke.fail-dialog", t.chat.play, { "mesbox:The rats quickly scamper back into their hole",
            "player:Hmm... this isn't working as well as I'd hoped." })
        cat_out(t, "smoke")
        use_on_loc(t, "smoke.learn", "ratcatchers_smokey_weedpot", "ratcatchers_rathole5")
        t.ticks(26)
        t.chat.play({ "mesbox:The rats quickly scamper", "player:Hmm..." })
        use_on_loc(t, "smoke.plan", "ratcatchers_smokey_weedpot", "ratcatchers_rathole5")
        t.exec("smoke.plan-dialog", t.chat.play, { "npc:Hey", "player:Can't you see I'm busy adventuring?",
            "npc:Look, I know you're having fun", "player:Well then smarty paws", "npc:What we have here is a two cat job.",
            "npc:You're smoking the rats out", "player:I can see that for myself, thanks.", "npc:Listen, why don't you leave",
            "player:Great idea. I'll get right to it." })
        use_on_loc(t, "smoke.ambush", "ratcatchers_smokey_weedpot", "ratcatchers_rathole5")
        t.exec("smoke.ambush-dialog", t.chat.play, { "player:Hey cat, you know the drill, get going!",
            "npc:Exterminate! Exterminate!", "player:What?", "npc:Sorry, I got a little carried away there." })
        t.ticks(30)
        t.exec("smoke.after-dialog", t.chat.play, { "player:Hey cat, great job. What an ambush!", "npc:Mua-ha-ha!",
            "player:...?", "player:You know, you really scare me sometimes." })
        t.expect("smoke.stage100", t.quest.expect_stage("smoke_done"))

        -- finale: the tune on Port Sarim's docks ([10288..10391]) and Felkrash ([10494..10506]).
        t.ticklog.mark("leg.finale")
        t.exec("finale.stage", t.cheat, "::setvar varb1404_ratcatch_var 120")
        t.exec("finale.tele", t.cheat, "::tele 0_47_50_26_35")
        t.exec("finale.flute", t.player.inv_op, "snake_flute", 1)
        t.ticks(2)
        -- Every button is an IF1 component: the op-less IF_BUTTON (_ratcatch_flute.lua).
        local function press(sym)
            local _, w = t.ui.widget("ratcatcher_flute:" .. sym)
            t.ui.invoke(w, 0)
            t.ticks(2)
        end
        for i = 1, #TUNE do
            if TUNE[i] == "raised_d" then
                press("rc_flute_up_octave")
                press("rc_flute_d")
            else
                press(TUNE[i])
            end
            if i < #TUNE then press("rc_flute_next_note") end
        end
        press("rc_flute_play")
        t.expect("finale.stage125", t.quest.expect_stage("tune_played"))
        t.ticks(70)
        local docks = tile(t)
        t.check("finale.back", docks ~= nil and docks.x < 6400 and docks.level == 0,
            "after the scene: " .. (docks and (docks.x .. "," .. docks.z) or "?"))
        t.exec("finale.pits", t.cheat, "::tele 0_46_150_33_37")
        t.exec("finale.felkrash", t.player.talk_to, "vc_felkrash_the_bard", 1)
        t.exec("finale.felkrash-dialog", t.chat.play, { "player:Ha! I did it. Did you see?",
            "npc:I did indeed! A performance for the ages.", "npc:Now that you're a real ratcatcher",
            "player:Can you train my cat to be a wily cat?", "npc:I can only train overgrown cats into wily cats." })
        t.ticks(4)
        t.quest.expect_complete()
    end,
}
