-- The Grand Tree, played the way RSProx index 4429 plays it, end to end
-- (docs/rsprox-analysis/quests/the_grand_tree.md; every page asserted is a
-- recorded page, `[tick]` cites are 4429's). Content:
-- OSRS-Content/osrs-content/server/scripts/quests/quest_grandtree/ and
-- areas/area_gnome/scripts/{hazelmere,gnome_glider,gnome_gate}.rs2.
--
-- The recorder started with Agility below 25 and combat below 50 (the start's
-- warning names both, [281]) and drank an agility potion before the tree
-- ([1544]); the player here has the Agility and the potion, but melee levels
-- from the start (the linter keeps stats in setup), so the warning is its
-- skills-only line. The demon was fire-struck from a safespot ([1633..1913],
-- the recorder's kit); here it is fought in the recommended kit. Travel between
-- scenes is ::tele (the recorder's teleports are generic content); every
-- quest door, ladder, cell, cart and gate is played for real. Femi was helped
-- before the recording began, so the setup marks her boxes lifted.
return {
    id = "grandtree",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::setlevel agility 24",
        -- Melee for the demon: stats belong in setup (lint), so the start's
        -- warning is its skills-only line here, not the recorder's two.
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give 4dose1agility 1",
        "::give rune_scimitar 1",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 12",
        "::setvar varp6531_femi_help 2",
        "::tele 0_38_54_34_40",
    },

    run = function(t)
        t.ticklog.start()
        t.ticks(3)

        local bind_result, bind_detail = t.quest.bind({
            varp = "varp150_grandtree",
            constants = {
                not_started = 0, started = 10, spoken_hazelmere = 20, relayed_message_narnode = 30,
                spoken_glough = 40, found_prisoner = 50, spoken_prisoner = 60, found_journal = 70,
                released_prison = 80, obtained_lumber_order = 90, clue_charlie = 100,
                found_invasion_plans = 110, given_twigs = 120, unlocked_trapdoor = 130,
                defeated_black_demon = 140, searching_daconia = 150, complete = 160,
            },
            display = "The Grand Tree",
            points = 5,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        local function tile_text(r, tt)
            return r == "ok" and type(tt) == "table" and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end
        local function at(x, z, level)
            return function()
                local r, tt = t.world.tile()
                return r == "ok" and tt.x == x and tt.z == z and tt.level == level
            end
        end
        -- A scene that moves the player: wait for the tile, then grade it.
        local function landed(name, x, z, level, ticks)
            local ar = t.await({ level = at(x, z, level), note = name }, ticks)
            local r, tt = t.world.tile()
            t.check(name, ar == "ok", tile_text(r, tt) .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end
        -- A page that comes on its own (after a walk, a fade, an arrival).
        local function page(name, kind, ticks)
            t.expect(name, t.await({ level = function() return t.chat.kind() == kind end, note = name }, ticks))
        end
        -- A modal's rows, read off its components, against the recorded rows
        -- (the driver reads text without its tags; the tags are the wire's).
        local function plain(s)
            return (string.gsub(s or "", "<[^>]*>", ""))
        end
        local function rows(name, iface, prefix, first, want)
            local diff = nil
            for i, row in ipairs(want) do
                local _, text = t.ui.text(iface .. ":" .. prefix .. (first + i - 1))
                if diff == nil and plain(text) ~= plain(row) then
                    diff = prefix .. (first + i - 1) .. ": want '" .. row .. "' read '" .. tostring(text) .. "'"
                end
            end
            t.check(name, diff == nil, diff or (#want .. " rows match"))
        end
        local function scroll(name, item, want)
            t.exec(name .. ".read", t.player.inv_op, item, 1)
            t.expect(name .. ".open", t.ui.await_open("messagescroll", 5))
            rows(name .. ".rows", "messagescroll", "messcroll", 1, want)
            t.ui.close_modal()
            t.ticks(1)
        end
        -- The first spread of a book: the title and both sides' rows.
        local function book(name, item, title, left, right)
            t.exec(name .. ".read", t.player.inv_op, item, 1)
            t.expect(name .. ".open", t.ui.await_open("book", 5))
            local _, got = t.ui.text("book:title")
            t.check(name .. ".title", plain(got) == title, "title '" .. tostring(got) .. "' (want '" .. title .. "')")
            rows(name .. ".left", "book", "page_left_text_", 1, left)
            rows(name .. ".right", "book", "page_right_text_", 1, right)
            t.exec(name .. ".turn", t.ui.read_book, 10)
        end

        -- ---- 1. The start ([244..308]) ------------------------------------
        t.exec("start.talk", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("start.pages", t.chat.play, {
            "npc:Welcome Traveller. I am King Narnode. It's nice to see an outsider.",
            "player:Hi! It seems to be a busy settlement.",
            "npc:For now.",
            "choose:You seem worried, what's up?",
            "player:You seem worried, what's up?",
            "npc:Traveller, Can I speak to you in strictest confidence?",
            "player:Of course sire.",
            "npc:Not here, follow me.",
            "end",
        })
        -- Down the trapdoor on C+10 onto 2463,9896; the first page on C+17.
        landed("start.down", 2463, 9896, 0, 20)
        page("start.foundations.page", "player", 15)
        t.exec("start.foundations", t.chat.play, {
            "player:So what is this place?",
            "npc:These, my friend, are the foundations of the stronghold.",
            "player:They look like roots to me.",
            "npc:Not just any roots Traveller!",
            "player:Impressive. What exactly is the problem?",
            "npc:In the last two months our tree guardians have",
            "player:You mean the tree is ill?",
            "npc:In effect yes.",
            "mesbox:Before starting this quest, be aware that one or more of your skill",
            "choose:Yes.",
            "player:I'd be happy to help!",
            "npc:Thank Guthix for your arrival!",
            "npc:The first task is to find out what's killing the tree.",
            "player:Do you have an idea?",
            "npc:My top tree guardian, Glough, believes it's human",
            "player:Who's Hazelmere?",
            "npc:Hazelmere is one of the mages that created the Grand",
            "objbox:The king shows you a sample of bark.",
            "npc:The mage only talks in the old tongue, you'll need this.",
            "objbox:The king shows you a translation book.",
            "player:What is it?",
            "npc:It's a translation book, you'll need it to translate what",
            "objbox:The king hands you the bark sample and the translation",
            "npc:I'll show you the way back up.",
            "end",
        })
        t.expect("start.stage", t.quest.expect_stage("started"))
        t.expect("start.bark", t.inv.expect_has("grandtree_barksample", 1))
        t.expect("start.book", t.inv.expect_has("grandtree_translationbook", 1))
        landed("start.up", 2464, 3497, 0, 15)

        book("book", "grandtree_translationbook", "Gnome Translations",
            { "Gnome Translations", "", "written by Anita", "", "This text contains the ancient", "Gnome words I have",
                "managed to translate thus", "far." },
            { "-A-", "arpos: rocks", "ando: gate", "andra: city", "ataris: cow", "", "-C-", "cef: threat", "cheray: lazy",
                "Cinqo: King", "cretor: bucket" })

        -- ---- 2. Hazelmere ([457..494]) -----------------------------------
        t.cheat("::tele 0_41_48_53_25")
        t.ticks(3)
        t.exec("hazelmere.door", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen", at = { 2677, 3088, 0 },
            near = { 2677, 3089 }, far = { 2677, 3088 }, far_ok = function(tile) return tile.z <= 3088 end,
            far_desc = "inside Hazelmere's hut" })
        t.exec("hazelmere.ladder", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
            at = { 2677, 3087, 0 }, dest = { 2677, 3088, 1 }, slack = 1 })
        t.exec("hazelmere.talk", t.player.talk_to, "grandtree_hazelmere", 1)
        t.exec("hazelmere.pages", t.chat.play, {
            "mesbox:The mage starts to speak but all you hear is:",
            "npc:Blah. Blah, blah, blah, blah...blah!",
            "objbox:You give the bark sample to Hazelmere.",
            "mesbox:The mage carefully examines the sample.",
            "npc:Blah, blah...Daconia...blah, blah.",
            "player:Can you write this down and I'll try and translate it?",
            "npc:Blah, blah?",
            "mesbox:You make a writing motion.",
            "objbox:Hazelmere has given you the scroll.",
            "end",
        })
        t.expect("hazelmere.stage", t.quest.expect_stage("spoken_hazelmere"))
        t.expect("hazelmere.bark_gone", t.inv.expect_absent("grandtree_barksample"))
        scroll("hazelmere.scroll", "grandtree_scroll", { "", "", "", "", "", "", "",
            "Es lemanto meso pro eis prit ta Cinqo mond.", "Mi lovos ta lemanto Daconia arpos",
            "et Daconia arpos eto meriz ta priw!", "", "", "", "", "" })
        t.exec("hazelmere.again", t.player.talk_to, "grandtree_hazelmere", 1)
        t.exec("hazelmere.again.pages", t.chat.play, {
            "npc:Blah, blah...Daconia...blah, blah.",
            "mesbox:You still can't understand Hazelmere.",
            "end",
        })
        -- [488..494]: the scroll dropped (privately) and written again.
        t.exec("hazelmere.drop", t.player.drop, "grandtree_scroll")
        t.exec("hazelmere.rewrite", t.player.talk_to, "grandtree_hazelmere", 1)
        t.exec("hazelmere.rewrite.pages", t.chat.play, {
            "npc:Blah, blah...Daconia...blah, blah.",
            "player:Can you write this down and I'll try and translate it?",
            "npc:Blah, blah?",
            "mesbox:You make a writing motion.",
            "objbox:Hazelmere has given you the scroll.",
            "end",
        })
        t.expect("hazelmere.scroll", t.inv.expect_has("grandtree_scroll", 1))

        -- ---- 3. The translation, wrong then right ([611..668]) -----------
        t.cheat("::tele 0_38_54_34_40")
        t.ticks(3)
        t.exec("translate.wrong.talk", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("translate.wrong", t.chat.play, {
            "player:Hello again, your highness.",
            "npc:Hello Traveller, did you speak to Hazelmere?",
            "player:Yes! I managed to find him.",
            "npc:Do you understand what he said?",
            "choose:I think so!",
            "player:I think so!",
            "npc:So what did he say?",
            "choose:King Narnode must be stopped, he is a madman!",
            "player:King Narnode must be stopped, he is a madman!",
            "choose:The tree is fine, you have nothing to fear.",
            "player:The tree is fine, you have nothing to fear.",
            "choose:There is no need for haste, just send a runner.",
            "player:There is no need for haste.",
            "choose:Use a bucket of milk from a sacred cow.",
            "player:Use a bucket of milk from a sacred cow.",
            "choose:You must wait till the second night.",
            "player:You must wait till the second night.",
            "npc:Wait a minute, that doesn't sound like Hazelmere!",
            "player:Erm...I think so.",
            "npc:I'm sorry Traveller but this is no good.",
            "end",
        })
        t.exec("translate.right.talk", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("translate.right", t.chat.play, {
            "player:Hello again, your highness.",
            "npc:Hello Traveller, did you speak to Hazelmere?",
            "player:Yes! I managed to find him.",
            "npc:Do you understand what he said?",
            "choose:I think so!",
            "player:I think so!",
            "npc:So what did he say?",
            "choose:None of the above.",
            "choose:None of the above.",
            "choose:A man came to me with the King's seal.",
            "player:A man came to me with the King's seal.",
            "choose:I gave the man Daconia rocks.",
            "player:I gave the man Daconia rocks.",
            "choose:And Daconia rocks will kill the tree!",
            "player:And Daconia rocks will kill the tree!",
            "npc:Of course! I should've known!",
            "player:What are Daconia stones?",
            "npc:Hazelmere created the Daconia stones.",
            "npc:This is terrible! The stones must be recovered!",
            "player:Can I help?",
            "npc:First I must warn the tree guardians.",
            "npc:If he's not there he will be at his girlfriend Anita's place.",
            "player:OK! I'll be back soon.",
            "end",
        })
        t.expect("translate.stage", t.quest.expect_stage("relayed_message_narnode"))

        -- ---- 4. Glough, his cupboard and chest ([691..722]) --------------
        t.cheat("::tele 0_38_54_43_7")
        t.ticks(3)
        local function glough_up(name)
            t.exec(name, t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 2476, 3463, 0 }, dest = { 2475, 3463, 1 }, slack = 1 })
        end
        local function glough_down(name)
            t.exec(name, t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
                at = { 2476, 3463, 1 }, dest = { 2477, 3463, 0 }, slack = 1 })
        end
        glough_up("glough.up")
        t.exec("glough.talk", t.player.talk_to, "grandtree_glough", 1)
        t.exec("glough.pages", t.chat.play, {
            "player:Hello.",
            "mesbox:The gnome is munching on a worm hole.",
            "npc:Can I help human? Can't you see I'm eating?!",
            "mesbox:The gnome continues to eat.",
            "player:The King asked me to inform you that the Daconia",
            "npc:Surely not!",
            "player:Apparently a human took them from Hazelmere.",
            "npc:I should've known! The humans are going to invade!",
            "player:Never!",
            "npc:Your type can't be trusted! I'll take care of this!",
            "end",
        })
        t.expect("glough.stage", t.quest.expect_stage("spoken_glough"))
        t.exec("cupboard.open", t.player.click_loc, "grandtree_cupboardclosed", 1, { at = { 2476, 3465, 1 } })
        t.ticks(3)
        t.exec("cupboard.search", t.player.click_loc, "grandtree_cupboardopen", 2, { at = { 2476, 3465, 1 } })
        t.expect("cupboard.nothing", t.msg.expect("You search the cupboard but find nothing."))
        t.exec("chest.locked", t.player.click_loc, "grandtree_chestclosed", 1, { at = { 2482, 3462, 1 } })
        page("chest.locked.wait", "mesbox", 12)
        t.exec("chest.locked.page", t.chat.play, { "mesbox:The chest is locked.", "end" })

        -- ---- 5. The King, the prisoner ([754..795]) ----------------------
        t.cheat("::tele 0_38_54_34_40")
        t.ticks(3)
        t.exec("prisoner.king", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("prisoner.king.pages", t.chat.play, {
            "player:Hello, your highness. Have you any news on the",
            "npc:It's OK Traveller, thanks to Glough!",
            "player:Wow! That was quick!",
            "npc:Yes Glough really knows what he's doing.",
            "npc:Maybe Glough was right, maybe humans are invading!",
            "player:I doubt it, can I speak to the prisoner?",
            "npc:Certainly. He's on the top level of the tree.",
            "end",
        })
        t.expect("prisoner.stage", t.quest.expect_stage("found_prisoner"))
        -- The tree's ladder, rung by rung ([768..779]).
        t.exec("tree.up1", t.player.climb, { loc = "grandtree_ladderbottom", op = 1, op_name = "Climb-up",
            at = { 2466, 3495, 0 }, dest = { 2466, 3496, 1 }, slack = 1 })
        t.exec("tree.up2", t.player.climb, { loc = "grandtree_laddermiddle_bottom", op = 2, op_name = "Climb-up",
            at = { 2466, 3495, 1 }, dest = { 2466, 3496, 2 }, slack = 1 })
        t.exec("tree.up3", t.player.climb, { loc = "grandtree_laddermiddle_top", op = 2, op_name = "Climb-up",
            at = { 2466, 3495, 2 }, dest = { 2466, 3496, 3 }, slack = 1 })
        -- Charlie is answered from where the ladder leaves the player, two
        -- tiles from him through the cell door ([782]).
        t.exec("charlie.talk", t.player.talk_to, "grandtree_charlie_multi", 1)
        t.exec("charlie.pages", t.chat.play, {
            "player:Tell me. Why would you want to kill the Grand Tree?",
            "npc:What do you mean?!",
            "player:Don't tell me, you just happened to be caught carrying",
            "npc:All I know is that I did what I was asked.",
            "player:I don't understand.",
            "npc:Glough paid me to go to this gnome on a hill.",
            "npc:I've been doing it for weeks",
            "player:Sounds like Glough is hiding something!",
            "npc:I don't know what he's up to.",
            "player:Okay. Thanks Charlie.",
            "npc:Good luck!",
            "end",
        })
        t.expect("charlie.stage", t.quest.expect_stage("spoken_prisoner"))
        -- Bottom-Floor from the top ([794..795]).
        t.exec("tree.bottom", t.player.climb, { loc = "grandtree_laddertop", op = 2, op_name = "Bottom-Floor",
            at = { 2466, 3495, 3 }, dest = { 2466, 3496, 0 }, slack = 1 })
        t.exec("culprit.king", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("culprit.king.pages", t.chat.play, {
            "npc:Hello Traveller. So, did you speak to the culprit?",
            "player:Yes I did and something's not right!",
            "npc:What do you mean?",
            "player:The prisoner said he was paid by Glough to get the",
            "npc:That's absurd! He's just trying to save himself!",
            "npc:Since Glough's wife died he's been a little strange.",
            "npc:Humans are planning to invade and wipe out the tree",
            "player:But why?",
            "npc:Who knows? You may have to leave soon Traveller!",
            "end",
        })

        -- ---- 6. The journal, the arrest, the cell ([836..889]) -----------
        t.cheat("::tele 0_38_54_43_7")
        t.ticks(3)
        glough_up("journal.up")
        t.exec("journal.search", t.player.click_loc, "grandtree_cupboardopen", 2, { at = { 2476, 3465, 1 } })
        page("journal.found.page", "objbox", 10)
        t.exec("journal.found", t.chat.play, { "objbox:You've found Glough's Journal!", "end" })
        t.expect("journal.stage", t.quest.expect_stage("found_journal"))
        book("journal", "grandtree_journal", "Journal",
            { "<col=ff0000>The migration failed!", "", "After spending half a century", "hiding underground you" },
            { "Living in constant fear of", "human aggression, we are in" })
        t.exec("arrest.talk", t.player.talk_to, "grandtree_glough", 1)
        t.exec("arrest.pages", t.chat.play, {
            "player:Glough! I don't know what you're up to but I know",
            "npc:You're a fool human! You have no idea what's going on.",
            "player:I know the Grand Tree's dying!",
            "npc:How dare you accuse me! I'm the head tree guardian!",
        })
        page("arrest.guard.page", "npc", 10)
        t.exec("arrest.guard", t.chat.play, { "npc:Come with me!", "end" })
        landed("arrest.cell", 2464, 3496, 3, 16)
        page("cell.king.page", "npc", 12)
        t.exec("cell.release", t.chat.play, {
            "npc:Traveller please accept my apologies!",
        })
        page("cell.player.page", "player", 8)
        t.exec("cell.pages", t.chat.play, {
            "player:I don't think you can trust Glough, your highness.",
            "npc:I know he can be a bit extreme at times.",
            "npc:I'm afraid Glough has placed guards on the front gate",
            "player:Well, OK.",
            "npc:I'm sorry again Traveller!",
            "end",
        })
        t.expect("cell.stage", t.quest.expect_stage("released_prison"))
        landed("cell.out", 2465, 3496, 3, 4)

        -- ---- 7. The glider to Karamja ([905..921]) -----------------------
        t.exec("glider.talk", t.player.talk_to, "pilot_grand_tree", 1)
        t.exec("glider.pages", t.chat.play, {
            "npc:Hi, the King said that you need to leave?",
            "player:Apparently humans are invading!",
            "npc:I find that hard to believe! I have lots of human friends.",
            "player:I don't understand it either!",
            "npc:So where to?",
            "choose:Take me to Karamja please!",
            "player:Take me to Karamja please!",
            "npc:Okay! You're the boss!",
        })
        landed("glider.crash", 2918, 3057, 0, 10)
        page("glider.sorry.page", "npc", 8)
        t.exec("glider.sorry", t.chat.play, { "npc:Sorry about that.", "*", "*", "*", "*", "*", "*", "*", "*", "end" })

        -- ---- 8. The shipyard: the gate, the foreman ([940..1100]) --------
        t.cheat("::tele 0_46_47_0_33")
        t.ticks(3)
        t.exec("shipyard.gate", t.player.click_loc, "grandtree_fencegate_l", 1, { at = { 2945, 3041, 0 } })
        t.exec("shipyard.password", t.chat.play, {
            "npc:Hey you! What are you up to?",
            "player:I'm trying to open the gate!",
            "npc:I can see that! Why?",
            "choose:Glough sent me.",
            "player:Glough sent me.",
            "npc:Hmm...really? What for?",
            "player:You're wasting my time! Take me to your superior!",
            "npc:OK. Password.",
            "choose:Ka.", "player:Ka.",
            "choose:Lu.", "player:Lu.",
            "choose:Min.", "player:Min.",
            "npc:Sorry to have kept you.",
            "end",
        })
        landed("shipyard.through", 2945, 3041, 0, 8)
        t.cheat("::tele 0_46_47_57_36")
        t.ticks(3)
        t.exec("foreman.talk", t.player.talk_to, "grandtree_foreman", 1)
        t.exec("foreman.pages", t.chat.play, {
            "player:Hello, are you in charge?",
            "npc:That's right, and you are...?",
            "player:Glough sent me to check on how you are doing.",
            "npc:Right. Glough sent a human?",
            "player:His gnomes are busy.",
            "npc:Hmm...in that case we'd better go to my office.",
            "npc:Follow me.",
        })
        landed("foreman.office", 2955, 3033, 0, 10)
        page("foreman.office.page", "npc", 30)
        -- [1032..1041]: the wrong answer; he says "Die imposter!" and does not attack.
        t.exec("foreman.wrong", t.chat.play, {
            "npc:Tell me again why you're here.",
            "player:Er...Glough sent me?",
            "npc:By the way how is Glough? Still with his wife?",
            "choose:Always arguing as usual!",
            "player:Always arguing as usual!",
            "npc:Really? That's odd, considering she died last year.",
            "end",
        })
        t.ticks(8)
        t.exec("foreman.again", t.player.talk_to, "grandtree_foreman", 1)
        t.exec("foreman.right", t.chat.play, {
            "npc:Tell me again why you're here.",
            "player:Er...Glough sent me?",
            "npc:By the way how is Glough? Still with his wife?",
            "choose:Sadly his wife is no longer with us!",
            "player:Sadly his wife is no longer with us!",
            "npc:Right answer. I have to watch for imposters.",
            "npc:What's Glough's favourite dish?",
            "choose:He loves worm holes.",
            "player:He loves worm holes.",
            "npc:OK. Just one more. What's the name of his new",
            "choose:Anita.",
            "player:Anita.",
            "npc:Well, well, you do know Glough.",
            "player:Of course, security is paramount.",
            "npc:As you can see things are going well.",
            "player:Indeed.",
            "npc:When I was asked to build a fleet large enough to",
            "player:That's a lot of troops!",
            "npc:True but if the gnomes are really going to take over",
            "player:Take over?",
            "npc:Of course, why else would Glough want 30 battleships?",
            "player:No?",
            "npc:I mean, for the kind of battleships Glough's ordered I'll",
            "objbox:The foreman has given you the lumber order.",
            "player:OK. I'll head off and give this order to Glough.",
            "end",
        })
        t.expect("foreman.stage", t.quest.expect_stage("obtained_lumber_order"))
        t.expect("foreman.order", t.inv.expect_has("grandtree_order", 1))

        -- ---- 9. Femi's cart ([1159..1182]) -------------------------------
        t.cheat("::tele 0_38_52_25_54")
        t.ticks(3)
        t.exec("femi.talk", t.player.talk_to, "grandtree_femi", 1)
        t.exec("femi.pages", t.chat.play, {
            "player:I can't believe they won't let me in!",
            "npc:I don't believe all this rubbish about an invasion.",
            "player:I really need to see King Narnode.",
            "npc:Well, as you helped me I suppose I could.",
            "player:OK, what should I do?",
            "npc:Jump in the back of the cart.",
        })
        landed("femi.in", 2461, 3407, 0, 12)
        t.exec("femi.after", t.chat.play, {
            "npc:Okay, traveller, you'd better get going.",
            "player:Thanks again!",
            "npc:That's okay, all the best.",
            "end",
        })

        -- ---- 10. Charlie's clue, the King, Anita ([1261..1350]) ----------
        t.cheat("::tele 0_38_54_34_40")
        t.ticks(3)
        t.exec("tree.top", t.player.climb, { loc = "grandtree_ladderbottom", op = 2, op_name = "Top-Floor",
            at = { 2466, 3495, 0 }, dest = { 2466, 3496, 3 }, slack = 1 })
        t.exec("clue.talk", t.player.talk_to, "grandtree_charlie_multi", 1)
        t.exec("clue.pages", t.chat.play, {
            "player:How are you doing Charlie?",
            "npc:I've been better.",
            "player:Glough has some plan to rule Gielinor!",
            "npc:I wouldn't put it past him, the Gnome's crazy!",
            "player:I need some proof to convince the King.",
            "npc:Hmm... you could be in luck!",
            "player:Okay, I'll see what I can find.",
            "end",
        })
        t.expect("clue.stage", t.quest.expect_stage("clue_charlie"))
        t.exec("tree.bottom2", t.player.climb, { loc = "grandtree_laddertop", op = 2, op_name = "Bottom-Floor",
            at = { 2466, 3495, 3 }, dest = { 2466, 3496, 0 }, slack = 1 })
        t.exec("revolt.king", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("revolt.king.pages", t.chat.play, {
            "player:Hello, your highness.",
            "npc:Please Traveller, if the gnomes see me talking to you",
            "player:That's crazy!",
            "npc:Glough's scared the whole town",
            "player:Don't you understand he's creating his own army?!",
            "npc:Please Traveller, leave before it's too late!",
            "end",
        })
        t.cheat("::tele 0_37_54_23_55")
        t.ticks(3)
        t.exec("anita.up", t.player.climb, { loc = "spiralstairs_wooden", op = 1, op_name = "Climb-up",
            at = { 2389, 3512, 0 }, dest = { 2388, 3513, 1 }, slack = 1 })
        t.exec("anita.talk", t.player.talk_to, "grandtree_anita", 1)
        t.exec("anita.pages", t.chat.play, {
            "player:Hello there.",
            "npc:Oh hello, I've seen you with the King.",
            "player:Yes, I'm helping him with a problem.",
            "npc:You must know my boyfriend Glough then?",
            "player:Indeed!",
            "npc:Could you do me a favour?",
            "choose:I suppose so.",
            "player:I suppose so.",
            "npc:Please give this key to Glough, he left it here last night.",
            "objbox:Anita gives you a key.",
            "npc:Thanks a lot.",
            "player:No... thank you!",
            "end",
        })
        t.expect("anita.key", t.inv.expect_has("grandtree_gloughskey", 1))

        -- ---- 11. The plans, the twigs ([1405..1493]) ---------------------
        t.cheat("::tele 0_38_54_43_7")
        t.ticks(3)
        glough_up("plans.up")
        t.exec("plans.glough", t.player.talk_to, "grandtree_glough", 1)
        t.exec("plans.glough.pages", t.chat.play, {
            "player:I'm going to stop you, Glough!",
            "npc:You're becoming quite annoying traveller!",
            "mesbox:Glough is searching his pockets.",
            "npc:Where are that darn key?",
            "npc:Leave human, before I have you put in the cage!",
            "end",
        })
        t.exec("plans.chest", t.player.use_on, "grandtree_gloughskey", t.player.by_symbol("loc", "grandtree_chestclosed"))
        page("plans.found.page", "objbox", 12)
        t.exec("plans.found", t.chat.play, { "objbox:You have found a scroll!", "end" })
        t.expect("plans.stage", t.quest.expect_stage("found_invasion_plans"))
        scroll("plans.scroll", "grandtree_invasionplans", { "", "<col=ff0000>Invasion", "",
            "Troops board three fleets of battleships at Karamja.", "<col=00ff00>Fleet 1",
            "Attacks Misthalin from the south.", "<col=00ff00>Fleet 2",
            "Groups at Crandor and attacks Asgarnia from the west.", "<col=00ff00>Fleet 3",
            "Sails north to attack Kandarin from south, reinforced", "by gnome foot soldiers leaving gnome stronghold.",
            "", "<col=ff0000>Take no prisoners!", "", "" })
        glough_down("plans.down")
        t.cheat("::tele 0_38_54_34_40")
        t.ticks(3)
        t.exec("twigs.king", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("twigs.king.pages", t.chat.play, {
            "player:Hi, your highness, did you think about what I said?",
            "npc:Look, if you're right about Glough I would have him",
            "player:Look, I found this at Glough's home!",
            "objbox:You give the King the invasion plans.",
            "npc:If these are to be believed then this is terrible!",
            "npc:But it's not proof, any one could have made these.",
            "npc:On the other hand, if Glough's right about the humans",
            "npc:The Grand Tree's still slowly dying",
            "objbox:The King has given you some twigs lashed together.",
            "end",
        })
        t.expect("twigs.stage", t.quest.expect_stage("given_twigs"))
        t.expect("twigs.plans_gone", t.inv.expect_absent("grandtree_invasionplans"))
        for _, twig in ipairs({ "grandtree_twigt", "grandtree_twigu", "grandtree_twigz", "grandtree_twigo" }) do
            t.expect("twigs." .. twig, t.inv.expect_has(twig, 1))
        end

        -- ---- 12. The watchtower, the pillars ([1522..1566]) --------------
        t.cheat("::tele 0_38_54_43_7")
        t.ticks(3)
        glough_up("tower.up")
        t.exec("tower.glough", t.player.talk_to, "grandtree_glough", 1)
        t.exec("tower.glough.pages", t.chat.play, {
            "player:Hello there!",
            "npc:You shouldn't be here human!",
            "player:What do you mean?",
            "npc:The Gnome Stronghold is for gnomes alone!",
            "player:Surely not!",
            "npc:We don't need your sort round here!",
            "mesbox:He doesn't seem very nice!",
            "end",
        })
        t.exec("tower.refused", t.player.click_loc, "grandtree_climbtree", 1, { at = { 2484, 3464, 1 } })
        page("tower.refused.wait", "mesbox", 12)
        t.exec("tower.refused.page", t.chat.play, { "mesbox:You need at least 25 Agility to climb the tree.", "end" })
        t.exec("tower.potion", t.player.inv_op, "4dose1agility", 1)
        t.ticks(2)
        t.expect("tower.boosted", t.inv.expect_has("3dose1agility", 1))
        t.exec("tower.climb", t.player.climb, { loc = "grandtree_climbtree", op = 1, op_name = "Climb",
            at = { 2484, 3464, 1 }, dest = { 2486, 3465, 2 }, slack = 1 })
        local pillars = {
            { "grandtree_twigt", "grandtree_pillart", 2485 }, { "grandtree_twigu", "grandtree_pillaru", 2486 },
            { "grandtree_twigz", "grandtree_pillarz", 2487 }, { "grandtree_twigo", "grandtree_pillaro", 2488 },
        }
        for _, p in ipairs(pillars) do
            t.exec("pillar." .. p[2], t.player.use_on, p[1], t.player.by_symbol("loc", p[2]), { at = { p[3], 3467, 2 } })
            t.ticks(2)
        end
        page("pillars.trapdoor.wait", "mesbox", 6)
        t.exec("pillars.trapdoor", t.chat.play, { "mesbox:With a grinding of machinery, a trapdoor snaps open!", "end" })
        t.expect("pillars.stage", t.quest.expect_stage("unlocked_trapdoor"))

        -- ---- 13. Glough's scene and the demon ([1574..1913]) -------------
        for _, item in ipairs({ "rune_scimitar", "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield" }) do
            t.exec("wear." .. item, t.player.equip, item)
        end
        local cam0 = t.cutscene.mark()
        t.exec("trapdoor", t.player.click_loc, "grandtree_trapdoortoweropen", 1, { at = { 2487, 3464, 2 } })
        page("scene.page", "player", 40)
        t.exec("scene.pages", t.chat.play, {
            "player:Glough?",
            "npc:You really are becoming a headache!",
            "player:You're crazy, Glough!",
            "npc:Bah! Well, soon you'll see, the gnomes are ready to",
            "player:What makes you think I'll let you get away with it?",
        })
        -- [1609..1613]: the page with no continue while the demon appears, the
        -- same page with its continue four ticks later.
        t.ticks(5)
        t.exec("scene.friend", t.chat.play, { "npc:Fool... meet my little friend!" })
        t.exec("scene.camera", t.cutscene.await, "scene", { since = cam0, expect = {
            { op = "moveto", height = 300 },
            { op = "lookat", height = 225 },
            { op = "moveto", height = 500 },
            { op = "moveto", height = 2000 },
            { op = "moveto", height = 600 },
            { op = "lookat", height = 300 },
            { op = "reset" },
        } })
        landed("scene.out", 2491, 9864, 0, 10)
        t.expect("demon.present", t.npc.await_present("grandtree_blackdemon", 20, 20))
        t.exec("demon.glough", t.player.talk_to, "grandtree_glough_battle", 1)
        t.exec("demon.glough.page", t.chat.play, { "npc:Just die, human!", "end" })
        t.exec("demon.fight", t.npc.await_dead, "grandtree_blackdemon", 400, 20, 10, { eat = { item = "shark", below = 60 } })
        t.expect("demon.stage", t.quest.expect_stage("defeated_black_demon"))
        t.expect("demon.glough_gone", t.npc.await_gone("grandtree_glough_battle", 12))

        -- ---- 14. The King below, the roots, the reward ([1974..2163]) ----
        t.exec("roots.walk", t.player.walk_route, { { 2462, 9896 } }, { max_hop = 40 })
        t.exec("roots.king", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("roots.king.pages", t.chat.play, {
            "npc:Traveller you're wounded! What happened?",
            "player:It's Glough! He set a demon on me!",
            "npc:What?! Glough?! With a demon?!",
            "player:Glough has a store of Daconia rocks further up the",
            "npc:Never! Not Glough! He's a good gnome at heart!",
        })
        page("roots.guard1", "npc", 4)
        t.exec("roots.guard1.pages", t.chat.play, { "npc:Guard!" })
        page("roots.sire", "npc", 10)
        t.exec("roots.sire.pages", t.chat.play, { "npc:Sire!", "npc:Go and check out that passage!" })
        page("roots.report", "npc", 10)
        t.exec("roots.report.pages", t.chat.play, {
            "npc:We found Glough hiding under a horde of Daconia",
            "player:That's what I've been trying to tell you!",
            "npc:I..I don't know what to say!",
            "npc:Guard! Call off the military training!",
            "npc:The humans are not attacking!",
            "npc:Yes sir!",
            "npc:You have my full apologies Traveller!",
            "npc:A reward will have to wait though",
            "end",
        })
        t.expect("roots.stage", t.quest.expect_stage("searching_daconia"))

        -- The rolled root (daconia_coords: index -> coord); its south tile.
        local roots = {
            { "largeroot_gnome", 2456, 9886 }, { "largeroot_gnome", 2456, 9886 }, { "largeroot2_gnome", 2457, 9881 },
            { "largeroot2_gnome", 2455, 9874 }, { "largeroot_gnome", 2443, 9878 },
            { "largeroot2_gnome", 2439, 9881, via = { 2444, 9892 } },
            { "largeroot2_gnome", 2444, 9893 }, { "largeroot_gnome", 2452, 9893 }, { "largeroot2_gnome", 2465, 9891 },
            { "largeroot2_gnome", 2468, 9890 }, { "largeroot_gnome", 2467, 9896 }, { "largeroot_gnome", 2473, 9897 },
            { "largeroot2_gnome", 2481, 9904 }, { "largeroot_gnome", 2485, 9885 }, { "largeroot_gnome", 2490, 9889 },
            { "largeroot2_gnome", 2467, 9872 },
        }
        local rr, ri = t.var.server("varp6544_daconia_rock_root")
        local root = roots[(ri or 0) + 1]
        t.check("roots.rolled", rr == "ok" and root ~= nil, "daconia_rock_root = " .. tostring(ri))
        local route = { { root[2], root[3] - 1 } }
        if root.via then
            route = { root.via, { root[2], root[3] - 1 } }
        end
        t.exec("roots.walk2", t.player.walk_route, route, { max_hop = 40 })
        t.exec("roots.search", t.player.click_loc, root[1], 1, { at = { root[2], root[3], 0 } })
        page("roots.found.page", "objbox", 12)
        t.exec("roots.found", t.chat.play, { "objbox:You've found a Daconia rock!", "end" })
        t.expect("roots.rock", t.inv.expect_has("grandtree_daconiarock", 1))

        t.exec("reward.walk", t.player.walk_route, { { 2462, 9896 } }, { max_hop = 40 })
        local sr, snapshot = t.skill.snapshot()
        t.check("reward.snapshot", sr == "ok", tostring(sr))
        t.exec("reward.king", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("reward.pages", t.chat.play, {
            "npc:Traveller, have you managed to find the Daconia?",
            "player:Is this it?",
            "npc:Yes! Excellent, well done!",
            "objbox:You give the King the Daconia rock.",
            "npc:It's incredible, the tree's health is improving already!",
            "npc:To think Glough had me fooled all along!",
            "player:All that matters now is that humans and gnomes can",
            "npc:I'll drink to that!",
            "npc:From now on I vow to make this stronghold a",
            "player:Thanks!",
            "player:I think!",
            "npc:It should make your stay here easier.",
            "player:Mine?",
            "npc:Very few know of the secret mine under the Grand",
            "player:Strange!",
            "npc:That's magic trees for you!",
            "npc:All the best Traveller and thanks again!",
            "player:You too, your highness!",
            "end",
        })
        t.ticks(2)
        t.quest.expect_complete()
        t.expect("reward.rock_gone", t.inv.expect_absent("grandtree_daconiarock"))
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 18400, snapshot))
        t.expect("reward.agility_xp", t.skill.expect_gain("agility", 7900, snapshot))
        t.expect("reward.magic_xp", t.skill.expect_gain("magic", 2150, snapshot))
        t.finish(0)
    end,
}
