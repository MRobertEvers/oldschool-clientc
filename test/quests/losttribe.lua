-- The Lost Tribe -- the flow of RSProx index 4454 (rev 241, 2026-11-02), the only recorded
-- playthrough: docs/rsprox-analysis/quests/the_lost_tribe.md. The witness is a roll at the
-- start; the test pins the recorded one (the Cook, 2) with ::losttribewitness.
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03; re-driven b63): a goto only hops
-- between open tiles outside. Every closed space is entered and left by its own door, stair,
-- ladder, trapdoor or hole, on every visit:
--   * Lumbridge castle: the keep's front doorway is open map (inaccastledoubledoor*open, no op);
--     the north spiral stairs spiralstairsbottom_3 / spiralstairsmiddle 3204,3229 (no maplink
--     row: +-1 level on the tile, ladders.rs2 [proc,climb]); the Duke's room behind elfdoor
--     3207,3222,1 (room x >= 3208, maps/m50_50.jl2); Sigmund's chest room behind elfdoor
--     3207,3214,1 (room x >= 3208). The kitchen is open map (qip_cook_door 3208,3211 and
--     inactiveelfdoor 3215,3212 have no op).
--   * The castle cellar: the kitchen trapdoor qip_cook_trapdoor_open 3209,3216 (maplink
--     maplink_0_50_50_10_16_down: 3210,3216 -> 3210,9616) and the cellar ladder
--     ladder_from_cellar 3209,9616 (maplink_0_50_150_10_16_up: 3210,9616 -> 3210,3216); the dug
--     hole lost_tribe_cavewall_hole_walldecor (losttribe.rs2 [oploc1,...]: cellar side 3219 ->
--     ^lt_tunnel_enter 3221,9618, tunnel side x >= 3220 -> ^lt_cellar_exit 3219,9618).
--   * The maze's collapsing floor (losttribe_tunnels.rs2) sweeps the player down the river to
--     3230,9582 in the Lumbridge Swamp Caves ([2508]); the test breaks a Lumbridge tablet out.
--   * Varrock palace library: fai_varrock_castle_door 3210,3490 (library z >= 3490).
--   * The Goblin Village generals' hut: goblin_outpost_poordoor_double_inner 2957,3509 (hut
--     z >= 3510).
--   * The H.A.M. hideout: osf_trapdoor_closed 3166,3252 picked (op5), osf_trapdoor_open down
--     (-> ^lt_ham_trapdoor_in 3149,9652) and osf_ham_ladder 3149,9653 up (-> 3165,3251)
--     (losttribe_ham.rs2).
--   * The Dorgeshuun mines: Mistag's "way out" (-> ^lt_mines_exit 3235,9610) and Kazgar's
--     guide (-> ^lt_mines_enter 3309,9613) are the npcs' own moves (losttribe_mistag.rs2).
return {
    id = "losttribe",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::complete quest_goblindiplomacy", "::complete quest_runemysteries", "::setlevel mining 17", "::setlevel agility 13", "::setlevel thieving 13", "::give iron_pickaxe 1", "::give lit_black_candle 1", "::give tinderbox 1", "::give poh_tablet_lumbridgeteleport 3",
        -- food for the walk out of the Swamp Caves after the maze's floor trap: their cave bugs set on a
        -- 10-hitpoint player at once (run 2: 10 -> 5 hp over the 108-tile walk)
        "::give trout 4" },
    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb532_lost_tribe_quest",
            constants = { not_started = 0, started = 1, witness = 2, permission = 3, tunnel = 4, book = 5, symbol = 6, generals = 7, mistag = 8, ham_hunt = 9, treaty = 10, complete = 11 },
            row = "quest_losttribe", display = "The Lost Tribe", points = 1,
        })
        t.step("quest.bind", br == "ok" and "PASS" or "FAIL", bd)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local r, d, tr, tl

        local function tile_text()
            local rr, tt = t.world.tile()
            if rr ~= "ok" or type(tt) ~= "table" then
                return tostring(rr)
            end
            return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
        end

        -- ---------------------------------------------------------- the castle's crossings
        -- Waypoints from reach.py's closed-door paths (every hop <= 10 tiles).
        local COURTYARD_TO_STAIRS = { { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } }
        local STAIRS_TO_COURTYARD = { { 3207, 3227 }, { 3214, 3226 }, { 3215, 3219 }, { 3222, 3218 } }
        local STAIRS_TO_KITCHEN = { { 3207, 3227 }, { 3214, 3226 }, { 3215, 3219 }, { 3215, 3212 },
            { 3209, 3211 }, { 3208, 3213 }, { 3210, 3216 } }
        local KITCHEN_TO_STAIRS = { { 3208, 3213 }, { 3209, 3211 }, { 3215, 3212 }, { 3215, 3219 },
            { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } }
        local COURTYARD_TO_KITCHEN = { { 3215, 3218 }, { 3215, 3212 }, { 3209, 3211 }, { 3208, 3213 }, { 3210, 3216 } }
        -- the cellar, ladder foot 3210,9616 <-> the rubble / hole 3219,9618 (brickwalls between)
        local CELLAR_TO_HOLE = { { 3208, 9620 }, { 3214, 9620 }, { 3216, 9616 }, { 3219, 9618 } }
        local HOLE_TO_LADDER = { { 3216, 9616 }, { 3214, 9620 }, { 3208, 9620 }, { 3210, 9616 } }

        local function walk(name, points, level)
            t.exec(name, t.player.walk_route, points, { level = level or 0 })
        end
        local function stairs_up(tag)
            t.exec(tag .. ".stairsUp", t.player.climb, { loc = "spiralstairsbottom_3", op = 1, op_name = "Climb-up",
                at = { 3204, 3229, 0 }, src = { 3205, 3228 }, dest = { 3205, 3228, 1 } })
        end
        local function stairs_down(tag)
            t.exec(tag .. ".stairsDown", t.player.climb, { loc = "spiralstairsmiddle", op = 3, op_name = "Climb-down",
                at = { 3204, 3229, 1 }, dest = { 3205, 3228, 0 } })
        end
        local function duke_door_in(tag)
            t.exec(tag .. ".dukeDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
                at = { 3207, 3222, 1 }, near = { 3207, 3222 }, far = { 3209, 3221 },
                far_ok = function(tt) return tt.x >= 3208 end, far_desc = "in the Duke's room, x >= 3208" })
        end
        local function duke_door_out(tag)
            t.exec(tag .. ".dukeDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
                at = { 3207, 3222, 1 }, near = { 3208, 3222 }, far = { 3206, 3224 },
                far_ok = function(tt) return tt.x <= 3207 end, far_desc = "out of the Duke's room, x <= 3207" })
        end
        -- level 0 at the foot of the stairs -> the Duke's room
        local function up_to_duke(tag)
            stairs_up(tag)
            duke_door_in(tag)
        end
        -- the Duke's room -> level 0 at the foot of the stairs
        local function down_from_duke(tag)
            duke_door_out(tag)
            walk(tag .. ".toStairsTop", { { 3205, 3228 } }, 1)
            stairs_down(tag)
        end
        -- the kitchen (3210,3216) -> the cellar ladder foot (3210,9616)
        local function trapdoor_down(tag)
            t.exec(tag .. ".trapdoorDown", t.player.climb, { loc = "qip_cook_trapdoor_open", op = 1, op_name = "Climb-down",
                at = { 3209, 3216, 0 }, src = { 3210, 3216 }, dest = { 3210, 9616, 0 } })
        end
        -- the cellar ladder foot (3210,9616) -> the kitchen (3210,3216)
        local function ladder_up(tag)
            t.exec(tag .. ".cellarLadderUp", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
                at = { 3209, 9616, 0 }, src = { 3210, 9616 }, dest = { 3210, 3216, 0 } })
        end
        -- through the dug hole, cellar side (3219,9618) -> tunnel side (3221,9618), and back
        local function hole_in(name)
            t.exec(name, t.player.cross_trap, { loc = "lost_tribe_cavewall_hole_walldecor", at = { 3219, 9618, 0 },
                src = { 3219, 9618 }, dest = { 3221, 9618 }, attempts = 1 })
        end
        local function hole_out(name)
            t.exec(name, t.player.cross_trap, { loc = "lost_tribe_cavewall_hole_walldecor", at = { 3221, 9618, 0 },
                src = { 3221, 9618 }, dest = { 3219, 9618 }, attempts = 1 })
        end

        local function talk_menu(tag, npc, choice)
            t.exec(tag, t.player.talk_to, npc, 1)
            r, d = t.chat.drain({ stop_at = "options" }); t.expect(tag .. ".opts", r, d)
            t.exec(tag .. ".choose", t.chat.choose, choice)
        end
        local function choose(tag, choice)
            r, d = t.chat.drain({ stop_at = "options" }); t.expect(tag .. ".opts", r, d)
            t.exec(tag, t.chat.choose, choice)
        end
        local function drain_all(tag)
            r, d = t.chat.drain({ stop_at = "none" }); t.expect(tag, r, d)
        end
        local function landed(tag, x, z, level)
            tr, tl = t.world.tile()
            t.check(tag, tr == "ok" and tl.x == x and tl.z == z and tl.level == level,
                "at " .. tile_text() .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end
        -- a swamp-cave bug on the player can refuse the break ("I can't reach that!"): once more
        local function tablet_home(tag)
            t.ticks(2)
            t.player.inv_op("poh_tablet_lumbridgeteleport", 1)
            t.ticks(6)
            local hr, ht = t.world.tile()
            if hr == "ok" and ht.z > 6400 then
                t.ticks(4)
                t.player.inv_op("poh_tablet_lumbridgeteleport", 1)
                t.ticks(6)
            end
            tr, tl = t.world.tile()
            t.check(tag .. ".tablet", tr == "ok" and tl.z < 6400, "after the tablet: " .. tile_text())
        end

        t.ticklog.start()

        -- [292..311] Sigmund, in the Duke's room: the start confirm, then the stage and the roll
        walk("goTalkToSigmund.toStairs", { { 3211, 3231 }, { 3217, 3230 }, { 3220, 3226 }, { 3221, 3220 },
            { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } })
        up_to_duke("goTalkToSigmund")
        talk_menu("sigmund.start", "lost_tribe_sigmund", "Do you have any quests for me?")
        choose("sigmund.confirm", "Yes.")
        drain_all("sigmund.end")
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        t.exec("witness.pin", t.cheat, "::losttribewitness 2")

        -- [429..448] the Cook, the recorded witness
        down_from_duke("talkToCook")
        walk("talkToCook.toKitchen", STAIRS_TO_KITCHEN)
        talk_menu("cook", "cook", "Do you know what happened in the castle cellar?")
        drain_all("cook.end")
        t.exec("cook.witness", t.var.await, "varb532_lost_tribe_quest", 2, 4)

        -- [694..723] the Duke's permission, written on the closing click
        walk("talkToDuke.toStairs", KITCHEN_TO_STAIRS)
        up_to_duke("talkToDuke")
        talk_menu("duke.cook", "duke_of_lumbridge", "The cook says he saw something in the cellar")
        drain_all("duke.permission")
        t.exec("duke.permits", t.var.await, "varb532_lost_tribe_quest", 3, 4)

        -- [772..801] the dig, the squeeze, the brooch (taken twice: the pile stays)
        down_from_duke("goDownIntoBasement")
        walk("goDownIntoBasement.toKitchen", STAIRS_TO_KITCHEN)
        trapdoor_down("goDownIntoBasement")
        walk("usePickaxeOnRubble.walk", CELLAR_TO_HOLE)
        t.exec("usePickaxeOnRubble", t.player.use_on, "iron_pickaxe", t.player.by_symbol("loc", "lost_tribe_cellar_wall"))
        t.exec("rubble.dug", t.var.await, "varb532_lost_tribe_quest", 4, 12)
        drain_all("rubble.objbox")
        hole_in("climbThroughHole")
        t.ticks(2)
        t.exec("dark.medium", t.var.await, "varb278_darkness_level", 2, 4)
        walk("grabBrooch.walk", { { 3222, 9618 }, { 3224, 9618 }, { 3229, 9610 } })
        t.exec("grabBrooch", t.player.click_obj, "lost_tribe_brooch", 3)
        t.exec("brooch.have", t.inv.await, "lost_tribe_brooch", 1, 6)
        do
            local brooch_obj = t.player.by_symbol("obj", "lost_tribe_brooch")
            local hr, h = t.world.hazard_at(3230, 9610, 0)
            local left = 0
            if hr == "ok" then
                for i = 1, #h.objs do
                    if h.objs[i].obj_id == brooch_obj.id then left = left + 1 end
                end
            end
            t.check("brooch.stays", hr == "ok" and left == 1, "left on 3230,9610,0: " .. left .. " (want 1, [799])")
        end

        -- [2499..2514] a floor trap (3239,9622): the river, the candle out, the dark, relit
        local wr, wd = t.player.walk_to(3239, 9622, 20)
        t.note("trap.step: walk_to 3239,9622 answered " .. tostring(wr) .. " " .. tostring(wd))
        t.ticks(20)
        landed("trap.bank", 3230, 9582, 0)
        t.exec("trap.light.out", t.inv.await, "unlit_black_candle", 1, 3)
        t.exec("dark.black", t.var.await, "varb278_darkness_level", 3, 3)
        t.exec("relight", t.player.use_item_on_item, "tinderbox", "unlit_black_candle")
        t.exec("relit", t.inv.await, "lit_black_candle", 1, 4)
        t.exec("dark.lit", t.var.await, "varb278_darkness_level", 2, 4)
        tablet_home("broochHome")

        -- [1224..1250] the brooch shown to the Duke
        t.exec("goto-showBroochToDuke.courtyard", t.player.goto_tile, 3222, 3218, 0)
        walk("showBroochToDuke.toStairs", COURTYARD_TO_STAIRS)
        up_to_duke("showBroochToDuke")
        talk_menu("duke.brooch", "duke_of_lumbridge", "I dug through the rubble...")
        drain_all("duke.brooch.end")
        t.expect("quest.stage.book", t.quest.expect_stage("book"))

        -- [1317..1434] Reldo, the bookcase, the book read to its symbols, the Dorgeshuun symbol
        down_from_duke("talkToReldo")
        walk("talkToReldo.outOfCastle", STAIRS_TO_COURTYARD)
        t.exec("goto-talkToReldo.palaceFront", t.player.goto_tile, 3212, 3460, 0)
        walk("talkToReldo.toLibrary", { { 3212, 3466 }, { 3212, 3472 }, { 3207, 3473 }, { 3206, 3478 },
            { 3206, 3484 }, { 3208, 3488 }, { 3210, 3489 } })
        t.exec("talkToReldo.libraryDoorIn", t.player.pass_door, { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open",
            at = { 3210, 3490, 0 }, near = { 3210, 3489 }, far = { 3210, 3492 },
            far_ok = function(tt) return tt.z >= 3490 end, far_desc = "in the library, z >= 3490" })
        talk_menu("reldo", "reldo", "What can you tell me about this brooch?")
        drain_all("reldo.end")
        t.exec("walk-searchBookcase", t.player.walk_to, 3208, 3496)
        t.exec("searchBookcase", t.player.click_loc, "lost_tribe_bookcase", 1)
        drain_all("bookcase.chat")
        t.exec("book.have", t.inv.await, "lost_tribe_book", 1, 5)
        t.exec("readBook", t.player.inv_op, "lost_tribe_book", 1)
        t.exec("book.open", t.ui.await_open, "lost_tribe_symbol_book", 10)
        t.ticks(2)
        local wgr, w = t.ui.widget("lost_tribe_symbol_book:lost_tribe_right_arrow")
        t.step("book.arrow", wgr == "ok" and "PASS" or "FAIL", tostring(wgr) .. " " .. tostring(w))
        for i = 1, 5 do
            t.ui.invoke(w, 1)
            t.ticks(2)
            t.exec("book.mark" .. i, t.var.await, "varb541_lost_tribe_bookmark", i, 3)
            if i < 5 then t.expect("book.unfinished" .. i, t.quest.expect_stage("book")) end
        end
        t.exec("readBook.done", t.var.await, "varb532_lost_tribe_quest", 6, 4)
        local sgr, sw = t.ui.widget("lost_tribe_symbol_book:com_48")
        if sgr == "ok" then t.ui.invoke(sw, 1) end
        t.ticks(2)
        drain_all("book.symbol")

        -- [1868..2085] the generals: the Dorgeshuun, the legend, the brooch, both greetings
        t.exec("talkToGenerals.libraryDoorOut", t.player.pass_door, { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open",
            at = { 3210, 3490, 0 }, near = { 3210, 3491 }, far = { 3210, 3488 },
            far_ok = function(tt) return tt.z <= 3489 end, far_desc = "out of the library, z <= 3489" })
        walk("talkToGenerals.outOfPalace", { { 3208, 3488 }, { 3206, 3484 }, { 3206, 3478 }, { 3207, 3473 },
            { 3212, 3472 }, { 3212, 3466 }, { 3212, 3460 } })
        t.exec("goto-talkToGenerals.village", t.player.goto_tile, 2957, 3507, 0)
        t.exec("talkToGenerals.hutDoorIn", t.player.pass_door, { closed = "goblin_outpost_poordoor_double_inner",
            open = "goblin_outpost_openpoordoor_double_inner",
            at = { 2957, 3509, 0 }, near = { 2957, 3508 }, far = { 2957, 3511 },
            far_ok = function(tt) return tt.z >= 3510 end, far_desc = "in the generals' hut, z >= 3510" })
        talk_menu("generals", "general_bentnoze_red", "Have you ever heard of the Dorgeshuun?")
        choose("generals.who", "It doesn't really matter")
        choose("generals.why", "Well either way they refused to fight")
        choose("generals.cave", "Well I found a brooch underground...")
        choose("generals.greet", "Well why not show me both greetings?")
        drain_all("generals.watch")
        -- the demonstration: the chat closed, T+1 the dance, T+5 the bow, T+9 the mesbox
        t.ticks(10)
        choose("generals.thanks", "Thanks")
        drain_all("generals.end")
        t.expect("quest.stage.generals", t.quest.expect_stage("generals"))
        t.exec("walkToMistag.hutDoorOut", t.player.pass_door, { closed = "goblin_outpost_poordoor_double_inner",
            open = "goblin_outpost_openpoordoor_double_inner",
            at = { 2957, 3509, 0 }, near = { 2957, 3510 }, far = { 2957, 3507 },
            far_ok = function(tt) return tt.z <= 3509 end, far_desc = "out of the generals' hut, z <= 3509" })

        -- [2723..2904] the marked path to Mistag, the goblin bow
        t.exec("goto-walkToMistag.courtyard", t.player.goto_tile, 3222, 3218, 0)
        walk("walkToMistag.toKitchen", COURTYARD_TO_KITCHEN)
        trapdoor_down("walkToMistag")
        walk("walkToMistag.toHole", CELLAR_TO_HOLE)
        hole_in("enterTunnels")
        local line = { {3222,9618},{3224,9618},{3229,9610},{3238,9610},{3241,9612},{3246,9612},{3249,9619},{3254,9625},{3252,9631},{3243,9631},{3234,9631},{3230,9634},{3230,9643},{3237,9648},{3244,9648},{3246,9645},{3240,9641},{3244,9637},{3252,9642},{3252,9646},{3257,9656},{3263,9656},{3269,9656},{3277,9652},{3277,9647},{3267,9643},{3267,9638},{3276,9637},{3283,9641},{3290,9645},{3300,9641},{3307,9631},{3297,9627},{3295,9622},{3290,9617},{3293,9612},{3297,9606},{3303,9606},{3309,9612},{3317,9612} }
        walk("walkToMistag", line)
        t.player.emote("Goblin Bow")
        t.ticks(2)
        drain_all("emoteAtMistag.chat")
        t.exec("emoteAtMistag", t.var.await, "varb532_lost_tribe_quest", 8, 6)

        -- [2945..2955] Mistag shows the way out
        talk_menu("mistag.out", "lost_tribe_mistag", "Can you show me the way out of the mines?")
        drain_all("mistag.out.end")
        t.ticks(6)
        landed("mistag.landed", 3235, 9610, 0)
        walk("goTalkToDukeAfterEmote.toHole", { { 3227, 9613 }, { 3225, 9617 }, { 3221, 9618 } })
        hole_out("goTalkToDukeAfterEmote.holeOut")
        t.exec("dark.off", t.var.await, "varb278_darkness_level", 0, 4)
        walk("goTalkToDukeAfterEmote.toLadder", HOLE_TO_LADDER)
        ladder_up("goTalkToDukeAfterEmote")
        walk("goTalkToDukeAfterEmote.toStairs", KITCHEN_TO_STAIRS)
        up_to_duke("goTalkToDukeAfterEmote")
        talk_menu("duke.contact", "duke_of_lumbridge", "I've made contact with the cave goblins...")
        drain_all("duke.contact.end")
        t.exec("contact.stage", t.var.await, "varb532_lost_tribe_quest", 9, 6)

        -- [3345..3398] Sigmund's key, the chest, the Duke told
        t.exec("pickpocketSigmund", t.player.press, "lost_tribe_sigmund", 3, 20)
        t.exec("key.have", t.inv.await, "lost_tribe_chest_key", 1, 8)
        drain_all("key.objbox")
        duke_door_out("unlockChest")
        walk("unlockChest.toDoor", { { 3206, 3218 }, { 3206, 3214 } }, 1)
        t.exec("unlockChest.chestDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3214, 1 }, near = { 3207, 3214 }, far = { 3209, 3216 },
            far_ok = function(tt) return tt.x >= 3208 end, far_desc = "in the chest room, x >= 3208" })
        t.exec("unlockChest", t.player.click_loc, "lost_tribe_chest", 1)
        drain_all("chest.chat")
        t.exec("robes.have", t.inv.await, "ham_robe", 1, 6)
        t.exec("robes.state", t.var.await, "varb534_lost_tribe_ham", 1, 4)
        t.exec("tellDuke.chestDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3214, 1 }, near = { 3208, 3214 }, far = { 3206, 3214 },
            far_ok = function(tt) return tt.x <= 3207 end, far_desc = "out of the chest room, x <= 3207" })
        walk("tellDuke.toDukeDoor", { { 3206, 3218 }, { 3207, 3222 } }, 1)
        duke_door_in("tellDuke")
        talk_menu("duke.ham", "duke_of_lumbridge", "Did you know Sigmund is a member of HAM?")
        drain_all("duke.ham.end")
        t.exec("ham.told", t.var.await, "varb534_lost_tribe_ham", 3, 4)

        -- [3500..3591] the hideout: the lock picked (one try in three), down, the crate
        down_from_duke("enterHamLair")
        walk("enterHamLair.outOfCastle", STAIRS_TO_COURTYARD)
        t.exec("goto-enterHamLair.field", t.player.goto_tile, 3166, 3251, 0)
        t.exec("ham.picklock", t.player.click_loc, "osf_trapdoor_closed", 5)
        t.exec("ham.picklocked", t.var.await, "varb235_ham_thief", 1, 120)
        t.exec("enterHamLair", t.player.climb, { loc = "osf_trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3166, 3252, 0 }, dest = { 3149, 9652, 0 } })
        t.exec("searchHamCrates", t.player.click_loc, "lost_tribe_crate", 1)
        t.ticks(3)
        drain_all("crate.chat")
        t.exec("silverware.have", t.inv.await, "lost_tribe_silverware", 1, 6)
        tablet_home("silverwareHome")

        -- [3710..3742] the Duke dismisses Sigmund; the treaty
        t.exec("goto-goToDukeWithSilverware.courtyard", t.player.goto_tile, 3222, 3218, 0)
        walk("goToDukeWithSilverware.toStairs", COURTYARD_TO_STAIRS)
        up_to_duke("goToDukeWithSilverware")
        talk_menu("duke.silverware", "duke_of_lumbridge", "I found the missing silverware in the HAM cave!")
        drain_all("duke.silverware.end")
        t.exec("treaty.have", t.inv.await, "lost_tribe_treaty", 1, 6)
        local sr, sc = t.inv.count("lost_tribe_silverware")
        t.check("silverware.handedIn", sr == "ok" and sc == 0, "lost_tribe_silverware count " .. tostring(sc))
        t.expect("quest.stage.treaty", t.quest.expect_stage("treaty"))

        -- [3824..3838] Kazgar shows the way in
        down_from_duke("talkToKazgar")
        walk("talkToKazgar.toKitchen", STAIRS_TO_KITCHEN)
        trapdoor_down("talkToKazgar")
        walk("talkToKazgar.toHole", CELLAR_TO_HOLE)
        hole_in("enterTunnelsFinal")
        walk("talkToKazgar.walk", { { 3222, 9618 }, { 3224, 9618 }, { 3229, 9610 } })
        talk_menu("kazgar", "lost_tribe_guide", "Can you show me the way to the mines?")
        drain_all("kazgar.end")
        t.ticks(6)
        landed("kazgar.landed", 3309, 9613, 0)

        -- [3845..3949] the treaty, the signing, the rewards
        local ringr0, ring0 = t.inv.count("ring_of_life")
        local snap_r, snap = t.skill.snapshot()
        t.check("reward.snapshot", snap_r, "skill snapshot before the hand-in: " .. tostring(snap_r))
        walk("talkToMistagForEnd.walk", { { 3313, 9613 } })
        t.exec("talkToMistagForEnd", t.player.talk_to, "lost_tribe_mistag", 1)
        drain_all("mistag.treaty")
        -- the signing: every page waits on its click; the fades and walks between them do not
        local stage = 0
        for _ = 1, 60 do
            t.ticks(3)
            t.chat.drain({ stop_at = "none" })
            local vr, v = t.var.varbit("varb532_lost_tribe_quest")
            stage = v
            if vr == "ok" and v == 11 then break end
        end
        t.check("signing.done", stage == 11, "lost_tribe_quest " .. tostring(stage) .. " after the signing (want 11)")
        t.ticks(4)
        landed("signing.out", 3207, 3217, 0)
        t.exec("signing.scroll", t.ui.await_open, "questscroll", 10)
        t.quest.expect_complete()
        t.expect("reward.mining", t.skill.expect_gain("mining", 3000, snap))
        local ringr1, ring1 = t.inv.count("ring_of_life")
        t.check("reward.ring", ringr0 == "ok" and ringr1 == "ok" and ring0 == 0 and ring1 == 1,
            "ring_of_life " .. tostring(ring0) .. " -> " .. tostring(ring1) .. " (want 0 -> 1)")
        t.finish(0)
        return
    end,
}
