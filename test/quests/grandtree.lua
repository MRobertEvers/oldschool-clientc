-- The Grand Tree (varp grandtree). Client-driven end to end: every guide step is a real row.
-- Guide source: Quest Helper helpers/quests/thegrandtree/. Requirement: Agility 25 (::setlevel).
-- Combat gear and sharks are the guide's recommended kit (::give); the foreman and the black demon
-- are fought for real. Quest state is only ever advanced by the quest's own dialogue and locs.
--
-- Every closed space is crossed on foot, in and out, on every visit (door rule, owner 2026-10-03):
--   * the Stronghold's gnome_areagate 2459,3383 is the only way in on foot (reach.py 2461,3386 ->
--     2461,3380 UNREACHABLE at margins 80/160/300): cross_gate on every trip;
--   * the Grand Tree's ground floor is a 20-tile pocket behind the walk-through tree door
--     (treedoorl 2464,3492, gnome_gate.rs2 @open_tree_door): cross_gate on every visit;
--   * the tree's floors, Glough's house, Hazelmere's hut, Anita's room and Glough's watchtower are
--     climbed by their ladders/stairs/tree (t.player.climb); Hazelmere's elfdoor by pass_door;
--   * the Karamja shipyard is a fenced yard behind grandtree_fencegate_l 2945,3041;
--   * Lumbridge -> Kandarin and Karamja -> Kandarin are a real Camelot Teleport each (Karamja is an
--     island and the glider crashes, gnome_glider.rs2:54); overland hops depart and land on open ground.

-- The Grand Tree's ladders stand on 2466,3495 on every floor and move the player one plane on the
-- tile it stands on (ladders.loc climb_up_ladder / climb_spiral_middle_ladder / climb_down_ladder).
local TREE_LADDERS_UP = {
    { "F0ToF1", "grandtree_ladderbottom", 1, 0 },
    { "F1ToF2", "grandtree_laddermiddle_bottom", 2, 1 },
    { "F2ToF3", "grandtree_laddermiddle_top", 2, 2 },
}
local TREE_LADDERS_DOWN = {
    { "F3ToF2", "grandtree_laddertop", 1, 3 },
    { "F2ToF1", "grandtree_laddermiddle_top", 3, 2 },
    { "F1ToF0", "grandtree_laddermiddle_bottom", 3, 1 },
}

