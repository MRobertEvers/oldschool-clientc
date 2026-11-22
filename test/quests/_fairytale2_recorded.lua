-- Fairytale II - Cure a Queen in the order RSProx recorded it
-- (docs/rsprox-analysis/quests/fairytale2.md): Martin after Fairytale I
-- (index 4606 [82..126]) and his crops' 573 ticks, the start (4654 [210]),
-- the trashed grotto's pan, the vials and the certificate, the sheep, the
-- chef and the plaque, the Godfather's permission, the Co-ordinator, the
-- hideout through AIR, DLR, DJQ, AJS, Fairy Nuff and Fairy Very Wise, the
-- Godfather's pockets, the secateurs, a star flower on the Cosmic Entity's
-- plane, a gorak, the potion, and the Fairy Queen (4653 [394..437]).
-- Places are reached by teleport, the rings by their dial.
--
--     python3 tools/quest_gate/run.py --no-build --script test/quests/_fairytale2_recorded.lua --name fairytale2_recorded
local MARTIN_REPORT = {
            "choose:Ask about the quest.",
            "player:I've solved your problems! I've returned the Fairy<br>Queen's magical essence!",
            "npc:What are you talking about?",
            "player:The Fairy Queen was in an enchanted sleep, because<br>the Tanglefoot had taken the secateurs that contained<br>her magic. I fought and killed the Tanglefoot and have<br>given the Godfather the Fairy Queen's secateurs. She'll",
            "player:wake up now and everything will get back to normal.",
            "npc:I'm glad to hear it, but if you don't mind I'll go and<br>plant some seeds now and wait until they grow before I<br>give you the reward that I promised.",
            "player:I guess it does sound rather far-fetched. Ok, I'll come<br>back and see you after your crops have had a chance<br>to grow.",
            "npc:Great! I'll see you soon then.",
}
local MARTIN_WAIT = {
            "choose:Ask about the quest.",
            "player:So, have your vegetables grown yet?",
            "npc:Stop being so impatient, <NAME>. They'll grow in their<br>own good time - come back later.",
}
local MARTIN_START = {
            "choose:Ask about the quest.",
            "player:Hello Martin, you must be the slowest gardener in the<br>world! I don't suppose those crops have grown yet?",
            "npc:Well, you've got a nerve showing up around here again!",
            "player:What? What are you talking about?",
            "npc:You told me that you'd solved the problems of the<br>Gardening Group's members, well I've planted loads of<br>seeds since then and there's no doubt that even more<br>plants are dying!",
            "player:But I gave the Fairy Queen's secateurs back to the<br>Godfather, things should be growing again.",
            "npc:Yeah, sure, whatever. You know, I didn't believe you<br>when you told me that story about 'the fairies' in the<br>first place. You'd just better solve this problem, when<br>people get hungry, they get angry and you're the",
            "npc:person who said that everything would be all right, so<br>you're the person who they'll get angry with!",
            "choose:Yes.",
            "player:I suppose I'd better go and see what the problem is<br>then.",
}
local GODFATHER = {
            "choose:Where is the Fairy Queen?",
            "player:Godfather! Where is the Fairy Queen?",
            "npc:Whata are you-a talking about?",
            "player:I've just been to Fairy Nuff's cave and when I got<br>there I found it ransacked! Everything has been broken<br>and the Fairy Queen and Fairy Nuff are missing!",
            "npc:Essere nel panico! You-a say datta da Queen Bee, she<br>izza meesing? Theez izza terreeble! Who could-a have<br>done such a theeng? We musta find her! I weel<br>organise a search-a party right-a-way!",
            "player:Calm down! Let's think this through carefully.",
            "choose:Do you have any idea who could have done this?",
            "player:Do you have any idea who could have done this?",
            "npc:Errm...no! None atta all! Da Queen Bee, she izza well<br>liked by-a everyone.",
            "player:That's not very helpful.",
            "player:Where could she have been taken to?",
            "npc:Ze keednapper must-a have taken her through-a da<br>fairyreengs. Dey weel-a be long gone by-a now.",
            "player:We must go after them!",
            "npc:Yes! We must-a, but most-a of da folk are away from<br>Zanaris atta the momento.",
            "player:Well tell me what to do and I'll go myself.",
            "npc:Ye-es, dat izza good idea. Hokay, you may use-a da<br>fairyreengs. Talk-a to da co-ordinator, she will-a explain-<br>a to you how to use-a da reengs, but you must-a<br>promees me dat you weel come-a back-a here, eef you",
            "npc:find-a her. Whoever keednapped her must-a be very<br>powerful. You find-a da Queen Bee and den-a yous<br>come right back here and we will-a rescue her together.<br>Hokay? ",
            "choose:Yes, okay.",
            "player:Yes, okay.",
            "npc:Good. Now-a yous talk-a to da co-ordinator. She will-a<br>tell you how-a da fairyreengs work.",
}
local COORDINATOR = {
            "choose:The fairy rings.",
            "player:Hi, the Godfather said I should ask you to explain how<br>the fairy rings work.",
            "npc:Goodness! As if I don't have enough to do already<br>without giving every adventurer that comes along a<br>history lesson! Let's get this over with quickly. Zanaris is<br>an extremely old place, it was here long before we",
            "npc:fairyfolk came to live here. In fact it is rumoured to be<br>the nexus of all planes. When we arrived here we<br>found many of the magical properties of this place to be<br>dormant. One of these was the ability to jump between",
            "npc:planes using a powerful portal that had been sealed for<br>centuries. The Fairy Queen harnessed that magic and<br>created the fairy rings, these are not as powerful as the<br>original portal would have been, but are very effective",
            "npc:nonetheless. There are 3 fairy rings in Zanaris, the one<br>in the market and the one near the evil chicken's shrine<br>will only take users to one specific place. However the<br>third fairy ring, near the fountains, is a major portal.",
            "npc:Anybody who activates that fairy ring will find<br>themselves able to dial in a 3 digit code. These codes<br>will all take the user to different locations. Some of the<br>codes no longer work and other codes go to extremely",
            "npc:dangerous places, so be careful. Fortunately the fairy<br>rings are imbued with a powerful protective magic and<br>as long as you stand in the middle of them nothing will<br>be able to attack you. Remember, because you are a",
            "npc:human, you will need to be holding your dramen staff<br>to activate the rings. Now I must get back to work,<br>there is so much to do and so few fairies left to do it.",
}
local NUFF = {
            "npc:<NAME>! You found us! I'm so glad, I was afraid you<br>wouldn't get my message.",
            "player:Fairy Nuff, what happened?",
            "npc:After you brought back the Queen's enchanted<br>secateurs and gave them to the Fairy Godfather, I went<br>to him to ask for them so that I could wake Her<br>Majesty up, but he refused to give them to me. Then I",
            "npc:noticed that lots of my friends were missing, when I<br>asked him about them he said they were off working for<br>him and then he threatened to have his bodyguards pull<br>my wings off! I was scared for the Queen, and so I",
            "npc:moved her here.",
            "npc: When I returned to Zanaris I found my cave<br>ransacked! There were other fairies that had been<br>attacked by the Godfather and so we fled the city and<br>came here. I managed to hide a note that I hoped you",
            "npc:would find... ",
            "npc:... obviously you did.",
            "player:But why would the Godfather do this?",
            "npc:Isn't it obvious? He wants to rule Zanaris instead of the<br>Fairy Queen. The latest fairies to arrive have even<br>reported that he has invited all sorts of monsters to the<br>Lost City.",
            "player:So what should we do about it do you think?",
            "npc:I've got terrible news! One of our spies has just<br>returned from Zanaris and has reported that the<br>Godfather has enlisted an army of orcs! Apparently they<br>are wandering freely around our homes!",
            "player:I'm sorry, but who are you?",
            "npc:Oh, how rude of me! <NAME> this is Fairy Very Wise,<br>advisor to the Queen. Fairy Very Wise, this is<br><NAME>.",
            "npc:Oh, I've heard of you. You're the young adventurer<br>that defeated the Tanglefoot.",
            "player:Yes, that's me.",
            "npc:So you're also the idiot who gave that Godfather maniac<br>the Fairy Queen's magic secateurs! ",
            "player:But that was a mistake! I thought he was trying to help<br>the Fairy Queen!",
            "npc:So you're not evil, you're just not too bright. Well I'm<br>glad you found this place, although if you can find it it's<br>obviously not much of a hiding place, now you can help<br>fix the damage that you've done! ",
            "player:Alright, what do I need to do?",
            "npc:The first thing we have to do is wake up the Fairy<br>Queen. You will have to go back to Zanaris and find a<br>way to get those magical secateurs back from the<br>Godfather!",
            "player:That's easy I'll just pickpocket them off of him, I'm<br>really good at that.",
            "npc:Ok, just be careful, his henchmen have keen eyes and<br>will be watching out for him.",
}
local SECATEURS = {
            "player:You should have seen me! The Godfather didn't even<br>know I was going through his pockets! Here they are.",
            "npc:Fantastic! Let's just hope that it's not too late ...",
            "box:You hand fairy Nuff the Queen's enchanted secateurs.",
            "npc:Oh no! It didn't work! She's been asleep too long!",
            "player:Calm down! There's got to be something that we can<br>do. After all, we've given her back her magical essence,<br>right?",
            "npc:Yes, we have. There is one small chance that we might<br>be able to wake her up still, but I'd need a potion of<br>Magic Essence.",
            "player:Well that doesn't seem too bad, what do I need to do to<br>make one?",
            "npc:A potion of magic essence is made in the same way as<br>any other potion, first you mix a vial of water with a<br>base herb, in this case a Star Flower. Star Flowers are<br>rich in magic essence and are excellent for curing",
            "npc:poisons and that's what the Queen is suffering from - a<br>poisoning of her life essence. Then we need to add the<br>catalyst ingredient; a powdered Gorak's claw. Goraks<br>drain the life from their prey, so by using one of their",
            "npc:claws we are hoping to draw the Queen's life essence<br>back.",
            "player:Thanks for the lecture, but where do I get one of these<br>Star Flowers and where can I find a Gorak?",
            "npc:Star Flowers only grow on the higher planes and even<br>there they are extremely rare. You should be able to<br>access some of the higher planes by using the fairy<br>rings. Goraks also live on a different plane. They are",
            "npc:vicious creatures that draw the life essence from any<br>living thing they find. It seems that in drawing the life<br>force from the world that they live on they have trapped<br>themselves on a small plane, as far as I am aware, the",
            "npc:only way onto or off of their world is through the fairy<br>rings.",
}
local FINALE = {
            "npc:...Urgh! Wh... what happened? ...Why do I feel so<br>awful?",
            "npc:Your majesty! You're awake!",
            "npc:Wh... what happened? The last thing I remember was<br>confronting that Tanglefoot with the aid of the<br>Godfather.",
            "player:THE GODFATHER?",
            "npc:Yes, that's what I said... oh, hello <NAME>. Please<br>excuse me for not having noticed you earlier, I'm not<br>feeling quite myself. Why did you shout about the<br>Godfather?",
            "player:I'm not really sure how to tell you this, your majesty...<br>It seems as though you've fallen foul of a clever scheme<br>of the Godfather's...",
            "npc:That's absurd, why would the Godfather want to hurt<br>me?",
            "npc:I'm afraid it's true your majesty. You see nobody knew<br>that the Godfather was with you when you fought the<br>Tanglefoot...",
            "npc:But that's the point, I didn't fight the Tanglefoot. We<br>had only just entered its lair and sensing the danger to<br>itself, the Tanglefoot fled down a passage, then...<br>blackness!",
            "npc:Then your majesty, I think it may be worse than I<br>imagined. I suspect the Fairy Godfather attacked you<br>from behind and left you for dead. He has taken over<br>control of Zanaris whilst you were...er, ...sleeping. He",
            "npc:has enlisted the aid of orcs and other assorted ruffians,<br>if any of your subjects dared to question him, they<br>were assaulted. I very much suspect that if it wasn't<br>for the quick thinking of Fairy Nuff here, you would be",
            "npc:dead! She fled here with you and as many of your<br>loyal fairies as she could gather. We've been in hiding<br>ever since, until this brave adventurer managed to<br>awaken you.",
            "npc:I can't believe it! ",
            "npc:He shan't get away with this, how do I get back to<br>Zanaris? The Godfather shall answer for his crimes!",
            "npc:No! Stop your majesty!",
            "npc:Why, old friend?",
            "npc:The Godfather has recruited an army of villains, whilst<br>we just have the few fairies who escaped and although<br>they would gladly follow you into battle, many are still<br>wounded from their treatment at the tyrant's hands. I",
            "npc:must counsel caution, lest this brave hero's efforts are<br>all wasted and the godfather captures you. He won't let<br>you survive a second time.",
            "npc:There is wisdom in what you say. I shall ponder your<br>words before I act rashly. As for you, <NAME>, how<br>can I reward you? You have done the fairy people a<br>great favour that shall not be forgotten. I'm afraid you",
            "npc:find us in trying times, but please accept this magic<br>lamp as a sign of our thanks.",
            "box:The Fairy Queen gives you an XP lamp.",
}

