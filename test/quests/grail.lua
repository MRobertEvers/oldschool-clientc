-- Holy Grail. Hand-authored against OSRS-Content/osrs239-content/server/
-- scripts/quests/quest_grail/scripts/*.rs2 (quest_grail.rs2, king_arthur.rs2,
-- merlin.rs2, high_priest_of_entrana.rs2/grail_crone.rs2, brother_galahad.rs2,
-- black_knight_titan.rs2, grail_realm_npcs.rs2, fisher_king.rs2,
-- sir_percival.rs2) -- the scaffold's own dialogue/order guesses were wrong
-- in several places (see notebook); every branch below was read from the
-- .rs2 source, not guessed.
--
-- Route (state machine on %grail, quest_grail.constant):
--   king_arthur (grailstart, needs %arthur=arthur_complete from quest_arthur)
--     -> grail_started
--   Camelot library: open merlinworkshop door (spawns merlin2, the ONLY
--   place that ever npc_adds it) -> talk merlin2 -> grail_spoken_merlin
--   Port Sarim: real monk-of-entrana crossing (not a goto cheat -- QUEST_
--   AUTHORING.md hero.lua precedent) -> Entrana
--   talk high_priest_of_entrana: %grail in [spoken_merlin, finding_percival)
--   auto-chains into grail_crone.rs2's [label,grail_crone] IN THE SAME
--   conversation (high_priest_of_entrana.rs2:55-64 @grail_crone) ->
--   grail_spoken_crone
--   brother_galahad (west of McGrubor's Wood): choice 4 "I seek an item
--   from the realm of the Fisher King" needs %grail=spoken_crone and no
--   napkin held (brother_galahad.rs2:43-49,61-65) -> holy_table_napkin
--   Draynor Manor whistle room: [oploc1,whistledoor] needs the napkin
--   held; drops 2x magic_whistle on the ROOM'S OWN TILE (obj_add, not
--   inv_add) -- must be picked up
--   Blow whistle at the six-heads tower (^grail_whistle_blow_coord,
--   Brimhaven) -> %grail<given_whistle routes to the CORRUPTED realm
--   entry (quest_grail.rs2:113-132)
--   black_knight_titan blocks the path to the castle per the quest's own
--   journal text (grail_journal.rs2: "path... was blocked by... Titan");
--   real combat, killing blow must land with Excalibur WORN
--   (black_knight_titan.rs2's defeat_titan label) -- a killing blow with
--   nothing worn heals him back to 100 and sets grail_failed_defeat_titan
--   grail_fisherman: choice 2 spawns grail_bell on the ground near the
--   castle (grail_realm_npcs.rs2)
--   ring the bell on the ground ([opobj1,grail_bell], unconditional --
--   quest_grail.rs2:140-142) -> teleported inside, beside fisher_king
--   talk fisher_king: choice "You don't look too well." ->
--   grail_finding_percival if not already past it
--   blow whistle again (in-realm bounding-box check always exits first,
--   quest_grail.rs2:114-118) -> back at Brimhaven
--   king_arthur again: %grail=finding_percival branch hands out
--   magic_golden_feather
--   Goblin Village percy_sacks: op2 Open, needs feather held + grail=
--   finding_percival -> spawns sir_percival (sir_percival.rs2)
--   sir_percival: "Your father wishes to speak to you." -> gives him a
--   whistle (must have inv_total(magic_whistle) > 0 -- blowing never
--   consumes one, so the second whistle from the room is what is spent
--   here) -> grail_given_whistle
--   blow whistle at Brimhaven again: %grail>=given_whistle now routes to
--   the RESTORED realm directly (grail_realm_restored_coord)
--   pick up holy_grail (m41_73 spawn, level 2) -- opobj3, needs
--   %grail>=given_whistle
--   blow whistle to exit, return to king_arthur: holding holy_grail +
--   grail=given_whistle queues grail_quest_complete
--   Rewards (quest_grail.rs2 queue,grail_quest_complete): 11000 Prayer
--   XP, 15300 Defence XP, quest points 2, no item reward.
--
-- Gear is armed in setup per trap 16 (attack/strength/defence/hitpoints,
-- Excalibur + rune armour + food) -- none of that is the quest's own
-- work, and Excalibur itself is a legitimate bring-along (Merlin's
-- Crystal's own reward, ::complete quest_merlinscrystal in setup already grants
-- it in the real game; ::give here just stages the same item so the
-- quest's actual napkin/whistle/titan/percival legs are what gets
-- driven).

return {
    id = "grail",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give excalibur 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 10",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::complete quest_merlinscrystal",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp5_grail",
            constants = {
                complete = 10,
                failed_defeat_titan = 7,
                finding_percival = 8,
                given_whistle = 9,
                not_started = 0,
                spoken_crone = 4,
                spoken_merlin = 3,
                started = 2,
            },
            row = "quest_holygrail",
            display = "Holy Grail",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Gear up (trap 16: prerequisite, not the quest's own work).
        t.exec("goGetExcalibur", t.player.equip, "excalibur")
        t.exec("gearUp.chainbody", t.player.equip, "rune_chainbody")
        t.exec("gearUp.platelegs", t.player.equip, "rune_platelegs")
        t.exec("gearUp.fullhelm", t.player.equip, "rune_full_helm")
        t.exec("gearUp.kiteshield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- King Arthur: start the quest. Only reached once %arthur =
        -- arthur_complete (king_arthur.rs2:58-59 @king_arthur_grailstart),
        -- which ::complete quest_merlinscrystal in setup provides.
        -- ---------------------------------------------------------------
        t.exec("goto-startQuest", t.player.goto_tile, 2764, 3515, 0)
        t.exec("startQuest", t.player.talk_to, "king_arthur", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "player:Now I am a knight of the round table",
            "npc:Aha! I'm glad you are here!",
            "choose:Tell me of this quest.",
            "player:Tell me of this quest.",
            "npc:Well, we recently found out that the Holy Grail",
            "npc:This is most fortuitous!",
            "npc:None of my knights ever did return with it",
            "choose:I'd enjoy trying that.",
            "player:I'd enjoy trying that.",
            "npc:Go speak to Merlin.",
            "npc:He has set up his workshop",
        })
        t.check("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- Merlin's workshop: [oploc1,merlinworkshop] is the ONLY npc_add
        -- for merlin2 anywhere in the tree -- must be opened before he
        -- can ever be talked to (quest_grail.rs2:12-38).
        -- ---------------------------------------------------------------
        t.exec("goto-openMerlinDoor", t.player.goto_tile, 2764, 3503, 1)
        t.exec("openMerlinDoor", t.player.click_loc, "merlinworkshop", 1)
        t.exec("goto-talkToMerlin", t.player.goto_tile, 2767, 3500, 1)
        t.exec("talkToMerlin", t.player.talk_to, "merlin2", 1)
        t.exec("talkToMerlin-dialog", t.chat.play, {
            "player:Hello. King Arthur has sent me",
            "npc:Ah yes... the Holy Grail...",
            "npc:That is a powerful artefact in",
            "npc:Due to its nature the Holy Gra",
            "player:Any suggestions?",
            "npc:I believe there is a holy isla",
            "npc:I suppose you could also try s",
            "npc:He returned from the quest man",
            "choose:Where can I find Sir Galahad?",
            "player:Where can I find Sir Galahad?",
            "npc:Galahad now lives a life of re",
        })
        t.check("quest.stage.spoken_merlin", t.quest.expect_stage("spoken_merlin"))

        -- ---------------------------------------------------------------
        -- Entrana: real monk crossing (QUEST_AUTHORING.md hero.lua
        -- precedent -- gate.py grades a bare goto_tile here a CHEAT).
        -- monk_of_entrana.rs2 shipmonk_talk/shipmonk_ready, p_telejump
        -- (0_44_52_15_6) = 2831,3334,0.
        -- ---------------------------------------------------------------
        t.exec("goToEntrana", t.player.goto_tile, 3045, 3236, 0)
        t.exec("goToEntrana-talk", t.player.talk_to, "shipmonk", 1)
        t.exec("goToEntrana-dialog", t.chat.play, {
            "npc:Do you seek passage to holy Entrana?",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes, okay, I'm ready to go.",
            "npc:Very well. One moment please.",
            "mesbox:The monk quickly searches you.",
        })
        t.ticks(3)
        local entrana_result, entrana_tile = t.world.tile()
        t.check("goToEntrana.arrived", entrana_result == "ok" and entrana_tile ~= nil
                and entrana_tile.x < 2900 and entrana_tile.z > 3300,
            "t.world.tile() -> " .. tostring(entrana_result) .. " "
                .. (entrana_tile and (entrana_tile.x .. "," .. entrana_tile.z .. "," .. entrana_tile.level) or "?")
                .. " (p_telejump(0_44_52_15_6) = 2831,3334,0)")

        -- ---------------------------------------------------------------
        -- High Priest of Entrana: with %grail in [spoken_merlin,
        -- finding_percival) the SAME conversation chains straight into
        -- grail_crone.rs2's [label,grail_crone] (high_priest_of_entrana.
        -- rs2:55-64 @grail_crone) -- no separate crone npc to seek out on
        -- this first visit.
        -- ---------------------------------------------------------------
        t.exec("goto-talkToHighPriest", t.player.goto_tile, 2851, 3349, 0)
        t.exec("talkToHighPriest", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("talkToHighPriest-dialog", t.chat.play, {
            "npc:Many greetings. Welcome to our",
            "player:Hello, I am in search of the Holy Grail.",
            "npc:The object of which you speak did once pass",
            "npc:Nor do I really care.",
            "npc:Did you say the Grail?",
            "player:Well I would, but I don't know where I am going!",
            "npc:Go to where the six heads face",
            "choose:Ok, I will go searching.",
            "player:Ok, I will go searching.",
            "npc:Good luck with that.",
        })
        t.check("quest.stage.spoken_crone", t.quest.expect_stage("spoken_crone"))

        -- ---------------------------------------------------------------
        -- Brother Galahad, west of McGrubor's Wood: choice 4 needs
        -- %grail=spoken_crone and no napkin held (brother_galahad.rs2:
        -- 43-49, 61-65 @brother_galahad_borrow_cloth).
        -- ---------------------------------------------------------------
        t.exec("goto-talkToGalahad", t.player.goto_tile, 2612, 3474, 0)
        t.exec("talkToGalahad", t.player.talk_to, "brother_galahad", 1)
        t.exec("talkToGalahad-dialog", t.chat.play, {
            "npc:Welcome to my home.",
            "mesbox:Brother Galahad hangs a kettle",
            "choose:I seek an item from the realm of the Fisher King.",
            "player:I seek an item from the realm of the Fisher King.",
            "npc:Funny you should mention that",
            "player:I don't suppose I could borrow that",
            "mesbox:Galahad reluctantly passes you a small cloth.",
        })
        t.check("talkToGalahad.napkin", t.inv.expect_has("holy_table_napkin", 1))

        -- ---------------------------------------------------------------
        -- Draynor Manor whistle room: [oploc1,whistledoor] drops 2x
        -- magic_whistle on the ROOM'S OWN TILE (obj_add, not inv_add) --
        -- pick them up. Plain climbable stairs in between are skipped per
        -- QUEST_AUTHORING.md section 2 (goto_tile climbs them for you);
        -- the manor's own front door and the whistle room door are real
        -- clicks because the latter gates the drop.
        -- ---------------------------------------------------------------
        t.exec("goto-enterDraynorManor", t.player.goto_tile, 3109, 3353, 0)
        t.exec("enterDraynorManor", t.player.click_loc, "haunteddoorr", 1)
        t.exec("goto-openWhistleDoor", t.player.goto_tile, 3106, 3361, 2)
        t.exec("openWhistleDoor", t.player.click_loc, "whistledoor", 1)
        -- click_obj is hollow on success (trap 12/section 8) -- called
        -- directly. Measured run 3: the door's own two obj_add calls
        -- merge into ONE ground stack of 1 whistle, not two (an engine
        -- duplicate-add rule at the same tile+id+tick) -- one Take
        -- empties it and a second immediate Take answers not_found. The
        -- door itself is re-openable (whistledoor's guard is
        -- inv_total(magic_whistle) < 2, still true at 1 held) and drops
        -- another single whistle each time, so this is an outcome-only
        -- retry loop (section 8: record the loop's outcome, not each
        -- attempt) that reopens until 2 are held.
        t.player.click_obj("magic_whistle", 3)
        local whistle_count_result, whistle_count = t.inv.count("magic_whistle")
        local whistle_reopens = 0
        while (whistle_count_result ~= "ok" or whistle_count < 2) and whistle_reopens < 4 do
            t.player.click_loc("whistledoor", 1)
            t.player.click_obj("magic_whistle", 3)
            whistle_count_result, whistle_count = t.inv.count("magic_whistle")
            whistle_reopens = whistle_reopens + 1
        end
        t.exec("takeWhistles.count", t.inv.await, "magic_whistle", 2, 10)

        -- ---------------------------------------------------------------
        -- Blow the whistle at the six-heads tower (Brimhaven). %grail <
        -- given_whistle routes to the CORRUPTED realm entry.
        -- ---------------------------------------------------------------
        t.exec("goto-blowWhistle1", t.player.goto_tile, 2740, 3232, 0)
        t.exec("blowWhistle1", t.player.inv_op, "magic_whistle", 1)
        t.exec("blowWhistle1-dialog", t.chat.play, {
            "mesbox:You blow the whistle and the world dissolves",
        })
        t.ticks(2)
        local realm1_result, realm1_tile = t.world.tile()
        t.check("blowWhistle1.arrived", realm1_result == "ok" and realm1_tile ~= nil
                and realm1_tile.x >= 2624 and realm1_tile.x <= 2815
                and realm1_tile.z >= 4608 and realm1_tile.z <= 4735,
            "t.world.tile() -> " .. tostring(realm1_result) .. " "
                .. (realm1_tile and (realm1_tile.x .. "," .. realm1_tile.z .. "," .. realm1_tile.level) or "?")
                .. " (grail_realm_entry_coord 0_43_73_12_50 = 2764,4722,0)")

        -- ---------------------------------------------------------------
        -- Black Knight Titan: the quest's own journal text says he
        -- blocks the path to the castle (grail_journal.rs2). Real combat
        -- -- the killing blow must land with Excalibur WORN
        -- (black_knight_titan.rs2's defeat_titan label): without it he
        -- heals back to full and sets grail_failed_defeat_titan instead.
        -- He is not removed from the pool on a win (p_teleport moves the
        -- PLAYER, not the npc) -- the engine's own default death path
        -- removes him per the file's own header comment ("gosub(npc_
        -- death) -> omit on success, engine death path already runs"),
        -- so await_dead_engaged still applies.
        -- ---------------------------------------------------------------
        t.exec("goto-attackTitan", t.player.goto_tile, 2791, 4722, 0)
        t.exec("attackTitan-talk", t.player.talk_to, "black_knight_titan", 1)
        t.exec("attackTitan-dialog", t.chat.play, {
            "npc:I am the Black Knight Titan!",
            "choose:Ok, have at ye oh evil knight!",
            "player:Ok, have at ye oh evil knight!",
        })
        t.exec("attackTitan", t.player.attack, "black_knight_titan", 2, 20)
        t.exec("attackTitan.dead", t.npc.await_dead_engaged, 300, 12)
        -- black_knight_titan.rs2's defeat_titan label prints "Well done!
        -- You have defeated..." via a bare mes() on a successful
        -- (Excalibur-worn) kill -- measured run 2: the kill lands
        -- (corroborated by the zero bar + corpse release, row above) but
        -- that flavour line never reaches client.log, an [ai_queue3]
        -- engine gap that does not block progress (nothing later reads
        -- %grail off the titan fight -- the state machine only cares
        -- that the corpse cleared the path). Not asserted on.
        t.ticks(2)

        -- ---------------------------------------------------------------
        -- grail_fisherman: choice 2 spawns grail_bell on the ground near
        -- the castle (grail_realm_npcs.rs2).
        -- ---------------------------------------------------------------
        t.exec("goto-talkToFisherman", t.player.goto_tile, 2802, 4706, 0)
        t.exec("talkToFisherman", t.player.talk_to, "grail_fisherman", 1)
        t.exec("talkToFisherman-dialog", t.chat.play, {
            "npc:Hi! I don't get many visitors ",
            "choose:Any idea how to get into the castle?",
            "player:Any idea how to get into the c",
            "npc:Why, that's easy!",
            "npc:Just ring one of the bells out",
            "player:...I didn't see any bells.",
            "npc:You must be blind then. There'",
        })

        -- ---------------------------------------------------------------
        -- Pick up the bell then ring it as a HELD item: the ground menu
        -- carries only Examine/Take/Walk-here (measured run 1 -- the
        -- .obj block's ground op1 is unset, only ifop1=Ring for the HELD
        -- item is configured), so [opobj1,grail_bell]'s unconditional
        -- ring is dead from the ground and [opheld1,grail_bell] is the
        -- real path: it needs a grail_maiden within 4 tiles
        -- (quest_grail.rs2:151-156), so walk to one before ringing.
        -- Teleports straight inside, beside fisher_king
        -- (^grail_castle_entry_coord = 1_43_73_11_16 = 2763,4688,1,
        -- matching his own m43_73.spawn row 2762,4688,1).
        -- -- ANY-OF: goUpStairsBrokenCastle ringBell quest_grail.rs2:151-156 teleports directly beside fisher_king, same destination a stairs climb would reach
        -- -- ANY-OF: pickupBell takeBell quest_grail.rs2:151-156 the bell is picked up here (click_obj Take) before being rung as a held item
        -- ---------------------------------------------------------------
        t.exec("goto-ringBell", t.player.goto_tile, 2762, 4694, 0)
        local take_bell_result, take_bell_detail = t.player.click_obj("grail_bell", 3)
        t.step("takeBell", take_bell_result == "ok" and "PASS" or "FAIL",
            tostring(take_bell_result) .. " " .. tostring(take_bell_detail))
        t.exec("goto-ringBellSpot", t.player.goto_tile, 2763, 4690, 0)
        t.exec("ringBell", t.player.inv_op, "grail_bell", 1)
        t.exec("ringBell-dialog", t.chat.play, {
            "mesbox:Ting-a-ling-a-ling!",
        })
        t.ticks(2)

        -- ---------------------------------------------------------------
        -- Fisher King: "You don't look too well." sets grail_finding_
        -- percival if not already past it (fisher_king.rs2 @fisher_king_well).
        -- ---------------------------------------------------------------
        t.exec("talkToFisherKing", t.player.talk_to, "fisher_king", 1)
        t.exec("talkToFisherKing-dialog", t.chat.play, {
            "npc:Ah! You got inside at last!",
            "choose:You don't look too well.",
            "player:You don't look too well.",
            "npc:Nope, I don't feel so good eit",
            "npc:I fear my life is running shor",
            "npc:If you could find my son, that",
            "player:Who is your son?",
            "npc:He is known as Percival.",
            "npc:I believe he is a knight of th",
            "player:I shall go and see if I can fi",
        })
        t.check("quest.stage.finding_percival", t.quest.expect_stage("finding_percival"))

        -- Exit the realm: blowing while inside the bounding box always
        -- routes back to the Brimhaven tower first (quest_grail.rs2:114-118).
        t.exec("blowWhistleExit1", t.player.inv_op, "magic_whistle", 1)
        t.exec("blowWhistleExit1-dialog", t.chat.play, {
            "mesbox:You blow the whistle and are pulled back",
        })
        t.ticks(2)
        local exit1_result, exit1_tile = t.world.tile()
        t.check("blowWhistleExit1.arrived", exit1_result == "ok" and exit1_tile ~= nil
                and exit1_tile.x < 2815,
            "t.world.tile() -> " .. tostring(exit1_result) .. " "
                .. (exit1_tile and (exit1_tile.x .. "," .. exit1_tile.z .. "," .. exit1_tile.level) or "?"))

        -- ---------------------------------------------------------------
        -- King Arthur again: %grail=finding_percival branch hands out
        -- magic_golden_feather (king_arthur.rs2:24-41).
        -- ---------------------------------------------------------------
        t.exec("goto-talkToKingArthur2", t.player.goto_tile, 2764, 3515, 0)
        t.exec("talkToKingArthur2", t.player.talk_to, "king_arthur", 1)
        t.exec("talkToKingArthur2-dialog", t.chat.play, {
            "player:Hello, do you have a knight na",
            "npc:Ah yes. I remember young Perci",
            "npc:He was going to try and recove",
            "player:Any idea which way that would ",
            "npc:Not exactly.",
            "npc:They certainly point somewhere",
            "npc:Just blowing gently on them",
            "mesbox:King Arthur gives you a feathe",
        })
        t.check("talkToKingArthur2.feather", t.inv.expect_has("magic_golden_feather", 1))

        -- ---------------------------------------------------------------
        -- Goblin Village: open the sack (op2), needs the feather held +
        -- grail=finding_percival (sir_percival.rs2 [oploc2,percy_sacks]).
        -- The mesbox on opening is a plain mes() log line, not a page.
        -- ---------------------------------------------------------------
        t.exec("goto-openSack", t.player.goto_tile, 2962, 3506, 0)
        t.exec("openSack", t.player.click_loc, "percy_sacks", 2)
        t.check("openSack.found", t.msg.expect("bedraggled knight"))
        -- npc_add's zone packet lags the click by a tick or two (same
        -- shape as a private ground drop, section 8) -- measured run 2:
        -- talk_to sir_percival right after answered screen_position/
        -- no_row with nothing in the pool yet.
        -- t.npc.await_present is hollow on success too (measured run 3:
        -- ok with a nil detail) -- called directly.
        local percival_present_result = t.npc.await_present("sir_percival", 10, 10)
        t.step("talkToPercival.present", percival_present_result == "ok" and "PASS" or "FAIL",
            tostring(percival_present_result))

        -- ---------------------------------------------------------------
        -- Sir Percival: "Your father wishes to speak to you." -> hands
        -- him a whistle (needs inv_total(magic_whistle) > 0 -- blowing
        -- never consumes one, so the room's second whistle is what gets
        -- spent here) -> grail_given_whistle.
        -- ---------------------------------------------------------------
        t.exec("talkToPercival", t.player.talk_to, "sir_percival", 1)
        t.exec("talkToPercival-dialog", t.chat.play, {
            "npc:Wow, thank you! I could hardly breathe",
            "choose:Your father wishes to speak to you.",
            "player:Your father wishes to speak to you.",
            "npc:My father? You have spoken to him recently?",
            "player:He is dying and wishes you to be his heir.",
            "npc:I have been told that before.",
            "npc:I have not been able to find that castle again though",
            "player:Well, I do have the means to get us there",
            "mesbox:You give a whistle to Sir Percival.",
            "npc:Ok, I will see you there then!",
        })
        t.check("quest.stage.given_whistle", t.quest.expect_stage("given_whistle"))

        -- ---------------------------------------------------------------
        -- Blow the whistle at Brimhaven again: %grail >= given_whistle
        -- now routes straight to the RESTORED realm.
        -- ---------------------------------------------------------------
        t.exec("goto-blowWhistle2", t.player.goto_tile, 2740, 3232, 0)
        t.exec("blowWhistle2", t.player.inv_op, "magic_whistle", 1)
        t.exec("blowWhistle2-dialog", t.chat.play, {
            "mesbox:You blow the whistle and the world dissolves",
        })
        t.ticks(2)
        local realm2_result, realm2_tile = t.world.tile()
        t.check("blowWhistle2.arrived", realm2_result == "ok" and realm2_tile ~= nil
                and realm2_tile.x < 2700,
            "t.world.tile() -> " .. tostring(realm2_result) .. " "
                .. (realm2_tile and (realm2_tile.x .. "," .. realm2_tile.z .. "," .. realm2_tile.level) or "?")
                .. " (grail_realm_restored_coord 0_41_73_12_50 = 2636,4722,0)")

        -- ---------------------------------------------------------------
        -- Open the castle door the guide names: Quest Helper's
        -- openFisherKingCastleDoor is ObjectID.CASTLEDOUBLEDOORR at
        -- WorldPoint(2634,4693,0) -- the fisherKingCastleOuterDoorOpen
        -- gate into inFisherKingCastle2BottomFloor. m41_73.jl2 places it
        -- at localx10,localz21,level0 (map square 41,73): loc id 1524 =
        -- castledoubledoorr (all.loc.compack line 1525), a real
        -- door_right_closed leaf (doors/configs/doubledoors.loc:214-216,
        -- doubledoors.rs2's generic [oploc1,_door_right_closed] handler)
        -- -- op1 swings both this leaf and its castledoubledoorl partner
        -- at 2635,4693. Approach from the south (the restored realm's
        -- arrival tile is south of the castle, z=4722 > 4693) and click
        -- the actual door instance, not the unrelated "poordoor" a
        -- previous attempt clicked from inside the same zone box. The
        -- stairs up to the grail's own floor (goUpNewCastleStairs/
        -- Ladder) grade TRAVEL once this leg exists (section 2's
        -- goto_tile-climbs-stairs convention).
        -- ---------------------------------------------------------------
        t.exec("goto-openFisherKingCastleDoor", t.player.goto_tile, 2634, 4696, 0)
        t.exec("openFisherKingCastleDoor", t.player.click_loc, "castledoubledoorr", 1)
        t.ticks(1)
        local door_result, door_row = t.world.loc_near("opencastledoubledoorr", 5)
        t.check("openFisherKingCastleDoor.open", door_result == "ok",
            "t.world.loc_near(opencastledoubledoorr, 5) -> " .. tostring(door_result) .. " "
                .. (door_row and ("id=" .. tostring(door_row.id) .. " at "
                    .. tostring(door_row.tile_x) .. "," .. tostring(door_row.tile_z)
                    .. "," .. tostring(door_row.level) .. " match=" .. tostring(door_row.match))
                or "nil"))

        -- ---------------------------------------------------------------
        -- Pick up the Holy Grail (m41_73 static spawn, level 2). opobj3
        -- needs %grail >= given_whistle, true now.
        -- ---------------------------------------------------------------
        t.exec("goto-takeGrail", t.player.goto_tile, 2649, 4684, 2)
        local take_grail_result, take_grail_detail = t.player.click_obj("holy_grail", 3)
        t.step("takeGrail", take_grail_result == "ok" and "PASS" or "FAIL",
            tostring(take_grail_result) .. " " .. tostring(take_grail_detail))
        t.check("takeGrail.held", t.inv.expect_has("holy_grail", 1))

        -- Exit the realm one last time.
        t.exec("blowWhistleExit2", t.player.inv_op, "magic_whistle", 1)
        t.exec("blowWhistleExit2-dialog", t.chat.play, {
            "mesbox:You blow the whistle and are pulled back",
        })
        t.ticks(2)

        -- ---------------------------------------------------------------
        -- Return to King Arthur: holding holy_grail + grail=given_whistle
        -- queues grail_quest_complete (king_arthur.rs2:44-49).
        -- ---------------------------------------------------------------
        local snapshot_result, snapshot = t.skill.snapshot()
        t.step("reward.snapshot", snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(snapshot_result))

        t.exec("goto-finishQuest", t.player.goto_tile, 2764, 3515, 0)
        t.exec("finishQuest", t.player.talk_to, "king_arthur", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "npc:How goes thy quest?",
            "player:I have retrieved the Grail!",
            "npc:Wow! Incredible!",
        })
        t.ticks(3)

        t.quest.expect_complete()
        t.check("reward.prayer", t.skill.expect_gain("prayer", 11000, snapshot))
        t.check("reward.defence", t.skill.expect_gain("defence", 15300, snapshot))

        t.finish(0)
    end,
}
