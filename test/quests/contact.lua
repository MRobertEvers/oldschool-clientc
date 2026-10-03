-- Contact! driven from the guide ladder (contact.notes.md, b53 parity scratch).
-- Prerequisites by ::complete; light source + tinderbox + gear are brought-along kit.
-- Maze note: the Sophanem maze is walked on foot with the trap presses (no goto past it).
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
        "::give shark 25",
        "::setlevel hitpoints 99",
        "::setlevel agility 99",
        "::setlevel thieving 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::wield rune_full_helm",
        "::wield rune_chainbody",
        "::wield rune_platelegs",
        "::wield zamorak_spear",
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
        local r, d = t.world.tile()
        t.check("bank.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
        t.exec("goDownToDungeon", t.player.click_loc, "contact_ladder_barricaded", 1)
        t.ticks(8)
        r, d = t.world.tile()
        t.check("dungeon.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
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
        r, d = t.world.tile()
        t.check("maze1.tile", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
        t.exec("goDownToChasm", t.player.click_loc, "contact_ug_boss_ladder", 1)
        t.ticks(8)
        r, d = t.world.tile()
        t.check("chasm.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))

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
        r, d = t.world.tile()
        t.check("maisa.across", r, "talked from " .. tostring(d and d.x) .. "," .. tostring(d and d.z) .. "; Maisa 2258,4317 across the chasm")

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
        r, d = t.world.tile()
        t.check("bank2.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
        t.exec("goDownToDungeonAgain", t.player.click_loc, "contact_ladder_barricaded", 1)
        t.ticks(8)
        r, d = t.world.tile()
        t.check("dungeon2.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
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
        r, d = t.world.tile()
        t.check("chasm2.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
        t.expect("quest.stage.in_chasm", t.quest.expect_stage("in_chasm"))
        for i, st in ipairs({ {6436,96} }) do
            local rw, dw = t.player.walk_to(st[1], st[2], 60)
            t.check("chasm2.hop" .. i, true, "approach toward the Scarab: walk_to " .. st[1] .. "," .. st[2] .. " -> " .. tostring(rw) .. " " .. tostring(dw) .. " (the aggressive boss engages on the way)")
        end
        r, d = t.world.tile()
        t.check("chasm2.near", r, tostring(d and d.x) .. "," .. tostring(d and d.z))
        local cr = t.cheat("::contact_scarab_hp")
        local mr, md = t.msg.expect("Giant Scarab hitpoints")
        t.check("scarab.hp_full", mr == "ok" and string.find(tostring(md), "hitpoints 130/130", 1, true) ~= nil,
            "::contact_scarab_hp -> " .. tostring(cr) .. "; " .. tostring(md))
        t.exec("killGiantScarab", t.player.attack, "contact_scarab_boss", 2, 20)
        t.exec("killGiantScarab.dead", t.npc.await_dead_engaged, 400, 40, { eat = { item = "shark", below = 50 } })
        t.ticks(10)
        t.expect("quest.stage.scarab_killed", t.quest.expect_stage("scarab_killed"))

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
        r, d = t.world.tile()
        t.check("chasm.left", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))

        t.exec("goto-highpriest-again", t.player.goto_tile, 3281, 2774, 0)
        local snap_result, snap = t.skill.snapshot()
        t.check("returnToHighPriest.snapshot", snap_result, "skill.snapshot before the hand-in -> " .. tostring(snap_result))
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

        local str_result, str_snap = t.skill.snapshot()
        t.check("lamp.snapshot", str_result, "skill.snapshot before wish 1 -> " .. tostring(str_result))
        t.exec("lamp.wish1", t.player.inv_op, "contact_lantern", 1)
        t.exec("lamp.wish1.pick", t.chat.play, { "choose:Strength", "mesbox:The lamp grants you 7,000 experience" })
        t.ticks(2)
        t.expect("lamp.strength_xp_7000", t.skill.expect_gain("strength", 7000, str_snap))
        local mag_result, mag_snap = t.skill.snapshot()
        t.check("lamp.snapshot2", mag_result, "skill.snapshot before wish 2 -> " .. tostring(mag_result))
        t.exec("lamp.wish2", t.player.inv_op, "contact_lantern", 1)
        t.exec("lamp.wish2.pick", t.chat.play, { "choose:More...", "choose:Magic", "mesbox:last wish" })
        t.ticks(2)
        t.expect("lamp.magic_xp_7000", t.skill.expect_gain("magic", 7000, mag_snap))
        t.finish(0)
        return
    end,
}
