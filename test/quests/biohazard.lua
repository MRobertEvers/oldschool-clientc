-- Biohazard. Scaffolded by tools/quest_gate/new_quest.py from Quest
-- Helper's helpers/quests/biohazard/, then hand-corrected against the
-- pack's own scripts (OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_biohazard/, areas/area_ardougne_east/scripts/elena.rs2,
-- areas/area_combat_training/configs/combat_training.constant) and its
-- own audit doc (docs/quests/biohazard.md). Tier (quest_inventory.tsv): 1.
--
-- Fixes to the generator's first guess:
--   * setup's completed prerequisite is quest_plaguecity, not quest_elena
--     -- quest_elena is the quest's own SCRIPT directory name, but the
--     dbrow ::complete needs is quest_plaguecity (all.dbrow:6045; the
--     cheat ladder keys on it at quest_cheat.rs2:947-951, which calls
--     ~quest_elena_set_progress(^elena_complete) and that proc's own
--     stage>=freed_elena branch is what flips %plaguecity_elena_at_home
--     to 1, making elena2_vis the live multinpc leaf at 2592,3336,0).
--   * quest.bind's constants were wrong: the generator copied
--     quest_biohazard.constant's %bioerrand BIT constants (chancy/hops/
--     devinci correct/given/wrong, 1-9) onto %biohazard's own primary
--     ladder. %biohazard's real states 0/1/2/3/4/5/6/7/10/12/14/15/16
--     live in areas/area_combat_training/configs/combat_training.constant
--     (that file's own header explains the odd ownership: "Owned here
--     because the Combat Training Camp gate compiled first").
--
-- Fixture start: fresh_lumbridge.ini stands the player at 3206,3233,0
-- (Lumbridge, beside Hans). The first t.player.goto_tile below is what
-- actually leaves that tile, walking the whole way to Elena's house in
-- East Ardougne via ::goto.

return {
    id = "biohazard",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so gasmask fits
        "::biohazardreset",
        "::give gasmask 1", -- required equipment (Quest Helper's getItemRequirements()); worn at Omart, never the quest's own deliverable
        "::complete quest_plaguecity", -- Biohazard's only prerequisite (all.dbrow:6045 quest_biohazard's requirement_quests=36 -> quest_plaguecity)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "biohazard",
            constants = {
                not_started = 0,
                started = 1,
                spoken_jerico = 2,
                used_birdfeed = 3,
                released_pigeons = 4,
                climbed_ladder = 5,
                poisoned_stew = 6,
                found_distillator = 7,
                given_distillator = 10,
                spoken_chemist = 12,
                found_secret = 14,
                reported_elena = 15,
                complete = 16,
            },
            row = "quest_biohazard",
            display = "Biohazard", -- all.dbrow:466 quest_biohazard's own displayname
            points = 3, -- all.dbrow:485 questpoints=3
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- setup's cheats are server-side; let the client see biohazard=0 before reading it
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ==== Elena, East Ardougne (2592,3336,0; elena2_vis, [opnpc1,elena2]) ====
        -- switch_int(%biohazard), case ^biohazard_not_started: with Plague
        -- City complete, ~biohazard_plague_city_done is true so the "still
        -- have so much work to do" refusal branch is skipped. Page 1 is
        -- the PLAYER's own greeting text with <nc_name(elena2)> spliced
        -- in (trap 18); it is skipped here with the '*' wildcard rather
        -- than guessed, since the interpolated name is not literal source
        -- text (chat.play matches plain substrings, elena.rs2:97).
        t.exec("goto-talkToElena", t.player.goto_tile, 2592, 3336, 0)
        t.exec("talkToElena", t.player.talk_to, "elena2_vis", 1)
        t.exec("talkToElena-dialog", t.chat.play, {
            "*", -- page 1: player "Good day to you <nc_name(elena2)>." (elena.rs2:96)
            "npc:You too, thanks for freeing me.",
            "npc:It's just a shame the mourners confiscated my equipment.",
            "player:What did they take?",
            "npc:My distillator. I can't test any plague samples without it.",
            "npc:I must somehow retrieve that distillator",
            "choose:I'll try to retrieve it for you.",
            "player:I'll try to retrieve it for you.",
            "npc:I was hoping you would say that.",
            "player:Any ideas?",
            "npc:My father's friend Jerico is in communication with West Ardougne.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started")) -- elena.rs2:117 writes ^biohazard_started on choice 1

        -- ==== Jerico, next to the chapel (2612,3324,0; [opnpc1,jerico]) ====
        -- switch_int(%biohazard), case ^biohazard_started: every line here
        -- is literal (no interpolation), spelled verbatim from jerico.rs2:44-51.
        t.exec("goto-talkToJerico", t.player.goto_tile, 2612, 3324, 0)
        t.exec("talkToJerico", t.player.talk_to, "jerico", 1)
        t.exec("talkToJerico-dialog", t.chat.play, {
            "player:Hello Jerico.",
            "npc:Hello, I've been expecting you.",
            "player:That's right.",
            "npc:My messenger pigeons help me communicate with friends over the wall.",
            "npc:I have arranged for two friends",
            "npc:But be careful, if the mourners catch you the punishment will be severe.",
            "player:Thanks Jerico.",
        })
        t.expect("quest.stage.spoken_jerico", t.quest.expect_stage("spoken_jerico")) -- jerico.rs2:48

        -- Investigate the watchtower (leaf biowatchtower_op per the multiloc
        -- resolve, trap 20; the wrapper "biowatchtower" is never the click
        -- target). Only shows the mourner exchange when mournerwatchtower is
        -- nearby (quest_biohazard_locs.rs2:51-56) -- optional lore, not a
        -- state change, so no chat.play is asserted on it.
        t.exec("goto-investigateWatchtower", t.player.goto_tile, 2562, 3301, 0)
        t.exec("investigateWatchtower", t.player.click_loc, "biowatchtower_op", 1)

        -- Get bird feed from Jerico's cupboard: op1 opens it
        -- (jericoscupboardshut->jericoscupboardopen, 500), op1 again
        -- searches the open form and grants birdfeed (quest_biohazard_locs.rs2:20-47)
        -- now that %biohazard >= ^biohazard_spoken_jerico.
        t.exec("goto-getBirdFeed", t.player.goto_tile, 2612, 3326, 0)
        t.exec("openJericoCupboard", t.player.click_loc, "jericoscupboardshut", 1)
        t.ticks(3) -- let the loc_change land before the second click sees the open form (QUEST_AUTHORING.md section 8)
        t.exec("searchJericoCupboard", t.player.click_loc, "jericoscupboardopen", 1)
        -- The search opens a real ~mesbox (measured run 1: inv_add never
        -- fired, birdfeed stayed absent) that gates the rest of the label --
        -- quest_biohazard_locs.rs2:29-47 runs its guard chain and the grant
        -- only after this page and the follow-up player line are dismissed.
        t.exec("searchJericoCupboard-dialog", t.chat.play, {
            "mesbox:The cupboard is full of birdfeed.",
            "player:Mmm, birdfeed!",
        })
        -- Poll, not a bare read: the grant's inv_add lands a tick or two
        -- behind the dialogue's own last page (QUEST_AUTHORING.md section 8
        -- -- measured run 2: a bare expect_has right after the chat.play
        -- still read absent). t.inv.await answers `ok` with a nil detail
        -- (hollow, measured run 3) -- called directly, count read back after.
        local birdfeed_await_result = t.inv.await("birdfeed", 1, 10)
        local birdfeed_count_result, birdfeed_count = t.inv.count("birdfeed")
        t.step("expectBirdfeed",
            (birdfeed_await_result == "ok" and birdfeed_count_result == "ok" and (birdfeed_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(birdfeed,1,10) -> %s; inv.count(birdfeed) -> %s (%s)",
                tostring(birdfeed_await_result), tostring(birdfeed_count_result), tostring(birdfeed_count)))

        -- Use the bird feed on the watchtower leaf (oplocu,biowatchtower_op,
        -- quest_biohazard_locs.rs2:58-66): needs %biohazard = spoken_jerico
        -- and last_useitem = birdfeed, consumes it, writes ^biohazard_used_birdfeed.
        -- measured run 5: goto_tile(2562,3301,0) then use_on answered `ok`
        -- (settled on the step-off's own map_flag) but no seed/fallback
        -- message ever printed and %biohazard stayed at 2 -- msg.last(6)
        -- showed a genuine "I can't reach that!" AFTER that arrival, so the
        -- watchtower's real clickable tile is not there. world.loc_near
        -- reads the live scene's own placement instead of the guessed
        -- WorldPoint.
        -- world.loc_near needs the player standing somewhere nearby first
        -- (measured run 6: called before the goto, from Jerico's house 65
        -- tiles off, it answered not_found) -- go to the investigate tile
        -- first, then read the live scene's own placement from there.
        t.exec("goto-useBirdfeedOnWatchtower", t.player.goto_tile, 2562, 3301, 0)
        local wt_locate_result, wt_row = t.world.loc_near("biowatchtower_op", 15)
        t.step("watchtower.locate", wt_locate_result == "ok" and "PASS" or "FAIL",
            string.format("world.loc_near(biowatchtower_op,15) -> %s tile=%s,%s,%s match=%s id=%s",
                tostring(wt_locate_result),
                tostring(wt_row and wt_row.tile_x), tostring(wt_row and wt_row.tile_z), tostring(wt_row and wt_row.level),
                tostring(wt_row and wt_row.match), tostring(wt_row and wt_row.id)))
        if wt_locate_result == "ok" and wt_row and wt_row.tile_x
            and (wt_row.tile_x ~= 2562 or wt_row.tile_z ~= 3301) then
            t.exec("goto-useBirdfeedOnWatchtower-2", t.player.goto_tile, wt_row.tile_x, wt_row.tile_z, wt_row.level or 0)
        end
        local watchtower_target, watchtower_lookup = t.player.by_symbol("loc", "biowatchtower_op")
        t.step("watchtower.lookup", watchtower_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc,biowatchtower_op) -> " .. tostring(watchtower_lookup))
        t.exec("useBirdfeedOnWatchtower", t.player.use_on, "birdfeed", watchtower_target)
        t.ticks(2)
        -- RETRY after 73a4251d0: QD.player._reach_retry now walks a loc's
        -- other approach tiles when the server first answers "I can't reach
        -- that!" (and no longer grades that refusal PASS on the map_flag
        -- settle arm), so use_on's press -- previously refused from every
        -- side probed here -- now lands for real (measured: reached from
        -- approach tile 3 of 29) and [oplocu,biowatchtower_op]
        -- (quest_biohazard_locs.rs2:58-66) runs: birdfeed is consumed and
        -- %biohazard advances spoken_jerico(2) -> used_birdfeed(3). The old
        -- diag.postUseOn/watchtower.stageUnchanged rows here asserted the
        -- PRE-fix bug (no message, stage stuck at 2); replaced below with
        -- the fixed behaviour and the quest driven on.
        local birdfeed_left_result, birdfeed_left = t.inv.count("birdfeed")
        t.check("birdfeed.consumed", birdfeed_left_result == "ok" and (birdfeed_left or 0) == 0,
            string.format("inv.count(birdfeed) -> %s (%s), expected 0 -- consumed by [oplocu,biowatchtower_op]",
                tostring(birdfeed_left_result), tostring(birdfeed_left)))
        t.expect("quest.stage.used_birdfeed", t.quest.expect_stage("used_birdfeed"))

        -- Pick up the (empty) pigeon cage behind Jerico's house -- a ground
        -- OBJ (areas/world/configs/m40_51.spawn:116-118), never handed over
        -- in dialogue. click_obj answers `ok` with a nil detail (trap 12's
        -- hollow shape), so called directly with the before/after count as
        -- the real detail (matches test/quests/sheepherder.lua's cattleprod).
        t.exec("goto-pigeonCage", t.player.goto_tile, 2618, 3324, 0)
        local cage_before_result, cage_before = t.inv.count("pigeoncage")
        local cage_pickup_result, cage_pickup_detail = t.player.click_obj("pigeoncage")
        local cage_after_result, cage_after = t.inv.count("pigeoncage")
        t.step("pickupPigeonCage",
            (cage_pickup_result == "ok" and cage_after_result == "ok" and cage_after > (cage_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj(pigeoncage) -> %s (%s); pigeoncage %s -> %s",
                tostring(cage_pickup_result), tostring(cage_pickup_detail), tostring(cage_before), tostring(cage_after)))

        -- ---- The remaining seam: nothing in this content pack ever fills
        -- the cage. ----
        --
        -- [opheld1,pigeons] (quest_biohazard_locs.rs2:69-79) is the ONLY
        -- trigger that writes ^biohazard_released_pigeons (state 3->4), and
        -- it fires on the HELD item "pigeons" ("Pigeon cage" / "It's full of
        -- pigeons.", all.obj:5609, ifop1=Open) -- a distinct obj id from
        -- "pigeoncage" ("Pigeon cage" / "It's empty...", all.obj:5625, no
        -- ifop line at all) that the ground spawn behind Jerico's house
        -- actually grants (areas/world/configs/m40_51.spawn:116-118, three
        -- rows, all "pigeoncage"). No trigger anywhere under OSRS-Content/
        -- ever converts a carried "pigeoncage" into "pigeons" -- whole-tree
        -- grep (`grep -rn pigeoncage OSRS-Content/`) finds only the varp
        -- reset (quest_biohazard.rs2:70), [opheld1,pigeons]'s own
        -- inv_add(inv, pigeoncage, 1) on release, and the three ground spawn
        -- rows -- never a producer of the full "pigeons" item. Both of this
        -- checkout's own C++ selftests confirm the gap by cheating past it
        -- instead of driving it: src/torirsserver/test/quest_biohazard_selftest.u.h:437-439
        -- calls `selftest_give(player, obj_pigeons, 1)` immediately before
        -- firing SS_TRIGGER_OPHELD1 by hand, and
        -- src/torirsserver/torirs_server_world_selftest.c:49160-49161 does
        -- the same with `inv_set(player, 0, obj_pigeons, 1)`.
        --
        -- Probe: walk back to the release zone and press the (empty) cage's
        -- own op1 -- the same op number [opheld1,pigeons] uses -- while
        -- standing where that trigger checks
        -- (inzone(0_39_51_63_35, 0_40_51_5_43), quest_biohazard_locs.rs2:70)
        -- to confirm no [opheld1,pigeoncage] trigger exists and the state
        -- never advances.
        t.exec("goto-releasePigeons", t.player.goto_tile, 2562, 3301, 0)
        local probe_result, probe_detail = t.player.inv_op("pigeoncage", 1)
        t.check("pigeoncage.releaseProbe", probe_result ~= nil,
            "inv_op(pigeoncage, 1) in the release zone -> " .. tostring(probe_result) .. " (" .. tostring(probe_detail) .. ")")
        local release_stage_result, release_stage_value = t.quest.stage()
        t.check("watchtower.stageStillUsedBirdfeed", release_stage_result == "ok" and release_stage_value == 3,
            string.format("quest.stage() -> %s %s (still used_birdfeed=3, not released_pigeons=4 -- pressing the empty cage's own op1 has no [opheld1,pigeoncage] trigger to run)",
                tostring(release_stage_result), tostring(release_stage_value)))

        t.blocked("content_bug: quest_biohazard_locs.rs2:69-79 ([opheld1,pigeons]) is the only " ..
            "writer of ^biohazard_released_pigeons and keys on the HELD item 'pigeons' (all.obj:5609, " ..
            "\"Pigeon cage\"/\"It's full of pigeons.\", ifop1=Open), never the 'pigeoncage' item " ..
            "(all.obj:5625, \"Pigeon cage\"/\"It's empty...\", no ifop line at all) the ground spawn " ..
            "behind Jerico's house actually grants (areas/world/configs/m40_51.spawn:116-118, three " ..
            "rows, all 'pigeoncage'). No trigger anywhere under OSRS-Content/ converts a carried " ..
            "'pigeoncage' into 'pigeons' -- pickupPigeonCage above is this pack's only source of the " ..
            "item and it granted 'pigeoncage', and pressing that item's own op1 in the release zone " ..
            "(pigeoncage.releaseProbe) answered '" .. tostring(probe_result) .. ": " .. tostring(probe_detail) ..
            "' with %biohazard staying at used_birdfeed=3 (watchtower.stageStillUsedBirdfeed), not " ..
            "released_pigeons=4. Both of this checkout's own selftests reach state 4 only by cheating " ..
            "obj_pigeons in directly (src/torirsserver/test/quest_biohazard_selftest.u.h:437-439 " ..
            "selftest_give; src/torirsserver/torirs_server_world_selftest.c:49160-49161 inv_set), " ..
            "confirming no click chain reaches it either. This is now the quest's own first remaining " ..
            "stopper: 73a4251d0's reach-retry fix cleared the earlier driver seam here " ..
            "(useBirdfeedOnWatchtower now lands for real, see quest.stage.used_birdfeed above), so what " ..
            "is left is the missing pigeoncage->pigeons conversion, a content defect, not a driver one.")
        return
    end,
}
