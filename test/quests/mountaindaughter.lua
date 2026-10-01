-- Mountain Daughter, driven end to end against
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_mountaindaughter/.
-- docs/quests/mountain_daughter.md is the pinned brief (wiki oldids + the
-- port's own breakpoint derivation); this file follows its stage table.
--
-- Content notes carried from the brief's "found while pinning" section:
--  * mdaughter_cliff_boulder (rope) never p_teleports the player -- it is
--    flavour only (mountaindaughter_camp.rs2 [oplocu,mdaughter_cliff_boulder]
--    has no p_teleport/loc_change). The real entry into the camp is
--    [oploc1,mdaughter_rockslide], ungated at every stage. This file drives
--    both: the rope on the boulder (real content, the guide's own
--    enterCamp step), then the rockslide (the port's actual entry
--    mechanism) -- neither is a goto_tile bypass, both are real clicks.
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
--    listenToSpirit-dialog/returnToSpirit-dialog below are read off that
--    file's [label,mdq_spirit_first_listen]/[label,mdq_spirit_check_progress]
--    branches directly (trap 17: rebuilt from the .rs2, not the wiki prose).
--  * The 5-muddy-rock requirement (mdaughter_burial.rs2's
--    [opheld3,mdaughter_daughter_corpse]... [oplocu,mdaughter_burialmound])
--    only has FOUR ground spawns in areas/world/configs/m43_57.spawn, so a
--    fifth needs one of the four to respawn -- collectRocks below loops
--    passes over the four spots with a wait between them.
--  * mdaughter_burialmound (5862) is a multiloc CHILD with no map placement
--    of its own; the scene holds its base wrapper mdaughter_multimound
--    (5861, maps/m43_57.jl2:1001) -- click_loc's "base" resolve rule
--    (docs/QUEST_AUTHORING.md trap 20) reaches it through the child symbol.
--  * seam27: every lake crossing is a plain press from a walked tile. The
--    clump of rocks and the first flat stone sit two tiles across BLOCK-
--    flagged water from the only open tile (maps/m43_57.jm2), so they are
--    [aploc1]/[aplocu] triggers with p_aprange(2) (mountaindaughter_camp.rs2),
--    and the three landings are open stand tiles inside Quest Helper's
--    LAKE_ISLAND_1/2/3 zones (2772,3684 / 2774,3689 / 2778,3691 -- the pool
--    island, where the mound, the pool and the return stone are all walked
--    to). The cave path's two Dead trees (2802,3703 and 2807,3703) are
--    chopped with the axe and their stumps stepped over
--    (mountaindaughter_kendal.rs2), both ways.

return {
    id = "mountaindaughter",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel agility 20",
        "::give rope 1",
        "::give bronze_pickaxe 1",
        "::give bronze_axe 1",
        "::give woodplank 1",
        "::give leather_gloves 1",
        "::give rune_scimitar 1",
        "::give lobster 5",
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

        -- Loc targets resolved once, up front: by_symbol only maps a
        -- content symbol to a {kind,id} table (docs/QUEST_AUTHORING.md
        -- section 3), it does not check liveness, and use_on/walk_near need
        -- that table form rather than click_loc's bare symbol string.
        local loc_boulder = t.player.by_symbol("loc", "mdaughter_cliff_boulder")
        local loc_lake_tree = t.player.by_symbol("loc", "mdaughter_lake_tree")
        local loc_polerocks = t.player.by_symbol("loc", "mdaughter_polerocks")
        local loc_flatstone1 = t.player.by_symbol("loc", "mdaughter_flatstone1")
        local loc_flatstone2 = t.player.by_symbol("loc", "mdaughter_flatstone2")

        -- The cave path's Dead tree at (x, z): step over its stump if it is
        -- one, else chop it first (a stump regrows after
        -- ^mdq_dead_tree_stump_ticks, even between the listing and the
        -- press, so a refused step is chopped and tried once more). The
        -- chop's click answers on the walk's map flag, before p_arrivedelay
        -- and the swing land, so the stump is read 6 ticks later. A copy
        -- selector naming no live copy answers no_row and presses nothing
        -- (QUEST_AUTHORING verb table). Graded on the tile: the player must
        -- end on the other side of the tree's column from where he started.
        local function cross_dead_tree(name, x, z)
            local _, before = t.world.tile()
            local r, d
            for attempt = 1, 2 do
                r, d = t.player.click_loc("mdaughter_passable_tree_stump", 1, { at = {x, z} })
                if r == "ok" then break end
                t.exec(name .. ".chop", t.player.click_loc, "mdaughter_passable_tree", 1, { at = {x, z} })
                t.ticks(6)
            end
            t.ticks(2)
            local _, after = t.world.tile()
            local crossed = before ~= nil and after ~= nil and after.z == z and
                ((before.x < x and after.x > x) or (before.x > x and after.x < x))
            t.check(name .. ".stepOver", r == "ok" and crossed,
                tostring(r) .. " " .. tostring(d) .. " -- " ..
                (before and (before.x .. "," .. before.z) or "?") .. " -> " ..
                (after and (after.x .. "," .. after.z) or "?"))
        end

        t.exec("equip-scimitar", t.player.equip, "rune_scimitar")
        t.exec("equip-gloves", t.player.equip, "leather_gloves")

        -- enterCamp: rope on the boulder (real content, mes-only -- see
        -- header note), then the rockslide, which is what actually crosses
        -- into the camp (mountaindaughter_camp.rs2 [oploc1,mdaughter_rockslide]).
        t.exec("goto-enterCamp", t.player.goto_tile, 2765, 3666, 0)
        t.exec("enterCamp", t.player.use_on, "rope", loc_boulder)
        t.exec("goto-enterCampOverRocks", t.player.goto_tile, 2760, 3658, 0)
        t.exec("enterCampOverRocks", t.player.click_loc, "mdaughter_rockslide", 1)

        -- talkToHamal: accept the quest (mountaindaughter_camp.rs2
        -- [label,mdq_hamal_offer]).
        t.exec("goto-talkToHamal", t.player.goto_tile, 2811, 3673, 0)
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

        -- speakToSpirit: dig mud, pick up the pole, rub the tree, climb,
        -- pole-vault, plank across, listen (mountaindaughter_camp.rs2's
        -- lake puzzle + mountaindaughter_spirit.rs2
        -- [label,mdq_spirit_first_listen]).
        t.exec("goto-digUpMud", t.player.goto_tile, 2805, 3661, 0)
        t.exec("digUpMud", t.player.click_loc, "mdaughter_roots_1", 1)
        t.ticks(1) -- trap 24: a click verb's ok is the server sentence, not the container update
        t.exec("inv.mud", t.inv.expect_has, "mdaughter_mud", 1)

        -- pickupPole: click_obj is hollow on success (trap 12) -- call it
        -- directly and read the count back.
        t.exec("goto-pickupPole", t.player.goto_tile, 2813, 3685, 0)
        local pickupPole_result, pickupPole_detail = t.player.click_obj("mdaughter_stick")
        t.check("pickupPole", pickupPole_result == "ok", tostring(pickupPole_result) .. " " .. tostring(pickupPole_detail))
        t.exec("inv.pole", t.inv.expect_has, "mdaughter_stick", 1)

        t.exec("goto-rubMudIntoTree", t.player.goto_tile, 2772, 3679, 0)
        t.exec("rubMudIntoTree", t.player.use_on, "mdaughter_mud", loc_lake_tree)
        t.exec("climbTree", t.player.click_loc, "mdaughter_lake_tree", 3)

        -- poleVaultRocks / plankRocks: both used across the water from the
        -- outcrop the tree drops you on (header note, seam27).
        t.exec("poleVaultRocks", t.player.use_on, "mdaughter_stick", loc_polerocks)
        t.exec("plankRocks", t.player.use_on, "woodplank", loc_flatstone1)

        -- listenToSpirit: seam23 rewrote Asleif's spirit as pure ~mesbox +
        -- ~p_choice2, verbatim from mountaindaughter_spirit.rs2
        -- [label,mdq_spirit_first_listen] (no chathead, no npc -- see the
        -- header note).
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
        -- shore (guide step per MountainDaughter.java's
        -- helpTheCamp.addStep(onIsland3, plankRocksReturn), "Use a plank on
        -- the flat stone to return to shore").
        t.exec("returnToShore", t.player.use_on, "woodplank", loc_flatstone2)

        -- helpTheCamp: diplomacy.
        t.exec("goto-hamal2", t.player.goto_tile, 2811, 3673, 0)
        t.exec("talkToHamalAfterSpirit", t.player.talk_to, "mdaughter_hamal")
        t.exec("talkToHamalAfterSpirit-dialog", t.chat.play, {
            "player:About the people of Rellekka",
            "npc:Jokul",
        })
        t.exec("goto-jokul", t.player.goto_tile, 2811, 3679, 0)
        t.exec("talkToJokul", t.player.talk_to, "mdaughter_jokul")
        t.exec("talkToJokul-dialog", t.chat.play, {
            "player:relations with Rellekka",
            "npc:short on food",
            "player:Do you know where to find more food?",
            "npc:White Pearl",
            "npc:Svidi",
        })
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
        -- checks ~mdq_brundt_relevant first (trap 19's owner-checks-first
        -- fix), so this is the correct symbol to press, never the
        -- "_child" variant mountaindaughter_camp.rs2's header comment names
        -- (that trigger belongs to quest_fremennikexiles and is reached only
        -- through the base's hand-over).
        t.exec("goto-brundt", t.player.goto_tile, 2659, 3671, 0)
        t.exec("speakToBrundt", t.player.talk_to, "viking_brundt")
        t.exec("speakToBrundt-dialog", t.chat.play, {
            "player:Svidi sent me",
            "npc:Ancient Rock",
        })
        t.exec("goto-rock", t.player.goto_tile, 2799, 3662, 0)
        local loc_ancient = t.player.by_symbol("loc", "mdaughter_ancient_rock")
        t.exec("getRockFragment", t.player.use_on, "bronze_pickaxe", loc_ancient)
        t.ticks(1)
        t.exec("inv.half_rock", t.inv.expect_has, "mdaughter_half_rock", 1)
        t.exec("goto-brundt2", t.player.goto_tile, 2659, 3671, 0)
        t.exec("returnToBrundt", t.player.talk_to, "viking_brundt")
        t.exec("returnToBrundt-dialog", t.chat.play, {
            "player:piece of the Ancient Rock",
            "npc:just that, a rock",
            "npc:safety guarantee",
        })
        t.exec("inv.guarantee", t.inv.expect_has, "mdaughter_safety_guarantee", 1)
        t.exec("goto-svidi2", t.player.goto_tile, 2717, 3667, 0)
        t.exec("returnToSvidi", t.player.talk_to, "mdaughter_svidi")
        t.exec("returnToSvidi-dialog", t.chat.play, {
            "player:safety guarantee from Brundt",
            "npc:head in today",
        })
        t.exec("goto-hamal3", t.player.goto_tile, 2811, 3673, 0)
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

        -- food: the White Pearl on White Wolf Mountain (gloves worn).
        t.exec("goto-fruit", t.player.goto_tile, 2849, 3499, 0)
        t.exec("getFruit", t.player.click_loc, "mdaughter_white_pearl_bush", 3)
        t.ticks(1)
        t.exec("inv.fruit", t.inv.expect_has, "mdaughter_white_pearl_fruit", 1)
        t.exec("eatFruit", t.player.inv_op, "mdaughter_white_pearl_fruit", 3)
        t.ticks(1)
        t.exec("inv.seed", t.inv.expect_has, "mdaughter_white_pearl_seed", 1)
        t.exec("goto-hamal4", t.player.goto_tile, 2811, 3673, 0)
        t.exec("giveSeed", t.player.talk_to, "mdaughter_hamal")
        t.exec("giveSeed-dialog", t.chat.play, {
            "player:About your food supplies",
            "npc:White Pearl seed",
        })

        -- returnToSpirit (the second visit): tree, pole, plank, listen.
        t.exec("goto-tree2", t.player.goto_tile, 2772, 3679, 0)
        t.exec("climbTree2", t.player.click_loc, "mdaughter_lake_tree", 3)
        t.exec("poleVaultRocks2", t.player.use_on, "mdaughter_stick", loc_polerocks)
        t.exec("plankRocks2", t.player.use_on, "woodplank", loc_flatstone1)
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
        -- noPlankRocksReturn: "Attempt to jump across the flat stone WITHOUT
        -- a plank" -- the stone's own Jump-across op.
        t.exec("returnToShore2", t.player.click_loc, "mdaughter_flatstone2", 1)

        -- the Kendal (mountaindaughter_kendal.rs2): the path east of the
        -- lake is blocked by two Dead trees; chop each with the axe (setup's
        -- bronze_axe) and step over its stump, then Enter the cave, which
        -- teleports into it, where the multi_bear (mdaughter_bearman while
        -- %mdaughter_bear_multi_state=0) is talked to first, revealing itself
        -- and spawning the real fighter npc.
        t.exec("goto-cave", t.player.goto_tile, 2799, 3703, 0)
        cross_dead_tree("caveTree1", 2802, 3703)
        cross_dead_tree("caveTree2", 2807, 3703)
        t.exec("enterCave", t.player.click_loc, "mdaughter_caveentrance", 1)
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
        local present_result = t.npc.await_present("mdaughter_bearman_fighter", 20, 15)
        t.check("killKendal.present", present_result == "ok", "npc.await_present -> " .. tostring(present_result))
        t.exec("killKendal.attack", t.player.attack, "mdaughter_bearman_fighter", 2, 30)
        t.exec("killKendal", t.npc.await_dead_engaged, 80, 8)
        t.ticks(10)
        t.expect("quest.stage.kendal_killed", t.quest.expect_stage("kendal_killed"))
        t.exec("inv.bearhead", t.inv.expect_has, "mdaughter_bear_helmet", 1)
        local corpse_r, corpse_d = t.player.click_obj("mdaughter_daughter_corpse")
        t.check("grabCorpse", corpse_r == "ok", tostring(corpse_r) .. " " .. tostring(corpse_d))
        t.exec("inv.corpse", t.inv.expect_has, "mdaughter_daughter_corpse", 1)
        t.exec("leaveCave", t.player.click_loc, "mdaughter_caveexit", 1)
        t.ticks(2)
        -- back west past the same two trees (a stump regrows,
        -- ^mdq_dead_tree_stump_ticks).
        cross_dead_tree("leaveTree2", 2807, 3703)
        cross_dead_tree("leaveTree1", 2802, 3703)

        -- bringCorpseToHamal
        t.exec("goto-hamal5", t.player.goto_tile, 2811, 3673, 0)
        t.exec("bringCorpseToHamal", t.player.talk_to, "mdaughter_hamal")
        t.exec("bringCorpseToHamal-dialog", t.chat.play, {
            "player:But he's not a god!",
            "npc:man in a bearsuit",
            "player:I will.",
            "npc:muddy rocks",
        })
        t.expect("quest.stage.corpse_given", t.quest.expect_stage("corpse_given"))

        -- collectRocks: five muddy rocks, but areas/world/configs/m43_57.spawn
        -- only carries FOUR mdaughter_rock ground spawns -- one has to
        -- respawn, so this loops the four spots across up to four passes.
        local function rocks()
            local r, n = t.inv.count("mdaughter_rock")
            return tonumber(n) or 0
        end
        local rock_spots = { {2804, 3660}, {2809, 3679}, {2812, 3680}, {2812, 3687} }
        local pass = 0
        -- a taken spawn returns after ^lootdrop_duration (200 ticks,
        -- drop_tables/configs/lootdrop.constant; torirs_server_world.c
        -- ground_tick), so four passes 60 ticks apart can end just short of
        -- the first return when all four were picked on pass 1: six passes.
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

        t.exec("goto-ragnar", t.player.goto_tile, 2766, 3677, 0)
        t.exec("speakRagnar", t.player.talk_to, "mdaughter_ragnar")
        t.exec("speakRagnar-dialog", t.chat.play, {
            "npc:I feared as much",
            "npc:bury it with her",
        })
        t.exec("inv.necklace", t.inv.expect_has, "mdaughter_necklace", 1)

        -- buryCorpseOnIsland + createCairn: back across the lake a third
        -- time to bury Asleif and raise the cairn on the burial mound.
        t.exec("goto-tree3", t.player.goto_tile, 2772, 3679, 0)
        t.exec("climbTree3", t.player.click_loc, "mdaughter_lake_tree", 3)
        t.exec("poleVaultRocks3", t.player.use_on, "mdaughter_stick", loc_polerocks)
        t.exec("plankRocks3", t.player.use_on, "woodplank", loc_flatstone1)
        t.exec("buryCorpse", t.player.inv_op, "mdaughter_daughter_corpse", 3)
        t.ticks(2)
        t.exec("inv.corpse_gone", t.inv.expect_absent, "mdaughter_daughter_corpse")
        local loc_mound = t.player.by_symbol("loc", "mdaughter_burialmound")
        t.exec("createCairn", t.player.use_on, "mdaughter_rock", loc_mound)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
