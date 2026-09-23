-- Fishing Contest, end to end through the real client.
--
-- The scaffold (tools/quest_gate/new_quest.py, from Quest Helper's guide)
-- guessed a dialogue tree that does not match this content pack at all --
-- Quest Helper's own dialog text ("I was wondering what was down those
-- stairs?", no "just") never matches areas/area_white_wolf_mountain/scripts/
-- mountain_dwarf.rs2's actual rows, and its loc targets (kr_seers_table2,
-- mcgruborlooserailing) are not wired to garlic/worm grants anywhere in this
-- pack at all. Rewritten by hand against the quest's own .rs2 files:
--   quests/quest_fishingcompo/scripts/{quest_fishingcompo,
--     quest_fishingcompo_gate,hemenster_fishing}.rs2
--   areas/area_white_wolf_mountain/scripts/mountain_dwarf.rs2
--   areas/area_seers/scripts/hemenster/bonzo.rs2
--
-- Garlic and the red vine worm have NO gather interaction wired in this
-- port -- configs/quest_fishingcompo_comp.varp's own banner calls the quest
-- "landed with hemenster thin NPCs (slice 12b); full quest deferred", and
-- neither `kr_seers_table2` nor `red_worm_junction` has a single [oploc*]
-- handler anywhere in server/scripts. They are brought-along prerequisites
-- here (::give), same as the fishing rod and the entrance fee coins --
-- never the quest's own deliverable, which is the dialogue tree, the garlic
-- stashed in the pipe (a real [oplocu,garlicpipe] use_on, not a click), and
-- the giant carp caught live at the pipe fishing spot.
--
-- Every t.player.goto_tile below is the SAME absolute tile the content
-- pack's own fishbmp_* debugproc cheats (quest_fishingcompo.rs2) teleport a
-- tester to for that exact stage (0_MM_MM_LL_LL decodes to
-- mapsquareX*64+localX, mapsquareY*64+localY) -- cross-checked against the
-- live *.spawn rows for tunnel_dwarf/bonzo/the two fish-spot npcs, not
-- guessed.
--
-- Fishing level 10 is Quest Helper's own stated requirement to even be
-- offered the quest (mountain_dwarf.rs2's tunnel_dwarf_friends branch
-- returns before the accept prompt under it) -- a prerequisite skill, set
-- once in setup, never touched again.

