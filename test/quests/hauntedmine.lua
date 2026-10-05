-- Haunted Mine (quest_hauntedmine).
--
-- WALLS (door rule, owner 2026-10-03): every door, gate, trapdoor, ladder, stair, crawl and lift
-- between the player and a target is pressed on every visit, in and out. Checked with
-- test/quests/orchestrator/matthew-mbp-m4/reports/sample_tools/{reach,comp,locs_near}.py (doors
-- closed):
--   * Lumbridge -> the Salve: only through the Varrock members' gate 3319,3467-3468; Morytania only
--     the way Priest in Peril opens it: the Paterdomus trapdoor 3405,3507, pip_underground_door1
--     3405,9895 and door2 3431,9897 (walk-through), Drezel's advice (60 -> 61) and the holy barrier
--     3440,9886 (p_telejump to 3423,3485). From there to the Zealot is open ground (reach margin 250:
--     REACH closed-doors len=677, so the Mort Myre gate 3443,3458 is not the only way).
--   * The mine: two cart-tunnel crawls (hauntedmine_back_entrance2 3429,3225 approached from
--     3428,3225; hauntedmine_back_entrance1 3430,3233 approached from 3429,3233; both tiles in the
--     overland component of the Zealot), and every level below them is a p_teleport ladder
--     (hauntedmine_dungeon.rs2): each one is a t.player.climb row graded on its landing.
--   * The lift room's lift (lift_side_r) p_teleports into the flooded pool and wades the player out to
--     the south shore 2725,4452 (hauntedmine_dungeon.rs2:415 + :422 p_exactmove(^hmq_flooded_shore)):
--     a climb row graded on that exact tile. The shore is in the corridor of both stair tops.
--   * Treus Dayth's room x 2775-2798 z 4442-4469 behind hauntedmine_boss_door 2799,4453 (a
--     door_selfstage door, maps/m43_69.jl2): pass_door in and out.
--   * The crystal room behind hauntedmine_rewarddoor_l 2773,4450 (south edge of 2773,4450).
--   * The flooded level's two stair tops (2746,4436 / 2692,4436) share one corridor (comp.py: 223
--     tiles); the stairs below each are p_teleports (hauntedmine_dayth.rs2).
return {
    id = "hauntedmine",
    fixture = "fresh_lumbridge.ini",
    max_frames = 220000, -- the walk in through Paterdomus, the mine's eleven climbs and the Dayth fight
    setup = {
        "::clearinv",
        "::complete quest_priestinperil",
        "::setlevel crafting 35",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        -- Protect from Missiles (prayers.dbrow: level 40) for Treus Dayth: his pickaxe throw is cut by a
        -- third under it (hauntedmine_dayth.rs2:152); with no prayer seam35 lost 5 of 7 fights
        -- (docs/quest_authoring/verbs-combat.md "A covered Attack press, a boss that teleports").
        "::setlevel prayer 43",
        "::give magic_shortbow 1",
        "::give rune_arrow 400",
        -- 23 sharks (the prayer potion's old slot; 43 prayer points outlast the fight, r2: 10-11 left):
        -- r2 runs ate 15-19 (account hauntedmine_b: 22 -> 3). Pack at the chisel: 23 + dagger + Zealot's
        -- key + fungus + chisel = 27 of 28, the bow and arrows worn.
        "::give shark 23",
        -- Priest in Peril's own reward (::complete grants no items); Drezel's holy-barrier advice
        -- needs it held (mausoleum_drezel.rs2:29-34).
        "::give dagger_wolfbane 1",
    },
    run = function(t)
        t.quest.bind({
            varp = "varp382_hauntedmine",
            constants = { not_started = 0, started = 1, dayth_killed = 9, key_collected = 10, complete = 11 },
            row = "quest_hauntedmine",
            display = "Haunted Mine",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- Into Morytania: the Varrock members' gate, the Paterdomus trapdoor, the two mausoleum
        -- gates, Drezel's advice and the holy barrier.
        -- ---------------------------------------------------------------
        t.exec("goto-enterMorytania.varrockGate", t.player.goto_tile, 3318, 3468, 0)
        t.exec("enterMorytania.varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
            open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
        t.exec("goto-enterMorytania.trapdoor", t.player.goto_tile, 3405, 3506, 0)
        t.exec("enterMorytania.openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
            end,
            note = "enterMorytania: the trapdoor opens",
        }, 6)
        local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
        local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
        t.check("enterMorytania.trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
            "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
        t.exec("enterMorytania.descend", t.player.cross_trap, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906 }, attempts = 2 })
        t.exec("enterMorytania.gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
            near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
            far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
        t.exec("enterMorytania.gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
            near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
            far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
        -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:145-154, LostCity drezel.rs2:138-147):
        -- 60 -> 61, the holy barrier opens.
        t.exec("enterMorytania.talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec("enterMorytania.talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })

        -- ---------------------------------------------------------------
        -- The Zealot (open ground outside the mine).
        -- ---------------------------------------------------------------
        t.exec("goto-zealot", t.player.goto_tile, 3443, 3258, 0)
        t.exec("talkToZealot", t.player.talk_to, "saradominist_zealot", 1)
        t.exec("talkToZealot-dialog", t.chat.play, {
            "npc:State thy allegiance",
            "choose:/Saradomin/",
            "player:I follow",
            "npc:Ah, a wise",
            "choose:/challenges/",
            "player:I come seeking",
            "npc:A noble cause",
            "choose:What quest is that then?",
            "player:What quest",
            "npc:I seek to reclaim",
            "npc:*",
            "npc:*",
            "choose:/other way/",
            "player:Is there any other",
            "npc:Indeed I have",
            "npc:*",
            "choose:/borrow/",
            "player:Can I borrow",
            "npc:*",
            "options",
        })
        t.key("escape")
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local hk, hv = t.var.server("varb2397_hauntedmine_heardaboutkey")
        t.check("heardaboutkey", hk == "ok" and hv == 1, "varb2397_hauntedmine_heardaboutkey " .. tostring(hk) .. " " .. tostring(hv) .. " (want 1)")
        t.exec("pickpocketZealot", t.player.press, "saradominist_zealot", 3, 8)
        t.exec("zealotkey", t.inv.await, "hauntedmine_lift_key", 1, 6)
        t.exec("wield", t.player.equip, "magic_shortbow")
        t.exec("wield-arrows", t.player.equip, "rune_arrow")
        -- Rapid (combat_tab.rs2 [if_button,combat_interface:style_slot_1] -> %com_mode 1; combat.rs2:314
        -- a ranged swing one tick sooner): Treus Dayth is a DPS race, the hazards grow as he weakens.
        do
            local tab_result = t.ui.tab("combat")
            t.ticks(2)
            local wr, w = t.ui.widget("combat_interface:style_slot_1")
            t.ui.invoke(w, 1)
            t.ticks(2)
            local _, mode = t.var.varp("varp43_com_mode")
            t.check("rangedRapid", tab_result == "ok" and wr == "ok" and mode == 1, "combat tab " .. tostring(tab_result)
                .. ", style_slot_1 " .. tostring(wr) .. "; varp43_com_mode " .. tostring(mode) .. " (want 1, Rapid)")
            t.ui.tab("inventory")
            t.ticks(1)
        end

        local function click(name, sym, x, z, op)
            t.exec(name, t.player.click_loc, sym, op or 1, { at = { x, z } })
            t.ticks(3)
        end
        -- One mine climb: every ladder, crawl and stair here is a p_teleport (hauntedmine_dungeon.rs2,
        -- hauntedmine_dayth.rs2). A landing in the surface frame <-> the underground frame (z // 6400)
        -- is a plain climb; one inside the frame names the script line that moves the player.
        local function climb(name, loc, op_name, at, dest, same, src, ticks)
            local spec = { loc = loc, op = 1, op_name = op_name, at = { at[1], at[2], 0 }, dest = { dest[1], dest[2], 0 },
                src = src, ticks = ticks }
            if same ~= nil then
                spec.same_level = same
            end
            t.exec(name, t.player.climb, spec)
        end

        -- ---------------------------------------------------------------
        -- South tunnel: fungus -> cart -> levers -> panel, and back out.
        -- ---------------------------------------------------------------
        t.exec("walk-enterMine", t.player.walk_to, 3428, 3225, 40)
        climb("enterMine", "hauntedmine_back_entrance2", "Crawl-down", { 3429, 3225 }, { 3420, 9620 }, nil, { 3428, 3225 })
        climb("goDownFromLevel1South", "hauntedmine_laddertop", "Climb-down", { 3422, 9625 }, { 2782, 4568 })
        climb("goDownFromLevel2South", "hauntedmine_laddertop_1sw", "Climb-down", { 2798, 4567 }, { 2733, 4510 },
            "hauntedmine_dungeon.rs2:53 p_teleport(^hmq_level3_south_enter)")
        climb("goDownToFungusRoom", "hauntedmine_laddertop_1e", "Climb-down", { 2725, 4486 }, { 2789, 4488 },
            "hauntedmine_dungeon.rs2:76 p_teleport(^hmq_cartroom_enter)")
        click("pickFungus", "glowing_mushroom2", 2793, 4493)
        t.exec("fungus-held", t.inv.await, "glowing_fungus", 1, 6)
        click("putFungusInCart", "hauntedmine_puzzle_cart", 2778, 4506)
        t.exec("begincart", t.msg.expect, "You place the glowing fungus")
        t.exec("fungus-gone-from-pack", t.inv.await, "glowing_fungus", 0, 4)
        -- The four levers the target needs flipped from 0 (quest_hauntedmine.constant ^hmq_lever_target_*:
        -- a=1 b=1 e=1 i=1, the rest 0; lever1 -> b, lever2 -> a, lever5 -> e, lever6 -> i,
        -- hauntedmine_dungeon.rs2:227-273), then the points panel sends the cart.
        click("pullLeverA", "hauntedmine_point_lever1", 2785, 4517)
        click("pullLeverB", "hauntedmine_point_lever2", 2784, 4517)
        click("pullLeverE", "hauntedmine_point_lever5", 2785, 4515)
        click("pullLeverF", "hauntedmine_point_lever6", 2768, 4533)
        click("readPanel", "hauntedmine_points_info", 2769, 4522)
        t.exec("endcart", t.msg.expect, "mine cart trundles")
        t.exec("endcart.var", t.var.await_server, "varb2396_hauntedmine_endcart_fungus", 1, 4)
        -- back out the south tunnel, every ladder climbed; the cart room's floor winds round the tracks
        -- (one walk_to from the panel stalled at the fungus wall, run 1), so it is walked in hops
        t.exec("walk-goUpFromFungusRoom", t.player.walk_route, { { 2770, 4521 }, { 2777, 4520 }, { 2778, 4513 },
            { 2779, 4506 }, { 2786, 4505 }, { 2791, 4502 }, { 2793, 4496 }, { 2793, 4490 }, { 2789, 4487 } })
        climb("goUpFromFungusRoom", "hauntedmine_ladder_1w", "Climb-up", { 2789, 4486 }, { 2733, 4510 },
            "hauntedmine_dungeon.rs2:158 p_teleport(^hmq_level3_south_enter)", { 2789, 4487 })
        climb("goUpFromLevel3South", "hauntedmine_ladder_1ne", "Climb-up", { 2734, 4503 }, { 2797, 4566 },
            "hauntedmine_dungeon.rs2:145 p_teleport(^hmq_level2_south_top)")
        climb("goUpFromLevel2South", "hauntedmine_ladder", "Climb-up", { 2782, 4569 }, { 3420, 9620 })
        climb("leaveLevel1South", "lotr_back_entrance1_inside", "Crawl-through", { 3408, 9623 }, { 3429, 3225 })

        -- ---------------------------------------------------------------
        -- North tunnel: collect the fungus, the lift room, the valve, the lift.
        -- ---------------------------------------------------------------
        t.exec("walk-enterMineNorth", t.player.walk_to, 3429, 3233, 30)
        climb("enterMineNorth", "hauntedmine_back_entrance1", "Crawl-down", { 3430, 3233 }, { 3405, 9631 }, nil, { 3429, 3233 })
        climb("goDownLevel1North", "hauntedmine_laddertop", "Climb-down", { 3413, 9633 }, { 2773, 4576 })
        climb("goDownLevel2North", "hauntedmine_laddertop_1sw", "Climb-down", { 2797, 4599 }, { 2732, 4534 },
            "hauntedmine_dungeon.rs2:57 p_teleport(^hmq_level3_north_enter)")
        climb("goDownToCollectFungus", "hauntedmine_laddertop_1e", "Climb-down", { 2710, 4540 }, { 2774, 4538 },
            "hauntedmine_dungeon.rs2:72 p_teleport(^hmq_collectroom_enter)")
        click("collectFungus", "hauntedmine_puzzle_cart", 2774, 4537)
        t.exec("collectFungus-dialog", t.chat.play, { "player:Take it" })
        t.exec("fungus-collected", t.inv.await, "glowing_fungus", 1, 6)
        climb("goUpFromCollectRoom", "hauntedmine_ladder_1w", "Climb-up", { 2774, 4540 }, { 2710, 4538 },
            "hauntedmine_dungeon.rs2:154 p_teleport(^hmq_collect_exit)")
        t.exec("walk-goDownFromLevel3NorthEast", t.player.walk_route, { { 2710, 4538 }, { 2712, 4532 }, { 2719, 4533 },
            { 2727, 4533 }, { 2734, 4534 }, { 2739, 4531 }, { 2736, 4526 }, { 2731, 4529 } })
        climb("goDownFromLevel3NorthEast", "hauntedmine_laddertop_1e", "Climb-down", { 2732, 4529 }, { 2797, 4529 },
            "hauntedmine_dungeon.rs2:74 p_teleport(^hmq_liftladder_enter)", { 2731, 4529 })
        t.exec("walk-chisel", t.player.walk_route, { { 2797, 4529 }, { 2802, 4526 }, { 2804, 4520 }, { 2802, 4514 },
            { 2803, 4509 }, { 2800, 4504 }, { 2801, 4501 } })
        t.exec("pickUpChisel", t.player.click_obj, "chisel", 3)
        t.exec("chisel", t.inv.await, "chisel", 1, 6)
        t.drive.camera(0, 383, 300)
        -- The valve (hauntedmine_dungeon.rs2:363-386). Walk beside it first: use_on does not follow a
        -- 50-tick walk from the crate (verbs-inventory-shops: use_on past its settle).
        t.exec("walk-valve", t.player.walk_to, 2807, 4496, 60)
        -- Turn before the key: the valve is locked until the Zealot's key is used on it (:374-378). The
        -- press is the attempt; the lock (its line, liftpoweredonce still 0) is the row.
        local tr, td = t.player.click_loc("hauntedmine_lift_valve", 1, { at = { 2808, 4496 } })
        t.note("Turn before the key (attempt): click_loc hauntedmine_lift_valve -> " .. tostring(tr) .. " " .. tostring(td))
        t.ticks(2)
        do
            local lr, ld = t.msg.expect("locked in position. There is a small keyhole")
            local _, once0 = t.var.server("varb2393_hauntedmine_liftpoweredonce")
            local _, now0 = t.var.server("varb2394_hauntedmine_liftpowerednow")
            t.check("valveLockedWithoutKey", lr == "ok" and once0 == 0 and now0 == 0, tostring(lr) .. " "
                .. tostring(ld) .. "; varb2393_hauntedmine_liftpoweredonce " .. tostring(once0)
                .. ", varb2394_hauntedmine_liftpowerednow " .. tostring(now0) .. " (want the lock line, 0 and 0)")
        end
        -- useKeyOnValve: [oplocu,hauntedmine_lift_valve] with the key unlocks it (:363-371) and opens the
        -- flow at once (@hmq_valve_turn, :381-386), so a Turn after the key only says "already open"
        -- (:382-384); the guide's openValve ("Turn the valve") is that same flow, graded on the line and
        -- liftpowerednow (openValve.*). The key is kept.
        local valve = t.player.by_symbol("loc", "hauntedmine_lift_valve")
        t.exec("useKeyOnValve", t.player.use_on, "hauntedmine_lift_key", valve)
        t.exec("useKeyOnValve.line", t.msg.expect, "The key unlocks the valve.")
        t.exec("useKeyOnValve.var", t.var.await_server, "varb2393_hauntedmine_liftpoweredonce", 1, 4)
        do
            local kr, keys = t.inv.count("hauntedmine_lift_key")
            t.check("useKeyOnValve.keyKept", kr == "ok" and keys == 1,
                "hauntedmine_lift_key in pack " .. tostring(keys) .. " (want 1: the valve keeps no key)")
        end
        t.exec("openValve.flowing", t.msg.expect, "Water begins to flow through the lift mechanism")
        t.exec("openValve.var", t.var.await_server, "varb2394_hauntedmine_liftpowerednow", 1, 4)
        -- The lift: p_teleport into the pool (:415), then the wade to the south shore 2725,4452
        -- (:422 p_exactmove(^hmq_flooded_shore), quest_hauntedmine.constant:224 0_42_69_37_36).
        t.exec("goDownLift", t.player.climb, { loc = "lift_side_r", op = 1, op_name = "Go-down", at = { 2807, 4492, 0 },
            dest = { 2725, 4452, 0 }, ticks = 70, same_level = "hauntedmine_dungeon.rs2:415 "
                .. "p_teleport(^hmq_floodedroom_enter) + :422 p_exactmove(^hmq_flooded_shore)" })
        t.exec("lift-line", t.msg.expect, "You take the lift down")
        t.exec("lift-wade", t.msg.expect, "You wade out to the south shore")

        -- ---------------------------------------------------------------
        -- The flooded level: from the shore along the corridor to the east stair top. One walk_to
        -- 2747,4440 from the shore stalled at 2745,4438 beside the stair (r2 run 1), so it goes in hops
        -- down the shore's west side and along the corridor (reach.py: each hop REACH, len <= 11).
        -- ---------------------------------------------------------------
        t.exec("walk-flooded-east", t.player.walk_route, { { 2722, 4447 }, { 2720, 4438 }, { 2730, 4437 },
            { 2740, 4437 }, { 2747, 4440 } })
        t.drive.camera(0, 383, 300)
        climb("goDownToDayth", "hauntedmine_dark_stairs_top", "Walk-down", { 2746, 4436 }, { 2810, 4453 },
            "hauntedmine_dayth.rs2:24 p_teleport(^hmq_dayth_stair_enter)")

        -- ---------------------------------------------------------------
        -- Treus Dayth's room, behind hauntedmine_boss_door 2799,4453 (door_selfstage: the leaf swings
        -- one tile west, onto 2798,4453's north edge).
        -- ---------------------------------------------------------------
        local BOSS_IN = { closed = "hauntedmine_boss_door", open = "hauntedmine_boss_door", at = { 2799, 4453, 0 },
            near = { 2800, 4453 }, far = { 2797, 4453 } }
        local BOSS_OUT = { closed = "hauntedmine_boss_door", open = "hauntedmine_boss_door", at = { 2799, 4453, 0 },
            near = { 2797, 4453 }, far = { 2801, 4453 } }
        t.exec("tryToPickUpKey.bossDoorIn", t.player.pass_door, BOSS_IN)
        -- Protect from Missiles goes up BEFORE the key: Dayth spawns on the press and swings on his
        -- 4-tick ai_timer (hauntedmine_dayth.rs2:89-93, 122-154), the prayer read on that tick.
        do
            local _, pts = t.skill.read("prayer")
            local tab_result = t.ui.tab("prayer")
            t.ticks(2)
            local wr, w = t.ui.widget("prayerbook:prayer14")
            t.ui.invoke(w, 1)
            t.ticks(2)
            local _, on = t.var.varbit("varb4117_prayer_protectfrommissiles")
            t.check("killDayth.protectMissiles", tab_result == "ok" and wr == "ok" and on == 1
                and type(pts) == "table" and pts.level >= 40,
                "varb4117_prayer_protectfrommissiles " .. tostring(on) .. "; prayer points before "
                    .. tostring(type(pts) == "table" and (tostring(pts.level) .. "/" .. tostring(pts.base_level)) or pts))
        end
        t.drive.camera(0, 383, 400)
        -- tryToPickUpKey: the press answers late or not at all (run 3: `timeout ... did not move in 8
        -- tick(s)` while the line below was already in the chatbox), so the press is the attempt and
        -- Dayth's rising is the row.
        local kpr, kpd = t.player.press("hauntedmine_boss_key", 1, 8)
        t.note("press hauntedmine_boss_key op 1 -> " .. tostring(kpr) .. " " .. tostring(kpd))
        local rr, rd = t.msg.expect("Treus Dayth rises")
        if rr ~= "ok" then rr, rd = t.msg.await("Treus Dayth rises", 40) end
        t.expect("tryToPickUpKey", rr, rd)
        -- The fighting tile 2793,4452: off the cart track rows (hauntedmine_dayth.rs2:213-219
        -- [proc,hmq_on_dayth_track]: up to 9 EVERY tick once Dayth is at 25 hp or less) and 5 or more from
        -- every crane (size 3, m43_69.spawn 2782,4458 2787,4446 2787,4451 2793,4458; up to 10 each 5 ticks
        -- within 3, hauntedmine_dayth.rs2:189-196). Top-down and zoomed out, so a re-engage on a shifted
        -- Dayth is not a `covered` press probed pixel by pixel while he hits (run 12 died inside one).
        t.exec("walk-fightTile", t.player.walk_to, 2793, 4452, 12)
        t.drive.camera(0, 383, 900)
        -- One Attack and one kill wait, both eating under 85 (b63-seam1: t.player.attack's opts.eat, and
        -- await_dead_engaged's re-engagements eat before every press and press fast under the line --
        -- runs 12, 15 and 18 died inside a re-attack that probed pixels with nobody eating). The wait
        -- follows Dayth's npc_tele to his new slot (run 21: 6 re-engagements, dead in 129 ticks).
        local EAT = { eat = { item = "shark", below = 85 } }
        local food_result0, food_before = t.inv.count("shark")
        local _, attack_d = t.exec("attackDayth", t.player.attack, "hauntedmine_boss_ghost", 2, 20, EAT)
        local _, kill_d = t.exec("killDayth", t.npc.await_dead_engaged, 300, 30, EAT)
        t.exec("dayth-killed", t.var.await, "varp382_hauntedmine", 9, 10)
        do
            -- the lowest hp each eater READ (every tick of the press and the wait), never a fallback
            local low_attack = tonumber(tostring(attack_d):match("lowest hp (%d+)/"))
            local low_kill = tonumber(tostring(kill_d):match("lowest hp (%d+)/"))
            local lowest = (low_attack ~= nil and low_kill ~= nil) and math.min(low_attack, low_kill) or nil
            local _, hitpoints = t.skill.read("hitpoints")
            local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
            local food_result, food_left = t.inv.count("shark")
            t.check("killDayth.margin", lowest ~= nil and max_hp ~= nil and food_result0 == "ok" and food_result == "ok"
                and lowest * 4 >= max_hp and food_left >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. " (attack press " .. tostring(low_attack)
                    .. ", kill wait " .. tostring(low_kill) .. "), sharks " .. tostring(food_before) .. " -> "
                    .. tostring(food_left) .. " (margin: lowest hp >= a quarter of max AND at least one shark left)")
        end
        -- Protection held the whole fight (prayer will not regenerate: 43 points staged, read before the
        -- key), then off: the fight is over.
        do
            local _, on_kill = t.var.varbit("varb4117_prayer_protectfrommissiles")
            local _, pts_kill = t.skill.read("prayer")
            t.ui.tab("prayer")
            t.ticks(2)
            local _, w = t.ui.widget("prayerbook:prayer14")
            t.ui.invoke(w, 1)
            t.ticks(2)
            local _, on = t.var.varbit("varb4117_prayer_protectfrommissiles")
            local _, pts = t.skill.read("prayer")
            t.check("killDayth.prayerHeld", on_kill == 1 and type(pts_kill) == "table" and pts_kill.level > 0,
                "at the kill: varb4117_prayer_protectfrommissiles " .. tostring(on_kill) .. ", prayer points "
                    .. tostring(type(pts_kill) == "table" and pts_kill.level or pts_kill) .. " (want 1 and > 0)")
            t.check("killDayth.prayerOff", on == 0, "varb4117_prayer_protectfrommissiles " .. tostring(on)
                .. "; prayer points left " .. tostring(type(pts) == "table" and pts.level or pts))
        end
        -- the key from beside it: from the fighting tile the press landed on a crate (run 8)
        t.exec("walk-pickUpKey", t.player.walk_to, 2790, 4454, 12)
        local pkr, pkd = t.player.press("hauntedmine_boss_key", 1, 8)
        t.note("pickUpKey press: " .. tostring(pkr) .. " " .. tostring(pkd))
        t.exec("pickUpKey", t.inv.await, "hauntedmine_reward_key", 1, 8)
        t.exec("goUpFromDayth.bossDoorOut", t.player.pass_door, BOSS_OUT)
        t.drive.camera(1024, 383, 300)
        climb("goUpFromDayth", "hauntedmine_light_stairs_bottom", "Walk-up", { 2812, 4452 }, { 2746, 4439 },
            "hauntedmine_dayth.rs2:56 p_teleport(^hmq_dark_dayth_exit)")

        -- ---------------------------------------------------------------
        -- West along the flooded corridor to the crystal stair, the reward door, the outcrop.
        -- ---------------------------------------------------------------
        local WEST = { { 2740, 4437 }, { 2730, 4437 }, { 2720, 4437 }, { 2710, 4437 }, { 2700, 4439 }, { 2693, 4440 } }
        local EAST = { { 2700, 4439 }, { 2710, 4437 }, { 2720, 4437 }, { 2730, 4437 }, { 2740, 4437 }, { 2747, 4440 } }
        t.exec("walk-flooded-west", t.player.walk_route, WEST)
        climb("goDownToCrystals", "hauntedmine_dark_stairs_top", "Walk-down", { 2692, 4436 }, { 2758, 4453 },
            "hauntedmine_dayth.rs2:37 p_teleport(^hmq_crystalentrance_enter)")
        local REWARD_IN = { closed = "hauntedmine_rewarddoor_l", open = "hauntedmine_rewarddoor_l", at = { 2773, 4450, 0 },
            near = { 2773, 4450 }, far = { 2773, 4448 } }
        local REWARD_OUT = { closed = "hauntedmine_rewarddoor_l", open = "hauntedmine_rewarddoor_l", at = { 2773, 4450, 0 },
            near = { 2773, 4448 }, far = { 2773, 4451 } }
        t.exec("openRewardDoor", t.player.pass_door, REWARD_IN)
        local _, xpsnap = t.skill.snapshot()
        local _, shards_before = t.inv.count("crystalshard_necklace_unstrung")
        click("cutCrystal", "crystalcorner", 2787, 4428)
        t.exec("stage-complete", t.var.await, "varp382_hauntedmine", 11, 10)
        t.quest.expect_complete()
        -- crystal_outcrop: hauntedmine_dayth.rs2:296-300 -- one salve shard, 22,000 Strength XP, 2 quest points
        t.check("reward.strength_xp", t.skill.expect_gain("strength", 22000, xpsnap))
        local _, shards_after = t.inv.count("crystalshard_necklace_unstrung")
        t.check("reward.salve_shard", shards_before == 0 and shards_after == 1,
            "crystalshard_necklace_unstrung " .. tostring(shards_before) .. " -> " .. tostring(shards_after) .. " (want 0 -> 1)")

        -- ---------------------------------------------------------------
        -- The dark rooms: down each stair without a lit fungus, and back up.
        -- ---------------------------------------------------------------
        local dr, dd = t.player.drop("glowing_fungus")
        t.check("drop-fungus", tostring(dd):find("crumbles to ashes", 1, true) ~= nil, tostring(dr) .. " " .. tostring(dd))
        t.exec("leaveCrystalRoom.rewardDoorOut", t.player.pass_door, REWARD_OUT)
        climb("leaveCrystalRoom", "hauntedmine_light_stairs_bottom", "Walk-up", { 2755, 4452 }, { 2692, 4439 },
            "hauntedmine_dayth.rs2:58 p_teleport(^hmq_dark_crystal_exit)")
        climb("dark-crystal", "hauntedmine_dark_stairs_top", "Walk-down", { 2692, 4436 }, { 2711, 4591 },
            "hauntedmine_dayth.rs2:29 p_teleport(^hmq_dark_crystal_enter)")
        climb("leaveDarkCrystalRoom", "hauntedmine_dark_stairs_bottom", "Walk-up", { 2709, 4591 }, { 2692, 4439 },
            "hauntedmine_dayth.rs2:45 p_teleport(^hmq_dark_crystal_exit)")
        t.exec("walk-flooded-east2", t.player.walk_route, EAST)
        climb("dark-dayth", "hauntedmine_dark_stairs_top", "Walk-down", { 2746, 4436 }, { 2730, 4562 },
            "hauntedmine_dayth.rs2:20 p_teleport(^hmq_dark_dayth_enter)")
        climb("leaveDarkDaythRoom", "hauntedmine_dark_stairs_bottom", "Walk-up", { 2731, 4561 }, { 2746, 4439 },
            "hauntedmine_dayth.rs2:48 p_teleport(^hmq_dark_dayth_exit)")
        t.finish(0)
    end,
}
