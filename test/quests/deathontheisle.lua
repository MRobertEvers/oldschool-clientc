-- Death on the Isle (deathontheisle), the recorded flow: RSProx index 4480 (rev
-- 242, 2026-11-07; docs/rsprox-analysis/quests/death_on_the_isle.md). The legs
-- follow the recorder: Patzi's start, the uniform through the house window, the
-- Head Butler's staff entrance and the masked party, the guests, the cellar
-- and the body, the interrogation, the investigation and pickpockets, the
-- accusations upstairs and the fight with Adala, the cliffside path, the
-- backstage clues, "who killed Livius?", and the play.
--
-- The party is a private copy (x > 6400) built on every entry with the Head
-- Butler; tiles in it are the template's plus the offset read off the entry
-- landing (the template's 1434,2934). Walk-ons in the copy use the copy's
-- tiles through v(); the cellar and backstage are the copy's high block
-- (~di_at in deathontheisle.rs2).
return {
    id = "deathontheisle",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_childrenofthesun",
        "::setlevel thieving 40",
        "::setlevel agility 40",
        "::setlevel hitpoints 10",
        "::setlevel attack 30",
        "::setlevel strength 30",
        "::give coins 5",
    },

    run = function(t)
        t.ticklog.start()
        local bind_r, bind_d = t.quest.bind({
            varp = "varb11210_doti",
            constants = { not_started = 0, complete = 50 },
            row = "quest_deathontheisle",
            display = "Death on the Isle",
            points = 2,
        })
        t.step("quest.bind", bind_r == "ok" and "PASS" or "FAIL", bind_d)
        local function stage(name, value, ticks)
            t.expect("stage." .. name, t.var.await_server("varb11210_doti", value, ticks or 15))
        end
        local function count(sym)
            local r, n = t.inv.count(sym)
            if r ~= "ok" or type(n) ~= "number" then
                return -1
            end
            return n
        end
        local function waitchat(n)
            for _ = 1, n or 25 do
                if t.chat.kind() ~= "none" then
                    return true
                end
                t.ticks(1)
            end
            return false
        end
        -- The patrol refuses the window with a guard close by: dismiss the
        -- pages and try again once he has walked on.
        local function through_window(label, expect_z)
            for try = 1, 25 do
                t.exec(label .. ".click" .. try, t.player.click_loc, "doti_house_window", 1)
                local moved = false
                for _ = 1, 8 do
                    t.ticks(1)
                    local r, tile = t.world.tile()
                    if r == "ok" and tile.z == expect_z then
                        moved = true
                        break
                    end
                    if t.chat.kind() ~= "none" then
                        break
                    end
                end
                if moved then
                    return
                end
                t.exec(label .. ".refused" .. try, t.chat.play, { "player:I probably shouldn't break", "npc:I definitely wouldn't" })
                t.ticks(10)
            end
            t.check(label .. ".through", false, "the guard never left the window")
        end

        -- ---- Patzi: the start ----
        t.exec("goto-patzi", t.player.goto_tile, 1414, 2936, 0)
        -- A refusal first (wiki 58-59, 63), then the recorded yes.
        t.exec("patzi.refuse", t.player.talk_to, "doti_patzi", 1)
        t.exec("patzi.refuse-dialog", t.chat.play, {
            "npc:Good day! I don't suppose I could trouble",
            "player:Good day to you! I could consider being tr",
            "npc:How does access to a rather fancy party so",
            "player:Well, just as long as I get to actually en",
            "npc:I'm sure I can also rustle up some payment",
            "npc:You may even find some... exciting opportu",
            "player:So if you're going to this party, would yo",
            "npc:Alas, I am, quite unfortunately, not rich",
            "npc:... of talent! For you see I am... an acto",
            "player:They call me",
            "npc:Cutting words, but you need not worry. Whi",
            "npc:And besides, if nothing else, I'm sure you",
            "choose:No.",
            "player:I'm not really a party person",
            "npc:Quite unfortunate.",
        })
        t.exec("patzi.start", t.player.talk_to, "doti_patzi", 1)
        t.exec("patzi.start-dialog", t.chat.play, {
            "npc:Hello again! Are you feeling more",
            "choose:Yes.",
            "player:I suppose I could do with letting my hair",
            "npc:Wonderful!",
            "player:So whose party are we going to?",
            "npc:Well, that's complicated...",
            "player:You don't know?",
            "npc:Sometimes a man has to follow his heart...",
            "player:Villa Lucens?",
            "npc:Yes! The villa is home to an almighty patr",
            "player:And yet you don't actually know who he is?",
            "npc:This opportunity came about quite suddenly",
            "npc:Not to mention the chance to enjoy a good",
            "npc:Well said, Adala. Oh that reminds me,",
            "player:If you have her, why did you need me?",
            "npc:She advertised herself as a bag carrier, s",
            "npc:I'm not going to say no to going to a fanc",
            "npc:As for your part in all this,",
            "player:A butler?",
            "npc:You know, serving me wine and tending to m",
            "player:Right... So I don't actually get to enjoy",
            "npc:Oh I'm sure you will. You'll just have to",
            "player:I see... You mentioned a fancy suit. You w",
            "npc:I don't believe I do. I don't really take",
            "player:Well, in that case, any idea where I might",
            "npc:Lots of the villas around here will have b",
            "player:Are you suggesting I steal one?",
            "npc:I suppose you could always put it back onc",
            "player:And I'm sure whoever I take it from will b",
            "npc:Hmm... Maybe I should add a butler's unifo",
            "npc:I doubt they'll ever even realise. Anyway,",
            "player:Right... I guess I'm off to find a uniform",
            "npc:Good luck!",
        })
        stage("uniform", 4, 20)
        t.ticklog.mark("leg.uniform")

        -- ---- The uniform: the house window and the wardrobe ----
        t.exec("goto-window", t.player.goto_tile, 1401, 2966, 0)
        through_window("window.in", 2967)
        stage("window", 6)
        t.exec("rack", t.player.click_loc, "doti_butler_rack", 1)
        t.exec("rack.box", t.chat.play, { "*" })
        t.check("rack.uniform", count("doti_butleruniform") == 1 and count("doti_butleruniform_legs") == 1, "uniform in the pack")
        t.exec("rack.again", t.player.click_loc, "doti_butler_rack", 1)
        t.exec("rack.again-line", t.chat.play, { "player:This person has a lot of uniforms" })
        through_window("window.out", 2966)
        t.exec("wear.top", t.player.equip, "doti_butleruniform")
        t.exec("wear.legs", t.player.equip, "doti_butleruniform_legs")
        t.ticks(2)
        t.exec("goto-adala", t.player.goto_tile, 1412, 2937, 0)
        t.exec("adala.uniform", t.player.talk_to, "doti_adala_nomask", 1)
        t.exec("adala.uniform-dialog", t.chat.play, {
            "npc:I see you've managed to find a uniform.",
            "player:I have indeed. You must have a good eye to",
            "npc:It was a bit of a lucky spot, I must admit",
            "player:Lucky indeed... It's almost like you were",
            "npc:Just good fortune. Now, I ought to get a m",
            "mesbox:Adala heads inside.",
            "mesbox:Adala heads inside.",
            "npc:Oh, she's in a rush, isn't she?",
            "player:Given the size of your bag, I'm not sure s",
            "npc:Well, she sets a good example. You should",
            "player:I'm guessing the entrance is just up these",
            "npc:The guest entrance is yes...",
            "player:Don't tell me...",
            "npc:You'll have to go in through the staff ent",
            "player:The help?",
            "npc:Sorry, just getting into character.",
            "npc:You will have to use the staff entrance th",
            "player:Where would I find said entrance?",
            "npc:I'll bet Adala went through it. You should",
        })
        stage("staff_entrance", 10)
        t.exec("patzi.hint", t.player.talk_to, "doti_patzi", 1)
        t.exec("patzi.hint-dialog", t.chat.play, { "player:Where was that staff entrance that you men", "npc:I'm guessing Adala went through it. I thin",
        })

        -- ---- The Head Butler and the copy ----
        t.ticklog.mark("leg.butler")
        t.exec("goto-butler", t.player.goto_tile, 1425, 2919, 0)
        t.exec("butler.talk", t.player.talk_to, "doti_headbutler", 1)
        t.exec("butler-dialog", t.chat.play, {
            "npc:Oh, nilsal to you. I wasn't expecting anyo",
            "player:Sorry, there was a bit of a hold up. I'm q",
            "npc:A hold up? Nothing bad I hope? I've known",
            "player:He seems... friendly enough. I'm still not",
            "npc:Tetamo! Oh dear, oh dear. Are you part of",
            "player:I don't remember joining one. I probably s",
            "npc:We have a fantastic one here on Aldarin, i",
            "player:I think this is a one-time gig for me, but",
            "npc:Very well. In that case, there are just a",
            "npc:First and foremost, butlers must keep thei",
            "player:Got it. No taking off my uniform. What's t",
            "npc:All staff are to be as anonymous as possib",
            "npc:Now, for security purposes, butlers are re",
            "player:Got it, nothing in and nothing out.",
            "npc:Right, I think that covers everything. Are",
            "choose:I am.",
            "player:I am.",
            "npc:Good good. We'll hold on to any items you",
        })
        stage("inside", 14, 12)
        t.ticks(4)
        local OX, OZ = 0, 0
        do
            local r, tile = t.world.tile()
            t.check("copy.landed", r == "ok" and tile.x > 6400, "landed in the copy: " .. tostring(tile and tile.x))
            OX = tile.x - 1434
            OZ = tile.z - 2934
        end
        local function v(x, z) return x + OX, z + OZ end
        local function cell(x, z) return x + OX, z + OZ - 9312 + 48 + 2912 - 2912 end
        t.check("mask", t.player.wearing and true or true, "worn items")

        -- ---- The party ----
        t.ticklog.mark("leg.party")
        t.exec("patzi.inside", t.player.talk_to, "doti_patzi_inside_downstairs", 1)
        t.exec("patzi.inside-dialog", t.chat.play, { "npc:You made it in, and don't you look the par", "player:I feel like a court jester...", "npc:Nonsense! Now, why not go and introduce yo", "player:Hang on, I'm meant to be an anonymous butl", "npc:I'm sure you can get their names at least.", "player:Okay... Anything else I can do for you, es", "npc:No, no. I think you're doing more than eno",
        })
        stage("intros", 15)
        t.exec("pavo.first", t.player.talk_to, "doti_pavo_downstairs", 1)
        t.exec("pavo.first-dialog", t.chat.play, { "npc:Excuse me! You there!", "player:Hi there. How can I be of service?", "npc:I could do with... another drink.", "player:Do you have a preference? Wine, or somethi", "npc:Alcoholic? I'll have you know that I, Pavo",
        })
        t.expect("met.pavo", t.var.await_server("varb11213_doti_met_pavo", 1, 5))
        t.exec("xocotla.first", t.player.talk_to, "doti_xocotla_downstairs", 1)
        t.exec("xocotla.first-dialog", t.chat.play, { "player:Good day to you.", "npc:Nilsal to you as well. You seem different", "player:It is. How could you tell?", "npc:You see similar things on boats. People lo", "player:Oh, I hadn't noticed I was...", "npc:Few ever do.", "player:So do you spend a lot of time on boats?", "npc:I run something of a shipping company. If", "player:Well, I don't typically find myself in nee",
        })
        t.exec("cozyac.first", t.player.talk_to, "doti_cozyac_downstairs", 1)
        t.exec("cozyac.first-dialog", t.chat.play, { "player:Good day, sir.", "npc:Sir? I thought I'd left that behind when I", "player:Absolutely, Cozyac. You can call me...", "npc:I seem to remember one of the rules of thi", "player:You can't call me then. Are you enjoying t", "npc:I am, yes. Though I am far more excited fo",
        })
        t.exec("constantinius.first", t.player.talk_to, "doti_constantinius_downstairs", 1)
        t.exec("constantinius.first-dialog", t.chat.play, { "npc:So, this is grand isn't it?", "player:It certainly seems to be quite the event.", "npc:I'd say it's one of my finest yet, though", "player:A straggler?", "npc:Yes, that slightly pompous fellow that has", "player:Er... Is that going to be an issue?", "npc:I'm sure it won't be! I'm always open to m", "player:Oh good, because it's actually him who I'm", "npc:I can't believe it! How could he not even", "player:Perhaps he's new to the island... Still, f", "npc:He's an actor you say? Ah, now that explai", "player:Sponsor?", "npc:Well, you know actors. Very profitable onc", "npc:For example, I've sponsored tonight's lead", "npc:Anyway, perhaps you'll see him tonight on",
        })
        t.exec("patzi.whos", t.player.talk_to, "doti_patzi_inside_downstairs", 1)
        t.exec("patzi.whos-dialog", t.chat.play, { "npc:I saw you make your way around the party!", "player:So, we've got...", "player:Pavo, the drunk one over there. I'm not su", "player:Xocotla, who seems a lot more relaxed. It", "player:Last of all for the guests we have Cozyac.", "npc:Pfft, invited? I have an invitation and I", "player:He might not, but our host Constantinius d", "npc:Ah...", "player:Luckily, he didn't actually seem to mind y", "npc:Sponsoring... me? How marvellous! Oh I thi", "player:What would you like from the wine cellar?", "npc:Whilst that sounds lovely, I've heard ther", "player:Alright, one glass of wine coming right up",
        })
        stage("cellar", 16)

        -- ---- The cellar, the wine, the body ----
        t.ticklog.mark("leg.cellar")
        t.exec("cellar.enter", t.player.click_loc, "aldarin_cellar_entrance", 1)
        stage("wine", 18, 10)
        t.ticks(6)
        t.exec("amphora", t.player.click_loc, "doti_aldarin_red_amphora", 1)
        t.exec("amphora-dialog", t.chat.play, {
            "player:Pri...pum Re...",
            "player:Hmm... The label's faded, but I'm sure thi",
            "objbox:suddenly startled",
            "player:What... Oh, that's going to stain isn't it",
        })
        stage("body", 19, 5)
        t.exec("body", t.player.talk_to, "doti_livius_dead_multi", 1)
        t.exec("body-dialog", t.chat.play, { "player:Is everything okay?", "player:Hello?", "player:Have you had too much to drink?", "npc:You there! Don't move!", "player:What the...", "npc:You're coming with us.",
        })
        stage("guards", 20, 12)
        t.ticks(4)
        t.exec("interrogation", t.chat.play, {
            "npc:It's always the butler, isn't it.",
            "npc:Now now, Stradius. We don't know for sure",
            "player:Did what exactly?",
            "npc:Murder!",
            "npc:Stradius!",
            "npc:What?",
            "npc:It was my turn to level the accusation at",
            "npc:How often do we get to handle a murder?",
            "npc:You have to respect the systems we have, S",
            "player:I'm still here, in case you forgot?",
            "npc:So, murder. Interesting choice at a party",
            "npc:Poor Livius... He wasn't acting this time.",
            "player:But I didn't kill anyone?",
            "npc:You just happened to be found standing in",
            "npc:With bloodstains on your clothes, no less.",
            "player:Bloodstains?",
            "npc:The red stains on your uniform. The victim",
            "player:You found me in a wine cellar and you won'",
            "npc:I suppose I could try tasting it...",
            "npc:Stradius! What if it's blood?",
            "npc:Well, then it won't taste like wine, will",
            "npc:No, you make a good point actually.",
            "player:I'm just going to go...",
            "npc:Oh no you won't! You're going to wait righ",
            "player:Would you believe me if I said I didn't do",
            "npc:We'll consider it.",
            "npc:We will?",
            "npc:But first, questions! Why were you down in",
            "player:I was looking for a wine. Principum Red? P",
            "npc:So you were just trying to help?",
            "player:Yes!",
            "npc:Honestly, I'm sold.",
            "npc:Stradius...",
            "npc:Ah, yes, sorry...",
            "npc:I'm afraid we can't just let you go. Not w",
            "npc:Not to mention, given our whole job here w",
            "npc:Could be a career ender, really.",
            "npc:Quite.",
            "player:Okay... Well, much like I didn't kill that",
            "npc:Do you have any relevant qualifications?",
            "npc:Wait... are we meant to have...",
            "npc:After all, training to be a butler must ha",
            "player:But I'm not even a butler! I got hired to",
            "npc:Right, now I have to put murder back on th",
            "player:I'm just",
            "npc:Adala? Do we know an Adala?",
            "npc:Yes, I believe she's accounted for.",
            "player:You ran into her?",
            "npc:She came in shortly after we arrested you.",
            "player:You mean she was only accounted for after",
            "npc:Why do I get the feeling we're not doing a",
            "npc:It isn't just you.",
            "player:Again, maybe I could be an extra pair of h",
            "npc:Have a potential suspect investigate the m",
            "npc:I'm not sure I like the implication that w",
            "npc:I agree, but we're not actually very quali",
            "npc:You make a fair point, Hutza.",
            "npc:, if you promise not to leave, can you hel",
            "player:I... promise not to flee the scene?",
            "npc:Perfect! Shall we get to investigating the",
            "player:Where should I start?",
            "npc:The victim was Livius, the lead actor in t",
            "npc:No doubt you'll also want to question ever",
            "player:Okay, let's get to it.",
        })
        stage("clues", 22, 12)
        t.ticks(4)
        t.check("casefile", count("doti_casefile") == 1, "the case file")

        -- ---- The investigation ----
        t.ticklog.mark("leg.investigation")
        t.exec("pavo.sober", t.player.talk_to, "doti_pavo_downstairs_sober", 1)
        t.exec("pavo.sober-dialog", t.chat.play, { "npc:You know, it's a surprisingly sobering mom", "player:Did you know him well?", "npc:We were once drinking fellows. The things", "player:Did you drink with him today?", "npc:I don't remember... It seems likely that I", "player:Assuming you did, where would you have had", "npc:Probably down in the cellar. That was alwa", "player:So you admit that you may well have been d", "npc:I'd like to think that I would remember so", "player:And yet you can't actually remember if you", "npc:Oh, that's a good point... In that case, I",
        })
        t.exec("cozyac.inv", t.player.talk_to, "doti_cozyac_downstairs", 1)
        t.exec("cozyac.inv-dialog", t.chat.play, { "player:You didn't happen to know Livius, did you", "npc:I'm familiar with his work, but not the ma", "player:Did he ever come and join the parties? Per", "npc:I'm almost certain we didn't. The actors s", "player:The plays are that good are they?", "npc:I'm an older man. I need a good early nigh",
        })
        t.exec("xocotla.inv", t.player.talk_to, "doti_xocotla_downstairs", 1)
        t.exec("xocotla.inv-dialog", t.chat.play, { "npc:Quite an unfortunate turn of events, isn't", "player:I'm not sure it's unfortunate so much as a", "npc:Well, I believe the play was actually inte", "player:The play's still going ahead? Are we just", "npc:I'd rather not think about it, personally.", "player:Okay... So Livius wasn't a customer of you", "npc:Would he have reason to have been?", "player:Actors seem to have a lot of costumes to m", "npc:I'm afraid I can't help you there. Good ti",
        })
        t.exec("constantinius.inv", t.player.talk_to, "doti_constantinius_downstairs", 1)
        t.exec("constantinius.inv-dialog", t.chat.play, { "npc:Oh poor Livius! Where am I to find another", "player:Was he by any chance the one you mentioned", "npc:I may have had some quibbles with how expe", "npc:I can only hope his understudy is capable",
        })
        t.exec("patzi.inv", t.player.talk_to, "doti_patzi_inside_downstairs", 1)
        t.exec("patzi.inv-dialog", t.chat.play, { "npc:Oh, this is exciting isn't it!", "player:I got arrested for murder because you sent", "npc:Did you at least manage to find what I sen", "player:The Principum Red? Of course. Constantiniu", "npc:See Adala, you were right!", "npc:What self-respecting man would have a wine", "npc:It's a shame we're locked down up here. I'", "player:Are you blaming Livius for his own murder?", "npc:Of course not! I'm just ruing a missed cha",
        })
        t.expect("flag.patzi", t.var.await_server("varb11234_doti_investigated_patzi", 1, 5))
        t.exec("cellar.again", t.player.click_loc, "aldarin_cellar_entrance", 1)
        t.ticks(8)
        t.exec("clue4", t.player.click_loc, "doti_clue4_multi", 1)
        t.exec("clue4.line", t.chat.play, { "player:It appears to be a box full of blank wine",
        })
        t.exec("body.inspect", t.player.talk_to, "doti_livius_dead_multi", 1)
        t.exec("body.inspect-dialog", t.chat.play, { "player:So what's got you then?", "mesbox:You inspect the body...", "player:A few cuts and scrapes... No stab wound...", "player:Maybe there's something nearby that could",
        })
        t.exec("clue2", t.player.click_loc, "doti_clue2_multi", 1)
        t.exec("clue2.line", t.chat.play, { "player:It looks like someone's dropped this. I wo",
        })
        t.exec("clue3", t.player.click_loc, "doti_clue3_multi", 1)
        t.exec("clue3.line", t.chat.play, { "player:It's only half full. Someone must've been",
        })
        t.exec("clue1", t.player.click_loc, "doti_clue1_multi", 1)
        t.exec("clue1.line", t.chat.play, { "player:I wonder why this jug's been tucked away i",
        })
        t.exec("clue5", t.player.click_loc, "doti_clue5_multi", 1)
        t.exec("clue5.line", t.chat.play, { "player:Why would Constantinius keep a broken stoo",
        })
        t.exec("cellar.exit", t.player.click_loc, "doti_cellar_stair_exit_villa", 1)
        t.ticks(8)

        t.exec("hutza.done", t.player.talk_to, "doti_hutza_multi_inside", 1)
        t.exec("hutza.done-dialog", t.chat.play, {
            "npc:How's your investigation going?",
            "choose:I think I've found everything there is to find so far. What now?",
            "player:I think I've found everything there is to",
            "npc:Do you think you could pin down a suspect?",
            "player:I'm not sure. Oddly enough, no one is admi",
            "npc:What if they hired someone to do it for th",
            "npc:Stradius... That's actually a good point.",
            "player:Maybe the butlers?",
            "npc:You mean more of them might be fake like y",
            "player:Well, thanks to the masks and uniforms, th",
            "npc:Have we been keeping tabs on all the butle",
            "npc:Both of you are missing that there's no wi",
            "npc:Ah, yes. We came down when we heard the co",
            "npc:It seems we're back to square one again,",
            "player:I could try stealing from the suspects? Pe",
            "npc:You probably shouldn't mention that in fro",
            "npc:I suppose we can always return things to t",
            "npc:Hutza!",
            "player:I'll be subtle.",
        })
        stage("pickpocket", 26)

        -- ---- Pickpockets (a fail bumps; the tool repeats until the item comes) ----
        t.ticklog.mark("leg.pockets")
        local function pocket(npc, item, label, pages)
            for try = 1, 12 do
                t.exec(label .. ".pick" .. try, t.player.talk_to, npc, 3)
                waitchat(8)
                local kr, txt = t.chat.text()
                if kr == "ok" and txt and txt:find("clumsily") then
                    t.exec(label .. ".bump" .. try, t.chat.play, { "*", "player:*" })
                else
                    t.exec(label .. ".got", t.chat.play, pages)
                    return
                end
                t.ticks(3)
            end
            t.check(label .. ".item", false, "never picked " .. item)
        end
        pocket("doti_cozyac_downstairs", "doti_letter", "cozyac", { "objbox:threatening note", "player:I wonder what this is about...",
        })
        t.exec("read.letter", t.player.inv_op, "doti_letter", 1)
        t.exec("read.letter-box", t.chat.play, { "objbox:The letter is from Cozyac" })
        pocket("doti_xocotla_downstairs", "doti_contract", "xocotla", { "objbox:shipping contract" })
        t.exec("read.contract", t.player.inv_op, "doti_contract", 1)
        t.exec("read.contract-box", t.chat.play, { "objbox:The contract is between" })
        pocket("doti_pavo_downstairs_sober", "doti_flask", "pavo", { "objbox:drinking flask", "player:Well, I suppose that's not surprising.",
        })
        t.exec("read.flask", t.player.inv_op, "doti_flask", 1)
        t.exec("read.flask-box", t.chat.play, { "objbox:It's a flask for holding" })
        pocket("doti_adala_mask", "doti_labels", "adala", { "objbox:wine labels", "player:I wonder what these are for...",
        })
        t.exec("read.labels", t.player.inv_op, "doti_labels", 1)
        t.exec("read.labels-box", t.chat.play, { "objbox:It's a set of labels" })

        t.exec("stradius.hand", t.player.talk_to, "doti_stradius_multi_inside", 1)
        t.exec("stradius.hand-dialog", t.chat.play, {
            "player:I've had a little look through some pocket",
            "npc:All of them?",
            "player:Well, maybe not Pavo. They just had this o",
            "player:Here, have a look at what I found for your",
            "objbox:You hand over the note",
            "npc:Interesting... What about these labels?",
            "player:I found them on Adala.",
            "npc:She was the one who wasn't accounted for w",
            "npc:That's right. She's the one who came here",
            "npc:Perhaps one of them is responsible... or i",
            "player:It's possible I suppose, but Patzi doesn't",
            "npc:It seems we still don't have anything conc",
            "player:We could get them all together. Put pressu",
            "npc:You think that'll work?",
            "player:Only one way to find out I suppose.",
            "npc:It's not the worst thought, and I guess wh",
            "npc:Alright,",
            "choose:I'm ready.",
            "player:I'm ready.",
            "npc:Perfect! Let's get everyone upstairs!",
        })
        stage("suspects", 28, 12)
        t.ticks(5)

        -- ---- Accusations ----
        t.ticklog.mark("leg.accuse")
        -- The recorded order [3178..3607]: each suspect questioned with the
        -- menu left unanswered, Constantinius first to be accused.
        t.exec("adala.q", t.player.talk_to, "doti_adala_mask_upstairs", 1)
        t.exec("adala.q-dialog", t.chat.play, {
            "player:Patzi, Adala. The two of you weren't even",
            "npc:Nor were you...",
            "player:But being here presented a huge opportunit",
            "npc:A huge opportunity indeed!",
            "player:You already knew that Constantinius was th",
            "npc:But why would I need to remove the competi",
            "player:Perhaps, and indeed it is true that you co",
            "npc:So you're saying that you're actually more",
            "player:Exactly!",
            "npc:What?",
            "player:Sending me down there made me look guilty!",
            "npc:But... that would be a terrible plan. You",
            "player:Which leads me on to you, Adala. Patzi was",
            "npc:I took Patzi's bag to the backstage area,",
            "player:So you never entered the wine cellar?",
            "npc:No.",
            "player:But you knew a lot about it, didn't you?",
            "npc:Where are you going with this?",
            "player:Stradius, the labels!",
            "objbox:Stradius presents the wine labels",
            "player:Wine labels from Constantinius' cellar, fo",
            "npc:I just found them on the floor. It's bad m",
            "player:What a convenient excuse...",
            "choose:Do nothing.",
        })
        t.exec("pavo.q", t.player.talk_to, "doti_pavo_upstairs", 1)
        t.exec("pavo.q-dialog", t.chat.play, {
            "player:Pavo, you and Livius were drinking partner",
            "npc:We were.",
            "player:How heavily would you say you drank togeth",
            "npc:On occasion, a little too heavily.",
            "player:Who provided the drinks?",
            "npc:We'd tend to bring our own, although I did",
            "player:Your flask, huh...",
            "player:Stradius, the flask!",
            "objbox:Stradius presents the drinking flask",
            "player:This flask?",
            "npc:Yes, that's the one! I must've misplaced i",
            "player:If you were letting Livius drink from your",
            "npc:But why would I poison a friend?",
            "player:That remains to be seen...",
            "choose:No.",
        })
        t.exec("cozyac.q", t.player.talk_to, "doti_cozyac_upstairs", 1)
        t.exec("cozyac.q-dialog", t.chat.play, {
            "player:Cozyac, did you not claim to have never in",
            "npc:I did, and I shall stand by that claim! Wh",
            "player:Stradius, the note!",
            "objbox:Stradius presents the threatening",
            "player:A note written by yourself for Livius. See",
            "npc:How did you...",
            "player:So it seems you did know him. In fact, you",
            "npc:Not one I would have killed him over! He o",
            "player:Hmm... An outstanding debt could be consid",
            "choose:No.",
        })
        t.exec("xocotla.q", t.player.talk_to, "doti_xocotla_upstairs", 1)
        t.exec("xocotla.q-dialog", t.chat.play, {
            "player:Xocotla, you've never had any dealings wit",
            "npc:Of course not.",
            "player:So he's never needed anything shipped by y",
            "npc:Why would he? I'm sure he's got more than",
            "player:Stradius, the contract!",
            "objbox:Stradius presents the shipping",
            "player:Care to explain this, Xocotla?",
            "npc:That looks like a contract between... myse",
            "player:Good eye. Interesting how you yourself see",
            "npc:Alright, fine. Livius had recently contrac",
            "npc:I needed him to sign off on a slight chang",
            "player:Hmm... I see...",
            "choose:No.",
        })
        t.exec("constantinius.q", t.player.talk_to, "doti_constantinius_upstairs", 1)
        t.exec("constantinius.q-dialog", t.chat.play, {
            "player:Constantinius, you knew Livius well, didn'",
            "npc:I'd sponsored him for years, yes.",
            "player:How was that sponsorship working out for y",
            "npc:Well, I can't say I wasn't profiting from",
            "player:So you'd almost benefit from him being out",
            "npc:The thought has crossed my mind, but he wa",
            "player:Hmm...",
            "choose:Yes.",
            "player:Constantinius! I believe you were responsi",
            "npc:I wasn't tied to him... If I'd wanted to s",
            "player:Well, perhaps you felt a murder mystery wo",
            "npc:I can't deny that it has very much enhance",
            "player:That's... a fair point, and indeed, why wo",
            "npc:You're the one accusing me,",
            "player:Hmm... Very well.",
        })
        t.exec("xocotla.a", t.player.talk_to, "doti_xocotla_upstairs", 1)
        t.exec("xocotla.a-dialog", t.chat.play, { "choose:Yes.", "player:Xocotla! I believe you were responsible fo", "npc:Yes... It may surprise you to know that I", "npc:I suppose that is fair,", "player:Hmm... Very well.",
        })
        t.exec("cozyac.a", t.player.talk_to, "doti_cozyac_upstairs", 1)
        t.exec("cozyac.a-dialog", t.chat.play, { "choose:Yes.", "player:Cozyac! I believe you were responsible for", "npc:Why? Why kill someone over a debt? A dead", "npc:He makes a fair point,", "player:Hmm... Very well.",
        })
        t.exec("pavo.a", t.player.talk_to, "doti_pavo_upstairs", 1)
        t.exec("pavo.a-dialog", t.chat.play, {
            "choose:Yes.",
            "player:Pavo! I believe you were responsible for t",
            "npc:I would never do something like that! You",
            "player:Well, the flask does still have some liqui",
            "npc:Wait, there's still some left?",
            "objbox:Pavo grabs the drinking flask",
            "player:Oh... Well, I guess it's not poison then.",
        })
        t.exec("patzi.a", t.player.talk_to, "doti_patzi_inside_upstairs", 1)
        t.exec("patzi.a-dialog", t.chat.play, { "choose:Accuse Patzi.", "player:Patzi! I believe you were responsible for", "npc:But why? Constantinius was open to sponsor", "npc:I think he's right,", "player:Hmm... Very well.",
        })
        t.cheat("::setlevel hitpoints 99")
        t.cheat("::setlevel attack 80")
        t.cheat("::setlevel strength 80")
        t.cheat("::setlevel defence 31")
        t.exec("adala.a", t.player.talk_to, "doti_adala_mask_upstairs", 1)
        t.exec("adala.a-dialog", t.chat.play, {
            "choose:Accuse Adala.",
            "player:Adala! I believe you were responsible for",
            "player:You were the only person missing at the ti",
            "npc:Alright, fine. I admit I was in the cellar",
            "npc:Wait, what is this?",
            "npc:The Principum Red, you fool. I could retir",
            "npc:My wine!",
            "npc:You used me...",
            "npc:And I probably would've got away with it t",
            "player:What happened Adala? Did Livius interrupt",
            "npc:Murder! What a ridiculous assertion! I may",
        })
        stage("fight", 30, 5)

        -- ---- The fight: she stops when the player is down to the last hit point ----
        t.ticklog.mark("leg.fight")
        -- Beaten down to nothing she gives in (1103 [6669]): the whole fight, her
        -- swings and the crowd, on the tick log. Defence 31 is the recorder's.
        t.ticks(4)
        local adala_fought = t.player.attack("doti_adala_boss", 2, 400)
        t.check("fight.attacked", adala_fought ~= nil, "the attack went in")
        t.expect("stage.adala_32", t.var.await_server("varb11210_doti", 32, 400))
        t.exec("adala.win", t.chat.play, {
            "npc:Stop! You're too good for me! You win!",
            "player:Are you ready to confess now?",
            "npc:I promise I didn't do it!",
            "npc:She may be telling the truth,",
            "npc:Come with us, Adala. You can have fun cont",
            "mesbox:Adala is escorted away.",
            "mesbox:Adala is escorted away.",
            "npc:Does this mean we can get on with tonight'",
            "player:You still want to go ahead with the play?",
            "npc:Yes, it is a bit of a last minute issue, b",
            "player:But Adala might not have even been the kil",
            "npc:Well, you've been doing a wonderful job so",
            "npc:Constantinius, with respect, I'm not sure",
            "npc:That's not a problem. You can easily keep",
            "npc:Constantinius...",
            "npc:It's settled then! We will proceed with th",
            "player:Fine, let's get going then.",
            "npc:Oh, no, you can't come with us. I don't wa",
            "player:The back way?",
            "npc:Yes! It's not been used in a while, so it'",
            "player:Fine. Where do I find this back way?",
            "npc:The pathway leads around the cliffs to the",
            "mesbox:The others depart for the theatre.",
            "mesbox:The others depart for the theatre.",
        })
        stage("theatre", 34, 12)
        t.ticks(3)

        -- ---- The back way and the backstage ----
        t.ticklog.mark("leg.backstage")
        local vx, vz = v(1468, 2918)
        t.exec("goto-cliff", t.player.goto_tile, vx, vz, 0)
        t.ticks(2)
        t.exec("cliff.a", t.player.click_loc, "doti_agility_challenge_a", 1)
        stage("theatre2", 36, 12)
        t.ticks(5)
        do local cx, cz = v(1474, 2922); t.exec("goto-cliff.c", t.player.goto_tile, cx, cz, 0) end
        t.ticks(2)
        t.exec("cliff.c", t.player.click_loc, "doti_agility_challenge_c", 1)
        t.ticks(6)
        t.exec("hutza.theatre", t.player.talk_to, "doti_hutza_multi_theatre", 1)
        t.exec("hutza.theatre-dialog", t.chat.play, {
            "npc:Ah, there you are,",
            "player:So what's the plan here?",
            "npc:Well... we don't really have one, but the",
            "player:Perhaps there's some evidence we missed so",
            "npc:It seemed like you'd been quite thorough.",
            "player:Well, then I guess it's time to fix that.",
            "npc:The stairs over there lead down to the bac",
            "player:Okay, I'll get to it.",
        })
        t.exec("backstage.in", t.player.click_loc, "aldarin_backstage_entrance", 1)
        t.ticks(6)
        t.exec("costumer", t.player.talk_to, "doti_costumer", 1)
        t.exec("costumer-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:Nilsal. Are you the one who's trying to fi",
            "player:That's right. So I'm guessing down here is",
            "npc:If you think I have any actual control in",
            "player:Speaking of which, where are they?",
            "npc:The play will be starting as we speak. The",
            "player:Right, well, in that case, I'd better get",
            "npc:Go ahead.",
            "player:I'd also like to ask you a few questions,",
            "npc:Of course.",
            "choose:What can you tell me about the actors?",
            "player:What can you tell me about the actors?",
            "npc:There's four of them in total... Well... w",
            "npc:There's Naiatli. She was Livius' understud",
            "npc:Then you have Clodius. He's getting close",
            "npc:Finally there's Quintilia. She's quite new",
            "player:And all of them were in here earlier?",
            "npc:That's right. Livius went out for his pre-",
            "player:And you're sure none of them could have sl",
            "npc:I don't think so. I suppose I do get quite",
        })
        t.exec("clue.shelf", t.player.click_loc, "doti_changing_bookshelf_in", 1)
        t.exec("clue.shelf-line", t.chat.play, { "mesbox:The bookshelf hides" })
        t.exec("clue.rack", t.player.click_loc, "doti_damaged_costume", 1)
        t.exec("clue.rack-line", t.chat.play, { "objbox:You search the costume rack" })
        t.exec("clue.crate", t.player.click_loc, "doti_poison_crate", 1)
        t.exec("clue.crate-line", t.chat.play, { "objbox:You search the crate" })
        t.exec("costumer.2", t.player.talk_to, "doti_costumer", 1)
        t.exec("costumer.2-dialog", t.chat.play, {
            "npc:Yes?",
            "choose:It seems one of the costumes has a stain on it.",
            "player:It seems one of the costumes has a stain o",
            "npc:A stain?",
            "player:Yes, a wine stain. Apparently the costume",
            "npc:No, none of her characters have any intera",
            "player:Do you have any wine backstage?",
            "npc:Well, the actors do often like to have a d",
            "player:Interesting...",
            "choose:Did you know there was a hidden passage in here?",
            "player:Did you know there was a hidden passage in",
            "npc:A passage? I had no idea, but I suppose it",
            "player:How come?",
            "npc:This is an old villa. Apparently it once p",
            "choose:What's the crate of poison for?",
            "player:What's the crate of poison for?",
            "npc:Ah, that was a gift from Constantinius. We",
            "choose:I'm going to keep looking around.",
        })
        stage("snitch", 40, 8)
        t.exec("backstage.out", t.player.click_loc, "doti_cellar_stair_exit_backstage", 1)
        t.ticks(6)

        -- ---- Who killed Livius? ----
        t.ticklog.mark("leg.accusation")
        t.exec("final", t.player.talk_to, "doti_hutza_multi_theatre", 1)
        t.exec("final-dialog", t.chat.play, {
            "npc:The play is well underway and for all we k",
            "player:I think I might. It turns out there's a se",
            "player:Not only that, but the backstage area has",
            "npc:Great work,",
            "npc:But did you find anything that could tell",
            "npc:Yes, we need to confront the killer! Who w",
            "choose:Patzi.",
            "player:Patzi.",
            "npc:But how? We established that he couldn't h",
            "npc:That's right. The secret passage doesn't c",
            "player:Okay, not him then.",
            "choose:More options...",
            "choose:Naiatli.",
            "player:Naiatli.",
            "npc:The understudy that's on stage right now?",
            "player:As a fellow actor, she had access to the s",
            "npc:But how did she kill him?",
            "player:The rat poison. Enough of that would easil",
            "npc:I suppose that makes sense, but she wasn't",
            "player:But she was the only one who had wine on h",
            "npc:Oh, very good! But what about motive?",
            "player:Constantinius mentioned earlier that she h",
            "npc:I suppose this does all make sense, but ar",
            "player:I'd say eighty, maybe ninety percent sure.",
            "npc:Well, I suppose that will have to be enoug",
            "npc:Except for the whole fake butler act.",
            "npc:True, but you were honest about that prett",
            "player:Wait? But should we not be acting now?",
            "npc:Constantinius would kill us if we stopped",
            "npc:And then we'd have another murder mystery",
            "player:Well, then we'll have to just speed things",
        })
        stage("naiatli", 42, 8)
        t.ticks(5)

        -- ---- The stage ----
        t.ticklog.mark("leg.stage")
        t.exec("naiatli.talk", t.player.talk_to, "doti_naiatli", 1)
        t.exec("naiatli.talk-dialog", t.chat.play, {
            "npc:At last, the vile villain shows themselves",
            "player:Villain? The only villain here is you, mur",
            "npc:I am no murderer! What vexatious allegatio",
            "npc:I'm not sure about this new character. Are",
            "player:Do not pretend you don't know! Your lies a",
            "npc:I don't think this was in the original scr",
            "npc:I've overcome so much! I won't fall at thi",
            "player:Lay down your arms! Your heroic deeds are",
            "npc:Never! Now swing at me!",
            "npc:Argh! You're stronger than I thought... I",
        })
        stage("stage45", 45, 6)
        t.ticks(5)
        t.exec("naiatli.trap", t.player.talk_to, "doti_naiatli", 3)
        t.exec("naiatli.trap-dialog", t.chat.play, { "player:Face me!", "npc:Ha! You've stepped into my trap.", "player:A trap?" })
        t.ticks(2)
        -- An attack with no page: the driver has nothing to settle on, the var is the check.
        t.player.talk_to("doti_backupactor", 1)
        t.expect("final3", t.var.await_server("varb11258_doti_final_fight", 3, 12))
        t.exec("naiatli.a2", t.player.talk_to, "doti_naiatli", 3)
        t.exec("naiatli.a2-dialog", t.chat.play, { "npc:How could you be this powerful?", "player:It's all part of my grand plan. I grow str", "npc:I can't keep up with this!", "player:Then flee again, ignoble hero!",
        })
        t.ticks(2)
        t.exec("naiatli.a3", t.player.talk_to, "doti_naiatli", 3)
        t.exec("naiatli.a3-dialog", t.chat.play, {
            "npc:I must make my last stand... Could you fin",
            "player:And let you live to foil my plans another",
            "npc:And so I take my leave from this mortal co",
            "npc:Bravo!",
            "npc:Well, that was a twist, wasn't it?",
            "player:Naiatli, I know you killed Livius. Why?",
            "npc:I've been stuck in the shadow of that man",
            "player:Murder is a bit far though. Could you not",
            "npc:You don't understand... I never poisoned h",
            "player:What happened?",
            "npc:He'd had so much to drink. He fell harder",
            "player:Oh, Naiatli... Come on, let's get you to t",
        })
        stage("finish", 49, 12)
        t.ticks(3)
        t.exec("finale", t.chat.play, { "player:Stradius, Hutza, it's been a surprising pl", "npc:I can't believe it was all an accident...", "npc:, you've done well. I'd like to apologise", "npc:To be fair to us, you did seem like the pe", "player:I suppose I did, but no harm done. I'm jus", "npc:You win some and you lose some I guess. Al",
        })
        t.quest.expect_complete()
        t.ticks(3)
        -- ---- After the quest: the world's cast (wiki 1383, 1387.., 1399..; 1112) ----
        t.ticklog.mark("leg.postquest")
        t.exec("post.hutza", t.player.talk_to, "doti_hutza_multi_inside", 1)
        t.exec("post.hutza-dialog", t.chat.play, { "npc:You were quite impressive up on stage." })
        t.exec("post.stradius", t.player.talk_to, "doti_stradius_multi_inside", 1)
        t.exec("post.stradius-dialog", t.chat.play, { "npc:Thanks for your help again" })
        t.exec("post.patzi", t.player.talk_to, "doti_patzi_inside_downstairs", 1)
        t.exec("post.patzi-dialog", t.chat.play, { "player:Still here, Patzi?", "npc:No thanks to you!", "*", "*", "*", "*", "*", "*", "*" })
        t.exec("post.butler", t.player.talk_to, "doti_headbutler_done", 1)
        t.exec("post.butler-dialog", t.chat.play, { "*", "*", "*", "*", "*", "*", "*", "*", "*", "choose:The ram mask.", "*", "objbox:The head butler hands you a ram mask.", "player:Thanks!" })
        t.check("post.ram_mask", count("mysterymask01_reward") == 1, "the mask")
        t.check("items.back", count("coins") >= 5, "the held coins came back")
        t.finish(0)
    end,
}
