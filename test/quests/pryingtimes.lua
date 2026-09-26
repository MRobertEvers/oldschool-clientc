-- Prying Times -- 'Squawking' Steve Beanie's crowbar quest (after Pandemonium).
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_pryingtimes/.
--
-- RE-AUTHORED 2026-09-26 against parity1o's content rebuild (both
-- pryingtimes.rs2 and pryingtimes_locs.rs2 were rewritten that pass -- read
-- them fresh, not this file's own earlier banner, which described the OLD
-- shape: ::spawn'd Steve, a Tobias courier crossing with a ground crate and
-- p_teleports). What is true NOW:
--
--   * Steve is a real spawn (configs/steve_beanie.spawn, base symbol
--     `steve_beanie`, multivarbit=sailing_intro) -- no `::spawn` cheat.
--     He stands behind the bar counter; `[apnpc1,steve_beanie]` is the
--     pack's barkeep idiom (party_pete.rs2/death_barman) for talking across
--     furniture, so `talk_to("steve_beanie", 1)` just works from in front
--     of the bar.
--   * `pry_steve_talk` opens on a STANDARD menu ("Yarr! What will it be?",
--     `~p_choice5`) whose third row is the quest's own context-sensitive
--     line (`[proc,pry_steve_quest_row]`) -- never a bare quest question.
--   * The 0->5 leg writes port task 600 (configs/all.dbrow
--     port_task_prying_times) into a free Captain's log slot
--     (`[label,pry_steve_write_log]`); the 5->10 leg is that port task
--     itself on the GENERIC port-task engine
--     (sailing/scripts/port_tasks.rs2): Take-cargo at Port Sarim's ledger,
--     sail the player's OWN skiff, Deposit-cargo at the Pandemonium's
--     ledger (`[proc,pry_looty_delivered]` writes 10) -- no Tobias courier
--     branch, no ground crate, no p_teleport anywhere in this leg.
--   * The 25 leg (test the key) is a real SailStep: the player's own skiff
--     to the sea crate north-west of the Pandemonium
--     (`[aploc1,sailing_charting_drink_crate]`, reached at approach
--     distance from the deck), Pry-open from the deck's own minimenu, drink
--     the stout ON THE BOAT (the `[opheld1]` branch refuses it ashore).
--   * Steve's own sealed crate (`pry_crate_sealed`, a cache-map multiloc,
--     multivarbit=quest_pry) needs no placement at all; the sea crate
--     (`sailing_charting_drink_crate`) is `loc_add`ed by
--     `[proc,pry_ensure_crates]` once %quest_pry >= test_key.
--
-- Proved end to end (0 -> 35 in one run, no quest-stage cheat) at
-- build/parity_state/parity1o/pry_scripts/e_full.lua, SUMMARY 100 PASS
-- pass=100 fail=0, twice (parity1o steps 6 and 9). This file follows that
-- proof's own route/camera/press choices; the two departures from it are
-- QUEST_AUTHORING.md's own rules the scratch proof was not bound by:
--   1. The drink troll (`killTheTroll`, Quest Helper's own optional NpcStep
--      -- "Kill the Drink Troll, or log out", not in loadSteps()'s stage
--      map at all) is declared a content gap rather than fought, on TWO
--      independent content-side findings measured across this file's own
--      runs 4-8 (see the comment beside the marker below): its own
--      `[opnpc2,...]` binding is `~npc_retaliate(0);` alone with no
--      `@player_combat_start` jump (trap 31's exact shape -- a real Attack
--      press lands but never damages it, confirmed live), and its own
--      `npc_add` spawn was unreliable in the client's pool independent of
--      that. Confirmed from the .rs2: killing the troll writes no varp and
--      gates no stage -- `[label,pry_steve_talk]`'s 25 branch advances to
--      `open_crate` on the DIALOGUE alone
--      (`%sailing_charting_drink_crate_prying_times_complete = 1` from
--      drinking), so the guide's own "or log out" alternative costs nothing.
--   2. Reward rows: `skill.snapshot()` immediately before the hand-in click
--      (`pry_open_bar_crate` grants the stat/item rewards in the SAME
--      script pass as the click, before any of its own dialogue is even
--      drawn -- trap 24), then `skill.expect_gain`/`inv.expect_has` rows
--      after `quest.expect_complete()` for every reward
--      `[proc,pry_quest_complete]`/PryingTimes.java's own reward lists
--      name: 1000 Smithing XP (^pry_smith_xp = 10000 tenths), 800 Sailing
--      XP (^pry_sailing_xp = 8000 tenths), 25 oak sawmill coupons
--      (^pry_coupon_count), the crowbar itself.

