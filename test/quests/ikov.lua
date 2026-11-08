-- Temple of Ikov (Quest Helper helpers/quests/templeofikov/TempleOfIkov.java), the Armadyl ending:
-- Lucien -> boots of lightness -> lever piece -> bracket + mended lever -> ice arrows from the chests ->
-- trap lever (search, pull) -> Fire Warrior of Lesarkus (real ranged fight, ice arrow worn) -> Winelda
-- (20 limpwurts) -> shiny key -> secret wall -> Guardians (cleansing) -> Varrock Teleport out -> kill
-- Lucien in his house west of the Grand Exchange.
-- Brought along (guide getItemRequirements): a throwable weapon (rune darts), 20 limpwurts, a knife and a light
-- source. Ice arrows, the boots, the lever and the key are all gathered.
--
-- RE-DRIVE b63 (door rule, docs/QUEST_ORCHESTRATOR.md 2026-10-03). goto_tile is used only for plain travel
-- between open tiles: the first placement (Lumbridge -> the open ground south of the members' gate membergater
-- 2933,3320, crossed by t.player.cross_gate; b68), the hop on to the street outside the Flying Horse Inn, the overland
-- hop from Ardougne to the temple's entrance ladder, and Varrock (after a real Varrock Teleport) -> the street
-- outside Lucien's house. Everything else is walked and every obstacle pressed, both ways:
--   * the Flying Horse Inn door (poshdoor 2576,3320) in and out;
--   * the temple ladder (ladder_cellar, +6400), the dark stairs down/up (ikov_dungeon.rs2:53-62 p_teleport),
--     by t.player.climb;
--   * the web over the boots' pocket (bigweb_slashable 2654,9766, web.rs2: a knife used on it, half the cuts
--     fail) slashed and walked through going in and coming out;
--   * the north gate (Chamber of Fear), the castle double door to the lever room, the south gate (arrow room),
--     the trap-lever door, the fire warrior door and the secret wall, each by its own press;
--   * the ice area walked (ice spiders and all) to every chest and back.
-- Bridge weight (ikov_dungeon.rs2 [label,ikov_movebridge] `weight >= 0` breaks it): knife 453 + torch 500 +
-- 20 limpwurts 140 + 2 sharks 1300 + pendant 10 + worn boots -4535 = -2132 g (+56 with the lever); darts
-- and runes are stackable and weigh nothing (torirs_server_world.c player_weight_grams). The pack is full at
-- setup (28): the darts are wielded and the pendant worn at once so the pendant, boots, lever, ice arrows and
-- key each find a slot. Two sharks are all the food that fits, so the Fire Warrior (fire blast) is fought under
-- Protect from Magic (prayer 37 staged, points read first, lit before the door press, put out after).

-- Fight margin (fixer brief): lowest hp at least a quarter of the 99 staged hitpoints AND food left.
local FOOD = "shark"
-- One threshold for every walk and the fire warrior: the ice spiders, the north room's skeletons and
-- scorpions and the key path's demons take 50-75 hp in all (runs 2-8), and the pack holds two sharks.
local EAT_BELOW = 35
local VITALS = { eat = FOOD, below = EAT_BELOW }
local WEB_AT = { 2654, 9766, 0 }

