-- Darkness of Hallowvale -- driven as a 6-leg relay (docs/quest_authoring/relay.md).
-- Leg 1 = guide steps 1-11 (climbOverBrokenWall .. talkToCitizen).
-- Notes: docs/quests/ladders/darknessofhallowvale.notes.md.
--
-- RE-DRIVEN b72 for the door rule (BRIEF: no goto into or out of a closed space, no goto with no
-- on-foot route, no goto onto a solid tile). Travel, every hop checked with sample_tools/reach.py
-- (doors shut) and a maplink-aware flood of Meiyerditch:
--   * Into Morytania (the fixture stands in Lumbridge): the Varrock members' gate 3319,3468, the
--     Paterdomus trapdoor 3405,3507, the cellar's two gates, Drezel's Priest in Peril advice
--     (mausoleum_drezel.rs2:131-142, needs dagger_wolfbane held) and the holy barrier
--     (mausoleum_interactions.rs2:26, p_telejump 3423,3485). Same route as mortton.lua and
--     animalmagnetism.lua.
--   * 3423,3485 -> 3510,3470 (REACH 110) -> 3485,3282 (REACH 779) -> 3485,3244 (REACH 44), then
--     Burgh de Rott's fence gate burgh_fencegate_l 3485,3244 (the village is fenced: 3491,3229 is
--     inside it) and on foot to the inn.
--   * Meiyerditch is three walled pieces joined only by the quest's own moves: the south streets
--     (the rubble), the walls (barricade, ladders) and the north (lane, hideout, lab). The north is
--     reached by the sickle-logo course or by the Vyrewatch's Daeyalt mine (doh_urgent.rs2:162-176,
--     "the way back into the city for a player who sailed in"; the guard's p_telejump puts you in
--     the hideout's secret room 3638,3251, doh_urgent.rs2:281). Nothing walks out of the north, so
--     the way out is a Varrock Teleport (runes staged in setup) and the long way round again.
--   * Varrock Palace: the courtyard 3212,3466 is open street; the throne room is closed by
--     fai_varrock_castle_door 3218,3472 (blackarmgang.lua). Aeonisig's free teleport
--     (doh_urgent.rs2:117-133, at stage 170) lands in Drezel's cellar 3439,9896.
-- Readings for row details (file-level helpers, no state shared between legs).
local function at(t)
    local _, p = t.world.tile()
    return tostring(p.x) .. "," .. tostring(p.z) .. "," .. tostring(p.level)
end
local function tile(t)
    local _, p = t.world.tile()
    return p
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

-- A Meiyerditch door: pressed when its closed leaf is there; a leaf still standing open from an
-- earlier press has no closed copy to click and is walked through as it stands.
local function door_click(t, name, sym, x, y)
    local ok, why = t.player.click_loc(sym, 1, { at = { x, y } })
    local standing = ok ~= "ok" and string.find(tostring(why), "no copy of", 1, true) ~= nil
    t.check(name, ok == "ok" or standing, (standing and ("door " .. x .. "," .. y .. " still stands open (no closed copy to click)")
        or ("click_loc " .. sym .. " at " .. x .. "," .. y .. " -> " .. tostring(ok) .. " " .. tostring(why))))
end

-- A move graded on the tile it ends on.
local function landed(t, name, want, extra)
    local here = at(t)
    t.check(name, here == want, "tile " .. here .. " (want " .. want .. ")" .. (extra and (" " .. extra) or "") .. " messages: " .. said(t, 2))
end

