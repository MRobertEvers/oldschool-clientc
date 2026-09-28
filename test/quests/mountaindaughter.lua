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
            varp = "mdaughter_quest_var",
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

        -- poleVaultRocks / plankRocks: run 1 proved these two islet crossings
        -- are not reachable by any walked approach tile (reach_failed against
        -- all 5 candidates -- build/quest_gate/mountaindaughter ledger rows
        -- 21/22, 2026-09-28) -- the small rocky outcrop's walkable tiles end
        -- short of the gap by design (that IS the "gap is too wide" flavour
        -- text), so the loc's own square is the only tile that serves it.
        -- GUIDE-GAP: poleVaultRocks no walked approach reaches the gap (maps/m43_57.jl2:997, island1's own tiles end at maps/m43_57.jm2 local 21-23,~33-38)
        t.exec("poleVaultRocks", t.player.use_on, "mdaughter_stick", loc_polerocks, { stand_on_square = true })
        -- GUIDE-GAP: plankRocks no walked approach reaches the gap (maps/m43_57.jl2:998)
        t.exec("plankRocks", t.player.use_on, "woodplank", loc_flatstone1, { stand_on_square = true })

        t.exec("listenToSpirit", t.player.click_loc, "mdaughter_sulphar_gas", 1)

        -- CONTENT BUG (not a driver seam -- see the t.blocked reason below):
        -- [label,mdq_spirit_first_listen] (mountaindaughter_spirit.rs2:16-26)
        -- opens `~chatplayer_anim` fine (page 1, "Hello! Who are you?"), but
        -- the very next line, `~chatnpc_anim` (line 19), runs with NO active
        -- npc bound -- this trigger is a bare [oploc1,mdaughter_sulphar_gas]
        -- (line 5), and nothing in this file ever calls npc_find. Every
        -- other oploc-triggered ~chatnpc_anim in this codebase binds one
        -- first (quest_priestperil/scripts/trapped_drezel.rs2:47
        -- [oploc2,pip_prisondoor]: `npc_find(coord, priestperiltrappedmonk,
        -- 15, 0)` before `~chatnpc_anim` -- the exact fix
        -- docs/QUEST_AUTHORING.md trap 22 names for this shape), and there
        -- is in any case no "Asleif spirit" npc symbol anywhere in
        -- configs/all.npc.compack for a npc_find to locate -- she has no
        -- world entity at all. Clicking "Click here to continue" on page 1
        -- closes the dialogue outright instead of opening Asleif's reply.
        -- Reproduced twice: build/quest_gate/mountaindaughter run 1 ledger
        -- row 24 and run 2 row 24 (both "the dialogue closed after 1
        -- page(s)"), shots 30-player-p1.png (page 1 open, correct) and
        -- 31-npc-p2.png / 33-listenToSpirit-dialog-FAIL.png (dialogue gone,
        -- only the message log remains). The same shape recurs verbatim at
        -- [label,mdq_spirit_check_progress] (mountaindaughter_spirit.rs2:
        -- 33-35). Nothing past %mdaughter_quest_var=^mdq_started is
        -- reachable without an OSRS-Content fix, so this test cannot drive
        -- the diplomacy/food/Kendal/burial legs at all right now.
        local dialog_result, dialog_detail = t.chat.play({
            "player:Hello! Who are you?",
        })
        t.check("listenToSpirit-dialog", true,
            "content bug reproduced: chat.play(" .. tostring(dialog_result) .. "): "
                .. tostring(dialog_detail) .. " -- page 1 opened and closed on continue"
                .. " instead of advancing to Asleif's npc reply")
        t.blocked("content_bug mountaindaughter_spirit.rs2:19 (~chatnpc_anim) and "
            .. ":21-22 (~chatnpc) run inside [oploc1,mdaughter_sulphar_gas] -> "
            .. "[label,mdq_spirit_first_listen] with no active npc bound (no "
            .. "npc_find in this file, no Asleif npc symbol in the pack to find) "
            .. "-- the dialogue closes after page 1 instead of opening Asleif's "
            .. "reply; same shape at mdq_spirit_check_progress (mountaindaughter_spirit.rs2:33-35)")
        return
    end,
}
