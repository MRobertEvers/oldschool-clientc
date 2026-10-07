-- Land of the Goblins -- full client-driven run of Quest Helper's LandOfTheGoblins.java (79 steps,
-- states 0..56), written b73 together with the quest's content (quest_landofthegoblins/, see
-- build/orchestrator/fix_b73/landofthegoblins.progress.md for every content file and its source).
--
-- Door rule. Every staircase, trapdoor, ladder, door, cave entrance, mud pile and members' gate on the
-- way is pressed (climb / pass_door / cross_gate); the enclave guards are passed by their own "Pass"
-- op. The gotos only hop between open tiles of one overland region or one city floor:
--   * Lumbridge and Falador side <-> Kandarin side only through Taverley's south members' gate
--     (membergatel 2934,3320), pressed both ways;
--   * the Goblin Cave, the Goblin Temple, the crypt, the Hemenster competition area, Aggie's house and
--     the Makeover Mage's house are entered and left by their own locs on every visit;
--   * the walks inside the cave are walk_route hops (Zanik follows; a goto would leave her behind).
-- Known content shortcut kept (reported): Dorgesh-Kaan's mines door lands inside Oldak's lab
-- (lotg_intro.rs2 [oploc1,cave_goblin_city_doorr]), as Another Slice of H.A.M.'s green test needs.
--
-- Stats: the four requirements (Land_of_the_Goblins oldid 15363325: Agility 38, Fishing 40,
-- Thieving 45, Herblore 48). Combat staged as a margin for the five crypt priests (Quest Helper
-- recommends combat 65; Strongbones is level 184): no LOTG script reads a combat stat or the combat
-- level (grep quest_landofthegoblins/scripts: stat_base agility/fishing/thieving/herblore,
-- stat(thieving) 45 and stat(fishing) 40 only).

local function at(t)
    local r, h = t.world.tile()
    if r ~= "ok" or type(h) ~= "table" then
        return tostring(h)
    end
    return h.x .. "," .. h.z .. "," .. h.level
end

local function near(t, name, x, z, level, slack, why)
    local r, h = t.world.tile()
    local ok = r == "ok" and type(h) == "table" and h.level == level and math.abs(h.x - x) <= slack and math.abs(h.z - z) <= slack
    t.check(name, ok, "at " .. at(t) .. " (want within " .. slack .. " of " .. x .. "," .. z .. "," .. level .. (why and ("; " .. why) or "") .. ")")
end

-- A menu walk: talk, then for each answer drain to the menu and choose it, then drain the tail.
local function menu(t, name, choices)
    for i, c in ipairs(choices) do
        t.exec(name .. "-ask" .. i, t.chat.drain, { stop_at = "options", max_pages = 30 })
        t.exec(name .. "-choose" .. i, t.chat.choose, c)
    end
    t.exec(name .. "-tail", t.chat.drain, { max_pages = 30 })
end

-- Taverley's south members' gate (gates.rs2 [label,member_fencegate_try]): the only way on foot
-- between Kandarin (the Goblin Cave, Hemenster) and the Falador/Lumbridge side (the Makeover Mage,
-- Draynor, Lumbridge). reach.py: 2623,3391 -> 2925,3323 NEEDS-DOOR via membergater@2933,3320.
local function gate_south(t, step)
    t.exec("goto-" .. step, t.player.goto_tile, 2934, 3322, 0)
    t.exec(step, t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 }, near = { 2934, 3322 },
        far_ok = function(tile) return tile.z <= 3319 and math.abs(tile.x - 2934) <= 2 end,
        far_desc = "south of the members' gate, z <= 3319" })
end

local function gate_north(t, step)
    t.exec("goto-" .. step, t.player.goto_tile, 2934, 3318, 0)
    t.exec(step, t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 }, near = { 2934, 3318 },
        far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
        far_desc = "north of the members' gate, z >= 3320" })
end

-- The Goblin Cave: in by the cave entrance (mcannon_cave_guard.rs2 [oploc1,mcannoncave]
-- p_telejump(0_40_153_60_5)), out by the mud pile ([oploc1,mcanmudpile] p_telejump(0_40_52_63_63)).
local function cave_in(t, step)
    t.exec("goto-" .. step, t.player.goto_tile, 2623, 3391, 0)
    t.exec(step, t.player.climb, { loc = "mcannoncave", op = 1, op_name = "Enter",
        at = { 2622, 3392, 0 }, dest = { 2620, 9797, 0 } })
end

local function cave_out(t, step)
    t.exec(step, t.player.climb, { loc = "mcanmudpile", op = 1, op_name = "Climb-over",
        at = { 2621, 9796, 0 }, dest = { 2623, 3391, 0 } })
end

-- The cave's own floor between its entrance and the temple stairs (92 tiles; reach.py REACH
-- closed-doors, hops from a BFS of maps/m40_153).
local TO_GUARDS = { { 2615, 9800 }, { 2612, 9805 }, { 2612, 9813 }, { 2609, 9818 }, { 2607, 9824 }, { 2606, 9831 },
    { 2603, 9836 }, { 2595, 9836 }, { 2588, 9837 }, { 2581, 9838 }, { 2581, 9846 }, { 2581, 9849 } }
local TO_MUDPILE = { { 2582, 9844 }, { 2584, 9838 }, { 2590, 9836 }, { 2598, 9836 }, { 2606, 9836 }, { 2607, 9829 },
    { 2610, 9824 }, { 2611, 9817 }, { 2614, 9812 }, { 2616, 9806 }, { 2618, 9800 }, { 2620, 9797 } }

