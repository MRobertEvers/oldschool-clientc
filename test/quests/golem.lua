-- The Golem, along RSProx index 4473 (rev 242, 2026-11-06): the recorder's own
-- route and order, every page and every stage write graded against it
-- (docs/rsprox-analysis/quests/the_golem.md; quest_golem/scripts/golem.rs2,
-- golem_portal.rs2, areas/varrock/scripts/curator.rs2 and vm_timeline.rs2).
--
-- Travel is the recorder's: the necklace of passage's Eagles' Eyrie (Rub,
-- third entry; [254], [1247], [1812]), the Digsite teleport scroll ([646],
-- [1766]) and a Varrock teleport tablet ([992]). The ruin under Uzer is the
-- stairs' maplink (3491,3090 <-> 2721,4886), the lair the portal's.

local function in_box(tile, x0, z0, x1, z1, level)
    return type(tile) == "table" and tile.level == level and tile.x >= x0 and tile.x <= x1
        and tile.z >= z0 and tile.z <= z1
end

return {
    id = "golem",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give softclay 4", -- the four repairs ([516..549])
        "::give papyrus 1", -- the program ([1822])
        "::give pestle_and_mortar 1", -- the dye ([1698])
        "::give vial_empty 1", -- the dye's vial
        "::give necklace_of_passage_5 1", -- the recorder's way into the desert
        "::give teleportscroll_digsite 2",
        "::give poh_tablet_varrockteleport 1",
        "::give water_skin4 3", -- desert heat
        "::setlevel crafting 20",
        "::setlevel thieving 25",
    },

    run = function(t)
        t.ticklog.start()
        local function count(sym)
            local r, n = t.inv.count(sym)
            if r ~= "ok" or type(n) ~= "number" then
                return -1
            end
            return n
        end
        local function varbit(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or nil
        end
        local function tile()
            local _, tt = t.world.tile()
            return tt
        end
        local function tile_text()
            local tt = tile()
            if type(tt) ~= "table" then
                return "?"
            end
            return string.format("%d,%d,%d", tt.x, tt.z, tt.level)
        end
        local function expect_var(name, sym, want)
            local v = varbit(sym)
            t.check(name, v == want, sym .. " = " .. tostring(v) .. " (want " .. tostring(want) .. ")")
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varb346_golem_a",
            constants = {
                not_started = 0, started = 1, repaired = 2, asked_curator = 3,
                statuette_placed = 4, door_open = 5, seen_lair = 6, told_golem = 7, complete = 10,
            },
            row = "quest_golem",
            display = "The Golem",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", "quest.bind golem_a -> " .. tostring(bind_result) .. " " .. tostring(bind_detail))
        t.ticks(3)
        t.check("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Eagles' Eyrie: Rub, third entry, the next charge in the slot.
        local function to_eyrie(name, have)
            t.ticklog.mark("leg." .. name)
            t.exec(name .. ".rub", t.player.inv_op, have, 4, { subop = 3 })
            t.ticks(6)
            local tt = tile()
            t.check(name .. ".landed", in_box(tt, 3403, 3156, 3407, 3160, 0), "at " .. tile_text() .. " (want 3405,3158 r2, 4473 [257])")
        end
        local function ruin_down(name)
            t.exec("goto-" .. name, t.player.goto_tile, 3490, 3090, 0)
            t.exec(name, t.player.climb, { loc = "golem_insidestairs_top", op = 1, op_name = "Climb-down",
                at = { 3492, 3090, 0 }, src = { 3491, 3090 }, dest = { 2721, 4886, 0 }, slack = 0,
                same_level = "maplink_0_54_48_35_18_down" })
        end
        local function ruin_up(name)
            t.exec("walk-" .. name, t.player.walk_to, 2721, 4886)
            t.exec(name, t.player.climb, { loc = "golem_insidestairs_base", op = 1, op_name = "Climb-up",
                at = { 2721, 4884, 0 }, dest = { 3491, 3090, 0 }, slack = 0,
                same_level = "maplink_0_42_76_33_22_up" })
        end
        local function golem()
            local target, r = t.player.by_symbol("npc", "golem_golem")
            t.step("golem.by_symbol", r == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(r))
            return target
        end
        local function talk_golem(name, lines)
            t.exec("talk-" .. name, t.player.talk_to, "golem_golem", 1)
            t.exec("talk-" .. name .. "-pages", t.chat.play, lines)
        end

        -- === The letter, the mushroom, the temple ([320..460]) ==========
        to_eyrie("eyrie1", "necklace_of_passage_5")
        t.exec("walk-letter", t.player.goto_tile, 3479, 3091, 0)
        t.exec("takeLetter", t.player.click_obj, "golem_letter")
        t.check("takeLetter.held", t.inv.await("golem_letter", 1, 10) == "ok", "golem_letter " .. count("golem_letter"))
        -- [338]: golem_b 1 with the scroll.
        t.exec("readLetter", t.player.inv_op, "golem_letter", 1)
        t.exec("readLetter.open", t.ui.await_open, "messagescroll", 5)
        t.exec("readLetter.row3", t.ui.expect_text, "messagescroll:messcroll3", "Dearest Varmen,", 5)
        expect_var("readLetter.golem_b", "varb347_golem_b", 1)
        t.ui.close_modal()
        t.check("readLetter.closed", t.ui.await_close("messagescroll", 5) == "ok", "messagescroll closed")

        t.exec("walk-mushroom", t.player.goto_tile, 3494, 3088, 0)
        t.exec("pickMushroom", t.player.click_loc, "golem_black_mushrooms", 1)
        t.exec("pickMushroom-box", t.chat.play, { "objbox:You pick a mushroom." })
        t.check("pickMushroom.held", count("golem_mushroom") == 1, "golem_mushroom " .. count("golem_mushroom"))

        -- [386..388]: golem_seen_underground with the first descent.
        ruin_down("ruinDown1")
        expect_var("ruinDown1.seen", "varb356_golem_seen_underground", 1)
        -- [396..407]: B, A, C turned left at stage 0.
        t.exec("turnB", t.player.click_loc, "golem_statuetteb", 1)
        t.ticks(1)
        t.exec("turnA", t.player.click_loc, "golem_statuettea", 1)
        t.ticks(1)
        t.exec("turnC", t.player.click_loc, "golem_statuettec", 1)
        t.ticks(1)
        expect_var("turnA.left", "varb349_golem_statuettestatusa", 1)
        expect_var("turnB.left", "varb350_golem_statuettestatusb", 1)
        expect_var("turnC.left", "varb351_golem_statuettestatusc", 1)
        -- [413..417]: the shut door.
        t.exec("shutDoor", t.player.click_loc, "golem_portal", 1, { at = { 2720, 4912, 0 } })
        t.expect("shutDoor.line", t.msg.expect("You can't find any way to open the door."))
        -- [429..436]: the strange implement, a ground spawn.
        t.exec("walk-implement", t.player.goto_tile, 2713, 4912, 0)
        t.exec("takeImplement", t.player.click_obj, "golem_golemkey")
        t.check("takeImplement.held", t.inv.await("golem_golemkey", 1, 10) == "ok", "golem_golemkey " .. count("golem_golemkey"))
        ruin_up("ruinUp1")

        -- === The start and the repair ([471..567]) ======================
        t.ticklog.mark("leg.start")
        talk_golem("start", {
            "npc:Damage... severe... task... incomplete...",
            "choose:Yes.",
            "player:Shall I try to repair you?",
            "npc:Repairs... needed...",
        })
        t.check("quest.stage.started", t.quest.expect_stage("started"))
        t.expect("start.line", t.msg.expect("You've started a new quest:"))
        talk_golem("started", { "npc:Damage... severe..." })
        local clay_lines = {
            "objbox:You apply some clay to the golem's wounds.",
            "objbox:You fix the golem's legs.",
            "objbox:The golem is nearly whole.",
            "objbox:You repair the golem with a final piece of clay.",
        }
        for i = 1, 4 do
            t.exec("clay" .. i, t.player.use_on, "softclay", golem())
            local pages = { clay_lines[i] }
            if i == 4 then
                pages = { clay_lines[4], "npc:Damage repaired...", "npc:Thank you. My body and mind are fully healed.",
                    "npc:Now I must complete my task by defeating the great enemy.", "player:What enemy?",
                    "npc:A great demon. It broke through from its dimension to attack the city.",
                    "npc:The golem army was created to fight it. Many were destroyed, but we drove the demon back!",
                    "npc:The demon is still wounded. You must open the portal so that I can strike the final blow and complete my task." }
            end
            t.exec("clay" .. i .. "-pages", t.chat.play, pages)
            expect_var("clay" .. i .. ".count", "varb348_golem_clay", i)
        end
        t.check("clay.used", count("softclay") == 0, "softclay " .. count("softclay"))
        t.check("quest.stage.repaired", t.quest.expect_stage("repaired"))
        talk_golem("portalHow", {
            "npc:My task is incomplete. You must open the portal so I can defeat the great demon.",
            "choose:How do I open the portal?",
            "player:How do I open the portal?",
            "npc:The four statuettes in the temple must be turned to the correct pattern.",
            "npc:I do not know the pattern. Golems are not permitted to open the portal.",
        })

        -- === Elissa and Varmen's notes ([646..989]) =====================
        t.ticklog.mark("leg.digsite")
        t.exec("digsiteScroll", t.player.inv_op, "teleportscroll_digsite", 1)
        t.ticks(5)
        t.check("digsiteScroll.landed", in_box(tile(), 3324, 3411, 3326, 3413, 0), "at " .. tile_text() .. " (4473 [649] 3324,3412)")
        t.exec("goto-elissa", t.player.goto_tile, 3378, 3428, 0)
        t.exec("talk-elissa", t.player.talk_to, "golem_elissa", 1)
        t.exec("talk-elissa-pages", t.chat.play, {
            "npc:Hello there.",
            "choose:I found a letter in the desert with your name on.",
            "player:I found a letter in the desert with your name on.",
            "npc:Ah, so you've found the ruins of Uzer.",
            "npc:I wrote that letter to my late husband when he was exploring there.",
            "npc:That was a great city as well, but the museum could only fund one excavation",
            "npc:If you're interested in his expedition, the notes he made are in the library in the Exam Centre.",
        })
        expect_var("elissa.golem_b", "varb347_golem_b", 2)

        t.exec("goto-examCentre", t.player.goto_tile, 3356, 3344, 0)
        t.exec("examDoorIn", t.player.pass_door, { closed = "qip_digsite_poshdoor", open = "qip_digsite_poshdoor_open",
            at = { 3357, 3344, 0 }, near = { 3356, 3344 }, far = { 3358, 3344 } })
        -- [903..906]: the tall shelf answers like the low ones.
        t.exec("goto-highShelf", t.player.goto_tile, 3366, 3336, 0)
        t.exec("highShelf", t.player.click_loc, "qip_digsite_bookcase_high", 1)
        t.ticks(2) -- the box a tick after the line ([905..906])
        t.exec("highShelf-box", t.chat.play, { "mesbox:The label on this shelf reads 'Earth Sciences'" })
        -- [931..934]
        t.exec("goto-bookcase", t.player.goto_tile, 3366, 3333, 0)
        t.exec("searchBookcase", t.player.click_loc, "golem_bookcase", 1)
        t.ticks(3) -- the box two ticks after the line ([932..934])
        t.exec("searchBookcase-box", t.chat.play, { "objbox:You find Varmen's expedition notes." })
        t.check("notes.held", count("golem_notes") == 1, "golem_notes " .. count("golem_notes"))
        -- [937]: golem_b 3 as the book opens.
        t.exec("readNotes", t.player.inv_op, "golem_notes", 1)
        t.exec("readNotes.open", t.ui.await_open, "book", 10)
        t.exec("readNotes.row1", t.ui.expect_text, "book:page_left_text_1", "Septober 19:", 5)
        expect_var("readNotes.golem_b", "varb347_golem_b", 3)
        t.ui.close_modal()
        t.check("readNotes.closed", t.ui.await_close("book", 5) == "ok", "book closed")

        -- === The museum ([992..1227]) ====================================
        t.ticklog.mark("leg.museum")
        t.exec("varrockTablet", t.player.inv_op, "poh_tablet_varrockteleport", 1)
        t.ticks(6)
        t.check("varrockTablet.landed", in_box(tile(), 3212, 3423, 3214, 3425, 0), "at " .. tile_text() .. " (4473 [996] 3212,3423)")
        t.exec("goto-curator", t.player.goto_tile, 3256, 3447, 0)
        t.exec("talk-curator", t.player.talk_to, "curator", 1)
        t.exec("talk-curator-pages", t.chat.play, {
            "npc:Welcome to the museum of Varrock.",
            "choose:I'm looking for a statuette recovered from the city of Uzer.",
            "player:I'm looking for a statuette recovered from the city of Uzer.",
            "npc:Ah yes, a very impressive artefact. The people of that city were excellent sculptors.",
            "npc:It's in the display case upstairs.",
            "player:No, I need to take it away with me.",
            "npc:What do you want it for?",
            "choose:Well, I, er, just want it.",
            "player:Well, I, er, just want it.",
            "npc:Well, you can't have it! This museum never lets go of its treasures.",
        })
        t.check("quest.stage.asked_curator", t.quest.expect_stage("asked_curator"))
        t.exec("pickpocket", t.player.talk_to, "curator", 3)
        t.exec("pickpocket-box", t.chat.play, { "objbox:You pick the curator's pocket and steal a key." })
        t.check("pickpocket.key", count("golem_statuettekey") == 1, "golem_statuettekey " .. count("golem_statuettekey"))
        t.exec("museumUp", t.player.climb, { loc = "fai_varrock_woodenstairs_castle", op = 1, op_name = "Climb-up",
            at = { 3266, 3452, 0 }, src = { 3266, 3451 }, dest = { 3266, 3455, 1 } })
        -- [1185..1193]: Study the case, then Close.
        t.exec("studyCase", t.player.click_loc, "vm_timeline_terracotta_statue", 1, { at = { 3257, 3453, 1 } })
        t.exec("studyCase.open", t.ui.await_open, "vm_timeline", 10)
        t.exec("studyCase.num", t.ui.expect_text, "vm_timeline:display_num", "30", 5)
        local _, close_button = t.ui.widget("vm_timeline:com_1")
        t.exec("studyCase.close", t.ui.invoke, close_button, 1)
        t.check("studyCase.closed", t.ui.await_close("vm_timeline", 5) == "ok", "vm_timeline closed by Close (com_1)")
        -- [1207..1210]: Open; the key stays.
        t.exec("openCase", t.player.click_loc, "vm_timeline_terracotta_statue", 2, { at = { 3257, 3453, 1 } })
        t.exec("openCase-box", t.chat.play, { "objbox:You open the cabinet and retrieve the statuette." })
        t.check("openCase.statuette", count("golem_statuette") == 1 and count("golem_statuettekey") == 1,
            "golem_statuette " .. count("golem_statuette") .. ", key " .. count("golem_statuettekey") .. " (the key stays, 4473 [1208])")
        expect_var("openCase.retrieved", "varb355_golem_retrieved_statuette", 1)

        -- === The door, the golem, the lair ([1247..1555]) ================
        to_eyrie("eyrie2", "necklace_of_passage_4")
        ruin_down("ruinDown2")
        local alcove_result, alcove = t.world.loc_near("golem_statuetted", 25)
        t.check("alcove.found", alcove_result == "ok", "world.loc_near golem_statuetted -> " .. tostring(alcove_result))
        t.exec("placeStatuette", t.player.use_on, "golem_statuette", alcove)
        t.ticks(2)
        expect_var("placeStatuette.d", "varb352_golem_statuettestatusd", 1)
        t.check("quest.stage.statuette_placed", t.quest.expect_stage("statuette_placed"))
        t.exec("turnD", t.player.click_loc, "golem_statuetted", 1)
        t.ticks(1)
        expect_var("turnD.right", "varb352_golem_statuettestatusd", 2)
        t.exec("turnC2", t.player.click_loc, "golem_statuettec", 1)
        t.expect("turnC2.door", t.msg.expect("The door grinds open."))
        t.check("quest.stage.door_open", t.quest.expect_stage("door_open"))
        ruin_up("ruinUp2")
        talk_golem("opened", {
            "npc:My task is incomplete. You must open the portal so I can defeat the great demon.",
            "player:I opened the portal.",
            "npc:Golems cannot pass through the portal. But the demon will soon emerge. I must ready myself for combat!",
        })
        ruin_down("ruinDown3")
        t.ticklog.mark("leg.lair")
        t.exec("walk-portal", t.player.goto_tile, 2721, 4911, 0)
        t.exec("enterPortal", t.player.click_loc, "golem_portal", 1, { at = { 2720, 4912, 0 } })
        t.ticks(3)
        t.check("enterPortal.landed", in_box(tile(), 2720, 4884, 2720, 4884, 2), "at " .. tile_text() .. " (4473 [1450] 2720,4884,2)")
        t.check("quest.stage.seen_lair", t.quest.expect_stage("seen_lair"))
        t.exec("leaveLair", t.player.click_loc, "golem_demon_portal", 1, { at = { 2719, 4883, 2 } })
        t.ticks(4)
        t.check("leaveLair.landed", in_box(tile(), 2721, 4911, 2721, 4911, 0), "at " .. tile_text() .. " (4473 [1506] 2721,4911,0)")
        ruin_up("ruinUp3")
        talk_golem("demonDead", {
            "npc:My task is incomplete. You must open the portal so I can defeat the great demon.",
            "player:It's okay, the demon is dead!",
            "npc:The demon must be defeated...",
            "player:No, you don't understand. I saw the demon's skeleton. It must have died of its wounds.",
            "npc:Demon must be defeated! Task incomplete.",
        })
        t.check("quest.stage.told_golem", t.quest.expect_stage("told_golem"))

        -- === The dye, the feather, the pen, the program ([1698..1826]) ===
        t.ticklog.mark("leg.pen")
        t.exec("crushMushroom", t.player.use_item_on_item, "golem_mushroom", "pestle_and_mortar")
        t.exec("crushMushroom-box", t.chat.play, { "objbox:You crush the mushroom and create some black dye." })
        t.check("crushMushroom.dye", count("golem_ink") == 1 and count("vial_empty") == 0 and count("pestle_and_mortar") == 1,
            "golem_ink " .. count("golem_ink") .. ", vial " .. count("vial_empty") .. ", pestle " .. count("pestle_and_mortar"))
        to_eyrie("eyrie3", "necklace_of_passage_3")
        t.exec("goto-phoenix", t.player.goto_tile, 3419, 3153, 0)
        t.exec("grabFeather", t.player.talk_to, "golem_phoenix", 3)
        t.check("grabFeather.held", t.inv.await("golem_phoenixfeather", 1, 10) == "ok", "golem_phoenixfeather " .. count("golem_phoenixfeather"))
        t.expect("grabFeather.line", t.msg.expect("You grab a tail-feather."))
        t.exec("dipFeather", t.player.use_item_on_item, "golem_phoenixfeather", "golem_ink")
        t.exec("dipFeather-box", t.chat.play, { "objbox:You dip the phoenix feather into the dye." })
        t.check("dipFeather.pen", count("golem_pen") == 1 and count("golem_ink") == 1, "golem_pen " .. count("golem_pen") .. ", dye kept " .. count("golem_ink"))
        t.exec("writeProgram", t.player.use_item_on_item, "golem_pen", "papyrus")
        t.exec("writeProgram-box", t.chat.play, { "objbox:YOUR TASK IS DONE" })
        t.check("writeProgram.program", count("golem_program") == 1 and count("papyrus") == 0 and count("golem_pen") == 1,
            "golem_program " .. count("golem_program") .. ", papyrus " .. count("papyrus") .. ", pen " .. count("golem_pen"))

        -- === The skull and the end ([1862..1938]) ========================
        t.ticklog.mark("leg.end")
        t.exec("goto-golem", t.player.goto_tile, 3486, 3091, 0)
        t.exec("programShut", t.player.use_on, "golem_program", golem())
        t.expect("programShut.line", t.msg.expect("You can't see a way to put the instructions in the golem's skull."))
        t.exec("insertKey", t.player.use_on, "golem_golemkey", golem())
        t.expect("insertKey.line", t.msg.expect("You insert the key and the golem's skull hinges open."))
        expect_var("insertKey.open", "varb353_golem_head_open", 1)
        local skills_r, skills = t.skill.snapshot()
        t.step("skill.snapshot", skills_r == "ok" and "PASS" or "FAIL", "skill.snapshot -> " .. tostring(skills_r))
        t.exec("insertProgram", t.player.use_on, "golem_program", golem())
        t.exec("insertProgram-pages", t.chat.play, {
            "npc:New instructions... Updating program...",
            "npc:Task complete!",
            "npc:Thank you. Now my mind is at rest.",
        })
        t.ticks(2)
        t.check("insertProgram.used", count("golem_program") == 0, "golem_program " .. count("golem_program"))
        t.quest.expect_complete()
        t.check("reward.crafting", t.skill.expect_gain("crafting", 1000, skills))
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1000, skills))
        -- [1938]: the skull shuts by itself once the scroll is gone.
        t.ticks(12)
        t.expect("skullShuts", t.msg.expect("The golem's skull shuts automatically."))
        expect_var("skullShuts.var", "varb353_golem_head_open", 0)
        t.finish(0)
    end,
}
