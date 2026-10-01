-- Vampyre Slayer. Rewritten from tools/quest_gate/new_quest.py's scaffold
-- against the quest's own .rs2 (OSRS-Content/osrs239-content/server/scripts/
-- quests/quest_vampire/, areas/draynor/scripts/morgan.rs2,
-- areas/varrock/scripts/harlow.rs2, areas/varrock/scripts/bartender.rs2,
-- areas/draynor/scripts/garlic_cupboard.rs2, doors/scripts/doors.rs2).
--
-- Route: Morgan (accept) -> upstairs in Morgan's house for garlic (the
-- cupboard has no state guard at all, so grabbing it before Harlow is legal
-- content order) -> Dr Harlow at the Blue Moon Inn (first visit sets
-- quest_vampire_spoke_to_harlow with no beer needed yet) -> the Blue Moon
-- Inn's own bartender for a beer (bluemoon_bartender, 2 coins,
-- getItemRequirements() lists beerOrTwoCoins as brought-along -- the setup
-- below gives coins, and the beer itself is bought live through the
-- guide's own buyBeer step, never ::given) -> Harlow again (hands over the
-- stake once a beer is in the pack) -> Draynor Manor's front door
-- (haunteddoorl, a real, unconditional doors.rs2 category door -- opened
-- for real) -> the manor's basement.
--
-- goDownToBasement (cryptstairsdown): grepped the whole tree -- no
-- [oploc,cryptstairsdown] handler exists anywhere (ladders_stairs/configs/
-- ladders.loc only carries `category=climb_down` with no maplink.dbrow row
-- and no per-symbol script), so a live click answers the generic +/-1-plane
-- default, not a real descent. This is exactly section 2 / section 8's
-- sanctioned "floors and ladders" / "goto_tile bypass ... covers a
-- SCRIPTED PUZZLE-GATED door too, whenever the hand-in reads no lever or
-- door state" case (count_draynor's [ai_queue3] hand-in reads only
-- inv_total(stake)/inv_total(hammer), no door or lever var) -- goto_tile
-- straight to the coffin's own underground tile, the same trick section 2
-- names for the Wizards' Tower trapdoor (z+6400: m48_152 local 5,47 ->
-- 3077,9775,0, decoded off the loc's own maps/m48_152.jl2 row and
-- quest_vampire.rs2's npc_add coord 0_48_152_6_46).
--
-- Combat: count_draynor spawns off quest_vampire.npc's authored block
-- (hitpoints=35 attack=30 strength=25 defence=30, wiki level 34) and is
-- weakened by garlic AT SPAWN (npc_statsub -10/-10/-10/-40, clamped) since
-- garlic is already in the pack before the coffin is first opened. The
-- kill is finished by count_draynor.rs2's [ai_queue3] (seam19/seam21 fix:
-- npc_findhero binds the killer at CORPSE) -- stake+hammer both required in
-- the pack; the stake is consumed, the hammer is not. The completion is
-- QUEUED three ticks after the "You hammer the stake..." line, so this
-- waits for that line and then polls %vampire server-side rather than a
-- flat sleep (trap in section 8's completion-varp bullet).

return {
    id = "vampire",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99", -- combatGear is brought along, not gathered -- a fresh level-3 character cannot wear rune or safely trade hits with a level-34 aggressive npc
        "::give hammer 1", -- getItemRequirements(): brought along, no obtain step in the guide
        "::give coins 10", -- beerOrTwoCoins: brought-along currency for the live buyBeer step (price 2)
        "::give rune_scimitar 1", -- combatGear: brought along, worn in run()
        "::give adamant_platebody 1", -- NOT rune platebody: F2P rune platebody is gated on Dragon Slayer being complete first (measured run 3)
        "::give adamant_platelegs 1",
        "::give shark 5", -- food, not in the guide's item list but cheap insurance against a level-34 aggressive npc
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "vampire",
            constants = {
                not_started = 0,
                started = 1,
                spoke_to_harlow = 2,
                complete = 3,
            },
            row = "quest_vampyreslayer",
            display = "Vampyre Slayer", -- the quest-list dbrow text (configs/all.dbrow:8572 "values=1:0:Vampyre Slayer")
            journal_title = "Vampire Slayer", -- vampire_journal.rs2's own ~quest_journal("Vampire Slayer", ...) title, spelled differently in this port
            points = 3,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ---------------------------------------------------- talkToMorgan
        t.exec("goto-talkToMorgan", t.player.goto_tile, 3098, 3268, 0)
        t.exec("talkToMorgan", t.player.talk_to, "morgan", 1)
        t.exec("talkToMorgan-dialog", t.chat.play, {
            "npc:Could it be? A bold adventurer! Please, you must help us!",
            "player:What is it? What's the problem?",
            "npc:It's the evil vampyre, Count Draynor!",
            "choose:Yes.",
            "player:Sounds like a job for me. Where should I start?",
            "npc:Oh, thank goodness! I've been hoping this day would come",
            "npc:If you speak to him, I'm sure he'll be able to help.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- --------------------------------------------------- cGetGarlic --
        -- goUpstairsMorgan (plain navigation, a maplink-driven staircase in
        -- Morgan's own house -- section 2's "goto_tile the destination tile
        -- with ITS level is the whole of it", no click_loc on the stairs).
        t.exec("goUpstairsMorgan", t.player.goto_tile, 3098, 3268, 1)
        t.exec("cupboard.open", t.player.click_loc, "garliccupboardshut", 1)
        t.exec("getGarlic", t.player.click_loc, "garliccupboardopen", 1)
        t.exec("garlic.received", t.inv.await, "garlic", 1, 5)

        -- ----------------------------------------------------- talkToHarlow
        t.exec("goto-talkToHarlow", t.player.goto_tile, 3222, 3397, 0)
        t.exec("talkToHarlow", t.player.talk_to, "dr_harlow", 1)
        t.exec("talkToHarlow-dialog", t.chat.play, {
            "npc:Buy me a drink pleassh",
            "choose:I need your help dealing with a vampyre.",
            "player:I need your help dealing with a vampyre.",
            "npc:A vampyre you shhay",
            "player:Not just any vampyre. Count Draynor.",
            "npc:Draynor? Well, buy me a beer firsht",
            "player:Are you sure you've not had enough?",
            "npc:Huh? No, I don't think ssho. Now, buy ush a beer.",
        })
        t.expect("quest.stage.spoke_to_harlow", t.quest.expect_stage("spoke_to_harlow"))

        -- --------------------------------------------------------- buyBeer
        t.exec("goto-buyBeer", t.player.goto_tile, 3226, 3399, 0)
        t.exec("buyBeer", t.player.talk_to, "bluemoon_bartender", 1)
        t.exec("buyBeer-dialog", t.chat.play, {
            "npc:What can I do yer for?",
            "choose:A glass of your finest ale please.",
            "player:A glass of your finest ale please.",
            "npc:No problemo. That'll be 2 coins.",
        })
        t.exec("beer.received", t.inv.await, "beer", 1, 5)

        -- ------------------------------------------------ talkToHarlowAgain
        t.exec("goto-talkToHarlowAgain", t.player.goto_tile, 3222, 3397, 0)
        t.exec("talkToHarlowAgain", t.player.talk_to, "dr_harlow", 1)
        t.exec("talkToHarlowAgain-dialog", t.chat.play, {
            "npc:Buy me a drink pleassh",
            "player:Yes, here you go.",
            "mesbox:You give a beer to Dr Harlow.",
            "npc:Cheersh, matey",
            "player:Now, about Count Draynor",
            "npc:Yesh, Count Draynor! The evil nashty vampyre",
            "npc:You want to havesh a go?",
            "player:So how do I make sure I'm prepared?",
            "npc:Most vampyres regenerate.",
            "mesbox:Dr Harlow hands you a stake.",
            "npc:Takesh that to Draynor Manor",
            "npc:Oh, and yoush should take some garlic",
            "player:Garlic? Hmm",
        })
        t.exec("stake.received", t.inv.await, "stake", 1, 5)

        -- --------------------------------------------------- prepareAndKillDraynor
        -- enterDraynorManor: a real doors.rs2 category door, opened for
        -- real from outside its south side (the open_manor_entrance side
        -- test refuses when coordz(coord) > coordz(loc_coord)).
        t.exec("goto-enterDraynorManor", t.player.goto_tile, 3108, 3351, 0)
        t.exec("enterDraynorManor", t.player.click_loc, "haunteddoorl", 1)

        -- goDownToBasement: see file banner -- cryptstairsdown has no real
        -- trigger in this port, so this is the sanctioned ladder/stairs
        -- goto_tile bypass, not a walked click.
        t.exec("goDownToBasement", t.player.goto_tile, 3079, 9776, 0)

        -- ------------------------------------------------------- openCoffin
        t.exec("openCoffin", t.player.click_loc, "vampcoffin", 1)
        -- quest_vampire_coffin_open runs p_delay(4) before npc_add + the
        -- garlic-weaken message, so give it the room before reading either.
        -- t.npc.await_present is hollow (ok/timeout only, no detail --
        -- QUEST_AUTHORING trap 12), so read the npc back with t.npc.nearest
        -- and write its slot/coord into the row ourselves.
        local present_result = t.npc.await_present("count_draynor", 10, 15)
        local nearest_result, nearest_row = t.npc.nearest("count_draynor", 10)
        local present_detail
        if nearest_result == "ok" then
            present_detail = string.format(
                "count_draynor slot %s (element %s) at %s,%s,%s",
                tostring(nearest_row.slot), tostring(nearest_row.element_id),
                tostring(nearest_row.x), tostring(nearest_row.z), tostring(nearest_row.level))
        else
            present_detail = "count_draynor await_present=" .. tostring(present_result)
                .. ", nearest lookup=" .. tostring(nearest_result)
        end
        t.check("draynor.present", present_result == "ok", present_detail)
        -- The weaken mes() lands in the same tick as npc_add, which
        -- draynor.present's own wait already polled past -- read it back
        -- with msg.expect (already in the ring), not msg.await (trap:
        -- section 8's "a line still in the ring" rule).
        t.check("draynor.weakened", t.msg.expect("weakened by the garlic"))

        -- --------------------------------------------------------- killDraynor
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")
        t.exec("equip.body", t.player.equip, "adamant_platebody")
        t.exec("equip.legs", t.player.equip, "adamant_platelegs")

        t.exec("killDraynor", t.player.attack, "count_draynor", 2, 15)
        t.exec("killDraynor.dead", t.npc.await_dead_engaged, 60, 6)

        -- Snapshot AFTER the fight, not before: ordinary combat damage
        -- (rune scimitar, accurate style) grants its own Attack xp on top
        -- of the quest's flat reward, and every hit has already landed by
        -- the time the npc's slot leaves the pool -- run 2 measured
        -- delta=4925 against expected=4825 for exactly this reason (100 xp
        -- of real combat xp mixed into the snapshot window). Only the
        -- queued completion's stat_advance(attack, 48250) happens after
        -- this point.
        local snapshot_result, snapshot = t.skill.snapshot()
        local attack_before = type(snapshot) == "table" and snapshot.attack or nil
        local snapshot_detail
        if type(attack_before) == "table" then
            snapshot_detail = string.format(
                "attack level=%s experience=%s",
                tostring(attack_before.level), tostring(attack_before.experience))
        else
            snapshot_detail = "attack reading unavailable: " .. tostring(attack_before)
        end
        t.check("attack.snapshot", snapshot_result == "ok", snapshot_detail)

        -- The stake finish is a queued proc (count_draynor.rs2's
        -- [ai_queue3]): wait for its own mes() line, then poll the
        -- completion varp server-side rather than a flat sleep (section 8).
        t.exec("draynor.staked", t.msg.await, "hammer the stake into the vampyre", 20)
        t.exec("vampire.complete_var", t.var.await_server, "vampire", 3, 15)

        t.quest.expect_complete()
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 4825, snapshot))

        t.finish(0)
    end,
}
