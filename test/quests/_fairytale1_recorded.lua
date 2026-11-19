-- Fairytale I - Growing Pains in the order RSProx index 4605 recorded it
-- (docs/rsprox-analysis/quests/fairytale1.md): Martin's start, five gardeners
-- (Frizzy, Elstan, Heskel, Fayeth, Treznor), Martin, Zanaris through the shed,
-- the Godfather, Fairy Nuff, Zandar, Malignius and the grave, the Nature
-- Spirit's ceremony, the Shady Grove's copy and the Tanglefoot, the Godfather.
-- Reaches each place by teleport (the walked route is not built: Open in the
-- doc); every conversation is the recorded one.
--
--     python3 tools/quest_gate/run.py --no-build --script test/quests/_fairytale1_recorded.lua --name fairytale1_recorded
local ITEMS = {
    [0] = "white_berries", "mortmyrepear", "mortmyrebuddingstem", "mortmyremushroom", "nature_talisman",
    "avantoe", "irit_leaf", "blue_dragon_scale", "mosquito_proboscis", "jangerberries", "cactus_potato",
    "crushed_gemstone", "snapdragon", "bucket_supercompost", "volencia_moss", "babydragon_bones",
    "uncut_diamond", "raw_cave_eel", "edible_seaweed", "oystershell", "charcoal", "red_vine_worm",
    "snail_corpse3", "red_spiders_eggs", "mort_slimey_eel", "grapes", "uncut_ruby", "tbwt_jogre_bones",
    "king_worm", "snape_grass", "lime",
}

