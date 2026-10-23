-- The Stronghold of Security, all four floors, by one account, as RSProx
-- index 4346 recorded it (docs/rsprox-analysis/stronghold_of_security.md).
-- Content: areas/area_stronghold/scripts/stronghold.rs2 (the hole, gates,
-- rewards, portals, ladders, ropes, the plaque), stronghold_npcs.rs2 (Count
-- Check, Solztun), stronghold_questions.rs2 + .generated.rs2 (the questions).
--
-- Start: Lumbridge, an account of combat level 50 -- 99 Defence and
-- Hitpoints, everything else 1 -- and ten sharks: above the Vault of War's
-- portal level (26), below the Catacomb of Famine's (51). Count Check fails
-- its PIN check and sends it to the Stronghold once; the Vault portal admits
-- it before that floor's claim, and the Famine and Sepulchre portals refuse it.
--
-- The run: Vault portal by level -> its treasure room -> up the ladder to the
-- surface and back in; then every floor's maze on foot, gate by gate (the
-- first gate's greeting and the player's "sucked through" page), to its
-- reward (2,000 / 3,000 / 5,000 coins and the Flap, Slap Head, Idea emotes,
-- the Box of Health's restore; the Cradle's boots and Stamp, twice), down
-- each ladder through its warning, the plaque, Solztun, and up the
-- Sepulchre's rope to the surface. Finally back in: with every floor claimed
-- no gate asks a question and the Vault chest pays nothing twice.
--
-- The routes are reach.py's door-paths over the cache maps
-- (test/quests/orchestrator/.../sample_tools/reach.py, start tile -> the tile
-- beside the floor's way down), each leaf's near/far tile from its wall edge
-- in maps/*.jl2. The answer key is the wiki's table (Stronghold_of_Security,
-- "Doors") with RSProx 4346's text over it
-- (docs/areas/stronghold/sources/rsprox_4346_overrides.tsv), keyed by question
-- text, with the door's whole response to the right answer and to one wrong
-- one: the run answers its first question wrong on purpose, and the door
-- either lets it through or asks again (both recorded).
return {
    id = "strongholdofsecurity",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give shark 10",
    },

    run = function(t)
        t.ticklog.start()
        local PREFIX = "To pass you must answer me this:"
        local KEY = {
            ["What do you do if someone asks you for your password or bank PIN to make you a member for free?"] = {
                right = "Don't tell them anything and click the 'Report Abuse' button.",
                said_right = "Correct! Press the 'Report Abuse' Button and fill in the offending player's name and correct category.",
                wrong = "Give them the information they asked for.",
                said_wrong = "Wrong! Membership requires a subscription paid or a bond to be redeemed - they are trying to trick you. Never give your account details to anyone. Press the 'Report Abuse' button and fill in the offending player's name and the correct category.",
            },
            ["You have been offered a free giveaway or double XP invitation via in- game chat or email. What should you do?"] = {
                right = "Report the incident and do not click any links.",
                said_right = "Correct! This is an attempt to obtain your account details. If it happened in the game, report it via the Report Abuse option.",
                wrong = "Respond quickly so as not to miss the offer.",
                said_wrong = "Wrong! Jagex will NEVER make offers like this - it is an attempt to obtain your account details. If it happnes in the game, report via the Report Abuse option.",
            },
            ["You have been offered a free giveaway or double XP invitation via social media or a livestream. What should you do?"] = {
                right = "Report the incident and do not click any links.",
                said_right = "Correct! This is an attempt to obtain your account details and should be reported to the relevant organisation via their own reporting systems.",
                wrong = "Respond quickly so as not to miss the offer.",
                said_wrong = "Wrong! Jagex will NEVER make offers like this - it is an attempt to obtain your account details. Please report the incident using whatever reporting methods offered by the site you are on.",
            },
            ["Is it safe to get someone to level your account?"] = {
                right = "No, you should never allow anyone to use your account.",
                said_right = "Correct! Allowing someone else to use your account means they may steal it, or may get it banned by cheating on it.",
                wrong = "Yes, so long as you change your password when they have finished.",
                said_wrong = "Wrong! If you allow someone to use your account they may steal it. Alternatively they may use macros/bots to increase your level, and thus get the account banned.",
            },
            ["Hey adventurer! You've been randomly selected for a prize of 1 year of free membership! I'm just going to need some of your account details so I can put it on your account!"] = {
                right = "No way! I'm reporting you to Jagex!",
                said_right = "Correct! Make sure to report anybody asking for your account login details.",
                wrong = "Wowee! Let me just write those down for you.",
                said_wrong = "Incorrect! Do not trust anybody claiming you have won a prize and asking for your account details.",
            },
            ["What is the best security step you can take to keep your registered email secure?"] = {
                right = "Set up two-factor authentiation with my email provider.",
                said_right = "Correct! Enabling two-factor authentication on your email account wil help protect your email from hijackers.",
                wrong = "Have a complicated password set to my email.",
                said_wrong = "Wrong! Complicated passwords are always a good idea but they are not the very best security step you can take to keep your email secure.",
            },
            ["What is the best way to secure your account?"] = {
                right = "Two-factor authentication on your account and your registered email.",
                said_right = "Correct! Using each of these features will maximise your account security.",
                wrong = "A long and complicated password because you have good memory.",
                said_wrong = "Incorrect! While a strong password is recommended, two-factor authentication adds extra levels of security.",
            },
            ["How do I set a bank PIN?"] = {
                right = "Talk to any banker.",
                said_right = "Correct! Simply talking to a banker will give you the option to set a bank PIN. Never use personal details for passwords or bank PINs!",
                wrong = "Use the account management section on the website.",
                said_wrong = "Wrong! Your password can be changed from the account management section, but you must talk to a banker to set a bank PIN. Never use personal details for passwords or bank PINs!",
            },
            ["What should I do if I receive an email asking me to verify my identity or Account details due to suspicious activity?"] = {
                right = "Delete it - it's a fake!",
                said_right = "Correct! Jagex will NEVER email you unless you've used the website to change your account. Account details can only be verified through using account recovery systems on the Old School RuneScape website, NOT through responding to emails.",
                wrong = "Email them back with the information it asks for.",
                said_wrong = "Wrong! Jagex will NEVER email to ask for account details, so don't send any info back.",
            },
            ["Who can I give my password to?"] = {
                right = "Nobody.",
                said_right = "Correct! Your password should be kept secret from everyone. You should *never* give it out under any circumstances.",
                wrong = "My friends.",
                said_wrong = "Wrong! Your password should be kept secret from everyone. You should *never* give it out under any circumstances.",
            },
            ["What do I do if my account is compromised?"] = {
                right = "Secure my device and reset my password.",
                said_right = "Correct! Securing your systems with virus and malware scans is important before resetting your account password to ensure your details are not compromised again. Don't forget to secure your email account too!",
                wrong = "Post on social media about it.",
                said_wrong = "Wrong! Social media will often not solve the problem directly. You'd eventually need to go to the Old School RuneScape website to regain access to your account.",
            },
            ["What do I do if a moderator asks me for my account details?"] = {
                right = "Politely tell them no, then use the 'Report Abuse' button.",
                said_right = "Correct! Report any attempt to gain your account details as it is a very serious breach of Old School RuneScape's rules. Never use personal details for passwords or bank PINs!",
                wrong = "Tell them whatever they want to know.",
                said_wrong = "Wrong! Never give your account details to anyone! This includes things like account creation details, contact details and passwords. Never use personal details for passwords or bank PINs!",
            },
            ["A player trades you some valuable items, provides you a bond, then asks if you want to share your account so he can help you make progress. How do you respond?"] = {
                right = "Decline the offer and report that player.",
                said_right = "Correct! Never share your login details with another player.",
                wrong = "Give the player access since they are a higher level.",
                said_wrong = "Incorrect! Never share your login details with another player.",
            },
            ["Where is it safe to use my Old School RuneScape password?"] = {
                right = "Only on the Old School RuneScape website.",
                said_right = "Correct! Always make sure you are entering your password only on the Old School RuneScape website as other sites may try to steal it.",
                wrong = "On Old School RuneScape and all fansites.",
                said_wrong = "Wrong! Always use a unique password purely for your Old School RuneScape account.",
            },
            ["Whose responsibility is it to keep your account secure?"] = {
                right = "Me.",
                said_right = "Correct! Make sure to use the tools Jagex recommend, such as two-factor authentication options.",
                wrong = "Jagex.",
                said_wrong = "Incorrect! Jagex can offer tools such as two-factor authentication options, but you muse make sure you use them correctly to keep your info safe!",
            },
            ["Psst! Adventurer! I've got a special offer for you, but you're going to have to trust me. If you give me some gold coins, I'll give you back twice whatever you gave me! How does that sound?"] = {
                right = "No way! You'll just take my gold for your own! Reported!",
                said_right = "Correct! If it sounds too good to be true, it probably is! Be wary of these types of scams.",
                wrong = "I'm not sure... but giving a few coins to test it won't hurt.",
                said_wrong = "Incorrect! Do not trust players asking for gold offering to return a higher amount.",
            },
            ["Is it okay to buy an Old School RuneScape account?"] = {
                right = "No, you should never buy an account.",
                said_right = "Correct! Buying accounts is against the rules. Also you could lose the account if the original owner takes it back.",
                wrong = "Yes if it is from someone you know.",
                said_wrong = "Wrong! If you buy an account, the person who originally made it may take it back, and you will lose anything you paid for it.",
            },
            ["My friend asks me for my password so that he can do a difficult quest for me. Do I give it to him?"] = {
                right = "Don't give them my password.",
                said_right = "Correct! You can make it alone and the success will taste even better. Don't forget you can ask people for advice too!",
                wrong = "Yes. He is my best friend and I already spent ages trying this quest.",
                said_wrong = "Wrong! Don't give your password to anyone otherwise you can lose everything you have worked so hard for.",
            },
            ["A player tells you to search for a video online, click the link in the description and comment on the forum post to win a cash prize. What do you do?"] = {
                right = "Report the player for phishing.",
                said_right = "Correct! Always be wary of these links and double check you are on the official Old School RuneScape website before entering your login details!",
                wrong = "Do what they ask, using the provided link in the video description.",
                said_wrong = "Incorrect! Don't trust these types of link even if they look similar to the Old School RuneScape website.",
            },
            ["Adventurer, I'll trade items with you for an amazing price, but you've got to come immediately to a particular place on a different game world. Hurry up! Come now before you lose out! What do you say?"] = {
                right = "Nope, you're tricking me into going somewhere dangerous.",
                said_right = "Correct! If it sounds too good to be true, it probably is! Be wary of these types of scams.",
                wrong = "Okay, I'll take my valuables there now so we can trade.",
                said_wrong = "Incorrect! Do not trust players trying to rush you into going somewhere unfamiliar. It's often an attempt to lure you into taking items somewhere dangerous, where you'll lose them.",
            },
            ["Which of these is an important characteristic of a secure password?"] = {
                right = "It's never reused on other websites or accounts.",
                said_right = "Correct! Make sure to use a unique password to keep your account secure.",
                wrong = "It incorporates your real name or birthday.",
                said_wrong = "Incorrect! Using personal details as your password makes it easier to guess!",
            },
            ["You're watching a stream by someone claiming to be Jagex offering double XP. What do you do?"] = {
                right = "Report the stream as a scam. Real Jagex streams have a 'verified' mark.",
                said_right = "Correct! This is a common phishing method and puts your account at risk!",
                wrong = "Click the link! I love double XP!",
                said_wrong = "Incorrect! This is a common phishing method and puts your account at risk!",
            },
            ["Will Jagex prevent me from saying my PIN in game?"] = {
                right = "No.",
                said_right = "Correct! Jagex will not block your PIN so don't type it! Anyone asking you to say your PIN is trying to trick you.",
                wrong = "Yes.",
                said_wrong = "Wrong! Jagex does NOT block your PIN so don't type it! Anyone asking you to say your PIN is trying to trick you.",
            },
            ["A website claims that they can make me a player moderator. What should I do?"] = {
                right = "Nothing, it's a fake.",
                said_right = "Correct! Remember that moderators are hand picked by Jagex and contact is made through the game inbox only.",
                wrong = "Give them my account info and password.",
                said_wrong = "Wrong! This will almost certainly lead to your account being hijacked. No wabsite can make you a moderator as tehy are hand picked by Jagex.",
            },
            ["What do I do if I think I have a keylogger or virus?"] = {
                right = "Virus scan my device then change my password.",
                said_right = "Correct! Removing the keylogger must be the priority, otherwise anything you type can be given away. Remember to change your password and bank PIN afterwards.",
                wrong = "Change my password then virus scan my device.",
                said_wrong = "Wrong! If you change your password while you still have the keylogger, it will still be insecure. Remove the keylogger first. Never use personal details for passwords or bank PINs!",
            },
            ["What should you do if another player messages you recommending a website to purchase items and/or gold?"] = {
                right = "Do not visit the website and report the player who messaged you.",
                said_right = "Correct! Buying and selling items and gold is against the rules and results in a permanent ban!",
                wrong = "Check out the website, it never hurts to look around!",
                said_wrong = "Incorrect! Websites offering these services should not be trusted!",
            },
            ["Can I leave my account logged in while I'm out of the room?"] = {
                right = "No.",
                said_right = "Correct! This is the safest, both in terms of security and keeping your items! Leaving your character loged in can also allow someone to steal your items or your entire account!",
                wrong = "Yes, for up to an hour.",
                said_wrong = "Wrong! you should log out in case you are attacked. Leaving  your character logged in can also allow someone to steal your items or your entire account!",
            },
            ["What do you do if someone asks you for your password or bank PIN to make you a player moderator?"] = {
                right = "Don't give them the information and send an 'Abuse report'",
                said_right = "Correct! Press the 'Report Abuse' button and fill in the offenfing player's name and the correct category.",
                wrong = "Don't tell them anything and ignore them.",
                said_wrong = "Quite good. But we should try to stop scammers. So please report them using the 'Report Abuse' button.",
            },
            ["What is an example of a good bank PIN?"] = {
                right = "The birthday of a famous person or event.",
                said_right = "Well done! Unless you tell someone, they are unlikely to guess who or what you have chosen, and you can always look it up. Never use personal details for passwords or bank PINs!",
                wrong = "Your real life bank PIN.",
                said_wrong = "This is a bad idea as if someone happens to find out your bank PIN on Old School RuneScape, they then have your real life bank PIN! Never use personal details for passwords or bank PINs!",
            },
            ["What should you do if your real-life friend asks for your password so he can check your stats?"] = {
                right = "Don't give out your password to anyone. Not even close friends.",
                said_right = "Correct! Doing so could result in losing your items and gold and puts your account at risk.",
                wrong = "Give them your password since they're a friend in real life.",
                said_wrong = "Incorrect! Don't trust anybody with your account login details!",
            },
            ["A player starts asking you about very specific details linked to your account, such as when you created your account, your birthday date, internet provider etc. How should you react?"] = {
                right = "Don't share your information and report the player.",
                said_right = "Correct! These details could've been used within the account recovery system and possibly compromise your account.",
                wrong = "Be friendly and answer the questions.",
                said_wrong = "Incorrect! These details can be used within the account recovery system and possibly compromise your account.",
            },
            ["How do I remove a hijacker from my account?"] = {
                right = "Use the Account Recovery system.",
                said_right = "Correct! Visiting oldschool.runescape.com and using the account recovery system will allow you to kick hijackers off of your account by locking it as stolen.",
                wrong = "Ask on social media.",
                said_wrong = "Wrong! Visiting oldschool.runescape.com and using the account recovery system will allow you to kick hijackers off your account by locking it as stolen.",
            },
            ["You are part way through the Stronghold of Security when you have to answer another question. After you answer the question, you should..."] = {
                right = "Read the text and follow the advice given.",
                said_right = "Correct! As tempting as it is to click through all the text and get your reward as soon as possible, it's important to know these security tips.",
                wrong = "Click through the text as fast as possible to get your reward sooner.",
                said_wrong = "Incorrect! You should read the text and follow the advice to keep your account as secure as possible.",
            },
        }
        -- Each leaf: symbol, leaf tile, the tile before it, the tile past it.
        local ROUTE = {
            war = {
            { "sos_war_door_face_mirr", 1859, 5238, 1859, 5239, 1859, 5238 },
            { "sos_war_door_face_mirr", 1859, 5235, 1859, 5236, 1859, 5235 },
            { "sos_war_door_face_mirr", 1865, 5227, 1864, 5227, 1865, 5227 },
            { "sos_war_door_face_mirr", 1868, 5227, 1867, 5227, 1868, 5227 },
            { "sos_war_door_face_mirr", 1876, 5240, 1875, 5240, 1876, 5240 },
            { "sos_war_door_face_mirr", 1879, 5240, 1878, 5240, 1879, 5240 },
            { "sos_war_door_face", 1884, 5243, 1883, 5243, 1884, 5243 },
            { "sos_war_door_face", 1887, 5243, 1886, 5243, 1887, 5243 },
            { "sos_war_door_face_mirr", 1904, 5243, 1903, 5243, 1904, 5243 },
            { "sos_war_door_face_mirr", 1908, 5243, 1907, 5243, 1908, 5243 },
            { "sos_war_door_face", 1904, 5233, 1904, 5234, 1904, 5233 },
            { "sos_war_door_face", 1904, 5230, 1904, 5231, 1904, 5230 },
            },
            fam = {
            { "sos_fam_door_face_mirr", 2039, 5244, 2040, 5244, 2039, 5244 },
            { "sos_fam_door_face", 2037, 5244, 2037, 5244, 2036, 5244 },
            { "sos_fam_door_face_mirr", 2026, 5239, 2026, 5238, 2026, 5239 },
            { "sos_fam_door_face", 2026, 5241, 2026, 5241, 2026, 5242 },
            { "sos_fam_door_face", 2019, 5242, 2019, 5243, 2019, 5242 },
            { "sos_fam_door_face_mirr", 2019, 5240, 2019, 5240, 2019, 5239 },
            { "sos_fam_door_face_mirr", 2013, 5240, 2013, 5239, 2013, 5240 },
            { "sos_fam_door_face", 2013, 5242, 2013, 5242, 2013, 5243 },
            { "sos_fam_door_face", 2005, 5237, 2005, 5238, 2005, 5237 },
            { "sos_fam_door_face_mirr", 2005, 5235, 2005, 5235, 2005, 5234 },
            { "sos_fam_door_face_mirr", 2016, 5228, 2015, 5228, 2016, 5228 },
            { "sos_fam_door_face", 2018, 5228, 2018, 5228, 2019, 5228 },
            },
            pest = {
            { "sos_pest_door_face", 2132, 5257, 2132, 5256, 2132, 5257 },
            { "sos_pest_door_face", 2132, 5260, 2132, 5259, 2132, 5260 },
            { "sos_pest_door_face_mirr", 2137, 5263, 2137, 5263, 2138, 5263 },
            { "sos_pest_door_face_mirr", 2140, 5263, 2140, 5263, 2141, 5263 },
            { "sos_pest_door_face_mirr", 2153, 5264, 2153, 5264, 2154, 5264 },
            { "sos_pest_door_face_mirr", 2156, 5264, 2156, 5264, 2157, 5264 },
            { "sos_pest_door_face", 2163, 5275, 2163, 5274, 2163, 5275 },
            { "sos_pest_door_face", 2163, 5278, 2163, 5277, 2163, 5278 },
            { "sos_pest_door_face", 2155, 5286, 2155, 5285, 2155, 5286 },
            { "sos_pest_door_face", 2155, 5289, 2155, 5288, 2155, 5289 },
            { "sos_pest_door_face", 2152, 5291, 2153, 5291, 2152, 5291 },
            { "sos_pest_door_face_mirr", 2149, 5291, 2149, 5291, 2148, 5291 },
            },
            death = {
            { "sos_death_door_face_mirr", 2355, 5218, 2355, 5218, 2355, 5219 },
            { "sos_death_door_face_mirr", 2355, 5221, 2355, 5221, 2355, 5222 },
            { "sos_death_door_face_mirr", 2336, 5237, 2336, 5237, 2335, 5237 },
            { "sos_death_door_face_mirr", 2333, 5237, 2333, 5237, 2332, 5237 },
            { "sos_death_door_face", 2323, 5243, 2323, 5243, 2323, 5242 },
            { "sos_death_door_face", 2323, 5240, 2323, 5240, 2323, 5239 },
            { "sos_death_door_face", 2333, 5227, 2332, 5227, 2333, 5227 },
            { "sos_death_door_face", 2336, 5227, 2335, 5227, 2336, 5227 },
            { "sos_death_door_face", 2341, 5224, 2341, 5225, 2341, 5224 },
            { "sos_death_door_face", 2341, 5221, 2341, 5222, 2341, 5221 },
            },
        }

        local function norm(s)
            s = tostring(s or ""):gsub("<[^>]*>", " ")
            return string.lower((s:gsub("%s+", " "):gsub("^ ", ""):gsub(" $", "")))
        end
        local ANSWER = {}
        for q, a in pairs(KEY) do
            ANSWER[norm(q)] = a
        end
        local function here()
            local r, tl = t.world.tile()
            return r == "ok" and tl or nil
        end
        local function tile_str(tl)
            return tl and (tl.x .. "," .. tl.z .. "," .. tl.level) or "nil"
        end
        local function at(x, z)
            local tl = here()
            return tl ~= nil and tl.x == x and tl.z == z
        end
        local function hp()
            local _, s = t.skill.read("hitpoints")
            return s
        end
        local function eat_if_low(name)
            local s = hp()
            if s and s.level < 60 and select(2, t.inv.count("shark")) > 0 then
                t.exec(name .. ".eat", t.player.eat, "shark")
            end
        end

        local asked = 0
        local wrong_done = false
        local greeted = false
        local greeting_text = ""
        local function is_question(text)
            return norm(text):sub(1, #norm(PREFIX)) == norm(PREFIX)
        end
        -- Every npc page up now, continued, joined; stops in front of a page
        -- that opens a NEW question when `stop_at_question` (a door re-asking
        -- after a wrong answer puts it on the click of its response's last page).
        local function read_pages(stop_at_question)
            local pages = {}
            while t.chat.kind() == "npc" do
                local tr, text = t.chat.text()
                if tr ~= "ok" then
                    break
                end
                if stop_at_question and #pages > 0 and is_question(text) then
                    break
                end
                pages[#pages + 1] = tostring(text)
                t.chat.continue_()
                t.ticks(1)
            end
            return table.concat(pages, " ")
        end
        -- The door's page(s) after a press: nothing (a first gate, a skip roll,
        -- a claimed floor), the first gate's greeting, or questions: the
        -- player's pick and the door's answer to it, and after a wrong pick
        -- either the way through or another question (RSProx 4346 [499..501],
        -- [614..626]). Returns "none", "greeting", "right" or "wrong".
        local function answer_door(name)
            t.await({
                level = function()
                    return t.chat.kind() ~= "none" or false
                end,
                note = "a door page",
            }, 4)
            if t.chat.kind() == "none" then
                return "none"
            end
            local verdict = "none"
            local question = read_pages(false)
            if norm(question):find("^greetings adventurer") then
                greeted = true
                greeting_text = norm(question)
                t.check(name .. ".greeting", norm(question):find("please pass through and begin your adventure", 1, true) ~= nil,
                    "the first gate's two pages: '" .. question .. "'")
                return "greeting"
            end
            while true do
                local stem = norm(question):gsub("^" .. norm(PREFIX):gsub("%p", "%%%0") .. " ?", "")
                local key = ANSWER[stem]
                asked = asked + 1
                t.check(name .. ".question" .. asked, key ~= nil and t.chat.kind() == "options",
                    "'" .. question .. "' -- " .. (key and "in the key" or "NOT in the key")
                    .. ", page " .. t.chat.kind())
                if key == nil then
                    return "wrong"
                end
                local pick, want = key.right, key.said_right
                verdict = "right"
                if not wrong_done then
                    wrong_done = true
                    pick, want, verdict = key.wrong, key.said_wrong, "wrong"
                end
                t.exec(name .. ".answer" .. asked, t.chat.choose, pick)
                t.ticks(1)
                local reply = read_pages(true)
                t.check(name .. ".response" .. asked, norm(reply) == norm(want),
                    "'" .. reply .. "'" .. (norm(reply) == norm(want) and "" or (" -- key: '" .. want .. "'")))
                if verdict == "right" then
                    return verdict
                end
                -- Wrong: another question now, or through.
                local _, next_text = t.chat.text()
                if t.chat.kind() ~= "npc" or not is_question(next_text) then
                    return verdict
                end
                question = read_pages(false)
            end
        end

        -- One gate leaf crossed: from its near tile, pressed, questioned or
        -- not, and graded on the player standing on its far tile.
        local function gate(name, g)
            local sym, lx, lz, nx, nz, fx, fz = g[1], g[2], g[3], g[4], g[5], g[6], g[7]
            eat_if_low(name)
            if not at(nx, nz) then
                t.exec("walk-" .. name, t.player.walk_to, nx, nz, 80)
            end
            local pr, pd = t.player.click_loc(sym, 1, { at = { lx, lz } })
            t.note("press " .. sym .. "@" .. lx .. "," .. lz .. ": " .. tostring(pr) .. " " .. tostring(pd))
            local verdict = answer_door(name)
            t.await({ level = function() return at(fx, fz) end, note = "past " .. sym }, 12)
            t.check(name, at(fx, fz), sym .. " at " .. lx .. "," .. lz .. " (" .. verdict .. "): player at "
                .. tile_str(here()) .. ", want " .. fx .. "," .. fz)
            if verdict == "greeting" then
                -- 4346 [238]: on the far side, a tick after the move. The page
                -- reader above may already have continued it with the door's.
                if t.chat.kind() ~= "none" then
                    t.exec(name .. ".suckedThrough", t.chat.play, {
                        "player:Oh my! I just got sucked through that door", "end" })
                else
                    t.check(name .. ".suckedThrough", greeting_text:find("oh my! i just got sucked through that door", 1, true) ~= nil,
                        "the player's page followed the greeting")
                end
            end
        end
        -- A pair's FIRST leaf never asks (sos_should_ask); the second may.
        local function maze(floor)
            for i, g in ipairs(ROUTE[floor]) do
                local asked_before = asked
                gate(floor .. ".gate" .. i, g)
                if i % 2 == 1 then
                    t.check(floor .. ".gate" .. i .. ".silent", asked == asked_before,
                        "first leaf of pair " .. ((i + 1) // 2) .. ": " .. (asked - asked_before) .. " question(s)")
                end
            end
        end
        -- Ladders up, ropes and the hole: one op, one move (the dungeon is at
        -- z ~5200 on level 0, so every one of them is a same-level maplink).
        local function climb(name, loc, lx, lz, dest)
            t.player.climb({ loc = loc, at = { lx, lz, 0 }, dest = dest, slack = 2,
                same_level = "maplink " .. loc })
            t.check(name, true, "climbed " .. loc .. " to " .. tile_str(here()))
        end
        -- A way down: the "Are you sure you want to climb down?" modal, its
        -- count one higher, "Yes", then the move (4346 [418..427]).
        local downs = 0
        local function climb_down(name, loc, lx, lz, dest)
            t.exec(name, t.player.click_loc, loc, 1, { at = { lx, lz } })
            t.check(name .. ".warning", t.ui.await_open("cws_warning_4", 10) == "ok", "cws_warning_4 open")
            downs = downs + 1
            t.exec(name .. ".count", t.var.await, "varb3854_cws_warning_4", downs, 5)
            local wr, yes = t.ui.widget("cws_warning_4:warn1")
            t.check(name .. ".yesWidget", wr == "ok", "cws_warning_4:warn1 -> " .. tostring(wr))
            t.exec(name .. ".yes", t.ui.invoke, yes, 1)
            t.await({ level = function() return at(dest[1], dest[2]) end, note = "the floor below" }, 10)
            t.check(name .. ".landed", at(dest[1], dest[2]), "at " .. tile_str(here()))
        end
        local function coins()
            local _, n = t.inv.count("coins")
            return n or 0
        end
        local function claim(name, loc, lx, lz, gain, emote, voice, emote_name)
            local before = coins()
            t.exec(name, t.player.click_loc, loc, 1, { at = { lx, lz } })
            t.exec(name .. ".boxes", t.chat.play, {
                "mesbox:" .. voice,
                "mesbox:You have also unlocked the " .. emote_name .. " emote!",
                "end",
            })
            t.exec(name .. ".coins", t.inv.await, "coins", before + gain, 10)
            t.exec(name .. ".emote", t.var.await, emote, 1, 5)
        end
        local function cradle(name, cell, boots, first)
            t.exec(name, t.player.click_loc, "sos_death_pram", 1, { at = { 2344, 5214 } })
            t.exec(name .. ".boxes", t.chat.play, {
                "mesbox:As your hand touches the cradle, you hear a voice in your head",
                "mesbox:... welcome adventurer... you have learnt the value of securing your",
                "objbox:You may claim these boots. They protect your feet",
                "objbox:You can return here for more boots at any time",
            })
            t.check(name .. ".menu", t.ui.await_open("skillmulti", 10) == "ok", "skillmulti open")
            local mr, pick = t.ui.widget("skillmulti:" .. cell)
            t.check(name .. ".cell", mr == "ok", "skillmulti:" .. cell .. " -> " .. tostring(mr))
            t.exec(name .. ".pick", t.ui.invoke, pick, 1)
            t.ticks(1)
            t.exec(name .. ".prize", t.chat.play, {
                "objbox:You claim your prize: " .. boots .. (first and " You have unlocked the 'Stamp Foot' emote." or ""),
                "end",
            })
        end

        ---------------------------------------------------------------
        -- Count Check: the PIN check, then the one teleport
        ---------------------------------------------------------------
        -- Auto Retaliate off, by the combat tab's own button: the floors are
        -- aggressive and a monster that followed the player onto a gate's
        -- near tile swings through the wall once they are past it; retaliating
        -- chases it ("I can't reach that!"), and the gate press would read
        -- that line as its own refusal and press again from the far side.
        t.check("combatTab", t.ui.tab("combat"), "opened the combat tab")
        local rr, retaliate = t.ui.widget("combat_interface:retaliate")
        t.check("autoRetaliateOff", t.ui.invoke(retaliate, 1), "combat_interface:retaliate " .. tostring(rr))
        t.exec("autoRetaliateOff.var", t.var.await, "varp172_option_nodef", 1, 5)
        t.exec("goto-count", t.player.goto_tile, 3238, 3201, 0)
        t.exec("count.talk", t.player.talk_to, "count_check", 1)
        t.exec("count.check", t.chat.play, {
            "npc:Ahahahaha! I am Count Check, the renowned security",
            "choose:Check my account, Count Check!",
            "player:Check my account, Count Check!",
            "npc:You do not have a Bank PIN, so you fail my checks!",
            "end",
        })
        t.exec("count.pinLine", t.msg.expect, "ask to check your PIN settings")
        t.exec("count.talk2", t.player.talk_to, "count_check", 1)
        t.exec("count.offer", t.chat.play, {
            "npc:Ahahahaha! I am Count Check",
            "choose:Where can I learn more about security?",
            "player:Where can I learn more about security?",
            "npc:I see you have not visited it. Would you like to? I can send you straight there - but only once.",
            "choose:Yes",
        })
        t.await({ level = function() return at(3081, 3421) end, note = "Barbarian Village" }, 10)
        t.check("count.teleported", at(3081, 3421), "at " .. tile_str(here()))
        t.exec("count.varbit", t.var.await, "varb5371_sos_teleported_by_count", 1, 5)

        ---------------------------------------------------------------
        -- The way in, and the Vault portal by combat level
        ---------------------------------------------------------------
        climb("enter", "sos_dung_ent_open", 3081, 3420, { 1859, 5243, 0 })
        t.exec("enter.mesbox", t.chat.play, { "mesbox:You squeeze through the hole and find a ladder", "end" })
        t.exec("warPortal.byLevel", t.player.click_loc, "sos_war_portal", 1, { at = { 1863, 5238 } })
        t.await({ level = function() local tl = here(); return tl and tl.x >= 1905 end, note = "the treasure room" }, 15)
        t.check("warPortal.landed", at(1914, 5222), "combat 50 >= 26 before the claim: at " .. tile_str(here()))
        t.exec("warPortal.msg", t.msg.expect, "whisked through to the treasure room")
        climb("warTreasure.up", "sos_war_ladd_up", 1913, 5226, { 3081, 3421, 0 })
        t.exec("warTreasure.up.msg", t.msg.expect, "twist and wind in all directions")
        climb("enter2", "sos_dung_ent_open", 3081, 3420, { 1859, 5243, 0 })
        t.exec("enter2.mesbox", t.chat.play, { "mesbox:You squeeze through the hole", "end" })

        ---------------------------------------------------------------
        -- Vault of War
        ---------------------------------------------------------------
        maze("war")
        t.check("war.greeted", greeted, "the first gate greeted the first visit")
        claim("giftOfPeace", "sos_war_chest", 1907, 5222, 2000, "varb2309_sos_emote_flap",
            "The box hinges creak and appear to be forming audible words....", "Flap")
        local again = coins()
        t.exec("giftOfPeace.again", t.player.click_loc, "sos_war_chest", 1, { at = { 1907, 5222 } })
        t.exec("giftOfPeace.again.msg", t.msg.expect, "already claimed")
        t.check("giftOfPeace.again.coins", coins() == again, "coins " .. again .. " -> " .. coins())
        climb_down("warDown", "sos_war_ladd_down", 1902, 5222, { 2042, 5245 })

        ---------------------------------------------------------------
        -- Catacomb of Famine
        ---------------------------------------------------------------
        t.exec("famPortal.refused", t.player.click_loc, "sos_fam_portal", 1, { at = { 2039, 5240 } })
        t.ticks(3) -- the line comes at L+2, after the walk the press returns on
        t.exec("famPortal.refused.msg", t.msg.expect, "not of sufficient experience to take the shortcut")
        t.check("famPortal.stayed", here() ~= nil and here().x > 2030, "still at the start: " .. tile_str(here()))
        maze("fam")
        claim("grainOfPlenty", "sos_fam_sack", 2021, 5215, 3000, "varb2310_sos_emote_doh",
            "The grain shifts in the sack, sighing audible words....", "Slap Head")
        climb_down("famDown", "sos_fam_ladd_down", 2026, 5218, { 2123, 5252 })

        ---------------------------------------------------------------
        -- Pit of Pestilence
        ---------------------------------------------------------------
        maze("pest")
        claim("boxOfHealth", "sos_pest_chest", 2144, 5280, 5000, "varb2311_sos_emote_idea",
            "The box hinges creak and appear to be forming audible words....", "Idea")
        t.exec("boxOfHealth.msg", t.msg.expect, "You feel refreshed and renewed.")
        local low = {}
        for _, stat in ipairs({ "hitpoints", "prayer", "attack", "defence", "strength" }) do
            local _, r = t.skill.read(stat)
            if not r or r.level < r.base_level then
                low[#low + 1] = stat .. " " .. tostring(r and r.level) .. "/" .. tostring(r and r.base_level)
            end
        end
        t.check("boxOfHealth.restore", #low == 0, #low == 0 and "every stat at base or above" or table.concat(low, "; "))
        climb_down("pestDown", "sos_pest_ladd_down", 2148, 5284, { 2358, 5215 })

        ---------------------------------------------------------------
        -- Sepulchre of Death
        ---------------------------------------------------------------
        t.exec("deathPortal.refused", t.player.click_loc, "sos_death_portal", 1, { at = { 2365, 5212 } })
        t.ticks(3) -- the line comes at L+2, after the walk the press returns on
        t.exec("deathPortal.refused.msg", t.msg.expect, "not of sufficient experience to take the shortcut")
        maze("death")
        cradle("cradleOfLife", "b", "Fighting boots", true)
        t.exec("cradleOfLife.boots", t.inv.await, "sos_boots2", 1, 5)
        t.exec("cradleOfLife.emote", t.var.await, "varb2312_sos_emote_stamp", 1, 5)
        cradle("cradleOfLife.again", "c", "Fancier boots", false)
        t.exec("cradleOfLife.again.boots", t.inv.await, "sos_boots3", 1, 5)
        t.check("questions.asked", asked > 0, asked .. " question(s) asked on the way down")

        -- The plaque, and Solztun (4346 [1166..1216]).
        t.exec("thanks", t.player.click_loc, "sos_thankplayers", 1, { at = { 2349, 5211 } })
        t.check("thanks.open", t.ui.await_open("messagescroll2", 10) == "ok", "messagescroll2 open")
        local cr, close = t.ui.widget("messagescroll2:messagescroll2_close")
        t.check("thanks.closeWidget", cr == "ok", "messagescroll2_close -> " .. tostring(cr))
        t.exec("thanks.close", t.ui.invoke, close, 1)
        t.exec("solztun.talk", t.player.talk_to, "sos_barb_spirit", 1)
        t.exec("solztun.chat", t.chat.play, {
            "npc:Can you hear me?",
            "player:*",
            "npc:I'm no ghost! I'm a spirit",
            "player:Oh, that's... interesting. Also rather disturbing...",
            "npc:Try not to think about it.",
            "choose:Who are you?",
            "player:Who are you?",
            "npc:I am Solztun, the greatest barbarian explorer!",
            "player:It's very nice to meet you, Solztun!",
            "npc:I know.",
            "player:... so, you were exploring this place?",
            "npc:Yes, my sister told me not to go",
            "player:Oh! Your sister is Litara",
            "npc:I died... Of course, I tried escaping",
            "player:Nobody came to help?!",
            "npc:Oh no, they had no idea I was hurt",
            "player:Were you not a fan of the barbarian life style?",
            "npc:It was all death and pillaging",
            "player:Ah, I too enjoy those.",
            "choose:Bye!",
            "player:Bye!",
            "npc:Stay safe, adventurer.",
            "end",
        })
        t.exec("solztun.brother", t.var.await, "varb5639_sos_brother_found", 1, 5)

        climb("deathUp", "sos_death_rope_up", 2350, 5215, { 3081, 3421, 0 })
        t.exec("deathUp.msg", t.msg.expect, "You shin up the rope, squeeze through a passage then climb a ladder.")

        ---------------------------------------------------------------
        -- Completed: no gate asks, nothing pays twice
        ---------------------------------------------------------------
        climb("enter3", "sos_dung_ent_open", 3081, 3420, { 1859, 5243, 0 })
        t.exec("enter3.mesbox", t.chat.play, { "mesbox:You squeeze through the hole", "end" })
        local asked_before = asked
        -- Three pairs: an unclaimed Stronghold asks on each second leaf four
        -- times in five, so three silent pairs are not a lucky roll.
        for i = 1, 6 do
            gate("complete.gate" .. i, ROUTE.war[i])
        end
        t.check("complete.noQuestions", asked == asked_before,
            "questions asked crossing three pairs after completion: " .. (asked - asked_before))
        t.finish(0)
    end,
}