return {
    id = "grandtree",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so the quest's items fit
        "::setlevel agility 25",
        "::setlevel hitpoints 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::give rune_scimitar 1",
        "::give mithril_platebody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 10",
        -- two Camelot Teleports (magic_spells.dbrow magic_spell_teleport_camelot: level 45, 5 air
        -- + 1 law): Lumbridge -> Kandarin past the members' wall, and off Karamja after the glider crash
        "::setlevel magic 45",
        "::give airrune 10",
        "::give lawrune 2",
    },

    run = function(t)
        local function tile_text(r, tl)
            if r ~= "ok" or type(tl) ~= "table" then
                return tostring(r)
            end
            return tostring(tl.x) .. "," .. tostring(tl.z) .. "," .. tostring(tl.level)
        end

        local function camelot_teleport(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })
        end

        -- The Stronghold gate (gnome_gate.rs2 [oploc1,gnome_areagate]): a press from either entrance
        -- force-moves the player three tiles through it; north entrance 2461,3385, south 2461,3382.
        local function gate_in(name)
            t.exec(name, t.player.cross_gate, { loc = "gnome_areagate", at = { 2459, 3383, 0 }, near = { 2461, 3382 },
                far_ok = function(tile) return tile.z >= 3385 end, far_desc = "inside the Stronghold, z >= 3385" })
        end
        local function gate_out(name)
            t.exec(name, t.player.cross_gate, { loc = "gnome_areagate", at = { 2459, 3383, 0 }, near = { 2461, 3385 },
                far_ok = function(tile) return tile.z <= 3382 end, far_desc = "south of the Stronghold gate, z <= 3382" })
        end

        -- The Grand Tree door (gnome_gate.rs2 @open_tree_door): the press puts the player on x 2465 and
        -- force-moves two tiles through the door row z 3492.
        local function tree_door_in(name)
            t.exec(name, t.player.cross_gate, { loc = "treedoorl", at = { 2464, 3492, 0 }, near = { 2465, 3491 },
                far_ok = function(tile) return tile.z >= 3493 and tile.z <= 3498 and tile.x >= 2463 and tile.x <= 2468 end,
                far_desc = "inside the Grand Tree's ground floor, z 3493..3498" })
        end
        local function tree_door_out(name)
            t.exec(name, t.player.cross_gate, { loc = "treedoorl", at = { 2464, 3492, 0 }, near = { 2465, 3493 },
                far_ok = function(tile) return tile.z <= 3491 end, far_desc = "outside the Grand Tree, z <= 3491" })
        end

        -- inside the Stronghold: to the tree door (open ground, reach.py 2461,3386 -> 2465,3489
        -- closed-doors len 111) and through it
        local function into_tree(pfx)
            t.exec("goto-" .. pfx .. ".treeDoor", t.player.goto_tile, 2465, 3489, 0)
            tree_door_in(pfx .. ".treeDoorIn")
        end

        local function climb_tree_up(suffix)
            for _, l in ipairs(TREE_LADDERS_UP) do
                t.exec("climbGrandTree" .. l[1] .. suffix, t.player.climb, { loc = l[2], op = l[3], op_name = "Climb-up",
                    at = { 2466, 3495, l[4] }, src = { 2466, 3494 }, dest = { 2466, 3494, l[4] + 1 } })
            end
        end
        local function climb_tree_down(suffix)
            for _, l in ipairs(TREE_LADDERS_DOWN) do
                t.exec("climbGrandTree" .. l[1] .. suffix, t.player.climb, { loc = l[2], op = l[3], op_name = "Climb-down",
                    at = { 2466, 3495, l[4] }, src = { 2466, 3494 }, dest = { 2466, 3494, l[4] - 1 } })
            end
        end

        -- Glough's house: ladder 2476,3463 (level 0) / laddertop (level 1), +-1 plane on 2476,3462.
        local function glough_up(name)
            t.exec(name, t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 2476, 3463, 0 }, src = { 2476, 3462 }, dest = { 2476, 3462, 1 } })
        end
        local function glough_down(name)
            t.exec(name, t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
                at = { 2476, 3463, 1 }, src = { 2476, 3462 }, dest = { 2476, 3462, 0 } })
        end
        -- tree door -> Glough's ladder foot (reach.py 2465,3490 -> 2476,3462 closed-doors len 41) and back
        local function tree_to_glough(pfx)
            t.exec(pfx .. ".walkToGlough", t.player.walk_route, { { 2467, 3484 }, { 2468, 3477 }, { 2469, 3470 }, { 2473, 3466 }, { 2477, 3462 }, { 2476, 3462 } })
        end
        local function glough_to_tree(pfx)
            t.exec(pfx .. ".walkToTree", t.player.walk_route, { { 2472, 3464 }, { 2466, 3466 }, { 2466, 3474 }, { 2466, 3482 }, { 2465, 3489 }, { 2465, 3490 } })
            tree_door_in(pfx .. ".treeDoorIn")
        end

        local function fight_margin(name, fight, detail)
            local low = tonumber(tostring(detail):match("lowest hp (%d+)/"))
            local left_result, left = t.inv.count("shark")
            t.check(name, low ~= nil and low >= 25 and left_result == "ok" and left ~= nil and left >= 1,
                fight .. ": lowest hp " .. tostring(low) .. "/99 (staged ::setlevel hitpoints 99), sharks staged 10, left "
                    .. tostring(left) .. " (" .. tostring(left_result) .. ") -- margin: lowest hp >= 25 AND a shark left")
        end

        local bind_result, bind_detail = t.quest.bind({
            varp = "varp150_grandtree",
            constants = {
                not_started = 0,
                started = 10,
                spoken_hazelmere = 20,
                relayed_message_narnode = 30,
                spoken_glough = 40,
                found_prisoner = 50,
                spoken_prisoner = 60,
                found_journal = 70,
                released_prison = 80,
                obtained_lumber_order = 90,
                clue_charlie = 100,
                found_invasion_plans = 110,
                given_twigs = 120,
                unlocked_trapdoor = 130,
                defeated_black_demon = 140,
                searching_daconia = 150,
                complete = 160,
            },
            display = "The Grand Tree",
            points = 5,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- the recommended combat kit, worn for the two fights
        for _, item in ipairs({ "rune_scimitar", "mithril_platebody", "rune_platelegs", "rune_full_helm", "rune_kiteshield" }) do
            t.exec("wear." .. item, t.player.equip, item)
        end

        -- Lumbridge -> Kandarin: on foot only through a members' gate, so a real teleport, then the
        -- overland hop Camelot -> south of the Stronghold gate (reach.py 2757,3478 -> 2461,3380
        -- closed-doors len 410).
        camelot_teleport("goToStronghold.camelotTeleport")
        t.exec("goto-goToStronghold.gate", t.player.goto_tile, 2461, 3379, 0)

        -- First entry: while %varp5856_femi_help = 0 a press from the south with Femi within 6 tiles
        -- opens her boxes instead of the gate (gnome_gate.rs2:36-38 -> femi.rs2 @grandtree_femi_boxes).
        -- Helping (option 2) sets femi_help 2, which is what lets her sneak the player in later for free.
        t.exec("goToStronghold.gateApproach", t.player.walk_route, { { 2461, 3382 } })
        local press_result, press_detail = t.player.click_loc("gnome_areagate", 1, { at = { 2459, 3383 } })
        t.await({ level = function()
            if t.chat.kind() ~= "none" then
                return true
            end
            local r, tl = t.world.tile()
            return r == "ok" and tl.z >= 3385
        end, note = "Femi's boxes page or the gate's walk-through" }, 12)
        if t.chat.kind() ~= "none" then
            t.exec("goToStronghold.femiBoxes-dialog", t.chat.play, {
                "npc:Hello there", "player:Hi!", "npc:Could you help me lift", "options", "choose:OK then.",
                "player:OK then", "npc:Thanks traveller" })
            -- if_close, then the player lifts the boxes onto the cart before her last page
            t.expect("goToStronghold.femiBoxes.thanks_page", t.await({ level = function() return t.chat.kind() == "npc" end,
                note = "Femi's thanks after the boxes" }, 20))
            t.exec("goToStronghold.femiBoxes-dialog-2", t.chat.play, { "npc:Thanks again friend", "end" })
            local femi_result, femi_help = t.var.server("varp5856_femi_help")
            t.check("goToStronghold.femiHelped", femi_result == "ok" and femi_help == 2,
                "click_loc gnome_areagate -> " .. tostring(press_result) .. "; varp5856_femi_help = " .. tostring(femi_help)
                    .. " (" .. tostring(femi_result) .. "), want 2: the boxes lifted (femi.rs2 @grandtree_femi_boxes)")
            gate_in("goToStronghold.gateIn")
        else
            -- no page: Femi stood more than 6 tiles off, so the same press was the gate's walk-through
            local first_result, first_tile = t.world.tile()
            t.check("goToStronghold.gateIn", first_result == "ok" and first_tile.level == 0 and first_tile.z >= 3385,
                "click_loc gnome_areagate -> " .. tostring(press_result) .. " " .. tostring(press_detail)
                    .. "; no Femi page; after the press " .. tile_text(first_result, first_tile)
                    .. " (want through the gate from 2461,3382: z >= 3385)")
        end
        into_tree("talkToKingNarnode")

        -- talkToKingNarnode: (make sure to have two empty inventory slots to start the quest)
        t.exec("talkToKingNarnode", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("talkToKingNarnode-dialog-1", t.chat.play, {
            "npc:Welcome Traveller", "player:Hi! It seems", "npc:For now", "options",
            "choose:You seem worried, what's up?", "player:You seem worried", "npc:Traveller, Can I speak",
            "player:Of course sire", "npc:Not here, follow me" })
        t.expect("narnode.caves_page", t.await({ level = function() return t.chat.kind() == "player" end, note = "caves page" }, 60))
        t.exec("talkToKingNarnodeCaves-dialog", t.chat.play, {
            "player:So what is this place", "npc:These, my friend", "player:They look like roots", "npc:Not just any roots",
            "player:Impressive", "npc:In the last two months", "player:You mean the tree is ill", "npc:In effect yes", "options",
            "choose:I'd be happy to help!", "player:I'd be happy to help", "npc:Thank Guthix", "npc:The first task",
            "player:Do you have an idea", "npc:My top tree guardian", "player:Who's Hazelmere", "npc:Hazelmere is one of the mages",
            "mesbox:The king has given you a sample of bark", "npc:The mage only talks",
            "mesbox:The king has given you a translation book", "player:What is it", "npc:It's a translation book",
            "npc:I'll show you the way back up" })
        -- the king walks to the roots' ladder and the player is telejumped back up beside the
        -- tree's trapdoor (king_narnode.rs2:271-290: 0_38_54_32_41 = 2464,3497,0)
        t.expect("narnode.backUp", t.await({ level = function()
            local r, tl = t.world.tile()
            return r == "ok" and tl.level == 0 and tl.z < 6400
        end, note = "Narnode's climb back up out of the roots" }, 40))
        local up_result, up_tile = t.world.tile()
        t.check("narnode.backUp.tile", up_result == "ok" and up_tile.x == 2464 and up_tile.z == 3497 and up_tile.level == 0,
            "after the caves: " .. tile_text(up_result, up_tile) .. " (want 2464,3497,0 inside the Grand Tree)")
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- climbUpToHazelmere: the island east of Yanille. Out of the tree and the Stronghold on foot,
        -- then overland (reach.py 2461,3380 -> 2677,3090 closed-doors len 566) to his hut's door.
        tree_door_out("goToHazelmere.treeDoorOut")
        t.exec("goto-goToHazelmere.gate", t.player.goto_tile, 2461, 3388, 0)
        gate_out("goToHazelmere.gateOut")
        t.exec("goto-climbUpToHazelmere", t.player.goto_tile, 2677, 3090, 0)
        t.exec("climbUpToHazelmere.doorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2677, 3088, 0 }, near = { 2677, 3089 }, far = { 2677, 3088 } })
        t.exec("climbUpToHazelmere", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
            at = { 2677, 3087, 0 }, src = { 2677, 3086 }, dest = { 2677, 3086, 1 } })
        t.exec("talkToHazelmere", t.player.talk_to, "grandtree_hazelmere", 1)
        t.exec("talkToHazelmere-dialog", t.chat.play, {
            "mesbox:The mage starts to speak", "npc:Blah. Blah, blah", "mesbox:You give the bark sample",
            "mesbox:The mage carefully examines", "npc:Blah, blah...Daconia", "player:Can you write this down",
            "npc:Blah, blah?", "mesbox:You make a writing motion", "mesbox:Hazelmere has given you the scroll" })
        t.ticks(2)
        t.expect("quest.stage.spoken_hazelmere", t.quest.expect_stage("spoken_hazelmere"))

        -- bringScrollToKingNarnode: down, out of the hut, overland back to the Stronghold gate
        t.exec("leaveHazelmere.ladderDown", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2677, 3087, 1 }, src = { 2677, 3086 }, dest = { 2677, 3086, 0 } })
        t.exec("leaveHazelmere.doorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2677, 3088, 0 }, near = { 2677, 3088 }, far = { 2677, 3090 } })
        t.exec("goto-bringScrollToKingNarnode.gate", t.player.goto_tile, 2461, 3379, 0)
        gate_in("bringScrollToKingNarnode.gateIn")
        into_tree("bringScrollToKingNarnode")
        t.exec("bringScrollToKingNarnode", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("bringScrollToKingNarnode-dialog", t.chat.play, {
            "player:Hello again, your highness", "npc:Hello Traveller, did you speak", "player:Yes! I managed",
            "npc:Do you understand", "options", "choose:I think so!", "player:I think so", "npc:So what did he say",
            "options", "choose:None of the above.", "options", "choose:None of the above.",
            "options", "choose:A man came to me with the King's seal.", "player:A man came to me",
            "options", "choose:I gave the man Daconia rocks.", "player:I gave the man Daconia rocks",
            "options", "choose:And Daconia rocks will kill the tree!", "player:And Daconia rocks will kill the tree",
            "npc:Of course! I should've known", "player:What are Daconia stones", "npc:Hazelmere created",
            "npc:This is terrible", "player:Can I help", "npc:First I must warn", "npc:If he's not there",
            "player:OK! I'll be back soon" })
        t.ticks(2)
        t.expect("quest.stage.relayed_message_narnode", t.quest.expect_stage("relayed_message_narnode"))

        -- climbUpToGlough / talkToGlough
        tree_door_out("climbUpToGlough.treeDoorOut")
        tree_to_glough("climbUpToGlough")
        glough_up("climbUpToGlough")
        t.exec("talkToGlough", t.player.talk_to, "grandtree_glough", 1)
        t.exec("talkToGlough-dialog", t.chat.play, {
            "player:Hello", "mesbox:The gnome is munching", "npc:Can I help human", "mesbox:The gnome continues to eat",
            "player:The King asked me to inform you", "npc:Surely not", "player:Apparently a human took them",
            "npc:I should've known", "player:Never", "npc:Your type can't be trusted" })
        t.ticks(2)
        t.expect("quest.stage.spoken_glough", t.quest.expect_stage("spoken_glough"))

        -- talkToKingNarnodeAfterGlough
        glough_down("talkToKingNarnodeAfterGlough.ladderDown")
        glough_to_tree("talkToKingNarnodeAfterGlough")
        t.exec("talkToKingNarnodeAfterGlough", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("talkToKingNarnodeAfterGlough-dialog", t.chat.play, {
            "player:Hello, your highness", "npc:It's OK Traveller, thanks to Glough", "player:Wow! That was quick",
            "npc:Yes Glough really knows", "npc:Maybe Glough was right", "player:I doubt it, can I speak to the prisoner",
            "npc:Certainly" })
        t.ticks(2)
        t.expect("quest.stage.found_prisoner", t.quest.expect_stage("found_prisoner"))

        -- the Grand Tree's ladders, floor 0 to floor 3
        climb_tree_up("")
        t.expect("talkToCharlie.present", t.npc.await_present("grandtree_charlie", 8, 10))
        t.exec("talkToCharlie", t.player.talk_to, "grandtree_charlie", 1)
        t.exec("talkToCharlie-dialog", t.chat.play, {
            "player:Tell me. Why would you want to kill", "npc:What do you mean", "player:Don't tell me",
            "npc:All I know is that I did what I was asked", "player:I don't understand", "npc:Glough paid me",
            "npc:I've been doing it for weeks", "player:Sounds like Glough is hiding something",
            "npc:I don't know what he's up to", "player:OK. Thanks Charlie", "npc:Good luck" })
        t.ticks(2)
        t.expect("quest.stage.spoken_prisoner", t.quest.expect_stage("spoken_prisoner"))

        -- back down the tree
        climb_tree_down("")

        -- returnToGlough / findGloughJournal
        tree_door_out("returnToGlough.treeDoorOut")
        tree_to_glough("returnToGlough")
        glough_up("returnToGlough")
        t.exec("findGloughJournal.open", t.player.click_loc, "grandtree_cupboardclosed", 1)
        t.ticks(2)
        t.exec("findGloughJournal", t.player.click_loc, "grandtree_cupboardopen", 2)
        t.exec("findGloughJournal-dialog", t.chat.play, { "mesbox:You've found Glough's Journal" })
        t.ticks(2)
        t.expect("quest.stage.found_journal", t.quest.expect_stage("found_journal"))

        -- talkToGloughAgain: he has you arrested
        t.exec("talkToGloughAgain", t.player.talk_to, "grandtree_glough", 1)
        t.exec("talkToGloughAgain-dialog", t.chat.play, {
            "player:I don't know what you're up to", "npc:You're a fool human", "player:Grand Tree's dying",
            "npc:How dare you accuse", "npc:Guards! Guards!" })
        t.expect("glough.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "Come with me page" }, 40))
        t.exec("glough.commands", t.chat.play, { "npc:Come with me" })
        t.expect("charlie.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "cell page" }, 60))
        t.exec("talkToCharlieFromCell-dialog", t.chat.play, {
            "npc:So they got you as well", "player:It's Glough", "npc:I shouldn't tell you", "npc:But if you want",
            "player:Why?", "npc:Glough sent me to Karamja", "npc:Karamja Shipyard", "player:Thanks Charlie" })
        t.expect("narnode.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "narnode page" }, 60))
        t.exec("talkToKingNarnodeBeforeEscape-dialog-1", t.chat.play, { "npc:Traveller please accept" })
        t.expect("narnode.wait_trust", t.await({ level = function() return t.chat.kind() == "player" end, note = "trust page" }, 60))
        t.exec("talkToKingNarnodeBeforeEscape-dialog-2", t.chat.play, {
            "player:I don't think you can trust Glough", "npc:I know he can be a bit extreme",
            "npc:I'm afraid Glough has placed guards", "player:Well, OK", "npc:I'm sorry again", "end" })
        t.ticks(3)
        t.expect("quest.stage.released_prison", t.quest.expect_stage("released_prison"))

        -- escapeByGlider: the pilot on top of the Grand Tree; the glider crashes on Karamja at
        -- 2917,3058 and does not fly back (gnome_glider.rs2:43-66)
        t.exec("escapeByGlider", t.player.talk_to, "pilot_grand_tree", 1)
        t.exec("escapeByGlider-dialog", t.chat.play, {
            "npc:Hi, the King said", "player:Apparently humans are invading", "npc:I find that hard to believe",
            "player:I don't understand it either", "npc:So where to", "options", "choose:Take me to Karamja please!",
            "player:Take me to Karamja please", "npc:OK! You're the boss" })
        -- The crash pages (gnome_glider.rs2:56-63, LostCity gnome_glider.rs2:101-108) never show here: the
        -- server aborts the script at its first ~chatnpc_anim after the p_teleport ("npc_coord with no
        -- active npc", chat.rs2:273 from gnome_glider.rs2:56: the pilot stayed on the Grand Tree).
        -- Nothing in them moves quest state; the landing is graded below.
        t.ticks(8)
        local crash_result, crash_tile = t.world.tile()
        t.check("escapeByGlider.landed", crash_result == "ok" and crash_tile.level == 0
                and math.abs(crash_tile.x - 2917) <= 2 and math.abs(crash_tile.z - 3058) <= 2,
            "after the glider: " .. tile_text(crash_result, crash_tile) .. " (want the Karamja crash site 0_45_47_37_50 = 2917,3058,0)")

        -- enterTheShipyard: walked from the crash (reach.py 2917,3058 -> 2944,3041 closed-doors len 44);
        -- the worker's password quiz (shipyardworker.rs2 @shipyardworker_gate) opens the gate when he
        -- stands within 5 tiles, else the gate opens at once (grandtree_shipyard_gate.rs2:16-33)
        t.exec("enterTheShipyard.walk", t.player.walk_route, { { 2924, 3057 }, { 2931, 3056 }, { 2939, 3056 }, { 2943, 3052 }, { 2944, 3045 }, { 2944, 3041 } })
        t.exec("enterTheShipyard", t.player.cross_gate, { loc = "grandtree_fencegate_l", at = { 2945, 3041, 0 },
            near = { 2944, 3041 }, far_ok = function(tile) return tile.x >= 2945 end, far_desc = "inside the shipyard, x >= 2945",
            chat = {
                "npc:What are you up to", "player:trying to open the gate", "npc:I can see that", "options",
                "choose:Glough sent me.", "player:Glough sent me", "npc:really", "player:wasting my time", "npc:Password",
                "options", "choose:Ka.", "player:Ka.", "options", "choose:Lu.", "player:Lu.", "options", "choose:Min.", "player:Min.",
                "npc:Sorry to have kept you" },
            chat_optional = "the shipyard worker quizzes only within 5 tiles of the gate (grandtree_shipyard_gate.rs2:17)" })

        -- talkToForeman: inside the yard (comp.py: one fenced component, the gate its only exit),
        -- walked to the foreman; the wrong answer makes him attack; kill him for the lumber order
        t.exec("talkToForeman.walk", t.player.walk_route, { { 2952, 3039 }, { 2956, 3035 }, { 2962, 3033 }, { 2966, 3037 },
            { 2969, 3042 }, { 2975, 3044 }, { 2981, 3046 }, { 2986, 3049 }, { 2993, 3048 }, { 3000, 3049 }, { 3000, 3043 } })
        t.exec("talkToForeman", t.player.talk_to, "grandtree_foreman", 1)
        t.exec("talkToForeman-dialog-1", t.chat.play, {
            "player:Hello, are you in charge", "npc:That's right", "player:Glough sent me", "npc:Right. Glough sent a human",
            "player:His gnomes are busy", "npc:Hmm", "npc:Follow me", "end" })
        t.expect("foreman.wait_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "office page" }, 80))
        t.exec("talkToForeman-dialog-2", t.chat.play, {
            "npc:Tell me again", "player:Er", "npc:By the way how is Glough", "options",
            "choose:Yes, they're getting on great.", "player:Yes, they're getting on great", "npc:Really? That's odd", "end" })
        local _, foreman_detail = t.exec("talkToForeman.fight", t.npc.await_dead, "grandtree_foreman", 200, 12, 8,
            { eat = { item = "shark", below = 60 } })
        fight_margin("talkToForeman.margin", "the foreman (level 23)", foreman_detail)
        t.ticks(3)
        t.exec("talkToForeman.order", t.player.click_obj, "grandtree_order", 3)
        t.expect("quest.stage.obtained_lumber_order", t.quest.expect_stage("obtained_lumber_order"))

        -- goTalkToCharlie3: off Karamja by Camelot Teleport, overland to the Stronghold gate, where
        -- the guards turn the player away (gnome_gate.rs2:24-31) and Femi sneaks you past it
        camelot_teleport("gnomeGate.camelotTeleport")
        t.exec("goto-gnomeGate", t.player.goto_tile, 2461, 3379, 0)
        t.exec("gnomeGate.approach", t.player.walk_route, { { 2461, 3382 } })
        t.exec("gnomeGate", t.player.click_loc, "gnome_areagate", 1, { at = { 2459, 3383 } })
        t.exec("gnomeGate-dialog", t.chat.play, {
            "npc:I'm afraid that we have orders", "player:Orders from who", "npc:The head tree guardian",
            "player:Glough!", "npc:I'm sorry but you'll have to leave", "end" })
        local refused_result, refused_tile = t.world.tile()
        t.check("gnomeGate.refused", refused_result == "ok" and refused_tile.z <= 3382 and refused_tile.level == 0,
            "after the guard's refusal: " .. tile_text(refused_result, refused_tile) .. " (want still south of the gate, z <= 3382)")
        t.exec("femi", t.player.talk_to, "grandtree_femi", 1)
        t.exec("femi-dialog", t.chat.play, {
            "player:I can't believe they won't let me in", "npc:I don't believe all this rubbish",
            "player:I really need to see King Narnode", "npc:Well, as you helped me", "player:OK, what should I do",
            "npc:Jump in the back of the cart" })
        -- femi.rs2 @grandtree_femi_sneakin: the cart run lands at 0_38_53_27_17 = 2459,3409 inside
        t.expect("femi.sneakin_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "Femi's farewell page" }, 30))
        t.exec("femi.sneakin-dialog", t.chat.play, { "npc:OK, traveller", "player:Thanks again", "npc:That's OK, all the best" })
        local sneak_result, sneak_tile = t.world.tile()
        t.check("femi.sneakin", sneak_result == "ok" and sneak_tile.level == 0 and sneak_tile.z >= 3400,
            "after the cart: " .. tile_text(sneak_result, sneak_tile) .. " (want inside the Stronghold, 2459,3409,0)")

        -- climb to Charlie again
        into_tree("goTalkToCharlie3")
        climb_tree_up("-again")
        t.expect("talkToCharlie3.present", t.npc.await_present("grandtree_charlie", 8, 10))
        t.exec("talkToCharlie3", t.player.talk_to, "grandtree_charlie", 1)
        t.exec("talkToCharlie3-dialog", t.chat.play, {
            "player:How are you doing Charlie", "npc:I've been better", "player:Glough has some plan",
            "npc:wouldn't put it past him", "player:need some proof", "npc:you could be in luck",
            "player:Where does she live", "npc:west of the toad swamp", "player:see what I can find", "end" })
        t.ticks(2)
        t.expect("quest.stage.clue_charlie", t.quest.expect_stage("clue_charlie"))
        climb_tree_down("-again")

        -- climbUpToAnita / talkToAnita: her staircase's maplink rows (maplink.dbrow
        -- maplink_0_37_54_23_56: 2391,3512 -> 2388,3513,1; down 2388,3513,1 -> 2389,3514,0)
        tree_door_out("climbUpToAnita.treeDoorOut")
        t.exec("goto-climbUpToAnita", t.player.goto_tile, 2391, 3511, 0)
        t.exec("climbUpToAnita", t.player.climb, { loc = "spiralstairs_wooden", op = 1, op_name = "Climb-up",
            at = { 2389, 3512, 0 }, src = { 2391, 3512 }, dest = { 2388, 3513, 1 } })
        t.exec("talkToAnita", t.player.talk_to, "grandtree_anita", 1)
        t.exec("talkToAnita-dialog", t.chat.play, {
            "player:Hello there", "npc:Oh hello, I've seen you with the King", "player:Yes, I'm helping him",
            "npc:You must know my boyfriend Glough", "player:Indeed", "npc:Could you do me a favour",
            "player:I suppose so", "npc:Please give this key", "mesbox:Anita gives you a key", "npc:Thanks a lot",
            "player:No...thank you" })
        t.exec("leaveAnita.stairsDown", t.player.climb, { loc = "spiralstairstop_wooden", op = 1, op_name = "Climb-down",
            at = { 2389, 3513, 1 }, src = { 2388, 3513 }, dest = { 2389, 3514, 0 }, slack = 1 })

        -- findInvasionPlans: Glough's key opens his chest (open ground, reach.py 2389,3514 ->
        -- 2476,3462 closed-doors len 141)
        t.exec("goto-climbUpToGloughAgain", t.player.goto_tile, 2476, 3461, 0)
        glough_up("climbUpToGloughAgain")
        t.exec("findInvasionPlans", t.player.use_on, "grandtree_gloughskey", t.player.by_symbol("loc", "grandtree_chestclosed"))
        t.exec("findInvasionPlans-dialog", t.chat.play, { "mesbox:You have found a scroll" })
        t.ticks(2)
        t.expect("quest.stage.found_invasion_plans", t.quest.expect_stage("found_invasion_plans"))

        -- takeInvasionPlansToKing
        glough_down("takeInvasionPlansToKing.ladderDown")
        glough_to_tree("takeInvasionPlansToKing")
        t.exec("takeInvasionPlansToKing", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("takeInvasionPlansToKing-dialog", t.chat.play, {
            "player:Hi, your highness, did you think", "npc:Look, if you're right about Glough",
            "player:Look, I found this at Glough's home", "mesbox:You give the King the invasion plans",
            "npc:If these are to be believed", "npc:But it's not proof", "mesbox:The King has given you some twigs",
            "npc:On the other hand", "npc:The Grand Tree's still slowly dying" })
        t.ticks(2)
        t.expect("quest.stage.given_twigs", t.quest.expect_stage("given_twigs"))

        -- climbUpToGloughForWatchtower / climbUpToWatchtower / placeTwigs
        tree_door_out("climbUpToGloughForWatchtower.treeDoorOut")
        tree_to_glough("climbUpToGloughForWatchtower")
        glough_up("climbUpToGloughForWatchtower")
        -- grandtree_locs_climb.rs2 [oploc1,grandtree_climbtree]: agility 25, telejump to loc +2,+1, one plane up
        t.exec("climbUpToWatchtower", t.player.climb, { loc = "grandtree_climbtree", op = 1, op_name = "Climb-up",
            at = { 2484, 3464, 1 }, dest = { 2486, 3465, 2 } })
        t.exec("placeTwigsT", t.player.use_on, "grandtree_twigt", t.player.by_symbol("loc", "grandtree_pillart"))
        t.exec("placeTwigsU", t.player.use_on, "grandtree_twigu", t.player.by_symbol("loc", "grandtree_pillaru"))
        t.exec("placeTwigsZ", t.player.use_on, "grandtree_twigz", t.player.by_symbol("loc", "grandtree_pillarz"))
        t.exec("placeTwigsO", t.player.use_on, "grandtree_twigo", t.player.by_symbol("loc", "grandtree_pillaro"))
        t.ticks(2)
        t.expect("quest.stage.unlocked_trapdoor", t.quest.expect_stage("unlocked_trapdoor"))

        -- climbDownTrapDoor: Glough's black demon. The tower trapdoor (jl2 m38_54 level 2 55,8 =
        -- 2487,3464) teleports to 0_38_154_59_8 = 2491,9864 (grandtree_locs_climb.rs2 @grandtree_trapdoor_enter)
        t.exec("climbDownTrapDoor", t.player.climb, { loc = "grandtree_trapdoortoweropen", op = 1, op_name = "Climb-down",
            at = { 2487, 3464, 2 }, dest = { 2491, 9864, 0 }, slack = 1 })
        t.expect("glough.page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "Glough headache page" }, 60))
        t.exec("climbDownTrapDoor-dialog", t.chat.play, {
            "npc:You really are becoming a headache", "player:You're crazy Glough", "npc:Bah! Well, soon you'll see",
            "player:What makes you think", "npc:Fool...meet my little friend", "end" })
        -- Glough's demon cutscene (glough.rs2:143-155): cam_shake, cam_moveto the tower floor, cam_lookat
        -- the tile beside the player, and cam_reset once the demon has walked in.
        t.exec("climbDownTrapDoor.cutscene", t.cutscene.await, "climbDownTrapDoor", { expect = {
            { op = "moveto", coord = "0_38_154_44_13", height = 2000 },
            { op = "lookat" },
            { op = "reset" },
        } })
        t.expect("demon.present", t.npc.await_present("grandtree_blackdemon", 20, 40))
        local _, demon_detail = t.exec("killBlackDemon", t.npc.await_dead, "grandtree_blackdemon", 400, 20, 10,
            { eat = { item = "shark", below = 60 } })
        fight_margin("killBlackDemon.margin", "the black demon (level 172)", demon_detail)
        t.ticks(3)
        t.expect("quest.stage.defeated_black_demon", t.quest.expect_stage("defeated_black_demon"))

        -- climbDownTrapDoorAfterFight / talkToKingAfterFight: the demon's tunnel runs into the roots
        -- (reach.py 2491,9864 -> 2465,9896 closed-doors len 128): one dungeon, open tile to open tile
        t.exec("goto-talkToKingAfterFight", t.player.goto_tile, 2465, 9896, 0)
        t.exec("talkToKingAfterFight", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("talkToKingAfterFight-dialog-1", t.chat.play, {
            "npc:Traveller you're wounded", "player:It's Glough", "npc:What?! Glough", "player:Glough has a store",
            "npc:Never! Not Glough" })
        t.expect("king.guard_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "guard page" }, 40))
        t.exec("talkToKingAfterFight-dialog-2", t.chat.play, { "npc:Guard!" })
        t.expect("king.sire_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "guard Sire page" }, 60))
        t.exec("talkToKingAfterFight-dialog-3", t.chat.play, { "npc:Sire!", "npc:Go and check" })
        t.expect("king.found_page", t.await({ level = function() return t.chat.kind() == "npc" end, note = "guard found Glough page" }, 60))
        t.exec("talkToKingAfterFight-dialog-4", t.chat.play, {
            "npc:We found Glough", "player:That's what I've been trying to tell you", "npc:I..I don't know what to say",
            "npc:Guard! Call off", "npc:The humans are not attacking", "npc:Yes sir", "npc:You have my full apologies",
            "npc:And my gratitude", "npc:A reward will have to wait", "npc:Help us search, we have little time!", "end" })
        t.ticks(2)
        t.expect("quest.stage.searching_daconia", t.quest.expect_stage("searching_daconia"))

        -- findDaconiaStone: the root the quest picked (daconia_coords), absolute x = 2432 + X, z = 9856 + Z.
        -- Every root's south tile is walkable from the king (reach.py 2465,9896 -> each, closed-doors len 3..43).
        local roots = {
            { "largeroot_gnome", 2456, 9886 }, { "largeroot_gnome", 2456, 9886 }, { "largeroot2_gnome", 2457, 9881 },
            { "largeroot2_gnome", 2455, 9874 }, { "largeroot_gnome", 2443, 9878 }, { "largeroot2_gnome", 2439, 9881 },
            { "largeroot2_gnome", 2444, 9893 }, { "largeroot_gnome", 2452, 9893 }, { "largeroot2_gnome", 2465, 9891 },
            { "largeroot2_gnome", 2468, 9890 }, { "largeroot_gnome", 2467, 9896 }, { "largeroot_gnome", 2473, 9897 },
            { "largeroot2_gnome", 2481, 9904 }, { "largeroot_gnome", 2485, 9885 }, { "largeroot_gnome", 2490, 9889 },
            { "largeroot2_gnome", 2467, 9872 },
        }
        local root_result, root_index = t.var.server("varp5869_daconia_rock_root")
        t.check("findDaconiaStone.root", root_result == "ok" and root_index ~= nil and roots[root_index + 1] ~= nil,
            "daconia_rock_root = " .. tostring(root_index))
        local root = roots[(root_index or 0) + 1]
        t.exec("findDaconiaStone.walk", t.player.walk_route, { { root[2], root[3] - 1 } }, { max_hop = 40 })
        t.exec("findDaconiaStone", t.player.click_loc, root[1], 1, { at = { root[2], root[3], 0 } })
        t.exec("findDaconiaStone-dialog", t.chat.play, { "mesbox:You've found a Daconia Rock" })
        t.exec("findDaconiaStone.have", t.inv.expect_has, "grandtree_daconiarock", 1)

        -- giveDaconiaStoneToKingNarnode
        t.exec("giveDaconiaStoneToKingNarnode.walk", t.player.walk_route, { { 2465, 9896 } }, { max_hop = 40 })
        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("giveDaconiaStone.snapshot", snapshot_result == "ok", "snapshot " .. tostring(snapshot_result))
        t.exec("giveDaconiaStoneToKingNarnode", t.player.talk_to, "grandtree_narnode", 1)
        t.exec("giveDaconiaStoneToKingNarnode-dialog", t.chat.play, {
            "npc:Traveller, have you managed to find the Daconia", "player:Is this it", "npc:Yes! Excellent, well done",
            "mesbox:You give the King the Daconia rock", "npc:It's incredible", "npc:To think Glough had me fooled",
            "player:All that matters now", "npc:I'll drink to that", "npc:From now on I vow", "player:Thanks!",
            "player:I think!", "npc:It should make your stay", "player:Mine?", "npc:Very few know", "player:Strange!",
            "npc:That's magic trees for you", "npc:All the best Traveller", "player:You too, your highness", "end" })
        t.ticks(4)
        t.quest.expect_complete()

        -- rewards: 18400 Attack, 7900 Agility, 2150 Magic experience, 5 quest points
        t.expect("reward.attack_xp", t.skill.expect_gain("attack", 18400, snapshot))
        t.expect("reward.agility_xp", t.skill.expect_gain("agility", 7900, snapshot))
        t.expect("reward.magic_xp", t.skill.expect_gain("magic", 2150, snapshot))
        t.finish(0)
    end,
}