return {
    id = "pryingtimes",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::pryingtimes", -- resets %quest_pry, grants sailing_log/steel_bar/redberry_pie/hammer,
                          -- tops smithing/sailing if under the quest's own requirement,
                          -- teleports to ^pry_bar_front_coord (in front of Steve's bar)
        "::setlevel smithing 30", -- belt-and-braces: ~pry_can_start gates on stat_base(smithing) >= 30
        "::setlevel sailing 20", -- and stat_base(sailing) >= 12 -- both real base levels, not xp alone
        -- NOT combat levels here: `[label,pry_steve_start]`'s own
        -- `~player_combat_level < 10` warning mesbox is part of the
        -- transcript's own startQuest dialogue (measured run 1 -- a
        -- combat level raised in setup skips that mesbox outright and the
        -- chat.play list mismatches on it). The optional drink troll is
        -- declared a content gap rather than fought (see killTheTroll below),
        -- so no combat gear is needed at all.
        "::give coins 100", -- Captain Tobias's own return-trip fare (30gp) is a prerequisite too
        -- The player's own skiff, moored at the Pandemonium -- the same
        -- sailing setup e_full.lua and the conformance harness use.
        "::setvar sailing_boat_1_owned 1",
        "::setvar sailing_boat_1_type 1",
        "::setvar sailing_boat_1_port 1",
        "::setvar sailing_last_personal_boat_boarded 1",
        "::setvar sailing_boat_1_hotspot_6 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "quest_pry",
            constants = {
                not_started = 0,
                deliver = 5,
                let_steve = 10,
                get_key = 15,
                give_key = 20,
                test_key = 25,
                open_crate = 30,
                complete = 35,
            },
            row = "quest_pryingtimes",
            display = "Prying Times", -- configs/all.dbrow [quest_pryingtimes] displayname
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        local stage0_result, stage0_value = t.var.server("quest_pry")
        t.check("quest.stage.not_started", stage0_result == "ok" and stage0_value == 0,
            "quest_pry = " .. tostring(stage0_value) .. " (" .. tostring(stage0_result) .. "), want 0")

        -- NO goto_tile onto Steve's own tile here (measured run 3): the
        -- setup debugproc already stands the player at ^pry_bar_front_coord,
        -- OUTSIDE the bar counter, and [apnpc1,steve_beanie]'s own
        -- range-2 check is what lets talk_to reach him across it. A
        -- goto_tile to Steve's own (3050,2966) tile instead teleports the
        -- player BEHIND the counter -- talk_to still steps off onto the
        -- neighbouring 3049,2966, but that tile is enclosed too, so every
        -- later walk_to snaps back to it and every deliverCargo approach
        -- reads reach_failed. Trust the setup teleport; talk_to alone
        -- reaches Steve across the bar from it (e_full.lua's own proof).

        -- startQuest / getDeliveryTask: one continuous dialogue
        -- (Transcript:'Squawking'_Steve_Beanie's standard menu into
        -- Transcript:Prying_Times' "Starting off"), ending with
        -- [label,pry_steve_write_log] writing port task 600 into the log in
        -- the SAME script pass -- no second click between the quest start
        -- and the task being written (PryingTimes.java's own
        -- startQuest.addSubSteps(getDeliveryTask)).
        t.exec("startQuest", t.player.talk_to, "steve_beanie", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Yarr! What will it be?",
            "choose:Got any work that needs doing here?",
            "player:Got any work that needs doing here?",
            "npc:Well, it just so happens that I'm looking for a rough, tough, piratical type for an adventure on the high seas!",
            "npc:And I'm happy to say...",
            "npc:... it is you!",
            "mesbox:Before starting this miniquest, be aware that your combat level is lower than the recommended level of 10.",
            "choose:Yes.",
            "player:Great! So what do you need?",
            -- "Hoist your mast, <displayname>, ..." -- <displayname> is
            -- substituted server-side to the player's real name, so a
            -- literal match on the templated text never finds it; match a
            -- substring either side of the substitution instead.
            "npc:Hoist your mast,",
            "player:Looty? You mean treasure?",
            "npc:Yarr!",
            "player:Well, I can't say no to a bit of treasure. What do I need to do?",
            "npc:Shiver me timbers! There is a crate full of high value goods just sitting at the port of Sarim!",
            "player:Port Sarim? I thought you said far off land?",
            "npc:Aye! The far off land of Sarim!",
            "player:Okay... but surely if this is pirate treasure, someone would have noticed... Port Sarim is a busy place.",
            "npc:Luckily for us, a cunning Jolly Roger of a pirate has disguised it to look like a normal crate of cargo!",
            "player:Hang on... is this actually just a normal cargo delivery? You know there's a notice board on the docks for that sort of thing...",
            "npc:Normal?! Normal?! What kind of scallywag are you? There's nothing normal about a valuable crate of precious plundered pirate looty!",
            "npc:Besides, you've already said you'll do it, and pirates never go back on their word.",
            "player:I'm fairly certain they do, but fine. I'll go collect this crate for you.",
            "npc:Me hearty goes out to you! Once you have the looty, just leave it with the port master here. Now, give me your log and I'll write out the details.",
            "*",
            "npc:Now, get to it, sailor! Heave to and ballast your grog!",
        })
        local slots0_result, slots0, text0 = t.sail.tasks()
        t.check("getDeliveryTask.log", slots0_result == "ok" and slots0 and slots0[1] and slots0[1].id == 600,
            "sail.tasks() -> " .. tostring(slots0_result) .. " " .. tostring(text0))
        t.exec("accepted.mes", t.msg.expect, "Pandemonium pirate looty delivery")
        local stage1_result, stage1_value = t.var.server("quest_pry")
        t.check("quest.stage.deliver", stage1_result == "ok" and stage1_value == 5,
            "quest_pry = " .. tostring(stage1_value) .. " (" .. tostring(stage1_result) .. "), want 5")

        -- deliverCargo -- the real PortTaskStep, sailed in the player's own
        -- skiff (no teleport anywhere in this leg). Route measured
        -- parity1o step 4/6: Pandemonium 3073,2984 -> Port Sarim 3057,3189.
        t.player.walk_to(3068, 2987, 60)
        t.exec("deliverCargo.board", t.sail.board, "sailing_gangplank_the_pandemonium")
        t.exec("deliverCargo.helm", t.sail.helm, "Helm")
        t.exec("deliverCargo.sails", t.sail.sails, true)
        t.drive.camera(0, 383, 900)
        t.exec("deliverCargo.leg1", t.sail.sail_to, 3082, 2984, 2, 200)
        t.exec("deliverCargo.leg2", t.sail.sail_to, 3082, 3012, 2, 300)
        t.exec("deliverCargo.leg3", t.sail.sail_to, 3043, 3051, 3, 400)
        t.exec("deliverCargo.leg4", t.sail.sail_to, 3037, 3105, 3, 400)
        t.exec("deliverCargo.leg5", t.sail.sail_to, 3043, 3158, 3, 400)
        t.exec("deliverCargo.leg6", t.sail.sail_to, 3044, 3181, 3, 300)
        t.exec("deliverCargo.leg7", t.sail.sail_to, 3057, 3189, 2, 300)
        t.exec("deliverCargo.furl", t.sail.sails, false)
        local hull1_result, hull1_state = t.sail.state()
        local hull1_detail
        if hull1_result == "ok" then
            hull1_detail = string.format("aboard=%s hull=%s,%s arrivals=%s last=%s",
                tostring(hull1_state.aboard), tostring(hull1_state.hull_x), tostring(hull1_state.hull_z),
                tostring(hull1_state.arrivals), tostring(hull1_state.arrival_last))
        else
            hull1_detail = tostring(hull1_result) .. " " .. tostring(hull1_state)
        end
        t.check("deliverCargo.at_sarim", hull1_result == "ok", hull1_detail)
        t.exec("deliverCargo.disembark", t.sail.disembark, "sailing_gangplank_port_sarim")
        t.drive.camera(0, 128, 600)
        t.exec("deliverCargo.take", t.sail.cargo_take, "dock_loading_bay_ledger_table_port_sarim", 1)
        local carrying_result, carrying_value = t.var.server("sailing_carrying_cargo")
        t.check("deliverCargo.carrying", carrying_result == "ok" and carrying_value == 1,
            "sailing_carrying_cargo = " .. tostring(carrying_value) .. " (" .. tostring(carrying_result) .. "), want 1")

        -- Back to the Pandemonium with the looty in hand.
        t.exec("deliverCargo.board2", t.sail.board, "sailing_gangplank_port_sarim")
        t.exec("deliverCargo.helm2", t.sail.helm, "Helm")
        t.exec("deliverCargo.sails2", t.sail.sails, true)
        t.drive.camera(1024, 383, 900)
        t.exec("deliverCargo.back1", t.sail.sail_to, 3058, 3170, 3, 300)
        t.exec("deliverCargo.back2", t.sail.sail_to, 3043, 3158, 3, 300)
        t.exec("deliverCargo.back3", t.sail.sail_to, 3037, 3105, 3, 400)
        t.exec("deliverCargo.back4", t.sail.sail_to, 3043, 3051, 3, 400)
        t.exec("deliverCargo.back5", t.sail.sail_to, 3082, 3012, 3, 400)
        t.exec("deliverCargo.back6", t.sail.sail_to, 3082, 2986, 2, 300)
        t.exec("deliverCargo.back7", t.sail.sail_to, 3074, 2984, 1, 200)
        t.exec("deliverCargo.furl2", t.sail.sails, false)
        local hull2_result, hull2_state = t.sail.state()
        local hull2_detail
        if hull2_result == "ok" then
            hull2_detail = string.format("aboard=%s hull=%s,%s arrivals=%s last=%s",
                tostring(hull2_state.aboard), tostring(hull2_state.hull_x), tostring(hull2_state.hull_z),
                tostring(hull2_state.arrivals), tostring(hull2_state.arrival_last))
        else
            hull2_detail = tostring(hull2_result) .. " " .. tostring(hull2_state)
        end
        t.check("deliverCargo.at_pandemonium", hull2_result == "ok", hull2_detail)
        t.exec("deliverCargo.disembark2", t.sail.disembark, "sailing_gangplank_the_pandemonium")
        t.drive.camera(0, 128, 600)
        local task_xp_snapshot_result, task_xp_before = t.skill.snapshot()
        t.exec("deliverCargo", t.sail.cargo_deliver, "dock_loading_bay_ledger_table_pandemonium", 10)
        t.exec("deliverCargo-dialog", t.chat.play, {
            "*",
            "npc:Thanks, sailor. I'll get that all sorted out.",
        })
        if task_xp_snapshot_result == "ok" then
            t.exec("deliverCargo.xp", t.skill.expect_gain, "sailing", 180, task_xp_before)
        end

        local stage2_result, stage2_value = t.var.server("quest_pry")
        t.check("quest.stage.let_steve", stage2_result == "ok" and stage2_value == 10,
            "quest_pry = " .. tostring(stage2_value) .. " (" .. tostring(stage2_result) .. "), want 10")

        -- letSteveKnow.
        t.player.walk_to(3050, 2968, 40)
        t.exec("letSteveKnow", t.player.talk_to, "steve_beanie", 1)
        t.exec("letSteveKnow-dialog", t.chat.play, {
            "npc:Yarr! What will it be?",
            "choose:I delivered that cargo for you.",
            "player:I delivered that cargo for you.",
            "npc:Shiver me timbers! That was fast!",
            "player:So could I have some 'looty' now?",
            "npc:Blistering barnacles! You thought that was it? You are too funny, fellow sailor!",
            "player:I get the feeling you're about to ask me to do something else...",
            "npc:Aye! This is no ordinary crate,", -- <displayname> substituted -- see the note above
            "player:But it is an ordinary crate, Steve. There was even a label on it addressed to you.",
            "npc:I hope you're not trying to call Squawking Steve Beanie a liar?! That crate is pirate looty, and anything suggesting otherwise is clearly a plot by my rival pirate captains!",
            "player:Right... So what's this special key then?",
            "npc:It is something very few can forge! I suspect we will require the help of a master scallywag smith!",
            "player:Is it essential that they be a scallywag as well as a smith? If not, I know of a dwarf near Mudskipper Point who might be able to help us.",
            "npc:A dwarf? Yarr! That's an acceptable compromise! Ready your port side and go see this dwarf!",
            "player:Okay, I'll grab a redberry pie and go visit Thurgo...",
        })
        local stage3_result, stage3_value = t.var.server("quest_pry")
        t.check("quest.stage.get_key", stage3_result == "ok" and stage3_value == 15,
            "quest_pry = " .. tostring(stage3_value) .. " (" .. tostring(stage3_result) .. "), want 15")

        -- getKey -- Thurgo (server/scripts/areas/world/configs/m46_49.spawn).
        t.exec("goto-thurgo", t.player.goto_tile, 3001, 3144, 0)
        t.exec("getKey", t.player.talk_to, "thurgo", 1)
        t.exec("getKey-dialog", t.chat.play, {
            "choose:I need some help with a 'special key'.",
            "player:I need some help with a 'special key'.",
            "npc:A key? What for?",
            "player:Well, truth be told, I'm not sure it's really a key that I need. I just need something that can open a crate.",
            "npc:Ah, well a crowbar should do that. It's a simple tool.",
            "player:Can you help me make one?",
            "npc:I can indeed. We'll need a hammer, a steel bar, and of course, a redberry pie.",
            "choose:Yes.",
            "*",
            "npc:That should do nicely.",
            "player:Thanks! I should probably see what Steve makes of this.",
        })
        t.exec("getKey.crowbar", t.inv.await, "sailing_charting_crowbar", 1, 10)
        t.exec("getKey.pie_gone", t.inv.expect_absent, "redberry_pie")
        local stage4_result, stage4_value = t.var.server("quest_pry")
        t.check("quest.stage.give_key", stage4_result == "ok" and stage4_value == 20,
            "quest_pry = " .. tostring(stage4_value) .. " (" .. tostring(stage4_result) .. "), want 20")

        -- giveKey -- Captain Tobias's own post-Pandemonium ferry
        -- (areas/port_sarim/scripts/sailors.rs2 karamja_sailor_talk), the
        -- fare PryingTimes.java itself names for reaching Steve.
        t.exec("goto-tobias", t.player.goto_tile, 3029, 3214, 0)
        t.exec("tobias", t.player.talk_to, "captain_tobias", 1)
        t.exec("tobias-dialog", t.chat.play, {
            "npc:Hello there. Do you want to travel somewhere? We can take you to Musa Point on Karamja, or if you prefer, we can drop you off at the Pandemonium on the way. It only costs 30 coins.",
            "choose:Yes please.",
            "player:Yes please.",
            "npc:Where would you like to go?",
            "choose:I'd like to go to the Pandemonium.",
            "player:I'd like to go to the Pandemonium.",
            "mesbox:The ship arrives at the Pandemonium.",
        })
        local tobias_tile_result, tobias_tile = t.world.tile()
        t.check("tobias.arrived", tobias_tile_result == "ok" and tobias_tile.x == 3066 and tobias_tile.z == 2987,
            "world.tile() -> " .. tostring(tobias_tile_result) .. " " ..
                tostring(tobias_tile and tobias_tile.x) .. "," .. tostring(tobias_tile and tobias_tile.z))

        t.player.walk_to(3050, 2968, 40)
        t.exec("giveKey", t.player.talk_to, "steve_beanie", 1)
        t.exec("giveKey-dialog", t.chat.play, {
            "npc:Yarr!",
            "choose:I made that 'special key' you needed.",
            "player:I made that 'special key' you needed.",
            "*",
            "npc:Aha! There she crows! This is perfect!",
            "player:I'm glad you approve. Shall I use it to crack this crate open?",
            "*",
            "npc:Wait!",
            "*",
            "player:What's wrong?",
            "npc:An artifact of this power needs a test before we try it on our precious looty!",
            "player:...",
            "player:Fine. What should I test it on?",
            "npc:I noticed a crate of grog floating around by the north westmost island of the Pandemonium. Cast off your keel and sail your vessel over there.",
            "player:And then?",
            "npc:Well, you open it with your new key and sample the contents, of course.",
            "player:Why would I even want to do that? How do you even know it's drinkable?",
            "npc:I'm a true pirate, so I know good grog when I see it. As for why, no sailor has truly experienced the sea until they have drunk everything it has to offer.",
            "npc:In fact, you should keep a log of it all. I know I do!",
            "player:Okay... this doesn't seem very healthy, but fine.",
            "npc:Yarr! That's the spirit!",
        })
        local stage5_result, stage5_value = t.var.server("quest_pry")
        t.check("quest.stage.test_key", stage5_result == "ok" and stage5_value == 25,
            "quest_pry = " .. tostring(stage5_value) .. " (" .. tostring(stage5_result) .. "), want 25")

        t.exec("barDoor", t.player.click_loc, "pandemonium_door_reverse", 1)

        -- sailToCrate / testKey -- the real SailStep to the sea crate,
        -- pried open from the deck (aploc1, reached at approach distance).
        t.player.walk_to(3068, 2987, 60)
        t.exec("sailToCrate.board", t.sail.board, "sailing_gangplank_the_pandemonium")
        t.exec("sailToCrate.helm", t.sail.helm, "Helm")
        t.exec("sailToCrate.sails", t.sail.sails, true)
        t.drive.camera(0, 383, 900)
        t.exec("sailToCrate.leg1", t.sail.sail_to, 3082, 2984, 2, 200)
        t.exec("sailToCrate.leg2", t.sail.sail_to, 3082, 3012, 2, 300)
        t.exec("sailToCrate.leg3", t.sail.sail_to, 3040, 3012, 2, 300)
        t.exec("sailToCrate.leg4", t.sail.sail_to, 3013, 3008, 2, 300)
        t.exec("sailToCrate", t.sail.sail_to, 3013, 3005, 1, 200)
        t.exec("sailToCrate.furl", t.sail.sails, false)
        local hull3_result, hull3_state = t.sail.state()
        local hull3_detail
        if hull3_result == "ok" then
            hull3_detail = string.format("aboard=%s hull=%s,%s arrivals=%s last=%s",
                tostring(hull3_state.aboard), tostring(hull3_state.hull_x), tostring(hull3_state.hull_z),
                tostring(hull3_state.arrivals), tostring(hull3_state.arrival_last))
        else
            hull3_detail = tostring(hull3_result) .. " " .. tostring(hull3_state)
        end
        t.check("sailToCrate.hull", hull3_result == "ok", hull3_detail)
        t.exec("offHelm", t.sail._press_deck_row, "Navigate", "Helm")
        t.drive.camera(1024, 383, 1100)
        t.ticks(2)
        t.exec("testKey", t.sail._press_deck_row, "Pry-open", "Sealed crate", 16, 14)
        t.exec("testKey-dialog", t.chat.play, {
            "*",
            "player:This doesn't look like grog...",
        })
        t.exec("testKey.stout", t.inv.await, "sailing_charting_drink_crate_prying_times", 1, 10)

        -- drinkTheStout -- on deck only (the [opheld1] branch's own refusal
        -- ashore).
        t.exec("drinkTheStout", t.player.inv_op, "sailing_charting_drink_crate_prying_times", 1)
        t.exec("drinkTheStout-dialog", t.chat.play, {
            "mesbox:This drink has been sealed in a crate for an unknown amount of time. It could do anything to you, good or bad. Are you sure you want to drink it?",
            "choose:Yes.",
        })
        t.exec("drinkTheStout.charted", t.var.await_server, "sailing_charting_drink_crate_prying_times_complete", 1, 10)
        t.exec("drinkTheStout.mes", t.msg.expect, "Charting complete: Find a sealed crate near the Pandemonium")

        -- killTheTroll -- QuestHelper's own step is explicitly optional
        -- ("Kill the Drink Troll, or log out") and is not in loadSteps()'s
        -- stage map at all, only in the panel listing: nothing past this
        -- point depends on the troll's death -- confirmed from the .rs2,
        -- pry_steve_talk's test_key branch advances to open_crate on the
        -- drink alone (%sailing_charting_drink_crate_prying_times_complete
        -- = 1, already checked above). The "or log out" alternative is
        -- taken here rather than attempted live, for two independent
        -- reasons measured across runs 4-8 of this file, both content-side:
        --  1. [opnpc2,sailing_charting_drink_crate_prying_times_effect_troll]
        --     (pryingtimes_locs.rs2:172-173) is `~npc_retaliate(0);` alone
        --     with no `@player_combat_start` jump after it --
        --     QUEST_AUTHORING.md trap 31's exact shape (an opnpc2 binding
        --     that replaces the engine's own combat wildcard and never
        --     re-enters it) -- CONFIRMED live (run 4): a real Attack press
        --     lands ("ok Attack ..." six re-engagements over 60 ticks) but
        --     the troll's own health bar never appears; no player Attack
        --     can ever land a hit on this npc.
        --  2. Its own `npc_add(coord, ..., 200)` spawn (the opheld1 drink
        --     branch) was unreliable in the client's pool on this run's own
        --     machine independent of (1): run 7 saw `npc.nearest` answer
        --     `no_row` right after drinking, and run 8's `npc.await_present`
        --     TIMED OUT at 10 ticks -- so even locating it to press cannot
        --     be made deterministic without a real hit ever landing to make
        --     the wait meaningful.
        -- GUIDE-GAP: killTheTroll pryingtimes_locs.rs2:172 -- opnpc2 is `~npc_retaliate(0);` alone with no @player_combat_start jump, so no Attack can ever land a hit (trap 31's exact shape); the guide's own "or log out" alternative is taken

        -- Back to the Pandemonium (the guide's "or log out" alternative taken above).
        t.drive.camera(0, 128, 600)
        t.exec("sailBack.helm", t.sail.helm, "Helm")
        t.drive.camera(0, 383, 900)
        t.exec("sailBack.turn", t.sail._press_heading, 8)
        t.ticks(8)
        local hull4_result, hull4_state = t.sail.state()
        local hull4_detail
        if hull4_result == "ok" then
            hull4_detail = string.format("aboard=%s hull=%s,%s arrivals=%s last=%s",
                tostring(hull4_state.aboard), tostring(hull4_state.hull_x), tostring(hull4_state.hull_z),
                tostring(hull4_state.arrivals), tostring(hull4_state.arrival_last))
        else
            hull4_detail = tostring(hull4_result) .. " " .. tostring(hull4_state)
        end
        t.check("sailBack.turned", hull4_result == "ok", hull4_detail)
        t.exec("sailBack.sails", t.sail.sails, true)
        t.exec("sailBack.leg1", t.sail.sail_to, 3012, 3016, 2, 300)
        t.exec("sailBack.leg2", t.sail.sail_to, 3082, 3016, 2, 400)
        t.drive.camera(1024, 383, 900)
        t.exec("sailBack.leg3", t.sail.sail_to, 3082, 2986, 2, 300)
        t.exec("sailBack.leg4", t.sail.sail_to, 3074, 2984, 1, 200)
        t.exec("sailBack.furl", t.sail.sails, false)
        t.exec("sailBack.disembark", t.sail.disembark, "sailing_gangplank_the_pandemonium")
        t.drive.camera(0, 128, 600)

        -- goToSteve.
        t.player.walk_to(3050, 2968, 40)
        t.exec("goToSteve", t.player.talk_to, "steve_beanie", 1)
        t.exec("goToSteve-dialog", t.chat.play, {
            "npc:Yarr!",
            "choose:About that crate...",
            "player:About that crate...",
            "npc:Have you sampled that grog yet, sailor?",
            "player:I have. Did you know that troll was going to jump out and attack me? I had the fright of my life!",
            "npc:Ah, blistering barnacles! I did forget to mention that does sometimes happen.",
            "player:Of course it does. Right, no more wasting time. I'm assuming that's the crate next to you? I'm opening it.",
            "npc:Of course! You are the captain after all, my first mate!",
        })
        local stage6_result, stage6_value = t.var.server("quest_pry")
        t.check("quest.stage.open_crate", stage6_result == "ok" and stage6_value == 30,
            "quest_pry = " .. tostring(stage6_value) .. " (" .. tostring(stage6_result) .. "), want 30")

        -- openCrate -- reward snapshot BEFORE the click: pry_open_bar_crate
        -- grants the stat/item rewards in the same script pass as the
        -- click, before any of its own dialogue draws (trap 24).
        t.exec("barDoor2", t.player.click_loc, "pandemonium_door_reverse", 1)
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))

        t.exec("openCrate", t.player.click_loc, "pry_crate_sealed", 1)
        t.exec("openCrate-dialog", t.chat.play, {
            "*",
            "*",
            "npc:Your looty, sailor! Take as many as you like.",
            "*",
        })
        t.exec("openCrate.after", t.chat.play, {
            "npc:Now, with your new looty in hand, you're ready to take on the seas like never before! Soon you'll be as great a sailor as me! Speaking of which, you're not still using that raft, are you?",
            "player:Why do you ask?",
            "npc:Well, I think it's about time you sort yourself out with an upgrade! You should check in with Jim. I'm sure he'll have something fitting. Now, avast!",
        })
        -- pry_open_bar_crate's own stage/reward writes still land one
        -- server tick behind the click's settle (trap 24) -- settle before
        -- reading any of it.
        t.ticks(3)

        local stage7_result, stage7_value = t.var.server("quest_pry")
        t.check("quest.stage.complete", stage7_result == "ok" and stage7_value == 35,
            "quest_pry = " .. tostring(stage7_value) .. " (" .. tostring(stage7_result) .. "), want 35")

        -- t.quest.expect_complete() -- writes quest.varp_complete,
        -- quest.scroll_title, quest.points, quest.journal and photographs
        -- the completion scroll.
        local expect_complete_result, expect_complete_detail = t.quest.expect_complete()
        t.step("quest.expect_complete", expect_complete_result == "ok" and "PASS" or "FAIL",
            tostring(expect_complete_result) .. " " .. tostring(expect_complete_detail))

        -- Rewards: [proc,pry_quest_complete] (pryingtimes.rs2) and
        -- PryingTimes.java's own getExperienceRewards/getItemRewards --
        -- 1000 Smithing XP, 800 Sailing XP, 25 oak sawmill coupons, the
        -- crowbar (already held from Thurgo; the proc's own
        -- `inv_freespace(inv) > 0` re-grant is skipped when one is already
        -- carried).
        t.check("reward.smithing", t.skill.expect_gain("smithing", 1000, reward_before))
        t.check("reward.sailing", t.skill.expect_gain("sailing", 800, reward_before))
        t.check("reward.coupons", t.inv.expect_has("sawmill_coupon_oak", 25))
        t.check("reward.crowbar", t.inv.expect_has("sailing_charting_crowbar", 1))

        t.finish(0)
        return
    end,
}
