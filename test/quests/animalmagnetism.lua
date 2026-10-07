-- Animal Magnetism: Ava -> Malcolm/Alice (Ectofuntus farm) -> Old crone -> chickens ->
-- witch/magnet -> undead trees/Turael -> research notes -> container.
-- The chicken-catching cutscene is not played by the content (CUTSCENES.tsv ported=no):
-- the scene is a ~mesbox page, driven through as dialogue.
--
-- WALLS (door rule, re-driven in b71). Every goto departs from and lands on open ground;
-- every door, gate, barrier, trapdoor and ladder between the player and a target is pressed
-- on every visit, in and out. Legs checked with test/quests/orchestrator/matthew-mbp-m4/
-- reports/sample_tools/{reach,comp,locs_near}.py (doors closed):
--   * Draynor Manor: Ava (m48_52.spawn anma_assistant_multi 3093,3357) stands in the secret
--     room behind the library bookcase. In: the front doors haunteddoorr 3109,3353 (a
--     walk-through that refuses from inside, quest_haunted.rs2:35-57), hall door D1 3109,3358,
--     D2 3106,3368, D3 3103,3364 (the bookcase room), hauntedbookcasel 3097,3358 (answers only
--     from the east, p_teleport to 3096,3358, quest_haunted.rs2:172-177). Out: hauntedleverup
--     3096,3357 (to 3098,3358, quest_haunted.rs2:186-194), D3, the corridor, the kitchen door
--     D6 3120,3356 and hauntedbackdoor 3123,3361 (the route grail.lua takes out).
--   * The witch (anma_witch_multi 3099,3370) is in the west room x 3097-3101 z 3367-3373
--     behind D 3101,3371 (from the north corridor).
--   * Morytania: in the way Priest in Peril opens it (the Varrock members' gate
--     fai_varrock_member_gatel 3319,3468, the Paterdomus trapdoor 3405,3507,
--     pip_underground_door1 3405,9895 and door2 3431,9897, Drezel's advice 60 -> 61, the holy
--     barrier 3440,9886 -> 3423,3485); out by the east trapdoor pipeastsidetrapdoor 3422,3485
--     (-> 3440,9887, mausoleum_interactions.rs2:50-59), both gates back west/north, the cellar
--     ladder 3405,9907 and the Varrock gate west.
--   * Old crone's house x 3460-3465 z 3556-3560 behind ahoy_harbour_door 3461,3555 (m54_55.jl2).
--   * Burthorpe (Turael) only through Taverley's members' gate membergater 2935,3450, both ways.
--   * Landings: Alice at 3627,3527 (3627,3528 is the wheelbarrow), the mine at 2978,3241
--     (inside ^anma_rimm_mine_sw/ne 2970-2984 x 3230-3249), Turael's open-doorway house 2931,3535.
return {
    id = "animalmagnetism",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give mithril_axe 1",
        "::give iron_bar 5",
        "::give amulet_of_ghostspeak 1",
        "::give ectotoken 20",
        "::give hard_leather 1",
        "::give blessedstar 1",
        "::give anma_p_buttons 1",
        "::give hammer 1",
        "::give dagger_wolfbane 1", -- Priest in Peril's own reward (::complete grants no items); Drezel's holy-barrier advice needs it held (mausoleum_drezel.rs2:29-34)
        "::setlevel slayer 18",
        "::setlevel crafting 19",
        "::setlevel ranged 30",
        "::setlevel woodcutting 35",
        "::complete quest_restlessghost",
        "::complete quest_ernestthechicken",
        "::complete quest_priestinperil",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb3185_anma_main",
            constants = {
                not_started = 0, fetch_chickens = 10, talk_alice = 20, return_malcolm = 30,
                return_alice = 40, return_malcolm2 = 50, return_alice2 = 60, talk_crone = 70,
                crone_mirror = 73, give_amulet = 76, talk_malcolm_amulet = 80, chicken_cutscene = 90, buy_chickens = 100, give_ava = 110,
                talk_witch = 120, witch_bars = 130, make_magnet = 140, undead_trees = 150,
                tree_bounce = 160, turael_axe = 170, cut_twigs = 180, give_twigs = 190,
                notes = 200, translate = 210, pattern = 220, give_container = 230,
                complete = 240,
            },
            row = "quest_animalmagnetism",
            display = "Animal Magnetism",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ------------------------------------------------------------ crossings
        local PD, PDO = "draynor_panelled_door", "draynor_panelled_door_open"
        local function door(row, at, near, far)
            t.exec(row, t.player.pass_door, { closed = PD, open = PDO, at = at, near = near, far = far })
        end
        -- Into Ava's secret room from the open grounds south of the manor.
        local function ava_in(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3109, 3351, 0)
            t.exec(step .. ".frontDoor", t.player.pass_door, { closed = "haunteddoorr", open = "haunteddoorr_inactive",
                at = { 3109, 3353, 0 }, near = { 3109, 3352 }, far = { 3109, 3354 } })
            door(step .. ".hallDoor", { 3109, 3358, 0 }, { 3109, 3357 }, { 3109, 3359 })
            door(step .. ".wingDoor", { 3106, 3368, 0 }, { 3106, 3368 }, { 3106, 3370 })
            door(step .. ".bookcaseRoomDoor", { 3103, 3364, 0 }, { 3103, 3364 }, { 3103, 3362 })
            t.exec(step .. ".toBookcase", t.player.walk_to, 3098, 3358, 30)
            t.exec(step .. ".bookcase", t.player.cross_gate, { loc = "hauntedbookcasel", at = { 3097, 3358, 0 },
                near = { 3098, 3358 }, far_ok = function(tile) return tile.x <= 3096 and tile.z >= 3355 and tile.z <= 3362 end,
                far_desc = "in the secret room behind the bookcase, x <= 3096 (quest_haunted.rs2:172-177 lands 3096,3358)" })
        end
        -- Out of the secret room by the lever, then out of the bookcase room into the corridor.
        local function ava_out(step)
            t.exec(step .. ".toLever", t.player.walk_to, 3096, 3357, 30)
            t.exec(step .. ".lever", t.player.cross_gate, { loc = "hauntedleverup", at = { 3096, 3357, 0 },
                near = { 3096, 3357 }, far_ok = function(tile) return tile.x >= 3098 end,
                far_desc = "back in the bookcase room, x >= 3098 (quest_haunted.rs2:186-194 lands 3098,3358)" })
            door(step .. ".bookcaseRoomDoor", { 3103, 3364, 0 }, { 3103, 3363 }, { 3103, 3365 })
        end
        -- From the corridor out of the manor: the kitchen door and the back door.
        local function manor_out(step)
            t.exec(step .. ".corridor", t.player.walk_route,
                { { 3106, 3369 }, { 3113, 3370 }, { 3114, 3362 }, { 3113, 3356 }, { 3119, 3356 } }, { level = 0 })
            door(step .. ".kitchenDoor", { 3120, 3356, 0 }, { 3119, 3356 }, { 3121, 3356 })
            t.exec(step .. ".toBackDoor", t.player.walk_to, 3123, 3360, 30)
            t.exec(step .. ".backDoor", t.player.pass_door, { closed = "hauntedbackdoor", open = "hauntedbackdoor",
                at = { 3123, 3361, 0 }, near = { 3123, 3360 }, far = { 3123, 3362 } })
        end
        local function witch_in(step)
            door(step .. ".westRoomDoor", { 3101, 3371, 0 }, { 3102, 3371 }, { 3100, 3371 })
        end
        local function witch_out(step)
            door(step .. ".westRoomDoor", { 3101, 3371, 0 }, { 3101, 3371 }, { 3103, 3371 })
        end
        local function crone_in(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3461, 3554, 0)
            t.exec(step .. ".doorIn", t.player.pass_door, { closed = "ahoy_harbour_door", open = "ahoy_harbour_door_open",
                at = { 3461, 3555, 0 }, near = { 3461, 3554 }, far = { 3461, 3557 } })
        end
        local function crone_out(step)
            t.exec(step .. ".doorOut", t.player.pass_door, { closed = "ahoy_harbour_door", open = "ahoy_harbour_door_open",
                at = { 3461, 3555, 0 }, near = { 3461, 3556 }, far = { 3461, 3554 } })
        end
        local function taverley_in(step)
            t.exec("goto-" .. step, t.player.goto_tile, 2936, 3450, 0)
            t.exec(step, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2936, 3450 },
                far_ok = function(tile) return tile.x <= 2935 end, far_desc = "through the members' gate into Taverley, x <= 2935" })
        end
        local function taverley_out(step)
            t.exec("goto-" .. step, t.player.goto_tile, 2932, 3450, 0)
            t.exec(step, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2934, 3450 },
                far_ok = function(tile) return tile.x >= 2936 end, far_desc = "out of Taverley, x >= 2936", far = { 2937, 3450 } })
        end
        local TRAP_AT = { 3405, 3507, 0 }
        local function morytania_in(step)
            t.exec("goto-" .. step .. ".varrockGate", t.player.goto_tile, 3318, 3468, 0)
            t.exec(step .. ".varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
                open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
            t.exec("goto-" .. step .. ".trapdoor", t.player.goto_tile, 3405, 3506, 0)
            if t.world.loc_near("trapdoor", 3, { at = TRAP_AT }) == "ok" then
                t.exec(step .. ".openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = TRAP_AT })
                t.await({
                    level = function()
                        return t.world.loc_near("trapdoor_open", 3, { at = TRAP_AT }) == "ok"
                    end,
                    note = step .. ": the trapdoor opens",
                }, 6)
            else
                t.note(step .. ": the trapdoor stands open (opened within its 500-tick revert), not pressed")
            end
            local tdo_r = t.world.loc_near("trapdoor_open", 3, { at = TRAP_AT })
            local tdc_r = t.world.loc_near("trapdoor", 3, { at = TRAP_AT })
            t.check(step .. ".trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
                "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. "; closed trapdoor there -> " .. tostring(tdc_r)
                    .. " (want the open leaf and no closed one)")
            t.exec(step .. ".descend", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
                at = TRAP_AT, src = { 3405, 3506 }, dest = { 3405, 9906, 0 } })
            t.exec(step .. ".gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                near = { 3405, 9896 }, far_ok = function(tile) return tile.z > 6400 and tile.z <= 9894 end,
                far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
            t.exec(step .. ".gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
                near = { 3430, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x >= 3432 end,
                far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
            -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:145-154): 60 -> 61, the barrier opens.
            t.exec(step .. ".drezelPresent", t.npc.await_present, "priestperiltrappedmonk2", 15, 12)
            t.exec(step .. ".talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
            t.exec(step .. ".talkToDrezel-dialog", t.chat.play, {
                "player:So can I pass through that barrier now?",
                "npc:Ah, ",
                "npc:Morytania is an evil land",
                "npc:You should take some basic precautions",
                "npc:In many ways Werewolves",
                "npc:and it is a holy relic",
                "npc:wolf form is incredibly powerful",
                "player:Okay, I will keep it equipped",
            })
            t.exec(step .. ".drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
            t.exec(step .. ".holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
                at = { 3440, 9886, 0 }, near = { 3440, 9887 },
                far_ok = function(tile) return tile.x == 3423 and tile.z == 3485 end,
                far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2:28 p_telejump(0_53_54_31_29))" })
        end
        local function morytania_out(step)
            t.exec("goto-" .. step, t.player.goto_tile, 3422, 3484, 0)
            if t.world.loc_near("pipeastsidetrapdoor", 3, { at = { 3422, 3485, 0 } }) == "ok" then
                t.exec(step .. ".openTrapdoor", t.player.click_loc, "pipeastsidetrapdoor", 1, { at = { 3422, 3485, 0 } })
                t.await({
                    level = function()
                        return t.world.loc_near("pipeastsidetrapdoor_open", 3, { at = { 3422, 3485, 0 } }) == "ok"
                    end,
                    note = step .. ": the east trapdoor opens",
                }, 6)
            else
                t.note(step .. ": the east trapdoor stands open, not pressed")
            end
            local edo_r = t.world.loc_near("pipeastsidetrapdoor_open", 3, { at = { 3422, 3485, 0 } })
            local edc_r = t.world.loc_near("pipeastsidetrapdoor", 3, { at = { 3422, 3485, 0 } })
            t.check(step .. ".trapdoorOpen", edo_r == "ok" and edc_r ~= "ok",
                "pipeastsidetrapdoor_open on 3422,3485,0 -> " .. tostring(edo_r) .. "; closed trapdoor there -> "
                    .. tostring(edc_r) .. " (want the open leaf and no closed one)")
            t.exec(step .. ".descend", t.player.climb, { loc = "pipeastsidetrapdoor_open", op = 1, op_name = "Climb-down",
                at = { 3422, 3485, 0 }, src = { 3422, 3484 }, dest = { 3440, 9887, 0 }, slack = 1 })
            t.exec(step .. ".gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
                near = { 3432, 9897 }, far_ok = function(tile) return tile.z > 6400 and tile.x <= 3431 end,
                far_desc = "onto the second gate's tile or west of it, x <= 3431 (gates.rs2 check_priest_peril_gate: leaving lands on the gate tile)",
                far = { 3430, 9897 }, ticks = 60 })
            t.exec(step .. ".gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                near = { 3405, 9894 }, far_ok = function(tile) return tile.z >= 9895 and tile.z < 9920 end,
                far_desc = "north of the golden-key gate, z >= 9895", ticks = 60 })
            t.exec(step .. ".ladder", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
                at = { 3405, 9907, 0 }, src = { 3405, 9906 }, dest = { 3405, 3506, 0 } })
            t.exec("goto-" .. step .. ".varrockGate", t.player.goto_tile, 3321, 3468, 0)
            t.exec(step .. ".varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
                open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3321, 3468 }, far = { 3318, 3468 } })
        end
        t.ticks(3)
        t.exec("wear-ghostspeak", t.player.equip, "amulet_of_ghostspeak")
        t.ticks(2)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- 1.1 talkToAva
        ava_in("talkToAva")
        t.exec("talkToAva", t.player.talk_to, "anma_assistant", 1)
        t.ticks(2)
        local pip_r, pip_v = t.var.varp("varp302_priestperil")
        t.check("prereq-priestinperil-state", pip_r == "ok" and pip_v == 60,
            "after ::complete quest_priestinperil varp302_priestperil = " .. tostring(pip_v)
            .. " (quest_cheat.rs2:1011 sets ^priestperil_complete = 60)")
        t.exec("talkToAva-dialogue", t.chat.play, {
            "npc:Hello there and welcome",
            "choose:I would be happy to make your home a better place.",
            "player:I would be happy",
            "npc:Yay, I didn't even",
            "npc:Don't worry, though",
            "player:Great, will I be able",
            "npc:Don't be silly",
            "player:I'm not convinced",
            "npc:I'll use one for my bed",
            "player:Very well then",
        })
        t.ticks(2)
        t.expect("quest.stage.fetch_chickens", t.quest.expect_stage("fetch_chickens"))
        -- 1.2 talkToAlicesHusband: out of the manor, into Morytania by Paterdomus
        ava_out("talkToAlicesHusband.leaveAva")
        manor_out("talkToAlicesHusband.leaveManor")
        morytania_in("talkToAlicesHusband.enterMorytania")
        t.exec("goto-talkToAlicesHusband", t.player.goto_tile, 3618, 3528, 0)
        t.exec("talkToAlicesHusband", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("talkToAlicesHusband-dialogue", t.chat.play, {
            "npc:Hello, how can I help you",
            "player:Would I be able to buy some",
            "npc:Talk to my wife",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_alice", t.quest.expect_stage("talk_alice"))

        -- 1.3 talkToAlice
        t.exec("walk-talkToAlice", t.player.walk_to, 3627, 3527, 40)
        t.exec("talkToAlice", t.player.talk_to, "farming_shopkeeper_4", 1)
        t.exec("talkToAlice-dialogue", t.chat.play, {
            "npc:Hello. Would you like to see my farming",
            "choose:I'm here about a quest.",
            "player:I'm here about a quest",
            "npc:My husband wants to sell",
            "npc:Tell him I would allow",
        })
        t.ticks(2)
        t.expect("quest.stage.return_malcolm", t.quest.expect_stage("return_malcolm"))

        -- 1.4 talkToAlicesHusband2
        t.exec("walk-talkToAlicesHusband2", t.player.walk_to, 3618, 3528, 40)
        t.exec("talkToAlicesHusband2", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("talkToAlicesHusband2-dialogue", t.chat.play, {
            "npc:Well? What did Alice say",
            "player:She says she cannot understand",
            "npc:Then go back and tell her",
        })
        t.ticks(2)
        t.expect("quest.stage.return_alice", t.quest.expect_stage("return_alice"))

        -- 1.5 talkToAlice2
        t.exec("walk-talkToAlice2", t.player.walk_to, 3627, 3527, 40)
        t.exec("talkToAlice2", t.player.talk_to, "farming_shopkeeper_4", 1)
        t.exec("talkToAlice2-dialogue", t.chat.play, {
            "npc:Well? What did he say this time",
            "player:He still cannot hear you",
            "npc:Then we are no further forward",
        })
        t.ticks(2)
        t.expect("quest.stage.return_malcolm2", t.quest.expect_stage("return_malcolm2"))

        -- (guide folds the second Malcolm and Alice visits into the s50/s60 hops)
        t.exec("walk-talkToAlicesHusband2b", t.player.walk_to, 3618, 3528, 40)
        t.exec("talkToAlicesHusband2b", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("talkToAlicesHusband2b-dialogue", t.chat.play, {
            "npc:This is hopeless",
            "player:I'll ask Alice",
        })
        t.ticks(2)
        t.expect("quest.stage.return_alice2", t.quest.expect_stage("return_alice2"))
        t.exec("walk-talkToAlice2b", t.player.walk_to, 3627, 3527, 40)
        t.exec("talkToAlice2b", t.player.talk_to, "farming_shopkeeper_4", 1)
        t.exec("talkToAlice2b-dialogue", t.chat.play, {
            "npc:There is an old crone west",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_crone", t.quest.expect_stage("talk_crone"))

        -- 1.6 talkToOldCrone (twice)
        crone_in("talkToOldCrone")
        t.exec("talkToOldCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToOldCrone-dialogue", t.chat.play, {
            "npc:Alice's husband needs to speak",
        })
        t.ticks(2)
        t.expect("quest.stage.crone_mirror", t.quest.expect_stage("crone_mirror"))
        t.exec("talkToOldCrone2", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToOldCrone2-dialogue", t.chat.play, {
            "npc:There - a crone-made amulet",
        })
        t.ticks(2)
        t.expect("quest.stage.give_amulet", t.quest.expect_stage("give_amulet"))
        t.exec("crone-amulet-in-pack", t.inv.expect_has, "amulet_of_humanspeak", 1)

        -- 1.7 giveAmuletToHusband
        crone_out("giveAmuletToHusband")
        t.exec("goto-giveAmuletToHusband", t.player.goto_tile, 3618, 3528, 0)
        t.exec("giveAmuletToHusband", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("giveAmuletToHusband-dialogue", t.chat.play, {
            "npc:Give me that amulet",
            "choose:Okay, you need it more than I do, I suppose.",
            "player:Okay, you need it more",
            "npc:Ta, mate",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_malcolm_amulet", t.quest.expect_stage("talk_malcolm_amulet"))

        -- 1.8 talkToAlicesHusband3 / 1.9 buyUndeadChickens
        t.exec("talkToAlicesHusband3", t.player.talk_to, "anma_ghost_farmer_amulet", 1)
        t.exec("talkToAlicesHusband3-dialogue", t.chat.play, {
            "npc:That's better",
            "npc:Alice! The chickens",
            "mesbox:Alice and Malcolm call",
            "npc:There. Now I can sell",
            "npc:I can hand over a chicken",
            "choose:Buy the chickens for ecto-tokens.",
            "npc:There you go",
        })
        t.ticks(2)
        t.expect("quest.stage.give_ava", t.quest.expect_stage("give_ava"))
        t.exec("buyUndeadChickens", t.inv.expect_has, "anma_chicken_sack_full", 2)

        -- 1.10 giveChickensToAva
        morytania_out("giveChickensToAva.leaveMorytania")
        ava_in("giveChickensToAva")
        t.exec("giveChickensToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveChickensToAva-dialogue", t.chat.play, {
            "npc:Wonderful! Those chickens",
            "npc:Next I need a bar magnet",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_witch", t.quest.expect_stage("talk_witch"))
        -- 1.11 talkToWitch (twice): out of the secret room, into the west room
        ava_out("talkToWitch.leaveAva")
        witch_in("talkToWitch")
        t.exec("talkToWitch", t.player.talk_to, "anma_witch", 1)
        t.exec("talkToWitch-dialogue", t.chat.play, {
            "npc:Hello, hello, my poppet",
            "player:Ava told me to ask you",
            "npc:Don't worry, deary, I can tell",
            "npc:Just bring me 5 iron bars",
            "player:I'll be back",
        })
        t.ticks(2)
        t.expect("quest.stage.witch_bars", t.quest.expect_stage("witch_bars"))
        t.exec("talkToWitch2", t.player.talk_to, "anma_witch", 1)
        t.exec("talkToWitch2-dialogue", t.chat.play, {
            "npc:Great, you'll go far",
            "npc:Hit the bar with a plain old",
        })
        t.ticks(2)
        t.expect("quest.stage.make_magnet", t.quest.expect_stage("make_magnet"))
        t.exec("witch-selected-iron", t.inv.expect_has, "anma_iron_bar", 1)

        -- 1.12 goToIronMine / 1.13 useHammerOnMagnet
        witch_out("goToIronMine")
        manor_out("goToIronMine.leaveManor")
        t.exec("goto-goToIronMine", t.player.goto_tile, 2978, 3241, 0)
        t.exec("useHammerOnMagnet", t.player.use_item_on_item, "hammer", "anma_iron_bar")
        t.exec("useHammerOnMagnet-magnet", t.inv.await, "anma_magnet", 1, 10)
        t.expect("useHammerOnMagnet-message", t.msg.expect("You hammer the iron bar and create a magnet."))

        -- 1.14 giveMagnetToAva
        ava_in("giveMagnetToAva")
        t.exec("giveMagnetToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveMagnetToAva-dialogue", t.chat.play, {
            "npc:Great stuff! With the Witch",
            "npc:We need a source of wood",
            "npc:Try using a woodcutting axe",
        })
        t.ticks(2)
        t.expect("quest.stage.undead_trees", t.quest.expect_stage("undead_trees"))
        -- 1.15 attemptToCutTree
        ava_out("attemptToCutTree.leaveAva")
        manor_out("attemptToCutTree.leaveManor")
        t.exec("goto-attemptToCutTree", t.player.goto_tile, 3108, 3350, 0)
        t.exec("attemptToCutTree", t.player.talk_to, "nasty_tree_choppable", 1)
        t.ticks(4)
        t.expect("attemptToCutTree-bounce", t.msg.expect("The axe bounces off the undead wood"))
        t.ticks(2)
        t.expect("quest.stage.tree_bounce", t.quest.expect_stage("tree_bounce"))
        ava_in("attemptToCutTree-report")
        t.exec("attemptToCutTree-report", t.player.talk_to, "anma_assistant", 1)
        t.exec("attemptToCutTree-report-dialogue", t.chat.play, {
            "npc:Fortunately for you",
            "player:Tell me the worst",
            "npc:The first is more interesting",
            "npc:Of course, you won't be able",
            "player:I'm not exactly addicted",
            "npc:Well, in that case",
            "npc:As he's not known",
        })
        t.ticks(2)
        t.expect("quest.stage.turael_axe", t.quest.expect_stage("turael_axe"))

        -- 1.16 talkToTurael (twice)
        ava_out("talkToTurael.leaveAva")
        manor_out("talkToTurael.leaveManor")
        taverley_in("talkToTurael.memberGateIn")
        t.exec("goto-talkToTurael", t.player.goto_tile, 2931, 3535, 0)
        local tur_r, tur_d = t.npc.nearest("slayer_master_1_tureal", 25)
        t.check("talkToTurael-npc-spawned", tur_r == "ok", "npc.nearest(slayer_master_1_tureal, 25) = " .. tostring(tur_r) .. " " .. tostring(tur_d))
        t.exec("talkToTurael", t.player.talk_to, "slayer_master_1_tureal", 1)
        t.exec("talkToTurael-dialogue", t.chat.play, {
            "player:I'm here about those undead trees",
            "npc:Ahh, you came to the right man",
            "player:I think I need some of the wood",
            "npc:Sounds like you need a blessed axe",
            "npc:If you can give me a mithril axe",
            "player:Okay, so I'll see whether I can spare",
        })
        t.ticks(2)
        t.expect("quest.stage.turael_axe-heard", t.quest.expect_stage("turael_axe"))
        t.exec("talkToTurael2", t.player.talk_to, "slayer_master_1_tureal", 1)
        t.exec("talkToTurael2-dialogue", t.chat.play, {
            "npc:I can make an axe for you now",
            "choose:I'd love one, thanks.",
            "npc:Here's a new axe",
        })
        t.ticks(2)
        t.expect("quest.stage.cut_twigs", t.quest.expect_stage("cut_twigs"))
        t.exec("talkToTurael-axe", t.inv.expect_has, "anma_axe", 1)

        -- 1.17 cutTree (30% of cuts fail by design: retry until the twigs land)
        taverley_out("cutTree.memberGateOut")
        t.exec("goto-cutTree", t.player.goto_tile, 3108, 3350, 0)
        local twig_have = 0
        for attempt = 1, 8 do
            t.player.talk_to("nasty_tree_choppable", 1)
            t.ticks(6)
            local _, twig_count = t.inv.count("anma_wood")
            twig_have = twig_count or 0
            if twig_have >= 1 then break end
        end
        t.check("cutTree-twigs", twig_have >= 1, "anma_wood in backpack after chopping with the blessed axe = " .. tostring(twig_have))
        t.expect("quest.stage.give_twigs", t.quest.expect_stage("give_twigs"))

        -- 1.18 giveTwigsToAva / 1.19 getNotesFromAva
        ava_in("giveTwigsToAva")
        t.exec("giveTwigsToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveTwigsToAva-dialogue", t.chat.play, {
            "npc:You certainly took your time",
            "player:I'd say they didn't grow on trees",
            "npc:Quite. Now that we have all",
            "npc:I've gathered research notes",
        })
        t.ticks(2)
        t.expect("quest.stage.notes", t.quest.expect_stage("notes"))
        t.exec("getNotesFromAva-notes", t.inv.expect_has, "anma_garb_notes", 1)

        -- 1.20 translateNotes: all nine start on; the fixed solution turns 1,3,4,6,7,8 off
        t.exec("translateNotes-open", t.player.inv_op, "anma_garb_notes", 1)
        t.exec("translateNotes-await", t.ui.await_open, "anma_rgb", 10)
        for _, n in ipairs({1, 3, 4, 6, 7, 8}) do
            local _, sw = t.ui.widget("anma_rgb:anma_buton_" .. n .. "_on")
            local inv_r = t.ui.invoke(sw, 1)
            t.ticks(2)
            local _, bit_value = t.var.varp("varp6204_anma_note_bits")
            t.check("translateNotes-switch" .. n, inv_r == "ok", "clicked anma_buton_" .. n .. "_on (component " .. tostring(sw) .. "); varp6204_anma_note_bits = " .. tostring(bit_value))
        end
        t.ticks(2)
        t.expect("quest.stage.translate", t.quest.expect_stage("translate"))
        t.exec("translateNotes-translated", t.inv.expect_has, "anma_trans_notes", 1)
        t.key("escape")

        -- 1.21 giveNotesToAva
        t.exec("giveNotesToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveNotesToAva-dialogue", t.chat.play, {
            "npc:For all I know",
            "npc:I've given you a pattern",
            "npc:If you are having trouble",
        })
        t.ticks(2)
        t.expect("quest.stage.pattern", t.quest.expect_stage("pattern"))
        t.exec("giveNotesToAva-pattern", t.inv.expect_has, "anma_pattern", 1)

        -- 1.22 buildPattern
        t.exec("buildPattern", t.player.use_item_on_item, "anma_pattern", "hard_leather")
        t.exec("buildPattern-container", t.inv.await, "anma_container", 1, 10)
        t.expect("quest.stage.give_container", t.quest.expect_stage("give_container"))

        -- 1.23 giveContainerToAva
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.check("giveContainerToAva-snapshot", xp_snapshot_result == "ok", "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))
        t.exec("giveContainerToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveContainerToAva-dialogue", t.chat.play, {
            "npc:Perfect! With the undead chicken",
        })
        t.ticks(3)
        t.expect("reward.crafting_xp", t.skill.expect_gain("crafting", 1000, xp_snapshot))
        t.expect("reward.fletching_xp", t.skill.expect_gain("fletching", 1000, xp_snapshot))
        t.expect("reward.slayer_xp", t.skill.expect_gain("slayer", 1000, xp_snapshot))
        t.expect("reward.woodcutting_xp", t.skill.expect_gain("woodcutting", 2500, xp_snapshot))
        t.exec("reward.attractor", t.inv.expect_has, "anma_30_reward", 1)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
