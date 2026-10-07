-- Troll Stronghold, driven end to end through the real client, one row per Quest Helper step
-- (TrollStronghold.java ladder: tools/quest_gate/ladder.py troll).
-- Setup stages only the prerequisite (Death Plateau, ::complete), the levels the quest asks for
-- (Agility 15 for the rocks, Thieving for the guards' pockets), a wielded weapon, food and 12 coins for
-- Tenzing's boots (getItemRequirements). Climbing boots are BOUGHT from Tenzing; Dad, the Troll General,
-- the guards, both keys and both cells are real.
-- Rewards (QuestPointReward + law talisman item): 1 quest point, no experience; the talisman is read back.
return {
    id = "troll",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv",
        "::complete quest_deathplateau",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        -- Agility 70, not 40: the rocks are upass_obstacles.rs2 [label,rockslide_obstacle] stat_random(agility, 160, 300),
        -- which slips back ~16% of the time at 40 (climbOverRocks FAILed in run hp_troll_2) and never at 68+.
        "::setlevel agility 70",
        "::setlevel thieving 60",
        -- Protect from Melee for the Troll General (needs Prayer 43; wiki Protect from Melee). Since the eat-delay port
        -- (raid branch, OSRS-Content 7936c59bf9) an eat no longer holds his hits: 26 sharks ran out with him at 1/30.
        -- 99 points cover ~495 ticks of the prayer's 1 point / 5 ticks drain at +0 prayer bonus (wiki Prayer).
        "::setlevel prayer 99",
        "::give rune_scimitar 1",
        "::wield rune_scimitar",
        "::give shark 26",
        "::give coins 12",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp317_troll_quest",
            constants = { not_started = 0, started = 10, defeated_dad = 20, entered_prison = 30, freed_godric = 40, complete = 50 },
            row = "quest_trollstronghold",
            display = "Troll Stronghold",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Door rule (docs/QUEST_ORCHESTRATOR.md; b69 re-drive): every goto leaves from and lands on open ground,
        -- and every closed space is entered and left by its own door, gate, stile, rock ridge, cave mouth or
        -- stair on every visit (static collision read with reach.py / comp.py, maps m44_55..m45_57, m44_157,
        -- m45_156). The route out of the prison at the end is the secret door (troll_stronghold_exit) and the
        -- secret path south over both rock pairs and Death Plateau's climbing rocks into Burthorpe.
        local function in_tenzing_house(tile) return tile.x >= 2819 and tile.x <= 2822 and tile.z >= 3554 and tile.z <= 3557 end
        local VITALS = { eat = "shark", below = 60 }

        ------------------------------------------------------------------
        -- talkToDenulth
        ------------------------------------------------------------------
        -- Lumbridge -> Burthorpe on foot: overland to the open ground east of Taverley's members' gate
        -- (reach.py 3206,3233 -> 2938,3450 REACH closed-doors len 515), the walk-through gate pressed
        -- (gates.rs2 [label,member_fencegate_try]; Taverley is x <= 2935), then overland to Denulth's
        -- open-air camp (reach.py 2934,3449 -> 2895,3529 REACH len 131; death.lua talks to him from 2896,3531).
        t.exec("goto-talkToDenulth.memberGate", t.player.goto_tile, 2938, 3450, 0)
        t.exec("talkToDenulth.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
            near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
            far_desc = "inside Taverley, x <= 2935" })
        t.exec("goto-talkToDenulth", t.player.goto_tile, 2896, 3531, 0)
        t.exec("talkToDenulth", t.player.talk_to, "death_ig_commander", 1)
        t.exec("talkToDenulth-dialog", t.chat.play, {
            "player:Hello",
            "npc:Welcome back friend",
            "choose:How goes your fight with the trolls?",
            "player:How goes your fight",
            "npc:bad news",
            "player:What happened?",
            "npc:*",
            "choose:Is there anything I can do to help?",
            "player:Is there anything",
            "npc:The way to the stronghold is treacherous",
            "choose:I'll get Godric back!",
            "player:I'll get Godric back",
            "npc:God speed friend",
        })
        t.chat.drain({})
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        ------------------------------------------------------------------
        -- travelToTenzing / buyClimbingBoots (12 coins)
        ------------------------------------------------------------------
        -- Tenzing's fenced yard (death_fencegate_l 2824,3555, east edge) and his house (death_sherpa_door
        -- 2822,3555; walk-through, no knock: ::complete quest_deathplateau sets %death_map = scouted_area,
        -- quest_cheat.rs2:305, death_doors_mechanism.rs2:31-36). The goto stops on the lane outside the gate.
        t.exec("goto-travelToTenzing", t.player.goto_tile, 2826, 3555, 0)
        t.exec("travelToTenzing.gateIn", t.player.pass_door, { closed = "death_fencegate_l", open = "death_openfencegate_l",
            at = { 2824, 3555, 0 }, near = { 2825, 3555 }, far = { 2823, 3555 } })
        t.exec("travelToTenzing.doorIn", t.player.cross_gate, { loc = "death_sherpa_door", at = { 2822, 3555, 0 },
            near = { 2823, 3555 }, far_ok = in_tenzing_house, far_desc = "inside Tenzing's house, x 2819-2822 z 3554-3557" })
        t.ticks(2)
        t.exec("buyClimbingBoots", t.player.talk_to, "death_sherpa", 1)
        t.exec("buyClimbingBoots-dialog", t.chat.play, {
            "player:Hello Tenzing",
            "npc:Hello again traveller",
            "choose:Can I buy some Climbing boots?",
            "player:Can I buy some Climbing boots",
            "npc:12 gold",
            "choose:OK, sounds good.",
            "player:OK, sounds good",
            "mesbox:Tenzing has given you some Climbing boots",
            "npc:Was there anything else",
            "choose:Nothing, thanks!",
            "player:Nothing, thanks",
        })
        t.chat.drain({})
        t.ticks(2)
        local boots_result, boots_count = t.inv.count("death_climbingboots")
        t.check("buyClimbingBoots.boots", boots_result == "ok" and boots_count == 1,
            "climbing boots in the backpack: " .. tostring(boots_count))
        t.exec("equipClimbingBoots", t.player.equip, "death_climbingboots")

        ------------------------------------------------------------------
        -- climbOverStile, climbOverRocks
        ------------------------------------------------------------------
        -- Out through the back door (death_sherpa_backdoor 2820,3557, walk-through past ^death_got_map) into the
        -- backyard, whose only other way out is the stile death_fullstyle 2817,3562-3563 (death.lua header).
        t.exec("climbOverStile.backDoor", t.player.cross_gate, { loc = "death_sherpa_backdoor", at = { 2820, 3557, 0 },
            near = { 2820, 3557 }, far_ok = function(tile) return tile.z >= 3558 end, far_desc = "in the backyard, z >= 3558" })
        -- The stile is stiles.rs2 [oploc1,_stile]: two bare p_teleports round a silent exactmove, too short a hop
        -- for click_loc's teleport settle (start-and-travel "A short hop (stiles)"), so the row is the tiles.
        t.exec("walk-climbOverStile", t.player.walk_to, 2817, 3561, 20)
        local stile_before_result, stile_before = t.world.tile()
        local stile_click, stile_detail = t.player.click_loc("death_fullstyle", 1, { at = { 2817, 3562, 0 } })
        t.ticks(5)
        local stile_after_result, stile_after = t.world.tile()
        t.check("climbOverStile", stile_before_result == "ok" and stile_before ~= nil and stile_before.z <= 3561
                and stile_after_result == "ok" and stile_after ~= nil and stile_after.level == 0 and stile_after.z >= 3564,
            "click_loc(death_fullstyle) -> " .. tostring(stile_click) .. " " .. tostring(stile_detail)
            .. "; tile " .. tostring(stile_before and (stile_before.x .. "," .. stile_before.z))
            .. " -> " .. tostring(stile_after and (stile_after.x .. "," .. stile_after.z)) .. " (want z >= 3564: north of the stile)")
        -- North of the stile to the first rocks is open ground (reach.py 2817,3564 -> 2856,3611 REACH len 86).
        -- troll_climbingrocks 2856,3612 is @rockslide_obstacle (upass_obstacles.rs2:31-58): from the south it ends
        -- one tile north of the loc, and the south approach needs the boots worn (quest_troll.rs2:16).
        t.exec("goto-climbOverRocks", t.player.goto_tile, 2856, 3611, 0)
        t.exec("climbOverRocks", t.player.cross_trap, { loc = "troll_climbingrocks", op_name = "Climb",
            at = { 2856, 3612, 0 }, src = { 2856, 3611 }, dest = { 2856, 3613 }, vitals = VITALS })

        ------------------------------------------------------------------
        -- enterArena (Dad's warning page)
        ------------------------------------------------------------------
        -- The path east to the arena crosses two rock ridges by their own ops (reach.py 2856,3613 -> 2896,3619:
        -- NEEDS-OP via troll_climbingrocks_top 2859,3626 and troll_climbingrocks_bottom 2878,3622). From the west,
        -- _top moves the player 3 east (quest_troll.rs2:22-48) and _bottom 2+1 east (quest_troll.rs2:50-77).
        t.exec("walk-enterArena.westRocks", t.player.walk_to, 2858, 3626, 40)
        t.exec("enterArena.westRocks", t.player.cross_trap, { loc = "troll_climbingrocks_top", op_name = "Climb",
            at = { 2859, 3626, 0 }, src = { 2858, 3626 }, dest = { 2861, 3626 }, vitals = VITALS })
        t.exec("walk-enterArena.eastRocks", t.player.walk_to, 2877, 3622, 40)
        t.exec("enterArena.eastRocks", t.player.cross_trap, { loc = "troll_climbingrocks_bottom", op_name = "Climb",
            at = { 2878, 3622, 0 }, src = { 2877, 3622 }, dest = { 2880, 3622 }, vitals = VITALS })
        t.exec("walk-enterArena", t.player.walk_to, 2896, 3619, 40)
        local arena_click, arena_detail = t.player.click_loc("troll_stronghold_arena_entrance_right", 1, { at = { 2897, 3619, 0 } })
        t.ticks(2)
        local warn_kind = t.chat.kind()
        local _, warn_text = t.chat.text()
        t.check("enterArena", arena_click == "ok" and warn_kind == "npc"
                and string.find(tostring(warn_text), "No human pass", 1, true) ~= nil,
            "click_loc -> " .. tostring(arena_click) .. " " .. tostring(arena_detail)
            .. "; chat kind=" .. tostring(warn_kind) .. " text=" .. tostring(warn_text))
        t.chat.drain({})
        t.chat.close()

        ------------------------------------------------------------------
        -- fightDad
        ------------------------------------------------------------------
        t.player.walk_to(2908, 3614, 40)
        local arena_walk_result, arena_walk_tile = t.world.tile()
        t.check("walkIntoArena", arena_walk_result == "ok" and arena_walk_tile ~= nil
                and arena_walk_tile.x >= 2900,
            "walked through the opened arena entrance to " .. tostring(arena_walk_tile and (arena_walk_tile.x .. "," .. arena_walk_tile.z)))
        t.exec("fightDad-talk", t.player.talk_to, "troll_champion", 1)
        t.exec("fightDad-dialog", t.chat.play, {
            "npc:What tiny human do in troll arena",
            "choose:I accept your challenge!",
            "player:I accept your challenge",
            "npc:Tiny human brave",
        })
        t.chat.drain({})
        t.chat.close()
        t.ticks(1)

        local surrendered = false
        local rounds = 0
        local ate = 0
        local dad_lowest = nil -- margin row below: the lowest Hitpoints read between attacks
        local seen = {}
        local last_attack_result = nil
        local last_attack_detail = nil
        while (not surrendered) and rounds < 160 do
            rounds = rounds + 1
            local hp_r, hp = t.skill.read("hitpoints")
            if hp_r == "ok" and type(hp) == "table" and hp.level ~= nil and (dad_lowest == nil or hp.level < dad_lowest) then
                dad_lowest = hp.level
            end
            if hp_r == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < 60 then
                t.player.inv_op("shark", 1)
                ate = ate + 1
            end
            local k0 = t.chat.kind()
            if k0 ~= "none" then
                surrendered = true
                seen[#seen + 1] = "before-attack:" .. k0
                break
            end
            last_attack_result, last_attack_detail = t.player.attack("troll_champion", 2, 3)
            t.ticks(2) -- let the attack click settle before the surrender page is looked for
            local k1 = t.chat.kind()
            if k1 ~= "none" then
                surrendered = true
                seen[#seen + 1] = "after-attack:" .. k1
            end
            local sv_r, sv = t.quest.stage()
            if sv_r == "ok" and sv ~= nil and sv >= 20 and not surrendered then
                seen[#seen + 1] = "stage>=20 with chat none at round " .. rounds
                break
            end
        end
        local surrender_result, surrender_detail = t.chat.play({
            "npc:Stop! You win",
            "choose:I'll be going now.",
            "player:I'll be going now",
        })
        t.check("fightDad", surrendered and surrender_result == "ok", "Dad's surrender page up: chat kind at loop end="
            .. tostring(surrendered) .. " " .. table.concat(seen, ";") .. " after " .. rounds .. " rounds, sharks eaten "
            .. ate .. "; last attack " .. tostring(last_attack_result) .. " " .. tostring(last_attack_detail) .. "; surrender chat -> " .. tostring(surrender_result) .. " " .. tostring(surrender_detail))
        t.chat.close()
        t.ticks(3)
        t.expect("quest.stage.defeated_dad", t.quest.expect_stage("defeated_dad"))
        local _, sharks_after_dad = t.inv.count("shark")
        t.check("fightDad-margin", dad_lowest ~= nil and dad_lowest * 4 >= 99 and (sharks_after_dad or 0) >= 1,
            "lowest hp read in the fight " .. tostring(dad_lowest) .. "/99, sharks eaten " .. ate .. ", left "
            .. tostring(sharks_after_dad) .. " (margin: lowest hp >= a quarter of 99 AND food left)")

        ------------------------------------------------------------------
        -- leaveArena, enterArenaCavern, leaveArenaCavern, enterStronghold
        ------------------------------------------------------------------
        -- The arena's north gate (troll_stronghold_arena_exit_left 2916,3629, south edge; door_selfstage_open once
        -- %troll_quest >= defeated_dad, quest_troll.rs2:100-112) opens into a 100-tile walled pocket whose only other
        -- way out is the cave mouth troll_pass_entrance 2903,3644 (comp.py 2916,3631: doors on edge = the two gate
        -- leaves only). The cave's west-angle copies teleport 2907,10019 in and 2908,3654 out (quest_troll.rs2:127-133).
        t.exec("walk-leaveArena", t.player.walk_to, 2916, 3628, 40)
        t.exec("leaveArena", t.player.pass_door, { closed = "troll_stronghold_arena_exit_left",
            open = "troll_stronghold_arena_exit_left", at = { 2916, 3629, 0 }, near = { 2916, 3628 }, far = { 2916, 3631 } })
        t.exec("walk-enterArenaCavern", t.player.walk_to, 2903, 3643, 30)
        t.exec("enterArenaCavern", t.player.climb, { loc = "troll_pass_entrance", op = 1, op_name = "Enter",
            at = { 2903, 3644, 0 }, src = { 2903, 3643 }, dest = { 2907, 10019, 0 } })
        t.ticks(3)
        t.exec("walk-leaveArenaCavern", t.player.walk_to, 2906, 10035, 40)
        t.exec("leaveArenaCavern", t.player.climb, { loc = "troll_pass_exit", op = 1, op_name = "Exit",
            at = { 2906, 10036, 0 }, src = { 2906, 10035 }, dest = { 2908, 3654, 0 } })
        t.ticks(3)
        -- The cave's north mouth and the stronghold's front door share Trollheim's summit component (reach.py
        -- 2908,3654 -> 2840,3690 REACH closed-doors len 208): an overland hop to the open tile beside the door.
        t.exec("goto-enterStronghold", t.player.goto_tile, 2840, 3690, 0)
        t.exec("enterStronghold", t.player.climb, { loc = "troll_stronghold_door", op = 1, op_name = "Enter",
            at = { 2839, 3689, 0 }, dest = { 2837, 10090, 2 } })

        ------------------------------------------------------------------
        -- killGeneral: the prison key is a ground drop
        ------------------------------------------------------------------
        t.ticks(8) -- the scene the door teleport built settles before any npc slot is trusted
        -- Protect from Melee before the generals' hall (recipe: verbs-combat.md "Turning on a protection prayer"): the
        -- three generals are aggressive slash fighters (troll_general*: damagetype 1, attackrate 4, strength 140 +
        -- strengthbonus 100, combat_stats.generated.npc), and a prayed npc melee hit is 0 (combat_stats.rs2 playerhit_n_melee_apply).
        do
            local tab_result = t.ui.tab("prayer")
            t.ticks(2)
            local wr, w = t.ui.widget("prayerbook:prayer15")
            t.ui.invoke(w, 1)
            t.ticks(2)
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check("killGeneral-protectMelee", tab_result == "ok" and wr == "ok" and on == 1, "varb4118_prayer_protectfrommelee " .. tostring(on) .. "; prayer skill " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        local _, sharks_at_general = t.inv.count("shark")
        t.exec("goto-killGeneral", t.player.goto_tile, 2835, 10088, 2)
        t.ticks(4)
        local have_prison_key = false
        local general_symbols = { "troll_general", "troll_general2", "troll_general3" }
        local general_tries = 0
        local general_notes = {}
        local general_lowest, general_ticks = nil, 0 -- margin row below: lowest hp and ticks over every general fought
        while (not have_prison_key) and general_tries < 6 do
            general_tries = general_tries + 1
            local sym = general_symbols[((general_tries - 1) % 3) + 1]
            local attack_result = t.player.attack(sym, 2, 4)
            if attack_result == "ok" then
                -- eat below 75, not 90: a shark heals 20, so below 90 spent food on hits the prayer already stops
                local dead_result, dead_detail = t.npc.await_dead_engaged(240, 40, { eat = { item = "shark", below = 75 } })
                local lowest_here = tonumber(tostring(dead_detail):match("lowest hp (%d+)/"))
                if lowest_here and (general_lowest == nil or lowest_here < general_lowest) then general_lowest = lowest_here end
                general_ticks = general_ticks + (tonumber(tostring(dead_detail):match("dead after (%d+) tick")) or 0)
                general_notes[#general_notes + 1] = sym .. " attack=" .. tostring(attack_result) .. " dead=" .. tostring(dead_result)
                if dead_result == "ok" then
                    t.ticks(4)
                    t.player.click_obj("troll_key_prison", 3)
                    t.ticks(3)
                    local key_result, key_has = t.inv.has("troll_key_prison")
                    have_prison_key = (key_result == "ok" and key_has)
                end
            else
                general_notes[#general_notes + 1] = sym .. " attack=" .. tostring(attack_result)
            end
        end
        t.check("killGeneral", have_prison_key, "troll_key_prison in the backpack after " .. general_tries
            .. " general(s): " .. table.concat(general_notes, "; "))
        local _, sharks_after_general = t.inv.count("shark")
        local _, hp_after_general = t.skill.read("hitpoints")
        t.check("killGeneral-margin", general_lowest ~= nil and general_lowest * 4 >= 99 and (sharks_after_general or 0) >= 1,
            "sharks staged 26, at the generals " .. tostring(sharks_at_general) .. ", eaten in the fight "
            .. tostring((sharks_at_general or 0) - (sharks_after_general or 0)) .. ", left " .. tostring(sharks_after_general)
            .. ", lowest hp in the fight " .. tostring(general_lowest) .. "/99, hp after "
            .. tostring(type(hp_after_general) == "table" and (hp_after_general.current or hp_after_general.level) or hp_after_general)
            .. ", " .. general_ticks .. " tick(s) of general fighting (margin: lowest hp >= a quarter of 99 AND food left)")

        ------------------------------------------------------------------
        -- goDownInStronghold, goThroughPrisonDoor, goDownToPrison
        ------------------------------------------------------------------
        t.exec("goto-goDownInStronghold", t.player.goto_tile, 2844, 10107, 2)
        t.exec("goDownInStronghold", t.player.click_loc, "troll_stronghold_stairstop", 1)
        t.ticks(3)
        t.exec("goThroughPrisonDoor", t.player.click_loc, "troll_stronghold_prison_door_closed", 1)
        t.ticks(3)
        t.expect("quest.stage.entered_prison", t.quest.expect_stage("entered_prison"))
        t.exec("goDownToPrison", t.player.click_loc, "troll_stronghold_stairstop", 1)
        t.ticks(3)

        ------------------------------------------------------------------
        -- getBerryKey, freeEadgar (pickpocket, else kill the awake guard)
        ------------------------------------------------------------------
        -- A pickpocket that fails wakes the guard (troll_stronghold_camp_guard.rs2); the awake guard is then fought.
        -- Every such fight lands in the guards' margin row after freeGodric.
        local guard_fights, guard_lowest = 0, nil
        local function guard_fight(detail)
            guard_fights = guard_fights + 1
            local lowest_here = tonumber(tostring(detail):match("lowest hp (%d+)/"))
            if lowest_here and (guard_lowest == nil or lowest_here < guard_lowest) then guard_lowest = lowest_here end
        end
        t.exec("goto-getBerryKey", t.player.goto_tile, 2834, 10084, 0)
        local berry_got = false
        local berry_tries = 0
        while (not berry_got) and berry_tries < 15 do
            berry_tries = berry_tries + 1
            t.player.press("troll_prison_guard2", 3, 8)
            t.ticks(2)
            local kr, kh = t.inv.has("troll_key_eadgar")
            berry_got = (kr == "ok" and kh)
            if (not berry_got) and t.chat.kind() ~= "none" then t.chat.close() end
            if (not berry_got) and berry_tries >= 3 and t.npc.nearest("troll_prison_guard2_awake", 6) ~= nil then
                local ar = t.player.attack("troll_prison_guard2_awake", 2, 8)
                if ar == "ok" or ar == "timeout" then
                    local _, guard_detail = t.npc.await_dead_engaged(200, 40, { eat = { item = "shark", below = 90 } })
                    guard_fight(guard_detail)
                    t.ticks(4)
                    t.player.click_obj("troll_key_eadgar", 3)
                    t.ticks(3)
                    local kr2, kh2 = t.inv.has("troll_key_eadgar")
                    berry_got = (kr2 == "ok" and kh2)
                end
            end
        end
        t.check("getBerryKey", berry_got, "troll_key_eadgar in the backpack after " .. berry_tries .. " attempt(s)")
        t.exec("goto-freeEadgar", t.player.goto_tile, 2834, 10082, 0)
        t.exec("freeEadgar", t.player.click_loc, "troll_celldoor_eadgar", 1)
        t.exec("freeEadgar-dialog", t.chat.play, { "npc:Thanks" })
        t.chat.close()
        t.ticks(2)

        ------------------------------------------------------------------
        -- getTwigKey, freeGodric
        ------------------------------------------------------------------
        t.exec("goto-getTwigKey", t.player.goto_tile, 2834, 10080, 0)
        local twig_got = false
        local twig_tries = 0
        while (not twig_got) and twig_tries < 15 do
            twig_tries = twig_tries + 1
            t.player.press("troll_prison_guard1", 3, 8)
            t.ticks(2)
            local kr, kh = t.inv.has("troll_key_godric")
            twig_got = (kr == "ok" and kh)
            if (not twig_got) and t.chat.kind() ~= "none" then t.chat.close() end
            if (not twig_got) and twig_tries >= 3 and t.npc.nearest("troll_prison_guard1_awake", 6) ~= nil then
                local ar = t.player.attack("troll_prison_guard1_awake", 2, 8)
                if ar == "ok" or ar == "timeout" then
                    local _, guard_detail = t.npc.await_dead_engaged(200, 40, { eat = { item = "shark", below = 90 } })
                    guard_fight(guard_detail)
                    t.ticks(4)
                    t.player.click_obj("troll_key_godric", 3)
                    t.ticks(3)
                    local kr2, kh2 = t.inv.has("troll_key_godric")
                    twig_got = (kr2 == "ok" and kh2)
                end
            end
        end
        t.check("getTwigKey", twig_got, "troll_key_godric in the backpack after " .. twig_tries .. " attempt(s)")
        t.exec("goto-freeGodric", t.player.goto_tile, 2834, 10078, 0)
        t.exec("freeGodric", t.player.click_loc, "troll_celldoor_godric", 1)
        t.exec("freeGodric-dialog", t.chat.play, { "npc:Thank you" })
        t.chat.close()
        t.ticks(2)
        t.expect("quest.stage.freed_godric", t.quest.expect_stage("freed_godric"))
        if guard_fights > 0 then
            local _, sharks_after_guards = t.inv.count("shark")
            t.check("prisonGuards-margin", guard_lowest ~= nil and guard_lowest * 4 >= 99 and (sharks_after_guards or 0) >= 1,
                guard_fights .. " awake guard fight(s), lowest hp " .. tostring(guard_lowest) .. "/99, sharks left "
                .. tostring(sharks_after_guards) .. " (margin: lowest hp >= a quarter of 99 AND food left)")
        end

        ------------------------------------------------------------------
        -- goToDunstan
        ------------------------------------------------------------------
        -- Out of the prison on foot (the guide names no way back; a goto out of the dungeon is a cheat): the secret
        -- door troll_stronghold_exit (2823-2825,10048-10049, op Open; quest_troll.rs2:122-125 p_teleport 2827,3646,
        -- no gate) from the corridor tile north of it (reach.py 2834,10078 -> 2823,10050 REACH len 41); down the
        -- ledge to the second rock pair (troll_climbingrocks 2834,3628, @rockslide_obstacle: from the north it ends
        -- one tile south), round to the first pair from the north (2856,3612 -> 2856,3611), along the secret path to
        -- Death Plateau's climbing rocks (death_climbingrocks_top 2880,3595: worn climbing boots, exactmove 3 south;
        -- death_locs.rs2:40-48), then Burthorpe is one walking component (reach.py 2880,3592 -> 2921,3569 REACH
        -- len 184) to the street south of Dunstan's house; his door poordoor 2921,3571 (death.lua dunstan_in).
        t.exec("walk-goToDunstan.secretDoor", t.player.walk_to, 2823, 10050, 60)
        t.exec("goToDunstan.secretDoor", t.player.climb, { loc = "troll_stronghold_exit", op = 1, op_name = "Open",
            at = { 2823, 10048, 0 }, src = { 2823, 10050 }, dest = { 2827, 3646, 0 } })
        t.ticks(3)
        t.exec("walk-goToDunstan.northRocks", t.player.walk_to, 2834, 3629, 40)
        t.exec("goToDunstan.northRocks", t.player.cross_trap, { loc = "troll_climbingrocks", op_name = "Climb",
            at = { 2834, 3628, 0 }, src = { 2834, 3629 }, dest = { 2834, 3627 }, vitals = VITALS })
        t.exec("walk-goToDunstan.southRocks", t.player.walk_to, 2856, 3613, 60)
        t.exec("goToDunstan.southRocks", t.player.cross_trap, { loc = "troll_climbingrocks", op_name = "Climb",
            at = { 2856, 3612, 0 }, src = { 2856, 3613 }, dest = { 2856, 3611 }, vitals = VITALS })
        -- In hops: the scene built round the secret door's landing ends at x 2879, and drive.move_to only routes to a
        -- tile the client has loaded (run 1: walk_to 2880,3596 from 2856,3611 refused move_to). reach.py 2856,3611 ->
        -- 2863,3609 -> 2876,3604 -> 2880,3596 REACH closed-doors len 9 / 18 / 12.
        t.exec("walk-goToDunstan.plateauPath", t.player.walk_to, 2863, 3609, 30)
        t.exec("walk-goToDunstan.plateauPath2", t.player.walk_to, 2876, 3604, 40)
        t.ticks(2)
        t.exec("walk-goToDunstan.plateauRocks", t.player.walk_to, 2880, 3596, 40)
        t.exec("goToDunstan.plateauRocks", t.player.cross_trap, { loc = "death_climbingrocks_top", op_name = "Climb",
            at = { 2880, 3595, 0 }, src = { 2880, 3596 }, dest = { 2880, 3593 }, vitals = VITALS })
        t.exec("goto-goToDunstan", t.player.goto_tile, 2921, 3569, 0)
        t.exec("goToDunstan.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2921, 3571, 0 }, near = { 2921, 3571 }, far = { 2921, 3572 } })
        t.exec("goToDunstan", t.player.talk_to, "death_smithy", 1)
        t.exec("goToDunstan-dialog", t.chat.play, {
            "player:Has Godric returned home?",
            "npc:He is safe and sound",
            "player:I'm glad to hear it",
            "npc:I have very little",
        })
        t.ticks(2)
        t.chat.drain({})
        t.quest.expect_complete()
        local talisman_result, talisman_count = t.inv.count("law_talisman")
        t.check("reward.law_talisman", talisman_result == "ok" and talisman_count == 1,
            "law_talisman in the backpack: " .. tostring(talisman_count))
        t.finish(0)
    end,
}
