-- Rum Deal, end to end through the real client -- docs/quests/rum_deal.md
-- (pinned brief) + Quest Helper's RumDeal.java/SlugSteps.java (steps.put
-- 0-18 + the sub-helper's fish/deposit/lever ladder).
--
-- HOW THIS FILE WAS BUILT. Not scaffolded from new_quest.py: a parity-proof
-- scratch driver already exists at
-- build/quest_gate/s22fs_rumdeal3/script/rumdeal_copy_nogloves.lua (parity2c
-- + seam22, 106/108 PASS) that drove every real leg of this content through
-- the live client and proved it matches the guide. This file is that same
-- leg ladder, adapted onto the committed test shape, with four changes:
--
--   1. `deal_slayer_gloves` worn for the fever-spider fight instead of
--      eating through the seam22 forced-hit-and-disease penalty. The guide
--      lists slayer gloves in getItemRequirements() (a bring-along, not a
--      gathered item), so `::give`-ing them in setup is legitimate (trap
--      16/rule c), and wearing them removes the forced 12.5%-of-Hitpoints
--      hit and the disease entirely (skill_slayer/scripts/slayer_gear.rs2
--      `slayer_needs_gloves` gates on `npc_type = deal_fever_spiders1`;
--      slayer_specials.rs2 `slayer_on_npc_hit_player` only applies the
--      forced hit + `~apply_disease` when the gloves are NOT worn) -- the
--      spider's ordinary accuracy-rolled melee still lands either way
--      ("fever spiders take the player's damage with or without gloves"),
--      which is why the fight is still driven as a real kill, not skipped.
--   2. `inv.blindweed` read with `t.inv.await` instead of a bare
--      `t.inv.expect_has` right after `patch.pick` (a `click_loc`) -- trap
--      24: a click verb's `ok` is the server's sentence one tick before the
--      container update reaches the client; only `use_on` waits for it.
--      The nogloves scratch run FAILed this exact row (`deal_blindweed:
--      absent`) with the stage already advanced to deliver_blindweed.
--   3. `carcass.pickup` (`t.player.click_obj`) called directly with
--      `t.check` instead of through `t.exec` -- trap 12/section 8:
--      `click_obj` answers `ok` with a nil detail, which `t.exec` grades
--      FAIL `hollow`. The scratch run hit exactly this.
--   4. `spirit.spawned` written with `t.check` instead of `t.step` -- the
--      scratch run passed a boolean condition as `t.step`'s second
--      argument (the verdict-word slot), which `core.lua` now tolerates at
--      runtime but `lint_quest.py` refuses outright (trap 287: `t.step`
--      takes "PASS"/"FAIL"/"BLOCKED", `t.check` takes the condition).
--   5. Reward rows added: the scratch driver never asserted the quest's
--      three literal XP rewards or the retained holy wrench (section 7's
--      reviewer rule -- a reward Quest Helper lists needs a row). The
--      amounts are read directly off the dbrow's own completion proc
--      (deal_shared.rs2 `[proc,deal_quest_complete]`: stat_advance
--      fishing/prayer/farming, all `^deal_reward_*_xp` = 70000 tenths =
--      7000 XP each, matching RumDeal.java's ExperienceReward list and the
--      wiki's Rewards section exactly) rather than a scroll-parsed number,
--      since all three are already pinned in the content and the brief.
--
-- Rewards NOT asserted: "Access to Braindeath Island" is narrative access
-- with no varp/item/xp signal to read back (RumDeal.java's getItemRewards()
-- carries only the holy wrench; the `coins` argument to
-- `~quest_complete_rewards` is that call's icon-obj parameter, not a coin
-- grant -- questpoints.rs2's own signature names it `$icon`).
--
-- Sanctioned setup cheats only (::clearinv/::give/::setlevel/::complete):
-- Zogre Flesh Eaters and Priest in Peril are hard prerequisites
-- (deal_shared.rs2 `[proc,deal_meets_requirements]`) with no in-quest path
-- to satisfy them, so they are staged with `::complete <dbrow>` (trap 294 --
-- `quest_zogreflesheaters`/`quest_priestinperil` are the dbrow names, not
-- the folder names). No `::kill`, `::setvar` on `%deal_quest`/`%deal_farming`
-- /`%deal_barrel`/`%deal_multi_hopper` anywhere in run().

return {
    id = "rumdeal",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_zogreflesheaters",
        "::complete quest_priestinperil",
        "::setlevel fishing 50",
        "::setlevel prayer 47",
        "::setlevel crafting 42",
        "::setlevel slayer 42",
        "::setlevel farming 40",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        -- getItemRequirements(): combatGear, dibber, rake, slayerGloves --
        -- all bring-along, none of them this quest's own gathered work
        -- (docs/quests/rum_deal.md section 7: the basement cupboard that
        -- would hand out the rake/dibber in real OldSchool is inert in
        -- this port, so there is no in-game source for them at all).
        "::give rake 1",
        "::give dibber 1",
        "::give deal_slayer_gloves 1",
        "::give bucket_empty 1",
        "::give fishbowl_net 1",
        "::give shark 5",
        "::give rune_scimitar 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp600_deal_quest",
            constants = {
                not_started = 0, started = 1, growing_blindweed = 2,
                deliver_blindweed = 3, hopper_blindweed = 4,
                told_get_water = 5, get_water = 6,
                told_get_sluglings = 7, get_sluglings = 8,
                told_kill_spirit = 9, kill_spirit = 10,
                told_kill_spider = 11, kill_spider = 12,
                told_get_swill = 13, get_swill = 14,
                return_to_finish = 15, complete = 19,
            },
            display = "Rum Deal",
            points = 2,
        })
        t.ticks(3)
        t.expect("bind.not_started", t.quest.expect_stage("not_started"))

        -- Leg 1: Pirate Pete, north east of the Ectofuntus (deal_pete.rs2).
        t.exec("pete.goto", t.player.goto_tile, 3680, 3537, 0)
        t.exec("pete.talk", t.player.talk_to, "deal_pete")
        t.exec("pete.dialogue", t.chat.play, {
            "player:Who are you",
            "npc:Pirate Pete",
            "npc:Now here's my plan",
            "npc:myself.",
            "player:And you want me",
            "npc:That's the spirit",
            "npc:your trouble.",
            "options",
            "choose:Keep your money -- I'll help for free.",
            "player:Keep it. I'll help you out for free.",
            "npc:Nonsense, take a boat over",
            "mesbox:agree to help Pirate Pete",
        })
        t.expect("pete.started", t.quest.expect_stage("started"))

        -- Leg 2: Captain Braindeath grants the Blindweed seed
        -- (deal_braindeath.rs2, %deal_quest=started branch).
        t.exec("braindeath.goto1", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.seed", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.seed.dialogue", t.chat.play, {
            "player:Pete sent me",
            "npc:Aye, that I do",
            "npc:Blindweed",
            "mesbox:Blindweed seed",
        })
        t.expect("braindeath.growing", t.quest.expect_stage("growing_blindweed"))
        t.expect("inv.seed", t.inv.expect_has("deal_blindweed_seed", 1))

        -- Leg 3: the real Blindweed patch puzzle -- rake 3x, plant with the
        -- dibber, wait for the settimer'd grow, pick (deal_farming.rs2).
        local patch = t.player.by_symbol("loc", "deal_blindweed")
        -- goDownstairs (Quest Helper: "Go down to the island's farming
        -- patch...", target deal_stairs_top) has no [oploc*,deal_stairs_top]
        -- trigger at all -- only ladders.loc's op1=Climb-down label -- so
        -- the generic [proc,climb] maplink is what governs the crossing,
        -- and it is keyed on the PLAYER's own tile, not the loc's
        -- (QUEST_AUTHORING.md section 2's `~maplink_try` note): goto_tile
        -- the destination floor is the documented way to cross a stairs/
        -- ladder loc, never a click on it first.
        -- ANY-OF: goDownstairs patch.goto ladders_stairs/scripts/ladders.rs2:69 -- [proc,climb]'s ~maplink_try keys off the player's tile, not deal_stairs_top's; goto_tile lands the floor the stairs would have
        t.exec("patch.goto", t.player.goto_tile, 2163, 5070, 0)
        t.exec("patch.rake1", t.player.use_on, "rake", patch)
        t.exec("patch.rake2", t.player.use_on, "rake", patch)
        t.exec("patch.rake3", t.player.use_on, "rake", patch)
        t.expect("patch.raked", t.var.expect("varb1366_deal_farming", 3))
        t.exec("patch.plant", t.player.use_on, "deal_blindweed_seed", patch)
        t.expect("patch.planted", t.var.expect("varb1366_deal_farming", 4))
        -- Named after Quest Helper's own step variable (waitForGrowth,
        -- DetailedQuestStep, "Wait 5 minutes for the blindweed to grow.") --
        -- a targetless narrative step, credited only by a row whose name
        -- equals it (trap 32/helper_coverage.py's driven()).
        t.exec("waitForGrowth", t.var.await, "varb1366_deal_farming", 5, 520)
        t.exec("patch.pick", t.player.click_loc, "deal_blindweed", 1)
        t.expect("braindeath.deliver", t.quest.expect_stage("deliver_blindweed"))
        -- click_loc's `ok` lands one server tick before the backpack update
        -- reaches the client (trap 24) -- a bare inv.expect_has here read
        -- "absent" on the nogloves scratch run with the stage already
        -- advanced. inv.await polls instead.
        t.exec("inv.blindweed", t.inv.await, "deal_blindweed", 1, 10)

        -- Leg 4: hand the Blindweed to Braindeath, drop it in the hopper.
        t.exec("braindeath.goto2", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.deliver.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.deliver.dialogue", t.chat.play, {
            "player:I've got the Blindweed",
            "npc:top floor",
        })
        t.expect("braindeath.hopper_stage", t.quest.expect_stage("hopper_blindweed"))
        local hopper = t.player.by_symbol("loc", "deal_hopper")
        t.exec("hopper.goto1", t.player.goto_tile, 2142, 5102, 2)
        t.exec("hopper.blindweed", t.player.use_on, "deal_blindweed", hopper)
        t.expect("hopper.told_water", t.quest.expect_stage("told_get_water"))

        -- Leg 5: stagnant water -- Braindeath grants a bucket, open the
        -- gate, fill it, pour it in (deal_water_hopper.rs2).
        t.exec("braindeath.goto3", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.water.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.water.dialogue", t.chat.play, {
            "player:in the hopper",
            "npc:stagnant water",
        })
        t.expect("quest.stage.get_water", t.quest.expect_stage("get_water"))
        t.exec("gate.goto", t.player.goto_tile, 2120, 5098, 0)
        t.exec("gate.open", t.player.click_loc, "deal_gate_closed", 1)
        -- seam rumdeal_gate_keeps_its_wall: the gate now opens through the
        -- shared door code; walk north through 2120,5098 to the lake.
        local w1r, w1d = t.player.walk_to(2120, 5125, 30)
        local w1t, w1 = t.world.tile()
        t.check("openGate.walk_north", w1t == "ok" and w1.z >= 5120,
            "walk_to 2120,5125 -> " .. tostring(w1r) .. " (" .. tostring(w1d) .. ") at " .. tostring(w1.x) .. "," .. tostring(w1.z))
        local w2r, w2d = t.player.walk_to(2128, 5142, 40)
        local w2t, w2 = t.world.tile()
        t.check("walk-north_island", w2t == "ok" and w2.z >= 5138,
            "walk_to 2128,5142 -> " .. tostring(w2r) .. " (" .. tostring(w2d) .. ") at " .. tostring(w2.x) .. "," .. tostring(w2.z))
        local w3r, w3d = t.player.walk_to(2132, 5158, 40)
        local w3t, w3 = t.world.tile()
        t.check("walk-useBucketOnWater", w3t == "ok" and w3.z >= 5150,
            "walk_to 2132,5158 -> " .. tostring(w3r) .. " (" .. tostring(w3d) .. ") at " .. tostring(w3.x) .. "," .. tostring(w3.z))
        local stagnant = t.player.by_symbol("loc", "deal_stagnant")
        t.exec("lake.fill", t.player.use_on, "bucket_empty", stagnant)
        t.expect("inv.stagnant", t.inv.expect_has("deal_stagnant_bucket", 1))
        t.exec("hopper.goto2", t.player.goto_tile, 2142, 5102, 2)
        t.exec("hopper.water", t.player.use_on, "deal_stagnant_bucket", hopper)
        t.expect("hopper.told_sluglings", t.quest.expect_stage("told_get_sluglings"))

        -- Leg 6: five sluglings, fished around the coast, into the
        -- pressure barrel, lever pulled (deal_sluglings.rs2 + SlugSteps.java).
        t.exec("braindeath.goto4", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.sluglings.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.sluglings.dialogue", t.chat.play, {
            "player:water's in the hopper",
            "npc:sluglings",
        })
        t.expect("braindeath.get_sluglings", t.quest.expect_stage("get_sluglings"))
        t.exec("coast.goto", t.player.goto_tile, 2173, 5074, 0)
        for i = 1, 5 do
            t.exec("slugling.fish" .. i, t.player.talk_to, "deal_squid")
        end
        t.expect("inv.sluglings", t.inv.expect_has("deal_slugling", 5))
        local pressure = t.player.by_symbol("loc", "deal_pressure")
        t.exec("barrel.goto", t.player.goto_tile, 2142, 5102, 2)
        for i = 1, 5 do
            t.exec("slugling.deposit" .. i, t.player.use_on, "deal_slugling", pressure)
        end
        t.expect("barrel.full", t.var.expect("varb1354_deal_barrel", 5))
        t.exec("lever.pull", t.player.click_loc, "deal_multi_lever", 1)
        t.expect("lever.told_spirit", t.quest.expect_stage("told_kill_spirit"))

        -- Leg 7: the evil spirit -- Braindeath's wrench, Davey's blessing,
        -- a REAL fight (deal_combat.rs2, [opnpc2,deal_evil_spirit] ->
        -- @player_combat_start per the seam20 trap-31 fix).
        t.exec("braindeath.goto5", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.spirit.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.spirit.dialogue", t.chat.play, {
            "player:barrel's full",
            "npc:evil spirit",
        })
        t.expect("braindeath.kill_spirit", t.quest.expect_stage("kill_spirit"))
        t.expect("inv.wrench", t.inv.expect_has("deal_wrench", 1))
        t.exec("davey.goto", t.player.goto_tile, 2132, 5100, 1)
        t.exec("davey.talk", t.player.talk_to, "deal_davey")
        t.exec("davey.dialogue", t.chat.play, {
            "player:might be able to help",
            "npc:possessed",
            "mesbox:blesses the wrench",
        })
        t.expect("inv.wrench_blessed", t.inv.expect_has("deal_wrench_blessed", 1))
        local multicontrol = t.player.by_symbol("loc", "deal_multicontrol")
        t.exec("control.goto", t.player.goto_tile, 2144, 5101, 1)
        t.exec("control.wrench", t.player.use_on, "deal_wrench_blessed", multicontrol)
        -- `~mesbox(...)` suspends the calling script (trap 22) --
        -- `~deal_spawn_evilspirit` is the line AFTER the mesbox call in
        -- `[oplocu,deal_multicontrol]`, so it does not run until this
        -- dialogue closes.
        t.exec("control.mesbox_continue", t.chat.play, { "mesbox:*" })
        local spirit_present = t.npc.await_present("deal_evil_spirit", 10, 10)
        t.check("spirit.spawned", spirit_present == "ok",
            "npc.await_present(deal_evil_spirit,10,10) -> " .. tostring(spirit_present))
        t.exec("spirit.attack", t.player.attack, "deal_evil_spirit", 2, 20)
        t.exec("spirit.dead", t.npc.await_dead_engaged, 80, 8)
        t.ticks(6)
        t.expect("spirit.told_spider", t.quest.expect_stage("told_kill_spider"))

        -- Leg 8: the fever spider basement -- a REAL kill on a world-
        -- spawned npc, carcass carried up to the hopper (deal_combat.rs2's
        -- [ai_queue3,deal_fever_spiders1] via the wildcard [opnpc2,_]
        -- combat-start binding; deal_water_hopper.rs2's oplocu branch).
        t.exec("braindeath.goto6", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.spider.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.spider.dialogue", t.chat.play, {
            "player:evil spirit's gone",
            "npc:fever spiders",
        })
        t.expect("braindeath.kill_spider", t.quest.expect_stage("kill_spider"))
        t.exec("basement.goto", t.player.goto_tile, 2148, 5103, 0)
        t.exec("scimitar.equip", t.player.equip, "rune_scimitar")
        -- Slayer gloves worn: slayer_gear.rs2's slayer_needs_gloves gates
        -- on npc_type = deal_fever_spiders1, and slayer_specials.rs2's
        -- slayer_on_npc_hit_player only forces the extra 12.5%-of-Hitpoints
        -- hit + ~apply_disease when the gloves are NOT worn -- with them
        -- on, only the spider's ordinary accuracy-rolled melee can land,
        -- so a plain attack + kill wait is safe here (no eat loop needed).
        t.exec("gloves.equip", t.player.equip, "deal_slayer_gloves")
        t.exec("spider.attack", t.player.attack, "deal_fever_spiders1", 2, 30)
        -- Run 1: (40, 6) timed out with the spider already at 2/40 hp and
        -- 0 re-engagements (the fight was progressing the whole time, just
        -- short of budget) -- widened to match the evil spirit's own (80, 8)
        -- ceiling; the spider has less HP so this has room to spare.
        t.exec("spider.dead", t.npc.await_dead_engaged, 80, 10)
        local carcass_click_result = t.player.click_obj("deal_spider_body")
        local carcass_count_result, carcass_count = t.inv.count("deal_spider_body")
        -- click_obj answers ok with a nil detail (trap 12/section 8's
        -- hollow list) -- call it directly and write the counts read back.
        t.check("carcass.pickup",
            carcass_click_result == "ok" and carcass_count_result == "ok" and carcass_count == 1,
            "click_obj deal_spider_body -> " .. tostring(carcass_click_result)
                .. "; deal_spider_body count " .. tostring(carcass_count_result)
                .. " " .. tostring(carcass_count))
        t.expect("inv.carcass", t.inv.expect_has("deal_spider_body", 1))
        t.exec("hopper.goto3", t.player.goto_tile, 2142, 5102, 2)
        t.exec("hopper.spider", t.player.use_on, "deal_spider_body", hopper)
        t.expect("hopper.told_swill", t.quest.expect_stage("told_get_swill"))

        -- Leg 9: the finished mash -- fill a bucket from the output tap,
        -- Donnie's taste test, back to Braindeath to finish
        -- (deal_water_hopper.rs2's tap, deal_donnie.rs2).
        t.exec("braindeath.goto7", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.swill.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.swill.dialogue", t.chat.play, {
            "player:spider's in the hopper",
            "npc:mash done",
        })
        t.expect("braindeath.get_swill", t.quest.expect_stage("get_swill"))
        local tap = t.player.by_symbol("loc", "deal_brewvat_tap")
        -- walk from Braindeath to the tap on the same floor (a ::goto out of
        -- the north-island region reads as skipping the gate step).
        local tapr, tapd = t.player.walk_to(2142, 5094, 30)
        local tapt, tapp = t.world.tile()
        t.check("tap.walk", tapt == "ok" and tapp.z <= 5096,
            "walk_to 2142,5094 -> " .. tostring(tapr) .. " (" .. tostring(tapd) .. ") at " .. tostring(tapp.x) .. "," .. tostring(tapp.z))
        t.exec("tap.fill", t.player.use_on, "bucket_empty", tap)
        t.expect("inv.swill", t.inv.expect_has("deal_bucket_swill", 1))
        t.exec("donnie.goto", t.player.goto_tile, 2150, 5078, 0)
        t.exec("donnie.talk", t.player.talk_to, "deal_captian_donnie")
        t.exec("donnie.dialogue", t.chat.play, {
            "player:Here, try this",
            "npc:rough",
            "mesbox:approval",
        })
        t.expect("donnie.return_to_finish", t.quest.expect_stage("return_to_finish"))

        -- Leg 10: finish with Braindeath -- ~deal_quest_complete grants
        -- 7000 Fishing/Prayer/Farming XP each (deal_shared.rs2's
        -- stat_advance calls against configs/rumdeal.constant's
        -- ^deal_reward_*_xp = 70000 tenths, matching RumDeal.java's
        -- ExperienceReward list and the wiki's Rewards section).
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.step("rumdeal.xp_before_read", xp_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))
        t.exec("braindeath.goto8", t.player.goto_tile, 2144, 5109, 1)
        t.exec("braindeath.finish.talk", t.player.talk_to, "deal_captian_braindeath")
        t.exec("braindeath.finish.dialogue", t.chat.play, {
            "player:taste of the swill",
            "npc:Then it's done",
            "npc:work fixing that control.",
        })
        t.quest.expect_complete()

        -- Every reward Quest Helper lists (getExperienceRewards +
        -- getItemRewards): three literal XP grants and the retained holy
        -- wrench. "Access to Braindeath Island" has no readable signal.
        t.exec("reward.fishing_xp", t.skill.expect_gain, "fishing", 7000, xp_snapshot)
        t.exec("reward.prayer_xp", t.skill.expect_gain, "prayer", 7000, xp_snapshot)
        t.exec("reward.farming_xp", t.skill.expect_gain, "farming", 7000, xp_snapshot)
        t.expect("reward.holy_wrench", t.inv.expect_has("deal_wrench_blessed", 1))

        t.finish(0)
    end,
}
