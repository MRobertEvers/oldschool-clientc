-- Biohazard. Scaffolded by tools/quest_gate/new_quest.py from Quest
-- Helper's helpers/quests/biohazard/, then hand-corrected against the
-- pack's own scripts (OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_biohazard/, areas/area_ardougne_east/scripts/elena.rs2,
-- areas/area_combat_training/configs/combat_training.constant) and its
-- own audit doc (docs/quests/biohazard.md). Tier (quest_inventory.tsv): 1.
--
-- RETRY 2026-09-21 (queue: "the ground spawn in m40_51.spawn is now the
-- full cage 'pigeons' -- rewrite pickupPigeonCage to expect item pigeons
-- and drive on"): confirmed live --
-- areas/world/configs/m40_51.spawn:116-118 now spawns THREE ground rows of
-- item 'pigeons' (all.obj:5609, "It's full of pigeons.", ifop1=Open)
-- behind Jerico's house, not the empty 'pigeoncage' the previous content_bug
-- row (864a752cf) was written against. [opheld1,pigeons]
-- (quest_biohazard_locs.rs2:69-79) is therefore reachable by a real click
-- chain now: pick up the ground 'pigeons' item, press its own op1 while
-- standing in the release zone. The content_bug this file previously ended
-- on is gone -- rewritten below (pickupPigeons/releasePigeons) and the
-- quest driven the rest of the way to completion using every remaining
-- .rs2 in quest_biohazard/, areas/area_ardougne_east/{elena,king_lathas}.rs2,
-- areas/area_ardougne_west/{mourner,doors}.rs2, areas/varrock/east_gate.rs2
-- and quests/quest_eaglepeak/asyff.rs2 (Biohazard's own priest-gown arm),
-- plus docs/quests/biohazard.md section 11's shipped route and its
-- selftest PASS trigger list, and coordinates decoded from
-- quest_biohazard.constant's ^biohazard_west_wall_dest/^east_wall_dest/
-- ^biohazard_crate_coord_a/b plus `questhelper_extract.py biohazard --check`'s
-- WorldPoint table (npc/loc placements have no *.spawn-style text source of
-- their own past the ones grepped below -- trap 20 -- so click_loc/use_on's
-- own three-rule resolve is trusted for the mourner-HQ locs the same way
-- watchtower/cauldron leaves were before).
--
-- The mournerstew2 encounter (Quest Helper step "infiltrateMourners") is a
-- REAL fight, not the doc's "cheat-skip only" debugproc
-- (biohazard_pass_mourner exists purely as a selftest shortcut, per
-- quest_biohazard.rs2:85-91's own comment and docs/quests/biohazard.md
-- section 11's "mournerstew2 has no authored combat block (named
-- cheat-skip only)" -- misleading: mourner.rs2:47-78's [opnpc1,mournerstew2]
-- and its ai_queue3 key-drop queue are real, driven here with
-- player.attack/npc.await_dead_engaged, not the debug cheat); combat gear
-- is a prerequisite (trap 16/26), staged in setup like mortton.lua's shades.
--
-- Fixes to the generator's first guess (kept from the green/content_bug
-- history):
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
-- East Ardougne via ::goto. Pure-navigation hops with no state-changing
-- trigger of their own (Kilron's return wall crossing, walking through the
-- Varrock east gate once no vial is carried) are driven with goto_tile
-- straight to the next real interaction, the same way this suite already
-- treats a ladder or a door (section 2: "no click_loc on the ladder
-- first").

return {
    id = "biohazard",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so gasmask fits
        "::biohazardreset",
        "::give gasmask 1", -- required equipment (Quest Helper's getItemRequirements()); worn at Omart, never the quest's own deliverable
        "::complete quest_plaguecity", -- Biohazard's only prerequisite (all.dbrow:6045 quest_biohazard's requirement_quests=36 -> quest_plaguecity)
        -- Combat gear prerequisite for the real mournerstew2 fight
        -- (mourner.rs2:47-78/79-83; recommended combat level 10, a level 13
        -- mourner) -- same idiom as mortton.lua's Loar shades, trap 26.
        "::give rune_scimitar 1",
        "::setlevel hitpoints 99",
        "::setlevel attack 70",
        "::setlevel strength 70",
        "::setlevel defence 70",
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

        -- Worn before anything else -- Omart's check is on worn state at
        -- the moment of crossing, not on when the mask was donned.
        t.exec("equipGasmask", t.player.equip, "gasmask")

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
        local birdfeed_left_result, birdfeed_left = t.inv.count("birdfeed")
        t.check("birdfeed.consumed", birdfeed_left_result == "ok" and (birdfeed_left or 0) == 0,
            string.format("inv.count(birdfeed) -> %s (%s), expected 0 -- consumed by [oplocu,biowatchtower_op]",
                tostring(birdfeed_left_result), tostring(birdfeed_left)))
        t.expect("quest.stage.used_birdfeed", t.quest.expect_stage("used_birdfeed"))

        -- Pick up the pigeons -- a ground OBJ behind Jerico's house
        -- (areas/world/configs/m40_51.spawn:116-118), never handed over in
        -- dialogue. RETRY fix: the live ground spawn is now the FULL item
        -- 'pigeons' (all.obj:5609, ifop1=Open), not the empty 'pigeoncage'
        -- this row previously expected -- [opheld1,pigeons]
        -- (quest_biohazard_locs.rs2:69-79) is the trigger that both
        -- releases them AND is the only writer of
        -- ^biohazard_released_pigeons, so picking up 'pigeons' here is what
        -- actually lets the rest of the quest run. click_obj answers `ok`
        -- with a nil detail (trap 12's hollow shape), so called directly
        -- with the before/after count as the real detail.
        t.exec("goto-pigeons", t.player.goto_tile, 2618, 3324, 0)
        local pigeons_before_result, pigeons_before = t.inv.count("pigeons")
        local pigeons_pickup_result, pigeons_pickup_detail = t.player.click_obj("pigeons")
        local pigeons_after_result, pigeons_after = t.inv.count("pigeons")
        t.step("pickupPigeons",
            (pigeons_pickup_result == "ok" and pigeons_after_result == "ok" and pigeons_after > (pigeons_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj(pigeons) -> %s (%s); pigeons %s -> %s",
                tostring(pigeons_pickup_result), tostring(pigeons_pickup_detail), tostring(pigeons_before), tostring(pigeons_after)))

        -- Release them in the authored zone (inzone(0_39_51_63_35,
        -- 0_40_51_5_43) = worldX 2559-2565, worldZ 3299-3307 -- the
        -- investigate/watchtower tile 2562,3301 already sits inside it) by
        -- pressing the held item's own op1 -- [opheld1,pigeons] itself,
        -- not a second click on the watchtower. inv_op is not the hollow
        -- shape (it reports its own press count/condition), so t.exec.
        t.exec("goto-releasePigeons", t.player.goto_tile, 2562, 3301, 0)
        t.exec("releasePigeons", t.player.inv_op, "pigeons", 1)
        t.ticks(2)
        local pigeoncage_result, pigeoncage_count = t.inv.count("pigeoncage")
        t.check("pigeoncage.returned", pigeoncage_result == "ok" and (pigeoncage_count or 0) >= 1,
            string.format("inv.count(pigeoncage) -> %s (%s), expected >=1 -- [opheld1,pigeons] hands back the empty cage",
                tostring(pigeoncage_result), tostring(pigeoncage_count)))
        t.expect("quest.stage.released_pigeons", t.quest.expect_stage("released_pigeons"))

        -- ==== Omart, the wall crossing (2559,3266,0; [opnpc1,omart]) ====
        -- At ^biohazard_released_pigeons, choosing to cross (gasmask worn,
        -- already equipped above) runs @biohazard_climb_ladder itself --
        -- the ladder loc is never clicked (section 2's floors-and-ladders
        -- rule extends to this talk-triggered crossing too).
        t.exec("goto-talkToOmart", t.player.goto_tile, 2559, 3266, 0)
        t.exec("talkToOmart", t.player.talk_to, "omart", 1)
        t.exec("talkToOmart-dialog", t.chat.play, {
            "npc:Well done, the guards are having real trouble",
            "options",
            "choose:Okay, let's do it.",
            "player:Okay, let's do it.",
            "end",
        })
        t.ticks(3) -- @biohazard_climb_ladder's own p_delay(3) before the teleport lands
        t.expect("quest.stage.climbed_ladder", t.quest.expect_stage("climbed_ladder"))

        -- RETRY: guide step enterBackyardOfHeadquarters ("Squeeze through
        -- the fence to enter the Mourner's Headquarters yard") is a real
        -- click, ObjectID.MOURNERSTEWFENCE at WorldPoint(2541,3331,0) --
        -- decoded from the map text itself (trap 29:
        -- OSRS-Content/osrs239-content/maps/m39_52.jl2:2269, "0 45 3: 2068
        -- 0 2" -> level 0, localx 45 localz 3 in mapsquare 39,52 ->
        -- 39*64+45,52*64+3 = 2541,3331). [oploc1,mournerstewfence]
        -- (general_use/scripts/fence.rs2:5-32) is unconditional -- no gown,
        -- no stage check, just an agility_exactmove squeeze-through with no
        -- chat and no mes() -- so the goto straight to the rotten apple
        -- this file previously used was cheating past it. Approach from
        -- outside the yard (west of the fence tile) and click it; the
        -- squeeze's own p_teleport(end) lands inside, then the existing
        -- goto to the apple is plain movement within the now-open yard.
        t.exec("goto-mournerFenceApproach", t.player.goto_tile, 2538, 3331, 0)
        t.exec("enterBackyardOfHeadquarters", t.player.click_loc, "mournerstewfence", 1)
        -- MEASURED run 2: a bare world.tile() read right after the click
        -- caught the squeeze mid-animation (still 2541,3331,0, the fence's
        -- own loc_coord -- fence.rs2's agility_exactmove takes several
        -- ticks) and FAILed on a row the very next goto's own retry detail
        -- proved had actually landed ("still at 2542,3331,0 after 10
        -- tick(s)" mid-flight, then arriving 2549,3332,0 on attempt 2).
        -- Poll instead of a single read (QUEST_AUTHORING.md section 8's
        -- "poll, not a bare read").
        local fence_settle_result, fence_settle_detail = t.await({
            level = function()
                local tile_result, tile = t.world.tile()
                return tile_result == "ok" and tile and tile.x and tile.x > 2541
            end,
            note = "fence.crossed_settle",
        }, 10)
        t.step("enterBackyardOfHeadquarters.crossed", fence_settle_result == "ok" and "PASS" or "FAIL",
            "await(world.tile().x > 2541) after the fence squeeze -> "
                .. tostring(fence_settle_result) .. " " .. tostring(fence_settle_detail))

        -- Get a rotten apple off the ground west of the wall
        -- (areas/world/configs/m39_52.spawn:56, 2549,3332,0 -- right by the
        -- mournercauldron courtyard) and poison the mourners' stew with it
        -- (oplocu,mournercauldron_op, quest_biohazard_locs.rs2:103-121;
        -- the visible leaf per the shipped fix, trap 20's base/child resolve
        -- the same as biowatchtower above).
        t.exec("goto-rottenApple", t.player.goto_tile, 2549, 3332, 0)
        local apple_before_result, apple_before = t.inv.count("rottenapples")
        local apple_pickup_result, apple_pickup_detail = t.player.click_obj("rottenapples")
        local apple_after_result, apple_after = t.inv.count("rottenapples")
        t.step("takeRottenApple",
            (apple_pickup_result == "ok" and apple_after_result == "ok" and apple_after > (apple_before or 0)) and "PASS" or "FAIL",
            string.format("click_obj(rottenapples) -> %s (%s); rottenapples %s -> %s",
                tostring(apple_pickup_result), tostring(apple_pickup_detail), tostring(apple_before), tostring(apple_after)))

        local cauldron_locate_result, cauldron_row = t.world.loc_near("mournercauldron_op", 15)
        t.step("cauldron.locate", cauldron_locate_result == "ok" and "PASS" or "FAIL",
            string.format("world.loc_near(mournercauldron_op,15) -> %s tile=%s,%s,%s match=%s id=%s",
                tostring(cauldron_locate_result),
                tostring(cauldron_row and cauldron_row.tile_x), tostring(cauldron_row and cauldron_row.tile_z), tostring(cauldron_row and cauldron_row.level),
                tostring(cauldron_row and cauldron_row.match), tostring(cauldron_row and cauldron_row.id)))
        if cauldron_locate_result == "ok" and cauldron_row and cauldron_row.tile_x then
            t.exec("goto-cauldron", t.player.goto_tile, cauldron_row.tile_x, cauldron_row.tile_z, cauldron_row.level or 0)
        end
        local cauldron_target, cauldron_lookup = t.player.by_symbol("loc", "mournercauldron_op")
        t.step("cauldron.lookup", cauldron_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc,mournercauldron_op) -> " .. tostring(cauldron_lookup))
        t.exec("poisonCauldron", t.player.use_on, "rottenapples", cauldron_target)
        -- MEASURED run 2: the success branch's own %biohazard write sits
        -- AFTER an internal p_delay(3) (quest_biohazard_locs.rs2:115-118 --
        -- inv_del/mes run immediately, THEN p_delay(3), THEN the write), so
        -- a flat t.ticks(3) read the var one tick too early (client=server=5
        -- right then) and the write landed several rows later instead
        -- (found_distillator's own check read client=6 by then). Poll for
        -- the real transition instead of a fixed wait.
        local poison_settle_result, poison_settle_detail = t.await({
            level = function()
                local stage_result, stage_value = t.quest.stage()
                return stage_result == "ok" and stage_value == 6
            end,
            note = "cauldron.poisoned_settle",
        }, 15)
        t.step("cauldron.poisonedSettle", poison_settle_result == "ok" and "PASS" or "FAIL",
            "await(quest.stage()==poisoned_stew) after use_on's internal p_delay(3) -> "
                .. tostring(poison_settle_result) .. " " .. tostring(poison_settle_detail))
        t.expect("quest.stage.poisoned_stew", t.quest.expect_stage("poisoned_stew"))

        -- Search the nurse's cupboard for a doctor's gown -- only fills
        -- once %biohazard >= ^biohazard_poisoned_stew (quest_biohazard_locs.rs2:
        -- 123-142). MEASURED run 1: a hand-guessed WorldPoint (2544,3326,0)
        -- landed beside a Painting/Dead tree, not the cupboard, and
        -- world.loc_near(bionursescupboardshut,20) from the cauldron
        -- courtyard answered not_found -- mourner1's own line
        -- ("The nurse's hut is to the south west", mourner.rs2:41) says the
        -- hut is a SEPARATE building; goto its WorldPoint
        -- (`questhelper_extract.py biohazard --check`'s (2518,3276,0))
        -- first, then resolve the live placement from there.
        t.exec("goto-nurseHut", t.player.goto_tile, 2518, 3276, 0)
        local nursecupboard_locate_result, nursecupboard_row = t.world.loc_near("bionursescupboardshut", 25)
        t.step("nurseCupboard.locate", nursecupboard_locate_result == "ok" and "PASS" or "FAIL",
            string.format("world.loc_near(bionursescupboardshut,25) -> %s tile=%s,%s,%s match=%s id=%s",
                tostring(nursecupboard_locate_result),
                tostring(nursecupboard_row and nursecupboard_row.tile_x), tostring(nursecupboard_row and nursecupboard_row.tile_z), tostring(nursecupboard_row and nursecupboard_row.level),
                tostring(nursecupboard_row and nursecupboard_row.match), tostring(nursecupboard_row and nursecupboard_row.id)))
        if nursecupboard_locate_result == "ok" and nursecupboard_row and nursecupboard_row.tile_x then
            t.exec("goto-nurseCupboard", t.player.goto_tile, nursecupboard_row.tile_x, nursecupboard_row.tile_z, nursecupboard_row.level or 0)
        end
        t.exec("openNurseCupboard", t.player.click_loc, "bionursescupboardshut", 1)
        t.ticks(3)
        t.exec("searchNurseCupboard", t.player.click_loc, "bionursescupboardopen", 1)
        local gown_await_result = t.inv.await("doctor_gown", 1, 10)
        local gown_count_result, gown_count = t.inv.count("doctor_gown")
        t.step("expectDoctorGown",
            (gown_await_result == "ok" and gown_count_result == "ok" and (gown_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(doctor_gown,1,10) -> %s; inv.count(doctor_gown) -> %s (%s)",
                tostring(gown_await_result), tostring(gown_count_result), tostring(gown_count)))
        t.exec("equipDoctorGown", t.player.equip, "doctor_gown")

        -- RETRY (queue: "enterMournerHeadquarters is CHEAT -- goto_tile
        -- lands past the door the guide names (mournerstewdoor)"): the
        -- previous attempt's goto_tile straight to 2551,3327,1 skipped this
        -- click outright. Biohazard.java's own enterMournerHeadquarters
        -- step is an ObjectStep on MOURNERSTEWDOOR at WorldPoint(2551,
        -- 3320,0) -- the SAME tile and loc test/quests/mourningsendparti.lua:
        -- 476-477 already clicks successfully for its own (later) leg
        -- through this door, so the earlier claim that click_loc could not
        -- settle here does not hold. At %biohazard=poisoned_stew|
        -- found_distillator with the gown worn (doors.rs2:19-24 ->
        -- @west_ardougne_mourner_headquarters_doors ->
        -- doors.rs2:76-101), the guard binds mourner_armed_guard and opens
        -- a real chatnpc_specific_anim page ("In you go doc."), not a
        -- silent trigger -- p_pausebutton (chat.rs2:117), so it is a
        -- t.chat.play page, then if_close + the silent forcemove
        -- ~west_ardy_walk_door (doors.rs2:115-131, no mes of its own, so
        -- the crossing is confirmed by tile only, same as trap 20's
        -- teleport-door case).
        t.exec("goto-mournerHqDoor", t.player.goto_tile, 2551, 3320, 0)
        t.exec("enterMournerHeadquarters", t.player.click_loc, "mournerstewdoor", 1)
        t.exec("mournerHqDoor-dialog", t.chat.play, {
            "npc:In you go doc.",
            "end",
        })
        t.ticks(2) -- west_ardy_walk_door's own p_telejump lands a tick or two after if_close
        local hq_tile_result, hq_tile = t.world.tile()
        t.step("enterMournerHeadquarters.crossed",
            (hq_tile_result == "ok" and hq_tile and (hq_tile.x ~= 2551 or hq_tile.z ~= 3320)) and "PASS" or "FAIL",
            string.format("world.tile() -> %s %s,%s,%s (expected off the door tile 2551,3320 after west_ardy_walk_door's forcemove)",
                tostring(hq_tile_result), tostring(hq_tile and hq_tile.x), tostring(hq_tile and hq_tile.z), tostring(hq_tile and hq_tile.level)))

        -- Upstairs, the sick mourner (mournerstew2, m39_51.spawn:83,
        -- 2551,3327,1 -- goto_tile climbs the stairs itself, section 2).
        -- Any of the three answers ends in ~npc_retaliate(0) (mourner.rs2:
        -- 47-73); the shortest branch (option 1) is driven here. MEASURED
        -- run 2: the rune_scimitar setup gave was never worn, so 60 ticks
        -- of re-engagement only chipped 15/30 hp off a level 13 target --
        -- equip it before the fight (trap 26).
        t.exec("equipRuneScimitar", t.player.equip, "rune_scimitar")
        t.exec("goto-mournerstew2", t.player.goto_tile, 2551, 3327, 1)
        t.exec("talkToMournerstew2", t.player.talk_to, "mournerstew2", 1)
        t.exec("mournerstew2-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:You're here at last!",
            "player:Hmm... interesting, sounds like food poisoning.",
            "npc:Yes, I'd figured that out already.",
            "options",
            "choose:Just hold your breath and count to ten.",
            "player:Just hold your breath and count to ten.",
            "npc:What? How will that help?",
            "player:Erm... I'm new, I just started.",
            "npc:You're no doctor!",
            "end",
        })
        -- t.player.attack takes the npc SYMBOL directly (combat.lua's
        -- QD.player.attack resolves it itself through
        -- by_symbol/npc.nearest) -- not a resolved {kind,id} table
        -- (mortton.lua's own note on the same mistake); handing it the
        -- npc.nearest row raised `bad argument #2 to 'symbol' (string
        -- expected, got table)` (measured run 1).
        local mournerstew2_find_result = t.npc.nearest("mournerstew2", 10)
        t.check("mournerstew2.find", mournerstew2_find_result == "ok",
            "npc.nearest(mournerstew2, 10) -> " .. tostring(mournerstew2_find_result))
        -- MEASURED run 3 (15 ticks), run 5 (25 ticks) AND run 6 (50 ticks,
        -- attack raised 40->70): every one still reads `timeout` -- "no hit
        -- landed" -- while npc.await_dead_engaged below always goes on to
        -- win the SAME fight a little later regardless. combat.lua's
        -- QD.player.attack stamps QD._combat_last (the engagement
        -- await_dead_engaged re-presses on) on the timeout path too, past
        -- its own settle check -- a click that pressed a real Attack row
        -- always leaves it, hit or miss. Section 8/trap 26's own words:
        -- "A timeout is a miss streak..., NOT a failed click -- raise
        -- ticks, and read npc.await_dead's row for whether the fight was
        -- won." Raising ticks/accuracy three times running did not change
        -- the outcome, so this row is graded on the PRESS landing a real
        -- Attack row, not on a splat arriving inside its own window -- the
        -- next row is the actual verdict on the fight.
        local mournerstew2_attack_result, mournerstew2_attack_detail = t.player.attack("mournerstew2", 2, 50)
        t.check("mournerstew2.attack",
            mournerstew2_attack_result == "ok" or mournerstew2_attack_result == "timeout",
            "attack(mournerstew2,2,50) -> " .. tostring(mournerstew2_attack_result) .. " " .. tostring(mournerstew2_attack_detail))
        t.exec("mournerstew2.dead", t.npc.await_dead_engaged, 60)
        t.expect("player.aliveAfterMourner", t.player.alive())

        -- ai_queue3,mournerstew2 (mourner.rs2:79-97) fires
        -- defeat_biohazard_mourner once its hero-npc queue settles and
        -- grants the key (~biohazard_give(mournerkeytw)) -- t.inv.await is
        -- hollow (trap 12), so called directly and the count read back.
        local key_await_result = t.inv.await("mournerkeytw", 1, 15)
        local key_count_result, key_count = t.inv.count("mournerkeytw")
        t.step("expectMournerKey",
            (key_await_result == "ok" and key_count_result == "ok" and (key_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(mournerkeytw,1,15) -> %s; inv.count(mournerkeytw) -> %s (%s)",
                tostring(key_await_result), tostring(key_count_result), tostring(key_count)))

        -- Search the exact third-from-left crate (^biohazard_crate_coord_a/b
        -- decode to 2554,3327,1 / 2555,3327,1 -- quest_biohazard.constant)
        -- for Elena's distillator (quest_biohazard_locs.rs2:249-281); still
        -- %biohazard=poisoned_stew and gown worn, both required.
        t.exec("goto-mournerCrate", t.player.goto_tile, 2554, 3327, 1)
        t.exec("searchMournerCrate", t.player.click_loc, "mournercrateup", 1)
        local distillator_await_result = t.inv.await("distillator", 1, 10)
        local distillator_count_result, distillator_count = t.inv.count("distillator")
        t.step("expectDistillator",
            (distillator_await_result == "ok" and distillator_count_result == "ok" and (distillator_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(distillator,1,10) -> %s; inv.count(distillator) -> %s (%s)",
                tostring(distillator_await_result), tostring(distillator_count_result), tostring(distillator_count)))
        t.expect("quest.stage.found_distillator", t.quest.expect_stage("found_distillator"))

        -- ==== Back to Elena with the distillator ====
        -- elena.rs2:60-94, case ^biohazard_found_distillator: hands over
        -- the distillator for three vials + a sample. TWO if_close/reopen
        -- boundaries sit in this one branch (line 73 and, MEASURED run 3,
        -- a second one at line 78 this file's plan first missed -- "the
        -- dialogue closed after 2 page(s)" on the item-grant lines), so
        -- three chat.play lists with two awaits between them
        -- (QUEST_AUTHORING.md section 8's payout-reopen trap;
        -- hunt.lua's hunt.luthas_payout_reopen is the worked example).
        t.exec("goto-elenaDistillator", t.player.goto_tile, 2592, 3336, 0)
        t.exec("talkToElenaDistillator", t.player.talk_to, "elena2_vis", 1)
        t.exec("elenaDistillator-dialog-1", t.chat.play, {
            "npc:So, have you managed to retrieve my distillator?",
            "player:Yes, here it is!",
            "npc:You have? That's great!",
            "player:Those look pretty fancy.",
            "npc:Well, yes and no.",
            "player:You're not kidding, I can smell it from here!",
            "end",
        })
        local elena_reopen_result, elena_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "elena.distillator_reopen",
        }, 10)
        t.step("elena.distillatorReopen", elena_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after if_close+mes+p_delay(2) -> "
                .. tostring(elena_reopen_result) .. " " .. tostring(elena_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("elenaDistillator-dialog-2", t.chat.play, {
            "npc:I don't understand... the touch paper hasn't changed colour",
            "npc:You'll need to go and see my old mentor Guidor.",
            "end",
        })
        local elena_reopen2_result, elena_reopen2_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "elena.distillator_reopen_2",
        }, 10)
        t.step("elena.distillatorReopen2", elena_reopen2_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after a SECOND if_close+inv grant+p_delay(2) -> "
                .. tostring(elena_reopen2_result) .. " " .. tostring(elena_reopen2_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("elenaDistillator-dialog-3", t.chat.play, {
            "npc:But first you'll need some more touch paper.",
            "npc:Just don't get into any fights",
            "npc:Those vials are fragile",
            "end",
        })
        local vials_await_result = t.inv.await_all({
            liquid_honey = 1,
            ethenea = 1,
            sulphuric_broline = 1,
            plaguesample = 1,
        }, 10)
        t.step("expectVialsAndSample", vials_await_result == "ok" and "PASS" or "FAIL",
            "inv.await_all({liquid_honey=1,ethenea=1,sulphuric_broline=1,plaguesample=1},10) -> " .. tostring(vials_await_result))
        t.expect("quest.stage.given_distillator", t.quest.expect_stage("given_distillator"))

        -- ==== The Chemist, Rimmington (2934,3210,0; [opnpc1,chemist]) ====
        -- case ^biohazard_given_distillator: choosing "This can't wait,
        -- I'm carrying a plague sample." runs straight into
        -- @chemist_touchpaperguidor, granting touch_paper and writing
        -- ^biohazard_spoken_chemist (chemist.rs2:39-48,84-97).
        t.exec("goto-chemist", t.player.goto_tile, 2934, 3210, 0)
        t.exec("talkToChemist", t.player.talk_to, "chemist", 1)
        t.exec("chemist-dialog", t.chat.play, {
            "npc:Sorry, I'm afraid we're just closing now.",
            "options",
            "choose:This can't wait, I'm carrying a plague sample.",
            "player:This can't wait, I'm carrying a plague sample",
            "npc:A plague sample? Keep it sealed.",
            "player:Who knows... I just need some touch paper for a guy called Guidor.",
            "npc:Guidor? This one's on me then",
            "npc:It's just that there've been rumours",
            "npc:They're even doing spot checks in Varrock.",
            "player:Oh right... so am I going to be ok carrying these three vials",
            "npc:With touch paper as well?",
            "npc:They're not the most reliable people in the world.",
            "npc:It's better than entering Varrock",
            "player:Ok, thanks for your help.",
            "npc:Yes well don't stand around here gassing.",
            "end",
        })
        local touchpaper_await_result = t.inv.await("touch_paper", 1, 10)
        local touchpaper_count_result, touchpaper_count = t.inv.count("touch_paper")
        t.step("expectTouchPaper",
            (touchpaper_await_result == "ok" and touchpaper_count_result == "ok" and (touchpaper_count or 0) >= 1) and "PASS" or "FAIL",
            string.format("inv.await(touch_paper,1,10) -> %s; inv.count(touch_paper) -> %s (%s)",
                tostring(touchpaper_await_result), tostring(touchpaper_count_result), tostring(touchpaper_count)))
        t.expect("quest.stage.spoken_chemist", t.quest.expect_stage("spoken_chemist"))

        -- ==== Errand boys, Rimmington: hand each the CORRECT vial ====
        -- (errand_boys.rs2). drunk1/gambler1/artist1 spawn rows put them
        -- within a few tiles of the chemist; goto once, talk to each.
        t.exec("goto-drunk1", t.player.goto_tile, 2930, 3218, 0)
        t.exec("talkToDrunk1", t.player.talk_to, "drunk1", 1)
        -- MEASURED run 3: choosing a vial does NOT echo the choice text
        -- back as its own player page -- hops_sulphuricbroline's label
        -- jumps straight to "Ok, I'll see you in Varrock." after the
        -- mes()-only grant line, so an echo entry here is a mismatch.
        t.exec("drunk1-dialog", t.chat.play, {
            "player:Hi, I've got something for you to take to Varrock.",
            "npc:Sounds like pretty thirsty work.",
            "player:Well, there's an Inn in Varrock",
            "npc:Don't worry, I'm a pretty resourceful fellow",
            "options",
            "choose:You give him the vial of sulphuric broline...",
            "player:Ok, I'll see you in Varrock.",
            "npc:Sure, I'm a regular at the Dancing Donkey Inn",
            "end",
        })
        local drunk1_sulphuric_result, drunk1_sulphuric = t.inv.count("sulphuric_broline")
        t.check("drunk1.gaveCorrectVial", drunk1_sulphuric_result == "ok" and (drunk1_sulphuric or 0) == 0,
            string.format("inv.count(sulphuric_broline) -> %s (%s), expected 0 -- hops_sulphuricbroline sets hops_correct",
                tostring(drunk1_sulphuric_result), tostring(drunk1_sulphuric)))

        t.exec("goto-gambler1", t.player.goto_tile, 2929, 3222, 0)
        t.exec("talkToGambler1", t.player.talk_to, "gambler1", 1)
        -- MEASURED run 3: missed the branch's own opening PLAYER line
        -- (trap 18) -- gambler1.rs2 opens with the player's greeting, not
        -- Chancy's own line, and (as with drunk1 above) the choice is not
        -- echoed back as its own page.
        t.exec("gambler1-dialog", t.chat.play, {
            "player:Hello, I've got a vial for you to take to Varrock.",
            "npc:Tssch... that chemist asks for a lot",
            "player:Maybe you should ask him for more money.",
            "npc:Nah... I just use my initiative",
            "options",
            "choose:You give him the vial of liquid honey...",
            "player:Right. I'll see you later in the Dancing Donkey Inn.",
            "npc:Be lucky!",
            "end",
        })
        local gambler1_honey_result, gambler1_honey = t.inv.count("liquid_honey")
        t.check("gambler1.gaveCorrectVial", gambler1_honey_result == "ok" and (gambler1_honey or 0) == 0,
            string.format("inv.count(liquid_honey) -> %s (%s), expected 0 -- chancy_liquidhoney sets chancy_correct",
                tostring(gambler1_honey_result), tostring(gambler1_honey)))

        t.exec("goto-artist1", t.player.goto_tile, 2927, 3218, 0)
        t.exec("talkToArtist1", t.player.talk_to, "artist1", 1)
        -- MEASURED run 3: missed the branch's own opening PLAYER line
        -- (trap 18), same shape as gambler1 above.
        t.exec("artist1-dialog", t.chat.play, {
            "player:Hello, I hear you're an errand boy for the chemist.",
            "npc:Well that's my job yes.",
            "player:Good for you.",
            "npc:Go on then.",
            "options",
            "choose:You give him the vial of ethenea...",
            "player:Ok, we're meeting at the Dancing Donkey in Varrock right?.",
            "npc:That's right.",
            "end",
        })
        local artist1_ethenea_result, artist1_ethenea = t.inv.count("ethenea")
        t.check("artist1.gaveCorrectVial", artist1_ethenea_result == "ok" and (artist1_ethenea or 0) == 0,
            string.format("inv.count(ethenea) -> %s (%s), expected 0 -- devinci_ethenea sets devinci_correct",
                tostring(artist1_ethenea_result), tostring(artist1_ethenea)))

        -- ==== Collect the vials back in Varrock (Dancing Donkey Inn) ====
        -- Two of the three collection branches (drunk2/gambler2) call
        -- if_close right after their own npc page and reopen a fresh one
        -- (the same payout-reopen shape as Elena's distillator handoff
        -- above); artist2's does not.
        t.exec("goto-drunk2", t.player.goto_tile, 3268, 3389, 0)
        t.exec("talkToDrunk2", t.player.talk_to, "drunk2", 1)
        t.exec("drunk2-dialog-1", t.chat.play, {
            "player:Hello, how was your journey?",
            "npc:Pretty thirst-inducing actually",
            "player:Please tell me that you haven't drunk the contents",
            "npc:Oh the gods no!",
            "npc:Here's your vial anyway.",
            "end",
        })
        local drunk2_reopen_result, drunk2_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "player"
            end,
            note = "drunk2.collect_reopen",
        }, 10)
        t.step("drunk2.collectReopen", drunk2_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == player) after if_close+mes+p_delay(2) -> "
                .. tostring(drunk2_reopen_result) .. " " .. tostring(drunk2_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("drunk2-dialog-2", t.chat.play, {
            "player:Thanks, I'll let you get your drink now.",
            "end",
        })
        local drunk2_vial_result, drunk2_vial = t.inv.count("sulphuric_broline")
        t.check("drunk2.returnedVial", drunk2_vial_result == "ok" and (drunk2_vial or 0) >= 1,
            string.format("inv.count(sulphuric_broline) -> %s (%s), expected >=1", tostring(drunk2_vial_result), tostring(drunk2_vial)))

        t.exec("goto-gambler2", t.player.goto_tile, 3271, 3388, 0)
        -- MEASURED run 3 AND run 4: the raw click misses the npc model
        -- entirely ("none of 99 pixels...holds it"), identically, twice in
        -- a row, from every pose click_minimenu's own hunt already tries --
        -- a real driver pixel-search seam on this specific cramped spot
        -- (three errand boys within a couple of tiles of the Dancing Donkey
        -- Inn door), not a one-off miss a plain retry fixes. Section 3's
        -- `t.drive.op` is the documented last resort for exactly this --
        -- "the logged bypass, never the default" -- used only after both
        -- a normal press and one retry have already failed, and noted as
        -- such rather than claimed as proof the tile was reachable.
        local gambler2_talk_result, gambler2_talk_detail = t.player.talk_to("gambler2", 1)
        if gambler2_talk_result ~= "ok" then
            gambler2_talk_result, gambler2_talk_detail = t.player.talk_to("gambler2", 1)
        end
        if gambler2_talk_result ~= "ok" then
            local gambler2_op_target = t.player.by_symbol("npc", "gambler2")
            if gambler2_op_target then
                gambler2_talk_result, gambler2_talk_detail = t.drive.op(gambler2_op_target, 1)
                gambler2_talk_detail = "[bypass after 2 failed on-screen presses] " .. tostring(gambler2_talk_detail)
                t.ticks(2) -- world_op sends the packet with no settle wait of its own; give the page a moment to open
            end
        end
        t.check("talkToGambler2", gambler2_talk_result == "ok",
            "talk_to(gambler2,1) -> " .. tostring(gambler2_talk_result) .. " " .. tostring(gambler2_talk_detail))
        t.exec("gambler2-dialog-1", t.chat.play, {
            "player:Hi, thanks for doing that.",
            "npc:No problem.",
            "end",
        })
        local gambler2_reopen_result, gambler2_reopen_detail = t.await({
            level = function()
                return t.chat.kind() == "npc"
            end,
            note = "gambler2.collect_reopen",
        }, 10)
        t.step("gambler2.collectReopen", gambler2_reopen_result == "ok" and "PASS" or "FAIL",
            "await(chat.kind() == npc) after if_close+mes+p_delay(2) -> "
                .. tostring(gambler2_reopen_result) .. " " .. tostring(gambler2_reopen_detail)
                .. " -- chat.kind() now " .. tostring(t.chat.kind()))
        t.exec("gambler2-dialog-2", t.chat.play, {
            "npc:Next time give me something more valuable",
            "player:That was the idea.",
            "end",
        })
        local gambler2_vial_result, gambler2_vial = t.inv.count("liquid_honey")
        t.check("gambler2.returnedVial", gambler2_vial_result == "ok" and (gambler2_vial or 0) >= 1,
            string.format("inv.count(liquid_honey) -> %s (%s), expected >=1", tostring(gambler2_vial_result), tostring(gambler2_vial)))

        t.exec("goto-artist2", t.player.goto_tile, 3272, 3389, 0)
        -- Same cramped-cluster click miss as gambler2 above (MEASURED run 3
        -- and run 4, identically); same retry-then-bypass fallback.
        local artist2_talk_result, artist2_talk_detail = t.player.talk_to("artist2", 1)
        if artist2_talk_result ~= "ok" then
            artist2_talk_result, artist2_talk_detail = t.player.talk_to("artist2", 1)
        end
        if artist2_talk_result ~= "ok" then
            local artist2_op_target = t.player.by_symbol("npc", "artist2")
            if artist2_op_target then
                artist2_talk_result, artist2_talk_detail = t.drive.op(artist2_op_target, 1)
                artist2_talk_detail = "[bypass after 2 failed on-screen presses] " .. tostring(artist2_talk_detail)
                t.ticks(2)
            end
        end
        t.check("talkToArtist2", artist2_talk_result == "ok",
            "talk_to(artist2,1) -> " .. tostring(artist2_talk_result) .. " " .. tostring(artist2_talk_detail))
        t.exec("artist2-dialog", t.chat.play, {
            "npc:Hello again.",
            "player:Well, as they say, it's always sunny in RuneScape.",
            "npc:Ok, here it is.",
            "player:Thanks, you've been a big help.",
            "end",
        })
        local artist2_vial_result, artist2_vial = t.inv.count("ethenea")
        t.check("artist2.returnedVial", artist2_vial_result == "ok" and (artist2_vial or 0) >= 1,
            string.format("inv.count(ethenea) -> %s (%s), expected >=1", tostring(artist2_vial_result), tostring(artist2_vial)))

        -- ==== Asyff/tailorp, Varrock (3281,3398,0): free priest gown ====
        -- (quests/quest_eaglepeak/scripts/asyff.rs2:46-81) -- offered while
        -- %biohazard is between spoken_chemist and found_secret and
        -- %biohazard_free_clothes=0, both true here.
        t.exec("goto-asyff", t.player.goto_tile, 3281, 3398, 0)
        t.exec("talkToAsyff", t.player.talk_to, "tailorp", 1)
        t.exec("asyff-dialog", t.chat.play, {
            "npc:Now you look like someone who goes to a lot of fancy dress parties.",
            "player:Errr... what are you saying exactly?",
            "npc:I'm just saying that perhaps you would like to peruse my selection of garments.",
            "options",
            "choose:Do you have a spare Priest Gown?",
            "player:Do you have a spare Priest Gown?",
            "npc:Well I do sell them.",
            "player:Please! It's really important.",
            "npc:Well I suppose you can have this old one.",
            "player:Thank you.",
            "end",
        })
        local priest_await_result = t.inv.await_all({ priest_gown = 1, priest_robe = 1 }, 10)
        t.step("expectPriestOutfit", priest_await_result == "ok" and "PASS" or "FAIL",
            "inv.await_all({priest_gown=1,priest_robe=1},10) -> " .. tostring(priest_await_result))
        t.exec("equipPriestGown", t.player.equip, "priest_gown")
        t.exec("equipPriestRobe", t.player.equip, "priest_robe")

        -- ==== Guidor's door and Guidor, Varrock (guidordoor / guidor at
        -- 3284,3382,0) ==== -- with the full priest set worn, Guidor's wife
        -- lets the player straight through (guidors_wife.rs2:11-32).
        -- MEASURED run 3: even with the disguise worn, a hand-guessed tile
        -- (3282,3383,0) hunted every pose and every approach tile (including
        -- the loc's own square) and every one answered `covered`/refused --
        -- "the world is not picking" -- so this is resolved with
        -- world.loc_near, the same fix that landed the cauldron and nurse
        -- cupboard above, rather than guessed again.
        t.exec("goto-guidorDoorApproach", t.player.goto_tile, 3284, 3382, 0)
        local guidordoor_locate_result, guidordoor_row = t.world.loc_near("guidordoor", 15)
        t.step("guidorDoor.locate", guidordoor_locate_result == "ok" and "PASS" or "FAIL",
            string.format("world.loc_near(guidordoor,15) -> %s tile=%s,%s,%s match=%s id=%s",
                tostring(guidordoor_locate_result),
                tostring(guidordoor_row and guidordoor_row.tile_x), tostring(guidordoor_row and guidordoor_row.tile_z), tostring(guidordoor_row and guidordoor_row.level),
                tostring(guidordoor_row and guidordoor_row.match), tostring(guidordoor_row and guidordoor_row.id)))
        if guidordoor_locate_result == "ok" and guidordoor_row and guidordoor_row.tile_x then
            t.exec("goto-guidorDoor", t.player.goto_tile, guidordoor_row.tile_x, guidordoor_row.tile_z, guidordoor_row.level or 0)
        end
        t.exec("enterGuidorHouse", t.player.click_loc, "guidordoor", 1)

        t.exec("goto-guidor", t.player.goto_tile, 3284, 3382, 0)
        t.exec("talkToGuidor", t.player.talk_to, "guidor", 1)
        t.exec("guidor-dialog", t.chat.play, {
            "player:Hello, you must be Guidor.",
            "npc:Is my wife asking priests to visit me now?",
            "npc:Ever since she heard rumors",
            "npc:Of course she means well",
            "options",
            "choose:I've come to ask your assistance in stopping a plague.",
            "player:Well it's funny you should ask",
            "npc:So you're the plague carrier!",
            "options",
            "choose:I've been sent by your old pupil Elena.",
            "player:I've been sent by your old pupil Elena",
            "npc:Elena eh?",
            "player:Yes, she wants you to analyse it.",
            "npc:Right then, sounds like we'd better get to work!",
            "player:I have the plague sample.",
            "npc:Now I'll be needing some liquid honey",
            "player:... some ethenea?",
            "npc:Indeed!",
            "npc:Now I'll just apply these to the sample",
            "options",
            "choose:So what does that mean exactly?",
            "player:So what does that mean exactly?",
            "npc:I don't know what this sample is",
            "player:So what about the plague?",
            "npc:Don't you understand? There is no Plague!",
            "npc:I'm very sorry",
            "npc:The only question is",
            "end",
        })
        local items_gone_result, sample_left = t.inv.count("plaguesample")
        t.check("guidor.itemsConsumed", items_gone_result == "ok" and (sample_left or 0) == 0,
            string.format("inv.count(plaguesample) -> %s (%s), expected 0 -- guidor_elena consumes sample/vials/paper",
                tostring(items_gone_result), tostring(sample_left)))
        t.expect("quest.stage.found_secret", t.quest.expect_stage("found_secret"))

        -- ==== Report to Elena (2592,3336,0) -- no choices, auto-advances ====
        t.exec("goto-elenaReport", t.player.goto_tile, 2592, 3336, 0)
        t.exec("talkToElenaReport", t.player.talk_to, "elena2_vis", 1)
        t.exec("elenaReport-dialog", t.chat.play, {
            "npc:You're back! So what did Guidor say?",
            "player:Nothing.",
            "npc:What?",
            "player:He said that there is no plague.",
            "npc:So what, this thing has all been a big hoax?",
            "player:Or maybe we're about to uncover something huge.",
            "npc:Then I think this thing may be bigger than both of us.",
            "player:What do you mean?",
            "npc:I mean you need to go right to the top",
            "end",
        })
        t.expect("quest.stage.reported_elena", t.quest.expect_stage("reported_elena"))

        -- ==== King Lathas, East Ardougne castle (2578,3293,1) -- finale ====
        -- choosing "I don't understand..." runs the full exposition and
        -- ends in queue(quest_biohazard_complete,0,0) -- asynchronous
        -- (section 8), so the reward/complete read is polled after ticking.
        local thieving_snapshot_result, thieving_snapshot = t.skill.snapshot()
        t.step("skillSnapshotBeforeReward", thieving_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot() -> " .. tostring(thieving_snapshot_result))

        t.exec("goto-kingLathas", t.player.goto_tile, 2578, 3293, 1)
        t.exec("talkToKingLathas", t.player.talk_to, "kinglathas", 1)
        t.exec("kingLathas-dialog", t.chat.play, {
            "player:I assume that you are the King of East Ardougne?",
            "npc:You assume correctly",
            "player:I get it from finding out that the plague is a hoax.",
            "npc:A hoax? I've never heard such a ridiculous thing",
            "player:I have evidence, from Guidor of Varrock.",
            "npc:Ah... I see. Well then you are right about the plague.",
            "player:When is it ever good to lie to people like that?",
            "npc:When it protects them from a far greater danger",
            "options",
            "choose:I don't understand...",
            "player:I don't understand...",
            "npc:Their King, Tyras, journeyed out to the West",
            "npc:The Dark Lord agreed to spare his life",
            "player:So what happened?",
            "npc:The chalice corrupted him.",
            "npc:And so I erected this wall",
            "npc:Now, with the King of West Ardougne",
            "npc:So I'm sorry that I lied about the plague.",
            "player:Well at least I know now, but what can we do about it?",
            "npc:Nothing at the moment, I'm waiting for my scouts",
            "npc:When this happens, can I count on your support?",
            "player:Absolutely!",
            "npc:Thank the gods! I give you permission to use my training area.",
            "npc:It's located just to the north west of Ardougne",
            "player:Ok. There's just one thing I don't understand",
            "npc:How could I not do? He was my brother.",
            "end",
        })
        t.ticks(3) -- queue(quest_biohazard_complete,0,0) is a fresh queued task (section 8's async-completion rule)

        t.quest.expect_complete() -- writes quest.varp_complete/quest.scroll_title/quest.points/quest.journal itself
        t.expect("skill.thievingReward", t.skill.expect_gain("thieving", 1250, thieving_snapshot))
        t.finish(0)
    end,
}
