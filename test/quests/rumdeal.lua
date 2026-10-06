-- Rum Deal, end to end through the real client -- docs/quests/rum_deal.md
-- (pinned brief) + Quest Helper's RumDeal.java/SlugSteps.java (steps.put
-- 0-18 + the sub-helper's fish/deposit/lever ladder).
--
-- STATUS (b67 door-rule re-drive, round 2): end to end on the content of
-- seam matthew-mbp-m4-b67-seam1 (OSRS-Content 973a687d72). Round 1 stopped
-- at an honest t.blocked; the seam closed its three content gaps:
--
--   1. Travel. Pirate Pete's accept now ends "Wonderful! Just pick up your
--      diversion and we'll leave!" / "What diversion?", knocks the player
--      out and p_telejumps him into Captain Braindeath's room, 2144,5108,1
--      (deal_pete.rs2 [proc,deal_pete_knockout]; Transcript:Rum Deal,
--      Cutscene 1). That page (startOff.wake / startOff.cutscene) is the
--      run's travel row to the island; no goto goes there.
--   2. The island's stairs. rumdeal_maplink.dbrow (quest_rumdeal/configs)
--      joins each flight's foot outside floor 0 with its head on floor 1;
--      before it, ladders.rs2 [proc,climb] dropped the player into a sealed
--      pocket under floor 1 or onto a tile no walk left.
--   3. The Evil spirit's combat block, quest_rumdeal/configs/rumdeal.npc
--      (wiki Evil_spirit oldid 15199641): 90 hp, 170/146/100, crush; since
--      seam matthew-mbp-m4-b68-seam1 it swings the page's max hit 28
--      (deal_combat.rs2 [ai_opplayer2,deal_evil_spirit],
--      ^deal_evil_spirit_melee_maxhit). The fight is the wiki's: Protect
--      from Melee (Walkthrough "Evil spirits"; docs/quests/rum_deal.md
--      section 4) on the quest's own Prayer 47 requirement.
--
-- The two ladders use the default climb: the top-floor ladder
-- deal_ladder_up/deal_laddertop 2163,5092 L1<->L2 lands 2162,5092 (the
-- client's approach tile), the basement ladder deal_laddertop/deal_ladder_up
-- 2139,5105 L1<->L0 lands 2138,5105.
--
-- WALLS (door rule, owner 2026-10-03/05). Every goto departs from and lands on
-- open ground outside; every gate, trapdoor, barrier, stair and ladder is
-- pressed on every visit. Checked with test/quests/orchestrator/
-- matthew-mbp-m4/reports/sample_tools/{reach,comp,locs_near}.py (doors
-- closed, --root this worktree):
--   * Lumbridge (fixture 3206,3233,0) -> the Varrock members' gate's west
--     side 3318,3468: REACH closed-doors len 389 (the first goto).
--   * Into Morytania only through fai_varrock_member_gatel 3319,3468 (reach
--     3318,3468 -> 3405,3506 NEEDS-DOOR via it), then the way Priest in
--     Peril opens it: the Paterdomus trapdoor 3405,3507 (down to
--     3405,9906), pip_underground_door1 3405,9895 and door2 3431,9897,
--     Drezel's advice (60 -> 61) and the holy barrier 3440,9886 (p_telejump
--     to 3423,3485, mausoleum_interactions.rs2:26-31) -- ghostsahoy.lua's
--     route, green on v3.
--   * The holy barrier's landing 3423,3485 -> Pirate Pete 3680,3536 (north
--     of Port Phasmatys' barrier, outside the town): REACH closed-doors len
--     314 at margins 30/80/160.
--   * On the island: floor 1 is one walk (575 tiles) from Braindeath's
--     room to Davey, the brewing control, the tap and every stair head and
--     ladder; the top floor and the basement are ladders only; outside
--     floor 0 is the stairs only; the lake is north of deal_gate_closed
--     2120,5098 (stored on raw level 1 over a bridge column, so pass_door
--     names loc_level = 1; reach 2120,5097 -> 2134,5161 NEEDS-DOOR via it),
--     pressed both ways. No goto on the island.
--
-- Rewards (read off deal_shared.rs2 [proc,deal_quest_complete]: stat_advance
-- fishing/prayer/farming ^deal_reward_*_xp = 70000 tenths = 7000 XP each,
-- RumDeal.java's ExperienceReward list and the wiki's Rewards section; the
-- retained holy wrench) are asserted after expect_complete.
-- "Access to Braindeath Island" has no varp/item/xp signal to read back.
--
-- Sanctioned setup cheats only (::clearinv/::give/::setlevel/::complete):
-- Zogre Flesh Eaters and Priest in Peril are hard prerequisites
-- (deal_shared.rs2 `[proc,deal_meets_requirements]`) staged with `::complete
-- <dbrow>`; the Wolfbane dagger is Priest in Peril's own reward (::complete
-- grants no items) and Drezel's barrier advice needs it held
-- (mausoleum_drezel.rs2:29-34). No ::setvar on any deal_ var in run().
--
-- Combat level: Attack/Strength/Defence/Hitpoints 99 are staged for the
-- island fights. Prayer 47 is the quest's requirement (wiki infobox,
-- ~deal_meets_requirements), not a fight stage; it unlocks Protect from
-- Melee (43). No prayer potion is staged: the guide lists none, and 47
-- points last the spirit fight (killSpirit.prayerLasted asserts it). No dialogue this run passes branches on the combat level
-- (deal_pete.rs2 reads ~deal_meets_requirements only; Drezel's
-- [label,drezel_access_holy_barrier] reads nothing).