return {
    id = "fishingcompo",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give red_vine_worm 1", -- no gather interaction wired for this in the pack; a brought-along prerequisite
        "::give garlic 1", -- same: no kr_seers_table2 handler anywhere in server/scripts
        "::give fishing_rod 1",
        "::give coins 10", -- Bonzo's 5gp entrance fee
        "::setlevel fishing 10", -- mountain_dwarf.rs2's own accept-prompt gate
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "fishingcompo",
            constants = {
                not_started = 0,
                started = 1,
                in_comp = 2,
                garlic_comp = 3,
                won_comp = 4,
                complete = 5,
            },
            row = "quest_fishingcontest", -- configs/all.dbrow:2839
            display = "Fishing Contest", -- all.dbrow:2847 displayname, confirmed against the live row
            points = 1, -- configs/quest_fishingcompo.constant: ^fishingcompo_questpoints = 1
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------ start the quest
        -- mountain_dwarf.rs2 [label,tunnel_dwarf_talk] -> not_started ->
        -- tunnel_dwarf_start. The "friend" branch (stairs -> why not? -> if
        -- you were my friend -> let's be friends -> how do I earn that) is
        -- the ONLY branch mountain_dwarf.rs2 wires to the accept prompt --
        -- "I was just stopping to say hello!" and "I didn't want to
        -- anyway."/"I'm bigger than you." are all dead ends with no
        -- @tunnel_dwarf_accept underneath them.
        t.exec("goto-dwarf-start", t.player.goto_tile, 2877, 3483, 0)
        t.exec("dwarf.greet", t.player.talk_to, "tunnel_dwarf")
        local d1r, d1d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_stairs_choice", d1r, d1d)
        t.shot("dwarf-greet-options")

        t.exec("dwarf.choose_stairs", t.chat.choose, "I was just wondering what was down those stairs?")
        local d2r, d2d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_whynot_choice", d2r, d2d)

        t.exec("dwarf.choose_whynot", t.chat.choose, "Why not?")
        local d3r, d3d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_friend_choice", d3r, d3d)

        t.exec("dwarf.choose_friend", t.chat.choose, "If you were my friend I wouldn't mind.")
        local d4r, d4d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_letsbefriends_choice", d4r, d4d)

        t.exec("dwarf.choose_letsbefriends", t.chat.choose, "Well, let's be friends!")
        local d5r, d5d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_howearn_choice", d5r, d5d)

        t.exec("dwarf.choose_howearn", t.chat.choose, "And how am I meant to do that?")
        local d6r, d6d = t.chat.drain({ stop_at = "options" })
        t.expect("dwarf.drain_to_accept_choice", d6r, d6d)
        t.shot("dwarf-accept-options")

        t.exec("dwarf.choose_yes_start", t.chat.choose, "Yes.")
        local d7r, d7d = t.chat.drain({ stop_at = "none" })
        t.expect("dwarf.drain_accept_close", d7r, d7d)

        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local pass_result, pass_detail = t.inv.await("fishing_competition_pass", 1, 10)
        t.step("dwarf.pass_granted", pass_result == "ok" and "PASS" or "FAIL",
            "fishing_competition_pass await 1 -> " .. tostring(pass_result) .. " " .. tostring(pass_detail))

        -- ------------------------------------------------ pay Bonzo, enter
        -- bonzo.rs2 [label,bonzo_talk] -> started & paid=0 -> bonzo_waiting_entry.
        -- goto_tile lands past the fishinggateclosedr gate the same way the
        -- content pack's own fishbmp_gate cheat does -- a straight teleport,
        -- no click_loc on the gate.
        t.exec("goto-bonzo-pay", t.player.goto_tile, 2641, 3437, 0)
        t.exec("bonzo.greet", t.player.talk_to, "bonzo")
        local b1r, b1d = t.chat.drain({ stop_at = "options" })
        t.expect("bonzo.drain_to_enter_choice", b1r, b1d)
        t.shot("bonzo-entry-options")

        t.exec("bonzo.choose_enter", t.chat.choose, "I'll enter the competition please.")
        local b2r, b2d = t.chat.drain({ stop_at = "none" })
        t.expect("bonzo.drain_entry_close", b2r, b2d)
        t.shot("bonzo-spot-assigned")

        t.expect("quest.stage.in_comp", t.quest.expect_stage("in_comp"))
        -- Trap 24: a click verb's `ok` is the server's SENTENCE, not the
        -- container update -- the coins delta lands a tick behind the
        -- dialogue's own close, so poll for it rather than reading a bare
        -- t.inv.count on the line below.
        local coins_result, coins_left = t.inv.await("coins", 5, 10)
        t.check("bonzo.fee_paid", coins_result == "ok",
            "coins after the 5gp fee await 5 -> " .. tostring(coins_result) .. " " .. tostring(coins_left))

        -- ------------------------------------------------ scare off the stranger
        -- [oplocu,garlicpipe] switches on last_useitem = garlic -> @stash_garlic
        -- -- a real use_on, never a plain click_loc (that hits [oploc1,garlicpipe]
        -- instead, "The pipe smells of sewage."), and it is the ONLY way
        -- into %fishingcompo=garlic_comp: hemenster_fishing.rs2's own
        -- [label,hemenster_catch] only ever adds raw_giant_carp when
        -- npc_type=0_41_53_sinisterfishspot, the spot @stash_garlic hands the
        -- player once the Sinister Stranger is smoked out of it -- the
        -- willow-tree spot (compofishspot) the player starts the competition
        -- at never yields anything better than a sardine, garlic or not.
        --
        -- RETRY after 8cd829faf: this used to be unreachable -- use_on's
        -- far-side retry landed a real menu row on the garlicpipe wall but
        -- did not re-arm the held item first, so the press spent its arming
        -- on the wall's ordinary Examine row and use_on could only ever
        -- answer `refused` (pointer.lua's old "NO RE-ARM BEFORE A RETRY
        -- PRESS" banner). pointer.lua now re-arms through api_drive.inv_arm
        -- before every retry press -- far-side loop included -- so the stash
        -- lands. Assert that now, and drive the quest on to completion.
        local pipe_target, pipe_lookup = t.player.by_symbol("loc", "garlicpipe")
        t.step("garlicpipe.lookup", pipe_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc, garlicpipe) -> " .. tostring(pipe_lookup)
                .. " id=" .. tostring(pipe_target and pipe_target.id)
                .. " match=" .. tostring(pipe_target and pipe_target.match))

        t.exec("goto-garlicpipe", t.player.goto_tile, 2638, 3445, 0)
        t.exec("garlicpipe.stash", t.player.use_on, "garlic", pipe_target)

        -- quest_fishingcompo_gate.rs2 [label,stash_garlic]: with
        -- %fishingcompo_paid=1 already (Bonzo's fee, above), the stash also
        -- fires the Sinister Stranger's and Bonzo's reaction lines in the
        -- SAME tick, both npc-first pages, then Bonzo's own mesbox -- three
        -- pages, no player choice in any of them.
        t.exec("garlicpipe.stranger_reacts", t.chat.play, {
            "npc:Arrgh! WHAT is that GHASTLY smell",
            "npc:Hmm. You'd better go and take the area by the pipes then.",
            "mesbox:Your fishing competition spot is now beside the pipes.",
            "end",
        })
        t.expect("quest.stage.garlic_comp", t.quest.expect_stage("garlic_comp"))
        t.shot("garlicpipe-stashed")

        -- ------------------------------------------------ catch the giant carp
        -- hemenster_fishing.rs2 [opnpc1,0_41_53_sinisterfishspot] ->
        -- attempt_fish_hemenster -> both "my spot" guards miss
        -- (%fishingcompo=garlic_comp, not in_comp; npc_type is the sinister
        -- spot, not compofishspot) -> ~get_hemenster_bait picks the carried
        -- red_vine_worm -> [label,hemenster_catch] grants raw_giant_carp
        -- because npc_type=0_41_53_sinisterfishspot. Tile is the same one
        -- fishbmp_carp's own debugproc teleports a tester to (0_41_53_13_52
        -- decodes to 2637,3444).
        t.exec("goto-fishspot", t.player.goto_tile, 2637, 3444, 0)
        t.exec("fish.carp", t.player.talk_to, "0_41_53_sinisterfishspot")
        local carp_result, carp_detail = t.inv.await("raw_giant_carp", 1, 10)
        t.step("fish.carp_landed", carp_result == "ok" and "PASS" or "FAIL",
            "raw_giant_carp await 1 -> " .. tostring(carp_result) .. " " .. tostring(carp_detail))
        t.shot("carp-caught")

        -- ------------------------------------------------ hand the carp to Bonzo
        -- bonzo.rs2 [label,bonzo_talk] -> %fishingcompo=garlic_comp ->
        -- @bonzo_howdoing -> raw_giant_carp carried -> the "enough to win"
        -- choice -> @bonzo_handover_catch: mesbox, inv_del the carp,
        -- %fishingcompo=won_comp, the trophy granted. Same tile as the
        -- earlier bonzo.fee_paid step (fishbmp_handin's own 0_41_53_17_45).
        t.exec("goto-bonzo-handin", t.player.goto_tile, 2641, 3437, 0)
        t.exec("bonzo.howdoing", t.player.talk_to, "bonzo")
        t.exec("bonzo.accept_carp", t.chat.play, {
            "npc:So how are you doing so far?",
            "options",
            "choose:I have this big fish. Is it enough to win?",
            "player:I have this big fish. Is it enough to win?",
            "mesbox:You hand over your catch.",
            "npc:We have a new winner!",
            "mesbox:You are given the Hemenster fishing trophy!",
            "end",
        })
        t.expect("quest.stage.won_comp", t.quest.expect_stage("won_comp"))
        local trophy_result, trophy_detail = t.inv.await("hemenster_fishing_trophy", 1, 10)
        t.step("bonzo.trophy_granted", trophy_result == "ok" and "PASS" or "FAIL",
            "hemenster_fishing_trophy await 1 -> " .. tostring(trophy_result) .. " " .. tostring(trophy_detail))
        t.shot("trophy-won")

        -- ------------------------------------------------ hand the trophy to the
        -- tunnel dwarf -- this is the quest's own completion trigger
        -- mountain_dwarf.rs2 [label,tunnel_dwarf_won]: the trophy is handed
        -- over and queue(fishingcompo_quest_complete, 0, 0) is queued --
        -- quest_fishingcompo.rs2's [queue,...] flips %fishingcompo=complete
        -- and runs stat_advance(fishing, 24370) (the scroll's "2,437 Fishing
        -- XP"). Snapshot before the hand-in, the way cooks_assistant's shape
        -- calls for.
        local skill_snap_result, skill_snap = t.skill.snapshot()
        t.step("quest.skill_snapshot", skill_snap_result == "ok" and "PASS" or "FAIL",
            "snapshot before hand-in -> " .. tostring(skill_snap_result))

        t.exec("goto-dwarf-handin", t.player.goto_tile, 2877, 3483, 0)
        t.exec("dwarf.trophy_handin", t.player.talk_to, "tunnel_dwarf")
        t.exec("dwarf.won_dialogue", t.chat.play, {
            "npc:Have you won yet?",
            "player:Yes I have!",
            "npc:Well done! So where is the trophy?",
            "player:I have it right here!",
            "mesbox:You give the trophy to the dwarf.",
            "npc:That's a mighty fine trophy!",
            "npc:You can use the tunnel under White Wolf Mountain",
            "player:Thanks!",
            "end",
        })
        -- Completion is asynchronous (section 8): the queued proc above runs
        -- behind this dialogue's own close, not inside it.
        t.ticks(3)

        t.quest.expect_complete()
        -- Reviewer rejection (batch sonnet-b9): expect_gain was called bare,
        -- with no t.check/t.expect around it, so its result was discarded
        -- and the ledger ended at quest.journal with zero reward.* rows.
        -- quest_fishingcompo_quest_complete grants stat_advance(fishing,
        -- 24370) -- the "2,437 Fishing XP" the completion scroll advertises
        -- -- so record it.
        t.check("reward.fishing", t.skill.expect_gain("fishing", 2437, skill_snap))
        t.finish(0)
    end,
}
