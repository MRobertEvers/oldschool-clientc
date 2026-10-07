return {
    id = "hazeelcult",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::give rune_scimitar 1", "::give adamant_platebody 1", "::give adamant_platelegs 1", "::give shark 10" },
    run = function(t)
        t.quest.bind({ varp = "varp223_hazeelcultquest", constants = { not_started = 0, started = 2, spoken_clivet = 3, clivet_decision = 4,
            poured_poison = 5, finished_side_task = 6, given_armour_or_scroll = 7, complete = 9 }, display = "Hazeel Cult", points = 1 })

        -- The Carnillean house (door rule). Ground floor, from reach.py / locs_near.py on the maps: the front door
        -- poshdoor 2569,3273 (street z 3274 | hall z 3271-3272, x 2568-2569); the hall's west door poshdoor_reverse
        -- 2567,3271 (east edge of 2567,3271) into Sir Ceril's room (x 2564-2567); the hall's east door
        -- poshdoor_reverse 2570,3271 (west edge of 2570,3271) into the stairs/ladder room (x 2570-2571, z 3267-3271).
        -- Every room change presses the door (or finds it standing open), in and out, named by the room it leaves.
        local FRONT = { closed = "poshdoor", open = "poshdooropen", at = { 2569, 3273, 0 } }
        local WEST = { closed = "poshdoor_reverse", open = "poshdooropen_reverse", at = { 2567, 3271, 0 } }
        local EAST = { closed = "poshdoor_reverse", open = "poshdooropen_reverse", at = { 2570, 3271, 0 } }
        local function door(name, base, near, far)
            t.exec(name, t.player.pass_door, { closed = base.closed, open = base.open, at = base.at, near = near, far = far })
        end
        local function room()
            local _, tile = t.world.tile()
            assert(tile and tile.level == 0, "house room read needs the ground floor")
            if tile.z >= 3273 then return "out" end
            if tile.z == 3272 or (tile.z == 3271 and tile.x >= 2568 and tile.x <= 2569) then return "hall" end
            if tile.x <= 2567 then return "west" end
            return "east"
        end
        -- Walk the door graph out - hall - west / east to the named room, one pressed door per edge.
        local function go_room(tag, want)
            local here = room()
            if here == want then return end
            if here == "west" then door(tag .. ".westDoorOut", WEST, { 2567, 3271 }, { 2568, 3271 }); here = "hall"
            elseif here == "east" then door(tag .. ".eastDoorOut", EAST, { 2570, 3271 }, { 2569, 3271 }); here = "hall"
            elseif here == "out" then door(tag .. ".frontDoorIn", FRONT, { 2569, 3274 }, { 2569, 3272 }); here = "hall" end
            if here == want then return end
            if want == "out" then door(tag .. ".frontDoorOut", FRONT, { 2569, 3272 }, { 2569, 3274 })
            elseif want == "west" then door(tag .. ".westDoorIn", WEST, { 2568, 3271 }, { 2567, 3271 })
            elseif want == "east" then door(tag .. ".eastDoorIn", EAST, { 2569, 3271 }, { 2570, 3271 }) end
        end
        local function enter_house(tag, want)
            t.exec(tag .. ".goto-front", t.player.goto_tile, 2569, 3275, 0)
            go_room(tag, want or "west")
        end
        local function leave_house(tag) go_room(tag, "out") end
        -- The stairs carnillean_stairs 2568,3268 (forceapproach: north side only, pressed from the hall's
        -- 2568-2569,3271). maplink rows maplink_0_40_51_8_7_up / _9_7_up land on 2568,3267,1 (the upstairs
        -- landing), and carnillean_stairstop's maplink_1_40_51_8_3_down / _9_3_down from 2568-2569,3267,1 land
        -- on 2568,3271,0 (maplink.dbrow:4085-4174; transports.tsv:661-664).
        local function stairs_up(name)
            go_room(name, "hall")
            t.exec(name, t.player.climb, { loc = "carnillean_stairs", op = 1, op_name = "Climb-up",
                at = { 2568, 3268, 0 }, src = { 2569, 3271 }, dest = { 2568, 3267, 1 } })
            t.ticks(2)
        end
        local function stairs_down(name)
            t.exec(name, t.player.climb, { loc = "carnillean_stairstop", op = 1, op_name = "Climb-down",
                at = { 2568, 3268, 1 }, src = { 2568, 3267 }, dest = { 2568, 3271, 0 } })
            t.ticks(2)
        end
        -- Upstairs: the landing corridor (x 2568-2571, z 3267-3268) and the east bedroom (x 2572-2573, z 3267-3270:
        -- Ceril 2573,3268, Jones 2573,3269, the cupboard hazeelcbshut 2573,3267, the secret wall
        -- carnilleanbookcase 2572,3270 north edge) behind poshdoor 2572,3268,1 (west edge); behind the wall the
        -- passage 2572,3271 with ladder 2573,3271,1 to the chest room (laddertop 2573,3271,2, chest 2571,3269,2).
        local UPDOOR = { closed = "poshdoor", open = "poshdooropen", at = { 2572, 3268, 1 } }
        local function bedroom_in(name) door(name, UPDOOR, { 2571, 3268 }, { 2572, 3268 }) end
        local function bedroom_out(name) door(name, UPDOOR, { 2572, 3268 }, { 2571, 3268 }) end
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        -- First placement: the only walk from the Lumbridge fixture to Ardougne goes through the members' gate
        -- membergater 2933,3320 (reach.py 3206,3233 -> 2569,3274: NEEDS-DOOR via membergater@2933,3320; 3206,3233 ->
        -- 2933,3318: REACH closed-doors len=388). Overland to its south side, the gate pressed (ikov.lua), overland
        -- from its north side to the street outside the house (reach.py 2933,3322 -> 2569,3274 at 250: REACH len=842).
        t.exec("goto-memberGate", t.player.goto_tile, 2933, 3318, 0)
        t.exec("startQuest.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
            near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
        enter_house("startQuest")
        t.ticks(2)
        t.exec("ceril.start", t.player.talk_to, "sir_ceril_carnillean", 1)
        t.exec("ceril.start.dialog", t.chat.play, {
            "player:Hello there.", "npc:Blooming, thieving", "options", "choose:What's wrong?", "player:What's wrong?",
            "npc:It's those blooming cultists", "player:Have they taken much?", "npc:They first broke in", "player:And you are",
            "npc:Why, I am Sir Ceril", "npc:Perhaps you would be able", "options", "choose:/^Yes, of course/", "player:Yes, of course",
            "npc:That's very noble", "npc:They're some kind of crazy cult", "player:How do you know", "npc:My old butler", "player:That's awful", "npc:No, it's ok", "player:Ok. I'll see what I can do." })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        leave_house("caveTrip1")
        t.exec("goto-cave", t.player.goto_tile, 2587, 3237, 0)
        t.exec("cave.enter", t.player.click_loc, "hazeelcultcave", 1)
        t.ticks(3)
        local r, tile = t.world.tile()
        local x, z = tile and tile.x, tile and tile.z
        t.check("cave.inside", r == "ok" and z ~= nil and z > 9000, "tile " .. tostring(x) .. "," .. tostring(z))
        t.exec("clivet.talk", t.player.talk_to, "clivet_hazeel_cultist", 1)
        t.exec("clivet.talk.dialog", t.chat.play, {
            "player:Do you know the Carnilleans", "npc:You mind your business", "player:Look, I KNOW", "npc:If you want to stay healthy",
            "player:I have my orders", "npc:So... that two faced", "player:Sir Ceril Carnillean is a man", "npc:Is he now", "npc:And none of it",
            "options", "choose:What do you mean?", "player:What do you mean?",
            "npc:The Carnillean home", "npc:in this land", "npc:Hazeel and his", "npc:Zamorakians they", "npc:They have grown fat", "npc:about, and made",
            "player:The politics", "npc:Well then friend", "npc:Join our cult", "options",
            "choose:You're crazy, I'd never help you.", "player:You're crazy, I'd never help you.",
            "npc:Then you are a fool", "mesbox:The man jumps onto the raft" })
        t.ticks(3)
        t.expect("quest.stage.clivet_decision", t.quest.expect_stage("clivet_decision"))
        t.expect("side.goodside", t.var.await_server("varp5927_hazeelcult_side", 0, 5))
        local nr = t.npc.nearest("clivet_hazeel_cultist", 8)
        t.check("clivet.gone", nr ~= "ok", "npc.nearest clivet after decision = " .. tostring(nr))
        -- valves: partial first (valve 1 right only -> first island), then the full solution
        -- out of the cave by its own stairs (hazeelcultstairs -> 0_40_50_27_37 = 2587,3237), never a goto out
        t.exec("stairs.out1", t.player.click_loc, "hazeelcultstairs", 1)
        t.ticks(3)
        t.exec("goto-valve1", t.player.goto_tile, 2562, 3249, 0)
        t.exec("valve1.right", t.player.click_loc, "sewervalve1", 1)
        t.exec("valve1.right.dialog", t.chat.play, { "options", "choose:Turn right.", "mesbox:You turn the large metal valve to the right" })
        t.expect("valves.bit0", t.var.await_server("varp5928_hazeelcult_valves", 1, 5))
        t.exec("goto-cave2", t.player.goto_tile, 2587, 3237, 0)
        t.exec("cave.enter2", t.player.click_loc, "hazeelcultcave", 1)
        t.ticks(3)
        t.exec("raft.partial", t.player.click_loc, "hazeelsewerraft", 1)
        t.exec("raft.partial.dialog", t.chat.play, { "mesbox:The raft washes up the sewer, and stops at the first island" })
        t.ticks(2)
        local r2, tile2 = t.world.tile()
        t.check("raft.island1", r2 == "ok" and tile2 and tile2.x == 2578 and tile2.z == 9687, "tile " .. tostring(tile2 and tile2.x) .. "," .. tostring(tile2 and tile2.z))
        -- the island raft carries the player back to the cave entrance
        t.exec("raft.back", t.player.click_loc, "hazeelsewerraft", 1)
        t.exec("raft.back.dialog", t.chat.play, { "mesbox:The raft flows with the current back" })
        t.ticks(2)
        local r3, tile3 = t.world.tile()
        t.check("raft.at_cave", r3 == "ok" and tile3 and tile3.x == 2567 and tile3.z == 9680, "tile " .. tostring(tile3 and tile3.x) .. "," .. tostring(tile3 and tile3.z))
        t.exec("stairs.up", t.player.click_loc, "hazeelcultstairs", 1)
        t.ticks(3)
        -- remaining valves: 2,4,5 right; 3 left (LostCity quest_hazeelcult.rs2 sewervalve3 op1 sets the bit? here left clears)
        local valves = { { "sewervalve2", 2572, 3261, "Turn right." }, { "sewervalve3", 2585, 3247, "Turn left." },
            { "sewervalve4", 2597, 3261, "Turn right." }, { "sewervalve5", 2611, 3244, "Turn right." } }
        for _, v in ipairs(valves) do
            t.exec("goto-" .. v[1], t.player.goto_tile, v[2], v[3], 0)
            t.exec(v[1], t.player.click_loc, v[1], 1)
            local dir = (v[4] == "Turn left.") and "left" or "right"
            t.exec(v[1] .. ".dialog", t.chat.play, { "options", "choose:" .. v[4], "mesbox:You turn the large metal valve to the " .. dir })
        end
        t.expect("valves.mask", t.var.await_server("varp5928_hazeelcult_valves", 27, 5))
        t.exec("goto-cave3", t.player.goto_tile, 2587, 3237, 0)
        t.exec("cave.enter3", t.player.click_loc, "hazeelcultcave", 1)
        t.ticks(3)
        t.exec("raft.end", t.player.click_loc, "hazeelsewerraft", 1)
        t.exec("raft.end.dialog", t.chat.play, { "mesbox:The raft washes up the sewer, past the islands" })
        t.ticks(4)
        t.exec("alomone.talk", t.player.talk_to, "alomone_hazeel_cultist_1op", 1)
        t.exec("alomone.talk.dialog", t.chat.play, {
            "npc:How did YOU get in here", "player:I've come for the Carnillean family armour", "npc:I thought I made it clear",
            "player:So the butler's part", "npc:Well you won't live long" })
        do local vr, v1, v2 = t.var.server("varb14770_hazeelcult_alomone_vis"); t.check("alomone.vis_fight", true, "var.server hazeelcult_alomone_vis = " .. tostring(vr) .. " " .. tostring(v1) .. " " .. tostring(v2)) end
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")
        t.exec("equip.body", t.player.equip, "adamant_platebody")
        t.exec("equip.legs", t.player.equip, "adamant_platelegs")
        local sharks_before = select(2, t.inv.count("shark"))
        t.exec("alomone.attack", t.player.attack, "alomone_hazeel_cultist_2op", 2, 15, { eat = { item = "shark", below = 50 } })
        local _, dead_detail = t.exec("alomone.dead", t.npc.await_dead_engaged, 60, 6, { eat = { item = "shark", below = 50 } })
        do
            local lowest = tonumber(tostring(dead_detail):match("lowest hp (%d+)/"))
            local _, hitpoints = t.skill.read("hitpoints")
            local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
            local food_result, food_left = t.inv.count("shark")
            t.check("alomone.margin", lowest ~= nil and max_hp ~= nil and food_result == "ok" and lowest * 4 >= max_hp and food_left >= 1,
                "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", sharks " .. tostring(sharks_before) .. " -> " .. tostring(food_left) .. " (margin: lowest hp >= a quarter of max AND a shark left)")
        end
        t.ticks(3)
        t.expect("quest.stage.finished_side_task", t.quest.expect_stage("finished_side_task"))
        do local vr, v1, v2 = t.var.server("varb14770_hazeelcult_alomone_vis"); t.check("alomone.vis_dead", true, "var.server hazeelcult_alomone_vis = " .. tostring(vr) .. " " .. tostring(v1) .. " " .. tostring(v2)) end
        t.exec("bones.dropped", t.inv.await, "bones", 0, 1)
        -- chest: armour (OSRS-era: moved from Alomone drop to the hideout chest, wiki oldid=15285220)
        t.exec("goto-chest", t.player.goto_tile, 2610, 9675, 0)
        t.exec("chest.search", t.player.click_loc, "hazeel_chest_closed", 1)
        t.exec("armour.received", t.inv.await, "carnillean_armour", 1, 6)
        t.exec("raft.return", t.player.click_loc, "hazeelsewerraft", 1)
        t.exec("raft.return.dialog", t.chat.play, { "mesbox:The raft flows with the current back" })
        t.ticks(3)
        t.exec("stairs.up2", t.player.click_loc, "hazeelcultstairs", 1)
        t.ticks(3)
        enter_house("armour")
        t.ticks(2)
        t.exec("ceril.armour", t.player.talk_to, "sir_ceril_carnillean", 1)
        t.exec("ceril.armour.dialog", t.chat.play, {
            "player:Ceril. How are you?", "player:Look! I've recovered your armour!", "npc:Well done!", "npc:Before we send you on your way",
            "player:I'd rather not", "npc:W-what? RIGHT!" })
        t.ticks(2)
        local st = t.quest.stage()
        t.check("ceril.armour.stage", true, "stage after ground-floor hand-in talk = " .. tostring(st))
        local sv = t.var.varp("varp3748_hazeelcult_secondary")
        t.check("probe.secondary", true, "varp hazeelcult_secondary client read = " .. tostring(sv))
        -- Upstairs: the hand-in only completes on level 1 (ceril_carnillean.rs2 ceril_give_armour, coordy != 0).
        stairs_up("armourUp.stairs")
        bedroom_in("armourUp.bedroomDoorIn")
        local nr1, nrow1 = t.npc.nearest("sir_ceril_carnillean", 20)
        t.check("upstairs.ceril", nr1 == "ok", "npc.nearest sir_ceril_carnillean from level 1 = " .. tostring(nr1) .. " " .. tostring(nrow1 and nrow1.x) .. "," .. tostring(nrow1 and nrow1.z))
        local nr2, nrow2 = t.npc.nearest("butler_jones_hazeel_cultist", 20)
        t.check("upstairs.jones", nr2 == "ok", "npc.nearest butler_jones from level 1 = " .. tostring(nr2) .. " " .. tostring(nrow2 and nrow2.x) .. "," .. tostring(nrow2 and nrow2.z))
        t.exec("ceril.up", t.player.talk_to, "sir_ceril_carnillean", 1)
        t.exec("ceril.up.dialog", t.chat.play, {
            "player:Look! I've recovered your armour!", "npc:Well done!", "npc:Before we send you on your way",
            "player:I'd rather not", "npc:W-what? RIGHT!", "npc:Jones! This commoner", "*", "npc:Humph. Quite right", "npc:Right. I have decided",
            "npc:but I must also compensate", "mesbox:Sir Ceril gives you 5 gold", "npc:Now take it", "*", "mesbox:Jones smirks", "mesbox:You have... kind of... completed" })
        t.ticks(2)
        t.expect("quest.stage.given_armour_or_scroll", t.quest.expect_stage("given_armour_or_scroll"))
        t.exec("coins.5", t.inv.await, "coins", 5, 2)
        -- cupboard (LostCity: Open on hazeelcbshut, then Search on hazeelcbopen)
        local snap_r, snap = t.skill.snapshot()
        t.exec("cupboard.open", t.player.click_loc, "hazeelcbshut", 1)
        t.ticks(2)
        t.exec("cupboard.search", t.player.click_loc, "hazeelcbopen", 1)
        t.exec("cupboard.dialog", t.chat.play, {
            "mesbox:You search the cupboard thoroughly", "player:Ceril!", "npc:What do you want now", "player:Look what I've found",
            "mesbox:You hand Ceril the bottle", "npc:I... I don't believe it", "npc:You called m'lud", "npc:JONES! Just WHAT",
            "npc:P-p-poison", "npc:Yes, that's right", "npc:Rats eh?", "player:Then how about this?", "mesbox:You show Ceril the amulet",
            "npc:Wh-what???", "npc:Jones! We trusted you", "npc:You senile old fool", "npc:To think we took you in", "npc:GUARD!",
            "mesbox:A Guard rushes", "npc:Take him away", "npc:Don't think this is the last", "npc:Now now. Come along",
            "npc:It looks like I am indebted", "player:No problem", "npc:But if it weren't for you", "npc:the very least",
            "player:Thank you Lord Ceril", "npc:No, no, thank YOU" })
        t.ticks(3)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.quest.expect_complete()
        t.exec("coins.2005", t.inv.await, "coins", 2005, 4)
        t.expect("thieving.xp", t.skill.expect_gain("thieving", 1500, snap))
        -- Back down to the ground floor before the second playthrough.
        bedroom_out("armourDown.bedroomDoorOut")
        stairs_down("armourDown.stairs")
        -- Second playthrough: the Hazeel (evil) side of Clivet's fork. The hazeelcultreset cheat only
        -- rewinds the varps of the quest already finished above; every step below is driven for real.
        t.check("h.reset", t.cheat("::hazeelcultreset") == "ok", "::hazeelcultreset issued after the Ceril branch completed")
        t.ticks(3)
        t.expect("quest.stage.not_started.hazeel", t.quest.expect_stage("not_started"))
        do
        go_room("h.start", "west")
        t.ticks(2)
        t.exec("h.ceril.start", t.player.talk_to, "sir_ceril_carnillean", 1)
        t.exec("h.ceril.start.dialog", t.chat.play, {
            "player:Hello there.", "npc:Blooming, thieving", "options", "choose:What's wrong?", "player:What's wrong?",
            "npc:It's those blooming cultists", "player:Have they taken much?", "npc:They first broke in", "player:And you are",
            "npc:Why, I am Sir Ceril", "npc:Perhaps you would be able", "options", "choose:/^Yes, of course/", "player:Yes, of course",
            "npc:That's very noble", "npc:They're some kind of crazy cult", "player:How do you know", "npc:My old butler", "player:That's awful", "npc:No, it's ok", "player:Ok. I'll see what I can do." })
        t.ticks(2)
        t.expect("quest.stage.started.hazeel", t.quest.expect_stage("started"))
        leave_house("h.caveTrip1")
        t.exec("h.goto-cave", t.player.goto_tile, 2587, 3237, 0)
        t.exec("h.cave.enter", t.player.click_loc, "hazeelcultcave", 1)
        t.ticks(3)
        t.exec("h.clivet.talk", t.player.talk_to, "clivet_hazeel_cultist", 1)
        t.exec("h.clivet.talk.dialog", t.chat.play, {
            "player:Do you know the Carnilleans", "npc:You mind your business", "player:Look, I KNOW", "npc:If you want to stay healthy",
            "player:I have my orders", "npc:So... that two faced", "player:Sir Ceril Carnillean is a man", "npc:Is he now", "npc:And none of it",
            "options", "choose:What do you mean?", "player:What do you mean?",
            "npc:The Carnillean home", "npc:in this land", "npc:Hazeel and his", "npc:Zamorakians they", "npc:They have grown fat", "npc:about, and made",
            "player:The politics", "npc:Well then friend", "npc:Join our cult", "options",
            "choose:So what would I have to do?", "player:So what would I have to do?",
            "npc:You must prove your loyalty", "npc:So what say you", "options", "choose:Ok, count me in.", "player:Ok, count me in.",
            "npc:Excellent.", "npc:Here. Take this poison" })
        t.ticks(3)
        t.expect("quest.stage.clivet_decision.hazeel", t.quest.expect_stage("clivet_decision"))
        t.expect("side.evilside", t.var.await_server("varp5927_hazeelcult_side", 1, 5))
        t.exec("h.poison.have", t.inv.await, "poison", 1, 3)
        t.exec("h.clivet.still_there", t.npc.await_present, "clivet_hazeel_cultist", 8, 3)
        -- out of the cave by its stairs, back into the house, and down the kitchen ladder
        t.exec("h.stairs.out1", t.player.click_loc, "hazeelcultstairs", 1)
        t.ticks(3)
        -- kitchen: carnillean_ladder_down 2570,3267 (east room) -> ^hazeelcult_basement_landing_coord
        -- 0_39_151_48_31 = 2544,9695; carnillean_ladder_up 2544,9694 -> ^hazeelcult_kitchen_landing_coord
        -- 0_40_51_11_3 = 2571,3267 (quest_hazeelcult.constant:44-51, quest_hazeelcult_locs.rs2:320-324)
        enter_house("h.kitchen", "east")
        t.exec("h.ladder.down", t.player.climb, { loc = "carnillean_ladder_down", op = 1, op_name = "Climb-down",
            at = { 2570, 3267, 0 }, src = { 2570, 3268 }, dest = { 2544, 9695, 0 } })
        t.ticks(2)
        local range = t.player.by_symbol("loc", "carnilleanrange")
        t.check("h.range.found", range ~= nil, "carnilleanrange resolved")
        t.exec("h.poison.pour", t.player.use_on, "poison", range)
        t.exec("h.poison.pour.dialog", t.chat.play, { "mesbox:You pour the poison into the hot pot" })
        t.ticks(2)
        t.expect("quest.stage.poured_poison", t.quest.expect_stage("poured_poison"))
        t.exec("h.poison.gone", t.inv.await, "poison", 0, 3)
        t.exec("h.crate.search", t.player.click_loc, "carnilleancrate", 1)
        t.exec("h.crate.dialog", t.chat.play, { "mesbox:You search the crate" })
        t.exec("h.key.have", t.inv.await, "carnilleanchestkey", 1, 3)
        t.exec("h.ladder.up", t.player.climb, { loc = "carnillean_ladder_up", op = 1, op_name = "Climb-up",
            at = { 2544, 9694, 0 }, src = { 2544, 9695 }, dest = { 2571, 3267, 0 } })
        t.ticks(2)
        go_room("h.poisonNews", "west")
        t.ticks(2)
        t.exec("h.ceril.poison", t.player.talk_to, "sir_ceril_carnillean", 1)
        t.exec("h.ceril.poison.dialog", t.chat.play, {
            "player:Hello again.", "npc:Oh.. the inhumanity", "npc:thoughtless and careless", "player:Scruffy?", "npc:He's been with our family",
            "player:Your DOG got poisoned", "player:That's not right.", "npc:I agree!", "player:Uh.... yeah" })
        t.ticks(2)
        t.expect("quest.stage.poured_poison.ceril", t.quest.expect_stage("poured_poison"))
        -- Clivet: mark of Hazeel
        leave_house("h.caveTrip2")
        t.exec("h.goto-cave2", t.player.goto_tile, 2587, 3237, 0)
        t.exec("h.cave.enter2", t.player.click_loc, "hazeelcultcave", 1)
        t.ticks(3)
        t.exec("h.clivet.mark", t.player.talk_to, "clivet_hazeel_cultist", 1)
        t.exec("h.clivet.mark.dialog", t.chat.play, {
            "player:Hello.", "player:I poured the poison", "npc:Yes, we heard all about it", "player:Ok. So what's next?", "npc:Here. Wear this amulet",
            "player:How does this amulet help", "npc:Hazeel in his wisdom", "npc:Each sewer valve", "npc:Starting from left to right", "npc:When you solve the sequence" })
        t.exec("h.mark.have", t.inv.await, "mark_of_hazeel", 1, 3)
        -- valves (mask 27)
        t.exec("h.stairs.up", t.player.click_loc, "hazeelcultstairs", 1)
        t.ticks(3)
        local hvalves = { { "sewervalve1", 2562, 3249, "Turn right." }, { "sewervalve2", 2572, 3261, "Turn right." }, { "sewervalve3", 2585, 3247, "Turn left." },
            { "sewervalve4", 2597, 3261, "Turn right." }, { "sewervalve5", 2611, 3244, "Turn right." } }
        for _, v in ipairs(hvalves) do
            t.exec("h.goto-" .. v[1], t.player.goto_tile, v[2], v[3], 0)
            t.exec("h." .. v[1], t.player.click_loc, v[1], 1)
            local dir = (v[4] == "Turn left.") and "left" or "right"
            t.exec("h." .. v[1] .. ".dialog", t.chat.play, { "options", "choose:" .. v[4], "mesbox:You turn the large metal valve to the " .. dir })
        end
        t.expect("h.valves.mask", t.var.await_server("varp5928_hazeelcult_valves", 27, 5))
        t.exec("h.goto-cave3", t.player.goto_tile, 2587, 3237, 0)
        t.exec("h.cave.enter3", t.player.click_loc, "hazeelcultcave", 1)
        t.ticks(3)
        t.exec("h.raft.end", t.player.click_loc, "hazeelsewerraft", 1)
        t.exec("h.raft.end.dialog", t.chat.play, { "mesbox:The raft washes up the sewer, past the islands" })
        t.ticks(4)
        t.exec("h.alomone.talk", t.player.talk_to, "alomone_hazeel_cultist_1op", 1)
        t.exec("h.alomone.talk.dialog", t.chat.play, {
            "player:Hi there.", "npc:Well well well", "npc:To accomplish this", "npc:Hazeel in his mighty cunning", "npc:The words to this powerful",
            "npc:in their Butler Jones", "npc:Go back to the mansion" })
        t.ticks(2)
        t.expect("quest.stage.finished_side_task.hazeel", t.quest.expect_stage("finished_side_task"))
        -- back by raft and the cave stairs, into the house: Jones stands in the hall (^hazeelcult_jones_coord 2568,3271)
        t.exec("h.raft.return", t.player.click_loc, "hazeelsewerraft", 1)
        t.exec("h.raft.return.dialog", t.chat.play, { "mesbox:The raft flows with the current back" })
        t.ticks(2)
        t.exec("h.stairs.out2", t.player.click_loc, "hazeelcultstairs", 1)
        t.ticks(3)
        enter_house("h.jones", "hall")
        t.ticks(2)
        t.exec("h.jones.talk", t.player.talk_to, "butler_jones_hazeel_cultist", 1)
        t.exec("h.jones.talk.dialog", t.chat.play, { "npc:Hello again friend", "player:", "npc:You don't have to pretend", "player:So do you have any idea", "npc:No idea I'm afraid", "player:And Sir Ceril", "npc:Ha!", "player:I'll keep on looking" })
        t.ticks(2)
        -- upstairs: the bedroom, the secret wall (Knock-at: ~check_axis carries the player across the north edge
        -- of 2572,3270, quest_hazeelcult_locs.rs2:161-178), the ladder to level 2, the chest unlocked with the crate key
        stairs_up("h.scroll.stairsUp")
        bedroom_in("h.scroll.bedroomDoorIn")
        t.exec("h.bookcase.knock", t.player.cross_trap, { loc = "carnilleanbookcase_knock", op = 1, op_name = "Knock-at",
            at = { 2572, 3270, 1 }, src = { 2572, 3270 }, dest = { 2572, 3271 }, attempts = 1 })
        t.ticks(2)
        t.exec("h.ladder.f2", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
            at = { 2573, 3271, 1 }, src = { 2572, 3271 }, dest = { 2572, 3271, 2 } })
        t.ticks(2)
        local chest = t.player.by_symbol("loc", "carnilleanshutchest")
        t.check("h.chest.found", chest ~= nil, "carnilleanshutchest resolved")
        t.exec("h.chest.locked", t.player.click_loc, "carnilleanshutchest", 1)
        t.exec("h.chest.unlock", t.player.use_on, "carnilleanchestkey", chest)
        t.exec("h.chest.dialog", t.chat.play, { "mesbox:Inside the chest you find the Scroll of Hazeel" })
        t.ticks(2)
        t.expect("quest.stage.given_armour_or_scroll.hazeel", t.quest.expect_stage("given_armour_or_scroll"))
        t.exec("h.scroll.have", t.inv.await, "hazeel_scroll", 1, 3)
        -- down the ladder, back through the secret wall (south: p_teleport(loc_coord) 2572,3270), out of the
        -- bedroom and down the stairs to Jones in the hall
        t.exec("h.laddertop.f2", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2573, 3271, 2 }, src = { 2572, 3271 }, dest = { 2572, 3271, 1 } })
        t.ticks(2)
        t.exec("h.bookcase.knockBack", t.player.cross_trap, { loc = "carnilleanbookcase_knock", op = 1, op_name = "Knock-at",
            at = { 2572, 3270, 1 }, src = { 2572, 3271 }, dest = { 2572, 3270 }, attempts = 1 })
        t.ticks(2)
        bedroom_out("h.scroll.bedroomDoorOut")
        stairs_down("h.scroll.stairsDown")
        t.exec("h.jones.talk2", t.player.talk_to, "butler_jones_hazeel_cultist", 1)
        t.exec("h.jones.talk2.dialog", t.chat.play, { "player:Hello Jones", "npc:Have you recovered", "player:I have it right here", "npc:Incredible", "npc:Quick, get it back" })
        t.ticks(2)
        -- back to Alomone by raft, the ritual
        leave_house("h.caveTrip4")
        t.exec("h.goto-cave4", t.player.goto_tile, 2587, 3237, 0)
        t.exec("h.cave.enter4", t.player.click_loc, "hazeelcultcave", 1)
        t.ticks(3)
        t.exec("h.raft.end2", t.player.click_loc, "hazeelsewerraft", 1)
        t.exec("h.raft.end2.dialog", t.chat.play, { "mesbox:The raft washes up the sewer, past the islands" })
        t.ticks(10)
        t.exec("h.goto-near-alomone", t.player.goto_tile, 2607, 9680, 0)
        t.ticks(3)
        local snap_r2, snap2 = t.skill.snapshot()
        local _, coins_before = t.inv.count("coins")
        t.exec("h.alomone.ritual", t.player.talk_to, "alomone_hazeel_cultist_1op", 1)
        t.exec("h.alomone.ritual.dialog", t.chat.play, {
            "player:Hi.", "npc:Have you brought me", "player:Yep. Got it", "npc:FINALLY", "mesbox:You hand Alomone", "npc:Yes... YES", "npc:Watch adventurer",
            "npc:Lord Hazeel", "npc:Sentente", "mesbox:Alomone continues", "npc:Dintenta", "mesbox:As Alomone finishes" })
        local rr, rd = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "hazeel.reopen" }, 12)
        t.step("h.hazeel.reopen", rr == "ok" and "PASS" or "FAIL", "await chat reopen after npc_add + p_delay(5) -> " .. tostring(rr) .. " " .. tostring(rd))
        t.exec("h.alomone.ritual.dialog2", t.chat.play, {
            "npc:My loyal followers", "npc:Soon this world",
            "mesbox:Hazeel turns", "npc:Adventurer. I know", "npc:You may not be", "npc:Weak as I am", "npc:the riches", "player:I serve nobody", "npc:Your insolence", "npc:Although your true",
            "mesbox:Hazeel gives you some coins", "npc:And now I must", "npc:to join my fellow", "mesbox:The cultists let out" })
        t.ticks(8)
        t.expect("quest.stage.complete.hazeel", t.quest.expect_stage("complete"))
        t.exec("h.reward.coins", t.inv.await, "coins", coins_before + 2000, 4)
        t.expect("h.reward.thieving", t.skill.expect_gain("thieving", 1500, snap2))
        end
        t.finish(0)
    end,
}