return {
    id = "ikov",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel thieving 42",
        "::setlevel ranged 80",
        "::setlevel defence 60",
        "::setlevel hitpoints 99",
        "::setlevel prayer 37", -- Protect from Magic for the Fire Warrior (he casts fire blast: ikov_firewarrior.rs2 [ai_applayer2])
        "::give knife 1",
        "::give torch_lit 1",
        "::give rune_dart 100",
        "::give limpwurt_root 20",
        "::give poh_tablet_varrockteleport 1", -- 4483 [1011]: the way the recorder leaves for Lucien's house
        "::give shark 2",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp26_ikov",
            constants = {
                not_started = 0,
                started = 10,
                disabled_trap = 20,
                pulled_lever = 30,
                defeated_fire_warrior = 40,
                spoken_winelda = 50,
                paid_winelda = 60,
                helping_armadyl = 70,
                complete = 80,
            },
            row = "quest_templeofikov",
            display = "Temple of Ikov",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        do local _, d = t.ticklog.start(); t.note("ticklog: " .. tostring(d)) end
        -- the pack is full (28); the darts go on now so Lucien's pendant has a slot
        t.exec("wield-throwableWeapon", t.player.equip, "rune_dart")

        -- One fight's margin from await_dead_engaged's eat detail (combat.lua: `lowest hp x/y`).
        local function margin_row(name, fight, detail)
            local lowest = tonumber(tostring(detail):match("lowest hp (%-?%d+)/"))
            local food_result, food_left = t.inv.count(FOOD)
            t.check(name, lowest ~= nil and lowest >= 25 and food_result == "ok",
                fight .. ": lowest hp " .. tostring(lowest) .. "/99 (staged ::setlevel hitpoints 99), " .. FOOD
                    .. " staged 2, left " .. tostring(food_left) .. " (" .. tostring(food_result) .. ")"
                    .. " -- margin: lowest hp >= 25")
        end

        -- The web over the boots' pocket: web.rs2 [oplocu,bigweb_slashable] (knife allowed), random(2) -- a
        -- failed cut says "You fail to cut through it." -- and loc_change(bigweb_slashed, 100). Each cut is an
        -- attempt (t.note); the row is the web's own state on its tile.
        -- The camera reader is client-only: on scriptrun t.cutscene.mark() is nil and the row is skipped.
        local function cutscene_row(row, name, spec)
            if t.cutscene.mark() == nil then
                t.note(row .. ": no camera reader on this lane, skipped")
                return
            end
            t.exec(row, t.cutscene.await, name, spec)
        end

        local function slash_web(tag)
            local cuts = 0
            local web_left = t.world.loc_near("bigweb_slashable", 3, { at = WEB_AT, slack = 0 }) == "ok"
            while web_left and cuts < 8 do
                cuts = cuts + 1
                local web = t.player.by_symbol("loc", "bigweb_slashable")
                local cut_result, cut_detail = t.player.use_on("knife", web)
                local _, lines = t.msg.last(2)
                local text = ""
                for i = 1, #(lines or {}) do text = text .. " | " .. tostring(lines[i].text) end
                t.note(tag .. " cut " .. cuts .. ": use knife on the web -> " .. tostring(cut_result) .. " "
                    .. tostring(cut_detail) .. text)
                t.ticks(2)
                web_left = t.world.loc_near("bigweb_slashable", 3, { at = WEB_AT, slack = 0 }) == "ok"
            end
            local slashed_result = t.world.loc_near("bigweb_slashed", 3, { at = WEB_AT, slack = 0 })
            t.check(tag .. ".slashWeb", (not web_left) and slashed_result == "ok",
                "bigweb_slashed on 2654,9766,0: " .. tostring(slashed_result) .. ", bigweb_slashable left: "
                    .. tostring(web_left) .. " after " .. cuts .. " knife cut(s)"
                    .. (cuts == 0 and " (the web still stood slashed)" or ""))
        end

        -- ---------------------------------------------------------------- talkToLucien
        -- First placement (owner ruling 2026-10-05: the first goto obeys the door rule): the only walk from the
        -- Lumbridge fixture to Ardougne goes through the members' gate membergater 2933,3320 (reach.py 3206,3233 ->
        -- 2578,3320: UNREACHABLE at 30/80/160, NEEDS-DOOR via membergater@2933,3320 at 250). So the run lands on the
        -- open ground SOUTH of that gate (reach.py 3206,3233 -> 2933,3318: REACH closed-doors len=388 at 30/80/160),
        -- crosses the walk-through gate by its verb (gates.rs2 [label,member_fencegate_try]), graded on the tiles,
        -- then travels overland to the open street east of the Flying Horse Inn's south door (reach.py 2933,3322 ->
        -- 2578,3320: REACH closed-doors len=797 at margin 250: the walk only fits the wider flood box, no door on it).
        t.exec("goto-memberGate", t.player.goto_tile, 2933, 3318, 0)
        t.exec("talkToLucien.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
            near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
        t.exec("goto-talkToLucien", t.player.goto_tile, 2578, 3320, 0)
        t.exec("talkToLucien.innDoorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2576, 3320, 0 }, near = { 2576, 3320 }, far = { 2575, 3320 } })
        t.exec("talkToLucien", t.player.talk_to, "ikov_lucien1", 1)
        t.exec("talkToLucien-dialog", t.chat.play, {
            -- RSProx index 4478 [711]..[727]: the Why route; no opening menu, no
            -- skill warning (the setup has Thieving 42 and Ranged 40).
            "npc:I seek a hero to go on an impo",
            "player:I am a mighty hero!",
            "npc:I require the Staff of Armadyl",
            "npc:Take care hero! There is a dan",
            "choose:Why can't you get it yourself?",
            "player:Why can't you get it yourself?",
            "npc:The guardians of the Staff of",
            "npc:They have set up a magical",
            "choose:Yes.",
            "player:That sounds like a laugh!",
            "npc:It's not as easy as it sounds.",
            "player:I'm up for it!",
            "npc:I shall give you a pendant.",
            "npc:Here is my pendant.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        -- The inn Lucien stays until the trap is found (ikov_lucien_vis = 0): the
        -- reminder at [735]..[739].
        t.exec("talkToLucien-again", t.player.talk_to, "ikov_lucien1", 1)
        t.exec("talkToLucien-again-dialog", t.chat.play, {
            "npc:I told you not to meet me here",
            "player:Sorry! Can you remind me",
            "npc:My patience grows thin hero!",
            "npc:I need the Staff of Armadyl.",
        })
        local _, pendant_count = t.inv.count("ikov_pendantoflucien")
        t.check("talkToLucien-pendant", pendant_count == 1, "pendant of Lucien in the pack: " .. tostring(pendant_count))
        -- The lost pendant, recorded 4478 [1718]..[1728]: a fresh face a tick after the page, reset when
        -- the player walks off.
        t.exec("lostPendant.drop", t.player.drop, "ikov_pendantoflucien")
        t.player.walk_to(2575, 3320, 12)
        t.ticks(2)
        t.exec("lostPendant.talk", t.player.talk_to, "ikov_lucien1", 1)
        t.exec("lostPendant.dialog", t.chat.play, {
            "npc:I told you not to meet me here again!",
            "player:I've lost the pendant",
            "npc:Imbecile!",
            "objbox:Lucien has given you another pendant",
        })
        t.exec("wear-pendantOfLucien", t.player.equip, "ikov_pendantoflucien")
        t.exec("leaveInn.innDoorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
            at = { 2576, 3320, 0 }, near = { 2575, 3320 }, far = { 2577, 3320 } })

        -- ---------------------------------------------------------------- enterDungeonForBoots
        -- Overland travel between open tiles (reach.py: REACH with every door closed).
        t.exec("goto-enterDungeonForBoots", t.player.goto_tile, 2677, 3404, 0)
        -- ladders.rs2 [oploc1,ladder_cellar]: p_telejump(movecoord(coord, 0, 0, 6400)) from the player's tile
        t.exec("enterDungeonForBoots", t.player.climb, { loc = "ladder_cellar", op = 1, op_name = "Climb-down",
            at = { 2677, 3405, 0 }, src = { 2677, 3404 }, dest = { 2677, 9804, 0 }, slack = 1 })

        -- ---------------------------------------------------------------- goDownToBoots
        t.exec("walk-goDownToBoots", t.player.walk_route, { { 2669, 9806 }, { 2661, 9806 }, { 2653, 9808 }, { 2649, 9805 } })
        t.exec("goDownToBoots", t.player.climb, { loc = "ikov_darkstairsdown", op = 1, op_name = "Climb-down",
            at = { 2650, 9804, 0 }, src = { 2649, 9805 }, dest = { 2641, 9764, 0 },
            same_level = "ikov_dungeon.rs2:53-56 [label,ikov_stairs_down] p_teleport(0_41_152_17_36), a lit torch carried" })

        -- ---------------------------------------------------------------- getBoots
        t.exec("walk-getBoots", t.player.walk_route, { { 2649, 9766 }, { 2654, 9765 } })
        slash_web("getBoots")
        t.exec("getBoots.webIn", t.player.pass_door, { closed = "bigweb_slashable", open = "bigweb_slashed",
            at = WEB_AT, near = { 2654, 9765 }, far = { 2654, 9766 } })
        -- the floor pair is the worn variant; the pickup converts it to the carried one, so click_obj's own
        -- wait (for the floor variant's count) never sees a rise -- read the carried pair instead
        local boots_click_result, boots_click_detail = t.player.click_obj("ikov_bootsoflightnessworn", 3)
        local boots_await_result = t.inv.await("ikov_bootsoflightness", 1, 10)
        t.check("getBoots", boots_await_result == "ok", "click_obj " .. tostring(boots_click_result) .. " (" .. tostring(boots_click_detail) .. "); carried boots await " .. tostring(boots_await_result))
        t.exec("getBoots-pack", t.inv.expect_has, "ikov_bootsoflightness", 1)
        local equip_boots_result, equip_boots_detail = t.player._inv_dispatch("ikov_bootsoflightness", 2, "equip")
        t.check("getBoots-wear", equip_boots_result == "ok", tostring(equip_boots_result) .. " " .. tostring(equip_boots_detail))
        t.ticks(4)
        t.exec("getBoots-worn", t.inv.expect_absent, "ikov_bootsoflightness")

        -- ---------------------------------------------------------------- goUpFromBoots
        t.exec("walk-goUpFromBoots.webSide", t.player.walk_to, 2654, 9766, 10)
        slash_web("goUpFromBoots")
        t.exec("goUpFromBoots.webOut", t.player.pass_door, { closed = "bigweb_slashable", open = "bigweb_slashed",
            at = WEB_AT, near = { 2654, 9766 }, far = { 2654, 9765 } })
        t.exec("walk-goUpFromBoots", t.player.walk_route, { { 2646, 9766 }, { 2639, 9765 } })
        t.exec("goUpFromBoots", t.player.climb, { loc = "ikov_darkstairs", op = 1, op_name = "Climb-up",
            at = { 2638, 9763, 0 }, src = { 2639, 9765 }, dest = { 2649, 9805, 0 },
            same_level = "4478 [911]: ikov_darkstairs lands on 2649,9805 facing north" })

        -- ---------------------------------------------------------------- pickUpLever
        -- The north gate (ikov_dungeon.rs2 [label,ikov_northgate]): in from the south only with the pendant
        -- worn; out always. Pressed on every crossing.
        local function in_chamber(tile) return tile.z >= 9815 end
        local function south_of_north_gate(tile) return tile.z <= 9814 end
        -- The refusals the recorder met first: the south gate before the lever (4478 [869]) and the
        -- north gate without the pendant worn (4478 [880], 4483 [80]).
        t.exec("southGateShut.walk", t.player.walk_route, { { 2656, 9807 }, { 2662, 9805 } })
        t.exec("southGateShut", t.player.click_loc, "ikov_mendedleverdoorl", 1, { at = { 2662, 9803 } })
        t.exec("southGateShut-dialog", t.chat.play, { "mesbox:The door won't open!" })
        t.exec("northGateRefused.pendantOff", t.player.unequip, "ikov_pendantoflucien")
        t.exec("northGateRefused.walk", t.player.walk_route, { { 2662, 9809 }, { 2662, 9814 } })
        t.exec("northGateRefused", t.player.click_loc, "ikov_dooroffearl", 1, { at = { 2662, 9815 } })
        t.ticks(3)
        do
            local _, tile = t.world.tile()
            t.check("northGateRefused.stumble", tile.x == 2662 and tile.z == 9813, "thrown back to " .. tile.x .. "," .. tile.z)
        end
        t.exec("northGateRefused.pendantOn", t.player.equip, "ikov_pendantoflucien")
        t.exec("walk-northGate", t.player.walk_route, { { 2658, 9812 }, { 2662, 9814 } })
        t.exec("northGate", t.player.cross_gate, { loc = "ikov_dooroffearl", at = { 2662, 9815, 0 },
            near = { 2662, 9814 }, far_ok = in_chamber, far_desc = "in the Chamber of Fear, z >= 9815" })
        -- The bridge without the boots: the recorded fall (4478 [1160], [1280], [1358]):
        -- it gives way on the middle tile, drops the player on the far side at 2648,9826.
        local _, hp_before_fall = t.skill.read("hitpoints")
        -- The worn pair becomes the carried pair in the pack (ikov_bootsoflightness, the swap), so the
        -- driver's unequip reads "backpack 0->0"; the pack row below is the check.
        local boots_off_result, boots_off_detail = t.player.unequip("ikov_bootsoflightnessworn")
        t.note("bridgeFall.bootsOff " .. tostring(boots_off_result) .. " " .. tostring(boots_off_detail))
        t.exec("bridgeFall.bootsInPack", t.inv.expect_has, "ikov_bootsoflightness", 1)
        t.player.walk_to(2651, 9828, 20)
        -- The short deadline: a walk still in progress when the bridge throws the player
        -- re-clicks its destination and walks them back onto the bridge.
        t.player.walk_to(2649, 9828, 3)
        t.ticks(8)
        local bridge_x, bridge_z
        do
            local _, tile = t.world.tile()
            bridge_x, bridge_z = tile.x, tile.z
            local _, hp_after_fall = t.skill.read("hitpoints")
            local _, lines = t.msg.last(4)
            local text = ""
            for i = 1, #(lines or {}) do text = text .. " | " .. tostring(lines[i].text) end
            t.check("bridgeFall", tile.x == 2648 and tile.z == 9826
                and hp_before_fall.level - hp_after_fall.level >= 20
                and text:find("Good thing that lava was shallow!", 1, true) ~= nil,
                "landed " .. tile.x .. "," .. tile.z .. " hp " .. hp_before_fall.level .. " -> "
                    .. hp_after_fall.level .. text)
        end
        local equip_again_result, equip_again_detail = t.player._inv_dispatch("ikov_bootsoflightness", 2, "equip")
        t.check("bridgeFall.bootsOn", equip_again_result == "ok", tostring(equip_again_result) .. " " .. tostring(equip_again_detail))
        t.ticks(4)
        -- The landing is the pocket south of the lever room, joined only to the west ladder (4478
        -- [1285]..[1299]); the recorder climbed out and came back in by the first ladder.
        t.exec("bridgeFall.toLadder", t.player.walk_route, { { 2646, 9823 }, { 2643, 9817 }, { 2637, 9809 } }, { vitals = VITALS })
        t.exec("bridgeFall.ladderUp", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
            at = { 2637, 9808, 0 }, src = { 2637, 9809 }, dest = { 2637, 3409, 0 }, slack = 1 })
        t.exec("bridgeFall.backOverland", t.player.goto_tile, 2677, 3404, 0)
        t.exec("bridgeFall.ladderDown", t.player.climb, { loc = "ladder_cellar", op = 1, op_name = "Climb-down",
            at = { 2677, 3405, 0 }, src = { 2677, 3404 }, dest = { 2677, 9804, 0 }, slack = 1 })
        t.exec("bridgeFall.toGate", t.player.walk_route, { { 2669, 9806 }, { 2662, 9807 }, { 2662, 9814 } }, { vitals = VITALS })
        t.exec("bridgeFall.gateAgain", t.player.cross_gate, { loc = "ikov_dooroffearl", at = { 2662, 9815, 0 },
            near = { 2662, 9814 }, far_ok = in_chamber, far_desc = "in the Chamber of Fear, z >= 9815" })
        t.player.walk_to(2652, 9828, 20)
        t.ticks(4)
        t.player.walk_to(2647, 9828, 14)
        t.ticks(6)
        do
            local _, tile = t.world.tile()
            bridge_x, bridge_z = tile.x, tile.z
        end
        -- The fall lands in the south-west pocket that only reaches the ladder up
        -- (4478 [1285]..[1296]); back round to the chamber with a goto and the ladder.
        t.exec("goto-afterFall", t.player.goto_tile, 2677, 3404, 0)
        t.exec("reenterDungeon", t.player.climb, { loc = "ladder_cellar", op = 1, op_name = "Climb-down",
            at = { 2677, 3405, 0 }, src = { 2677, 3404 }, dest = { 2677, 9804, 0 }, slack = 1 })
        t.exec("walk-northGate-2", t.player.walk_route, { { 2669, 9806 }, { 2662, 9810 }, { 2662, 9814 } })
        t.exec("northGate-2", t.player.cross_gate, { loc = "ikov_dooroffearl", at = { 2662, 9815, 0 },
            near = { 2662, 9814 }, far_ok = in_chamber, far_desc = "in the Chamber of Fear, z >= 9815" })
        t.player.walk_to(2652, 9828, 20)
        t.ticks(4)
        t.player.walk_to(2647, 9828, 14)
        t.ticks(6)
        do
            local _, tile = t.world.tile()
            bridge_x, bridge_z = tile.x, tile.z
        end
        t.check("pickUpLever-bridge", bridge_x <= 2647, "crossed the lava bridge wearing the boots, now at " .. bridge_x .. "," .. bridge_z)
        -- seam29: the lever room is behind a closed castle double door (castledoubledoorl/r at
        -- 2645,9828/9829, wall on the west edge; LostCity maps/m41_153.jm2 has its doors at the same tiles).
        t.exec("openLeverRoomDoor", t.player.pass_door, { closed = "castledoubledoorl", open = "opencastledoubledoorl",
            at = { 2645, 9828, 0 }, near = { 2645, 9828 }, far = { 2644, 9828 } })
        t.exec("pickUpLever", t.player.click_obj, "ikov_lever", 3)
        t.exec("pickUpLever-pack", t.inv.expect_has, "ikov_lever", 1)

        -- ---------------------------------------------------------------- leave the chamber, useLeverOnHole, pullLever
        t.exec("leaveLeverRoomDoor", t.player.pass_door, { closed = "castledoubledoorl", open = "opencastledoubledoorl",
            at = { 2645, 9828, 0 }, near = { 2644, 9828 }, far = { 2645, 9828 } })
        t.player.walk_to(2652, 9828, 20)
        t.ticks(2)
        local recross_x, recross_z
        do
            local _, tile = t.world.tile()
            recross_x, recross_z = tile.x, tile.z
        end
        local _, recent_lines = t.msg.last(3)
        local recent_text = ""
        for i = 1, #recent_lines do recent_text = recent_text .. " | " .. recent_lines[i].text end
        t.check("pickUpLever-recross", recross_x >= 2651, "crossed back east over the bridge carrying the lever, now at " .. recross_x .. "," .. recross_z .. recent_text)
        t.exec("walk-gateInside", t.player.walk_route, { { 2657, 9828 }, { 2661, 9822 }, { 2661, 9816 } })
        t.exec("exitNorthGate", t.player.cross_gate, { loc = "ikov_dooroffearr", at = { 2661, 9815, 0 },
            near = { 2661, 9816 }, far_ok = south_of_north_gate, far_desc = "south of the north gate, z <= 9814" })
        t.exec("walk-useLeverOnHole", t.player.walk_route, { { 2665, 9810 }, { 2671, 9805 } })
        t.exec("useLeverOnHole", t.player.use_on, "ikov_lever", (t.player.by_symbol("loc", "ikov_leverbracket")))
        t.exec("useLeverOnHole-dialog", t.chat.play, { "mesbox:You fit the lever into the bracket." })
        t.exec("useLeverOnHole-gone", t.inv.expect_absent, "ikov_lever")
        t.exec("pullLever", t.player.click_loc, "ikov_mendedlever", 1)
        t.ticks(8) -- recorded 4478 [2121]..[2127]: the box comes six ticks after the click
        t.exec("pullLever-dialog", t.chat.play, { "mesbox:You hear the clunking of some hidden machinery." })

        -- ---------------------------------------------------------------- enterArrowRoom, collectArrows
        -- The south gate (ikov_dungeon.rs2 [label,ikov_southgate]) opens from the north once the mended lever
        -- has been pulled; it is pressed both ways.
        t.exec("walk-enterArrowRoom", t.player.walk_route, { { 2666, 9805 }, { 2662, 9804 } })
        t.exec("enterArrowRoom", t.player.cross_gate, { loc = "ikov_mendedleverdoorl", at = { 2662, 9803, 0 },
            near = { 2662, 9804 }, far_ok = function(tile) return tile.z <= 9802 end,
            far_desc = "south of the south gate, z <= 9802" })

        -- ---------------------------------------------------------------- collectArrows
        -- The six ice-area chests are dead-end alcoves of the arrow room (m42_153.jl2 loc 103); only ONE
        -- holds arrows at a time (ikov_dungeon.rs2 ikov_chest_pay), so open and search until a search pays.
        -- One ice arrow is enough with a throwable weapon (guide hasArrowsWithThrowableWeapon). The ice area is
        -- walked (reach.py's closed-door path, hops <= 8): to the first chest, chest to chest, and from the
        -- chest that paid back to the south gate through the junction 2711,9844.
        local chest_stands = {
            { 2745, 9822, 2745, 9821 },
            { 2739, 9835, 2738, 9835 },
            { 2746, 9848, 2747, 9848 },
            { 2719, 9839, 2719, 9838 },
            { 2710, 9849, 2710, 9850 },
            { 2729, 9849, 2729, 9850 },
        }
        local chest_routes = {
            { { 2670, 9798 }, { 2678, 9797 }, { 2686, 9801 }, { 2687, 9809 }, { 2691, 9817 }, { 2694, 9825 },
              { 2694, 9833 }, { 2695, 9841 }, { 2703, 9841 }, { 2711, 9844 }, { 2719, 9844 }, { 2727, 9844 },
              { 2735, 9844 }, { 2743, 9843 }, { 2745, 9835 }, { 2745, 9827 }, { 2745, 9822 } },
            { { 2744, 9830 }, { 2739, 9835 } },
            { { 2742, 9843 }, { 2746, 9848 } },
            { { 2738, 9845 }, { 2730, 9844 }, { 2722, 9844 }, { 2719, 9839 } },
            { { 2711, 9844 }, { 2710, 9849 } },
            { { 2718, 9846 }, { 2726, 9847 }, { 2729, 9849 } },
        }
        local to_junction = {
            { { 2744, 9830 }, { 2743, 9838 }, { 2735, 9842 }, { 2727, 9844 }, { 2719, 9844 }, { 2711, 9844 } },
            { { 2732, 9843 }, { 2724, 9844 }, { 2716, 9844 }, { 2711, 9844 } },
            { { 2738, 9845 }, { 2730, 9844 }, { 2722, 9844 }, { 2714, 9844 }, { 2711, 9844 } },
            { { 2711, 9844 } },
            { { 2711, 9844 } },
            { { 2721, 9846 }, { 2713, 9845 }, { 2711, 9844 } },
        }
        local junction_to_gate = { { 2703, 9841 }, { 2695, 9841 }, { 2694, 9833 }, { 2694, 9825 }, { 2692, 9817 },
            { 2687, 9809 }, { 2686, 9801 }, { 2678, 9797 }, { 2670, 9798 }, { 2662, 9798 }, { 2662, 9802 } }
        local WALK_VITALS = VITALS
        local arrows_found = 0
        local paid_chest = nil
        for chest_index = 1, #chest_stands do
            local stand = chest_stands[chest_index]
            t.exec("walk-collectArrows-" .. chest_index, t.player.walk_route, chest_routes[chest_index], { vitals = WALK_VITALS })
            t.exec("collectArrows-open-" .. chest_index, t.player.click_loc, "ikov_chestclosed", 1, { at = { stand[3], stand[4] } })
            t.ticks(2)
            t.exec("collectArrows-search-" .. chest_index, t.player.click_loc, "ikov_chestopen", 1, { at = { stand[3], stand[4] } })
            t.ticks(3)
            if t.chat.kind() ~= "none" then
                t.exec("collectArrows-found-" .. chest_index, t.chat.play, { "*" })
            end
            t.ticks(2)
            local _, arrow_total = t.inv.count("ice_arrow")
            arrows_found = arrow_total
            if arrow_total > 0 then
                paid_chest = chest_index
                break
            end
        end
        t.check("collectArrows", arrows_found > 0, "ice arrows in the pack after searching the chests: "
            .. tostring(arrows_found) .. " (chest " .. tostring(paid_chest) .. " paid)")

        -- ---------------------------------------------------------------- returnToMainRoom
        if paid_chest then
            t.exec("walk-returnToMainRoom.junction", t.player.walk_route, to_junction[paid_chest], { vitals = WALK_VITALS })
        end
        t.exec("walk-returnToMainRoom", t.player.walk_route, junction_to_gate, { vitals = WALK_VITALS })
        t.exec("returnToMainRoom", t.player.cross_gate, { loc = "ikov_mendedleverdoorl", at = { 2662, 9803, 0 },
            near = { 2662, 9802 }, far_ok = function(tile) return tile.z >= 9803 end,
            far_desc = "north of the south gate, z >= 9803" })

        -- ---------------------------------------------------------------- goSearchThievingLever
        t.exec("walk-northGate-again", t.player.walk_route, { { 2662, 9809 }, { 2662, 9814 } })
        t.exec("northGate-again", t.player.cross_gate, { loc = "ikov_dooroffearl", at = { 2662, 9815, 0 },
            near = { 2662, 9814 }, far_ok = in_chamber, far_desc = "in the Chamber of Fear, z >= 9815" })
        local lever_route = { { 2666, 9824 }, { 2666, 9836 }, { 2665, 9847 }, { 2665, 9853 } }
        t.exec("walk-goSearchThievingLever.route", t.player.walk_route, lever_route, { max_hop = 12, vitals = VITALS })
        do
            local _, tile = t.world.tile()
            t.check("walk-goSearchThievingLever", math.abs(tile.x - 2665) <= 3 and tile.z >= 9850,
                "walked through the north room to the trap lever: " .. tile.x .. "," .. tile.z)
        end
        -- The armed pull: recorded 4478 [2026]..[2036] and [2609]..[2617]: the lever,
        -- the fall to the pit at 2682,9855, a step south, 20 damage; the ladder back
        -- (4478 [2039]). Before the search, the only time it can happen.
        do
            local _, hp_before = t.skill.read("hitpoints")
            local pull_armed_result, pull_armed_detail = t.player.click_loc("ikov_traplever", 1)
            t.note("armed pull -> " .. tostring(pull_armed_result) .. " " .. tostring(pull_armed_detail))
            t.ticks(12)
            local _, tile = t.world.tile()
            local _, hp_after = t.skill.read("hitpoints")
            -- 20 damage, read 12 ticks apart: a point of regeneration can fall between the two reads.
            t.check("trapArmed.fall", tile.x == 2682 and tile.z == 9854 and hp_before.level - hp_after.level >= 19 and hp_before.level - hp_after.level <= 20,
                "landed " .. tile.x .. "," .. tile.z .. " hp " .. hp_before.level .. " -> " .. hp_after.level)
            t.expect("trapArmed.stage", t.quest.expect_stage("started"))
            t.player.walk_to(2682, 9850, 10)
            t.exec("trapArmed.ladder", t.player.climb, { loc = "ikov_trapladder", op = 1, op_name = "Climb-up",
                at = { 2682, 9849, 0 }, src = { 2682, 9850 }, dest = { 2665, 9849, 0 },
                same_level = "4478 [2040]: the pit's ladder lands on 2665,9849" })
            t.exec("walk-backToLever", t.player.walk_route, { { 2665, 9853 } }, { vitals = VITALS })
        end

        -- The trapdoor south of the lever: the refused open (4478 [2020]).
        t.exec("trapdoorRefused", t.player.click_loc, "ikov_trapdoor", 1, { at = { 2665, 9849 } })
        t.exec("trapdoorRefused-dialog", t.chat.play, { "mesbox:You try to open the trapdoor" })
        t.exec("goSearchThievingLever", t.player.click_loc, "ikov_traplever", 2)
        t.exec("goSearchThievingLever-dialog", t.chat.play, { "mesbox:You find a trap on the lever! You disable" })
        t.expect("quest.stage.disabled_trap", t.quest.expect_stage("disabled_trap"))
        -- The driver has no signal for a pull with no line or page: graded on the stage below.
        local pull_result, pull_detail = t.player.click_loc("ikov_traplever", 1)
        t.note("goPullThievingLever -> " .. tostring(pull_result) .. " " .. tostring(pull_detail))
        t.ticks(4)
        t.expect("quest.stage.pulled_lever", t.quest.expect_stage("pulled_lever"))

        -- ---------------------------------------------------------------- tryToEnterWitchRoom, fightLes
        local door_route = { { 2664, 9847 }, { 2658, 9845 }, { 2651, 9844 }, { 2648, 9849 }, { 2648, 9857 } }
        t.exec("walk-trapLeverDoor", t.player.walk_route, door_route, { max_hop = 12, vitals = VITALS })
        -- ikov_dungeon.rs2 [oploc1,ikov_trapleverdoor]: a walk-through door once the trap lever is pulled
        t.exec("trapLeverDoor", t.player.cross_gate, { loc = "ikov_trapleverdoor", at = { 2648, 9857, 0 },
            near = { 2648, 9857 }, far_ok = function(tile) return tile.z >= 9858 end,
            far_desc = "north of the trap-lever door, z >= 9858" })
        t.player.walk_to(2646, 9865, 30)
        t.player.walk_to(2646, 9869, 20)
        -- The warrior fights with fire blast (ikov_firewarrior.rs2 [ai_applayer2]
        -- ~npc_cast_spell_with_forced_max_hit(^fire_blast, 4, 8); LostCity fire_warrior_of_leskarus.rs2:14-15),
        -- 20-40 hp off an unprotected player over a 60-100 tick fight (runs 2-9). Protect from Magic goes up
        -- BEFORE the door press that brings him in, so it is lit before his first cast; prayer does not
        -- regenerate, so the points are read first (37 staged; the fight drains about 1 point per 5 ticks).
        do
            local points_result, points, points_detail = t.prayer.points()
            t.check("fightLes.prayerPoints", points_result == "ok" and points and points.level >= 25, tostring(points_detail))
        end
        t.exec("fightLes.protectFromMagic", t.prayer.set, "protectfrommagic", true)
        -- Silent to the settle (camera and npc only): graded on the warrior's arrival.
        local door_result, door_detail = t.player.click_loc("ikov_firewarriordoor", 1)
        t.note("tryToEnterWitchRoom -> " .. tostring(door_result) .. " " .. tostring(door_detail))
        t.exec("tryToEnterWitchRoom-warrior", t.npc.await_present, "ikov_firewarrior", 8, 20)
        -- The Fire Warrior door cut (ikov_dungeon.rs2:383-416, LostCity ikov_dungeon.rs2:183-217): the
        -- camera moves to the corridor, looks at the door, at the warrior's smoke puff, back at the
        -- door for the fireblast, and resets once the player is thrown back.
        cutscene_row("tryToEnterWitchRoom.cutscene", "tryToEnterWitchRoom", { expect = {
            { op = "moveto", coord = "0_41_154_17_12", height = 1000 },
            { op = "lookat", coord = "0_41_154_22_14", height = 50 },
            { op = "lookat", coord = "0_41_154_22_10", height = 50 },
            { op = "lookat", coord = "0_41_154_22_14", height = 50 },
            { op = "reset" },
        } })
        t.ticks(12)
        t.exec("fightLes-talkRefused", t.npc.by_symbol, "ikov_firewarrior")
        -- Recorded 4483 [286]..[328], npc info included: the two talks, the refused attack without ice
        -- arrows (his face reset the tick after it, no cast until he is hit), then the arrows.
        t.exec("fightLes.prayerOffForTalk", t.prayer.set, "protectfrommagic", false)
        t.exec("fightLes.pilgrim-talk", t.player.talk_to, "ikov_firewarrior", 1)
        t.exec("fightLes.pilgrim-dialog", t.chat.play, {
            "npc:Who dares to enter",
            "choose:A humble pilgrim.",
            "player:A humble pilgrim.",
            "npc:I haven't seen a pilgrim",
            "npc:Temple is closed!",
        })
        t.exec("fightLes.prayerOnForHero", t.prayer.set, "protectfrommagic", true)
        t.exec("fightLes.hero-talk", t.player.talk_to, "ikov_firewarrior", 1)
        t.exec("fightLes.hero-dialog", t.chat.play, {
            "npc:Who dares to enter",
            "choose:A mighty hero!",
            "player:A mighty hero!",
            "npc:Pathetic fool!",
        })
        t.ticks(3)
        t.exec("fightLes.refused", t.player.talk_to, "ikov_firewarrior", 2)
        t.exec("fightLes.refused-dialog", t.chat.play, { "npc:Your puny weapons" })
        t.ticks(3)
        t.exec("wear-iceArrows", t.player.equip, "ice_arrow")
        t.exec("fightLes", t.player.attack, "ikov_firewarrior", 2, 30)
        -- [388]..[389], [522]..[525], [549]..[550]: his face goes out again the tick after the player's step.
        t.ticks(8)
        do
            local _, tile = t.world.tile()
            -- Across his front, as the recorder ran: a straight line away keeps the angle and sends nothing.
            t.player.walk_to(2642, 9864, 12)
        end
        t.exec("fightLes.reengage", t.player.attack, "ikov_firewarrior", 2, 30)
        local les_result, les_detail = t.npc.await_dead_engaged(400, 12, { eat = { item = FOOD, below = EAT_BELOW } })
        t.step("fightLes-dead", les_result == "ok" and "PASS" or "FAIL", tostring(les_detail))
        margin_row("fightLes.margin", "Fire Warrior of Lesarkus", les_detail)
        t.exec("fightLes.prayerOff", t.prayer.set, "protectfrommagic", false)
        t.ticks(12)
        t.expect("quest.stage.defeated_fire_warrior", t.quest.expect_stage("defeated_fire_warrior"))

        -- ---------------------------------------------------------------- enterLesDoor, giveWineldaLimps
        t.exec("enterLesDoor", t.player.cross_gate, { loc = "ikov_firewarriordoor", at = { 2646, 9870, 0 },
            near = { 2646, 9870 }, far_ok = function(tile) return tile.z >= 9871 end,
            far_desc = "past the fire warrior door, z >= 9871" })
        t.player.walk_to(2655, 9875, 30)
        t.exec("giveWineldaLimps-talk", t.player.talk_to, "ikov_winelda", 1)
        t.exec("giveWineldaLimps-dialog", t.chat.play, {
            "npc:Hehe! We see you're in a pickle",
            "npc:Wants to be getting over",
            "choose:Yes I do!",
            "player:Yes I do!",
            "npc:I'm knowing some magic",
            "npc:Don't tell them!",
            "player:If you're such a great witch",
            "npc:See! They pester Winelda!",
            "player:I can do something for you!",
            "npc:Good! Don't pester!",
            "npc:Get Winelda 20 limpwurt",
            "npc:Then we shows them some magic",
        })
        t.ticks(2)
        t.expect("quest.stage.spoken_winelda", t.quest.expect_stage("spoken_winelda"))
        -- Recorded 4483 [641]..[651]: walking off resets her face, the next talk turns her again.
        do
            local _, tile = t.world.tile()
            if tile.x >= 2655 then
                t.player.walk_to(2651, 9876, 12)
            else
                t.player.walk_to(2658, 9875, 12)
            end
            t.ticks(2)
        end
        t.exec("giveWineldaLimps", t.player.talk_to, "ikov_winelda", 1)
        t.exec("giveWineldaLimps-pay", t.chat.play, {
            "player:I've got you the limpwurt roots",
            "npc:Good! Good! My potion",
            "npc:Now we shows them ours magic",
        })
        -- Winelda's teleport cut (ikov_winelda.rs2:48-63, LostCity winelda.rs2): the camera moves over the
        -- lava, looks at the player, then at the far bank she lands on, and resets after the puff.
        cutscene_row("giveWineldaLimps.cutscene", "giveWineldaLimps", { expect = {
            { op = "moveto", coord = "0_41_154_35_24", height = 1500 },
            { op = "lookat", height = 50 },
            { op = "lookat", coord = "0_41_154_40_20", height = 50 },
            { op = "reset" },
        } })
        t.ticks(20)
        t.expect("quest.stage.paid_winelda", t.quest.expect_stage("paid_winelda"))
        t.exec("giveWineldaLimps-limpwurtsGone", t.inv.expect_absent, "limpwurt_root")
        do
            local _, tile = t.world.tile()
            t.check("winelda-teleport", tile.z >= 9872 and tile.x >= 2660, "Winelda's magic carried us over the lava: " .. tile.x .. "," .. tile.z)
        end

        -- ---------------------------------------------------------------- pickUpKey
        local key_route = { { 2660, 9882 }, { 2649, 9891 }, { 2639, 9891 }, { 2632, 9888 }, { 2631, 9879 }, { 2631, 9869 }, { 2630, 9860 } }
        local key_route_out = {}
        for hop = 1, #key_route do key_route_out[hop] = key_route[hop] end
        key_route_out[#key_route_out + 1] = { 2629, 9859 }
        t.exec("walk-pickUpKey", t.player.walk_route, key_route_out, { max_hop = 12, vitals = VITALS })
        t.exec("pickUpKey", t.player.click_obj, "ikov_shinykey", 3)
        t.exec("pickUpKey-pack", t.inv.expect_has, "ikov_shinykey", 1)

        -- ---------------------------------------------------------------- pushWall
        local key_route_back = {}
        for hop = #key_route, 2, -1 do key_route_back[#key_route_back + 1] = key_route[hop] end
        key_route_back[#key_route_back + 1] = { 2643, 9891 }
        t.exec("walk-pushWall", t.player.walk_route, key_route_back, { max_hop = 12, vitals = VITALS })
        t.exec("pushWall", t.player.cross_gate, { loc = "secretdoor2", at = { 2643, 9892, 0 },
            near = { 2643, 9892 }, far_ok = function(tile) return tile.z >= 9893 end,
            far_desc = "through the secret door into the guardians' hall, z >= 9893" })

        -- ---------------------------------------------------------------- makeChoice (help the Guardians)
        t.exec("makeChoice-removePendant", t.player.unequip, "ikov_pendantoflucien")
        t.exec("makeChoice-talk", t.player.talk_to, "ikov_guardianmale", 1)
        t.exec("makeChoice-dialog", t.chat.play, {
            "npc:Thou hast ventured deep",
            "choose:I seek the Staff of Armadyl.",
            "player:I seek the Staff of Armadyl.",
            "npc:We are the guardians of the staff",
            "choose:Lucien will give me a grand reward for it!",
            "player:Lucien will give me a grand reward",
            "npc:Thou art working for that spawn",
            "choose:You're right, it's time for my yearly bath.",
            "player:You're right, it's time",
            "mesbox:The guardian splashes holy water",
            "npc:You have been cleansed!",
            "npc:Lucien must not get hold",
            "npc:Hast thou come across",
            "choose:Ok! I'll help!",
            "player:Ok! I'll help!",
            "npc:So he is close by?",
            "player:Yes!",
            "npc:He must be gaining in power",
            "objbox:The guardian has given you a pendant",
        })
        t.ticks(2)
        t.expect("quest.stage.helping_armadyl", t.quest.expect_stage("helping_armadyl"))
        t.exec("makeChoice-pendantOfArmadyl", t.inv.expect_has, "ikov_pendantofarmardyl", 1)
        -- The stage-70 talks, recorded 4483 [921]..[972]: a male and two females, each turned a tick after
        -- the page and reset when the player walks off.
        do
            local function not_yet(tag, symbol)
                t.exec(tag .. "-talk", t.player.talk_to, symbol, 1)
                t.exec(tag .. "-dialog", t.chat.play, {
                    "npc:Have you killed Lucien?",
                    "player:Not yet.",
                    "npc:Hurry friend!",
                })
            end
            local function walk_off()
                local _, tile = t.world.tile()
                if tile.x < 2644 then
                    t.player.walk_to(2649, 9908, 30)
                else
                    t.player.walk_to(2637, 9911, 30)
                end
                t.ticks(2)
            end
            not_yet("stage70.male", "ikov_guardianmale")
            walk_off()
            not_yet("stage70.female1", "ikov_guardianfemale")
            walk_off()
            not_yet("stage70.female2", "ikov_guardianfemale")
        end

        -- ---------------------------------------------------------------- killLucien
        t.exec("killLucien-wear", t.player.equip, "ikov_pendantofarmardyl")
        -- The key path's skeletons and lesser demons take their share (runs 3-4: 52 -> 32 hp between the fire
        -- warrior and Lucien); a player low on hitpoints eats before leaving rather than mid-fight.
        do
            local _, hp = t.skill.read("hitpoints")
            local level = hp and hp.level or 0
            local _, sharks_held = t.inv.count(FOOD)
            if level < EAT_BELOW and (sharks_held or 0) > 0 then
                local _, sharks_before = t.inv.count(FOOD)
                local eat_result, eat_detail = t.player.inv_op(FOOD, 1)
                t.ticks(3)
                local _, hp_after = t.skill.read("hitpoints")
                local _, sharks_after = t.inv.count(FOOD)
                t.check("killLucien.eatBeforeLeaving", hp_after and hp_after.level > level and sharks_after == sharks_before - 1,
                    "hitpoints " .. level .. " -> " .. tostring(hp_after and hp_after.level) .. ", " .. FOOD .. " "
                        .. tostring(sharks_before) .. " -> " .. tostring(sharks_after) .. " (inv_op " .. tostring(eat_result)
                        .. " " .. tostring(eat_detail) .. ")")
            else
                t.note("hitpoints " .. level .. "/99 leaving the dungeon: no need to eat")
            end
        end
        -- Out of the dungeon the way the recorder left it: a Varrock teleport tablet
        -- (4483 [1011]..[1015]), then overland to the street outside Lucien's door.
        do
            local tab_result, tab_detail = t.player.inv_op("poh_tablet_varrockteleport", 1)
            t.ticks(7)
            local _, tile = t.world.tile()
            t.check("exitTeleport", tile.x >= 3211 and tile.x <= 3215 and tile.z >= 3422 and tile.z <= 3426,
                "broke the tablet (" .. tostring(tab_result) .. " " .. tostring(tab_detail) .. "), landed "
                    .. tile.x .. "," .. tile.z)
        end
        t.exec("goto-killLucien", t.player.goto_tile, 3122, 3490, 0)
        -- ikov_luciendoor.rs2: a walk-through door (p_telejump) once the quest is started
        t.exec("killLucien.houseDoorIn", t.player.cross_gate, { loc = "ikov_luciendoor", at = { 3122, 3488, 0 },
            near = { 3122, 3488 }, far_ok = function(tile) return tile.z <= 3487 end,
            far_desc = "inside Lucien's house, z <= 3487" })
        do
            local _, hp = t.skill.read("hitpoints")
            t.note("hitpoints before Lucien: " .. tostring(hp and hp.level) .. "/99")
        end
        -- The stage-70 reminder (4483 [1108]..[1114]) and the mind trick without the pendant
        -- ([1127]..[1136]), before the fight.
        t.exec("lucienRemind", t.player.talk_to, "ikov_lucien2", 1)
        t.exec("lucienRemind-dialog", t.chat.play, {
            "npc:Yes?",
            "player:Sorry! Can you remind me",
            "npc:My patience grows thin hero!",
            "npc:I need the Staff of Armadyl.",
        })
        t.exec("mindTrick-removePendant", t.player.unequip, "ikov_pendantofarmardyl")
        t.exec("mindTrick-attack", t.player.talk_to, "ikov_lucien2", 2)
        t.exec("mindTrick-dialog", t.chat.play, { "npc:You don't want to attack me." })
        t.ticks(5) -- recorded 4483 [1129]..[1133]: the box comes four ticks after the page's continue
        t.exec("mindTrick-box", t.chat.play, { "mesbox:You decide not to attack Lucien." })
        t.exec("mindTrick-wearPendant", t.player.equip, "ikov_pendantofarmardyl")
        -- [1143]..[1155]: the pendant on, an attack turns him ([1145]); off mid-fight, the next press is
        -- the trick again.
        t.exec("mindTrick2-attack", t.player.attack, "ikov_lucien2", 2, 30)
        t.exec("mindTrick2-removePendant", t.player.unequip, "ikov_pendantofarmardyl")
        -- The combat loop re-asks [apnpc2] every tick, so with the pendant off the next swing is already
        -- the trick; a press on top of it would run the trick twice.
        t.ticks(1)
        if t.chat.kind() == "none" then
            t.exec("mindTrick2-press", t.player.talk_to, "ikov_lucien2", 2)
        end
        t.exec("mindTrick2-dialog", t.chat.play, { "npc:You don't want to attack me." })
        t.ticks(5)
        t.exec("mindTrick2-box", t.chat.play, { "mesbox:You decide not to attack Lucien." })
        t.exec("mindTrick2-wearPendant", t.player.equip, "ikov_pendantofarmardyl")
        t.exec("killLucien", t.player.attack, "ikov_lucien2", 2, 30)
        -- Lucien is level 14 (quest_ikov.npc: attack 12, strength 10, 17 hp): the last shark is kept for a real
        -- emergency, not eaten at the fire warrior's threshold (run 2 ate it at once on a 49-hp entry).
        -- Lucien never reads dead: ikov_lucien2.rs2 [label,ikov_lucien2_died] heals him and queues the defeat
        -- page; a re-engage press there closed the page and killed him again (run 3: 12 re-engages over 400
        -- ticks), so the wait never re-presses (attempts 0) and the defeat page is the kill's outcome.
        local lucien_result, lucien_detail = t.npc.await_dead_engaged(60, 0, { eat = { item = FOOD, below = 30 } })
        t.note("await_dead_engaged on Lucien -> " .. tostring(lucien_result) .. ": " .. tostring(lucien_detail))
        margin_row("killLucien.margin", "Lucien", lucien_detail)
        -- The reward is paid when the defeat page is continued ([queue,ikov_lucien_defeated]): read the skills
        -- while it is up, after every hit of the kill has paid its combat xp.
        t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "Lucien's defeat page up" }, 20)
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.note("skill.snapshot with the defeat page up -> " .. tostring(xp_snapshot_result) .. " (the reward rows below grade the deltas)")
        -- Recorded 4483 [1183]..[1204]: the page twice, the second one with
        -- Lucien gone and the first never answered: wait for his departure.
        t.ticks(8)
        t.exec("killLucien-defeated", t.chat.play, { "npc:You have defeated me for now" })
        t.ticks(10)

        -- ---------------------------------------------------------------- rewards
        -- ikov_lucien2.rs2 [proc,ikov_quest_rewards]: stat_advance(ranged, 105000) / (fletching, 80000) tenths,
        -- 1 quest point (wiki: 10,500 Ranged, 8,000 Fletching, 1 QP).
        -- scroll.reward_xp does not parse thousands separators ("10,500" reads 500), so read the lines
        local rewards_result, rewards = t.scroll.rewards()
        local rewards_text = ""
        if rewards_result == "ok" then
            for i = 1, #rewards.lines do rewards_text = rewards_text .. rewards.lines[i] .. " | " end
        end
        t.check("scroll.reward_ranged", rewards_text:find("10,500 Ranged XP", 1, true) ~= nil,
            "scroll lines: " .. rewards_text)
        t.check("scroll.reward_fletching", rewards_text:find("8,000 Fletching XP", 1, true) ~= nil,
            "scroll lines: " .. rewards_text)
        t.expect("reward.ranged_xp", t.skill.expect_gain("ranged", 10500, xp_snapshot))
        t.expect("reward.fletching_xp", t.skill.expect_gain("fletching", 8000, xp_snapshot))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
