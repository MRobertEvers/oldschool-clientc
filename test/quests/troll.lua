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
        "::setlevel agility 40",
        "::setlevel thieving 60",
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

        ------------------------------------------------------------------
        -- talkToDenulth
        ------------------------------------------------------------------
        t.exec("goto-talkToDenulth", t.player.goto_tile, 2895, 3529, 0)
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
        t.exec("goto-travelToTenzing", t.player.goto_tile, 2820, 3556, 0)
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
        t.exec("goto-climbOverStile", t.player.goto_tile, 2817, 3562, 0)
        local stile_before_result, stile_before = t.world.tile()
        local stile_click, stile_detail = t.player.click_loc("death_fullstyle", 1)
        t.ticks(5)
        local stile_after_result, stile_after = t.world.tile()
        t.check("climbOverStile", (stile_click == "ok" or stile_click == "timeout") and stile_after_result == "ok" and stile_after ~= nil
                and stile_before ~= nil and (stile_after.x ~= stile_before.x or stile_after.z ~= stile_before.z),
            "click_loc -> " .. tostring(stile_click) .. " " .. tostring(stile_detail)
            .. "; tile " .. tostring(stile_before and (stile_before.x .. "," .. stile_before.z))
            .. " -> " .. tostring(stile_after and (stile_after.x .. "," .. stile_after.z)))
        t.exec("goto-climbOverRocks", t.player.goto_tile, 2856, 3611, 0)
        local rocks_before_result, rocks_before = t.world.tile()
        local rocks_click, rocks_detail = t.player.click_loc("troll_climbingrocks", 1)
        t.ticks(6)
        local rocks_after_result, rocks_after = t.world.tile()
        t.check("climbOverRocks", rocks_click == "ok" and rocks_after_result == "ok" and rocks_after ~= nil
                and rocks_before ~= nil and (rocks_after.x ~= rocks_before.x or rocks_after.z ~= rocks_before.z),
            "click_loc -> " .. tostring(rocks_click) .. " " .. tostring(rocks_detail)
            .. "; tile " .. tostring(rocks_before and (rocks_before.x .. "," .. rocks_before.z))
            .. " -> " .. tostring(rocks_after and (rocks_after.x .. "," .. rocks_after.z)))

        ------------------------------------------------------------------
        -- enterArena (Dad's warning page)
        ------------------------------------------------------------------
        t.exec("goto-enterArena", t.player.goto_tile, 2896, 3619, 0)
        local arena_click, arena_detail = t.player.click_loc("troll_stronghold_arena_entrance_right", 1)
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
        t.exec("goto-fightDad", t.player.goto_tile, 2908, 3614, 0)
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
        local seen = {}
        local last_attack_result = nil
        local last_attack_detail = nil
        while (not surrendered) and rounds < 160 do
            rounds = rounds + 1
            local hp_r, hp = t.skill.read("hitpoints")
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

        ------------------------------------------------------------------
        -- leaveArena, enterArenaCavern, leaveArenaCavern, enterStronghold
        ------------------------------------------------------------------
        t.exec("goto-leaveArena", t.player.goto_tile, 2915, 3628, 0)
        local exit_click, exit_detail = t.player.click_loc("troll_stronghold_arena_exit_left", 1)
        t.ticks(2)
        local exit_tile_result, exit_tile = t.world.tile()
        t.check("leaveArena", exit_click == "ok" and exit_tile_result == "ok" and exit_tile ~= nil,
            "click_loc -> " .. tostring(exit_click) .. " " .. tostring(exit_detail)
            .. "; tile " .. tostring(exit_tile and (exit_tile.x .. "," .. exit_tile.z)))
        t.exec("goto-enterArenaCavern", t.player.goto_tile, 2904, 3644, 0)
        local cave_before_result, cave_before = t.world.tile()
        local cave_click, cave_detail = t.player.click_loc("troll_pass_entrance", 1)
        t.ticks(3)
        local cave_after_result, cave_after = t.world.tile()
        t.check("enterArenaCavern", cave_click == "ok" and cave_after_result == "ok" and cave_after ~= nil
                and cave_before ~= nil and (cave_after.z ~= cave_before.z or cave_after.x ~= cave_before.x),
            "click_loc -> " .. tostring(cave_click) .. " " .. tostring(cave_detail)
            .. "; tile " .. tostring(cave_before and (cave_before.x .. "," .. cave_before.z))
            .. " -> " .. tostring(cave_after and (cave_after.x .. "," .. cave_after.z)))
        t.exec("goto-leaveArenaCavern", t.player.goto_tile, 2907, 10036, 0)
        local pass_before_result, pass_before = t.world.tile()
        local pass_click, pass_detail = t.player.click_loc("troll_pass_exit", 1)
        t.ticks(3)
        local pass_after_result, pass_after = t.world.tile()
        t.check("leaveArenaCavern", pass_click == "ok" and pass_after_result == "ok" and pass_after ~= nil
                and pass_before ~= nil and (pass_after.z ~= pass_before.z or pass_after.x ~= pass_before.x),
            "click_loc -> " .. tostring(pass_click) .. " " .. tostring(pass_detail)
            .. "; tile " .. tostring(pass_before and (pass_before.x .. "," .. pass_before.z))
            .. " -> " .. tostring(pass_after and (pass_after.x .. "," .. pass_after.z)))
        t.exec("goto-enterStronghold", t.player.goto_tile, 2839, 3689, 0)
        local door_before_result, door_before = t.world.tile()
        local door_click, door_detail = t.player.click_loc("troll_stronghold_door", 1)
        t.ticks(3)
        local door_after_result, door_after = t.world.tile()
        t.check("enterStronghold", door_click == "ok" and door_after_result == "ok" and door_after ~= nil
                and door_before ~= nil and (door_after.z ~= door_before.z or door_after.x ~= door_before.x),
            "click_loc -> " .. tostring(door_click) .. " " .. tostring(door_detail)
            .. "; tile " .. tostring(door_before and (door_before.x .. "," .. door_before.z))
            .. " -> " .. tostring(door_after and (door_after.x .. "," .. door_after.z)))

        ------------------------------------------------------------------
        -- killGeneral: the prison key is a ground drop
        ------------------------------------------------------------------
        t.ticks(8) -- the scene the door teleport built settles before any npc slot is trusted
        t.exec("goto-killGeneral", t.player.goto_tile, 2835, 10088, 2)
        t.ticks(4)
        local have_prison_key = false
        local general_symbols = { "troll_general", "troll_general2", "troll_general3" }
        local general_tries = 0
        local general_notes = {}
        while (not have_prison_key) and general_tries < 6 do
            general_tries = general_tries + 1
            local sym = general_symbols[((general_tries - 1) % 3) + 1]
            local attack_result = t.player.attack(sym, 2, 4)
            if attack_result == "ok" then
                local dead_result = t.npc.await_dead_engaged(240, 40, { eat = { item = "shark", below = 90 } })
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
                    t.npc.await_dead_engaged(200, 40, { eat = { item = "shark", below = 90 } })
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
                    t.npc.await_dead_engaged(200, 40, { eat = { item = "shark", below = 90 } })
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

        ------------------------------------------------------------------
        -- goToDunstan
        ------------------------------------------------------------------
        t.exec("goto-goToDunstan", t.player.goto_tile, 2919, 3575, 0)
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
