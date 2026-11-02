-- Wanted! -- Temple Knights recruitment, Solus Dellagar hunt.
-- Guide: docs/quests/wanted.md (Quest Helper wanted/Wanted.java, steps.put
-- 0..10 -> complete 11). Content: OSRS-Content quest_wanted (wanted_tiffy_amik.rs2,
-- wanted_commorb.rs2, wanted_daquarius.rs2, wanted_mage.rs2, wanted_hunt.rs2).
--
-- Requirements (wiki infobox, docs/quests/wanted.md section 2): 32 quest
-- points; Recruitment Drive, The Lost Tribe, Priest in Peril, Enter the
-- Abyss all finished; the ability to defeat a level 32 Black Knight; 10,000
-- coins (or components) for the Commorb; 20 un-noted essence. The Sir Amik
-- Varze NPC this quest reuses (areas/falador/scripts/sir_amik_varze.rs2)
-- ALSO gates its own [opnpc1] on %spy = ^blackknight_complete (Black
-- Knights' Fortress, a real Recruitment Drive prerequisite) before it will
-- even reach the %rd_main switch that leads to Wanted!'s own branch, so
-- that quest is completed too. The remaining ::complete lines are unrelated
-- filler quests (grepped clear of rd_teleporter_guy/sir_amik_varze/
-- lord_daquarius/rcu_zammy_mage) whose quest points alone reach the 32 QP
-- gate; ::complete only writes state + quest points (quest_cheat.rs2), never
-- rewards, so this is staging the quest's own genuine gate, not cheating
-- Wanted!'s own work.
--
-- Door rule (b67 re-drive; docs/QUEST_ORCHESTRATOR.md standing rules). Every goto departs from and
-- lands on an open tile outside, checked with sample_tools/reach.py --root <worktree> (doors closed,
-- margin 160; the notebook build/orchestrator/fix_b67/wanted.progress.md lists every leg):
--   * no setup placement: `::wanted` stood the player on the solid tile 2997,3373 and only reset
--     %wanted_* (already 0 on the fresh fixture), so the run starts at the fixture's Lumbridge tile
--     3206,3233 and walks to Falador Park (REACH 384);
--   * the White Knights' Castle keep door and both spiral staircases, both ways, on every visit;
--   * Taverley is past the members' gate membergater 2935,3450 (the only walk from Falador): it is
--     crossed by its verb in and out, the dungeon ladder climbed both ways, the base door passed;
--   * Morytania (Canifis) has no on-foot route and no spell this pack implements lands there
--     (skill_magic/scripts/spells/teleport.rs2): the Paterdomus route Priest in Peril opens
--     (mortton.lua's): the Varrock members' gate, the trapdoor, the two mausoleum gates, Drezel's
--     advice and the holy barrier;
--   * the random pool stops: Lumbridge/Varrock-side stops (6, 7, 11, 12, 19) are walked; Kandarin's
--     (8, 9, 10, 14, 15, 16, 18) are past a members' gate, so Camelot Teleport first; Karamja's
--     (5, 13) by Port Sarim's paid boat (and Karamja's members' gate for Brimhaven); the Slayer
--     Tower (17) on foot from Canifis, else the Paterdomus route again. Leaving Camelot (where
--     Solus throws the player) and the last pool stop is Varrock Teleport;
--   * the runes, the boat fares and the wolfbane dagger are banked in setup (the backpack is full
--     until the mage takes the 20 essence) and withdrawn at Varrock's east bank by click.
-- Staged Magic 45 is Camelot Teleport's level. The 99 melee stats already set the combat level, so
-- Magic changes nothing a dialogue reads; no script on the route (quest_wanted, sir_amik_varze,
-- quest_priestperil, aubury, the Port Sarim sailors, gnome_gate, femi) branches on combat level.
return {
    id = "wanted",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- the hunt is walked now: Falador, Taverley and back, Paterdomus into Morytania, three pool stops, Lumbridge cellar
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so requirements fit
        "::complete quest_blackknightsfortress", -- sir_amik_varze.rs2's own [opnpc1] gate (%spy = 4) ahead of its %rd_main switch
        "::complete quest_recruitmentdrive", -- Wanted!'s own requirement; also unblocks rd_teleporter_guy/sir_amik_varze's Wanted! branch (%rd_main = ^rd_complete)
        "::complete quest_runemysteries", -- Aubury only teleports a player who finished Rune Mysteries (aubury.rs2:62), the guide's goToEssenceMine
        "::complete quest_losttribe", -- Wanted!'s own requirement
        "::complete quest_priestinperil", -- Wanted!'s own requirement (dbrow quest_priestinperil, not quest_priestperil); also the Paterdomus route into Morytania
        "::complete miniquest_entertheabyss", -- Wanted!'s own requirement (dbrow miniquest_entertheabyss, not quest_entertheabyss)
        -- Filler quest points only, to clear the 32 QP gate (~wanted_meets_requirements) -- none of these touch rd_teleporter_guy/sir_amik_varze/lord_daquarius/rcu_zammy_mage.
        "::complete quest_druidicritual",
        "::complete quest_romeoandjuliet",
        "::complete quest_ernestthechicken",
        "::complete quest_demonslayer",
        "::complete quest_vampyreslayer",
        "::complete quest_princealirescue",
        "::complete quest_murdermystery",
        "::complete quest_makinghistory",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel magic 45", -- Camelot Teleport's level (Varrock Teleport needs 25); see the header on combat level
        "::setlevel agility 13", -- the Lost Tribe hole to Dorgesh-Kaan's mine (losttribe.rs2: "You need an Agility level of 13")
        "::give coins 10000", -- getItemRequirements(): 10,000 coins for the Commorb (bought, not made)
        "::give blankrune_high 20", -- 20 pure essence for the Mage of Zamorak (RSProx index 4464 [1948]: pure, "20 Pure Essences" in the journal)
        "::give rune_scimitar 1",
        "::wield rune_scimitar", -- worn in setup so its backpack slot is free for the light source
        "::give slayer_helm 1", -- getItemRequirements(): the spiny helmet OR slayer helm for the swamp caves (no spiny helmet obj exists; slayer_helm is the guide's alternative)
        "::wield slayer_helm",
        "::give candle_lantern_lit 1", -- getItemRequirements(): a light source for the swamp caves
        "::give rope 1", -- getItemRequirements(): A rope (goDownToLumbridgeSwampCaves: the hole's first entry ties it, slice_sergeants.rs2:72-86)
        "::give shark 4", -- food for the Black Knight / Solus fights (four, not five: the backpack is full at 28 with the light source, and the Commorb purchase needs one free slot)
        -- Travel a player uses, banked (the backpack is full until the mage takes the essence) and
        -- withdrawn by click at Varrock's east bank. At most five casts: a pool teleport from Canifis,
        -- Varrock out of Camelot, Camelot for pool stops 4 and 6, Varrock after the last stop.
        "::bankgive lawrune 5",
        "::bankgive airrune 21", -- 3 Camelot (5 each) + 2 Varrock (3 each)
        "::bankgive firerune 3", -- up to 3 Varrock Teleports
        "::bankgive coins 60", -- two Port Sarim fares (sailors.rs2 karamja_sailor_pay, 30 each) if pool stops 5 and 13 are both drawn
        "::bankgive dagger_wolfbane 1", -- Priest in Peril's own reward (::complete grants no items); Drezel's advice counts it in the bank (mausoleum_drezel.rs2:28-33 ~obj_gettotal, inv_procs.rs2:221 = inv + bank + worn)
    },

    run = function(t)
        -- A conversation by its menus: every page between them is continued (or, a
        -- page with nothing to click, waited out: the hunt's scenes are timed,
        -- RSProx index 4464), each menu answered with the next choice in order.
        -- A scan scene is over when its stop's bit is written, the lock is lifted and its
        -- last (clickable) page is gone.
        local function scene_done(var)
            return function()
                local _, v = t.var.server(var)
                local _, cs = t.var.server("varb542_cutscene_status")
                return v == 1 and cs == 0 and t.chat.kind() == "none"
            end
        end

        local function converse(choices, opts)
            opts = opts or {}
            local seen = {}
            if opts.open then
                -- a scene's first page comes a couple of ticks after the click that starts it
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the scene's first page" }, opts.open)
            end
            for _, choice in ipairs(choices) do
                local r, d = t.chat.drain({ stop_at = "options", wait_ticks = opts.wait_ticks or 120, max_pages = 200 })
                if r ~= "ok" or d ~= "options" then
                    return "mismatch", "before '" .. choice .. "': drain -> " .. tostring(r) .. " " .. tostring(d)
                end
                local cr, cd = t.chat.choose(choice)
                if cr ~= "ok" then
                    return cr, "choose '" .. choice .. "': " .. tostring(cd)
                end
                seen[#seen + 1] = choice
            end
            local r, d = t.chat.drain({ wait_ticks = opts.wait_ticks or 120, max_pages = 200 })
            if r ~= "ok" then
                return r, "after the menus: " .. tostring(d)
            end
            -- A scene has gaps with no page up (a spell's empty box, a fade): keep
            -- draining until the scene's own end (a varbit it writes) is in.
            if opts.done then
                local budget = opts.budget or 300
                while not opts.done() and budget > 0 do
                    t.ticks(1)
                    budget = budget - 1
                    if t.chat.kind() ~= "none" then
                        r, d = t.chat.drain({ wait_ticks = opts.wait_ticks or 120, max_pages = 200 })
                        if r ~= "ok" then
                            return r, "in the scene: " .. tostring(d)
                        end
                    end
                end
                if not opts.done() then
                    return "timeout", "the scene's end never came"
                end
            end
            return "ok", #seen .. " menu(s): " .. table.concat(seen, " / ") .. "; ended on " .. tostring(d)
        end

        -- The 15-location random pool (missions 5-19; wanted.constant zones,
        -- wanted_hunt.rs2 ~wanted_pool_clue) -- three are drawn at random by
        -- the server for scan positions 2/4/6, so the test reads back which
        -- ids got drawn instead of assuming any particular location.
        -- { name, hop x, hop z, level, item, zone minx, maxx, minz, maxz } -- the hop is an OPEN tile
        -- outside, never inside a building or behind a gate. 9, 12, 13, 16, 17, 18 and 19 are behind a
        -- gate, door, railing or cave entrance, so the hop stands OUTSIDE it and enter_pool goes in on
        -- foot (and out again at position 6, the one position Solus does not teleport you away from):
        --   9  the Gnome Stronghold gate gnome_areagate 2459,3383, then the Grand Tree door treedoorl 2464,3492
        --   12 the Wizards' Tower north door fai_wiztower_poor_door 3109,3167 (the hall is in the zone)
        --   13 the Brimhaven pub's east door (open in the map: poordooropen leaf at 2799,3167)
        --   16 McGrubor's Wood loose railing mcgruborlooserailing 2662,3500 (mcgrubors_wood.rs2: one tile across)
        --   17 the Slayer Tower double door slayertower_door 3428,3535
        --   18 the Yanille pub door poshdoor 2551,3082
        --   19 the Lumbridge Swamp Caves hole, the cave walk and the stepping stone
        local POOL = {
            [5]  = { "Musa Point", 2916, 3160, 0, "banana", 2908, 2924, 3152, 3168 },
            [6]  = { "Draynor Market", 3081, 3250, 0, "horsey_black", 3077, 3085, 3246, 3254 },
            [7]  = { "the goblin village", 2957, 3507, 0, "goblin_armour", 2947, 2967, 3499, 3516 },
            [8]  = { "Ardougne Market", 2661, 3307, 0, "fur", 2655, 2668, 3301, 3313 },
            [9]  = { "the Grand Tree", 2461, 3379, 0, "gnome_hat_cream", 2463, 2469, 3493, 3499 },
            [10] = { "the Shrine of Scorpius", 2465, 3228, 0, "blessedsnake", 2461, 2470, 3225, 3231 },
            [11] = { "Ali Morrisane's stall", 3303, 3213, 0, "feud_karidian_fakebeard", 3300, 3306, 3209, 3216 },
            [12] = { "the Wizards' Tower", 3109, 3169, 0, "bluewizhat", 3104, 3114, 3155, 3166 },
            [13] = { "the pub in Brimhaven", 2801, 3167, 0, "eye_patch", 2791, 2800, 3154, 3170 },
            [14] = { "Castle Wars", 2447, 3090, 0, "castlewars_ticket", 2435, 2447, 3081, 3099 },
            [15] = { "Rellekka", 2659, 3657, 0, "viking_cloak_brown", 2654, 2664, 3650, 3665 },
            [16] = { "McGrubor's Wood", 2660, 3500, 0, "red_vine_worm", 2662, 2677, 3484, 3504 },
            [17] = { "the Slayer Tower", 3428, 3533, 0, "slayer_earmuffs", 3405, 3453, 3534, 3580 },
            [18] = { "the pub in Yanille", 2551, 3086, 0, "greenmans_ale", 2548, 2557, 3077, 3082 },
            [19] = { "the Lumbridge Swamp Caves", 3170, 3176, 0, "giant_frog_legs", 3216, 3239, 9540, 9555 },
        }
        -- How each pool stop is reached (reach.py, doors closed, margin 160):
        --   from Lumbridge 3222,3218, the Champions' Guild 3191,3367 or Varrock Teleport's 3213,3424:
        --     6, 7, 11, 12, 19 REACH closed-doors (11 by the open way round the Al Kharid toll gate);
        --     8, 9, 10, 14, 15, 16, 18 only through membergater 2935,3450 or 2933,3320;
        --     5, 13 UNREACHABLE (Karamja is an island); 17 UNREACHABLE (Morytania, past the Salve).
        --   from Camelot Teleport's 2757,3478: 8 (271), 9 (411), 10 (552), 14 (736), 15 (287), 16 (145),
        --     18 (598) REACH closed-doors.
        --   Port Sarim's seaman 3028,3221 REACH from all three; Musa Point's jetty 2956,3146 -> 2916,3160
        --     REACH 56, -> 2818,3182 (east of Karamja's membergatel) REACH 176; 2815,3182 -> 2801,3167 REACH 29.
        --   Canifis 3485,3481 -> 3428,3533 REACH 119; the holy barrier's 3423,3485 -> 3428,3533 REACH 77.
        -- The recorded stops' scan tiles (RSProx index 4464): Brimhaven [3235], Goblin Village [3681],
        -- the Slayer Tower [4426]. The rest scan anywhere in the place.
        local SCAN_TILE = { [13] = { 2798, 3167 }, [7] = { 2955, 3510 }, [17] = { 3426, 3538 } }
        local function to_scan_tile(pfx, id)
            if SCAN_TILE[id] then
                t.exec(pfx .. ".scanTile", t.player.walk_to, SCAN_TILE[id][1], SCAN_TILE[id][2], 30)
            end
        end
        local CAMELOT_POOLS = { [8] = true, [9] = true, [10] = true, [14] = true, [15] = true, [16] = true, [18] = true }
        local KARAMJA_POOLS = { [5] = true, [13] = true }

        local function tile_text(r, tt)
            return r == "ok" and (tt.x .. "," .. tt.z .. "," .. tt.level) or tostring(r)
        end

        local function varrock_teleport(name)
            t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = name,
                runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock square, tele_coord 0_50_53_13_32" })
        end
        local function camelot_teleport(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        end

        -- Taverley's members' gate (gates.rs2 member_fencegate_try, a walk-through: pressed on every crossing).
        local function taverley_gate_in(pfx)
            t.exec("goto-" .. pfx .. ".memberGate", t.player.goto_tile, 2938, 3450, 0)
            t.exec(pfx .. ".memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
                far_desc = "inside Taverley, x <= 2935" })
        end
        local function taverley_gate_out(pfx)
            t.exec("goto-" .. pfx .. ".memberGate", t.player.goto_tile, 2933, 3450, 0)
            t.exec(pfx .. ".memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2934, 3450 }, far_ok = function(tile) return tile.x >= 2936 end,
                far_desc = "out of Taverley, x >= 2936", far = { 2937, 3450 } })
        end

        -- Into Morytania by the Paterdomus route (mortton.lua's enterMorytania, fix_b66): the Varrock
        -- members' gate 3319,3468, the trapdoor 3405,3507, the mausoleum gates, Drezel's advice
        -- (mausoleum_drezel.rs2:145-154, 60 -> 61; only on the first trip) and the holy barrier
        -- (mausoleum_interactions.rs2:26: p_telejump to 3423,3485 once %priestperil = 61).
        local function enter_morytania(pfx)
            t.exec("goto-" .. pfx .. ".varrockGate", t.player.goto_tile, 3318, 3468, 0)
            t.exec(pfx .. ".varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
                open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
            t.exec("goto-" .. pfx .. ".trapdoor", t.player.goto_tile, 3405, 3506, 0)
            -- an earlier trip's press can still stand open (loc_change ... 500): then it is not pressed again
            if t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) ~= "ok" then
                t.exec(pfx .. ".openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507, 0 } })
                t.await({
                    level = function()
                        return t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } }) == "ok"
                    end,
                    note = pfx .. ": the trapdoor opens",
                }, 6)
            end
            local tdo_r, tdo = t.world.loc_near("trapdoor_open", 3, { at = { 3405, 3507, 0 } })
            local tdc_r = t.world.loc_near("trapdoor", 3, { at = { 3405, 3507, 0 } })
            t.check(pfx .. ".trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
                "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. " "
                    .. (tdo_r == "ok" and (tdo.tile_x .. "," .. tdo.tile_z .. "," .. tdo.level) or tostring(tdo))
                    .. "; closed trapdoor there -> " .. tostring(tdc_r) .. " (want the open leaf and no closed one)")
            t.exec(pfx .. ".descend", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
                at = { 3405, 3507, 0 }, src = { 3405, 3506 }, dest = { 3405, 9906, 0 } })
            t.exec(pfx .. ".gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
                far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
            t.exec(pfx .. ".gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
                near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
                far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
            local ppr, ppv = t.var.server("varp302_priestperil")
            if ppr == "ok" and ppv == 60 then
                -- Priest in Peril's farewell advice (the dagger is in the bank: ~obj_gettotal counts it).
                t.exec(pfx .. ".talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
                -- Priest in Peril rebuilt from RSProx 4459 (mausoleum_drezel.rs2): the blessing, then his menu.
                t.exec(pfx .. ".talkToDrezel-dialog", converse, { "And you. See you later." })
            end
            t.exec(pfx .. ".drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
            t.exec(pfx .. ".holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
                at = { 3440, 9886, 0 }, near = { 3440, 9887 },
                far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
                far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
            t.exec(pfx .. ".holyBarrier-msg", t.msg.expect, "You pass through the holy barrier")
        end

        -- Port Sarim -> Musa Point (sailors.rs2 karamja_sailor_talk -> karamja_sailor_pay: 30 coins,
        -- p_delay(2), p_telejump to the deck 2956,3143,1, its mesbox), then the gangplank ashore.
        -- Dragon Slayer and the Sailing intro are not started, so the menu is the plain p_choice2.
        local function sail_to_musa(pfx)
            t.exec("goto-" .. pfx .. ".seaman", t.player.goto_tile, 3028, 3221, 0)
            local coins0_r, coins0 = t.inv.count("coins")
            t.exec(pfx .. ".talkToSeaman", t.player.talk_to, "seaman_lorris", 1)
            t.exec(pfx .. ".talkToSeaman-dialog", t.chat.play, {
                "npc:Do you want to go on a trip to Karamja?",
                "npc:The trip will cost you 30 coins.",
                "options",
                "choose:Yes please.",
                "player:Yes please.",
            })
            local sail_r, sail_d = t.await({
                level = function()
                    return t.chat.kind() == "mesbox"
                end,
                note = pfx .. ": the arrival mesbox after p_delay(2) + telejump",
            }, 15)
            t.step(pfx .. ".sail", sail_r == "ok" and "PASS" or "FAIL",
                "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. tostring(sail_d))
            t.exec(pfx .. ".arrive", t.chat.play, { "mesbox:The ship arrives at Karamja." })
            local deck_r, deck = t.world.tile()
            local coins1_r, coins1 = t.inv.count("coins")
            t.check(pfx .. ".onDeckAtMusaPoint", deck_r == "ok" and deck.level == 1
                    and math.abs(deck.x - 2956) <= 2 and math.abs(deck.z - 3143) <= 2
                    and coins0_r == "ok" and coins1_r == "ok" and coins0 ~= nil and coins1 ~= nil and coins0 - coins1 == 30,
                "tile " .. tile_text(deck_r, deck) .. " (want the deck 2956,3143,1), coins " .. tostring(coins0)
                    .. " -> " .. tostring(coins1) .. " (want -30)")
            t.exec(pfx .. ".disembark", t.player.climb, { loc = "sarimshipplank_off", op_name = "Cross",
                at = { 2956, 3144, 1 }, src = { 2956, 3143 }, dest = { 2956, 3146, 0 }, slack = 1 })
        end

        -- The Gnome Stronghold gate (gnome_gate.rs2 [oploc1,gnome_areagate]): a press from either
        -- entrance force-moves the player three tiles through it (south entrance 2461,3382, north
        -- 2461,3385). While %varp6531_femi_help = 0 a press from the south with Femi within 6 tiles
        -- opens her boxes instead (gnome_gate.rs2:36-38 -> femi.rs2 @grandtree_femi_boxes); declining
        -- sets femi_help 1 (femi.rs2:84-87), and the gate is then crossed by its verb.
        local function stronghold_gate_in(pfx)   -- from the pool hop 2461,3379 (open ground south of the gate)
            t.exec(pfx .. ".gateApproach", t.player.walk_route, { { 2461, 3382 } })
            local press_result, press_detail = t.player.click_loc("gnome_areagate", 1, { at = { 2459, 3383 } })
            t.await({ level = function()
                if t.chat.kind() ~= "none" then
                    return true
                end
                local r, tl = t.world.tile()
                return r == "ok" and tl.z >= 3385
            end, note = "Femi's boxes page or the gate's walk-through" }, 12)
            if t.chat.kind() ~= "none" then
                t.exec(pfx .. ".femiBoxes-dialog", t.chat.play, {
                    "npc:Hello there", "player:Hi!", "npc:Could you help me lift", "options",
                    "choose:Sorry, I'm a bit busy.", "player:Sorry, I'm a bit busy.", "npc:Oh, OK, I'll do it myself." })
                local femi_result, femi_help = t.var.server("varp6531_femi_help")
                t.check(pfx .. ".femiDeclined", femi_result == "ok" and femi_help == 1,
                    "click_loc gnome_areagate -> " .. tostring(press_result) .. "; varp6531_femi_help = " .. tostring(femi_help)
                        .. " (" .. tostring(femi_result) .. "), want 1: her boxes declined (femi.rs2:84-87)")
                t.exec(pfx .. ".gateIn", t.player.cross_gate, { loc = "gnome_areagate", at = { 2459, 3383, 0 }, near = { 2461, 3382 },
                    far_ok = function(tile) return tile.z >= 3385 end, far_desc = "inside the Stronghold, z >= 3385" })
            else
                -- no page: Femi stood more than 6 tiles off (or was already declined), so the press was the gate's walk-through
                local first_result, first_tile = t.world.tile()
                t.check(pfx .. ".gateIn", first_result == "ok" and first_tile.level == 0 and first_tile.z >= 3385,
                    "click_loc gnome_areagate -> " .. tostring(press_result) .. " " .. tostring(press_detail)
                        .. "; no Femi page; after the press " .. tile_text(first_result, first_tile)
                        .. " (want through the gate from 2461,3382: z >= 3385)")
            end
        end

        -- The Lumbridge Swamp Caves: the hole's first entry ties the rope (slice_sergeants.rs2:72-86), the
        -- cave walk (reach.py 3169,9571 -> 3221,9556 REACH 133; hops of at most 10 tiles) and stepping stone b.
        local CAVE_WAY = { { 3169, 9571 }, { 3163, 9573 }, { 3158, 9573 }, { 3150, 9573 }, { 3146, 9573 }, { 3149, 9564 },
            { 3157, 9560 }, { 3164, 9555 }, { 3174, 9557 }, { 3184, 9557 }, { 3194, 9553 }, { 3203, 9556 }, { 3212, 9559 }, { 3221, 9556 } }

        -- Enter whichever closed space a pool position drew (the hop stands outside it).
        local function enter_pool(pfx, id)
            if id == 9 then
                stronghold_gate_in(pfx .. ".stronghold")
                -- inside the Stronghold: open ground to the tree door (reach.py 2461,3386 -> 2465,3489 REACH 111)
                t.exec("goto-" .. pfx .. ".treeDoor", t.player.goto_tile, 2465, 3489, 0)
                t.exec(pfx .. ".treeDoorIn", t.player.cross_gate, { loc = "treedoorl", at = { 2464, 3492, 0 }, near = { 2465, 3491 },
                    far_ok = function(tile) return tile.z >= 3493 and tile.z <= 3498 and tile.x >= 2463 and tile.x <= 2468 end,
                    far_desc = "inside the Grand Tree's ground floor, z 3493..3498" })
            elseif id == 12 then
                t.exec(pfx .. ".wizardsTowerIn", t.player.pass_door, { closed = "fai_wiztower_poor_door", open = "fai_wiztower_poor_door_open",
                    at = { 3109, 3167, 0 }, near = { 3109, 3168 }, far = { 3109, 3164 },
                    far_ok = function(tile) return tile.z <= 3166 end, far_desc = "inside the tower's hall, z <= 3166" })
            elseif id == 13 then
                t.exec(pfx .. ".brimhavenPubIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                    at = { 2800, 3167, 0 }, near = { 2801, 3167 }, far = { 2795, 3162 },
                    far_ok = function(tile) return tile.x <= 2799 end, far_desc = "inside the pub, x <= 2799" })
            elseif id == 16 then
                t.exec(pfx .. ".goToMcGruborsWood", t.player.cross_gate, { loc = "mcgruborlooserailing", at = { 2662, 3500, 0 },
                    near = { 2661, 3500 }, far_ok = function(tile) return tile.x >= 2662 end,
                    far_desc = "inside McGrubor's Wood, x >= 2662 (mcgrubors_wood.rs2: one tile east through the railing)" })
            elseif id == 17 then
                t.exec(pfx .. ".slayerTowerIn", t.player.pass_door, { closed = "slayertower_door", open = "slayertower_door_open",
                    at = { 3428, 3535, 0 }, near = { 3428, 3534 }, far = { 3428, 3538 },
                    far_ok = function(tile) return tile.z >= 3536 end, far_desc = "inside the tower, z >= 3536" })
            elseif id == 18 then
                t.exec(pfx .. ".pubIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                    at = { 2551, 3082, 0 }, near = { 2551, 3083 }, far = { 2552, 3079 },
                    far_ok = function(tile) return tile.z <= 3082 end, far_desc = "inside the pub, z <= 3082" })
            elseif id == 19 then
                local _, rope_before = t.inv.count("rope")
                t.exec(pfx .. ".goDownToLumbridgeSwampCaves", t.player.climb, { loc = "goblin_cave_entrance", op = 1, op_name = "Climb-down",
                    at = { 3169, 3172, 0 }, src = { 3169, 3171 }, dest = { 3169, 9571, 0 } })
                local rope_result, rope_after = t.inv.count("rope")
                t.check(pfx .. ".ropeTied", rope_result == "ok" and rope_before == 1 and rope_after == 0,
                    "rope " .. tostring(rope_before) .. " -> " .. tostring(rope_after) .. " (" .. tostring(rope_result)
                        .. "; the hole's first entry ties the rope: slice_sergeants.rs2:72-86)")
                t.exec(pfx .. ".caveWalk", t.player.walk_route, CAVE_WAY, { level = 0 })
                t.exec(pfx .. ".crossSteppingStone", t.player.cross_trap, { loc = "swamp_cave_steppingstone_b", op = 1,
                    op_name = "Jump-across", at = { 3221, 9554, 0 }, src = { 3221, 9556 }, dest = { 3221, 9552 } })
                t.exec(pfx .. ".walkToEndOfCaves", t.player.walk_to, 3222, 9548)
            end
        end

        -- Leave whatever closed space position 6 drew, the way it was entered.
        local function leave_pool(pfx, id)
            if id == 9 then
                t.exec(pfx .. ".treeDoorOut", t.player.cross_gate, { loc = "treedoorl", at = { 2464, 3492, 0 }, near = { 2465, 3493 },
                    far_ok = function(tile) return tile.z <= 3491 end, far_desc = "outside the Grand Tree, z <= 3491" })
                t.exec("goto-" .. pfx .. ".gateOut", t.player.goto_tile, 2461, 3386, 0)
                t.exec(pfx .. ".gateOut", t.player.cross_gate, { loc = "gnome_areagate", at = { 2459, 3383, 0 }, near = { 2461, 3385 },
                    far_ok = function(tile) return tile.z <= 3382 end, far_desc = "south of the Stronghold gate, z <= 3382" })
            elseif id == 12 then
                t.exec(pfx .. ".wizardsTowerOut", t.player.pass_door, { closed = "fai_wiztower_poor_door", open = "fai_wiztower_poor_door_open",
                    at = { 3109, 3167, 0 }, near = { 3109, 3165 }, far = { 3109, 3169 },
                    far_ok = function(tile) return tile.z >= 3167 end, far_desc = "outside the tower's north door, z >= 3167" })
            elseif id == 13 then
                t.exec(pfx .. ".brimhavenPubOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                    at = { 2800, 3167, 0 }, near = { 2798, 3167 }, far = { 2801, 3167 },
                    far_ok = function(tile) return tile.x >= 2800 end, far_desc = "outside the pub's east door, x >= 2800" })
            elseif id == 16 then
                t.exec(pfx .. ".railingOut", t.player.cross_gate, { loc = "mcgruborlooserailing", at = { 2662, 3500, 0 },
                    near = { 2662, 3500 }, far_ok = function(tile) return tile.x <= 2661 end,
                    far_desc = "outside McGrubor's Wood, x <= 2661 (one tile west through the railing)" })
            elseif id == 17 then
                t.exec(pfx .. ".slayerTowerOut", t.player.pass_door, { closed = "slayertower_door", open = "slayertower_door_open",
                    at = { 3428, 3535, 0 }, near = { 3428, 3537 }, far = { 3428, 3533 },
                    far_ok = function(tile) return tile.z <= 3535 end, far_desc = "outside the tower door, z <= 3535" })
            elseif id == 18 then
                t.exec(pfx .. ".pubOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                    at = { 2551, 3082, 0 }, near = { 2551, 3081 }, far = { 2551, 3085 },
                    far_ok = function(tile) return tile.z >= 3083 end, far_desc = "outside the pub door, z >= 3083" })
            elseif id == 19 then
                -- back to the stone's south bank first: cross_trap never walks more than 2 tiles onto src
                t.exec(pfx .. ".toSteppingStone", t.player.walk_to, 3221, 9552)
                t.exec(pfx .. ".recrossSteppingStone", t.player.cross_trap, { loc = "swamp_cave_steppingstone_b", op = 1,
                    op_name = "Jump-across", at = { 3221, 9554, 0 }, src = { 3221, 9552 }, dest = { 3221, 9556 } })
                local back = {}
                for i = #CAVE_WAY, 1, -1 do back[#back + 1] = CAVE_WAY[i] end
                t.exec(pfx .. ".caveWalkBack", t.player.walk_route, back, { level = 0 })
                t.exec(pfx .. ".climbRopeOut", t.player.climb, { loc = "swamp_cave_climbing_rope", op = 1, op_name = "Climb",
                    at = { 3169, 9572, 0 }, src = { 3169, 9571 }, dest = { 3169, 3171, 0 } })
            end
        end

        -- Travel to a pool stop's hop from `from` ("canifis", or "mainland" for the Champions' Guild /
        -- Lumbridge courtyard), then go in. Each travel leg is the one the table above names.
        local function travel_to_pool(pfx, id, from)
            local pool = POOL[id]
            if id == 17 then
                if from ~= "canifis" then
                    enter_morytania(pfx .. ".morytania")
                end
            elseif CAMELOT_POOLS[id] then
                camelot_teleport(pfx .. ".camelotTeleport")
            elseif from == "canifis" then
                varrock_teleport(pfx .. ".varrockTeleport")
            end
            if KARAMJA_POOLS[id] then
                sail_to_musa(pfx .. ".boat")
                if id == 13 then
                    t.exec("goto-" .. pfx .. ".karamjaGate", t.player.goto_tile, 2818, 3182, 0)
                    t.exec(pfx .. ".karamjaGate", t.player.cross_gate, { loc = "membergatel", at = { 2816, 3182, 0 },
                        near = { 2817, 3182 }, far_ok = function(tile) return tile.x <= 2815 end,
                        far_desc = "west of the gate in Brimhaven, x <= 2815" })
                end
            end
            t.exec(pfx .. ".goto", t.player.goto_tile, pool[2], pool[3], pool[4])
            enter_pool(pfx, id)
            -- a crossing that moves the player (the railing's ~agility_exactmove, the tree door's
            -- ~forcemove) still holds them when its verb reads the landing, and a Commorb scan pressed
            -- then is dropped (b67: McGrubor's run 1, the Grand Tree on accounts x15-x17)
            t.ticks(5)
        end

        local function read_pool_draw()
            local drawn = nil
            for id = 5, 19 do
                if drawn == nil then
                    local _, assigned = t.var.server("varb" .. (1063 + 2 * id) .. "_wanted_mission" .. id)
                    local _, doneflag = t.var.server("varb" .. (1064 + 2 * id) .. "_wanted_mission" .. id .. "complete")
                    if assigned == 1 and doneflag ~= 1 then
                        drawn = id
                    end
                end
            end
            return drawn
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1051_wanted_main",
            constants = {
                not_started = 0,
                started = 1,
                briefed = 2,
                amik_first = 3,
                tiffy_second = 4,
                amik_second = 5,
                tiffy_third = 6,
                get_commorb = 7,
                investigation = 8,
                hunt = 9,
                final_battle = 10,
                complete = 11,
            },
            display = "Wanted!",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        local qp_before_result, qp_before = t.var.varp("varp101_qp")
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        do
            local _, scim = t.inv.count("rune_scimitar")
            local _, helm = t.inv.count("slayer_helm")
            local _, lamp = t.inv.count("candle_lantern_lit")
            t.check("equip.worn", scim == 0 and helm == 0 and lamp == 1, "scimitar and helm wielded in setup (backpack copies " .. tostring(scim) .. "/" .. tostring(helm) .. "), light source carried " .. tostring(lamp))
        end

        -- 1. Sir Tiffy Cashien, Falador Park: the clerk's error, the loophole. The first goto: the
        -- fixture's Lumbridge tile 3206,3233 -> the open park tile beside him (reach.py REACH 384;
        -- 2997,3373 is solid).
        t.exec("goto-tiffy1", t.player.goto_tile, 2997, 3374, 0)
        t.exec("tiffy1.talk", t.player.talk_to, "rd_teleporter_guy", 1)
        -- RSProx index 4464 [175..314]: his menu, the dossier, the start confirm, the
        -- plan; "What am I supposed to be doing again?" back to his menu, asked once more.
        t.exec("tiffy1.dialog", converse, { "Do you have any jobs for me yet?", "Yes.", "Ask about the Wanted! Quest",
            "Do I really have to quit the Temple Knights?", "How will all that help?",
            "What am I supposed to be doing again?", "Ask about the Wanted! Quest" })
        t.expect("quest.stage.amik_first", t.quest.expect_stage("amik_first"))

        -- The White Knights' Castle: the goto lands in the open courtyard (2968,3338: the park
        -- reaches it with every door shut, REACH 71); the west keep holding the spiral stairs is
        -- behind the double door fai_falador_castledoubledoorl/r at 2965,3338-3339 (courtyard
        -- x >= 2965, keep x <= 2964), crossed on foot both ways on every visit. The stairs are
        -- climbed from their maplink src tiles (ladders_stairs/configs/maplink.dbrow:8903-8989):
        -- 2955,3337,0 -> 2956,3338,1; 2960,3340,1 -> 2959,3339,2; and back down
        -- 2959,3339,2 -> 2960,3340,1; 2956,3338,1 -> 2955,3337,0.
        local function castle_in(pfx)
            t.exec("goto-" .. pfx .. ".castle", t.player.goto_tile, 2968, 3338, 0)
            t.exec(pfx .. ".castleDoorIn", t.player.pass_door, { closed = "fai_falador_castledoubledoorl",
                open = "fai_falador_opencastledoubledoorl", at = { 2965, 3338, 0 }, near = { 2966, 3338 }, far = { 2962, 3338 },
                far_ok = function(tile) return tile.x <= 2964 end, far_desc = "inside the west keep, x <= 2964" })
            t.exec(pfx .. ".climbToWhiteKnightsCastleF1", t.player.climb, { loc = "fai_falador_castle_spiralstairs", op = 1,
                op_name = "Climb-up", at = { 2954, 3338, 0 }, src = { 2955, 3337 }, dest = { 2956, 3338, 1 } })
            t.exec(pfx .. ".climbToWhiteKnightsCastleF2", t.player.climb, { loc = "fai_falador_castle_spiralstairs", op = 1,
                op_name = "Climb-up", at = { 2960, 3338, 1 }, src = { 2960, 3340 }, dest = { 2959, 3339, 2 } })
        end
        local function castle_out(pfx)
            t.exec(pfx .. ".stairsDownF1", t.player.climb, { loc = "fai_falador_castle_spiralstairstop", op = 1,
                op_name = "Climb-down", at = { 2960, 3339, 2 }, src = { 2959, 3339 }, dest = { 2960, 3340, 1 } })
            t.exec(pfx .. ".stairsDownGround", t.player.climb, { loc = "fai_falador_castle_spiralstairstop", op = 1,
                op_name = "Climb-down", at = { 2955, 3338, 1 }, src = { 2956, 3338 }, dest = { 2955, 3337, 0 } })
            t.exec(pfx .. ".castleDoorOut", t.player.pass_door, { closed = "fai_falador_castledoubledoorl",
                open = "fai_falador_opencastledoubledoorl", at = { 2965, 3338, 0 }, near = { 2963, 3338 }, far = { 2968, 3338 },
                far_ok = function(tile) return tile.x >= 2965 end, far_desc = "back in the open courtyard, x >= 2965" })
        end

        -- 2. Sir Amik Varze (White Knights' Castle, 2nd floor): DECLINE the squire offer
        castle_in("amik1")
        t.exec("amik1.talk", t.player.talk_to, "sir_amik_varze", 1)
        -- [456..525]: what a squire does, then no.
        t.exec("amik1.dialog", converse, { "What does being a Squire entail?", "No, not right now..." })
        t.expect("quest.stage.tiffy_second", t.quest.expect_stage("tiffy_second"))
        castle_out("amik1")

        -- 3. Tiffy: a crisis has arisen (2997,3374: the open park tile beside him; 2997,3373 is solid)
        t.exec("goto-tiffy2", t.player.goto_tile, 2997, 3374, 0)
        t.exec("tiffy2.talk", t.player.talk_to, "rd_teleporter_guy", 1)
        -- [587..602]
        t.exec("tiffy2.dialog", converse, { "Ask about the Wanted! Quest" })
        t.expect("quest.stage.amik_second", t.quest.expect_stage("amik_second"))

        -- 4. Amik: Solus Dellagar is back; accept the mission
        castle_in("amik2")
        t.exec("amik2.talk", t.player.talk_to, "sir_amik_varze", 1)
        -- [657..718]: who Solus is, the reward, then yes.
        t.exec("amik2.dialog", converse, { "Who is Solus?", "What will I get for helping?", "Sure, I'll help you!" })
        t.expect("quest.stage.tiffy_third", t.quest.expect_stage("tiffy_third"))
        castle_out("amik2")

        -- 5. Tiffy offers the Commorb: buy it for 10,000 coins
        t.exec("goto-tiffy3", t.player.goto_tile, 2997, 3374, 0)
        t.exec("tiffy3.talk", t.player.talk_to, "rd_teleporter_guy", 1)
        -- [765..804], then bought (UNVERIFIED: the recorder had one made, [913..927]).
        t.exec("tiffy3.dialog", converse, { "Ask about the Wanted! Quest", "Buy One", "YES", "YES" })
        t.expect("quest.stage.investigation", t.quest.expect_stage("investigation"))
        t.expect("commorb.held", t.inv.expect_has("wanted_crystal_ball", 1))
        do
            local cr, coins = t.inv.count("coins")
            t.check("commorb.paid", cr == "ok" and coins == 0, "coins in the backpack after the purchase " .. tostring(coins) .. " (" .. tostring(cr) .. "; want 0: 10,000 -> 0)")
        end

        -- 6. Commorb Contact: Savant sends you to the Black Knights' Base and the Mage of Zamorak
        t.exec("contact1.op", t.player.inv_op, "wanted_crystal_ball", 2)
        -- [947..1047]: the first call, then the assignment.
        t.exec("contact1.dialog", converse, { "Current Assignment" })
        t.expect("commorb.contacted", t.var.await_server("varb1054_wanted_contacted_savant", 1, 10))
        t.expect("commorb.intel", t.var.await_server("varb1053_wanted_commorb_intel", 1, 10))

        -- 6b. The observation records, played in the White Knights' Castle courtyard where the
        -- recorder watched them (RSProx index 4464 [1296..1521], [1567..1781]): each a scene in a
        -- copy of its room, the player put back where they stood.
        t.exec("goto-records", t.player.goto_tile, 2968, 3338, 0)
        for rec = 1, 2 do
            t.ticks(4) -- the last record's script ends a tick after its unlock
            local br, bt = t.world.tile()
            t.exec("record" .. rec .. ".play", t.player.inv_op, "wanted_crystal_ball", 4)
            t.exec("record" .. rec .. ".scene", converse, { "Current Assignment", "Solus Observation Record " .. rec }, { wait_ticks = 40 })
            t.await({ level = function()
                local _, v = t.var.server("varb542_cutscene_status")
                return v == 0
            end, note = "record " .. rec .. " over" }, 260)
            local ar, at = t.world.tile()
            t.check("record" .. rec .. ".back", br == "ok" and ar == "ok" and at.x == bt.x and at.z == bt.z and at.level == bt.level,
                "before " .. tile_text(br, bt) .. " after " .. tile_text(ar, at) .. " (want the same tile)")
        end

        -- 7. Lord Daquarius (Taverley Dungeon, SW room) tells you nothing. Falador Park -> east of
        -- Taverley's members' gate (REACH 135), through it, then 2934,3450 -> beside the dungeon
        -- ladder (REACH 102). The ladder's maplink row keys the player's tile: 2884,3398 -> 2884,9798.
        taverley_gate_in("taverleyIn")
        t.exec("goto-taverley", t.player.goto_tile, 2884, 3398, 0)
        t.exec("enterTaverleyDungeon", t.player.climb, { loc = "ladder_outside_to_underground", op = 1, op_name = "Climb-down",
            at = { 2884, 3397, 0 }, src = { 2884, 3398 }, dest = { 2884, 9798, 0 } })
        -- travel hop inside the dungeon to the open hall tile NORTH of the Black Knights' base door
        -- (reach.py 2884,9798 -> 2907,9701: closed-door REACH 316, one passage, no door crossed)
        t.exec("goto-base-door", t.player.goto_tile, 2907, 9701, 0)
        -- the base's double door castledoubledoorr/l at 2907-2908,9698 (south wall: the hall is z >= 9698, the base z <= 9697)
        t.exec("goToBlackKnightsBase", t.player.pass_door, { closed = "castledoubledoorr", open = "opencastledoubledoorr",
            at = { 2907, 9698, 0 }, near = { 2907, 9699 }, far = { 2907, 9695 },
            far_ok = function(tile) return tile.z <= 9697 end, far_desc = "inside the Black Knights' base, z <= 9697" })
        t.exec("base.at_daquarius", t.player.walk_to, 2893, 9683, 40)
        t.exec("daquarius1.talk", t.player.talk_to, "lord_daquarius", 1)
        -- [2223..2484]: the threat, Savant's advice.
        t.exec("daquarius1.dialog", converse, {})
        t.expect("daquarius.hint_talked", t.var.await_server("varb1055_wanted_daquarius_hint", 1, 10))

        -- 8. Kill a Black Knight after talking to Daquarius (drop_tables/scripts/black_knight.rs2)
        t.exec("blackknight.attack", t.player.attack, "black_knight", 2, 20)
        local bkr, bkd = t.exec("blackknight.dead", t.npc.await_dead_engaged, 60, 8, { eat = { item = "shark", below = 50 } })
        do
            local low = tonumber(string.match(tostring(bkd), "lowest hp (%d+)/"))
            local _, sharks = t.inv.count("shark")
            t.check("blackknight.margin", low ~= nil and low >= 25 and sharks ~= nil and sharks >= 1, "lowest hp " .. tostring(low) .. " (want >= 25), sharks left " .. tostring(sharks) .. " (want >= 1): " .. tostring(bkd))
        end
        -- [2536]: his "Stop!" on the drop tick; the hint on its click [2539].
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Lord Daquarius's Stop! page" }, 10)
        t.exec("daquarius.stop", converse, {})
        t.expect("daquarius.hint_dead", t.var.await_server("varb1055_wanted_daquarius_hint", 2, 10))

        -- 9. Daquarius gives in: Solus is somewhere with fur "not from a bear"
        t.exec("daquarius2.talk", t.player.talk_to, "lord_daquarius", 1)
        -- [2580..2598]; the mage not yet paid, Savant sends the player on (UNVERIFIED, wiki).
        t.exec("daquarius2.dialog", converse, {})
        t.expect("daquarius.exposition", t.var.await_server("varb1058_wanted_lord_d_exposition", 1, 10))

        -- 10. Mage of Zamorak, Varrock Zamorakian chapel: 20 un-noted essence for the tip.
        -- Out of the base through the door pressed to get in (standing open unless it swung back),
        -- back to the ladder (REACH 316), up it (maplink 2884,9798 -> 2884,3398), out of Taverley by
        -- the members' gate (2884,3398 -> 2933,3450 REACH 103), then 2937,3450 -> the chapel street (REACH 410).
        t.exec("leaveBase", t.player.pass_door, { closed = "castledoubledoorr", open = "opencastledoubledoorr",
            at = { 2907, 9698, 0 }, near = { 2907, 9696 }, far = { 2907, 9701 },
            far_ok = function(tile) return tile.z >= 9699 end, far_desc = "the open hall north of the door, z >= 9699" })
        t.exec("goto-ladder", t.player.goto_tile, 2884, 9798, 0)
        t.exec("leaveTaverleyDungeon", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
            at = { 2884, 9797, 0 }, src = { 2884, 9798 }, dest = { 2884, 3398, 0 } })
        taverley_gate_out("taverleyOut")
        -- the Zamorak chapel's only door fai_varrock_poor_door_flipped at 3255,3388 (east wall:
        -- the street is x <= 3255, the chapel x >= 3256), crossed on foot going in and coming out
        t.exec("goto-mage", t.player.goto_tile, 3253, 3388, 0)
        t.exec("mage.chapelDoorIn", t.player.pass_door, { closed = "fai_varrock_poor_door_flipped",
            open = "fai_varrock_poor_door_open_flipped", at = { 3255, 3388, 0 }, near = { 3254, 3388 }, far = { 3257, 3387 },
            far_ok = function(tile) return tile.x >= 3256 end, far_desc = "inside the chapel, x >= 3256" })
        t.exec("mage1.talk", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        -- [1117..1194] his story and price; [1943..1966] the essence and what he knows;
        -- Daquarius already questioned, so the Canifis deduction is here (UNVERIFIED, wiki).
        t.exec("mage1.dialog", converse, { "Solus Dellagar." })
        t.expect("mage.price", t.var.await_server("varb1056_wanted_zammy_mage_hint", 1, 10))
        t.exec("mage2.talk", t.player.talk_to, "rcu_zammy_mage1_edge", 1)
        t.exec("mage2.dialog", converse, { "Solus Dellagar." })
        t.expect("quest.stage.hunt", t.quest.expect_stage("hunt"))
        t.expect("essence.spent", t.inv.expect_absent("blankrune_high"))
        t.exec("mage.chapelDoorOut", t.player.pass_door, { closed = "fai_varrock_poor_door_flipped",
            open = "fai_varrock_poor_door_open_flipped", at = { 3255, 3388, 0 }, near = { 3257, 3387 }, far = { 3253, 3388 },
            far_ok = function(tile) return tile.x <= 3255 end, far_desc = "back on the street west of the door, x <= 3255" })

        -- The hunt's travel kit, out of the bank now the essence has left the backpack: the street
        -- north of Varrock's east bank (REACH 56; the bank's doorway 3253-3254,3423 has no door op).
        t.exec("goto-bank", t.player.goto_tile, 3253, 3426, 0)
        t.exec("bank.open", t.bank.open, "fai_varrock_bankbooth", 2, { at = { 3253, 3419, 0 } })
        t.exec("bank.withdraw.law", t.bank.withdraw, "lawrune", "all")
        t.exec("bank.withdraw.air", t.bank.withdraw, "airrune", "all")
        t.exec("bank.withdraw.fire", t.bank.withdraw, "firerune", "all")
        t.exec("bank.withdraw.coins", t.bank.withdraw, "coins", 60) -- the fixture's bank holds coins of its own: take the two fares only
        t.check("bank.close", t.bank.close())
        t.exec("bank.walkOut", t.player.walk_to, 3253, 3426)

        -- 11. The hunt for Solus, position 1 -- Canifis (fixed): establishes Savant contact.
        -- Into Morytania by Paterdomus, then the barrier's 3423,3485 -> Canifis (REACH 76).
        enter_morytania("enterCanifis")
        -- Entering square 54_54 starts Savant's briefing (RSProx index 4464 [2749], [2763]).
        t.exec("canifis.goto", t.player.goto_tile, 3485, 3481, 0)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Savant's Canifis briefing" }, 20)
        t.exec("canifis.briefing", converse, {}, { open = 10 })
        t.expect("canifis.mission1", t.var.await_server("varb1065_wanted_mission1", 1, 10))
        -- The recorded scan tile, 4 from Solus's 3488,3489 [2831].
        t.exec("canifis.scanTile", t.player.walk_to, 3492, 3485, 30)
        t.exec("canifis.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("canifis.scene", converse, {}, { open = 10, done = scene_done("varb1066_wanted_mission1complete") })
        t.expect("canifis.done", t.var.await_server("varb1066_wanted_mission1complete", 1, 10))

        -- Read back which pool id got drawn for position 2 (missions 5-19).
        local pos2_id = read_pool_draw()
        t.check("hunt.pos2_drawn", pos2_id ~= nil, "position 2 pool id drawn = " .. tostring(pos2_id) .. " (" .. tostring(pos2_id and POOL[pos2_id][1]) .. ")")

        -- 12. Position 2 -- Solus forcibly teleports the player to Camelot. Out of Morytania: the
        -- Slayer Tower on foot, Kandarin by Camelot Teleport, the rest by Varrock Teleport.
        local pool2 = POOL[pos2_id]
        travel_to_pool("pos2", pos2_id, "canifis")
        do
            local zr, zt = t.world.tile()
            t.check("pos2.in_pool_zone", zr == "ok" and zt.x >= pool2[6] and zt.x <= pool2[7] and zt.z >= pool2[8] and zt.z <= pool2[9],
                "tile " .. (zr == "ok" and (zt.x .. "," .. zt.z .. "," .. zt.level) or tostring(zr)) .. " in " .. pool2[1] .. " zone x " .. pool2[6] .. "-" .. pool2[7] .. " z " .. pool2[8] .. "-" .. pool2[9])
        end
        to_scan_tile("pos2", pos2_id)
        t.exec("pos2.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("pos2.scene", converse, {}, { open = 10, done = scene_done("varb1067_wanted_mission2") })
        local camelot_result, camelot_tile = t.world.tile()
        t.check("pos2.teleported_camelot", camelot_result == "ok" and camelot_tile ~= nil
                and camelot_tile.x == 2759 and camelot_tile.z == 3480,
            "player tile " .. tostring(camelot_result == "ok" and (camelot_tile.x .. "," .. camelot_tile.z .. "," .. camelot_tile.level) or camelot_result)
                .. " (Teleother Camelot lands 2759,3480: RSProx index 4464 [3289])")
        t.expect("pos2.blue_cape", t.inv.expect_has("blue_cape", 1))
        t.expect("hunt.pos2_complete", t.var.await_server("varb1067_wanted_mission2", 1, 10))

        -- 13. Champions' Guild (fixed): Solus casts Smoke Barrage -- no damage, no poison.
        -- Camelot is past the members' gate from Varrock: Varrock Teleport, then 3213,3424 -> the
        -- guild's door step 3191,3367 (REACH 78). championdoor 3191,3363 (south wall of the step tile)
        -- is a walk-through (champions_guild.rs2: p_teleport across, never swings), pressed both ways;
        -- the guild master greets an entering player.
        varrock_teleport("cg.varrockTeleport")
        t.exec("cg.goto", t.player.goto_tile, 3191, 3367, 0)
        t.exec("cg.openGuildDoor", t.player.cross_gate, { loc = "championdoor", at = { 3191, 3363, 0 }, near = { 3191, 3364 },
            far_ok = function(tile) return tile.z <= 3362 and tile.z >= 3352 and tile.x >= 3188 and tile.x <= 3194 end,
            far_desc = "inside the guild, z 3352..3362", chat = { "npc:Greetings bold adventurer" },
            chat_optional = "the guild master greets only an entering player (champions_guild.rs2:12)" })
        t.exec("cg.scanTile", t.player.walk_to, 3194, 3357, 20) -- [3459]
        t.exec("cg.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("cg.scene", converse, {}, { open = 10, done = scene_done("varb1068_wanted_mission2complete") })
        t.expect("hunt.pos2_marked_complete", t.var.await_server("varb1068_wanted_mission2complete", 1, 10))
        t.exec("cg.openGuildDoorOut", t.player.cross_gate, { loc = "championdoor", at = { 3191, 3363, 0 }, near = { 3191, 3362 },
            far_ok = function(tile) return tile.z >= 3363 end, far_desc = "out of the guild, z >= 3363", far = { 3191, 3367 } })

        -- Read back which pool id got drawn for position 4.
        local pos4_id = read_pool_draw()
        t.check("hunt.pos4_drawn", pos4_id ~= nil, "position 4 pool id drawn = " .. tostring(pos4_id) .. " (" .. tostring(pos4_id and POOL[pos4_id][1]) .. ")")

        -- 14. Position 4 -- Solus's Flames of Zamorak: damage as a % of current HP, never lethal
        local pool4 = POOL[pos4_id]
        travel_to_pool("pos4", pos4_id, "mainland")
        do
            local zr, zt = t.world.tile()
            t.check("pos4.in_pool_zone", zr == "ok" and zt.x >= pool4[6] and zt.x <= pool4[7] and zt.z >= pool4[8] and zt.z <= pool4[9],
                "tile " .. (zr == "ok" and (zt.x .. "," .. zt.z .. "," .. zt.level) or tostring(zr)) .. " in " .. pool4[1] .. " zone x " .. pool4[6] .. "-" .. pool4[7] .. " z " .. pool4[8] .. "-" .. pool4[9])
        end
        to_scan_tile("pos4", pos4_id)
        local hp_before_result, hp_before_reading = t.skill.read("hitpoints")
        t.step("pos4.hp_before", hp_before_result == "ok" and "PASS" or "FAIL", "hitpoints=" .. tostring(hp_before_reading and hp_before_reading.level))
        t.exec("pos4.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("pos4.scene", converse, {}, { open = 10, done = scene_done("varb1069_wanted_mission3") })
        local hp_after_result, hp_after_reading = t.skill.read("hitpoints")
        -- read after the scene (~150 ticks): one regenerated hitpoint is allowed
        t.check("pos4.flames_damage", hp_after_result == "ok" and hp_after_reading ~= nil and hp_after_reading.level >= 10 and hp_after_reading.level <= 11,
            "hitpoints " .. tostring(hp_before_reading and hp_before_reading.level) .. " -> " .. tostring(hp_after_reading and hp_after_reading.level)
                .. " (want 10: RSProx index 4464 [3732] 29 -> 10)")
        local wkc_result, wkc_tile = t.world.tile()
        t.check("pos4.woke_at_falador_castle", wkc_result == "ok" and wkc_tile ~= nil and wkc_tile.x == 2971 and wkc_tile.z == 3338 and wkc_tile.level == 0,
            "player tile " .. tostring(wkc_result == "ok" and (wkc_tile.x .. "," .. wkc_tile.z .. "," .. wkc_tile.level) or wkc_result)
                .. " (want 2971,3338,0: [3737])")
        for eat_n = 1, 3 do
            t.player.inv_op("shark", 1)
            t.ticks(5)
        end
        t.expect("pos4.bone_spear", t.inv.expect_has("cave_goblin_bone_spear", 1))
        t.expect("hunt.pos4_complete", t.var.await_server("varb1069_wanted_mission3", 1, 10))

        -- 15. Dorgesh-Kaan mine (fixed): the 'hostage' Woman who is Solus. The castle's wakeup tile
        -- 2970,3345 -> Lumbridge's courtyard (REACH 445); the kitchen is a doorway (REACH 30), the
        -- cellar trapdoor and ladder climbed by their maplink rows, the Lost Tribe hole squeezed
        -- through (losttribe.rs2 [oploc1,lost_tribe_cavewall_hole_walldecor]: 3219 -> 3221,9618 and back
        -- to 3218,9618), and the tunnel to the mine (3221,9618 -> 3315,9629 REACH 167, one passage).
        t.exec("goto-lumbridge-castle", t.player.goto_tile, 3222, 3218, 0)
        t.exec("goDownToLumbridgeCellar", t.player.climb, { loc = "qip_cook_trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3209, 3216, 0 }, src = { 3210, 3216 }, dest = { 3210, 9616, 0 } })
        t.exec("dk.toHole", t.player.walk_route, { { 3219, 9618 } }, { level = 0 })
        t.exec("dk.squeezeThroughHole", t.player.cross_trap, { loc = "lost_tribe_cavewall_hole_walldecor", at = { 3219, 9618, 0 },
            src = { 3219, 9618 }, dest = { 3221, 9618 }, attempts = 1 })
        t.exec("dk.goto", t.player.goto_tile, 3315, 9629, 0)
        t.exec("dk.scanTile", t.player.walk_to, 3310, 9612, 40) -- [3993]
        t.exec("dk.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        t.exec("dk.scene", converse, {}, { open = 10, done = scene_done("varb1070_wanted_mission3complete") })
        t.expect("hunt.pos5_complete", t.var.await_server("varb1070_wanted_mission3complete", 1, 10))

        -- Read back which pool id got drawn for position 6.
        local pos6_id = read_pool_draw()
        t.check("hunt.pos6_drawn", pos6_id ~= nil, "position 6 pool id drawn = " .. tostring(pos6_id) .. " (" .. tostring(pos6_id and POOL[pos6_id][1]) .. ")")

        -- 16. Position 6 -- Solus summons a level 32 Black Knight (Wanted!)
        local pool6 = POOL[pos6_id]
        -- leave the mine the way we came: back along the tunnel, squeeze out through the hole, climb the cellar ladder
        t.exec("dk.goto_tunnel_end", t.player.goto_tile, 3221, 9618, 0)
        t.exec("dk.squeezeBackThroughHole", t.player.cross_trap, { loc = "lost_tribe_cavewall_hole_walldecor", at = { 3221, 9618, 0 },
            src = { 3221, 9618 }, dest = { 3219, 9618 }, attempts = 1 }) -- the hole goes 3219 <-> 3221 (RSProx index 4464 [3977])
        t.exec("dk.climbOutOfCellar", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
            at = { 3209, 9616, 0 }, src = { 3210, 9616 }, dest = { 3210, 3216, 0 } })
        -- out of the castle kitchen into the open courtyard (a doorway, no door: REACH 30)
        t.exec("dk.backInCourtyard", t.player.walk_to, 3222, 3218, 30)
        travel_to_pool("pos6", pos6_id, "mainland")
        do
            local zr, zt = t.world.tile()
            t.check("pos6.in_pool_zone", zr == "ok" and zt.x >= pool6[6] and zt.x <= pool6[7] and zt.z >= pool6[8] and zt.z <= pool6[9],
                "tile " .. (zr == "ok" and (zt.x .. "," .. zt.z .. "," .. zt.level) or tostring(zr)) .. " in " .. pool6[1] .. " zone x " .. pool6[6] .. "-" .. pool6[7] .. " z " .. pool6[8] .. "-" .. pool6[9])
        end
        to_scan_tile("pos6", pos6_id)
        t.exec("pos6.scan", t.player.inv_op, "wanted_crystal_ball", 1)
        -- The scene's last page has nothing to click and stays up through the fight [4451..4504].
        t.ticks(30)
        t.expect("pos6.knight_present", t.npc.await_present("wanted_summoned_black_knight", 6, 10))
        t.exec("pos6.attack", t.player.attack, "wanted_summoned_black_knight", 2, 20)
        local k6r, k6d = t.exec("pos6.knight_dead", t.npc.await_dead_engaged, 60, 8, { eat = { item = "shark", below = 50 } })
        do
            local low = tonumber(string.match(tostring(k6d), "lowest hp (%d+)/"))
            local _, sharks = t.inv.count("shark")
            t.check("pos6.margin", low ~= nil and low >= 25 and sharks ~= nil and sharks >= 1, "lowest hp " .. tostring(low) .. " (want >= 25), sharks left " .. tostring(sharks) .. " (want >= 1): " .. tostring(k6d))
        end
        t.await({ level = function() return t.chat.kind() == "objbox" or t.chat.kind() == "npc" end, note = "Savant's transfer" }, 10)
        t.exec("pos6.savant", converse, {}, { done = function() local _, v = t.var.server("varb1071_wanted_mission4"); return v == 1 end })
        t.expect("pos6.essence", t.inv.expect_has("cert_blankrune_high", 20))
        t.expect("hunt.pos6_complete", t.var.await_server("varb1071_wanted_mission4", 1, 10))
        do
            local chain_ok, chain_txt = true, ""
            -- A stop's bit goes back to 0 as it completes (RSProx index 4464 [3352]: mission13 1 -> 0).
            for _, vn in ipairs({ "varb1068_wanted_mission2complete", "varb1070_wanted_mission3complete", "varb1071_wanted_mission4" }) do
                local vr, vv = t.var.server(vn)
                chain_ok = chain_ok and vr == "ok" and vv == 1
                chain_txt = chain_txt .. vn .. "=" .. tostring(vv) .. " "
            end
            local sr, sv = t.quest.stage()
            chain_ok = chain_ok and sr == "ok" and sv == 9
            t.check("huntDownSolus", chain_ok, "scan chain read back from the server: " .. chain_txt .. "stage=" .. tostring(sv) .. " (want 9 = hunt); pool draws " .. tostring(POOL[pos2_id][1]) .. ", " .. tostring(POOL[pos4_id][1]) .. ", " .. tostring(POOL[pos6_id][1]))
        end

        -- leave whatever closed space position 6 drew, the way it was entered (positions 2 and 4
        -- need no exit: Solus teleports you out), then Varrock Teleport back to Aubury (every pool
        -- stop but 6, 7, 11, 12 and 19 is past a members' gate, on Karamja or in Morytania)
        leave_pool("pos6", pos6_id)
        do
            local ur, ut = t.world.tile()
            t.check("pos6.back_outside", ur == "ok" and ut.z < 9000 and ut.level == 0, "tile " .. tile_text(ur, ut) .. " on the surface, outside, before the teleport to Varrock")
        end
        varrock_teleport("goToEssenceMine.varrockTeleport")

        -- 17. Rune essence mine (fixed): Solus found, fought for real. Varrock square -> the street
        -- south of Aubury's shop (REACH 69). Its door fai_varrock_poor_door (closed leaf on the north
        -- wall of 3253,3398) stands open in the map (fai_varrock_poor_door_open at 3253,3399).
        t.exec("goto-aubury", t.player.goto_tile, 3253, 3396, 0)
        t.exec("aubury.shopDoorIn", t.player.pass_door, { closed = "fai_varrock_poor_door", open = "fai_varrock_poor_door_open",
            at = { 3253, 3398, 0 }, near = { 3253, 3397 }, far = { 3253, 3400 },
            far_ok = function(tile) return tile.z >= 3399 end, far_desc = "inside Aubury's shop, z >= 3399" })
        t.exec("goToEssenceMine", t.player.talk_to, "aubury", 4)
        -- The scene in the copy of the mine [4596..4707]: Solus turns attackable on "YOU DIE NOW!".
        t.expect("mine.solus_present", t.npc.await_present("wanted_solus_attackable", 15, 160))
        t.exec("mine.attack", t.player.attack, "wanted_solus_attackable", 2, 20)
        local msr, msd = t.exec("mine.solus_dead", t.npc.await_dead_engaged, 60, 8, { eat = { item = "shark", below = 50 } })
        do
            local low = tonumber(string.match(tostring(msd), "lowest hp (%d+)/"))
            local _, sharks = t.inv.count("shark")
            t.check("mine.margin", low ~= nil and low >= 25 and sharks ~= nil and sharks >= 1, "lowest hp " .. tostring(low) .. " (want >= 25), sharks left " .. tostring(sharks) .. " (want >= 1): " .. tostring(msd))
        end
        t.expect("quest.stage.final_battle", t.quest.expect_stage("final_battle"))
        -- Savant's pages, the fade and the way back to where the teleport came from [4951..4972].
        t.exec("mine.after", converse, {}, { open = 10, done = function() local _, v = t.var.server("varb542_cutscene_status"); return v == 0 and t.chat.kind() == "none" end })
        t.expect("trophy.held", t.inv.expect_has("wanted_solus_trophy", 1))
        t.exec("aubury.shopDoorOut", t.player.pass_door, { closed = "fai_varrock_poor_door", open = "fai_varrock_poor_door_open",
            at = { 3253, 3398, 0 }, near = { 3253, 3400 }, far = { 3253, 3396 },
            far_ok = function(tile) return tile.z <= 3397 end, far_desc = "on the street south of the shop, z <= 3397" })

        local snap_result, snap = t.skill.snapshot()
        t.step("reward.snapshot", snap_result == "ok" and "PASS" or "FAIL",
            "slayer xp=" .. tostring(snap and snap.slayer and snap.slayer.xp))

        -- 19. Show the proof to Sir Amik Varze (the street south of Aubury's -> the castle courtyard, REACH 421)
        castle_in("amik3")
        t.exec("amik3.talk", t.player.talk_to, "sir_amik_varze", 1)
        -- [5068..5109]: the scroll on "Well hand it over then, and let's see it!".
        t.exec("amik3.dialog", converse, {})
        -- t.quest.expect_complete() is not driven bare here: its own
        -- quest.journal row opens a FRESH journal right after scroll.close()
        -- with no settle between the two, and QUEST_AUTHORING.md documents
        -- this as a DETERMINISTIC (not flaky) channel degradation nothing a
        -- quest file can reach fixes ("Section 7's minimum shape is the
        -- intended way out, and this is the case it is FOR -- quest.journal
        -- is not itself required"). Measured here: quest.varp_complete,
        -- quest.scroll_title and quest.points all read correctly through
        -- expect_complete() (server=11, scroll title matched, qp +1) while
        -- quest.journal alone read complete=false/lines=1 on an otherwise
        -- fully-settled completion. rovingelves.lua/mourningsendpartii.lua
        -- hand-roll the same three rows for the identical reason; doing the
        -- same here rather than shipping a row measured to give bad content.
        t.settle()
        local varp_complete_result, varp_complete_value, varp_complete_kind, varp_complete_source = t.quest.stage()
        t.check("quest.varp_complete", varp_complete_result == "ok" and varp_complete_value == 11,
            "t.quest.stage() -> " .. tostring(varp_complete_result) .. " " .. tostring(varp_complete_value)
                .. " kind=" .. tostring(varp_complete_kind) .. " source=" .. tostring(varp_complete_source)
                .. " (want 11 = ^wanted_complete)")

        local scroll_title_result, scroll_title = t.scroll.title()
        -- gate.py requires the completion scroll photographed on THIS row
        -- (a shot, or the literal "[scroll already photographed:" marker
        -- quest.lua's own expect_complete builds when the frame is a
        -- duplicate of an earlier shot) -- same recipe as its bare verb.
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        t.check("quest.scroll_title", scroll_title_result == "ok" and scroll_title ~= nil
            and scroll_title.name ~= nil and scroll_title.name:find("Wanted!", 1, true) ~= nil,
            "scroll.title() after the hand-in -> " .. tostring(scroll_title_result) .. " name="
                .. tostring(scroll_title and scroll_title.name) .. scroll_shot_note)
        t.scroll.close()
        -- His explanation after the scroll [5119..5133].
        t.exec("amik3.after", converse, {})

        -- Rewards (wiki Wanted!): 1 quest point and 5,000 Slayer experience.
        local qp_after_result, qp_after = t.var.varp("varp101_qp")
        t.check("quest.points", qp_after_result == "ok" and qp_before_result == "ok"
            and qp_after == qp_before + 1,
            "qp (varp) " .. tostring(qp_before) .. " -> " .. tostring(qp_after) .. " (want +1)")

        t.check("reward.slayer_xp", t.skill.expect_gain("slayer", 5000, snap))
        t.finish(0)
    end,
}
