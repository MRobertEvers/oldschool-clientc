-- Ratcatchers. Guide: RatCatchers.java via ladder.py (46 steps). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_ratcatchers/scripts/ratcatchers.rs2.
-- Guide requires: Icthlarin's Little Helper finished, The Giant Dwarf started
-- (ratcatchers_shared.rs2 ratcatch_meets_prereqs), a non-overgrown cat, a Catspeak amulet.
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03): no goto_tile enters or leaves a
-- closed space. Every door, gate, ladder and manhole is pressed on every crossing:
--   * Gertrude's front door fai_varrock_door 3151,3412 (in and out);
--   * the Varrock manhole down and the sewer ladder up (climb, map frame 0 <-> 1);
--   * Jimmy Dazzler's house: poshdoor 2569,3322 (front) and 2565,3320 (his room), in and out;
--   * Varrock's members' east gate guidorgatelclosed 3264,3405 (walk-through, east_gate.rs2) on
--     every trip to and from Hooknosed Jack's quarter (comp.py: a 473-tile pocket whose only
--     on-foot way out is the gate);
--   * the warehouse doors fai_varrock_poor_door 3267,3382 and 3272,3380 and its ladder, both ways;
--   * Veldaban's door dwarf_keldagrim_door 2827,10218 out of the GE trapdoor's landing room;
--   * the Shantay Pass doorway with a bought pass, then the Rug Merchant's carpet to Pollnivneach
--     (the desert's only way in on foot is the pass);
--   * the mansion (map square m44_79, a sealed island): IN by the party directions' own prompt
--     ("Follow the directions to the house.", ratcatchers.rs2 [opheld1,ratcatchers_party_directions]
--     p_telejump to the garden 2847,5066), the walk round the back and the trellis up (lands on the
--     trellis top 2844,5104,1); the upper floor's door 2838,5099, the mansion ladder both ways, the
--     ground floor's door 2860,5093 both ways; OUT by the trellis top's Climb-down, which leaves the
--     grounds for Ardougne outside Jimmy's front door (ratcatchers.rs2 [proc,ratcatch_leave_grounds],
--     OSRS wiki Ratcatchers/Quick_guide "Climb back down the trellis to return to Ardougne");
--   * the Port Sarim manhole and the rat pits' ladder by their cache maplink rows
--     (maplink_0_47_50_10_31_down 3018,3231 -> 2962,9650; maplink_0_46_150_18_50_up -> 3018,3233).
-- Long trips are real teleports cast by click (t.player.teleport_cast): Ardougne once, to reach
-- Jimmy from the Varrock sewer (the scroll is read by click at the start), Varrock and Falador. Every other goto is an overland hop between
-- open tiles (reach.py REACH closed-doors) or inside one passage (the sewer, Keldagrim).
-- Hooknosed Jack wanders: each talk to him is awaited present and retried (attempts are notes,
-- the talk's own row and the quest stage are the grade).

local VARROCK_RUNES = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }
local FALADOR_RUNES = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }
local ARDOUGNE_RUNES = { { "waterrune", 2 }, { "lawrune", 2 } }

local function count(t, item)
    local r, n = t.inv.count(item)
    if r ~= "ok" then return -1 end
    return n
end

local function tile_text(t)
    local r, tl = t.world.tile()
    if r ~= "ok" or not tl then return "tile ?" end
    return tl.x .. "," .. tl.z .. "," .. tl.level
end

-- Varrock's members' east gate (east_gate.rs2 [label,varrock_east_gate]): walk-through, pressed on
-- every crossing; from the west the press lands on the gate tile 3264, from the east on 3263.
local function east_gate_in(t, name)
    t.exec(name, t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
        near = { 3263, 3405 }, far_ok = function(tile) return tile.x >= 3264 end,
        far_desc = "inside Varrock's south-east quarter, x >= 3264" })
end
local function east_gate_out(t, name)
    t.exec(name, t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
        near = { 3264, 3405 }, far_ok = function(tile) return tile.x <= 3263 end,
        far_desc = "back in Varrock, x <= 3263" })
end

-- The rat warehouse south of Jack: outer door 3267,3382 (east edge), inner door 3272,3380 (south
-- edge) in front of the ladder room (comp.py: yard 473 -> hallway 20 -> ladder room 35 tiles).
local function warehouse_in(t, name)
    t.exec(name .. ".outerDoor", t.player.pass_door, { closed = "fai_varrock_poor_door",
        open = "fai_varrock_poor_door_open", at = { 3267, 3382, 0 }, near = { 3267, 3382 }, far = { 3268, 3382 } })
    t.exec(name .. ".innerDoor", t.player.pass_door, { closed = "fai_varrock_poor_door",
        open = "fai_varrock_poor_door_open", at = { 3272, 3380, 0 }, near = { 3272, 3380 }, far = { 3272, 3379 } })
end
local function warehouse_out(t, name)
    t.exec(name .. ".innerDoor", t.player.pass_door, { closed = "fai_varrock_poor_door",
        open = "fai_varrock_poor_door_open", at = { 3272, 3380, 0 }, near = { 3272, 3379 }, far = { 3272, 3380 } })
    t.exec(name .. ".outerDoor", t.player.pass_door, { closed = "fai_varrock_poor_door",
        open = "fai_varrock_poor_door_open", at = { 3267, 3382, 0 }, near = { 3268, 3382 }, far = { 3267, 3382 } })
end
-- maplink_0_51_52_3_51_up: 3267,3379,0 -> 3269,3379,1 (keyed on the player's tile).
local function warehouse_ladder_up(t, name)
    t.exec(name, t.player.climb, { loc = "fai_varrock_ladder", op = 1, op_name = "Climb-up",
        at = { 3268, 3379, 0 }, src = { 3267, 3379 }, dest = { 3269, 3379, 1 } })
end
local function warehouse_ladder_down(t, name)
    t.exec(name, t.player.climb, { loc = "fai_varrock_laddertop", op = 1, op_name = "Climb-down",
        at = { 3268, 3379, 1 }, dest = { 3267, 3379, 0 }, slack = 1 })
end

-- Jimmy Dazzler's house north of Ardougne Castle: front door 2569,3322 (west edge), his room's
-- door 2565,3320 (west edge); comp.py: street 2,997 -> hall 21 -> his room 16 tiles.
local function jimmy_in(t, name, at_door)
    -- at_door: the player already stands on 2570,3322,0 (the trellis's own landing), no hop.
    if not at_door then t.exec("goto-" .. name .. ".house", t.player.goto_tile, 2570, 3322, 0) end
    t.exec(name .. ".frontDoorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
        at = { 2569, 3322, 0 }, near = { 2569, 3322 }, far = { 2568, 3322 } })
    t.exec(name .. ".roomDoorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
        at = { 2565, 3320, 0 }, near = { 2565, 3320 }, far = { 2564, 3320 } })
