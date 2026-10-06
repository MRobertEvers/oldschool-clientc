-- The Slug Menace. Relay file: one leg per runner (docs/quest_authoring/relay.md).
-- Leg 1: Tiffy -> Witchaven talks -> ruin -> false wall -> imposing door -> scan.
--
-- RE-DRIVEN b69 (door rule). Nothing is goto'd into or out of a closed space:
--   * Falador -> Witchaven crosses the Taverley members' gate membergater 2935,3450
--     (cross_gate, every visit; reach.py: every walk from Falador opens it). The
--     setup no longer runs ::theslugmenace (it stood the player ON rd_bench2_half
--     2996,3373, a solid tile); a fresh fixture already has every quest var at 0.
--   * Hobb's / the mayor's room (castledoubledoorl|r 2713,3291-3292, west edge, stays
--     open 500 ticks), Lovecraft's room (slug2_village_poordoor 2730,3292, east edge),
--     Jorral's room (makinghistory_doubledoorr 2433,3347, east edge) and Bailey's room
--     (slug2_village_poordoor 2768,3276, west edge) are each entered and left by pass_door.
--   * The false wall lands at the WEST END of the sea slug tunnel (2323,5104); the door
--     is walked to (quest-helper: "Follow the path until you reach an imposing door").
--     The tunnel is left the real way: its Passage (slug2_cave_entrance 2322,5104, Enter)
--     back into the shrine room, then the ruin exit ladder (slug2_dongeon_ruin_exit).
--   * The Fishing Platform is left by Jeb's boat (slug2_holgart_jeb once track1 = 1;
--     Transcript:Jeb "Fishing Platform"), landing on Holgart's shore stand 2722,3305.
--   * Witchaven -> Falador crosses membergater 2935,3450 going out (cross_gate).
--   * Each altar is entered the runecraft way: its talisman used on the mysterious
--     ruins (runecraft.rs2 [oplocu,_rc_ruins]), the blank rune charged on the altar,
--     then the exit portal clicked. The five talismans are staged in setup.
--   * The fight is graded with a margin row (lowest hp >= a quarter of max, food left).

local function held_summary(t)
    local held = {}
    for slot = 0, 27 do
        local r, cell = t.inv.slot(slot)
        if r == "ok" and cell.name ~= "" and cell.count ~= 0 then
            held[#held + 1] = cell.name .. "x" .. cell.count
        end
    end
    return #held > 0 and table.concat(held, ",") or "empty"
end

-- Falador side -> inside Taverley: open ground east of membergater, then the press
-- (gates.rs2 [label,member_fencegate_try]; Taverley is x <= 2935).
local function taverley_gate_in(t, name)
    t.exec("goto-" .. name, t.player.goto_tile, 2938, 3450, 0)
    t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
        near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
        far_desc = "inside Taverley, x <= 2935" })
end

-- Hobb's room (also the mayor's study): castledoubledoorl|r 2713,3291-3292, wall on the
-- west edge of x 2713; the room is x 2704-2712. A double door stands open 500 ticks.
local function hobb_door(t, name, going_in)
    t.exec(name, t.player.pass_door, { closed = "castledoubledoorl", open = "opencastledoubledoorl",
        at = { 2713, 3291, 0 }, near = going_in and { 2713, 3291 } or { 2712, 3291 },
        far = going_in and { 2712, 3291 } or { 2713, 3291 } })
end

-- Lovecraft's room: slug2_village_poordoor 2730,3292 (selfstage; wall on the east edge of
-- x 2730, the room is x 2731-2736).
local function lovecraft_door(t, name, going_in)
    t.exec(name, t.player.pass_door, { closed = "slug2_village_poordoor", open = "slug2_village_poordoor",
        at = { 2730, 3292, 0 }, near = going_in and { 2730, 3292 } or { 2731, 3292 },
        far = going_in and { 2731, 3292 } or { 2730, 3292 } })
end

-- Jorral's room: makinghistory_doubledoorr 2433,3347 (wall on the east edge of x 2433).
local function jorral_door(t, name, going_in)
    t.exec(name, t.player.pass_door, { closed = "makinghistory_doubledoorr", open = "makinghistory_doubledoorr_open",
        at = { 2433, 3347, 0 }, near = going_in and { 2433, 3347 } or { 2434, 3347 },
        far = going_in and { 2434, 3347 } or { 2433, 3347 } })
end

-- Bailey's room on the Fishing Platform: slug2_village_poordoor 2768,3276 (wall on the
-- west edge of x 2768; the room is x 2763-2767).
local function bailey_door(t, name, going_in)
    t.exec(name, t.player.pass_door, { closed = "slug2_village_poordoor", open = "slug2_village_poordoor",
        at = { 2768, 3276, 0 }, loc_level = 1, near = going_in and { 2768, 3276 } or { 2767, 3276 },
        far = going_in and { 2767, 3276 } or { 2768, 3276 } })
