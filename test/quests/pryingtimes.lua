-- Prying Times, driven through Steve Beanie / Thurgo / the two crates --
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_pryingtimes/.
--
-- Read against the .rs2 directly (pryingtimes.rs2, pryingtimes_locs.rs2),
-- not the Quest Helper scaffold's guesses: the scaffold split the second
-- Steve Beanie conversation into three separate talk_to steps
-- (getDeliveryTask/letSteveKnow), but the .rs2's `pry_steve_talk` label
-- chains the pry_deliver -> pry_let_steve transition inside ONE dialogue
-- via a self `@pry_steve_talk;` recursion -- no second click happens
-- between those two stages, so this file drives it as one t.exec.
--
-- `::pryingtimes` (the quest's own debugproc) resets %quest_pry, grants the
-- steel bar / redberry pie / hammer / sailing log and teleports the player
-- to pry_steve_coord (3050,2966,0) -- but its own
-- `stat_advance(smithing, 15000)` is only 1500 ACTUAL xp (this VM's
-- stat_advance takes tenths -- pryingtimes.constant's own comment on
-- ^pry_smith_xp says as much), nowhere near enough to reach the base-30
-- ^pry_smith_req the quest itself gates on ([proc,pry_can_start]) from a
-- level-1 character. Confirmed live: without a real level-30 smithing, the
-- very first Steve Beanie talk_to answers "Ahoy. Come back when you've got
-- your sea legs and the knack for smithing." on every session, forever.
-- `::setlevel smithing 30` in setup is the fix -- a prerequisite skill
-- level, exactly like the doc's own `::give`-for-prerequisites carve-out.
--
-- Grep across the whole of OSRS-Content/osrs239-content/server/scripts
-- turns up NO `*.spawn` row for steve_beanie anywhere, and no `npc_add` for
-- him outside quest_pandemonium's own BMP debug helpers
-- (`panbmp_near_steve`) -- Pandemonium itself is deferred in this pack
-- (pryingtimes.rs2's own header: "Deferred: ... Pandemonium full quest"),
-- so nothing ever places him. `::spawn steve_beanie_1op` (the generic
-- engine npc cheat -- torirs_server_world.c's `::spawn <npc_name> [count]`,
-- "the npc half of ::give") places the CHILD symbol pryingtimes.rs2 itself
-- registers `[opnpc1,steve_beanie_1op]` on, directly, sidestepping the
-- steve_beanie multinpc parent's multivarbit=sailing_intro table (which
-- only defines slots 1-20 and would render nothing at the soft-skip value
-- 40 the debugproc uses).
--
-- Stage tracking: `%quest_pry` is a VARBIT (configs/all.varbit:84653,
-- `basevar=pry_main startbit=0 endbit=6`), not a plain varp -- there is no
-- `quest_pry` entry in configs/all.varp at all. Two seams this file used to
-- work around are now fixed and confirmed live in this checkout:
--
--   1. `pry_main` (the varbit's basevar) used to be a bare `all.varp` NAME
--      RESERVATION with no `transmit=yes` -- an untransmitted carrier reads
--      a confident 0 forever (QUEST_AUTHORING.md section 8's "config group
--      cache thrash" bullet). `server/scripts/quests/quest_pryingtimes/
--      configs/pryingtimes.varp` now declares `[pry_main] protect=no
--      transmit=yes scope=perm`, and `all.varp.compack` carries `pry_main`
--      at id 4960 -- confirmed by grep, this run.
--   2. Before 2026-09-20, quest.stage()/expect_stage()/expect_complete()'s
--      quest.varp_complete row read `QD.var.varp(bound.varp)` directly, not
--      the varp-or-varbit-transparent resolver -- so every one of those
--      calls answered `no_row`/refused for a varbit-tracked quest, this one
--      by name (quest.lua's own bind banner cites pryingtimes.lua as the
--      motivating case). `QD.quest._bound_kind`/`QD.quest._reading` now
--      resolve the bound name's kind ONCE at bind time and route every read
--      through the matching pair (quest.lua:26-47, dated 2026-09-20).
--
-- Both fixes landed before this run, so `t.quest.expect_complete()` is
-- called for real at the bottom of this file instead of being worked
-- around. Stage checks in between still read `t.var.varbit("quest_pry")`
-- directly (the verb the doc lists as `t.var.varp`'s varbit counterpart) --
-- that is a style choice, not a workaround for a live bug.
--
-- RE-AUTHORED 2026-09-23 (content parity1c/parity1d): both this quest's
-- boat crossings are now real, gated player choices in the .rs2, not bare
-- narration -- see pryingtimes.rs2/pryingtimes_locs.rs2's own headers.
--   1. The Port Sarim<->Pandemonium courier crossing (guide G:174): a
--      SECOND click on Steve while %quest_pry=deliver (not holding the
--      crate) opens `pry_steve_talk`'s `pry_deliver` sub-branch -- a real
--      choice, then p_teleport to ^pry_looty_coord (3047,3221,0), where the
--      looty crate is a real GROUND ITEM to pick up (Take). The return leg
--      is Captain Tobias (areas/port_sarim/scripts/sailors.rs2's
--      `karamja_sailor_talk`, checked first while the crate is held), 30gp,
--      p_teleport back to ^pry_steve_coord.
--   2. The SailStep to the floating grog crate (guide G:185): a second
--      click on Steve while %quest_pry=test_key opens on the choice
--      "Sail your vessel to the floating crate.", p_teleport to
--      ^pry_sea_crate_coord (3013,2998,0); the RETURN leg is a second click
--      on the (by then emptied) crate itself, offering "Sail back to the
--      Pandemonium."
-- Neither crossing is the deferred generic PortTaskStep minigame (a job
-- board assigning one of several cargo types, then real point-to-point
-- open-sea helm navigation) -- that piece is genuinely absent from this
-- pack (pryingtimes.rs2's own header, and quest_pandemonium.rs2:330-341
-- soft-skips the identical route the same way) and is a cross-quest Sailing
-- gap, not something this file can drive.
-- No content-gap marker is needed in this file for that: it is not a step
-- of Quest Helper's own PryingTimes.java ladder (a generic job board and
-- helm are not this quest's mechanic -- Steve assigns one fixed crate, not
-- a pool of cargo types), so helper_coverage.py has no step here to grade
-- CONTENT_GAP against; the deferred piece is pryingtimes.rs2:13-25's own
-- citation of the cross-quest Sailing engine gap, not a leg of this guide.

return {
    id = "pryingtimes",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::pryingtimes", -- resets %quest_pry, grants steel_bar/redberry_pie/hammer/sailing_log,
                          -- teleports to pry_steve_coord (3050,2966,0)
        "::setlevel smithing 30", -- ::pryingtimes's own smithing top-up (1500 xp) is not enough
                                   -- for the quest's base-30 requirement -- see banner above
        "::spawn steve_beanie_1op", -- this content pack never places him -- see banner above
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

        local stage0_result, stage0_value = t.var.varbit("quest_pry")
        t.check("quest.stage.not_started", stage0_result == "ok" and stage0_value == 0,
            "quest_pry (varbit) = " .. tostring(stage0_value) .. " (" .. tostring(stage0_result) .. "), want 0")

        -- The fixture/debugproc already stands the player at pry_steve_coord;
        -- goto_tile is still the first far-step cheat + arrival await this
        -- doc's section 2 asks for before the first talk_to.
        t.exec("goto-steve1", t.player.goto_tile, 3050, 2966, 0)

        -- Session 1: quest_pry not_started -> deliver (pry_steve_talk's
        -- "not started" tail: ~pry_can_start is true now that smithing is
        -- really 30, so this opens directly on the p_choice3, no preceding
        -- npc/player line).
        t.exec("startQuest", t.player.talk_to, "steve_beanie_1op", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "choose:Got any work that needs doing here?",
            "player:Got any work that needs doing here?",
            "npc:Shiver me timbers! There is a crate full of high value goods just sitting at the port of Sarim!",
            "player:Well, I can't say no to a bit of treasure. What do I need to do?",
            "npc:Take a courier task for the looty, sail it here to the Pandemonium, and I'll cut you in.",
            "choose:Yes.",
            "player:Yes.",
            "npc:Grand! Collect the looty from Port Sarim's ledger and bring it to the Pandemonium docks.",
        })
        local stage1_result, stage1_value = t.var.varbit("quest_pry")
        t.check("quest.stage.deliver", stage1_result == "ok" and stage1_value == 5,
            "quest_pry (varbit) = " .. tostring(stage1_value) .. " (" .. tostring(stage1_result) .. "), want 5")

        -- Real Port Sarim<->Pandemonium courier crossing, outbound leg
        -- (guide's own `deliverCargo` PortTaskStep, G:174, content
        -- parity1d): a SECOND click on Steve while %quest_pry=deliver and
        -- the looty crate is not yet held opens `pry_deliver`'s own
        -- sub-branch, which opens on the PLAYER's line
        -- (`~chatplayer("About that job...")` is the branch's own first
        -- call -- read from pryingtimes.rs2, not guessed). Named for the
        -- guide's own step (helper_coverage.py matches by row name; a
        -- PortTaskStep carries no npc/loc target of its own to match by).
        t.exec("deliverCargo", t.player.talk_to, "steve_beanie_1op", 1)
        t.exec("deliverCargo-dialog", t.chat.play, {
            "player:About that job you wanted doing...",
            "npc:Sail that looty from Port Sarim to the Pandemonium docks, then come tell me.",
            "choose:Cast off and sail to Port Sarim yourself.",
        })
        -- The choice's own mes()+p_teleport lands at ^pry_looty_coord
        -- (0_47_50_39_21 -> 3047,3221,0) a few ticks behind the page close
        -- (doc section 2's teleport-dialogue rule) -- await the tile before
        -- the next press rather than trusting the choice's own settle alone.
        local sail1_result, sail1_detail = t.await({
            level = function()
                local tile_result, tile = t.world.tile()
                return tile_result == "ok" and tile.x == 3047 and tile.z == 3221 and tile.level == 0
            end,
            note = "deliverCargo.arrive",
        }, 10)
        t.check("deliverCargo.arrive", sail1_result == "ok",
            "t.await(tile == 3047,3221,0) -> " .. tostring(sail1_result) .. " " .. tostring(sail1_detail))

        -- The courier's own looty crate -- a real GROUND ITEM
        -- (pryingtimes_locs.rs2's [proc,pry_ensure_crates], plain obj_add
        -- with no opobj trigger of its own -- the engine's ordinary Take op,
        -- the same idiom quest_losttribe.rs2:235's brooch uses).
        local looty_locate_result, looty_locate_row = t.world.obj_near("cargo_crate_nothing_sinister_port_sarim", 10)
        t.check("lootyCrate.placed", looty_locate_result == "ok",
            "world.obj_near(cargo_crate_nothing_sinister_port_sarim, 10) -> " .. tostring(looty_locate_result)
                .. " " .. tostring(looty_locate_row))
        -- click_obj is hollow on success (trap 12/section 8's "the auto-shot
        -- photographs the frame, not the assertion") -- t.exec FAILs any
        -- `ok` with a nil detail, so call it directly and read the
        -- backpack back ourselves.
        local pickup_result, pickup_detail = t.player.click_obj("cargo_crate_nothing_sinister_port_sarim")
        t.step("pickUpLooty", pickup_result == "ok" and "PASS" or "FAIL",
            "click_obj(cargo_crate_nothing_sinister_port_sarim) -> " .. tostring(pickup_result)
                .. " " .. tostring(pickup_detail))
        local looty_await_result, looty_await_detail = t.inv.await("cargo_crate_nothing_sinister_port_sarim", 1, 10)
        local looty_result, looty_count = t.inv.count("cargo_crate_nothing_sinister_port_sarim")
        t.check("pickUpLooty.held",
            looty_await_result == "ok" and looty_result == "ok" and looty_count == 1,
            string.format("await=%s(%s) cargo_crate_nothing_sinister_port_sarim %s=%s (want 1)",
                tostring(looty_await_result), tostring(looty_await_detail),
                tostring(looty_result), tostring(looty_count)))

        -- Captain Tobias's own return leg (areas/port_sarim/scripts/
        -- sailors.rs2 `karamja_sailor_talk`, checked FIRST in that label
        -- while the looty crate is held) -- Captain Tobias's own spawn row
        -- (areas/world/configs/m47_50.spawn), plain travel from the drop
        -- tile to reach him. His courier branch opens on the NPC's own line
        -- (trap 18's "almost always" has exceptions -- read the .rs2), and
        -- ::pryingtimes's setup tops coins up to 30 for the fare.
        t.exec("goto-tobias", t.player.goto_tile, 3028, 3216, 0)
        t.exec("tobiasRide", t.player.talk_to, "captain_tobias", 1)
        t.exec("tobiasRide-dialog", t.chat.play, {
            "npc:Need a lift back out to the Pandemonium with that cargo?",
            "npc:It'll cost you the usual 30 coins.",
            "choose:Yes please.",
            "player:Yes please.",
            "mesbox:The ship arrives at the Pandemonium.",
        })
        local sail2_result, sail2_detail = t.await({
            level = function()
                local tile_result, tile = t.world.tile()
                return tile_result == "ok" and tile.x == 3050 and tile.z == 2966 and tile.level == 0
            end,
            note = "tobiasRide.arrive",
        }, 10)
        t.check("tobiasRide.arrive", sail2_result == "ok",
            "t.await(tile == 3050,2966,0) -> " .. tostring(sail2_result) .. " " .. tostring(sail2_detail))

        -- Session 2: quest_pry deliver -> let_steve -> get_key, ALL inside
        -- one dialogue -- with the crate now held, `pry_deliver`'s OTHER
        -- sub-branch runs: the hand-in mesbox, then choosing (silently,
        -- inv_del) sets %quest_pry=let_steve and jumps straight back into
        -- `@pry_steve_talk;` in the same script pass, which immediately
        -- matches the pry_let_steve branch and re-prints the identical
        -- player line before the two chatnpc lines that set get_key.
        t.exec("letSteveKnow", t.player.talk_to, "steve_beanie_1op", 1)
        t.exec("letSteveKnow-dialog", t.chat.play, {
            "player:I delivered that cargo for you.",
            "mesbox:You haul the crate of looty off your shoulder and set it down behind the bar.",
            "player:I delivered that cargo for you.",
            "npc:Shiver me timbers, that crate is locked! Only a special key can open it.",
            "npc:There's an Imcando dwarf named Thurgo near Mudskipper Point who might forge one. Take him a steel bar, a hammer, and a redberry pie.",
        })
        local stage2_result, stage2_value = t.var.varbit("quest_pry")
        t.check("quest.stage.get_key", stage2_result == "ok" and stage2_value == 15,
            "quest_pry (varbit) = " .. tostring(stage2_value) .. " (" .. tostring(stage2_result) .. "), want 15")

        -- Thurgo -- server/scripts/areas/world/configs/m46_49.spawn:18
        t.exec("goto-thurgo", t.player.goto_tile, 3001, 3144, 0)
        t.exec("getKey", t.player.talk_to, "thurgo", 1)
        t.exec("getKey-dialog", t.chat.play, {
            "player:I need some help with a 'special key'.",
            "npc:A special key? Sounds expensive. Got a steel bar, a hammer, and a redberry pie?",
            "choose:So, can you help me make a crowbar?",
            "player:So, can you help me make a crowbar?",
            "npc:Aye",
            "choose:Yes.",
        })
        -- pry_thurgo_make_crowbar consumed the steel bar + pie and granted
        -- the crowbar, then advanced quest_pry to give_key -- verify the
        -- actual inventory swap, not just the varp (t.chat.play's ok is
        -- about the dialogue, never the effects behind it). A plain
        -- inv.count right after the last choose read crowbar=0/steel_bar=1
        -- despite the chatbox already printing "You hammer a crowbar out of
        -- your steel bar..." (measured) -- inv.await actually polls (state.lua)
        -- where a bare read can race the sync packet.
        local crowbar_await_result, crowbar_await_detail = t.inv.await("sailing_charting_crowbar", 1, 10)
        local crowbar_result, crowbar_count = t.inv.count("sailing_charting_crowbar")
        local steelbar_result, steelbar_count = t.inv.count("steel_bar")
        t.check("getKey.crowbar",
            crowbar_await_result == "ok" and crowbar_result == "ok" and crowbar_count == 1
                and steelbar_result == "ok" and steelbar_count == 0,
            string.format("await=%s(%s) crowbar %s=%s, steel_bar %s=%s (want crowbar 1, steel_bar 0)",
                tostring(crowbar_await_result), tostring(crowbar_await_detail),
                tostring(crowbar_result), tostring(crowbar_count), tostring(steelbar_result), tostring(steelbar_count)))
        local stage3_result, stage3_value = t.var.varbit("quest_pry")
        t.check("quest.stage.give_key", stage3_result == "ok" and stage3_value == 20,
            "quest_pry (varbit) = " .. tostring(stage3_value) .. " (" .. tostring(stage3_result) .. "), want 20")

        -- Back to Steve to hand over the crowbar.
        t.exec("goto-steve2", t.player.goto_tile, 3050, 2966, 0)
        t.exec("giveKey", t.player.talk_to, "steve_beanie_1op", 1)
        t.exec("giveKey-dialog", t.chat.play, {
            "player:I made that 'special key' you needed.",
            "mesbox:You show Steve the crowbar.",
            "npc:A crowbar? Well, if it opens crates I'll take it. Test it on the floating crate north-west of the Pandemonium first",
        })
        local stage4_result, stage4_value = t.var.varbit("quest_pry")
        t.check("quest.stage.test_key", stage4_result == "ok" and stage4_value == 25,
            "quest_pry (varbit) = " .. tostring(stage4_value) .. " (" .. tostring(stage4_result) .. "), want 25")

        -- Real SailStep to the floating grog crate (guide's own
        -- `sailToCrate` step, G:185, content parity1c): a second click on
        -- Steve while %quest_pry=test_key opens on the NPC's own line
        -- (trap 18 exception -- read from pryingtimes.rs2, not guessed) and
        -- the choice p_teleports to ^pry_sea_crate_coord -- goto_tile would
        -- cheat past a real, guide-named travel step here (rule (b)). Named
        -- for the guide's own step, same reason as deliverCargo above.
        t.exec("sailToCrate", t.player.talk_to, "steve_beanie_1op", 1)
        t.exec("sailToCrate-dialog", t.chat.play, {
            "npc:I noticed a crate of grog floating around by the north westmost island of the Pandemonium.",
            "npc:Cast off your keel and sail your vessel over there.",
            "choose:Sail your vessel to the floating crate.",
        })
        local sail3_result, sail3_detail = t.await({
            level = function()
                local tile_result, tile = t.world.tile()
                return tile_result == "ok" and tile.x == 3013 and tile.z == 2998 and tile.level == 0
            end,
            note = "sailToCrate.arrive",
        }, 10)
        t.check("sailToCrate.arrive", sail3_result == "ok",
            "t.await(tile == 3013,2998,0) -> " .. tostring(sail3_result) .. " " .. tostring(sail3_detail))

        -- The floating sea crate -- configs/all.loc [sailing_charting_drink_crate]
        -- (op1=Pry-open), pry_sea_crate_coord (0_47_46_5_54 -> 3013,2998,0).
        -- [proc,pry_ensure_crates] (pryingtimes_locs.rs2) places it with
        -- loc_add once %quest_pry >= ^pry_test_key -- giveKey above already
        -- drove the stage write that calls it, so it stands here now
        -- (fixed; this used to be a t.blocked -- QUEUE.tsv's reopen note).
        local seacrate_locate_result, seacrate_locate_row = t.world.loc_near("sailing_charting_drink_crate", 40)
        t.check("seaCrate.placed", seacrate_locate_result == "ok",
            "world.loc_near(sailing_charting_drink_crate, 40) -> " .. tostring(seacrate_locate_result)
                .. " " .. tostring(seacrate_locate_row))

        local stout_before_result, stout_before = t.inv.count("sailing_charting_drink_crate_prying_times")
        t.exec("testKey", t.player.click_loc, "sailing_charting_drink_crate", 1)
        -- inv.await actually polls, where a bare read can race the sync
        -- packet (measured on getKey.crowbar above).
        local stout_await_result = t.inv.await("sailing_charting_drink_crate_prying_times", stout_before + 1, 10)
        local stout_after_result, stout_after = t.inv.count("sailing_charting_drink_crate_prying_times")
        t.check("testKey.stout",
            stout_await_result == "ok" and stout_before_result == "ok" and stout_after_result == "ok"
                and stout_after == stout_before + 1,
            string.format("await=%s sailing_charting_drink_crate_prying_times %s -> %s (want +1)",
                tostring(stout_await_result), tostring(stout_before), tostring(stout_after)))

        -- Drink the stout: opheld1 on the bottle opens a Yes/No, then a
        -- drink troll spawns (optional combat -- headless path skips the
        -- fight, the quest state only needs the completion flag below).
        t.exec("drinkStout", t.player.inv_op, "sailing_charting_drink_crate_prying_times", 1)
        t.exec("drinkStout-dialog", t.chat.play, {
            "choose:Yes.",
        })
        -- pry_drink_stout inv_del's the bottle on a successful drink -- the
        -- real effect the p_choice2 click drove, not just that a page
        -- closed. inv.await(name, 0, ticks) never waits at all
        -- (total >= 0 is always true -- QUEST_AUTHORING.md section 8), so
        -- this polls count == 0 directly instead.
        local stoutgone_result, stoutgone_detail = t.await({
            level = function()
                local result, total = t.inv.count("sailing_charting_drink_crate_prying_times")
                return result == "ok" and total == 0
            end,
            note = "drinkStout.consumed",
        }, 10)
        local stoutdrunk_result, stoutdrunk_count = t.inv.count("sailing_charting_drink_crate_prying_times")
        t.check("drinkStout.consumed",
            stoutgone_result == "ok" and stoutdrunk_result == "ok" and stoutdrunk_count == 0,
            string.format("await=%s(%s) sailing_charting_drink_crate_prying_times count=%s/%s after drinking (want 0)",
                tostring(stoutgone_result), tostring(stoutgone_detail),
                tostring(stoutdrunk_result), tostring(stoutdrunk_count)))

        -- Real return leg of the SailStep (pryingtimes_locs.rs2's
        -- [proc,pry_open_sea_crate]): with the sample flag already set, a
        -- SECOND click on the now-emptied crate offers the ride back
        -- instead of the earlier pry-open -- goto_tile would cheat past
        -- this real, guide-named travel step (rule (b)).
        t.exec("sailBackToSteve", t.player.click_loc, "sailing_charting_drink_crate", 1)
        t.exec("sailBackToSteve-dialog", t.chat.play, {
            "choose:Sail back to the Pandemonium.",
        })
        local sail4_result, sail4_detail = t.await({
            level = function()
                local tile_result, tile = t.world.tile()
                return tile_result == "ok" and tile.x == 3050 and tile.z == 2966 and tile.level == 0
            end,
            note = "sailBackToSteve.arrive",
        }, 10)
        t.check("sailBackToSteve.arrive", sail4_result == "ok",
            "t.await(tile == 3050,2966,0) -> " .. tostring(sail4_result) .. " " .. tostring(sail4_detail))

        -- Steve is a bare `::spawn` npc -- this content pack never places
        -- him (file banner above) -- and he is gone from the client's
        -- entity pool by the time the sea-crate round trip lands back here
        -- (measured: goToSteve answered "no npc 14968 (steve_beanie_1op) in
        -- the client's entity pool" without this row; the drink troll's own
        -- npc_add near the sea crate, a few rows above, is the only other
        -- npc this run adds, and it is the one difference from the earlier
        -- Port Sarim/Thurgo round trips that both left him standing). Same
        -- infrastructure cheat setup already uses to compensate for the
        -- missing placement, not quest work -- re-issued here rather than
        -- once at setup.
        local respawn_result = t.cheat("::spawn steve_beanie_1op")
        t.step("respawn.steve", respawn_result == "ok" and "PASS" or "FAIL",
            "::spawn steve_beanie_1op -> " .. tostring(respawn_result))
        local respawn_present_result, respawn_present_detail = t.npc.await_present("steve_beanie_1op", 15, 10)
        t.check("respawn.steve.present", respawn_present_result == "ok",
            "npc.await_present(steve_beanie_1op, 15) -> " .. tostring(respawn_present_result)
                .. " " .. tostring(respawn_present_detail))

        -- Tell Steve the key works.
        t.exec("goToSteve", t.player.talk_to, "steve_beanie_1op", 1)
        t.exec("goToSteve-dialog", t.chat.play, {
            "player:About that crate...",
            "npc:So the key works? Brilliant! Now use it on my sealed crate behind the bar.",
        })
        local stage5_result, stage5_value = t.var.varbit("quest_pry")
        t.check("quest.stage.open_crate", stage5_result == "ok" and stage5_value == 30,
            "quest_pry (varbit) = " .. tostring(stage5_value) .. " (" .. tostring(stage5_result) .. "), want 30")

        -- Reward snapshot before the hand-in.
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))

        -- Steve's sealed crate behind the bar -- pry_crate_multi's
        -- multiloc21/26/31=pry_crate_sealed (multivarbit=quest_pry), pry_bar_crate_coord
        -- (0_47_46_40_21 -> 3048,2965,0). [proc,pry_ensure_crates] places
        -- pry_crate_multi with loc_add once %quest_pry >= ^pry_deliver,
        -- confirmed already standing by now (fixed; this used to be a
        -- t.blocked -- QUEUE.tsv's reopen note).
        local barcrate_locate_result, barcrate_locate_row = t.world.loc_near("pry_crate_sealed", 40)
        t.check("barCrate.placed", barcrate_locate_result == "ok",
            "world.loc_near(pry_crate_sealed, 40) -> " .. tostring(barcrate_locate_result)
                .. " " .. tostring(barcrate_locate_row))

        t.exec("openCrate", t.player.click_loc, "pry_crate_sealed", 1)

        -- pry_open_bar_crate (pryingtimes_locs.rs2) runs ~pry_quest_complete
        -- in the same script pass as the click, with no mesbox/page in
        -- between -- but the stage/varp/reward writes still land one server
        -- tick behind the click's own settle (QUEST_AUTHORING.md trap 24),
        -- so settle a tick before reading any of it.
        t.ticks(3)

        -- t.quest.expect_complete() -- NOW CALLED FOR REAL. Both seams that
        -- used to make it structurally unable to pass on this varbit-tracked
        -- quest are fixed (see the file banner): pry_main is a transmitted
        -- varp now, and quest.varp_complete reads it through the bound kind
        -- instead of a bare QD.var.varp. Writes quest.varp_complete,
        -- quest.scroll_title, quest.points, quest.journal and photographs the
        -- completion scroll.
        local expect_complete_result, expect_complete_detail = t.quest.expect_complete()
        t.step("quest.expect_complete", expect_complete_result == "ok" and "PASS" or "FAIL",
            tostring(expect_complete_result) .. " " .. tostring(expect_complete_detail))

        -- Rewards documented in [proc,pry_quest_complete] (pryingtimes.rs2)
        -- and in the quest's own `~quest_complete_rewards(quest_pryingtimes,
        -- "1000 Smithing XP|800 Sailing XP|25 oak sawmill coupons|Unlimited
        -- crowbars from the crate|Ability to chart forgotten drinks", ...)`
        -- call -- the literal STAT/ITEM grants this pack makes (the proc's
        -- own stat_advance/inv_add lines), not the two non-grant lines in
        -- that string ("Unlimited crowbars"/"Ability to chart" are
        -- unlockable behaviour, nothing to assert beyond the crowbar item
        -- check below). Sailing XP is now real too (2026-09-23 parity
        -- pass) -- pryingtimes.constant's ^pry_sailing_xp = 8000 (tenths),
        -- 800 actual, granted on top of whatever ::pryingtimes's own setup
        -- top-up already put in the snapshot taken above. The crowbar is
        -- the item reward, but we already hold ours from Thurgo (the
        -- proc's own `if (inv_total(...) < 1)` guard skips re-granting one
        -- we already have) -- expect_has still checks the backpack
        -- actually holds it.
        t.check("reward.smithing", t.skill.expect_gain("smithing", 1000, reward_before))
        t.check("reward.sailing", t.skill.expect_gain("sailing", 800, reward_before))
        t.check("reward.coupons", t.inv.expect_has("sawmill_coupon_oak", 25))
        t.check("reward.crowbar", t.inv.expect_has("sailing_charting_crowbar", 1))

        t.finish(0)
        return
    end,
}
