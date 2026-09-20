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
        local coins_result, coins_left = t.inv.count("coins")
        t.check("bonzo.fee_paid", coins_result == "ok" and coins_left == 5,
            "coins after the 5gp fee = " .. tostring(coins_left) .. " (read " .. tostring(coins_result) .. ")")

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
        local pipe_target, pipe_lookup = t.player.by_symbol("loc", "garlicpipe")
        t.step("garlicpipe.lookup", pipe_target ~= nil and "PASS" or "FAIL",
            "by_symbol(loc, garlicpipe) -> " .. tostring(pipe_lookup)
                .. " id=" .. tostring(pipe_target and pipe_target.id)
                .. " match=" .. tostring(pipe_target and pipe_target.match))

        -- garlicpipe (configs/all.loc:354, shape1=4 "straight wall") sits at
        -- 2638,3446 exactly, right against the two-hole wall south of the
        -- Hemenster competition pond. Tried and recorded here, not
        -- guessed once: player ON its own tile (0 away, the fishbmp_garlic
        -- debugproc's own SERVER-SIDE p_oploc coordinate, which needs no
        -- client pixel at all), 1 tile short and 2 tiles short, approaching
        -- from x=2638 AND x=2639 -- covering both live element ids the scene
        -- resolves this symbol to nearby (536875057 and 536875058) -- every
        -- one of the five answers `covered` from all five of click_minimenu's
        -- own camera poses: "pickset held=false, menu has no row for it".
        -- This is mortton.lua's shades_experimentshelf seam exactly
        -- ("the shelf sits tight against the hut's inner wall... occludes
        -- the pixel every pose lands on"), but that one had a way out --
        -- drive.op, the numbered-op bypass -- and this one does not:
        -- DrivePointer_WorldOp (src/plugin/torirs_plugin_drive.h:647) only
        -- fabricates a NUMBERED op row (op1 "Search" here, which answers
        -- fine and is not the quest's own trigger), never a held-item
        -- USEHELD_ON* row -- that wildcard only exists inside
        -- click_minimenu's own "select" path (pointer.lua:560-564), which is
        -- exactly the path that is covered. There is no verb in this driver
        -- that reaches an [oplocu] handler without a clean world pick.
        t.exec("goto-garlicpipe", t.player.goto_tile, 2638, 3445, 0)
        local stash_result, stash_detail = t.player.use_on("garlic", pipe_target)
        t.shot("garlicpipe-covered")
        t.check("garlicpipe.stash_covered", stash_result ~= "ok",
            "use_on(garlic, garlicpipe) -> " .. tostring(stash_result) .. " " .. tostring(stash_detail)
                .. " -- confirmed covered at 0/1/2 tiles and both live element ids across separate runs")

        t.blocked("quests/quest_fishingcompo/scripts/quest_fishingcompo_gate.rs2:4-9 " ..
            "[oplocu,garlicpipe] / script/plugins/quest_driver/pointer.lua:1739 use_on: " ..
            "using garlic on the garlicpipe wall (2638,3446,0) never lands a menu row " ..
            "from any client position -- click_minimenu's five camera poses all answer " ..
            "`covered` (\"pickset held=false, menu has no row for it\") whether the " ..
            "player stands on the wall's own tile, 1 tile short, or 2 tiles short, and " ..
            "for both live element ids (536875057, 536875058) the scene resolves the " ..
            "symbol to nearby. Stashing the garlic is the only way to move the Sinister " ..
            "Stranger off the sinisterfishspot (quest_fishingcompo_gate.rs2 " ..
            "[label,stash_garlic]), and hemenster_fishing.rs2's own [label,hemenster_catch] " ..
            "only ever grants raw_giant_carp at THAT spot -- so this seam blocks the whole " ..
            "rest of the quest, not just one step. A numbered-op bypass exists " ..
            "(t.drive.op, src/plugin/torirs_plugin_drive.h:647 DrivePointer_WorldOp, used " ..
            "for mortton.lua's identically-occluded shades_experimentshelf) but only " ..
            "fabricates a declared op row (op1 Search here) -- never a held-item " ..
            "USEHELD_ON* row, which only exists inside the covered click_minimenu " ..
            "\"select\" path (pointer.lua:560-564) -- so there is no verb in this driver " ..
            "that reaches an [oplocu] handler without a clean world pick.")
        return
    end,
}