end
local function jimmy_out(t, name)
    t.exec(name .. ".roomDoorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
        at = { 2565, 3320, 0 }, near = { 2564, 3320 }, far = { 2565, 3320 } })
    t.exec(name .. ".frontDoorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
        at = { 2569, 3322, 0 }, near = { 2568, 3322 }, far = { 2570, 3322 } })
end

-- A talk to a wandering npc (Hooknosed Jack): awaited present, pressed, and pressed again when the
-- press missed (he stepped between aim and press). Each miss is a note; the row is the last press.
local function talk_wanderer(t, name, npc)
    local r, d
    for try = 1, 6 do
        -- Walk up to him first when he is more than 3 tiles off: from the warehouse door he stands
        -- ~17 tiles north, off the viewport, and the presses read `covered ... 67 off-viewport`
        -- (run3, three misses).
        local _, me = t.world.tile()
        local _, _, rows = t.npc.tiles(npc, 30)
        local him = rows and rows[1]
        if him and me and math.max(math.abs(him.x - me.x), math.abs(him.z - me.z)) > 3 then
            local wr, wd = t.player.walk_to(him.x, him.z - 1, 30)
            t.note(name .. "-approach" .. try .. ": " .. npc .. " at " .. him.x .. "," .. him.z .. ", walk_to "
                .. him.x .. "," .. (him.z - 1) .. " -> " .. tostring(wr) .. " " .. string.sub(tostring(wd), 1, 120))
        end
        local pr = t.npc.await_present(npc, 15, 5)
        r, d = t.player.talk_to(npc, 1)
        if r == "ok" or t.chat.kind() ~= "none" then break end
        t.note(name .. "-try" .. try .. ": talk_to " .. npc .. " -> " .. tostring(r) .. " "
            .. string.sub(tostring(d), 1, 240) .. " (await_present " .. tostring(pr) .. "); pressed again")
        t.ticks(2)
    end
    t.check(name, r == "ok" or t.chat.kind() ~= "none", "talk_to " .. npc .. " -> " .. tostring(r) .. " "
        .. string.sub(tostring(d), 1, 240) .. "; chat " .. tostring(t.chat.kind()))
end

-- A cat catch: one press per standing rat. The attempt is a note; the stage / varbit is the grade.
local function catch_press(t, name, sym, slot)
    local r, d = t.player.press(sym, 2, 10, { slot = slot })
    local pounced = false
    if t.chat.kind() == "mesbox" then
        t.exec(name .. "-pounce", t.chat.play, { "mesbox:Your cat pounces" })
        pounced = true
    end
    t.note(name .. ": press " .. sym .. " op 2 -> " .. tostring(r) .. " " .. string.sub(tostring(d), 1, 260)
        .. (pounced and " (pounced)" or " (no pounce page)"))
    return pounced
end

