-- Contact! driven from the guide ladder (contact.notes.md, b53 parity scratch).
-- Prerequisites by ::complete; light source + tinderbox + gear are brought-along kit.
-- Maze note: the Sophanem maze is walked on foot with the trap presses (no goto past it).
-- b56 sampler fixes: every arrival row compares the tile it reads against where the step must
-- end; the Scarab approach walks to a tile it can reach (6436,96 was the scarab mage's spawn
-- tile, contact_scarab.rs2:24, and the server routed every walk to it to 6439,92); the fight is
-- prayed (Protect from Melee, as the guide says), the kit fits, and killGiantScarab-margin grades it.
return {
    id = "contact",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_gertrudescat",
        "::complete quest_princealirescue",
        "::complete quest_icthlarinslittlehelper",
        "::give tinderbox 1",
        "::give bullseye_lantern 1",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give zamorak_spear 1",
        "::setlevel hitpoints 99",
        "::setlevel agility 99",
        "::setlevel thieving 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        -- Quest Helper Contact.java killGiantScarab: "Pray melee if you are meleeing it";
        -- prayer potions are a listed requirement (:138). Protect from Melee needs 43; 70
        -- points last ~350 ticks of it, past the ~220-tick fight, so no potion is carried.
        "::setlevel prayer 70",
        -- Worn BEFORE the food is given: with the gear still in the pack only 22 of the
        -- old 25 sharks fitted (b56 sampler, play shot 079).
        "::wield rune_full_helm",
        "::wield rune_chainbody",
        "::wield rune_platelegs",
        "::wield zamorak_spear",
        -- Antipoison: Contact.java getItemRecommended (:139, :288); the Scarab poisons
        -- (contact_scarab.rs2:79-85, severity 41) and the b55 run ticked 50 -> 13 after it.
        "::give 4doseantipoison 1",
        -- 24 sharks: tinderbox + lantern + antipoison + 24 = 27, and Kaleef's parchment
        -- takes the 28th slot.
        "::give shark 24",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb3274_contact",
            constants = { not_started = 0, told_jex = 30, investigating = 40, read = 50, met_maisa = 60, told_osman = 70, osman_outside = 80, in_chasm = 90, scarab_killed = 110, ready = 120, complete = 130 },
            display = "Contact!",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- An arrival row reads the tile and grades it against where the step must end; the
        -- tolerance is far smaller than the distance from where the step started.
        local function arrived(name, want_x, want_z, want_level, tol, from_text)
            local tr, th = t.world.tile()
            local ok = tr == "ok" and th ~= nil and th.level == want_level
                and math.abs(th.x - want_x) <= tol and math.abs(th.z - want_z) <= tol
            t.check(name, ok, ((tr == "ok" and th) and (th.x .. "," .. th.z .. "," .. th.level) or tostring(th))
                .. " (want within " .. tol .. " of " .. want_x .. "," .. want_z .. "," .. want_level .. "; " .. from_text .. ")")
            return ok, th
        end
        local function hp_now()
            local hr, hs = t.skill.read("hitpoints")
            return hr == "ok" and hs and (hs.current or hs.level) or nil
        end

        t.exec("goto-highpriest", t.player.goto_tile, 3281, 2774, 0)
        t.exec("talkToHighPriest", t.player.talk_to, "ics_little_hipriest_vis", 1)
        t.exec("talkToHighPriest-dialog", t.chat.play, {
            "npc:Adventurer, welcome",
            "choose:/Sounds like a quest/",
            "player:Sounds like a quest for me",
            "npc:I tried to negotiate",
            "mesbox:disguises",
            "player:Is there any way into Menaphos from below",
            "npc:Sect of Scabaras",
        })
        t.ticks(2)
        t.expect("quest.stage.told_jex", t.quest.expect_stage("told_jex"))

        t.exec("goto-jex", t.player.goto_tile, 3312, 2797, 0)
        t.exec("talkToJex", t.player.talk_to, "contact_jex", 1)
        t.exec("talkToJex-dialog", t.chat.play, {
            "player:The High Priest sent me",
            "npc:this temple once belonged",
            "npc:bring a light source",
            "choose:Better get down there.",
            "player:Better get down there",
            "mesbox:Jex waves you",
        })
        t.ticks(2)
        t.expect("quest.stage.investigating", t.quest.expect_stage("investigating"))

        t.exec("goDownToBank", t.player.click_loc, "contact_temple_trapdoor_open", 1)
        t.exec("goDownToBank.continue", t.chat.continue_, true)
        t.ticks(6)
        arrived("bank.arrived", 2766, 5132, 0, 3, "the trapdoor is at 3315,2797 in Sophanem")
        t.exec("goDownToDungeon", t.player.click_loc, "contact_ladder_barricaded", 1)
        t.ticks(8)
        arrived("dungeon.arrived", 2166, 4409, 2, 2, "the ladder is in the bank vault at 2766,5130")
        local mazeDown = {
                {"w",2166,4409},
                {"w",2166,4401},
                {"w",2161,4401},
                {"w",2161,4403},
                {"w",2160,4403},
                {"t",2159,4403},
                {"w",2159,4403},
                {"w",2157,4403},
                {"w",2157,4404},
                {"w",2156,4404},
                {"w",2156,4408},
                {"w",2154,4408},
                {"w",2154,4410},
                {"w",2143,4410},
                {"w",2143,4409},
                {"w",2140,4409},
                {"t",2139,4409},
                {"w",2139,4409},
                {"w",2134,4409},
                {"l",2134,4410,"contact_ug_ladder_top_twofloors",2},
                {"w",2136,4410},
                {"w",2136,4404},
                {"w",2137,4404},
                {"w",2137,4403},
                {"w",2138,4403},
                {"w",2138,4401},
                {"w",2140,4401},
                {"w",2140,4400},
                {"w",2148,4400},
                {"w",2148,4394},
                {"w",2149,4394},
                {"w",2149,4369},
                {"w",2136,4369},
                {"w",2136,4368},
                {"w",2135,4368},
                {"w",2135,4367},
                {"w",2133,4367},
                {"w",2133,4366},
                {"w",2131,4366},
                {"w",2131,4365},
                {"w",2130,4365},
                {"w",2130,4364},
                {"w",2122,4364},
                {"w",2122,4363},
                {"w",2121,4363},
                {"w",2121,4361},
                {"w",2120,4361},
                {"w",2120,4360},
                {"w",2116,4360},
                {"w",2116,4359},
                {"w",2115,4359},
                {"w",2115,4358},
                {"l",2115,4357,"contact_ug_ladder_twofloors",0},
                {"w",2115,4359},
                {"w",2120,4359},
                {"w",2120,4357},
                {"w",2136,4357},
                {"w",2136,4361},
                {"w",2131,4361},
                {"w",2131,4363},
                {"w",2127,4363},
                {"w",2127,4362},
                {"w",2123,4362},
                {"w",2123,4364},
                {"w",2116,4364},
            }
    local mazeUp = {
                {"w",2116,4364},
                {"w",2124,4364},
                {"w",2124,4362},
                {"w",2127,4362},
                {"w",2127,4363},
                {"w",2132,4363},
                {"w",2132,4362},
                {"w",2136,4362},
                {"w",2136,4357},
                {"w",2119,4357},
                {"w",2119,4359},
                {"w",2115,4359},
                {"w",2115,4358},
                {"l",2115,4357,"contact_ug_ladder_top_twofloors",2},
                {"w",2115,4359},
                {"w",2116,4359},
                {"w",2116,4360},
                {"w",2120,4360},
                {"w",2120,4361},
                {"w",2121,4361},
                {"w",2121,4363},
                {"w",2122,4363},
                {"w",2122,4364},
                {"w",2130,4364},
                {"w",2130,4365},
                {"w",2131,4365},
                {"w",2131,4366},
                {"w",2133,4366},
                {"w",2133,4367},
                {"w",2135,4367},
                {"w",2135,4368},
                {"w",2136,4368},
                {"w",2136,4369},
                {"w",2149,4369},
                {"w",2149,4374},
                {"w",2147,4374},
                {"w",2147,4375},
                {"w",2146,4375},
                {"w",2146,4381},
                {"w",2145,4381},
                {"w",2145,4382},
                {"w",2144,4382},
                {"w",2144,4395},
                {"w",2142,4395},
                {"w",2142,4396},
                {"w",2140,4396},
                {"w",2140,4397},
                {"w",2134,4397},
                {"w",2134,4409},
                {"l",2134,4410,"contact_ug_ladder_twofloors",0},
                {"w",2136,4410},
                {"w",2136,4409},
                {"w",2137,4409},
                {"t",2138,4409},
                {"w",2138,4409},
                {"w",2143,4409},
                {"w",2143,4410},
                {"w",2155,4410},
                {"w",2155,4409},
                {"w",2161,4409},
                {"w",2161,4408},
                {"w",2162,4408},
                {"w",2162,4401},
                {"w",2166,4401},
                {"w",2166,4409},
            }
        for i, st in ipairs(mazeDown) do
            if st[1] == "w" then
                local rw, dw = t.player.walk_to(st[2], st[3], 80)
                t.expect("maze1.hop" .. i, rw, dw or ("walked to " .. st[2] .. "," .. st[3]))
            elseif st[1] == "t" then
                t.exec("maze1.evade" .. i, t.player.click_loc, "contact_spiketrap_floor", 1, { at = { st[2], st[3], 2 } })
            else
                t.exec("maze1.ladder" .. i, t.player.click_loc, st[4], 1, { at = { st[2], st[3], st[5] } })
                t.ticks(3)
            end
        end
        arrived("maze1.tile", 2116, 4364, 2, 1, "the maze's last hop, beside the boss ladder 2116,4365 (Contact.java goDownToChasm); the maze began at 2166,4409")
        t.exec("goDownToChasm", t.player.click_loc, "contact_ug_boss_ladder", 1)
        t.ticks(8)
        arrived("chasm.arrived", 2296, 4298, 0, 2, "the chasm's ladder foot; came from 2116,4364,2")

        for i, st in ipairs({ {2296,4298},{2295,4298},{2295,4296},{2287,4296},{2287,4295},{2285,4295},{2285,4294},{2281,4294},{2281,4300},{2282,4300},{2282,4301},{2284,4301},{2284,4302},{2286,4302},{2286,4303},{2287,4303},{2287,4304},{2292,4304},{2292,4307},{2293,4307},{2293,4315},{2294,4315},{2294,4316},{2297,4316},{2297,4319},{2289,4319},{2289,4318},{2287,4318},{2287,4317},{2286,4317},{2286,4314},{2284,4314} }) do
            local rw, dw = t.player.walk_to(st[1], st[2], 80)
            t.expect("chasm1.hop" .. i, rw, dw or ("walked to " .. st[1] .. "," .. st[2]))
        end
        t.exec("searchKaleef", t.player.click_loc, "contact_dead_body_kaleef_vis", 1)
        t.chat.continue_(true)
        t.ticks(3)
        t.inv.await("contact_kaleef_scroll", 1, 10)
        t.check("parchment.in_inv", t.inv.expect_has("contact_kaleef_scroll", 1))
        t.exec("readParchment", t.player.inv_op, "contact_kaleef_scroll", 1)
        t.chat.continue_(true)
        t.ticks(2)
        t.chat.continue_(true)
        t.ticks(2)
        t.expect("quest.stage.read", t.quest.expect_stage("read"))

        -- talkToMaisa: she stands "at the other end of a gaping chasm" (wiki Contact! oldid
        -- 15292391); the talk is an [apnpc1] across it (contact_maisa.rs2, seam b53-seam3).
        for i, st in ipairs({ {2264,4314},{2264,4317} }) do
            local rw, dw = t.player.walk_to(st[1], st[2], 80)
            t.expect("maisa.hop" .. i, rw, dw or ("walked to " .. st[1] .. "," .. st[2]))
        end
        t.exec("talkToMaisa", t.player.talk_to, "contact_maisa_multi", 1)
        t.exec("talkToMaisa-q1", t.chat.play, {
            "npc:you're not Kaleef",
            "player:Kaleef is dead",
            "npc:Where was Prince Ali imprisoned",
            "choose:Draynor Village.",
            "player:Draynor Village",
            "npc:Who helped free him",
        })
        t.exec("talkToMaisa.again", t.player.talk_to, "contact_maisa_multi", 1)
        t.exec("talkToMaisa-q2", t.chat.play, {
            "choose:Leela.",
            "player:Leela",
            "npc:I believe you",
            "npc:Find him in Al Kharid",
        })
        t.ticks(2)
        t.expect("quest.stage.met_maisa", t.quest.expect_stage("met_maisa"))
        -- talked from the EAST lip (x 2263-2265); Maisa stands at 2258,4317 across the chasm
        arrived("maisa.across", 2264, 4317, 0, 1, "Maisa 2258,4317 across the chasm")

        t.exec("goto-osman", t.player.goto_tile, 3288, 3180, 0)
        t.exec("talkToOsman", t.player.talk_to, "contact_osman_multi", 1)
        t.exec("talkToOsman-dialog", t.chat.play, {
            "player:I've just come from Maisa",
            "npc:Menaphos and Al Kharid have been rivals",
            "choose:It could drive a wedge between the Menaphite cities.",
            "player:It could drive a wedge",
            "npc:Now that is interesting",
        })
        t.ticks(2)
        t.expect("quest.stage.told_osman", t.quest.expect_stage("told_osman"))

        t.exec("goto-osman-outside", t.player.goto_tile, 3285, 2814, 0)
        t.exec("talkToOsmanOutsideSoph", t.player.talk_to, "contact_osman_desert_multi", 1)
        t.exec("talkToOsmanOutsideSoph-dialog", t.chat.play, {
            "npc:how do you propose I get into Menaphos",
            "choose:I know of a secret entrance to the north.",
            "player:I know of a secret entrance",
            "npc:The old Sect of Scabaras tunnels",
            "mesbox:Osman heads down",
        })
        t.ticks(2)
        t.expect("quest.stage.osman_outside", t.quest.expect_stage("osman_outside"))

        t.exec("goto-jex-again", t.player.goto_tile, 3313, 2797, 0)
        t.exec("goDownToBankAgain", t.player.click_loc, "contact_temple_trapdoor_open", 1)
        t.exec("goDownToBankAgain.continue", t.chat.continue_, true)
        t.ticks(6)
        arrived("bank2.arrived", 2766, 5132, 0, 3, "the trapdoor is at 3315,2797 in Sophanem")
        t.exec("goDownToDungeonAgain", t.player.click_loc, "contact_ladder_barricaded", 1)
        t.ticks(8)
        arrived("dungeon2.arrived", 2166, 4409, 2, 2, "the ladder is in the bank vault at 2766,5130")
        for i, st in ipairs(mazeDown) do
            if st[1] == "w" then
                local rw, dw = t.player.walk_to(st[2], st[3], 80)
                t.expect("maze2.hop" .. i, rw, dw or ("walked to " .. st[2] .. "," .. st[3]))
            elseif st[1] == "t" then
                t.exec("maze2.evade" .. i, t.player.click_loc, "contact_spiketrap_floor", 1, { at = { st[2], st[3], 2 } })
            else
                t.exec("maze2.ladder" .. i, t.player.click_loc, st[4], 1, { at = { st[2], st[3], st[5] } })
                t.ticks(3)
            end
        end
        t.exec("goDownToChasmAgain", t.player.click_loc, "contact_ug_boss_ladder", 1)
        t.ticks(4)
        t.exec("goDownToChasmAgain.mesbox", t.chat.continue_, true)
        t.ticks(4)
        -- The second visit is the player's own copy of the chasm (contact_dungeon.rs2
        -- ~contact_enter_private_chasm: p_telejump to instance-local 56,10 of a copy of m35_67),
        -- so the arrival is graded on the copy's local tile, not on an absolute one.
        do
            local tr, th = t.world.tile()
            local lx, lz = th and (th.x % 64), th and (th.z % 64)
            t.check("chasm2.arrived", tr == "ok" and th.level == 0 and th.x >= 6400
                and math.abs(lx - 56) <= 1 and math.abs(lz - 10) <= 1,
                tostring(th and th.x) .. "," .. tostring(th and th.z) .. "," .. tostring(th and th.level)
                    .. " = instance-local " .. tostring(lx) .. "," .. tostring(lz)
                    .. " (want an instance copy, x >= 6400, local 56,10 +-1; came from 2116,4364,2)")
        end
        t.expect("quest.stage.in_chasm", t.quest.expect_stage("in_chasm"))

        -- Protect from Melee BEFORE the approach: the Scarab and its four summons are aggressive
        -- (contact.npc huntmode). Recipe: verbs-combat.md "Turning on a protection prayer".
        local function protect_melee(name, want)
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            local tab_result, wr = "ok", "ok"
            if now ~= want then
                tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local pw
                wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
            end
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check(name, tab_result == "ok" and wr == "ok" and on == want,
                "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on) .. " (want " .. want .. "); prayer "
                    .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        protect_melee("killGiantScarab-protectMelee", 1)

        -- Approach: 6439,92 (chasm-local 39,28). The old target 6436,96 is local 36,32 -- the
        -- scarab mage's spawn tile (contact_scarab.rs2:24, movecoord(spot, 4, 0, -3)) -- and is
        -- not reachable on foot: the server routed every walk to it to 6439,92 and walk_to
        -- timed out there (b55 committed run; probe build/quest_gate/fixb56_contact_probe),
        -- with no loc with an op beside the stop. 6439,92 is the reachable tile on that line.
        do
            local rw, dw = t.player.walk_to(6439, 92, 60)
            local tr, th = t.world.tile()
            t.check("chasm2.hop1", rw == "ok" and tr == "ok" and th.x == 6439 and th.z == 92,
                "walk_to 6439,92 -> " .. tostring(rw) .. " " .. tostring(dw) .. "; at "
                    .. tostring(th and th.x) .. "," .. tostring(th and th.z) .. " (from the ladder foot 6456,74)")
        end
        do
            local nr, nd = t.npc.tiles("contact_scarab_boss", 12)
            t.check("chasm2.near", nr == "ok", "Giant Scarab within 12 tiles of the approach tile: " .. tostring(nr) .. " " .. tostring(nd))
        end
        local cr = t.cheat("::contact_scarab_hp")
        local mr, md = t.msg.expect("Giant Scarab hitpoints")
        t.check("scarab.hp_full", mr == "ok" and string.find(tostring(md), "hitpoints 130/130", 1, true) ~= nil,
            "::contact_scarab_hp -> " .. tostring(cr) .. "; " .. tostring(md))
        t.exec("killGiantScarab", t.player.attack, "contact_scarab_boss", 2, 20)
        local _, sharks_before = t.inv.count("shark")
        local _, scarab_detail = t.exec("killGiantScarab.dead", t.npc.await_dead_engaged, 400, 40, { eat = { item = "shark", below = 60 } })
        local lowest_fight = tonumber(tostring(scarab_detail):match("lowest hp (%d+)/"))
        local scarab_ticks = tonumber(tostring(scarab_detail):match("dead after (%d+) tick"))
        local _, sharks_left = t.inv.count("shark")
        t.check("killGiantScarab-margin", lowest_fight ~= nil and lowest_fight >= 25 and (sharks_left or 0) >= 1,
            "lowest hp in the fight " .. tostring(lowest_fight) .. "/99, sharks " .. tostring(sharks_before) .. " -> "
                .. tostring(sharks_left) .. " left, Scarab dead after " .. tostring(scarab_ticks)
                .. " ticks, hp now " .. tostring(hp_now()) .. "/99 (margin: lowest hp >= 25 AND sharks left >= 1)")
        protect_melee("killGiantScarab-prayerOff", 0)
        t.ticks(10)
        t.expect("quest.stage.scarab_killed", t.quest.expect_stage("scarab_killed"))

        -- Cure the poison the Scarab left (its ranged swing poisons, severity 41), then eat back
        -- up before walking on: the b55 run walked off at 50 hp and the poison took it to 13.
        do
            local _, p0 = t.var.varp("varp102_poison")
            local drank = 0
            if (tonumber(p0) or 0) > 0 then
                t.player.inv_op("4doseantipoison", 1)
                drank = 1
                t.ticks(2)
            end
            local _, p1 = t.var.varp("varp102_poison")
            local _, doses = t.inv.count("3doseantipoison")
            t.check("cureScarabPoison", (tonumber(p1) or 1) <= 0 and (drank == 0 or doses == 1),
                "varp102_poison " .. tostring(p0) .. " -> " .. tostring(p1) .. " (want <= 0); antipoison doses drunk "
                    .. drank .. ", 3doseantipoison now " .. tostring(doses))
        end
        do
            local ate = 0
            local h0 = hp_now()
            for _ = 1, 4 do
                local h = hp_now()
                if h == nil or h >= 80 then break end
                t.player.inv_op("shark", 1)
                ate = ate + 1
                t.ticks(3)
            end
            local h1 = hp_now()
            local _, left = t.inv.count("shark")
            t.check("eatAfterScarab", h1 ~= nil and h1 >= 60,
                "hp " .. tostring(h0) .. " -> " .. tostring(h1) .. "/99 after " .. ate .. " shark(s); sharks left " .. tostring(left) .. " (want >= 60)")
        end

        t.exec("pickUpKeris", t.player.click_obj, "contact_keris", 3)
        t.inv.await("contact_keris", 1, 10)
        t.check("keris.in_inv", t.inv.expect_has("contact_keris", 1))
        t.exec("talkToOsmanChasm", t.player.talk_to, "contact_osman_cave_instance", 1)
        t.exec("talkToOsmanChasm-dialog", t.chat.play, {
            "npc:Told you I had it under control",
            "player:What about Maisa's plan",
            "npc:Menaphos will smuggle",
            "npc:Go and tell the High Priest",
        })
        t.ticks(2)
        t.expect("quest.stage.ready", t.quest.expect_stage("ready"))

        t.player.walk_to(6442, 70, 60)
        t.exec("leaveChasm", t.player.click_loc, "contact_boss_ug_ladder", 1)
        t.ticks(6)
        arrived("chasm.left", 2116, 4364, 2, 2, "the boss ladder's top in the maze; came from the instance copy")

        t.exec("goto-highpriest-again", t.player.goto_tile, 3281, 2774, 0)
        local _, snap = t.skill.snapshot()
        t.exec("returnToHighPriest", t.player.talk_to, "ics_little_hipriest_vis", 1)
        t.exec("returnToHighPriest-dialog", t.chat.play, {
            "npc:did it work",
            "player:Osman and Maisa have arranged",
            "npc:At last",
        })
        t.ticks(4)
        t.quest.expect_complete()
        t.expect("contact.thieving_xp_7000", t.skill.expect_gain("thieving", 7000, snap))
        t.check("contact.keris_kept", t.inv.expect_has("contact_keris", 1))
        t.check("contact.lamp_given", t.inv.expect_has("contact_lantern", 1))

        local _, str_snap = t.skill.snapshot()
        t.exec("lamp.wish1", t.player.inv_op, "contact_lantern", 1)
        t.exec("lamp.wish1.pick", t.chat.play, { "choose:Strength", "mesbox:The lamp grants you 7,000 experience" })
        t.ticks(2)
        t.expect("lamp.strength_xp_7000", t.skill.expect_gain("strength", 7000, str_snap))
        local _, mag_snap = t.skill.snapshot()
        t.exec("lamp.wish2", t.player.inv_op, "contact_lantern", 1)
        t.exec("lamp.wish2.pick", t.chat.play, { "choose:More...", "choose:Magic", "mesbox:last wish" })
        t.ticks(2)
        t.expect("lamp.magic_xp_7000", t.skill.expect_gain("magic", 7000, mag_snap))
        t.finish(0)
        return
    end,
}
