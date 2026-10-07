-- Temple of the Eye. Content:
--   OSRS-Content/osrs239-content/server/scripts/quests/quest_templeoftheeye/scripts/templeoftheeye.rs2
--   configs/templeoftheeye.constant (stages 0..130, %varb13738_tote on varp3405_tote_primary),
--   configs/templeoftheeye.spawn (Persten, the apprentices), configs/templeoftheeye_maplink.dbrow
--   (the temple's way out).
-- Guide: quest-helper helpers/quests/templeoftheeye/TempleOfTheEye.java (`python3 tools/quest_gate/ladder.py
-- templeoftheeye`); wiki quick guide oldid 15317311, walkthrough oldid 15304735, transcript oldid 15330441.
--
-- Flow the .rs2 authors (stage -> stage, the trigger that moves it):
--    0 -> 10   Wizard Persten (tote_persten_alkharid_parent, 3285,3232): "Yes." -> the Eye amulet
--   10 -> 15   Varrock Mage of Zamorak (rcu_zammy_mage1_edge, the chapel 3259,3383)
--   15         Tea Seller (tea_seller.rs2 -> @tote_tea_seller_talk): a strong cup of tea
--   15 -> 20   the mage again with the tea; 20 -> 25 the mage's one-time teleport into the Abyss
--   25 -> 35   Dark Mage (rcu_zammy_mage2 3039,4834): the spell; the six energies appear
--   35 -> 40   touch the energies in this player's own random order (a wrong touch resets them)
--   40 -> 45   Dark Mage: the Abyssal incantation
--   45 -> 60   Persten: amulet back (55), "Yes." -> one-time teleport to the tower ladder room 3105,3162
--   60 -> 70   Sedridor (65 when he takes the incantation, 70 at the end of the talk)
--   70 -> 75   Traiborn: "I need your apprentices to help with an incantation."
--   75 -> 80   Felix, Tamara and Cordelia each show their riddle (the third one moves the stage)
--   80 -> 85   Traiborn: "I think I know what a thingummywut is!" -> 11
--   85 -> 90   Sedridor + Persten in the basement ("Not yet." here keeps the two steps apart)
--   90 -> 95   Sedridor: "So we're ready to perform the incantation?" -> "Let's do it."
--   95 -> 100  the basement portal (tote_portal_to_gotr_child 3104,9574) -> the temple 2399,5630
--  100 -> 105  the three apprentices in the temple
--  105 -> 110  Persten (the vision); 110 -> 115 Persten again (the rift)
--  115 -> 125  Cordelia: the Guardians of the Rift tutorial (summarised), back to the basement 3104,9573
--  125 -> 130  Sedridor: ~toe_quest_complete (9210 Runecraft XP, medium pouch)
--
-- Door rule: every building is entered and left through its door (pass_door), every floor change is
-- its own ladder/stair (climb), the temple is entered and left by its portals; the gotos run between open
-- overland tiles only (reach.py, listed at each goto).
return {
    id = "templeoftheeye",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the quest items fit
        "::setlevel runecraft 10", -- the quest's own requirement (^toe_req_runecraft, :18 ~toe_has_runecraft)
        "::complete miniquest_entertheabyss", -- the other start gate (%varp492_abyssal_miniquest, :12)
        "::give bronze_pickaxe 1", -- Quest Helper kit for the Guardians tutorial (Pickaxe)
        "::give chisel 1", -- Quest Helper kit for the Guardians tutorial (Chisel)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb13738_tote",
            constants = {
                not_started = 0, persten = 5, mage = 10, tea = 15, mage2 = 20, abyss = 25, dark = 30,
                runes = 35, dark2 = 40, persten2 = 45, amulet_back = 55, archmage = 60, archmage_wait = 65,
                traiborn = 70, puzzle = 75, traiborn2 = 80, archmage2 = 85, incant = 90, cutscene = 95,
                investigate = 100, persten_t = 105, debrief = 110, tutorial = 115, tutorial2 = 120,
                finish = 125, complete = 130,
            },
            row = "quest_templeoftheeye",
            display = "Temple of the Eye",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        local function stage(name)
            t.expect("quest.stage." .. name, t.quest.expect_stage(name))
        end
        local function tile_text()
            local r, tt = t.world.tile()
            if r ~= "ok" or type(tt) ~= "table" then
                return "tile " .. tostring(r)
            end
            return tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level)
        end
        local function door(name, closed_sym, open_sym, door_x, door_z, door_level, near_x, near_z, far_x, far_z, far_ok, far_desc)
            t.exec(name, t.player.pass_door, { closed = closed_sym, open = open_sym, at = { door_x, door_z, door_level },
                near = { near_x, near_z }, far = { far_x, far_z }, far_ok = far_ok, far_desc = far_desc })
        end
        local function present(row, sym, radius)
            local r, d = t.npc.await_present(sym, radius, 10)
            t.check(row, r == "ok", "npc.await_present(" .. sym .. ", " .. radius .. ") -> " .. tostring(r) .. " "
                .. tostring(d) .. " at " .. tile_text())
        end

        -- Varrock Zamorak chapel: fai_varrock_poor_door_flipped 3255,3388 (street x <= 3255, chapel x >= 3256).
        local function chapel_in(pfx)
            door(pfx .. ".chapelDoorIn", "fai_varrock_poor_door_flipped", "fai_varrock_poor_door_open_flipped", 3255, 3388, 0,
                3254, 3388, 3257, 3387, function(tt) return tt.x >= 3256 and tt.level == 0 end, "inside the chapel, x >= 3256")
        end
        local function chapel_out(pfx)
            door(pfx .. ".chapelDoorOut", "fai_varrock_poor_door_flipped", "fai_varrock_poor_door_open_flipped", 3255, 3388, 0,
                3257, 3387, 3253, 3388, function(tt) return tt.x <= 3255 and tt.level == 0 end, "back on the street, x <= 3255")
        end
        -- Wizards' Tower basement: the ladder corridor <-> Sedridor's room (poordoor 3108,9570).
        local function sedridor_in(pfx)
            door(pfx .. ".sedridorDoorIn", "poordoor", "poordooropen", 3108, 9570, 0, 3109, 9570, 3106, 9570,
                function(tt) return tt.x <= 3107 and tt.z >= 9566 and tt.z <= 9574 end, "inside Sedridor's room, x <= 3107")
        end
        local function sedridor_out(pfx)
            door(pfx .. ".sedridorDoorOut", "poordoor", "poordooropen", 3108, 9570, 0, 3107, 9570, 3109, 9570,
                function(tt) return tt.x >= 3108 end, "in the corridor east of Sedridor's door, x >= 3108")
        end
        local function ladder_down(row)
            -- maplink [maplink_0_48_49_33_26_down]: 3105,3162 -> 3104,9576 (frame 0 -> 1)
            t.exec(row, t.player.climb, { loc = "wizards_tower_laddertop", op = 1, op_name = "Climb-down",
                at = { 3104, 3162, 0 }, src = { 3105, 3162 }, dest = { 3104, 9576, 0 } })
        end
        local function ladder_up(row)
            -- maplink [maplink_0_48_149_32_40_up]: 3104,9576 -> 3105,3162 (frame 1 -> 0)
            t.exec(row, t.player.climb, { loc = "wizards_tower_ladder", op = 1, op_name = "Climb-up",
                at = { 3103, 9576, 0 }, src = { 3104, 9576 }, dest = { 3105, 3162, 0 } })
        end
        -- Traiborn's room on the tower's 1st floor: fai_wiztower_poor_door 3109,3162,1.
        local function traiborn_in(pfx)
            door(pfx .. ".roomDoorIn", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3109, 3162, 1,
                3109, 3162, 3111, 3162, function(tt) return tt.x >= 3110 and tt.level == 1 end, "in Traiborn's room, x >= 3110")
        end
        local function traiborn_out(pfx)
            door(pfx .. ".roomDoorOut", "fai_wiztower_poor_door", "fai_wiztower_poor_door_open", 3109, 3162, 1,
                3110, 3162, 3108, 3162, function(tt) return tt.x <= 3108 and tt.level == 1 end, "out of Traiborn's room, x <= 3108")
        end

        -- The start gates @toe_persten_start reads: Runecraft 10, then Enter the Abyss.
        stage("not_started")
        local vr, vv = t.var.varp("varp492_abyssal_miniquest")
        t.expect("requirements.enterTheAbyss", (vr == "ok" and vv == 4) and "ok" or "refused",
            "varp492_abyssal_miniquest -> " .. tostring(vr) .. " " .. tostring(vv) .. " (want 4, ^eta_complete)")
        local rr, rs = t.skill.read("runecraft")
        local rc_level = (rr == "ok" and type(rs) == "table") and rs.level or nil
        t.check("requirements.runecraft", rc_level ~= nil and rc_level >= 10,
            "skill.read(runecraft) -> " .. tostring(rr) .. " level " .. tostring(rc_level) .. " (want >= 10, ^toe_req_runecraft)")

        -- =========================== Leg 1: Persten, the mage, the tea, the Abyss
        -- Fixture 3206,3233 -> the open street beside Persten (reach.py: REACH closed-doors len=323, round the
        -- north end of the Al Kharid fence; no toll gate on the way).
        t.exec("goto-talkToPersten1", t.player.goto_tile, 3284, 3232, 0)
        present("talkToPersten1.present", "tote_persten_alkharid_parent", 12)
        t.exec("talkToPersten1", t.player.talk_to, "tote_persten_alkharid_parent", 1)
        t.exec("talkToPersten1-dialog", t.chat.play, {
            "player:What's a wizard doing in Al Kharid?",
            "npc:Why shouldn't a wizard be in Al Kharid?",
            "player:Er... good question.",
            "npc:Well for a start, I'm not just any wizard.",
            "npc:I have a little task, if you're willing.",
            "choose:Yes.",
            "player:Alright, what kind of task is it?",
            "npc:Seek the Mage of Zamorak near the Wilderness ditch.",
            "mesbox:Persten hands you the amulet.",
        })
        stage("mage")
        t.expect("talkToPersten1.amulet", t.inv.await("tote_amulet", 1, 5))

        -- Al Kharid street -> the street outside the Varrock chapel door (REACH closed-doors len=277).
        t.exec("goto-talkToMage1", t.player.goto_tile, 3253, 3388, 0)
        chapel_in("talkToMage1")
        t.exec("talkToMage1", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMage1-dialog", t.chat.play, { "npc:Fetch Herbert a strong cup of tea first." })
        stage("tea")
        chapel_out("getTeaForMage")
        -- The open market street to the stall front (REACH closed-doors len=44).
        t.exec("getTeaForMage.walk", t.player.walk_to, 3271, 3415, 60)
        t.exec("getTeaForMage", t.player.talk_to, "tea_seller", 1)
        t.exec("getTeaForMage-dialog", t.chat.play, { "npc:I hope Herbert enjoys it." })
        t.expect("getTeaForMage.tea", t.inv.await("tote_cup_of_tea_strong", 1, 5))
        t.exec("talkToMage2.walk", t.player.walk_to, 3253, 3388, 60)
        chapel_in("talkToMage2")
        t.exec("talkToMage2", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("talkToMage2-dialog", t.chat.play, { "npc:Fine. Herbert has his tea. Now into the Abyss." })
        stage("mage2")
        t.expect("talkToMage2.teaTaken", t.inv.expect_absent("tote_cup_of_tea_strong"))
        t.exec("teleportViaHerbert", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("teleportViaHerbert-dialog", t.chat.play, {
            "npc:The spell to determine the origin of that amulet will only work from the Abyss.",
            "choose:Yes.",
            "npc:Veniens! Sallakar! Rinnesset!",
        })
        local in_abyss = t.await({
            level = function()
                local r, tt = t.world.tile()
                return r == "ok" and math.abs(tt.x - 3040) <= 2 and math.abs(tt.z - 4834) <= 2
            end,
            note = "the Abyss landing 3040,4834 (^toe_abyss_coord)",
        }, 10)
        t.check("teleportViaHerbert.landed", in_abyss == "ok", "await -> " .. tostring(in_abyss) .. " at " .. tile_text())
        stage("abyss")
        t.ticks(3)

        -- =========================== Leg 2: the Dark Mage, the energies, back to Persten
        t.exec("talkToDarkMage1", t.player.talk_to, "rcu_zammy_mage2", 1)
        t.exec("talkToDarkMage1-dialog", t.chat.play, {
            "player:This amulet is giving off abyssal energy.",
            "npc:Let's have a look then.",
            "*", -- objbox: You show the amulet to the Dark Mage.
            "npc:Right, just give me a moment.",
            "*", -- objbox: The Dark Mage casts a spell on the amulet.
        })
        t.exec("talkToDarkMage1-spell", t.chat.drain, {})
        stage("runes")

        -- touchRunes (Quest Helper RuneEnergyStep): the order is random per player, so it is found by trial and
        -- error, read from what the player sees: a right touch turns that energy white (its *_empowered form)
        -- and keeps the others; a wrong one turns every white one back (wiki quick guide oldid 15317311).
        local energies = { "earth", "cosmic", "death", "nature", "law", "fire" }
        for _, e in ipairs(energies) do
            local er, ed = t.world.loc_near("tote_abyssal_energy_" .. e .. "_vis", 8)
            t.check("touchRunes.shown." .. e, er == "ok", "loc_near(tote_abyssal_energy_" .. e .. "_vis, 8) -> "
                .. tostring(er) .. " " .. tostring(ed and (ed.x or ed[1]) or ""))
        end
        local function lit(e)
            local r = t.world.loc_near("tote_abyssal_energy_" .. e .. "_empowered", 8)
            return r == "ok"
        end
        local known = {}
        local is_known = {}
        local prefix_lit = true
        local touches = 0
        local function touch(e)
            touches = touches + 1
            local r = t.player.click_loc("tote_abyssal_energy_" .. e .. "_vis", 1)
            t.ticks(2)
            local mr, md = t.msg.last(1)
            local line = (mr == "ok" and type(md) == "table" and md[1]) and tostring(md[1].text) or tostring(md)
            return r, line
        end
        while #known < 6 and touches < 60 do
            local found = false
            for _, cand in ipairs(energies) do
                if not is_known[cand] then
                    if not prefix_lit then
                        for i, k in ipairs(known) do
                            local r, line = touch(k)
                            t.check("touchRunes.retouch" .. touches .. "." .. k, lit(k),
                                "click_loc(" .. k .. ") -> " .. tostring(r) .. ", known #" .. i .. " white again: " .. line)
                        end
                        prefix_lit = true
                    end
                    local r, line = touch(cand)
                    local white = lit(cand)
                    if white then
                        known[#known + 1] = cand
                        is_known[cand] = true
                        t.check("touchRunes.order" .. #known, white and line:find("reacts strangely", 1, true) ~= nil,
                            "click_loc(" .. cand .. ") -> " .. tostring(r) .. ": white (" .. line .. ")")
                        found = true
                        break
                    end
                    prefix_lit = (#known == 0)
                    t.check("touchRunes.try" .. touches, line:find("does not respond", 1, true) ~= nil,
                        "click_loc(" .. cand .. ") -> " .. tostring(r) .. ": not this one, the white ones reset (" .. line .. ")")
                end
            end
            if not found then
                break
            end
        end
        t.check("touchRunes", #known == 6, "order " .. table.concat(known, ",") .. " in " .. touches .. " touch(es)")
        stage("dark2")
        t.exec("talkToDarkMage2", t.player.talk_to, "rcu_zammy_mage2", 1)
        t.exec("talkToDarkMage2-dialog", t.chat.play, {
            "player:I've worked out which order the energy goes in.",
            "npc:Yes, I can see that. Now, hang on.",
        })
        t.exec("talkToDarkMage2-scroll", t.chat.drain, {})
        t.expect("talkToDarkMage2.scroll", t.inv.await("tote_incantation", 1, 5))
        stage("persten2")

        -- Out of the Abyss the real way: the Fire rift (runecraft_abyss.rs2:330 ~abyss_rift -> the fire
        -- altar, runecraft.dbrow enter_coord 2576,4848), then the altar's exit portal (runecraft.rs2:16 ->
        -- exit_coord 3310,3252, the fire ruins north of the duel arena).
        t.exec("leaveAbyss.fireRift", t.player.climb, { loc = "abyss_exit_to_fire", op = 1, op_name = "Exit-through",
            at = { 3029, 4830, 0 }, dest = { 2576, 4848, 0 }, slack = 2, ticks = 25,
            same_level = "runecraft_abyss.rs2:396 p_telejump(runecraft_fire enter_coord 0_40_75_16_48)" })
        t.exec("leaveAbyss.firePortal", t.player.climb, { loc = "firetemple_exit_portal", op = 1, op_name = "Use",
            at = { 2574, 4850, 0 }, dest = { 3310, 3252, 0 }, slack = 2, ticks = 25,
            same_level = "runecraft.rs2:24 p_telejump(runecraft_fire exit_coord 0_51_50_46_52)" })
        -- The fire ruins -> Persten's street (REACH closed-doors len=46).
        t.exec("talkToPersten2.walk", t.player.walk_to, 3284, 3232, 80)
        present("talkToPersten2.present", "tote_persten_alkharid_parent", 12)
        t.exec("talkToPersten2", t.player.talk_to, "tote_persten_alkharid_parent", 1)
        t.exec("talkToPersten2-dialog", t.chat.play, {
            "npc:Hello again! Any luck with that amulet?",
            "player:Yes! I have an incantation",
        })
        t.exec("talkToPersten2-amulet", t.chat.drain, { stop_at = "options" })
        stage("archmage")
        t.expect("finishTalkToPersten2.amuletBack", t.inv.expect_absent("tote_amulet"))
        t.exec("teleportToArchmage", t.chat.choose, "Yes.")
        local at_tower = t.await({
            level = function()
                local r, tt = t.world.tile()
                return r == "ok" and tt.level == 0 and tt.x == 3105 and tt.z == 3162
            end,
            note = "the Wizards' Tower ladder room 3105,3162 (^toe_tower_coord)",
        }, 10)
        t.check("teleportToArchmage.landed", at_tower == "ok", "await -> " .. tostring(at_tower) .. " at " .. tile_text())
        t.ticks(3)

        -- =========================== Leg 3: Sedridor, Traiborn, the apprentices
        ladder_down("goDownToArchmage")
        sedridor_in("talktoArchmage1")
        t.exec("talktoArchmage1", t.player.talk_to, "head_wizard", 1)
        t.exec("talktoArchmage1-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talktoArchmage1-ask", t.chat.choose, "I need your help with an incantation.")
        t.exec("talktoArchmage1-dialog", t.chat.drain, {})
        t.expect("talktoArchmage1.incantationGiven", t.inv.expect_absent("tote_incantation"))
        stage("traiborn")
        t.exec("finishTalkingToArchmage1", t.player.talk_to, "head_wizard", 1)
        t.exec("finishTalkingToArchmage1-menu", t.chat.drain, { stop_at = "options" })
        t.exec("finishTalkingToArchmage1-ask", t.chat.choose, "Can you help me with that incantation?")
        t.exec("finishTalkingToArchmage1-dialog", t.chat.play, {
            "player:Can you help me with that incantation?",
            "npc:I haven't finished my analysis of it yet.",
        })
        t.exec("finishTalkingToArchmage1-end", t.chat.drain, {})
        sedridor_out("goUpToTraibornBasement")
        ladder_up("goUpToTraibornBasement")
        t.exec("goUpToTraiborn", t.player.climb, { loc = "fai_wiztower_spiralstairs", op = 1, op_name = "Climb-up",
            at = { 3103, 3159, 0 }, src = { 3105, 3160 }, dest = { 3104, 3161, 1 } })
        traiborn_in("talktoTrailborn1")
        t.exec("talktoTrailborn1", t.player.talk_to, "traiborn", 1)
        t.exec("talktoTrailborn1-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talktoTrailborn1-ask", t.chat.choose, "I need your apprentices to help with an incantation.")
        t.exec("talktoTrailborn1-dialog", t.chat.drain, {})
        stage("puzzle")
        -- The three apprentices stand in Traiborn's room (templeoftheeye.spawn), in no particular order.
        present("talkToFelix.present", "tote_felix_wizard_tower_parent", 6)
        t.exec("talkToFelix", t.player.talk_to, "tote_felix_wizard_tower_parent", 1)
        t.exec("talkToFelix-dialog", t.chat.drain, { stop_at = "mesbox" })
        t.expect("talkToFelix.riddle", t.chat.expect_text("Water is six and Mind is eight."))
        t.exec("talkToFelix-end", t.chat.drain, {})
        present("talkToTamara.present", "tote_tamara_wizard_tower_parent", 6)
        t.exec("talkToTamara", t.player.talk_to, "tote_tamara_wizard_tower_parent", 1)
        t.exec("talkToTamara-dialog", t.chat.drain, { stop_at = "mesbox" })
        t.expect("talkToTamara.riddle", t.chat.expect_text("Earth is five and Air is three."))
        t.exec("talkToTamara-end", t.chat.drain, {})
        stage("puzzle")
        present("talkToCordelia.present", "tote_cordelia_wizard_tower_parent", 6)
        t.exec("talkToCordelia", t.player.talk_to, "tote_cordelia_wizard_tower_parent", 1)
        t.exec("talkToCordelia-dialog", t.chat.drain, { stop_at = "mesbox" })
        t.expect("talkToCordelia.riddle", t.chat.expect_text("Fire is four and Body is seven."))
        t.exec("talkToCordelia-end", t.chat.drain, {})
        stage("traiborn2")
        -- The riddles together (wiki walkthrough oldid 15304735): Air 3 < x/3... a thingummywut is 11.
        t.exec("talktoTrailborn2", t.player.talk_to, "traiborn", 1)
        t.exec("talktoTrailborn2-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talktoTrailborn2-ask", t.chat.choose, "I think I know what a thingummywut is!")
        t.exec("talktoTrailborn2-prompt", t.chat.drain, { stop_at = "count" })
        -- the script closes the chat page (if_close) and opens the number prompt a tick later
        local prompt = t.await({ level = function() return t.chat.kind() == "count" end,
            note = "Traiborn's number prompt (p_countdialog)" }, 6)
        t.check("talktoTrailborn2-promptOpen", prompt == "ok", "await -> " .. tostring(prompt) .. ", chat.kind " .. tostring(t.chat.kind()))
        -- one digit a tick, after the prompt has settled (death.lua: a fresh count prompt drops a key sent
        -- with no tick between); the answer is graded on Traiborn's own reply below
        t.ticks(2)
        for digit in ("11"):gmatch(".") do
            t.text(digit)
            t.ticks(1)
        end
        t.key("enter")
        t.ticks(2)
        t.exec("talktoTrailborn2-dialog", t.chat.play, {
            "player:11!",
            "npc:Well done! That's correct!",
        })
        t.exec("talktoTrailborn2-end", t.chat.drain, {})
        stage("archmage2")
        traiborn_out("goDownToArchmageFloorOne")
        t.exec("goDownToArchmageFloorOne", t.player.climb, { loc = "fai_wiztower_spiralstairs_middle", op = 3,
            op_name = "Climb-down", at = { 3103, 3159, 1 }, src = { 3103, 3161 }, dest = { 3104, 3161, 0 } })
        ladder_down("goDownToArchmage2")
        sedridor_in("talktoArchmage2")
        present("talktoArchmage2.persten", "tote_persten_tower_parent", 8)
        t.exec("talktoArchmage2", t.player.talk_to, "head_wizard", 1)
        t.exec("talktoArchmage2-dialog", t.chat.drain, { stop_at = "options" })
        stage("incant")
        t.exec("talktoArchmage2-notYet", t.chat.choose, "Not yet.")
        t.exec("talktoArchmage2-end", t.chat.drain, {})
        t.exec("performIncantation", t.player.talk_to, "head_wizard", 1)
        t.exec("performIncantation-menu", t.chat.drain, { stop_at = "options" })
        t.exec("performIncantation-ask", t.chat.choose, "So we're ready to perform the incantation?")
        t.exec("performIncantation-ready", t.chat.drain, { stop_at = "options" })
        t.exec("performIncantation-go", t.chat.choose, "Let's do it.")
        t.exec("performIncantation-cutscene", t.chat.drain, {})
        stage("cutscene")

        -- =========================== Leg 4: the temple, the tutorial, the reward
        -- enterPortal: the portal the incantation opened (tote_portal_to_gotr_parent 3104,9574, a Stool until 91).
        t.exec("enterPortal", t.player.climb, { loc = "tote_portal_to_gotr_child", op = 1, op_name = "Enter",
            at = { 3104, 9574, 0 }, dest = { 2399, 5630, 0 }, slack = 1, ticks = 20,
            same_level = "templeoftheeye.rs2 [oploc1,tote_portal_to_gotr_child] p_teleport(^toe_temple_portal_coord)" })
        t.exec("templeCutscene1", t.chat.drain, {})
        stage("investigate")
        -- The temple's own way out and back in (Quest Helper investigateTemple: enterWizardBasement ->
        -- enterPortal): gotr_entry (2397,5628) -> the basement 3104,9573, then the basement portal again.
        t.exec("leaveTemple", t.player.climb, { loc = "gotr_entry", op = 1, op_name = "Enter",
            at = { 2397, 5628, 0 }, dest = { 3104, 9573, 0 }, slack = 1, ticks = 20,
            same_level = "templeoftheeye_maplink.dbrow toe_maplink_temple_exit_*_62 -> 0_48_149_32_37" })
        t.exec("enterPortal.again", t.player.climb, { loc = "tote_portal_to_gotr_child", op = 1, op_name = "Enter",
            at = { 3104, 9574, 0 }, dest = { 2399, 5630, 0 }, slack = 1, ticks = 20,
            same_level = "templeoftheeye.rs2 [oploc1,tote_portal_to_gotr_child] p_teleport(^toe_temple_portal_coord)" })
        stage("investigate")
        -- Inside the temple (one walled underwater floor, entered by the portal): the apprentices on Quest
        -- Helper's tiles. reach.py from 2399,5631: Felix 2401,5643 len=14, Tamara 2385,5659 len=42,
        -- Persten 2400,5667 len=37, Cordelia 2397,5677 len=48.
        t.exec("talkToFelix2.walk", t.player.walk_to, 2401, 5641, 40)
        present("talkToFelix2.present", "tote_felix_temple", 8)
        t.exec("talkToFelix2", t.player.talk_to, "tote_felix_temple", 1)
        t.exec("talkToFelix2-dialog", t.chat.play, {
            "player:That's an interesting statue.",
            "npc:See the plaque on it?",
        })
        t.exec("talkToFelix2-end", t.chat.drain, {})
        t.exec("talkToTamara2.walk", t.player.walk_to, 2386, 5657, 60)
        present("talkToTamara2.present", "tote_tamara_temple", 8)
        t.exec("talkToTamara2", t.player.talk_to, "tote_tamara_temple", 1)
        t.exec("talkToTamara2-dialog", t.chat.play, {
            "player:What are you looking at?",
            "npc:Look! Rune guardians",
        })
        t.exec("talkToTamara2-end", t.chat.drain, {})
        t.exec("talkToCordelia2.walk", t.player.walk_to, 2397, 5675, 60)
        present("talkToCordelia2.present", "tote_cordelia_temple", 8)
        t.exec("talkToCordelia2", t.player.talk_to, "tote_cordelia_temple", 1)
        t.exec("talkToCordelia2-dialog", t.chat.play, {
            "player:Found anything interesting?",
            "npc:There's something strange about this big hole here.",
        })
        t.exec("talkToCordelia2-end", t.chat.drain, {})
        stage("persten_t")
        t.exec("talkToPersten3.walk", t.player.walk_to, 2400, 5669, 40)
        present("talkToPersten3.present", "tote_persten_temple", 8)
        t.exec("talkToPersten3", t.player.talk_to, "tote_persten_temple", 1)
        t.exec("talkToPersten3-dialog", t.chat.play, {
            "player:Hey!",
            "npc:Hmm? Oh, I was looking at these markings here.",
        })
        t.exec("templeCutscene2", t.chat.drain, {})
        stage("debrief")
        t.exec("debrief", t.player.talk_to, "tote_persten_temple", 1)
        t.exec("debrief-dialog", t.chat.play, { "npc:Hey! Let's see what the others have found!" })
        t.exec("debrief-cutscene", t.chat.drain, {})
        stage("tutorial")
        -- guardiansTutorial / templeCutscene3: Cordelia after the Great Guardian appears (wiki quick guide:
        -- "Speak to Apprentice Cordelia."); the tutorial instance is summarised and ends in the basement.
        t.exec("guardiansTutorial.walk", t.player.walk_to, 2397, 5675, 40)
        t.exec("guardiansTutorial", t.player.talk_to, "tote_cordelia_temple", 1)
        t.exec("guardiansTutorial-dialog", t.chat.play, { "npc:Well don't think I'll be stopping them all again!" })
        t.exec("templeCutscene3", t.chat.drain, {})
        local in_basement = t.await({
            level = function()
                local r, tt = t.world.tile()
                return r == "ok" and tt.x == 3104 and tt.z == 9573 and tt.level == 0
            end,
            note = "back in Sedridor's room 3104,9573 (^toe_basement_coord)",
        }, 10)
        t.check("guardiansTutorial.backInTower", in_basement == "ok", "await -> " .. tostring(in_basement) .. " at " .. tile_text())
        stage("finish")
        t.ticks(3)
        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("finishQuest.snapshot", snapshot_result == "ok", "skill.snapshot -> " .. tostring(snapshot_result))
        t.exec("finishQuest", t.player.talk_to, "head_wizard", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:Sedridor! It all went wrong!",
            "npc:What do you mean? Was there no teleportation matrix?",
        })
        t.exec("finishQuest-end", t.chat.drain, {})
        t.quest.expect_complete()
        t.expect("reward.runecraft_xp", t.skill.expect_gain("runecraft", 9210, snapshot))
        t.expect("reward.mediumPouch", t.inv.await("rcu_pouch_medium", 1, 5))
        t.expect("reward.amulet", t.inv.await("tote_amulet", 1, 5))
    end,
}
