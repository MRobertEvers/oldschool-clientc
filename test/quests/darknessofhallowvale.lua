-- Darkness of Hallowvale -- driven as a 6-leg relay (docs/quest_authoring/relay.md).
-- Leg 1 = guide steps 1-11 (climbOverBrokenWall .. talkToCitizen).
-- Notes: docs/quests/ladders/darknessofhallowvale.notes.md.
-- Readings for row details (file-level helpers, no state shared between legs).
local function at(t)
    local _, p = t.world.tile()
    return tostring(p.x) .. "," .. tostring(p.z) .. "," .. tostring(p.level)
end
local function said(t, n)
    local _, l = t.msg.last(n)
    local o = {}
    for i = 1, #l do
        local m = l[i]
        o[#o + 1] = type(m) == "table" and tostring(m.text or m.message or m[1]) or tostring(m)
    end
    return table.concat(o, " | ")
end

local function door_click(t, name, sym, x, y)
    -- leg 5 opened these doors a few ticks ago on a continuous run; a door still standing open has no closed copy to click
    local ok, why = t.player.click_loc(sym, 1, { at = { x, y } })
    if ok then
        t.check(name, true, "clicked " .. sym .. " at " .. x .. "," .. y)
    elseif string.find(tostring(why), "no copy of", 1, true) then
        t.check(name, true, "door " .. x .. "," .. y .. " is still open from leg 5 (no closed copy to click)")
    else
        t.check(name, false, tostring(why))
    end
end

return {
    id = "darknessofhallowvale",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- six legs of ticks add up past the 2,000-tick default budget (relay.md)
    setup = {
        "::clearinv",
        "::complete quest_priestinperil", -- guide prerequisite chain: Priest in Peril (In Aid of the Myreque needs it; Drezel's cellar shell is drawn only for varp302 8..61)
        "::complete quest_inaidofthemyreque", -- guide prerequisite: In Aid of the Myreque
        "::darknessofhallowvale", -- stages the quest at the top (doh_shared.rs2:410), never past it
        "::give hammer 1", -- guide: usePlankOnBoat items (Hammer)
        "::give woodplank 2", -- guide: usePlankOnBoat/usePlankOnChute items (Plank, one each)
        "::give knife 1", -- guide: travelToMyrequeBase items (Knife, for the hideout wall)
        "::give nails 8", -- guide: usePlankOnBoat/usePlankOnChute items (four nails each)
        "::setlevel construction 5", -- requirement: Construction 5
        "::setlevel mining 20", -- requirement: Mining 20
        "::setlevel thieving 22", -- requirement: Thieving 22
        "::setlevel agility 26", -- requirement: Agility 26
        "::setlevel crafting 32", -- requirement: Crafting 32
        "::setlevel magic 33", -- requirement: Magic 33
        "::setlevel strength 40", -- requirement: Strength 40
    },
    bind = {
        varp = "varb2573_myq3_main_quest",
        constants = {
            not_started = 0,
            started = 10,
            boat_fixed = 20,
            chute_fixed = 30,
            arrived_wall = 40,
            floor_kicked = 50,
            ral_directions = 60,
            complete = 320,
        },
        row = "quest_darknessofhallowvale",
        display = "Darkness of Hallowvale",
        points = 2,
    },

    legs = {
        {
            name = "burgh_to_citizen",
            run = function(t)
                -- LEG 1 BEGIN: climbOverBrokenWall
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

                -- climbOverBrokenWall: the Broken wall on the inn's north edge (doh_burgh.rs2:14)
                t.exec("goto-climbOverBrokenWall", t.player.goto_tile, 3491, 3229, 0)
                t.exec("climbOverBrokenWall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                t.check("climbOverBrokenWall.crossed", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- enterBurghPubBasement: Open then Climb-down the pub trapdoor (doh_burgh.rs2:36)
                t.exec("enterBurghPubBasement.open", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
                t.ticks(3)
                t.exec("enterBurghPubBasement", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
                t.ticks(6)
                t.check("enterBurghPubBasement.below", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- talkToVeliaf: Veliaf asks for the Meiyerditch contact (doh_burgh.rs2:101)
                t.exec("talkToVeliaf", t.player.talk_to, "myq5_veliaf_child", 1)
                t.exec("talkToVeliaf-dialog", t.chat.play, {
                    "player:Is there something I can do to help out?",
                    "npc:Actually, yes",
                    "choose:Yes.",
                    "player:Yes.",
                    "npc:Good. There's a boat",
                })
                t.ticks(2)
                t.expect("quest.stage.started", t.quest.expect_stage("started"))

                -- leavePubBasement: the ladder back up (myreque2_burgh.rs2)
                t.exec("leavePubBasement", t.player.click_loc, "burgh_inn_basement_ladderup", 1)
                t.ticks(5)
                t.check("leavePubBasement.up", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- back over the Broken wall (south side needs no Agility), then to the boathouse
                t.exec("leaveInn.wall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                t.check("leaveInn.outside", true, "tile " .. at(t))
                t.exec("goto-usePlankOnBoat", t.player.goto_tile, 3524, 3177, 0)

                -- usePlankOnBoat: Hammer, Plank, Nails; Yes to "Repair the boat?" (doh_burgh.rs2:179)
                t.exec("usePlankOnBoat", t.player.click_loc, "sang_boat_broken_multiloc", 1)
                t.exec("usePlankOnBoat-dialog", t.chat.play, { "choose:Yes." })
                t.ticks(4)
                t.expect("quest.stage.boat_fixed", t.quest.expect_stage("boat_fixed"))
                t.check("usePlankOnBoat.spent", true, "planks " .. tostring(select(2, t.inv.count("woodplank"))) .. " nails " .. tostring(select(2, t.inv.count("nails"))) .. " messages: " .. said(t, 2))

                -- usePlankOnChute (doh_burgh.rs2:206)
                t.exec("goto-usePlankOnChute", t.player.goto_tile, 3523, 3174, 0)
                t.exec("usePlankOnChute", t.player.click_loc, "sang_boathouse_chute_broken_multiloc", 1)
                t.exec("usePlankOnChute-dialog", t.chat.play, { "choose:Yes." })
                t.ticks(4)
                t.expect("quest.stage.chute_fixed", t.quest.expect_stage("chute_fixed"))

                -- pushBoat: the mended boat goes down the chute (doh_burgh.rs2:179)
                t.exec("goto-pushBoat", t.player.goto_tile, 3524, 3177, 0)
                t.exec("pushBoat", t.player.click_loc, "sang_boat_broken_multiloc", 1)
                t.ticks(6)
                t.check("pushBoat.pushed", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- boardBoat: the boat afloat (doh_burgh.rs2:230)
                t.exec("goto-boardBoat", t.player.goto_tile, 3523, 3171, 0)
                t.exec("boardBoat", t.player.click_loc, "sang_boat_water_multiloc", 1)
                t.ticks(8)
                t.expect("quest.stage.arrived_wall", t.quest.expect_stage("arrived_wall"))
                t.check("boardBoat.arrived", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- the deck has no way off but the rock two tiles north (doh_meiyerditch.rs2:16), then Climb-up
                t.exec("jumpBoatRock", t.player.click_loc, "sang_boat_jump_rock", 1)
                t.ticks(4)
                t.check("jumpBoatRock.on", true, "tile " .. at(t))
                t.exec("climbWallRock", t.player.click_loc, "sang_boat_wall_climb_up_rock", 1)
                t.ticks(5)
                t.check("climbWallRock.up", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- kickBoard: Search the marked floorboards, Yes to kick them in (doh_meiyerditch.rs2:47)
                t.exec("walk-kickBoard", t.player.goto_tile, 3590, 3173, 1)
                t.exec("kickBoard", t.player.click_loc, "meiyerditch_wall_floorboards_multi_loc", 1)
                t.exec("kickBoard-dialog", t.chat.play, { "choose:Yes." })
                t.ticks(4)
                t.expect("quest.stage.floor_kicked", t.quest.expect_stage("floor_kicked"))
                t.check("kickBoard.kicked", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- climbDownBoard: the hole is now an ordinary Climb-down
                t.exec("climbDownBoard", t.player.click_loc, "meiyerditch_wall_floorboards_multi_loc", 1)
                t.ticks(5)
                t.check("climbDownBoard.below", true, "tile " .. at(t) .. " messages: " .. said(t, 2))

                -- the corridor's breach: Climb-over the rubble to the city side (doh_meiyerditch.rs2:70)
                t.exec("climbRubble", t.player.click_loc, "myq3_rubble_west_wall", 1)
                t.ticks(4)
                t.check("climbRubble.over", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("goto-talkToCitizen", t.player.goto_tile, 3596, 3214, 0) -- the citizen stands at 3597,3214; the east side is walled off

                -- talkToCitizen: whisper about the Myreque (doh_meiyerditch.rs2:123)
                t.exec("talkToCitizen", t.player.talk_to, "myq3_citizen_male_old_1", 1)
                t.exec("talkToCitizen-dialog", t.chat.play, {
                    "player:(whisper) Do you know about the Myreque?",
                    "npc:(whisper) Keep your voice down",
                    "choose:(whisper) I really need to meet the Myreque.",
                    "player:(whisper) I really need",
                    "npc:(whisper) I can't help you",
                    "choose:How can Old Man Ral help me?",
                    "player:How can Old Man Ral",
                    "npc:(whisper) He knows the paths",
                })
                t.ticks(3)
                t.expect("quest.stage.ral_directions", t.quest.expect_stage("ral_directions"))

                local _, stage = t.quest.stage()
                t.check("leg.1.end", true, "tile " .. at(t) .. " stage varb2573_myq3_main_quest=" .. tostring(stage) .. " (ral_directions); backpack: hammer " .. tostring(select(2, t.inv.count("hammer"))) .. ", planks " .. tostring(select(2, t.inv.count("woodplank"))) .. ", nails " .. tostring(select(2, t.inv.count("nails"))) .. "; next: Old Man Ral, southwest of the city")
                -- LEG 1 END
            end,
        },
        {
            name = "ral_to_vertida",
            run = function(t)
                -- LEG 2 BEGIN: talkToRal
                t.ticks(2)
                t.check("leg.2.start", true, "tile " .. at(t))
                t.exec("goto-talkToRal", t.player.goto_tile, 3602, 3206, 0)
                t.exec("talkToRal.door", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3602, 3208 } })
                t.ticks(3)
                t.exec("talkToRal", t.player.talk_to, "sanguinesti_old_man_ral", 1)
                t.exec("talkToRal-dialog", t.chat.play, {
                    "player:Someone said you could help me.",
                    "npc:Oh yes? And who told you that",
                    "choose:Old Man Ral, the sage of Sanguinesti.",
                    "player:Old Man Ral, the sage",
                    "npc:Ha! Then you've been sent by one who knows",
                })
                t.exec("talkToRal-dialog.rest", t.chat.drain, { max_pages = 6 })
                t.ticks(3)
                t.expect("quest.stage.course", t.quest.expect_stage(65))
                -- COURSE: ladder to the roofs (obstacle 1)
                t.exec("course.door1", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3597, 3205 } })
                t.ticks(3)
                t.exec("course.ladder1", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3595, 3204 } })
                t.ticks(4)
                t.check("course.ladder1.up", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump2", t.player.click_loc, "myq3_agil_2_jump_south", 1)
                t.ticks(4)
                t.check("course.jump2.across", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump3", t.player.click_loc, "myq3_agil_3_jump_east", 1)
                t.ticks(4)
                t.check("course.jump3.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.push4", t.player.click_loc, "myq3_agil_4_pushwall_multi", 1)
                t.ticks(4)
                t.check("course.push4.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.floor4", t.player.click_loc, "myq3_agil_4_active_floor_1", 1)
                t.ticks(4)
                t.check("course.floor4.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.crawl5", t.player.click_loc, "myq3_agil_5_crawl_wall", 1)
                t.ticks(4)
                t.check("course.crawl5.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.push6", t.player.click_loc, "myq3_agil_6_pushwall_multi", 1)
                t.ticks(4)
                t.check("course.push6.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.floor6", t.player.click_loc, "myq3_agil_6_active_floor_1", 1)
                t.ticks(4)
                t.check("course.floor6.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder7", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3601, 3215 } })
                t.ticks(4)
                t.check("course.ladder7.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.table8a", t.player.click_loc, "myq3_agil_8_trapdoor_tunnel_multi", 1)
                t.ticks(4)
                t.check("course.table8a.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.table8b", t.player.click_loc, "myq3_agil_8_trapdoor_tunnel_multi", 1)
                t.ticks(4)
                t.check("course.table8b.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.tunnel8c", t.player.click_loc, "myq3_agil_8_trapdoor_tunnel_multi", 1)
                t.ticks(4)
                t.check("course.tunnel8c.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf10", t.player.click_loc, "myq3_agil_10_shelf_climb_up", 1)
                t.ticks(4)
                t.check("course.shelf10.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.crawl11", t.player.click_loc, "myq3_agil_11_crawl_wall", 1)
                t.ticks(4)
                t.check("course.crawl11.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump12", t.player.click_loc, "myq3_agil_12_jump_east", 1)
                t.ticks(4)
                t.check("course.jump12.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder13", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3603, 3222 } })
                t.ticks(4)
                t.check("course.ladder13.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("travelToPots", t.player.click_loc, "myq3_ghetto_pots_search", 1)
                t.ticks(4)
                t.check("travelToPots.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.door14", t.player.click_loc, "myq3_agil_14_locked_door", 1)
                t.ticks(4)
                t.check("course.door14.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder16", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3618, 3219 } })
                t.ticks(4)
                t.check("course.ladder16.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump17", t.player.click_loc, "myq3_agil_17_jump_south", 1)
                t.ticks(4)
                t.check("course.jump17.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf18", t.player.click_loc, "myq3_agil_18_shelf_climb_up", 1)
                t.ticks(4)
                t.check("course.shelf18.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder19", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3610, 3210 } })
                t.ticks(4)
                t.check("course.ladder19.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump20", t.player.click_loc, "myq3_agil_20_jump_south", 1)
                t.ticks(4)
                t.check("course.jump20.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder21", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3612, 3203 } })
                t.ticks(4)
                t.check("course.ladder21.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.line22", t.player.click_loc, "myq3_agil_22_tightrope_east", 1)
                t.ticks(4)
                t.check("course.line22.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder23", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3625, 3203 } })
                t.ticks(4)
                t.check("course.ladder23.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.push24", t.player.click_loc, "myq3_agil_24_pushwall_multi", 1)
                t.ticks(4)
                t.check("course.push24.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.floor24", t.player.click_loc, "myq3_agil_24_active_floor_1", 1)
                t.ticks(4)
                t.check("course.floor24.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf25", t.player.click_loc, "myq3_agil_25_shelf_climb_up", 1)
                t.ticks(4)
                t.check("course.shelf25.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf26", t.player.click_loc, "myq3_agil_26_shelf_climb_down", 1)
                t.ticks(4)
                t.check("course.shelf26.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump27", t.player.click_loc, "myq3_agil_27_jump_north", 1)
                t.ticks(4)
                t.check("course.jump27.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump29", t.player.click_loc, "myq3_agil_29_jump_north", 1)
                t.ticks(4)
                t.check("course.jump29.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump30", t.player.click_loc, "myq3_agil_30_jump_east", 1)
                t.ticks(4)
                t.check("course.jump30.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder31", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3630, 3239 } })
                t.ticks(4)
                t.check("course.ladder31.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("travelToLadderPart", t.player.click_loc, "myq3_agil_32_ladder_wall_multi", 1)
                t.ticks(4)
                t.check("travelToLadderPart.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder32d", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3630, 3239 } })
                t.ticks(4)
                t.check("course.ladder32d.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("travelToFixLadder", t.player.click_loc, "myq3_agil_33_ladder_floor_multi", 1)
                t.ticks(4)
                t.check("travelToFixLadder.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder33d", t.player.click_loc, "myq3_agil_33_ladder_floor_multi", 1)
                t.ticks(4)
                t.check("course.ladder33d.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- the fixed ladder lands at 3629,3239,0 inside a room whose only way out is the door 3631,3240
                t.exec("course.door3631", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3631, 3240 } })
                t.ticks(3)
                t.check("course.door3631.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- travel to the roof ladder north of the courtyard, then over the roofs to the north room
                -- travel north to the room above the hideout: door 3628,3250, the pocket, door 3625,3252, the lane west of
                -- the houses, then east along z 3254-3256 to the north room (the jump boards 41 and the stairs are a roof
                -- detour; the ground path reaches 3636,3256 as well)
                t.player.walk_to(3633, 3240, 10)
                t.ticks(2)
                t.exec("course.door3628", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3628, 3250 } })
                t.ticks(3)
                t.player.walk_to(3626, 3253, 20)
                t.ticks(1)
                t.exec("course.door3625", t.player.click_loc, "area_sanguine_ghetto_door2", 1, { at = { 3625, 3252 } })
                t.ticks(3)
                t.player.walk_to(3631, 3257, 40)
                t.ticks(1)
                t.player.walk_to(3631, 3261, 40)
                t.ticks(2)
                t.check("course.walkRoofLadder", true, "tile " .. at(t) .. " (north of door 3631,3259; the north room is only open from the roof)")
                t.exec("course.door3631n", t.player.click_loc, "area_sanguine_ghetto_door2", 1, { at = { 3631, 3259 } })
                t.ticks(3)
                t.exec("course.ladder3631", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3631, 3258 } })
                t.ticks(4)
                t.check("course.ladder3631.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.player.walk_to(3632, 3256, 10)
                t.ticks(2)
                t.exec("course.jump41", t.player.click_loc, "myq3_agil_41_jump_east", 1)
                t.ticks(4)
                t.check("course.jump41.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.stairs3639", t.player.click_loc, "area_sanguine_ghetto_stairs_down", 1, { at = { 3639, 3256 } })
                t.ticks(4)
                t.check("course.stairs3639.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                local wall = t.player.by_symbol("loc", "area_sanguine_myreque_secret_wall_closed")
                t.exec("travelToMyrequeBase", t.player.use_on, "knife", wall)
                t.ticks(4)
                t.expect("quest.stage.wall_found", t.quest.expect_stage(70))
                t.exec("course.wallpush", t.player.click_loc, "area_sanguine_myreque_secret_wall_closed", 1)
                t.ticks(5)
                t.check("course.wallpush.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("pressDecoratedWall", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(4)
                t.check("pressDecoratedWall.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("enterRug", t.player.click_loc, "sang_myreque_hideout_rug_trapdoor_unhidden", 1)
                t.ticks(4)
                t.expect("quest.stage.at_hideout", t.quest.expect_stage(80))
                t.exec("enterRug.down", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                t.check("enterRug.down.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.player.walk_to(3629, 9640)
                t.ticks(2)
                t.check("talkToVertida.near", true, "tile " .. at(t))
                t.exec("talkToVertida", t.player.talk_to, "myq4_vertida_visible", 1)
                t.exec("talkToVertida-dialog", t.chat.drain, { max_pages = 8 })
                t.ticks(3)
                t.expect("quest.stage.vertida_met", t.quest.expect_stage(90))
                t.check("leg.2.end", true, "tile " .. at(t) .. ", stage varb2573_myq3_main_quest=90, backpack hammer, knife, Vertida's message for Veliaf")
                -- LEG 2 END
            end,
        },
        {
            name = "veliaf_to_mines",
            run = function(t)
                -- LEG 3 BEGIN: talkToVeliafAfterContact
                t.ticks(2)
                t.check("leg.3.start", true, "tile " .. at(t))
                -- talkToVeliafAfterContact: back up to Burgh de Rott (plain travel), over the Broken wall,
                -- down the pub trapdoor; Vertida's message to Veliaf (doh_burgh.rs2:86)
                t.exec("goto-talkToVeliafAfterContact", t.player.goto_tile, 3491, 3229, 0)
                t.exec("talkToVeliafAfterContact.wall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                -- the trapdoor stays open from leg 1 (a varbit), so one click climbs down
                t.exec("talkToVeliafAfterContact.down", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
                t.ticks(6)
                t.check("talkToVeliafAfterContact.below", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("talkToVeliafAfterContact", t.player.talk_to, "myq5_veliaf_child", 1)
                t.exec("talkToVeliafAfterContact-dialog", t.chat.play, {
                    "player:Vertida asked me to bring you this.",
                    "npc:So the Meiyerditch cell still stands",
                    "choose:What should I do now?",
                    "player:What should I do now?",
                    "npc:Drezel sent word",
                })
                t.ticks(2)
                t.expect("quest.stage.veliaf_warned", t.var.expect("varb2573_myq3_main_quest", 110))
                t.exec("leaveVeliaf.ladder", t.player.click_loc, "burgh_inn_basement_ladderup", 1)
                t.ticks(5)

                -- goDownToDrezel: the trapdoor east of Paterdomus
                t.exec("goto-goDownToDrezel", t.player.goto_tile, 3422, 3484, 0)
                t.exec("goDownToDrezel.open", t.player.click_loc, "pipeastsidetrapdoor", 1)
                t.ticks(3)
                t.exec("goDownToDrezel", t.player.click_loc, "pipeastsidetrapdoor_open", 1)
                t.ticks(6)
                t.check("goDownToDrezel.below", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.player.walk_to(3438, 9895, 20)
                t.check("walk-talkToDrezel.at", true, "tile " .. at(t))
                t.exec("talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
                t.exec("talkToDrezel-dialog", t.chat.play, {
                    "player:Veliaf sent me.",
                    "npc:Strange noises outside",
                    "player:I'll take a look.",
                })
                t.ticks(2)
                t.expect("quest.stage.bush_wait", t.var.expect("varb2573_myq3_main_quest", 120))

                -- leaveDrezelToBushes: the west ladder
-- the cellar's two doors (area_mausoleum/scripts/gates.rs2:7, :25), then the west ladder
                t.player.walk_to(3433, 9897, 20)
                t.exec("leaveDrezelToBushes.door2", t.player.click_loc, "pip_underground_door2", 1)
                t.ticks(3)
                local wr, wd = t.player.walk_to(3407, 9895, 40)
                t.check("walk-leaveDrezelToBushes.at", true, "walk_to " .. tostring(wr) .. " " .. tostring(wd) .. " tile " .. at(t))
                t.exec("leaveDrezelToBushes.door1", t.player.click_loc, "pip_underground_door1", 1)
                t.ticks(3)
                t.player.walk_to(3405, 9905, 30)
                t.exec("leaveDrezelToBushes", t.player.click_loc, "ladder_from_cellar", 1)
                t.ticks(4)
                t.check("leaveDrezelToBushes.up", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- searchBushes
                t.player.walk_to(3391, 3481, 30)
                t.check("walk-searchBushes.at", true, "tile " .. at(t))
                t.exec("searchBushes", t.player.click_loc, "myq_pt3_cutscene_werewolf_bush", 1)
                t.ticks(10)
                t.check("searchBushes.found", true, "tile " .. at(t) .. " chat " .. tostring(t.chat.kind()) .. " messages: " .. said(t, 3))
                t.exec("searchBushes-dialog", t.chat.drain, { max_pages = 4 })
                t.ticks(3)
                t.expect("quest.stage.bushes_done", t.var.expect("varb2573_myq3_main_quest", 130))

                -- returnFromBushesToDrezel: the surface trapdoor at 3405,3507
                t.player.walk_to(3405, 3505, 30)
                t.check("walk-returnFromBushesToDrezel.at", true, "tile " .. at(t))
                t.exec("returnFromBushesToDrezel.open", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507 } })
                t.ticks(3)
                t.exec("returnFromBushesToDrezel", t.player.click_loc, "trapdoor_open", 1, { at = { 3405, 3507 } })
                t.ticks(5)
                t.check("returnFromBushesToDrezel.below", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("walk-talkToDrezel2.door1", t.player.click_loc, "pip_underground_door1", 1)
                t.ticks(3)
                t.player.walk_to(3430, 9897, 40)
                t.exec("walk-talkToDrezel2.door2", t.player.click_loc, "pip_underground_door2", 1)
                t.ticks(3)
                t.player.walk_to(3438, 9895, 30)
                t.check("walk-talkToDrezel2.at", true, "tile " .. at(t))
                t.exec("talkToDrezel.runes", t.player.talk_to, "priestperiltrappedmonk2", 1)
                t.exec("talkToDrezel.runes-dialog", t.chat.drain, { max_pages = 8 })
                t.ticks(2)
                t.expect("quest.stage.runes_given", t.var.expect("varb2573_myq3_main_quest", 135))
                t.check("talkToDrezel.runes.held", true, "law " .. tostring(select(2, t.inv.count("lawrune"))) .. " fire " .. tostring(select(2, t.inv.count("firerune"))) .. " air " .. tostring(select(2, t.inv.count("airrune"))))

                -- talkToRoald: Varrock Teleport from Drezel's runes, then the castle
                t.exec("teleportToVarrock", t.player.cast, "varrock_teleport")
                t.ticks(6)
                t.check("teleportToVarrock.at", true, "tile " .. at(t))
                t.exec("goto-talkToRoald", t.player.goto_tile, 3222, 3471, 0)
                t.exec("talkToRoald", t.player.talk_to, "king_roald", 1)
                t.exec("talkToRoald-dialog", t.chat.play, {
                    "player:I must speak with you on a matter of grave importance.",
                    "npc:Speak, then.",
                    "choose:Talk to the king about Morytania.",
                    "player:Your majesty, werewolves",
                    "npc:Werewolves over the Salve?",
                    "choose:What should I do now?",
                    "player:What should I do now?",
                    "npc:Go back to your friends",
                })
                t.ticks(2)
                t.expect("quest.stage.roald_told", t.var.expect("varb2573_myq3_main_quest", 170))

                -- talkToVeliafAfterDrezel: back to Burgh de Rott
                t.exec("goto-talkToVeliafAfterDrezel", t.player.goto_tile, 3491, 3229, 0)
                t.exec("talkToVeliafAfterDrezel.wall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                -- the trapdoor stays open from leg 1 (a varbit), so one click climbs down
                t.exec("talkToVeliafAfterDrezel.down", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
                t.ticks(6)
                t.exec("talkToVeliafAfterDrezel", t.player.talk_to, "myq5_veliaf_child", 1)
                t.exec("talkToVeliafAfterDrezel-dialog", t.chat.play, {
                    "player:Werewolves are crossing the Salve",
                    "npc:Then the Myreque in Meiyerditch need to hear it too",
                })
                t.ticks(2)
                t.expect("quest.stage.veliaf_told", t.var.expect("varb2573_myq3_main_quest", 180))
                t.exec("leaveVeliaf2.ladder", t.player.click_loc, "burgh_inn_basement_ladderup", 1)
                t.ticks(5)

                -- goToMines: a Vyrewatch in Meiyerditch sends the player to the Daeyalt mine
                t.exec("goto-goToMines", t.player.goto_tile, 3615, 3246, 0)
                t.exec("goToMines", t.player.talk_to, "sang_myq3_female_flying_vyrewatch_3", 1)
                t.exec("goToMines-dialog", t.chat.play, {
                    "npc:You there, citizen",
                    "choose:Send me to the mines.",
                    "player:Send me to the mines.",
                    "npc:The mines?",
                    "choose:/Send me to the mines!/",
                    "player:Send me to the mines!",
                    "npc:A bit of menial work",
                })
                t.ticks(4)
                t.check("goToMines.at", true, "tile " .. at(t))
                -- mineDaeyaltThenLeave: a spare pick from a miner, fifteen ores into the cart, the guard
                t.exec("mineDaeyaltThenLeave.pick", t.player.talk_to, "myreque_pt3_miner1", 1)
                t.exec("mineDaeyaltThenLeave.pick-dialog", t.chat.play, {
                    "npc:Keep your voice down",
                    "choose:Do you have a spare pick?",
                    "player:Do you have a spare pick?",
                    "npc:Here. It's an old bronze one",
                })
                t.expect("mineDaeyaltThenLeave.pick.held", t.inv.expect_has("bronze_pickaxe", 1))
                -- the mine: Daeyalt rocks, five ores to the cart at a time (doh_urgent.rs2:222, :249)
                t.exec("mineDaeyaltThenLeave.walk", t.player.goto_tile, 2389, 4624, 2)
                local cart = t.player.by_symbol("loc", "area_sanguine_minecart_multiloc")
                for load = 1, 15 do
                    t.player.click_loc("area_sanguine_mine_minerocks_01", 1)
                    t.inv.await("castle_drakan_daeyalt_ore", 1, 80)
                    t.player.use_on("castle_drakan_daeyalt_ore", cart)
                    t.ticks(2)
                    if load % 5 == 0 then
                        local _, loaded = t.var.varbit("varb2588_myq3_sang_punish_mining_cart")
                        t.check("mineDaeyaltThenLeave.loaded" .. load, loaded == load, "cart holds " .. tostring(loaded) .. " of 15 loads after " .. load .. " ore")
                    end
                end
                t.exec("mineDaeyaltThenLeave", t.player.talk_to, "sang_myq3_mine_guard_juvinate_male", 1)
                t.exec("mineDaeyaltThenLeave-dialog", t.chat.play, {
                    "player:The cart's full.",
                    "npc:Back to the streets with you",
                })
                t.ticks(4)
                t.check("mineDaeyaltThenLeave.out", true, "tile " .. at(t))

                -- returnToMeiyBase: press the decorated wall (doh_meiyerditch.rs2:539)
                t.exec("returnToMeiyBase", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                t.check("returnToMeiyBase.pressed", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                local _, q = t.quest.stage()
                t.check("leg.3.end", q == 180, "tile " .. at(t) .. ", stage varb2573_myq3_main_quest=" .. tostring(q) .. ", backpack hammer, knife, bronze_pickaxe, runes spent on the teleport")
                -- LEG 3 END
            end,
        },
        {
            name = "walls_to_safalaan",
            run = function(t)
                -- LEG 4 BEGIN: climbUpDrakanWalls
                t.ticks(2)
                t.check("leg.4.start", true, "tile " .. at(t))
                -- the rug trapdoor stays open from leg 2, so one click climbs down
                t.exec("vertida.down", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                t.player.walk_to(3629, 9640)
                t.ticks(2)
                t.exec("vertida", t.player.talk_to, "myq4_vertida_visible", 1)
                t.exec("vertida-dialog", t.chat.drain, { max_pages = 8 })
                t.ticks(3)
                t.expect("quest.stage.return_meiyerditch", t.quest.expect_stage(190))
                t.check("vertida.at", true, "tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())))
                -- the random room above Ral's house: in through his door, up the ladder, and down again
                t.exec("goto-goDownFromRandomRoom", t.player.goto_tile, 3598, 3206, 0)
                t.exec("goDownFromRandomRoom.door", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3597, 3205 } })
                t.ticks(3)
                t.exec("goDownFromRandomRoom.up", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3595, 3204 } })
                t.ticks(4)
                t.check("goDownFromRandomRoom.near", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("goDownFromRandomRoom", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3595, 3204 } })
                t.ticks(4)
                t.check("goDownFromRandomRoom.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- the way to the walls: plain travel to the breach north of the rubble, over it, then up the floor
                t.exec("goto-climbUpFloor", t.player.goto_tile, 3591, 3181, 0)
                t.exec("climbUpFloor.rubble", t.player.click_loc, "myq3_rubble_east_wall", 1)
                t.ticks(4)
                t.check("climbUpFloor.rubble.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("climbUpFloor", t.player.click_loc, "mieyerditch_wall_underboards_multi_loc", 1)
                t.ticks(4)
                t.check("climbUpFloor.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("climbDownWallLadder", t.player.click_loc, "myq3_ladder_down", 2, { at = { 3588, 3210 } })
                t.ticks(4)
                t.check("climbDownWallLadder.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- searchRockySurface: opens the barricade pass for a while (doh_meiyerditch.rs2:631)
                t.exec("searchRockySurface", t.player.click_loc, "myq3_secret_rock_barricade_unlock", 1)
                t.ticks(3)
                t.check("searchRockySurface.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- goThroughBarricade: through the pass, then up the ladder beyond it
                t.player.walk_to(3590, 3228, 60)
                t.check("goThroughBarricade.through", true, "tile " .. at(t))
                t.exec("goThroughBarricade", t.player.click_loc, "myq3_ladder_up", 2, { at = { 3593, 3230 } })
                t.ticks(4)
                t.check("goThroughBarricade.up", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- climbLadderSecondWall
                t.player.walk_to(3588, 3250, 30)
                t.drive.camera(0, 450, 500)
                t.ticks(1)
                t.exec("climbLadderSecondWall", t.player.click_loc, "myq3_ladder_up_2", 2, { at = { 3588, 3251 } })
                t.ticks(4)
                t.check("climbLadderSecondWall.up", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- climbDownFromThirdWall
                t.exec("climbDownFromThirdWall", t.player.click_loc, "myq3_ladder_down_2", 2, { at = { 3588, 3259 } })
                t.ticks(4)
                t.check("climbDownFromThirdWall.down", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- climbUpDrakanWalls
                t.player.walk_to(3588, 3280, 40)
                t.player.walk_to(3591, 3300, 40)
                t.player.walk_to(3595, 3309, 60)
                t.check("climbUpDrakanWalls.near", true, "tile " .. at(t))
                -- the seam-4 fix: the wall shortcut is pressed for real from 3595,3309,1 and lands on 3595,3312,0
                t.exec("climbUpDrakanWalls", t.player.click_loc, "darkm_outer_wall_3h_meyerditch_wall_shortcut_bottom", 1)
                t.ticks(4)
                t.check("climbUpDrakanWalls", at(t) == "3595,3312,0", "tile " .. at(t) .. " (north of the wall, ground level) messages: " .. said(t, 2))
                -- talkToSafalaan: on the wall-walk (doh_castle.rs2:110, doh_safalaan_first)
                t.player.walk_to(3586, 3330, 80)
                t.check("talkToSafalaan.near", true, "tile " .. at(t))
                t.exec("talkToSafalaan", t.player.talk_to, "myreque_pt3_safalaan", 1)
                t.exec("talkToSafalaan-dialog", t.chat.play, {
                    "player:Vertida sent me.",
                    "npc:Noted, and troubling.",
                    "npc:west and south",
                    "choose:Okay, lead the way.",
                    "player:Okay, lead the way.",
                    "npc:North first.",
                })
                t.ticks(3)
                t.check("talkToSafalaan.stage", true, "tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())) .. " charcoal " .. tostring(select(2, t.inv.count("charcoal"))) .. " papyrus " .. tostring(select(2, t.inv.count("papyrus"))))
                t.expect("quest.stage.sketch_north", t.quest.expect_stage(200))
                -- drawNorthWall: charcoal on papyrus, standing on the north sickle logo (doh_castle.rs2:121)
                -- the wall-walk bends: leg by leg along it, each walk_to goes as far as the walk reaches
                for _, p in ipairs({ { 3572, 3331 }, { 3556, 3337 }, { 3556, 3379 }, { 3556, 3379 } }) do
                    t.player.walk_to(p[1], p[2], 40)
                end
                t.check("drawNorthWall.near", true, "tile " .. at(t))
                t.exec("drawNorthWall", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(5)
                t.expect("drawNorthWall.sketch", t.inv.expect_has("myq3_castle_sketch_1", 1))
                t.expect("quest.stage.sketch_west", t.quest.expect_stage(210))
                t.check("drawNorthWall.said", true, "tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())) .. " messages: " .. said(t, 2))
                -- drawWestWall: the west sickle logo
                for _ = 1, 4 do
                    t.player.walk_to(3522, 3357, 40)
                end
                t.check("drawWestWall.near", true, "tile " .. at(t))
                t.exec("drawWestWall", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(5)
                t.expect("drawWestWall.sketch", t.inv.expect_has("myq3_castle_sketch_2", 1))
                t.expect("quest.stage.sketch_south_start", t.quest.expect_stage(220))
                t.check("drawWestWall.said", true, "tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())) .. " messages: " .. said(t, 2))
                t.check("leg.4.end", true, "tile " .. at(t) .. ", stage varb2573_myq3_main_quest=220 (sketch_south_start), backpack hammer, knife, bronze_pickaxe, charcoal, papyrus, myq3_castle_sketch_1 and _2; next: the south sickle logo (3572,3331), Vanstrom")
                -- LEG 4 END
            end,
        },
        {
            name = "south_sketch_to_fireplace",
            run = function(t)
                -- LEG 5 BEGIN: drawSouthWall
                t.ticks(2)
                t.check("leg.5.start", true, "tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())))
                -- back along the wall-walk to the south sickle logo
                for _, p in ipairs({ { 3556, 3379 }, { 3556, 3337 }, { 3572, 3331 }, { 3572, 3331 } }) do
                    t.player.walk_to(p[1], p[2], 60)
                end
                t.check("drawSouthWall.near", true, "tile " .. at(t))
                -- drawSouthWall: charcoal on papyrus on the south logo brings Vanstrom down (doh_castle.rs2:138)
                t.exec("drawSouthWall", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(2)
                t.check("drawSouthWall.vanstrom", true, "tile " .. at(t) .. " messages: " .. said(t, 3))
                -- tankVanstrom: five blows (doh_castle.rs2:185), then he knocks the player out and Sarius stands over them
                t.exec("tankVanstrom", t.npc.await_present, "myq3_vanstrom_klause_vampyre_attack", 12, 10)
                -- five blows 8 ticks apart (attackrate 8); the fifth opens the knock-out mesbox, which holds the stage at 220 until it is read
                t.ticks(44)
                t.exec("tankVanstrom.dismiss", t.chat.drain, { max_pages = 4 })
                t.expect("quest.stage.vanstrom_done", t.quest.expect_stage(230))
                t.check("tankVanstrom.done", true, "tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())) .. " messages: " .. said(t, 4))
                -- talkToSarius (doh_castle.rs2:223)
                t.exec("talkToSarius", t.player.talk_to, "myq3_sarius_guile", 1)
                t.exec("talkToSarius-dialog", t.chat.play, {
                    "player:Who are you?",
                    "npc:Sarius Guile",
                    "npc:If you want to know",
                    "player:What do you mean?",
                    "npc:Finish your sketch",
                })
                t.ticks(2)
                t.expect("quest.stage.sarius_talked", t.quest.expect_stage(240))
                -- finishSouthSketch (doh_castle.rs2:161)
                t.player.walk_to(3572, 3331, 10)
                t.ticks(1)
                t.check("finishSouthSketch.near", true, "tile " .. at(t))
                t.exec("finishSouthSketch", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(5)
                t.expect("finishSouthSketch.sketch", t.inv.expect_has("myq3_castle_sketch_3", 1))
                t.expect("quest.stage.sketches_complete", t.quest.expect_stage(250))
                t.check("finishSouthSketch.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- useKnifeOnFireplace: the room with the fireplace by the Myreque hideout (doh_lab.rs2:12)
                t.exec("goto-useKnifeOnFireplace", t.player.goto_tile, 3633, 3240, 0)
                t.exec("useKnifeOnFireplace.door3628", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3628, 3250 } })
                t.ticks(3)
                t.player.walk_to(3627, 3254, 20)
                t.ticks(1)
                t.check("useKnifeOnFireplace.near", true, "tile " .. at(t))
                local fire = t.player.by_symbol("loc", "myq3_fireplace_loose_tile")
                t.exec("useKnifeOnFireplace", t.player.use_on, "knife", fire)
                t.ticks(4)
                t.expect("useKnifeOnFireplace.message", t.inv.expect_has("myq3_sarius_message", 1))
                t.expect("quest.stage.safalaan_briefed", t.quest.expect_stage(260))
                t.check("useKnifeOnFireplace.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- leaveMeiyerBase: down the rug trapdoor, then up the hideout ladder (ladders.rs2:199 via tasteofhope.rs2:300)
                -- back to the decorated wall: out of the pocket by door 3625,3252, east along the lane to the north room
                t.exec("leaveMeiyerBase.door3625", t.player.click_loc, "area_sanguine_ghetto_door2", 1, { at = { 3625, 3252 } })
                t.ticks(3)
                -- the secret room is only open from the roof (leg 2's route): north of door 3631,3259, ladder up, jump board, stairs down
                t.player.walk_to(3631, 3257, 40)
                t.ticks(1)
                t.player.walk_to(3631, 3261, 40)
                t.ticks(2)
                t.exec("leaveMeiyerBase.door3631", t.player.click_loc, "area_sanguine_ghetto_door2", 1, { at = { 3631, 3259 } })
                t.ticks(3)
                t.exec("leaveMeiyerBase.ladder3631", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3631, 3258 } })
                t.ticks(4)
                t.player.walk_to(3632, 3256, 10)
                t.ticks(2)
                t.exec("leaveMeiyerBase.jump41", t.player.click_loc, "myq3_agil_41_jump_east", 1)
                t.ticks(4)
                t.exec("leaveMeiyerBase.stairs3639", t.player.click_loc, "area_sanguine_ghetto_stairs_down", 1, { at = { 3639, 3256 } })
                t.ticks(4)
                t.check("leaveMeiyerBase.room", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("leaveMeiyerBase.wallpush", t.player.click_loc, "area_sanguine_myreque_secret_wall_closed", 1)
                t.ticks(5)
                t.check("leaveMeiyerBase.wallpush.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("leaveMeiyerBase.press", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                t.check("leaveMeiyerBase.pressed", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("leaveMeiyerBase.rug", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                t.check("leaveMeiyerBase.down", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("leaveMeiyerBase", t.player.click_loc, "area_sanguine_myreque_hideout_ladder_up", 1)
                t.ticks(5)
                t.check("leg.5.end", true, "tile " .. at(t) .. ", stage varb2573_myq3_main_quest=" .. tostring(select(2, t.quest.stage())) .. " (safalaan_briefed), backpack hammer, knife, bronze_pickaxe, myq3_castle_sketch_1/2/3, myq3_sarius_message; next: Safalaan on the wall-walk")
                -- LEG 5 END
            end,
        },
        {
            name = "portrait_to_lab",
            run = function(t)
                -- LEG 6 BEGIN: inspectPortrait
                t.ticks(2)
                t.check("leg.6.start", true, "tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())))
                -- the portrait hangs inside the fireplace room: door 3628,3250 from the south (leg 5's route in)
                t.exec("goto-inspectPortrait", t.player.goto_tile, 3633, 3240, 0)
                door_click(t, "inspectPortrait.door3628", "area_sanguine_ghetto_door1", 3628, 3250)
                t.ticks(3)
                t.player.walk_to(3627, 3250, 20)
                t.ticks(1)
                -- inspectPortrait (doh_lab.rs2:75): before the knife it is only examined
                t.exec("inspectPortrait", t.player.click_loc, "myq3_statue_painting_multi", 1)
                t.ticks(3)
                t.check("inspectPortrait.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- useKnifeOnPortrait (doh_lab.rs2:55): slash it open, then Inspect takes the key
                local portrait = t.player.by_symbol("loc", "myq3_statue_painting_multi")
                t.exec("useKnifeOnPortrait", t.player.use_on, "knife", portrait)
                t.ticks(5)
                t.check("useKnifeOnPortrait.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("useKnifeOnPortrait.key", t.player.click_loc, "myq3_statue_painting_multi", 1)
                t.ticks(3)
                t.expect("useKnifeOnPortrait.keyheld", t.inv.expect_has("myq3_lab_ornate_key", 1))
                -- readMessage (doh_lab.rs2:50)
                t.exec("readMessage", t.player.inv_op, "myq3_sarius_message", 1)
                t.ticks(2)
                t.check("readMessage.said", true, "tile " .. at(t) .. " chat " .. tostring(select(2, t.chat.text())) .. " messages: " .. said(t, 2))
                t.exec("readMessage.dismiss", t.chat.drain, { max_pages = 3 })
                -- back to the hideout (the route leg 5 took out): door 3625,3252 out of the room, then the roof
                door_click(t, "goBase.door3625", "area_sanguine_ghetto_door2", 3625, 3252)
                t.ticks(3)
                t.player.walk_to(3631, 3257, 40)
                t.ticks(1)
                t.player.walk_to(3631, 3261, 40)
                t.ticks(2)
                door_click(t, "goBase.door3631", "area_sanguine_ghetto_door2", 3631, 3259)
                t.ticks(3)
                t.exec("goBase.ladder3631", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3631, 3258 } })
                t.ticks(4)
                t.player.walk_to(3632, 3256, 10)
                t.ticks(2)
                t.exec("goBase.jump41", t.player.click_loc, "myq3_agil_41_jump_east", 1)
                t.ticks(4)
                t.exec("goBase.stairs3639", t.player.click_loc, "area_sanguine_ghetto_stairs_down", 1, { at = { 3639, 3256 } })
                t.ticks(4)
                t.exec("goBase.wallpush", t.player.click_loc, "area_sanguine_myreque_secret_wall_closed", 1)
                t.ticks(5)
                t.exec("goBase.press", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                t.exec("goBase.rug", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                t.check("goBase.down", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- talkToSafalaanInBase (doh_castle.rs2:110, doh_safalaan_handover): he stands in the north room
                t.player.walk_to(3627, 9640, 60)
                t.ticks(1)
                t.player.walk_to(3627, 9643, 30)
                t.ticks(1)
                t.check("talkToSafalaanInBase.near", true, "tile " .. at(t))
                t.exec("talkToSafalaanInBase", t.player.talk_to, "myreque_pt3_safalaan", 1)
                t.exec("talkToSafalaanInBase-dialog", t.chat.play, {
                    "player:I have the sketches",
                    "npc:This is grim work",
                    "npc:The laboratory's entrance",
                })
                t.ticks(3)
                t.expect("quest.stage.message_found", t.quest.expect_stage(270))
                t.check("talkToSafalaanInBase.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2) .. " sketches left " .. tostring(select(2, t.inv.count("myq3_castle_sketch_1"))))
                -- useKnifeOnTapestry (doh_lab.rs2:113): the building in the north east of the city
                t.exec("goto-useKnifeOnTapestry", t.player.goto_tile, 3638, 3302, 0)
                local tapestry = t.player.by_symbol("loc", "myq3_lab_tapestry_multi")
                t.exec("useKnifeOnTapestry", t.player.use_on, "knife", tapestry)
                t.ticks(5)
                t.check("useKnifeOnTapestry.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- through the slashed tapestry (doh_lab.rs2:128, a hop to the far side)
                t.exec("useKnifeOnTapestry.through", t.player.click_loc, "myq3_lab_tapestry_multi", 1)
                t.ticks(4)
                t.check("useKnifeOnTapestry.through.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- useKeyOnStatue (doh_lab.rs2:120): the ornate key from the portrait, turned once in the vampyre statue
                local statue = t.player.by_symbol("loc", "myq3_lab_vamp_statue_multi")
                t.exec("useKeyOnStatue", t.player.use_on, "myq3_lab_ornate_key", statue)
                t.ticks(5)
                t.expect("quest.stage.lab_unlocked", t.quest.expect_stage(280))
                t.check("useKeyOnStatue.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- the lab corridor's door is unlocked by the statue (doh_lab.rs2:166)
                t.exec("goDownToLab.door", t.player.click_loc, "myq3_laboratory_door_closed", 1)
                t.ticks(4)
                t.check("goDownToLab.door.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- goDownToLab (doh_lab.rs2:176 via sinsofthefather.rs2:511)
                t.exec("goDownToLab", t.player.click_loc, "myq3_lab_stairs_down", 1)
                t.ticks(5)
                t.expect("quest.stage.lab_entered", t.quest.expect_stage(290))
                t.check("goDownToLab.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- getRunes (doh_lab.rs2:198): the broken rune case, one search
                t.exec("getRunes", t.player.click_loc, "myq3_broken_rune_case", 1)
                t.ticks(4)
                t.expect("getRunes.law", t.inv.expect_has("lawrune", 1))
                t.expect("quest.stage.book_taken", t.quest.expect_stage(300))
                t.check("getRunes.said", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("getRunes.dismiss", t.chat.drain, { max_pages = 3 })
                -- telegrabBook: Telekinetic Grab (Magic 33, law + air) on the Haemalchemy volume on its shelf
                t.player.walk_to(3625, 9692, 20)
                t.ticks(1)
                t.exec("telegrabBook", t.player.cast, "telegrab", { kind = "obj", id = "myq3_haemalchemy_vol_1" })
                t.ticks(3)
                t.expect("telegrabBook.book", t.inv.expect_has("myq3_haemalchemy_vol_1", 1))
                -- leaveLab (ladders.rs2:175)
                t.exec("leaveLab", t.player.click_loc, "myq3_lab_stairs_up", 1)
                t.ticks(5)
                t.check("leaveLab.at", true, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- bringMessageToVeliafToFinish, first half: the book to Safalaan in the hideout (doh_castle.rs2:92, 300 -> 310)
                -- (door 3631,3259 shuts again on its own; click it if it has)
                t.exec("goto-bringMessageToVeliafToFinish", t.player.goto_tile, 3631, 3261, 0)
                t.ticks(2)
                door_click(t, "bringMessage.door3631", "area_sanguine_ghetto_door2", 3631, 3259)
                t.ticks(3)
                t.exec("bringMessage.ladder3631", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3631, 3258 } })
                t.ticks(4)
                t.player.walk_to(3632, 3256, 10)
                t.ticks(2)
                t.exec("bringMessage.jump41", t.player.click_loc, "myq3_agil_41_jump_east", 1)
                t.ticks(4)
                t.exec("bringMessage.stairs3639", t.player.click_loc, "area_sanguine_ghetto_stairs_down", 1, { at = { 3639, 3256 } })
                t.ticks(4)
                t.exec("bringMessage.wallpush", t.player.click_loc, "area_sanguine_myreque_secret_wall_closed", 1)
                t.ticks(5)
                t.exec("bringMessage.press", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                t.exec("bringMessage.rug", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                t.player.walk_to(3627, 9640, 60)
                t.ticks(1)
                t.player.walk_to(3627, 9643, 30)
                t.ticks(1)
                t.exec("bringMessage.safalaan", t.player.talk_to, "myreque_pt3_safalaan", 1)
                t.exec("bringMessage.safalaan-dialog", t.chat.play, {
                    "player:I found this in the laboratory",
                    "npc:Haemalchemy... this confirms",
                })
                t.ticks(3)
                t.expect("quest.stage.sealed_message", t.quest.expect_stage(310))
                t.expect("bringMessage.sealed", t.inv.expect_has("myq3_safalaan_message", 1))
                -- second half: the sealed message to Veliaf under Burgh de Rott (doh_burgh.rs2:50)
                t.exec("bringMessage.hideoutladder", t.player.click_loc, "area_sanguine_myreque_hideout_ladder_up", 1)
                t.ticks(5)
                t.exec("goto-bringMessageToVeliaf", t.player.goto_tile, 3491, 3229, 0)
                t.exec("bringMessageToVeliaf.wall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                t.exec("bringMessageToVeliaf.down", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
                t.ticks(6)
                local _, snap = t.skill.snapshot()
                t.exec("bringMessageToVeliafToFinish", t.player.talk_to, "myq5_veliaf_child", 1)
                t.exec("bringMessageToVeliafToFinish-dialog", t.chat.play, {
                    "player:Safalaan asked me to bring you this.",
                    "npc:Proof of what Drakan's brood",
                })
                t.ticks(3)
                t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
                t.expect("reward.tome", t.inv.expect_has("myq3_xp_tome_3", 1))
                t.exec("xp.agility", t.skill.expect_gain, "agility", 7000, snap)
                t.exec("xp.thieving", t.skill.expect_gain, "thieving", 6000, snap)
                t.exec("xp.construction", t.skill.expect_gain, "construction", 2000, snap)
                t.quest.expect_complete()
                t.check("leg.6.finish", true, "tile " .. at(t) .. ", stage varb2573_myq3_main_quest=" .. tostring(select(2, t.quest.stage())) .. " (complete), backpack hammer, knife, bronze_pickaxe, charcoal, myq3_xp_tome_3")
                -- LEG 6 END
            end,
        },
    },
}
