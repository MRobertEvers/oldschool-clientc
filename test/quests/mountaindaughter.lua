-- Mountain Daughter, driven end to end against
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_mountaindaughter/.
-- docs/quests/mountain_daughter.md is the pinned brief (wiki oldids + the
-- port's own breakpoint derivation); this file follows its stage table.
--
-- Door rule (b60 re-drive): every closed space is entered and left by its
-- own click on every visit. The Mountain Camp (a 998-tile walking component
-- with NO on-foot way out: reach.py 2717,3667 -> 2766,3676 UNREACHABLE at
-- margins 30/80/160) is entered over the rockslide and left by a real
-- Camelot Teleport; Hamal's tent (mdaughter_tent_door 2805,3672) and the
-- Ancient Rock's tent (mdaughter_rocktent_door 2799,3665) are entered and
-- left through their doors; the lake islands are reached only by the tree,
-- the pole and the plank; the Kendal's cave only by its entrance and exit;
-- the cave path's two Dead trees are stepped over both ways. Long trips are
-- a Camelot Teleport plus an overland goto between open tiles.
--
-- CONTENT BUG (b60, why this file ends at t.blocked): the rockslide
-- (mountaindaughter_camp.rs2:32-34 [oploc1,mdaughter_rockslide]) p_teleports
-- the player to ^mdq_camp_enter_coord = 0_43_57_16_20 = 2768,3668
-- (quest_mountaindaughter.constant:198), a cliff tile walled in on all four
-- sides: walk_to 2768,3663 / 2761,3660 stall there (probe
-- build/quest_gate/mountaindaughter_probe1). The rope on the boulder
-- (camp.rs2:21-30) is mes-only. So no player can walk into the camp, and the
-- rockslide (it always lands on that coord) is no way out of it either. The
-- Ancient Rock's tent door (mdaughter_rocktent_door/_doorl, op1 Go-through)
-- has no [oploc1] handler and no door category, so that tent cannot be
-- entered. Everything below the block is the honest route, proven in a
-- scratch copy that bypassed only those two content bugs
-- (build/orchestrator/fix_b60/mountaindaughter.progress.md).
--
-- Content notes carried from the brief's "found while pinning" section:
--  * The pole (mdaughter_stick) and plank (woodplank) are never consumed by
--    mountaindaughter_camp.rs2's lake-crossing triggers (oplocu on
--    mdaughter_polerocks/mdaughter_flatstone1 only check last_useitem, never
--    inv_del), so once carried they serve every later crossing.
--  * mdaughter_flatstone2's oploc1 (no item) always succeeds and always
--    lands mdq_shore_return_coord -- the "without a plank" return leg the
--    guide calls noPlankRocksReturn/plankRocksReturn is simplified in the
--    port to the same outcome either way (constant file, section 7).
--  * seam23: Asleif's spirit has no npc and no chathead -- her every line is
--    ~mesbox (the wiki Transcript's {{tbox|'...'}} form), never
--    ~chatnpc/~chatnpc_anim (mountaindaughter_spirit.rs2's own header note).
--  * The 5-muddy-rock requirement only has FOUR ground spawns in
--    areas/world/configs/m43_57.spawn, so a fifth needs one of the four to
--    respawn -- collectRocks below loops passes over the four spots.
--  * mdaughter_burialmound (5862) is a multiloc CHILD with no map placement
--    of its own; the scene holds its base wrapper mdaughter_multimound
--    (5861, maps/m43_57.jl2:1001) -- click_loc's "base" resolve rule
--    (docs/QUEST_AUTHORING.md trap 20) reaches it through the child symbol.
--  * seam27: the clump of rocks and the first flat stone are [aploc1]/[aplocu]
--    triggers with p_aprange(2); the landings are island1 2772,3684, island2
--    2774,3689, island3 2778,3691 and the shore 2766,3676 (constants
--    mdq_island1/2/3_coord, mdq_shore_return_coord), each graded on its tile.
--    The cave path's two Dead trees (2802,3703 and 2807,3703) are chopped
--    with the axe and their stumps stepped over (mountaindaughter_kendal.rs2),
--    both ways.

return {
    id = "mountaindaughter",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel agility 20",
        "::setlevel magic 45",
        "::give rope 1",
        "::give bronze_pickaxe 1",
        "::give bronze_axe 1",
        "::give woodplank 1",
        "::give leather_gloves 1",
        "::give rune_scimitar 1",
        "::give lobster 5",
        "::give airrune 25",
        "::give lawrune 5",
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb260_mdaughter_quest_var",
            constants = {
                not_started = 0,
                started = 10,
                spirit_heard = 20,
                ready_for_kendal = 30,
                kendal_found = 40,
                kendal_killed = 50,
                corpse_given = 60,
                complete = 70,
            },
            row = "quest_mountaindaughter",
            display = "Mountain Daughter",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local loc_boulder = t.player.by_symbol("loc", "mdaughter_cliff_boulder")
        local loc_lake_tree = t.player.by_symbol("loc", "mdaughter_lake_tree")
        local loc_polerocks = t.player.by_symbol("loc", "mdaughter_polerocks")
        local loc_flatstone1 = t.player.by_symbol("loc", "mdaughter_flatstone1")
        local loc_flatstone2 = t.player.by_symbol("loc", "mdaughter_flatstone2")

        -- Camelot Teleport: magic_spells.dbrow:131-136 (level 45, 5 air + 1
        -- law, tele_coord 0_43_54_5_22 = 2757,3478). Five casts staged.
        local CAMELOT_RUNES = { { "airrune", 5 }, { "lawrune", 1 } }
        local function camelot(name, where)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = CAMELOT_RUNES, where = where })
        end

        local function tile_text(tt)
            if type(tt) ~= "table" then return "?" end
            return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
        end

        -- A teleport-on-op crossing (the tree, the pole, the planks, the
        -- cave) graded on the exact landing tile the content's coord names.
        local function landed(name, x, z, why)
            t.ticks(2)
            local r, tt = t.world.tile()
            t.check(name, r == "ok" and tt.x == x and tt.z == z and tt.level == 0,
                "t.world.tile() -> " .. tostring(r) .. " " .. tile_text(tt) .. " (want " .. x .. "," .. z .. ",0: " .. why .. ")")
        end

        -- Into the Mountain Camp over the rockslide (2760,3658, 2x2), from the
        -- open ground south of it, then a walk to an open camp tile
        -- (2768,3663, inside the 998-tile camp component). Graded on the
        -- tiles: outside (z <= 3657) before the press, on 2768,3663 after
        -- the walk.
        local CAMP_IN = { 2768, 3663 }
        local function enter_camp(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2760, 3656, 0)
            local _, before = t.world.tile()
            local pr, pd = t.player.click_loc("mdaughter_rockslide", 1)
            t.ticks(2)
            local _, land = t.world.tile()
            local wr, wd = t.player.walk_to(CAMP_IN[1], CAMP_IN[2], 30)
            local _, after = t.world.tile()
            local ok = type(before) == "table" and before.z <= 3657 and before.level == 0
                and type(after) == "table" and after.x == CAMP_IN[1] and after.z == CAMP_IN[2] and after.level == 0
            return ok, "south of the rockslide at " .. tile_text(before) .. "; Climb-over -> " .. tostring(pr) .. " "
                .. tostring(pd) .. "; landed " .. tile_text(land) .. "; walk_to " .. CAMP_IN[1] .. "," .. CAMP_IN[2]
                .. " -> " .. tostring(wr) .. " " .. tostring(wd) .. "; now " .. tile_text(after)
        end

        -- Hamal's tent: the closed double door's left leaf at 2805,3672
        -- (doubledoors.loc:1006-1020), pressed or found standing open, on
        -- every visit, in and out.
        local function hamal_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2803, 3672, 0)
            t.exec(name .. ".tentDoorIn", t.player.pass_door, { closed = "mdaughter_tent_door",
                open = "mdaughter_tent_door_open", at = { 2805, 3672, 0 }, near = { 2804, 3672 }, far = { 2806, 3671 } })
        end
        local function hamal_out(name)
            t.exec(name .. ".tentDoorOut", t.player.pass_door, { closed = "mdaughter_tent_door",
                open = "mdaughter_tent_door_open", at = { 2805, 3672, 0 }, near = { 2806, 3671 }, far = { 2803, 3672 } })
        end

        -- The lake: the muddied tree drops you on island1, the pole vaults
        -- to island2, the plank crosses to island3 (the pool island).
        local function cross_lake(sfx)
            t.exec("goto-tree" .. sfx, t.player.goto_tile, 2772, 3678, 0)
            t.exec("climbTree" .. sfx, t.player.click_loc, "mdaughter_lake_tree", 3)
            landed("climbTree" .. sfx .. ".island1", 2772, 3684, "mdq_island1_coord")
            t.exec("poleVaultRocks" .. sfx, t.player.use_on, "mdaughter_stick", loc_polerocks)
            landed("poleVaultRocks" .. sfx .. ".island2", 2774, 3689, "mdq_island2_coord")
            t.exec("plankRocks" .. sfx, t.player.use_on, "woodplank", loc_flatstone1)
            landed("plankRocks" .. sfx .. ".island3", 2778, 3691, "mdq_island3_coord")
        end

        -- A Dead tree on the cave path at (x, 3703): chopped if it stands
        -- (a stump regrows after ^mdq_dead_tree_stump_ticks = 100), then the
        -- stump's Step-over forcemoves the player straight across
        -- (kendal.rs2 [oploc1,mdaughter_passable_tree_stump]).
        local function dead_tree(name, x, src_x, dest_x)
            t.player.walk_to(src_x, 3703, 20) -- cross_trap grades the src tile
            local sr = t.world.loc_near("mdaughter_passable_tree_stump", 12, { at = { x, 3703, 0 } })
            if sr ~= "ok" then
                t.exec(name .. ".chop", t.player.click_loc, "mdaughter_passable_tree", 1, { at = { x, 3703 } })
                t.ticks(6)
                local cr, cd = t.world.loc_near("mdaughter_passable_tree_stump", 12, { at = { x, 3703, 0 } })
                t.check(name .. ".stump", cr == "ok", "stump on " .. x .. ",3703 after the chop -> " .. tostring(cr)
                    .. (type(cd) == "table" and (" at " .. tostring(cd.tile_x) .. "," .. tostring(cd.tile_z)) or (" " .. tostring(cd))))
            end
            t.exec(name, t.player.cross_trap, { loc = "mdaughter_passable_tree_stump", op_name = "Step-over",
                at = { x, 3703, 0 }, src = { src_x, 3703 }, dest = { dest_x, 3703 } })
        end

        t.exec("equip-scimitar", t.player.equip, "rune_scimitar")
        t.exec("equip-gloves", t.player.equip, "leather_gloves")

        -- enterCamp: Lumbridge to Rellekka is a long trip -- Camelot Teleport,
        -- then overland (2757,3478 -> 2764,3666 REACH closed-doors len=401).
        camelot("enterCamp.camelotTeleport", "Camelot, for Rellekka")
        t.exec("goto-enterCamp", t.player.goto_tile, 2764, 3666, 0)
        local rope_r, rope_d = t.exec("enterCamp", t.player.use_on, "rope", loc_boulder)
        t.check("enterCamp.message", t.msg.expect("You tie the rope around the boulder") == "ok",
            "the boulder's line (camp.rs2:29) after use_on -> " .. tostring(rope_r) .. " " .. tostring(rope_d))

        local in_ok, in_detail = enter_camp("enterCampOverRocks")
        if not in_ok then
            t.check("enterCampOverRocks.reading", true, in_detail)
            t.blocked("content_bug: the Mountain Camp cannot be walked into. [oploc1,mdaughter_rockslide] "
                .. "(OSRS-Content/osrs239-content/server/scripts/quests/quest_mountaindaughter/scripts/mountaindaughter_camp.rs2:32-34) "
                .. "p_teleports to ^mdq_camp_enter_coord = 0_43_57_16_20 = 2768,3668 (configs/quest_mountaindaughter.constant:198), "
                .. "a cliff tile whose four neighbours are all blocked (maps/m43_57), so the player is stuck there: "
                .. in_detail .. ". The rope on the boulder ([oplocu,mdaughter_cliff_boulder], camp.rs2:21-30) is mes-only "
                .. "and moves no one. Needed: the rockslide crossed both ways like the real game's Climb-over "
                .. "(south 2760,3657 <-> north 2760,3660, an open camp tile), so the camp can be left on foot too. "
                .. "Next on the route: mdaughter_rocktent_door/_doorl (2799,3665/2800,3665, op1 Go-through) has no [oploc1] "
                .. "handler and no door category (no hit for rocktent under server/scripts), so the Ancient Rock (2799,3660) "
                .. "inside it cannot be reached.")
            do return end
        end
        t.check("enterCampOverRocks", in_ok, in_detail)

        -- talkToHamal: accept the quest (mountaindaughter_camp.rs2
        -- [label,mdq_hamal_offer]).
        hamal_in("talkToHamal")
        t.exec("talkToHamal", t.player.talk_to, "mdaughter_hamal")
        t.exec("talkToHamal-dialog", t.chat.play, {
            "player:Why is everyone so hostile?",
            "npc:Forgive my people. Strangers have brought us nothing",
            "player:So what are you doing up here?",
            "npc:We are the Mountain Tribe",
            "npc:went to investigate.",
            "options",
            "choose:I will search for her!",
            "player:I will search for her!",
            "npc:Thank you. I will instruct my people",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        hamal_out("talkToHamal")

        -- speakToSpirit: dig mud, pick up the pole, rub the tree, climb,
        -- pole-vault, plank across, listen.
        t.exec("goto-digUpMud", t.player.goto_tile, 2805, 3660, 0)
        t.exec("digUpMud", t.player.click_loc, "mdaughter_roots_1", 1)
        t.ticks(1) -- trap 24: a click verb's ok is the server sentence, not the container update
        t.exec("inv.mud", t.inv.expect_has, "mdaughter_mud", 1)

        t.exec("goto-pickupPole", t.player.goto_tile, 2813, 3685, 0)
        local pickupPole_result, pickupPole_detail = t.player.click_obj("mdaughter_stick")
        t.check("pickupPole", pickupPole_result == "ok", tostring(pickupPole_result) .. " " .. tostring(pickupPole_detail))
        t.exec("inv.pole", t.inv.expect_has, "mdaughter_stick", 1)

        t.exec("goto-rubMudIntoTree", t.player.goto_tile, 2772, 3678, 0)
        t.exec("rubMudIntoTree", t.player.use_on, "mdaughter_mud", loc_lake_tree)
        t.ticks(1)
        t.exec("rubMudIntoTree.mudUsed", t.inv.expect_absent, "mdaughter_mud")
        t.exec("climbTree", t.player.click_loc, "mdaughter_lake_tree", 3)
        landed("climbTree.island1", 2772, 3684, "mdq_island1_coord")
        t.exec("poleVaultRocks", t.player.use_on, "mdaughter_stick", loc_polerocks)
        landed("poleVaultRocks.island2", 2774, 3689, "mdq_island2_coord")
        t.exec("plankRocks", t.player.use_on, "woodplank", loc_flatstone1)
        landed("plankRocks.island3", 2778, 3691, "mdq_island3_coord")

        -- listenToSpirit: verbatim from mountaindaughter_spirit.rs2
        -- [label,mdq_spirit_first_listen] (no chathead, no npc).
        t.exec("listenToSpirit", t.player.click_loc, "mdaughter_sulphar_gas", 1)
        t.exec("listenToSpirit-dialog", t.chat.play, {
            "mesbox:clearly hear the voice",
            "mesbox:",
            "player:Er... yes, hello?",
            "mesbox:Listen to me",
            "options",
            "choose:Hello! Who are you?",
            "player:Hello! Who are you?",
            "mesbox:I am all around you",
            "player:answered both questions",
            "mesbox:I am the voice of Asleif",
            "player:Wait, you're Asleif?",
            "mesbox:no longer in your world",
            "player:But I thought you said",
            "mesbox:echo of the past",
            "options",
            "choose:So what exactly do you want from me?",
            "player:So what exactly do you want from me?",
            "mesbox:greatly concerned",
            "player:Er, I don't think they like",
            "mesbox:judge then",
            "mesbox:isolated themselves",
            "mesbox:children of the Fremennik",
            "mesbox:closer together again",
            "options",
            "choose:That sounds like something I can do.",
            "player:All right, that sounds like",
            "mesbox:There is another thing",
            "player:Yes?",
            "mesbox:new supply of food",
            "options",
            "choose:I'll get right on it.",
            "player:I'll get right on it.",
            "player:peace with Rellekka",
            "mesbox:may the gods bless you",
        })
        t.expect("quest.stage.spirit_heard", t.quest.expect_stage("spirit_heard"))

        -- plankRocksReturn: the plank on the second flat stone back to the
        -- camp's shore.
        t.exec("returnToShore", t.player.use_on, "woodplank", loc_flatstone2)
        landed("returnToShore.shore", 2766, 3676, "mdq_shore_return_coord")

        -- helpTheCamp: diplomacy.
        hamal_in("hamal2")
        t.exec("talkToHamalAfterSpirit", t.player.talk_to, "mdaughter_hamal")
        t.exec("talkToHamalAfterSpirit-dialog", t.chat.play, {
            "player:About the people of Rellekka",
            "npc:Jokul",
        })
        hamal_out("hamal2")
        t.exec("goto-jokul", t.player.goto_tile, 2811, 3680, 0)
        t.exec("talkToJokul", t.player.talk_to, "mdaughter_jokul")
        t.exec("talkToJokul-dialog", t.chat.play, {
            "player:relations with Rellekka",
            "npc:short on food",
            "player:Do you know where to find more food?",
            "npc:White Pearl",
            "npc:Svidi",
        })

        -- Svidi is outside the camp, which has no on-foot exit: Camelot
        -- Teleport, then overland (2757,3478 -> 2717,3667 REACH len=357).
        camelot("svidi.camelotTeleport", "Camelot, out of the Mountain Camp, for Svidi")
        t.exec("goto-svidi", t.player.goto_tile, 2717, 3667, 0)
        t.exec("talkToSvidi", t.player.talk_to, "mdaughter_svidi")
        t.exec("talkToSvidi-dialog", t.chat.play, {
            "player:Hamal sent me",
            "npc:I won't go in there",
            "player:Can't I persuade you",
            "npc:safety guarantee",
        })

        -- speakToBrundt: the shared npc's SPAWNED (base) symbol is
        -- "viking_brundt" (areas/world/configs/m41_57.spawn:54) --
        -- quest_viking/scripts/viking_brundt.rs2's own [opnpc1,viking_brundt]
        -- checks ~mdq_brundt_relevant first (trap 19).
        t.exec("goto-brundt", t.player.goto_tile, 2660, 3670, 0)
        t.exec("speakToBrundt", t.player.talk_to, "viking_brundt")
        t.exec("speakToBrundt-dialog", t.chat.play, {
            "player:Svidi sent me",
            "npc:Ancient Rock",
        })

        -- getRockFragment: back into the camp, through the rock tent's door.
        local rock_in_ok, rock_in_detail = enter_camp("rock.enterCamp")
        t.check("rock.enterCamp", rock_in_ok, rock_in_detail)
        t.exec("goto-rock", t.player.goto_tile, 2799, 3667, 0)
        local rt_r, rt_d = t.player.pass_door({ closed = "mdaughter_rocktent_door", at = { 2799, 3665, 0 },
            near = { 2799, 3666 }, far = { 2799, 3663 } })
        if rt_r ~= "ok" then
            t.check("rock.tentDoorIn.reading", true, tostring(rt_r) .. " " .. tostring(rt_d))
            t.blocked("content_bug: mdaughter_rocktent_door/_doorl (2799,3665/2800,3665, op1 Go-through, configs/all.loc) "
                .. "has no [oploc1] handler and no door category (no hit for rocktent under "
                .. "OSRS-Content/osrs239-content/server/scripts), so the Ancient Rock (2799,3660) inside cannot be reached: "
                .. tostring(rt_r) .. " " .. tostring(rt_d))
            do return end
        end
        t.check("rock.tentDoorIn", rt_r == "ok", tostring(rt_d))
        local loc_ancient = t.player.by_symbol("loc", "mdaughter_ancient_rock")
        t.exec("getRockFragment", t.player.use_on, "bronze_pickaxe", loc_ancient)
        t.ticks(1)
        t.exec("inv.half_rock", t.inv.expect_has, "mdaughter_half_rock", 1)
        t.exec("rock.tentDoorOut", t.player.pass_door, { closed = "mdaughter_rocktent_door", at = { 2799, 3665, 0 },
            near = { 2799, 3664 }, far = { 2799, 3667 } })

        camelot("brundt2.camelotTeleport", "Camelot, out of the Mountain Camp, for Rellekka")
        t.exec("goto-brundt2", t.player.goto_tile, 2660, 3670, 0)
        t.exec("returnToBrundt", t.player.talk_to, "viking_brundt")
        t.exec("returnToBrundt-dialog", t.chat.play, {
            "player:piece of the Ancient Rock",
            "npc:just that, a rock",
            "npc:safety guarantee",
        })
        t.exec("inv.half_rock_given", t.inv.expect_absent, "mdaughter_half_rock")
        t.exec("inv.guarantee", t.inv.expect_has, "mdaughter_safety_guarantee", 1)
        t.exec("goto-svidi2", t.player.goto_tile, 2717, 3667, 0)
        t.exec("returnToSvidi", t.player.talk_to, "mdaughter_svidi")
        t.exec("returnToSvidi-dialog", t.chat.play, {
            "player:safety guarantee from Brundt",
            "npc:head in today",
        })
        t.exec("inv.guarantee_given", t.inv.expect_absent, "mdaughter_safety_guarantee")

        local h3_ok, h3_detail = enter_camp("hamal3.enterCamp")
        t.check("hamal3.enterCamp", h3_ok, h3_detail)
        hamal_in("hamal3")
        t.exec("returnToHamalAboutDiplomacy", t.player.talk_to, "mdaughter_hamal")
        t.exec("returnToHamalAboutDiplomacy-dialog", t.chat.play, {
            "options",
            "choose:About the people of Rellekka...",
            "player:About the people of Rellekka",
            "npc:never been better",
        })
        t.exec("returnToHamalAboutFood", t.player.talk_to, "mdaughter_hamal")
        t.exec("returnToHamalAboutFood-dialog", t.chat.play, {
            "player:About your food supplies",
            "npc:Our stores are running low",
            "npc:White Pearl",
        })
        hamal_out("hamal3")

        -- food: the White Pearl on White Wolf Mountain (gloves worn):
        -- Camelot Teleport out of the camp, overland to the bush
        -- (2757,3478 -> 2849,3498 REACH len=226), and back by Camelot.
        camelot("fruit.camelotTeleport", "Camelot, out of the Mountain Camp, for White Wolf Mountain")
        t.exec("goto-fruit", t.player.goto_tile, 2849, 3498, 0)
        t.exec("getFruit", t.player.click_loc, "mdaughter_white_pearl_bush", 3)
        t.ticks(1)
        t.exec("inv.fruit", t.inv.expect_has, "mdaughter_white_pearl_fruit", 1)
        t.exec("eatFruit", t.player.inv_op, "mdaughter_white_pearl_fruit", 3)
        t.ticks(1)
        t.exec("inv.seed", t.inv.expect_has, "mdaughter_white_pearl_seed", 1)
        camelot("hamal4.camelotTeleport", "Camelot, from White Wolf Mountain, for Rellekka")
        local h4_ok, h4_detail = enter_camp("hamal4.enterCamp")
        t.check("hamal4.enterCamp", h4_ok, h4_detail)
        hamal_in("hamal4")
        t.exec("giveSeed", t.player.talk_to, "mdaughter_hamal")
        t.exec("giveSeed-dialog", t.chat.play, {
            "player:About your food supplies",
            "npc:White Pearl seed",
        })
        t.exec("inv.seed_given", t.inv.expect_absent, "mdaughter_white_pearl_seed")
        hamal_out("hamal4")

        -- returnToSpirit (the second visit): tree, pole, plank, listen.
        cross_lake("2")
        t.exec("returnToSpirit", t.player.click_loc, "mdaughter_sulphar_gas", 1)
        t.exec("returnToSpirit-dialog", t.chat.play, {
            "player:Asleif spirit thing",
            "mesbox:Yes,",
            "player:I did what you asked me to",
            "mesbox:I can sense that this is so",
            "player:any other quests for me",
            "mesbox:one more task",
            "mesbox:does not believe that I am dead",
            "mesbox:You must convince him",
            "player:How do I do that, exactly?",
            "mesbox:attacked by some creature",
            "mesbox:cannot provide further assistance",
        })
        t.expect("quest.stage.ready_for_kendal", t.quest.expect_stage("ready_for_kendal"))
        -- noPlankRocksReturn: the stone's own Jump-across op.
        t.exec("returnToShore2", t.player.click_loc, "mdaughter_flatstone2", 1)
        landed("returnToShore2.shore", 2766, 3676, "mdq_shore_return_coord")

        -- the Kendal (mountaindaughter_kendal.rs2): both Dead trees east,
        -- then the cave entrance, which teleports into the cave
        -- (^mdq_cave_arrive_coord 2790,10083).
        t.exec("goto-cave", t.player.goto_tile, 2799, 3703, 0)
        dead_tree("caveTree1", 2802, 2801, 2803)
        dead_tree("caveTree2", 2807, 2806, 2808)
        t.exec("enterCave", t.player.click_loc, "mdaughter_caveentrance", 1)
        landed("enterCave.inCave", 2790, 10083, "mdq_cave_arrive_coord")
        local multibear_present = t.npc.await_present("mdaughter_multi_bear", 20, 15)
        t.check("talkToKendal.present", multibear_present == "ok", "npc.await_present -> " .. tostring(multibear_present))
        t.exec("talkToKendal", t.player.talk_to, "mdaughter_multi_bear")
        t.exec("talkToKendal-dialog", t.chat.play, {
            "npc:Who dares enter the domain of a god?",
            "player:It's just me, no one special.",
            "npc:do you bring an offering",
            "player:You mean a sacrifice?",
            "npc:fed me well over the years",
            "player:You look like a man in a bearsuit!",
            "npc:Perceptive of you",
            "npc:I am no god, true.",
            "player:Can I see that corpse?",
            "npc:Never did find out who she was.",
            "player:I humbly request to be given the remains.",
            "npc:You'll get nothing from me",
            "player:I will kill you myself!",
            "npc:Then die like the rest of them!",
        })
        t.expect("quest.stage.kendal_found", t.quest.expect_stage("kendal_found"))
        -- The Kendal is a real fight: quest_mountaindaughter.npc:3-24 (hp 50,
        -- attack/strength 65, defence 60, crush, attackrate 6), max hit 9
        -- (wiki The_Kendal oldid 15199460). Lobsters eaten below 30.
        local present_result = t.npc.await_present("mdaughter_bearman_fighter", 20, 15)
        t.check("killKendal.present", present_result == "ok", "npc.await_present -> " .. tostring(present_result))
        t.exec("killKendal.attack", t.player.attack, "mdaughter_bearman_fighter", 2, 30)
        local _, kill_detail = t.exec("killKendal", t.npc.await_dead_engaged, 120, 8,
            { eat = { item = "lobster", below = 30 } })
        local low = tonumber(tostring(kill_detail):match("lowest hp (%d+)/"))
        local food_r, food_left = t.inv.count("lobster")
        food_left = tonumber(food_left)
        t.check("killKendal.margin", low ~= nil and low >= 15 and food_r == "ok" and food_left ~= nil and food_left >= 1,
            "lowest hp " .. tostring(low) .. "/60 (the kill wait's own reading), lobsters staged 5, left " .. tostring(food_left) .. " (" .. tostring(food_r)
                .. ") (margin: lowest hp >= 15, a quarter of 60, AND food left)")
        t.ticks(10)
        t.expect("quest.stage.kendal_killed", t.quest.expect_stage("kendal_killed"))
        t.exec("inv.bearhead", t.inv.expect_has, "mdaughter_bear_helmet", 1)
        local corpse_r, corpse_d = t.player.click_obj("mdaughter_daughter_corpse")
        t.check("grabCorpse", corpse_r == "ok", tostring(corpse_r) .. " " .. tostring(corpse_d))
        t.exec("inv.corpse", t.inv.expect_has, "mdaughter_daughter_corpse", 1)
        t.exec("leaveCave", t.player.click_loc, "mdaughter_caveexit", 1)
        landed("leaveCave.outside", 2808, 3703, "mdq_cave_exit_coord")
        -- back west past the same two trees (a stump regrows).
        dead_tree("leaveTree2", 2807, 2808, 2806)
        dead_tree("leaveTree1", 2802, 2803, 2801)

        -- bringCorpseToHamal
        hamal_in("hamal5")
        t.exec("bringCorpseToHamal", t.player.talk_to, "mdaughter_hamal")
        t.exec("bringCorpseToHamal-dialog", t.chat.play, {
            "player:But he's not a god!",
            "npc:man in a bearsuit",
            "player:I will.",
            "npc:muddy rocks",
        })
        t.expect("quest.stage.corpse_given", t.quest.expect_stage("corpse_given"))
        hamal_out("hamal5")

        -- collectRocks: five muddy rocks from FOUR spawns -- one has to
        -- respawn (^lootdrop_duration 200 ticks), so up to six passes.
        local function rocks()
            local r, n = t.inv.count("mdaughter_rock")
            return tonumber(n) or 0
        end
        local rock_spots = { {2804, 3660}, {2809, 3679}, {2812, 3680}, {2812, 3687} }
        local pass = 0
        while rocks() < 5 and pass < 6 do
            pass = pass + 1
            for i, spot in ipairs(rock_spots) do
                if rocks() >= 5 then break end
                t.player.goto_tile(spot[1], spot[2], 0)
                local r, d = t.player.click_obj("mdaughter_rock")
                t.note("rock pass " .. pass .. " spot " .. i .. ": " .. tostring(r) .. " " .. tostring(d))
            end
            if rocks() < 5 then t.ticks(60) end
        end
        t.exec("inv.rocks", t.inv.expect_has, "mdaughter_rock", 5)

        t.exec("goto-ragnar", t.player.goto_tile, 2766, 3676, 0)
        t.exec("speakRagnar", t.player.talk_to, "mdaughter_ragnar")
        t.exec("speakRagnar-dialog", t.chat.play, {
            "npc:I feared as much",
            "npc:bury it with her",
        })
        t.exec("inv.necklace", t.inv.expect_has, "mdaughter_necklace", 1)

        -- buryCorpseOnIsland + createCairn: back across the lake a third
        -- time to bury Asleif and raise the cairn on the burial mound.
        cross_lake("3")
        t.exec("buryCorpse", t.player.inv_op, "mdaughter_daughter_corpse", 3)
        t.ticks(2)
        t.exec("inv.corpse_gone", t.inv.expect_absent, "mdaughter_daughter_corpse")
        local loc_mound = t.player.by_symbol("loc", "mdaughter_burialmound")
        local _, reward_snap = t.skill.snapshot()
        t.exec("createCairn", t.player.use_on, "mdaughter_rock", loc_mound)
        t.ticks(1)
        t.exec("createCairn.rocksUsed", t.inv.expect_absent, "mdaughter_rock")
        t.quest.expect_complete()
        -- burial.rs2:92-98 [proc,mdq_quest_complete]: 2000 Prayer XP and
        -- 1000 Attack XP (^mdq_reward_prayer_xp 20000 / ^mdq_reward_attack_xp
        -- 10000 tenths).
        t.expect("reward.prayer_xp", t.skill.expect_gain("prayer", 2000, reward_snap))
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 1000, reward_snap))
        t.finish(0)
    end,
}