return {
    id = "ratcatchers",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel hitpoints 70", "::setlevel defence 40",
        "::setlevel magic 51",                 -- the travel teleports (Ardougne 51, Falador 37, Varrock 25)
        "::complete quest_gertrudescat", -- Icthlarin prerequisite; Gertrude only offers ratcatchers after Fluffs is done
        "::complete quest_icthlarinslittlehelper",   -- guide prerequisite quest
        "::complete quest_giantdwarf",  -- guide prerequisite (must be started)
        "::complete quest_plaguecity",  -- Ardougne Teleport (teleport.rs2:16-22); the scroll is read by click
        "::give ardougnescroll 1",
        "::give kittenobject 1",        -- guide: A non-overgrown cat
        "::give ics_little_amulet_of_catspeak 1", -- guide: Catspeak amulet
        "::give vial_empty 1", "::give kwuarm 1", "::give red_spiders_eggs 1", -- guide: brought for Jack's poison
        "::give cheese 4", "::give bucket_milk 1", "::give unicorn_horn_dust 1", "::give marentill 1",
        "::give trout 6", "::give pot_empty 1", "::give weeds 1", "::give tinderbox 1",
        "::give coins 400",             -- 101 snake charmer + 5 Shantay pass + 200 carpet fare
        -- 1 x Ardougne, 2 x Varrock, 2 x Falador Teleport (magic_spells.dbrow runesrequired)
        "::give waterrune 4", "::give lawrune 6", "::give airrune 12", "::give firerune 2",
    },
    bind = {
        varp = "varb1404_ratcatch_var",
        constants = {
            not_started = 0, sewer_started = 5, sewer_caught_base = 6, sewer_all_caught = 14,
            sewer_reported = 15, jimmy_talked = 20, jimmy_directions = 22, mansion_catching = 30,
            complete = 127, mansion_done = 35, jimmy_done = 40,
            jack_poisoning_holes = 45, jack_holes_done = 50, jack_after_cheese = 55, apoth_done = 60,
            jack_cured = 62, kingrat_defeated = 65, jack_after_fight = 70, joe_talked = 75, joe_smoked = 80,
            joe_again = 85, felkrash_talked = 90, face_talked = 95, charm_obtained = 100, tune_played = 105,
        },
        row = "quest_ratcatchers",
        display = "Ratcatchers",
        points = 2,
    },
    legs = {
        { name = "sewer", run = function(t)
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
            t.exec("equipCatspeak", t.player.equip, "ics_little_amulet_of_catspeak")
            -- The Ardougne Teleport scroll (elena_teleport_scroll.rs2): read once, the spell is learnt.
            t.exec("readArdougneScroll", t.player.inv_op, "ardougnescroll", 1)
            t.exec("readArdougneScroll-dialog", t.chat.play, { "mesbox:You memorise", "mesbox:You can now cast" })
            local _, el = t.var.server("varp165_elenaquest")
            t.check("readArdougneScroll.learnt", el == 30 and count(t, "ardougnescroll") == 0,
                "varp165_elenaquest = " .. tostring(el) .. " (elena_complete_read_scroll = 30), scroll left " .. count(t, "ardougnescroll"))

            -- LEG 1: talkToGertrude -- her house 3148-3153,3404-3411, front door 3151,3412 (south edge).
            t.exec("goto-talkToGertrude", t.player.goto_tile, 3151, 3414, 0)
            t.exec("talkToGertrude.doorIn", t.player.pass_door, { closed = "fai_varrock_door", open = "fai_varrock_door_open",
                at = { 3151, 3412, 0 }, near = { 3151, 3412 }, far = { 3151, 3411 } })
            t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
            t.exec("talkToGertrude-dialog", t.chat.play, {
                "player:Hello Gertrude.", "npc:Oh, hello!", "npc:They used to be", "player:Ratcatchers?",
                "npc:Jimmy Dazzler", "choose:Yes.", "player:Yes.",
            })
            t.ticks(2)
            t.expect("quest.stage.sewer_started", t.quest.expect_stage("sewer_started"))
            t.exec("talkToGertrude.doorOut", t.player.pass_door, { closed = "fai_varrock_door", open = "fai_varrock_door_open",
                at = { 3151, 3412, 0 }, near = { 3151, 3411 }, far = { 3151, 3413 } })

            -- enterSewer: the manhole south-east of Varrock Castle (manholes.rs2: Open, then Climb-down,
            -- p_telejump(coord + 6400) from the tile the player stands on).
            t.exec("goto-enterSewer", t.player.goto_tile, 3236, 3460, 0)
            t.exec("openManhole", t.player.click_loc, "manholeclosed", 1, { at = { 3237, 3458, 0 } })
            t.ticks(2)
            local mr = t.world.loc_near("manholeopen", 4, { at = { 3237, 3458, 0 } })
            t.check("openManhole.open", mr == "ok", "manholeopen on 3237,3458,0 after the Open press: " .. tostring(mr))
            t.exec("enterSewer", t.player.climb, { loc = "manholeopen", op = 1, op_name = "Climb-down",
                at = { 3237, 3458, 0 }, src = { 3236, 3458 }, dest = { 3236, 9858, 0 } })

            -- talkToPhingspet: one passage of the sewer (reach.py 3236,9858 -> 3245,9867 REACH len=18).
            t.exec("goto-talkToPhingspet", t.player.goto_tile, 3245, 9867, 0)
            t.exec("talkToPhingspet", t.player.talk_to, "vc_phingspet", 1)
            t.exec("talkToPhingspet-dialog", t.chat.play, {
                "player:Gertrude sent me", "npc:About time!",
            })
            t.ticks(2)
            t.expect("quest.stage.sewer_caught_base", t.quest.expect_stage("sewer_caught_base"))
            -- catch8Rats: each press is an attempt (a rat can wander off); the stage is the grade.
            local tries = 0
            while tries < 30 do
                local _, v = t.var.server("varb1404_ratcatch_var")
                if v >= 14 then break end
                tries = tries + 1
                local _, _, rows = t.npc.tiles("rat", 30)
                local row = nil
                for _, rr in ipairs(rows or {}) do if rr.z > 9855 and rr.z < 9919 then row = rr break end end
                if row then
                    catch_press(t, "catch8Rats-try" .. tries, "rat", row.slot)
                    t.ticks(2)
                else
                    t.ticks(3)
                end
            end
            t.expect("quest.stage.sewer_all_caught", t.quest.expect_stage("sewer_all_caught"))
            t.exec("talkToPhingspetAgain", t.player.talk_to, "vc_phingspet", 1)
            t.exec("talkToPhingspetAgain-dialog", t.chat.play, { "player:My cat's caught eight rats", "npc:Wonderful" })
            t.ticks(2)
            t.expect("quest.stage.sewer_reported", t.quest.expect_stage("sewer_reported"))
            -- Out by the sewer's own ladder: maplink_0_50_154_37_2_up 3237,9858,0 -> 3236,3458,0.
            t.exec("leaveSewer", t.player.climb, { loc = "fai_varrock_manhole_ladder", op = 1, op_name = "Climb-up",
                at = { 3237, 9858, 0 }, src = { 3237, 9858 }, dest = { 3236, 3458, 0 } })
        end },

        { name = "jimmy", run = function(t)
            -- talkToJimmy: Ardougne Teleport, the overland walk from the market (reach.py 2661,3301 ->
            -- 2570,3322 REACH len=128), and Jimmy's two doors.
            t.player.teleport_cast("ardougne_teleport", { 2661, 3301, 0 }, { name = "talkToJimmy.ardougneTeleport",
                runes = ARDOUGNE_RUNES, where = "Ardougne market" })
            jimmy_in(t, "talkToJimmy")
            t.exec("talkToJimmy", t.player.talk_to, "vc_jimmy_dazzler", 1)
            t.exec("talkToJimmy-dialog", t.chat.play, { "player:Gertrude sent me", "npc:Ah, a new recruit" })
            t.ticks(2)
            t.expect("quest.stage.jimmy_talked", t.quest.expect_stage("jimmy_talked"))
            jimmy_out(t, "talkToJimmy")
            -- readDirections (Quest Helper: "Follow the directions to the house."): read from Ardougne
            -- with the cat, the scroll's prompt, the p_telejump to the mansion garden 2847,5066
            -- (ratcatchers.rs2 [opheld1,ratcatchers_party_directions]; the mansion is on no map).
            local dir0 = count(t, "ratcatchers_party_directions")
            t.check("readDirections.from", tile_text(t) == "2570,3322,0" and dir0 == 1,
                "reading from " .. tile_text(t) .. " outside Jimmy's front door (Ardougne), directions held " .. dir0)
            t.exec("readDirections", t.player.inv_op, "ratcatchers_party_directions", 1)
            t.exec("readDirections-follow", t.chat.play, { "mesbox:The scroll contains directions to a manor",
                "choose:Follow the directions to the house." })
            t.await({ level = function() local r, tl = t.world.tile() return r == "ok" and tl.z > 5000 and tl.z < 5200 end,
                note = "the directions' p_telejump to the mansion grounds" }, 10)
            t.expect("quest.stage.jimmy_directions", t.quest.expect_stage("jimmy_directions"))
            local _, dt = t.world.tile()
            t.check("readDirections.landed", dt ~= nil and dt.x == 2847 and dt.z == 5066 and dt.level == 0,
                "followed the directions to " .. tile_text(t) .. " (the garden 2847,5066,0, Quest Helper climbTrellis's first point)")
        end },

        { name = "mansion", run = function(t)
            -- climbTrellis approach: Quest Helper's climbTrellis line (RatCatchers.java:336-352) round
            -- the back of the mansion, hops of at most 10 (reach.py 2847,5066 -> 2844,5105 REACH len=74).
            t.exec("walkToTrellis", t.player.walk_route, {
                { 2840, 5066 }, { 2840, 5075 }, { 2833, 5075 }, { 2826, 5075 }, { 2826, 5082 }, { 2824, 5084 },
                { 2824, 5088 }, { 2826, 5090 }, { 2826, 5093 }, { 2824, 5095 }, { 2824, 5099 }, { 2826, 5101 },
                { 2826, 5111 }, { 2835, 5112 }, { 2837, 5112 }, { 2837, 5111 }, { 2844, 5106 },
            })
            -- climbTrellis: needs the directions read and the cat; spawns the six private vc_rat
            -- (ratcatch_spawn_mansion_rats) and lands on the trellis top inside the upper floor.
            t.exec("climbTrellis", t.player.climb, { loc = "vc_trellis_base", op = 1, op_name = "Climb-up",
                at = { 2844, 5105, 0 }, dest = { 2844, 5104, 1 } })
            t.expect("quest.stage.mansion_catching", t.quest.expect_stage("mansion_catching"))
            -- catchRat1: the north-west room 2832-2838,5093-5100, door vc_elfdoor 2838,5099 (east edge).
            t.exec("catchRat1.doorIn", t.player.pass_door, { closed = "vc_elfdoor", open = "vc_elfdooropen",
                at = { 2838, 5099, 1 }, near = { 2839, 5099 }, far = { 2838, 5099 } })
            local function rat_slot(x, z, level)
                local _, _, rows = t.npc.tiles("vc_rat", 20)
                for _, rr in ipairs(rows or {}) do
                    if rr.x == x and rr.z == z and rr.level == level then return rr.slot end
                end
                return nil
            end
            local function catch(name, x, z, level, bit, near)
                t.exec("walk-" .. name, t.player.walk_to, near[1], near[2])
                local slot = rat_slot(x, z, level)
                t.check(name .. ".present", slot ~= nil, "private vc_rat on " .. x .. "," .. z .. "," .. level .. ": slot " .. tostring(slot))
                -- A press from the first pose can answer `covered` under the upper floor: re-aim the
                -- camera (yaw 0 / 1024 / 512, a steep pitch) between attempts. Nothing walks.
                local poses = { { 0, 383, 600 }, { 1024, 450, 500 }, { 512, 450, 500 }, { 1536, 383, 600 } }
                for try = 1, #poses do
                    if slot == nil or select(2, t.var.server(bit)) == 1 then break end
                    t.drive.camera(poses[try][1], poses[try][2], poses[try][3])
                    catch_press(t, name .. "-try" .. try, "vc_rat", slot)
                    t.ticks(2)
                end
                local _, caught = t.var.server(bit)
                t.check(name .. ".caught", caught == 1, bit .. " = " .. tostring(caught) .. " (ratcatch_mansion_rat_caught)")
            end
            catch("catchRat1", 2832, 5098, 1, "varb1424_vc_raton_off1", { 2835, 5098 })
            t.exec("catchRat1.doorOut", t.player.pass_door, { closed = "vc_elfdoor", open = "vc_elfdooropen",
                at = { 2838, 5099, 1 }, near = { 2838, 5099 }, far = { 2839, 5099 } })
            -- catchRat2And3: the east side of the upper floor (comp.py: one 240-tile floor with the landing).
            catch("catchRat2", 2861, 5093, 1, "varb1425_vc_raton_off2", { 2860, 5093 })
            catch("catchRat3", 2858, 5087, 1, "varb1426_vc_raton_off3", { 2858, 5088 })
            -- climbDownLadderInMansion: laddertop 2862,5092,1, no maplink row: one plane down on the tile.
            t.exec("climbDownLadderInMansion", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
                at = { 2862, 5092, 1 }, src = { 2862, 5093 }, dest = { 2862, 5093, 0 } })
            -- catchRemainingRats: two in the ladder room (40 tiles), one north of door 2860,5093 (north edge).
            catch("catchRemainingRats5", 2857, 5091, 0, "varb1428_vc_raton_off5", { 2859, 5091 })
            catch("catchRemainingRats6", 2863, 5086, 0, "varb1429_vc_raton_off6", { 2863, 5087 })
            t.exec("catchRemainingRats.doorIn", t.player.pass_door, { closed = "vc_elfdoor", open = "vc_elfdooropen",
                at = { 2860, 5093, 0 }, near = { 2860, 5093 }, far = { 2860, 5094 } })
            catch("catchRemainingRats4", 2863, 5101, 0, "varb1427_vc_raton_off4", { 2863, 5100 })
            t.expect("quest.stage.mansion_done", t.quest.expect_stage("mansion_done"))
            -- leaveMansion: back the way in -- the ground floor's door, the mansion ladder up (no
            -- maplink row: one plane up on the tile), the upper floor to the trellis top, Climb-down.
            t.exec("leaveMansion.doorOut", t.player.pass_door, { closed = "vc_elfdoor", open = "vc_elfdooropen",
                at = { 2860, 5093, 0 }, near = { 2860, 5094 }, far = { 2860, 5093 } })
            t.exec("leaveMansion.ladderUp", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
                at = { 2862, 5092, 0 }, src = { 2862, 5093 }, dest = { 2862, 5093, 1 } })
            t.exec("walk-leaveMansion.trellisTop", t.player.walk_route, { { 2856, 5098 }, { 2848, 5102 }, { 2844, 5104 } })
            -- The trellis top (vc_blank_trellis_top_trigger 2844,5104,1) is the grounds' way out:
            -- [oploc1,vc_blank_trellis_top_trigger] -> [proc,ratcatch_leave_grounds], p_telejump to
            -- 0_40_51_10_58 = 2570,3322,0, the open street tile outside Jimmy Dazzler's front door
            -- (OSRS wiki Ratcatchers/Quick_guide: "Climb back down the trellis to return to Ardougne").
            t.exec("leaveMansion.trellisDown", t.player.climb, { loc = "vc_blank_trellis_top_trigger", op = 1,
                op_name = "Climb-down", at = { 2844, 5104, 1 }, dest = { 2570, 3322, 0 } })
            t.expect("quest.stage.mansion_done.afterExit", t.quest.expect_stage("mansion_done"))
            -- talkToJimmyAgain: from the trellis's landing straight through Jimmy's two doors.
            jimmy_in(t, "talkToJimmyAgain", true)
            t.exec("talkToJimmyAgain", t.player.talk_to, "vc_jimmy_dazzler", 1)
            t.exec("talkToJimmyAgain-dialog", t.chat.play, { "player:My cat caught all six", "npc:Splendid" })
            t.ticks(2)
            t.expect("quest.stage.jimmy_done", t.quest.expect_stage("jimmy_done"))
            jimmy_out(t, "talkToJimmyAgain")
        end },

        { name = "jack", run = function(t)
            -- talkToJack: Varrock Teleport, the walk to the east gate (reach.py 3212,3424 -> 3262,3405
            -- REACH len=71), the gate.
            t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "talkToJack.varrockTeleport",
                runes = VARROCK_RUNES, where = "Varrock square" })
            t.exec("goto-talkToJack.eastGate", t.player.goto_tile, 3262, 3405, 0)
            east_gate_in(t, "talkToJack.eastGateIn")
            talk_wanderer(t, "talkToJack", "vc_hooknosed_jack")
            t.exec("talkToJack-dialog", t.chat.play, {
                "player:Gertrude sent me", "npc:Good, good.", "player:I've got a vial", "npc:Perfect",
            })
            t.ticks(2)
            t.expect("quest.stage.jack_poisoning_holes", t.quest.expect_stage("jack_poisoning_holes"))
            t.check("talkToJack.poison", count(t, "rat_poison") == 1 and count(t, "vial_empty") == 0 and count(t, "kwuarm") == 0
                and count(t, "red_spiders_eggs") == 0,
                "rat_poison " .. count(t, "rat_poison") .. ", vial/kwuarm/eggs left " .. count(t, "vial_empty") .. "/"
                    .. count(t, "kwuarm") .. "/" .. count(t, "red_spiders_eggs"))
            t.exec("useRatPoisonOnCheese", t.player.use_item_on_item, "cheese", "rat_poison")
            t.ticks(2)
            t.check("useRatPoisonOnCheese.count", count(t, "ratcatchers_poisonedcheese") == 4 and count(t, "cheese") == 0,
                "poisoned cheese " .. count(t, "ratcatchers_poisonedcheese") .. ", plain cheese " .. count(t, "cheese"))
            warehouse_in(t, "climbJackLadder")
            warehouse_ladder_up(t, "climbJackLadder")
            local order = { 2, 3, 4, 1 }
            for _, n in ipairs(order) do
                local before = count(t, "ratcatchers_poisonedcheese")
                local hole, hres = t.player.by_symbol("loc", "ratcatchers_rathole" .. n)
                t.check("useCheeseOnHole" .. n .. ".resolve", hole ~= nil, "by_symbol -> " .. tostring(hres))
                t.exec("useCheeseOnHole" .. n, t.player.use_on, "ratcatchers_poisonedcheese", hole)
                t.ticks(2)
                t.check("useCheeseOnHole" .. n .. ".used", count(t, "ratcatchers_poisonedcheese") == before - 1,
                    "poisoned cheese " .. before .. " -> " .. count(t, "ratcatchers_poisonedcheese"))
            end
            t.expect("quest.stage.jack_holes_done", t.quest.expect_stage("jack_holes_done"))
            warehouse_ladder_down(t, "goDownToJack")
            warehouse_out(t, "goDownToJack")
            talk_wanderer(t, "talkToJackAfterCheese", "vc_hooknosed_jack")
            t.exec("talkToJackAfterCheese-dialog", t.chat.play, {
                "player:I've poisoned all four", "npc:Good work!", "choose:Talk about the Ratcatchers Quest.",
                "player:Talk about the Ratcatchers Quest.", "npc:The King Rat lives",
            })
            t.ticks(2)
            t.expect("quest.stage.jack_after_cheese", t.quest.expect_stage("jack_after_cheese"))
            -- talkToApoth: out by the gate, the walk west (reach.py 3262,3405 -> 3196,3402 REACH len=101).
            east_gate_out(t, "talkToApoth.eastGateOut")
            t.exec("goto-talkToApoth", t.player.goto_tile, 3196, 3402, 0)
            t.exec("talkToApoth", t.player.talk_to, "apothecary", 1)
            t.exec("talkToApoth-dialog", t.chat.play, {
                "player:Hooknosed Jack sent me", "npc:I've got just the ingredients",
            })
            t.ticks(2)
            t.expect("quest.stage.apoth_done", t.quest.expect_stage("apoth_done"))
            t.check("talkToApoth.antipoison", count(t, "ratcatchers_cat_antipoison") == 1 and count(t, "bucket_milk") == 0
                and count(t, "unicorn_horn_dust") == 0 and count(t, "marentill") == 0,
                "cat antipoison " .. count(t, "ratcatchers_cat_antipoison") .. ", milk/dust/marrentill left "
                    .. count(t, "bucket_milk") .. "/" .. count(t, "unicorn_horn_dust") .. "/" .. count(t, "marentill"))
            t.exec("goto-talkToJackAfterApoth.eastGate", t.player.goto_tile, 3262, 3405, 0)
            east_gate_in(t, "talkToJackAfterApoth.eastGateIn")
            talk_wanderer(t, "talkToJackAfterApoth", "vc_hooknosed_jack")
            t.exec("talkToJackAfterApoth-dialog", t.chat.play, { "player:The Apothecary gave me", "npc:Good. Head back" })
            t.ticks(2)
            t.expect("quest.stage.jack_cured", t.quest.expect_stage("jack_cured"))
            warehouse_in(t, "climbJackLadderAgain")
            warehouse_ladder_up(t, "climbJackLadderAgain")
            local wall, wres = t.player.by_symbol("loc", "vc_blank_walldecor")
            t.check("useCatOnHole.resolve", wall ~= nil, "by_symbol -> " .. tostring(wres))
            -- The hole is on the wall north of the player: from the default camera (south, looking north)
            -- the player's own model covers it and every press hunts pixels for ~25 ticks (7-9 King Rat
            -- rounds; probe rc_r2_fight1). A camera turned to the north-west (yaw 1792) presses it at once
            -- (5-7 ticks a feed, probe rc_r2_fight2), so a fish lands while the cat is still hurt.
            local function face_hole() t.drive.camera(1792, 383, 600) end
            face_hole()
            t.exec("useCatOnHole", t.player.use_on, "kittenobject", wall)
            t.exec("useCatOnHole-dialog", t.chat.play, { "mesbox:Send your cat", "choose:Yes.", "choose:Be careful in there, cat!" })
            local _, active = t.var.server("varp7202_ratcatch_kr_active")
            t.check("useCatOnHole.sent", active == 1, "varp7202_ratcatch_kr_active = " .. tostring(active))
            -- feedCatAsItFights: ratcatch_kingrat_round every 3 ticks; the cat has 5 hp, the King Rat 10
            -- (ratcatchers.constant); the cat hits random(3), the King random(2) a round. A trout used on
            -- the wall heals 2 (ratcatch_feed_cat). The cat is fed once it is hurt; in a short fight the
            -- last fish can still land after the win (the win resets the hurt to 0 and the fish is
            -- refused) -- that attempt is a note. "Be careful" makes a nearly dead cat
            -- scramble out (hurt 4) instead of dying: fed back to full and sent in again.
            local function cat_hp_lines()
                local _, lines = t.msg.last(100)
                local lowest, rounds = 5, 0
                for _, line in ipairs(lines or {}) do
                    local h = string.match(tostring(line.text), "cat health (%d)/5")
                    if h then
                        rounds = rounds + 1
                        if tonumber(h) < lowest then lowest = tonumber(h) end
                    end
                    if string.find(tostring(line.text), "almost dead", 1, true) and lowest > 1 then lowest = 1 end
                end
                return lowest, rounds
            end
            local fed, trout0, resent = 0, count(t, "trout"), 0
            local function feed(label, hurt)
                local tb = count(t, "trout")
                face_hole()
                t.exec(label, t.player.use_on, "trout", wall)
                for _ = 1, 4 do
                    if count(t, "trout") < tb then break end
                    t.ticks(1)
                end
                local _, hurt2 = t.var.server("varp7204_ratcatch_kr_cat_hurt")
                local _, v2 = t.var.server("varb1404_ratcatch_var")
                if count(t, "trout") == tb and v2 >= 65 then
                    t.note(label .. ": the fight ended (stage " .. v2 .. ") before the trout landed; trout kept " .. tb)
                else
                    t.check(label .. ".ate", count(t, "trout") == tb - 1,
                        "trout " .. tb .. " -> " .. count(t, "trout") .. "; cat hurt " .. hurt .. " -> " .. tostring(hurt2) .. " of 5")
                    fed = fed + 1
                end
            end
            for _ = 1, 160 do
                local _, v = t.var.server("varb1404_ratcatch_var")
                if v >= 65 then break end
                local _, hurt = t.var.server("varp7204_ratcatch_kr_cat_hurt")
                local _, act = t.var.server("varp7202_ratcatch_kr_active")
                hurt = hurt or 0
                if act == 0 and resent < 2 then
                    -- the cat scrambled out nearly dead: feed it whole, send it back in.
                    while (select(2, t.var.server("varp7204_ratcatch_kr_cat_hurt")) or 0) > 0 and count(t, "trout") > 0 do
                        feed("feedCatAsItFights-" .. (fed + 1), select(2, t.var.server("varp7204_ratcatch_kr_cat_hurt")))
                    end
                    resent = resent + 1
                    face_hole()
                    t.exec("useCatOnHole.again" .. resent, t.player.use_on, "kittenobject", wall)
                    t.exec("useCatOnHole.again" .. resent .. "-dialog", t.chat.play, { "mesbox:Send your cat", "choose:Yes.", "choose:Be careful in there, cat!" })
                elseif hurt >= 1 and count(t, "trout") > 0 then
                    feed("feedCatAsItFights-" .. (fed + 1), hurt)
                end
                t.ticks(1)
            end
            t.expect("quest.stage.kingrat_defeated", t.quest.expect_stage("kingrat_defeated"))
            t.expect("feedCatAsItFights.won", t.msg.expect("the King Rat is dead"))
            local lowest, rounds = cat_hp_lines()
            t.check("feedCatAsItFights.margin", rounds > 0 and lowest >= 2 and count(t, "trout") > 0 and count(t, "kittenobject") == 1,
                "the cat's lowest hp " .. lowest .. "/5 over " .. rounds .. " reported round(s) (a quarter is 1.25; read from the "
                    .. "'cat health N/5' lines), " .. fed .. " trout eaten, trout " .. trout0 .. " -> " .. count(t, "trout")
                    .. ", sent back in " .. resent .. " time(s), cat still held " .. count(t, "kittenobject"))
            warehouse_ladder_down(t, "goDownToJackAfterFight")
            warehouse_out(t, "goDownToJackAfterFight")
            talk_wanderer(t, "talkToJackAfterFight", "vc_hooknosed_jack")
            t.exec("talkToJackAfterFight-dialog", t.chat.play, { "player:My cat defeated", "npc:Incredible!" })
            t.ticks(2)
            t.expect("quest.stage.jack_after_fight", t.quest.expect_stage("jack_after_fight"))
        end },

        { name = "keldagrim", run = function(t)
            -- travelToKeldagrim: out by the gate, the walk to the Grand Exchange trapdoor (reach.py
            -- 3262,3405 -> 3140,3502 REACH len=219), the trapdoor (forget_keldagrim.rs2:9-19, a
            -- p_teleport to Veldaban's room under the city).
            east_gate_out(t, "travelToKeldagrim.eastGateOut")
            t.exec("goto-travelToKeldagrim", t.player.goto_tile, 3140, 3502, 0)
            t.exec("travelToKeldagrim", t.player.click_loc, "ge_keldagrim_trapdoor", 1)
            t.exec("travelToKeldagrim-dialog", t.chat.play, { "mesbox:The trapdoor leads", "choose:Yes please.", "player:Yes please." })
            t.await({ level = function() local r, tl = t.world.tile() return r == "ok" and tl.z > 6400 end,
                note = "the trapdoor's p_teleport into Keldagrim" }, 10)
            local _, kt = t.world.tile()
            t.check("travelToKeldagrim.tile", kt.x == 2827 and kt.z == 10214 and kt.level == 0,
                "landed " .. tile_text(t) .. " (^forget_veldaban_coord 2827,10214,0)")
            -- Out of the landing room (28 tiles) by its door 2827,10218 (south edge).
            t.exec("talkToSmokinJoe.veldabanDoor", t.player.pass_door, { closed = "dwarf_keldagrim_door",
                open = "dwarf_keldagrim_door_open", at = { 2827, 10218, 0 }, near = { 2827, 10217 }, far = { 2827, 10219 } })
            -- Across the city (reach.py 2827,10220 -> 2929,10211 REACH len=135).
            t.exec("goto-talkToSmokinJoe", t.player.goto_tile, 2929, 10211, 0)
            t.exec("talkToSmokinJoe", t.player.talk_to, "vc_smokin_joe", 1)
            t.exec("talkToSmokinJoe-dialog", t.chat.play, { "player:Gertrude sent me", "npc:A cave rat problem" })
            t.ticks(2)
            t.expect("quest.stage.joe_talked", t.quest.expect_stage("joe_talked"))
            t.exec("makeWeedPot", t.player.use_item_on_item, "weeds", "pot_empty")
            t.ticks(2)
            t.check("makeWeedPot.count", count(t, "ratcatchers_weedpot") == 1 and count(t, "weeds") == 0 and count(t, "pot_empty") == 0,
                "pot of weeds " .. count(t, "ratcatchers_weedpot") .. ", weeds/pot left " .. count(t, "weeds") .. "/" .. count(t, "pot_empty"))
            t.exec("lightWeeds", t.player.use_item_on_item, "tinderbox", "ratcatchers_weedpot")
            t.ticks(2)
            t.check("lightWeeds.count", count(t, "ratcatchers_smokey_weedpot") == 1 and count(t, "ratcatchers_weedpot") == 0,
                "smouldering pot " .. count(t, "ratcatchers_smokey_weedpot"))
            local hole5, h5res = t.player.by_symbol("loc", "ratcatchers_rathole5")
            t.check("usePotOnHole.resolve", hole5 ~= nil, "by_symbol -> " .. tostring(h5res))
            t.exec("usePotOnHole", t.player.use_on, "ratcatchers_smokey_weedpot", hole5)
            t.ticks(2)
            local _, drill = t.var.server("varb1410_ratcatch_catknowsdrill")
            t.check("usePotOnHole.kept", count(t, "ratcatchers_smokey_weedpot") == 1 and drill == 1,
                "the first puff keeps the pot (" .. count(t, "ratcatchers_smokey_weedpot") .. ") and teaches the drill: varb1410 = " .. tostring(drill))
            t.exec("usePotOnHoleAgain", t.player.use_on, "ratcatchers_smokey_weedpot", hole5)
            t.ticks(2)
            t.check("usePotOnHoleAgain.used", count(t, "ratcatchers_smokey_weedpot") == 0,
                "smouldering pot " .. count(t, "ratcatchers_smokey_weedpot"))
            t.expect("quest.stage.joe_smoked", t.quest.expect_stage("joe_smoked"))
            t.exec("talkToJoeAgain", t.player.talk_to, "vc_smokin_joe", 1)
            t.exec("talkToJoeAgain-dialog", t.chat.play, { "player:My cat's cleared", "npc:Ha! Knew that cat" })
            t.ticks(2)
            t.expect("quest.stage.joe_again", t.quest.expect_stage("joe_again"))
        end },

        { name = "sarim", run = function(t)
            -- enterSarimRatPits: Falador Teleport out of Keldagrim, the walk to Port Sarim (reach.py
            -- 2965,3379 -> 3017,3231 REACH len=202), the manhole beside The Face.
            t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "enterSarimRatPits.faladorTeleport",
                runes = FALADOR_RUNES, where = "Falador" })
            t.exec("goto-enterSarimRatPits", t.player.goto_tile, 3018, 3234, 0)
            -- The manhole by its cache maplink row maplink_0_47_50_10_31_down (keyed on the player's tile
            -- 3018,3231 -> 2962,9650, the rat pits beside Felkrash; map frame 0 -> 1), then the pits
            -- passage to Felkrash (reach.py 2962,9650 -> 2978,9642 REACH len=46).
            local function enter_pits(name)
                t.exec(name, t.player.climb, { loc = "vc_manhole_open", op = 1, op_name = "Climb-down",
                    at = { 3018, 3232, 0 }, src = { 3018, 3231 }, dest = { 2962, 9650, 0 } })
                t.exec("walk-" .. name .. ".felkrash", t.player.walk_to, 2978, 9642, 60)
                t.exec(name .. ".felkrashPresent", t.npc.await_present, "vc_felkrash_the_bard", 10, 10)
            end
            enter_pits("enterSarimRatPits")
            t.exec("talkToFelkrash", t.player.talk_to, "vc_felkrash_the_bard", 1)
            t.exec("talkToFelkrash-dialog", t.chat.play, { "player:Gertrude sent me", "npc:Impressive!" })
            t.ticks(2)
            t.expect("quest.stage.felkrash_talked", t.quest.expect_stage("felkrash_talked"))
            -- leaveSarimRatPits: the pits' ladder by maplink_0_46_150_18_50_up (2962,9650 -> 3018,3233,
            -- beside the manhole and The Face), then two tiles on foot.
            t.exec("leaveSarimRatPits", t.player.climb, { loc = "vc_ladder", op = 1, op_name = "Climb-up",
                at = { 2962, 9651, 0 }, src = { 2962, 9650 }, dest = { 3018, 3233, 0 } })
            t.exec("walk-talkToTheFaceAgain", t.player.walk_to, 3019, 3234, 10)
            t.exec("talkToTheFaceAgain", t.player.talk_to, "vc_face", 1)
            t.exec("talkToTheFaceAgain-dialog", t.chat.play, {
                "player:I've spoken to Felkrash.", "npc:Felkrash always", "choose:I just don't think Felkrash was that impressive.",
                "player:I just don't think", "npc:Heh.",
            })
            t.ticks(2)
            t.expect("quest.stage.face_talked", t.quest.expect_stage("face_talked"))

            -- useCoinOnPot: Varrock Teleport, the walk to the Shantay Pass (reach.py 3212,3424 ->
            -- 3304,3120 REACH len=402), a pass, the doorway (the desert's only way in on foot), the Rug
            -- Merchant's carpet to north Pollnivneach, the walk to Ali's money pot (REACH len=70).
            t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "useCoinOnPot.varrockTeleport",
                runes = VARROCK_RUNES, where = "Varrock square" })
            t.exec("goto-useCoinOnPot.shantay", t.player.goto_tile, 3304, 3123, 0)
            local coins0, pass0 = count(t, "coins"), count(t, "shantay_pass")
            t.exec("useCoinOnPot.buyPass", t.player.talk_to, "shantay", 1)
            t.exec("useCoinOnPot.buyPass-dialog", t.chat.play, {
                "npc:Hello effendi, I am Shantay.", "npc:I see you're new.",
                "choose:I want to buy a shantay pass for 5 gold coins.", "player:I want to buy a shantay pass for",
                "mesbox:You purchase a Shantay Pass.",
            })
            t.check("useCoinOnPot.buyPass-paid", count(t, "shantay_pass") == pass0 + 1 and count(t, "coins") == coins0 - 5,
                "shantay_pass " .. pass0 .. " -> " .. count(t, "shantay_pass") .. ", coins " .. coins0 .. " -> " .. count(t, "coins") .. " (5 coins, shantay.rs2)")
            -- North of the doorway (z 3116) so the pass-check branch runs (shantay_pass.rs2:75-84).
            t.exec("walk-useCoinOnPot.toDoorway", t.player.walk_route, { { 3304, 3118 } })
            t.exec("useCoinOnPot.doorway", t.player.click_loc, "shantay_pass_henge_doorway", 1, { at = { 3302, 3116, 0 } })
            t.exec("useCoinOnPot.doorway-dialog", t.chat.play, {
                "mesbox:There is a large poster on the wall", "mesbox:The Desert is a VERY Dangerous place",
                "mesbox:That seems pretty scary!", "choose:Yeah, that poster doesn't scare me!",
                "npc:Can I see your Shantay Desert Pass", "mesbox:You hand over a Shantay Pass.", "player:Sure, here you go!",
                "npc:Here, have a disclaimer",
            })
            t.await({ level = function() local r, tl = t.world.tile() return r == "ok" and tl.z < 3116 end,
                note = "the queued Shantay gate teleport" }, 10)
            local _, st = t.world.tile()
            t.check("useCoinOnPot.doorway-landed", st.level == 0 and st.z < 3116 and count(t, "shantay_pass") == pass0,
                "after the doorway " .. tile_text(t) .. " (south of z 3116), shantay_pass " .. count(t, "shantay_pass") .. " (handed over)")
            local coins1 = count(t, "coins")
            t.exec("useCoinOnPot.rugMerchant", t.player.talk_to, "magic_carpet_seller1", 3)
            t.exec("useCoinOnPot.rugMerchant-dialog", t.chat.play, {
                "npc:*", "npc:*", "choose:I want to travel to Pollnivneach.", "player:I want to travel to Pollnivneach.",
            })
            local landed_r = t.await({
                level = function()
                    local r, tl = t.world.tile()
                    return r == "ok" and tl.x == 3349 and tl.z == 3003
                end,
                note = "carpet landed on the north Pollnivneach pad 3349,3003",
            }, 80)
            t.check("useCoinOnPot.carpet", landed_r == "ok" and count(t, "coins") == coins1 - 200,
                "carpet await " .. tostring(landed_r) .. ", rider at " .. tile_text(t) .. " (pad 3349,3003,0); coins "
                    .. coins1 .. " -> " .. count(t, "coins") .. " (fare 200, magic_carpet.rs2 ~carpet_fare)")
            t.ticks(3)
            t.exec("goto-useCoinOnPot", t.player.goto_tile, 3355, 2951, 0)
            local bowl, bres = t.player.by_symbol("loc", "feud_money_bowl")
            t.check("useCoinOnPot.resolve", bowl ~= nil, "by_symbol -> " .. tostring(bres))
            local coins2 = count(t, "coins")
            t.exec("useCoinOnPot", t.player.use_on, "coins", bowl)
            t.exec("useCoinOnPot-dialog", t.chat.play, {
                "player:I want to talk to you about animal charming.", "npc:Animal charming's", "player:What if I offered",
                "npc:Now you're speaking", "npc:A pleasure doing business",
            })
            t.ticks(2)
            t.expect("quest.stage.charm_obtained", t.quest.expect_stage("charm_obtained"))
            t.check("useCoinOnPot.paid", count(t, "coins") == coins2 - 101 and count(t, "snake_flute") == 1 and count(t, "ratcatchers_music") == 1,
                "coins " .. coins2 .. " -> " .. count(t, "coins") .. " (101, ^ratcatch_snakecharmer_coins), snake charm "
                    .. count(t, "snake_flute") .. ", music " .. count(t, "ratcatchers_music"))

            -- returnToSarim: Falador Teleport out of the desert, the walk to the manhole.
            t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "returnToSarim.faladorTeleport",
                runes = FALADOR_RUNES, where = "Falador" })
            t.exec("goto-returnToSarim", t.player.goto_tile, 3018, 3234, 0)
            t.exec("clickSnakeCharm", t.player.inv_op, "snake_flute", 1)
            t.exec("clickSnakeCharm-open", t.ui.await_open, "ratcatcher_flute", 10)
            -- playSnakeCharm (QH RatCharming): D, G, E, F#, D raised, B, C#, A, each an IF1
            -- press (op 0, the plain IF_BUTTON a real click sends).
            local tune = {
                { "rc_flute_d", 1 }, { "rc_flute_g", 2 }, { "rc_flute_e", 3 }, { "rc_flute_fsharp", 4 },
                { "rc_flute_up_octave", nil }, { "rc_flute_d", 5 }, { "rc_flute_up_octave", nil },
                { "rc_flute_b", 6 }, { "rc_flute_csharp", 7 }, { "rc_flute_a", 8 },
            }
            for i, n in ipairs(tune) do
                local _, w = t.ui.widget("ratcatcher_flute:" .. n[1])
                t.ui.invoke(w, 0)
                t.ticks(2)
                local _, len = t.var.server("varb1421_ratcatch_music_len")
                local _, hi = t.var.server("varb1413_vc_note_current_hi")
                local ok = (n[2] == nil) or (len == n[2]) or (n[2] == 8)
                t.check("playSnakeCharm-" .. i .. "-" .. n[1], ok, "music_len=" .. tostring(len) .. " hi=" .. tostring(hi))
            end
            local ar, ad = t.var.await_server("varb1404_ratcatch_var", 105, 8)
            t.expect("playSnakeCharm.await", ar, ad)
            t.expect("quest.stage.tune_played", t.quest.expect_stage("tune_played"))
            t.expect("playSnakeCharm.message", t.msg.expect("procession of rats"))
            -- enterPitsForEnd, talkToFelkrashForEnd.
            enter_pits("enterPitsForEnd")
            local snap_result, snap = t.skill.snapshot()
            t.expect("ratcatchers.snapshot", snap_result, "snapshot before hand-in")
            local pole0 = count(t, "vc_rat_pole")
            local _, qp0 = t.var.server("varp101_qp")
            t.exec("talkToFelkrashForEnd", t.player.talk_to, "vc_felkrash_the_bard", 1)
            t.exec("talkToFelkrashForEnd-dialog", t.chat.play, { "player:I've charmed the rats", "npc:Astonishing!" })
            t.ticks(2)
            -- Rewards (ratcatchers.rs2:684-689, wiki Ratcatchers): 4,500 Thieving xp, a rat pole, 2 quest points.
            t.expect("ratcatchers.thieving_xp_up", t.skill.expect_gain("thieving", 4500, snap))
            t.quest.expect_complete()
            t.check("ratcatchers.rat_pole", count(t, "vc_rat_pole") == pole0 + 1, "vc_rat_pole " .. pole0 .. " -> " .. count(t, "vc_rat_pole"))
            local _, qp1 = t.var.server("varp101_qp")
            t.check("ratcatchers.quest_points", qp0 ~= nil and qp1 == qp0 + 2,
                "varp101_qp " .. tostring(qp0) .. " -> " .. tostring(qp1) .. " (2 quest points, wiki Ratcatchers)")
            t.finish(0)
        end },
    },
}
