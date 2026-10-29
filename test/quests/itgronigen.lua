-- Observatory Quest, end to end through the real client, in the order RSProx
-- index 4412 (rev 241, 2026-10-29) plays it: the assistant's questions, the
-- professor's start, the planks, the bar and the glass, the goblin village,
-- the dungeon (the warning, the chests, the sleeping guard, the kitchen gate,
-- the stove), the mould and the lens, the professor gone ahead, the scene in
-- the Observatory, the telescope and the constellation. Every page below is
-- the recording's unless marked [wiki]; the ticks are in
-- docs/rsprox-analysis/quests/observatory_quest.md.
--
-- Content: quests/quest_itgronigen/scripts/*.rs2,
-- skill_crafting/scripts/glass/glass.rs2 (the lens).

return {
    id = "itgronigen",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::give woodplank 3",
        "::give bronze_bar 1",
        "::give molten_glass 1",
        "::give steel_scimitar 1",
        "::give lobster 12",
        "::setlevel crafting 10",
        "::setlevel attack 40",
        "::setlevel strength 40",
        "::setlevel defence 30",
        "::setlevel hitpoints 50",
        "::wield steel_scimitar",
    },
    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp112_itgronigen",
            constants = {
                not_started = 0, started = 1, given_planks = 2, given_bronze = 3, given_glass = 4,
                given_mould = 5, sent_telescope = 6, complete = 7, claimed_wine = 8,
            },
            row = "quest_observatoryquest",
            display = "Observatory Quest",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        -- the wire audit's side (docs/rsprox-analysis/quests/observatory_quest.md)
        t.ticklog.start()
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ------------------------------------------------------------------
        -- Crossings (door rule, owner 2026-10-03). Every one is pressed or
        -- walked on every visit; no goto lands in, or leaves, a closed space.
        --
        -- The professor's reception (maps/m38_49.jl2: qip_obs_office_wall,
        -- x 2438-2445 z 3184-3191) has ONE door, qip_obs_reception_door on
        -- the north edge of 2442,3183 (doors.loc pairs it with
        -- qip_obs_reception_door_open). The professor (2439,3186) and his
        -- assistant (2443,3189) both stand inside (m38_49.spawn).
        -- ------------------------------------------------------------------
        local function in_reception(tile)
            return tile.level == 0 and tile.x >= 2438 and tile.x <= 2445 and tile.z >= 3184 and tile.z <= 3191
        end
        local function outside_reception(tile)
            return tile.level == 0 and tile.z <= 3183 and not in_reception(tile)
        end
        local RECEPTION_DOOR = { 2442, 3183, 0 }
        local function reception_in(name)
            t.exec(name, t.player.pass_door, { closed = "qip_obs_reception_door", open = "qip_obs_reception_door_open",
                at = RECEPTION_DOOR, near = { 2442, 3182 }, far = { 2442, 3184 }, far_ok = in_reception,
                far_desc = "inside the reception, x 2438-2445 z 3184-3191" })
        end
        local function reception_out(name)
            t.exec(name, t.player.pass_door, { closed = "qip_obs_reception_door", open = "qip_obs_reception_door_open",
                at = RECEPTION_DOOR, near = { 2442, 3184 }, far = { 2442, 3182 }, far_ok = outside_reception,
                far_desc = "outside the reception, z <= 3183" })
        end
        -- The dungeon stairs (qip_obs_vstairs2 2458,3186) stand in the ruined
        -- goblin-village walls east of the reception, open to the south (no
        -- door loc). The static flood (reach.py, doors closed, op locs
        -- blocked) walks 57 tiles round them: these are its waypoints.
        local ROUTE_TO_VSTAIRS = { { 2442, 3182 }, { 2447, 3179 }, { 2450, 3174 }, { 2454, 3170 }, { 2458, 3166 },
            { 2459, 3173 }, { 2460, 3180 }, { 2458, 3184 }, { 2458, 3185 } }
        local ROUTE_FROM_VSTAIRS = { { 2458, 3185 }, { 2461, 3180 }, { 2459, 3174 }, { 2459, 3166 }, { 2453, 3168 },
            { 2449, 3172 }, { 2447, 3178 }, { 2442, 3181 }, { 2442, 3182 } }
        -- Up (observatory_stairs.rs2): the east stair lands beside the
        -- village's down-stairs, 2458,3185 (RSProx index 4412 [1087]; the
        -- cache spells the op "Climb up").
        local function dungeon_exit_up(name)
            t.exec(name, t.player.climb, { loc = "qip_obs_stairs1_dungeon", op = 1, op_name = "Climb up",
                at = { 2355, 9395, 0 }, src = { 2355, 9394 }, dest = { 2458, 3185, 0 } })
        end

        -- ------------------------------------------------------------------
        -- Start. From the fixture's Lumbridge street a members' gate is the
        -- only way on foot west into Kandarin (reach.py 3206,3233 ->
        -- 2442,3182: UNREACHABLE at margins 30/80/160/250 with every door
        -- closed, NEEDS-DOOR via membergater@2933,3320 at 400; sampler ruling
        -- b59 (a)). So the run's first goto stops on open ground outside
        -- Taverley's east members' gate (gates.rs2 walk-through), the gate is
        -- pressed on foot, and the overland hop to the street south of the
        -- reception door departs from open Taverley ground (reach.py
        -- 2933,3450 -> 2442,3182: REACH closed-doors len 1011, margin 160).
        -- ------------------------------------------------------------------
        t.exec("goto-talkToProfessor.memberGate", t.player.goto_tile, 2938, 3450, 0)
        t.exec("talkToProfessor.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
            near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
            far_desc = "inside Taverley, x <= 2935" })
        -- the walk-through lands on the gate tile; step one more onto Taverley's ground
        t.exec("talkToProfessor.intoTaverley", t.player.walk_to, 2933, 3450)
        t.exec("goto-talkToProfessor", t.player.goto_tile, 2442, 3182, 0)
        reception_in("talkToProfessor.receptionIn")

        -- A hand-over (index 4412 [589..591]): the player's page's click
        -- shuts the chat for a tick while both play the hand-over seq, then
        -- the professor's next page opens. Two plays, the gap awaited.
        local function handover(name, before, after)
            t.exec(name .. ".dialog", t.chat.play, before)
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = name .. ": the page after the hand-over" }, 6)
            t.exec(name .. ".after", t.chat.play, after)
        end

        -- RSProx index 4412 (rev 241): the assistant before the quest
        -- [444..479], each answer back to the menu without its topic.
        t.exec("assistant.before", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.before.dialog", t.chat.play, {
            "player:Hi, are you busy?",
            "npc:Me? I'm always busy.",
            "npc:See that man there? That's the professor.",
            "npc:Anyway, how might I help you?",
            "choose:I was wondering what you do here.",
            "player:I was wondering what you do here.",
            "npc:Glad you ask. This is the Observatory reception.",
            "npc:Up on the cliff is the Observatory dome",
            "npc:Usually...",
            "player:What do you mean, 'usually'?",
            "npc:*Ahem*. Back to work, please.",
            "npc:I'd speak with the professor. He'll explain.",
            "choose:Can I have a look through that telescope?",
            "player:Can I have a look through that telescope?",
            "npc:You can. You won't see much though.",
            "player:And that's because?",
            "npc:Just talk to the professor. He'll fill you in.",
            "choose:What's that board for?",
            "player:What's that board for?",
            "npc:The one behind me?",
            "player:Fallen stars?",
            "npc:That's right.",
            "npc:Whenever one does",
            "player:I see. And how do people find them?",
            "npc:There's a lot of amateur astronomers",
            "player:Could I do that?",
            "npc:I imagine so.",
            "player:Awesome. I might just give it a go.",
            "npc:Good luck.",
            "player:Good to know. Thanks.",
            "choose:I'd better be off.",
            "player:I'd better be off.",
            "end",
        })

        -- The start [509..551]: the menu, the intro, "An Observatory?", the
        -- confirm; the sign, the stage and the chest with the last page.
        t.exec("talkToProfessor", t.player.talk_to, "observatory_professor_multi", 1)
        t.exec("talkToProfessor.dialog", t.chat.play, {
            "npc:Hello.",
            "choose:Talk about the Observatory quest.",
            "player:Hi, I was...",
            "npc:Welcome to the magnificent wonder of the Observatory",
            "player:Wow, nice intro.",
            "npc:Why, thanks! How might I help you?",
            "choose:An Observatory?",
            "player:An Observatory?",
            "npc:Of course. We have a superb telescope",
            "npc:A truly marvellous invention",
            "npc:Well, it would be if it worked.",
            "npc:Don't interrupt!",
            "player:What? It doesn't work?",
            "npc:Oh, no, no, no.",
            "npc:Nope, dead serious.",
            "npc:Arghhh!",
            "player:So, it's broken. How come?",
            "npc:Oh, I suppose there's no use keeping it secret.",
            "player:Up on the hill? Yes, I've seen them.",
            "npc:It's a horde of goblins.",
            "npc:Since they moved here",
            "npc:Last week, my telescope was tampered with.",
            "npc:Now, parts need replacing",
            "npc:Strikes me that this visitor could help us.",
            "npc:Stop being so rude.",
            "npc:...",
            "npc:Although, he has a point.",
            "player:What, me?",
            "choose:Yes.",
            "player:Sounds interesting, what can I do for you?",
            "npc:Oh, thanks so much.",
            "npc:I shall need some materials",
            "npc:Let's start with three planks of wood",
            "npc:As if I don't have enough work to do.",
            "npc:Go talk to him if you need some advice.",
            "player:Okay, I'll be right back.",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        local _, sign = t.var.server("varb3828_observatory_starsign")
        t.check("start.sign", type(sign) == "number" and sign >= 0 and sign <= 11,
            "starsign = " .. tostring(sign) .. " (rolled with the start, index 4412 [550])")
        sign = type(sign) == "number" and sign or 0

        -- [554..557] the assistant with the planks (a random greeting first)
        t.exec("assistant.planks", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.planks.dialog", t.chat.play, {
            "*", "*",
            "player:I've got some planks for the telescope's base.",
            "npc:Good work! The professor has been eagerly awaiting them.",
            "end",
        })

        -- [584..594], [614..629], [650..662]: the three hand-overs
        t.exec("giveProfessorPlanks", t.player.talk_to, "observatory_professor_multi", 1)
        handover("giveProfessorPlanks", {
            "npc:Hello.",
            "choose:Talk about the Observatory quest.",
            "player:Hi again!",
            "npc:It's my helping hand, back again.",
            "npc:Do you have the planks yet?",
            "player:Yes, I've got them. Here they are.",
        }, {
            "npc:Well done. This will make a big difference.",
            "npc:Now, the bronze for the tube. Oh, assistant!",
            "npc:Okay, okay, ask me if you need any help",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_planks", t.quest.expect_stage("given_planks"))
        t.expect("planks.gone", t.inv.expect_absent("woodplank"))

        t.exec("giveProfessorBar", t.player.talk_to, "observatory_professor_multi", 1)
        handover("giveProfessorBar", {
            "npc:Hello.",
            "choose:Talk about the Observatory quest.",
            "player:Hi.",
            "npc:The traveller returns!",
            "player:Still working hard?",
            "npc:Some of us are.",
            "npc:What did I tell you about speaking when spoken to?",
            "npc:So, ",
            "player:I certainly do. Here you go.",
        }, {
            "npc:Great. Now all I need is the lens made.",
            "npc:Please get me some molten glass.",
            "npc:Oi! Lazy bones!",
            "player:What? I'm not lazy.",
            "npc:Not you! I'm talking to my assistant.",
            "npc:Calm down old man, I heard.",
            "npc:Thank you. Wait a minute",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_bronze", t.quest.expect_stage("given_bronze"))
        t.expect("bar.gone", t.inv.expect_absent("bronze_bar"))

        t.exec("assistant.glass", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.glass.dialog", t.chat.play, {
            "*", "*",
            "player:I've managed to get hold of some molten glass.",
            "npc:I suggest you have a word with the professor, in that case.",
            "end",
        })

        t.exec("giveProfessorGlass", t.player.talk_to, "observatory_professor_multi", 1)
        handover("giveProfessorGlass", {
            "npc:Hello.",
            "choose:Talk about the Observatory quest.",
            "npc:How are you getting on finding me some molten glass?",
            "player:Here it is.",
        }, {
            "npc:Excellent work, let's make the lens.",
            "npc:It'll need to be made to an exact shape and size.",
            "npc:Well, obviously, hence why we have a lens mould.",
            "npc:Not any more. One of those goblins took it.",
            "npc:Great, just what I need.",
            "player:I'll have a look - where should I start?",
            "npc:No idea. You could ask my USELESS assistant",
            "npc:What have I done to deserve this?",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_glass", t.quest.expect_stage("given_glass"))
        t.expect("glass.taken", t.inv.expect_absent("molten_glass"))

        -- [678..688] where the mould is
        t.exec("assistant.mould", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.mould.dialog", t.chat.play, {
            "*", "*",
            "player:Where can I find this lens mould you mentioned?",
            "npc:I'm sure I heard one of those goblins talking about it.",
            "player:What makes you say that?",
            "npc:I had a nice new star chart, until recently.",
            "npc:...as some kind of makeshift hankey",
            "player:Oh dear.",
            "npc:You may want to look through the dungeon",
            "player:Thanks for the advice.",
            "end",
        })

        -- ------------------------------------------------------------------
        -- The goblin village [711..732]: three of its goblins, then the
        -- signpost [355..364] on the way round to the dungeon's stairs.
        -- ------------------------------------------------------------------
        reception_out("enterDungeon.receptionOut")
        t.exec("goblins.drunk", t.player.talk_to, "qip_obs_drunken_goblin", 1)
        t.exec("goblins.drunk.dialog", t.chat.play, {
            "player:Hi there.",
            "npc:Hey, ids me matesh!",
            "player:Sorry, have we met?",
            "npc:Yeah! you wazsh wiv me",
            "player:I have no idea what you're going on about.",
            "npc:Glad yeeash remembers.",
            "end",
        })
        t.exec("goblins.cook", t.player.talk_to, "qip_obs_cooking_goblin", 1)
        t.exec("goblins.cook.dialog", t.chat.play, {
            "player:Hello.",
            "npc:Shush! I'm concentrating.",
            "player:Oh, sorry.",
            "end",
        })
        t.exec("goblins.fire", t.player.talk_to, "qip_obs_goblin_setting_a_fire", 1)
        t.exec("goblins.fire.dialog", t.chat.play, {
            "player:Where did you get that lens?",
            "npc:From that strange metal thing up on the hill.",
            "player:You should give that back!",
            "npc:Even if it's cracked?",
            "player:Ah, well, I suppose it's of no use. But, still.",
            "end",
        })

        -- Down (qip_obs_vstairs2 [755..766]): the warning, its count, "Proceed
        -- regardless", the landing (maplink.dbrow 2355,9394).
        local warnings = 0
        local function vstairs_down(name)
            t.exec(name, t.player.click_loc, "qip_obs_vstairs2", 1, { at = { 2458, 3186 } })
            t.check(name .. ".warning", t.ui.await_open("cws_warning_9", 12) == "ok", "cws_warning_9 open")
            warnings = warnings + 1
            t.exec(name .. ".count", t.var.await, "varb3859_cws_warning_9", warnings, 5)
            local wr, yes = t.ui.widget("cws_warning_9:warn1")
            t.check(name .. ".yesWidget", wr == "ok", "cws_warning_9:warn1 -> " .. tostring(wr))
            t.exec(name .. ".yes", t.ui.invoke, yes, 1)
            t.await({ level = function()
                local r, tile = t.world.tile()
                return r == "ok" and tile.x == 2355 and tile.z == 9394
            end, note = "the dungeon's landing" }, 10)
        end
        t.exec("enterDungeon.walk", t.player.goto_tile, 2458, 3185, 0)
        t.ticklog.mark("leg.dungeon")
        vstairs_down("enterDungeon")
        t.ticks(2)
        local _, choice = t.var.server("varb3827_observatory_chestchoice")
        t.check("chestchoice", choice == 0 or choice == 1 or choice == 2,
            "chest choice = " .. tostring(choice) .. " (rolled with the start, 0, 1 or 2)")
        local closed = { [0] = "qip_obs_dungeon_chest_closed", [1] = "qip_obs_dungeon_chest_closed2", [2] = "qip_obs_dungeon_chest_closed3" }
        local open = { [0] = "qip_obs_dungeon_chest_open", [1] = "qip_obs_dungeon_chest_open2", [2] = "qip_obs_dungeon_chest_open3" }
        -- An open floor tile beside each chest (reach.py from the stairs'
        -- landing 2355,9394, doors closed: REACH 253 / 53 / 66). The dungeon
        -- is one passage; these hops are travel inside it.
        local beside = { [0] = { 2364, 9354 }, [1] = { 2359, 9365 }, [2] = { 2351, 9360 } }
        local wrong = ((choice or 0) + 1) % 3
        -- [796..835]: open (the line, two ticks later the open chest), search
        t.exec("goto-wrongchest", t.player.goto_tile, beside[wrong][1], beside[wrong][2], 0)
        t.exec("searchChests.wrong.open", t.player.click_loc, closed[wrong], 1)
        t.expect("wrongchest.opened", t.msg.expect("You open the chest."))
        t.ticks(3)
        t.exec("searchChests.wrong.search", t.player.click_loc, open[wrong], 1)
        t.expect("wrongchest.empty", t.msg.expect("The chest is empty."))
        t.expect("wrongchest.no_key", t.inv.expect_absent("keep_key"))
        t.exec("goto-keychest", t.player.goto_tile, beside[choice][1], beside[choice][2], 0)
        t.exec("searchChests.key.open", t.player.click_loc, closed[choice], 1)
        t.ticks(3)
        t.exec("searchChests.key.search", t.player.click_loc, open[choice], 1)
        t.exec("keychest.dialog", t.chat.play, { "objbox:You find a kitchen key.", "end" })
        t.exec("key.held", t.inv.await, "keep_key", 1, 5)

        -- ------------------------------------------------------------------
        -- The kitchen gate (2327,9393 / 2326,9393) and its guard asleep north
        -- of it (m36_146.spawn 2327,9394); the stove is south, in the kitchen.
        -- ------------------------------------------------------------------
        t.exec("goto-gate", t.player.goto_tile, 2327, 9395, 0)
        local gr, gdetail = t.player.click_loc("keepgate_closed", 1, { at = { 2327, 9393, 0 } })
        t.note("gate.click (attempt): " .. tostring(gr) .. " " .. tostring(gdetail))
        t.expect("gate.warn", t.msg.expect("the guard will hear you"))
        -- [907..910]: the wake; two ticks on a goblin_guard in his place
        t.exec("prodGuard", t.player.press, "qip_obs_goblin_guard", 1)
        t.ticks(3)
        t.exec("guard.attack", t.player.attack, "goblin_guard", 2)
        local _, guard_detail = t.exec("guard.dead", t.npc.await_dead_engaged, 240, 40, { eat = { item = "lobster", below = 25 } })
        -- Fight margin: lowest hp at least a quarter of the 50 staged (13)
        -- AND food left. The guard is quest_itgronigen.npc [goblin_guard]
        -- (hitpoints 43, attack 32, defence 37), a real block.
        local guard_low = type(guard_detail) == "string" and tonumber(guard_detail:match("lowest hp (%d+)/")) or nil
        local lr, lobsters = t.inv.count("lobster")
        t.check("guard.margin", guard_low ~= nil and guard_low >= 13 and lr == "ok" and type(lobsters) == "number" and lobsters >= 1,
            "goblin guard: lowest hp " .. tostring(guard_low) .. "/50, lobsters staged 12, left " .. tostring(lobsters)
                .. " (" .. tostring(lr) .. ") (margin: lowest hp >= 13, a quarter of 50, AND food left)")
        t.ticks(6)
        t.expect("guard.bones", t.world.obj_near("bones", 6))

        -- [994..998]: with the key, unlocked for good and walked through,
        -- the key discarded and the player's page.
        local function gate_side(name, want_inside)
            t.await({ level = function()
                local r, tile = t.world.tile()
                if r ~= "ok" then return false end
                if want_inside then return tile.z <= 9393 end
                return tile.z >= 9394
            end, note = name }, 12)
            local r, tile = t.world.tile()
            t.check(name, r == "ok" and ((want_inside and tile.z <= 9393) or (not want_inside and tile.z >= 9394)),
                "at " .. tostring(r == "ok" and (tile.x .. "," .. tile.z) or tile))
        end
        t.exec("gate.unlock", t.player.click_loc, "keepgate_closed", 1, { at = { 2327, 9393, 0 } })
        t.exec("gate.unlock.dialog", t.chat.play, { "player:You had better be quick, there may be more guards about.", "end" })
        t.expect("gate.unlocked", t.msg.expect("The gate unlocks."))
        t.expect("gate.key_gone", t.inv.expect_absent("keep_key"))
        gate_side("gate.inside", true)
        -- [1016..1027] the stove
        t.exec("inspectStove", t.player.click_loc, "qip_obs_dungeon_stove_top_multi", 1)
        t.exec("inspectStove.dialog", t.chat.play, {
            "mesbox:The goblins appear to have been using the lens mould to cook their stew!",
            "mesbox:You shake out its contents and take it with you.",
            "end",
        })
        t.exec("mould.held", t.inv.await, "lens_mould", 1, 6)
        -- [1030] searched again
        t.exec("inspectStove.again", t.player.click_loc, "qip_obs_dungeon_stove_empty", 1)
        t.exec("inspectStove.again.dialog", t.chat.play, { "mesbox:Just a plain stove. Nothing there.", "end" })
        -- [1055..1060] back out, no lines
        t.exec("gate.out", t.player.click_loc, "keepgate_closed", 1, { at = { 2327, 9393, 0 } })
        gate_side("gate.outside", false)

        -- out by the east stair (2355,9395 [1084..1087]), walk to the door, in
        t.exec("goto-exit", t.player.goto_tile, 2355, 9394, 0)
        dungeon_exit_up("leaveDungeon")
        t.exec("giveProfessorMould.walk", t.player.walk_route, ROUTE_FROM_VSTAIRS)
        reception_in("giveProfessorMould.receptionIn")

        t.exec("assistant.hasMould", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.hasMould.dialog", t.chat.play, {
            "*", "*",
            "player:I have the lens mould.",
            "npc:Well done on finding that!",
            "player:Will do.",
            "end",
        })

        -- [1132..1147] the mould; the glass given back (stage 5)
        t.exec("giveProfessorMould", t.player.talk_to, "observatory_professor_multi", 1)
        handover("giveProfessorMould", {
            "npc:Hello.",
            "choose:Talk about the Observatory quest.",
            "npc:Did you bring me the mould?",
            "player:I certainly have. You'll never guess",
            "npc:Well, from the smell",
            "player:Wow, good guess. Well, here you go.",
            "npc:Please don't give that to him.",
            "player:Explosion?",
            "npc:Erm, yes.",
            "player:I suppose it's better I don't ask.",
            "npc:You can use the mould with molten glass to make a new lens.",
        }, {
            "objbox:The professor gives you back the molten glass.",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.given_mould", t.quest.expect_stage("given_mould"))
        t.expect("glass.returned", t.inv.expect_has("molten_glass", 1))
        t.expect("mould.still_held", t.inv.expect_has("lens_mould", 1))

        -- [1153..1158] the lens: the first box has no continue row; two ticks
        -- on the lens and the second box
        t.exec("useGlassOnMould", t.player.use_item_on_item, "lens_mould", "molten_glass")
        t.exec("lens.made", t.inv.await, "lens", 1, 8)
        t.exec("useGlassOnMould.dialog", t.chat.play, {
            "objbox:You pour the molten glass into the mould and clasp it together...",
            "end",
        })
        t.expect("glass.spent", t.inv.expect_absent("molten_glass"))
        t.expect("mould.kept_after_cast", t.inv.expect_has("lens_mould", 1))

        -- [1163..1178] the lens; he goes ahead (stage 6), the lens and the
        -- mould gone with the box
        t.exec("giveProfessorLensAndMould", t.player.talk_to, "observatory_professor_multi", 1)
        handover("giveProfessorLensAndMould", {
            "npc:Hello.",
            "choose:Talk about the Observatory quest.",
            "npc:Is the lens finished?",
            "player:Yes, here it is. You may as well take this mould too.",
        }, {
            "npc:Wonderful, at last I can fix the telescope.",
            "npc:Would you accompany me to the Observatory?",
            "player:Sounds interesting. Count me in.",
            "npc:Superb.",
            "player:Okay. See you there.",
        })
        -- [1174..1176]: the fade, two ticks, the box
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the professor gone ahead: the box" }, 6)
        t.exec("giveProfessorLensAndMould.gone", t.chat.play, {
            "mesbox:The professor has gone ahead to the Observatory dome.",
            "end",
        })
        t.ticks(2)
        t.expect("quest.stage.sent_telescope", t.quest.expect_stage("sent_telescope"))
        t.expect("lens.gone", t.inv.expect_absent("lens"))
        t.expect("mould.taken_back", t.inv.expect_absent("lens_mould"))

        -- [1195..1198]
        t.exec("assistant.repairman", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        t.exec("assistant.repairman.dialog", t.chat.play, {
            "player:Hello again.",
            "npc:Ah, it's the telescope repairman!",
            "player:How can I get to the Observatory?",
            "npc:Well, since the bridge was ruined",
            "end",
        })

        -- ------------------------------------------------------------------
        -- To the Observatory through the dungeon's south-west stair: the scene
        -- [1317..1482] in a copy of the square, then the real ground floor.
        -- ------------------------------------------------------------------
        reception_out("enterDungeonAgain.receptionOut")
        t.exec("enterDungeonAgain.walk", t.player.walk_route, ROUTE_TO_VSTAIRS)
        vstairs_down("enterDungeonAgain")
        t.exec("goto-dome-stair", t.player.goto_tile, 2335, 9350, 0)
        t.ticklog.mark("leg.scene")
        local scene_mark = t.cutscene.mark()
        t.exec("enterObservatory", t.player.click_loc, "qip_obs_stairs1_dungeon", 1, { at = { 2335, 9351 } })
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the scene's first box" }, 20)
        local scene_pages = 0
        for _ = 1, 14 do
            t.chat.drain({ max_pages = 20 })
            scene_pages = scene_pages + 1
            local more = t.await({ level = function() return t.chat.kind() ~= "none" end,
                note = "the scene's next page" }, 40)
            if more ~= "ok" then
                break
            end
        end
        t.check("scene.pages", scene_pages >= 4,
            "drained " .. scene_pages .. " page run(s) (the box; the greeting; going upstairs; looking good)")
        -- The camera rows need the client's camera reader (none on scriptrun).
        if type(scene_mark) == "number" then
            t.exec("scene.cutscene", t.cutscene.await, "enterObservatory", { since = scene_mark, quiet = 200, expect = {
                { op = "moveto", height = 1925 },
                { op = "lookat", height = 75 },
                { op = "moveto", height = 600 },
                { op = "reset" },
            } })
        else
            t.note("scene.cutscene: no camera reader in this lane; the camera rows are the live client's")
        end
        t.ticks(5)
        t.exec("scene.seen", t.var.await, "varb3838_observatory_cutscene_seen", 1, 5)
        local back_r, back = t.world.tile()
        t.check("scene.landing", back_r == "ok" and back.x == 2438 and back.z == 3163 and back.level == 0,
            "after the scene at " .. tostring(back_r == "ok" and (back.x .. "," .. back.z .. "," .. back.level) or back)
                .. " (want 2438,3163,0 [1478])")

        -- [1498..1501] up: 2442,3159 on the floor above
        local function in_dome(tile)
            return tile.x >= 2436 and tile.x <= 2445 and tile.z >= 3157 and tile.z <= 3165
        end
        t.exec("goToF2Observatory", t.player.climb, { loc = "qip_obs_stairs1", op = 1, op_name = "Climb-up",
            at = { 2444, 3159, 0 }, dest = { 2442, 3159, 1 }, slack = 1, landed_ok = in_dome,
            landed_desc = "the Observatory's upper floor, 2442,3159" })

        -- [1517..1521]
        t.exec("professor.upstairs", t.player.talk_to, "observatory_professor_multi2", 1)
        t.exec("professor.upstairs.dialog", t.chat.play, {
            "npc:Welcome back.",
            "choose:Talk about the Observatory quest.",
            "player:Hi, this really is impressive.",
            "npc:Certainly is.",
            "end",
        })
        -- [1525..1539]
        t.ticklog.mark("leg.telescope")
        t.exec("viewTelescope", t.player.click_loc, "qip_obs_tele_gear_upper_multi", 1)
        t.expect("viewTelescope.looked", t.msg.expect("You look through the telescope."))
        t.check("viewTelescope.open", t.ui.await_open("qip_obs_telescope", 8) == "ok", "qip_obs_telescope open")
        t.exec("viewTelescope.looked_var", t.var.await, "varb3836_observatory_scopelooked", 1, 5)
        local cr, closeb = t.ui.widget("qip_obs_telescope:close_button")
        t.check("viewTelescope.closeWidget", cr == "ok", "qip_obs_telescope:close_button -> " .. tostring(cr))
        t.exec("viewTelescope.close", t.ui.invoke, closeb, 1)
        t.ticks(3)
        local names = { [0] = "Aquarius", "Capricorn", "Sagittarius", "Scorpio", "Libra", "Virgo", "Leo", "Cancer", "Gemini", "Taurus", "Aries", "Pisces" }
        local pages = { [0] = 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3 }
        local wrong_sign = (sign + 5) % 12
        local function answer_pages(name)
            local l = { "npc:Welcome back.", "choose:Talk about the Observatory quest.",
                "player:I've had a look through the telescope.", "npc:What did you see?", "player:It was..." }
            for _ = 1, pages[name] do l[#l + 1] = "choose:~ next ~" end
            l[#l + 1] = "choose:" .. names[name]
            l[#l + 1] = "player:" .. names[name] .. "!"
            return l
        end
        t.exec("professor.wrong", t.player.talk_to, "observatory_professor_multi2", 1)
        local wl = answer_pages(wrong_sign)
        wl[#wl + 1] = "npc:I'm afraid not."
        wl[#wl + 1] = "end"
        t.exec("professor.wrong.dialog", t.chat.play, wl)
        t.expect("quest.stage.sent_telescope.still", t.quest.expect_stage("sent_telescope"))

        -- Each sign's own reward, literal (observatory_telescope.rs2
        -- observatory_grant_constellation_reward, from the wiki's Rewards
        -- table): stat_advance is in tenths, so 8750 is 875 xp. Every sign
        -- also grants 2,250 Crafting xp (22500) and one uncut sapphire.
        local SIGN_REWARD = {
            [0] = { obj = "waterrune", n = 25 },
            [1] = { skill = "strength", n = 875 },
            [2] = { obj = "maple_longbow", n = 1 },
            [3] = { obj = "weapon_poison", n = 1 },
            [4] = { obj = "lawrune", n = 3 },
            [5] = { skill = "defence", n = 875 },
            [6] = { skill = "hitpoints", n = 875 },
            [7] = { obj = "amulet_of_defence", n = 1 },
            [8] = { obj = "black_2h_sword", n = 1 },
            [9] = { obj = "1dose2strength", n = 1 },
            [10] = { skill = "attack", n = 875 },
            [11] = { obj = "tuna", n = 3 },
        }
        local reward = SIGN_REWARD[sign]
        local function count(obj)
            local r, n = t.inv.count(obj)
            return r == "ok" and type(n) == "number" and n or nil
        end
        local sapphire_before = count("uncut_sapphire")
        local obj_before = reward.obj and count(reward.obj) or nil
        local _, snap = t.skill.snapshot()
        -- [1549..1581]
        t.exec("tellProfessorConstellation", t.player.talk_to, "observatory_professor_multi2", 1)
        local rl = answer_pages(sign)
        rl[#rl + 1] = "npc:That's exactly it!"
        rl[#rl + 1] = "player:Yes! Woo hoo!"
        rl[#rl + 1] = "*"
        rl[#rl + 1] = "*"
        rl[#rl + 1] = "npc:By Saradomin's earlobes!"
        rl[#rl + 1] = "npc:Look in your backpack"
        t.exec("tellProfessorConstellation.dialog", t.chat.play, rl)
        t.ticks(3)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.expect("reward.crafting", t.skill.expect_gain("crafting", 2250, snap))
        local sapphire_after = count("uncut_sapphire")
        t.check("reward.sapphire", sapphire_before ~= nil and sapphire_after ~= nil and sapphire_after - sapphire_before == 1,
            "uncut_sapphire " .. tostring(sapphire_before) .. " -> " .. tostring(sapphire_after) .. " (want +1)")
        if reward.skill then
            t.expect("reward.sign." .. names[sign], t.skill.expect_gain(reward.skill, reward.n, snap))
        else
            local obj_after = count(reward.obj)
            t.check("reward.sign." .. names[sign], obj_before ~= nil and obj_after ~= nil and obj_after - obj_before == reward.n,
                names[sign] .. ": " .. reward.obj .. " " .. tostring(obj_before) .. " -> " .. tostring(obj_after)
                    .. " (want +" .. reward.n .. ")")
        end
        local rr, rd = t.scroll.rewards()
        t.step("reward.lines", rr == "ok" and "PASS" or "FAIL", tostring(rr) .. " " .. tostring(type(rd) == "table" and table.concat(rd.lines or {}, " / ") or rd))
        t.expect("quest.complete", t.quest.expect_complete())

        -- ------------------------------------------------------------------
        -- Back to the assistant: down the dome's stairs, down into the
        -- dungeon, round to the east stair, up, and in by the door.
        -- maplink_1_38_49_11_24_down: 2443,3160,1 -> 2444,3162,0;
        -- maplink_0_38_49_7_28_down: 2439,3164,0 -> 2335,9350,0.
        -- ------------------------------------------------------------------
        t.exec("leaveObservatory.stairsDown", t.player.climb, { loc = "qip_obs_stairs2_down", op = 1, op_name = "Climb-down",
            at = { 2443, 3159, 1 }, src = { 2443, 3160 }, dest = { 2444, 3162, 0 } })
        t.exec("leaveObservatory.dungeonDown", t.player.climb, { loc = "qip_obs_stairs_to_dungeon2", op = 1,
            op_name = "Climb-down", at = { 2438, 3164, 0 }, src = { 2438, 3163 }, dest = { 2335, 9350, 0 } })
        -- dungeon travel (reach.py 2335,9350 -> 2355,9394: REACH, doors closed)
        t.exec("goto-exit2", t.player.goto_tile, 2355, 9394, 0)
        dungeon_exit_up("leaveDungeonAgain")
        t.exec("assistant.wine.walk", t.player.walk_route, ROUTE_FROM_VSTAIRS)
        reception_in("assistant.wine.receptionIn")

        -- assistant hands over the wine (stage 8)
        t.exec("assistant.wine", t.player.talk_to, "qip_obs_proffesors_assistant", 1)
        -- [wiki] (no recording claims the wine)
        t.exec("assistant.wine.dialog", t.chat.play, {
            "player:Hi assistant.",
            "choose:Wait a minute, what's your real name?",
            "player:Wait a minute, what's your real name?",
            "npc:My real name?",
            "player:I only know you as the professor's assistant.",
            "npc:Patrick.",
            "player:Hi, Patrick",
            "npc:Well, hello again",
            "npc:Oh, and, believe it or not",
            "player:You made that up!",
            "npc:No, honest!",
            "objbox:The assistant gives you some wine.",
            "player:Thanks very much.",
            "npc:No problem. Scorpius would be proud.",
            "player:Sorry?",
            "npc:You may want to check out the book",
            "end",
        })
        t.expect("quest.stage.claimed_wine", t.quest.expect_stage("claimed_wine"))
        t.expect("wine.held", t.inv.expect_has("jug_wine", 1))
    end,
}
