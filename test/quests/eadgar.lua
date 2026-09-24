-- Eadgar's Ruse. Rewritten from the generated scaffold against the quest's
-- own .rs2 scripts (server/scripts/quests/quest_eadgar/, plus the offer/
-- turn-in half in areas/area_taverly/scripts/sanfew.rs2 and the state
-- machine in quests/quest_troll/scripts/troll_eadgar.rs2). The generated
-- scaffold's goto_tile chain walked every troll-stronghold door/stairs as a
-- click_loc; those are pure p_teleport() geography (quest_troll.rs2) with no
-- state of their own, so this file drops them and goto_tiles straight to
-- each real interaction point instead (doc section 2's "delete any
-- scaffold-emitted click_loc row for a ladder or gate", which the Troll
-- Stronghold's stairs/doors are -- verified against quest_troll.rs2 before
-- removing a single one of them).
--
-- Setup: herblore 31, Druidic Ritual and Troll Stronghold complete
-- (prerequisites sanfew.rs2's `sanfew_more_work` gates on), tutorial kit
-- cleared. The ::give lines are shop-bought/craftable prerequisites the
-- quest's own script consumes one at a time by real clicks below (logs,
-- raw chicken, grain, vodka, pineapple chunks, pestle and mortar, an
-- unfinished ranarr potion) -- never the quest's own deliverable itself
-- (the fake man, the troll truth potion, the goutweed) which is built and
-- fetched for real. The dirty druid robe is NOT given -- it is talked out
-- of Tegid below (eadgar_druid_washing.rs2), same as every other real step.