end

-- Inside Taverley -> Falador side: open ground west of membergater, then the press.
local function taverley_gate_out(t, name)
    t.exec("goto-" .. name, t.player.goto_tile, 2934, 3450, 0)
    t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
        near = { 2935, 3450 }, far_ok = function(tile) return tile.x >= 2936 end,
        far_desc = "outside Taverley, x >= 2936" })
end

-- Out of the sea slug tunnel the real way: walk back to its Passage (slug2_cave_entrance
-- 2322,5104, op1=Enter) -> the shrine room side of the false wall (2700,9688), then the
-- ruin exit ladder (slug2_dongeon_ruin_exit 2696,9682) up to 2696,3282, the open tile south
-- of the Old ruin entrance west of Witchaven.
local function leave_tunnel(t, name)
    -- Walk the tunnel back to its west end (reach.py: REACH closed-doors len=159).
    t.exec("goto-" .. name .. ".passage", t.player.goto_tile, 2323, 5104, 0)
    -- click_loc's pose hunt answers "screen_position: framed nothing in 5 poses" for the
    -- Passage (a wall on the east edge of 2322,5104, model 18376, seen edge-on from the
    -- tunnel). As eadgar.lua's secret door: drive.op sends the op with no pixel and no
    -- route; the server still validates and runs the real [oploc1,slug2_cave_entrance].
    local passage = t.player.by_symbol("loc", "slug2_cave_entrance")
    local op_result, op_detail = t.drive.op(passage, 1)
    t.check(name .. ".passage", op_result == "ok", "drive.op(slug2_cave_entrance, 1) -> " .. tostring(op_result) .. " " .. tostring(op_detail))
    local pr, pd = t.await({
        level = function()
            local r, tt = t.world.tile()
            return r == "ok" and tt.x > 2600 and tt.z > 9000
        end,
        note = "back through the passage",
    }, 20)
    t.ticks(2)
    local _, shrine = t.world.tile()
    t.check(name .. ".shrine", pr == "ok" and shrine ~= nil and shrine.x == 2700 and shrine.z == 9688,
        "after the Passage -> " .. tostring(shrine and (shrine.x .. "," .. shrine.z .. "," .. shrine.level)) .. " (" .. tostring(pr) .. " " .. tostring(pd) .. ")")
    t.exec(name .. ".ladder", t.player.click_loc, "slug2_dongeon_ruin_exit", 1)
    local lr, ld = t.await({
        level = function()
            local r, tt = t.world.tile()
            return r == "ok" and tt.z < 4000
        end,
        note = "up the ruin exit ladder",
    }, 30)
    t.ticks(2)
    local _, up = t.world.tile()
    t.check(name .. ".surface", lr == "ok" and up ~= nil and up.x == 2696 and up.z == 3282 and up.level == 0,
        "after the ladder -> " .. tostring(up and (up.x .. "," .. up.z .. "," .. up.level)) .. " (" .. tostring(lr) .. " " .. tostring(ld) .. ")")
end

