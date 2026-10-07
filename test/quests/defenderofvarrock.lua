-- Defender of Varrock (tier 4), driven from Quest Helper's defenderofvarrock/DefenderOfVarrock.java
-- (python3 tools/quest_gate/ladder.py defenderofvarrock: 56 steps, 6 legs) and the wiki
-- (Defender_of_Varrock/Quick_guide oldid 15079953, Defender_of_Varrock oldid 15314409,
-- Transcript:Defender_of_Varrock oldid 15341906). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_defenderofvarrock/ (dov_elias.rs2: Elias,
-- the trail, the trapdoor, the base; dov_camdozaal.rs2: Ramarno, the golems, the Sacred Forge;
-- dov_rovin.rs2 + areas/varrock/scripts/captain_rovin.rs2: Rovin; dov_invasion.rs2: the library and
-- Aeonisig; reldo/king_roald/sir_prysin/romeo/horvik/curator.rs2 and quest_crest/scripts/
-- crest_dimintheis.rs2: the descendants and Dimintheis).
--
-- Stage values are the real game's (Quest Helper steps.put keys; quest_defenderofvarrock.constant).
--
-- Brought along (getItemRequirements / the dbrow): the eight prerequisite quests, Smithing 55 and Hunter
-- 52 (staged at exactly the requirement), "combat gear and food" and "any pickaxe". The combat stats
-- are a margin for the armoured zombies (level 82: 75 hp, 73 attack) and the chaos golem; no quest
-- script branches on a combat stat (grep stat_base/stat( in quest_defenderofvarrock: only Smithing and
-- Hunter, dov_elias.rs2 [proc,dov_qualify_fail_reason]).
--
-- Door rule: every goto lands on an open street, field or cave tile.
--   * The Jolly Boar Inn: an open doorway (fai_varrock_museum_door_inactive_l/r 3280-3281,3506, no op);
--     reach.py 3280,3510 -> 3283,3501 REACH closed-doors len=12, so talk_to walks in from the street.
--   * The trail clues are open countryside; bush1 (3325,3470), bush2 (3349,3495) and the trapdoor
--     (3343,3515) are east of the Varrock members' gate fai_varrock_member_gatel 3319,3468, crossed on
--     foot.
--   * The trapdoor (dov_base_entry) climbs down to the base (dov_elias.rs2 @dov_enter_base,
--     ^dov_dungeon_arrival_coord 3560,4551,0); the ladder dov_base_exit (3559,4552) climbs out beside it
--     (^dov_dungeon_exit_coord 3342,3515,0). Both gates are crossed by their own op, both ways.
--   * Varrock Palace: courtyard 3212,3460 (street), the hall doorway 3212-3213,3470 has no door; the
--     north-west tower door fai_varrock_castle_door 3203,3493, varrock_spiralstairs_taller /
--     _middle_taller to Captain Rovin (level 2); routes as test/quests/demon.lua. Elias' report takes
--     you up himself ("You head upstairs with Elias.", dov_elias.rs2 @dov_elias_palace_report).
--   * Camdozaal: bim_entrance 2999,3493 (belowicemountain.rs2 p_telejump(^bim_dungeon_coord
--     2952,5764)); out by bim_exit 2951,5761 (p_telejump(^bim_entrance_landing 2996,3494)).
--
-- The descendant who names the Fitzharmons is rolled per player at the census (walkthrough: "only one of
-- them (random for each player)"); the test hands the shield to all six in the quick guide's order and
-- never reads the roll -- the stage reaching 48 by the end is the observable.
-- GUIDE-GAP: talkToRoald talkToAeonisig talkToPrysin talkToRomeoFromInstance talkToHorvikFromInstance talkToHalenFromInstance talkToDimintheisFromInstance goToF1ForRovin goToF2ForRovin are the invasion-instance variants (dov_roald, dov_aeonisig, ... at x 3906-3964); the port has no instance and reaches the same npcs at their world placements, driven as the guide's OutsideInstance/world steps (dov_invasion.rs2:16-17)

return {
    id = "defenderofvarrock",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::complete quest_shieldofarrav",
        "::complete quest_templeofikov",
        "::complete quest_belowicemountain",
        "::complete quest_familycrest",
        "::complete quest_gardenoftranquillity",
        "::complete quest_whatliesbelow",
        "::complete quest_romeoandjuliet",
        "::complete quest_demonslayer",
        "::setlevel smithing 55",
        "::setlevel hunter 52",
        "::setlevel attack 70",
        "::setlevel strength 70",
        "::setlevel defence 70",
        "::setlevel hitpoints 75",
        "::setlevel prayer 43",
        "::setlevel mining 40",
        "::give rune_scimitar",
        "::give rune_full_helm",
        "::give rune_chainbody",
        "::give rune_platelegs",
        "::give rune_kiteshield",
        "::give rune_pickaxe",
        "::give shark 12",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb9655_dov",
            constants = {
                not_started = 0,
                started = 2,
                following = 4,
                trail_plant = 6,
                trail_key = 8,
                trail_done = 10,
                trapdoor_open = 12,
                dungeon_entered = 14,
                elias_dungeon = 16,
                balcony_1 = 18,
                gate1_open = 22,
                arrav = 24,
                gate2_open = 26,
                balcony_2 = 28,
                elias_palace = 30,
                rovin_forge_sent = 32,
                ramarno = 34,
                forge_done = 36,
                rovin_invasion = 40,
                reldo = 42,
                list_of_elders = 44,
                census = 46,
                candidates_done = 48,
                dimintheis = 50,
                finish_ready = 52,
                complete = 56,
            },
            row = "quest_defenderofvarrock",
            display = "Defender of Varrock",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function stage()
            local _, v = t.var.server("varb9655_dov")
            return tonumber(v)
        end
        local function at_stage(name, value, ticks)
            local r = t.var.await_server("varb9655_dov", value, ticks or 6)
            t.expect("quest.stage." .. name, t.quest.expect_stage(name))
            return r == "ok"
        end

        -- A conversation walked page by page: every option row in `picks` is chosen in order, every
        -- other page continued, and the row checks that each fragment of `want` was read, in order.
        -- (The transcript's long lines wrap onto pages of their own, so a page-exact list would pin
        -- the wrap, not the dialogue.) An empty gap between two scripts (a p_delay, a teleport) is
        -- waited out for a few ticks before the conversation counts as over.
        local function norm(s)
            s = tostring(s or ""):gsub("<br>", " "):gsub("<[^>]*>", ""):gsub("%s+", " ")
            return s
        end
        local function converse(name, picks, want)
            picks = picks or {}
            want = want or {}
            local pages, chosen, idle = {}, 0, 0
            for _ = 1, 160 do
                local kind = t.chat.kind()
                if kind == "options" then
                    idle = 0
                    local pick = picks[chosen + 1]
                    if not pick then
                        pages[#pages + 1] = "options(no pick left)"
                        break
                    end
                    local r = t.chat.choose(pick)
                    if r ~= "ok" then
                        pages[#pages + 1] = "choose(" .. pick .. ")=" .. tostring(r)
                        break
                    end
                    chosen = chosen + 1
                    pages[#pages + 1] = "choose:" .. pick
                elseif kind == "npc" or kind == "player" or kind == "mesbox" or kind == "objbox" then
                    idle = 0
                    local _, text = t.chat.text()
                    pages[#pages + 1] = kind .. ":" .. norm(text)
                    t.chat.continue_()
                else
                    idle = idle + 1
                    if idle > 4 then
                        break
                    end
                    t.ticks(1)
                end
            end
            local joined = table.concat(pages, " | ")
            local at, missing = 1, nil
            for _, frag in ipairs(want) do
                local found = string.find(joined, frag, at, true)
                if not found then
                    missing = frag
                    break
                end
                at = found + #frag
            end
            local ok = missing == nil and chosen == #picks
            t.check(name, ok, (missing and ("missing '" .. missing .. "'; ") or "")
                .. "chose " .. chosen .. "/" .. #picks .. "; " .. #pages .. " page(s): " .. joined:sub(1, 900))
            return ok
        end

        for _, piece in ipairs({ "rune_scimitar", "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield" }) do
            t.exec("wear-" .. piece, t.player.equip, piece)
        end

        local function prayer(name, want_on)
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            if (on == 1) ~= want_on then
                t.ui.tab("prayer")
                t.ticks(2)
                local _, w = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(w, 1)
                t.ticks(2)
                _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            end
            t.check(name, (on == 1) == want_on, "varb4118_prayer_protectfrommelee " .. tostring(on))
        end
        local function fight(name, npc)
            t.exec(name .. ".present", t.npc.await_present, npc, 14, 10)
            t.exec(name .. ".attack", t.player.attack, npc, 2, 30)
            local _, d = t.exec(name, t.npc.await_dead_engaged, 300, 3, { eat = { item = "shark", below = 40 } })
            local low = tonumber(tostring(d):match("lowest hp (%d+)/"))
            local _, sharks = t.inv.count("shark")
            t.check(name .. ".margin", low ~= nil and low >= 19 and (sharks or 0) >= 1,
                "lowest hp " .. tostring(low) .. "/75 (need >= 19, a quarter); sharks left " .. tostring(sharks)
                    .. " of 12 staged")
        end

        -- ================= LEG 1: Elias and the hunting trail =================
        -- talkToElias: the Jolly Boar Inn, in through its open doorway from the road north of it.
        t.exec("goto-talkToElias", t.player.goto_tile, 3280, 3510, 0)
        t.exec("talkToElias.talk", t.player.talk_to, "elias_white_jolly_boar", 1)
        converse("talkToElias", { "Yes.", "Ready when you are." }, {
            "player:Hello there.", "The name's Elias White", "choose:Yes.",
            "So what was this tip-off you received?", "what do you know of Arrav?",
            "Then shall we get started?", "choose:Ready when you are.", "I'm right behind you.",
        })
        at_stage("following", 4)

        -- inspectPlant: the plant north of the pub (3280,3516); its Inspect shows from stage 4.
        t.exec("walk-inspectPlant", t.player.walk_to, 3280, 3514, 20)
        -- 3280,3514 is inside the Wilderness warning strip (z 3512-3515, wilderness_warning.rs2:13-28,
        -- [zone,0_51_54_16_56]): the first step into it this session queues three pages and
        -- p_stopaction, so they are read before the plant is pressed.
        t.ticks(2)
        t.exec("wildernessWarning", t.chat.play, {
            "mesbox:WARNING! Proceed with caution",
            "mesbox:The further north you go",
            "mesbox:In the wilderness an indicator",
        })
        t.exec("inspectPlant", t.player.click_loc, "dov_hunting_plant_initial", 1, { at = { 3280, 3516, 0 } })
        converse("inspectPlant-dialog", {}, { "Look! Footprints." })
        t.exec("inspectPlant.flag", t.var.await_server, "varb9659_dov_hunting_trail_1", 1, 6)
        at_stage("trail_plant", 6)

        -- inspectRock: the small rocks to the west (3260,3514).
        t.exec("inspectRock", t.player.click_loc, "dov_hunting_boulder1", 1, { at = { 3260, 3514, 0 } })
        t.exec("inspectRock.flag", t.var.await_server, "varb9660_dov_hunting_trail_2", 1, 6)

        -- inspectPlant2: south, the plant south-west of the inn (3269,3480).
        t.exec("goto-inspectPlant2", t.player.goto_tile, 3270, 3484, 0)
        t.exec("inspectPlant2", t.player.click_loc, "dov_hunting_plant1", 1, { at = { 3269, 3480, 0 } })
        t.exec("inspectPlant2.flag", t.var.await_server, "varb9661_dov_hunting_trail_3", 1, 6)

        -- inspectBush1: the spiny bush south-east of the Saradomin statue (3293,3463): the grubby key.
        t.exec("goto-inspectBush1", t.player.goto_tile, 3293, 3466, 0)
        t.exec("inspectBush1", t.player.click_loc, "dov_hunting_plant2", 1, { at = { 3293, 3463, 0 } })
        converse("inspectBush1-dialog", {}, { "you find a key", "A key? I wonder what it opens" })
        t.exec("inspectBush1.flag", t.var.await_server, "varb9662_dov_hunting_trail_4", 1, 6)
        t.exec("inspectBush1.key", t.inv.await, "dov_base_key", 1, 6)
        at_stage("trail_key", 8)

        -- inspectBush2: the green bush by the gate to Silvarea (3325,3470), east of the members' gate.
        t.exec("goto-inspectBush2.memberGate", t.player.goto_tile, 3317, 3468, 0)
        t.exec("inspectBush2.memberGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("inspectBush2", t.player.click_loc, "dov_hunting_bush1", 1, { at = { 3325, 3470, 0 } })
        t.exec("inspectBush2.flag", t.var.await_server, "varb9663_dov_hunting_trail_5", 1, 6)

        -- inspectBush3: the small bush further east (3349,3495).
        t.exec("goto-inspectBush3", t.player.goto_tile, 3347, 3493, 0)
        t.exec("inspectBush3", t.player.click_loc, "dov_hunting_bush2", 1, { at = { 3349, 3495, 0 } })
        t.exec("inspectBush3.flag", t.var.await_server, "varb9664_dov_hunting_trail_6", 1, 6)
        at_stage("trail_done", 10)

        -- inspectTrapdoor: north, by the Wilderness ditch (3343,3515). At stage 10 it reads "Open": the
        -- key unlocks it (12), "Let's do it." and the climb down (14).
        t.exec("goto-inspectTrapdoor", t.player.goto_tile, 3342, 3510, 0)
        t.exec("inspectTrapdoor", t.player.climb, { loc = "dov_base_entry", op = 1, op_name = "Open",
            at = { 3343, 3515, 0 }, dest = { 3560, 4551, 0 },
            same_level = "dov_elias.rs2 @dov_enter_base ~climb_ladder_to(^dov_dungeon_arrival_coord)",
            chat = { "npc:A trapdoor!", "player:Only one way to find out", "*", "npc:Right, shall we go in?",
                "choose:Let's do it.", "player:Let's do it." } })
        t.ticks(2)
        local kr, kn = t.inv.count("dov_base_key")
        t.check("inspectTrapdoor.keyUsed", kr == "ok" and kn == 0, "dov_base_key count " .. tostring(kn) .. " (want 0: the key is used on the trapdoor)")

        -- ================= LEG 2: Zemouregal's base =================
        -- listenToElias: "Upon entering Zemouregal's Base" (14 -> 16).
        converse("listenToElias", {}, { "What is this place?", "I'm not sure.", "Then let's look around" })
        at_stage("elias_dungeon", 16)

        -- lookOverBalcony: Sharathteerk, Zemouregal and Arrav below (16 -> 18).
        t.exec("lookOverBalcony.click", t.player.click_loc, "dov_base_balcony_1", 1, { at = { 3564, 4569, 1 } })
        converse("lookOverBalcony", {}, { "corroding my insides", "Is that...?", "Arrav. So the stories are true",
            "I'll meet you in the palace" })
        at_stage("balcony_1", 18)

        -- pickupBottles: three bottles west of the balcony, by gate 1 (QH 3537,4572).
        for i = 1, 3 do
            t.exec("pickupBottles." .. i, t.player.click_obj, "dov_mist_bottle_empty")
        end
        t.exec("pickupBottles", t.inv.await, "dov_mist_bottle_empty", 3, 6)

        -- killZombies / collectRedMist, three times: each armoured zombie leaves red mist, a bottle fills.
        local function zombie_and_mist(pfx, i)
            prayer(pfx .. "." .. i .. ".protectMelee", true)
            fight(pfx .. "." .. i, "dov_armoured_zombie_melee_1")
            t.exec(pfx .. "." .. i .. ".collectRedMist", t.player.click_loc, "dov_red_mist", 1)
            t.exec(pfx .. "." .. i .. ".bottleFilled", t.inv.await, "dov_mist_bottle_full", i, 6)
        end
        for i = 1, 3 do
            zombie_and_mist("killZombies", i)
        end
        prayer("killZombies.prayerOff", false)
        t.exec("collectRedMist", t.inv.await, "dov_mist_bottle_full", 3, 4)

        -- openDoorToArrav: gate 1 (the west edge of 3536,4571) absorbs the mist; through it, Arrav.
        t.exec("openDoorToArrav.walk", t.player.walk_to, 3537, 4571, 30)
        t.exec("openDoorToArrav.click", t.player.click_loc, "dov_base_gate_closed_1", 1, { at = { 3536, 4571, 1 } })
        converse("openDoorToArrav", {}, { "You must leave this place", "Are you fighting Zemouregal's control?",
            "My heart... He has it.", "Leave before you" })
        at_stage("arrav", 24)
        local _, tile = t.world.tile()
        t.check("openDoorToArrav.through", type(tile) == "table" and tile.x == 3535 and tile.z == 4571,
            "west of gate 1 at " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile))
        t.exec("openDoorToArrav.bottlesEmptied", t.inv.await, "dov_mist_bottle_empty", 3, 4)

        -- killZombiesAgain / collectRedMistAgain: the corridor north to gate 2.
        t.exec("killZombiesAgain.walk", t.player.walk_to, 3529, 4583, 40)
        for i = 1, 3 do
            zombie_and_mist("killZombiesAgain", i)
        end
        prayer("killZombiesAgain.prayerOff", false)
        t.exec("collectRedMistAgain", t.inv.await, "dov_mist_bottle_full", 3, 4)

        -- goThroughSecondGate: gate 2 (the west edge of 3540,4597), east through it.
        t.exec("goThroughSecondGate.walk", t.player.walk_to, 3539, 4597, 40)
        t.exec("goThroughSecondGate", t.player.cross_gate, { loc = "dov_base_gate_closed_2", at = { 3540, 4597, 0 },
            loc_level = 1, near = { 3539, 4597 }, far_ok = function(tl) return tl.x >= 3540 end,
            far_desc = "east of gate 2, x >= 3540" })
        at_stage("gate2_open", 26)

        -- lookOverSecondBalcony (26 -> 28).
        t.exec("lookOverSecondBalcony.walk", t.player.walk_to, 3562, 4592, 40)
        t.exec("lookOverSecondBalcony.click", t.player.click_loc, "dov_base_balcony_2", 1, { at = { 3562, 4591, 1 } })
        converse("lookOverSecondBalcony", {}, { "he may be close to finding it", "Begin the final preparations",
            "I'd better get to Varrock right away" })
        at_stage("balcony_2", 28)

        -- Out the way we came: gate 2, the corridor, gate 1, the ladder by the arrival tile.
        t.exec("leaveBase.walkToGate2", t.player.walk_to, 3540, 4597, 40)
        t.exec("leaveBase.gate2", t.player.cross_gate, { loc = "dov_base_gate_closed_2", at = { 3540, 4597, 0 },
            loc_level = 1, near = { 3540, 4597 }, far_ok = function(tl) return tl.x <= 3539 end,
            far_desc = "west of gate 2, x <= 3539" })
        t.exec("leaveBase.walkToGate1", t.player.walk_to, 3535, 4571, 60)
        t.exec("leaveBase.gate1", t.player.cross_gate, { loc = "dov_base_gate_closed_1", at = { 3536, 4571, 0 },
            loc_level = 1, near = { 3535, 4571 }, far_ok = function(tl) return tl.x >= 3536 end,
            far_desc = "east of gate 1, x >= 3536" })
        t.exec("leaveBase.walkToLadder", t.player.walk_to, 3560, 4551, 60)
        t.exec("leaveBase.ladder", t.player.climb, { loc = "dov_base_exit", op = 1, op_name = "Climb-up",
            at = { 3559, 4552, 0 }, loc_level = 1, dest = { 3342, 3515, 0 },
            same_level = "dov_elias.rs2 [oploc1,dov_base_exit] ~climb_ladder_to(^dov_dungeon_exit_coord)" })
        t.ticks(2)
        if t.chat.kind() ~= "none" then
            t.chat.drain({})
        end
        -- Back west through the Varrock members' gate on foot (reach.py 3342,3515 -> 3212,3460 is
        -- NEEDS-DOOR via fai_varrock_member_gatel@3319,3468).
        t.exec("goto-leaveBase.memberGate", t.player.goto_tile, 3321, 3468, 0)
        t.exec("leaveBase.memberGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3320, 3468 }, far = { 3318, 3468 } })

        -- ================= LEG 3: the palace, Captain Rovin, Camdozaal =================
        local CASTLE_DOOR, CASTLE_DOOR_OPEN = "fai_varrock_castle_door", "fai_varrock_castle_door_open"
        local function palace_in(pfx)
            t.exec("goto-" .. pfx, t.player.goto_tile, 3212, 3460, 0)
            t.exec(pfx .. ".walkIn", t.player.walk_route, { { 3212, 3468 }, { 3211, 3475 }, { 3207, 3476 } })
        end
        -- Up the north-west tower to Captain Rovin (routes as test/quests/demon.lua).
        local function rovin_up(pfx, step1, step2)
            t.exec(pfx .. ".walkToTower", t.player.walk_route, { { 3207, 3476 }, { 3205, 3478 }, { 3205, 3486 },
                { 3203, 3491 }, { 3203, 3493 } })
            t.exec(pfx .. ".towerDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3203, 3493, 0 }, near = { 3203, 3493 }, far = { 3203, 3496 } })
            t.exec(step1, t.player.climb, { loc = "varrock_spiralstairs_taller", op = 1, op_name = "Climb-up",
                at = { 3202, 3497, 0 }, src = { 3203, 3496 }, dest = { 3204, 3497, 1 } })
            t.exec(step2, t.player.climb, { loc = "varrock_spiralstairs_middle_taller", op = 2,
                op_name = "Climb-up", at = { 3202, 3497, 1 }, src = { 3204, 3497 }, dest = { 3204, 3497, 2 } })
        end
        local function rovin_down(pfx, step1, step2)
            t.exec(step1 or (pfx .. ".stairsDown"), t.player.climb, { loc = "varrock_spiralstairstop", op = 1,
                op_name = "Climb-down", at = { 3202, 3497, 2 }, src = { 3204, 3497 }, dest = { 3203, 3496, 1 } })
            t.exec(step2 or (pfx .. ".stairsDown2"), t.player.climb, { loc = "varrock_spiralstairs_middle_taller", op = 3,
                op_name = "Climb-down", at = { 3202, 3497, 1 }, src = { 3204, 3497 }, dest = { 3203, 3496, 0 } })
            t.exec(pfx .. ".towerDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
                at = { 3203, 3493, 0 }, near = { 3203, 3494 }, far = { 3203, 3492 } })
        end
        local function palace_out(pfx)
            t.exec(pfx .. ".walkOutOfPalace", t.player.walk_route, { { 3205, 3486 }, { 3205, 3478 }, { 3207, 3476 },
                { 3211, 3475 }, { 3212, 3468 }, { 3212, 3460 } })
        end

        -- talkToEliasInPalace (28 -> 30): Elias outside Sir Prysin's room takes you up to Rovin, who
        -- sends you to Ramarno (talkToRovin, 30 -> 32).
        palace_in("talkToEliasInPalace")
        t.exec("talkToEliasInPalace.talk", t.player.talk_to, "elias_white_vis", 1)
        converse("talkToEliasInPalace", {}, { "How did things go after I left you?",
            "Come, let's fill him in", "You head upstairs with Elias.", "About time, adventurer.",
            "Zemouregal has his heart.", "The Imcando Dwarves of Camdozaal.", "speak to this Ramarno" })
        -- The one conversation passes 30 (Elias, dov_elias.rs2 @dov_elias_palace_report) and ends at 32
        -- (Rovin, dov_rovin.rs2 @dov_rovin_send_to_forge).
        t.check("talkToRovin", stage() == 32, "varb9655_dov=" .. tostring(stage())
            .. " after Elias took the player up and Rovin sent them to Ramarno")
        at_stage("rovin_forge_sent", 32)
        local _, upstairs = t.world.tile()
        t.check("talkToRovin.upstairs", type(upstairs) == "table" and upstairs.level == 2,
            "Elias took the player to Rovin's floor: " .. tostring(type(upstairs) == "table"
                and (upstairs.x .. "," .. upstairs.z .. "," .. upstairs.level) or upstairs))
        rovin_down("enterCamdozaal")
        palace_out("enterCamdozaal")

        -- enterCamdozaal: west of Ice Mountain; Below Ice Mountain done, the Ruins Entrance lets you
        -- straight in (belowicemountain.rs2 @bim_enter_dungeon p_telejump(^bim_dungeon_coord 2952,5764)).
        t.exec("goto-enterCamdozaal", t.player.goto_tile, 2996, 3494, 0)
        t.exec("enterCamdozaal", t.player.climb, { loc = "bim_entrance", op = 1, op_name = "Enter",
            at = { 2999, 3493, 0 }, dest = { 2952, 5764, 0 }, slack = 1,
            same_level = "belowicemountain.rs2 @bim_enter_dungeon p_telejump(^bim_dungeon_coord)" })
        t.ticks(3)

        -- talkToRamarno: the first word with him is Below Ice Mountain's own introduction (::complete
        -- leaves varb12068 at 0, so Ramarno stands by the entrance, camdozaal_ramarno_entrance_multi)
        -- (dov_camdozaal.rs2 -> belowicemountain.rs2 @bim_ramarno_intro, varb12068 0 -> 1); then he waits
        -- at his workshop by the Sacred Forge (camdozaal_ramarno_multi 2959,5809).
        t.exec("talkToRamarno.intro", t.player.talk_to, "camdozaal_ramarno_entrance_multi", 1)
        t.chat.drain({})
        t.exec("talkToRamarno.introSet", t.var.await_server, "varb12068_camdozaal_ramarno_intro", 1, 6)
        t.exec("talkToRamarno.walk", t.player.walk_to, 2959, 5807, 60)
        t.exec("talkToRamarno.talk", t.player.talk_to, "camdozaal_ramarno_multi", 1)
        converse("talkToRamarno", { "I need your help with a shield." }, { "Hello again.",
            "choose:I need your help with a shield.", "The Sacred Forge of course!",
            "imbue it using the core of a chaos golem" })
        at_stage("ramarno", 34)

        -- mineBarronite: the barronite rocks west of the forge (2941,5810).
        t.exec("mineBarronite", t.player.click_loc, "camdozaalrock1", 1, { at = { 2941, 5810, 0 } })
        t.exec("mineBarronite.got", t.inv.await, "camdozaal_barronite_deposit", 1, 10)

        -- killChaosGolems: the rubble of the eastern cavern (m47_90.spawn) awakens as a chaos golem.
        t.exec("goto-killChaosGolems", t.player.goto_tile, 3013, 5777, 0)
        prayer("killChaosGolems.protectMelee", true)
        t.exec("killChaosGolems.awaken", t.player.press, "camdozaal_golem_chaos_rock", 1)
        fight("killChaosGolems", "camdozaal_golem_chaos")
        prayer("killChaosGolems.prayerOff", false)
        t.exec("killChaosGolems.core", t.player.click_obj, "camdozaal_golem_core_chaos")
        t.exec("killChaosGolems.coreHeld", t.inv.await, "camdozaal_golem_core_chaos", 1, 6)

        -- useCoreOnDeposit, then useBarroniteOnForge: the Dream Theatre (34 -> 36).
        t.exec("useCoreOnDeposit", t.player.use_item_on_item, "camdozaal_golem_core_chaos", "camdozaal_barronite_deposit")
        t.exec("useCoreOnDeposit.got", t.inv.await, "dov_imbued_barronite", 1, 6)
        t.chat.drain({})
        t.exec("goto-useBarroniteOnForge", t.player.goto_tile, 2959, 5807, 0)
        t.exec("useBarroniteOnForge", t.player.use_on, "dov_imbued_barronite",
            t.player.by_symbol("loc", "bim_ruins_wallkit_sacred_forge_multi"))
        converse("useBarroniteOnForge-dreamTheatre", {}, { "Arrav?", "Only one with the blood of the founder",
            "A snooping adventurer!", "Well? How did it go?", "I know how to use the shield!" })
        at_stage("forge_done", 36)

        -- Out by the Ruins Exit (belowicemountain.rs2 [oploc1,bim_exit] p_telejump(^bim_entrance_landing)).
        t.exec("goto-leaveCamdozaal", t.player.goto_tile, 2952, 5763, 0)
        t.exec("leaveCamdozaal", t.player.climb, { loc = "bim_exit", op = 1, op_name = "Exit",
            at = { 2951, 5761, 0 }, dest = { 2996, 3494, 0 }, slack = 1,
            same_level = "belowicemountain.rs2 [oploc1,bim_exit] p_telejump(^bim_entrance_landing)" })

        -- @@LEG4@@
        -- ================= LEG 4: Rovin's shield, Reldo, the list and the census =================
        palace_in("talkToRovinAfterForge")
        rovin_up("talkToRovinAfterForge", "goToF1ForRovinNonInstance", "goToF2ForRovin")
        t.exec("talkToRovinAfterForge.talk", t.player.talk_to, "captain_rovin", 1)
        converse("talkToRovinAfterForge", {}, { "About time you showed up!", "only a descendant of Varrock's original founder",
            "see if Reldo can help you", "Captain Rovin gives you the Shield of Arrav." })
        t.exec("talkToRovinAfterForge.shield", t.inv.await, "dov_shield_of_arrav", 1, 6)
        at_stage("rovin_invasion", 40)

        -- talkToReldo: down the tower, along the library corridor, in through the library door 3210,3490.
        rovin_down("talkToReldo", "goToF1ForReldo", "goToF0ForReldo")
        t.exec("talkToReldo.walkToLibrary", t.player.walk_route, { { 3206, 3490 }, { 3207, 3488 }, { 3210, 3488 },
            { 3210, 3489 } })
        t.exec("talkToReldo.libraryDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3210, 3490, 0 }, near = { 3210, 3489 }, far = { 3210, 3491 } })
        t.exec("talkToReldo.talk", t.player.talk_to, "reldo", 1)
        converse("talkToReldo", {}, { "Oh wonderful... he's gone.", "Surok Magis.",
            "a document on the desk in the corner", "consult the latest census" })
        at_stage("reldo", 42)

        -- searchScrolls: the scrolls by the flipped table in the north-east corner (3216,3496).
        t.exec("searchScrolls", t.player.click_loc, "dov_varrock_fallen_scrolls", 1, { at = { 3216, 3496, 0 } })
        converse("searchScrolls-objbox", {}, { "You find a fragment of a document" })
        t.exec("searchScrolls.list", t.inv.await, "dov_name_list", 1, 6)

        -- readList (42 -> 44): the list's own Read.
        t.exec("readList", t.player.inv_op, "dov_name_list", 1)
        converse("readList-text", {}, { "the Council of Avarrocka gathered", "Laris Gontamue" })
        at_stage("list_of_elders", 44)

        -- readCensus (44 -> 46): the Varrock Census on its lectern (3214,3497).
        t.exec("readCensus", t.player.click_loc, "dov_varrock_ledger", 1, { at = { 3214, 3497, 0 } })
        at_stage("census", 46)
        t.exec("readCensus.flag", t.var.await_server, "varb9668_dov_read_census", 1, 6)
        t.exec("readCensus.libraryDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3210, 3490, 0 }, near = { 3210, 3491 }, far = { 3210, 3489 } })

        -- @@LEG5@@
        -- ================= LEG 5: the descendants, in the quick guide's order =================
        -- Each is handed the shield ("You pass the shield to ..., but nothing happens."); the one the
        -- census roll made adopted names the Fitzharmons. Every flag is set by the end of its talk.
        local function descendant(name, npc, picks, flag)
            t.exec(name .. ".talk", t.player.talk_to, npc, 1)
            local want = { "You pass the shield to" }
            if picks[1] then
                want = { "choose:" .. picks[1], "You pass the shield to" }
            end
            converse(name, picks, want)
            t.exec(name .. ".flag", t.var.await_server, flag, 1, 6)
        end

        -- talkToAeonisigOutsideInstance / talkToRoaldOutsideInstance: the throne room, door 3218,3472.
        t.exec("talkToAeonisigOutsideInstance.walk", t.player.walk_route, { { 3210, 3488 }, { 3207, 3488 },
            { 3205, 3486 }, { 3205, 3478 }, { 3207, 3476 }, { 3211, 3475 }, { 3215, 3472 }, { 3217, 3472 } })
        t.exec("talkToAeonisigOutsideInstance.throneDoorIn", t.player.pass_door, { closed = CASTLE_DOOR,
            open = CASTLE_DOOR_OPEN, at = { 3218, 3472, 0 }, near = { 3217, 3472 }, far = { 3219, 3472 } })
        descendant("talkToAeonisigOutsideInstance", "myq3_aeonisig_roalds_advisor", {}, "varb9670_dov_shield_aeonisig")
        descendant("talkToRoaldOutsideInstance", "king_roald", {}, "varb9669_dov_shield_roald")
        t.exec("talkToRoaldOutsideInstance.throneDoorOut", t.player.pass_door, { closed = CASTLE_DOOR,
            open = CASTLE_DOOR_OPEN, at = { 3218, 3472, 0 }, near = { 3219, 3472 }, far = { 3217, 3472 } })

        -- talkToPrysinOutsideInstance: his room in the south-west corner, door 3207,3472.
        t.exec("talkToPrysinOutsideInstance.walk", t.player.walk_route, { { 3214, 3472 }, { 3211, 3472 },
            { 3208, 3472 } })
        t.exec("talkToPrysinOutsideInstance.doorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3207, 3472, 0 }, near = { 3207, 3472 }, far = { 3205, 3472 } })
        descendant("talkToPrysinOutsideInstance", "sir_prysin", {}, "varb9671_dov_shield_prysin")
        t.exec("talkToPrysinOutsideInstance.doorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3207, 3472, 0 }, near = { 3206, 3472 }, far = { 3208, 3472 } })
        t.exec("talkToRomeo.walkOutOfPalace", t.player.walk_route, { { 3211, 3472 }, { 3212, 3468 }, { 3212, 3460 } })

        -- talkToRomeo: Varrock Square.
        t.exec("goto-talkToRomeo", t.player.goto_tile, 3211, 3428, 0)
        descendant("talkToRomeo", "romeo", {}, "varb9673_dov_shield_romeo")

        -- talkToHorvik: his armour shop north-east of the square (open doorway; reach.py REACH).
        t.exec("goto-talkToHorvik", t.player.goto_tile, 3229, 3432, 0)
        descendant("talkToHorvik", "horvik_the_armourer", { "I need your help with a shield." }, "varb9672_dov_shield_horvik")

        -- talkToHalen: Curator Haig in the Varrock Museum (reach.py from the square: REACH, no closed door).
        t.exec("goto-talkToHalen", t.player.goto_tile, 3255, 3440, 0)
        descendant("talkToHalen", "curator", { "I need your help with the zombie invasion." }, "varb9676_dov_shield_haig")
        at_stage("candidates_done", 48)

        -- @@LEG6@@
        -- ================= LEG 6: Dimintheis and the finish =================
        -- talkToDimintheis: Varrock's south-east quarter, in by the east gate guidorgatelclosed 3264,3405
        -- (east_gate.rs2 walk-through; routes as test/quests/crest.lua) and his house's west door 3278,3404.
        t.exec("goto-talkToDimintheis.eastGate", t.player.goto_tile, 3262, 3405, 0)
        t.exec("talkToDimintheis.eastGateIn", t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
            near = { 3263, 3405 }, far_ok = function(tl) return tl.x >= 3264 end,
            far_desc = "inside Varrock's south-east quarter, x >= 3264" })
        t.exec("talkToDimintheis.houseDoorIn", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3278, 3404, 0 }, near = { 3277, 3404 }, far = { 3279, 3404 } })
        t.exec("talkToDimintheis.talk", t.player.talk_to, "dimintheis", 1)
        converse("talkToDimintheis", { "Other", "I need your help with the zombie invasion." }, {
            "choose:Other", "choose:I need your help with the zombie invasion.",
            "it magically responds to his touch", "Quick, let's get to the palace!", "Die, zombie scum!",
            "It is time for us to head north." })
        at_stage("finish_ready", 52)
        t.exec("talkToDimintheis.houseDoorOut", t.player.pass_door, { closed = CASTLE_DOOR, open = CASTLE_DOOR_OPEN,
            at = { 3278, 3404, 0 }, near = { 3279, 3404 }, far = { 3276, 3404 } })
        t.exec("talkToDimintheis.eastGateOut", t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
            near = { 3264, 3405 }, far_ok = function(tl) return tl.x <= 3263 end, far_desc = "back in Varrock, x <= 3263" })

        -- finishQuest: Captain Rovin and Elias (goToF1ToFinish, goToF2ToFinish).
        palace_in("finishQuest")
        rovin_up("finishQuest", "goToF1ToFinish", "goToF2ToFinish")
        local snap_r, snap = t.skill.snapshot()
        t.check("finishQuest.snapshot", snap_r == "ok", "skill.snapshot -> " .. tostring(snap_r))
        t.exec("finishQuest.talk", t.player.talk_to, "captain_rovin", 1)
        converse("finishQuest", {}, { "not a single zombie remains!", "without Dimintheis.", "Elias departs.",
            "Today's victory will go down in history." })
        t.quest.expect_complete()
        t.expect("reward.smithing", t.skill.expect_gain("smithing", 15000, snap))
        t.expect("reward.hunter", t.skill.expect_gain("hunter", 15000, snap))
        t.finish(0)
    end,
}
