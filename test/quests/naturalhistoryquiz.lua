-- Varrock Museum basement: the Natural History Quiz, played end to end by one
-- account. Content: areas/varrock/scripts/vm_natural_history_quiz.rs2.
--
-- The answer key below is NOT read from the content. It is the OSRS wiki's
-- "Natural history quiz" page (oldschool.runescape.wiki, ?action=raw, read
-- 2026-10-15): all 84 questions, keyed by question text, valued by the
-- answer the wiki marks correct, with the wording RSProx 4340/1707 showed
-- where the two differ. The run clicks whichever row reads that text
-- (full stops ignored), so a question the content shows that the key does
-- not have, or a right answer on the wrong row, fails here.
-- Orlando Smith's lines are RSProx index 4340's pages (the recorded branches)
-- and Transcript:Orlando_Smith (the rest).
--
-- Route: Lumbridge -> the museum's basement stairs (3255,3451, Walk-down),
-- then Orlando (1759,4955), each display's Button and plaque room by room,
-- the reward, and both basement stair pairs (docs/rsprox-analysis/quests/
-- natural_history_quiz.md).
return {
    id = "naturalhistoryquiz",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
    },

    run = function(t)
        local IFACE = "vm_natural_history"
        local KEY = {
        ['How does a lizard regulate body heat?'] = "Sunlight",
        ['Who discovered how to kill lizards?'] = "The Slayer Masters",
        ['How many eyes does a lizard have?'] = "Three",
        ['What order do lizards belong to?'] = "Squamata",
        ['What happens when a lizard becomes cold?'] = "It becomes sleepy",
        ['Lizard skin is made of the same substance as?'] = "Hair",
        ['What is the name of the oldest tortoise ever recorded?'] = "Mibbiwocket",
        ["What is a tortoise's favourite food?"] = "Vegetables",
        ["Name the explorer who discovered the world's oldest tortoise."] = "Admiral Bake",
        ['How does the tortoise protect itself?'] = "Hard shell",
        ['If a tortoise had twenty rings on its shell, how old would it be?'] = "Twenty years",
        ['Which race breeds tortoises for battle?'] = "Gnomes",
        ['What is considered a delicacy by dragons?'] = "Runite",
        ["What is the best defence against a dragon's attack?"] = "Anti dragon-breath shield",
        ['How long do dragons live?'] = "Unknown",
        ['Which of these is not a type of dragon?'] = "Elemental",
        ['What is the favoured territory of a dragon?'] = "Old battle sites",
        ['Approximately how many feet tall do dragons stand?'] = "Twelve",
        ['How did the wyverns die out?'] = "Climate change",
        ['How many legs does a wyvern have?'] = "Two",
        ['Where have wyvern bones been found?'] = "Asgarnia",
        ['Which genus does the wyvern theoretically belong to?'] = "Reptiles",
        ["What are the wyverns' closest relations?"] = "Dragons",
        ['What is the ambient temperature of wyvern bones?'] = "Below room temperature",
        ['What is special about the shell of the giant Morytanian snail?'] = "It is resistant to acid",
        ['How do Morytanian snails capture their prey?'] = "Spitting acid",
        ['Which of these is a snail byproduct?'] = "Fireproof oil",
        ["What does 'Achatina Acidia' mean?"] = "Acid-spitting snail",
        ['How do snails move?'] = "Contracting and stretching",
        ["What is the 'trapdoor', which snails use to cover the entrance to their shells called?"] = "An operculum",
        ['What is snake venom adapted from?'] = "Stomach acid",
        ['Aside from their noses, what do snakes use to smell?'] = "Tongue",
        ['If a snake sticks its tongue out at you, what is it doing?'] = "Seeing how you smell",
        ['If some snakes use venom to kill their prey, what do other snakes use?'] = "Constriction",
        ['Lizards and snakes belong to the same order - what is it?'] = "Squamata",
        ['Which habitat do snakes prefer?'] = "Anywhere",
        ['We assume that sea slugs have a stinging organ on their soft skin - what is it called?'] = "Nematocysts",
        ['Why has the museum never examined a live sea slug?'] = "The researchers keep vanishing",
        ['What do we think the sea slug feeds upon?'] = "Seaweed",
        ['What are the two fangs presumed to be used for?'] = "Defence or display",
        ['Off of which coastline would you find sea slugs?'] = "Ardougne",
        ['In what way are sea slugs similar to snails?'] = "They have a hard shell",
        ['Which type of primates do monkeys belong to?'] = "Simian",
        ['Which have the lighter colour: Karamjan or Harmless monkeys?'] = "Harmless",
        ['Monkeys love bananas. What else do they like to eat?'] = "Bitternuts",
        ['There are two known families of monkeys. One is Karamjan, the other is...?'] = "Harmless",
        ['What colour mohawk do Karamjan monkeys have?'] = "Red",
        ['What have Karamjan monkeys taken a deep dislike to?'] = "Seaweed",
        ['Kalphites are ruled by a...?'] = "Pasha",
        ['What is the lowest caste in kalphite society?'] = "Worker",
        ['What are the armoured plates on a kalphite called?'] = "Lamellae",
        ['Are kalphites carnivores, herbivores or omnivores?'] = "Carnivores",
        ['What are kalphites assumed to have evolved from?'] = "Scarab beetles",
        ['Name the prominent figure in kalphite mythology?'] = "Scabaras",
        ["What is a terrorbird's preferred food?"] = "Anything",
        ['Who use terrorbirds as mounts?'] = "Gnomes",
        ['Where do terrorbirds get most of their water?'] = "Eating plants",
        ['How many claws do terrorbirds have?'] = "Four",
        ['What do terrorbirds eat to aid digestion?'] = "Stones",
        ['How many teeth do terrorbirds have?'] = "0",
        ['Which sense do penguins rely on when hunting?'] = "Sight",
        ['Which skill seems unusual for the penguins to possess?'] = "Planning",
        ['How do penguins keep warm?'] = "A layer of fat",
        ['What is the preferred climate for penguins?'] = "Cold",
        ['Describe the behaviour of penguins?'] = "Social",
        ['When do penguins fast?'] = "During breeding",
        ['What habitat do moles prefer?'] = "Subterranean",
        ['Why are moles considered to be an agricultural pest?'] = "They dig holes",
        ['Who discovered giant moles?'] = "Wyson the Gardener",
        ['What would you call a group of young moles?'] = "A labour",
        ["What is a mole's favourite food?"] = "Insects and other invertebrates",
        ['Which family do moles belong to?'] = "The Talpidae family",
        ['What is produced by feeding chilli to a camel?'] = "Toxic dung",
        ['If an ugthanki has one, how many does a bactrian have?'] = "Two",
        ['Camels:herbivore, carnivore or omnivore?'] = "Omnivore",
        ['What is the usual mood for a camel?'] = "Annoyed",
        ['Where would you find an ugthanki?'] = "Al Kharid",
        ['Which camel byproduct is known to be very nutritious?'] = "Milk",
        ['What is the favoured habitat of leeches?'] = "Water",
        ["What shape is the inside of a leech's mouth?"] = "'Y'-shaped",
        ['Which of these is not eaten by leeches?'] = "Apples",
        ['What contributed to the giant growth of Morytanian leeches?'] = "Environment",
        ['What is special about Morytanian leeches?'] = "They attack by jumping",
        ['How does a leech change when it feeds?'] = "It doubles in size",
        }
        local function norm(s)
            return string.lower((string.gsub(s or "", "[%s%.]", "")))
        end
        local ANSWER = {}
        for q, a in pairs(KEY) do
            ANSWER[norm(q)] = a
        end

        -- Display number, plaque and tile, Button and tile, progress varbit,
        -- and the panel title every open sends (RSProx 4340).
        local DISPLAYS = {
            { key = "lizard", n = 4, plaque = "vm_plaque_lizard", at = { 1743, 4978 }, button = "vm_button_lizard", bat = { 1741, 4978 }, var = "varb3675_vm_nathis_lizard_display", model = "vm_nathis_lizard", mid = 25557, spin = 5, title = "The Giant Lizard (Lacertilia Giganteus)" },
            { key = "tortoise", n = 3, plaque = "vm_plaque_battle_tortoise", at = { 1753, 4978 }, button = "vm_button_battle_tortoise", bat = { 1751, 4978 }, var = "varb3680_vm_nathis_tortoise_display", model = "vm_nathis_battle_tortoise", mid = 25547, spin = 5, title = "The Giant Tortoise (Testudines Giganteus)" },
            { key = "dragon", n = 1, plaque = "vm_plaque_dragon", at = { 1768, 4978 }, button = "vm_button_dragon", bat = { 1766, 4978 }, var = "varb3672_vm_nathis_dragon_display", model = "vm_nathis_dragon_01", mid = 25551, spin = 0, title = "The Dragon (Draconis Rex)" },
            { key = "wyvern", n = 2, plaque = "vm_plaque_wyvern", at = { 1778, 4978 }, button = "vm_button_wyvern", bat = { 1776, 4978 }, var = "varb3681_vm_nathis_wyvern_display", model = "vm_nathis_wyvern", mid = 25565, spin = 5, title = "The Wyvern (Draconis Ossis)" },
            { key = "snail", n = 14, plaque = "vm_plaque_snail", at = { 1776, 4963 }, button = "vm_button_snail", bat = { 1775, 4963 }, var = "varb3674_vm_nathis_snail_display", model = "vm_nathis_snail", mid = 25562, spin = 5, title = "The Giant Snail (Achatina Acidia Giganteus)" },
            { key = "snake", n = 13, plaque = "vm_plaque_snake", at = { 1783, 4963 }, button = "vm_button_snake", bat = { 1782, 4963 }, var = "varb3677_vm_nathis_snake_display", model = "vm_nathis_snake", mid = 25563, spin = 5, title = "The Snake (Serpentes Fellis)" },
            { key = "monkey", n = 11, plaque = "vm_plaque_monkey", at = { 1774, 4957 }, button = "vm_button_monkey", bat = { 1775, 4957 }, var = "varb3676_vm_nathis_monkey_display", model = "vm_nathis_monkey", mid = 25559, spin = 5, title = "The Monkey (Simiiformes Karamjan)" },
            { key = "seaslug", n = 12, plaque = "vm_plaque_seaslug", at = { 1781, 4957 }, button = "vm_button_seaslug", bat = { 1782, 4957 }, var = "varb3682_vm_nathis_seaslug_display", model = "vm_nathis_seaslugs_1", mid = 25561, spin = 0, title = "The Sea Slug (Opisthobranchia Alucinor)" },
            { key = "terrorbird", n = 9, plaque = "vm_plaque_terrorbird", at = { 1755, 4940 }, button = "vm_button_terrorbird", bat = { 1755, 4939 }, var = "varb3683_vm_nathis_terrorbird_display", model = "vm_nathis_terrorbird", mid = 25564, spin = 5, title = "The Terrorbird (Aves Terror)" },
            { key = "kalphite", n = 10, plaque = "vm_plaque_kalphite_queen", at = { 1762, 4938 }, button = "vm_button_kalphite_queen", bat = { 1762, 4939 }, var = "varb3684_vm_nathis_kalphite_display", model = "vm_nathis_kalphite_queen", mid = 25554, spin = 5, title = "The Kalphite Queen (Kalphiscarabeinae Pasha)" },
            { key = "camel", n = 6, plaque = "vm_plaque_camel", at = { 1737, 4963 }, button = "vm_button_camel", bat = { 1736, 4963 }, var = "varb3679_vm_nathis_camel_display", model = "vm_nathis_camel", mid = 25548, spin = 5, title = "The Camel (Camelus Bactrian)" },
            { key = "leech", n = 5, plaque = "vm_plaque_leech", at = { 1744, 4963 }, button = "vm_button_leech", bat = { 1743, 4963 }, var = "varb3685_vm_nathis_leech_display", model = "vm_nathis_leech", mid = 25555, spin = 5, title = "The Giant Leech (Hirudinea Acidia)" },
            { key = "mole", n = 7, plaque = "vm_plaque_mole", at = { 1735, 4957 }, button = "vm_button_mole", bat = { 1736, 4957 }, var = "varb3678_vm_nathis_mole_display", model = "vm_nathis_mole", mid = 25558, spin = 5, title = "The Giant Mole (Talpidae Wysonian)" },
            { key = "penguin", n = 8, plaque = "vm_plaque_penguine", at = { 1742, 4957 }, button = "vm_button_penguine", bat = { 1743, 4957 }, var = "varb3673_vm_nathis_penguin_display", model = "vm_nathis_penguine", mid = 25560, spin = 5, title = "The Penguin (Spheniscidae Callidus)" },
        }
        local function c(name)
            return IFACE .. ":" .. name
        end
        local function tile_str(tl)
            return tl and (tostring(tl.x) .. "," .. tostring(tl.z) .. "," .. tostring(tl.level)) or "nil"
        end
        local function server(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end

        -- Orlando, by the progress state his transcript branches on.
        local function talk_orlando(step, lines)
            t.ticklog.mark("leg." .. step)
            t.exec("goto-" .. step, t.player.goto_tile, 1759, 4957, 0)
            t.exec(step, t.player.talk_to, "vm_nathis_apprentice", 1)
            t.exec(step .. "-dialog", t.chat.play, lines)
            t.ticks(2)
        end

        -- The question on screen and the row the key's answer sits on.
        local function read_question(step)
            local qr, q = t.ui.text(c("vm_nathis_question"))
            local rows = {}
            for s = 1, 3 do
                local _, a = t.ui.text(c("vm_nathis_answer_0" .. s))
                rows[s] = a
            end
            local want = (qr == "ok") and ANSWER[norm(q)] or nil
            local slot = nil
            for s = 1, 3 do
                if want ~= nil and norm(rows[s]) == norm(want) then
                    slot = s
                end
            end
            t.check(step .. ".question", slot ~= nil,
                "'" .. tostring(q) .. "' rows [" .. tostring(rows[1]) .. " | " .. tostring(rows[2]) .. " | "
                    .. tostring(rows[3]) .. "]; key answer " .. tostring(want)
                    .. (slot and (" on row " .. slot) or (want and " on NO row" or " (question not in the key)")))
            return slot, q
        end

        local function press(step, sym)
            local wr, wid = t.ui.widget(c(sym))
            local ir, idetail = "no_row", "no widget"
            if wr == "ok" then
                ir, idetail = t.ui.invoke(wid, 1)
            end
            t.check(step, ir == "ok", sym .. " op1: widget " .. tostring(wr) .. ", invoke " .. tostring(ir)
                .. " " .. tostring(idetail))
        end

        -- An answer reaches the server next tick; Orlando's page follows it.
        local function await_page(step)
            t.expect(step .. ".page", t.await({
                level = function()
                    return t.chat.kind() == "npc"
                end,
                note = step .. ": Orlando's page",
            }, 5))
        end

        -- The plaque as every open sends it: the number, the title, the quiz
        -- group shown (or the creature group, once the display is done).
        local function check_panel(step, d)
            t.expect(step .. ".open", t.ui.await_open(IFACE, 20))
            t.expect(step .. ".number", t.ui.expect_text(c("vm_nathis_display_num"), "/^" .. d.n .. "$/"))
            t.expect(step .. ".title", t.ui.expect_text(c("vm_nathis_text"), d.title))
            -- creature_text ships hidden=yes and the quiz never touches it;
            -- scriptrun does not model the cache's flag, so it is not graded.
            local qs, ql = t.ui.shown(c("vm_nathis_quiz_text"))
            t.check(step .. ".layer", qs == "ok" and ql == true, "quiz_text shown=" .. tostring(ql))
            -- Live client only: scriptrun answers `unsupported`, and the
            -- wire audit checks IF_SETMODEL/IF_SETROTATESPEED there instead.
            local mr, mdetail, pose = t.ui.model_pose(c(d.model))
            t.check(step .. ".model", mr == "unsupported" or (mr == "ok" and pose and pose.model == d.mid and pose.y_speed == d.spin),
                tostring(mdetail) .. " model=" .. tostring(pose and pose.model) .. " (want " .. d.mid .. ", spin 0," .. d.spin .. ")")
        end

        local done = 0
        local function play_display(d, opts)
            local step = d.key
            t.ticklog.mark("leg.button." .. step)
            t.exec("button." .. step, t.player.click_loc, d.button, 1, { at = { d.bat[1], d.bat[2], 0 } })
            -- The longest hold is the sea slug's: release at push + 17.
            t.ticks(22)
            t.ticklog.mark("leg.study." .. step)
            t.exec("study." .. step, t.player.click_loc, d.plaque, 1, { at = { d.at[1], d.at[2], 0 } })
            check_panel("study." .. step, d)

            local asked = {}
            local function ask(s)
                local slot, q = read_question(s)
                if q then
                    t.check(s .. ".fresh", not asked[norm(q)], "'" .. tostring(q) .. "' asked twice in one sitting")
                    asked[norm(q)] = true
                end
                return slot
            end

            if opts and opts.wrong_first then
                local slot = ask(step .. ".wrong")
                if slot then
                    press(step .. ".wrong.press", "vm_nathis_answer_0" .. ((slot % 3) + 1))
                    await_page(step .. ".wrong")
                    t.exec(step .. ".wrong.verdict", t.chat.play, { "npc:Hmm, I don't think that's right, mate. Try again." })
                    check_panel(step .. ".wrong.reopen", d)
                    t.check(step .. ".wrong.no_progress", server(d.var) == 0,
                        d.var .. " = " .. tostring(server(d.var)) .. " after a wrong answer (want 0)")
                end
            end

            for k = 1, 3 do
                local s = step .. ".q" .. k
                local slot = ask(s)
                if not slot then
                    return
                end
                press(s .. ".press", "vm_nathis_answer_0" .. slot)
                await_page(s)
                local lines = { "npc:Nice job, mate. That looks about right." }
                if k == 3 then
                    lines[#lines + 1] = "npc:Bonza, mate! I think that's all of them."
                end
                t.exec(s .. ".verdict", t.chat.play, lines)
                t.expect(s .. ".progress", t.var.await_server(d.var, k, 5))
                if k < 3 then
                    check_panel(s .. ".reopen", d)
                end
            end
            done = done + 1
            t.expect(step .. ".kudos", t.var.await_server("varb3637_vm_kudos", 2 * done, 5))
            t.expect(step .. ".kudos_message", t.msg.expect("You have earned Kudos with the Museum!"))
            -- The last page closes on "Bonza": no reveal panel (4340 [479]).
            t.ticks(2)
            local pr, present = t.ui.shown(c("vm_nathis_question"))
            t.check(step .. ".closed", pr ~= "ok" or present == false, "quiz interface after Bonza: " .. tostring(pr) .. " " .. tostring(present))
        end

        t.ticklog.start()
        -- Into the basement.
        t.ticklog.mark("leg.stairs-down")
        t.exec("goto-museum", t.player.goto_tile, 3256, 3449, 0)
        t.exec("stairs-down", t.player.click_loc, "vm_basement_wall_stairs_down", 1, { at = { 3255, 3451, 0 } })
        t.ticks(4)
        local _, here = t.world.tile()
        t.check("stairs-down.landed", here ~= nil and here.x == 1759 and here.z == 4958 and here.level == 0,
            "after Walk-down at " .. tile_str(here) .. " (want 1759,4958,0: 4340 [410])")

        -- A plaque before Orlando's favour: the wiki's message, no interface.
        t.player.click_loc("vm_plaque_lizard", 1, { at = { 1743, 4978, 0 } })
        t.ticks(3)
        t.expect("study-before.message", t.msg.expect("This looks like some sort of quiz. I wonder if I can take part..."))
        local pr, present = t.ui.shown(c("vm_nathis_question"))
        t.check("study-before.no_interface", pr ~= "ok" or present == false, "quiz interface " .. tostring(pr) .. " " .. tostring(present))

        local intro = {
            "npc:G'day there, mate.",
            "player:Good day.",
            "npc:Well, mate, to tell you the truth, I think I've come a gutser with these displays.",
            "player:Come a what?",
            "npc:Gutser and no mistake. Me boss asked me to put together a quiz for the visitors.",
            "npc:But to be deadset with you, I wasn't paying much attention",
            "player:You mean the natural historian?",
            "npc:Yep, that's the bloke. Say, mate, you do me a favour?",
            "player:Perhaps. What do you need?",
            "npc:Well, you look like a pretty smart cobber.",
        }
        local decline = {}
        for i = 1, #intro do decline[i] = intro[i] end
        decline[#decline + 1] = "choose:No thanks I'm too busy."
        decline[#decline + 1] = "player:No thanks. I'm too busy."
        decline[#decline + 1] = "npc:Fair dinkum mate. I'm sure I'll get someone else to help me."
        talk_orlando("orlando-decline", decline)
        t.check("orlando-decline.not_agreed", server("varb3671_vm_nathis_agreed") == 0,
            "varb3671_vm_nathis_agreed = " .. tostring(server("varb3671_vm_nathis_agreed")) .. " (want 0)")

        local accept = {}
        for i = 1, #intro do accept[i] = intro[i] end
        accept[#accept + 1] = "choose:Sure thing."
        accept[#accept + 1] = "player:Sure thing."
        accept[#accept + 1] = "npc:Bonza, mate! I reckon three questions per case should be bang to rights."
        accept[#accept + 1] = "npc:Take a gander at each case and I'll look over your shoulder to give some advice."
        accept[#accept + 1] = "player:Will do."
        talk_orlando("orlando-accept", accept)
        t.expect("orlando-accept.agreed", t.var.await_server("varb3671_vm_nathis_agreed", 1, 5))

        talk_orlando("orlando-none-yet", {
            "npc:G'day there, mate. How're the display cases coming along?",
            "player:I haven't had the chance to take a look yet.",
            "npc:No worries, mate; just have a go when you can.",
            "player:Will do.",
        })

        play_display(DISPLAYS[1], { wrong_first = true })

        talk_orlando("orlando-progress", {
            "npc:G'day there, mate. How're the display cases coming along?",
            "player:I'm getting there. I still have a few left to do.",
            "npc:Bonza, mate. You're doing a great job so far.",
            "player:Righty ho! I'll get back and finish the rest.",
            "npc:Good luck!",
        })
        t.check("orlando-progress.no_reward", server("varb3688_vm_nathis_reward_given") == 0,
            "reward_given = " .. tostring(server("varb3688_vm_nathis_reward_given")) .. " mid-quiz (want 0)")

        for i = 2, #DISPLAYS do
            play_display(DISPLAYS[i])
        end
        t.check("all.no_reward_yet", server("varb3688_vm_nathis_reward_given") == 0,
            "reward_given = " .. tostring(server("varb3688_vm_nathis_reward_given"))
                .. " after the last display (want 0: Orlando pays it in conversation)")

        -- A finished plaque opens nothing: its title as a game message
        -- (RSProx 4347, all fourteen).
        local d = DISPLAYS[3]
        t.ticklog.mark("leg.restudy")
        t.player.click_loc(d.plaque, 1, { at = { d.at[1], d.at[2], 0 } })
        t.ticks(3)
        t.expect("restudy.message", t.msg.expect(d.title))
        local rr, rshown = t.ui.shown(c("vm_nathis_question"))
        t.check("restudy.no_interface", rr ~= "ok" or rshown == false, "quiz interface " .. tostring(rr) .. " " .. tostring(rshown))

        local _, snap = t.skill.snapshot()
        talk_orlando("orlando-reward", {
            "npc:G'day there, mate. How're the display cases coming along?",
            "player:All done. I've corrected the mistakes.",
            "npc:Beaut' mate! That's the best news I've had all day.",
            "player:Glad I could help.",
            "npc:Right, I'll have a chat to me boss over there",
            "npc:You know, with all this new information you've absorbed",
            "player:Oh, thank you very much.",
            "npc:No worries, mate. You take care now.",
            "player:Goodbye.",
        })
        t.expect("orlando-reward.flag", t.var.await_server("varb3688_vm_nathis_reward_given", 1, 5))
        t.expect("orlando-reward.message", t.msg.expect("You have gained 1000xp in Slayer and Hunter."))
        t.expect("orlando-reward.hunter", t.skill.expect_gain("hunter", 1000, snap))
        t.expect("orlando-reward.slayer", t.skill.expect_gain("slayer", 1000, snap))
        t.expect("orlando-reward.kudos", t.var.await_server("varb3637_vm_kudos", 28, 5))

        local _, snap2 = t.skill.snapshot()
        talk_orlando("orlando-after", { "npc:Thanks for all your help, mate. Hope you enjoy the museum!" })
        t.expect("orlando-after.hunter_paid_once", t.skill.expect_gain("hunter", 0, snap2))
        t.expect("orlando-after.slayer_paid_once", t.skill.expect_gain("slayer", 0, snap2))

        -- A natural historian (RSProx 4348): the introduction once, then
        -- straight to his menu; natural history ends the chat.
        t.ticklog.mark("leg.historian")
        t.exec("goto-historian", t.player.goto_tile, 1757, 4946, 0)
        t.exec("historian", t.player.talk_to, "vm_natural_historian_south", 1)
        t.exec("historian-dialog", t.chat.play, {
            "npc:Hello there and welcome to the Natural History exhibit",
            "player:Hello. So what is it you do here?",
            "npc:Well, we look after all of the research in this section.",
            "npc:When I'm not doing that",
            "player:Nice. So then, I've got a few questions for you.",
            "npc:Well, fire away",
            "choose:Tell me about natural history.",
            "player:Tell me about natural history.",
            "npc:Well, the field of natural history covers a wide range of sciences.",
            "npc:So we use biology",
            "npc:Though the field is growing all the time",
            "npc:A person interested in natural history is known as a naturalist.",
        })
        t.expect("historian.spiel", t.var.await_server("varb3687_vm_nathis_got_spiel", 1, 5))
        t.ticks(2)

        -- The terrorbird lecture: the cutscene in a copy of the basement,
        -- then back beside the terrorbird case (4348 [489..523]).
        t.ticklog.mark("leg.lecture")
        t.exec("historian-again", t.player.talk_to, "vm_natural_historian_south", 1)
        t.exec("lecture-dialog", t.chat.play, {
            "npc:Hello again",
            "player:I was hoping you could tell me about something.",
            "choose:Tell me about terrorbirds.",
            "player:Tell me about the terrorbirds.",
            "npc:Ahh terrorbirds, the fastest bird on two legs.",
            "npc:If you just follow me to the display case",
        })
        -- The first lecture page comes six ticks after the click (T+6).
        t.expect("lecture.first-page", t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "lecture: the first page in the copy",
        }, 12))
        local _, inside = t.world.tile()
        t.check("lecture.in-copy", inside ~= nil and inside.x > 6400, "during the lecture at " .. tile_str(inside) .. " (want a copy, x > 6400)")
        t.exec("lecture-pages", t.chat.play, {
            "npc:Terrorbirds live in nomadic groups",
            "npc:They mainly feed on seeds and other plants.",
            "npc:They have no teeth to chew with",
            "npc:They can go without water for a long time",
            "npc:Terrorbirds are known to eat almost anything",
            "npc:Terrorbirds usually weigh a little less than a small unicorn.",
            "npc:There are claws on two of the wings' fingers",
            "npc:This is an adaptation unique to terrorbirds",
            "npc:The gnomes in particular",
            "npc:There are a number of recorded incidents",
            "npc:A terrorbird is so fast",
            "npc:And this concludes my short lecture on terrorbirds.",
        })
        t.ticks(6)
        local _, back = t.world.tile()
        t.check("lecture.back", back ~= nil and back.x == 1757 and back.z == 4939 and back.level == 0,
            "after the lecture at " .. tile_str(back) .. " (want 1757,4939,0: 4348 [520])")
        t.expect("lecture.unlocked", t.var.await_server("varb542_cutscene_status", 0, 5))

        -- The male teacher and his pupil (4348 [37..46]).
        t.ticklog.mark("leg.male-teacher")
        t.exec("goto-male-teacher", t.player.goto_tile, 1759, 4946, 0)
        t.exec("male-teacher", t.player.talk_to, "vm_school_teacher_male_and_boy", 1)
        t.exec("male-teacher-dialog", t.chat.play, {
            "npc:Teacher! Sir! I need the toilet!",
            "npc:I told you to go before we got here.",
            "npc:But sir, I didn't need to go then!",
            "npc:Alright, come on then.",
        })

        -- The female teacher and pupil in the west wing (4340 [795..796]).
        t.ticklog.mark("leg.teacher")
        t.exec("goto-teacher", t.player.goto_tile, 1735, 4960, 0)
        t.exec("teacher", t.player.talk_to, "vm_teacher_girl_multi", 1)
        t.exec("teacher-dialog", t.chat.play, {
            "npc:What do you want to see now, dear?",
            "npc:The penguins, the penguins!",
        })

        -- The other basement flight and back, then up (4340 [1060..1093]).
        t.ticklog.mark("leg.stairs-lower")
        t.exec("goto-lower-stairs", t.player.goto_tile, 1761, 4962, 0)
        t.exec("stairs-lower-down", t.player.click_loc, "vm_basement_wall_stairs_down", 1, { at = { 1758, 4963, 1 } })
        t.ticks(4)
        _, here = t.world.tile()
        t.check("stairs-lower-down.landed", here ~= nil and here.x == 1631 and here.z == 4957 and here.level == 0,
            "after the hall's Walk-down at " .. tile_str(here) .. " (want 1631,4957,0: 4340 [1063])")
        t.exec("stairs-lower-up", t.player.click_loc, "vm_basement_wall_stairs_up", 1, { at = { 1630, 4958, 1 } })
        t.ticks(4)
        _, here = t.world.tile()
        t.check("stairs-lower-up.landed", here ~= nil and here.x == 1761 and here.z == 4964 and here.level == 0,
            "after Walk-up at " .. tile_str(here) .. " (want 1761,4964,0: 4340 [1084])")
        t.ticklog.mark("leg.stairs-up")
        t.exec("stairs-up", t.player.click_loc, "vm_basement_wall_stairs_up", 1, { at = { 1758, 4959, 1 } })
        t.ticks(5)
        _, here = t.world.tile()
        t.check("stairs-up.landed", here ~= nil and here.x == 3258 and here.z == 3452 and here.level == 0,
            "after the hall's Walk-up at " .. tile_str(here) .. " (want 3258,3452,0: 4340 [1093])")
        t.finish(0)
    end,
}