return {
    id = "theslugmenace",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::slugprep", -- guide requirement: Wanted!, Sea Slug and Recruitment Drive complete
        "::setlevel crafting 30", -- guide requirement: 30 Crafting
        "::setlevel runecraft 30", -- guide requirement: 30 Runecrafting
        "::setlevel slayer 30", -- guide requirement: 30 Slayer
        "::setlevel thieving 30", -- guide requirement: 30 Thieving
        "::give wanted_crystal_ball 1", -- guide item: Commorb (Sir Tiffy upgrades it to v2)
        "::give swamppaste 1", -- guide item for leg 2's useSwampPasteOnFragments (Quest Helper lists Swamp paste)
        "::give chisel 1", -- guide item for leg 3's useEmptyRunes (Chisel)
        "::give blankrune_high 9", -- guide item for leg 3's useEmptyRunes (Rune or Pure Essence, extra: shaping can shatter it)
        "::give air_talisman 1", -- the five talismans open the mysterious ruins to the altars the runes are charged at
        "::give mind_talisman 1",
        "::give water_talisman 1",
        "::give earth_talisman 1",
        "::give fire_talisman 1",
        "::setlevel attack 70", "::setlevel strength 70", "::setlevel defence 60", "::setlevel hitpoints 70", -- guide: melee weapon to fight the Slug Prince (level 62)
    },
    bind = {
        varp = "varb2610_slug2_main",
        constants = {
            not_started = 0, told_by_tiffy = 1, spoke_niall1 = 2, talked_one = 3,
            talked_two = 4, talked_three = 5, told_dungeon = 6, got_transcript = 7,
            maledict_flipped = 8, have_two_pages = 9, pages_torn = 10, fixed_page = 11, runes_used = 12, prince_dead = 13,
            complete = 14,
        },
        display = "The Slug Menace",
        points = 1,
    },
    legs = {
        { name = "witchaven", run = function(t)
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- Sir Tiffy stands on 2996,3373 (a bench); 2997,3372 is open floor beside him.
            t.exec("goto-talkToTiffy", t.player.goto_tile, 2997, 3372, 0)
            t.exec("talkToTiffy", t.player.talk_to, "rd_teleporter_guy", 1)
            t.exec("talkToTiffy-dialog", t.chat.play, {
                "player:Do you have any other jobs",
                "npc:As it happens, yes",
                "choose:Tell me more.",
                "player:Tell me more.",
                "npc:Reports of strange behaviour",
                "npc:I'd like you to go and investigate",
                "choose:I'll get right on it.",
                "player:I'll get right on it.",
                "npc:First things first",
                "npc:Go to Witchaven",
            })
            t.ticks(2)
            t.expect("quest.stage.told_by_tiffy", t.quest.expect_stage("told_by_tiffy"))
            t.expect("talkToTiffy.commorb_v2", t.inv.expect_has("slug2_crystal_ball", 1))

            -- Witchaven is behind the members' wall: reach.py from Falador opens membergater 2935,3450.
            taverley_gate_in(t, "talkToNiall.memberGate")
            t.exec("goto-talkToNiall", t.player.goto_tile, 2739, 3309, 0)
            t.exec("talkToNiall", t.player.talk_to, "slug2_oniall", 1)
            t.exec("talkToNiall-dialog", t.chat.play, {
                "player:Who are the important people",
                "npc:You'll want to speak with Brother Maledict",
                "player:Nothing at the moment thanks.",
            })
            t.ticks(2)
            t.expect("quest.stage.spoke_niall1", t.quest.expect_stage("spoke_niall1"))

            t.exec("goto-talkToMaledict", t.player.goto_tile, 2724, 3284, 0)
            t.exec("talkToMaledict", t.player.talk_to, "slug2_maledict", 1)
            t.exec("talkToMaledict-dialog", t.chat.play, {
                "player:Sir Tiffy sent me",
                "npc:Questions?",
                "player:There've been reports",
                "npc:I'm sure I don't know",
                "player:That's enough for now.",
            })
            t.ticks(2)
            t.expect("quest.stage.talked_one", t.quest.expect_stage("talked_one"))

            -- Hobb is inside the mayor's room: in by its double door, out by it.
            t.exec("goto-talkToHobb.door", t.player.goto_tile, 2715, 3291, 0)
            hobb_door(t, "talkToHobb.doorIn", true)
            t.exec("talkToHobb", t.player.talk_to, "slug2_hobb", 1)
            t.exec("talkToHobb-dialog", t.chat.play, {
                "player:I'm just looking around.",
                "npc:Well, look around somewhere else",
                "player:Nothing at the moment thanks.",
            })
            t.ticks(2)
            t.expect("quest.stage.talked_two", t.quest.expect_stage("talked_two"))
            hobb_door(t, "talkToHobb.doorOut", false)

            t.exec("goto-talkToHolgart", t.player.goto_tile, 2721, 3303, 0)
            t.exec("talkToHolgart", t.player.talk_to, "holgartlandtravel", 1)
            t.exec("talkToHolgart-dialog", t.chat.play, {
                "player:Sir Tiffy sent me",
                "npc:Strange?",
                "player:Nothing at the moment thanks.",
            })
            t.ticks(2)
            t.expect("quest.stage.talked_three", t.quest.expect_stage("talked_three"))

            t.exec("goto-talkToNiall2", t.player.goto_tile, 2739, 3309, 0)
            t.exec("talkToNiall2", t.player.talk_to, "slug2_oniall", 1)
            t.exec("talkToNiall2-dialog", t.chat.play, {
                "player:I've spoken to Maledict, Hobb and Holgart",
                "npc:All three, eh?",
            })
            t.ticks(2)
            t.expect("quest.stage.told_dungeon", t.quest.expect_stage("told_dungeon"))

            t.exec("goto-enterDungeon", t.player.goto_tile, 2696, 3285, 0)
            t.exec("enterDungeon", t.player.click_loc, "slug2_ruin_entrance", 1)
            t.ticks(3)
            local _, tile = t.world.tile()
            t.check("enterDungeon.below", tile ~= nil and tile.z > 9000, "arrived " .. tostring(tile and (tile.x .. "," .. tile.z .. "," .. tile.level)))

            t.exec("pushFalseWall", t.player.click_loc, "slug2_hidden_entrance", 1)
            t.ticks(2)
            t.exec("enterWall", t.player.click_loc, "slug2_hidden_entrance", 1)
            t.ticks(3)
            local _, tile2 = t.world.tile()
            t.check("enterWall.arrived", tile2 ~= nil and tile2.x == 2323 and tile2.z == 5104, "arrived " .. tostring(tile2 and (tile2.x .. "," .. tile2.z .. "," .. tile2.level)) .. " (want the tunnel's west end 2323,5104)")

            -- Follow the path to the imposing door (reach.py: REACH closed-doors len=159).
            t.exec("goto-tryToOpenImposingDoor", t.player.goto_tile, 2350, 5094, 0)
            t.exec("tryToOpenImposingDoor", t.player.click_loc, "slug2_cave_doors_closed", 1)
            t.ticks(2)

            local door = t.player.by_symbol("loc", "slug2_cave_doors_closed")
            t.exec("scanWithComm", t.player.use_on, "slug2_crystal_ball", door)
            t.ticks(2)
            t.expect("quest.stage.got_transcript", t.quest.expect_stage("got_transcript"))
            t.expect("scanWithComm.transcript", t.inv.expect_has("slug2_transcript", 1))

            local _, end_tile = t.world.tile()
            local _, stage = t.var.server("varb2610_slug2_main")
            t.check("leg.1.state", true, "tile=" .. tostring(end_tile and (end_tile.x .. "," .. end_tile.z .. "," .. end_tile.level))
                .. " stage=" .. tostring(stage) .. " inv=" .. held_summary(t))
        end },
        { name = "pages", run = function(t)
            -- LEG 2: dead slug -> Jorral -> Niall -> Maledict x2 -> pages -> fragments.
            -- The slug is a floor obj laid by the false wall; a checkpoint resume does not restore it, so leg 2 is iterated by full runs.
            t.exec("pickUpDeadSlug.wait", t.world.obj_near, "slug2_seaslug_young", 8)
            t.exec("pickUpDeadSlug", t.player.click_obj, "slug2_seaslug_young")
            t.ticks(2)
            t.expect("pickUpDeadSlug.held", t.inv.expect_has("slug2_seaslug_young", 1))

            -- Out of the tunnel the real way, then overland west to Jorral (reach.py: REACH len=348).
            leave_tunnel(t, "leaveCave")
            t.exec("goto-talkToJorral", t.player.goto_tile, 2431, 3347, 0)
            jorral_door(t, "talkToJorral.doorIn", true)
            t.exec("talkToJorral", t.player.talk_to, "makinghistory_jorral", 1)
            t.exec("talkToJorral-dialog", t.chat.play, {
                "player:Translations",
                "player:Could you translate this transcript",
                "npc:This is an old religious text",
                "npc:You'd best take this news back",
            })
            t.ticks(2)
            t.expect("talkToJorral.transcript_gone", t.inv.expect_absent("slug2_transcript"))
            t.expect("quest.stage.got_transcript", t.quest.expect_stage("got_transcript"))
            jorral_door(t, "talkToJorral.doorOut", false)

            t.exec("goto-talkToNiall3", t.player.goto_tile, 2739, 3309, 0)
            t.exec("talkToNiall3", t.player.talk_to, "slug2_oniall", 1)
            t.exec("talkToNiall3-dialog", t.chat.play, {
                "player:I found this transcript",
                "npc:An imposing door?",
            })
            t.ticks(2)

            t.exec("goto-talkToMaledict2", t.player.goto_tile, 2724, 3284, 0)
            t.exec("talkToMaledict2", t.player.talk_to, "slug2_maledict", 1)
            t.exec("talkToMaledict2-dialog", t.chat.play, {
                "player:I found a strange transcript",
                "npc:An imposing door, you say?",
                "npc:Come back and see me shortly",
            })
            t.ticks(2)
            t.expect("quest.stage.maledict_flipped", t.quest.expect_stage("maledict_flipped"))

            t.exec("talkToMaledict3", t.player.talk_to, "slug2_maledict", 1)
            t.exec("talkToMaledict3-dialog", t.chat.play, {
                "npc:Very well",
                "npc:Between the two of them",
            })
            t.ticks(2)

            t.exec("goto-searchMayorsDesk.door", t.player.goto_tile, 2715, 3291, 0)
            hobb_door(t, "searchMayorsDesk.doorIn", true)
            t.exec("searchMayorsDesk", t.player.click_loc, "slug2_mayors_desk", 1)
            t.ticks(3)
            t.expect("searchMayorsDesk.page1", t.inv.expect_has("slug2_page1", 1))
            hobb_door(t, "searchMayorsDesk.doorOut", false)

            t.exec("goto-talkToLovecraft.door", t.player.goto_tile, 2728, 3292, 0)
            lovecraft_door(t, "talkToLovecraft.doorIn", true)
            t.exec("talkToLovecraft", t.player.talk_to, "slug2_lovecraft", 1)
            t.exec("talkToLovecraft-dialog", t.chat.play, {
                "player:Brother Maledict said",
                "npc:...I was wondering when",
            })
            t.ticks(2)
            t.expect("talkToLovecraft.page2", t.inv.expect_has("slug2_page2", 1))
            t.expect("quest.stage.have_two_pages", t.quest.expect_stage("have_two_pages"))
            lovecraft_door(t, "talkToLovecraft.doorOut", false)

            t.exec("goto-talkToNiall4", t.player.goto_tile, 2739, 3309, 0)
            t.exec("talkToNiall4", t.player.talk_to, "slug2_oniall", 1)
            t.exec("talkToNiall4-dialog", t.chat.play, {
                "player:I need the third page",
                "npc:Blast it",
            })
            t.ticks(2)
            t.expect("talkToNiall4.fragment_a", t.inv.expect_has("slug2_page4a", 1))
            t.expect("talkToNiall4.fragment_b", t.inv.expect_has("slug2_page4b", 1))
            t.expect("talkToNiall4.fragment_c", t.inv.expect_has("slug2_page4c", 1))
            t.expect("quest.stage.pages_torn", t.quest.expect_stage("pages_torn"))

            -- NOTE: Quest Helper's useSwampPasteOnFragments is out of date; the port needs sea slug glue from Bailey, slugmenace_pages.rs2:111-113 and :134-137 (swamp paste is refused with the default message). Driven here: the paste is tried and refused; glue is leg 3.
            local paste_result, paste_detail = t.player.use_item_on_item("swamppaste", "slug2_page4a")
            t.check("useSwampPasteOnFragments", string.find(tostring(paste_detail), "Nothing interesting happens", 1, true) ~= nil,
                "result=" .. tostring(paste_result) .. " " .. tostring(paste_detail))
            t.ticks(2)
            t.expect("useSwampPasteOnFragments.refused", t.inv.expect_has("slug2_page4a", 1))
            local drop_result = t.player.drop("swamppaste")
            t.check("dropSwampPaste", drop_result == "ok", "drop swamppaste -> " .. tostring(drop_result) .. " (the paste is refused and useless; backpack room)")

            local _, end_tile = t.world.tile()
            local _, stage = t.var.server("varb2610_slug2_main")
            t.check("leg.2.end", true, "tile=" .. tostring(end_tile and (end_tile.x .. "," .. end_tile.z .. "," .. end_tile.level))
                .. " stage=" .. tostring(stage) .. " inv=" .. held_summary(t))
        end },
        { name = "prince", run = function(t)
            -- LEG 3: Jeb -> Bailey (glue) -> puzzle -> runes -> dungeon -> Slug Prince -> Tiffy.
            t.exec("goto-talkToJeb", t.player.goto_tile, 2721, 3304, 0)
            t.exec("talkToJeb", t.player.talk_to, "slug2_jeb", 1)
            t.exec("talkToJeb-dialog", t.chat.play, {
                "player:I understand you can take me to the Fishing Platform",
                "npc:Yes, we can do that",
                "player:Will you take me please",
                "npc:Board the boat and we shall depart",
            })
            -- ~slugmenace_jeb_witchaven_to_platform closes, delays, then opens
            -- the arrival mesbox ("You arrive at the fishing platform.").
            t.ticks(3)
            t.chat.drain({ stop_at = "none" })
            local _, jeb_tile = t.world.tile()
            t.check("talkToJeb.platform", jeb_tile ~= nil and jeb_tile.x >= 2760,
                "tile=" .. tostring(jeb_tile and (jeb_tile.x .. "," .. jeb_tile.z)))

            -- Bailey is in the room behind slug2_village_poordoor 2768,3276: in by the door, never a goto.
            bailey_door(t, "talkToBailey.doorIn", true)
            t.exec("talkToBailey", t.player.talk_to, "bailey", 1)
            t.exec("talkToBailey-dialog", t.chat.play, {
                "player:Sir Tiffy sent me",
                "player:I've got a dead sea slug here",
                "npc:Ha! Hand it over",
            })
            t.ticks(2)
            t.expect("talkToBailey.glue", t.inv.expect_has("slug2_slug_paste", 1))
            t.expect("talkToBailey.slug_gone", t.inv.expect_absent("slug2_seaslug_young"))

            -- useGlueOnFragment opens the puzzle (slugmenace_pages.rs2 label slugmenace_combine_fragments).
            -- the verb reads a pure interface open as "nothing happened", so grade the open itself.
            t.player.use_item_on_item("slug2_slug_paste", "slug2_page4a")
            local open_result, open_detail = t.ui.await_open("slug2_ressembling_torn_paper", 10)
            t.check("useGlueOnFragment", open_result == "ok", tostring(open_result) .. " " .. tostring(open_detail))
            t.ticks(2)

            -- solvePuzzle: select a fragment, flip/turn/slide it (interface 462 buttons) until it matches
            -- fragment 1 (slugmenace_puzzle.rs2 slugmenace_puzzle_solved).
            local control = "slug2_ressembling_torn_paper_controll:"
            local function read(name)
                local _, value = t.var.server(name)
                return value
            end
            local function solved()
                return read("varb2611_slug2_fixed_page") == 1
            end
            local function find_widget(name, sub)
                for _ = 1, 15 do
                    local result, widget = t.ui.widget(name, sub)
                    if result == "ok" and widget then return widget end
                    t.ticks(1)
                end
                t.blocked("seam: widget " .. name .. " (sub " .. tostring(sub) .. ") never mounted on the fragment puzzle")
            end
            local function press(button)
                t.ui.invoke(find_widget(control .. button), 1)
                t.ticks(1)
            end
            local function toggle_select(piece)
                t.ui.invoke(find_widget(control .. "select" .. piece, 2), 1)
                t.ticks(1)
            end
            local function fragment(piece)
                local base = ({ [1] = 876, [2] = 880, [3] = 884 })[piece]
                local names = ({ [1] = "frag1", [2] = "frag2", [3] = "frag3" })[piece]
                return {
                    x = read("varp" .. base .. "_slug2_" .. names .. "_xpos"),
                    y = read("varp" .. (base + 1) .. "_slug2_" .. names .. "_ypos"),
                    flip = read("varp" .. (base + 2) .. "_slug2_" .. names .. "_zpos"),
                    rot = read("varp" .. (base + 3) .. "_slug2_" .. names .. "_rot"),
                }
            end
            local presses = 0
            for piece = 1, 3 do
                toggle_select(piece)
                local f = fragment(piece)
                if f.flip ~= 1 then press("slug2_flip_button"); presses = presses + 1 end
                for _ = 1, 4 do
                    f = fragment(piece)
                    if f.rot == 1 then break end
                    press("slug2_rotate_button"); presses = presses + 1
                end
                if piece > 1 then
                    local anchor = fragment(1)
                    for _ = 1, 30 do
                        if solved() then break end
                        f = fragment(piece)
                        if f.x > anchor.x then press("slug2_move_left")
                        elseif f.x < anchor.x then press("slug2_move_right")
                        elseif f.y > anchor.y then press("slug2_move_up")
                        elseif f.y < anchor.y then press("slug2_move_down")
                        else break end
                        presses = presses + 1
                    end
                end
                -- a move inside the tolerance solves the puzzle and closes it, so deselect only while it is open.
                if not solved() then toggle_select(piece) end
            end
            t.ticks(3)
            t.expect("solvePuzzle", t.var.expect("varb2611_slug2_fixed_page", 1))
            t.expect("solvePuzzle.page3", t.inv.expect_has("slug2_page3", 1))
            t.expect("quest.stage.fixed_page", t.quest.expect_stage("fixed_page"))
            t.chat.continue_()
            t.check("solvePuzzle.presses", true, presses .. " button presses")

            -- useEmptyRunes: chisel + essence on the page ops make blank runes, each blank is charged at its altar.
            local shapes = {
                { name = "earth", page = "slug2_page1", op = 2, blank = "slug2_rune_earth_blank", rune = "slug2_rune_earth", altar = "earth_altar", x = 2658, z = 4841 },
                { name = "air", page = "slug2_page1", op = 3, blank = "slug2_rune_air_blank", rune = "slug2_rune_air", altar = "air_altar", x = 2844, z = 4834 },
                { name = "fire", page = "slug2_page2", op = 2, blank = "slug2_rune_fire_blank", rune = "slug2_rune_fire", altar = "fire_altar", x = 2585, z = 4838 },
                { name = "water", page = "slug2_page2", op = 3, blank = "slug2_rune_water_blank", rune = "slug2_rune_water", altar = "water_altar", x = 2716, z = 4836 },
                { name = "mind", page = "slug2_page3", op = 2, blank = "slug2_rune_mind_blank", rune = "slug2_rune_mind", altar = "mind_altar", x = 2786, z = 4841 },
            }
            for _, shape in ipairs(shapes) do
                local attempts = 0
                for _ = 1, 12 do
                    local _, held = t.inv.count(shape.blank)
                    if held and held > 0 then break end
                    t.player.inv_op(shape.page, shape.op)
                    attempts = attempts + 1
                    t.ticks(2)
                end
                t.expect("useEmptyRunes.shape-" .. shape.name, t.inv.expect_has(shape.blank, 1))
                t.check("useEmptyRunes.shape-" .. shape.name .. ".tries", true, attempts .. " chisel attempt(s)")
            end
            -- Back to Witchaven in Jeb's boat (he has Holgart's; Transcript:Jeb "Fishing Platform").
            bailey_door(t, "leavePlatform.doorOut", false)
            t.exec("leavePlatform", t.player.talk_to, "slug2_holgart_jeb", 1)
            t.exec("leavePlatform-dialog", t.chat.play, {
                "player:Hey, Jeb.",
                "npc:Business here is complete",
                "choose:Okay, let's go back.",
                "player:Okay, let's go back.",
                "npc:Then board the rowing boat.",
            })
            local br, bd = t.await({
                level = function()
                    local r, tt = t.world.tile()
                    return r == "ok" and tt.x < 2750
                end,
                note = "Jeb's boat to Witchaven",
            }, 20)
            t.ticks(2)
            local _, shore = t.world.tile()
            t.check("leavePlatform.shore", br == "ok" and shore ~= nil and shore.x == 2722 and shore.z == 3305 and shore.level == 0,
                "after Jeb's boat -> " .. tostring(shore and (shore.x .. "," .. shore.z .. "," .. shore.level)) .. " (" .. tostring(br) .. " " .. tostring(bd) .. ")")
            t.chat.continue_()
            taverley_gate_out(t, "leavePlatform.memberGate")

            -- Each blank is charged at its own altar, entered the runecraft way: the talisman used on the
            -- mysterious ruins (runecraft.rs2 [oplocu,_rc_ruins]), the exit portal clicked to come back out.
            -- near = the tile the portal sets you on (skill_runecraft runecraft.dbrow exit_coord).
            local altar_trip = {
                air = { ruin = "airtemple_ruined", talisman = "air_talisman", portal = "airtemple_exit_portal", x = 2983, z = 3288 },
                water = { ruin = "watertemple_ruined", talisman = "water_talisman", portal = "watertemple_exit_portal", x = 3182, z = 3162 },
                fire = { ruin = "firetemple_ruined", talisman = "fire_talisman", portal = "firetemple_exit_portal", x = 3310, z = 3252 },
                earth = { ruin = "earthtemple_ruined", talisman = "earth_talisman", portal = "earthtemple_exit_portal", x = 3302, z = 3477 },
                mind = { ruin = "mindtemple_ruined", talisman = "mind_talisman", portal = "mindtemple_exit_portal", x = 2980, z = 3511 },
            }
            local by_name = {}
            for _, shape in ipairs(shapes) do by_name[shape.name] = shape end
            for _, name in ipairs({ "air", "water", "fire", "earth", "mind" }) do
                local shape, trip = by_name[name], altar_trip[name]
                t.exec("goto-useEmptyRunes-" .. name, t.player.goto_tile, trip.x, trip.z, 0)
                local ruin = t.player.by_symbol("loc", trip.ruin)
                t.exec("enterAltar-" .. name, t.player.use_on, trip.talisman, ruin)
                local ar, ad = t.await({
                    level = function()
                        local r, tt = t.world.tile()
                        return r == "ok" and tt.z > 4000 and tt.z < 5000
                    end,
                    note = name .. " altar room entered",
                }, 20)
                t.ticks(3)
                local er, et = t.world.tile()
                t.check("enterAltar-" .. name .. ".inside", ar == "ok" and er == "ok" and et.z > 4000 and et.z < 5000,
                    "after the talisman on the ruins -> " .. tostring(et and (et.x .. "," .. et.z .. "," .. et.level))
                        .. " (" .. tostring(ar) .. " " .. tostring(ad) .. ")")
                local altar = t.player.by_symbol("loc", shape.altar)
                t.exec("useEmptyRunes-" .. name, t.player.use_on, shape.blank, altar)
                t.ticks(2)
                t.expect("useEmptyRunes-" .. name .. ".charged", t.inv.expect_has(shape.rune, 1))
                local dr = t.player.drop(trip.talisman)
                t.check("dropTalisman-" .. name, dr == "ok", "drop " .. trip.talisman .. " -> " .. tostring(dr) .. " (backpack room for the Slug Prince gear)")
                t.exec("exitAltar-" .. name, t.player.click_loc, trip.portal, 1)
                local xr, xd = t.await({
                    level = function()
                        local r, tt = t.world.tile()
                        return r == "ok" and tt.z < 4000
                    end,
                    note = name .. " portal exit",
                }, 20)
                t.ticks(2)
                local xr2, xt = t.world.tile()
                t.check("exitAltar-" .. name .. ".outside", xr == "ok" and xr2 == "ok" and xt.z < 4000,
                    "after the exit portal -> " .. tostring(xt and (xt.x .. "," .. xt.z .. "," .. xt.level)) .. " (" .. tostring(xr) .. " " .. tostring(xd) .. ")")
            end
            t.player.drop("chisel")

            -- Gear for the Slug Prince (guide: melee weapon; only melee can hurt it), given here so the
            -- earlier legs' backpack stays small.
            t.cheat("::give rune_scimitar 1")
            t.cheat("::give shark 10")
            t.ticks(2)
            t.exec("equip-scimitar", t.player.equip, "rune_scimitar")

            -- Back west through the members' wall (the mind ruin is north of Falador, beside the north gate).
            t.exec("goto-enterDungeonAgain.gate", t.player.goto_tile, 2938, 3450, 0)
            t.exec("enterDungeonAgain.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
                far_desc = "inside Taverley, x <= 2935" })
            t.exec("goto-enterDungeonAgain", t.player.goto_tile, 2697, 3283, 0)
            t.exec("enterDungeonAgain", t.player.click_loc, "slug2_ruin_entrance", 1)
            t.ticks(3)
            local _, below = t.world.tile()
            t.check("enterDungeonAgain.below", below ~= nil and below.z > 9000,
                "tile=" .. tostring(below and (below.x .. "," .. below.z .. "," .. below.level)))

            t.exec("enterWallAgain", t.player.click_loc, "slug2_hidden_entrance", 1)
            t.ticks(4)
            local _, cave = t.world.tile()
            t.check("enterWallAgain.cave", cave ~= nil and cave.x == 2323 and cave.z == 5104,
                "tile=" .. tostring(cave and (cave.x .. "," .. cave.z .. "," .. cave.level)) .. " (want the tunnel's west end 2323,5104)")
            t.exec("goto-useEmptyRunesOnDoor", t.player.goto_tile, 2350, 5094, 0)

            for _, rune in ipairs({ "slug2_rune_air", "slug2_rune_water", "slug2_rune_earth", "slug2_rune_fire", "slug2_rune_mind" }) do
                local door = t.player.by_symbol("loc", "slug2_cave_doors_closed")
                t.exec("useEmptyRunesOnDoor-" .. rune, t.player.use_on, rune, door)
                t.ticks(2)
                t.expect("useEmptyRunesOnDoor-" .. rune .. ".used", t.inv.expect_absent(rune))
            end
            t.expect("quest.stage.runes_used", t.quest.expect_stage("runes_used"))

            t.exec("openDoor", t.player.click_loc, "slug2_cave_doors_closed", 1)
            t.ticks(3)
            t.exec("killSlugPrince.present", t.npc.await_present, "slug2_the_slug_prince", 12, 10)
            t.exec("killSlugPrince", t.player.attack, "slug2_the_slug_prince", 2, 20)
            local _, shark_before = t.inv.count("shark")
            local _, kill_detail = t.exec("killSlugPrince.dead", t.npc.await_dead_engaged, 600, 40, { eat = { item = "shark", below = 35 } })
            t.ticks(10)
            -- Margin row: lowest hp at least a quarter of max AND food left.
            do
                local lowest = tonumber(tostring(kill_detail):match("lowest hp (%d+)/"))
                local _, hitpoints = t.skill.read("hitpoints")
                local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
                local food_result, food_left = t.inv.count("shark")
                t.check("killSlugPrince.margin", lowest ~= nil and max_hp ~= nil and food_result == "ok"
                    and lowest * 4 >= max_hp and food_left >= 1,
                    "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", sharks " .. tostring(shark_before)
                        .. " -> " .. tostring(food_left) .. " (margin: lowest hp >= a quarter of max AND at least one shark left)")
            end
            t.expect("quest.stage.prince_dead", t.quest.expect_stage("prince_dead"))

            -- reportBackToTiffy
            -- Out of the tunnel the real way, then east through the members' gate to Sir Tiffy.
            leave_tunnel(t, "leaveCaveAgain")
            taverley_gate_out(t, "reportBackToTiffy.memberGate")
            t.exec("goto-reportBackToTiffy", t.player.goto_tile, 2997, 3372, 0)
            local _, before = t.skill.snapshot()
            t.exec("reportBackToTiffy", t.player.talk_to, "rd_teleporter_guy", 1)
            t.exec("reportBackToTiffy-dialog", t.chat.play, {
                "player:Slug Menace",
                "player:It's done, Sir Tiffy",
                "npc:Excellent work!",
            })
            t.ticks(3)
            -- Quest Helper rewards (TheSlugMenace.java getExperienceRewards): 3500 Crafting, Runecraft and Thieving.
            t.expect("reward.crafting", t.skill.expect_gain("crafting", 3500, before))
            t.expect("reward.runecraft", t.skill.expect_gain("runecraft", 3500, before))
            t.expect("reward.thieving", t.skill.expect_gain("thieving", 3500, before))
            t.quest.expect_complete()
            t.finish(0)
        end },
    },
}
