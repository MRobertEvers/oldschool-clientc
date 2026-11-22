-- Haunted Mine (quest_hauntedmine), rebuilt 2026-11-22 from RSProx index 4647 (the owner's own
-- playthrough; docs/rsprox-analysis/quests/haunted_mine.md). It follows the recorded order and
-- walks the recorded failures on purpose, each its own row (`fail.*`):
--   * the Zealot's pocket before he has told of the key, and the Zamorak answer that ends the talk;
--   * the crystal hall's front doors (locked to everyone the recording shows);
--   * the lift before the valve, the locked valve, the valve race LOST (the cheeky ghost shuts it
--     28 ticks after it comes, 49 after the valve) and the dead lift after it;
--   * the flooded stair without a fungus (the dark room, its page, stage 6);
--   * a dropped fungus, a cart run that sinks (the fungus lost), one that comes back (lever a down),
--     a fungus carried out into the sunlight;
--   * the crystal doors without the key, a second key refused.
-- Every climb lands on the recorded tile from the recorded approach tile (~hmq_climb's fixed
-- offsets); the lifts are pressed from inside the lift, where the recorder stood.
--
-- WALLS (door rule, owner 2026-10-03): every door, gate, trapdoor, ladder, crawl and lift between
-- the player and a target is pressed on every visit. Morytania is entered the way Priest in Peril
-- opens it (the Paterdomus trapdoor, the two gates, Drezel's blessing, the holy barrier).
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
        -- The flooded chamber soaks it ([3215]): the soak is a row.
        "::give tinderbox 1",
    },
    run = function(t)
        t.ticklog.start()
        t.quest.bind({
            varp = "varp382_hauntedmine",
            constants = { not_started = 0, started = 1, entered = 3, valve_found = 4, lift_taken = 5, dark = 6,
                dayth_room = 7, ambushed = 8, dayth_killed = 9, crystals_reached = 10, complete = 11 },
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
            "npc:Into Morytania? Yes, you can.",
            "npc:I must warn you though, Morytania is an evil land.",
            "npc:Worst of all are the vampyres",
            "player:I'll be careful. Anything else I should know?",
            "npc:The first settlement you'll likely come across is Canifis",
            "npc:You should keep that dagger I gave you to hand.",
            "player:I see. Thanks.",
            "npc:One last thing.",
            "player:The Myreque?",
            "npc:Yes. They're a band of freedom fighters.",
            "npc:Veliaf, the leader of their Mort Myre group",
            "player:I'll keep an eye out for them.",
            "npc:Right, I think that's everything.",
            "mesbox:Drezel blesses you",
            "npc:Good luck out there. Stay safe.",
            "choose:And you. See you later.",
            "player:And you. See you later.",
        })
        t.exec("enterMorytania.drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
        t.exec("enterMorytania.holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
            at = { 3440, 9886, 0 }, near = { 3440, 9887 },
            far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
            far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })

        -- ---------------------------------------------------------------
        -- The Zealot (RSProx index 4647 [531..741]). Failures first: the pocket before he has told of
        -- the key, and the Zamorak answer that ends the talk; then the start.
        -- ---------------------------------------------------------------
        t.exec("goto-zealot", t.player.goto_tile, 3443, 3258, 0)
        t.exec("fail.pickpocketTooSoon", t.player.press, "saradominist_zealot", 3, 8)
        t.exec("fail.pickpocketTooSoon.line", t.msg.expect, "I doubt he's got much of value on him.")
        t.exec("fail.zamorak", t.player.talk_to, "saradominist_zealot", 1)
        t.exec("fail.zamorak-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:State thy allegiance stranger.",
            "choose:I serve only Zamorak.",
            "player:I serve only Zamorak. Strength through chaos.",
            "npc:Then begone.",
            "choose:Fine, I didn't want to talk to you either.",
            "player:Fine, I didn't want",
            "npc:Fear not fiend",
        })
        t.ticks(2)
        t.expect("fail.zamorak.notStarted", t.quest.expect_stage("not_started"))
        t.exec("talkToZealot", t.player.talk_to, "saradominist_zealot", 1)
        t.exec("talkToZealot-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:State thy allegiance stranger.",
            "choose:I follow the path of Saradomin.",
            "player:I follow the path of Saradomin. Strength through",
            "npc:Ah, a wise choice my friend.",
            "choose:I could ask the same of you.",
            "player:I could ask the same of you.",
            "npc:Saradomin has granted you wisdom.",
            "npc:I am upon a quest.",
            "choose:Yes.",
            "player:What quest is that then?",
            "npc:Well, below the very ground",
            "npc:As with many things",
            "npc:The mines are also the site",
            "npc:These crystals are thought",
            "npc:It is not known what happened",
            "npc:Saradomin has entrusted me alone",
            "player:How can an abandoned mine be dangerous?",
            "npc:Because the mines are said to be haunted.",
            "npc:Dayth failed and fell",
            "npc:Zamorak, furious at his mage",
            "npc:Further, Zamorak's then ruler",
            "choose:Is there any other way into the mines?",
            "player:Is there any other way into the mines?",
            "npc:Indeed I have found hope",
            "npc:Further, I have come into possession of a key",
            "choose:Can I borrow your key?",
            "player:Can I borrow your key?",
            "npc:I am afraid my task can be entrusted",
            "choose:Please? I'll be very careful with it.",
            "player:Please? I'll be very careful with it.",
            "npc:You know not what you deal with.",
            "choose:I'll trade for it.",
            "player:I'll trade for it.",
            "npc:Saradomin provides for my every need.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        t.exec("startedLine", t.msg.expect, "You've started a new quest:")
        t.exec("heardaboutkey", t.var.await_server, "varb2397_hauntedmine_heardaboutkey", 1, 4)
        t.exec("pickpocketZealot", t.player.press, "saradominist_zealot", 3, 8)
        t.exec("pickpocketZealot.line", t.msg.expect, "You pick the zealot's pocket and retrieve a small silvery key.")
        t.exec("zealotkey", t.inv.await, "hauntedmine_lift_key", 1, 6)
        t.exec("wield", t.player.equip, "magic_shortbow")
        t.exec("wield-arrows", t.player.equip, "rune_arrow")

        -- Every climb in the mine lands on the player's own tile shifted by the ladder's fixed offset
        -- (hauntedmine_dungeon.rs2 ~hmq_climb, 62 of 62 recorded): `src` is the recorded approach tile.
        local function climb(name, loc, op_name, at, src, dest, why, extra)
            local spec = { loc = loc, op = 1, op_name = op_name, at = { at[1], at[2], 0 }, src = src,
                dest = { dest[1], dest[2], 0 }, same_level = why }
            for k, v in pairs(extra or {}) do spec[k] = v end
            t.exec(name, t.player.climb, spec)
        end
        -- The recorder's way off the lift's shore ([2713..2729]).
        local SHORE_WEST = { { 2724, 4450 }, { 2722, 4448 }, { 2721, 4444 }, { 2721, 4441 }, { 2720, 4438 },
            { 2710, 4437 }, { 2700, 4439 }, { 2691, 4438 } }
        local SHIFT = "hauntedmine_dungeon.rs2 ~hmq_climb (the ladder's fixed offset, RSProx index 4647)"
        local STAIR = "hauntedmine_dayth.rs2 [oploc1,..stairs..] p_teleport (RSProx index 4647)"
        local LIFT = "hauntedmine_dungeon.rs2 @hmq_lift_down / [oploc1,lift_flooded] (RSProx index 4647 [2703], [3258])"
        -- A lift: pressed from wherever the player stands (the op walks them in, the recorder's own
        -- way round takes ~20 ticks, [2687..2702]); graded on the landing tile.
        local INTO_LIFT = { { 2802, 4503 }, { 2802, 4498 }, { 2804, 4494 }, { 2805, 4493 }, { 2807, 4493 } }
        local function into_lift(name)
            local wr, wt = t.world.tile()
            if wr == "ok" and type(wt) == "table" and wt.x == 2807 and wt.z == 4493 then return end
            t.exec(name .. ".intoLift", t.player.walk_route, INTO_LIFT)
        end
        local function ride(name, sym, at, dest, ticks)
            if sym ~= "lift_flooded" then into_lift(name) end
            local r, d = t.player.click_loc(sym, 1, { at = at })
            local got = t.await({
                level = function()
                    local wr, tt = t.world.tile()
                    return wr == "ok" and type(tt) == "table" and tt.x == dest[1] and tt.z == dest[2]
                end,
                note = name .. ": the landing",
            }, ticks or 60)
            local wr, tt = t.world.tile()
            t.check(name, got == "ok", "click_loc " .. sym .. " -> " .. tostring(r) .. " " .. tostring(d)
                .. "; at " .. tostring(type(tt) == "table" and (tt.x .. "," .. tt.z) or tt)
                .. " (want " .. dest[1] .. "," .. dest[2] .. ")")
        end
        local function click(name, sym, x, z, op)
            t.exec(name, t.player.click_loc, sym, op or 1, { at = { x, z } })
            t.ticks(3)
        end

        -- ---------------------------------------------------------------
        -- The front entrance: the crystal hall's doors are locked ([505..518]).
        -- ---------------------------------------------------------------
        climb("frontEntrance", "hauntedmine_main_entrance", "Enter", { 3452, 3243 }, { 3454, 3242 }, { 2792, 4592 },
            "hauntedmine_dungeon.rs2 [oploc1,hauntedmine_main_entrance] p_teleport (RSProx index 4647 [505])")
        click("fail.frontDoorLocked", "hauntedmine_rewarddoor_l", 2789, 4593)
        t.exec("fail.frontDoorLocked.line", t.msg.expect, "This door appears to be locked.")
        climb("frontEntranceOut", "hauntedmine_main_entrance_inside", "Exit", { 2790, 4589 }, { 2792, 4592 }, { 3454, 3242 },
            "hauntedmine_dungeon.rs2 [oploc1,hauntedmine_main_entrance_inside] p_teleport ([518])")

        -- ---------------------------------------------------------------
        -- In by the cart and the thin tunnel (stage 3), down to the lift room.
        -- ---------------------------------------------------------------
        t.exec("goto-cart", t.player.walk_to, 3446, 3236, 40)
        local ccr, ccd = t.player.click_loc("hauntedmine_obstacle_cart", 1, { at = { 3445, 3236 } })
        t.note("climbCart: click_loc -> " .. tostring(ccr) .. " " .. tostring(ccd))
        t.ticks(5)
        do
            local _, tile = t.world.tile()
            t.check("climbCart.landed", type(tile) == "table" and tile.x == 3444 and tile.z == 3236,
                "after the obstacle cart: " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile)
                    .. " (want 3444,3236: the recorded exactmove, [851])")
        end
        climb("enterMine", "hauntedmine_secondary_entrance", "Crawl-down", { 3440, 3232 }, { 3441, 3232 }, { 3436, 9637 })
        t.expect("quest.stage.entered", t.var.await_server("varp382_hauntedmine", 3, 4))
        climb("downToLevel2North", "hauntedmine_laddertop", "Climb-down", { 3413, 9633 }, { 3413, 9634 }, { 2773, 4578 })
        climb("downToLevel3North", "hauntedmine_laddertop_1sw", "Climb-down", { 2797, 4599 }, { 2797, 4598 },
            { 2733, 4534 }, SHIFT)
        climb("downToLiftRoom", "hauntedmine_laddertop_1e", "Climb-down", { 2732, 4529 }, { 2732, 4528 },
            { 2796, 4528 }, SHIFT)

        -- ---------------------------------------------------------------
        -- The lift room: the unpowered lift, the locked valve (stage 4), the chisel, the lost race.
        -- ---------------------------------------------------------------
        t.exec("walk-lift", t.player.walk_route, { { 2797, 4529 }, { 2802, 4526 }, { 2804, 4520 }, { 2802, 4514 },
            { 2803, 4509 } })
        click("fail.valveLocked", "hauntedmine_lift_valve", 2808, 4496)
        t.exec("fail.valveLocked.line", t.msg.expect, "The valve seems to be locked in position.")
        t.expect("quest.stage.valveFound", t.var.await_server("varp382_hauntedmine", 4, 4))
        t.exec("walk-chisel", t.player.walk_to, 2801, 4500, 80)
        t.exec("pickUpChisel", t.player.click_obj, "chisel", 3)
        t.exec("chisel", t.inv.await, "chisel", 1, 6)
        into_lift("fail.liftUnpowered")
        t.player.click_loc("lift_side_l", 1, { at = { 2807, 4494 } })
        t.ticks(3)
        t.exec("fail.liftUnpowered-dialog", t.chat.play, { "mesbox:The lift doesn't respond, it needs to be powered" })
        t.player.walk_to(2807, 4496, 80)
        local valve = t.player.by_symbol("loc", "hauntedmine_lift_valve")
        t.exec("useKeyOnValve", t.player.use_on, "hauntedmine_lift_key", valve)
        t.exec("useKeyOnValve.line", t.msg.expect, "The key unlocks the valve.")
        t.exec("openValve.line", t.msg.expect, "You open the valve. Water begins to flow")
        t.exec("openValve.var", t.var.await_server, "varb2394_hauntedmine_liftpowerednow", 1, 4)
        do
            local kr, keys = t.inv.count("hauntedmine_lift_key")
            t.check("useKeyOnValve.keyKept", kr == "ok" and keys == 1, "hauntedmine_lift_key " .. tostring(keys) .. " (want 1)")
        end
        -- The race lost on purpose: the cheeky ghost comes at +49, reaches the valve at +69, shuts it at +77.
        t.exec("fail.raceLost.ghost", t.npc.await_present, "hauntedmine_cheeky_ghost", 20, 60)
        t.exec("fail.raceLost.shut", t.var.await_server, "varb2394_hauntedmine_liftpowerednow", 0, 40)
        t.exec("fail.raceLost.line", t.msg.expect, "You hear the sound of a valve being turned.")
        t.exec("fail.raceLost.toLift", t.player.walk_route, { { 2803, 4500 }, { 2802, 4503 } })
        into_lift("fail.raceLost")
        t.player.click_loc("lift_side_l", 1, { at = { 2807, 4494 } })
        t.ticks(3)
        t.exec("fail.raceLost.liftDead-dialog", t.chat.play, { "mesbox:The lift doesn't respond" })
        click("reopenValve", "hauntedmine_lift_valve", 2808, 4496)
        t.exec("reopenValve.line", t.msg.expect, "You open the valve. Water begins to flow")
        ride("goDownLift", "lift_side_r", { 2807, 4492 }, { 2725, 4452 })
        t.exec("goDownLift.line", t.msg.expect, "You get in the lift. Now powered, the lift descends")
        t.expect("quest.stage.liftTaken", t.var.await_server("varp382_hauntedmine", 5, 4))
        t.exec("goDownLift.plunge", t.msg.expect, "plunging you straight into the middle of a chamber flooded")
        t.exec("goDownLift.soak", t.msg.expect, "The water soaks your tinderbox!")
        t.exec("goDownLift.damp", t.inv.await, "damp_tinderbox", 1, 4)

        -- ---------------------------------------------------------------
        -- The flooded level without a light: the west stair's dark room (stage 6) and back.
        -- ---------------------------------------------------------------
        t.exec("walk-flooded-west", t.player.walk_route, SHORE_WEST)
        climb("fail.darkRoom", "hauntedmine_dark_stairs_top", "Walk-down", { 2692, 4436 }, { 2691, 4438 },
            { 2712, 4593 }, STAIR, { chat = { "player:It's kind of dark down here. I should probably find" } })
        t.expect("quest.stage.dark", t.var.await_server("varp382_hauntedmine", 6, 4))
        climb("fail.darkRoom.out", "hauntedmine_dark_stairs_bottom", "Walk-up", { 2709, 4591 }, { 2712, 4593 },
            { 2691, 4437 }, STAIR)
        t.exec("walk-flooded-lift", t.player.walk_route, { { 2700, 4439 }, { 2710, 4437 }, { 2720, 4438 },
            { 2721, 4441 }, { 2721, 4444 }, { 2722, 4448 }, { 2724, 4452 } })
        ride("liftUp", "lift_flooded", { 2726, 4456 }, { 2807, 4493 })

        -- ---------------------------------------------------------------
        -- Out and round by the south tunnel to the cart room.
        -- ---------------------------------------------------------------
        t.exec("walk-liftRoomLadder", t.player.walk_route, { { 2805, 4500 }, { 2803, 4509 }, { 2802, 4514 },
            { 2804, 4520 }, { 2802, 4526 }, { 2796, 4528 } })
        climb("upFromLiftRoom", "hauntedmine_ladder_1w", "Climb-up", { 2796, 4529 }, { 2796, 4528 }, { 2732, 4528 }, SHIFT)
        t.exec("walk-level3North", t.player.walk_to, 2733, 4534, 30)
        climb("upToLevel2North", "hauntedmine_ladder_1ne", "Climb-up", { 2733, 4535 }, { 2733, 4534 }, { 2797, 4598 }, SHIFT)
        t.exec("walk-level2North", t.player.walk_to, 2774, 4577, 60)
        climb("upToTunnel", "hauntedmine_ladder", "Climb-up", { 2773, 4577 }, { 2774, 4577 }, { 3414, 9633 })
        t.exec("walk-tunnelNorth", t.player.walk_to, 3405, 9631, 30)
        climb("outNorth", "hauntedmine_back_entrance1_inside", "Crawl-through", { 3404, 9631 }, { 3405, 9631 }, { 3429, 3233 })
        t.exec("walk-backEntrance2", t.player.walk_to, 3428, 3225, 20)
        climb("inSouth", "hauntedmine_back_entrance2", "Crawl-down", { 3429, 3225 }, { 3428, 3225 }, { 3409, 9623 })
        t.exec("walk-tunnelSouth", t.player.walk_to, 3422, 9624, 30)
        climb("downToLevel2South", "hauntedmine_laddertop", "Climb-down", { 3422, 9625 }, { 3422, 9624 }, { 2782, 4568 })
        t.exec("walk-level2South", t.player.walk_to, 2798, 4568, 40)
        climb("downToLevel3South", "hauntedmine_laddertop_1sw", "Climb-down", { 2798, 4567 }, { 2798, 4568 },
            { 2734, 4504 }, SHIFT)
        t.exec("walk-level3South", t.player.walk_to, 2725, 4487, 60)
        climb("downToCartRoom", "hauntedmine_laddertop_1e", "Climb-down", { 2725, 4486 }, { 2725, 4487 },
            { 2789, 4487 }, SHIFT)

        -- ---------------------------------------------------------------
        -- The cart puzzle: a dropped fungus crumbles; a run that sinks takes the fungus; one with
        -- lever a down comes back; the solution sends it through ([1820..2275], [3553], [3758]).
        -- ---------------------------------------------------------------
        local LEVERS = {
            a = { sym = "hauntedmine_point_lever2", x = 2784, z = 4517, var = "varb2386_hauntedmine_lever_a" },
            b = { sym = "hauntedmine_point_lever1", x = 2785, z = 4517, var = "varb2385_hauntedmine_lever_b" },
            c = { sym = "hauntedmine_point_lever3", x = 2786, z = 4517, var = "varb2387_hauntedmine_lever_c" },
            d = { sym = "hauntedmine_point_lever4", x = 2786, z = 4515, var = "varb2388_hauntedmine_lever_d" },
            e = { sym = "hauntedmine_point_lever5", x = 2785, z = 4515, var = "varb2389_hauntedmine_lever_e" },
            i = { sym = "hauntedmine_point_lever6", x = 2768, z = 4533, var = "varb2390_hauntedmine_lever_i" },
            j = { sym = "hauntedmine_point_lever7", x = 2769, z = 4533, var = "varb2391_hauntedmine_lever_j" },
            k = { sym = "hauntedmine_point_lever8", x = 2770, z = 4533, var = "varb2392_hauntedmine_lever_k" },
        }
        local ORDER = { "a", "b", "c", "d", "e", "i", "j", "k" }
        -- Pull every lever whose bit differs from `want` ("ABcdEIjk": upper case = 1).
        local function set_levers(name, want)
            local pulled = 0
            t.player.walk_to(2785, 4516, 120)
            for n, key in ipairs(ORDER) do
                local ch = want:sub(n, n)
                local target = (ch == ch:upper()) and 1 or 0
                local lv = LEVERS[key]
                if key == "i" then
                    t.player.walk_to(2769, 4532, 120)
                end
                local _, now = t.var.server(lv.var)
                if now ~= target then
                    click(name .. ".lever_" .. key, lv.sym, lv.x, lv.z)
                    t.exec(name .. ".lever_" .. key .. ".var", t.var.await_server, lv.var, target, 6)
                    pulled = pulled + 1
                end
            end
            t.note(name .. ": " .. pulled .. " lever(s) pulled to " .. want)
        end
        local function start_run(name)
            click(name .. ".panel", "hauntedmine_points_info", 2769, 4522)
            t.ticks(2)
            local wr, w = t.ui.widget("hauntedmine_controls:start")
            t.check(name .. ".startButton", wr == "ok", "hauntedmine_controls:start -> " .. tostring(wr))
            t.ui.invoke(w, 1)
        end
        local POOLS_TO_CART = { { 2793, 4496 }, { 2791, 4502 }, { 2786, 4505 }, { 2779, 4506 } }
        local CART_TO_PANEL = { { 2778, 4513 }, { 2777, 4520 }, { 2770, 4521 } }
        local PANEL_TO_POOLS = { { 2777, 4520 }, { 2778, 4513 }, { 2779, 4506 }, { 2786, 4505 }, { 2791, 4502 },
            { 2793, 4496 } }
        local function fungus(name)
            click(name, "glowing_mushroom2", 2793, 4493)
            t.exec(name .. ".held", t.inv.await, "glowing_fungus", 1, 6)
        end
        local cart = t.player.by_symbol("loc", "hauntedmine_puzzle_cart")
        local function to_cart(name)
            t.exec(name .. ".walk", t.player.walk_route, POOLS_TO_CART)
            t.exec(name, t.player.use_on, "glowing_fungus", cart)
            t.exec(name .. ".line", t.msg.expect, "You place the glowing fungus in the mine cart.")
            t.exec(name .. ".var", t.var.await_server, "varb2395_hauntedmine_begincart_fungus", 1, 4)
            t.exec(name .. ".toPanel", t.player.walk_route, CART_TO_PANEL)
        end
        t.exec("walk-pools", t.player.walk_to, 2793, 4490, 20)
        fungus("pickFungus")
        t.exec("pickFungus.line", t.msg.expect, "You pull the fungus from the water. It is very cold to the touch.")
        local dr, dd = t.player.drop("glowing_fungus")
        t.note("drop glowing_fungus -> " .. tostring(dr) .. " " .. tostring(dd))
        t.exec("fail.dropFungus.line", t.msg.expect, "When you drop the fungus it crumbles mysteriously to dust.")
        t.exec("fail.dropFungus.gone", t.inv.await, "glowing_fungus", 0, 4)
        fungus("pickFungus2")
        to_cart("fungusInCart")
        -- The first look at the panel starts the puzzle and rolls the levers.
        click("firstLook", "hauntedmine_points_info", 2769, 4522)
        t.exec("firstLook.started", t.var.await_server, "varb2384_hauntedmine_pointspuzzlestarted", 1, 4)
        t.key("escape")
        t.ticks(2)
        set_levers("sink", "ABcdeiJk")
        start_run("fail.sink")
        t.exec("fail.sink.line", t.msg.await, "Oh dear, the mine cart seems to have sunk.", 16)
        t.exec("fail.sink.fungusLost", t.var.await_server, "varb2395_hauntedmine_begincart_fungus", 0, 4)
        t.key("escape")
        t.ticks(2)
        set_levers("returned", "aBcdeiJk")
        start_run("fail.returned")
        t.exec("fail.returned.line", t.msg.await, "How useful, it's come right back to where it started.", 16)
        t.key("escape")
        t.ticks(2)
        t.exec("walk-pools2", t.player.walk_route, PANEL_TO_POOLS)
        fungus("pickFungus3")
        to_cart("fungusInCart2")
        set_levers("solve", "ABcdEIjk")
        start_run("success")
        t.ticks(12)
        t.exec("success-dialog", t.chat.play, { "player:I wonder if that cart is anywhere useful now?" })
        t.exec("success.endcart", t.var.await_server, "varb2396_hauntedmine_endcart_fungus", 1, 4)
        t.key("escape")
        t.ticks(2)
        t.exec("walk-pools3", t.player.walk_route, PANEL_TO_POOLS)
        -- A fungus carried out into the sunlight crumbles ([2019]).
        fungus("pickFungus4")
        t.exec("walk-cartRoomLadder", t.player.walk_route, { { 2793, 4490 }, { 2789, 4487 } })
        climb("upFromCartRoom", "hauntedmine_ladder_1w", "Climb-up", { 2789, 4486 }, { 2789, 4487 }, { 2725, 4487 }, SHIFT)
        t.player.walk_to(2735, 4503, 60)
        climb("upToLevel2South", "hauntedmine_ladder_1ne", "Climb-up", { 2734, 4503 }, { 2735, 4503 }, { 2799, 4567 }, SHIFT)
        t.exec("walk-level2South-up", t.player.walk_to, 2783, 4569, 40)
        climb("upToTunnelSouth", "hauntedmine_ladder", "Climb-up", { 2782, 4569 }, { 2783, 4569 }, { 3423, 9625 })
        t.exec("walk-tunnelSouth-out", t.player.walk_to, 3409, 9623, 30)
        climb("fail.sunlight", "lotr_back_entrance1_inside", "Crawl-through", { 3408, 9623 }, { 3409, 9623 }, { 3428, 3225 },
            nil, { chat = { "mesbox:As you crawl out into the sunlight, the strange fungus" } })
        t.exec("fail.sunlight.ashes", t.inv.await, "glowing_fungus", 0, 4)

        -- ---------------------------------------------------------------
        -- In by the north tunnel to the far cart; the fungus; down the lift with it.
        -- ---------------------------------------------------------------
        t.exec("walk-backEntrance1", t.player.walk_to, 3429, 3233, 20)
        climb("inNorth", "hauntedmine_back_entrance1", "Crawl-down", { 3430, 3233 }, { 3429, 3233 }, { 3405, 9631 })
        t.exec("walk-tunnelNorth2", t.player.walk_to, 3413, 9634, 30)
        climb("downToLevel2North2", "hauntedmine_laddertop", "Climb-down", { 3413, 9633 }, { 3413, 9634 }, { 2773, 4578 })
        t.exec("walk-level2North2", t.player.walk_to, 2797, 4598, 60)
        climb("downToLevel3North2", "hauntedmine_laddertop_1sw", "Climb-down", { 2797, 4599 }, { 2797, 4598 },
            { 2733, 4534 }, SHIFT)
        t.exec("walk-collectLadder", t.player.walk_route, { { 2727, 4533 }, { 2719, 4533 }, { 2712, 4532 }, { 2710, 4539 } })
        climb("downToCollectRoom", "hauntedmine_laddertop_1e", "Climb-down", { 2710, 4540 }, { 2710, 4539 },
            { 2774, 4539 }, SHIFT)
        -- The crumbled fungi are ashes in the pack; a full pack would get the fungus at its feet ([2547]).
        while true do
            local ar, an = t.inv.count("ashes")
            if ar ~= "ok" or an < 1 then break end
            t.player.drop("ashes")
            t.ticks(2)
        end
        click("collectFungus", "hauntedmine_puzzle_cart", 2774, 4537)
        t.exec("collectFungus-dialog", t.chat.play, { "choose:Take it." })
        t.exec("collectFungus.held", t.inv.await, "glowing_fungus", 1, 6)
        t.exec("collectFungus.var", t.var.await_server, "varb2396_hauntedmine_endcart_fungus", 0, 4)
        click("collectFungus.empty", "hauntedmine_puzzle_cart", 2774, 4537)
        t.exec("collectFungus.empty.line", t.msg.expect, "The cart is empty.")
        climb("upFromCollectRoom", "hauntedmine_ladder_1w", "Climb-up", { 2774, 4540 }, { 2774, 4539 }, { 2710, 4539 }, SHIFT)
        t.exec("walk-liftLadder2", t.player.walk_route, { { 2712, 4532 }, { 2719, 4533 }, { 2727, 4533 }, { 2734, 4534 },
            { 2739, 4531 }, { 2736, 4526 }, { 2732, 4528 } })
        climb("downToLiftRoom2", "hauntedmine_laddertop_1e", "Climb-down", { 2732, 4529 }, { 2732, 4528 },
            { 2796, 4528 }, SHIFT)
        t.exec("walk-lift2", t.player.walk_route, { { 2797, 4529 }, { 2802, 4526 }, { 2804, 4520 }, { 2802, 4514 },
            { 2803, 4509 } })
        -- The second race's ghost shut the valve after the ride down (as at [2708]); from stage 5 the
        -- valve stays open once turned, with no ghost ([3177], [3291], [5283]).
        t.player.walk_to(2807, 4496, 80)
        click("turnValveAgain", "hauntedmine_lift_valve", 2808, 4496)
        t.exec("turnValveAgain.line", t.msg.expect, "You open the valve. Water begins to flow")
        t.exec("walk-afterValve", t.player.walk_route, { { 2803, 4500 }, { 2802, 4503 } })
        ride("goDownLift2", "lift_side_l", { 2807, 4494 }, { 2725, 4452 })
        t.exec("goDownLift2.line", t.msg.expect, "The lift descends further into the mines...")

        -- ---------------------------------------------------------------
        -- With the fungus: the crystal hall (locked without the key), then Dayth's room (stage 7).
        -- ---------------------------------------------------------------
        t.exec("walk-flooded-west2", t.player.walk_route, SHORE_WEST)
        climb("downToCrystalHall", "hauntedmine_dark_stairs_top", "Walk-down", { 2692, 4436 }, { 2691, 4438 },
            { 2758, 4454 }, STAIR)
        t.player.walk_to(2772, 4451, 40)
        click("fail.crystalDoorLocked", "hauntedmine_rewarddoor_r", 2772, 4450)
        t.exec("fail.crystalDoorLocked.line", t.msg.expect, "This door appears to be locked.")
        climb("upFromCrystalHall", "hauntedmine_light_stairs_bottom", "Walk-up", { 2755, 4452 }, { 2758, 4453 },
            { 2691, 4437 }, STAIR)
        t.exec("walk-flooded-east", t.player.walk_route, { { 2700, 4439 }, { 2710, 4437 }, { 2720, 4437 },
            { 2730, 4437 }, { 2740, 4437 }, { 2750, 4436 } })
        climb("downToDayth", "hauntedmine_dark_stairs_top", "Walk-down", { 2746, 4436 }, { 2750, 4436 },
            { 2811, 4454 }, STAIR)
        t.expect("quest.stage.daythRoom", t.var.await_server("varp382_hauntedmine", 7, 4))
        t.exec("walk-bossDoor", t.player.walk_to, 2799, 4453, 20)
        click("bossDoorIn", "hauntedmine_boss_door", 2799, 4453)
        do
            local _, tile = t.world.tile()
            t.check("bossDoorIn.through", type(tile) == "table" and tile.x <= 2798,
                "after the boss door: " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile)
                    .. " (want inside, x <= 2798: the walk-through, [5354])")
        end

        -- ---------------------------------------------------------------
        -- The key and Treus Dayth (the fight: hauntedmine_dayth_fight.rs2).
        -- ---------------------------------------------------------------
        do
            local tab_result = t.ui.tab("prayer")
            t.ticks(2)
            local wr, w = t.ui.widget("prayerbook:prayer14")
            t.ui.invoke(w, 1)
            t.ticks(2)
            local _, on = t.var.varbit("varb4117_prayer_protectfrommissiles")
            t.check("dayth.protectMissiles", tab_result == "ok" and wr == "ok" and on == 1,
                "varb4117_prayer_protectfrommissiles " .. tostring(on))
            t.ui.tab("inventory")
        end
        t.exec("walk-key", t.player.walk_to, 2789, 4454, 12)
        t.exec("takeKey.ambush", t.player.press, "hauntedmine_boss_key", 1, 8)
        t.ticks(5)
        t.exec("takeKey.ambush-dialog", t.chat.play, {
            "player:What the... ?",
            "mesbox:You hear a disembodied voice laughing at you",
        })
        t.expect("quest.stage.ambushed", t.var.await_server("varp382_hauntedmine", 8, 6))
        -- He rises over T..T+13 ([5543..5556]) and fades on two hits in three, gliding to another
        -- spot (haunted_mine_dayth.md): press Attack whenever the visible form is up, else wait.
        local EAT = { eat = { item = "shark", below = 70 } }
        -- The server's reading of this player's Dayth (::hmq_dayth_state, hauntedmine_dayth_fight.rs2):
        -- nil when none, else { form = 1 Treus Dayth | 2 the ghost, hp }.
        local function dayth()
            t.cheat("::hmq_dayth_state")
            t.ticks(2)
            local msgs = t.raid.cox_messages(6)
            for i = 1, #msgs do
                local f, hp = string.match(msgs[i], "hmq_dayth_state: (%-?%d+) (%-?%d+)")
                if f then
                    if tonumber(f) == 0 then return nil end
                    return { form = tonumber(f), hp = tonumber(hp) }
                end
            end
            return nil
        end
        t.ticks(14)
        local presses, waits, low = 0, 0, 99
        for _ = 1, 150 do
            local _, hp = t.skill.read("hitpoints")
            if type(hp) == "table" then
                low = math.min(low, hp.level or 99)
                if (hp.level or 99) < 70 then
                    t.player.eat("shark")
                    t.ticks(1)
                end
            end
            local d = dayth()
            if d == nil then break end
            if d.form == 1 then
                t.player.attack("hauntedmine_boss_ghost", 2, 6, EAT)
                presses = presses + 1
            else
                t.ticks(2)
                waits = waits + 1
            end
        end
        t.note("Dayth: " .. presses .. " attack press(es), " .. waits .. " wait(s) while he was a ghost; lowest hp " .. low)
        t.exec("killDayth-dialog", t.chat.play, { "mesbox:As you make the killing strike, the machinery" })
        t.expect("quest.stage.daythKilled", t.var.await_server("varp382_hauntedmine", 9, 6))
        do
            t.ui.tab("prayer")
            t.ticks(2)
            local _, w = t.ui.widget("prayerbook:prayer14")
            t.ui.invoke(w, 1)
            t.ticks(2)
            t.ui.tab("inventory")
        end
        t.exec("walk-key2", t.player.walk_to, 2789, 4454, 12)
        t.exec("takeKey", t.player.press, "hauntedmine_boss_key", 1, 8)
        t.ticks(2)
        t.exec("takeKey-dialog", t.chat.play, { "objbox:You take the key. It doesn't resist anymore." })
        t.exec("takeKey.held", t.inv.await, "hauntedmine_reward_key", 1, 4)
        t.exec("fail.secondKey", t.player.press, "hauntedmine_boss_key", 1, 8)
        t.exec("fail.secondKey.line", t.msg.expect, "You already have a key like that.")

        -- ---------------------------------------------------------------
        -- Out, west to the crystal hall, the doors (stage 10), the crystal (stage 11).
        -- ---------------------------------------------------------------
        t.exec("walk-bossDoorOut", t.player.walk_to, 2798, 4453, 20)
        click("bossDoorOut", "hauntedmine_boss_door", 2799, 4453)
        climb("upFromDayth", "hauntedmine_light_stairs_bottom", "Walk-up", { 2812, 4452 }, { 2811, 4453 },
            { 2750, 4437 }, STAIR)
        t.exec("walk-flooded-west3", t.player.walk_route, { { 2740, 4437 }, { 2730, 4437 }, { 2720, 4437 },
            { 2710, 4437 }, { 2700, 4439 }, { 2691, 4438 } })
        climb("downToCrystalHall2", "hauntedmine_dark_stairs_top", "Walk-down", { 2692, 4436 }, { 2691, 4438 },
            { 2758, 4454 }, STAIR)
        t.exec("walk-crystalDoor", t.player.walk_to, 2772, 4450, 30)
        click("crystalDoors", "hauntedmine_rewarddoor_r", 2772, 4450)
        t.expect("quest.stage.crystalsReached", t.var.await_server("varp382_hauntedmine", 10, 6))
        -- The journal at stage 10, row for row against RSProx index 4647 [7065] (17 rows).
        do
            local want = {
                "I met a fanatic Saradominist just outside the Morytanian",
                "mines.  He told me that the mines had been sealed, but",
                "contained within them crystals of a great power over evil.",
                "I managed to get into the mines via a thin tunnel high up in the",
                "mountains.",
                "Now I must reach the lower levels where apparently the",
                "crystals can be found.",
                "I managed to start the lift which allowed me to get to the lower",
                "levels.",
                "Now I can continue my search for the crystals.",
                "The lower levels of the mine were too dark to navigate.",
                "I had to use a glowing fungus from the upper levels to light",
                "the way.",
                "I found a key in the lower levels.  I had to fight a powerful",
                "ghost who tried to prevent me taking it.",
                "I used this key to reach the legendary crystals, now I just",
                "need to find some way to make use of them.",
            }
            local r, j = t.ui.journal_open("Haunted Mine")
            local lines = (r == "ok" and j and j.lines) or {}
            if r == "ok" then t.ui.journal_close() end
            local diff = nil
            for i = 1, math.max(#want, #lines) do
                local got = lines[i] and tostring(lines[i]):gsub("<[^>]*>", "") or nil
                if got ~= want[i] then
                    diff = "row " .. i .. ": want '" .. tostring(want[i]) .. "' read '" .. tostring(got) .. "'"
                    break
                end
            end
            t.check("journal.stage10", r == "ok" and diff == nil, tostring(r) .. " " .. #lines .. " rows; "
                .. (diff or "every row matches [7065]"))
        end
        local _, xpsnap = t.skill.snapshot()
        local crystal = t.player.by_symbol("loc", "crystalcorner")
        t.exec("walk-crystal", t.player.walk_to, 2787, 4429, 40)
        t.exec("cutCrystal", t.player.use_on, "chisel", crystal)
        t.exec("cutCrystal.line", t.msg.expect, "You cut a shard from the crystal.")
        t.exec("stage-complete", t.var.await, "varp382_hauntedmine", 11, 10)
        t.quest.expect_complete()
        t.check("reward.strength_xp", t.skill.expect_gain("strength", 22000, xpsnap))
        local _, shards = t.inv.count("crystalshard_necklace_unstrung")
        t.check("reward.salve_shard", shards == 1, "crystalshard_necklace_unstrung " .. tostring(shards) .. " (want 1)")
        t.finish(0)
    end,
}