return {
    id = "rumdeal",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_zogreflesheaters",
        "::complete quest_priestinperil",
        "::setlevel fishing 50",
        "::setlevel prayer 47",
        "::setlevel crafting 42",
        "::setlevel slayer 42",
        "::setlevel farming 40",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give dagger_wolfbane 1", -- Priest in Peril's own reward; Drezel's holy-barrier advice needs it held (mausoleum_drezel.rs2:29-34)
        -- getItemRequirements(): combatGear, dibber, rake, slayerGloves --
        -- all bring-along, none of them this quest's own gathered work
        -- (docs/quests/rum_deal.md section 7: the basement cupboard that
        -- would hand out the rake/dibber in real OldSchool is inert in
        -- this port, so there is no in-game source for them at all).
        "::give rake 1",
        "::give dibber 1",
        "::give deal_slayer_gloves 1",
        "::give shark 10", -- food for the spirit and the spiders; under Protect from Melee neither ate one (b68 r2), unprotected the spirit ate all 10 (b68-seam1)
        "::give rune_scimitar 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp600_deal_quest",
            constants = {
                not_started = 0, started = 1, growing_blindweed = 2,
                deliver_blindweed = 3, hopper_blindweed = 4,
                told_get_water = 5, get_water = 6,
                told_get_sluglings = 7, get_sluglings = 8,
                told_kill_spirit = 9, kill_spirit = 10,
                told_kill_spider = 11, kill_spider = 12,
                told_get_swill = 13, get_swill = 14,
                return_to_finish = 15, complete = 19,
            },
            display = "Rum Deal",
            points = 2,
        })
        t.ticks(3)
        t.expect("bind.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- Into Morytania (WALLS in the header): the Varrock members' gate,
        -- the Paterdomus trapdoor, the two mausoleum gates, Drezel's advice
        -- and the holy barrier.
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
        -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:145-154,
        -- LostCity drezel.rs2:138-147): 60 -> 61, the holy barrier opens.
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

        -- Leg 1: Pirate Pete, north east of the Ectofuntus (deal_pete.rs2),
        -- outside Port Phasmatys' walls: an overland hop from the barrier's
        -- landing (REACH closed-doors len 314), onto the open tile south of
        -- him (his own tile and the Row boat north of him are not stood on).
        t.exec("pete.goto", t.player.goto_tile, 3680, 3536, 0)
        t.exec("pete.talk", t.player.talk_to, "deal_pete")
        t.exec("pete.dialogue", t.chat.play, {
            "player:Who are you",
            "npc:Pirate Pete",
            "npc:Now here's my plan",
            "npc:myself.",
            "player:And you want me",
            "npc:That's the spirit",
            "npc:your trouble.",
            "options",
            "choose:Keep your money -- I'll help for free.",
            "player:Keep it. I'll help you out for free.",
            "npc:Wonderful! Just pick up your diversion",
            "player:What diversion?",
        })
        -- startOff (the guide's step 1 -> 2): Pete knocks the player out
        -- with a bottle and rows him to Captain Braindeath (deal_pete.rs2
        -- [proc,deal_pete_knockout], p_telejump; Transcript:Rum Deal
        -- "Setting out" / Cutscene 1): the player wakes in the captain's
        -- room, 2144,5108,1, the stage already started. This page is the
        -- travel row: no goto reaches the island.
        t.exec("startOff.wake", t.await, {
            level = function()
                local _, tile = t.world.tile()
                return tile ~= nil and tile.x == 2144 and tile.z == 5108 and tile.level == 1
                    and t.chat.kind() ~= "none"
            end,
            note = "startOff: knocked out and rowed to 2144,5108,1, the narration page up",
        }, 15)
        t.exec("startOff.cutscene", t.chat.play, { "mesbox:knocks you out with a bottle" })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- =================================================================
        -- The island. Floor 1 is walked; the top floor and the basement by
        -- their ladders; outside floor 0 by the stairs (rumdeal_maplink.dbrow);
        -- the lake past deal_gate_closed, both ways.
        -- =================================================================

        -- A fight's margin row: the lowest hitpoints the eater READ inside
        -- the attack press and the kill wait (their "lowest hp x/y" tags), at
        -- least a quarter of 99, AND sharks left (wanted.lua's shape).
        local EAT = { item = "shark", below = 70, op = 1 }
        local function margin_row(name, fight, attack_detail, dead_detail)
            local low_a = tonumber(string.match(tostring(attack_detail), "lowest hp (%d+)/"))
            local low_d = tonumber(string.match(tostring(dead_detail), "lowest hp (%d+)/"))
            local low = low_a
            if low_d ~= nil and (low == nil or low_d < low) then
                low = low_d
            end
            local fr, food = t.inv.count("shark")
            t.check(name, low ~= nil and low >= 25 and fr == "ok" and food ~= nil and food >= 1,
                fight .. ": lowest hp " .. tostring(low) .. "/99 (attack press " .. tostring(low_a)
                    .. ", kill wait " .. tostring(low_d) .. "), sharks left " .. tostring(food)
                    .. " (" .. tostring(fr) .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        end

        -- Floor 1 <-> the top floor: deal_ladder_up / deal_laddertop at
        -- 2163,5092; the click walks to 2162,5092 and the default climb
        -- lands there one plane up or down (b67 probe build/quest_gate/rumdeal_probe).
        local function up_to_top(name)
            t.exec(name .. ".walk", t.player.walk_to, 2162, 5092, 40)
            t.exec(name, t.player.climb, { loc = "deal_ladder_up", op = 1, op_name = "Climb-up",
                at = { 2163, 5092, 1 }, src = { 2162, 5092 }, dest = { 2162, 5092, 2 } })
        end
        local function down_from_top(name)
            t.exec(name, t.player.climb, { loc = "deal_laddertop", op = 1, op_name = "Climb-down",
                at = { 2163, 5092, 2 }, src = { 2162, 5092 }, dest = { 2162, 5092, 1 } })
        end
        -- Floor 1 <-> outside floor 0 by the main stairs 2149,5088 (the
        -- guide's goDownstairs / goUpFromBottom), by their maplink rows
        -- (quest_rumdeal/configs/rumdeal_maplink.dbrow): Climb-down from
        -- 2149,5089,1 lands 2149,5087,0 (rumdeal_maplink_down_37_33);
        -- Climb-up from 2150,5087,0 lands 2150,5089,1 (_up_38_31), and from
        -- the flight's other approach tiles on 2149 or 2150,5089,1.
        local function stairs_down(name)
            t.exec(name .. ".walk", t.player.walk_to, 2149, 5089, 40)
            t.exec(name, t.player.climb, { loc = "deal_stairs_top", op = 1, op_name = "Climb-down",
                at = { 2149, 5088, 1 }, src = { 2149, 5089 }, dest = { 2149, 5087, 0 },
                landed_ok = function(tile) return tile.z <= 5087 end,
                landed_desc = "outside, south of the stairs' foot (z <= 5087)" })
        end
        local function stairs_up(name)
            t.exec(name .. ".walk", t.player.walk_to, 2150, 5087, 80)
            t.exec(name, t.player.climb, { loc = "deal_stairs_bottom", op = 1, op_name = "Climb-up",
                at = { 2149, 5088, 0 }, dest = { 2150, 5089, 1 }, slack = 1,
                landed_ok = function(tile) return tile.z >= 5089 end,
                landed_desc = "the first floor, north of the stairs' head (z >= 5089)" })
        end
        local function to_braindeath(name)
            t.exec(name, t.player.walk_to, 2144, 5107, 40)
        end

        -- Arrival: the captain's room (startOff.wake) walks to Braindeath.
        to_braindeath("walk-talkToBraindeath")
        t.exec("braindeath.seed", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.seed.dialogue", t.chat.play, {
            "player:Pete sent me",
            "npc:Aye, that I do",
            "npc:Blindweed",
            "mesbox:Blindweed seed",
        })
        t.expect("braindeath.growing", t.quest.expect_stage("growing_blindweed"))
        t.expect("inv.seed", t.inv.expect_has("deal_blindweed_seed", 1))

        -- Leg 3: the real Blindweed patch puzzle -- rake 3x, plant with the
        -- dibber, wait for the settimer'd grow, pick (deal_farming.rs2).
        -- deal_blindweed is 2x2 at 2162-2163,5069-5070: stand west of it.
        local patch = t.player.by_symbol("loc", "deal_blindweed")
        stairs_down("goDownstairs")
        t.exec("walk-rakePatch", t.player.walk_to, 2161, 5070, 40)
        t.exec("patch.rake1", t.player.use_on, "rake", patch)
        t.exec("patch.rake2", t.player.use_on, "rake", patch)
        t.exec("patch.rake3", t.player.use_on, "rake", patch)
        t.expect("patch.raked", t.var.expect("varb1366_deal_farming", 3))
        t.exec("patch.plant", t.player.use_on, "deal_blindweed_seed", patch)
        t.expect("patch.planted", t.var.expect("varb1366_deal_farming", 4))
        t.exec("waitForGrowth", t.var.await, "varb1366_deal_farming", 5, 520)
        t.exec("patch.pick", t.player.click_loc, "deal_blindweed", 1)
        t.expect("braindeath.deliver", t.quest.expect_stage("deliver_blindweed"))
        t.exec("inv.blindweed", t.inv.await, "deal_blindweed", 1, 10)

        -- Leg 4: hand the Blindweed to Braindeath, drop it in the hopper.
        stairs_up("goUpStairsWithPlant")
        to_braindeath("walk-talkToBraindeathWithPlant")
        t.exec("braindeath.deliver.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.deliver.dialogue", t.chat.play, {
            "player:I've got the Blindweed",
            "npc:top floor",
        })
        t.expect("braindeath.hopper_stage", t.quest.expect_stage("hopper_blindweed"))
        local hopper = t.player.by_symbol("loc", "deal_hopper")
        up_to_top("climbUpToDropPlant")
        t.exec("hopper.blindweed", t.player.use_on, "deal_blindweed", hopper)
        t.expect("hopper.told_water", t.quest.expect_stage("told_get_water"))
        down_from_top("goDownFromDropPlant")

        -- Leg 5: stagnant water -- Braindeath grants a bucket, the west
        -- stairs, the gate, fill it, pour it in (deal_water_hopper.rs2).
        to_braindeath("walk-talkToBraindeathAfterPlant")
        t.exec("braindeath.water.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.water.dialogue", t.chat.play, {
            "player:in the hopper",
            "npc:stagnant water",
        })
        t.expect("quest.stage.get_water", t.quest.expect_stage("get_water"))
        -- Braindeath hands over the bucket (deal_braindeath.rs2 told_get_water).
        t.expect("inv.bucket", t.inv.expect_has("bucket_empty", 1))
        -- goDownForWater: the guide's west stairs 2137,5088: Climb-down from
        -- 2137,5089,1 lands 2137,5087,0 (rumdeal_maplink_down_25_33).
        t.exec("goDownForWater.walk", t.player.walk_to, 2137, 5089, 40)
        t.exec("goDownForWater", t.player.climb, { loc = "deal_stairs_top", op = 1, op_name = "Climb-down",
            at = { 2137, 5088, 1 }, src = { 2137, 5089 }, dest = { 2137, 5087, 0 },
            landed_ok = function(tile) return tile.z <= 5087 end,
            landed_desc = "outside, south of the west stairs' foot (z <= 5087)" })
        t.exec("walk-openGate", t.player.walk_to, 2120, 5097, 60)
        local GATE = { closed = "deal_gate_closed", open = "deal_gate_open", at = { 2120, 5098, 0 }, loc_level = 1 }
        t.exec("openGate", t.player.pass_door, { closed = GATE.closed, open = GATE.open, at = GATE.at,
            loc_level = GATE.loc_level, near = { 2120, 5097 }, far = { 2120, 5099 } })
        t.exec("walk-north_island", t.player.walk_route, { { 2120, 5099 }, { 2120, 5107 }, { 2120, 5115 },
            { 2120, 5123 }, { 2121, 5130 }, { 2122, 5137 }, { 2128, 5139 }, { 2135, 5140 }, { 2138, 5145 },
            { 2143, 5148 }, { 2150, 5149 }, { 2153, 5154 }, { 2147, 5156 } })
        -- the route's flood path (reach.Area.bfs, gate north side -> 2134,5161) winds east round the
        -- lake's south shore; use_on walks the last stretch to the water's edge.
        local stagnant = t.player.by_symbol("loc", "deal_stagnant")
        t.exec("useBucketOnWater", t.player.use_on, "bucket_empty", stagnant)
        t.expect("inv.stagnant", t.inv.expect_has("deal_stagnant_bucket", 1))
        -- The bucket's press walks the lake's north shore to a copy of
        -- deal_stagnant (seam run b67s1_rumdeal_unblocked: it ended on
        -- 2134,5161 or 2132,5165), so the way back starts there and goes
        -- round the lake's east side (reach.Area.bfs, len 136).
        t.exec("walk-south_island", t.player.walk_route, { { 2131, 5166 }, { 2136, 5169 }, { 2142, 5167 },
            { 2145, 5162 }, { 2151, 5160 }, { 2153, 5154 }, { 2150, 5149 },
            { 2143, 5148 }, { 2138, 5145 }, { 2135, 5140 }, { 2128, 5139 }, { 2122, 5137 }, { 2121, 5130 },
            { 2120, 5123 }, { 2120, 5115 }, { 2120, 5107 }, { 2120, 5099 } })
        t.exec("openGate.back", t.player.pass_door, { closed = GATE.closed, open = GATE.open, at = GATE.at,
            loc_level = GATE.loc_level, near = { 2120, 5099 }, far = { 2120, 5097 } })
        stairs_up("goUpWithWater")
        up_to_top("goUpToDropWater")
        t.exec("hopper.water", t.player.use_on, "deal_stagnant_bucket", hopper)
        t.expect("hopper.told_sluglings", t.quest.expect_stage("told_get_sluglings"))
        down_from_top("goDownFromTopAfterDropWater")

        -- Leg 6: five sluglings, fished from the deal_squid spot off the
        -- east coast (2173,5074, in the water: stand on 2172,5074), into the
        -- pressure barrel, lever pulled (deal_sluglings.rs2 + SlugSteps.java).
        to_braindeath("walk-talkToBraindeathAfterWater")
        t.exec("braindeath.sluglings.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.sluglings.dialogue", t.chat.play, {
            "player:water's in the hopper",
            "npc:sluglings",
        })
        t.expect("braindeath.get_sluglings", t.quest.expect_stage("get_sluglings"))
        -- ... and the fishbowl and net (told_get_sluglings).
        t.expect("inv.fishbowl_net", t.inv.expect_has("fishbowl_net", 1))
        stairs_down("goDownToSluglings")
        t.exec("walk-fish5Slugs", t.player.walk_to, 2172, 5074, 40)
        for i = 1, 5 do
            t.exec("slugling.fish" .. i, t.player.talk_to, "deal_squid")
        end
        t.expect("inv.sluglings", t.inv.expect_has("deal_slugling", 5))
        stairs_up("goUpF1ToPressure")
        up_to_top("goUpToF2ToPressure")
        local pressure = t.player.by_symbol("loc", "deal_pressure")
        for i = 1, 5 do
            t.exec("slugling.deposit" .. i, t.player.use_on, "deal_slugling", pressure)
        end
        t.expect("barrel.full", t.var.expect("varb1354_deal_barrel", 5))
        t.exec("lever.pull", t.player.click_loc, "deal_multi_lever", 1)
        t.expect("lever.told_spirit", t.quest.expect_stage("told_kill_spirit"))
        down_from_top("goDownAfterSlugs")

        -- Leg 7: the evil spirit -- Braindeath's wrench, Davey's blessing
        -- (all on floor 1, walked), then the fight.
        to_braindeath("walk-talkToBraindeathAfterSlugs")
        t.exec("braindeath.spirit.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.spirit.dialogue", t.chat.play, {
            "player:barrel's full",
            "npc:evil spirit",
        })
        t.expect("braindeath.kill_spirit", t.quest.expect_stage("kill_spirit"))
        t.expect("inv.wrench", t.inv.expect_has("deal_wrench", 1))
        t.exec("walk-talkToDavey", t.player.walk_to, 2132, 5099, 40)
        t.exec("davey.talk", t.player.talk_to, "deal_davey")
        t.exec("davey.dialogue", t.chat.play, {
            "player:might be able to help",
            "npc:possessed",
            "mesbox:blesses the wrench",
        })
        t.expect("inv.wrench_blessed", t.inv.expect_has("deal_wrench_blessed", 1))
        -- The guide's combat gear for the spirit and the spiders.
        t.exec("scimitar.equip", t.player.equip, "rune_scimitar")
        -- deal_multicontrol is 2x2 at 2143-2144,5100-5101: stand east of it.
        local multicontrol = t.player.by_symbol("loc", "deal_multicontrol")
        t.exec("walk-useWrenchOnControl", t.player.walk_to, 2145, 5101, 40)
        -- Protect from Melee goes up BEFORE the wrench: the spirit spawns
        -- on the control aggressive, and a protection prayer is read on its
        -- attack animation tick, so it must already be lit when it first
        -- swings. Points first: the quest's Prayer 47 must be there to spend.
        local bp_r, bp_d, bp = t.prayer.points()
        t.check("killSpirit.prayerPoints",
            bp_r == "ok" and type(bp) == "table" and bp.base_level >= 43 and bp.level >= 40,
            tostring(bp_d) .. " (Protect from Melee needs Prayer 43; want >= 40 points to spend)")
        t.exec("killSpirit.protectFromMelee", t.prayer.set, "protectfrommelee", true)
        t.exec("control.wrench", t.player.use_on, "deal_wrench_blessed", multicontrol)
        -- `~mesbox(...)` suspends the calling script (trap 22) --
        -- `~deal_spawn_evilspirit` is the line AFTER the mesbox call in
        -- `[oplocu,deal_multicontrol]`, so it does not run until this
        -- dialogue closes.
        t.exec("control.mesbox_continue", t.chat.play, { "mesbox:*" })
        local spirit_present = t.npc.await_present("deal_evil_spirit", 10, 10)
        t.check("spirit.spawned", spirit_present == "ok",
            "npc.await_present(deal_evil_spirit,10,10) -> " .. tostring(spirit_present))
        -- The spirit's block: quest_rumdeal/configs/rumdeal.npc (wiki
        -- Evil_spirit oldid 15199641, cache all.npc agrees): 90 hp,
        -- 170/146/100, crush, speed 4, aggressive; it swings the page's max
        -- hit 28 (deal_combat.rs2 [ai_opplayer2,deal_evil_spirit], seam
        -- matthew-mbp-m4-b68-seam1). Fought with the guide's combat gear (the
        -- rune scimitar), Protect from Melee (the wiki: it "negates its
        -- attacks") and sharks eaten below 70 inside the press and the wait.
        -- Without the prayer it ate all 10 sharks (b68-seam1 step 5).
        local _, sad = t.exec("killSpirit.attack", t.player.attack, "deal_evil_spirit", 2, 20, { eat = EAT })
        local _, sdd = t.exec("killSpirit", t.npc.await_dead_engaged, 120, 8, { eat = EAT })
        margin_row("killSpirit.margin", "Evil spirit (level 150)", sad, sdd)
        -- The prayer lasted the fight: still lit, points left (prayer will
        -- not regenerate; the guide stages no prayer potion).
        local sl_r, sl_d, sl_set = t.prayer.read()
        local sp_r, sp_d, sp = t.prayer.points()
        t.check("killSpirit.prayerLasted",
            sl_r == "ok" and type(sl_set) == "table" and sl_set.protectfrommelee == true
                and sp_r == "ok" and type(sp) == "table" and sp.level >= 1,
            "after the kill: " .. tostring(sp_d) .. "; " .. tostring(sl_d))
        t.exec("killSpirit.protectFromMeleeOff", t.prayer.set, "protectfrommelee", false)
        t.ticks(6)
        t.expect("spirit.told_spider", t.quest.expect_stage("told_kill_spider"))

        -- Leg 8: the fever spider basement, by its ladder both ways -- a REAL
        -- kill (deal_fever_spiders1: combat_stats.generated.npc:4448, 40 hp),
        -- carcass carried up to the hopper.
        to_braindeath("walk-talkToBraindeathAfterSpirit")
        t.exec("braindeath.spider.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.spider.dialogue", t.chat.play, {
            "player:evil spirit's gone",
            "npc:fever spiders",
        })
        t.expect("braindeath.kill_spider", t.quest.expect_stage("kill_spider"))
        t.exec("goDownToSpiders.walk", t.player.walk_to, 2138, 5105, 40)
        t.exec("goDownToSpiders", t.player.climb, { loc = "deal_laddertop", op = 1, op_name = "Climb-down",
            at = { 2139, 5105, 1 }, src = { 2138, 5105 }, dest = { 2138, 5105, 0 } })
        -- Slayer gloves worn: slayer_gear.rs2's slayer_needs_gloves gates
        -- on npc_type = deal_fever_spiders1, and slayer_specials.rs2's
        -- slayer_on_npc_hit_player only forces the extra 12.5%-of-Hitpoints
        -- hit + ~apply_disease when the gloves are NOT worn.
        t.exec("gloves.equip", t.player.equip, "deal_slayer_gloves")
        local _, pad = t.exec("spider.attack", t.player.attack, "deal_fever_spiders1", 2, 30, { eat = EAT })
        local _, pdd = t.exec("killSpider", t.npc.await_dead_engaged, 80, 10, { eat = EAT })
        margin_row("killSpider.margin", "Fever spider (level 49)", pad, pdd)
        local carcass_click_result = t.player.click_obj("deal_spider_body")
        local carcass_count_result, carcass_count = t.inv.count("deal_spider_body")
        t.check("pickUpCarcass",
            carcass_click_result == "ok" and carcass_count_result == "ok" and carcass_count == 1,
            "click_obj deal_spider_body -> " .. tostring(carcass_click_result)
                .. "; deal_spider_body count " .. tostring(carcass_count_result)
                .. " " .. tostring(carcass_count))
        t.exec("goUpFromSpidersWithCorpse", t.player.climb, { loc = "deal_ladder_up", op = 1, op_name = "Climb-up",
            at = { 2139, 5105, 0 }, src = { 2138, 5105 }, dest = { 2138, 5105, 1 } })
        up_to_top("goUpToDropSpider")
        t.exec("hopper.spider", t.player.use_on, "deal_spider_body", hopper)
        t.expect("hopper.told_swill", t.quest.expect_stage("told_get_swill"))
        down_from_top("goDownAfterSpider")

        -- Leg 9: the finished mash -- fill a bucket from the output tap
        -- (floor 1, walked), Donnie's taste test outside (the stairs).
        to_braindeath("walk-talkToBraindeathAfterSpider")
        t.exec("braindeath.swill.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.swill.dialogue", t.chat.play, {
            "player:spider's in the hopper",
            "npc:mash done",
        })
        t.expect("braindeath.get_swill", t.quest.expect_stage("get_swill"))
        -- ... and a second bucket for the swill (told_get_swill: the first
        -- one went into the hopper with the stagnant water).
        t.expect("inv.bucket_for_swill", t.inv.expect_has("bucket_empty", 1))
        local tap = t.player.by_symbol("loc", "deal_brewvat_tap")
        t.exec("useBucketOnTap", t.player.use_on, "bucket_empty", tap)
        t.expect("inv.swill", t.inv.expect_has("deal_bucket_swill", 1))
        stairs_down("goDownToDonnie")
        t.exec("walk-talkToDonnie", t.player.walk_to, 2151, 5079, 40)
        t.exec("donnie.talk", t.player.talk_to, "deal_captian_donnie")
        t.exec("donnie.dialogue", t.chat.play, {
            "player:Here, try this",
            "npc:rough",
            "mesbox:approval",
        })
        t.expect("donnie.return_to_finish", t.quest.expect_stage("return_to_finish"))

        -- Leg 10: finish with Braindeath -- ~deal_quest_complete grants
        -- 7000 Fishing/Prayer/Farming XP each.
        stairs_up("goUpToBraindeathToFinish")
        to_braindeath("walk-talkToBraindeathToFinish")
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.step("rumdeal.xp_before_read", xp_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))
        t.exec("braindeath.finish.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.finish.dialogue", t.chat.play, {
            "player:taste of the swill",
            "npc:Then it's done",
            "npc:work fixing that control.",
        })
        t.quest.expect_complete()

        -- Every reward Quest Helper lists (getExperienceRewards +
        -- getItemRewards): three literal XP grants and the retained holy
        -- wrench.
        t.exec("reward.fishing_xp", t.skill.expect_gain, "fishing", 7000, xp_snapshot)
        t.exec("reward.prayer_xp", t.skill.expect_gain, "prayer", 7000, xp_snapshot)
        t.exec("reward.farming_xp", t.skill.expect_gain, "farming", 7000, xp_snapshot)
        t.expect("reward.holy_wrench", t.inv.expect_has("deal_wrench_blessed", 1))

        t.finish(0)
    end,
}