-- The journal as Jagex sent it (4605), compared as plain text.
local J_START = {
    "<str>I spoke to Martin the Master Gardner who told me the",
    "<str>gardeners are suffering unseasonal bad harvests.",
}
local J_ZANARIS = {
    "<str>I have spoken to the gardeners who are members of the",
    "<str>Group of Advanced Gardeners. They sent me to Zanaris to",
    "<str>talk to the Fairy Queen.",
    "<str>I discovered that the Fairy Queen was missing. The Fairy",
    "<str>Godfather told me that she was extremely sick.",
}
local function join(a, b)
    local out = {}
    for _, r in ipairs(a) do out[#out + 1] = r end
    for _, r in ipairs(b) do out[#out + 1] = r end
    return out
end

return {
    id = "fairytale1_recorded",
    fixture = "fresh_lumbridge.ini",
    max_frames = 160000,
    setup = {
        "::clearinv",
        "::complete quest_lostcity",
        "::complete quest_naturespirit",
        "::give dramen_staff 1",
        "::give amulet_of_ghostspeak 1",
        "::give secateurs 1",
        "::give spade 1",
        "::give lobster 10",
        "::give coins 41000",
        "::setlevel farming 57",
        "::setlevel attack 70",
        "::setlevel strength 70",
        "::setlevel defence 70",
        "::setlevel hitpoints 70",
        "::setlevel prayer 99",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb1803_fairy_farmers_quest",
            constants = {
                not_started = 0, started = 10, gardeners_done = 20, godfather = 30, nuff = 40,
                zandar = 50, malignius = 60, enchanted = 70, secateurs = 80, complete = 90,
            },
            row = "quest_fairytale1",
            display = "Fairytale I - Growing Pains",
            points = 2,
        })
        t.ticklog.start()
        local function stage(name, want)
            local _, v = t.var.server("varb1803_fairy_farmers_quest")
            t.check("stage." .. name, v == want, "stage " .. tostring(v) .. " want " .. tostring(want))
        end
        local function plain(s)
            return (string.gsub(string.gsub(s, "<[^>]*>", ""), "^%s*(.-)%s*$", "%1"))
        end
        local function journal(name, want)
            local r, j = t.ui.journal_open("Fairytale I - Growing Pains")
            if r == "ok" then t.ui.journal_close() end
            local lines = (r == "ok" and j and j.lines) or {}
            local diff = nil
            for i = 1, math.max(#want, #lines) do
                if lines[i] ~= (want[i] and plain(want[i])) then
                    diff = "row " .. i .. ": want '" .. tostring(want[i]) .. "' read '" .. tostring(lines[i]) .. "'"
                    break
                end
            end
            t.check("journal." .. name, r == "ok" and diff == nil, tostring(r) .. " " .. #lines .. " rows; " .. (diff or "every row matches"))
            t.ticks(2)
        end
        local function await_page(n)
            for _ = 1, n or 12 do
                if t.chat.kind() ~= "none" then return end
                t.ticks(1)
            end
        end
        local function tele(name, coord)
            t.check(name .. ".tele", t.cheat("::tele " .. coord) == "ok", coord)
            t.ticks(3)
        end
        -- Each page's raw text, <br>s and all, against the recording (chat.play
        -- compares with the breaks read as spaces); "choose:" picks an option.
        local function play_exact(name, entries)
            for i, e in ipairs(entries) do
                local kind, arg = e:match("^(%a+):(.*)$")
                if kind == "choose" then
                    local r, d = t.chat.choose(arg)
                    t.check(name .. ".choose" .. i, r == "ok", tostring(r) .. " " .. tostring(d))
                    t.ticks(1)
                else
                    await_page(12)
                    local want_kind = (kind == "player") and "player" or kind
                    local got_kind = t.chat.kind()
                    local r, raw = t.chat.text()
                    local ok = (got_kind == want_kind) and r == "ok" and raw ~= nil
                    if ok then
                        if arg:find("<br>", 1, true) then
                            ok = raw == arg
                        else
                            ok = string.sub(string.gsub(raw, "%s*<br>%s*", " "), 1, #arg) == arg
                        end
                    end
                    t.check(name .. ".page" .. i, ok, "want " .. kind .. " '" .. arg .. "' got " .. tostring(got_kind) .. " '" .. tostring(raw) .. "'")
                    if not ok then return end
                    t.chat.continue_()
                    t.ticks(1)
                end
            end
        end
        local function talk(name, npc, pages)
            local r, d
            for try = 1, 6 do
                r, d = t.player.talk_to(npc, 1)
                if r == "ok" or t.chat.kind() ~= "none" then break end
                t.note(name .. "-try" .. try .. ": " .. tostring(r) .. " " .. tostring(d))
                t.ticks(3)
            end
            await_page()
            play_exact(name, pages)
            t.ticks(2)
        end

        -- Martin, the start [76..128].
        t.ticklog.mark("leg.martin")
        tele("martin", "0_48_50_6_57")
        talk("martin.start", "martin_the_master_farmer", {
            "choose:Ask about the quest.",
            "player:Hello.",
            "npc:I can't chat now, I have too many things to worry<br>about.",
            "choose:What is the banner for?",
            "player:What is the banner for?",
            "npc:That banner is the emblem of the Group of Advanced<br>Gardeners. We're a loosely affiliated group of gardeners<br>that are united in our concern for declining vegetable<br>production.",
            "player:Err, of course you are.",
            "npc:Say, you're a brave adventurer, would you be<br>interested in trying to get to the root of our woes?<br>There would be a cash reward if you could help us.",
            "choose:Of course I'll help.",
            "player:Of course I'll help.",
            "npc:I'm so glad to hear that.",
            "player:You don't look it.",
            "npc:Sorry, what was that?",
            "player:Oh, nothing. Don't worry about it.",
            "player:So tell me about your problems.",
            "npc:It all seemed to start about a month ago. I grow roses,<br>you know?",
            "player:Um, you seem to be getting side-tracked already.",
            "npc:What? Oh sorry. ",
            "npc:As I was saying about a month ago I noticed that the<br>yield on my prize rose bushes had dropped slightly. I<br>immediately suspected Crown Gall, a disease that can be<br>fatal to a Mister Lincoln.",
            "player:You're not making much sense. Who's Mister Lincoln?",
            "npc:Sorry, Mister Lincoln's are the variety of roses that I<br>specialise in. None of the plants showed any other<br>symptoms of disease though. Then I noticed a reduced<br>yield occurring in most of my other crops too.",
            "npc:I spoke to other gardeners that I know, and they had<br>all noticed similar problems. ",
            "npc:Vegetable patches are growing fewer vegetables, bushes<br>are producing fewer berries and everything that is<br>planted seems to be more prone to disease!",
            "npc:You've done some farming, haven't you noticed a<br>decrease in the amount of produce you are getting<br>lately?",
            "choose:Now that I think about it, you're right!",
            "player:Now that I think about it, you're right!",
            "npc:I know I am. Depressing isn't it?",
            "choose:Yes.",
            "player:So what do you want me to do?",
            "npc:I'd like you to find out what is causing this terrible<br>problem and to try and rectify whatever the problem is.",
            "player:Do you have any ideas where I should start?",
            "npc:If I were you I would start by speaking to some of the<br>other gardeners.",
        })
        stage("start", 10)
        journal("10.none", join(J_START, { "<col=000080>I will need to speak to other gardeners to get details." }))

        -- The five gardeners [177..509]; the theory is the order, not the gardener.
        local function gardener(name, coord, npc, theory)
            t.ticklog.mark("leg." .. name)
            tele(name, coord)
            local pages = {
                "choose:Are you a member of the Group of Advanced Gardeners?",
                "player:Are you a member of the Group of Advanced<br>Gardeners?",
                "npc:Why, yes I am. Why do you ask?",
                "player:Martin has asked me to investigate why all the crops<br>are failing.",
            }
            for _, p in ipairs(theory) do pages[#pages + 1] = p end
            talk(name, npc, pages)
        end
        gardener("frizzy", "0_47_50_49_57", "farming_gardener_spirit_tree_1", {
            "npc:Ah, Martin sent you, why didn't you say so? I<br>personally think that our problems are all down to the<br>fact that we haven't had enough rain of late. It's plain<br>to see - just look at the levels of the rivers.",
            "npc:We need more rain. I don't suppose you can do<br>anything about that?",
            "player:No, I'm afraid not.",
            "npc:Well there you go then. Until you find out how to<br>make it rain you're going to have a hard time solving<br>our problems.",
        })
        journal("10.one", join(J_START, {
            "<col=000080>I've spoken to one gardener who told me the problem is the",
            "<col=000080>lack of rain. I'd better speak to some other gardeners to get",
            "<col=000080>more details.",
        }))
        gardener("elstan", "0_47_51_45_40", "elstan", { "npc:Insects! That's what you can tell him! Insects! Stop the<br>insects from eating so many of our crops and we'd get<br>maximum yield, I tell you!" })
        journal("10.two", join(J_START, {
            "<col=000080>I've spoken to a couple of gardeners who gave me different",
            "<col=000080>stories. I should speak to some other gardeners to get more",
            "<col=000080>details.",
        }))
        gardener("heskel", "0_46_52_60_44", "farming_gardener_tree_2", {
            "npc:How can you ask such a thing? It's obvious! It's all you<br>adventurers. Every time I get a crop planted, some<br>adventurer comes along and digs it up and plants crops<br>of their own, as if they owned my farming patch!",
            "npc:It's really annoying! You should speak to your friends<br>about their rude behaviour!",
            "player:But...but...they're not my friends! I've never even<br>spoken to most of them!",
            "npc:Don't give me that! You're an adventurer, they're all<br>adventurers. I bet you have some sort of secret guild<br>and you gather there and say things like 'let's annoy<br>the gardeners this week', don't you?",
            "player:No, I mean yes. There is a heroes guild, but ...!",
            "npc:Aha, so you admit it then! You should be ashamed of<br>yourselves!",
            "player:Oh, I give up!",
        })
        gardener("fayeth", "0_49_50_54_32", "farming_gardener_tree_4", {
            "npc:Well obviously it's the seasons. They all seem to be out-<br>of-wack these days. Summer comes earlier and earlier<br>each year, but then it simply rushes by!",
            "npc:If you find out what is upsetting the seasons you'll find<br>out what is causing the harvest to fail.",
            "player:I don't suppose you have any ideas about what could<br>cause the seasons to be mixed up?",
            "npc:No, sorry, but find that out and you'll get to the root<br>of our problems.",
        })
        gardener("treznor", "0_50_54_26_0", "farming_gardener_tree_3", {
            "npc:Well I can tell by the look on your face that I'm not<br>the first person you've asked. Why don't you tell me<br>what you've heard from other members of the group so<br>far?",
            "player:Why not? They all seem to be a bunch of loonies<br>anyway.",
            "player:I've been told that it's the lack of rain, plagues of<br>insects and recently I was told that the entire order of<br>seasons is confused!",
            "player:I have no idea what to do about any of it. I'm an<br>adventurer, not a weather controller!",
            "npc:Did you ever consider that all those theories might be<br>correct?",
            "player:But they're all different, how could they all be correct?",
            "npc:I'll let you into a little secret that'll explain it all to you.<br>In a word - fairies!",
            "player:Oh no! Not another crack-pot theory!",
            "npc:I'm serious! ",
            "npc:Think it through. There are fairies who control<br>everything natural from the weather to the seasons.",
            "npc:Surely if all of those things have been mentioned as<br>catalysts to our problems it stands to reason that the<br>fairies are actually the cause?",
            "player:Right, well thanks for your input. I'll think over what<br>you said.",
        })
        journal("10.several", join(J_START, {
            "<col=000080>I've spoken to several gardeners who all have different ideas",
            "<col=000080>about what the problem actually is. I'd better go back and talk",
            "<col=000080>to <col=800000>Martin<col=000080> again.",
        }))
        -- A gardener already asked [509].
        talk("treznor.again", "farming_gardener_tree_3", {
            "choose:Are you a member of the Group of Advanced Gardeners?",
            "player:Are you a member of the Group of Advanced<br>Gardeners?",
            "npc:Why, yes I am. Why do you ask?",
            "player:Martin has asked me to investigate why all the crops<br>are failing.",
            "npc:I've already told you everything I suspect.",
        })

        -- Martin again [543..568]: stage 20 on his first page.
        t.ticklog.mark("leg.martin2")
        tele("martin2", "0_48_50_6_57")
        talk("martin.fairies", "martin_the_master_farmer", {
            "choose:Ask about the quest.",
            "npc:Hello again,",
            "player:To be honest I'm worried about the sort of person you<br>let join your organisation.",
            "npc:Why is that?",
            "player:I've heard theories ranging from climate change to<br>fairies from your members.",
            "npc:Hmm, that's an interesting idea. Now that I think about<br>it that does make some sense. Yes, maybe you should<br>talk to a fairy about it.",
            "player:O-kay! You're the boss, any idea which flower I should<br>look under to find a fairy?",
            "npc:No, sorry, but I did hear that there were some<br>adventurers with a similar problem in the swamps near<br>Lumbridge. Just don't mention my name if you talk to<br>them, they're trying to keep it a secret, but everybody",
            "npc:actually knows what they're up to.",
            "player:Yes, I've spoken to them before.",
        })
        stage("fairies", 20)
        journal("20", join(J_START, {
            "<col=000080>The gardeners seem to think that the fairies are responsible.",
            "<col=000080>I should talk to the <col=800000>Fairy Queen<col=000080>.",
        }))

        -- Zanaris through the shed [744..751], the Godfather [806..859].
        t.ticklog.mark("leg.zanaris")
        t.exec("wield.staff", t.player.equip, "dramen_staff")
        tele("shed", "0_50_49_1_33")
        t.exec("shed", t.player.click_loc, "zanarisdoor", 1, { at = { 3202, 3169, 0 } })
        t.ticks(8)
        local _, gf = t.var.server("varb1808_fairy_godfather_check")
        t.check("zanaris.godfather_check", gf == 1, "varbit 1808 " .. tostring(gf))
        local _, qc = t.var.server("varb1807_fairy_queen_check")
        t.check("zanaris.queen_check", qc == 1, "varbit 1807 " .. tostring(qc))
        tele("godfather", "0_38_69_14_13")
        talk("godfather", "fairy_godfather_multi", {
            "player:Who are you? Where's the Fairy Queen? What's going<br>on here? Where's the Fairy Queen's throne gone?",
            "npc:Ciao esploratore! So many questions. Whicha one would<br>you hava me answer first?",
            "choose:Where's the Fairy Queen's throne gone?",
            "player:Where's the Fairy Queen's throne gone?",
            "npc:An eenteresting place to start... I am-a here, ze Queen<br>Bee iz not. So my chair - eet iz-a here, her throne -<br>eet iz-a not. Eez there anytheeng else? I ama a bizy<br>fairy.",
            "choose:Where's the Fairy Queen?",
            "player:Where's the Fairy Queen?",
            "npc:Ah, eet iz a trageec-a tale. Da Queen Bee she iz-a,<br>how-you-say, seek?",
            "player:You mean sick. What happened?",
            "npc:Ah, a tragedy to be-a struck-a down so young. ",
            "npc:How-a much do ya know about-a da fairy folk, my beeg<br>friend?",
            "player:Not actually a whole deal, I'm afraid.",
            "npc:Well, let-a me enlighten yous. Zanaris iz-a a nexus of-a<br>da worlds. Da Queen Bee, she iz-a da guardian of-a all<br>da folk dat live-a in Zanaris.",
            "npc:So when-a thees Tanglefoot arrives, da Queen Bee she-<br>a goes to da Cosmic Temple area to kill-a eet, but<br>someting goes-a wrong. Da Queen Bee, she gets-a hurt<br>and is-a now inna da magic sleep.",
            "npc:She is-a being cared for by-a Fairy Nuff, who lives-a<br>just north of-a da bank.",
            "player:Thanks, I'll go see her then. Goodbye.",
            "npc:Ciao. Oh, and",
            "player:Yes?",
            "npc:A word-a of advice. You be-a careful not to cause da<br>trouble for da folk while you are in Zanaris. Ciao.",
            "player:?",
        })
        stage("godfather", 30)
        journal("30", join(J_ZANARIS, {
            "<col=000080>I need to speak to a <col=800000>fairy called Nuff<col=000080>, to get more details",
            "<col=000080>about her condition.",
        }))
        talk("slim_louie", "fairy_godfather_multi", { "npc:Da Boss is a busy fairy! He don' have time to talk to<br>yous humans. Vamoose!" })

        -- Fairy Nuff [1196..1304]: the list with stage 40.
        t.ticklog.mark("leg.nuff")
        tele("nuff", "0_37_69_21_52")
        talk("nuff", "fairy_nuff_multi", {
            "npc:Sorry, I'm very busy and can't chat. If you want to<br>know something go and talk to the Godfather.",
            "player:I did, he said I should come and talk to you!",
            "npc:Oh! ",
            "npc:Sorry, I didn't realise. ",
            "npc:What can I help you with then?",
            "player:Actually it's more a case of what can I help you with? I<br>told the Fairy Godfather that I would help you find a<br>cure for the Queen.",
            "npc:Oh, thank you! I really need help, I have no idea what<br>ails the Queen. You must be a mighty healer!",
            "player:Err...no, not me.",
            "npc:A great druid then?",
            "player:Umm...'fraid not.",
            "npc:A shaman?",
            "player:No.",
            "npc:A surgeon?",
            "player:No.",
            "npc:A doctor, medic, nurse?",
            "player:No, no and no.",
            "npc:So what are you then?",
            "player:I'm a mighty adventurer!",
            "npc:A what?!",
            "player:A mighty adventurer!",
            "npc:Yes, I heard. It's just not what I was expecting to<br>hear. ",
            "npc:But hang on, you might be just the sort of person I<br>need to help me!",
            "player:How is that then?",
            "npc:I'm sure the Godfather told you that the Queen is in<br>an enchanted sleep?",
            "player:Well sort of, I couldn't really understand everything he<br>was talking about.",
            "npc:Shhh, somebody might hear. Look you're a visitor, so I'll<br>tell you this much; it doesn't pay to talk about the<br>Godfather behind his back. ",
            "npc:But back to the problem at hand - the Fairy Queen.<br>She was brought here after her battle with the<br>Tanglefoot in a terrible state, extremely weak and<br>barely able to fly!",
            "npc:Here, I've written down a list of her symptoms.",
            "npc:When the Queen got back here, she was clearly<br>weakening fast. It appears as if the magic force that is<br>her life essence is slowly unravelling.",
            "npc:I have put her in a state of tempus fugit, that will slow<br>time down for her, and thus prolong the amount of time<br>that she remains alive.",
            "npc:Now, what I need you to do is find a mage by the<br>name of Zandar Horfyre. He has spent many years<br>studying magical illness. Take the list to him and ask<br>him if he knows what ails the Queen, please.",
            "player:That doesn't sound too difficult, where will I find him.",
            "npc:I believe he now resides",
        })
        stage("nuff", 40)
        t.expect("nuff.list", t.inv.expect_has("fairy_symptoms_list", 1))
        journal("40", join(J_ZANARIS, {
            "<col=000080>I spoke to <col=800000>Fairy Nuff<col=000080> who sent me, with a list of the Fairy",
            "<col=000080>Queens' symptoms, to find a mage called <col=800000>Zandar Horfyre<col=000080> at",
            "<col=000080>the <col=800000>Dark Wizards' Tower<col=000080>.",
        }))
        -- The healer's story [1349..1370], then the refusal [1325..1346].
        talk("nuff.healer", "fairy_nuff_multi", {
            "npc:Hello, can I help you at all?",
            "choose:What do you do around here?",
            "player:What do you do around here?",
            "npc:Oh, I'm the resident healer. My name is Nuff, Fairy<br>Nuff. Who are you?",
            "player:My name is",
            "npc:Pleased to meet you",
            "npc:It's my job to look after the sick, wounded or dying<br>fairies around here. Of course, fairies being magical by<br>nature, not many of them get hurt, so I don't normally<br>have much to do.",
            "player:What about healing humans? Lots of adventurers must<br>get hurt fighting the Otherworldly beings.",
            "npc:Yes they do. Silly isn't it? I do use my healing magic<br>on humans sometimes, but I don't like them attacking<br>our other guests, so I charge them for the service.",
            "player:How much do you charge to heal someone, then?",
            "npc:Well, the gold that you humans are so fond of has no<br>real value in Zanaris, so I ask my patients to pay me<br>in gems.",
            "npc:I will heal 4 hitpoints for an opal, 5 hitpoints for either a<br>sapphire or a piece of jade, 7 hitpoints for an emerald<br>or red topaz, 9 for a ruby and 11 for a diamond.",
            "player:Only 11 hitpoints for a diamond? Doesn't that seem a<br>bit steep?",
            "npc:Maybe to you, but you'd be surprised how many<br>adventurer's are willing to pay those prices.",
        })
        talk("nuff.heal", "fairy_nuff_multi", {
            "npc:Hello",
            "choose:Yes, please. I need healing.",
            "player:Yes, please. I need healing.",
            "npc:Sure, how many hitpoints would you like me to heal?",
            "choose:9 Hitpoints.",
            "player:9 Hitpoints.",
            "npc:That will cost you 1 ruby.",
            "choose:On second thoughts, no thanks.",
            "player:On second thoughts, no thanks.",
            "npc:Ok, suit yourself.",
        })

        -- Zandar [1597..1621]: the list goes with stage 50.
        t.ticklog.mark("leg.zandar")
        tele("zandar", "2_45_52_25_6")
        talk("zandar", "zandar_horfyre", {
            "player:Hello, I need your help.",
            "npc:And why should I help you, fool? You attack my<br>brethren and I, and then you demand my help! I think<br>not!",
            "player:But Fairy Nuff said you would help me!",
            "npc:Fairy Nuff sent you! ",
            "npc:How is she? I owe her a great deal, she saved me from<br>a mortal injury I sustained when I was investigating<br>the nature of Otherworldly beings. So, how can I help?",
            "player:The Fairy Queen is extremely ill. She was in a battle<br>with a Tanglefoot and her injuries have resulted in her<br>being in a coma.",
            "npc:This is grave news indeed! ",
            "npc:You think the Tanglefoot is responsible? I don't see how<br>that could be. A Tanglefoot will normally kill its victim<br>outright...unless...hmmm...I wonder...",
            "npc:I don't suppose you could give me more details about<br>the Fairy Queen's condition, could you?",
            "player:Fairy Nuff gave me a list of her symptoms. Here, have<br>a look at it.",
            "npc:Hmm...yes...slow discorporation of ethereal body, eh?<br>Hmm...I think I know what the problem might be.",
            "player:You do? That's great! What is it?",
            "npc:From what I know about fairies, their basic lifeforce is<br>pure magic. Much as we humans would refer to our<br>souls as our life essence, so too does a fairy have a life<br>essence of pure magic.",
            "npc:You could actually refer to fairies as magic embodied<br>and not be wrong.",
            "player:Yes, but about the Fairy Queen?",
            "npc:Patience, I'm getting to that. It appears the Fairy<br>Queen has lost a large part, indeed the majority, of her<br>life essence. Either it was drained from her by the<br>Tanglefoot or she did it voluntarily.",
            "npc:Although why she would do that is beyond me.",
            "player:So I guess I need to go and kill this Tanglefoot and<br>get back her magic essence if I am to help her?",
            "npc:Yes, but if I were you I would seek Malignus Mortifer,<br>the necromancer. He's been seen near Port Sarim with<br>his followers recently. I believe he once fought a<br>Tanglefoot.",
            "player:Malignus Mortifer. Okay, thanks very much.",
            "npc:Don't mention it, and give my regards to Nuff, when<br>you see her next.",
        })
        stage("zandar", 50)
        t.expect("zandar.list", t.inv.expect_absent("fairy_symptoms_list"))

        -- Malignius: the errand [1803..1873], the grave [2066..2095], the answer [2236..2282].
        t.ticklog.mark("leg.malignius")
        tele("malignius", "0_46_51_49_2")
        talk("malignius.errand", "elemental_wizard_boss", {
            "choose:I need help with fighting a Tanglefoot.",
            "player:I need help with fighting a Tanglefoot. Can you help<br>me?",
            "npc:Almost certainly, I am a great practitioner of the magic<br>arts, after all.",
            "player:Great, thanks. I was told by Zandar Horfyre that you<br>fought one once.",
            "npc:That is true and an epic battle it was too.",
            "player:That's great! I presume you won as you are here to<br>tell me about it, could you tell me how to defeat one,<br>please?",
            "npc:Of course I won! As I've already stated I am a master<br>of the magic arts. ",
            "npc:Why do you want to know how to kill a Tanglefoot?",
            "player:There is a Tanglefoot in the lost city of Zanaris and I<br>have been asked to kill it.",
            "npc:I see. Well I could tell you how to defeat your<br>Tanglefoot, however I would need you to perform a<br>little undertaking for me first.",
            "player:What exactly did you have in mind?",
            "npc:To the north-east of here is a haunted manor. Bring<br>me a skull from a grave you find there and you shall<br>have your answer.",
            "player:I'll go and get it then.",
            "npc:Fine, I'll see you soon.",
        })
        stage("errand", 50)
        t.ticklog.mark("leg.grave")
        tele("grave", "0_48_52_34_55")
        t.exec("dig", t.player.inv_op, "spade", 1)
        t.exec("dig.skull", t.inv.await, "fairy_skull", 1, 12)
        t.exec("dig.again", t.player.inv_op, "spade", 1)
        t.ticks(8)
        local _, skulls = t.inv.count("fairy_skull")
        t.check("dig.one_skull", skulls == 1, "skulls " .. tostring(skulls))
        tele("malignius2", "0_46_51_49_2")
        talk("malignius.skull", "elemental_wizard_boss", {
            "choose:I was asking you about fighting a Tanglefoot...",
            "player:I was asking you about fighting a Tanglefoot...",
            "npc:So, have you brought me the skull?",
            "player:Yes, I have it right here.",
            "npc:Right then, to business. A Tanglefoot can not be<br>harmed by might nor magic. The only weapon that can<br>damage them are a pair of enchanted secateurs!",
            "player:Where can I get a pair of these enchanted secateurs?",
            "npc:You will have to make them... listen carefully and I'll tell<br>you how.",
            "npc: Obviously you will need",
            "npc:Take them to the Nature Spirit and ask him to perform<br>the ceremony of Phasma Phasmatis Natura.",
            "player:Phasma Phasmatis Natura. Got it! Is that all?",
            "npc:Yes, that's all. You'll be able to use those secateurs to<br>kill the monster.",
            "player:Great, thanks very much.",
        })
        stage("malignius", 60)
        local _, i1 = t.var.server("varb1804_fairy_nature_item1")
        local _, i2 = t.var.server("varb1805_fairy_nature_item2")
        local _, i3 = t.var.server("varb1806_fairy_nature_item3")
        t.check("roll", i1 ~= nil and i1 >= 0 and i1 <= 28 and i2 == i1 + 1 and i3 == i1 + 2,
            "rows " .. tostring(i1) .. ", " .. tostring(i2) .. ", " .. tostring(i3))
        -- The recorded roll, for the journal's rows [2324].
        t.cheat("::setvar varb1804_fairy_nature_item1 26", false)
        t.cheat("::setvar varb1805_fairy_nature_item2 27", false)
        t.cheat("::setvar varb1806_fairy_nature_item3 28", false)
        t.ticks(2)
        local J_ESSENCE = join(J_ZANARIS, {
            "<str>I spoke to fairy Nuff who sent me, with a list of the Fairy",
            "<str>Queens' symptoms, to find a mage called Zandar Horfyre.",
            "<str>He told me that the Fairy Queen had lost her magical essence",
            "<str>and that I would have to kill the Tanglefoot to get it back.",
        })
        journal("60", join(J_ESSENCE, {
            "<col=800000>Malignus Mortifer<col=000080> told me that I need to make a pair of",
            "<col=000080><col=800000>Enchanted Secateurs<col=000080> to fight the <col=800000>Tanglefoot<col=000080>. To do this I will",
            "<col=000080>need to take <col=800000>a pair of secateurs, an uncut ruby, some Jogre",
            "<col=800000>bones and a kingworm<col=000080> to the <col=800000>Nature Spirit<col=000080>.",
        }))
        t.cheat("::give uncut_ruby 1", false)
        t.cheat("::give tbwt_jogre_bones 1", false)
        t.cheat("::give king_worm 1", false)
        t.ticks(2)

        -- The Nature Spirit's ceremony [3871..3891].
        t.ticklog.mark("leg.grotto")
        t.exec("wear.ghostspeak", t.player.equip, "amulet_of_ghostspeak")
        tele("grotto", "0_53_52_48_8")
        t.exec("grotto.door", t.player.click_loc, "grotto_door_druidicspirit", 1, { at = { 3440, 3337, 0 } })
        t.ticks(4)
        talk("nature_spirit", "filliman_tarlock_ns", {
            "npc:Welcome back, my friend. I sensed that you require<br>my help and so I have once more returned.",
            "player:Thank you! I need your help in enchanting a pair of<br>secateurs.",
            "npc:Secateurs? That's a strange thing to enchant. Would<br>you care to inform me why you need them?",
            "player:Sure. A Tanglefoot has invaded the fairy kingdom of<br>Zanaris and the only way I can fight it is with a pair of<br>enchanted secateurs, apparently.",
            "npc:Ah, a Tanglefoot! Your information is correct, secateurs<br>that have been blessed by a Nature Spirit are the only<br>weapons that can harm such a terrible creature.",
            "npc:I shall be glad to help you. Do you have all of the<br>required items for the ceremony?",
            "player:Yes, I have everything right here.",
            "npc:Then let us begin...",
        })
        -- The chat shuts on that click; "It is done!" comes six ticks later [3881..3887].
        await_page(12)
        play_exact("nature_spirit.done", {
            "npc:It is done!",
            "npc:Farewell,",
        })
        t.ticks(2)
        stage("enchanted", 70)
        t.expect("enchanted.secateurs", t.inv.expect_has("fairy_enchanted_secateurs", 1))
        t.expect("enchanted.ruby_gone", t.inv.expect_absent("uncut_ruby"))
        journal("70", join(J_ESSENCE, {
            "<col=000080>The <col=800000>Nature Spirit<col=000080> has enchanted a pair of secateurs for me to",
            "<col=000080>use in my battle with the <col=800000>Tanglefoot<col=000080>.",
        }))

        -- The Shady Grove [4203..4225], the Tanglefoot [4257..4426], the secateurs [4436].
        t.ticklog.mark("leg.grove")
        t.exec("wield.secateurs", t.player.equip, "fairy_enchanted_secateurs")
        tele("vines", "0_37_68_32_27")
        t.exec("vines", t.player.click_loc, "fairy_walls_multi", 1, { at = { 2399, 4379, 0 } })
        await_page()
        play_exact("vines", {
            "npc:I've received orders to let you through if you want.<br>Good luck.",
            "mesbox:You squeeze past",
        })
        t.ticks(4)
        local r_in, tile_in = t.world.tile()
        t.check("grove.copy", r_in == "ok" and tile_in.x > 6400, "in a copy: " .. tostring(tile_in and tile_in.x))
        -- The recorder ran round the vines to 2375,4377 before the first swing [4228..4258].
        local base_x, base_z = tile_in.x - 28, tile_in.z - 27
        t.exec("grove.walk", t.player.goto_tile, base_x + 7, base_z + 25, 0)
        -- Protect from Melee as the recorder had it [4249]: 41 of 41 of its swings 0.
        local _, pray_now = t.var.varbit("varb4118_prayer_protectfrommelee")
        if pray_now ~= 1 then
            t.ui.tab("prayer")
            t.ticks(2)
            local _, pw = t.ui.widget("prayerbook:prayer15")
            t.ui.invoke(pw, 1)
            t.ticks(2)
        end
        local _, pray_on = t.var.varbit("varb4118_prayer_protectfrommelee")
        t.check("grove.protect_melee", pray_on == 1, "varb4118 " .. tostring(pray_on))
        t.ui.tab("inventory")
        local attack_result, attack_detail
        for try = 1, 6 do
            attack_result, attack_detail = t.player.attack("fairy_tanglefoot", 2, 30)
            if attack_result == "ok" then break end
            t.ticks(3)
        end
        t.check("tanglefoot.attack", attack_result == "ok", tostring(attack_result) .. ": " .. tostring(attack_detail))
        local dead_result, dead_detail = t.npc.await_dead_engaged(600, 4, { eat = { item = "lobster", below = 25 } })
        t.step("tanglefoot.dead", dead_result == "ok" and "PASS" or "FAIL", tostring(dead_result) .. " " .. tostring(dead_detail))
        t.ticks(6)
        t.exec("take.secateurs", t.player.click_obj, "fairy_queen_secateurs", 3)
        t.exec("take.secateurs.inv", t.inv.await, "fairy_queen_secateurs", 1, 12)
        stage("secateurs", 80)
        journal("80", join(J_ESSENCE, {
            "<str>The Nature Spirit enchanted a pair of secateurs for me, which",
            "<str>I used to kill the Tanglefoot.",
            "<col=000080>I got back the <col=800000>Fairy Queens' Enchanted Secateurs<col=000080> and now",
            "<col=000080>need to take them back to the <col=800000>Fairy Godfather<col=000080>.",
        }))
        t.exec("grove.out", t.player.click_loc, "fairy_wall_tangle_door_open1", 1)
        await_page()
        play_exact("grove.out", { "mesbox:You squeeze past" })
        t.ticks(4)
        local r_out, tile_out = t.world.tile()
        t.check("grove.out", r_out == "ok" and tile_out.x == 2400 and tile_out.z == 4379, "at " .. tostring(tile_out and tile_out.x) .. "," .. tostring(tile_out and tile_out.z))

        -- The Godfather, the completion on the last click [4580..4596].
        t.ticklog.mark("leg.complete")
        tele("godfather2", "0_38_69_14_13")
        talk("godfather.complete", "fairy_godfather_multi", {
            "npc:Ciao",
            "player:You needn't have worried I managed to kill it!",
            "npc:Ben fatto, my friend! Da folk-a dey owe yous a great-a<br>deal! So did-a yous getta back da Queen Bee's magic<br>power?",
            "player:I retrieved this pair of Enchanted secateurs, I think<br>they must contain the Queen's missing magic.",
            "npc:Grazie infinite! I will give-a dem to da healing fairy. Da<br>Queen Bee, she will-a soon be strong again!",
        })
        t.ticks(3)
        stage("complete", 90)
        -- The rating box with the scroll, and a rating shuts both [4596..4600].
        local qp_before = nil
        t.check("rating.open", t.ui.await_open("quest_rating", 8) == "ok", "quest_rating open with the scroll")
        local rw, five = t.ui.widget("quest_rating:5")
        t.check("rating.widget", rw == "ok", "quest_rating:5 -> " .. tostring(rw))
        t.exec("rating.five", t.ui.invoke, five, 1)
        t.ticks(2)
        t.expect("rating.line", t.msg.expect("You gave a 5/5 rating to a quest: "))
        t.check("rating.closed", t.ui.await_close("questscroll", 6) == "ok", "the scroll shut with the rating")
        local _, qp = t.var.server("varp101_qp")
        t.note("qp after the quest: " .. tostring(qp))

        -- Malignius's sale of a new pair after the quest [wiki].
        t.ticklog.mark("leg.after")
        tele("malignius3", "0_46_51_49_2")
        talk("malignius.after", "elemental_wizard_boss", {
            "npc:So, ",
            "choose:About those enchanted secateurs again...",
            "player:About those enchanted secateurs again...",
            "npc:What now, ",
            "choose:Can I get another set of enchanted secateurs?",
            "player:Can I get another set of enchanted secateurs?",
            "npc:Well I guess it's your lucky day!",
            "player:That's great!",
            "player:How much would they cost?",
            "npc:I could let you have them for a mere 40,000 gp.",
            "choose:Ok, great.",
            "player:Ok, great.",
            "npc:A pleasure doing business with you, ",
        })
        t.expect("malignius.sold", t.inv.expect_has("fairy_enchanted_secateurs", 1))

        -- His clothes, and the mushroom [wiki Transcript:Malignius Mortifer]:
        -- the player is an npc under a mushroom loc for 12 ticks, a page with
        -- no continue, then themself again.
        talk("malignius.clothes", "elemental_wizard_boss", {
            "npc:So, ",
            "choose:... MORE ...",
            "choose:Where can I get clothes like those?",
            "player:Where can I get clothes like those?",
            "npc:Bah! Our garments are an outward sign",
            "player:What happens if I kill you and take them?",
            "npc:Try it and see!",
            "player:How about if you teach me enough about magic",
            "npc:How about if I turn you into a mushroom",
        })
        t.ticks(3)
        do
            local _, raw = t.chat.text()
            t.check("malignius.mushroom.page", tostring(raw):find("MMMmmph!", 1, true) ~= nil, tostring(raw))
            t.check("malignius.mushroom.loc", t.world.loc_near("mushroom_inactive", 1) == "ok", "no mushroom loc on the player")
        end
        t.ticks(12)
        t.check("malignius.mushroom.gone", t.chat.kind() == "none", "chat " .. tostring(t.chat.kind()))

        -- A gardener's other options [wiki]: advice, and a rake from the shop list.
        tele("elstan2", "0_47_51_45_40")
        talk("elstan.advice", "elstan", {
            "choose:Can you give me any farming advice?",
            "player:Can you give me any farming advice?",
            "npc:",
        })
        talk("elstan.sell", "elstan", {
            "choose:Can you sell me something?",
            "player:Can you sell me something?",
            "npc:That depends on whether I have it to sell.",
        })
        -- Wiki Transcript:Torrell: the line, then the Farming Supplies shop.
        t.expect("elstan.shop", t.ui.await_open("shopmain", 10))
        t.ui.close_modal()
        t.ticks(2)

        -- A Zanaris crate [3202: 2891].
        tele("crate", "0_37_69_32_41")
        t.exec("crate", t.player.click_loc, "fairy_crate", 1)
        t.ticks(3)
        t.expect("crate.line", t.msg.expect("You search the crate but find nothing."))
        t.finish(0)
    end,
}
