-- Regicide (quest_regicide), written as a relay of legs (docs/quest_authoring/relay.md).
-- Prerequisites per Quest Helper: Underground Pass complete (and Biohazard, which gates its cave
-- entrance); Agility 56 and Crafting 10 are required levels. No leg uses Crafting: its one check is the loom's
-- strip of cloth (regicide_bombcraft.rs2:112), and the strip (a guide item requirement, Regicide.java:244) is banked
-- at setup instead, so Crafting is not staged.
--
-- Door rule (b70): every closed space is entered and left by its own door, gate, stair or op, and
-- every Isafdar trap between the player and the target is pressed (regicide_traps.rs2; since
-- OSRS-Content 4b4cc88be6 a WALK over the woodspring's sprung tiles springs it, 8 hp a time).
--   * Lumbridge -> Ardougne on foot crosses the members' gate south of Taverley (reach.py 3206,3233 ->
--     2577,3298 NEEDS-DOOR via membergater@2933,3320): goto its south side, cross_gate, overland on
--     (2934,3322 -> 2577,3298 REACH len=813). The castle: double door w_ardougnedoubledoorl 2576,3298,
--     stairs 2571,3295, King Lathas's room behind elfdoor 2575,3293,1 (route as upass.lua).
--   * The cave mouth is past the West Ardougne city doors (2577,3298 -> 2437,3314 UNREACHABLE; from
--     2556,3300 REACH len=135): cross_gate ardougnedoor_r.
--   * Isafdar (reach.py/comp.py): the ring of leaves 2267,3204 and the cave share one walking pocket;
--     the woodspring 2235,3181 is its only way west, to the tracker (2234,3181 -> 2257,3150 REACH
--     len=56), the ring 2209,3202 and Iorwerth's camp (the log 2201,3237). Nothing from the camp side
--     needs the spring.
--   * Out of Tirannwn for the bomb's eastern half (furnace, Chemist, still) and at the end for Arianwyn:
--     Lumbridge Teleport (magic_spells.dbrow [magic_spell_teleport_lumbridge], tele_coord 0_50_50_21_18),
--     then overland (3221,3218 -> 3227,3254 the furnace REACH len=58; -> 2932,3216 Rimmington REACH len=421).
--     Any furnace burns the limestone (regicide_bombcraft.rs2 [label,regicide_make_quicklime], reached from
--     every smithing furnace). The Chemist's house: poshdoor 2932,3214 (biohazard.lua's crossing); the still
--     stands outside it (2932,3216 -> 2927,3212 REACH).

local function in_kings_room(tile)
    return tile.level == 1 and tile.x >= 2575 and tile.x <= 2579 and tile.z >= 3292 and tile.z <= 3294
end
local function MEMBER_GATE_NORTH()
    return { loc = "membergatel", at = { 2934, 3320, 0 }, near = { 2934, 3318 },
        far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
        far_desc = "north of the members' gate, z >= 3320", far = { 2934, 3322 } }
end
local function CASTLE_DOOR_IN()
    return { closed = "w_ardougnedoubledoorl", open = "w_ardougnedoubledoorlopen", at = { 2576, 3298, 0 },
        near = { 2577, 3298 }, far = { 2573, 3298 },
        far_ok = function(tile) return tile.level == 0 and tile.x <= 2575 end, far_desc = "in the castle hall, x <= 2575" }
end
local function CASTLE_DOOR_OUT()
    return { closed = "w_ardougnedoubledoorl", open = "w_ardougnedoubledoorlopen", at = { 2576, 3298, 0 },
        near = { 2574, 3298 }, far = { 2577, 3298 },
        far_ok = function(tile) return tile.level == 0 and tile.x >= 2576 end, far_desc = "on the street, x >= 2576" }
end
local function CASTLE_STAIRS_UP() -- the up row lands on the down stand tile 2571,3294,1
    return { loc = "stairs", op = 1, op_name = "Climb-up", at = { 2571, 3295, 0 }, src = { 2571, 3298 }, dest = { 2571, 3294, 1 } }
end
local function CASTLE_STAIRS_DOWN() -- maplink_1_40_51_11_30_down is keyed on the stand tile 2571,3294,1
    return { loc = "stairstop", op = 1, op_name = "Climb-down", at = { 2571, 3295, 1 }, src = { 2571, 3294 }, dest = { 2571, 3298, 0 } }
end
local function KINGS_DOOR_IN()
    return { closed = "elfdoor", open = "elfdooropen", at = { 2575, 3293, 1 }, near = { 2574, 3293 }, far = { 2577, 3293 },
        far_ok = in_kings_room, far_desc = "in King Lathas's room, x 2575-2579 z 3292-3294 level 1" }
end
local function KINGS_DOOR_OUT()
    return { closed = "elfdoor", open = "elfdooropen", at = { 2575, 3293, 1 }, near = { 2576, 3293 }, far = { 2573, 3293 },
        far_ok = function(tile) return tile.level == 1 and tile.x <= 2574 end, far_desc = "out of the king's room, x <= 2574 level 1" }
end
-- ardougnedoor_r copy 2558,3300 from the east forcemoves to 2556,3300 (area_ardougne_west/scripts/doors.rs2:35-58)
local function CITY_DOOR_WEST()
    return { loc = "ardougnedoor_r", at = { 2558, 3300, 0 }, near = { 2559, 3300 },
        far_ok = function(tile) return tile.x <= 2556 end, far_desc = "in West Ardougne, x <= 2556" }
end
-- Ardougne's castle street -> the cave mouth: the West Ardougne city doors, then overland.
local function street_to_cave_mouth(t, prefix)
    t.exec("goto-" .. prefix .. ".cityDoor", t.player.goto_tile, 2559, 3300, 0)
    t.exec(prefix .. ".cityDoor", t.player.cross_gate, CITY_DOOR_WEST())
    t.ticks(2)
    t.exec("goto-" .. prefix, t.player.goto_tile, 2437, 3314, 0) -- beside the cave mouth (2434,3314 is under upass_caveentrance2)
end
-- The members' gate south of Taverley, from its south side.
local function members_gate_north(t, prefix)
    t.exec("goto-" .. prefix .. ".memberGate", t.player.goto_tile, 2934, 3318, 0)
    t.exec(prefix .. ".memberGate", t.player.cross_gate, MEMBER_GATE_NORTH())
end
-- The castle street, the castle's double door and stairs, King Lathas's door.
local function up_to_lathas(t, prefix)
    t.exec("goto-" .. prefix .. ".castleStreet", t.player.goto_tile, 2577, 3298, 0)
    t.exec(prefix .. ".castleDoorIn", t.player.pass_door, CASTLE_DOOR_IN())
    t.exec(prefix, t.player.climb, CASTLE_STAIRS_UP()) -- ladders.rs2 [proc,climb]
    t.ticks(3)
    t.exec(prefix .. ".kingsDoorIn", t.player.pass_door, KINGS_DOOR_IN())
