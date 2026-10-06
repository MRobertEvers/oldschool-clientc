-- The Golem -- re-authored against the 2026-09-23 parity pass
-- (quest_golem/scripts/golem.rs2, golem_portal.rs2, areas/varrock/scripts/
-- curator.rs2, and the golem_elissa branch spliced into
-- quest_deserttreasureii/scripts/deserttreasureii.rs2).
--
-- Door rule (b65 re-drive): every crossing is clicked, nothing is goto'd
-- past. The route, each leg checked with the sample tools' reach.py/comp.py
-- (doors closed):
--   * The run starts at the fixture's tile in Lumbridge (3206,3233); the old
--     `::golem` setup teleport (into the desert, onto the stairs loc) is gone
--     -- a fresh account's golem varbits are already 0.
--   * The desert is behind the Shantay Pass doorway shantay_pass_henge_doorway
--     3302,3116 (its only way in on foot: reach.py 3304,3112 -> 3488,3089 REACH,
--     from the north side the walk needs the doorway). Going south: a pass bought
--     from Shantay and the doorway's pages (shantay_pass.rs2 [oploc1,
--     shantay_pass_henge_doorway] -> [queue,shantay_pass_enter]); going north it
--     only pushes the player 3 tiles (a player at or south of the loc).
--     Both by t.player.cross_gate. Uzer, the black mushrooms and the phoenix
--     are all on the desert side (REACH closed-doors among them).
--   * Curator Haig's hall in the Varrock museum is open to the street (the
--     west doorway fai_varrock_museum_door_inactive_l/r 3253,3448-3449 has no
--     op: reach.py 3250,3448 -> 3256,3447 REACH len=7). Its first floor is
--     climbed by fai_varrock_woodenstairs_castle (maplink_0_51_53_2_59_up:
--     3266,3451 -> 3266,3455,1) and left by fai_varrock_stairs_top
--     (maplink_1_51_53_2_63_down).
--   * The Dig Site is walled off from Varrock by the vm_fencegate double gate
--     3296,3428-3429 and from the south by the Varrock members' gate
--     fai_varrock_member_gater 3312,3332 (reach.py finds no other way); the
--     Exam Centre room (x 3348-3367 z 3332-3348) by qip_digsite_poshdoor
--     3352,3337. Each is pressed on its crossing (t.player.pass_door).
--   * The Uzer ruin under the city is reached by golem_insidestairs_top and
--     left by golem_insidestairs_base, each following its maplink row
--     (maplink_0_54_48_35_18_down: arch tile 3491,3090 -> 2721,4886,0;
--     maplink_0_42_76_33_22_up: -> 3491,3090,0): same level, another map
--     frame, so t.player.climb with same_level naming the row.
--   * The throne room is plane 2 of the same ruin (TheGolem.java throneRoom
--     2709..2731 x 4879..4919, plane 2), entered by the golem_portal door
--     (golem_portal.rs2 [label,golem_enter_demon_lair] -> ^golem_demon_lair
--     = golem.constant 2_42_76_32_20, 2720,4884,2) and left by
--     golem_demon_portal (its golem arm -> 2721,4911,0).
--
-- Rewards per the live ~quest_complete_rewards call: 1000 Thieving XP,
-- 1000 Crafting XP (^golem_craft_xp / ^golem_thieve_xp = 10000 tenths), one
-- quest point, and a carpet-ride service (not assertable).

local function in_throne_room(tile)
    return type(tile) == "table" and tile.level == 2 and tile.x >= 2709 and tile.x <= 2731
        and tile.z >= 4879 and tile.z <= 4919
end

