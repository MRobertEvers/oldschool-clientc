-- The Lost Tribe -- driven from the quest's own scripts
-- (OSRS-Content/.../quests/quest_losttribe/scripts/*.rs2).
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
--     ^lt_tunnel_enter 3221,9618, tunnel side x >= 3220 -> ^lt_cellar_exit 3218,9618).
--   * The maze's collapsing floor (losttribe_tunnels.rs2) drops the player into the Lumbridge
--     Swamp Caves at ^lt_swamp_landing 3209,9585, a pocket left by the swamp_cave_climbing_rope
--     3169,9572 (maplink_0_49_149_33_35: 3169,9571 -> 3169,3171).
--   * Bob's Brilliant Axes: its east door poordooropen 3233,3203 stands open (map-placed).
--   * Varrock palace library: fai_varrock_castle_door 3210,3490 (library z >= 3490).
--   * The Goblin Village generals' hut: goblin_outpost_poordoor_double_inner 2957,3509 (hut
--     z >= 3510).
--   * The H.A.M. hideout: osf_trapdoor_closed 3166,3252 picked (op5), osf_trapdoor_open down
--     (-> ^lt_ham_trapdoor_in 3149,9652) and osf_ham_ladder 3149,9653 up (-> 3165,3251)
--     (losttribe_ham.rs2).
--   * The Dorgeshuun mines: Mistag's "way out" (losttribe_finish.rs2 -> ^lt_brooch_tile
--     3230,9610) and Kazgar's shortcut (-> ^lt_mistag_tile) are the npcs' own teleports.
return {
    id = "losttribe",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::complete quest_goblindiplomacy", "::complete quest_runemysteries", "::setlevel mining 17", "::setlevel agility 13", "::setlevel thieving 13", "::give bronze_pickaxe 1", "::give candle_lantern_lit 1",
        -- food for the walk out of the Swamp Caves after the maze's floor trap: their cave bugs set on a
        -- 10-hitpoint player at once (run 2: 10 -> 5 hp over the 108-tile walk)
        "::give trout 4" },
    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb532_lost_tribe_quest",
            constants = { not_started = 0, started = 1, tunnel = 4, book = 5, symbol = 6, generals = 7, mistag = 8, ham_hunt = 9, treaty = 10, complete = 11 },
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
                src = { 3221, 9618 }, dest = { 3218, 9618 }, attempts = 1 })
        end

        -- start: Sigmund, in the Duke's room (fixture stands the player north of the keep, 3206,3233)
        walk("goTalkToSigmund.toStairs", { { 3211, 3231 }, { 3217, 3230 }, { 3220, 3226 }, { 3221, 3220 },
            { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } })
        up_to_duke("goTalkToSigmund")
        t.exec("talkToSigmund", t.player.talk_to, "lost_tribe_sigmund", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("sigmund.opts", r, d)
        t.exec("sigmund.quests", t.chat.choose, "Do you have any quests for me?")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("sigmund.end", r, d)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- the Cook is the red herring (guide: talkToAllAboutCellar)
        down_from_duke("talkToAllAboutCellar")
        walk("talkToAllAboutCellar.toKitchen", STAIRS_TO_KITCHEN)
        t.exec("talkToAllAboutCellar", t.player.talk_to, "cook", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("cook.end", r, d)
        t.exec("cook.nothing", t.var.await, "varb537_lost_tribe_contact", 0, 3)

        -- witness: Bob, in his axe shop (east door stands open)
        walk("talkToBob.toShop", { { 3208, 3213 }, { 3209, 3211 }, { 3215, 3212 }, { 3215, 3218 }, { 3222, 3218 },
            { 3230, 3218 }, { 3234, 3212 }, { 3234, 3205 }, { 3234, 3203 } })
        t.exec("talkToBob.shopDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3233, 3203, 0 }, near = { 3234, 3203 }, far = { 3231, 3204 },
            far_ok = function(tt) return tt.x <= 3233 and tt.x >= 3229 and tt.z >= 3201 and tt.z <= 3205 end,
            far_desc = "in Bob's shop, x 3229-3233 z 3201-3205" })
        t.exec("talkToBob", t.player.talk_to, "bob", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("bob.end", r, d)
        t.exec("bob.knows", t.var.await, "varb537_lost_tribe_contact", 1, 5)
        t.exec("talkToDuke.shopDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 3233, 3203, 0 }, near = { 3232, 3203 }, far = { 3235, 3203 },
            far_ok = function(tt) return tt.x >= 3234 end, far_desc = "out of Bob's shop, x >= 3234" })
        walk("talkToDuke.toStairs", { { 3234, 3210 }, { 3234, 3218 }, { 3226, 3218 }, { 3218, 3218 },
            { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } })
        up_to_duke("talkToDuke")
        t.exec("talkToDuke", t.player.talk_to, "duke_of_lumbridge", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("duke.permission", r, d)
        t.exec("duke.permits", t.var.await, "varb537_lost_tribe_contact", 2, 5)

        -- dig the rubble, squeeze through, take the brooch, trip the maze's floor trap
        down_from_duke("goDownIntoBasement")
        walk("goDownIntoBasement.toKitchen", STAIRS_TO_KITCHEN)
        t.exec("goDownIntoBasement", t.player.climb, { loc = "qip_cook_trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3209, 3216, 0 }, src = { 3210, 3216 }, dest = { 3210, 9616, 0 } })
        walk("usePickaxeOnRubble.walk", CELLAR_TO_HOLE)
        t.exec("usePickaxeOnRubble", t.player.use_on, "bronze_pickaxe", t.player.by_symbol("loc", "lost_tribe_cellar_wall"))
        t.exec("rubble.dug", t.var.await, "varb532_lost_tribe_quest", 4, 12)
        hole_in("climbThroughHole")
        hole_out("hole.back")
        hole_in("climbThroughHole2")
        walk("grabBrooch.walk", { { 3222, 9618 }, { 3224, 9618 }, { 3229, 9610 } })
        t.exec("grabBrooch", t.player.click_obj, "lost_tribe_brooch", 3)
        t.exec("brooch.have", t.inv.await, "lost_tribe_brooch", 1, 6)
        -- the maze: a floor trap (lost_tribe_trap_floor 3238,9622, maps/m50_150.jl2) drops the
        -- player into the swamp caves and puts the light out. The walk is cut short by the fall,
        -- so its answer is a note; the landing is the check.
        local wr, wd = t.player.walk_to(3238, 9622, 20)
        t.note("trap.step: walk_to 3238,9622 answered " .. tostring(wr) .. " " .. tostring(wd))
        t.ticks(4)
        tr, tl = t.world.tile()
        t.check("trap.fell", tr == "ok" and tl.x == 3209 and tl.z == 9585 and tl.level == 0,
            "after stepping on the floor trap: " .. tile_text() .. " (want ^lt_swamp_landing 3209,9585,0)")
        t.exec("trap.light.out", t.inv.await, "candle_lantern_unlit", 1, 3)
        -- CONTENT NOTE: no [opheldu,tinderbox] case relights a candle lantern (firemaking.rs2:25-55), and the tunnels never test for a light, so the walk back goes on with it unlit
        local lr, lc = t.inv.count("candle_lantern_unlit")
        t.check("light.stays.out", lc == 1, "candle_lantern_unlit count " .. tostring(lc) .. " -- no relight path in the content; the later tunnel walks need no light")
        -- out of the swamp caves by their climbing rope (reach.py 3209,9585 -> 3169,9571: REACH 108)
        t.exec("swampCaves.toRope", t.player.walk_route, { { 3201, 9586 }, { 3193, 9585 }, { 3186, 9583 }, { 3178, 9584 }, { 3171, 9586 },
            { 3163, 9587 }, { 3157, 9590 }, { 3151, 9589 }, { 3152, 9581 }, { 3155, 9575 }, { 3162, 9573 }, { 3169, 9571 } },
            { level = 0, vitals = { eat = "trout", below = 6 } })
        t.exec("swampCaves.ropeUp", t.player.climb, { loc = "swamp_cave_climbing_rope", op = 1, op_name = "Climb",
            at = { 3169, 9572, 0 }, src = { 3169, 9571 }, dest = { 3169, 3171, 0 } })
        -- margin: at least a quarter of the 10 hitpoints AND food left
        local hpr, hp = t.skill.read("hitpoints")
        local tor, trout = t.inv.count("trout")
        local hp_level = (hpr == "ok" and type(hp) == "table") and hp.level or nil
        t.check("swampCaves.margin", hp_level ~= nil and hp_level * 4 >= 10 and tor == "ok" and trout ~= nil and trout >= 1,
            "after the Swamp Caves: hitpoints " .. tostring(hp_level or hpr) .. "/10, trout left " .. tostring(trout))

        -- showBroochToDuke: the swamp -> the castle courtyard is open ground (reach.py REACH 152)
        t.exec("goto-showBroochToDuke.courtyard", t.player.goto_tile, 3222, 3218, 0)
        walk("showBroochToDuke.toStairs", COURTYARD_TO_STAIRS)
        up_to_duke("showBroochToDuke")
        t.exec("showBroochToDuke", t.player.talk_to, "duke_of_lumbridge", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("brooch.opts", r, d)
        t.exec("brooch.rubble", t.chat.choose, "I dug through the rubble...")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("brooch.end", r, d)
        t.exec("brooch.contact", t.var.await, "varb537_lost_tribe_contact", 3, 5)
        t.expect("quest.stage.book", t.quest.expect_stage("book"))

        -- Reldo hints at the book; the bookcase; read every spread. Varrock palace: the front
        -- doorway is open map (fai_varrock_museum_door_inactive_l/_r 3212-3213,3470); the
        -- library is behind fai_varrock_castle_door 3210,3490.
        down_from_duke("talkToReldo")
        walk("talkToReldo.outOfCastle", STAIRS_TO_COURTYARD)
        t.exec("goto-talkToReldo.palaceFront", t.player.goto_tile, 3212, 3460, 0)
        walk("talkToReldo.toLibrary", { { 3212, 3466 }, { 3212, 3472 }, { 3207, 3473 }, { 3206, 3478 },
            { 3206, 3484 }, { 3208, 3488 }, { 3210, 3489 } })
        t.exec("talkToReldo.libraryDoorIn", t.player.pass_door, { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open",
            at = { 3210, 3490, 0 }, near = { 3210, 3489 }, far = { 3210, 3492 },
            far_ok = function(tt) return tt.z >= 3490 end, far_desc = "in the library, z >= 3490" })
        t.exec("talkToReldo", t.player.talk_to, "reldo", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("reldo.opts", r, d)
        t.exec("reldo.brooch", t.chat.choose, "What can you tell me about this brooch?")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("reldo.end", r, d)
        t.exec("walk-searchBookcase", t.player.walk_to, 3208, 3496)
        t.exec("searchBookcase", t.player.click_loc, "lost_tribe_bookcase", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("bookcase.chat", r, d)
        t.exec("book.have", t.inv.await, "lost_tribe_book", 1, 5)
        t.exec("readBook", t.player.inv_op, "lost_tribe_book", 1)
        t.exec("book.open", t.ui.await_open, "lost_tribe_symbol_book", 10)
        t.ticks(2)
        local wgr, w = t.ui.widget("lost_tribe_symbol_book:lost_tribe_right_arrow")
        t.step("book.arrow", wgr == "ok" and "PASS" or "FAIL", tostring(wgr) .. " " .. tostring(w))
        t.exec("book.page0", t.ui.expect_text, "lost_tribe_symbol_book:com_32", "History of the Goblin Race")
        local comps = { "com_34", "com_39", "com_43", "com_23", "com_28" }
        local wants = { "war-god", "command", "twelve distinct", "Dorgeshuun", "Rekeshuun" }
        for i = 1, 5 do
            t.ui.invoke(w, 1)
            t.ticks(2)
            t.exec("book.page" .. i, t.ui.expect_text, "lost_tribe_symbol_book:" .. comps[i], wants[i])
            if i < 5 then t.expect("book.unfinished" .. i, t.quest.expect_stage("book")) end
        end
        t.ticks(3)
        r, d = t.chat.drain({ stop_at = "none" })
        t.ticks(3)
        t.exec("readBook.done", t.var.await, "varb532_lost_tribe_quest", 6, 8)
        t.key("escape")
        t.ticks(2)

        -- talkToGenerals: out of the library and the palace, overland to the Goblin Village
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
        t.exec("talkToGenerals", t.player.talk_to, "general_wartface_green", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("generals.opts", r, d)
        t.exec("generals.matter", t.chat.choose, "It doesn't really matter.")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("generals.end", r, d)
        t.expect("quest.stage.generals", t.quest.expect_stage("generals"))
        t.exec("walkToMistag.hutDoorOut", t.player.pass_door, { closed = "goblin_outpost_poordoor_double_inner",
            open = "goblin_outpost_openpoordoor_double_inner",
            at = { 2957, 3509, 0 }, near = { 2957, 3510 }, far = { 2957, 3507 },
            far_ok = function(tt) return tt.z <= 3509 end, far_desc = "out of the generals' hut, z <= 3509" })

        -- the marked path to Mistag: the castle kitchen, the trapdoor, the cellar, the hole
        t.exec("goto-walkToMistag.courtyard", t.player.goto_tile, 3222, 3218, 0)
        walk("walkToMistag.toKitchen", COURTYARD_TO_KITCHEN)
        trapdoor_down("walkToMistag")
        walk("walkToMistag.toHole", CELLAR_TO_HOLE)
        hole_in("enterTunnels")
        -- reach.py's walk through the maze, kept off every lost_tribe_trap_* tile (hops <= 10)
        local line = { {3222,9618},{3224,9618},{3229,9610},{3238,9610},{3241,9612},{3246,9612},{3249,9619},{3254,9625},{3252,9631},{3243,9631},{3234,9631},{3230,9634},{3230,9643},{3237,9648},{3244,9648},{3246,9645},{3240,9641},{3244,9637},{3252,9642},{3252,9646},{3257,9656},{3263,9656},{3269,9656},{3277,9652},{3277,9647},{3267,9643},{3267,9638},{3276,9637},{3283,9641},{3290,9645},{3300,9641},{3307,9631},{3297,9627},{3295,9622},{3290,9617},{3293,9612},{3297,9606},{3303,9606},{3309,9612},{3317,9612} }
        walk("walkToMistag", line)
        t.player.emote("Goblin Bow")
        t.ticks(2)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("emoteAtMistag.chat", r, d)
        t.exec("emoteAtMistag", t.var.await, "varb532_lost_tribe_quest", 8, 6)

        -- Mistag shows the way out (-> ^lt_brooch_tile 3230,9610); back through the hole, up the
        -- cellar ladder, through the keep to the Duke
        t.exec("mistag.talk", t.player.talk_to, "lost_tribe_mistag", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("mistag.opts", r, d)
        t.exec("mistag.out", t.chat.choose, "Can you show me the way out of the mine?")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("mistag.out.end", r, d)
        t.ticks(4)
        tr, tl = t.world.tile()
        t.check("mistag.landed", tr == "ok" and tl.x == 3230 and tl.z == 9610 and tl.level == 0,
            "after Mistag's way out: " .. tile_text() .. " (want ^lt_brooch_tile 3230,9610,0)")
        walk("goTalkToDukeAfterEmote.toHole", { { 3227, 9613 }, { 3225, 9617 }, { 3221, 9618 } })
        hole_out("goTalkToDukeAfterEmote.holeOut")
        walk("goTalkToDukeAfterEmote.toLadder", HOLE_TO_LADDER)
        ladder_up("goTalkToDukeAfterEmote")
        walk("goTalkToDukeAfterEmote.toStairs", KITCHEN_TO_STAIRS)
        up_to_duke("goTalkToDukeAfterEmote")
        t.exec("goTalkToDukeAfterEmote", t.player.talk_to, "duke_of_lumbridge", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("contact.opts", r, d)
        t.exec("contact.made", t.chat.choose, "I've made contact with the cave goblins...")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("contact.end", r, d)
        t.exec("contact.stage", t.var.await, "varb532_lost_tribe_quest", 9, 6)

        -- Sigmund's key, the chest in the next room, the H.A.M. hideout crate
        t.exec("pickpocketSigmund", t.player.press, "lost_tribe_sigmund", 3, 8)
        t.exec("key.have", t.inv.await, "lost_tribe_chest_key", 1, 8)
        duke_door_out("unlockChest")
        walk("unlockChest.toDoor", { { 3206, 3218 }, { 3206, 3214 } }, 1)
        t.exec("unlockChest.chestDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3214, 1 }, near = { 3207, 3214 }, far = { 3209, 3216 },
            far_ok = function(tt) return tt.x >= 3208 end, far_desc = "in the chest room, x >= 3208" })
        t.exec("unlockChest", t.player.click_loc, "lost_tribe_chest", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("chest.chat", r, d)
        t.exec("robes.have", t.inv.await, "ham_robe", 1, 6)
        t.exec("robes.state", t.var.await, "varb534_lost_tribe_ham", 1, 4)
        local kr, kc = t.inv.count("lost_tribe_chest_key")
        t.check("unlockChest.keyUsed", kr == "ok" and kc == 0, "lost_tribe_chest_key count " .. tostring(kc) .. " (losttribe_ham.rs2 [oploc1,lost_tribe_chest] inv_del)")
        t.exec("enterHamLair.chestDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3214, 1 }, near = { 3208, 3214 }, far = { 3206, 3214 },
            far_ok = function(tt) return tt.x <= 3207 end, far_desc = "out of the chest room, x <= 3207" })
        walk("enterHamLair.toStairsTop", { { 3206, 3221 }, { 3205, 3228 } }, 1)
        stairs_down("enterHamLair")
        walk("enterHamLair.outOfCastle", STAIRS_TO_COURTYARD)
        t.exec("goto-enterHamLair.field", t.player.goto_tile, 3163, 3256, 0)
        t.exec("ham.picklock", t.player.click_loc, "osf_trapdoor_closed", 5)
        t.exec("ham.picklocked", t.var.await, "varb235_ham_thief", 1, 6)
        t.exec("enterHamLair", t.player.climb, { loc = "osf_trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3166, 3252, 0 }, dest = { 3149, 9652, 0 } })
        t.exec("searchHamCrates", t.player.click_loc, "lost_tribe_crate", 1)
        t.ticks(3)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("crate.chat", r, d)
        t.exec("silverware.have", t.inv.await, "lost_tribe_silverware", 1, 6)
        t.exec("leaveHamLair", t.player.climb, { loc = "osf_ham_ladder", op = 1, op_name = "Climb-up",
            at = { 3149, 9653, 0 }, dest = { 3165, 3251, 0 } })

        -- goToDukeWithSilverware: the field -> the courtyard is open ground
        t.exec("goto-goToDukeWithSilverware.courtyard", t.player.goto_tile, 3222, 3218, 0)
        walk("goToDukeWithSilverware.toStairs", COURTYARD_TO_STAIRS)
        up_to_duke("goToDukeWithSilverware")
        t.exec("goToDukeWithSilverware", t.player.talk_to, "duke_of_lumbridge", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("silverware.chat", r, d)
        t.exec("treaty.have", t.inv.await, "lost_tribe_treaty", 1, 6)
        local sr, sc = t.inv.count("lost_tribe_silverware")
        t.check("silverware.handedIn", sr == "ok" and sc == 0, "lost_tribe_silverware count " .. tostring(sc))
        t.expect("quest.stage.treaty", t.quest.expect_stage("treaty"))

        -- talkToKazgar: the guide at the tunnel mouth shortcuts to Mistag
        down_from_duke("talkToKazgar")
        walk("talkToKazgar.toKitchen", STAIRS_TO_KITCHEN)
        trapdoor_down("talkToKazgar")
        walk("talkToKazgar.toHole", CELLAR_TO_HOLE)
        hole_in("enterTunnelsFinal")
        walk("talkToKazgar.walk", { { 3222, 9618 }, { 3224, 9618 }, { 3229, 9610 } })
        t.exec("talkToKazgar", t.player.talk_to, "lost_tribe_guide_2ops", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("kazgar.opts", r, d)
        t.exec("kazgar.mines", t.chat.choose, "Can you show me the way to the mines?")
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("kazgar.end", r, d)
        t.ticks(4)
        tr, tl = t.world.tile()
        t.check("kazgar.landed", tr == "ok" and tl.x >= 3300, "at " .. tile_text() .. " (want ^lt_mistag_tile 3319,9615)")

        -- talkToMistagForEnd: rewards (losttribe_finish.rs2 [proc,lost_tribe_quest_complete]:
        -- 3000 Mining XP, a Ring of life, 1 quest point)
        local ringr0, ring0 = t.inv.count("ring_of_life")
        local snap_r, snap = t.skill.snapshot()
        t.check("reward.snapshot", snap_r, "skill snapshot before the hand-in: " .. tostring(snap_r))
        t.exec("talkToMistagForEnd", t.player.talk_to, "lost_tribe_mistag_2ops", 1)
        r, d = t.chat.drain({ stop_at = "none" }); t.expect("mistag.treaty", r, d)
        t.ticks(3)
        t.quest.expect_complete()
        t.expect("reward.mining", t.skill.expect_gain("mining", 3000, snap))
        t.exec("reward.ring.await", t.inv.await, "ring_of_life", 1, 6)
        local ringr1, ring1 = t.inv.count("ring_of_life")
        t.check("reward.ring", ringr0 == "ok" and ringr1 == "ok" and ring0 == 0 and ring1 == 1,
            "ring_of_life " .. tostring(ring0) .. " -> " .. tostring(ring1) .. " (want 0 -> 1)")
        t.finish(0)
        return
    end,
}
