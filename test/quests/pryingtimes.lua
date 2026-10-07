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
-- proof's own route/camera/press choices; the departures from it are
-- QUEST_AUTHORING.md's own rules the scratch proof was not bound by:
--   1. The drink troll (`killTheTroll`, Quest Helper's own optional NpcStep
--      -- "Kill the Drink Troll, or log out", not in loadSteps()'s stage
--      map at all) IS fought here (see the comment beside killTheTroll
--      below): content parity1p (OSRS-Content
--      3b413493347fd8354462be678f06dc431c4091ed, landed the same day as
--      this file, before its own seam18 pass) fixed the `[opnpc2,...]`
--      binding an earlier attempt at this file found dead
--      (`~npc_retaliate(0);` alone, trap 31's exact shape, confirmed live
--      in that attempt's runs 4-8) -- it now reads `~npc_retaliate(0);
--      @player_combat_start;`, so a real Attack lands real damage and the
--      "or log out" alternative is no longer the only option. Killing the
--      troll still writes no varp and gates no stage
--      (`[label,pry_steve_talk]`'s 25 branch advances to `open_crate` on
--      the DIALOGUE alone, drinking's own
--      `%sailing_charting_drink_crate_prying_times_complete = 1`, checked
--      above regardless), so the fight is purely the guide's own optional
--      step, driven for its own sake.
--   2. Reward rows: `skill.snapshot()` immediately before the hand-in click
--      (`pry_open_bar_crate` grants the stat/item rewards in the SAME
--      script pass as the click, before any of its own dialogue is even
--      drawn -- trap 24), then `skill.expect_gain`/`inv.expect_has` rows
--      after `quest.expect_complete()` for every reward
--      `[proc,pry_quest_complete]`/PryingTimes.java's own reward lists
--      name: 1000 Smithing XP (^pry_smith_xp = 10000 tenths), 800 Sailing
--      XP (^pry_sailing_xp = 8000 tenths), 25 oak sawmill coupons
--      (^pry_coupon_count), the crowbar itself.
--
-- RE-DRIVEN 2026-10-05 for the door rule (batch matthew-mbp-m4-b65). The
-- Pandemonium is an island (comp.py from the bar front: a 462-tile
-- component whose edge is only the bar's own doors; reach.py from any
-- mainland tile UNREACHABLE at margins 30/80/160), so nothing may goto onto
-- or off it:
--   * the start: `::pryingtimes` ends in a p_teleport to the bar front, so
--     the setup ends with `::goto 3027 3217 0` instead -- an open tile on
--     Port Sarim's docks beside Captain Tobias (3028,3216; reach.py from the
--     fixture's 3206,3233: REACH closed-doors len 311) -- and the run takes
--     the ferry the guide names ("You can travel there via Captain Tobias
--     on Port Sarim docks for 30gp"), graded on the 30 coins and the
--     telejump's landing 3066,2987;
--   * Thurgo: the player's own skiff to Port Sarim and back (the courier
--     leg's own route, one helper per direction), with an overland goto
--     between Port Sarim's dock (ashore 3050,3192) and Thurgo (REACH
--     closed-doors len 135 both ways). The old goto-tobias landed ON a
--     sarim_barrel (3029,3214, solid) and the old goto-thurgo hopped off
--     the island;
--   * Steve's crate stands behind the bar's own door
--     (pandemonium_door_reverse 3047,2967, raw level 1 of the bridge deck):
--     crossed by pass_door, never a bare click;
--   * the drink troll's combat levels are staged in SETUP (no mid-run
--     ::setlevel), so `[label,pry_steve_start]`'s low-combat-level warning
--     mesbox no longer shows and is no longer in startQuest-dialog.

return {
    id = "pryingtimes",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000, -- four skiff crossings (two Port Sarim round trips) and the sea-crate trip
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::pryingtimes", -- resets %quest_pry, grants sailing_log/steel_bar/redberry_pie/hammer,
                          -- tops smithing/sailing if under the quest's own requirement,
                          -- teleports to ^pry_bar_front_coord (overridden by the ::goto below)
        "::setlevel smithing 30", -- belt-and-braces: ~pry_can_start gates on stat_base(smithing) >= 30
        "::setlevel sailing 20", -- and stat_base(sailing) >= 12 -- both real base levels, not xp alone
        -- The optional drink troll (wiki oldid 15200619: combat 14, 9/9/9
        -- atk/str/def, 25 hp, max hit 2) is fought unarmed -- no weapon is
        -- in the guide's item requirements. Steve's own warning names combat
        -- level 10 as the recommendation; 20s give a combat level past it
        -- and a fight with margin, with lobsters eaten inside the presses.
        "::setlevel attack 20",
        "::setlevel strength 20",
        "::setlevel defence 20",
        "::setlevel hitpoints 20",
        "::give lobster 4",
        "::give coins 100", -- Captain Tobias's fare to the Pandemonium (30gp)
        -- The player's own skiff, moored at the Pandemonium -- the same
        -- sailing setup e_full.lua and the conformance harness use.
        "::setvar varb19258_sailing_boat_1_owned 1",
        "::setvar varb19259_sailing_boat_1_type 1",
        "::setvar varb19260_sailing_boat_1_port 1",
        "::setvar varb18554_sailing_last_personal_boat_boarded 1",
        "::setvar varb19279_sailing_boat_1_hotspot_6 1",
        -- The start: an open tile on Port Sarim's docks beside Captain
        -- Tobias (m47_50.spawn 3028,3216), off the Pandemonium island the
        -- debugproc teleported to.
        "::goto 3027 3217 0",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb18317_quest_pry",
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

        local stage0_result, stage0_value = t.var.server("varb18317_quest_pry")
        t.check("quest.stage.not_started", stage0_result == "ok" and stage0_value == 0,
            "quest_pry = " .. tostring(stage0_value) .. " (" .. tostring(stage0_result) .. "), want 0")

        -- A hull reading graded on where the hull is, not on the read's status.
        local function hull_row(name, want_x, want_z, radius)
            local state_result, state = t.sail.state()
            local pass = false
            local detail
            if state_result == "ok" and type(state) == "table" then
                pass = state.aboard == true and state.hull_x ~= nil and state.hull_z ~= nil
                    and math.abs(state.hull_x - want_x) <= radius
                    and math.abs(state.hull_z - want_z) <= radius
                detail = string.format("aboard=%s hull=%s,%s heading=%s arrivals=%s last=%s (want aboard, hull within %d of %d,%d)",
                    tostring(state.aboard), tostring(state.hull_x), tostring(state.hull_z),
                    tostring(state.heading), tostring(state.arrivals), tostring(state.arrival_last),
                    radius, want_x, want_z)
            else
                detail = tostring(state_result) .. " " .. tostring(state)
            end
            t.check(name, pass, detail)
        end

        -- The player's own skiff, the Pandemonium -> Port Sarim (route
        -- measured parity1o step 4/6: 3073,2984 -> 3057,3189). Every row
        -- is named `<prefix>.<part>`.
        local function sail_pandemonium_to_sarim(prefix)
            t.exec(prefix .. ".toPlank", t.player.walk_to, 3068, 2987, 60)
            t.exec(prefix .. ".board", t.sail.board, "sailing_gangplank_the_pandemonium")
            t.exec(prefix .. ".helm", t.sail.helm, "Helm")
            t.exec(prefix .. ".sails", t.sail.sails, true)
            t.drive.camera(0, 383, 900)
            t.exec(prefix .. ".leg1", t.sail.sail_to, 3082, 2984, 2, 200)
            t.exec(prefix .. ".leg2", t.sail.sail_to, 3082, 3012, 2, 300)
            t.exec(prefix .. ".leg3", t.sail.sail_to, 3043, 3051, 3, 400)
            t.exec(prefix .. ".leg4", t.sail.sail_to, 3037, 3105, 3, 400)
            t.exec(prefix .. ".leg5", t.sail.sail_to, 3043, 3158, 3, 400)
            t.exec(prefix .. ".leg6", t.sail.sail_to, 3044, 3181, 3, 300)
            -- East first, then up to the berth: the straight line 3044,3181
            -- -> 3057,3189 passes 1.4 tiles from the pier post at
            -- 3048..3049,3186..3187 (OSRS-Content maps m47_49 jl2), which
            -- the old sail_to only cleared because it could not press a
            -- heading near the bow and so held north (b69 driver fix).
            t.exec(prefix .. ".leg7a", t.sail.sail_to, 3052, 3181, 2, 200)
            t.exec(prefix .. ".leg7", t.sail.sail_to, 3057, 3189, 2, 300)
            t.exec(prefix .. ".furl", t.sail.sails, false)
            hull_row(prefix .. ".at_sarim", 3057, 3189, 4)
            t.exec(prefix .. ".disembark", t.sail.disembark, "sailing_gangplank_port_sarim")
            t.drive.camera(0, 128, 600)
        end

        -- And back: Port Sarim -> the Pandemonium's gangplank (3070,2987).
        local function sail_sarim_to_pandemonium(prefix, suffix)
            t.exec(prefix .. ".board" .. suffix, t.sail.board, "sailing_gangplank_port_sarim")
            t.exec(prefix .. ".helm" .. suffix, t.sail.helm, "Helm")
            t.exec(prefix .. ".sails" .. suffix, t.sail.sails, true)
            t.drive.camera(1024, 383, 900)
            t.exec(prefix .. ".back1", t.sail.sail_to, 3058, 3170, 3, 300)
            t.exec(prefix .. ".back2", t.sail.sail_to, 3043, 3158, 3, 300)
            t.exec(prefix .. ".back3", t.sail.sail_to, 3037, 3105, 3, 400)
            t.exec(prefix .. ".back4", t.sail.sail_to, 3043, 3051, 3, 400)
            t.exec(prefix .. ".back5", t.sail.sail_to, 3082, 3012, 3, 400)
            t.exec(prefix .. ".back6", t.sail.sail_to, 3082, 2986, 2, 300)
            t.exec(prefix .. ".back7", t.sail.sail_to, 3074, 2984, 1, 200)
            t.exec(prefix .. ".furl" .. suffix, t.sail.sails, false)
            hull_row(prefix .. ".at_pandemonium", 3074, 2984, 4)
            t.exec(prefix .. ".disembark" .. suffix, t.sail.disembark, "sailing_gangplank_the_pandemonium")
            t.drive.camera(0, 128, 600)
        end

        -- To the Pandemonium: Captain Tobias's post-Pandemonium ferry
        -- (areas/port_sarim/scripts/sailors.rs2 [label,pandemonium_sailor_pay]:
        -- 30 coins, p_telejump to 3066,2987 beside the gangplank), the route
        -- startQuest's own guide text names.
        local coins0_result, coins0 = t.inv.count("coins")
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
        t.check("tobias.arrived", tobias_tile_result == "ok" and type(tobias_tile) == "table"
            and tobias_tile.x == 3066 and tobias_tile.z == 2987 and tobias_tile.level == 0,
            "world.tile() -> " .. tostring(tobias_tile_result) .. " " ..
                tostring(type(tobias_tile) == "table" and tobias_tile.x) .. "," ..
                tostring(type(tobias_tile) == "table" and tobias_tile.z) .. " (want 3066,2987,0)")
        local coins1_result, coins1 = t.inv.count("coins")
        t.check("tobias.fare", coins0_result == "ok" and coins1_result == "ok" and coins0 - coins1 == 30,
            "coins " .. tostring(coins0) .. " -> " .. tostring(coins1) .. " (want the 30gp fare)")

        -- startQuest / getDeliveryTask: one continuous dialogue
        -- (Transcript:'Squawking'_Steve_Beanie's standard menu into
        -- Transcript:Prying_Times' "Starting off"), ending with
        -- [label,pry_steve_write_log] writing port task 600 into the log in
        -- the SAME script pass -- no second click between the quest start
        -- and the task being written (PryingTimes.java's own
        -- startQuest.addSubSteps(getDeliveryTask)). Steve stands behind the
        -- bar counter (3050,2966); [apnpc1,steve_beanie] reaches him across
        -- it from the bar front 3050,2968, on the open deck (reach.py from
        -- the ferry's landing: REACH closed-doors len 35).
        t.exec("walk-steve", t.player.walk_to, 3050, 2968, 40)
        t.exec("startQuest", t.player.talk_to, "steve_beanie", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Yarr! What will it be?",
            "choose:Got any work that needs doing here?",
            "player:Got any work that needs doing here?",
            "npc:Well, it just so happens that I'm looking for a rough, tough, piratical type for an adventure on the high seas!",
            "npc:And I'm happy to say...",
            "npc:... it is you!",
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
        t.check("getDeliveryTask.log", slots0_result == "ok" and type(slots0) == "table"
            and slots0[1] ~= nil and slots0[1].id == 600,
            "sail.tasks() -> " .. tostring(slots0_result) .. " " .. tostring(text0))
        t.exec("accepted.mes", t.msg.expect, "Pandemonium pirate looty delivery")
        local stage1_result, stage1_value = t.var.server("varb18317_quest_pry")
        t.check("quest.stage.deliver", stage1_result == "ok" and stage1_value == 5,
            "quest_pry = " .. tostring(stage1_value) .. " (" .. tostring(stage1_result) .. "), want 5")

        -- deliverCargo -- the real PortTaskStep, sailed in the player's own
        -- skiff (no teleport anywhere in this leg).
        sail_pandemonium_to_sarim("deliverCargo")
        t.exec("deliverCargo.take", t.sail.cargo_take, "dock_loading_bay_ledger_table_port_sarim", 1)
        local carrying_result, carrying_value = t.var.server("varb19134_sailing_carrying_cargo")
        t.check("deliverCargo.carrying", carrying_result == "ok" and carrying_value == 1,
            "sailing_carrying_cargo = " .. tostring(carrying_value) .. " (" .. tostring(carrying_result) .. "), want 1")

        -- Back to the Pandemonium with the looty in hand.
        sail_sarim_to_pandemonium("deliverCargo", "2")
        local task_xp_snapshot_result, task_xp_before = t.skill.snapshot()
        t.exec("deliverCargo", t.sail.cargo_deliver, "dock_loading_bay_ledger_table_pandemonium", 10)
        t.exec("deliverCargo-dialog", t.chat.play, {
            "*",
            "npc:Thanks, sailor. I'll get that all sorted out.",
        })
        t.check("deliverCargo.xp", task_xp_snapshot_result == "ok"
                and t.skill.expect_gain("sailing", 180, task_xp_before) == "ok",
            "port task 600's 180 Sailing XP (snapshot " .. tostring(task_xp_snapshot_result) .. ")")

        local stage2_result, stage2_value = t.var.server("varb18317_quest_pry")
        t.check("quest.stage.let_steve", stage2_result == "ok" and stage2_value == 10,
            "quest_pry = " .. tostring(stage2_value) .. " (" .. tostring(stage2_result) .. "), want 10")

        -- letSteveKnow.
        t.exec("walk-steve2", t.player.walk_to, 3050, 2968, 40)
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
        local stage3_result, stage3_value = t.var.server("varb18317_quest_pry")
        t.check("quest.stage.get_key", stage3_result == "ok" and stage3_value == 15,
            "quest_pry = " .. tostring(stage3_value) .. " (" .. tostring(stage3_result) .. "), want 15")

        -- getKey -- Thurgo (server/scripts/areas/world/configs/m46_49.spawn
        -- 3001,3144). Off the island in the player's own skiff, ashore on
        -- Port Sarim's dock (3050,3192), then overland: reach.py
        -- 3050,3192 -> 3001,3144 REACH closed-doors len 135 (margins
        -- 30/80/160), and back 134.
        sail_pandemonium_to_sarim("toThurgo")
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
        t.exec("getKey.bar_gone", t.inv.expect_absent, "steel_bar")
        local stage4_result, stage4_value = t.var.server("varb18317_quest_pry")
        t.check("quest.stage.give_key", stage4_result == "ok" and stage4_value == 20,
            "quest_pry = " .. tostring(stage4_value) .. " (" .. tostring(stage4_result) .. "), want 20")

        -- giveKey -- back to Port Sarim's dock over land, the skiff moored
        -- there, and back to the Pandemonium in it.
        t.exec("goto-sarim-dock", t.player.goto_tile, 3050, 3192, 0)
        sail_sarim_to_pandemonium("fromThurgo", "")
        t.exec("walk-steve3", t.player.walk_to, 3050, 2968, 40)
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
        local stage5_result, stage5_value = t.var.server("varb18317_quest_pry")
        t.check("quest.stage.test_key", stage5_result == "ok" and stage5_value == 25,
            "quest_pry = " .. tostring(stage5_value) .. " (" .. tostring(stage5_result) .. "), want 25")

        -- sailToCrate / testKey -- the real SailStep to the sea crate,
        -- pried open from the deck (aploc1, reached at approach distance).
        t.exec("sailToCrate.toPlank", t.player.walk_to, 3068, 2987, 60)
        t.exec("sailToCrate.board", t.sail.board, "sailing_gangplank_the_pandemonium")
        t.exec("sailToCrate.helm", t.sail.helm, "Helm")
        t.exec("sailToCrate.sails", t.sail.sails, true)
        t.drive.camera(0, 383, 900)
        t.exec("sailToCrate.leg1", t.sail.sail_to, 3082, 2984, 2, 200)
        t.exec("sailToCrate.leg2", t.sail.sail_to, 3082, 3012, 2, 300)
        t.exec("sailToCrate.leg3", t.sail.sail_to, 3040, 3012, 2, 300)
        t.exec("sailToCrate.leg4", t.sail.sail_to, 3013, 3008, 2, 300)
        -- One tile further south than 3013,3005 r1: the sea crate stands at
        -- 3013,2998 (pryingtimes.constant ^pry_sea_crate_coord), and the
        -- deck hunt below finds its Pry-open row only from within ~7 tiles;
        -- r1 of 3013,3005 stopped the hull at 3014,3006, eight off, once
        -- sail_to kept to its leg's line (b69 driver fix).
        t.exec("sailToCrate", t.sail.sail_to, 3013, 3004, 1, 200)
        t.exec("sailToCrate.furl", t.sail.sails, false)
        hull_row("sailToCrate.hull", 3013, 3005, 3)
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
        t.exec("drinkTheStout.charted", t.var.await_server, "varb18585_sailing_charting_drink_crate_prying_times_complete", 1, 10)
        t.exec("drinkTheStout.mes", t.msg.expect, "Charting complete: Find a sealed crate near the Pandemonium")
        t.exec("drinkTheStout.gone", t.inv.expect_absent, "sailing_charting_drink_crate_prying_times")

        -- killTheTroll -- QuestHelper's own optional NpcStep ("Kill the
        -- Drink Troll, or log out"), not in loadSteps()'s stage map at all
        -- (nothing past this point depends on the troll's death --
        -- pry_steve_talk's test_key branch already advanced to open_crate
        -- on the drink alone). Content parity1p fixed the `[opnpc2,...]`
        -- binding (pryingtimes_locs.rs2:199-201 `~npc_retaliate(0);
        -- @player_combat_start;`), so a real Attack lands real damage. The
        -- troll's stats are the cache record's (configs/all.npc stat1-4 =
        -- 9/9/9/25, pryingtimes_locs.rs2:185-189). Fought unarmed with the
        -- setup's 20s; lobsters eaten inside the presses below 10 hp.
        local troll = "sailing_charting_drink_crate_prying_times_effect_troll"
        local troll_eat = { eat = { item = "lobster", below = 10 } }
        -- t.npc.await_present is hollow (bare ok, trap 12) -- call it
        -- directly and write the read-back ourselves, not through t.exec.
        local present_result = t.npc.await_present(troll, 30, 15)
        t.check("killTheTroll.present", present_result == "ok",
            "npc.await_present(radius 30, seam18's own measured aboard radius) -> "
                .. tostring(present_result))
        local food_result0, food_before = t.inv.count("lobster")
        local _, troll_attack_detail = t.exec("killTheTroll.attack", t.player.attack, troll, 2, 20, troll_eat)
        local _, troll_dead_detail = t.exec("killTheTroll", t.npc.await_dead_engaged, 240, 10, troll_eat)
        do
            local low_attack = tonumber(tostring(troll_attack_detail):match("lowest hp (%d+)/"))
            local low_dead = tonumber(tostring(troll_dead_detail):match("lowest hp (%d+)/"))
            local lowest = low_dead
            if low_attack ~= nil and (lowest == nil or low_attack < lowest) then
                lowest = low_attack
            end
            local _, hitpoints = t.skill.read("hitpoints")
            local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
            local food_result, food_left = t.inv.count("lobster")
            t.check("killTheTroll.margin", lowest ~= nil and max_hp ~= nil and food_result == "ok"
                    and lowest * 4 >= max_hp and food_left >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. " (attack press "
                    .. tostring(low_attack) .. ", kill wait " .. tostring(low_dead) .. "), lobsters "
                    .. tostring(food_before) .. " (" .. tostring(food_result0) .. ") -> " .. tostring(food_left)
                    .. " (margin: lowest hp >= a quarter of max AND at least one lobster left)")
        end

        -- The drop is a FLOOR OBJ ON THE DECK (seam18's DECK FLOOR OBJS
        -- fact): the server already sends it inside the deck sandwich, and
        -- the client paints/picks it through the deck's own minimenu, never
        -- t.world.obj_near/t.player.click_obj (both read only the root
        -- pool). One of ten drinks at random (wiki drop table, 1/10 each,
        -- pryingtimes_locs.rs2's own ai_queue3) -- press whichever landed.
        local troll_drink_names = {
            "Asgarnian ale", "Beer", "Brandy", "Dwarven stout", "Gin",
            "Kebab", "Rum", "Vodka", "Whisky", "Wizard's mind bomb",
        }
        local troll_drink_symbols = {
            "asgarnian_ale", "beer", "brandy", "dwarven_stout", "gin",
            "kebab", "rum", "vodka", "whisky", "wizards_mind_bomb",
        }
        t.drive.camera(1024, 383, 1100)
        t.ticks(2)
        t.exec("killTheTroll.take", t.sail._press_deck_row,
            troll_drink_names, "Take", 8, 16)
        -- The click's own `ok` is the server's sentence, not the container
        -- update (trap 24) -- `_press_deck_row` is a raw minimenu click with
        -- no built-in settle (unlike `t.player.click_obj`), so the pickup
        -- needs its own tick before the backpack read below.
        t.ticks(2)

        local troll_drink_found, troll_drink_count = nil, 0
        for troll_drink_i = 1, #troll_drink_symbols do
            local count_result, count_value = t.inv.count(troll_drink_symbols[troll_drink_i])
            if count_result == "ok" and count_value and count_value > 0 then
                troll_drink_found = troll_drink_symbols[troll_drink_i]
                troll_drink_count = count_value
            end
        end
        t.check("killTheTroll.drop", troll_drink_found ~= nil,
            "backpack holds " .. tostring(troll_drink_count) .. " " .. tostring(troll_drink_found)
                .. " (one of the troll's ten-way drop table, oldid 15200619)")

        -- Back to the Pandemonium.
        t.drive.camera(0, 128, 600)
        t.exec("sailBack.helm", t.sail.helm, "Helm")
        t.drive.camera(0, 383, 900)
        t.exec("sailBack.turn", t.sail._press_heading, 8)
        t.ticks(8)
        do
            local turn_result, turn_state = t.sail.state()
            t.check("sailBack.turned", turn_result == "ok" and type(turn_state) == "table"
                    and turn_state.aboard == true and turn_state.heading == 8,
                "aboard=" .. tostring(type(turn_state) == "table" and turn_state.aboard)
                    .. " heading=" .. tostring(type(turn_state) == "table" and turn_state.heading)
                    .. " hull=" .. tostring(type(turn_state) == "table" and turn_state.hull_x) .. ","
                    .. tostring(type(turn_state) == "table" and turn_state.hull_z)
                    .. " (" .. tostring(turn_result) .. "; want aboard, heading 8)")
        end
        t.exec("sailBack.sails", t.sail.sails, true)
        t.exec("sailBack.leg1", t.sail.sail_to, 3012, 3016, 2, 300)
        t.exec("sailBack.leg2", t.sail.sail_to, 3082, 3016, 2, 400)
        t.drive.camera(1024, 383, 900)
        t.exec("sailBack.leg3", t.sail.sail_to, 3082, 2986, 2, 300)
        t.exec("sailBack.leg4", t.sail.sail_to, 3074, 2984, 1, 200)
        t.exec("sailBack.furl", t.sail.sails, false)
        hull_row("sailBack.at_pandemonium", 3074, 2984, 4)
        t.exec("sailBack.disembark", t.sail.disembark, "sailing_gangplank_the_pandemonium")
        t.drive.camera(0, 128, 600)

        -- goToSteve.
        t.exec("walk-steve4", t.player.walk_to, 3050, 2968, 40)
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
        local stage6_result, stage6_value = t.var.server("varb18317_quest_pry")
        t.check("quest.stage.open_crate", stage6_result == "ok" and stage6_value == 30,
            "quest_pry = " .. tostring(stage6_value) .. " (" .. tostring(stage6_result) .. "), want 30")

        -- openCrate -- Steve's crate (pry_crate_multi 3048,2965) stands
        -- behind the bar's own door, pandemonium_door_reverse at 3047,2967
        -- (a north-edge wall leaf, stored on raw level 1 of the bridge deck
        -- the pub stands on; comp.py: the space behind it is 26 tiles whose
        -- only edge is that door). In through the door by pass_door.
        t.exec("barDoor", t.player.pass_door, { closed = "pandemonium_door_reverse",
            open = "pandemonium_door_reverse_open", at = { 3047, 2967, 0 }, loc_level = 1,
            near = { 3047, 2968 }, far = { 3047, 2966 } })
        -- Reward snapshot BEFORE the click: pry_open_bar_crate grants the
        -- stat/item rewards in the same script pass as the click, before
        -- any of its own dialogue draws (trap 24).
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local coupons_before_result, coupons_before = t.inv.count("sawmill_coupon_oak")
        local crowbars_before_result, crowbars_before = t.inv.count("sailing_charting_crowbar")

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

        local stage7_result, stage7_value = t.var.server("varb18317_quest_pry")
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
        -- 1000 Smithing XP, 800 Sailing XP, 25 oak sawmill coupons, and
        -- one crowbar from the crate ([proc,pry_open_bar_crate]
        -- pryingtimes_locs.rs2:250-253 "Steve hands you a crowbar." whenever
        -- the pack has a free slot, on top of Thurgo's).
        t.check("reward.smithing", t.skill.expect_gain("smithing", 1000, reward_before))
        t.check("reward.sailing", t.skill.expect_gain("sailing", 800, reward_before))
        local coupons_after_result, coupons_after = t.inv.count("sawmill_coupon_oak")
        t.check("reward.coupons", coupons_before_result == "ok" and coupons_after_result == "ok"
                and coupons_after - coupons_before == 25,
            "sawmill_coupon_oak " .. tostring(coupons_before) .. " -> " .. tostring(coupons_after) .. " (want +25)")
        local crowbars_after_result, crowbars_after = t.inv.count("sailing_charting_crowbar")
        t.check("reward.crowbar", crowbars_before_result == "ok" and crowbars_after_result == "ok"
                and crowbars_after - crowbars_before == 1,
            "sailing_charting_crowbar " .. tostring(crowbars_before) .. " -> " .. tostring(crowbars_after)
                .. " (want +1: Steve hands you a crowbar)")

        t.finish(0)
        return
    end,
}
