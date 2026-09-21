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
-- `quest_pry` entry in configs/all.varp at all. quest.lua's own
-- QD.quest.stage()/expect_stage() call `QD.var.varp(bound.varp)` directly
-- (quest.lua ~120-127), not the varp-or-varbit-transparent QD.var.server
-- its own quest.bind banner promises ("varp ... resolved through
-- QD.var.varp/QD.var.server, both varp-or-varbit-transparent already") --
-- so t.quest.expect_stage("...") answers `no_row`/refused for every stage
-- of THIS quest, confirmed live (run 1: `quest.stage.not_started FAIL 0
-- quest_pry`, the exact `return client_result, bound.varp` shape). Stage
-- checks below read `t.var.varbit("quest_pry")` directly instead -- the
-- verb the doc itself lists as the varbit counterpart of `t.var.varp`.

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

        -- Session 2: quest_pry deliver -> let_steve -> get_key, ALL inside
        -- one dialogue -- choosing "I delivered that cargo for you." sets
        -- %quest_pry=let_steve then jumps straight back into
        -- `@pry_steve_talk;` in the same script pass, which immediately
        -- matches the pry_let_steve branch and re-prints the identical
        -- player line before the two chatnpc lines that set get_key.
        t.exec("letSteveKnow", t.player.talk_to, "steve_beanie_1op", 1)
        t.exec("letSteveKnow-dialog", t.chat.play, {
            "choose:I delivered that cargo for you.",
            "player:I delivered that cargo for you.",
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

        -- The floating sea crate -- configs/all.loc [sailing_charting_drink_crate]
        -- (op1=Pry-open), pry_sea_crate_coord (0_47_46_5_54 -> 3013,2998,0).
        -- A plain read here (not a t.check/t.exec) so a `not_found` does not
        -- write a stray FAIL row ahead of the t.blocked it would demand.
        t.exec("goto-seaCrate", t.player.goto_tile, 3013, 2998, 0)
        local seacrate_locate_result, seacrate_locate_row = t.world.loc_near("sailing_charting_drink_crate", 40)
        if seacrate_locate_result ~= "ok" then
            t.blocked("sailing_charting_drink_crate: world.loc_near -> " .. tostring(seacrate_locate_result)
                .. " " .. tostring(seacrate_locate_row) .. " at 3013,2998,0 -- this content pack never places"
                .. " it (no loc_add anywhere under server/scripts/quests/quest_pryingtimes or"
                .. " quest_pandemonium, and no *.loc placement mirror for it either)")
            return
        end

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

        -- Tell Steve the key works.
        t.exec("goto-steve3", t.player.goto_tile, 3050, 2966, 0)
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
        -- (0_47_46_40_21 -> 3048,2965,0). Same plain-read shape as the sea
        -- crate above.
        local barcrate_locate_result, barcrate_locate_row = t.world.loc_near("pry_crate_sealed", 40)
        if barcrate_locate_result ~= "ok" then
            t.blocked("pry_crate_sealed: world.loc_near -> " .. tostring(barcrate_locate_result)
                .. " " .. tostring(barcrate_locate_row) .. " near 3050,2966,0 -- pry_crate_multi (the multiloc"
                .. " parent, configs/all.loc) is never placed anywhere under server/scripts")
            return
        end

        t.exec("openCrate", t.player.click_loc, "pry_crate_sealed", 1)

        -- Completion is real (openCrate above ran [proc,pry_quest_complete]
        -- for real, through a genuine click, never cheated) -- verify it
        -- and every reward the proc grants through verbs that read the
        -- WORLD, not the broken quest_pry path: the varbit directly for
        -- stage, skill.expect_gain for the smithing xp, inv.expect_has for
        -- the coupons and the crowbar.
        local complete_result, complete_value = t.var.varbit("quest_pry")
        t.check("quest.stage.complete", complete_result == "ok" and complete_value == 35,
            "quest_pry (varbit) = " .. tostring(complete_value) .. " (" .. tostring(complete_result)
                .. "), want 35")

        -- Rewards documented in [proc,pry_quest_complete] (pryingtimes.rs2):
        -- ^pry_smith_xp=10000 tenths = 1000 Smithing XP; ^pry_coupon_count=25
        -- oak sawmill coupons; the crowbar is the item reward, but we
        -- already hold ours from Thurgo (the proc's own `if (inv_total(...)
        -- < 1)` guard skips re-granting one we already have).
        t.check("reward.smithing", t.skill.expect_gain("smithing", 1000, reward_before))
        t.check("reward.coupons", t.inv.expect_has("sawmill_coupon_oak", 25))
        t.check("reward.crowbar", t.inv.expect_has("sailing_charting_crowbar", 1))

        -- t.quest.expect_complete() is deliberately NOT called: its own
        -- first row, quest.varp_complete, calls QD.var.varp("quest_pry")
        -- (quest.lua's expect_complete, same call quest.stage()/
        -- expect_stage() make -- ~120-127 and ~190-196), never the
        -- varp-or-varbit-transparent QD.var.server its own quest.bind
        -- banner promises ("resolved through QD.var.varp/QD.var.server,
        -- both varp-or-varbit-transparent already"). %quest_pry is
        -- registered ONLY as a varbit (configs/all.varbit:84653,
        -- basevar=pry_main startbit=0 endbit=6); no quest_pry entry exists
        -- in configs/all.varp at all, confirmed live below. Calling
        -- expect_complete() would write a guaranteed stray FAIL row for
        -- quest.varp_complete -- "ANYTHING ELSE ... IS REJECTED, NOT
        -- BLOCKED" (QUEST_AUTHORING.md section 6) -- so this file proves
        -- the same completion every other way a working verb can, then
        -- blocks on the one call that structurally cannot pass here.
        local varp_complete_result, varp_complete_detail = t.var.varp("quest_pry")
        t.blocked("t.quest.expect_complete()'s quest.varp_complete row calls QD.var.varp(\"quest_pry\") "
            .. "and can never pass on this quest -- confirmed here: t.var.varp(\"quest_pry\") -> "
            .. tostring(varp_complete_result) .. " " .. tostring(varp_complete_detail) .. ". "
            .. "The playthrough above is real and complete (quest_pry varbit=35, +1000 smithing xp, "
            .. "+25 oak sawmill coupons, crowbar in the backpack, all verified above) -- "
            .. "quest.expect_complete() itself is the only thing that cannot see it.")
        return
    end,
}
