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
        local stagnant = t.player.by_symbol("loc", "deal_stagnant")
        local walk_result, walk_detail = t.player.walk_to(2120, 5125, 20)
        local _, gate_tile = t.world.tile()
        t.check("gate.walk_through", walk_result == "timeout",
            "STALLED as the content_bug predicts: walk_to 2120,5125 after 'You open the gate.' -> " .. tostring(walk_result) .. " " .. tostring(walk_detail)
            .. " at " .. tostring(gate_tile.x) .. "," .. tostring(gate_tile.z))
        t.blocked("content_bug: deal_water_hopper.rs2:5 [oploc1,deal_gate_closed] swaps in deal_gate_open at the SAME loc_angle/loc_shape (loc_add($deal_gate_c, deal_gate_open, $deal_gate_a, $deal_gate_s, 500)); all.loc:112368 deal_gate_open has the same shape1=0,9872 wall, and the map places it as a wall (m33_79.jl2:3586 '1 8 42: 10172 0 1'), so the opened gate keeps blocking 2120,5098 -> north and the player cannot reach the lake. The generic door handler (doors.rs2:110) rotates the angle by +1 on open; the quest handler does not. The real game's gate opens and lets the player through (OSRS wiki Rum Deal: the gate on the pier is opened to reach the north of the island).")
        return
    end,
}