end
local function lumbridge_teleport(t, name, where)
    t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = name,
        runes = { { "earthrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }, where = where })
    t.ticks(3)
end

-- An Isafdar trap pressed from its stand tile and graded on the FAR tile, never on a chat line: a ring of leaves'
-- "You manage to cross safely." from the previous crossing is still in the last lines when the next one slips
-- (regicide_b70b leg 4). A pitfall slip drops the player in its pit (regicide_traps.rs2 [label,regicide_enter_pit]);
-- the hand holds put him on the ring's SOUTH tile ([oploc1,regicide_trap_hand_holds]), which is the far side of a
-- southward crossing. A woodspring slip throws him back to the stand side ([label,regicide_fail_trap_woodspring]).
local ISAFDAR_FAR = { -- stand tile -> far tile (the landings of the rows in skill_agility/configs/maplink_agility.dbrow)
    ["2267,3205"] = { 2267, 3201 }, -- ring of leaves by the cave, southward
    ["2238,3181"] = { 2234, 3181 }, -- woodspring, westward
    ["2209,3201"] = { 2209, 3205 }, -- ring of leaves south of the camp, northward
    ["2209,3205"] = { 2209, 3201 }, -- the same ring, southward
    ["2220,3152"] = { 2220, 3155 }, -- tripwire, northward
    ["2220,3155"] = { 2220, 3152 }, -- tripwire, southward
}
local function isafdar_cross(t, h, name, sym, stand_x, stand_z, at_x, at_z, via)
    local far = ISAFDAR_FAR[stand_x .. "," .. stand_z]
    assert(far, "isafdar_cross: no far tile for stand " .. stand_x .. "," .. stand_z)
    local function on_far()
        local _, w = t.world.tile()
        return w.level == 0 and math.abs(w.x - far[1]) <= 1 and math.abs(w.z - far[2]) <= 1, w
    end
    local crossed, note = false, ""
    for attempt = 1, 10 do
        -- a slip costs up to 15 (a pitfall) and hitpoints come out of the pass low: top up first (regicide_b70b died
        -- on a ring press at under 15 hp)
        for _ = 1, 3 do h.eat() end
        if attempt == 1 then
            h.travel("walk-" .. name, stand_x, stand_z, via)
            t.exec(name, t.player.click_loc, sym, 1, { at = { at_x, at_z } })
        else
            t.player.walk_to(stand_x, stand_z, 20)
            t.ticks(2)
            t.player.click_loc(sym, 1, { at = { at_x, at_z } })
        end
        t.ticks(6)
        local ok, w = on_far()
        if ok then crossed = true break end
        if w.z > 6400 then
            t.player.click_loc("regicide_trap_hand_holds", 1) -- out of the pit
            t.ticks(6)
            ok, w = on_far()
            note = note .. " [attempt " .. attempt .. ": slipped into the pit, hand holds -> " .. w.x .. "," .. w.z .. "]"
            if ok then crossed = true break end
        else
            note = note .. " [attempt " .. attempt .. ": slipped, at " .. w.x .. "," .. w.z .. "]"
        end
        h.eat()
    end
    local _, here = t.world.tile()
    t.check(name .. "-crossed", crossed, "from " .. stand_x .. "," .. stand_z .. " to " .. here.x .. "," .. here.z .. " (far side "
        .. far[1] .. "," .. far[2] .. ")" .. note .. " :: " .. h.last(3))
end
-- A log balance (regicide_traps.rs2 [label,regicide_logbalance]: three forcemoves), graded on the far end.
local function cross_log(t, name, at_x, land_x)
    local r, d = t.player.click_loc("regicide_logbalance1_start", 1, { at = { at_x, 3237 } })
    t.ticks(10)
    local _, w = t.world.tile()
    t.check(name, w.level == 0 and math.abs(w.x - land_x) <= 1 and w.z == 3237, "log at " .. at_x .. ",3237 pressed ("
        .. tostring(r) .. " " .. tostring(d) .. "), landed " .. w.x .. "," .. w.z .. " (far end x " .. land_x .. ")")
end
-- Poison from a snagged tripwire (regicide_traps.rs2 queue(poison_player, 0, 10)): the brought-along antipoison.
local function cure_poison(t, name)
    local _, poison = t.var.server("varp102_poison")
    if (poison or 0) <= 0 then return end
    for _, dose in ipairs({ "1doseantipoison", "2doseantipoison", "3doseantipoison", "4doseantipoison" }) do
        local _, n = t.inv.count(dose)
        if (n or 0) > 0 then
            t.player.inv_op(dose, 1)
            t.ticks(3)
            local _, after = t.var.server("varp102_poison")
            t.check(name, (after or 1) <= 0, "varp102_poison " .. tostring(poison) .. " -> " .. tostring(after) .. " (drank " .. dose .. ")")
            return
        end
    end
end

-- The Underground Pass maze (navigateMaze): five rock bridges over pits, each crossed east by its own Cross
-- (walkway_upass_narrow_mid_top, upass_obstacles.rs2 [oploc1,walkway_upass_narrow_mid_top]). A failed agility roll
-- drops the player off the bridge for 5 damage, and the walkways cannot be climbed back onto from below: the way on is
-- the maze again from a bridge the player can stand at. The landings (reach.py/comp.py): under 2380, 2387 and 2392 the
-- floor joins the maze's start (2392,9625 -> 2373,9634 REACH len=28); under 2399 a pocket whose one way out is bridge
-- 2392 crossed WEST (reach.py 2399,9634 -> 2373,9634 NEEDS-OP via walkway_upass_narrow_mid_top@2392,9627); under 2406 the
-- spiked pit, left by the z 9632 walkway's bridge 2406,9632 crossed west. Seen in regicide_b70b (b70): a fall off 2392
-- landed on 2392,9625 (z-2: the fall's p_exactmove then p_teleport both apply $dz, upass_obstacles.rs2:380-382; this is
-- LostCity's own behaviour, and the z-1 tiles are solid, so z-2 is the intended floor).
local MAZE_BRIDGES = {
    { 2380, 9634, { { 2373, 9634 } } },
    { 2387, 9631, { { 2384, 9634 }, { 2384, 9631 } } },
    { 2392, 9627, { { 2389, 9631 }, { 2389, 9627 } } },
    { 2399, 9632, { { 2395, 9627 }, { 2395, 9632 } } },
    { 2406, 9637, { { 2403, 9632 }, { 2403, 9637 } } },
}
local function maze_cross(t, prefix, eat)
    local function here() local _, w = t.world.tile() return w end
    local function press(x, z)
        local r = t.player.click_loc("walkway_upass_narrow_mid_top", 1, { at = { x, z } })
        t.ticks(10)
        return r
    end
    local i, falls, note = 1, 0, ""
    while i <= #MAZE_BRIDGES do
        local bx, bz, hops = MAZE_BRIDGES[i][1], MAZE_BRIDGES[i][2], MAZE_BRIDGES[i][3]
        local w = here()
        if w.x >= 2406 and w.z >= 9632 and w.z <= 9635 then
            -- the spiked pit under 2406: out by the z 9632 bridge westward
            t.player.walk_to(2407, 9632, 30)
            press(2406, 9632)
            w = here()
            note = note .. "<pit out -> " .. w.x .. "," .. w.z .. "> "
        elseif w.x >= 2393 and w.x <= 2400 and w.z >= 9633 and w.z <= 9637 then
            -- the pocket under 2399: out by bridge 2392 westward, onto its near tile
            press(2392, 9627)
            w = here()
            note = note .. "<pocket out -> " .. w.x .. "," .. w.z .. "> "
            if w.x == 2391 and w.z == 9627 then i = 3 end
        end
        bx, bz, hops = MAZE_BRIDGES[i][1], MAZE_BRIDGES[i][2], MAZE_BRIDGES[i][3]
        for _, hop in ipairs(hops) do t.player.walk_to(hop[1], hop[2], 60) end
        t.player.walk_to(bx - 1, bz, 60)
        w = here()
        if not (w.x == bx - 1 and w.z == bz) then
            -- on the floor below the walkways: back to the maze's start
            note = note .. "{off the walkway at " .. w.x .. "," .. w.z .. ", back to the start} "
            i = 1
            t.player.walk_to(2373, 9634, 60)
            falls = falls + 1
        else
            local r = press(bx, bz)
            local after = here()
            note = note .. "[" .. bx .. " " .. tostring(r) .. " -> " .. after.x .. "," .. after.z .. "] "
            if after.x == bx + 1 and after.z == bz then
                t.check(prefix .. bx .. (falls > 0 and ("-after-fall" .. falls) or ""), true, "now " .. after.x .. "," .. after.z .. " :: " .. note)
                note = ""
                i = i + 1
            else
                falls = falls + 1
                eat()
            end
        end
        if falls > 8 then
            local _, last = t.world.tile()
            t.check(prefix .. bx, false, "eight falls in the maze; at " .. last.x .. "," .. last.z .. " :: " .. note)
            return
        end
    end
end

-- Iban's four collapsed bridges between Iban's door and the temple (legs 2 and 6; Quest Helper's enterTemple line points,
-- Regicide.java:530-551). Each Cross is an agility roll, stat_random(agility,160,300) (upass_obstacles.rs2:431): at the
-- guide's Agility 56 that is ~238/256, so a bridge fails ~7% and four bridges about 1 walk in 4. A fall
-- (upass_obstacles.rs2:432-439) teleports the player to the dwarf cavern below, 2335,9821 or 2333,9866 level 0, for
-- 25% of current hitpoints + 4; while %varb9135_upass_koftik_chat is 0 (::complete quest_undergroundpass does not write
-- it) the fall also adds Koftik, who talks (:441-444, koftik.rs2:172 [label,koftik_isthatyou]).
-- The way back up is a cavewalltunnel_upass_up (upass_tunnels.rs2:21-25), and from either of its landings the one walk
-- back without another op is to bridge A's near end -- reach.py: 2113,4729 / 2150,4546 -> 2172,4686 REACH closed-doors
-- (len 108 / 170), while every later bridge's near end is NEEDS-OP via bridgecollapsed2@2164,4686 (or 2121,4686). So a
-- fall off any bridge walks back to A and crosses the four again.
--   landing 2335,9821 -> tunnel 2336,9793 (stand 2336,9794, REACH len=28) -> 1_33_71_38_2 = 2150,4546 level 1
--   landing 2333,9866 -> tunnel 2304,9915 (stand 2305,9915, REACH len=77) -> 1_33_73_1_57 = 2113,4729 level 1
-- Each walk-back hop is a reach.py REACH closed-doors leg (2172,4630 is solid: the south column goes 4618 -> 4642).
local IBAN_BRIDGES = {
    -- row, approach hops, loc, loc tile, far tile (upass_obstacles.rs2 [label,upass_cross_bridge] $end)
    { "crossBridgeA", { {2172,4723}, {2172,4686} }, "bridgecollapsed2", 2164, 4686, 2163, 4686 },
    { "crossBridgeB", { {2161,4686}, {2161,4699}, {2157,4699}, {2154,4697} }, "bridgecollapsed1", 2154, 4690, 2154, 4689 },
    { "crossBridgeC", { {2154,4686}, {2152,4685}, {2153,4682}, {2153,4678}, {2154,4676}, {2160,4676}, {2160,4670}, {2165,4670}, {2165,4667}, {2162,4667} }, "bridgecollapsed1", 2162, 4663, 2162, 4662 },
    { "crossBridgeD", { {2161,4659} }, "bridgecollapsed2", 2161, 4654, 2161, 4653 },
}
local IBAN_FALL_WAY_UP = {
    -- fall landing z, tunnel stand, tunnel loc, tunnel landing, walk back to bridge A's near end
    { 9821, { 2336, 9794 }, { 2336, 9793 }, { 2150, 4546 },
        { {2162,4546}, {2173,4559}, {2173,4583}, {2172,4606}, {2172,4618}, {2172,4642}, {2172,4654}, {2172,4678}, {2172,4686} } },
    { 9866, { 2305, 9915 }, { 2304, 9915 }, { 2113, 4729 },
        { {2124,4730}, {2135,4731}, {2146,4732}, {2158,4732}, {2170,4732}, {2172,4723}, {2172,4686} } },
}
local IBAN_MAX_FALLS = 6 -- at ~7% a press, a seventh fall in one walk means something else is wrong
-- Crosses the four bridges; suffix names the walk ("" in leg 2, "-again" in leg 6). Returns true on the far side of D;
-- otherwise writes a FAIL row, ends the run and returns false (the caller returns).
local function iban_bridges(t, suffix, eat)
    local function here() local _, w = t.world.tile() return w end
    local function walk_hops(hops)
        for _, hop in ipairs(hops) do
            for _ = 1, 3 do
                t.player.walk_to(hop[1], hop[2], 60)
                local w = here()
                if w.x == hop[1] and w.z == hop[2] then break end
            end
        end
    end
    local function give_up()
        t.finish(1)
        return false
    end
    local idx, falls, stuck = 1, 0, 0
    while idx <= #IBAN_BRIDGES do
        local st = IBAN_BRIDGES[idx]
        local label = st[1] .. suffix .. (falls > 0 and ("-after-fall" .. falls) or "") .. (stuck > 0 and ("-retry" .. stuck) or "")
        walk_hops(st[2])
        eat() -- a fall costs 25% of current hitpoints + 4
        t.exec(label, t.player.click_loc, st[3], 1, { at = { st[4], st[5] } })
        t.ticks(6)
        local w = here()
        if w.level == 1 and w.x == st[6] and w.z == st[7] then
            t.check(label .. "-landed", true, "crossed on the roll, now at " .. w.x .. "," .. w.z .. " level 1 (far side " .. st[6] .. "," .. st[7] .. ")")
            idx, stuck = idx + 1, 0
        elseif w.level == 1 then
            -- still up on the cavern floor but not across: the press did not take; walk the approach and press again
            stuck = stuck + 1
            if stuck > 2 then
                t.check(label .. "-landed", false, "three presses of " .. st[3] .. "@" .. st[4] .. "," .. st[5] .. " left the player at "
                    .. w.x .. "," .. w.z .. " level 1, not on the far side " .. st[6] .. "," .. st[7])
                return give_up()
            end
        else
            falls = falls + 1
            local way
            for _, cand in ipairs(IBAN_FALL_WAY_UP) do
                if w.level == 0 and math.abs(w.z - cand[1]) <= 3 then way = cand end
            end
            local fell_detail = "slipped off " .. st[3] .. "@" .. st[4] .. "," .. st[5] .. " to " .. w.x .. "," .. w.z .. " level " .. w.level
                .. " (dwarf cavern; upass_obstacles.rs2:432-436 lands 2335,9821 or 2333,9866)"
            if falls > IBAN_MAX_FALLS then
                t.check(label .. "-fell", false, "fall " .. falls .. " in one walk (the bound is " .. IBAN_MAX_FALLS .. "): " .. fell_detail)
                return give_up()
            end
            if not t.check(label .. "-fell", way ~= nil, fell_detail) then return give_up() end
            -- Koftik's first meeting opens on the landing: play it out before walking
            t.ticks(2)
            if t.chat.kind() ~= "none" then
                t.exec("goBackUpToIbansCavern" .. suffix .. "-" .. falls .. "-koftik", t.chat.drain, { max_pages = 20 })
            end
            eat()
            t.player.walk_to(way[2][1], way[2][2], 90)
            local up_label = "goBackUpToIbansCavern" .. suffix .. "-" .. falls
            t.exec(up_label, t.player.click_loc, "cavewalltunnel_upass_up", 1, { at = way[3] })
            t.ticks(4)
            local up = here()
            if not t.check(up_label .. "-tile", up.level == 1 and up.x == way[4][1] and up.z == way[4][2], "tunnel " .. way[3][1] .. "," .. way[3][2]
                    .. " up landed at " .. up.x .. "," .. up.z .. " level " .. up.level .. " (upass_tunnels.rs2:21-25: " .. way[4][1] .. "," .. way[4][2] .. ")") then
                return give_up()
            end
            walk_hops(way[5])
            local back = here()
            if not t.check(up_label .. "-walkback", back.level == 1 and back.x == 2172 and back.z == 4686,
                    "walked back to " .. back.x .. "," .. back.z .. " level " .. back.level .. ", bridge A's near end 2172,4686") then
                return give_up()
            end
            idx, stuck = 1, 0
        end
    end
    return true
end

return {
    id = "regicide",
    fixture = "fresh_lumbridge.ini",
    max_frames = 360000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_biohazard", -- Quest Helper prerequisite chain: Underground Pass needs it
        "::complete quest_undergroundpass", -- Quest Helper prerequisite; the arm leaves the finished pass's state (Lathas met, orbs, badges, unicorn, a rolled grid pattern: quest_cheat.rs2 quest_undergroundpass)
        -- Underground Pass prerequisite state for the second walk through the pass (leg 2): the area-1 well
        -- needs all four orbs (upass_well.rs2:12) and Iban's door the three badges and the horn
        -- (upass_bloodwell.rs2:25). A completed Underground Pass has delivered all of them.
        "::setlevel agility 56", -- Quest Helper: Agility 56 (rockslides, upass_obstacles.rs2:38)
        "::setlevel hitpoints 40", -- a questing account's fighting levels for the pass's spiders
        "::setlevel defence 30",
        "::give bronze_arrow 20", -- Arrows (metal, unpoisoned)
        "::give rope 2", -- Rope: the pit swing eats one each crossing (upass_obstacles.rs2:110), walked here and again by a later leg
        "::give spade 1", -- Spade
        "::give tinderbox 1", -- lights the cloth-wrapped arrow (the guide's fire)
        "::give lobster 6", -- food for the pass's traps
        -- Leg 4: killGuard is Quest Helper's Tyras guard (combat 110: 110 hitpoints, defence 100, attack 85, strength 95;
        -- regicide_tyras_guard.rs2 / combat_stats.generated.npc:21612). Brought along: a ranger's levels, a magic
        -- shortbow with rune arrows, and sharks. The guide lists combat gear for this fight.
        "::setlevel ranged 70",
        "::setlevel hitpoints 70",
        "::setlevel defence 40",
        "::give magic_shortbow 1", -- also enterTheDungeon's Bow (not crossbow): upass_bridge.rs2 [aploc1,oldbridge_guiderope] takes any weapon_bow
        "::give rune_arrow 150",
        -- Since the eat-delay port (raid branch, OSRS-Content 7936c59bf9) an eat no longer holds the Tyras guard's hits: the
        -- 12 sharks ran out (lowest 20/70) and the tripwire's poison finished the character. The guard is pure melee
        -- (regicide_tyras_guard.rs2 [ai_applayer2,regicide_old_camp_guard] ~npc_meleeattack), so leg 4 prays Protect from
        -- Melee (needs Prayer 43; 70 points last ~350 ticks at 1 point / 5 ticks, wiki Prayer; the fight took 268).
        "::setlevel prayer 70",
        -- Food: prayed, the guard fight eats little. These 4 sharks and the 6 lobsters are legs 1-5's food; leg 5 deposits
        -- what is left at Draynor bank (the bomb chapter's items need the slots) and leg 6 draws the 8 banked sharks there
        -- (::bankgive shark 8 below) for the second pass and Isafdar. Leg 1's cloth wrap also needs a free slot: setup
        -- must stay <= 26 slots (27 broke lightArrow, hp_regicide_1).
        "::give shark 4", -- b70: legs 3-4 had no food left after the pass's spear traps (run regicide 19:44, lobster x0 at leg 3's end)
        -- Quest Helper Regicide.java:260 recommends antidotes/antipoisons (ItemCollections.ANTIPOISONS) for the
        -- tripwire's poison (regicide_traps.rs2:23 queue(poison_player, 0, 10)).
        "::give 4doseantipoison 1",
        -- Two Lumbridge Teleports out of Tirannwn (header): Magic 31 and 1 earth, 3 air, 1 law each (magic_spells.dbrow
        -- [magic_spell_teleport_lumbridge]). Magic 31 does not move the combat level: Ranged 70 already sets it.
        "::setlevel magic 31",
        "::give earthrune 2",
        "::give airrune 6",
        "::give lawrune 2",
        -- The bomb chapter's brought-along items (Quest Helper Regicide.java item requirements: limestone, gloves, pestle and
        -- mortar, pot, coal for the still, strip of cloth, cooked rabbit; rope for the second pit swing) wait in the bank:
        -- the 28 slots cannot carry them through legs 1-4. Leg 5 draws them at Draynor bank on the way to the furnace and
        -- leg 6 again on the way back to the pass (gaps-combat.md "Bank the fight food").
        "::bankgive limestone 1",
        "::bankgive leather_gloves 1",
        "::bankgive pestle_and_mortar 1",
        "::bankgive pot_empty 1",
        "::bankgive coal 9",
        "::bankgive regicide_cloth 1",
        "::bankgive cooked_rabbit 1",
        "::bankgive rope 1",
        "::bankgive shark 8", -- leg 6's food (the second pass, the tripwires, the Isafdar traps), drawn at Draynor in leg 6
    },
    bind = {
        varp = "varp328_regicide_quest",
        constants = {
            not_started = 0, received_message = 1, spoken_lathas = 2, spoken_scouts = 3, spoken_iorwerth = 4,
            spoken_tracker = 5, shown_pendant = 6, found_footprints = 7, spoken_tracker2 = 8, defeated_guard = 9,
            entered_camp = 10, spoken_iorwerth2 = 11, killed_tyras = 12, reported_iorwerth = 13,
            spoken_arianwyn = 14, complete = 15,
        },
        row = "quest_regicide",
        display = "Regicide",
        points = 3,
    },

    legs = {
        { name = "lathas_and_first_half_of_the_pass", run = function(t)
            -- LEG 1 BEGIN: goToArdougneCastleFloor2
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- the members' gate, the castle door, the stairs and the king's door (header)
            members_gate_north(t, "goToArdougneCastleFloor2")
            up_to_lathas(t, "goToArdougneCastleFloor2")
            local _, at = t.world.tile()
            t.check("goToArdougneCastleFloor2-level", in_kings_room(at), "standing at " .. at.x .. "," .. at.z .. " level " .. at.level)

            t.exec("talkToKingLathas", t.player.talk_to, "kinglathas", 1) -- king_lathas.rs2:79
            t.exec("talkToKingLathas-dialog", t.chat.play, {
                "player:I received your message",
                "npc:Ahh... adventurer",
                "options",
                "choose:I assume you have a plan?",
                "player:I assume",
                "npc:I do indeed",
                "player:Elves?",
                "npc:Well that may",
                "player:So what do I need",
                "npc:You are to head",
                "npc:With their help",
                "options",
                "choose:Yes.",
                "player:Very well",
                "npc:My brother may not",
                "player:I see",
                "npc:Good luck",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_lathas", t.quest.expect_stage("spoken_lathas"))

            -- goDownCastleStairs: out the way in -- the king's door, the stairs, the castle door
            t.exec("goDownCastleStairs.kingsDoorOut", t.player.pass_door, KINGS_DOOR_OUT())
            t.exec("goDownCastleStairs", t.player.climb, CASTLE_STAIRS_DOWN()) -- maplink_1_40_51_11_30_down
            t.ticks(3)
            t.exec("goDownCastleStairs.castleDoorOut", t.player.pass_door, CASTLE_DOOR_OUT())
            -- enterWestArdougne, then enterTheDungeon: the cave mouth west of Ardougne (upass_entrance.rs2:9)
            street_to_cave_mouth(t, "enterWestArdougne")
            t.exec("enterTheDungeon", t.player.click_loc, "upass_caveentrance2", 1)
            t.ticks(6)
            _, at = t.world.tile()
            t.check("enterTheDungeon-tile", at.z > 9000, "underground at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(2))

            -- a failed agility roll slips the climber back (upass_obstacles.rs2:38): click until the far side
            local function other_side_lines()
                local _, lines = t.msg.last(12)
                local n = 0
                for _, line in ipairs(lines or {}) do
                    if string.find(tostring(type(line) == "table" and line.text or line), "step down the other side", 1, true) then n = n + 1 end
                end
                return n
            end
            local function climb(name, slide_x, slide_z)
                local crossed = false
                local detail = ""
                for attempt = 1, 8 do
                    local before = other_side_lines()
                    local result = t.player.click_loc("rockslide2_obstacle_upass", 1, { at = { slide_x, slide_z } })
                    t.ticks(8)
                    local _, here = t.world.tile()
                    detail = "attempt " .. attempt .. ": " .. tostring(result) .. " now at " .. here.x .. "," .. here.z
                    if other_side_lines() > before then
                        crossed = true
                        detail = detail .. " (server: ...and step down the other side)"
                        break
                    end
                end
                t.check(name, crossed, detail)
            end
            climb("climbOverRockslide1", 2480, 9713)
            -- rockslides 2 and 3 lie on the same corridor to the bridge; plain travel then climb
            t.exec("goto-climbOverRockslide2", t.player.goto_tile, 2473, 9706, 0)
            climb("climbOverRockslide2", 2471, 9706)
            climb("climbOverRockslide3", 2458, 9712)

            -- searchBagForCloth, useClothOnArrow, lightArrow, then the shot
            t.exec("goto-searchBagForCloth", t.player.goto_tile, 2453, 9716, 0) -- beside the bag (2452,9715 is under upass_gear)
            t.exec("searchBagForCloth", t.player.click_loc, "upass_gear", 1) -- koftik.rs2:143
            t.ticks(4)
            local crossed = false
            for attempt = 1, 5 do
                local sfx = attempt == 1 and "" or ("-retry" .. attempt)
                if attempt > 1 then
                    t.exec("goto-searchAgain" .. sfx, t.player.goto_tile, 2453, 9716, 0)
                    t.exec("searchBagForCloth" .. sfx, t.player.click_loc, "upass_gear", 1)
                    t.ticks(4)
                end
                t.exec("useClothOnArrow" .. sfx, t.player.use_item_on_item, "damp_cloth", "bronze_arrow") -- upass_bridge.rs2:13
                t.ticks(2)
                t.exec("lightArrow" .. sfx, t.player.use_item_on_item, "tinderbox", "unlitarrow") -- firemaking.rs2:25
                t.ticks(2)
                if attempt == 1 then t.exec("wieldBow", t.player.equip, "magic_shortbow") end
                t.exec("wieldLitArrow" .. sfx, t.player.equip, "litarrow")
                t.ticks(2)
                t.exec("goto-walkNorthEastOfBridge" .. sfx, t.player.goto_tile, 2450, 9722, 0)
                t.exec("shootBridgeRope" .. sfx, t.player.click_loc, "oldbridge_guiderope", 1) -- upass_bridge.rs2:64
                for _poll = 1, 12 do -- forcewalks then a teleport
                    t.ticks(4)
                    _, at = t.world.tile()
                    if at.x < 2444 then break end
                end
                _, at = t.world.tile()
                if at.x < 2444 then crossed = true break end
            end
            t.check("shootBridgeRope-crossed", crossed, "after the shot at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(4))

            -- The way on from the bridge, driven by clicks (upass_obstacles.rs2, upass_grid.rs2): the rope swing over the
            -- pit, rockslides 4 and 5, the grid, its lever and the spear traps. Row names carry -outbound because the
            -- return walk of a later leg meets the same obstacles.
            local function eat_if_low()
                local hp_result, hp_row = t.skill.read("hitpoints")
                if hp_result == "ok" and hp_row.level <= 14 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                end
            end
            t.exec("goto-crossThePit-outbound", t.player.goto_tile, 2461, 9699, 0)
            local pit_rock = t.player.by_symbol("loc", "obstical_rockswing_norope")
            t.exec("crossThePit-outbound", t.player.use_on, "rope", pit_rock) -- upass_obstacles.rs2:110
            t.ticks(12)
            _, at = t.world.tile()
            t.check("crossThePit-outbound-landed", at.x >= 2465, "after the swing at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            climb("climbOverRockslide4", 2491, 9691)
            climb("climbOverRockslide5", 2482, 9679)
            -- crossTheGrid: %varp6010_upass_grid_pattern names one safe 2-row band per column group (upass_grid.rs2:72-96);
            -- the timer fails a player outside all three. Band of digit d is z 9673+2(d-1) .. +1.
            local _, pattern = t.var.server("varp6010_upass_grid_pattern")
            pattern = tonumber(pattern) or 0
            local d1, d2, d3 = math.floor(pattern / 100) % 10, math.floor(pattern / 10) % 10, pattern % 10
            local function band_z(d) return 9673 + 2 * (d - 1) end
            t.check("crossTheGrid-pattern", d1 >= 1 and d1 <= 5 and d2 >= 1 and d3 >= 1, "upass_grid_pattern=" .. tostring(pattern) .. " -> safe bands z " .. band_z(d1) .. " / " .. band_z(d2) .. " / " .. band_z(d3))
            local grid_path = { { 2478, band_z(d1) }, { 2473, band_z(d1) }, { 2473, band_z(d2) }, { 2469, band_z(d2) }, { 2469, band_z(d3) }, { 2467, band_z(d3) }, { 2466, band_z(d3) } }
            local trail = {}
            for _, wp in ipairs(grid_path) do
                t.player.walk_to(wp[1], wp[2], 14)
                t.ticks(2)
                local _, here = t.world.tile()
                trail[#trail + 1] = here.x .. "," .. here.z
            end
            _, at = t.world.tile()
            t.check("crossTheGrid", at.x <= 2467 and not string.find(last_lines(6), "trap", 1, true), "walked the safe bands " .. table.concat(trail, " > ") .. " -> " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            t.player.walk_to(2466, 9673, 10)
            t.ticks(2)
            _, at = t.world.tile()
            t.check("goto-pullLeverAfterGrid-outbound", at.x == 2466 and at.z <= 9674, "walked south along x 2466, outside the grid zone, to " .. at.x .. "," .. at.z)
            t.exec("pullLeverAfterGrid-outbound", t.player.click_loc, "portcullis_lever_up", 1) -- upass_grid.rs2:25
            t.ticks(10)
            _, at = t.world.tile()
            t.check("pullLeverAfterGrid-outbound-through", at.x < 2465, "after the lever at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            -- the five spear traps (upass_obstacles.rs2:203): a disarm roll, a failure costs hp
            local function pass_trap(name, trap_x, trap_z)
                local detail = ""
                local passed = false
                for attempt = 1, 5 do
                    local result = t.player.click_loc("upass_speartrap", 1, { at = { trap_x, trap_z } })
                    t.ticks(2)
                    local kind = t.chat.kind()
                    local played = "no page (" .. tostring(kind) .. ")"
                    if kind ~= "none" then
                        played = tostring(t.chat.play({ "mesbox:It's a trap", "choose:Yes, I'll give it a go." }))
                    end
                    t.ticks(6)
                    local _, there = t.world.tile()
                    local hp_result, hp_row = t.skill.read("hitpoints")
                    detail = "attempt " .. attempt .. ": click " .. tostring(result) .. ", chat " .. played .. ", now " .. there.x .. "," .. there.z .. ", hp " .. tostring(hp_result == "ok" and hp_row.level or hp_result) .. " :: " .. last_lines(3)
                    if there.x < trap_x then passed = true break end
                    eat_if_low()
                end
                t.check(name, passed, detail)
                eat_if_low()
            end
            pass_trap("passTrap1-outbound", 2443, 9677)
            pass_trap("passTrap2-outbound", 2440, 9677)
            pass_trap("passTrap3-outbound", 2435, 9675)
            pass_trap("passTrap4-outbound", 2432, 9675)
            pass_trap("passTrap5-outbound", 2430, 9675)

            -- goBackUpToIbansCavern: the way back up after a fall off Iban's bridges (iban_bridges, legs 2 and 6). Not a content gap: driven when a crossing fails.
            -- The first half ends here, at the last spear trap; leg 2 starts its walk to the plank room and the well from this tile.

            local _, stage = t.quest.stage()
            local _, lobsters = t.inv.count("lobster")
            _, at = t.world.tile()
            t.check("leg.1.end", true, "player at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; regicide_quest=" .. tostring(stage) .. " read from the server; magic shortbow worn, spade/rope/tinderbox carried, lobster x" .. tostring(lobsters))
            -- LEG 1 END
        end },
        { name = "well_and_pass_west", run = function(t)
            -- LEG 2 BEGIN: leaveWellCave
            -- The ladder lists the pass back to front; the route runs collectPlank, climbDownWell,
            -- digMud, crossLedge, navigateMaze (pickCellLock, goThroughPipe), leaveUnicornArea,
            -- openIbansDoor, enterWell, leaveWellCave.
            local function where(label, want_x, want_z, want_level)
                local _, w = t.world.tile()
                local _, ml = t.msg.last(3)
                local out = {}
                for _, line in ipairs(ml or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                local ok = true
                if want_x then ok = math.abs(w.x - want_x) <= 6 and math.abs(w.z - want_z) <= 6 and w.level == want_level end
                t.check(label, ok, "standing at " .. w.x .. "," .. w.z .. " level " .. w.level .. " :: " .. table.concat(out, " | "))
            end

            -- collectPlank: the plank lies on the floor of the north room (m38_151.spawn:35)
            -- The way to the plank is the pass walked backwards from the last spear trap: east along the trap corridor to the
            -- pipe at 2451,9689 (upass_obstacles.rs2:88, the grating opens from this side only), north to the bridge side, then
            -- the north room. Plain travel between the obstacles.
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function eat_if_low()
                local hp_result, hp_row = t.skill.read("hitpoints")
                if hp_result == "ok" and hp_row.level <= 14 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                end
            end
            for _, hop in ipairs({ { 2446, 9677 }, { 2451, 9688 } }) do
                for _ = 1, 4 do
                    t.player.walk_to(hop[1], hop[2], 60)
                    local _, hw = t.world.tile()
                    if math.abs(hw.x - hop[1]) <= 1 and math.abs(hw.z - hop[2]) <= 1 then break end
                    eat_if_low()
                end
            end
            where("walkToPipe-tile", 2451, 9688, 0)
            local _, before_pipe = t.world.tile()
            t.exec("climbThroughPipeNorth", t.player.click_loc, "upass_pipe4", 1, { at = { 2451, 9689 } }) -- upass_obstacles.rs2:88
            t.ticks(14)
            local _, after_pipe = t.world.tile()
            t.check("climbThroughPipeNorth-tile", after_pipe.z > before_pipe.z + 3, "pipe from " .. before_pipe.x .. "," .. before_pipe.z .. " to " .. after_pipe.x .. "," .. after_pipe.z .. " :: " .. last_lines(3))
            for _, hop in ipairs({ { 2450, 9710 }, { 2440, 9722 }, { 2434, 9725 } }) do
                for _ = 1, 4 do
                    t.player.walk_to(hop[1], hop[2], 60)
                    local _, hw = t.world.tile()
                    if math.abs(hw.x - hop[1]) <= 1 and math.abs(hw.z - hop[2]) <= 1 then break end
                    eat_if_low()
                end
            end
            where("walkToPlankRoom", 2434, 9725, 0)
            local _, planks_before = t.inv.count("woodplank")
            t.exec("collectPlank", t.player.click_obj, "woodplank", 3)
            t.ticks(2)
            local _, planks = t.inv.count("woodplank")
            t.check("collectPlank-inv", planks == planks_before + 1, "woodplank " .. tostring(planks_before) .. " -> " .. tostring(planks))

            -- climbDownWell: upass_well.rs2:10, all four orbs placed -> 2423,9660
            -- Back to the well: the pit swing only goes east, so the plank room's way home is the whole pass forwards again
            -- (upass_obstacles.rs2:110): rope swing, rockslides 4 and 5, the grid, its lever and the five spear traps, then west.
            local function other_side_lines()
                local _, lines = t.msg.last(12)
                local n = 0
                for _, line in ipairs(lines or {}) do
                    if string.find(tostring(type(line) == "table" and line.text or line), "step down the other side", 1, true) then n = n + 1 end
                end
                return n
            end
            local function climb(name, slide_x, slide_z)
                local crossed = false
                local detail = ""
                for attempt = 1, 8 do
                    local before = other_side_lines()
                    local result = t.player.click_loc("rockslide2_obstacle_upass", 1, { at = { slide_x, slide_z } })
                    t.ticks(8)
                    local _, here = t.world.tile()
                    detail = "attempt " .. attempt .. ": " .. tostring(result) .. " now at " .. here.x .. "," .. here.z
                    if other_side_lines() > before then
                        crossed = true
                        detail = detail .. " (server: ...and step down the other side)"
                        break
                    end
                end
                t.check(name, crossed, detail)
            end
            local function pass_trap(name, trap_x, trap_z)
                local detail = ""
                local passed = false
                for attempt = 1, 5 do
                    local result = t.player.click_loc("upass_speartrap", 1, { at = { trap_x, trap_z } })
                    t.ticks(2)
                    local kind = t.chat.kind()
                    local played = "no page (" .. tostring(kind) .. ")"
                    if kind ~= "none" then
                        played = tostring(t.chat.play({ "mesbox:It's a trap", "choose:Yes, I'll give it a go." }))
                    end
                    t.ticks(6)
                    local _, there = t.world.tile()
                    local hp_result, hp_row = t.skill.read("hitpoints")
                    detail = "attempt " .. attempt .. ": click " .. tostring(result) .. ", chat " .. played .. ", now " .. there.x .. "," .. there.z .. ", hp " .. tostring(hp_result == "ok" and hp_row.level or hp_result) .. " :: " .. last_lines(3)
                    if there.x < trap_x then passed = true break end
                    eat_if_low()
                end
                t.check(name, passed, detail)
                eat_if_low()
            end
            local _, rope_count = t.inv.count("rope")
            t.check("ropeCarried", rope_count >= 1, "rope x" .. tostring(rope_count) .. " for the swing back over the pit")
            for _, hop in ipairs({ { 2450, 9712 }, { 2458, 9699 }, { 2461, 9699 } }) do
                for _ = 1, 4 do
                    t.player.walk_to(hop[1], hop[2], 60)
                    local _, hw = t.world.tile()
                    if math.abs(hw.x - hop[1]) <= 1 and math.abs(hw.z - hop[2]) <= 1 then break end
                    eat_if_low()
                end
            end
            where("goto-crossThePit-return", 2461, 9699, 0)
            local pit_rock = t.player.by_symbol("loc", "obstical_rockswing_norope")
            local at
            for attempt = 1, 3 do
                t.exec("crossThePit-return" .. (attempt > 1 and tostring(attempt) or ""), t.player.use_on, "rope", pit_rock) -- upass_obstacles.rs2:110
                t.ticks(12)
                _, at = t.world.tile()
                if at.x >= 2465 and at.z >= 9690 then break end
                -- a fall drops you in the swamp; walk back round is not driven here, so stop at the first miss
                break
            end
            t.check("crossThePit-return-landed", at.x >= 2465, "after the swing at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            local w4n = ""
            for _, pr in ipairs({ { 2467, 9699 }, { 2470, 9699 }, { 2475, 9695 }, { 2480, 9695 }, { 2489, 9691 } }) do
                local w4r, w4d = t.player.walk_to(pr[1], pr[2], 40)
                local _, pw = t.world.tile()
                w4n = w4n .. pr[1] .. "," .. pr[2] .. ":" .. tostring(w4r) .. " " .. tostring(w4d) .. " at " .. pw.x .. "," .. pw.z .. "; "
            end
            t.note("walk to rockslide 4: " .. w4n)
            climb("climbOverRockslide4-return", 2491, 9691)
            t.player.walk_to(2484, 9679, 40)
            climb("climbOverRockslide5-return", 2482, 9679)
            local _, pattern = t.var.server("varp6010_upass_grid_pattern")
            pattern = tonumber(pattern) or 0
            local d1, d2, d3 = math.floor(pattern / 100) % 10, math.floor(pattern / 10) % 10, pattern % 10
            local function band_z(d) return 9673 + 2 * (d - 1) end
            t.check("crossTheGrid-return-pattern", d1 >= 1 and d1 <= 5 and d2 >= 1 and d3 >= 1, "upass_grid_pattern=" .. tostring(pattern) .. " -> safe bands z " .. band_z(d1) .. " / " .. band_z(d2) .. " / " .. band_z(d3))
            local grid_path = { { 2478, band_z(d1) }, { 2473, band_z(d1) }, { 2473, band_z(d2) }, { 2469, band_z(d2) }, { 2469, band_z(d3) }, { 2467, band_z(d3) }, { 2466, band_z(d3) } }
            local grid_trail = {}
            for _, wp in ipairs(grid_path) do
                t.player.walk_to(wp[1], wp[2], 14)
                t.ticks(2)
                local _, here = t.world.tile()
                grid_trail[#grid_trail + 1] = here.x .. "," .. here.z
            end
            _, at = t.world.tile()
            t.check("crossTheGrid-return", at.x <= 2467 and not string.find(last_lines(6), "trap", 1, true), "walked the safe bands " .. table.concat(grid_trail, " > ") .. " -> " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            t.player.walk_to(2466, 9673, 10)
            t.ticks(2)
            _, at = t.world.tile()
            t.check("goto-pullLeverAfterGrid-return", at.x == 2466 and at.z <= 9674, "walked south along x 2466, outside the grid zone, to " .. at.x .. "," .. at.z)
            t.exec("pullLeverAfterGrid-return", t.player.click_loc, "portcullis_lever_up", 1) -- upass_grid.rs2:25
            t.ticks(10)
            _, at = t.world.tile()
            t.check("pullLeverAfterGrid-return-through", at.x < 2465, "after the lever at " .. at.x .. "," .. at.z .. " :: " .. last_lines(4))
            pass_trap("passTrap1-return", 2443, 9677)
            pass_trap("passTrap2-return", 2440, 9677)
            pass_trap("passTrap3-return", 2435, 9675)
            pass_trap("passTrap4-return", 2432, 9675)
            pass_trap("passTrap5-return", 2430, 9675)
            for _ = 1, 4 do
                t.player.walk_to(2417, 9677, 40)
                local _, hw = t.world.tile()
                if math.abs(hw.x - 2417) <= 1 and math.abs(hw.z - 9677) <= 1 then break end
            end
            where("walkToWell-tile", 2417, 9677, 0)
            t.exec("climbDownWell", t.player.click_loc, "cave_well", 1)
            t.ticks(6)
            where("climbDownWell-tile", 2423, 9660, 0)

            -- navigateMaze: the cell lock and the pipe are its sub steps
            -- the corridor tile of the guide (2393,9655), walked from the well landing; a goto to 2393,9657 lands INSIDE the cell
            t.player.walk_to(2410, 9656, 60)
            t.player.walk_to(2393, 9655, 60)
            where("walkToCell-tile", 2393, 9655, 0)
            -- each pick is stat_random(thieving,128,400) (upass_unicorn.rs2:13), ~50% at Thieving 1: sixteen presses, then a FAIL
            local picked, picks = false, 0
            for attempt = 1, 16 do -- two railings stand on x 2393 (z 9656 then z 9655); each pick can fail
                picks = attempt
                local _, before = t.world.tile()
                t.player.click_loc("cave_railings2", 1, { at = { 2393, before.z >= 9657 and 9656 or 9655 } }) -- upass_unicorn.rs2:11
                t.ticks(8)
                local _, after = t.world.tile()
                if after.z <= 9654 then picked = true end
                if picked then break end
            end
            do
                local _, cell = t.world.tile()
                t.check("pickCellLock", picked and cell.level == 0 and cell.x == 2393 and cell.z <= 9654, "through the cell lock after " .. picks
                    .. " press(es): standing at " .. cell.x .. "," .. cell.z .. " level " .. cell.level .. " (south of the railings, z <= 9654) :: " .. last_lines(3))
            end

            -- digMud: spade on the loose mud (upass_unicorn_tunnels.rs2:9) -> 2392,9646
            t.ticks(1)
            local mud = t.player.by_symbol("loc", "upass_mud")
            t.exec("digMud", t.player.use_on, "spade", mud) -- upass_unicorn_tunnels.rs2:9
            t.ticks(8)
            where("digMud-tile", 2392, 9646, 0)

            -- crossLedge: walk the mud tunnel to the ledge's east end (upass_obstacles.rs2:313) -> 2374,9638
            t.player.walk_to(2376, 9644, 40)
            t.exec("crossLedge", t.player.click_loc, "upass_ledge", 1)
            t.ticks(8)
            where("crossLedge-tile", 2374, 9638, 0)

            -- navigateMaze (seam2): the maze is five rock bridges over pits (walkway_upass_narrow_mid_top, op1 Cross,
            -- upass_obstacles.rs2:360 = LostCity upass_obstacles.rs2:291); a walk stops at each one (they block by the
            -- game), so each is clicked from its west side. A failed agility roll drops you under it (z-1, or z+1 at
            -- 2399,9632 and 2406,9632) for 5 damage; walk back round to the near side and click again.
            local function mz_here() local _, w = t.world.tile() return w end
            maze_cross(t, "navigateMaze-bridge", eat_if_low)
            for _, hop in ipairs({ { 2421, 9637 }, { 2422, 9634 }, { 2422, 9610 }, { 2421, 9606 }, { 2419, 9605 } }) do
                t.player.walk_to(hop[1], hop[2], 40)
            end
            where("navigateMaze-pipeMouth", 2419, 9605, 0)

            -- goThroughPipe: upass_pipe6 at 2417,9605 crawls west; Underground Pass is complete, so the crawl lands 26
            -- tiles further west in the room where the unicorn died (upass_obstacles.rs2:410-414) -> 2387,9605
            local piped = false
            for _ = 1, 4 do
                t.player.click_loc("upass_pipe6", 1, { at = { 2417, 9605 } })
                t.ticks(16)
                if mz_here().x < 2395 then piped = true break end
            end
            local pw = mz_here()
            t.check("goThroughPipe", piped and math.abs(pw.x - 2387) <= 3 and pw.z == 9605, "after the pipe at " .. pw.x .. "," .. pw.z .. " level " .. pw.level)

            -- leaveUnicornArea: walk to the south face of upass_unicorn_doorl 2375,9611 (angle south) -> 2371,9666
            -- (upass_unicorn_tunnels.rs2:27-29)
            for _, hop in ipairs({ { 2378, 9605 }, { 2378, 9607 }, { 2375, 9607 }, { 2375, 9610 } }) do
                t.player.walk_to(hop[1], hop[2], 30)
            end
            where("goto-leaveUnicornArea-walk", 2375, 9610, 0)
            t.exec("leaveUnicornArea", t.player.click_loc, "upass_unicorn_doorl", 1, { at = { 2375, 9611 } })
            t.ticks(6)
            where("leaveUnicornArea-tile", 2371, 9666, 0)
            -- openIbansDoor: with the badges and the horn the door opens onto Iban's temple
            -- 2371,9666 -> 2369,9718 is a 185-step walk round the tunnel (m37_151.jm2 collision): up the west column, east
            -- through the cavern, north up the east side, west along z 9721 to Iban's door; hops are local tiles + 2368,9664
            local trail = {}
            for _, hop in ipairs({ {3,14}, {5,25}, {10,30}, {10,33}, {20,36}, {20,40}, {40,40}, {40,42}, {55,43}, {56,52}, {56,57},
                    {45,57}, {31,57}, {25,58}, {22,57}, {20,55}, {10,55}, {1,54} }) do
                local hx, hz = hop[1] + 2368, hop[2] + 9664
                for attempt = 1, 4 do
                    local wr = t.player.walk_to(hx, hz, 50)
                    local w = mz_here()
                    if wr == "ok" and w.x == hx and w.z == hz then break end
                    if attempt == 4 then trail[#trail + 1] = "STALL " .. hx .. "," .. hz .. " at " .. w.x .. "," .. w.z end
                end
            end
            t.note("walk " .. table.concat(trail, " ## "))
            where("walkToIbansDoor", 2369, 9718, 0)
            t.exec("openIbansDoor", t.player.click_loc, "cavetempledoor2r", 1)
            t.ticks(6)
            where("openIbansDoor-tile", 2173, 4725, 1) -- the door's landing on Iban's side, level 1 (runs regicide, regicide_b70b)

            -- enterWell
            -- enterTemple (seam1): Quest Helper's line points (Regicide.java:530-551) from Iban's door landing, crossing the
            -- four collapsed bridges on the line (iban_bridges above: an agility roll each, a fall walked back up and round), then
            -- Iban's temple doors send a Regicide player to the ruined temple (upass_tomb.rs2 open_iban_door)
            if not iban_bridges(t, "", eat_if_low) then return end
            t.player.walk_to(2147, 4648, 20)
            where("walkToTemple", 2147, 4648, 1)
            t.exec("enterTemple", t.player.click_loc, "upass_templedoor_closed_right", 1, { at = { 2143, 4648 } })
            t.ticks(4)
            where("enterTemple-tile", 2014, 4712, 1)
            t.exec("enterWell", t.player.click_loc, "regicide_voyage_temple_well1", 1) -- regicide_route.rs2:11
            t.ticks(6)
            where("enterWell-tile", 2343, 9622, 0)

            -- leaveWellCave: arrival fires the Idris scene (regicide_route.rs2:35)
            t.exec("goto-leaveWellCave", t.player.goto_tile, 2315, 9624, 0)
            t.exec("leaveWellCave", t.player.click_loc, "regicide_voyage_temple_exit", 1) -- regicide_route.rs2:27
            for _poll = 1, 10 do
                if t.chat.kind() ~= "none" then break end
                t.ticks(1)
            end
            t.check("leaveWellCave-scene", t.chat.kind() ~= "none", "Idris's scene opened on the arrival tile, page kind " .. tostring(t.chat.kind()))
            t.exec("talkToIdris-dialog", t.chat.play, {
                "npc:Halt human",
                "npc:Wait! What was that",
                "npc:Are you the human",
                "player:Yes that's me",
                "npc:Good... We've been expecting you",
                "npc:You should speak with Lord Iorwerth",
            })
            t.ticks(4)
            t.expect("quest.stage.spoken_scouts", t.quest.expect_stage("spoken_scouts"))
            local _, stage = t.quest.stage()
            local _, at = t.world.tile()
            t.check("leg.2.end", true, "player at " .. at.x .. "," .. at.z .. " level " .. at.level .. "; regicide_quest=" .. tostring(stage) .. " read from the server; planks " .. tostring(planks))
            -- LEG 2 END
        end },
        { name = "pass_east_and_tirannwn_traps", run = function(t)
            -- LEG 3 BEGIN: crossThePit
            -- The pass steps of this leg's ladder rows (crossThePit, pullLeverAfterGrid, passTrap1-5) are the same
            -- locs leg 1 pressed on the way in (rows ...-outbound) and leg 2 pressed on the way back west
            -- (rows ...-return): the player arrives here already out of the pass, at stage 3, so nothing is
            -- re-entered by a teleport. Leg 3 is the walk from the arrival tile to Lord Iorwerth's camp.
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function where(label, want_x, want_z, want_level)
                local _, w = t.world.tile()
                local ok = true
                if want_x then ok = math.abs(w.x - want_x) <= 6 and math.abs(w.z - want_z) <= 6 and w.level == want_level end
                t.check(label, ok, "standing at " .. w.x .. "," .. w.z .. " level " .. w.level .. " :: " .. last_lines(3))
            end
            -- Brought-along food only (setup: lobsters and sharks), eaten when the hazards have taken hitpoints below 40.
            local function eat_if_low()
                local hp_result, hp_row = t.skill.read("hitpoints")
                if hp_result ~= "ok" or (hp_row.current or hp_row.level) > 40 then return end
                for _, food in ipairs({ "lobster", "shark" }) do
                    local _, n = t.inv.count(food)
                    if (n or 0) > 0 then
                        t.player.inv_op(food, 1)
                        t.ticks(3)
                        return
                    end
                end
            end
            -- Walking is real travel; a hazard in the way is crossed with its own click. The walk verb stops short
            -- on a long route, so ask again until it stands near the target.
            local function travel(label, x, z, via)
                for _, pt in ipairs(via or {}) do
                    t.player.walk_to(pt[1], pt[2], 40)
                    t.ticks(1)
                end
                for _ = 1, 6 do
                    local _, w = t.world.tile()
                    if math.abs(w.x - x) <= 1 and math.abs(w.z - z) <= 1 then break end
                    t.player.walk_to(x, z, 40)
                    t.ticks(1)
                end
                where(label, x, z, 0)
            end
            -- Every crossing fires ~maplink_agility, which only answers on the exact source tile of its row
            -- (skill_agility/configs/maplink_agility.dbrow): the stand tiles are those rows' src tiles. A failed
            -- pitfall drops the player in a pit; the hand holds put them back on the src tile.
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, _success_text, via)
                isafdar_cross(t, { travel = travel, eat = eat_if_low, last = last_lines }, name, sym, stand_x, stand_z, at_x, at_z, via)
            end

            where("leg.3.start", 2312, 3216, 0)
            -- goFromCaveToLeaves: the ring of leaves at 2267,3205 (maplink src 2267,3205), on Quest Helper's line; the hops
            -- keep off the woodspring at 2295,3214 (reach.py 2312,3216 -> 2267,3205 REACH len=66 by 2298,3209 / 2287,3207)
            travel("walk-goFromCaveToLeaves", 2267, 3205, { { 2302, 3212 }, { 2298, 3209 }, { 2287, 3207 }, { 2271, 3211 }, { 2269, 3207 } })
            cross("goFromCaveToLeaves", "regicide_pitfall_side", 2267, 3205, 2267, 3204, "cross safely")
            where("goFromCaveToLeaves-tile", 2267, 3201, 0)
            -- goFromLeavesToStickTrap: the spring trap, crossed west from its east side (Quest Helper's line 2267,3201 ->
            -- 2258,3182 -> 2238,3181). The ring's pocket has no other way west (comp.py: its one edge op is the spring), and a
            -- walk over the sprung tiles 2236-2237,3181 springs it (regicide_traps.rs2 [timer,regicide_woodspring_walk]):
            -- the hops keep north of z 3181 until the stand tile (reach.py 2267,3201 -> 2238,3181 REACH len=49).
            travel("walk-goFromLeavesToStickTrap", 2238, 3181, { { 2262, 3193 }, { 2259, 3186 }, { 2239, 3186 } })
            cross("goFromLeavesToStickTrap", "regicide_trap_woodspring", 2238, 3181, 2235, 3181, "skillfully pass")
            where("goFromLeavesToStickTrap-tile", 2234, 3181, 0)
            -- climbThroughForest: the dense forest west of the tracker, on foot from the spring's west side (reach.py
            -- 2234,3181 -> 2240,3149 REACH len=40; the tripwire at 2251,3168 stays east of the hops). The gate
            -- regicide_route.rs2:72-75 refuses it below spoken_tracker2 ("You can see no way to get past this."), so at
            -- stage 3 the press is the refusal; the real crossing is driven at that stage in leg 4.
            travel("walk-climbThroughForest", 2240, 3149, { { 2233, 3173 }, { 2239, 3168 } })
            t.exec("climbThroughForest", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2238, 3148 } })
            t.ticks(4)
            t.check("climbThroughForest-refused", string.find(last_lines(4), "no way to get past", 1, true) ~= nil, "stage 3 refusal :: " .. last_lines(3))
            -- goUpToLeafTowardsLog: the ring of leaves south of the camp (maplink src 2209,3201), on foot back past the spring's
            -- west side (Quest Helper's line 2234,3181 -> 2224,3180 -> 2209,3201; reach.py 2233,3181 -> 2209,3201 REACH len=44)
            travel("walk-goUpToLeafTowardsLog", 2209, 3201, { { 2239, 3168 }, { 2233, 3173 }, { 2224, 3180 }, { 2219, 3188 }, { 2212, 3194 } })
            cross("goUpToLeafTowardsLog", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely")
            where("goUpToLeafTowardsLog-tile", 2209, 3205, 0)
            -- goCrossLogToCamp: the log north to Iorwerth's camp (regicide_traps.rs2:73)
            travel("walk-goCrossLogToCamp", 2201, 3236, { { 2205, 3215 }, { 2203, 3225 }, { 2201, 3232 } })
            cross_log(t, "goCrossLogToCamp", 2201, 2196)
            where("goCrossLogToCamp-tile", 2196, 3237, 0)
            -- talkToIorwerth: Lord Iorwerth at the camp, stage spoken_scouts (lord_iorwerth.rs2:12)
            travel("walk-talkToIorwerth", 2203, 3253)
            t.exec("talkToIorwerth", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("talkToIorwerth-dialog", t.chat.play, {
                "player:Hello there", "npc:Ahh", "npc:Unfortunately", "player:I see",
                "npc:Indeed", "npc:You'll find him", "player:Thank you",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_iorwerth", t.quest.expect_stage(4))
            t.ticks(10)
            local _, w = t.world.tile()
            local _, stage = t.quest.stage()
            local _, lobsters = t.inv.count("lobster")
            t.check("leg.3.end", true, "tile " .. w.x .. "," .. w.z .. " level " .. w.level .. ", regicide_quest=" .. tostring(stage) .. ", lobster x" .. tostring(lobsters) .. ", magic shortbow worn, tinderbox, spade, bronze arrows, woodplank (rope spent on the pit)")
            -- LEG 3 END
        end },
        { name = "tracker_and_camp_guard", run = function(t)
            -- LEG 4 BEGIN: goFromCaveToLeaves
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function where(label, want_x, want_z, want_level)
                local _, w = t.world.tile()
                local ok = true
                if want_x then ok = math.abs(w.x - want_x) <= 6 and math.abs(w.z - want_z) <= 6 and w.level == want_level end
                t.check(label, ok, "standing at " .. w.x .. "," .. w.z .. " level " .. w.level .. " :: " .. last_lines(3))
            end
            -- Brought-along food only (setup: lobsters and sharks), eaten when the hazards have taken hitpoints below 40.
            local function eat_if_low()
                local hp_result, hp_row = t.skill.read("hitpoints")
                if hp_result ~= "ok" or (hp_row.current or hp_row.level) > 40 then return end
                for _, food in ipairs({ "lobster", "shark" }) do
                    local _, n = t.inv.count(food)
                    if (n or 0) > 0 then
                        t.player.inv_op(food, 1)
                        t.ticks(3)
                        return
                    end
                end
            end
            -- Walking is real travel; a hazard in the way is crossed with its own click. The walk verb stops short on
            -- a long route ("stalled at"), so ask again until it stands near the target.
            local function travel(label, x, z, via)
                for _, pt in ipairs(via or {}) do
                    t.player.walk_to(pt[1], pt[2], 40)
                    t.ticks(1)
                end
                for _ = 1, 4 do
                    local _, w = t.world.tile()
                    if math.abs(w.x - x) <= 1 and math.abs(w.z - z) <= 1 then break end
                    t.player.walk_to(x, z, 40)
                    t.ticks(1)
                end
                where(label, x, z, 0)
            end
            -- A ring of leaves pressed from its stand tile, walked to (never a goto: the far side is another pocket). A slip
            -- drops the player in the pit; the hand holds put him on the ring's SOUTH tile (regicide_traps.rs2
            -- [oploc1,regicide_trap_hand_holds]), so a southward crossing that slipped has still crossed (far_z).
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, _success_text, via)
                isafdar_cross(t, { travel = travel, eat = eat_if_low, last = last_lines }, name, sym, stand_x, stand_z, at_x, at_z, via)
            end
            -- Isafdar hop lists (reach.py REACH closed-doors between every pair; the header's pockets)
            local RING_TO_TRACKER = { { 2212, 3194 }, { 2219, 3188 }, { 2225, 3183 }, { 2233, 3178 }, { 2236, 3171 }, { 2239, 3168 },
                { 2242, 3161 }, { 2247, 3157 }, { 2253, 3152 } }
            local TRACKER_TO_RING = { { 2240, 3155 }, { 2239, 3165 }, { 2235, 3168 }, { 2232, 3171 }, { 2232, 3177 }, { 2225, 3180 },
                { 2221, 3184 }, { 2219, 3188 }, { 2212, 3194 } }
            local LOG_TO_RING = { { 2205, 3236 }, { 2205, 3224 }, { 2207, 3217 }, { 2209, 3213 } }
            local RING_TO_LOG = { { 2209, 3208 }, { 2205, 3212 }, { 2203, 3220 }, { 2203, 3235 } }
            -- Camp to tracker: the log, the ring of leaves south, then on foot past the spring's west side (the tracker and the
            -- ring share one pocket: reach.py 2209,3201 -> 2257,3150 REACH len=99; the spring leads only east, to the cave).
            local function camp_to_tracker(tag)
                cross_log(t, "crossLogFromCamp" .. tag, 2197, 2201)
                where("crossLogFromCamp" .. tag .. "-tile", 2201, 3237, 0)
                cross("goFromCampToLeavesSouth" .. tag, "regicide_pitfall_side", 2209, 3205, 2209, 3204, "cross safely", LOG_TO_RING)
                travel("walk-toTracker" .. tag, 2257, 3150, RING_TO_TRACKER)
            end

            camp_to_tracker("")

            -- talkToTracker (regicide_camp_tracker.rs2:38): stage spoken_iorwerth, no pendant yet
            t.exec("talkToTracker", t.player.talk_to, "regicide_old_camp_tracker_vis")
            t.exec("talkToTracker-dialog", t.chat.play, {
                "player:Hello", "npc:Human! You must be", "player:No I'm", "npc:And you have something",
                "player:Well... Err", "npc:As I was saying",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_tracker", t.quest.expect_stage(5))

            -- goReturnToIorwerth: back the way we came (spring on foot, the ring north, the log, the camp)
            cross("goReturnToIorwerth", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely", TRACKER_TO_RING)
            travel("walk-goReturnToIorwerth-log", 2201, 3236, RING_TO_LOG)
            cross_log(t, "goReturnToIorwerth-log", 2201, 2196)
            where("goReturnToIorwerth-log-tile", 2196, 3237, 0)
            travel("walk-goReturnToIorwerth-camp", 2203, 3253)
            t.exec("goReturnToIorwerth-talk", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("goReturnToIorwerth-dialog", t.chat.play, {
                "npc:Good day", "player:Your scout refused", "npc:Bless his loyalty", "mesbox:Lord Iorwerth gives you a crystal pendant",
            })
            t.ticks(2)
            t.expect("pendant.held", t.inv.expect_has("regicide_crystal_pendant", 1))

            -- goReturnToTracker: and again to the tracker, to show the pendant (stage shown_pendant)
            camp_to_tracker("-again")
            t.exec("goReturnToTracker", t.player.talk_to, "regicide_old_camp_tracker_vis")
            t.exec("goReturnToTracker-dialog", t.chat.play, {
                "player:Hello", "npc:Human! You must be", "player:No I'm", "npc:And you have something",
                "mesbox:You show the tracker", "npc:That's Lord Iorwerth's pendant",
                "player:I need to find Tyras", "npc:Well this was his old camp", "player:Can I help at all",
                "npc:As it goes", "player:What is?", "npc:Ahh I guess", "npc:I tell you what",
            })
            t.ticks(2)
            t.expect("quest.stage.shown_pendant", t.quest.expect_stage(6))

            -- clickTracks: Follow the footprints west of the camp (regicide_camp_tracker.rs2:31)
            travel("walk-clickTracks", 2243, 3150)
            t.exec("clickTracks", t.player.click_loc, "regicide_old_camp_footprints_vis_op", 1)
            t.ticks(3)
            t.expect("quest.stage.found_footprints", t.quest.expect_stage(7))

            -- goTalkToTrackerAfterTracks: he explains the dense wood (stage spoken_tracker2)
            travel("walk-backToTracker", 2255, 3149)
            t.exec("goTalkToTrackerAfterTracks", t.player.talk_to, "regicide_old_camp_tracker_vis")
            t.exec("goTalkToTrackerAfterTracks-dialog", t.chat.play, {
                "player:I've found tracks", "npc:These forests", "player:Thanks",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_tracker2", t.quest.expect_stage(8))
            t.exec("wield-rune-arrows", t.player.equip, "rune_arrow")
            t.ticks(1)

            -- climbThroughForest (stage 8): three dense-forest locs in a row west of the tracker, o3 2238,3148,
            -- o2 2235,3148, o1 2232,3148 (LostCity quest_regicide.rs2:388-480, regicide_route.rs2); each one crossed
            -- three squares west on z=3149. The guard is summoned on 2231,3149 (LostCity spawn_tyras_guard).
            travel("walk-toForestEdge", 2240, 3149, { { 2248, 3149 } })
            t.exec("climbThroughForest-stage8", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2238, 3148 } })
            t.ticks(6)
            where("climbThroughForest-tile", 2237, 3149, 0)
            t.exec("climbThroughForest-o2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2235, 3148 } })
            t.ticks(6)
            where("climbThroughForest-o2-tile", 2234, 3149, 0)
            t.exec("climbThroughForest-o1", t.player.click_loc, "regicide_cross_over1", 1, { at = { 2232, 3148 } })
            t.ticks(6)
            local _, me = t.world.tile()
            t.check("climbThroughForest-o1-tile", me.x == 2231 and me.z == 3149, "standing at " .. me.x .. "," .. me.z .. " :: " .. last_lines(3))
            t.exec("guard.arrived", t.npc.await_present, "regicide_old_camp_guard", 12, 10)

            -- Protect from Melee for the guard (recipe: verbs-combat.md "Turning on a protection prayer"); a prayed npc
            -- melee hit is 0 (combat_stats.rs2 playerhit_n_melee_apply). Turned off after the kill.
            local function protect_melee(name, want)
                local tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
                local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                local _, pr = t.skill.read("prayer")
                t.check(name, tab_result == "ok" and wr == "ok" and on == want, "varb4118_prayer_protectfrommelee " .. tostring(on)
                    .. " (want " .. want .. "); prayer " .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
            end
            protect_melee("killGuard-protectMelee", 1)
            local _, sharks_at_guard = t.inv.count("shark")

            -- killGuard: a real fight (lvl 110); the quest queues regicide_quest_guard_defeated on its death
            t.exec("killGuard", t.player.attack, "regicide_old_camp_guard", 2, 30)
            local _, guard_detail = t.exec("killGuard-dead", t.npc.await_dead_engaged, 400, 3, { eat = { item = "shark", below = 35 } })
            local guard_lowest = tonumber(tostring(guard_detail):match("lowest hp (%d+)/"))
            local _, sharks_after_guard = t.inv.count("shark")
            t.check("killGuard-margin", (sharks_after_guard or 0) >= 2 or (guard_lowest or 0) > 25,
                "sharks staged 4, at the guard " .. tostring(sharks_at_guard) .. ", eaten in the fight "
                .. tostring((sharks_at_guard or 0) - (sharks_after_guard or 0)) .. ", left " .. tostring(sharks_after_guard)
                .. ", lowest hp in the fight " .. tostring(guard_lowest) .. "/70, guard dead after "
                .. tostring(tostring(guard_detail):match("dead after (%d+) tick")) .. " ticks (margin: sharks left >= 2 or lowest hp > 25)")
            protect_melee("killGuard-prayerOff", 0)
            t.ticks(3)
            t.expect("quest.stage.defeated_guard", t.quest.expect_stage("defeated_guard"))

            -- note: enterTyrasCamp is a position-only step (2190,3144) behind the camp passage regicide_cross_over2_tyras_camp, regicide_route.rs2:72-75 (stage 9 -> 10); it is driven in leg 5 with goKillGuardAtSecondForest
            -- crossTripwire: the tripwire north of the path at 2220,3154 (regicide_traps.rs2:19; pass or snag, both continue)
            travel("walk-toTripwire", 2220, 3152, { { 2228, 3150 }, { 2223, 3151 } })
            t.exec("crossTripwire", t.player.click_loc, "regicide_trap_tripwire", 1)
            t.ticks(6)
            local _, tw = t.world.tile()
            t.check("crossTripwire-tile", tw.z >= 3155, "player " .. tw.x .. "," .. tw.z .. " :: " .. last_lines(3))
            -- A snag poisons at severity 10 (regicide_traps.rs2:23; poison.rs2 [queue,poison_player]): drink the
            -- brought-along antipoison when it did (anti_poison.rs2: %varp102_poison = min(poison, -5) cures it).
            local _, poison_at_wire = t.var.server("varp102_poison")
            local antipoison_note = "not poisoned"
            if (poison_at_wire or 0) > 0 then
                antipoison_note = "drank antipoison: " .. tostring(t.player.inv_op("4doseantipoison", 1))
                t.ticks(3)
            end
            local _, poison_after_wire = t.var.server("varp102_poison")
            local _, hp_wire = t.skill.read("hitpoints")
            local hp_after_wire = type(hp_wire) == "table" and hp_wire.level or nil
            t.check("crossTripwire-poison", (poison_after_wire or 1) <= 0 and (hp_after_wire or 0) > 25,
                "varp102_poison " .. tostring(poison_at_wire) .. " -> " .. tostring(poison_after_wire) .. " (" .. antipoison_note
                .. "), hitpoints " .. tostring(hp_after_wire) .. "/70 (floor 25)")

            local _, w = t.world.tile()
            local _, stage = t.quest.stage()
            local _, sharks = t.inv.count("shark")
            t.check("leg.4.end", true, "tile " .. w.x .. "," .. w.z .. " level " .. w.level .. ", regicide_quest=" .. tostring(stage) .. ", shark x" .. tostring(sharks))
            -- LEG 4 END
        end },
        { name = "camp_and_bomb_ingredients", run = function(t)
            -- LEG 5 BEGIN: goKillGuardAtSecondForest
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function where(label, want_x, want_z, want_level)
                local _, w = t.world.tile()
                local ok = true
                if want_x then ok = math.abs(w.x - want_x) <= 6 and math.abs(w.z - want_z) <= 6 and w.level == want_level end
                t.check(label, ok, "standing at " .. w.x .. "," .. w.z .. " level " .. w.level .. " :: " .. last_lines(3))
            end
            local function at_exact(label, want_x, want_z)
                local _, w = t.world.tile()
                t.check(label, w.x == want_x and w.z == want_z, "at " .. w.x .. "," .. w.z .. "," .. w.level .. " want " .. want_x .. "," .. want_z .. " :: " .. last_lines(3))
            end
            -- Brought-along food only (setup: lobsters and sharks), eaten when the hazards have taken hitpoints below 40.
            local function eat_if_low()
                local hp_result, hp_row = t.skill.read("hitpoints")
                if hp_result ~= "ok" or (hp_row.current or hp_row.level) > 40 then return end
                for _, food in ipairs({ "lobster", "shark" }) do
                    local _, n = t.inv.count(food)
                    if (n or 0) > 0 then
                        t.player.inv_op(food, 1)
                        t.ticks(3)
                        return
                    end
                end
            end
            local travel -- below; a crossing walks to its stand tile (never a goto across a trap)
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, _success_text, via)
                isafdar_cross(t, { travel = travel, eat = eat_if_low, last = last_lines }, name, sym, stand_x, stand_z, at_x, at_z, via)
            end
            -- Walking is real travel; ask again until it stands near the target.
            travel = function(label, x, z, via)
                for _, pt in ipairs(via or {}) do
                    t.player.walk_to(pt[1], pt[2], 40)
                    t.ticks(1)
                end
                for _ = 1, 4 do
                    local _, w = t.world.tile()
                    if math.abs(w.x - x) <= 2 and math.abs(w.z - z) <= 2 then break end
                    t.player.walk_to(x, z, 40)
                    t.ticks(1)
                end
                where(label, x, z, 0)
            end

            -- The bomb chapter's items wait in the bank (setup ::bankgive); they are drawn at Draynor below.

            -- Leg 4 ends poisoned and hurt, just north of the tripwire: eat before the next hazards.
            t.player.inv_op("shark", 1)
            t.ticks(3)
            t.player.inv_op("shark", 1)
            t.ticks(3)

            -- goKillGuardAtSecondForest: "Go through the dense forest north then to the west". The middle passage is the
            -- three dense forests o3 2216,3161 / o2 2216,3164 / o3 2216,3167 (regicide_route.rs2, each crossed three squares
            -- north by the loc's own geometry); the guard at the camp entrance is dealt with below.
            travel("goKillGuardAtSecondForest-walk-toForests", 2217, 3160, { { 2218, 3158 } }) -- on foot from the tripwire pocket, no goto
            t.exec("goKillGuardAtSecondForest-forest1", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3161 } })
            t.ticks(6)
            at_exact("goKillGuardAtSecondForest-forest1-tile", 2217, 3163)
            t.exec("goKillGuardAtSecondForest-forest2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2216, 3164 } })
            t.ticks(6)
            at_exact("goKillGuardAtSecondForest-forest2-tile", 2217, 3166)
            t.exec("goKillGuardAtSecondForest-forest3", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3167 } })
            t.ticks(6)
            at_exact("goKillGuardAtSecondForest-forest3-tile", 2217, 3169)
            travel("goKillGuardAtSecondForest-walk", 2188, 3172, { { 2217, 3173 }, { 2203, 3180 }, { 2188, 3180 } })
            -- note: goKillGuardAtSecondForest the guard at 2188,3170 is regicide_tyras_camp_guard; the quest credits EITHER guard once (regicide_tyras_guard.rs2:11 and :35 queue regicide_quest_guard_defeated, gated on spoken_tracker2 at :52), and leg 4 already killed regicide_old_camp_guard for real, so by stage 9 the camp guard's kill grants nothing and the camp passage does not summon it (regicide_route.rs2:80)

            -- goIntoTyrasCamp: the camp passage, o2_tyras 2187,3169 / o3 2187,3166 / o1_tyras 2187,3163 (stage 9 -> 10)
            t.exec("goIntoTyrasCamp-forest1", t.player.click_loc, "regicide_cross_over2_tyras_camp", 1, { at = { 2187, 3169 } })
            t.ticks(6)
            at_exact("goIntoTyrasCamp-forest1-tile", 2188, 3168)
            local _, camp_stage = t.var.server("varp328_regicide_quest")
            t.check("quest.stage.entered_camp", camp_stage == 10, "regicide_quest=" .. tostring(camp_stage) .. " :: " .. last_lines(2))
            t.exec("goIntoTyrasCamp-forest2", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2187, 3166 } })
            t.ticks(6)
            at_exact("goIntoTyrasCamp-forest2-tile", 2188, 3165)
            t.exec("goIntoTyrasCamp-forest3", t.player.click_loc, "regicide_cross_over1_tyras_camp", 1, { at = { 2187, 3163 } })
            t.ticks(6)
            at_exact("goIntoTyrasCamp-forest3-tile", 2188, 3162)

            -- enterTyrasCamp: the position-only step at 2190,3144, where an empty barrel lies (m34_49.spawn:67-69)
            travel("enterTyrasCamp", 2190, 3146, { { 2189, 3155 } })
            t.exec("enterTyrasCamp-barrel1", t.player.click_obj, "regicide_barrel_empty", 3)
            t.exec("enterTyrasCamp-barrel1-held", t.inv.await, "regicide_barrel_empty", 1, 10)
            t.exec("enterTyrasCamp-barrel2", t.player.click_obj, "regicide_barrel_empty", 3)
            t.exec("enterTyrasCamp-barrel2-held", t.inv.await, "regicide_barrel_empty", 2, 10)

            -- getSulphur: a piece off the shore south of the old camp (regicide_bombcraft.rs2:33, needs stage 10)
            -- getSulphur is east of the tracker: the way back from the camp is the way in, reversed, every crossing clicked.
            travel("getSulphur-back-toCampPassage", 2188, 3162, { { 2189, 3155 } })
            local back_steps = {
                { "getSulphur-back-camp1", "regicide_cross_over1_tyras_camp", 2187, 3163, 2188, 3165 },
                { "getSulphur-back-camp2", "regicide_cross_over3", 2187, 3166, 2188, 3168 },
                { "getSulphur-back-camp3", "regicide_cross_over2_tyras_camp", 2187, 3169, 2188, 3171 },
            }
            for _, s in ipairs(back_steps) do
                t.exec(s[1], t.player.click_loc, s[2], 1, { at = { s[3], s[4] } })
                t.ticks(6)
                at_exact(s[1] .. "-tile", s[5], s[6])
            end
            travel("getSulphur-back-toMiddle", 2217, 3169, { { 2188, 3180 }, { 2203, 3180 }, { 2217, 3173 } })
            local middle_steps = {
                { "getSulphur-back-middle1", "regicide_cross_over3", 2216, 3167, 2217, 3166 },
                { "getSulphur-back-middle2", "regicide_cross_over2", 2216, 3164, 2217, 3163 },
                { "getSulphur-back-middle3", "regicide_cross_over3", 2216, 3161, 2217, 3160 },
            }
            for _, s in ipairs(middle_steps) do
                t.exec(s[1], t.player.click_loc, s[2], 1, { at = { s[3], s[4] } })
                t.ticks(6)
                at_exact(s[1] .. "-tile", s[5], s[6])
            end
            eat_if_low()
            travel("getSulphur-back-toTripwire", 2220, 3155, { { 2218, 3158 } })
            t.exec("getSulphur-back-tripwire", t.player.click_loc, "regicide_trap_tripwire", 1)
            t.ticks(6)
            local _, tw = t.world.tile()
            t.check("getSulphur-back-tripwire-tile", tw.z <= 3153, "player " .. tw.x .. "," .. tw.z .. " :: " .. last_lines(3))
            cure_poison(t, "getSulphur-back-tripwire-antipoison")
            eat_if_low()
            travel("getSulphur-back-toForests", 2231, 3149, { { 2223, 3151 }, { 2228, 3150 } })
            local east_steps = {
                { "getSulphur-back-west1", "regicide_cross_over1", 2232, 3148, 2234, 3149 },
                { "getSulphur-back-west2", "regicide_cross_over2", 2235, 3148, 2237, 3149 },
                { "getSulphur-back-west3", "regicide_cross_over3", 2238, 3148, 2240, 3149 },
            }
            for _, s in ipairs(east_steps) do
                t.exec(s[1], t.player.click_loc, s[2], 1, { at = { s[3], s[4] } })
                t.ticks(6)
                at_exact(s[1] .. "-tile", s[5], s[6])
            end
            travel("getSulphur-walk", 2261, 3133, { { 2248, 3149 }, { 2257, 3150 }, { 2262, 3140 } })
            t.exec("getSulphur", t.player.click_loc, "regicide_sulphar2", 1, { at = { 2261, 3130 } })
            t.exec("getSulphur-held", t.inv.await, "regicide_sulphar", 1, 10)

            -- fill2Barrels: fill both empty barrels from the tar collection (regicide_bombcraft.rs2:53)
            travel("walk-fill2Barrels", 2263, 3129)
            t.exec("fill2Barrels-1", t.player.click_loc, "regicide_tar_collection", 1, { at = { 2263, 3127 } })
            t.exec("fill2Barrels-1-held", t.inv.await, "regicide_barrel_tar", 1, 10)
            t.exec("fill2Barrels-2", t.player.click_loc, "regicide_tar_collection", 1, { at = { 2263, 3127 } })
            t.exec("fill2Barrels-2-held", t.inv.await, "regicide_barrel_tar", 2, 10)

            -- goToIorwerthAfterCamp: back the way leg 4 came (tracker, the spring on foot, the ring north, the log, the camp)
            -- the tracker's pocket reaches the ring without the spring (header; reach.py 2257,3150 -> 2209,3201 REACH len=99)
            travel("goToIorwerthAfterCamp-walk-toTracker", 2257, 3150, { { 2262, 3140 } })
            cross("goToIorwerthAfterCamp-ring", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely",
                { { 2240, 3155 }, { 2239, 3165 }, { 2235, 3168 }, { 2232, 3171 }, { 2232, 3177 }, { 2225, 3180 }, { 2221, 3184 }, { 2219, 3188 }, { 2212, 3194 } })
            travel("walk-goToIorwerthAfterCamp-log", 2201, 3236, { { 2209, 3208 }, { 2205, 3212 }, { 2203, 3220 }, { 2203, 3235 } })
            cross_log(t, "goToIorwerthAfterCamp-log", 2201, 2196)
            where("goToIorwerthAfterCamp-log-tile", 2196, 3237, 0)
            travel("walk-goToIorwerthAfterCamp-camp", 2203, 3253)
            t.exec("goToIorwerthAfterCamp-talk", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("goToIorwerthAfterCamp-dialog", t.chat.play, {
                "npc:how goes your search", "player:I've finally tracked", "npc:Good job", "npc:I have this book",
                "player:Well that should", "npc:Indeed", "mesbox:Lord Iorwerth gives you a book",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_iorwerth2", t.quest.expect_stage(11))
            t.expect("book.held", t.inv.expect_has("regicide_alchemy", 1))

            -- readBigBookOfBangs: Read the book (regicide_alchemy.rs2:8), five pages
            t.exec("readBigBookOfBangs", t.player.inv_op, "regicide_alchemy", 1)
            t.exec("readBigBookOfBangs-pages", t.chat.drain, { max_pages = 20 }) -- five mesbox pages, each split into a heading and a body page
            local _, read_flag = t.var.varbit("varb8453_regicide_read_book")
            t.check("readBigBookOfBangs-flag", read_flag == 1, "varb8453_regicide_read_book=" .. tostring(read_flag))

            -- The five questions Iorwerth answers (lord_iorwerth.rs2:119-192; Regicide.java knowHowToMakeBomb needs every flag).
            -- One conversation: menu A holds quicklime / sulphur / naphtha, "More options..." holds the barrel and the fuse.
            t.exec("askAboutQuicklime", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("askAboutQuicklime-dialog", t.chat.play, {
                "player:Hello", "npc:Have you had any luck", "options", "choose:I need some quicklime.",
                "player:I need some quicklime", "npc:Quicklime?", "player:Apparently", "npc:Ah, I see", "npc:Anyway, is there anything else",
            })
            t.ticks(2)
            local _, quicklime_chat = t.var.varbit("varb8458_regicide_quicklime_chat")
            t.check("askAboutQuicklime-flag", quicklime_chat == 1, "varb8458_regicide_quicklime_chat=" .. tostring(quicklime_chat))
            t.exec("askAboutSulphur", t.chat.play, {
                "options", "choose:I need some sulphur.", "player:I need some sulphur", "npc:Check the shore", "player:Sounds good",
                "npc:Anyway, is there anything else",
            })
            t.ticks(2)
            local _, sulphur_chat = t.var.varbit("varb8457_regicide_sulphur_chat")
            t.check("askAboutSulphur-flag", sulphur_chat == 1, "varb8457_regicide_sulphur_chat=" .. tostring(sulphur_chat))
            t.exec("askAboutNaphtha", t.chat.play, {
                "options", "choose:I need some naphtha.", "player:I need some naphtha", "npc:Naphtha?", "player:According to the book",
                "npc:Well you should be able", "player:Hmm", "npc:Perfect", "npc:Anyway, is there anything else",
            })
            t.ticks(2)
            local _, naphtha_chat = t.var.varbit("varb8459_regicide_naphtha_chat")
            t.check("askAboutNaphtha-flag", naphtha_chat == 1, "varb8459_regicide_naphtha_chat=" .. tostring(naphtha_chat))
            t.exec("askAboutBarrel", t.chat.play, {
                "options", "choose:More options...", "options", "choose:I need a barrel.", "player:I need a barrel",
                "npc:Have a look around the camp", "player:Will do", "npc:Is there anything else",
            })
            t.ticks(2)
            local _, barrel_chat = t.var.varbit("varb8456_regicide_barrel_chat")
            t.check("askAboutBarrel-flag", barrel_chat == 1, "varb8456_regicide_barrel_chat=" .. tostring(barrel_chat))
            t.exec("askAboutFuse", t.chat.play, {
                "options", "choose:More options...", "options", "choose:I need a fuse.", "player:I need a fuse",
                "npc:Some sort of fabric", "npc:Is there anything else",
            })
            t.chat.close()
            t.ticks(2)
            local _, fuse_chat = t.var.varbit("varb8455_regicide_fuse_chat")
            t.check("askAboutFuse-flag", fuse_chat == 1, "varb8455_regicide_fuse_chat=" .. tostring(fuse_chat))

            -- useLimestoneOnFurnace: ANY furnace burns limestone (smelting.rs2:81-84 -> regicide_bombcraft.rs2:91, stage 11),
            -- gloved so the quicklime does not burn the hands. The furnace driven is Lumbridge's (reached below).
            -- Out of Tirannwn by Lumbridge Teleport from Iorwerth's camp (header).
            lumbridge_teleport(t, "useLimestoneOnFurnace.lumbridgeTeleport", "Lumbridge, out of Iorwerth's camp in Tirannwn")
            -- Draynor bank (open doorway, bankdoor_*_inactive; demon.lua's route: reach.py 3221,3218 -> 3097,3246 REACH
            -- len=180): the left-over food goes in (no hazard before leg 6, which draws its own), the bomb chapter's items
            -- come out. The book stays: the Chemist answers "Your quest." only while it is held (chemist.rs2
            -- [opnpc1,chemist] inv_total(inv, regicide_alchemy) > 0). The gloves are drawn and worn first so the nine
            -- coal fit (28 slots).
            local function bank_open(name)
                t.exec(name, t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
            end
            t.exec("goto-useLimestoneOnFurnace.draynorBank", t.player.goto_tile, 3097, 3246, 0)
            bank_open("useLimestoneOnFurnace.bankOpen")
            for _, food in ipairs({ "lobster", "shark" }) do
                local _, held = t.inv.count(food)
                if (held or 0) > 0 then t.exec("useLimestoneOnFurnace.deposit." .. food, t.bank.deposit, food, "all") end
            end
            t.exec("useLimestoneOnFurnace.withdraw.gloves", t.bank.withdraw, "leather_gloves", 1)
            t.check("useLimestoneOnFurnace.bankClose", t.bank.close())
            t.exec("wear-gloves", t.player.equip, "leather_gloves")
            t.ticks(1)
            bank_open("useLimestoneOnFurnace.bankOpen2")
            t.exec("useLimestoneOnFurnace.withdraw.limestone", t.bank.withdraw, "limestone", 1)
            t.exec("useLimestoneOnFurnace.withdraw.pestle", t.bank.withdraw, "pestle_and_mortar", 1)
            t.exec("useLimestoneOnFurnace.withdraw.pot", t.bank.withdraw, "pot_empty", 1)
            t.exec("useLimestoneOnFurnace.withdraw.cloth", t.bank.withdraw, "regicide_cloth", 1)
            t.exec("useLimestoneOnFurnace.withdraw.coal", t.bank.withdraw, "coal", 9)
            t.check("useLimestoneOnFurnace.bankClose2", t.bank.close())
            t.exec("useLimestoneOnFurnace.walkOut", t.player.walk_route, { { 3094, 3246 }, { 3097, 3246 } })
            t.exec("leg5.pack", t.inv.await_all, { limestone = 1, pestle_and_mortar = 1, pot_empty = 1, regicide_cloth = 1, coal = 9 }, 10)
            -- overland to the street south of the Lumbridge smithy (reach.py 3097,3246 -> 3226,3250 REACH len=165); its
            -- doorway is open and the use walks in
            t.exec("goto-useLimestoneOnFurnace", t.player.goto_tile, 3226, 3250, 0)
            local furnace_result, furnace_target = t.world.loc_near("fai_falador_furnace", 12)
            t.check("useLimestoneOnFurnace-locate", furnace_result == "ok", "world.loc_near(fai_falador_furnace,12) -> " .. tostring(furnace_result))
            t.exec("useLimestoneOnFurnace", t.player.use_on, "limestone", furnace_target)
            t.exec("useLimestoneOnFurnace-held", t.inv.await, "regicide_quicklime", 1, 10)

            -- usePestleOnQuicklime / usePestleOnSulphur: grind_ingredient.rs2:84 and :91 (a pot is consumed by the quicklime)
            t.exec("usePestleOnQuicklime", t.player.use_item_on_item, "regicide_quicklime", "pestle_and_mortar")
            t.exec("usePestleOnQuicklime-held", t.inv.await, "regicide_quicklime_dust", 1, 10)
            t.exec("usePestleOnSulphur", t.player.use_item_on_item, "regicide_sulphar", "pestle_and_mortar")
            t.exec("usePestleOnSulphur-held", t.inv.await, "regicide_sulphar_dust", 1, 10)

            -- talkToChemist: Rimmington, "Your quest." (chemist.rs2, biohazard_complete arm, stage 11 with the book)
            -- Rimmington overland (reach.py 3226,3250 -> 2932,3216 REACH), the Chemist's door in (biohazard.lua's crossing)
            t.exec("goto-talkToChemist", t.player.goto_tile, 2932, 3216, 0)
            t.exec("talkToChemist.doorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2932, 3214, 0 }, near = { 2932, 3215 }, far = { 2932, 3212 } })
            t.exec("talkToChemist", t.player.talk_to, "chemist")
            t.exec("talkToChemist-dialog", t.chat.play, {
                "options", "choose:Your quest.", "player:Good day. I was hoping", "npc:Ah, you'll be wanting",
                "player:How do I use it", "npc:It's quite simple", "npc:You must also", "player:Is that all",
                "npc:You'll also need plenty", "player:I see, thanks",
            })
            t.ticks(2)
            local _, chemist_chat = t.var.varbit("varb8449_regicide_chemist_chat")
            t.check("talkToChemist-flag", chemist_chat == 1, "varb8449_regicide_chemist_chat=" .. tostring(chemist_chat))
            -- out the way in: the still stands outside the house (Quest Helper "the still outside the Chemist's house")
            t.exec("talkToChemist.doorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2932, 3214, 0 }, near = { 2932, 3213 }, far = { 2932, 3216 } })

            local _, w = t.world.tile()
            local _, stage = t.quest.stage()
            local _, tar = t.inv.count("regicide_barrel_tar")
            local _, dust = t.inv.count("regicide_quicklime_dust")
            local _, sdust = t.inv.count("regicide_sulphar_dust")
            t.check("leg.5.end", true, "tile " .. w.x .. "," .. w.z .. " level " .. w.level .. ", regicide_quest=" .. tostring(stage)
                .. ", barrel_tar x" .. tostring(tar) .. ", quicklime_dust x" .. tostring(dust) .. ", sulphar_dust x" .. tostring(sdust))
            -- LEG 5 END
        end },
        { name = "bomb_catapult_and_report", run = function(t)
            -- LEG 6 BEGIN: useTarOnFractionalisingStill
            -- Drawn at Draynor in leg 5: coal for the still's heat and the strip of cloth for the fuse.
            t.exec("leg6.pack", t.inv.await_all, { regicide_cloth = 1, coal = 9 }, 10)
            local _, coal_n = t.inv.count("coal")

            t.drive.camera(0, 383, 600)
            -- leg 5 left the player on the Chemist's doorstep (2932,3216); the still is outside the house and the use walks
            -- to it (reach.py 2932,3216 -> 2927,3212 REACH len=9)
            t.ui.tab("inventory")
            t.ticks(2)
            local still_target = t.player.by_symbol("loc", "regicide_fractionalizing_still")
            t.exec("useTarOnFractionalisingStill", t.player.use_on, "regicide_barrel_tar", still_target)
            local open_result = t.ui.await_open("regicide_still")
            t.check("operateStill-open", open_result == "ok", "ui.await_open(regicide_still) -> " .. tostring(open_result))

            -- operateStill: every control on interface 286 is an IF1 graphic button (op 0, trap 33). Tar valve to
            -- the top, back the pressure off once the flow climbs, and feed coal whenever the heat needle drops below
            -- the green band (bits 13-25 of varp331_regicide_still_settings), reading the server after each press.
            local _, tar_up = t.ui.widget("regicide_still:regicide_tar_valve_up")
            local _, pressure_up = t.ui.widget("regicide_still:regicide_pressure_valve_up")
            local _, add_coal = t.ui.widget("regicide_still:regicide_add_coal")
            t.ui.invoke(tar_up, 0)
            t.ui.invoke(tar_up, 0)
            local coal_presses, pressure_presses, total, polls = 0, 0, 0, 0
            while polls < 40 and total < 26 do
                polls = polls + 1
                local settings_r, settings = t.var.server("varp331_regicide_still_settings")
                if settings_r == "ok" and settings ~= nil then
                    if settings < 0 then settings = settings + 4294967296 end
                    local function bit(n) return math.floor(settings / (2 ^ n)) % 2 == 1 end
                    local tar_at_max = bit(31)
                    local pressure_at_base = bit(26)
                    local flow_high = bit(10) or bit(11) or bit(12)
                    local heat_below_green = bit(13) or bit(14) or bit(15) or bit(16) or bit(17) or bit(18)
                    if not tar_at_max then t.ui.invoke(tar_up, 0) end
                    if tar_at_max and pressure_at_base and flow_high then
                        t.ui.invoke(pressure_up, 0)
                        pressure_presses = pressure_presses + 1
                    end
                    if heat_below_green and coal_presses < coal_n then
                        t.ui.invoke(add_coal, 0)
                        coal_presses = coal_presses + 1
                    end
                end
                t.ticks(2)
                local total_r, total_v = t.var.server("varp330_regicide_still_total")
                if total_r == "ok" and total_v ~= nil then total = total_v end
            end
            t.check("operateStill", total >= 26, string.format("varp330_regicide_still_total=%s after %d poll(s), %d coal, %d pressure press(es)",
                tostring(total), polls, coal_presses, pressure_presses))
            t.key("escape") -- the close icon fires but never unmounts the panel; Escape is the player's own close
            local closed = t.ui.await_close("regicide_still")
            t.check("operateStill-closed", closed == "ok", "ui.await_close(regicide_still) -> " .. tostring(closed))
            t.expect("operateStill-naphtha", t.inv.await("regicide_barrel_naphtha", 1, 10))

            -- useQuicklimeOnNaphtha / useGroundSulphurOnNaphtha: regicide_bombcraft.rs2:156-192
            t.ui.tab("inventory")
            t.ticks(1)
            t.exec("useQuicklimeOnNaphtha", t.player.use_item_on_item, "regicide_quicklime_dust", "regicide_barrel_naphtha")
            t.expect("useQuicklimeOnNaphtha-held", t.inv.await("regicide_barrel_naphtha_quicklime_mix", 1, 10))
            t.exec("useGroundSulphurOnNaphtha", t.player.use_item_on_item, "regicide_sulphar_dust", "regicide_barrel_naphtha_quicklime_mix")
            t.expect("useGroundSulphurOnNaphtha-held", t.inv.await("regicide_barrel_lid", 1, 10))
            t.exec("useClothOnBarrelBomb", t.player.use_item_on_item, "regicide_cloth", "regicide_barrel_lid")
            t.expect("useClothOnBarrelBomb-held", t.inv.await("regicide_barrel_lid_fused", 1, 10))

            -- goThroughUndergroundPassAgain: the guide sends the player back through the pass, every obstacle again
            -- (the loc triggers are the ones legs 1-3 drove). A plain row per obstacle, named with -again.
            local function last_lines(n)
                local _, lines = t.msg.last(n)
                local out = {}
                for _, line in ipairs(lines or {}) do out[#out + 1] = tostring(type(line) == "table" and line.text or line) end
                return table.concat(out, " | ")
            end
            local function where(label, want_x, want_z, want_level)
                local _, w = t.world.tile()
                local ok = true
                if want_x then ok = math.abs(w.x - want_x) <= 6 and math.abs(w.z - want_z) <= 6 and w.level == want_level end
                t.check(label, ok, "standing at " .. w.x .. "," .. w.z .. " level " .. w.level .. " :: " .. last_lines(3))
            end
            local function at_exact(label, want_x, want_z)
                local _, w = t.world.tile()
                t.check(label, w.x == want_x and w.z == want_z, "at " .. w.x .. "," .. w.z .. "," .. w.level .. " want " .. want_x .. "," .. want_z .. " :: " .. last_lines(3))
            end
            -- Brought-along food only (setup: lobsters and sharks), eaten when the hazards have taken hitpoints below 40.
            local function eat_if_low()
                local hp_result, hp_row = t.skill.read("hitpoints")
                if hp_result ~= "ok" or (hp_row.current or hp_row.level) > 40 then return end
                for _, food in ipairs({ "lobster", "shark" }) do
                    local _, n = t.inv.count(food)
                    if (n or 0) > 0 then
                        t.player.inv_op(food, 1)
                        t.ticks(3)
                        return
                    end
                end
            end
            local travel -- defined below; crossings walk to their stand tile
            local function cross(name, sym, stand_x, stand_z, at_x, at_z, _success_text, via)
                isafdar_cross(t, { travel = travel, eat = eat_if_low, last = last_lines }, name, sym, stand_x, stand_z, at_x, at_z, via)
            end
            travel = function(label, x, z, via)
                for _, pt in ipairs(via or {}) do
                    t.player.walk_to(pt[1], pt[2], 40)
                    t.ticks(1)
                end
                for _ = 1, 4 do
                    local _, w = t.world.tile()
                    if math.abs(w.x - x) <= 2 and math.abs(w.z - z) <= 2 then break end
                    t.player.walk_to(x, z, 40)
                    t.ticks(1)
                end
                where(label, x, z, 0)
            end

            -- the spare coal and the empty barrel would crowd the pack for the pass's items
            for _ = 1, 9 do t.player.drop("coal") t.ticks(1) end
            t.player.drop("regicide_barrel_empty")
            t.ticks(1)
            t.exec("leg6.pack-lean", t.inv.count, "coal")
            -- Draynor bank again on the way north (reach.py 2932,3216 -> 3097,3246 REACH len=257): the catapult guard's cooked
            -- rabbit and the rope for the pit's second swing (the first walk's two ropes were spent)
            t.exec("goto-enterTheDungeon-again.draynorBank", t.player.goto_tile, 3097, 3246, 0)
            t.exec("enterTheDungeon-again.bankOpen", t.bank.open, "bankbooth", 2, { at = { 3091, 3243 } })
            t.exec("enterTheDungeon-again.withdraw.rabbit", t.bank.withdraw, "cooked_rabbit", 1)
            t.exec("enterTheDungeon-again.withdraw.rope", t.bank.withdraw, "rope", 1)
            t.exec("enterTheDungeon-again.withdraw.food", t.bank.withdraw, "shark", 8)
            t.check("enterTheDungeon-again.bankClose", t.bank.close())
            t.exec("enterTheDungeon-again.walkOut", t.player.walk_route, { { 3094, 3246 }, { 3097, 3246 } })
            t.exec("leg6.rope", t.inv.await_all, { rope = 1, cooked_rabbit = 1, shark = 8 }, 10)

            -- enterTheDungeon (again): Draynor -> the members' gate (reach.py 3097,3246 -> 2934,3318 REACH len=237), the
            -- castle street, the West Ardougne city doors and the cave mouth (header), then upass_entrance.rs2:9
            members_gate_north(t, "enterWestArdougne-again")
            street_to_cave_mouth(t, "enterWestArdougne-again")
            t.exec("enterTheDungeon-again", t.player.click_loc, "upass_caveentrance2", 1)
            t.ticks(6)
            local _, at = t.world.tile()
            t.check("enterTheDungeon-again-tile", at.z > 9000, "underground at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(2))

            local function other_side_lines()
                local _, lines = t.msg.last(12)
                local n = 0
                for _, line in ipairs(lines or {}) do
                    if string.find(tostring(type(line) == "table" and line.text or line), "step down the other side", 1, true) then n = n + 1 end
                end
                return n
            end
            local function climb(name, slide_x, slide_z)
                local crossed = false
                local detail = ""
                for attempt = 1, 8 do
                    local before = other_side_lines()
                    local result = t.player.click_loc("rockslide2_obstacle_upass", 1, { at = { slide_x, slide_z } })
                    t.ticks(8)
                    local _, here = t.world.tile()
                    detail = "attempt " .. attempt .. ": " .. tostring(result) .. " now at " .. here.x .. "," .. here.z
                    if other_side_lines() > before then
                        crossed = true
                        detail = detail .. " (server: ...and step down the other side)"
                        break
                    end
                end
                t.check(name, crossed, detail)
            end
            climb("climbOverRockslide1-again", 2480, 9713)
            t.exec("goto-climbOverRockslide2-again", t.player.goto_tile, 2473, 9706, 0)
            climb("climbOverRockslide2-again", 2471, 9706)
            climb("climbOverRockslide3-again", 2458, 9712)

            -- searchBagForCloth, useClothOnArrow, lightArrow, shootBridgeRope (again)
            t.exec("goto-searchBagForCloth-again", t.player.goto_tile, 2453, 9716, 0) -- beside the bag (2452,9715 is under upass_gear)
            t.exec("searchBagForCloth-again", t.player.click_loc, "upass_gear", 1)
            t.ticks(4)
            local crossed = false
            for attempt = 1, 5 do
                local sfx = attempt == 1 and "-again" or ("-again-retry" .. attempt)
                if attempt > 1 then
                    t.exec("goto-searchAgain" .. sfx, t.player.goto_tile, 2453, 9716, 0)
                    t.exec("searchBagForCloth" .. sfx, t.player.click_loc, "upass_gear", 1)
                    t.ticks(4)
                end
                t.exec("useClothOnArrow" .. sfx, t.player.use_item_on_item, "damp_cloth", "bronze_arrow")
                t.ticks(2)
                t.exec("lightArrow" .. sfx, t.player.use_item_on_item, "tinderbox", "unlitarrow")
                t.ticks(2)
                t.exec("wieldLitArrow" .. sfx, t.player.equip, "litarrow")
                t.ticks(2)
                t.exec("goto-walkNorthEastOfBridge" .. sfx, t.player.goto_tile, 2450, 9722, 0)
                t.exec("shootBridgeRope" .. sfx, t.player.click_loc, "oldbridge_guiderope", 1)
                for _poll = 1, 12 do
                    t.ticks(4)
                    _, at = t.world.tile()
                    if at.x < 2444 then break end
                end
                _, at = t.world.tile()
                if at.x < 2444 then crossed = true break end
            end
            t.check("shootBridgeRope-again-crossed", crossed, "after the shot at " .. at.x .. "," .. at.z .. " level " .. at.level .. " :: " .. last_lines(4))

            -- crossThePit (again): rope on the rock (upass_obstacles.rs2:110). The swing's roll is stat_random(agility, 100,
            -- 410): at Agility 56 that is 100 + 310*55/98 = 274 of 256, never a fall, so one rope and one press.
            t.exec("goto-crossThePit-again", t.player.goto_tile, 2461, 9699, 0)
            local rock = t.player.by_symbol("loc", "obstical_rockswing_norope")
            t.exec("crossThePit-again", t.player.use_on, "rope", rock)
            t.ticks(10)
            local _, swing_at = t.world.tile()
            t.check("crossThePit-again-crossed", swing_at.x >= 2464, "after the swing at " .. swing_at.x .. "," .. swing_at.z .. " :: " .. last_lines(4))
            climb("climbOverRockslide4-again", 2491, 9691)
            climb("climbOverRockslide5-again", 2482, 9679)
            -- crossTheGrid (again): the safe bands come from %varp6010_upass_grid_pattern (upass_grid.rs2:72-96)
            do
                local _, pattern = t.var.server("varp6010_upass_grid_pattern")
                pattern = tonumber(pattern) or 0
                if pattern == 0 then
                    -- (no mid-run grid-pattern seed: the ::complete arm rolled it; leg 3/6 read %varp6010_upass_grid_pattern)
                    t.ticks(2)
                    _, pattern = t.var.server("varp6010_upass_grid_pattern")
                    pattern = tonumber(pattern) or 0
                end
                local d1, d2, d3 = math.floor(pattern / 100) % 10, math.floor(pattern / 10) % 10, pattern % 10
                local function band_z(d) return 9673 + 2 * (d - 1) end
                t.check("crossTheGrid-again-pattern", d1 >= 1 and d1 <= 5 and d2 >= 1 and d3 >= 1, "upass_grid_pattern=" .. tostring(pattern) .. " -> safe bands z " .. band_z(d1) .. " / " .. band_z(d2) .. " / " .. band_z(d3))
                local grid_path = { { 2478, band_z(d1) }, { 2473, band_z(d1) }, { 2473, band_z(d2) }, { 2469, band_z(d2) }, { 2469, band_z(d3) }, { 2467, band_z(d3) }, { 2466, band_z(d3) } }
                local grid_trail = {}
                for _, wp in ipairs(grid_path) do
                    t.player.walk_to(wp[1], wp[2], 14)
                    t.ticks(2)
                    local _, here = t.world.tile()
                    grid_trail[#grid_trail + 1] = here.x .. "," .. here.z
                end
                local _, gridat = t.world.tile()
                t.check("crossTheGrid-again", gridat.x <= 2467 and not string.find(last_lines(6), "trap", 1, true), "walked the safe bands " .. table.concat(grid_trail, " > ") .. " -> " .. gridat.x .. "," .. gridat.z .. " :: " .. last_lines(4))
                t.player.walk_to(2466, 9673, 10)
                t.ticks(2)
                local _, leverat = t.world.tile()
                t.check("walk-pullLeverAfterGrid-again", leverat.x == 2466 and leverat.z <= 9674, "walked south along x 2466 to " .. leverat.x .. "," .. leverat.z)
            end
            t.exec("pullLeverAfterGrid-again", t.player.click_loc, "portcullis_lever_up", 1)
            t.ticks(8)
            do
                local _, lv = t.world.tile()
                t.check("pullLeverAfterGrid-again-tile", lv.level == 0 and lv.x < 2465, "after the lever at " .. lv.x .. "," .. lv.z .. " level " .. lv.level .. " (through the portcullis: west of x 2465) :: " .. last_lines(4))
            end

            local traps = {
                -- name, stand x, z, trap loc x (the player must end west of it: upass_speartrap sits on the corridor)
                { "passTrap1-again", 2445, 9677, 2443 }, { "passTrap2-again", 2442, 9677, 2440 },
                { "passTrap3-again", 2436, 9675, 2435 }, { "passTrap4-again", 2434, 9675, 2432 }, { "passTrap5-again", 2433, 9675, 2430 },
            }
            local failures = 0
            for _, trap in ipairs(traps) do
                local name, sx, sz, trap_x = trap[1], trap[2], trap[3], trap[4]
                local passed = false
                for attempt = 1, 12 do
                    if attempt == 1 then
                        -- on foot down the corridor first (a walk_to the stand tile stops beside the trap, which is close enough),
                        -- so the press has the trap on screen; the walk is travel, the press is the row
                        t.player.walk_to(sx, sz, 30)
                        t.exec(name, t.player.click_loc, "upass_speartrap", 1, { at = { trap_x, sz } })
                        t.ticks(2)
                        t.exec(name .. "-dialog", t.chat.play, { "mesbox:The markings appear", "choose:/give it a go/" })
                    else
                        t.player.click_loc("upass_speartrap", 1, { at = { trap_x, sz } })
                        t.ticks(2)
                        t.chat.play({ "mesbox:The markings appear", "choose:/give it a go/" })
                    end
                    t.ticks(6)
                    local _, here = t.world.tile()
                    if here.x < trap_x then passed = true break end
                    failures = failures + 1
                    if failures % 3 == 0 then
                        eat_if_low()
                    end
                end
                local _, here = t.world.tile()
                t.check(name .. "-tile", passed, "standing at " .. here.x .. "," .. here.z .. " level " .. here.level .. " :: " .. last_lines(3))
            end

            -- climbDownWell, pickCellLock, digMud, crossLedge, goThroughPipe, leaveUnicornArea, openIbansDoor (again)
            -- The plank room is not on the way: the woodplank collected in leg 2 is still in the pack (the ledge keeps it), so the
            -- player walks from the last spear trap straight west to the well (plain travel, no obstacle between).
            t.expect("leg6.plank-kept", t.inv.expect_has("woodplank", 1))
            for _ = 1, 4 do
                t.player.walk_to(2417, 9677, 40)
                local _, hw = t.world.tile()
                if math.abs(hw.x - 2417) <= 1 and math.abs(hw.z - 9677) <= 1 then break end
            end
            where("walkToWell-again-tile", 2417, 9677, 0)
            t.exec("climbDownWell-again", t.player.click_loc, "cave_well", 1)
            t.ticks(6)
            where("climbDownWell-again-tile", 2423, 9660, 0)
            t.player.walk_to(2410, 9656, 60)
            t.player.walk_to(2393, 9655, 60)
            where("walkToCell-again-tile", 2393, 9655, 0)
            -- each pick is stat_random(thieving,128,400) (upass_unicorn.rs2:13), ~50% at Thieving 1: sixteen presses, then a FAIL
            local picked, picks = false, 0
            for attempt = 1, 16 do -- two railings stand on x 2393 (z 9656 then z 9655); each pick can fail
                picks = attempt
                local _, before = t.world.tile()
                t.player.click_loc("cave_railings2", 1, { at = { 2393, before.z >= 9657 and 9656 or 9655 } }) -- upass_unicorn.rs2:11
                t.ticks(8)
                local _, after = t.world.tile()
                if after.z <= 9654 then picked = true end
                if picked then break end
            end
            do
                local _, cell = t.world.tile()
                t.check("pickCellLock-again", picked and cell.level == 0 and cell.x == 2393 and cell.z <= 9654, "through the cell lock after " .. picks
                    .. " press(es): standing at " .. cell.x .. "," .. cell.z .. " level " .. cell.level .. " (south of the railings, z <= 9654) :: " .. last_lines(3))
            end
            t.ticks(1)
            local mud = t.player.by_symbol("loc", "upass_mud")
            t.exec("digMud-again", t.player.use_on, "spade", mud)
            t.ticks(8)
            where("digMud-again-tile", 2392, 9646, 0)
            t.player.walk_to(2376, 9644, 40)
            t.exec("crossLedge-again", t.player.click_loc, "upass_ledge", 1)
            t.ticks(8)
            where("crossLedge-again-tile", 2374, 9638, 0)
            do
            local function mz_here() local _, w = t.world.tile() return w end
            maze_cross(t, "navigateMaze-again-bridge", eat_if_low)
            for _, hop in ipairs({ { 2421, 9637 }, { 2422, 9634 }, { 2422, 9610 }, { 2421, 9606 }, { 2419, 9605 } }) do
                t.player.walk_to(hop[1], hop[2], 40)
            end
            where("navigateMaze-again-pipeMouth", 2419, 9605, 0)
            local piped = false
            for _ = 1, 4 do
                t.player.click_loc("upass_pipe6", 1, { at = { 2417, 9605 } })
                t.ticks(16)
                if mz_here().x < 2395 then piped = true break end
            end
            local pw = mz_here()
            t.check("goThroughPipe-again", piped and math.abs(pw.x - 2387) <= 3 and pw.z == 9605, "after the pipe at " .. pw.x .. "," .. pw.z .. " level " .. pw.level)
            for _, hop in ipairs({ { 2378, 9605 }, { 2378, 9607 }, { 2375, 9607 }, { 2375, 9610 } }) do
                t.player.walk_to(hop[1], hop[2], 30)
            end
            where("goto-leaveUnicornArea-again-walk", 2375, 9610, 0)
            t.exec("leaveUnicornArea-again", t.player.click_loc, "upass_unicorn_doorl", 1, { at = { 2375, 9611 } })
            t.ticks(6)
            where("leaveUnicornArea-again-tile", 2371, 9666, 0)
            local door_trail = {}
            for _, hop in ipairs({ {3,14}, {5,25}, {10,30}, {10,33}, {20,36}, {20,40}, {40,40}, {40,42}, {55,43}, {56,52}, {56,57},
                    {45,57}, {31,57}, {25,58}, {22,57}, {20,55}, {10,55}, {1,54} }) do
                local hx, hz = hop[1] + 2368, hop[2] + 9664
                for attempt = 1, 4 do
                    local wr = t.player.walk_to(hx, hz, 50)
                    local w = mz_here()
                    if wr == "ok" and w.x == hx and w.z == hz then break end
                    if attempt == 4 then door_trail[#door_trail + 1] = "STALL " .. hx .. "," .. hz .. " at " .. w.x .. "," .. w.z end
                end
            end
            t.note("walk " .. table.concat(door_trail, " ## "))
            where("walkToIbansDoor-again", 2369, 9718, 0)
            t.exec("openIbansDoor-again", t.player.click_loc, "cavetempledoor2r", 1)
            t.ticks(6)
            where("openIbansDoor-again-tile", 2173, 4725, 1)
            -- Iban's four bridges again, each fall walked back up and round (iban_bridges)
            if not iban_bridges(t, "-again", eat_if_low) then return end
            t.player.walk_to(2147, 4648, 20)
            where("walkToTemple-again", 2147, 4648, 1)
            t.exec("enterTemple-again", t.player.click_loc, "upass_templedoor_closed_right", 1, { at = { 2143, 4648 } })
            t.ticks(4)
            where("enterTemple-again-tile", 2014, 4712, 1)
            end
            t.exec("enterWell-again", t.player.click_loc, "regicide_voyage_temple_well1", 1)
            t.ticks(6)
            where("enterWell-again-tile", 2343, 9622, 0)
            t.exec("goto-leaveWellCave-again", t.player.goto_tile, 2315, 9624, 0)
            t.exec("leaveWellCave-again", t.player.click_loc, "regicide_voyage_temple_exit", 1)
            t.ticks(6)
            where("leaveWellCave-again-tile", 2312, 3216, 0)
            local _, arrived = t.world.tile()
            t.check("goThroughUndergroundPassAgain", arrived.level == 0 and math.abs(arrived.x - 2312) <= 6 and math.abs(arrived.z - 3216) <= 6,
                "second walk of the pass ended in Tirannwn at " .. arrived.x .. "," .. arrived.z .. " level " .. arrived.level .. " after the rockslides, bridge, pit, grid, lever, five spear traps, well, cell lock, mud, ledge, maze, pipe, unicorn door, Iban's door, bridges, temple well and exit")

            -- Tirannwn again, all on foot: the ring of leaves, then the tracker's dense forests west (stage 11), the tripwire
            -- north, the three middle forests north and the camp road (regicide_traps.rs2, regicide_route.rs2).
            cross("goFromCaveToLeaves-again", "regicide_pitfall_side", 2267, 3205, 2267, 3204, "cross safely",
                { { 2302, 3212 }, { 2298, 3209 }, { 2287, 3207 }, { 2271, 3211 }, { 2269, 3207 } })
            -- the spring west (the ring's pocket has no other way to the tracker's forests: header), then on foot
            cross("goFromLeavesToStickTrap-again", "regicide_trap_woodspring", 2238, 3181, 2235, 3181, "skillfully pass",
                { { 2262, 3193 }, { 2259, 3186 }, { 2239, 3186 } })
            where("goFromLeavesToStickTrap-again-tile", 2234, 3181, 0)
            travel("walk-climbThroughForest-again", 2240, 3149, { { 2233, 3173 }, { 2239, 3168 } })
            local west_steps = {
                { "climbThroughForest-again-o3", "regicide_cross_over3", 2238, 3148, 2237, 3149 },
                { "climbThroughForest-again-o2", "regicide_cross_over2", 2235, 3148, 2234, 3149 },
                { "climbThroughForest-again-o1", "regicide_cross_over1", 2232, 3148, 2231, 3149 },
            }
            for _, ws in ipairs(west_steps) do
                t.exec(ws[1], t.player.click_loc, ws[2], 1, { at = { ws[3], ws[4] } })
                t.ticks(6)
                at_exact(ws[1] .. "-tile", ws[5], ws[6])
            end
            eat_if_low()
            cross("goFromTyrasToTrap-again", "regicide_trap_tripwire", 2220, 3152, 2220, 3153, "step over", { { 2228, 3150 }, { 2223, 3151 } })
            local _, pocket = t.world.tile()
            t.check("goFromTyrasToTrap-again-tile", pocket.z >= 3155, "north of the tripwire at " .. pocket.x .. "," .. pocket.z .. " :: " .. last_lines(3))
            cure_poison(t, "goFromTyrasToTrap-again-antipoison")
            eat_if_low()
            -- goGiveRabbitToGuard: the three dense forests north of the tripwire, o3 2216,3161 / o2 2216,3164 / o3 2216,3167
            -- (a cross_over3 landing in this mapsquare forgets the rabbit, regicide_route.rs2:172, so they come first)
            travel("goGiveRabbitToGuard-walk-toForests", 2217, 3160, { { 2218, 3158 } })
            t.exec("goGiveRabbitToGuard-forest1", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3161 } })
            t.ticks(6)
            at_exact("goGiveRabbitToGuard-forest1-tile", 2217, 3163)
            t.exec("goGiveRabbitToGuard-forest2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2216, 3164 } })
            t.ticks(6)
            at_exact("goGiveRabbitToGuard-forest2-tile", 2217, 3166)
            t.exec("goGiveRabbitToGuard-forest3", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3167 } })
            t.ticks(6)
            at_exact("goGiveRabbitToGuard-forest3-tile", 2217, 3169)
            travel("goGiveRabbitToGuard-walk2", 2185, 3183, { { 2217, 3173 }, { 2203, 3180 }, { 2188, 3180 } })

            -- useRabbitOnGuard: the lazy guard at the catapult eats it (regicide_tyras_lazy_guard.rs2:51-56)
            local guard = t.player.by_symbol("npc", "regicide_tyras_lazy_guard_vis")
            t.exec("useRabbitOnGuard", t.player.use_on, "cooked_rabbit", guard)
            t.exec("useRabbitOnGuard-dialog", t.chat.play, {
                "player:Here, I caught this", "npc:You cooked me a rabbit", "player:No problem",
            })
            t.ticks(2)
            local _, fed = t.var.varbit("varb8447_regicide_given_rabbit")
            t.check("useRabbitOnGuard-flag", fed == 1, "varb8447_regicide_given_rabbit=" .. tostring(fed))

            -- useBombOnCatapult: the fused barrel on the catapult (regicide_bombcraft.rs2:215); the player is carried to the
            -- tent and back at the end of the scene
            local catapult = t.player.by_symbol("loc", "regicide_catapult_right")
            t.exec("useBombOnCatapult", t.player.use_on, "regicide_barrel_lid_fused", catapult)
            t.exec("useBombOnCatapult-stage", t.var.await, "varp328_regicide_quest", 12, 60)
            t.ticks(4)
            t.expect("quest.stage.killed_tyras", t.quest.expect_stage(12))

            -- leaveFromCatapult: back east along the camp road, the three forests southward, then the tripwire
            -- (regicide_traps.rs2:19) -- "Go to the east, then south to the traps and cross them."
            travel("leaveFromCatapult-walk", 2217, 3170, { { 2188, 3180 }, { 2203, 3180 }, { 2217, 3173 } })
            t.exec("leaveFromCatapult-forest1", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3167 } })
            t.ticks(6)
            at_exact("leaveFromCatapult-forest1-tile", 2217, 3166)
            t.exec("leaveFromCatapult-forest2", t.player.click_loc, "regicide_cross_over2", 1, { at = { 2216, 3164 } })
            t.ticks(6)
            at_exact("leaveFromCatapult-forest2-tile", 2217, 3163)
            t.exec("leaveFromCatapult-forest3", t.player.click_loc, "regicide_cross_over3", 1, { at = { 2216, 3161 } })
            t.ticks(6)
            at_exact("leaveFromCatapult-forest3-tile", 2217, 3160)
            travel("leaveFromCatapult-walk-toTripwire", 2220, 3158, { { 2218, 3158 } })
            cross("leaveFromCatapult", "regicide_trap_tripwire", 2220, 3155, 2220, 3153, "step over")
            local _, tw = t.world.tile()
            t.check("leaveFromCatapult-tile", tw.z <= 3153, "south of the tripwire at " .. tw.x .. "," .. tw.z .. " :: " .. last_lines(3))
            cure_poison(t, "leaveFromCatapult-antipoison")
            eat_if_low()
            -- the road to Iorwerth: east through the three forests west of the tracker, the spring flats, the ring, the log
            travel("goTalkToIorwerthAfterRegicide-walk-toForests", 2231, 3149, { { 2223, 3151 }, { 2228, 3150 } })
            local east_steps = {
                { "goTalkToIorwerthAfterRegicide-east1", "regicide_cross_over1", 2232, 3148, 2234, 3149 },
                { "goTalkToIorwerthAfterRegicide-east2", "regicide_cross_over2", 2235, 3148, 2237, 3149 },
                { "goTalkToIorwerthAfterRegicide-east3", "regicide_cross_over3", 2238, 3148, 2240, 3149 },
            }
            for _, es in ipairs(east_steps) do
                t.exec(es[1], t.player.click_loc, es[2], 1, { at = { es[3], es[4] } })
                t.ticks(6)
                at_exact(es[1] .. "-tile", es[5], es[6])
            end
            -- the forest's east side to the ring without the spring (header; reach.py 2240,3149 -> 2209,3201 REACH len=83)
            travel("goTalkToIorwerthAfterRegicide-walk-toRing", 2209, 3201, { { 2240, 3155 }, { 2239, 3165 }, { 2235, 3168 },
                { 2232, 3171 }, { 2232, 3177 }, { 2225, 3180 }, { 2221, 3184 }, { 2219, 3188 }, { 2212, 3194 } })

            -- goTalkToIorwerthAfterRegicide: the ring of leaves, the log, the camp
            cross("goTalkToIorwerthAfterRegicide-ring", "regicide_pitfall_side", 2209, 3201, 2209, 3202, "cross safely")
            travel("goTalkToIorwerthAfterRegicide-walk-toLog", 2201, 3236, { { 2209, 3208 }, { 2205, 3212 }, { 2203, 3220 }, { 2203, 3235 } })
            cross_log(t, "goTalkToIorwerthAfterRegicide-log", 2201, 2196)
            where("goTalkToIorwerthAfterRegicide-log-tile", 2196, 3237, 0)
            travel("walk-talkToIorwerthAfterRegicide", 2203, 3253)
            t.exec("talkToIorwerthAfterRegicide", t.player.talk_to, "lord_iorwerth_vis")
            t.exec("talkToIorwerthAfterRegicide-dialog", t.chat.play, {
                "player:Lord Iorwerth, it is done", "npc:Good good", "npc:I'm sure you will want",
                "mesbox:Lord Iorwerth gives you a scroll", "npc:As a token", "player:Thank you my lord",
            })
            t.ticks(2)
            t.expect("quest.stage.reported_iorwerth", t.quest.expect_stage(13))
            t.expect("message.held", t.inv.expect_has("regicide_iorwerth_message", 1))

            -- talkToArianwyn: outside Ardougne Castle, the scene fires on walking into his zone (x 2584-2591 z 3296-3303)
            -- with the message (regicide_route.rs2 [zone,0_40_51_24_32]). The way there: Lumbridge Teleport out of
            -- Iorwerth's camp, the members' gate, overland to the castle street (header).
            lumbridge_teleport(t, "talkToArianwyn.lumbridgeTeleport", "Lumbridge, out of Iorwerth's camp in Tirannwn")
            members_gate_north(t, "talkToArianwyn")
            t.exec("goto-talkToArianwyn", t.player.goto_tile, 2579, 3298, 0)
            t.player.walk_to(2586, 3298, 20)
            for _poll = 1, 12 do
                if t.chat.kind() ~= "none" then break end
                t.ticks(1)
            end
            t.check("talkToArianwyn", t.chat.kind() ~= "none", "Arianwyn's scene opened on walking in, page kind " .. tostring(t.chat.kind()))
            t.exec("talkToArianwyn-dialog", t.chat.play, {
                "npc:Are you the human", "player:Yes, that's me", "npc:Thank Seren", "player:What do you mean",
                "npc:I am Arianwyn", "npc:There is much to explain", "player:Well you seem to know",
                "npc:Good, we understand", "mesbox:You show the message", "mesbox:King Lathas", "player:I had no idea",
                "npc:I have a few ideas", "npc:Once you are done", "player:You want me to help", "npc:The chance for redemption",
                "npc:This isn't a struggle", "npc:Deliver your message",
            })
            t.ticks(2)
            t.expect("quest.stage.spoken_arianwyn", t.quest.expect_stage(14))

            -- goTalkToLathasToFinish: the castle door, the stairs and the king's door (header), then King Lathas
            up_to_lathas(t, "goToArdougneCastleFloor2-finish")
            local _, in_room = t.world.tile()
            t.check("goToArdougneCastleFloor2-finish-room", in_kings_room(in_room), "standing at " .. in_room.x .. "," .. in_room.z .. " level " .. in_room.level)
            t.exec("goTalkToLathasToFinish", t.player.talk_to, "kinglathas", 1)
            local _, coins_before = t.inv.count("coins")
            local _, xp_snapshot = t.skill.snapshot()
            t.exec("goTalkToLathasToFinish-dialog", t.chat.play, {
                "player:My lord, Tyras is dead", "npc:This is grand news", "player:Yes, I have a letter",
                "mesbox:You hand the king", "npc:Yes... Good", "player:Does this mean", "npc:Not yet", "npc:Anyway",
            })
            t.ticks(4)
            t.expect("reward.agility", t.skill.expect_gain("agility", 13750, xp_snapshot))
            local _, coins_after = t.inv.count("coins")
            t.check("reward.coins", coins_after - coins_before == 15000, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (15000 documented)")
            t.quest.expect_complete()
            t.finish(0)
            -- LEG 6 END
        end },
    },
}
