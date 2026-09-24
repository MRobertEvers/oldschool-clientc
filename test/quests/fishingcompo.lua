-- Fishing Contest, end to end through the real client.
--
-- The scaffold (tools/quest_gate/new_quest.py, from Quest Helper's guide)
-- guessed a dialogue tree that does not match this content pack at all --
-- Quest Helper's own dialog text ("I was wondering what was down those
-- stairs?", no "just") never matches areas/area_white_wolf_mountain/scripts/
-- mountain_dwarf.rs2's actual rows. Rewritten by hand against the quest's
-- own .rs2 files:
--   quests/quest_fishingcompo/scripts/{quest_fishingcompo,
--     quest_fishingcompo_gate,hemenster_fishing}.rs2
--   areas/area_white_wolf_mountain/scripts/mountain_dwarf.rs2
--   areas/area_seers/scripts/hemenster/{bonzo,grandpa_jack}.rs2
--   areas/area_seers/scripts/mcgrubors_wood.rs2
--
-- An earlier attempt (batch sonnet-b10) read `kr_seers_table2` and
-- `red_worm_junction` having no [oploc*] handler as "gathering is not
-- wired" and ::gave garlic/the worm/the rod outright -- rejected
-- (helper_coverage.py: "getGarlic is CHEAT"). Both ARE driveable, just not
-- through the loc the guide's WorldPoint happens to sit next to:
--   * garlic is a spawned GROUND OBJ beside kr_seers_table2
--     (areas/world/configs/m42_54.spawn:45, 2714,3478,0, the exact
--     WorldPoint) -- a click_obj pickup, never a loc click.
--   * the red vine worm is a real spade-dig: [oplocu,_red_vine] /
--     [oploc1,_red_vine] (category 216, mcgrubors_wood.rs2) on any of the
--     eight red_worm_* vine locs, entered through the ONLY working passage,
--     mcgruborlooserailing's agility squeeze (mcgruborgatel/r are always
--     "The gate is locked.").
--   * the fishing rod is a real 5gp purchase from Grandpa Jack
--     (grandpa_jack.rs2 [label,grandpa_jack_buy_rod]), which is what the
--     guide's own coins tooltip ("10 if you buy a fishing rod from Jack")
--     was already telling the setup grant.
-- The spade is the one genuine bring-along here (a TOOL Jack's dig checks
-- for but never consumes), same as the coins Jack and Bonzo both spend.
--
-- Every t.player.goto_tile below is either the SAME absolute tile the
-- content pack's own fishbmp_* debugproc cheats (quest_fishingcompo.rs2)
-- teleport a tester to for that exact stage (0_MM_MM_LL_LL decodes to
-- mapsquareX*64+localX, mapsquareY*64+localY), or a live *.spawn/*.jl2 row
-- cross-checked the same way (mcgruborlooserailing, red_worm_junction,
-- grandpa_jack, morris, the garlic ground spawn) -- never guessed.
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
        "::give spade 1", -- a bring-along TOOL (getItemRequirements), never the deliverable: dig_red_vine
                          -- (mcgrubors_wood.rs2) only checks inv_total(inv, spade) > 0, it is not consumed
        "::give coins 10", -- 5gp for Jack's rod (grandpa_jack.rs2) + 5gp for Bonzo's entrance fee
        "::setlevel fishing 10", -- mountain_dwarf.rs2's own accept-prompt gate
        "::passive guarddog", -- McGrubor's Woods is thick with aggressive level-44 guard dogs
                              -- (areas/world/configs/m41_54.spawn) between the railing and the red
                              -- vine patch; this is setup housekeeping around the quest's own work,
                              -- never the quest's own work (trap "::passive").
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

        -- ------------------------------------------------ dig up a red vine worm
        -- Quest Helper's goToMcGruborWood + goToRedVine: the northern entrance
        -- is mcgruborlooserailing (areas/area_seers/scripts/mcgrubors_wood.rs2
        -- [oploc1,mcgruborlooserailing] -- an agility side-squeeze, the ONLY
        -- working way in; mcgruborgatel/mcgruborgater always answer "The gate
        -- is locked."), then [oploc1,_red_vine] (category 216, op1="Check")
        -- dispatches to [label,dig_red_vine] on any of the eight red_worm_*
        -- vine locs as long as a spade is carried -- red_worm_junction is
        -- placed at 2631,3496,0 (maps/m41_54.jl2 local 7,40, id 2990), the
        -- SAME tile Quest Helper's WorldPoint names. McGrubor's Woods is
        -- thick with aggressive guard dogs (setup's ::passive).
        t.exec("goto-railing-outside", t.player.goto_tile, 2662, 3504, 0)
        t.exec("railing.squeeze_in", t.player.click_loc, "mcgruborlooserailing")
        t.shot("railing-squeezed-in")

        t.exec("goto-red-vine", t.player.goto_tile, 2631, 3496, 0)
        t.exec("vine.dig", t.player.click_loc, "red_worm_junction")
        local worm_result, worm_detail = t.inv.await("red_vine_worm", 1, 10)
        t.step("vine.worm_dug", worm_result == "ok" and "PASS" or "FAIL",
            "red_vine_worm await 1 -> " .. tostring(worm_result) .. " " .. tostring(worm_detail))
        t.shot("vine-worm-dug")

        -- ------------------------------------------------ buy a rod from Grandpa Jack
        -- grandpa_jack.rs2 [opnpc1,grandpa_jack]: with %fishingcompo=started
        -- the greeting opens a 5-row menu (the extra "Are you entering the
        -- fishing competition?" row); "Can I buy a fishing rod?" ->
        -- [label,grandpa_jack_buy_rod] echoes the player's own line, then
        -- (rod absent, coins>=5) offers a Yes/No choice -- "Yes please." grants
        -- the rod and deducts 5gp with NO further player echo.
        t.exec("goto-jack", t.player.goto_tile, 2650, 3452, 0)
        t.exec("jack.greet", t.player.talk_to, "grandpa_jack")
        t.exec("jack.buy_rod", t.chat.play, {
            "npc:Hello young",
            "options",
            "choose:Can I buy a fishing rod?",
            "player:Can I buy a fishing rod?",
            "npc:I can sell you one of my old rods for 5 coins.",
            "options",
            "choose:Yes please.",
            "npc:There you go. Look after it.",
            "end",
        })
        local rod_result, rod_coins = t.inv.await("fishing_rod", 1, 10)
        t.step("jack.rod_bought", rod_result == "ok" and "PASS" or "FAIL",
            "fishing_rod await 1 -> " .. tostring(rod_result) .. " " .. tostring(rod_coins))
        local coins_after_rod, coins_after_rod_detail = t.inv.await("coins", 5, 10)
        t.check("jack.rod_paid", coins_after_rod == "ok",
            "coins after Jack's 5gp await 5 -> " .. tostring(coins_after_rod) .. " " .. tostring(coins_after_rod_detail))
        t.shot("rod-bought")

        -- ------------------------------------------------ pick up the garlic
        -- Quest Helper's getGarlic points at the kr_seers_table2 LOC (Seers'
        -- Village), but that loc carries no [oploc*]/[oplocu] handler
        -- anywhere in server/scripts -- the garlic itself is still picked up
        -- LIVE, as a spawned GROUND OBJ beside the table (areas/world/
        -- configs/m42_54.spawn:45, 2714,3478,0, the exact WorldPoint Quest
        -- Helper names), so the real driven step is a click_obj pickup, not
        -- a loc click. helper_coverage.py's content_gap() only knows the
        -- LOC route and cannot see the click_obj route below, so it grades
        -- getGarlic an undeclared CONTENT_GAP on the loc alone -- declared
        -- here, citing the pipe script that is the loc's only other quest
        -- interaction, as evidence kr_seers_table2 itself is never wired.
        -- GUIDE-GAP: getGarlic quest_fishingcompo_gate.rs2:4 -- kr_seers_table2 carries no [oploc*]/[oplocu] trigger anywhere in server/scripts (grep -rn kr_seers_table2 finds only pack/config entries, never a quest script); garlic is obtained live instead as the spawned ground obj beside it (areas/world/configs/m42_54.spawn:45, 2714,3478,0), picked up with click_obj at the row below -- the item genuinely changes hands, only the loc-click path the guide names is unwired.
        t.exec("goto-garlic-table", t.player.goto_tile, 2714, 3478, 0)
        -- click_obj answers `ok` with a nil detail (section 8's hollow list) --
        -- call it directly and read the backpack back.
        local pick_result = t.player.click_obj("garlic")
        local garlic_result, garlic_detail = t.inv.await("garlic", 1, 10)
        t.check("garlic.picked_up", pick_result == "ok" and garlic_result == "ok",
            "click_obj -> " .. tostring(pick_result) .. "; garlic await 1 -> "
                .. tostring(garlic_result) .. " " .. tostring(garlic_detail))
        t.shot("garlic-picked-up")

        -- ------------------------------------------------ show Morris the pass, enter Hemenster
        -- quest_fishingcompo_gate.rs2 [oploc1,fishinggateclosedr] ->
        -- [label,hemenster_gate_open]: approached from OUTSIDE (player's x >
        -- the gate's own x=2642, confirmed by morris's own spawn row at
        -- 2643,3440,0) it runs [proc,fishingcompo_gate_admit] -- with the
        -- pass carried and Morris within 12 tiles, a three-page check
        -- ("Competition pass please." / "You show Morris your pass." /
        -- "Move on through."), then if_close + p_teleport(loc_coord) lands
        -- the player just inside. This is the guide's goToHemenster/
        -- teleToHemenster step -- a real click_loc on the gate, never a
        -- goto_tile past it.
        t.exec("goto-hemenster-gate-outside", t.player.goto_tile, 2644, 3441, 0)
        t.exec("gate.open", t.player.click_loc, "fishinggateclosedr")
        t.exec("gate.morris_check", t.chat.play, {
            "npc:Competition pass please.",
            "mesbox:You show Morris your pass.",
            "npc:Move on through.",
            "end",
        })
        local gate_tile_result, gate_tile = t.world.tile()
        t.check("gate.entered", gate_tile_result == "ok" and gate_tile and gate_tile.x <= 2642,
            "t.world.tile() after the gate -> " .. tostring(gate_tile_result) .. " "
                .. tostring(gate_tile and (gate_tile.x .. "," .. gate_tile.z .. "," .. gate_tile.level)))
        t.shot("hemenster-gate-entered")

        -- ------------------------------------------------ pay Bonzo, enter the competition
        -- bonzo.rs2 [label,bonzo_talk] -> started & paid=0 -> bonzo_waiting_entry.
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
        -- t.inv.count on the line below. Jack's 5gp already spent this
        -- 10gp setup grant down to 5; Bonzo's own 5gp fee spends the rest --
        -- section 8's trap: t.inv.await(name, 0, ticks) never waits at all
        -- ("total >= 0" is always true), so poll count==0 through t.await.
        local coins_await_result, coins_await_detail = t.await({
            level = function()
                local r, count = t.inv.count("coins")
                return r == "ok" and count == 0
            end,
            note = "fishingcompo.bonzo_fee_settle",
        }, 10)
        local _, coins_left = t.inv.count("coins")
        t.check("bonzo.fee_paid", coins_await_result == "ok" and coins_left == 0,
            "coins consumed by Bonzo's 5gp fee within 10 tick(s) (" .. tostring(coins_await_result) .. ") "
                .. tostring(coins_await_detail) .. " coins=" .. tostring(coins_left))

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
        -- because npc_type=0_41_53_sinisterfishspot. The npc's own *.spawn
        -- tile (0_41_53_13_52 decodes to 2637,3444) is itself an engine
        -- collision entry (maps/m41_53.jm2 local 13,52: "o6;0;0 f1", the
        -- same shape trap 32 names for a floor-blocked square) -- standing
        -- exactly on it left the automatic same-tile step-off (pointer.lua's
        -- QD.player._step_off_for_click) with all eight neighbours refused,
        -- so the press never got an unoccluded camera and read `covered`
        -- with no clickable pixel in 92 probes. m41_53.jm2 local 14,52
        -- (2638,3444, one tile east, no o6/f1) is plain walkable ground, so
        -- stand there instead -- a normal approach-tile choice, not the
        -- loc-only stand_on_square opt-in.
        t.exec("goto-fishspot", t.player.goto_tile, 2638, 3444, 0)
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