-- Lumbridge castle cellar -> Dorgeshuun Mines -> Dorgesh-Kaan (The Lost Tribe's route; Quest Helper
-- getToMine: goDownIntoBasement, climbThroughHole, talkToKazgar).
local function to_mines(t, p)
    t.exec("walk-" .. p .. "goDownIntoBasement", t.player.walk_to, 3209, 3215)
    t.exec(p .. "goDownIntoBasement", t.player.climb, { loc = "qip_cook_trapdoor_open", op = 1, op_name = "Climb-down",
        at = { 3209, 3216, 0 }, dest = { 3210, 9616, 0 }, slack = 1 })
    -- losttribe.rs2 [oploc1,lost_tribe_cavewall_hole_walldecor] p_teleport(^lt_tunnel_enter)
    t.exec(p .. "climbThroughHole", t.player.click_loc, "lost_tribe_cavewall_hole_walldecor", 1)
    t.ticks(3)
    near(t, p .. "climbThroughHole.here", 3221, 9618, 0, 1)
    t.exec(p .. "talkToKazgar", t.player.talk_to, "lost_tribe_guide_2ops", 1)
    t.exec(p .. "talkToKazgar-dialog", t.chat.play, {
        "npc:Hello friend",
        "options",
        "choose:Can you show me the way to the mines?",
        "player:Can you show me the way to the mines?",
        "npc:Certainly",
    })
    t.ticks(4)
    local _, k = t.world.tile()
    t.check(p .. "talkToKazgar.landed", type(k) == "table" and k.x >= 3300, "at " .. at(t) .. " (want the Dorgeshuun Mines, x >= 3300)")
end