local ZANARIS_RING = { 2412, 4434 }

return {
    id = "fairytale2_recorded",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::complete quest_fairytale1",
        "::complete quest_druidicritual",
        "::give dragon_scimitar 1",
        "::give dramen_staff 1",
        "::give vial_water 1",
        "::give pestle_and_mortar 1",
        "::give lobster 10",
        "::setlevel herblore 57",
        "::setlevel farming 49",
        "::setlevel thieving 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb2326_fairy2_queencure_quest",
            constants = {
                not_started = 0, waiting = 5, may_start = 10, started = 20, grotto = 30, permission = 40,
                hideout = 45, steal = 50, stolen = 60, given = 65, essence = 70, flower_vial = 72,
                essence_made = 73, poured = 75, awake = 80, complete = 90,
            },
            row = "quest_fairytale2",
            display = "Fairytale II - Cure a Queen",
            points = 2,
        })
        t.ticklog.start()
        local function stage(name, want)
            local _, v = t.var.server("varb2326_fairy2_queencure_quest")
            t.check("stage." .. name, v == want, "stage " .. tostring(v) .. " want " .. tostring(want))
        end
        local function tile_text()
            local r, tt = t.world.tile()
            if r ~= "ok" or type(tt) ~= "table" then return "?" end
            return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
        end
        local function at(x, z)
            local r, tt = t.world.tile()
            return r == "ok" and type(tt) == "table" and tt.x == x and tt.z == z
        end
        local function count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end
        local function plain(s)
            return (string.gsub(string.gsub(s, "<[^>]*>", ""), "^%s*(.-)%s*$", "%1"))
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
        -- Each page against the recording, raw (<br>s and all); a page naming
        -- the recorder, and the rest of that speech, wraps on the name's
        -- width, so only its kind is compared.
        local function play_exact(name, entries)
            local loose = false
            for i, e in ipairs(entries) do
                local kind, arg = e:match("^(%a+):(.*)$")
                if kind == "choose" then
                    local r, d = t.chat.choose(arg)
                    t.check(name .. ".choose" .. i, r == "ok", tostring(r) .. " " .. tostring(d))
                    t.ticks(1)
                    loose = false
                else
                    await_page(12)
                    local got_kind = t.chat.kind()
                    local r, raw = t.chat.text()
                    local want_kind = kind
                    if kind == "box" then want_kind = got_kind end
                    if arg:find("<NAME>", 1, true) then loose = true end
                    local ok = (got_kind == want_kind) and r == "ok" and raw ~= nil
                    if ok and not loose then ok = raw == arg end
                    t.check(name .. ".page" .. i, ok, "want " .. kind .. " '" .. arg .. "' got " .. tostring(got_kind) .. " '" .. tostring(raw) .. "'")
                    if not ok then return end
                    -- A speech ends where Jagex's page is not full.
                    if loose and not arg:find("<br>.*<br>.*<br>") then loose = false end
                    t.chat.continue_()
                    t.ticks(1)
                end
            end
        end
        local function talk(name, npc, pages, op)
            local r, d
            for try = 1, 6 do
                r, d = t.player.talk_to(npc, op or 1)
                if r == "ok" or t.chat.kind() ~= "none" then break end
                t.note(name .. "-try" .. try .. ": " .. tostring(r) .. " " .. tostring(d))
                t.ticks(3)
            end
            await_page()
            play_exact(name, pages)
            t.ticks(2)
        end
        local function finish_chat()
            for _ = 1, 30 do
                if t.chat.kind() == "none" then return end
                t.chat.continue_()
                t.ticks(1)
            end
        end
        local function press(sym)
            local r, w = t.ui.widget(sym)
            if r ~= "ok" then return false end
            t.ui.invoke(w, 1)
            t.ticks(1)
            return true
        end
        -- Configure on the ring the player stands at, the wheels turned to
        -- the code (they open on A I P), Confirm.
        local function dial(name, loc, where, code)
            t.player.click_loc(loc, 2, { at = where })
            t.check(name .. ".open", t.ui.await_open("fairyrings", 8) == "ok", "the dial at " .. where[1] .. "," .. where[2])
            local wheels = { math.floor(code / 16), math.floor(code / 4) % 4, code % 4 }
            for w = 1, 3 do
                for _ = 1, wheels[w] do press("fairyrings:" .. w .. "_clockwise") end
            end
            press("fairyrings:confirm")
            t.ticks(7)
        end
        local function hideout(name)
            tele(name .. ".zanaris", "0_37_69_44_19")
            dial(name .. ".air", "fairyring_homehub", ZANARIS_RING, 2)
            t.check(name .. ".air.land", at(2700, 3247), "AIR: " .. tile_text())
            dial(name .. ".dlr", "fairyring_minorhub", { 2700, 3247 }, 22)
            t.check(name .. ".dlr.land", at(2213, 3099), "DLR: " .. tile_text())
            dial(name .. ".djq", "fairyring_minorhub", { 2213, 3099 }, 31)
            await_page(4)
            local _, raw = t.chat.text()
            t.check(name .. ".djq.line", raw == "Wow, fairy magic sure is useful, I hardly moved at all!", tostring(raw))
            finish_chat()
            dial(name .. ".ajs", "fairyring_minorhub", { 2213, 3099 }, 13)
            t.check(name .. ".hideout", at(2328, 4426), "AJS after the sequence: " .. tile_text())
        end

        -- Martin after Fairytale I (4606 [82..114]), then impatient [123..126].
        t.ticklog.mark("leg.martin")
        tele("martin", "0_48_50_6_57")
        talk("martin.report", "martin_the_master_farmer", MARTIN_REPORT)
        stage("waiting", 5)
        talk("martin.wait", "martin_the_master_farmer", MARTIN_WAIT)
        -- The crops grow 573 ticks after the report (4606 [114] -> [687]).
        t.ticks(580)
        stage("may_start", 10)
        talk("martin.start", "martin_the_master_farmer", MARTIN_START)
        stage("start", 20)

        -- The grotto (4654 [550..580]): walked into, the pan, the line.
        t.ticklog.mark("leg.grotto")
        tele("grotto", "0_37_69_23_45")
        local mark = t.cutscene.mark()
        t.player.walk_to(2387, 4471)
        local cr, cd = t.cutscene.await("grotto", { since = mark, timeout = 40, expect = { { op = "moveto", height = 300 }, { op = "lookat", height = 25 }, { op = "reset" } } })
        t.check("grotto.pan", cr == "ok", tostring(cd))
        await_page(40)
        play_exact("grotto.line", { "player:Wow! Somebody has trashed this cave! And what's<br>happened to Fairy Nuff and the Fairy Queen?" })
        stage("grotto", 30)

        -- The vials and the certificate [589..677].
        t.player.click_loc("fairytale2_bench_viles_lft_multi", 1)
        await_page()
        play_exact("vials", { "box:You don't find anything useful, just some glass from a<br>broken vial." })
        t.player.click_loc("fairytale2_cert_large_broken_a_multi", 1)
        await_page()
        play_exact("certificate", { "box:You pick up Fairy Nuff's certificate, being careful not<br>to cut yourself on its broken frame." })
        t.check("certificate.held", count("fairytale2_nuffs_certificate") == 1, "Nuff's certificate in the pack")
        t.player.inv_op("fairytale2_nuffs_certificate", 1)
        t.check("certificate.read", t.ui.await_open("fairy2_certificate_broken", 4) == "ok", "Read opens interface 423")
        t.ui.close_modal()
        t.ticks(1)
        t.player.inv_op("fairytale2_nuffs_certificate", 2)
        t.check("certificate.study", t.ui.await_open("fairy2_message", 4) == "ok", "Study opens interface 424")
        t.ui.close_modal()
        t.ticks(1)

        -- The sheep and the chef see it [881..887], [1218..1223]; the plaque [780..783].
        tele("sheep", "0_37_69_48_19")
        talk("sheep", "fairy_sheep", {
            "box:You show the certificate to the sheep.",
            "player:Do you know what these strange marks mean?",
            "npc:The humans found some symbols<br> And doesn't know what they mean<br>So now it's cross-examining me<br>And spoiling my routine!",
        })
        tele("chef", "0_37_69_18_24")
        talk("chef", "fairy_chef", {
            "box:You show the certificate to the Fairy chef.",
            "player:Do you know what these strange marks mean?",
            "npc:Sorry dearie, no, but I have seen a sign with similar<br>marks near the mysterious ruins. You know, the ones<br>that lead you to the Cosmic Rune Altar.",
        })
        tele("plaque", "0_37_68_41_18")
        t.player.click_loc("fairy2_runetemple_sign", 1)
        t.check("plaque.open", t.ui.await_open("fairy2_runetemple_sign", 6) == "ok", "the plaque's interface 425")
        t.ui.close_modal()
        t.ticks(1)

        -- The Godfather's permission [1491..1523], then the Co-ordinator [1553..1561].
        t.ticklog.mark("leg.godfather")
        tele("godfather", "0_38_69_14_13")
        talk("godfather", "fairy_godfather_multi", GODFATHER)
        stage("permission", 40)
        tele("coordinator", "0_38_69_19_33")
        talk("coordinator", "fairy_coordinator", COORDINATOR)
        t.ticks(2)

        -- The hideout (4654 [1631..1728]) and Fairy Nuff [1861..1890].
        t.ticklog.mark("leg.hideout")
        t.cheat("::wield dramen_staff")
        t.ticks(1)
        hideout("hideout1")
        tele("nuff", "0_36_69_48_36")
        talk("nuff", "fairy_nuff_multi2", NUFF)
        stage("steal", 50)

        -- The Godfather's pockets: back to Zanaris by the hideout ring's
        -- Zanaris op, then pickpocket until it works (a failure wakes in
        -- Lumbridge, [2041]).
        t.ticklog.mark("leg.pockets")
        t.player.click_loc("fairyring_minorhub", 1, { at = { 2328, 4426 } })
        t.ticks(8)
        t.check("hideout.zanaris", at(2412, 4434), "the hideout ring's Zanaris: " .. tile_text())
        -- Only from behind him works: stand north of him and try until his
        -- 20-tick turn faces him south (a try while he faces anyone else is
        -- the knockout, [2030..2045]).
        local knocked = 0
        for try = 1, 16 do
            tele("pockets" .. try, "0_38_69_14_12")
            t.player.talk_to("fairy_godfather_multi", 3)
            t.ticks(14)
            local _, v = t.var.server("varb2326_fairy2_queencure_quest")
            if v == 60 then break end
            if at(3139, 3228) then
                knocked = knocked + 1
                t.note("pockets.try" .. try .. ": knocked out to 3139,3228")
            end
            finish_chat()
        end
        finish_chat()
        stage("stolen", 60)
        t.note("pockets: " .. knocked .. " knockout(s) before his back was turned")
        t.check("secateurs.held", count("fairy_queen_secateurs2") == 1, "the Queen's secateurs")

        -- The secateurs to Nuff [2324..2350].
        t.ticklog.mark("leg.secateurs")
        hideout("hideout2")
        tele("nuff2", "0_36_69_48_36")
        t.player.use_on("fairy_queen_secateurs2", t.player.by_symbol("npc", "fairy_nuff_multi2"))
        await_page()
        play_exact("secateurs", SECATEURS)
        stage("essence", 70)

        -- A star flower on the Cosmic Entity's plane [2433..2693].
        t.ticklog.mark("leg.starflower")
        tele("ckp.ring", "0_36_69_24_11")
        dial("ckp", "fairyring_minorhub", { 2328, 4426 }, 40)
        t.check("ckp.land", at(2075, 4848), "CKP: " .. tile_text())
        for _ = 1, 40 do
            local r = t.player.talk_to("fairy2_starflower_fullgrown", 1)
            if count("fairytale2_flower_herbs") > 0 then break end
            t.ticks(10)
        end
        t.check("starflower.picked", count("fairytale2_flower_herbs") == 1, "a star flower")

        -- A gorak (4653 [48..261]).
        t.ticklog.mark("leg.gorak")
        t.player.click_loc("fairyring_minorhub", 1, { at = { 2075, 4848 } })
        t.ticks(8)
        dial("dir", "fairyring_homehub", ZANARIS_RING, 18)
        t.check("dir.land", at(3038, 5348), "DIR: " .. tile_text())
        -- Several goraks hunt at once and hit through Protect: the fight's
        -- damage is not what this test checks.
        t.cheat("::god")
        t.cheat("::wield dragon_scimitar")
        t.ticks(1)
        for _ = 1, 30 do
            local _, killed = t.var.server("varb2382_fairy2_gorak_killed")
            if killed == 1 then break end
            t.player.attack("fairytale2_gorak", 2, 10)
            t.ticks(8)
        end
        t.ticks(3)
        t.player.click_obj("fairytale2_gorak_claws", 3)
        t.ticks(3)
        t.check("gorak.claws", count("fairytale2_gorak_claws") == 1, "the claws, the killer's own drop")

        t.cheat("::wield dramen_staff")
        t.ticks(1)
        t.player.click_loc("fairyring_minorhub", 1, { at = { 3038, 5348 } })
        t.ticks(8)
        t.check("dir.leave", at(2412, 4434), "back in Zanaris: " .. tile_text())
        -- The goraks drain a random stat on every accurate hit; restore the
        -- level the brew needs.
        t.cheat("::setlevel herblore 57")
        t.ticks(1)

        -- The potion [280..291], mixed in Zanaris (4653 [271..291]).
        t.ticklog.mark("leg.potion")
        t.player.use_item_on_item("fairytale2_flower_herbs", "vial_water")
        t.ticks(3)
        stage("flower_vial", 72)
        t.player.use_item_on_item("pestle_and_mortar", "fairytale2_gorak_claws")
        t.ticks(2)
        t.player.use_item_on_item("fairytale2_ground_gorak_claws", "fairytale2_starflowervial")
        t.ticks(3)
        stage("essence_made", 73)

        -- The Queen [394..437].
        t.ticklog.mark("leg.queen")
        hideout("hideout3")
        tele("queen", "0_36_69_48_36")
        t.player.use_on("3dosemagicess", t.player.by_symbol("loc", "fairy_queen_sleeping_multi2"))
        t.ticks(6)
        stage("awake", 80)
        talk("finale", "fairy_queen_multi2", FINALE)
        t.ticks(3)
        stage("complete", 90)
        t.check("lamp", count("twocats_rewardlamp") == 1, "the Fairy Queen's lamp")
    end,
}