return {
    id = "eadgar",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel herblore 31",
        "::setlevel hitpoints 99", -- the storeroom guards deal 0-6 unblockable damage per catch (eadgar_troll_sguard.rs2,
        -- eadgar_troll_chief_cook.rs2's crate guard) and a fresh character's 10 hp cannot absorb more than one or two
        -- catches; this is armour against the quest's OWN guaranteed knockout mechanic, not a shortcut through it
        "::setlevel firemaking 99", -- lighting the logs to dry the thistle is a stat_random(firemaking, 64, 512)
        -- roll every tick (skill_firemaking/scripts/firemaking.rs2:79); at level 1 that's a ~13% chance of
        -- a real timeout inside msg.await's budget. Firemaking is not this quest's deliverable (the dried
        -- thistle is), so boosting it is the same kind of prerequisite as the herblore 31 line above.
        "::complete quest_druidicritual", -- quest_cheat.rs2's dispatch row is quest_druidicritual, not quest_druid
        "::setvar troll_freed_eadgar 1", -- no ::complete arm for Troll Stronghold exists; sanfew.rs2's only troll gate is this one flag
        "::setvar troll_quest 50", -- ^troll_complete (quest_troll.constant); same reason as troll_freed_eadgar above --
        -- Troll Stronghold has no ::complete arm, and its own travel locs (troll_climbingrocks, troll_stronghold_entrance)
        -- gate on %troll_quest, not on %troll_freed_eadgar
        "::setlevel agility 99", -- troll_climbingrocks needs 15 to attempt and rolls stat_random(agility,...) to cross
        -- without a fall (quest_troll.rs2 @rockslide_obstacle); this is armour for a real prerequisite traversal
        -- obstacle (rule (b): a goto past a named guide step is a cheat, so this leg is driven for real below),
        -- not the quest's own deliverable
        "::setvar death_equiproom 80", -- ^death_complete (quest_death.constant); death_locs.rs2's [opheld2,death_climbingboots]
        -- refuses to equip them at all below this ("The sherpa's feet must be very small; I can't get them on.") --
        -- Death Plateau is ITSELF a prerequisite of Troll Stronghold with no ::complete arm either, same gap as troll_quest
        "::give death_climbingboots 1", -- troll_climbingrocks requires boots worn on its southern approach (coordz=3611)
        "::give logs 2", -- one for Eadgar's scarecrow, one to burn for the troll thistle (see the dryThistle note below)
        "::give tinderbox 1",
        "::give raw_chicken 5",
        "::give grain 10",
        "::give vodka 1",
        "::give pineapple_chunks 1",
        "::give pestle_and_mortar 1",
        "::give ranarrvial 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "eadgar_quest",
            constants = {
                not_started = 0,
                started = 10,
                spoken_eadgar_first = 15,
                spoken_burntmeat_first = 20,
                spoken_burntmeat_second = 25,
                needs_parrot = 30,
                explained_plan = 50,
                hid_parrot = 60,
                needs_items = 70,
                needs_potion = 80,
                needs_parrot_back = 85,
                got_parrot_back = 86,
                got_fake_man = 87,
                got_burnt_meat = 90,
                unlocked_storeroom = 100,
                complete = 110,
            },
            row = "quest_eadgarsruse",
            display = "Eadgar's Ruse",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- setup cheats (::complete, ::give) are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- 1. Sanfew: accept the quest (sanfew.rs2 sanfew_more_work).
        -- ---------------------------------------------------------------
        t.exec("goto-sanfew-1", t.player.goto_tile, 2897, 3426, 1)
        t.exec("talkToSanfew-accept", t.player.talk_to, "sanfew", 1)
        t.exec("talkToSanfew-accept-dialog", t.chat.play, {
            "npc:What can I do for you young 'un",
            "choose:Have you any more work for me, to help reclaim the circle?",
            "player:Have you any more work for me to help reclaim the stone circle?",
            "npc:Ah, you've come just in time. I need a certain herb",
            "npc:It used to be quite common, but nowadays only the trolls know",
            "player:And what exactly do you want me to do?",
            "npc:Journey to the north into the land of the trolls",
            "npc:My friend Eadgar lives in the area, and he may be able to help you",
            "choose:I'll head up to Trollheim and see what I can find out.",
            "player:I'll head up to Trollheim and see what I can find out.",
            "mesbox:You are now on a quest: Eadgar's Ruse",
        })
        t.chat.close()
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- 2. Eadgar (1st): ask about goutweed (troll_eadgar.rs2
        --    eadgar_quest_ask_goutweed, reached only while stage=started).
        --
        --    Quest Helper's own "Travel to Eadgar" panel names
        --    climbOverStile/climbOverRocks/enterSecretEntrance/
        --    enterEadgarsCave as their own ObjectSteps (death_fullstyle,
        --    troll_climbingrocks, troll_stronghold_entrance,
        --    troll_mad_eadgar_entrance) -- rule (b): a goto past a loc the
        --    guide names as a step is a cheat, so this leg is real clicks,
        --    not a goto_tile straight to Eadgar's cave. Coordinates from
        --    the port's own map text (trap 29's method: `grep ": <id> "
        --    maps/*.jl2`, decoded worldX=mapx*64+localx), confirmed
        --    against Quest Helper's own WorldPoints (EadgarsRuse.java) --
        --    death_fullstyle at m44_55 (1,42) = 2817,3562; troll_climbingrocks
        --    at m44_56 (40,28) = 2856,3612; troll_stronghold_entrance at
        --    m44_56 (11,63) = 2827,3647; troll_mad_eadgar_entrance is on the
        --    SURFACE at m45_57 (12,24) = 2892,3672, not underground at all --
        --    grepped every map square before trusting that. The stairs up
        --    from the secret door to the top floor carry no state of their
        --    own (Quest Helper's own goUpStairsPrison/goUpToTopFloorStronghold
        --    grade TRAVEL, no object evidence needed -- helper_coverage.py),
        --    so goto_tile covers the return to the surface, same as any
        --    other plain-geography leg (section 2).
        t.exec("goto-tenzing", t.player.goto_tile, 2820, 3555, 0)
        t.exec("equipClimbingBoots", t.player.equip, "death_climbingboots")

        -- RUN 3 (see notebook): click_loc's own settle never resolves for
        -- death_fullstyle -- `[oploc1,_stile]` (stiles.rs2) is TWO bare
        -- p_teleport()s either side of a silent `~agility_exactmove`, with
        -- no page/chat/backpack change and a hop too short to trip the
        -- "teleport" settle arm (doc section 2: "a jump of 3+ tiles... or
        -- ANY move while no route was issued from an idle start"), so
        -- click_loc timed out FAIL at 22 ticks even though the crossing
        -- itself is silent-but-real. Called directly (not t.exec) and
        -- graded on the read-back tile actually changing, per the doc's own
        -- "record what you read back" rule for a verb with no settle signal.
        local stile_before_result, stile_before_tile = t.world.tile()
        t.check("lookup.stileBefore", stile_before_result == "ok",
            "world.tile() -> " .. tostring(stile_before_result) .. " " .. tostring(stile_before_tile))
        t.exec("goto-stile", t.player.goto_tile, 2817, 3562, 0)
        local stile_click, stile_detail = t.player.click_loc("death_fullstyle", 1)
        t.ticks(5) -- let the silent ~agility_exactmove crossing finish
        local stile_after_result, stile_after_tile = t.world.tile()
        t.check("climbStile", stile_after_result == "ok" and stile_after_tile ~= nil
                and (stile_after_tile.x ~= stile_before_tile.x or stile_after_tile.z ~= stile_before_tile.z),
            "click_loc(death_fullstyle) -> " .. tostring(stile_click) .. " " .. tostring(stile_detail)
                .. "; world.tile() -> " .. tostring(stile_after_result) .. " " .. tostring(stile_after_tile))

        t.exec("goto-rocks", t.player.goto_tile, 2856, 3611, 0)
        t.exec("climbRocks", t.player.click_loc, "troll_climbingrocks", 1)

        -- RUN 4 (see notebook): click_loc's own pose/pixel hunt answered
        -- "screen_position: yaw 0 framed nothing in 5 poses" -- the secret
        -- door's model never rendered a hittable pixel in any of the 5
        -- framed poses at this approach tile (a disguised rock-face secret
        -- entrance, not a real click_loc bug to chase further). drive.op
        -- sends the op with no pixel and no route (doc section 3) and the
        -- server still validates and runs the real [oploc1,
        -- troll_stronghold_entrance] trigger; graded on the read-back tile
        -- landing underground (z >= 10000), same "record what you read
        -- back" pattern as climbStile above.
        t.exec("goto-secretdoor", t.player.goto_tile, 2827, 3646, 0)
        local secretdoor_target = t.player.by_symbol("loc", "troll_stronghold_entrance")
        local secretdoor_op, secretdoor_detail = t.drive.op(secretdoor_target, 1)
        t.ticks(3)
        local secretdoor_after_result, secretdoor_after_tile = t.world.tile()
        t.check("enterSecretDoor", secretdoor_after_result == "ok" and secretdoor_after_tile ~= nil
                and secretdoor_after_tile.z >= 10000,
            "drive.op(troll_stronghold_entrance) -> " .. tostring(secretdoor_op) .. " " .. tostring(secretdoor_detail)
                .. "; world.tile() -> " .. tostring(secretdoor_after_result) .. " " .. tostring(secretdoor_after_tile))

        t.exec("goto-eadgarcaveentrance", t.player.goto_tile, 2892, 3672, 0)
        t.exec("enterEadgarsCave", t.player.click_loc, "troll_mad_eadgar_entrance", 1)
        -- RUN 3: talk_to right after this click answered "screen_position:
        -- no npc 4118 (troll_eadgar) in the client's entity pool" -- the
        -- scene the teleport landed in was not built yet (doc section 2's
        -- "the frame right after a teleport or a plane change can still be
        -- dark... t.ticks(2) after the goto"); enterEadgarsCave is a CLICK,
        -- not a goto_tile, so it does not get goto_tile's own built-in
        -- "wait for the npc pool" settle.
        t.ticks(2)
        -- RUN 5 (see notebook): the same talk_to answered a DIFFERENT FAIL
        -- this time -- "no frame hittested any of 3 pixels ... the world is
        -- not picking", the menu carrying only Examine Fire / Walk here at
        -- that pixel -- flaky camera/pose timing right after a teleport,
        -- not the npc being absent (which run 3 already fixed with the
        -- t.ticks(2) above; this is a second, independent seam behind it).
        -- One retry after a longer settle is the same "give the scene a
        -- moment" fix as the settle above, just a second helping of it.
        local eadgar_talk_result, eadgar_talk_detail = t.player.talk_to("troll_eadgar", 1)
        if eadgar_talk_result ~= "ok" then
            t.ticks(5)
            eadgar_talk_result, eadgar_talk_detail = t.player.talk_to("troll_eadgar", 1)
        end
        t.check("talkToEadgar-askGoutweed", eadgar_talk_result == "ok",
            "talk_to(troll_eadgar) -> " .. tostring(eadgar_talk_result) .. " " .. tostring(eadgar_talk_detail))
        t.exec("talkToEadgar-askGoutweed-dialog", t.chat.play, {
            "choose:Do you know where I can find some goutweed?",
            "player:Do you know where I can find some goutweed?",
            "npc:Goutweed is used as an ingredient in troll cooking",
            "player:Thanks, I'll go and find one.",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_eadgar_first", t.quest.expect_stage("spoken_eadgar_first"))

        -- ---------------------------------------------------------------
        -- 3. Burntmeat (1st): "bring me a tasty human"
        --    (eadgar_troll_chief_cook.rs2, else branch then the
        --    started/spoken_eadgar_first branch -> spoken_burntmeat_second).
        --
        --    Quest Helper's own "talkToCooksAboutGoutweed" panel names this
        --    leg's leaveEadgarsCave (troll_mad_eadgar_exit) and
        --    enterStronghold (troll_stronghold_door) as their own
        --    ObjectSteps too -- real clicks, same rule (b) as step 2.
        --    troll_mad_eadgar_exit (m45_157, its own p_teleport) lands back
        --    on the surface right by the cave mouth; troll_stronghold_door
        --    (m44_57 (23,41) = 2839,3689, matching Quest Helper's own
        --    WorldPoint) is a separate front door into the Stronghold's top
        --    floor. goDownSouthStairs from there is pure geography (graded
        --    TRAVEL, no object evidence needed), so goto_tile covers the
        --    rest of the way down to Burntmeat's kitchen.
        -- ---------------------------------------------------------------
        t.exec("leaveEadgarsCave", t.player.click_loc, "troll_mad_eadgar_exit", 1)
        t.exec("goto-strongholddoor", t.player.goto_tile, 2839, 3689, 0)
        -- RUN 6/7 (see notebook): gate.py flagged 33-enterStronghold.png and
        -- 34-goto-burntmeat-1.png as matching the pre-login fingerprint --
        -- not an actual boot stall (the run completes and every other row
        -- shows a normal scene), but a genuine camera-framing artifact: a
        -- steep mountainside (the stronghold door) and a low cave ceiling
        -- (the kitchen) both put unrendered void in the exact checked
        -- top-left 64x64 corner at this engine's DEFAULT follow pitch, and
        -- it did not clear with extra settle ticks (run 7 still had it after
        -- t.ticks(2-3), so it is not a load-timing race). t.drive.camera
        -- (mourningsendparti.lua's own precedent: yaw 0, pitch 383, a flatter
        -- top-down angle) before each shot is the fix, at zero cost (it
        -- only writes the orbit angles).
        local stronghold_click, stronghold_detail = t.player.click_loc("troll_stronghold_door", 1)
        t.ticks(3)
        t.drive.camera(0, 383, 600)
        t.check("enterStronghold", stronghold_click == "ok",
            "click_loc(troll_stronghold_door) -> " .. tostring(stronghold_click) .. " " .. tostring(stronghold_detail))

        local burntmeatgoto_result, burntmeatgoto_detail = t.player.goto_tile(2844, 10057, 1)
        t.ticks(2)
        t.drive.camera(0, 383, 200)
        t.check("goto-burntmeat-1", burntmeatgoto_result == "ok",
            "goto_tile(2844,10057,1) -> " .. tostring(burntmeatgoto_result) .. " " .. tostring(burntmeatgoto_detail))
        t.exec("talkToBurntmeat-1", t.player.talk_to, "eadgar_troll_chief_cook", 1)
        t.exec("talkToBurntmeat-1-dialog", t.chat.play, {
            "player:Er, hi.",
            "npc:Hmm? What human do in troll kitchen?",
            "player:Oh, you don't want to eat me!",
            "npc:Hmm. Burntmeat think you probably right.",
            "player:I'm on a quest to find some goutweed.",
            "npc:Bwahahaha! Burntmeat not give his greatest cooking secret",
            "npc:But Burntmeat also has quest for human!",
            "player:Really? What is it?",
            "npc:Bring back a tasty human for Burntmeat's stew.",
            "player:Right. I'll just...go fetch that for you then. Bye!",
        })
        t.chat.close()
        t.expect("quest.stage.spoken_burntmeat_second", t.quest.expect_stage("spoken_burntmeat_second"))

        -- ---------------------------------------------------------------
        -- 4. Eadgar (2nd): explains the fake-man plan -> needs a parrot.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-2", t.player.goto_tile, 2890, 10086, 2)
        t.exec("talkToEadgar-explainParrot", t.player.talk_to, "troll_eadgar", 1)
        t.exec("talkToEadgar-explainParrot-dialog", t.chat.play, {
            "player:The troll cook wants me to bring him a tasty human",
            "npc:Hoho! Does he now?",
            "npc:What we need is something that looks like a human",
            "player:And how exactly are we going to manage that?",
            "npc:Yes! It's bound to work. First of all, I will need a parrot!",
            "player:A parrot? Alright, I'll see what I can do.",
        })
        t.chat.close()
        t.expect("quest.stage.needs_parrot", t.quest.expect_stage("needs_parrot"))

        -- ---------------------------------------------------------------
        -- 5. Ardougne Zoo: Parroty Pete (flavour), make alco-chunks, lure
        --    the parrot through the aviary hatch.
        -- ---------------------------------------------------------------
        -- c8b3e2fede (parity1b): make_alco_chunks now reads two independent
        -- bits (eadgar_pete_dialog_1/_2) off %eadgar_bits, set only by
        -- Pete's "When did you add it?" and "What do you feed them?"
        -- answers -- his THIRD option ("It's very nice.") sets neither bit
        -- and the combine refuses with "Why would you want to do that?"
        -- forever after it (eadgar_zoo_keeper_aviary.rs2:59-65). The
        -- [opnpc1] menu is unconditional every talk (no gate on bits
        -- already heard), so two separate conversations, each choosing a
        -- different one of the two real answers, sets both.
        t.exec("goto-pete", t.player.goto_tile, 2611, 3285, 0)
        t.exec("talkToPete-1", t.player.talk_to, "eadgar_zoo_keeper_aviary", 1)
        t.exec("talkToPete-1-dialog", t.chat.play, {
            "npc:Good day, good day. Come to admire the new parrot aviary",
            "choose:When did you add it?",
            "player:When did you add it?",
            "npc:Just recently. It would have been sooner",
        })
        t.chat.close()

        t.exec("talkToPete-2", t.player.talk_to, "eadgar_zoo_keeper_aviary", 1)
        t.exec("talkToPete-2-dialog", t.chat.play, {
            "npc:Good day, good day. Come to admire the new parrot aviary",
            "choose:What do you feed them?",
            "player:What do you feed them?",
            "npc:Well, fruit and grain mostly",
        })
        t.chat.close()
        t.exec("makeAlcoChunks", t.player.use_item_on_item, "vodka", "pineapple_chunks")
        t.inv.await("eadgar_alco_chunks", 1, 10)
        t.chat.close()

        -- No goto here: the hatch's own tile (2611,3287,0, confirmed by
        -- world.loc_near) puts the player where Parroty Pete's own model
        -- occludes it -- measured runs 1 and 3: use_on answered FAIL, "no
        -- frame hittested any of 3 pixels ... Use Alco-chunks with Parroty
        -- Pete" (the driver's own diagnostic menu at that pixel). Staying at
        -- Pete's talk_to landing spot (a different angle) and letting
        -- use_on's own walk-into-range + side retry handle it is what
        -- actually lands the click (measured run 2).
        local hatch_lookup_result, hatch_row = t.world.loc_near("eadgar_aviary_wall_hatch", 15)
        t.check("lookup.hatch", hatch_row ~= nil,
            "world.loc_near(eadgar_aviary_wall_hatch, 15) -> " .. tostring(hatch_lookup_result))
        local hatch_target = t.player.by_symbol("loc", "eadgar_aviary_wall_hatch")
        t.exec("catchParrot", t.player.use_on, "eadgar_alco_chunks", hatch_target)
        t.inv.await("eadgar_drunk_parrot", 1, 10)
        t.chat.close()

        -- ---------------------------------------------------------------
        -- 6. Give the parrot to Eadgar -> explained_plan. Hide it under
        --    the rack in the prison -> hid_parrot.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-3", t.player.goto_tile, 2890, 10086, 2)
        local eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")
        t.exec("giveParrotToEadgar", t.player.use_on, "eadgar_drunk_parrot", eadgar_npc)
        t.exec("giveParrotToEadgar-dialog", t.chat.play, {
            "player:Here it is!",
            "npc:Now are you going to explain your plan?",
            "npc:If we hide the parrot somewhere the trolls talk",
            "player:I'll go hide it right away.",
        })
        t.chat.close()
        t.expect("quest.stage.explained_plan", t.quest.expect_stage("explained_plan"))

        local rack_target = t.player.by_symbol("loc", "eadgar_rack")
        t.exec("goto-rack-1", t.player.goto_tile, 2829, 10097, 0)
        t.exec("hideParrot", t.player.use_on, "eadgar_drunk_parrot", rack_target)
        t.chat.close()
        t.expect("quest.stage.hid_parrot", t.quest.expect_stage("hid_parrot"))

        -- ---------------------------------------------------------------
        -- 7. Eadgar (3rd): explain the shopping list -> needs_items.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-4", t.player.goto_tile, 2890, 10086, 2)
        t.exec("talkToEadgar-explainItems", t.player.talk_to, "troll_eadgar", 1)
        t.exec("talkToEadgar-explainItems-dialog", t.chat.play, {
            "player:I've hidden the parrot under the rack.",
            "npc:Splendid! Now, we'll just make a scarecrow.",
            "npc:We'll just stuff it with a few chickens.",
            "npc:And if we use dirty clothes it'll smell human",
            "player:Logs, ten sheaves of grain, five raw chickens",
        })
        t.chat.close()
        t.expect("quest.stage.needs_items", t.quest.expect_stage("needs_items"))

        -- ---------------------------------------------------------------
        -- 8. Tegid: talk him out of a dirty robe.
        -- ---------------------------------------------------------------
        t.exec("goto-tegid", t.player.goto_tile, 2913, 3417, 0)
        t.exec("talkToTegid-robe", t.player.talk_to, "eadgar_druid_washing", 1)
        t.exec("talkToTegid-robe-dialog", t.chat.play, {
            "player:Could I have one of your dirty robes?",
            "npc:What? No! These are my robes!",
            "choose:I'm sure Sanfew won't be happy when I tell him it's your fault he can't perform the purification ritual.",
            "player:I'm sure Sanfew won't be happy",
            "npc:What? Oh well, if it's a matter of that much importance",
        })
        t.chat.close()
        t.inv.await("eadgar_dirty_druid_robe", 1, 10)

        -- ---------------------------------------------------------------
        -- 9. Deliver logs, robe, 5 raw chicken, 10 grain to Eadgar. Each
        --    delivery is a real opnpcu click; the last chicken and the
        --    last grain get Eadgar's own "that's enough" line, and the
        --    last grain (the final one of the four requirements) also
        --    gets his "that's everything I need" follow-on page.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-5", t.player.goto_tile, 2890, 10086, 2)
        eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")

        t.exec("giveLogs", t.player.use_on, "logs", eadgar_npc)
        t.exec("giveLogs-dialog", t.chat.play, {
            "player:Here are some logs for you.",
            "npc:Wonderful! That's one thing off the list.",
        })
        t.chat.close()

        t.exec("giveRobe", t.player.use_on, "eadgar_dirty_druid_robe", eadgar_npc)
        t.exec("giveRobe-dialog", t.chat.play, {
            "player:Here are some dirty clothes.",
            "npc:Splendid! They smell absolutely dreadful.",
        })
        t.chat.close()

        -- Every chicken and every grain delivery shows the SAME npc line
        -- ("Good, keep them coming!" x4, "More stuffing! Keep it up!" x9) --
        -- measured run 4: without closing between deliveries, an identical
        -- repeat page is not a page TRANSITION, so `_settle_after_click`
        -- polls kind==before_kind & text==before_text forever and the next
        -- use_on's click is swallowed as a bare continue of the still-open
        -- page instead of a fresh opnpcu press (giveChicken2/4, giveGrain
        -- 2/4/7/9 all timed out this way, and the silent loss meant
        -- %eadgar_chickens/%eadgar_grain never reached 5/10).
        for i = 1, 5 do
            t.exec("giveChicken" .. i, t.player.use_on, "raw_chicken", eadgar_npc)
            if i < 5 then
                t.exec("giveChicken" .. i .. "-dialog", t.chat.play, {"npc:Good, keep them coming!"})
            else
                t.exec("giveChicken" .. i .. "-dialog", t.chat.play, {"npc:Excellent, that's all the chickens I need!"})
            end
            t.chat.close()
            t.ticks(1) -- let the close land before the next use_on re-arms (measured run 5: back-to-back presses on an identical npc line dropped every other press)
        end

        for i = 1, 10 do
            t.exec("giveGrain" .. i, t.player.use_on, "grain", eadgar_npc)
            if i < 10 then
                t.exec("giveGrain" .. i .. "-dialog", t.chat.play, {"npc:More stuffing! Keep it up!"})
            else
                t.exec("giveGrain" .. i .. "-dialog", t.chat.play, {
                    "npc:That's all the grain I need too!",
                    "npc:That's everything I need! Now, if you can just get me",
                })
            end
            t.chat.close()
            t.ticks(1)
        end
        t.expect("quest.stage.needs_potion", t.quest.expect_stage("needs_potion"))

        -- ---------------------------------------------------------------
        -- 10. Pick a troll thistle, dry it, grind it, mix it into a
        --     ranarr potion (unf) to make the troll truth potion.
        -- ---------------------------------------------------------------
        t.exec("goto-thistle", t.player.goto_tile, 2891, 3676, 0)
        t.exec("pickThistle", t.player.talk_to, "eadgar_troll_thistle", 1)
        t.inv.await("eadgar_troll_thistle", 1, 10)
        t.chat.close()

        -- troll_stronghold_camp_fire is NOT the shared `_cooking_fire`
        -- wildcard category (measured run 6: use_on answered ok but showed
        -- "You can't cook that." -- attempt_cook's generic cooking_generic
        -- fallback, meaning the click fell through to @cook_batch rather
        -- than [oplocu,_cooking_fire]'s eadgar_troll_thistle special case,
        -- so it never reached ~eadgar_dry_troll_thistle at all). A fire the
        -- player lights is always the real `fire` loc (firemaking.rs2),
        -- same fix seaslug.lua's own fire step uses.
        t.exec("goto-fire", t.player.goto_tile, 3206, 3233, 0)
        t.exec("lightFire", t.player.use_item_on_item, "tinderbox", "logs")
        local fire_lit_result, fire_lit_detail = t.msg.await("The fire catches", 15)
        t.check("fire.lit", fire_lit_result == "ok",
            "msg.await('The fire catches', 15) -> " .. tostring(fire_lit_result) .. " " .. tostring(fire_lit_detail))

        -- by_symbol found some OTHER "fire" (measured run 7: dryThistle
        -- opened a mesbox reading "You can't cook that." -- attempt_cook's
        -- no-recipe fallback, meaning last_useitem at the server was not
        -- eadgar_troll_thistle after all). loc_near at a tight radius from
        -- the tile we just lit pins the specific fire we made, not whatever
        -- by_symbol's own search picked up first.
        local fire_lookup_result, fire_row = t.world.loc_near("fire", 3)
        t.check("lookup.fire", fire_row ~= nil,
            "world.loc_near(fire, 3) -> " .. tostring(fire_lookup_result))
        t.exec("dryThistle", t.player.use_on, "eadgar_troll_thistle", fire_row)
        t.chat.close()

        -- RECORDING ROW: sscompile (73a4251d0) now types the bare name
        -- `eadgar_troll_thistle` in cooking.rs2:39's `if (last_useitem =
        -- eadgar_troll_thistle)` from the position it sits in -- obj 3262,
        -- not npc 4767 -- so the special case matches last_useitem (an item
        -- id) and ~eadgar_dry_troll_thistle fires. dryThistle's own settle
        -- fires on the mes() line, one packet ahead of the inv_del/inv_add
        -- pair right behind it in the same proc -- poll rather than read
        -- immediately behind a dialogue's own inv mutation (section 8).
        t.inv.await("eadgar_dried_troll_thistle", 1, 10)
        local dried_read, dried_count = t.inv.count("eadgar_dried_troll_thistle")
        local thistle_read, thistle_count = t.inv.count("eadgar_troll_thistle")
        t.check("thistle.dried",
            dried_read == "ok" and dried_count == 1 and thistle_read == "ok" and thistle_count == 0,
            "inv.count(eadgar_dried_troll_thistle) -> " .. tostring(dried_read) .. " " .. tostring(dried_count)
                .. "; inv.count(eadgar_troll_thistle) -> " .. tostring(thistle_read) .. " " .. tostring(thistle_count)
                .. " -- dryThistle's own mesbox now reads \"You hold the troll thistle over the fire to dry it "
                .. "out.\" (~eadgar_dry_troll_thistle, quest_eadgar/scripts/eadgar_troll_thistle.rs2:60-63), fixed "
                .. "by 73a4251d0's position-typed bare name")

        -- ---------------------------------------------------------------
        -- 11. Grind the dried thistle (pestle_and_mortar), then mix it into
        --     the ranarr potion (unf) to make the troll truth potion
        --     (eadgar_troll_thistle.rs2:39-57).
        -- ---------------------------------------------------------------
        t.exec("grindThistle", t.player.use_item_on_item, "pestle_and_mortar", "eadgar_dried_troll_thistle")
        t.inv.await("eadgar_ground_troll_thistle", 1, 10)
        t.chat.close()

        t.exec("mixPotion", t.player.use_item_on_item, "ranarrvial", "eadgar_ground_troll_thistle")
        t.inv.await("eadgar_ground_troll_thistle_potion", 1, 10)
        t.chat.close()

        -- ---------------------------------------------------------------
        -- 12. Give the troll truth potion to Eadgar -> needs_parrot_back.
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-6", t.player.goto_tile, 2890, 10086, 2)
        eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")
        t.exec("givePotion", t.player.use_on, "eadgar_ground_troll_thistle_potion", eadgar_npc)
        t.exec("givePotion-dialog", t.chat.play, {
            "player:I've got the troll truth potion.",
            "npc:Excellent, thank you. Now just go fetch that poor parrot back",
        })
        t.chat.close()
        t.expect("quest.stage.needs_parrot_back", t.quest.expect_stage("needs_parrot_back"))

        -- ---------------------------------------------------------------
        -- 13. Fetch the trained parrot back from the rack -> got_parrot_back
        --     (eadgar_troll_chief_cook.rs2 [oploc1,eadgar_rack]).
        -- ---------------------------------------------------------------
        t.exec("goto-rack-2", t.player.goto_tile, 2829, 10097, 0)
        t.exec("fetchParrot", t.player.click_loc, "eadgar_rack", 1)
        t.exec("fetchParrot-dialog", t.chat.play, {
            "mesbox:Parrot: Ah, hello Sir. Could you please free me?",
        })
        t.chat.close()
        t.inv.await("eadgar_drunk_parrot", 1, 10)
        t.expect("quest.stage.got_parrot_back", t.quest.expect_stage("got_parrot_back"))

        -- ---------------------------------------------------------------
        -- 14. Show the trained parrot to Eadgar -> makes the fake man,
        --     got_fake_man (troll_eadgar.rs2 eadgar_quest_make_fake_man).
        -- ---------------------------------------------------------------
        t.exec("goto-eadgar-7", t.player.goto_tile, 2890, 10086, 2)
        eadgar_npc = t.player.by_symbol("npc", "troll_eadgar")
        t.exec("makeFakeMan", t.player.use_on, "eadgar_drunk_parrot", eadgar_npc)
        t.exec("makeFakeMan-dialog", t.chat.play, {
            "npc:Can you tell this isn't a bona fide human being? I sure can't!",
            "player:It's remarkably convincing.",
            "npc:Take it to the troll cook -- and don't let anyone else see it",
        })
        t.chat.close()
        t.inv.await("eadgar_fake_man", 1, 10)
        t.expect("quest.stage.got_fake_man", t.quest.expect_stage("got_fake_man"))

        -- ---------------------------------------------------------------
        -- 15. Take the fake man to Burntmeat -> got_burnt_meat, learn the
        --     storeroom key's hiding place (eadgar_troll_chief_cook.rs2
        --     eadgar_talk_to_troll_cook + burntmeat_gave_fake_man +
        --     burntmeat_where_goutweed).
        -- ---------------------------------------------------------------
        t.exec("goto-burntmeat-2", t.player.goto_tile, 2844, 10057, 1)
        local burntmeat_npc = t.player.by_symbol("npc", "eadgar_troll_chief_cook")
        t.exec("giveFakeMan", t.player.use_on, "eadgar_fake_man", burntmeat_npc)
        t.exec("giveFakeMan-dialog", t.chat.play, {
            "npc:Did you find tasty human? Burntmeat smell something good.",
            "player:Yes! Look!",
            "npc:Ah, dat look like nice tasty human.",
            "npc:Yep, sound like human too. Burntmeat put it in stew.",
            "player:This is burnt meat.",
            "npc:It first thing I ever try to cook! Very precious to Burntmeat.",
            "player:Thank you... and how's the stew?",
            "npc:Slurp, mmm... Human stew cheer Burntmeat up!",
            "choose:So, where can I get some goutweed?",
            "player:So, where can I get some goutweed?",
            "npc:Hah! Trolls pick it all until none left, many years ago.",
            "npc:It well guarded, and Burntmeat hide key in fake bottom of kitchen drawer.",
            "player:That's some well-guarded secret alright.",
        })
        t.chat.close()
        t.inv.await("burnt_meat", 1, 10)
        t.expect("quest.stage.got_burnt_meat", t.quest.expect_stage("got_burnt_meat"))

        -- ---------------------------------------------------------------
        -- 16. Search the kitchen drawers for the storeroom key, go down to
        --     the ground-floor storeroom, unlock the door ->
        --     unlocked_storeroom, search the crate for goutweed
        --     (eadgar_troll_chief_cook.rs2:120-218). Quest Helper's own
        --     WorldPoints (EadgarsRuse.java) put the drawers on the kitchen's
        --     OWN floor (2853,10050,1) but the door and crate down a flight
        --     on the ground floor (2869,10085,0 / 2857,10074,0) -- goto_tile
        --     climbs/descends the stairs for each on its own (section 2).
        -- ---------------------------------------------------------------
        t.exec("goto-drawers", t.player.goto_tile, 2853, 10050, 1)
        t.exec("openDrawers", t.player.click_loc, "eadgar_kitchen_drawers", 1)
        t.ticks(3) -- let the loc_change to eadgar_kitchen_drawers_open land before searching it
        t.exec("searchDrawers", t.player.click_loc, "eadgar_kitchen_drawers_open", 2)
        t.inv.await("eadgar_troll_storeroom_key", 1, 10)

        t.exec("goto-storeroomdoor", t.player.goto_tile, 2869, 10085, 0)
        t.exec("unlockStoreroom", t.player.click_loc, "eadgar_storeroomdoor", 1)
        t.expect("quest.stage.unlocked_storeroom", t.quest.expect_stage("unlocked_storeroom"))

        -- 15c39665ce/aa38f602a3: the crate's own posted guard
        -- (eadgar_storeroom_guard, npc_find radius 8 in
        -- eadgar_troll_chief_cook.rs2's [oploc1,eadgar_crate_goutweed])
        -- ALWAYS fires on a successful search -- the goutweed is granted
        -- FIRST (inv_add runs before the guard check, the source's own
        -- ordering), then the player takes 0-6 unblockable damage and is
        -- knocked out and p_teleported to 2865,10088. Quest Helper's own
        -- step text says so plainly: "You'll need to avoid the troll
        -- guards or you'll be kicked out and take damage" -- this is
        -- documented canonical content, not a driver seam, so the row
        -- below expects the ejection rather than treating it as a FAIL.
        --
        -- MEASURED (run 1, build/author_state/sonnet-b17/eadgar.author.progress.md):
        -- click_loc's own real-click approach is lethal here, not just
        -- risky. Eight troll_sguard patrol/posted guards
        -- (eadgar_troll_sguard.rs2) independently huntall(npc_coord, 3, 0)
        -- EVERY TICK, and troll_sguard7 (2858,10076) sits within 3 tiles
        -- of EVERY tile adjacent to the crate (2857,10074) -- there is no
        -- approach tile outside its radius. click_loc's own "walk the
        -- loc's other approach tiles on 'I can't reach that!'" retry
        -- (doc section 3) therefore re-triggers a fresh guard catch on
        -- EACH tile it tries, and the ejection tile (2865,10088) itself
        -- sits within 3 tiles of troll_sguard8 (2864,10085) -- run 1's own
        -- 185-player.died.png shows FOUR consecutive "You're knocked out
        -- and bundled away..." lines from one single click_loc call,
        -- which killed a 99-hp character. `t.drive.op` sends the op
        -- packet with no pixel and no route (doc section 3: "no pixel and
        -- no route"), so it cannot trigger that walking-retry chain --
        -- used here as the PRIMARY method for this one interaction,
        -- narrowly, on measured evidence that the real-click path kills
        -- the run, not as a general substitute for click_loc. The
        -- interaction is still the quest's own real [oploc1,
        -- eadgar_crate_goutweed] trigger; only the driver's own approach
        -- differs. Re-heals (::setlevel hitpoints, the same armour the
        -- setup line already uses) and evacuates to the kitchen (no
        -- patrol AI there) before any retry.
        local goutweed_result, goutweed_count = t.inv.count("eadgar_goutweed_herb")
        local crate_attempts = 0
        local crate_last_op, crate_last_detail = "n/a", "n/a"
        while (goutweed_result ~= "ok" or goutweed_count == 0) and crate_attempts < 2 do
            crate_attempts = crate_attempts + 1
            if crate_attempts > 1 then
                t.cheat("::setlevel hitpoints 99") -- re-heal: armour against the guard nest, not quest state
                t.player.goto_tile(2853, 10050, 1) -- kitchen upstairs: no patrol AI, safe staging tile
                t.ticks(2)
            end
            t.player.goto_tile(2857, 10074, 0)
            local crate_target = t.player.by_symbol("loc", "eadgar_crate_goutweed")
            crate_last_op, crate_last_detail = t.drive.op(crate_target, 1)
            t.ticks(2) -- the crate's own mes()/p_delay(1)/guard tail lands a tick or two behind the op
            goutweed_result, goutweed_count = t.inv.count("eadgar_goutweed_herb")
        end
        t.check("searchCrate", goutweed_result == "ok" and goutweed_count > 0,
            "drive.op(eadgar_crate_goutweed, 1) after " .. crate_attempts .. " attempt(s) -- last op "
                .. tostring(crate_last_op) .. " " .. tostring(crate_last_detail)
                .. "; inv.count(eadgar_goutweed_herb) -> " .. tostring(goutweed_result) .. " " .. tostring(goutweed_count)
                .. " -- drive.op used here (not click_loc) because click_loc's own approach-tile retry is lethal in "
                .. "this guard nest, measured run 1 (see notebook)")

        local ejected_result, ejected_tile = t.world.tile()
        t.check("crate.ejected", ejected_result == "ok" and ejected_tile ~= nil
                and ejected_tile.x == 2865 and ejected_tile.z == 10088,
            "world.tile() -> " .. tostring(ejected_result) .. " " .. tostring(ejected_tile)
                .. " -- the crate's own posted guard always knocks the player out and teleports them here on a "
                .. "successful search (eadgar_troll_chief_cook.rs2's [oploc1,eadgar_crate_goutweed])")
        t.expect("player.alive_after_ejection", t.player.alive())

        -- ---------------------------------------------------------------
        -- 17. Hand the goutweed to Sanfew -> quest complete
        --     (areas/area_taverly/scripts/sanfew.rs2 sanfew_eadgar_turnin).
        --     Reward: 11000 Herblore XP, no item, per
        --     ~quest_complete_rewards(quest_eadgarsruse,
        --     "11000 Herblore XP|Trollheim Teleport spell|..."), sanfew.rs2:190.
        -- ---------------------------------------------------------------
        local herblore_snap_result, herblore_snap = t.skill.snapshot()
        t.check("reward.snapshot", herblore_snap_result == "ok",
            "skill.snapshot() -> " .. tostring(herblore_snap_result))

        t.exec("goto-sanfew-2", t.player.goto_tile, 2897, 3426, 1)
        t.exec("talkToSanfew-turnin", t.player.talk_to, "sanfew", 1)
        t.exec("talkToSanfew-turnin-dialog", t.chat.play, {
            "npc:What can I do for you young 'un",
            "choose:Have you any more work for me, to help reclaim the circle?",
            "player:Have you any more work for me to help reclaim the stone circle?",
            "npc:Did you find some goutweed for me?",
            "player:I have some goutweed!",
            "npc:Excellent! I will be able to complete the next part of the ritual now.",
            "npc:If you ever come across more goutweed, bring it to me",
        })
        t.chat.close()
        t.ticks(3) -- completion's own reward scroll mounts asynchronously (section 8)

        t.quest.expect_complete()
        t.exec("reward.herblore_xp", t.skill.expect_gain, "herblore", 11000, herblore_snap)
        t.finish(0)
        return
    end,
}