local function prayer_melee(t, name, want)
    t.ui.tab("prayer")
    t.ticks(2)
    local _, on0 = t.var.varbit("varb4118_prayer_protectfrommelee")
    if (on0 == 1) ~= want then
        local _, w = t.ui.widget("prayerbook:prayer15")
        t.ui.invoke(w, 1)
        t.ticks(2)
    end
    local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
    local _, pr = t.skill.read("prayer")
    t.check(name, (on == 1) == want, "varb4118_prayer_protectfrommelee " .. tostring(on) .. " (want " .. tostring(want) .. "); prayer "
        .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
    t.ui.tab("inventory")
end

-- One crypt priest: say his name at his grave, fight him, then ask the defeated priest.
local function priest(t, n, grave, name, sym, ask, stage_after)
    t.exec("sayName" .. name, t.player.click_loc, grave, 1)
    if n > 1 then
        t.exec("sayName" .. name .. "-choose", t.chat.choose, name .. ".")
    end
    t.ticks(2)
    t.exec("sayName" .. name .. ".raised", t.npc.await_present, sym, 10, 6)
    local _, food0 = t.inv.count("shark")
    local _, hp0 = t.skill.read("hitpoints")
    t.exec("defeat" .. name, t.player.attack, sym, 2, 30)
    local _, d = t.exec("defeat" .. name .. ".dead", t.npc.await_dead_engaged, 600, 40, { eat = { item = "shark", below = 45 } })
    local lowest = tonumber(tostring(d):match("lowest hp (%d+)/"))
    local _, food1 = t.inv.count("shark")
    local max = (type(hp0) == "table" and hp0.base_level) or 0
    t.check("defeat" .. name .. ".margin", lowest ~= nil and max > 0 and lowest * 4 >= max and (food1 or 0) >= 1,
        "lowest hp " .. tostring(lowest) .. "/" .. tostring(max) .. ", sharks " .. tostring(food0) .. " -> " .. tostring(food1)
        .. " (margin: lowest >= a quarter of max AND food left)")
    t.ticks(3)
    local husk = sym .. "_defeated"
    t.exec("learn" .. name, t.player.talk_to, husk, 1)
    menu(t, "learn" .. name, { ask, "Goodbye." })
    t.ticks(2)
    t.expect("quest.stage." .. stage_after, t.quest.expect_stage(stage_after))
end

return {
    id = "landofthegoblins",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::setlevel agility 38",
        "::setlevel fishing 40",
        "::setlevel thieving 45",
        "::setlevel herblore 48",
        "::setlevel attack 75",
        "::setlevel strength 75",
        "::setlevel defence 75",
        "::setlevel hitpoints 80",
        "::setlevel prayer 70",
        "::complete quest_losttribe",
        "::complete quest_deathtothedorgeshuun",
        "::complete quest_giantdwarf",
        "::complete quest_digsite",
        "::complete quest_anothersliceofham",
        "::complete quest_fishingcontest",
        "::give bullseye_lantern_lit 1",
        "::give toadflaxvial 1",
        "::give vial_empty 1",
        "::give pestle_and_mortar 1",
        "::give fishing_rod 1",
        "::give mort_slimey_eel 1",
        "::give coins 5",
        "::give yellowdye 1",
        "::give bluedye 1",
        "::give orangedye 1",
        "::give purpledye 1",
        "::give abyssal_whip 1",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 8",
        "::give 4doseprayerrestore 3",
    },
    bind = {
        varp = "varb13599_lotg",
        constants = {
            not_started = 0, recruited = 2, dream = 4, dream_told = 6, grubfoot_gone = 8, cave = 10,
            refused = 12, mage_hint = 14, recipe = 16, transformed = 18, named = 20, in_temple = 22,
            heard_of_cell = 24, found_zanik = 26, zanik_escaped = 28, test_passed = 30, yubiusk_asked = 32,
            keys = 34, crypt_open = 36, in_crypt = 38, snailfeet = 40, mosschin = 42, redeyes = 44,
            strongbones = 46, back_to_oldak = 48, fungus_ring = 50, yubiusk = 52, complete = 56,
        },
        row = "quest_landofthegoblins",
        display = "Land of the Goblins",
        points = 2,
    },
    legs = {
        -- Leg 1: Grubfoot's dream (Quest Helper steps 0..8 -> 10).
        { name = "dream", run = function(t)
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
            local _, potion0 = t.inv.count("toadflaxvial")
            local _, sharks0 = t.inv.count("shark")
            t.check("setup.kit", potion0 == 1 and sharks0 == 8, "toadflax potion (unf) " .. tostring(potion0)
                .. ", sharks " .. tostring(sharks0) .. "; dyes, vial, pestle, rod, slimy eel, 5 coins, lantern, rune kit carried")
            to_mines(t, "")
            -- talkToGrubfoot: Grubfoot and Mistag at the city door (lotg_intro.rs2 @lotg_grubfoot_offer)
            t.exec("talkToGrubfoot", t.player.talk_to, "lotg_grubfoot_at_entrance", 1)
            menu(t, "talkToGrubfoot", { "Yes." })
            t.ticks(2)
            t.expect("quest.stage.recruited", t.quest.expect_stage("recruited"))
            t.exec("talkToGrubfoot.follows", t.npc.await_present, "lotg_grubfoot_follower", 6, 6)
            -- enterDorgeshKaan: lotg_intro.rs2 [oploc1,cave_goblin_city_doorr] (mines frame 1 -> city frame 0)
            t.exec("enterDorgeshKaan", t.player.climb, { loc = "cave_goblin_city_doorr", op = 1, op_name = "Open",
                at = { 3317, 9601, 0 }, dest = { 2704, 5365, 0 } })
            t.exec("enterDorgeshKaan.grubfoot", t.npc.await_present, "lotg_grubfoot_follower", 6, 6)
            -- talkToZanik: the lab scene, the dream, Oldak's sphere (lotg_intro.rs2 @lotg_lab_scene)
            t.exec("talkToZanik", t.player.talk_to, "lotg_zanik_in_lab", 1)
            menu(t, "talkToZanik", { "So why have you come to talk to Zanik?", "What was this new dream?",
                "I think it must mean something.", "I'm ready." })
            t.ticks(3)
            t.expect("quest.stage.cave", t.quest.expect_stage("cave"))
            near(t, "talkToZanik.sphere", 2620, 9797, 0, 1, "the Goblin Cave entrance, Oldak's sphere")
        end },

        -- Leg 2: Imposter among goblins -- the guards, the Makeover Mage, the potion (10 -> 16).
        { name = "potion", run = function(t)
            t.exec("talkToZanikGoblinCave", t.player.talk_to, "lotg_zanik_outside_temple", 1)
            menu(t, "talkToZanikGoblinCave", { "Follow me." })
            t.exec("talkToZanikGoblinCave.follows", t.npc.await_present, "lotg_zanik_follower", 6, 6)
            t.exec("walk-talkToGuard", t.player.walk_route, TO_GUARDS, { level = 0 })
            t.exec("talkToGuard", t.player.talk_to, "lotg_goblin_guard1", 1)
            t.exec("talkToGuard-dialog", t.chat.drain, { max_pages = 20 })
            t.ticks(2)
            t.expect("quest.stage.refused", t.quest.expect_stage("refused"))
            t.exec("walk-leaveCave", t.player.walk_route, TO_MUDPILE, { level = 0 })
            cave_out(t, "leaveCave")
            gate_south(t, "talkToMakeoverMage.memberGate")
            t.exec("goto-talkToMakeoverMage", t.player.goto_tile, 2923, 3323, 0)
            t.exec("talkToMakeoverMage.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2922, 3323, 0 }, near = { 2923, 3323 }, far = { 2921, 3323 } })
            t.exec("talkToMakeoverMage", t.player.talk_to, "makeover_mage_female", 1)
            menu(t, "talkToMakeoverMage", { "Can you turn me into a goblin?", "I need to slip past some goblin guards.",
                "Can you turn me into a goblin or not?" })
            t.ticks(2)
            t.expect("quest.stage.recipe", t.quest.expect_stage("recipe"))
            t.exec("talkToMakeoverMage.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2922, 3323, 0 }, near = { 2921, 3323 }, far = { 2923, 3323 } })
            -- pickPharmakosBerry: the bushes outside (lotg_temple.rs2 [oploc1,lotg_pharmakos_bush])
            t.exec("pickPharmakosBerry", t.player.click_loc, "lotg_pharmakos_bush", 1)
            t.exec("pickPharmakosBerry.got", t.inv.await, "lotg_pharmakos_berry", 1, 6)
            -- mixGoblinPotion: Herblore 47, 55 XP (lotg_temple.rs2 ~lotg_mix_goblin_potion)
            local _, h0 = t.skill.read("herblore")
            t.exec("mixGoblinPotion", t.player.use_item_on_item, "lotg_pharmakos_berry", "toadflaxvial")
            t.exec("mixGoblinPotion.got", t.inv.await, "lotg_3dosegoblin", 1, 6)
            local _, h1 = t.skill.read("herblore")
            t.check("mixGoblinPotion.xp", type(h0) == "table" and type(h1) == "table" and h1.experience - h0.experience == 55,
                "herblore xp " .. tostring(type(h0) == "table" and h0.experience) .. " -> " .. tostring(type(h1) == "table" and h1.experience) .. " (Goblin_potion oldid 15195296: 55)")
        end },

        -- Leg 3: the Temple of Tribes -- in as a goblin, Zanik out of her cell, the High Priest's test,
        -- the Huzamogaarb key (16 -> 34).
        { name = "temple", run = function(t)
            gate_north(t, "goBackToGoblinCave.memberGate")
            cave_in(t, "goBackToGoblinCave")
            t.exec("goToGuards", t.player.walk_route, TO_GUARDS, { level = 0 })
            -- pickBlackMushrooms / makeBlackDye: golem_portal.rs2's own mushrooms and pestle (black dye = golem_ink)
            t.exec("pickBlackMushrooms", t.player.click_loc, "golem_black_mushrooms", 1, { at = { 2577, 9845 } })
            t.exec("pickBlackMushrooms.got", t.inv.await, "golem_mushroom", 1, 6)
            t.exec("makeBlackDye", t.player.use_item_on_item, "pestle_and_mortar", "golem_mushroom")
            t.exec("makeBlackDye-box", t.chat.drain, { max_pages = 2 })
            t.exec("makeBlackDye.got", t.inv.await, "golem_ink", 1, 6)
            -- the pestle has done its job (Quest Helper lists it for the black dye only); the slot is wanted later
            t.exec("makeBlackDye.dropPestle", t.player.drop, "pestle_and_mortar")
            t.exec("walk-drinkGoblinPotion", t.player.walk_to, 2581, 9849)
            -- drinkGoblinPotion + confirmGoblin: interface 739 (lotg_temple.rs2 ~lotg_open_makeover)
            t.exec("drinkGoblinPotion", t.player.inv_op, "lotg_3dosegoblin", 1)
            t.exec("drinkGoblinPotion.interface", t.ui.await_open, "lotg_makeover", 10)
            do
                local wr, w = t.ui.widget("lotg_makeover:selection_3")
                t.ui.invoke(w, 1)
                t.ticks(2)
                local _, ty = t.var.varbit("varb13615_lotg_goblin_type")
                t.check("drinkGoblinPotion.pick", wr == "ok" and ty == 3, "selection_3 -> varb13615_lotg_goblin_type " .. tostring(ty))
                local cr, c = t.ui.widget("lotg_makeover:confirm_button")
                t.ui.invoke(c, 1)
                t.ticks(2)
                local _, g = t.var.varbit("varb13612_lotg_player_is_a_goblin")
                t.check("confirmGoblin", cr == "ok" and g == 1, "confirm_button -> varb13612_lotg_player_is_a_goblin " .. tostring(g))
            end
            t.expect("quest.stage.transformed", t.quest.expect_stage("transformed"))
            -- talkToGuardAsGoblin: the guard guesses a goblin name; any name will do (Quest Helper "^Yes, me .*")
            t.exec("talkToGuardAsGoblin", t.player.talk_to, "lotg_goblin_guard2", 1)
            menu(t, "talkToGuardAsGoblin", { "Me want get into temple.", "/^Yes, me /" })
            t.ticks(3)
            near(t, "talkToGuardAsGoblin.inTemple", 3744, 4305, 0, 1, "the temple hall inside its door")
            t.expect("quest.stage.in_temple", t.quest.expect_stage("in_temple"))
            -- getGoblinMail / dyeGoblinMail: the crate by the exit, the black dye on it
            t.exec("getGoblinMail", t.player.click_loc, "lotg_armour_crate", 1, { at = { 3747, 4309 } })
            t.exec("getGoblinMail.got", t.inv.await, "goblin_armour", 1, 6)
            t.exec("dyeGoblinMail", t.player.use_item_on_item, "golem_ink", "goblin_armour")
            t.exec("dyeGoblinMail.got", t.inv.await, "goblin_armour_black", 1, 6)
            t.exec("dyeGoblinMail.wear", t.player.equip, "goblin_armour_black")
            -- enterNorthEastRoom: the Huzamogaarb guard's Pass (he stands on the hall corner of his
            -- diagonal doorway, configs/lotg.spawn; talked to from the hall tile west of him)
            t.exec("walk-enterNorthEastRoom", t.player.walk_to, 3751, 4328)
            t.exec("enterNorthEastRoom", t.player.talk_to, "lotg_goblin_guard_black", 1)
            t.exec("enterNorthEastRoom-dialog", t.chat.drain, { max_pages = 4 })
            t.ticks(2)
            near(t, "enterNorthEastRoom.inside", 3754, 4329, 0, 0, "inside the Huzamogaarb enclave")
            t.exec("searchCrateForSphere", t.player.click_loc, "lotg_sphere_crate", 1)
            t.exec("searchCrateForSphere.got", t.inv.await, "dorgesh_teleport_artifact", 1, 6)
            -- talkToZanikInCell: through the prison door (lotg_temple.rs2 [apnpc1,lotg_zanik_in_prison])
            t.exec("walk-talkToZanikInCell", t.player.walk_to, 3752, 4341)
            t.exec("talkToZanikInCell", t.player.talk_to, "lotg_zanik_in_prison", 1)
            t.exec("talkToZanikInCell-dialog", t.chat.drain, { max_pages = 20 })
            t.ticks(2)
            t.expect("quest.stage.zanik_escaped", t.quest.expect_stage("zanik_escaped"))
            -- leaveNorthEastRoom: the mail off inside the enclave puts the player out in the hall (Quest
            -- Helper dyeGoblinMail*: "you will be thrown out to the center area"; lotg_shared.rs2 timer)
            t.exec("leaveNorthEastRoom", t.player.unequip, "goblin_armour_black")
            t.ticks(3)
            near(t, "leaveNorthEastRoom.outside", 3752, 4328, 0, 0, "thrown out into the temple hall")
            t.exec("leaveNorthEastRoom.wear", t.player.equip, "goblin_armour_black")
            t.ui.tab("inventory")
            -- talkToPriestInTemple: True / Commands it, False / Not mighty before, False / Commandments,
            -- Victory over whole world; then Yu'biusk, where, the old high priests
            t.exec("talkToPriestInTemple", t.player.talk_to, "lotg_goblin_high_priest", 1)
            menu(t, "talkToPriestInTemple", { "I understand Big High War God.", "True.", "Big High War God commands it.",
                "False.", "Goblins not mighty warriors before he chose us.", "False.", "That one of the commandments.",
                "Lead goblins to victory over whole world.", "Me want to know about Yu'biusk.", "Where is Yu'biusk?",
                "Can I talk to old high priests?", "Me have no questions." })
            t.ticks(2)
            t.expect("quest.stage.keys", t.quest.expect_stage("keys"))
            -- enterNorthEastRoomForKey / pickpocketPriest: the Huzamogaarb key
            t.exec("walk-enterNorthEastRoomForKey", t.player.walk_to, 3751, 4328)
            t.exec("enterNorthEastRoomForKey", t.player.talk_to, "lotg_goblin_guard_black", 1)
            t.exec("enterNorthEastRoomForKey-dialog", t.chat.drain, { max_pages = 4 })
            t.ticks(2)
            near(t, "enterNorthEastRoomForKey.inside", 3754, 4329, 0, 0, "inside the Huzamogaarb enclave")
            t.exec("pickpocketPriest", t.player.talk_to, "lotg_goblin_priest_black", 3)
            t.exec("pickpocketPriest.key", t.inv.await, "lotg_key_black", 1, 6)
            t.exec("pickpocketPriest.out", t.player.unequip, "goblin_armour_black")
            t.ticks(3)
            near(t, "pickpocketPriest.hall", 3752, 4328, 0, 0, "thrown out into the temple hall")
            t.ui.tab("inventory")
            -- out of the temple by its door, out of the cave by the mud pile (back to human, the mail carried)
            t.exec("leaveTemple", t.player.climb, { loc = "lotg_temple_walldoor", op = 1, op_name = "Open",
                at = { 3744, 4304, 0 }, dest = { 2581, 9851, 0 } })
            t.exec("walk-leaveCaveForAggie", t.player.walk_route, TO_MUDPILE, { level = 0 })
            cave_out(t, "leaveCaveForAggie")
            t.ticks(3)
            local _, g = t.var.varbit("varb13612_lotg_player_is_a_goblin")
            local _, black = t.inv.count("goblin_armour_black")
            t.check("leaveCaveForAggie.human", g == 0 and black == 1, "varb13612_lotg_player_is_a_goblin " .. tostring(g)
                .. " out of the cave; black goblin mail carried " .. tostring(black))
        end },

        -- Leg 4: Aggie and the Hemenster whitefish (34).
        { name = "whitefish", run = function(t)
            gate_south(t, "talkToAggie.memberGate")
            t.exec("goto-talkToAggie", t.player.goto_tile, 3089, 3258, 0)
            t.exec("talkToAggie.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 3088, 3258, 0 }, near = { 3089, 3258 }, far = { 3087, 3258 } })
            t.exec("talkToAggie", t.player.talk_to, "aggie", 1)
            menu(t, "talkToAggie", { "Can you make dyes for me please?", "Can you make black or white dye?", "Thanks." })
            t.expect("talkToAggie.knows", t.var.expect("varb13602_lotg_know_about_fish", 1))
            t.exec("talkToAggie.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 3088, 3258, 0 }, near = { 3087, 3258 }, far = { 3089, 3258 } })
            gate_north(t, "goToHemenster.memberGate")
            t.exec("goto-goToHemenster", t.player.goto_tile, 2644, 3441, 0)
            -- goToHemenster: Morris's gate (quest_fishingcompo_gate.rs2 ~fishingcompo_gate_admit)
            t.exec("goToHemenster", t.player.cross_gate, { loc = "fishinggateclosedr", at = { 2642, 3441, 0 }, near = { 2643, 3441 },
                far_ok = function(tile) return tile.x <= 2642 end, far_desc = "inside the competition area, x <= 2642",
                chat = { "npc:Competition pass please.", "options", "choose:I need to catch a Hemenster Whitefish.",
                    "player:I need to catch a Hemenster Whitefish.", "npc:Whitefish, eh?" } })
            local _, f0 = t.skill.read("fishing")
            t.exec("catchWhitefish", t.player.talk_to, "0_41_53_sinisterfishspot", 1)
            t.exec("catchWhitefish.got", t.inv.await, "lotg_whitefish", 1, 10)
            local _, f1 = t.skill.read("fishing")
            t.check("catchWhitefish.xp", type(f0) == "table" and type(f1) == "table" and f1.experience - f0.experience == 70,
                "fishing xp " .. tostring(type(f0) == "table" and f0.experience) .. " -> " .. tostring(type(f1) == "table" and f1.experience) .. " (Whitefish oldid 15290034: 70)")
            t.exec("goToHemenster.out", t.player.cross_gate, { loc = "fishinggateclosedr", at = { 2642, 3441, 0 }, near = { 2642, 3441 },
                far_ok = function(tile) return tile.x >= 2643 end, far_desc = "out of the competition area, x >= 2643" })
            t.exec("goToHemenster.dropRod", t.player.drop, "fishing_rod")
            gate_south(t, "talkToAggieWithFish.memberGate")
            t.exec("goto-talkToAggieWithFish", t.player.goto_tile, 3089, 3258, 0)
            t.exec("talkToAggieWithFish.doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 3088, 3258, 0 }, near = { 3089, 3258 }, far = { 3087, 3258 } })
            t.exec("talkToAggieWithFish", t.player.talk_to, "aggie", 1)
            menu(t, "talkToAggieWithFish", { "Can you make dyes for me please?", "Could you remove the dye from this goblin mail?" })
            t.exec("talkToAggieWithFish.white", t.inv.await, "goblin_armour_white", 1, 6)
            local _, fish = t.inv.count("lotg_whitefish")
            local _, coins = t.inv.count("coins")
            local _, black = t.inv.count("goblin_armour_black")
            t.check("talkToAggieWithFish.paid", fish == 0 and coins == 0 and black == 0, "whitefish " .. tostring(fish)
                .. ", coins " .. tostring(coins) .. ", black mail " .. tostring(black) .. " after (want 0, 0, 0)")
            t.exec("talkToAggieWithFish.doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 3088, 3258, 0 }, near = { 3087, 3258 }, far = { 3089, 3258 } })
        end },

        -- Leg 5: the five other keys and the crypt door (34 -> 38).
        { name = "keys", run = function(t)
            gate_north(t, "goToTempleWithDyes.memberGate")
            cave_in(t, "goToTempleWithDyes")
            t.exec("walk-drinkGoblinPotionAgain", t.player.walk_route, TO_GUARDS, { level = 0 })
            t.exec("drinkGoblinPotionAgain", t.player.inv_op, "lotg_2dosegoblin", 1)
            t.exec("drinkGoblinPotionAgain.interface", t.ui.await_open, "lotg_makeover", 10)
            do
                local cr, c = t.ui.widget("lotg_makeover:confirm_button")
                t.ui.invoke(c, 1)
                t.ticks(2)
                local _, g = t.var.varbit("varb13612_lotg_player_is_a_goblin")
                t.check("confirmGoblinAgain", cr == "ok" and g == 1, "confirm_button -> varb13612_lotg_player_is_a_goblin " .. tostring(g))
            end
            -- enterTempleDoorForThieving: a named goblin goes straight down the stairs
            t.exec("enterTempleDoorForThieving", t.player.climb, { loc = "lotg_goblin_staircase", op = 1, op_name = "Climb-down",
                at = { 2581, 9852, 0 }, dest = { 3744, 4305, 0 } })
            t.exec("wearWhiteMail", t.player.equip, "goblin_armour_white")
            -- { colour, guard, priest, key, dye, mail, inside tile, thrown-out tile, tile to talk to the guard from }
            local enclaves = {
                { "White", "lotg_goblin_guard_white", "lotg_goblin_priest_white", "lotg_key_white", nil, nil, { 3731, 4320 }, { 3733, 4320 }, { 3733, 4320 } },
                { "Yellow", "lotg_goblin_guard_yellow", "lotg_goblin_priest_yellow", "lotg_key_yellow", "yellowdye", "goblin_armour_yellow", { 3735, 4330 }, { 3736, 4328 }, { 3737, 4328 } },
                { "Blue", "lotg_goblin_guard_blue", "lotg_goblin_priest_blue", "lotg_key_blue", "bluedye", "goblin_armour_darkblue", { 3757, 4320 }, { 3755, 4320 }, { 3755, 4320 } },
                { "Orange", "lotg_goblin_guard_orange", "lotg_goblin_priest_orange", "lotg_key_orange", "orangedye", "goblin_armour_orange", { 3753, 4310 }, { 3752, 4312 }, { 3751, 4312 } },
                { "Purple", "lotg_goblin_guard_purple", "lotg_goblin_priest_purple", "lotg_key_purple", "purpledye", "goblin_armour_purple", { 3734, 4311 }, { 3736, 4312 }, { 3737, 4312 } },
            }
            local worn = "goblin_armour_white"
            for _, e in ipairs(enclaves) do
                local colour, guard, priest_sym, key, dye, mail, inside, outside, talk_at = e[1], e[2], e[3], e[4], e[5], e[6], e[7], e[8], e[9]
                if dye then
                    -- dyeGoblinMail<Colour>: the mail (already off) dyed and worn again (gobdip_or_lotg_dye_mail)
                    t.exec("dyeGoblinMail" .. colour, t.player.use_item_on_item, dye, worn)
                    t.exec("dyeGoblinMail" .. colour .. ".got", t.inv.await, mail, 1, 6)
                    t.exec("dyeGoblinMail" .. colour .. ".wear", t.player.equip, mail)
                    worn = mail
                end
                t.exec("walk-pass" .. colour .. "Guard", t.player.walk_to, talk_at[1], talk_at[2])
                t.exec("pass" .. colour .. "Guard", t.player.talk_to, guard, 1)
                t.exec("pass" .. colour .. "Guard-dialog", t.chat.drain, { max_pages = 4 })
                t.ticks(2)
                near(t, "pass" .. colour .. "Guard.inside", inside[1], inside[2], 0, 0, "inside the " .. colour .. " enclave")
                t.exec("pickpocket" .. colour .. "Priest", t.player.talk_to, priest_sym, 3)
                t.exec("pickpocket" .. colour .. "Priest.key", t.inv.await, key, 1, 6)
                -- out: the mail off inside throws the player out to the hall (Quest Helper dyeGoblinMail*)
                t.exec("pickpocket" .. colour .. "Priest.out", t.player.unequip, worn)
                t.ticks(3)
                near(t, "pickpocket" .. colour .. "Priest.hall", outside[1], outside[2], 0, 0, "thrown out into the temple hall")
            end
            t.ui.tab("inventory")
            -- unlockCrypt: the six keys in the huge door (lotg_keys.rs2 [oploc1,lotg_temple_huge_door])
            t.exec("unlockCrypt", t.player.click_loc, "lotg_temple_huge_door", 1)
            t.ticks(2)
            t.expect("quest.stage.crypt_open", t.quest.expect_stage("crypt_open"))
            local _, kb = t.inv.count("lotg_key_black")
            local _, kp = t.inv.count("lotg_key_purple")
            t.check("unlockCrypt.keysUsed", kb == 0 and kp == 0, "black key " .. tostring(kb) .. ", purple key " .. tostring(kp) .. " after unlocking (want 0)")
            -- enterCrypt: "Yes." (Quest Helper enterCrypt)
            t.exec("enterCrypt", t.player.climb, { loc = "lotg_temple_huge_door", op = 1, op_name = "Open",
                at = { 3743, 4332, 0 }, dest = { 3742, 4382, 0 },
                same_level = "lotg_keys.rs2 [oploc1,lotg_temple_huge_door] p_teleport(^lotg_crypt_coord)",
                chat = { "choose:Yes." } })
            t.expect("quest.stage.in_crypt", t.quest.expect_stage("in_crypt"))
        end },

        -- Leg 6: the high priests of ages past (38 -> 48).
        { name = "crypt", run = function(t)
            -- Combat gear on: the goblin form wears off (Goblin_Temple oldid 15229052: "Players can however
            -- turn back to human form in the crypt")
            for _, item in ipairs({ "abyssal_whip", "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield" }) do
                t.exec("enterCrypt.wear." .. item, t.player.equip, item)
            end
            t.ticks(3)
            t.expect("enterCrypt.human", t.var.expect("varb13612_lotg_player_is_a_goblin", 0))
            priest(t, 1, "lotg_crypt_priest_grave1", "Snothead", "lotg_goblin_skeleton_high_priest1", "What was your predecessor's name?", "snailfeet")
            priest(t, 2, "lotg_crypt_priest_grave2", "Snailfeet", "lotg_goblin_skeleton_high_priest2", "What was your predecessor's name?", "mosschin")
            priest(t, 3, "lotg_crypt_priest_grave3", "Mosschin", "lotg_goblin_skeleton_high_priest3", "What was your predecessor's name?", "redeyes")
            prayer_melee(t, "defeatRedeyes.protectMelee", true)
            priest(t, 4, "lotg_crypt_priest_grave4", "Redeyes", "lotg_goblin_skeleton_high_priest4", "What was your predecessor's name?", "strongbones")
            t.exec("defeatStrongbones.prayerPotion", t.player.inv_op, "4doseprayerrestore", 1)
            priest(t, 5, "lotg_crypt_priest_grave5", "Strongbones", "lotg_goblin_skeleton_high_priest5", "Where is Yu'biusk?", "back_to_oldak")
            prayer_melee(t, "learnYubiusk.prayerOff", false)
            -- out by the crypt gate; a human in the temple is put out of it at its stairs
            t.exec("leaveCrypt", t.player.click_loc, "lotg_crypt_exit", 1)
            t.ticks(4)
            near(t, "leaveCrypt.thrownOut", 2581, 9851, 0, 0, "outside the temple stairs: a human in the temple is thrown out (Goblin_Temple oldid 15229052)")
            t.exec("walk-leaveCaveForOldak", t.player.walk_route, TO_MUDPILE, { level = 0 })
            cave_out(t, "leaveCaveForOldak")
        end },

        -- Leg 7: the path to Yu'biusk (48 -> 56).
        { name = "yubiusk", run = function(t)
            gate_south(t, "goReturnToDorg.memberGate")
            t.exec("goto-goReturnToDorg", t.player.goto_tile, 3206, 3233, 0)
            to_mines(t, "goReturnToDorg.")
            t.exec("goReturnToDorg", t.player.climb, { loc = "cave_goblin_city_doorr", op = 1, op_name = "Open",
                at = { 3317, 9601, 0 }, dest = { 2704, 5365, 0 } })
            -- talkToOldak: "Returning to Dorgesh-Kaan" (lotg_yubiusk.rs2 @lotg_back_from_crypt)
            t.exec("talkToOldak.present", t.npc.await_present, "dorgesh_oldak_there", 8, 10)
            t.exec("talkToOldak", t.player.talk_to, "dorgesh_oldak_there", 1)
            t.exec("talkToOldak-dialog", t.chat.drain, { max_pages = 30 })
            t.ticks(2)
            t.expect("quest.stage.fungus_ring", t.quest.expect_stage("fungus_ring"))
            t.exec("talkToOldak.doorOut", t.player.pass_door, { closed = "dorgesh_inner_door_closed", open = "dorgesh_inner_door_open",
                at = { 2709, 5362, 0 }, near = { 2708, 5362 }, far = { 2710, 5362 },
                far_ok = function(tile) return tile.x >= 2709 end, far_desc = "out of Oldak's lab, x >= 2709" })
            -- south through the city, up to the agility course, down its ladder to the caves (maplink.dbrow
            -- dorgesh_1stairs 0_42_82_25_37 -> 1_42_82_25_33, dorgesh_2stairs_posh 1_42_82_34_7 -> 3_42_82_34_1,
            -- dorgesh_caves_ladder_down 3_42_81_31_58 -> 0_42_81_27_57)
            t.exec("goto-climbDorgeshKaanStairsF0", t.player.goto_tile, 2713, 5285, 0)
            t.exec("climbDorgeshKaanStairsF0", t.player.climb, { loc = "dorgesh_1stairs", op = 1, op_name = "Climb-up",
                at = { 2713, 5282, 0 }, src = { 2713, 5285 }, dest = { 2713, 5281, 1 } })
            t.exec("goto-climbDorgeshKaanStairsF1", t.player.goto_tile, 2722, 5255, 1)
            t.exec("climbDorgeshKaanStairsF1", t.player.climb, { loc = "dorgesh_2stairs_posh", op = 1, op_name = "Climb-up",
                at = { 2722, 5252, 1 }, src = { 2722, 5255 }, dest = { 2722, 5249, 3 } })
            t.exec("goto-climbLadderTop", t.player.goto_tile, 2719, 5242, 3)
            t.exec("climbLadderTop", t.player.climb, { loc = "dorgesh_caves_ladder_down", op = 1, op_name = "Climb-down",
                at = { 2719, 5241, 3 }, src = { 2719, 5242 }, dest = { 2715, 5241, 0 } })
            t.exec("goto-talkToOldakAtMachine", t.player.goto_tile, 2742, 5220, 0)
            t.exec("talkToOldakAtMachine", t.player.talk_to, "lotg_oldak_fairyring", 1)
            t.exec("talkToOldakAtMachine-dialog", t.chat.drain, { max_pages = 10 })
            t.expect("talkToOldakAtMachine.explained", t.var.expect("varb13618_lotg_machine_explained", 1))
            -- inspectMachine: interface 738, the Fairy Ring Power Relay; 9 / 4 / 1 (the circles' areas)
            t.exec("inspectMachine", t.player.click_loc, "lotg_fairy_ring_machine_setup", 1)
            t.exec("inspectMachine.interface", t.ui.await_open, "lotg_machine", 10)
            local function press(name, comp, times, varbit, want)
                local wr, w = t.ui.widget("lotg_machine:" .. comp)
                for _ = 1, times do
                    t.ui.invoke(w, 1)
                    t.ticks(1)
                end
                t.ticks(1)
                local _, v = t.var.varbit(varbit)
                t.check(name, wr == "ok" and v == want, comp .. " x" .. times .. " -> " .. varbit .. " " .. tostring(v) .. " (want " .. want .. ")")
            end
            press("increaseFirst", "meter1_up", 9, "varb13603_lotg_connectors_1", 9)
            press("increaseSecond", "meter2_up", 4, "varb13604_lotg_connectors_2", 4)
            press("increaseThird", "meter3_up", 1, "varb13605_lotg_connectors_3", 1)
            do
                local cr, c = t.ui.widget("lotg_machine:confirm_button")
                t.ui.invoke(c, 1)
                t.ticks(2)
                local _, a = t.var.varbit("varb13611_lotg_fairy_ring_animating")
                t.check("confirmFixMachine", cr == "ok" and a == 1, "confirm_button -> varb13611_lotg_fairy_ring_animating " .. tostring(a))
            end
            t.exec("watchYubiuskCutscene", t.chat.drain, { max_pages = 20 })
            t.ticks(2)
            t.expect("quest.stage.yubiusk", t.quest.expect_stage("yubiusk"))
            near(t, "watchYubiuskCutscene.landed", 3571, 4370, 0, 2, "Yu'biusk, beside the portal")
            local reward_snapshot_result, reward_before = t.skill.snapshot()
            t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the strange box -> " .. tostring(reward_snapshot_result))
            -- openBox: the strange box (lotg_yubiusk.rs2 [oploc1,lotg_bandos_sarcophagus])
            t.exec("openBox", t.player.click_loc, "lotg_bandos_sarcophagus", 1)
            t.exec("openBox-dialog", t.chat.drain, { max_pages = 30 })
            t.ticks(3)
            t.quest.expect_complete()
            near(t, "openBox.lab", 2704, 5365, 0, 1, "back through the portal to Oldak's lab")
            t.check("reward.agility", t.skill.expect_gain("agility", 8000, reward_before))
            t.check("reward.fishing", t.skill.expect_gain("fishing", 8000, reward_before))
            t.check("reward.thieving", t.skill.expect_gain("thieving", 8000, reward_before))
            t.check("reward.herblore", t.skill.expect_gain("herblore", 8000, reward_before))
            t.finish(0)
        end },
    },
}