return {
    id = "golem",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give softclay 4", -- ^golem_clay_needed = 4
        "::give vial_empty 1", -- crushes the black mushroom into ink
        "::give pestle_and_mortar 1", -- tool for the mushroom grind
        "::give papyrus 1", -- written on with the phoenix-quill pen
        "::give hammer 1", -- prerequisite tool for the demon throne's gems (not the quest's own deliverable)
        "::give coins 10", -- two Shantay passes, 5 gp each (shantay.rs2)
        "::give water_skin4 2", -- desert heat (desert_heat.rs2: a drink per 150 ticks in desert_zones)
        "::setlevel crafting 20", -- ^golem_craft_req
        "::setlevel thieving 25", -- ^golem_thieve_req
    },

    run = function(t)
        local function count(sym)
            local r, n = t.inv.count(sym)
            if r ~= "ok" or type(n) ~= "number" then
                return -1
            end
            return n
        end
        local function reading()
            local r, tile = t.world.tile()
            if r ~= "ok" or type(tile) ~= "table" then
                return "tile " .. tostring(r)
            end
            return string.format("%d,%d,%d", tile.x, tile.z, tile.level)
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varb346_golem_a", -- a varbit (all.varbit.compack:347), bound the same as a varp
            constants = {
                not_started = 0,
                offered = 1,
                repaired = 2,
                tasked = 3,
                portal_open = 6,
                need_program = 7,
                head_open = 8,
                complete = 10,
            },
            row = "quest_golem", -- all.dbrow.compack:66, the literal symbol ~quest_complete_rewards uses
            display = "The Golem",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", "quest.bind golem_a -> " .. tostring(bind_result) .. " " .. tostring(bind_detail))
        t.ticks(3)
        t.check("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Buy a Shantay pass and go south through the doorway (shantay.rs2: 5 gp;
        -- shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway]). The first trip
        -- reads the poster and gets the disclaimer; with the disclaimer held the
        -- second trip goes straight to the pass check.
        local function into_desert(prefix, first)
            t.exec("goto-" .. prefix .. ".shantay", t.player.goto_tile, 3304, 3123, 0)
            local coins0, pass0 = count("coins"), count("shantay_pass")
            t.exec(prefix .. ".buyPass", t.player.talk_to, "shantay", 1)
            local lines = first and { "npc:Hello effendi, I am Shantay.", "npc:I see you're new." }
                or { "npc:Hello again friend." }
            lines[#lines + 1] = "choose:I want to buy a shantay pass for 5 gold coins."
            lines[#lines + 1] = "player:I want to buy a shantay pass for"
            lines[#lines + 1] = "mesbox:You purchase a Shantay Pass."
            t.exec(prefix .. ".buyPass-dialog", t.chat.play, lines)
            local pass_await = t.inv.await("shantay_pass", 1, 5)
            t.check(prefix .. ".buyPass-paid", pass_await == "ok" and count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
                "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins") .. " (want -5, shantay.rs2)")
            local door_chat = {}
            if first then
                door_chat = { "mesbox:There is a large poster on the wall", "mesbox:The Desert is a VERY Dangerous place",
                    "mesbox:That seems pretty scary!", "choose:Yeah, that poster doesn't scare me!" }
            end
            door_chat[#door_chat + 1] = "npc:Can I see your Shantay Desert Pass"
            door_chat[#door_chat + 1] = "mesbox:You hand over a Shantay Pass."
            door_chat[#door_chat + 1] = "player:Sure, here you go!"
            if first then
                door_chat[#door_chat + 1] = "npc:Here, have a disclaimer"
            end
            t.exec(prefix .. ".shantayDoorway", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
                near = { 3304, 3118 }, far_ok = function(tile) return tile.z <= 3115 end,
                far_desc = "south of the Shantay Pass doorway, z <= 3115", chat = door_chat })
            t.check(prefix .. ".shantayDoorway.passHandedOver", count("shantay_pass") == pass0,
                "shantay_pass " .. count("shantay_pass") .. " after the doorway (handed over), disclaimer " .. count("thshantaydisc"))
        end
        -- North through the doorway: free from the south (shantay_pass.rs2: a
        -- player at or south of the loc is pushed 3 tiles north).
        local function out_of_desert(prefix)
            t.exec("goto-" .. prefix .. ".shantayNorth", t.player.goto_tile, 3304, 3113, 0)
            t.exec(prefix .. ".shantayDoorwayNorth", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
                near = { 3304, 3114 }, far_ok = function(tile) return tile.z > 3116 end, far_desc = "north of the Shantay doorway, z > 3116" })
        end

        -- talkToGolem: the golem stands at its spawn (m54_48.spawn:15, 3488,3090,0).
        into_desert("talkToGolem", true)
        t.exec("goto-talkToGolem", t.player.goto_tile, 3486, 3090, 0)
        t.exec("talkToGolem-offer", t.player.talk_to, "golem_golem", 1)
        t.exec("talkToGolem-offer-dialog", t.chat.play, {
            "npc:Damage... severe... task... incomplete...",
            "choose:Shall I try to repair you?",
            "player:Shall I try to repair you?",
            "npc:Repairs... needed...",
        })
        t.check("quest.stage.offered", t.quest.expect_stage("offered"))

        -- useClay: 4 soft clay, one use each ([opnpcu,golem_golem] ->
        -- [label,golem_clay_repair]: each use takes one clay, answers with a
        -- ~mesbox and raises %golem_clay; the fourth flips golem_a to repaired).
        -- The target is re-resolved before each use (the golem's model changes).
        for i = 1, 4 do
            local target, target_result = t.player.by_symbol("npc", "golem_golem")
            t.step("golem.by_symbol-" .. i, target_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(target_result))
            local clay_before = count("softclay")
            t.exec("repairClay" .. i, t.player.use_on, "softclay", target)
            local continue_result = t.chat.continue_()
            t.check("repairClay" .. i .. "-continue", continue_result == "ok", "chat.continue_ after repairClay" .. i .. " -> " .. tostring(continue_result))
            t.ticks(1)
            local clay_result, clay_value = t.var.server("varb348_golem_clay")
            t.check("repairClay" .. i .. ".clay", clay_result == "ok" and clay_value == i and count("softclay") == clay_before - 1,
                "golem_clay (varbit) = " .. tostring(clay_value) .. " (want " .. i .. "), softclay " .. clay_before .. " -> " .. count("softclay"))
        end
        t.check("quest.stage.repaired", t.quest.expect_stage("repaired"))

        -- Talk again: golem_a=repaired runs straight through to tasked.
        t.exec("talkToGolem-task", t.player.talk_to, "golem_golem", 1)
        t.exec("talkToGolem-task-dialog", t.chat.play, {
            "npc:Damage repaired...",
            "npc:Thank you. My body and mind are fully healed.",
            "npc:Now I must complete my task by defeating the great enemy.",
            "player:What enemy?",
            "npc:A great demon. It broke through from its dimension to attack the city.",
            "npc:The golem army was created to fight it. Many were destroyed, but we drove the demon back!",
            "npc:The demon is still wounded. You must open the portal so that I can strike the final blow and complete my task.",
        })
        t.check("quest.stage.tasked", t.quest.expect_stage("tasked"))

        -- pickUpLetter: a ground obj (m54_48.spawn:19, 3479,3092,0), walked to.
        t.exec("walk-letter", t.player.walk_to, 3480, 3092)
        local letter_before = count("golem_letter")
        local letter_click_result, letter_click_detail = t.player.click_obj("golem_letter")
        t.inv.await("golem_letter", 1, 10)
        local letter_after = count("golem_letter")
        t.step("pickUpLetter", (letter_click_result == "ok" and letter_after > letter_before) and "PASS" or "FAIL",
            string.format("click_obj golem_letter -> %s (%s), count %s -> %s",
                tostring(letter_click_result), tostring(letter_click_detail), tostring(letter_before), tostring(letter_after)))
        -- [opheld1,golem_letter]: sets %golem_b=1, mesbox about the notes/statuette.
        t.exec("readLetter", t.player.inv_op, "golem_letter", 1)
        local close_letter_result = t.chat.continue_()
        t.check("readLetter-continue", close_letter_result == "ok", "chat.continue_ after readLetter -> " .. tostring(close_letter_result))
        local letter_b_result, letter_b_value = t.var.server("varb347_golem_b")
        t.check("readLetter.golem_b", letter_b_result == "ok" and letter_b_value == 1, "golem_b (varbit) = " .. tostring(letter_b_value) .. " (want 1)")

        -- pickBlackMushroom: golem_black_mushrooms (3495,3088, a solid loc by the
        -- stair house; shadowstorm_dye.rs2 [oploc1,golem_black_mushrooms], no
        -- stage gate) pressed from the open ground; click_loc walks to it.
        t.exec("walk-mushroom", t.player.walk_to, 3496, 3087)
        local mushroom_before = count("golem_mushroom")
        t.exec("pickMushroom", t.player.click_loc, "golem_black_mushrooms", 1)
        local mushroom_await_result = t.inv.await("golem_mushroom", 1, 10)
        t.check("pickMushroom-sync", mushroom_await_result == "ok" and count("golem_mushroom") == mushroom_before + 1,
            "golem_mushroom " .. mushroom_before .. " -> " .. count("golem_mushroom") .. " (inv.await " .. tostring(mushroom_await_result) .. ")")
        -- grindMushroom: the pestle used on the mushroom with a vial carried
        -- ([opheldu,golem_mushroom] last_useitem=pestle_and_mortar).
        t.exec("grindMushroom", t.player.use_item_on_item, "pestle_and_mortar", "golem_mushroom")
        local close_grind_result = t.chat.continue_()
        t.check("grindMushroom-continue", close_grind_result == "ok", "chat.continue_ after grindMushroom -> " .. tostring(close_grind_result))
        local ink_await_result = t.inv.await("golem_ink", 1, 10)
        t.check("grindMushroom.ink", ink_await_result == "ok" and count("golem_ink") == 1 and count("golem_mushroom") == 0 and count("vial_empty") == 0,
            "golem_ink " .. count("golem_ink") .. ", golem_mushroom " .. count("golem_mushroom") .. ", vial_empty " .. count("vial_empty")
                .. " (inv.await " .. tostring(ink_await_result) .. ")")

        -- stealFeather: the desert phoenix north-west of Uzer ([opnpc3,golem_phoenix]).
        t.exec("goto-stealFeather", t.player.goto_tile, 3414, 3155, 0)
        t.exec("stealFeather", t.player.talk_to, "golem_phoenix", 3)
        local close_feather_result = t.chat.continue_()
        t.check("stealFeather-continue", close_feather_result == "ok", "chat.continue_ after stealFeather -> " .. tostring(close_feather_result))
        local feather_await_result = t.inv.await("golem_phoenixfeather", 1, 10)
        t.check("stealFeather.feather", feather_await_result == "ok" and count("golem_phoenixfeather") == 1,
            "golem_phoenixfeather " .. count("golem_phoenixfeather") .. " (inv.await " .. tostring(feather_await_result) .. ")")

        -- useFeatherOnInk: the feather used on the ink ([opheldu,golem_ink]
        -- last_useitem=golem_phoenixfeather).
        t.exec("useFeatherOnInk", t.player.use_item_on_item, "golem_phoenixfeather", "golem_ink")
        local close_dip_result = t.chat.continue_()
        t.check("useFeatherOnInk-continue", close_dip_result == "ok", "chat.continue_ after useFeatherOnInk -> " .. tostring(close_dip_result))
        local pen_await_result = t.inv.await("golem_pen", 1, 10)
        t.check("useFeatherOnInk.pen", pen_await_result == "ok" and count("golem_pen") == 1 and count("golem_phoenixfeather") == 0,
            "golem_pen " .. count("golem_pen") .. ", golem_phoenixfeather " .. count("golem_phoenixfeather") .. " (inv.await " .. tostring(pen_await_result) .. ")")

        -- talkToCurator: north out of the desert, to the museum's open hall.
        out_of_desert("talkToCurator")
        t.exec("goto-talkToCurator", t.player.goto_tile, 3250, 3448, 0)
        t.exec("walk-talkToCurator.museumHall", t.player.walk_to, 3255, 3448)
        t.exec("talkToCurator", t.player.talk_to, "curator", 1)
        t.exec("talkToCurator-dialog", t.chat.play, {
            "npc:Welcome to the museum of Varrock.",
            "player:I'm looking for a statuette recovered from the city of Uzer.",
            "npc:Ah yes, a wonderful piece. It's in the display case upstairs.",
            "player:Could I take it, please?",
            "npc:This museum never lets go of its treasures.",
            "choose:I want to open a portal to the lair of an elder-demon.",
            "player:I want to open a portal to the lair of an elder-demon.",
            "npc:Good heavens! I'd never let you do such a dangerous thing.",
        })
        local curator_talk_result, curator_talk_value = t.var.server("varp7150_golem_talked_curator")
        t.check("talkToCurator.talked", curator_talk_result == "ok" and curator_talk_value == 1,
            "golem_talked_curator (varp, curator.rs2:127) = " .. tostring(curator_talk_value))

        t.exec("pickpocketCurator", t.player.talk_to, "curator", 3)
        local close4_result = t.chat.continue_()
        t.check("pickpocketCurator-continue", close4_result == "ok", "chat.continue_ after pickpocketCurator -> " .. tostring(close4_result))
        local key_await_result = t.inv.await("golem_statuettekey", 1, 10)
        t.check("pickpocketCurator-sync", key_await_result == "ok" and count("golem_statuettekey") == 1,
            "golem_statuettekey " .. count("golem_statuettekey") .. " (inv.await " .. tostring(key_await_result) .. ")")

        -- goUpInMuseum / openCabinet: the stairs to the first floor, then
        -- [oploc2,vm_timeline_terracotta_statue_multi] "Open" on the case
        -- (3257,3453,1), which takes the key and gives the statuette.
        t.exec("goUpInMuseum", t.player.climb, { loc = "fai_varrock_woodenstairs_castle", op = 1, op_name = "Climb-up",
            at = { 3266, 3452, 0 }, src = { 3266, 3451 }, dest = { 3266, 3455, 1 } })
        t.exec("openCabinet", t.player.click_loc, "vm_timeline_terracotta_statue_multi", 2, { at = { 3257, 3453, 1 } })
        local statuette_await_result = t.inv.await("golem_statuette", 1, 10)
        t.check("openCabinet.statuette", statuette_await_result == "ok" and count("golem_statuette") == 1 and count("golem_statuettekey") == 0,
            "golem_statuette " .. count("golem_statuette") .. ", golem_statuettekey " .. count("golem_statuettekey") .. " (inv.await " .. tostring(statuette_await_result) .. ")")
        t.exec("goDownInMuseum", t.player.climb, { loc = "fai_varrock_stairs_top", op = 1, op_name = "Climb-down",
            at = { 3266, 3453, 1 }, src = { 3266, 3455 }, dest = { 3266, 3451, 0 } })
        t.exec("walk-leaveMuseum", t.player.walk_route, { { 3258, 3448 }, { 3250, 3448 } })

        -- talkToElissa: through the Dig Site fence gate (the open leaf is the
        -- same symbol one tile over: doors_selfstage.loc:146).
        t.exec("goto-talkToElissa.fenceGate", t.player.goto_tile, 3293, 3429, 0)
        t.exec("talkToElissa.fenceGateEast", t.player.pass_door, { closed = "vm_fencegate_r", open = "vm_fencegate_r",
            at = { 3296, 3429, 0 }, near = { 3294, 3429 }, far = { 3297, 3429 } })
        t.exec("goto-talkToElissa", t.player.goto_tile, 3374, 3428, 0)
        t.exec("talkToElissa", t.player.talk_to, "golem_elissa", 1)
        t.exec("talkToElissa-dialog", t.chat.play, {
            "player:I found a letter in the desert with your name on it. It mentions notes by someone called Varmen.",
            "npc:Varmen? Oh, that old fossil! He used to work here. I think some of his old papers are still on the bookshelves in the Exam Centre.",
        })
        local elissa_b_result, elissa_b_value = t.var.server("varb347_golem_b")
        t.check("golem.b-elissa", elissa_b_result == "ok" and elissa_b_value == 2, "golem_b (varbit) = " .. tostring(elissa_b_value))

        -- searchBookcase: in the Exam Centre room, through its south door.
        t.exec("goto-searchBookcase", t.player.goto_tile, 3350, 3341, 0)
        t.exec("searchBookcase.examDoorIn", t.player.pass_door, { closed = "qip_digsite_poshdoor", open = "qip_digsite_poshdoor_open",
            at = { 3352, 3337, 0 }, near = { 3352, 3339 }, far = { 3352, 3336 } })
        t.exec("searchBookcase", t.player.click_loc, "golem_bookcase", 1)
        local close5_result = t.chat.continue_()
        t.check("searchBookcase-continue", close5_result == "ok", "chat.continue_ after searchBookcase -> " .. tostring(close5_result))
        local notes_await_result = t.inv.await("golem_notes", 1, 10)
        t.check("searchBookcase-sync", notes_await_result == "ok" and count("golem_notes") == 1,
            "golem_notes " .. count("golem_notes") .. " (inv.await " .. tostring(notes_await_result) .. ")")
        t.exec("readNotes", t.player.inv_op, "golem_notes", 1)
        local close_notes_result = t.chat.continue_()
        t.check("readNotes-continue", close_notes_result == "ok", "chat.continue_ after readNotes -> " .. tostring(close_notes_result))
        local notes_b_result, notes_b_value = t.var.server("varb347_golem_b")
        t.check("readNotes.golem_b", notes_b_result == "ok" and notes_b_value == 3, "golem_b (varbit) = " .. tostring(notes_b_value) .. " (want 3)")

        -- useQuillOnPapyrus: the pen used on the papyrus ([opheldu,papyrus]
        -- last_useitem=golem_pen, needs %golem_b>=3).
        t.exec("useQuillOnPapyrus", t.player.use_item_on_item, "golem_pen", "papyrus")
        local close_write_result = t.chat.continue_()
        t.check("useQuillOnPapyrus-continue", close_write_result == "ok", "chat.continue_ after useQuillOnPapyrus -> " .. tostring(close_write_result))
        local program_await_result = t.inv.await("golem_program", 1, 10)
        t.check("useQuillOnPapyrus.program", program_await_result == "ok" and count("golem_program") == 1 and count("papyrus") == 0,
            "golem_program " .. count("golem_program") .. ", papyrus " .. count("papyrus") .. " (inv.await " .. tostring(program_await_result) .. ")")
        t.exec("searchBookcase.examDoorOut", t.player.pass_door, { closed = "qip_digsite_poshdoor", open = "qip_digsite_poshdoor_open",
            at = { 3352, 3337, 0 }, near = { 3352, 3336 }, far = { 3352, 3339 } })

        -- enterRuin: out of the Dig Site by the members' gate, south through
        -- the Shantay Pass again, to the stair house in Uzer.
        t.exec("goto-enterRuin.memberGate", t.player.goto_tile, 3314, 3332, 0)
        t.exec("enterRuin.memberGateOut", t.player.pass_door, { closed = "fai_varrock_member_gater", open = "fai_varrock_member_gater_open",
            at = { 3312, 3332, 0 }, near = { 3313, 3332 }, far = { 3311, 3332 } })
        into_desert("enterRuin", false)
        t.exec("goto-enterRuin", t.player.goto_tile, 3489, 3090, 0)
        -- The stairs are pressed from the arch tile 3491,3090, the only open
        -- tile beside them (reach.py: every other tile of the stair house is
        -- solid; the press aims on the drawn model since seam b66,
        -- conformance seam.stairwell_pressed_on_its_model_from_the_arch).
        -- Climb-down follows its maplink row to 2721,4886,0 (same level,
        -- another map frame).
        t.exec("enterRuin", t.player.climb, { loc = "golem_insidestairs_top", op = 1, op_name = "Climb-down",
            at = { 3492, 3090, 0 }, src = { 3491, 3090 }, dest = { 2721, 4886, 0 }, slack = 0,
            same_level = "maplink_0_54_48_35_18_down" })

        -- useStatuette: the museum statuette into its own empty alcove (only
        -- golem_statuetted carries [oplocu,...]; golem_portal.rs2).
        local statuetted_result, statuetted_target = t.world.loc_near("golem_statuetted", 25)
        t.check("world.statuette-alcove", statuetted_result == "ok",
            "world.loc_near golem_statuetted -> " .. tostring(statuetted_result)
            .. " at " .. tostring(statuetted_target and statuetted_target.tile_x) .. "," .. tostring(statuetted_target and statuetted_target.tile_z) .. "," .. tostring(statuetted_target and statuetted_target.level))
        if statuetted_target ~= nil then
            t.exec("walk-statuette", t.player.walk_near, statuetted_target, 20, 1)
            t.ticks(1)
        end
        t.exec("placeStatuette", t.player.use_on, "golem_statuette", statuetted_target)
        t.ticks(1)
        local statusd1_result, statusd1_value = t.var.server("varb352_golem_statuettestatusd")
        t.check("placeStatuette.inserted", statusd1_result == "ok" and statusd1_value == 1 and count("golem_statuette") == 0,
            "golem_statuettestatusd (varbit) = " .. tostring(statusd1_value) .. ", golem_statuette carried = " .. count("golem_statuette"))

        -- turnStatue: A, B and D to face the door (quest-helper turnedStatue
        -- A=1, B=1, C=0 -- its rest state, never touched -- D=2). Each turn
        -- handler runs ~golem_check_statuettes_aligned (golem_portal.rs2).
        t.exec("turnStatuetteA", t.player.click_loc, "golem_statuettea", 1)
        t.exec("turnStatuetteB", t.player.click_loc, "golem_statuetteb", 1)
        t.exec("turnStatuetteD", t.player.click_loc, "golem_statuetted", 1)
        t.ticks(1)
        local statusa_result, statusa_value = t.var.server("varb349_golem_statuettestatusa")
        t.check("golem.statuettea-turned", statusa_result == "ok" and statusa_value == 1, "golem_statuettestatusa (varbit) = " .. tostring(statusa_value))
        local statusb_result, statusb_value = t.var.server("varb350_golem_statuettestatusb")
        t.check("golem.statuetteb-turned", statusb_result == "ok" and statusb_value == 1, "golem_statuettestatusb (varbit) = " .. tostring(statusb_value))
        local statusd2_result, statusd2_value = t.var.server("varb352_golem_statuettestatusd")
        t.check("golem.statuetted-turned", statusd2_result == "ok" and statusd2_value == 2, "golem_statuettestatusd (varbit) = " .. tostring(statusd2_value))
        t.check("quest.stage.portal_open", t.quest.expect_stage("portal_open"))

        -- enterThroneRoom: the golem_portal door (2720,4912,0; a multiloc whose
        -- child at golem_a>=6 is golem_demon_door_always_open) pressed from the
        -- tile south of it. The throne room is plane 2 of this ruin
        -- (TheGolem.java throneRoom 2709..2731 x 4879..4919, plane 2; the throne
        -- golem_demon_throne 2719,4913,2, the exit golem_demon_portal 2719,4883,2).
        t.exec("walk-enterThroneRoom", t.player.walk_to, 2720, 4911)
        -- The door is a level change (0 -> 2, golem_portal.rs2
        -- [label,golem_enter_demon_lair] p_teleport(^golem_demon_lair =
        -- 2_42_76_32_20)), graded by climb on the exact landing.
        t.exec("enterThroneRoom", t.player.climb, { loc = "golem_portal", op = 1, op_name = "Enter",
            at = { 2720, 4912, 0 }, src = { 2720, 4911 }, dest = { 2720, 4884, 2 }, slack = 0 })
        local seen_check_result, seen_check_value = t.var.server("varb356_golem_seen_underground")
        t.check("enterThroneRoom.seen", seen_check_result == "ok" and seen_check_value == 1,
            "golem_seen_underground (varbit, set in [label,golem_enter_demon_lair]) = " .. tostring(seen_check_value))
        local _, portal_tile = t.world.tile()
        t.check("enterThroneRoom.landed", in_throne_room(portal_tile) and portal_tile.x == 2720 and portal_tile.z == 4884,
            "at " .. reading() .. " (want 2720,4884,2, the throne room floor)")

        -- prizeGems: the hammer used on the demon's throne
        -- ([oplocu,golem_demon_throne] grants a ruby and golem_golemkey).
        local throne_result, throne_target = t.world.loc_near("golem_demon_throne", 40, { level = "here" })
        t.check("world.demon-throne", throne_result == "ok",
            "world.loc_near golem_demon_throne -> " .. tostring(throne_result)
            .. " at " .. tostring(throne_target and throne_target.tile_x) .. "," .. tostring(throne_target and throne_target.tile_z) .. "," .. tostring(throne_target and throne_target.level))
        if throne_target ~= nil then
            t.exec("walk-throne", t.player.walk_near, throne_target, 30, 1)
            t.ticks(1)
        end
        t.exec("prizeGems", t.player.use_on, "hammer", throne_target)
        local golemkey_await_result = t.inv.await("golem_golemkey", 1, 10)
        t.check("prizeGems.golemkey", golemkey_await_result == "ok" and count("golem_golemkey") == 1,
            "golem_golemkey " .. count("golem_golemkey") .. ", ruby " .. count("ruby") .. " (inv.await " .. tostring(golemkey_await_result) .. ")")

        -- leaveThroneRoom: golem_demon_portal (2719,4883,2) back to the ruin
        -- (maplink dest 0_42_76_33_47 = 2721,4911,0).
        t.exec("leaveThroneRoom", t.player.climb, { loc = "golem_demon_portal", op = 1, op_name = "Enter",
            at = { 2719, 4883, 2 }, dest = { 2721, 4911, 0 }, slack = 0 })
        -- leaveRuin: up golem_insidestairs_base (shadowstorm_dye.rs2 handler).
        t.exec("walk-leaveRuin", t.player.walk_to, 2721, 4886)
        t.exec("leaveRuin", t.player.climb, { loc = "golem_insidestairs_base", op = 1, op_name = "Climb-up",
            at = { 2721, 4884, 0 }, dest = { 3491, 3090, 0 }, slack = 0,
            same_level = "maplink_0_42_76_33_22_up" })

        -- talkToGolemAfterPortal: golem_a=portal_open & seen_underground=1.
        t.exec("walk-talkToGolemAfterPortal", t.player.walk_to, 3489, 3090)
        t.exec("talkToGolem-demondead", t.player.talk_to, "golem_golem", 1)
        t.exec("talkToGolem-demondead-dialog", t.chat.play, {
            "npc:My task is incomplete. You must open the portal so I can defeat the great demon.",
            "player:It's ok, the demon is dead!",
            "npc:The demon must be defeated...",
            "player:No, you don't understand. I saw the demon's skeleton. It must have died of its wounds.",
            "npc:Demon must be defeated! Task incomplete.",
        })
        t.check("quest.stage.need_program", t.quest.expect_stage("need_program"))

        -- useImplementOnGolem: the golem key ([opnpcu,golem_golem]
        -- last_useitem=golem_golemkey). A plain mes(), no page.
        local golem_target_key, golem_target_key_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-key", golem_target_key_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_key_result))
        t.exec("insertKey", t.player.use_on, "golem_golemkey", golem_target_key)
        t.ticks(1)
        local head_result, head_value = t.var.server("varb353_golem_head_open")
        t.check("insertKey.headOpen", head_result == "ok" and head_value == 1 and count("golem_golemkey") == 1,
            "golem_head_open (varbit, golem_portal.rs2 'You insert the key...') = " .. tostring(head_value)
                .. ", golem_golemkey kept = " .. count("golem_golemkey"))
        t.check("quest.stage.head_open", t.quest.expect_stage("head_open"))

        -- Reward snapshot BEFORE the hand-in.
        local skill_snapshot_result, skill_snapshot = t.skill.snapshot()
        t.step("skill.snapshot", skill_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot -> " .. tostring(skill_snapshot_result))

        -- useProgramOnGolem: [opnpcu,golem_golem] last_useitem=golem_program.
        local golem_target_program, golem_target_program_result = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol-program", golem_target_program_result == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(golem_target_program_result))
        t.exec("handInProgram", t.player.use_on, "golem_program", golem_target_program)
        t.exec("handInProgram-dialog", t.chat.play, {
            "npc:New instructions... Updating program...",
            "npc:Task complete!",
            "npc:Thank you. Now my mind is at rest.",
        })
        t.ticks(3)
        t.check("handInProgram.consumed", count("golem_program") == 0, "golem_program carried = " .. count("golem_program"))

        t.quest.expect_complete()
        t.check("reward.crafting", t.skill.expect_gain("crafting", 1000, skill_snapshot))
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1000, skill_snapshot))
        t.finish(0)
        return
    end,
}