-- Lumbridge or Varrock -> Morytania: the members' gate, the Paterdomus trapdoor, the cellar's two
-- gates, (once) Drezel's advice, and the holy barrier to 3423,3485.
local TRAP_AT = { 3405, 3507, 0 }
local function into_morytania(t, p, advice)
    t.exec("goto-" .. p .. ".varrockGate", t.player.goto_tile, 3318, 3468, 0)
    t.exec(p .. ".varrockGate", t.player.pass_door, { closed = "fai_varrock_member_gatel",
        open = "fai_varrock_member_gatel_open", at = { 3319, 3468, 0 }, near = { 3318, 3468 }, far = { 3321, 3468 } })
    t.exec("goto-" .. p .. ".trapdoor", t.player.goto_tile, 3405, 3506, 0)
    if t.world.loc_near("trapdoor", 3, { at = TRAP_AT }) == "ok" then
        t.exec(p .. ".openTrapdoor", t.player.click_loc, "trapdoor", 1, { at = TRAP_AT })
        t.await({
            level = function()
                return t.world.loc_near("trapdoor_open", 3, { at = TRAP_AT }) == "ok"
            end,
            note = p .. ": the trapdoor opens",
        }, 6)
    else
        t.note(p .. ": the trapdoor stands open (opened within its 500-tick revert), not pressed")
    end
    local tdo_r = t.world.loc_near("trapdoor_open", 3, { at = TRAP_AT })
    local tdc_r = t.world.loc_near("trapdoor", 3, { at = TRAP_AT })
    t.check(p .. ".trapdoorOpen", tdo_r == "ok" and tdc_r ~= "ok",
        "trapdoor_open on 3405,3507,0 -> " .. tostring(tdo_r) .. "; closed trapdoor there -> " .. tostring(tdc_r)
            .. " (want the open leaf and no closed one)")
    t.exec(p .. ".descend", t.player.climb, { loc = "trapdoor_open", op = 1, op_name = "Climb-down",
        at = TRAP_AT, src = { 3405, 3506 }, dest = { 3405, 9906, 0 } })
    t.exec(p .. ".gate1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
        near = { 3405, 9896 }, far_ok = function(tl) return tl.z > 6400 and tl.z <= 9894 end,
        far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
    t.exec(p .. ".gate2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
        near = { 3430, 9897 }, far_ok = function(tl) return tl.z > 6400 and tl.x >= 3432 end,
        far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
    if advice then
        -- Priest in Peril's farewell advice (mausoleum_drezel.rs2:131-142): 60 -> 61, the barrier opens.
        -- The quest's own Drezel branch is only for stages 110..189 (mausoleum_drezel.rs2:19).
        t.exec(p .. ".drezelPresent", t.npc.await_present, "priestperiltrappedmonk2", 15, 12)
        t.exec(p .. ".talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
        t.exec(p .. ".talkToDrezel-dialog", t.chat.play, {
            "player:So can I pass through that barrier now?",
            "npc:Ah, ",
            "npc:Morytania is an evil land",
            "npc:You should take some basic precautions",
            "npc:In many ways Werewolves",
            "npc:and it is a holy relic",
            "npc:wolf form is incredibly powerful",
            "player:Okay, I will keep it equipped",
        })
        t.exec(p .. ".drezelAdvice", t.var.await_server, "varp302_priestperil", 61, 8)
    end
end

local function holy_barrier(t, p)
    t.exec(p .. ".holyBarrier", t.player.cross_gate, { loc = "pip_underground_wall_side_withportal",
        at = { 3440, 9886, 0 }, near = { 3440, 9887 },
        far_ok = function(tl) return tl.x == 3423 and tl.z == 3485 end,
        far_desc = "east of the Salve at 3423,3485 (mausoleum_interactions.rs2 p_telejump(0_53_54_31_29))" })
end

-- 3423,3485 -> Burgh de Rott: overland to the fence gate north of the village, through it, and on
-- foot to the inn's south side (3491,3229).
local function into_burgh(t, p)
    t.exec("goto-" .. p .. ".canifisRoad", t.player.goto_tile, 3510, 3470, 0)
    t.exec("goto-" .. p .. ".mortton", t.player.goto_tile, 3485, 3282, 0)
    t.exec("goto-" .. p .. ".burghGate", t.player.goto_tile, 3485, 3244, 0)
    t.exec(p .. ".burghGate", t.player.pass_door, { closed = "burgh_fencegate_l", open = "burgh_openfencegate_l",
        at = { 3485, 3244, 0 }, near = { 3485, 3244 }, far = { 3485, 3243 } })
    t.exec(p .. ".walkInn", t.player.walk_to, 3491, 3229, 40)
end

-- Out of Burgh de Rott by its fence gate, overland to the east trapdoor's tile 3423,3485.
local function out_of_burgh(t, p, last_goto)
    t.exec(p .. ".walkGate", t.player.walk_to, 3485, 3243, 40)
    t.exec(p .. ".burghGate", t.player.pass_door, { closed = "burgh_fencegate_l", open = "burgh_openfencegate_l",
        at = { 3485, 3244, 0 }, near = { 3485, 3243 }, far = { 3485, 3244 } })
    t.exec("goto-" .. p .. ".mortton", t.player.goto_tile, 3485, 3282, 0)
    t.exec("goto-" .. p .. ".canifisRoad", t.player.goto_tile, 3510, 3470, 0)
    t.exec(last_goto, t.player.goto_tile, 3423, 3485, 0)
end

-- The inn: over the Broken wall (north edge of 3491,3230), down the pub trapdoor to Veliaf.
local function inn_in(t, p, open_first)
    t.exec(p .. ".wall", t.player.click_loc, "burgh_inn_climb_over", 1)
    t.ticks(4)
    t.check(p .. ".wall.in", tile(t).z >= 3231 and tile(t).z < 6400, "tile " .. at(t) .. " (want the trapdoor room, z >= 3231) messages: " .. said(t, 2))
    if open_first then
        t.exec(p .. ".open", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
        t.ticks(3)
    end
    t.exec(p .. ".down", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
    t.ticks(6)
    t.check(p .. ".below", tile(t).z > 6400, "tile " .. at(t) .. " (want the hideout under the pub, z > 6400) messages: " .. said(t, 2))
end

local function inn_out(t, p)
    t.exec(p .. ".ladder", t.player.click_loc, "burgh_inn_basement_ladderup", 1)
    t.ticks(5)
    landed(t, p .. ".up", "3490,3231,0")
    t.exec(p .. ".wall", t.player.click_loc, "burgh_inn_climb_over", 1)
    t.ticks(4)
    t.check(p .. ".wall.out", tile(t).z <= 3230 and tile(t).level == 0, "tile " .. at(t) .. " (want south of the Broken wall, z <= 3230)")
end

-- The mended boat from the boathouse to the deck under Meiyerditch's south wall, the rock and the
-- climb up it (doh_burgh.rs2:223, doh_meiyerditch.rs2:16), onto the wall-walk 3601,3163,1.
local function burgh_to_wall(t, p, board_row)
    t.exec("goto-" .. board_row, t.player.goto_tile, 3524, 3181, 0)
    t.exec(board_row, t.player.click_loc, "sang_boat_water_multiloc", 1)
    t.ticks(8)
    landed(t, board_row .. ".arrived", "3604,3161,1")
    t.exec(p .. ".jumpRock", t.player.click_loc, "sang_boat_jump_rock", 1)
    t.ticks(4)
    landed(t, p .. ".jumpRock.on", "3605,3163,1")
    t.exec(p .. ".climbRock", t.player.click_loc, "sang_boat_wall_climb_up_rock", 1)
    t.ticks(5)
    landed(t, p .. ".climbRock.up", "3601,3163,1")
end

-- From the wall-walk down the kicked-in floorboards and over the rubble into the south streets.
local function wall_to_streets(t, p)
    t.exec(p .. ".walkBoards", t.player.walk_to, 3590, 3173, 30)
    t.exec(p .. ".boardsDown", t.player.click_loc, "meiyerditch_wall_floorboards_multi_loc", 1)
    t.ticks(5)
    landed(t, p .. ".boardsDown.below", "3590,3173,0")
    t.exec(p .. ".rubble", t.player.click_loc, "myq3_rubble_west_wall", 1)
    t.ticks(4)
    landed(t, p .. ".rubble.over", "3591,3180,0")
end

-- The Vyrewatch's punishment: Send me to the mines (doh_urgent.rs2:162-176). They hover and drift, so
-- the nearest copy of any of `syms` is walked up to before the talk; a press that misses the moving
-- model is tried again on the next copy (each miss is a note, the talk itself the step's row).
local function vyrewatch_to_mines(t, name, syms)
    local talked, detail = nil, "no copy of " .. table.concat(syms, "/") .. " within 20"
    for attempt = 1, 4 do
        for _, sym in ipairs(syms) do
            local r, row = t.npc.nearest(sym, 20)
            if r == "ok" and type(row) == "table" and row.x then
                t.player.walk_to(row.x, row.z, 20)
                local tr, td = t.player.talk_to(sym, 1, { slot = row.slot })
                detail = sym .. " (slot " .. tostring(row.slot) .. " at " .. tostring(row.x) .. "," .. tostring(row.z) .. "," .. tostring(row.level) .. ") -> " .. tostring(tr) .. " " .. string.sub(tostring(td), 1, 200)
                if tr == "ok" then
                    talked = sym
                    break
                end
                t.note(name .. ": attempt " .. attempt .. " " .. detail)
            end
        end
        if talked then
            break
        end
        t.drive.camera(0, 450, 700)
        t.ticks(3)
    end
    t.check(name, talked ~= nil, detail .. " from tile " .. at(t))
    t.exec(name .. "-dialog", t.chat.play, {
        "npc:You there, citizen",
        "choose:Send me to the mines.",
        "player:Send me to the mines.",
        "npc:The mines?",
        "choose:/Send me to the mines!/",
        "player:Send me to the mines!",
        "npc:A bit of menial work",
    })
    t.ticks(4)
    landed(t, name .. ".at", "2389,4624,2", "(the Daeyalt mine, ^doh_mine_coord)")
end

-- Fifteen Daeyalt ores into the cart, five checked at a time (doh_urgent.rs2:213-266), then the
-- guard lets you out into the hideout's secret room (doh_urgent.rs2:281).
local function mine_and_leave(t, name)
    local cart = t.player.by_symbol("loc", "area_sanguine_minecart_multiloc")
    for load = 1, 15 do
        t.player.click_loc("area_sanguine_mine_minerocks_01", 1)
        local orer = t.inv.await("castle_drakan_daeyalt_ore", 1, 80)
        if orer ~= "ok" then
            t.check(name .. ".ore" .. load, false, "no Daeyalt ore after 80 ticks of mining (load " .. load .. ") messages: " .. said(t, 3))
        end
        t.player.use_on("castle_drakan_daeyalt_ore", cart)
        t.ticks(2)
        if load % 5 == 0 then
            local _, loaded = t.var.varbit("varb2588_myq3_sang_punish_mining_cart")
            t.check(name .. ".loaded" .. load, loaded == load, "cart holds " .. tostring(loaded) .. " of 15 loads after " .. load .. " ore")
        end
    end
    t.exec(name, t.player.talk_to, "sang_myq3_mine_guard_juvinate_male", 1)
    t.exec(name .. "-dialog", t.chat.play, {
        "player:The cart's full.",
        "npc:Back to the streets with you",
    })
    t.ticks(4)
    landed(t, name .. ".out", "3638,3251,0", "(the hideout's secret room, ^doh_hideout_wall_coord)")
end

-- The hideout's secret room -> the lane west of the houses: Push the wall north, the stairs up, the
-- jump board west, the ladder down and the door north (the course's last stretch, reversed).
local function secret_to_lane(t, p)
    t.exec(p .. ".wallpush", t.player.click_loc, "area_sanguine_myreque_secret_wall_closed", 1)
    t.ticks(5)
    t.check(p .. ".wallpush.north", tile(t).z >= 3253 and tile(t).level == 0, "tile " .. at(t) .. " (want north of the wall, z >= 3253: the wall is on 3640,3253's south edge) messages: " .. said(t, 2))
    t.exec(p .. ".stairsUp", t.player.click_loc, "area_sanguine_ghetto_stairs_up", 1, { at = { 3639, 3256 } })
    t.ticks(4)
    t.check(p .. ".stairsUp.roof", tile(t).level == 1, "tile " .. at(t) .. " (want the roof, level 1) messages: " .. said(t, 2))
    t.exec(p .. ".jump41", t.player.click_loc, "myq3_agil_41_jump_west", 1)
    t.ticks(4)
    t.check(p .. ".jump41.west", tile(t).x <= 3633 and tile(t).level == 1, "tile " .. at(t) .. " (want west of the gap, x <= 3633) messages: " .. said(t, 2))
    t.exec(p .. ".ladderDown", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3631, 3258 } })
    t.ticks(4)
    t.check(p .. ".ladderDown.below", tile(t).level == 0, "tile " .. at(t) .. " (want ground level under the ladder) messages: " .. said(t, 2))
    door_click(t, p .. ".door3631", "area_sanguine_ghetto_door2", 3631, 3259)
    t.ticks(3)
    t.exec(p .. ".walkLane", t.player.walk_to, 3631, 3261, 20)
end

-- The lane -> the secret room: the door, the ladder up, the jump board east, the stairs down and
-- Push the wall south (leg 2's way in).
local function lane_to_secret(t, p, no_push)
    t.exec(p .. ".walkLane", t.player.walk_to, 3631, 3261, 40)
    door_click(t, p .. ".door3631", "area_sanguine_ghetto_door2", 3631, 3259)
    t.ticks(3)
    t.exec(p .. ".ladder3631", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3631, 3258 } })
    t.ticks(4)
    t.check(p .. ".ladder3631.roof", tile(t).level == 1, "tile " .. at(t) .. " (want the roof, level 1) messages: " .. said(t, 2))
    t.player.walk_to(3632, 3256, 10)
    t.ticks(2)
    t.exec(p .. ".jump41", t.player.click_loc, "myq3_agil_41_jump_east", 1)
    t.ticks(4)
    t.exec(p .. ".stairs3639", t.player.click_loc, "area_sanguine_ghetto_stairs_down", 1, { at = { 3639, 3256 } })
    t.ticks(4)
    t.check(p .. ".stairs3639.room", tile(t).level == 0 and tile(t).z >= 3253, "tile " .. at(t) .. " (want the room north of the hideout wall) messages: " .. said(t, 2))
    if no_push then
        return
    end
    t.exec(p .. ".wallpush", t.player.click_loc, "area_sanguine_myreque_secret_wall_closed", 1)
    t.ticks(5)
    t.check(p .. ".wallpush.in", tile(t).z <= 3252 and tile(t).level == 0, "tile " .. at(t) .. " (want the secret room, z <= 3252) messages: " .. said(t, 2))
end

-- The lane <-> the fireplace room (the pocket, x 3626-3628 z 3248-3252): its west door 3625,3252.
local function lane_to_pocket(t, p)
    t.exec(p .. ".walkDoor", t.player.walk_to, 3624, 3252, 20)
    door_click(t, p .. ".door3625", "area_sanguine_ghetto_door2", 3625, 3252)
    t.ticks(3)
    t.exec(p .. ".walkIn", t.player.walk_to, 3625, 3251, 10)
end
local function pocket_to_lane(t, p)
    door_click(t, p .. ".door3625", "area_sanguine_ghetto_door2", 3625, 3252)
    t.ticks(3)
    t.exec(p .. ".walkOut", t.player.walk_to, 3624, 3252, 10)
end

-- A Varrock Teleport out of the walled north of Meiyerditch (runes staged in setup).
local function teleport_out(t, name)
    t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = name,
        runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock square, tele_coord 0_50_53_13_32" })
    t.ticks(3)
end

local function hideout_up(t, name)
    t.exec(name, t.player.click_loc, "area_sanguine_myreque_hideout_ladder_up", 1)
    t.ticks(5)
    landed(t, name .. ".at", "3639,3250,0", "(the secret room, ^doh_hideout_exit_coord)")
end

return {
    id = "darknessofhallowvale",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- six legs of ticks add up past the 2,000-tick default budget (relay.md)
    setup = {
        "::clearinv",
        "::complete quest_priestinperil", -- guide prerequisite chain: Priest in Peril (In Aid of the Myreque needs it; Drezel's cellar shell is drawn only for varp302 8..61)
        "::complete quest_inaidofthemyreque", -- guide prerequisite: In Aid of the Myreque
        "::give dagger_wolfbane 1", -- Priest in Peril's own reward (::complete grants no items); Drezel's holy-barrier advice needs it held (mausoleum_drezel.rs2:28-33)
        "::give hammer 1", -- guide: usePlankOnBoat items (Hammer)
        "::give woodplank 2", -- guide: usePlankOnBoat/usePlankOnChute items (Plank, one each)
        "::give knife 1", -- guide: travelToMyrequeBase items (Knife, for the hideout wall)
        "::give nails 8", -- guide: usePlankOnBoat/usePlankOnChute items (four nails each)
        "::give lawrune 3", -- three Varrock Teleports out of the walled north of Meiyerditch (nothing walks out of it)
        "::give airrune 9",
        "::give firerune 3",
        "::setlevel construction 5", -- requirement: Construction 5
        "::setlevel mining 20", -- requirement: Mining 20
        "::setlevel thieving 22", -- requirement: Thieving 22
        "::setlevel agility 26", -- requirement: Agility 26
        "::setlevel crafting 32", -- requirement: Crafting 32
        "::setlevel magic 33", -- requirement: Magic 33 (Telekinetic Grab; Varrock Teleport is 25)
        "::setlevel strength 40", -- requirement: Strength 40
        "::setlevel prayer 43", -- margin: Protect from Melee for Vanstrom (Quest Helper: "Tank Vanstrom for 5 hits or use Protect from Melee"); no quest script reads prayer or combat level (grep doh_*.rs2: only stat_base agility/mining/crafting/strength)
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

                -- Lumbridge -> Morytania -> Burgh de Rott (the header's route)
                into_morytania(t, "enterMorytania", true)
                holy_barrier(t, "enterMorytania")
                into_burgh(t, "toBurgh")

                -- climbOverBrokenWall: the Broken wall on the inn's north edge (doh_burgh.rs2:14)
                t.exec("climbOverBrokenWall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                t.check("climbOverBrokenWall.crossed", tile(t).z >= 3231, "tile " .. at(t) .. " (want the trapdoor room north of the wall) messages: " .. said(t, 2))

                -- enterBurghPubBasement: Open then Climb-down the pub trapdoor (doh_burgh.rs2:36)
                t.exec("enterBurghPubBasement.open", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
                t.ticks(3)
                t.exec("enterBurghPubBasement", t.player.click_loc, "burgh_inn_trapdoor_multiloc", 1)
                t.ticks(6)
                t.check("enterBurghPubBasement.below", tile(t).z > 6400, "tile " .. at(t) .. " messages: " .. said(t, 2))

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
                landed(t, "leavePubBasement.up", "3490,3231,0")

                -- back over the Broken wall (south side needs no Agility), then to the boathouse
                t.exec("leaveInn.wall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                t.check("leaveInn.outside", tile(t).z <= 3230, "tile " .. at(t))
                -- the boathouse is open ground inside the village fence (reach.py REACH closed-doors 81)
                t.exec("goto-usePlankOnBoat", t.player.goto_tile, 3524, 3181, 0)

                -- usePlankOnBoat: Hammer, Plank, Nails; Yes to "Repair the boat?" (doh_burgh.rs2:179)
                t.exec("usePlankOnBoat", t.player.click_loc, "sang_boat_broken_multiloc", 1)
                t.exec("usePlankOnBoat-dialog", t.chat.play, { "choose:Yes." })
                t.ticks(4)
                t.expect("quest.stage.boat_fixed", t.quest.expect_stage("boat_fixed"))
                local _, planks1 = t.inv.count("woodplank")
                local _, nails1 = t.inv.count("nails")
                t.check("usePlankOnBoat.spent", planks1 == 1 and nails1 == 4, "planks " .. tostring(planks1) .. " nails " .. tostring(nails1) .. " (want 1 and 4) messages: " .. said(t, 2))

                -- usePlankOnChute (doh_burgh.rs2:206): the click walks to the chute
                t.exec("usePlankOnChute", t.player.click_loc, "sang_boathouse_chute_broken_multiloc", 1)
                t.exec("usePlankOnChute-dialog", t.chat.play, { "choose:Yes." })
                t.ticks(4)
                t.expect("quest.stage.chute_fixed", t.quest.expect_stage("chute_fixed"))

                -- pushBoat: the mended boat goes down the chute (doh_burgh.rs2:179)
                t.exec("pushBoat", t.player.click_loc, "sang_boat_broken_multiloc", 1)
                t.ticks(6)
                t.exec("pushBoat.visible", t.var.await_server, "varb2587_myq3_sea_boat_visible", 1, 6)

                -- boardBoat: the boat afloat (doh_burgh.rs2:230)
                t.exec("boardBoat", t.player.click_loc, "sang_boat_water_multiloc", 1)
                t.ticks(8)
                t.expect("quest.stage.arrived_wall", t.quest.expect_stage("arrived_wall"))
                landed(t, "boardBoat.arrived", "3604,3161,1")

                -- the deck has no way off but the rock two tiles north (doh_meiyerditch.rs2:16), then Climb-up
                t.exec("jumpBoatRock", t.player.click_loc, "sang_boat_jump_rock", 1)
                t.ticks(4)
                landed(t, "jumpBoatRock.on", "3605,3163,1")
                t.exec("climbWallRock", t.player.click_loc, "sang_boat_wall_climb_up_rock", 1)
                t.ticks(5)
                landed(t, "climbWallRock.up", "3601,3163,1")

                -- kickBoard: Search the marked floorboards, Yes to kick them in (doh_meiyerditch.rs2:47)
                t.exec("walk-kickBoard", t.player.walk_to, 3590, 3173, 30)
                t.exec("kickBoard", t.player.click_loc, "meiyerditch_wall_floorboards_multi_loc", 1)
                t.exec("kickBoard-dialog", t.chat.play, { "choose:Yes." })
                t.ticks(4)
                t.expect("quest.stage.floor_kicked", t.quest.expect_stage("floor_kicked"))

                -- climbDownBoard: the hole is now an ordinary Climb-down
                t.exec("climbDownBoard", t.player.click_loc, "meiyerditch_wall_floorboards_multi_loc", 1)
                t.ticks(5)
                landed(t, "climbDownBoard.below", "3590,3173,0")

                -- the corridor's breach: Climb-over the rubble to the city side (doh_meiyerditch.rs2:70)
                t.exec("climbRubble", t.player.click_loc, "myq3_rubble_west_wall", 1)
                t.ticks(4)
                landed(t, "climbRubble.over", "3591,3180,0")
                -- the citizen stands at 3597,3214; the south streets are one open piece from the rubble
                t.exec("walk-talkToCitizen", t.player.walk_to, 3596, 3214, 60)

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
                t.note("leg.1.end: tile " .. at(t) .. " stage varb2573_myq3_main_quest=" .. tostring(stage) .. " (ral_directions); next: Old Man Ral, southwest of the city")
                -- LEG 1 END
            end,
        },
        {
            name = "ral_to_vertida",
            run = function(t)
                -- LEG 2 BEGIN: talkToRal
                t.ticks(2)
                t.note("leg.2.start: tile " .. at(t))
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
                landed(t, "course.ladder1.up", "3595,3205,1")
                t.exec("course.jump2", t.player.click_loc, "myq3_agil_2_jump_south", 1)
                t.ticks(4)
                t.note("course.jump2.across: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump3", t.player.click_loc, "myq3_agil_3_jump_east", 1)
                t.ticks(4)
                t.note("course.jump3.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.push4", t.player.click_loc, "myq3_agil_4_pushwall_multi", 1)
                t.ticks(4)
                t.note("course.push4.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.floor4", t.player.click_loc, "myq3_agil_4_active_floor_1", 1)
                t.ticks(4)
                t.note("course.floor4.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.crawl5", t.player.click_loc, "myq3_agil_5_crawl_wall", 1)
                t.ticks(4)
                t.note("course.crawl5.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.push6", t.player.click_loc, "myq3_agil_6_pushwall_multi", 1)
                t.ticks(4)
                t.note("course.push6.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.floor6", t.player.click_loc, "myq3_agil_6_active_floor_1", 1)
                t.ticks(4)
                t.note("course.floor6.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder7", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3601, 3215 } })
                t.ticks(4)
                t.check("course.ladder7.at", tile(t).level == 0, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.table8a", t.player.click_loc, "myq3_agil_8_trapdoor_tunnel_multi", 1)
                t.ticks(4)
                t.note("course.table8a.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.table8b", t.player.click_loc, "myq3_agil_8_trapdoor_tunnel_multi", 1)
                t.ticks(4)
                t.note("course.table8b.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.tunnel8c", t.player.click_loc, "myq3_agil_8_trapdoor_tunnel_multi", 1)
                t.ticks(4)
                t.note("course.tunnel8c.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf10", t.player.click_loc, "myq3_agil_10_shelf_climb_up", 1)
                t.ticks(4)
                t.check("course.shelf10.at", tile(t).level == 1, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.crawl11", t.player.click_loc, "myq3_agil_11_crawl_wall", 1)
                t.ticks(4)
                t.note("course.crawl11.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump12", t.player.click_loc, "myq3_agil_12_jump_east", 1)
                t.ticks(4)
                t.note("course.jump12.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder13", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3603, 3222 } })
                t.ticks(4)
                t.check("course.ladder13.at", tile(t).level == 0, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("travelToPots", t.player.click_loc, "myq3_ghetto_pots_search", 1)
                t.ticks(4)
                t.expect("travelToPots.key", t.inv.expect_has("myq3_agil_key_1", 1))
                t.exec("course.door14", t.player.click_loc, "myq3_agil_14_locked_door", 1)
                t.ticks(4)
                t.note("course.door14.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder16", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3618, 3219 } })
                t.ticks(4)
                t.check("course.ladder16.at", tile(t).level == 1, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump17", t.player.click_loc, "myq3_agil_17_jump_south", 1)
                t.ticks(4)
                t.note("course.jump17.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf18", t.player.click_loc, "myq3_agil_18_shelf_climb_up", 1)
                t.ticks(4)
                t.check("course.shelf18.at", tile(t).level == 2, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder19", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3610, 3210 } })
                t.ticks(4)
                t.check("course.ladder19.at", tile(t).level == 3, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump20", t.player.click_loc, "myq3_agil_20_jump_south", 1)
                t.ticks(4)
                t.note("course.jump20.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder21", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3612, 3203 } })
                t.ticks(4)
                t.check("course.ladder21.at", tile(t).level == 2, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.line22", t.player.click_loc, "myq3_agil_22_tightrope_east", 1)
                t.ticks(4)
                t.note("course.line22.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder23", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3625, 3203 } })
                t.ticks(4)
                t.check("course.ladder23.at", tile(t).level == 1, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.push24", t.player.click_loc, "myq3_agil_24_pushwall_multi", 1)
                t.ticks(4)
                t.note("course.push24.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.floor24", t.player.click_loc, "myq3_agil_24_active_floor_1", 1)
                t.ticks(4)
                t.note("course.floor24.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf25", t.player.click_loc, "myq3_agil_25_shelf_climb_up", 1)
                t.ticks(4)
                t.check("course.shelf25.at", tile(t).level == 2, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.shelf26", t.player.click_loc, "myq3_agil_26_shelf_climb_down", 1)
                t.ticks(4)
                t.check("course.shelf26.at", tile(t).level == 1, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump27", t.player.click_loc, "myq3_agil_27_jump_north", 1)
                t.ticks(4)
                t.note("course.jump27.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump29", t.player.click_loc, "myq3_agil_29_jump_north", 1)
                t.ticks(4)
                t.note("course.jump29.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.jump30", t.player.click_loc, "myq3_agil_30_jump_east", 1)
                t.ticks(4)
                t.note("course.jump30.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("course.ladder31", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3630, 3239 } })
                t.ticks(4)
                t.check("course.ladder31.at", tile(t).level == 2, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("travelToLadderPart", t.player.click_loc, "myq3_agil_32_ladder_wall_multi", 1)
                t.ticks(4)
                t.expect("travelToLadderPart.top", t.inv.expect_has("myq3_sanguine_ladder_top", 1))
                t.exec("course.ladder32d", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3630, 3239 } })
                t.ticks(4)
                t.check("course.ladder32d.at", tile(t).level == 1, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("travelToFixLadder", t.player.click_loc, "myq3_agil_33_ladder_floor_multi", 1)
                t.ticks(4)
                t.exec("travelToFixLadder.fixed", t.var.await_server, "varb2598_myq3_agil_laddertop_wall", 2, 6)
                t.exec("course.ladder33d", t.player.click_loc, "myq3_agil_33_ladder_floor_multi", 1)
                t.ticks(4)
                t.check("course.ladder33d.at", tile(t).level == 0, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- the fixed ladder lands at 3629,3239,0 inside a room whose only way out is the door 3631,3240
                t.exec("course.door3631", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3631, 3240 } })
                t.ticks(3)
                t.note("course.door3631.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                -- north through the big room, door 3628,3250 into the fireplace pocket, door 3625,3252 into
                -- the lane west of the houses, then the roof to the room above the hideout
                t.player.walk_to(3633, 3240, 10)
                t.ticks(2)
                t.exec("course.door3628", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3628, 3250 } })
                t.ticks(3)
                t.player.walk_to(3626, 3251, 20)
                t.ticks(1)
                pocket_to_lane(t, "course")
                lane_to_secret(t, "course", true)
                -- the knife on the hideout wall from the north room, then Push it (doh_meiyerditch.rs2:513, :531)
                local wall = t.player.by_symbol("loc", "area_sanguine_myreque_secret_wall_closed")
                t.exec("travelToMyrequeBase", t.player.use_on, "knife", wall)
                t.ticks(4)
                t.expect("quest.stage.wall_found", t.quest.expect_stage(70))
                t.exec("course.wallpush", t.player.click_loc, "area_sanguine_myreque_secret_wall_closed", 1)
                t.ticks(5)
                t.check("course.wallpush.at", tile(t).z <= 3252, "tile " .. at(t) .. " (want the secret room) messages: " .. said(t, 2))
                t.exec("pressDecoratedWall", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(4)
                t.exec("pressDecoratedWall.click", t.var.await_server, "varb2590_myq3_hideout_trapdoor", 1, 6)
                t.exec("enterRug", t.player.click_loc, "sang_myreque_hideout_rug_trapdoor_unhidden", 1)
                t.ticks(4)
                t.expect("quest.stage.at_hideout", t.quest.expect_stage(80))
                t.exec("enterRug.down", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                landed(t, "enterRug.down.at", "3626,9619,0", "(^doh_hideout_arrive_coord)")
                t.player.walk_to(3629, 9640)
                t.ticks(2)
                t.note("talkToVertida.near: tile " .. at(t))
                t.exec("talkToVertida", t.player.talk_to, "myq4_vertida_visible", 1)
                t.exec("talkToVertida-dialog", t.chat.drain, { max_pages = 8 })
                t.ticks(3)
                t.expect("quest.stage.vertida_met", t.quest.expect_stage(90))
                t.note("leg.2.end: tile " .. at(t) .. ", stage varb2573_myq3_main_quest=90, backpack hammer, knife, Vertida's message for Veliaf")
                -- LEG 2 END
            end,
        },
        {
            name = "veliaf_to_mines",
            run = function(t)
                -- LEG 3 BEGIN: talkToVeliafAfterContact
                t.ticks(2)
                t.note("leg.3.start: tile " .. at(t))
                -- talkToVeliafAfterContact: out of the hideout and the walled north of Meiyerditch by a
                -- Varrock Teleport, back into Morytania and Burgh de Rott, over the Broken wall and down
                -- the pub trapdoor (it stays open from leg 1: a varbit); Vertida's message (doh_burgh.rs2:86)
                hideout_up(t, "backToBurgh.hideoutLadder")
                teleport_out(t, "backToBurgh.varrockTeleport")
                into_morytania(t, "backToBurgh", false)
                holy_barrier(t, "backToBurgh")
                into_burgh(t, "backToBurgh")
                inn_in(t, "talkToVeliafAfterContact.inn", false)
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
                inn_out(t, "leaveVeliaf")

                -- goDownToDrezel: the trapdoor east of Paterdomus, out by the village gate and overland
                out_of_burgh(t, "toPaterdomus", "goto-goDownToDrezel")
                if t.world.loc_near("pipeastsidetrapdoor", 3, { at = { 3422, 3485, 0 } }) == "ok" then
                    t.exec("goDownToDrezel.open", t.player.click_loc, "pipeastsidetrapdoor", 1, { at = { 3422, 3485, 0 } })
                    t.ticks(3)
                else
                    t.note("goDownToDrezel: the east trapdoor stands open, not pressed")
                end
                t.exec("goDownToDrezel", t.player.click_loc, "pipeastsidetrapdoor_open", 1)
                t.ticks(6)
                t.check("goDownToDrezel.below", tile(t).z > 6400, "tile " .. at(t) .. " (want the mausoleum cellar) messages: " .. said(t, 2))
                t.player.walk_to(3438, 9895, 20)
                t.note("walk-talkToDrezel.at: tile " .. at(t))
                t.exec("talkToDrezel", t.player.talk_to, "priestperiltrappedmonk2", 1)
                t.exec("talkToDrezel-dialog", t.chat.play, {
                    "player:Veliaf sent me.",
                    "npc:Strange noises outside",
                    "player:I'll take a look.",
                })
                t.ticks(2)
                t.expect("quest.stage.bush_wait", t.var.expect("varb2573_myq3_main_quest", 120))

                -- leaveDrezelToBushes: the cellar's two doors (area_mausoleum/scripts/gates.rs2:7, :25), then the west ladder
                t.player.walk_to(3433, 9897, 20)
                t.exec("leaveDrezelToBushes.door2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
                    near = { 3432, 9897 }, far_ok = function(tl) return tl.z > 6400 and tl.x <= 3431 end,
                    far_desc = "west of the second gate, x <= 3431", ticks = 60 })
                t.exec("leaveDrezelToBushes.door1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                    near = { 3405, 9894 }, far_ok = function(tl) return tl.z > 6400 and tl.z >= 9895 end,
                    far_desc = "north of the golden-key gate, z >= 9895", ticks = 40 })
                t.player.walk_to(3405, 9905, 30)
                t.exec("leaveDrezelToBushes", t.player.click_loc, "ladder_from_cellar", 1)
                t.ticks(4)
                t.check("leaveDrezelToBushes.up", tile(t).z < 6400 and tile(t).level == 0, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- searchBushes
                t.exec("walk-searchBushes", t.player.walk_to, 3391, 3481, 30)
                t.exec("searchBushes", t.player.click_loc, "myq_pt3_cutscene_werewolf_bush", 1)
                t.ticks(10)
                t.note("searchBushes.found: tile " .. at(t) .. " chat " .. tostring(t.chat.kind()) .. " messages: " .. said(t, 3))
                t.exec("searchBushes-dialog", t.chat.drain, { max_pages = 4 })
                t.ticks(3)
                t.expect("quest.stage.bushes_done", t.var.expect("varb2573_myq3_main_quest", 130))

                -- returnFromBushesToDrezel: the surface trapdoor at 3405,3507
                t.exec("walk-returnFromBushesToDrezel", t.player.walk_to, 3405, 3505, 30)
                if t.world.loc_near("trapdoor", 3, { at = TRAP_AT }) == "ok" then
                    t.exec("returnFromBushesToDrezel.open", t.player.click_loc, "trapdoor", 1, { at = { 3405, 3507 } })
                    t.ticks(3)
                else
                    t.note("returnFromBushesToDrezel: the trapdoor stands open, not pressed")
                end
                t.exec("returnFromBushesToDrezel", t.player.click_loc, "trapdoor_open", 1, { at = { 3405, 3507 } })
                t.ticks(5)
                t.check("returnFromBushesToDrezel.below", tile(t).z > 6400, "tile " .. at(t) .. " messages: " .. said(t, 2))
                t.exec("walk-talkToDrezel2.door1", t.player.cross_gate, { loc = "pip_underground_door1", at = { 3405, 9895, 0 },
                    near = { 3405, 9896 }, far_ok = function(tl) return tl.z > 6400 and tl.z <= 9894 end,
                    far_desc = "south of the golden-key gate, z <= 9894", ticks = 30 })
                t.exec("walk-talkToDrezel2.door2", t.player.cross_gate, { loc = "pip_underground_door2", at = { 3431, 9897, 0 },
                    near = { 3430, 9897 }, far_ok = function(tl) return tl.z > 6400 and tl.x >= 3432 end,
                    far_desc = "Drezel's side of the second gate, x >= 3432", ticks = 60 })
                t.exec("walk-talkToDrezel2", t.player.walk_to, 3438, 9895, 30)
                t.exec("talkToDrezel.runes", t.player.talk_to, "priestperiltrappedmonk2", 1)
                t.exec("talkToDrezel.runes-dialog", t.chat.drain, { max_pages = 8 })
                t.ticks(2)
                t.expect("quest.stage.runes_given", t.var.expect("varb2573_myq3_main_quest", 135))
                local _, law = t.inv.count("lawrune")
                t.check("talkToDrezel.runes.held", (law or 0) >= 3, "law " .. tostring(law) .. " fire " .. tostring(select(2, t.inv.count("firerune"))) .. " air " .. tostring(select(2, t.inv.count("airrune"))) .. " (want Drezel's law rune on top of the two staged ones left)")

                -- talkToRoald: Varrock Teleport from Drezel's runes, then the castle: the open courtyard,
                -- the front doorway (no door) and the throne room's door 3218,3472 (blackarmgang.lua)
                t.player.teleport_cast("varrock_teleport", { 3213, 3424, 0 }, { name = "teleportToVarrock",
                    runes = { { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = "Varrock square, tele_coord 0_50_53_13_32" })
                t.ticks(3)
                t.exec("goto-talkToRoald", t.player.goto_tile, 3212, 3466, 0)
                t.exec("talkToRoald.throneDoor", t.player.pass_door, { closed = "fai_varrock_castle_door", open = "fai_varrock_castle_door_open",
                    at = { 3218, 3472, 0 }, near = { 3218, 3472 }, far = { 3220, 3472 } })
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
                -- Aeonisig's free teleport back to Paterdomus (doh_urgent.rs2:117, only at stage 170;
                -- Quest Helper's talkToRoald dialog step "Yes thanks. I'll accept the free teleport.")
                t.exec("talkToRoald.aeonisig", t.player.talk_to, "myq3_aeonisig_roalds_advisor", 1)
                t.exec("talkToRoald.aeonisig-dialog", t.chat.play, {
                    "player:His majesty said you might teleport me",
                    "npc:Certainly",
                    "choose:Yes thanks. I'll accept the free teleport.",
                    "player:Yes thanks.",
                })
                t.ticks(5)
                landed(t, "talkToRoald.aeonisig.landed", "3439,9896,0", "(Drezel's cellar, ^doh_temple_coord)")

                -- talkToVeliafAfterDrezel: the holy barrier, overland to Burgh de Rott
                holy_barrier(t, "afterRoald")
                into_burgh(t, "afterRoald")
                inn_in(t, "talkToVeliafAfterDrezel.inn", false)
                t.exec("talkToVeliafAfterDrezel", t.player.talk_to, "myq5_veliaf_child", 1)
                t.exec("talkToVeliafAfterDrezel-dialog", t.chat.play, {
                    "player:Werewolves are crossing the Salve",
                    "npc:Then the Myreque in Meiyerditch need to hear it too",
                })
                t.ticks(2)
                t.expect("quest.stage.veliaf_told", t.var.expect("varb2573_myq3_main_quest", 180))
                inn_out(t, "leaveVeliaf2")

                -- goToMines (Quest Helper getToNorthMeiy: boardBoat, then a Vyrewatch in Meiyerditch):
                -- the boat, the wall, down into the south streets, and the Vyrewatch hovering there
                burgh_to_wall(t, "toMines.wall", "toMines.boardBoat")
                wall_to_streets(t, "toMines.streets")
                vyrewatch_to_mines(t, "goToMines", { "sang_myq3_male_flying_vyrewatch_2", "sang_myq3_female_flying_vyrewatch_1",
                    "sang_myq3_female_flying_vyrewatch_3", "sang_myq3_male_flying_vyrewatch_1" })
                -- mineDaeyaltThenLeave: a spare pick from a miner, fifteen ores into the cart, the guard
                t.exec("mineDaeyaltThenLeave.pick", t.player.talk_to, "myreque_pt3_miner1", 1)
                t.exec("mineDaeyaltThenLeave.pick-dialog", t.chat.play, {
                    "npc:Keep your voice down",
                    "choose:Do you have a spare pick?",
                    "player:Do you have a spare pick?",
                    "npc:Here. It's an old bronze one",
                })
                t.expect("mineDaeyaltThenLeave.pick.held", t.inv.expect_has("bronze_pickaxe", 1))
                mine_and_leave(t, "mineDaeyaltThenLeave")

                -- returnToMeiyBase: press the decorated wall (doh_meiyerditch.rs2:539)
                t.exec("returnToMeiyBase", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                local _, trap = t.var.varbit("varb2590_myq3_hideout_trapdoor")
                t.check("returnToMeiyBase.pressed", (trap or 0) >= 1, "varb2590_myq3_hideout_trapdoor " .. tostring(trap) .. " tile " .. at(t) .. " messages: " .. said(t, 2))
                local _, q = t.quest.stage()
                t.check("leg.3.end", q == 180, "tile " .. at(t) .. ", stage varb2573_myq3_main_quest=" .. tostring(q) .. ", backpack hammer, knife, bronze_pickaxe")
                -- LEG 3 END
            end,
        },
        {
            name = "walls_to_safalaan",
            run = function(t)
                -- LEG 4 BEGIN: climbUpDrakanWalls
                t.ticks(2)
                t.note("leg.4.start: tile " .. at(t))
                -- the rug trapdoor stays open from leg 2, so one click climbs down
                t.exec("vertida.down", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                landed(t, "vertida.down.at", "3626,9619,0")
                t.player.walk_to(3629, 9640)
                t.ticks(2)
                t.exec("vertida", t.player.talk_to, "myq4_vertida_visible", 1)
                t.exec("vertida-dialog", t.chat.drain, { max_pages = 8 })
                t.ticks(3)
                t.expect("quest.stage.return_meiyerditch", t.quest.expect_stage(190))
                -- to the western wall: nothing walks from the north of the city to the south streets, so
                -- out by teleport and in again by the boat, down the floorboards and over the rubble
                hideout_up(t, "toWalls.hideoutLadder")
                teleport_out(t, "toWalls.varrockTeleport")
                into_morytania(t, "toWalls", false)
                holy_barrier(t, "toWalls")
                into_burgh(t, "toWalls")
                burgh_to_wall(t, "toWalls.wall", "toWalls.boardBoat")
                wall_to_streets(t, "toWalls.streets")
                -- the random room above Ral's house: in through its street door, up the ladder, and down again
                t.exec("walk-goDownFromRandomRoom", t.player.walk_to, 3597, 3206, 60)
                t.exec("goDownFromRandomRoom.door", t.player.click_loc, "area_sanguine_ghetto_door1", 1, { at = { 3597, 3205 } })
                t.ticks(3)
                t.exec("goDownFromRandomRoom.up", t.player.click_loc, "area_sanguine_ghetto_ladder_up", 1, { at = { 3595, 3204 } })
                t.ticks(4)
                landed(t, "goDownFromRandomRoom.near", "3595,3205,1", "(the random room)")
                t.exec("goDownFromRandomRoom", t.player.click_loc, "area_sanguine_ghetto_ladder_down", 1, { at = { 3595, 3204 } })
                t.ticks(4)
                landed(t, "goDownFromRandomRoom.at", "3595,3203,0")
                -- back out by the same door, then on foot to the rubble, over it, and up the floor
                door_click(t, "climbUpFloor.door", "area_sanguine_ghetto_door1", 3597, 3205)
                t.ticks(3)
                t.exec("walk-climbUpFloor", t.player.walk_to, 3592, 3182, 60) -- beside the rubble pile
                t.exec("climbUpFloor.rubble", t.player.click_loc, "myq3_rubble_east_wall", 1)
                t.ticks(4)
                landed(t, "climbUpFloor.rubble.at", "3589,3180,0")
                t.exec("climbUpFloor", t.player.click_loc, "mieyerditch_wall_underboards_multi_loc", 1)
                t.ticks(4)
                landed(t, "climbUpFloor.at", "3589,3174,1")
                t.exec("climbDownWallLadder", t.player.click_loc, "myq3_ladder_down", 2, { at = { 3588, 3210 } })
                t.ticks(4)
                landed(t, "climbDownWallLadder.at", "3588,3211,0")
                -- searchRockySurface: opens the barricade pass for a while (doh_meiyerditch.rs2:631)
                t.exec("searchRockySurface", t.player.click_loc, "myq3_secret_rock_barricade_unlock", 1)
                t.ticks(3)
                t.exec("searchRockySurface.click", t.msg.expect, "You search the rocky surface and hear a mechanical click.")
                -- goThroughBarricade: through the pass, then up the ladder beyond it
                t.exec("walk-goThroughBarricade", t.player.walk_to, 3589, 3228, 60)
                t.exec("goThroughBarricade", t.player.click_loc, "myq3_ladder_up", 2, { at = { 3593, 3230 } })
                t.ticks(4)
                landed(t, "goThroughBarricade.up", "3594,3230,1")
                -- climbLadderSecondWall
                t.player.walk_to(3588, 3250, 30)
                t.drive.camera(0, 450, 500)
                t.ticks(1)
                t.exec("climbLadderSecondWall", t.player.click_loc, "myq3_ladder_up_2", 2, { at = { 3588, 3251 } })
                t.ticks(4)
                landed(t, "climbLadderSecondWall.up", "3588,3252,2")
                -- climbDownFromThirdWall
                t.exec("climbDownFromThirdWall", t.player.click_loc, "myq3_ladder_down_2", 2, { at = { 3588, 3259 } })
                t.ticks(4)
                landed(t, "climbDownFromThirdWall.down", "3588,3260,1")
                -- climbUpDrakanWalls
                t.player.walk_to(3588, 3280, 40)
                t.player.walk_to(3591, 3300, 40)
                t.player.walk_to(3595, 3309, 60)
                t.note("climbUpDrakanWalls.near: tile " .. at(t))
                -- the seam-4 fix: the wall shortcut is pressed for real from 3595,3309,1 and lands on 3595,3312,0
                t.exec("climbUpDrakanWalls", t.player.click_loc, "darkm_outer_wall_3h_meyerditch_wall_shortcut_bottom", 1)
                t.ticks(4)
                t.check("climbUpDrakanWalls", at(t) == "3595,3312,0", "tile " .. at(t) .. " (north of the wall, ground level) messages: " .. said(t, 2))
                -- talkToSafalaan: on the wall-walk (doh_castle.rs2:110, doh_safalaan_first)
                t.player.walk_to(3586, 3330, 80)
                t.note("talkToSafalaan.near: tile " .. at(t))
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
                t.expect("quest.stage.sketch_north", t.quest.expect_stage(200))
                t.expect("talkToSafalaan.charcoal", t.inv.expect_has("charcoal", 1))
                -- drawNorthWall: charcoal on papyrus, standing on the north sickle logo (doh_castle.rs2:121)
                -- the wall-walk bends: leg by leg along it, each walk_to goes as far as the walk reaches
                -- (3556,3337 -> 3556,3379 has no direct walk: reach.py; the wall-walk goes round by the west logo)
                for _, p in ipairs({ { 3572, 3331 }, { 3556, 3337 }, { 3522, 3357 }, { 3529, 3376 }, { 3556, 3379 } }) do
                    t.player.walk_to(p[1], p[2], 60)
                end
                landed(t, "drawNorthWall.near", "3556,3379,0")
                t.exec("drawNorthWall", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(5)
                t.expect("drawNorthWall.sketch", t.inv.expect_has("myq3_castle_sketch_1", 1))
                t.expect("quest.stage.sketch_west", t.quest.expect_stage(210))
                -- drawWestWall: the west sickle logo
                for _ = 1, 4 do
                    t.player.walk_to(3522, 3357, 40)
                end
                landed(t, "drawWestWall.near", "3522,3357,0")
                t.exec("drawWestWall", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(5)
                t.expect("drawWestWall.sketch", t.inv.expect_has("myq3_castle_sketch_2", 1))
                t.expect("quest.stage.sketch_south_start", t.quest.expect_stage(220))
                t.note("leg.4.end: tile " .. at(t) .. ", stage varb2573_myq3_main_quest=220 (sketch_south_start); next: the south sickle logo (3572,3331), Vanstrom")
                -- LEG 4 END
            end,
        },
        {
            name = "south_sketch_to_fireplace",
            run = function(t)
                -- LEG 5 BEGIN: drawSouthWall
                t.ticks(2)
                t.note("leg.5.start: tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())))
                -- back along the wall-walk to the south sickle logo
                for _, p in ipairs({ { 3556, 3337 }, { 3572, 3331 } }) do
                    t.player.walk_to(p[1], p[2], 60)
                end
                landed(t, "drawSouthWall.near", "3572,3331,0")
                -- Vanstrom strikes five blows (doh_castle.rs2:201, ~npc_meleeattack): Protect from Melee
                -- first (Quest Helper's tankVanstrom: "Tank Vanstrom for 5 hits or use Protect from Melee")
                do
                    local tab_result = t.ui.tab("prayer")
                    t.ticks(2)
                    local wr, w = t.ui.widget("prayerbook:prayer15")
                    t.ui.invoke(w, 1)
                    t.ticks(2)
                    local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                    local _, pr = t.skill.read("prayer")
                    t.check("tankVanstrom.protectMelee", tab_result == "ok" and wr == "ok" and on == 1, "varb4118_prayer_protectfrommelee " .. tostring(on) .. "; prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
                end
                -- drawSouthWall: charcoal on papyrus on the south logo brings Vanstrom down (doh_castle.rs2:138)
                t.exec("drawSouthWall", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(2)
                t.exec("drawSouthWall.vanstrom", t.msg.expect, "Vanstrom Klause drops down onto the wall-walk beside you!")
                -- tankVanstrom: five blows (doh_castle.rs2:185), then he knocks the player out and Sarius stands over them
                t.exec("tankVanstrom", t.npc.await_present, "myq3_vanstrom_klause_vampyre_attack", 12, 10)
                -- five blows 8 ticks apart (attackrate 8); the fifth opens the knock-out mesbox, which holds the stage at 220 until it is read
                local low, max = 999, 0
                for _ = 1, 44 do
                    t.ticks(1)
                    local hr, hp = t.skill.read("hitpoints")
                    if hr == "ok" and type(hp) == "table" then
                        if hp.level and hp.level < low then low = hp.level end
                        max = hp.base_level or max
                    end
                end
                t.exec("tankVanstrom.dismiss", t.chat.drain, { max_pages = 4 })
                t.expect("quest.stage.vanstrom_done", t.quest.expect_stage(230))
                t.check("tankVanstrom.margin", max > 0 and low * 4 >= max, "lowest hp " .. tostring(low) .. "/" .. tostring(max) .. " over the five blows under Protect from Melee (a fight nothing kills; no food carried) messages: " .. said(t, 4))
                do
                    t.ui.tab("prayer")
                    t.ticks(2)
                    local _, w = t.ui.widget("prayerbook:prayer15")
                    t.ui.invoke(w, 1)
                    t.ticks(2)
                    local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                    t.note("tankVanstrom.prayerOff: varb4118_prayer_protectfrommelee " .. tostring(on))
                end
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
                landed(t, "finishSouthSketch.near", "3572,3331,0")
                t.exec("finishSouthSketch", t.player.use_item_on_item, "charcoal", "papyrus")
                t.ticks(5)
                t.expect("finishSouthSketch.sketch", t.inv.expect_has("myq3_castle_sketch_3", 1))
                t.expect("quest.stage.sketches_complete", t.quest.expect_stage(250))
                -- useKnifeOnFireplace (doh_lab.rs2:12), Quest Helper goOpenFireplace = getToNorthMeiy: back
                -- along the wall-walk, down the wall shortcut (maplink 3595,3312,0 -> 3595,3310,1), a
                -- Vyrewatch on the fourth wall, the mine, and from the secret room to the fireplace pocket
                for _, p in ipairs({ { 3586, 3330 }, { 3595, 3312 } }) do
                    t.player.walk_to(p[1], p[2], 80)
                end
                landed(t, "toFireplace.shortcutTop", "3595,3312,0")
                t.exec("toFireplace.shortcut", t.player.click_loc, "darkm_outer_wall_3h_meyerditch_wall_shortcut_top", 1)
                t.ticks(4)
                t.check("toFireplace.shortcut.down", tile(t).level == 1 and tile(t).z <= 3310, "tile " .. at(t) .. " (want the fourth wall, level 1) messages: " .. said(t, 2))
                vyrewatch_to_mines(t, "toFireplace.vyrewatch", { "sang_myq3_female_flying_ns_vyrewatch_3", "sang_myq3_male_flying_ns_vyrewatch_3",
                    "sang_myq3_female_flying_ns_vyrewatch_2", "sang_myq3_male_flying_ns_vyrewatch_2" })
                mine_and_leave(t, "toFireplace.mine")
                secret_to_lane(t, "toFireplace")
                lane_to_pocket(t, "toFireplace")
                t.note("useKnifeOnFireplace.near: tile " .. at(t))
                local fire = t.player.by_symbol("loc", "myq3_fireplace_loose_tile")
                t.exec("useKnifeOnFireplace", t.player.use_on, "knife", fire)
                t.ticks(4)
                t.expect("useKnifeOnFireplace.message", t.inv.expect_has("myq3_sarius_message", 1))
                t.expect("quest.stage.safalaan_briefed", t.quest.expect_stage(260))
                -- leaveMeiyerBase: out of the pocket, the roof to the secret room, down the rug, and up the
                -- hideout ladder (ladders.rs2:199 via tasteofhope.rs2:300)
                pocket_to_lane(t, "leaveMeiyerBase")
                lane_to_secret(t, "leaveMeiyerBase")
                t.exec("leaveMeiyerBase.press", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                t.exec("leaveMeiyerBase.rug", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                landed(t, "leaveMeiyerBase.down", "3626,9619,0")
                hideout_up(t, "leaveMeiyerBase")
                t.note("leg.5.end: tile " .. at(t) .. ", stage varb2573_myq3_main_quest=" .. tostring(select(2, t.quest.stage())) .. " (safalaan_briefed); next: the portrait")
                -- LEG 5 END
            end,
        },
        {
            name = "portrait_to_lab",
            run = function(t)
                -- LEG 6 BEGIN: inspectPortrait
                t.ticks(2)
                t.note("leg.6.start: tile " .. at(t) .. " stage " .. tostring(select(2, t.quest.stage())))
                -- the portrait hangs on the fireplace pocket's south wall (3627,3248): out of the secret
                -- room by the roof, along the lane, in by the pocket's door
                secret_to_lane(t, "toPortrait")
                lane_to_pocket(t, "toPortrait")
                t.player.walk_to(3627, 3249, 10)
                t.ticks(1)
                -- inspectPortrait (doh_lab.rs2:75): before the knife it is only examined
                t.exec("inspectPortrait", t.player.click_loc, "myq3_statue_painting_multi", 1)
                t.ticks(3)
                t.exec("inspectPortrait.said", t.msg.expect, "A portrait of a grim-faced vampyre noble.")
                -- useKnifeOnPortrait (doh_lab.rs2:55): slash it open, then Inspect takes the key
                local portrait = t.player.by_symbol("loc", "myq3_statue_painting_multi")
                t.exec("useKnifeOnPortrait", t.player.use_on, "knife", portrait)
                t.ticks(5)
                t.exec("useKnifeOnPortrait.slashed", t.var.await_server, "varb2595_myq3_statue_key_painting_state", 1, 8)
                t.exec("useKnifeOnPortrait.said", t.msg.expect, "You slash the portrait open with your knife.")
                t.exec("useKnifeOnPortrait.key", t.player.click_loc, "myq3_statue_painting_multi", 1)
                t.ticks(3)
                t.expect("useKnifeOnPortrait.keyheld", t.inv.expect_has("myq3_lab_ornate_key", 1))
                -- readMessage (doh_lab.rs2:50)
                t.exec("readMessage", t.player.inv_op, "myq3_sarius_message", 1)
                t.ticks(2)
                local _, page = t.chat.text()
                t.check("readMessage.said", string.find(tostring(page), "Haemalchemy", 1, true) ~= nil, "chat " .. tostring(page))
                t.exec("readMessage.dismiss", t.chat.drain, { max_pages = 3 })
                -- back to the hideout: out of the pocket, the roof, the secret room, the rug
                pocket_to_lane(t, "goBase")
                lane_to_secret(t, "goBase")
                t.exec("goBase.press", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                t.exec("goBase.rug", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                landed(t, "goBase.down", "3626,9619,0")
                -- talkToSafalaanInBase (doh_castle.rs2:110, doh_safalaan_handover): he stands in the north room
                t.player.walk_to(3627, 9640, 60)
                t.ticks(1)
                t.player.walk_to(3627, 9643, 30)
                t.ticks(1)
                t.note("talkToSafalaanInBase.near: tile " .. at(t))
                t.exec("talkToSafalaanInBase", t.player.talk_to, "myreque_pt3_safalaan", 1)
                t.exec("talkToSafalaanInBase-dialog", t.chat.play, {
                    "player:I have the sketches",
                    "npc:This is grim work",
                    "npc:The laboratory's entrance",
                })
                t.ticks(3)
                t.expect("quest.stage.message_found", t.quest.expect_stage(270))
                local _, sk = t.inv.count("myq3_castle_sketch_1")
                t.check("talkToSafalaanInBase.sketches", (sk or 0) == 0, "sketches left " .. tostring(sk) .. " (want 0: handed over)")
                -- useKnifeOnTapestry (doh_lab.rs2:113): the building in the north east of the city -- up the
                -- hideout ladder, the roof to the lane, the house north of it (doors 3631,3262 and 3631,3266),
                -- the street, and the building's door 3639,3302 (reach.py NEEDS-DOOR via those three)
                hideout_up(t, "toTapestry.hideoutLadder")
                secret_to_lane(t, "toTapestry")
                door_click(t, "toTapestry.door3631s", "area_sanguine_ghetto_door1", 3631, 3262)
                t.ticks(3)
                t.exec("toTapestry.walkHouse", t.player.walk_to, 3631, 3265, 20)
                door_click(t, "toTapestry.door3631n", "area_sanguine_ghetto_door1", 3631, 3266)
                t.ticks(3)
                t.exec("toTapestry.walkStreet", t.player.walk_to, 3640, 3302, 80)
                door_click(t, "toTapestry.door3639", "area_sanguine_ghetto_door1", 3639, 3302)
                t.ticks(3)
                t.exec("toTapestry.walkIn", t.player.walk_to, 3638, 3302, 10)
                local tapestry = t.player.by_symbol("loc", "myq3_lab_tapestry_multi")
                t.exec("useKnifeOnTapestry", t.player.use_on, "knife", tapestry)
                t.ticks(5)
                t.exec("useKnifeOnTapestry.slashed", t.var.await_server, "varb2594_myq3_tapestry_state", 1, 8)
                -- through the slashed tapestry (doh_lab.rs2:128, a hop to the far side)
                t.exec("useKnifeOnTapestry.through", t.player.click_loc, "myq3_lab_tapestry_multi", 1)
                t.ticks(4)
                t.check("useKnifeOnTapestry.through.at", tile(t).z >= 3305, "tile " .. at(t) .. " (want the statue room north of the tapestry) messages: " .. said(t, 2))
                -- useKeyOnStatue (doh_lab.rs2:120): the ornate key from the portrait, turned once in the vampyre statue
                local statue = t.player.by_symbol("loc", "myq3_lab_vamp_statue_multi")
                t.exec("useKeyOnStatue", t.player.use_on, "myq3_lab_ornate_key", statue)
                t.ticks(5)
                t.expect("quest.stage.lab_unlocked", t.quest.expect_stage(280))
                -- the lab corridor's door is unlocked by the statue (doh_lab.rs2:166)
                t.exec("goDownToLab.door", t.player.click_loc, "myq3_laboratory_door_closed", 1)
                t.ticks(4)
                t.note("goDownToLab.door.at: tile " .. at(t) .. " messages: " .. said(t, 2))
                -- goDownToLab (doh_lab.rs2:176 via sinsofthefather.rs2:511)
                t.exec("goDownToLab", t.player.click_loc, "myq3_lab_stairs_down", 1)
                t.ticks(5)
                t.expect("quest.stage.lab_entered", t.quest.expect_stage(290))
                t.check("goDownToLab.at", tile(t).z > 6400, "tile " .. at(t) .. " (want the laboratory underground) messages: " .. said(t, 2))
                -- getRunes (doh_lab.rs2:198): the broken rune case, one search
                local _, law_before = t.inv.count("lawrune")
                t.exec("getRunes", t.player.click_loc, "myq3_broken_rune_case", 1)
                t.ticks(4)
                local _, law_after = t.inv.count("lawrune")
                t.check("getRunes.law", (law_after or 0) > (law_before or 0), "lawrune " .. tostring(law_before) .. " -> " .. tostring(law_after) .. " (the case must give one)")
                t.expect("quest.stage.book_taken", t.quest.expect_stage(300))
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
                t.check("leaveLab.at", tile(t).z < 6400 and tile(t).level == 0, "tile " .. at(t) .. " messages: " .. said(t, 2))
                -- bringMessageToVeliafToFinish, first half: the book to Safalaan in the hideout (doh_castle.rs2:92,
                -- 300 -> 310): out by the lab door, the tapestry, the building's door, the street and the house
                door_click(t, "bringMessage.labDoor", "myq3_laboratory_door_closed", 3641, 3307)
                t.ticks(3)
                t.exec("bringMessage.walkStatue", t.player.walk_to, 3638, 3305, 20)
                t.exec("bringMessage.tapestry", t.player.click_loc, "myq3_lab_tapestry_multi", 1)
                t.ticks(4)
                t.check("bringMessage.tapestry.at", tile(t).z <= 3304, "tile " .. at(t) .. " (want the front room south of the tapestry) messages: " .. said(t, 2))
                door_click(t, "bringMessage.door3639", "area_sanguine_ghetto_door1", 3639, 3302)
                t.ticks(3)
                t.exec("bringMessage.walkStreet", t.player.walk_to, 3631, 3267, 80)
                door_click(t, "bringMessage.door3631n", "area_sanguine_ghetto_door1", 3631, 3266)
                t.ticks(3)
                t.exec("bringMessage.walkHouse", t.player.walk_to, 3631, 3263, 20)
                door_click(t, "bringMessage.door3631s", "area_sanguine_ghetto_door1", 3631, 3262)
                t.ticks(3)
                lane_to_secret(t, "bringMessage")
                t.exec("bringMessage.press", t.player.click_loc, "sang_myreque_hideout_symbol_multi", 1)
                t.ticks(3)
                t.exec("bringMessage.rug", t.player.click_loc, "sang_myreque_hideout_trapdoor_multiloc", 1)
                t.ticks(5)
                landed(t, "bringMessage.down", "3626,9619,0")
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
                -- second half: the sealed message to Veliaf under Burgh de Rott (doh_burgh.rs2:50): up the
                -- hideout ladder, a Varrock Teleport, Paterdomus and the holy barrier, the village gate, the inn
                hideout_up(t, "toVeliaf.hideoutLadder")
                teleport_out(t, "toVeliaf.varrockTeleport")
                into_morytania(t, "toVeliaf", false)
                holy_barrier(t, "toVeliaf")
                into_burgh(t, "toVeliaf")
                inn_in(t, "bringMessageToVeliaf.inn", false)
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
                t.note("leg.6.finish: tile " .. at(t) .. ", stage varb2573_myq3_main_quest=" .. tostring(select(2, t.quest.stage())) .. " (complete)")
                -- LEG 6 END
            end,
        },
    },
}
