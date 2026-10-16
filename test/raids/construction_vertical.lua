-- construction_vertical: the house between floors. A Skill Hall's staircase
-- climbed in Building Mode builds the hall above it; an upstairs room needs a
-- room under it; the stairs' Remove-room takes the hall above away again. A
-- Throne Room over an Oubliette gets the trapdoor whose ladder is built below
-- in the same transaction, and both are used. Content: poh_hall.rs2,
-- poh_rooms.rs2, poh_oubliette_access.rs2.
--
--   TORIRS_STDERR_UNBUFFERED=1 ./src/build_<you>_opt/torirsserver --scriptrun \
--       test/raids/construction_vertical.lua --bots 1 --name vertical --ticks 20000 \
--       --session $S/vertical --fixture test/raids/fixtures/fresh_lumbridge.ini

local DOOR_HOTSPOTS = { "poh_hotspot_doorl_rimmington", "poh_hotspot_doorr_rimmington",
    "poh_hotspot_door_dungeon", "poh_hotspot_door_dungeon2", "poh_hotspot_door_dungeon3",
    "poh_hotspot_door_dungeon4", "poh_hotspot_door_dungeon5", "poh_hotspot_door_dungeon6" }
local RIMMINGTON_PORTAL_SIDE = "0_46_50_9_24"
local BUILD_OPS = {}
local ROOM = { button = "vertical", floor = -1 }
local SLOTS = {}

return {
    id = "construction_vertical",
    fixture = "fresh_lumbridge.ini",
    frames = 200000,
    setup = { "::clearinv", "::maxstats", "::give coins 20000000", "::give hammer 1", "::give poh_saw 1" },

    run = function(t)
        local H = {}

        -- ------------------------------------------------------- readings
        function H.tile()
            local r, tl = t.world.tile()
            if r ~= "ok" or tl == nil then
                return { x = -1, z = -1, level = -1 }
            end
            return tl
        end
        function H.tile_str(tl)
            return tostring(tl.x) .. "," .. tostring(tl.z) .. " L" .. tostring(tl.level)
        end
        function H.inside()
            return H.tile().x >= 6400
        end
        function H.count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end
        function H.server(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end
        -- t.ui.is_modal answers (result, live): the result string is always truthy.
        function H.modal()
            local r, live = t.ui.is_modal()
            return r == "ok" and live == true
        end
        function H.building()
            return H.server("varb2176_poh_building_mode") == 1
        end
        function H.last_messages(n)
            local _, last = t.msg.last(n or 3)
            local text = {}
            for i = 1, #(last or {}) do
                text[#text + 1] = tostring(last[i].text)
            end
            return table.concat(text, " / ")
        end
        -- The newest message serial and the messages after `since`.
        function H.messages_since(since)
            local _, last = t.msg.last(12)
            local out = {}
            local top = since
            for i = 1, #(last or {}) do
                local serial = last[i].serial or 0
                if serial > since then
                    out[#out + 1] = tostring(last[i].text)
                end
                if serial > top then
                    top = serial
                end
            end
            return out, top
        end
        function H.inv_signature()
            local parts = {}
            for slot = 0, 27 do
                local r, item = t.inv.slot(slot)
                parts[#parts + 1] = (r == "ok" and type(item) == "table")
                    and (tostring(item.name) .. "x" .. tostring(item.count)) or "-"
            end
            return table.concat(parts, ",")
        end

        -- ------------------------------------------------------- actions
        function H.tele(coord)
            local r, d = t.cheat("::tele " .. coord, false)
            t.ticks(3)
            return r, d
        end
        function H.press(sym, sub)
            local r, w = t.ui.widget(sym, sub)
            if r ~= "ok" then
                return r, "ui.widget " .. tostring(sym) .. " " .. tostring(w)
            end
            return t.ui.invoke(w, 1)
        end
        function H.choose(prefix)
            return t.chat.play({ "options", "choose:/^" .. prefix:gsub("%p", "%%%0") .. "/" })
        end
        function H.await_choice(tag, ticks)
            for _ = 1, ticks or 4 do
                if t.chat.kind() == "options" then
                    return true
                end
                t.ticks(1)
            end
            t.check(tag, false, "no choice page; last messages: " .. H.last_messages(4))
            return false
        end
        -- Close whatever a step left open: a dialogue, a modal interface.
        function H.close_all()
            for _ = 1, 4 do
                local kind = t.chat.kind()
                local modal = H.modal()
                if kind == "none" and not modal then
                    return
                end
                t.key("escape")
                t.ticks(1)
            end
        end
        -- A clean backpack before every build: leftovers from a refused build
        -- would fill it, and "the materials were taken" must mean exactly.
        function H.reset_inv()
            t.cheat("::clearinv", false)
            t.cheat("::give coins 20000000", false)
            t.cheat("::give hammer 1", false)
            t.cheat("::give poh_saw 1", false)
            t.ticks(1)
        end
        -- Planted furniture is watered in, not hammered (poh_build.rs2
        -- ~poh_furniture_is_planted); nothing else wants the can's slot.
        local PLANTED = { hw20_poh_pumpkin = true, forestry_poh_beehive_part = true, greenman_statue = true }
        function H.planted(mats)
            for i = 1, #mats do
                if mats[i][1]:find("sapling", 1, true) or PLANTED[mats[i][1]] then
                    return true
                end
            end
            return false
        end
        -- The whole bill, into the backpack H.reset_inv just emptied.
        function H.give(mats)
            for i = 1, #mats do
                t.cheat("::give " .. mats[i][1] .. " " .. mats[i][2], false)
            end
            t.ticks(1)
        end

        -- House Options from the Settings side panel.
        function H.options_press(tag, button)
            t.ui.tab("options")
            H.press("settings_side:houseoptions")
            local r = t.ui.await_open("poh_options", 5)
            if r ~= "ok" then
                t.check(tag .. ".options", false, "House Options did not open: " .. tostring(r))
                return false
            end
            t.exec(tag, H.press, "poh_options:" .. button)
            t.ticks(4)
            return true
        end
        function H.set_building(tag, on)
            if H.building() == on then
                return true
            end
            H.options_press(tag, on and "build_mode_on" or "build_mode_off")
            local now = H.building()
            t.check(tag .. ".mode", now == on, "Building Mode " .. tostring(now) .. " want " .. tostring(on)
                .. "; " .. H.last_messages(2))
            return now == on
        end
        -- Into the house from the Rimmington portal (op 2 home, op 3 building).
        function H.enter(tag, building)
            if H.inside() then
                return true
            end
            H.tele(RIMMINGTON_PORTAL_SIDE)
            t.player.click_loc("poh_rimmington_portal", building and 3 or 2)
            t.ticks(4)
            return t.check(tag, H.inside(), "at " .. H.tile_str(H.tile()) .. "; " .. H.last_messages(2))
        end

        -- -------------------------------------------------------- the grid
        -- The instance square the player stands in (whole 64x64 squares).
        function H.base()
            local tl = H.tile()
            if tl.x < 6400 then
                return nil
            end
            return { x = (tl.x // 64) * 64, z = (tl.z // 64) * 64 }
        end
        -- A copy of `sym` inside grid square (gx, gz) on `level`.
        function H.in_square(sym, gx, gz, level)
            local base = H.base()
            if base == nil then
                return "not_visible", "not inside a house"
            end
            -- Four slack-3 windows tile the square without reaching a
            -- neighbour (whose wall hotspots sit one tile away).
            local seen = {}
            for _, c in ipairs({ { 3, 3 }, { 4, 4 }, { 3, 4 }, { 4, 3 } }) do
                local centre = { base.x + gx * 8 + c[1], base.z + gz * 8 + c[2], level }
                local r, d = t.world.loc_near(sym, 32, { at = centre, slack = 3 })
                if r == "ok" and d.tile_x >= base.x + gx * 8 and d.tile_x <= base.x + gx * 8 + 7
                    and d.tile_z >= base.z + gz * 8 and d.tile_z <= base.z + gz * 8 + 7 then
                    return "ok", d
                end
                seen[#seen + 1] = tostring(r == "ok" and (d.tile_x .. "," .. d.tile_z) or d)
            end
            return "not_found", table.concat(seen, " | ")
        end
        -- The door hotspot on grid square (gx, gz)'s east wall (local x 7, z 3..4).
        function H.east_door(gx, gz, level)
            local base = H.base()
            if base == nil then
                return nil, "not inside a house"
            end
            local misses = {}
            for dz = 3, 4 do
                local at = { base.x + gx * 8 + 7, base.z + gz * 8 + dz, level }
                for i = 1, #DOOR_HOTSPOTS do
                    local r, d = t.world.loc_near(DOOR_HOTSPOTS[i], 32, { at = at })
                    if r == "ok" then
                        return { sym = DOOR_HOTSPOTS[i], x = d.tile_x, z = d.tile_z, level = d.level }, nil
                    end
                    misses[#misses + 1] = tostring(d)
                end
            end
            return nil, "no door hotspot on the east wall of " .. gx .. "," .. gz .. ": "
                .. table.concat(misses, " | ")
        end

        -- -------------------------------------------------- place the room
        -- Where the room under test stands once placed: grid square + level.
        local SPOT = { gx = 5, gz = 4, level = 1 }
        local GROUND = 1

        -- Add `button` through the east doorway of (gx, gz, level).
        function H.add_room_east(tag, gx, gz, level, button, name, cost)
            local door, why = H.east_door(gx, gz, level)
            if door == nil then
                t.check(tag .. ".door", false, why)
                return false
            end
            local coins = H.count("coins")
            t.exec(tag .. ".door", t.player.click_loc, door.sym, 5, { at = { door.x, door.z, door.level } })
            local r = t.expect(tag .. ".panel", t.ui.await_open("poh_add_room", 6))
            if r ~= "ok" then
                H.close_all()
                return false
            end
            t.exec(tag .. ".pick", H.press, "poh_add_room:" .. button)
            if not H.await_choice(tag .. ".confirm_page") then
                H.close_all()
                return false
            end
            t.exec(tag .. ".confirm", H.choose, "Build the " .. name)
            t.ticks(4)
            return t.check(tag .. ".paid", coins - H.count("coins") == cost,
                "paid " .. (coins - H.count("coins")) .. " want " .. cost .. "; " .. H.last_messages(2))
        end
        function H.remove_room_east(tag, gx, gz, level, name)
            local door, why = H.east_door(gx, gz, level)
            if door == nil then
                t.check(tag .. ".door", false, why)
                return false
            end
            t.exec(tag .. ".door", t.player.click_loc, door.sym, 5, { at = { door.x, door.z, door.level } })
            if not H.await_choice(tag .. ".page") then
                H.close_all()
                return false
            end
            local _, top = H.messages_since(-1)
            t.exec(tag .. ".confirm", H.choose, "Remove the " .. name)
            t.ticks(3)
            local said = H.messages_since(top)
            local r = "refused"
            for i = 1, #said do
                if said[i]:find("has been removed", 1, true) then
                    r = "ok"
                end
            end
            return t.expect(tag .. ".removed", r, #said > 0 and table.concat(said, " / ") or "said nothing")
        end

        -- A basement room below the Garden: the Garden's centrepiece is built
        -- as a Dungeon entrance, whose Enter op adds the room underneath.
        function H.add_below_garden(tag, choice, name, cost)
            local r, spot = H.in_square("poh_crude_garden_1", 4, 3, GROUND)
            if r ~= "ok" then
                t.check(tag .. ".entrance_spot", false, tostring(spot))
                return false
            end
            H.give({ { "marble_block", 1 } })
            t.exec(tag .. ".entrance", t.player.click_loc, "poh_crude_garden_1", 5)
            if t.expect(tag .. ".entrance_panel", t.ui.await_open("poh_furniture_creation", 6)) ~= "ok" then
                return false
            end
            -- The entrance is the Garden centrepiece hotspot's fifth row.
            t.exec(tag .. ".entrance_pick", H.press, "poh_furniture_creation:05")
            t.expect(tag .. ".entrance_built", t.msg.await("You build the furniture.", 10))
            local coins = H.count("coins")
            t.exec(tag .. ".enter", t.player.click_loc, "poh_crude_garden_centrepiece5", 1)
            if not H.await_choice(tag .. ".pick_page") then
                return false
            end
            t.exec(tag .. ".pick", H.choose, "Build a " .. choice)
            if not H.await_choice(tag .. ".confirm_page") then
                return false
            end
            t.exec(tag .. ".confirm", H.choose, "Build the " .. name .. " below")
            t.ticks(4)
            t.check(tag .. ".paid", coins - H.count("coins") == cost,
                "paid " .. (coins - H.count("coins")) .. " want " .. cost .. "; " .. H.last_messages(2))
            -- Adding the room takes the builder down with it; Enter only when not.
            if H.tile().level ~= 0 then
                t.exec(tag .. ".down", t.player.click_loc, "poh_crude_garden_centrepiece5", 1)
                t.ticks(3)
            end
            return t.check(tag .. ".below", H.tile().level == 0, "at " .. H.tile_str(H.tile()))
        end

        -- A free tile in the room the test came in through (the Parlour, or
        -- the stairs room below ground). An op like Climb-over leaves the
        -- player inside a ring, and a rebuild keeps them where they stand.
        function H.home(tag)
            local base = H.base()
            if base == nil then
                return
            end
            -- Ground floor: the Parlour, outside any ring or fence. Basement:
            -- inside the room under test, within its own doors -- built
            -- dungeon doors are locked to everyone ("forced open or have the
            -- lock picked", Wiki Oak door), the owner included.
            local x, z = base.x + 4 * 8 + 3, base.z + 4 * 8 + 2
            if SPOT.level == 0 then
                -- Beside the west doorway it came in by: the centre of an
                -- Oubliette is inside its cage.
                x, z = base.x + SPOT.gx * 8 + 1, base.z + SPOT.gz * 8 + 3
            end
            local coord = string.format("%d_%d_%d_%d_%d", SPOT.level, x // 64, z // 64, x % 64, z % 64)
            t.note(tag .. ": back to " .. x .. "," .. z .. " (" .. coord .. ")")
            t.cheat("::tele " .. coord, false)
            t.ticks(2)
        end

        -- ---------------------------------------------- build one furniture
        -- slot index -> { tier, dx, dz, level }: offsets from the instance
        -- square, because every rebuild (Building Mode on/off, a room added)
        -- moves the whole house to a fresh square.
        local BUILT = {}
        function H.record(si, tier, d, sym)
            local base = H.base()
            local ops = tier.ops
            if tier.variants ~= nil and tier.variants[sym] ~= nil then
                ops = tier.variants[sym]
            end
            BUILT[si] = { tier = tier, loc = sym or tier.built, ops = ops,
                dx = d.tile_x - base.x, dz = d.tile_z - base.z, level = d.level }
        end
        -- The piece a build left at `at`: the hotspot's own tile first (three
        -- Parlour chairs stand side by side), then within its footprint (a 2x2
        -- tree's origin is its south-west corner), for each built form.
        function H.find_built(tier, at)
            local forms = { tier.built }
            if tier.variants ~= nil then
                for name in pairs(tier.variants) do
                    if name ~= tier.built then
                        forms[#forms + 1] = name
                    end
                end
            end
            local last = nil
            for _, slack in ipairs({ 0, 1, 2 }) do
                for i = 1, #forms do
                    local r, d = t.world.loc_near(forms[i], 24, { at = at, slack = slack })
                    if r == "ok" then
                        return "ok", d, forms[i]
                    end
                    last = d
                end
            end
            return "not_found", last, table.concat(forms, "|")
        end
        function H.at(have)
            local base = H.base() or { x = 0, z = 0 }
            return { base.x + have.dx, base.z + have.dz, have.level }
        end

        function H.find_hotspot(slot)
            return H.in_square(slot.hotspot, SPOT.gx, SPOT.gz, SPOT.level)
        end
        function H.build(si, slot, tier)
            local tag = "build." .. slot.hotspot .. "." .. tier.row
            local r, spot = H.find_hotspot(slot)
            if r ~= "ok" then
                -- One piece of a group (rug corners, crockery shelves) is built
                -- by its sibling's build and its hotspot is gone with it.
                for other = 1, #SLOTS do
                    if other ~= si and BUILT[other] ~= nil and SLOTS[other].row == slot.row then
                        t.note(tag .. ": built with " .. SLOTS[other].hotspot)
                        return false
                    end
                end
                if ROOM.button == "dungeon_stairs" and slot.hotspot:find("stairs", 1, true) then
                    -- Under the Garden's entrance the conjunction's ladder link
                    -- takes the staircase space (poh_garden.rs2).
                    t.note(tag .. ": the entrance link occupies the staircase space")
                    return false
                end
                t.check(tag .. ".hotspot", false, slot.hotspot .. " not in the room: " .. tostring(spot))
                return false
            end
            H.reset_inv()
            if H.planted(tier.mats) then
                t.cheat("::give watering_can_8 1", false)
            end
            H.give(tier.mats)
            local had = {}
            for i = 1, #tier.mats do
                had[i] = H.count(tier.mats[i][1])
            end
            local _, before = t.skill.snapshot()
            local _, top = H.messages_since(-1)
            local cr, cd = t.player.click_loc(slot.hotspot, 5, { at = { spot.tile_x, spot.tile_z, spot.level } })
            if cr ~= "ok" and tostring(cd):find("reach", 1, true) then
                -- Built in by the last piece (inside an Oubliette cage): out
                -- through the doorway and try again.
                H.home(tag)
                cr, cd = t.player.click_loc(slot.hotspot, 5, { at = { spot.tile_x, spot.tile_z, spot.level } })
            end
            t.check(tag .. ".click", cr == "ok", tostring(cd))
            -- Most hotspots open the creation panel; a few ask in the chatbox
            -- instead (stairs: up or down, then which staircase by name).
            local picker = nil
            for _ = 1, 6 do
                if t.chat.kind() == "options" then
                    picker = "menu"
                    break
                end
                if t.ui.await_open("poh_furniture_creation", 1) == "ok" then
                    picker = "panel"
                    break
                end
            end
            if picker == "menu" then
                local _, rows = t.chat.options()
                local parts = {}
                for _, row in ipairs(type(rows) == "table" and rows or {}) do
                    parts[#parts + 1] = type(row) == "table" and tostring(row.text) or tostring(row)
                end
                local text = table.concat(parts, " | ")
                if text:find("Build stairs going up", 1, true) then
                    H.choose("Build stairs going up")
                    t.ticks(1)
                end
                t.exec(tag .. ".pick", H.choose, tier.menu or tier.name)
            elseif picker == "panel" then
                t.check(tag .. ".panel", true, "poh_furniture_creation")
                t.exec(tag .. ".pick", H.press, string.format("poh_furniture_creation:%02d", tier.row))
            else
                if H.last_messages(2):find("already something built in that space", 1, true) then
                    for other = 1, #SLOTS do
                        if other ~= si and BUILT[other] ~= nil and SLOTS[other].row == slot.row then
                            t.note(tag .. ": the space holds " .. SLOTS[other].hotspot .. "'s piece")
                            return false
                        end
                    end
                end
                t.check(tag .. ".panel", false, "neither the creation panel nor a choice; " .. H.last_messages(2))
                H.close_all()
                return false
            end
            -- Judged on everything said since the click: a menu picker's answer
            -- lands while chat.play is still answering, before any await.
            local built = "timeout"
            for _ = 1, 10 do
                local said = H.messages_since(top)
                for i = 1, #said do
                    if said[i]:find("You build the furniture", 1, true) then
                        built = "ok"
                    end
                end
                if built == "ok" then
                    break
                end
                t.ticks(1)
            end
            if built ~= "ok" and H.last_messages(2):find("only have one of each mounted amulet", 1, true) then
                -- The house allows one mounted Xeric's talisman and one Digsite
                -- pendant; the other space holding it already is the answer.
                t.note(tag .. ": refused, one of each mounted amulet")
                H.close_all()
                return false
            end
            t.expect(tag .. ".built", built, tier.name .. "; " .. H.last_messages(2))
            if built ~= "ok" then
                H.close_all()
                return false
            end
            t.expect(tag .. ".xp", t.skill.expect_gain("construction", tier.xp10 // 10, before))
            local wrong = {}
            for i = 1, #tier.mats do
                local used = had[i] - H.count(tier.mats[i][1])
                if used ~= tier.mats[i][2] then
                    wrong[#wrong + 1] = tier.mats[i][1] .. " used " .. used .. " of " .. tier.mats[i][2]
                end
            end
            t.check(tag .. ".materials", #wrong == 0, #wrong == 0 and "exactly the bill" or table.concat(wrong, ", "))
            local lr, ld, sym = H.find_built(tier, { spot.tile_x, spot.tile_z, spot.level })
            t.check(tag .. ".placed", lr == "ok", tostring(sym) .. ": "
                .. (lr == "ok" and (ld.tile_x .. "," .. ld.tile_z) or tostring(ld)))
            if lr ~= "ok" then
                return false
            end
            H.record(si, tier, ld, sym)
            return true
        end
        -- Get the piece to tier `row`: an upgrade-only tier is made from its
        -- source, which may itself be an upgrade (crafting table 1 -> 2 -> 3).
        function H.reach_tier(si, slot, row)
            local tier = slot.tiers[row]
            if tier == nil then
                return false
            end
            if tier.upgrade_from ~= nil and not tier.upgrade_absolute then
                if not H.reach_tier(si, slot, tier.upgrade_from) then
                    return false
                end
                return H.upgrade(si, slot, tier)
            end
            return H.build(si, slot, tier)
        end

        -- The Upgrade op on the built source turns it into `tier`.
        function H.upgrade(si, slot, tier)
            local tag = "upgrade." .. slot.hotspot .. "." .. tier.row
            local have = BUILT[si]
            if have == nil then
                for other = 1, #SLOTS do
                    if other ~= si and BUILT[other] ~= nil and SLOTS[other].row == slot.row then
                        t.note(tag .. ": upgraded with " .. SLOTS[other].hotspot)
                        return false
                    end
                end
                t.check(tag .. ".source", false, "the source tier " .. tostring(tier.upgrade_from) .. " is not built")
                return false
            end
            local op = nil
            for i = 1, 4 do
                if have.ops["op" .. i] == "Upgrade" then
                    op = i
                end
            end
            if op == nil then
                t.check(tag .. ".op", false, have.loc .. " offers no Upgrade op (" .. have.tier.name .. ")")
                return false
            end
            H.reset_inv()
            H.give(tier.mats)
            local _, before = t.skill.snapshot()
            t.exec(tag .. ".click", t.player.click_loc, have.loc, op, { at = H.at(have) })
            t.ticks(4)
            local lr, ld, sym = H.find_built(tier, H.at(have))
            t.check(tag .. ".placed", lr == "ok", tostring(sym) .. ": "
                .. (lr == "ok" and (ld.tile_x .. "," .. ld.tile_z) or tostring(ld)) .. "; " .. H.last_messages(2))
            t.expect(tag .. ".xp", t.skill.expect_gain("construction", tier.xp10 // 10, before))
            if lr == "ok" then
                H.record(si, tier, ld, sym)
            end
            return lr == "ok"
        end
        function H.remove(si, slot)
            -- Its current form (an altar follows the chapel's icon).
            local fr, fd, fsym = H.find_built(BUILT[si].tier, H.at(BUILT[si]))
            if fr == "ok" then
                H.record(si, BUILT[si].tier, fd, fsym)
            end
            local have = BUILT[si]
            local tag = "remove." .. slot.hotspot .. "." .. have.tier.row
            local cr, cd = t.player.click_loc(have.loc, 5, { at = H.at(have) })
            if cr ~= "ok" and tostring(cd):find("reach", 1, true) then
                H.home(tag)
                cr, cd = t.player.click_loc(have.loc, 5, { at = H.at(have) })
            end
            if cr ~= "ok" and tostring(cd):find("reach", 1, true) then
                -- A ring's floor or a fence corner can be shut in by the rest
                -- of its own group; Remove works from any reachable piece.
                local forms = { have.tier.built }
                for name in pairs(have.tier.variants or {}) do
                    forms[#forms + 1] = name
                end
                for i = 1, #forms do
                    local r, d = H.in_square(forms[i], SPOT.gx, SPOT.gz, SPOT.level)
                    if r == "ok" and (d.tile_x ~= H.at(have)[1] or d.tile_z ~= H.at(have)[2]) then
                        cr, cd = t.player.click_loc(forms[i], 5, { at = { d.tile_x, d.tile_z, d.level } })
                        if cr == "ok" then
                            break
                        end
                    end
                end
            end
            t.check(tag .. ".click", cr == "ok", tostring(cd))
            if not H.await_choice(tag .. ".page") then
                H.close_all()
                return false
            end
            local _, top = H.messages_since(-1)
            t.exec(tag .. ".confirm", H.choose, "Really remove it?")
            t.ticks(1)
            local said = H.messages_since(top)
            local r = "refused"
            for i = 1, #said do
                if said[i]:find("You remove", 1, true) then
                    r = "ok"
                end
            end
            t.expect(tag .. ".removed", r, #said > 0 and table.concat(said, " / ") or "said nothing")
            local hr = H.find_hotspot(slot)
            t.check(tag .. ".hotspot_back", hr == "ok", slot.hotspot .. " back on its square")
            BUILT[si] = nil
            return r == "ok"
        end

        -- -------------------------------------------------- the options
        -- Press `op` on a built piece and judge the answer.
        function H.option(si, slot, op_index, op_name)
            -- The op before may have turned the piece into another form (a
            -- dummy with something attached, a lit burner): find it again.
            local fr, fd, fsym = H.find_built(BUILT[si].tier, H.at(BUILT[si]))
            if fr == "ok" then
                H.record(si, BUILT[si].tier, fd, fsym)
            else
                -- An Attach that stands the combat dummy up as an npc leaves no loc.
                t.note("option " .. slot.hotspot .. " " .. op_name .. ": the piece is not a loc any more ("
                    .. tostring(fd) .. ")")
                return
            end
            local have = BUILT[si]
            if have.ops["op" .. op_index] ~= op_name then
                t.note("option " .. slot.hotspot .. " " .. op_name .. ": now " .. have.loc
                    .. ", whose op" .. op_index .. " is " .. tostring(have.ops["op" .. op_index]))
                return
            end
            local tag = "option." .. slot.hotspot .. "." .. have.tier.row .. "." .. op_name:gsub("%W", "_")
            -- Stand beside it first: the walk up to a piece is not its answer.
            if fr == "ok" then
                t.player.walk_near(fd, 12)
                t.ticks(1)
            end
            local _, top = H.messages_since(-1)
            local inv = H.inv_signature()
            local tile = H.tile()
            local was_modal = H.modal()
            local _, _, pose = t.player.seq()
            local cr, cd = t.player.click_loc(have.loc, op_index, { at = H.at(have) })
            t.ticks(3)
            local said = H.messages_since(top)
            local moved = H.tile()
            local changes = {}
            if #said > 0 then
                changes[#changes + 1] = "said: " .. table.concat(said, " / ")
            end
            if t.chat.kind() ~= "none" then
                changes[#changes + 1] = "dialogue: " .. t.chat.kind()
            end
            if H.modal() and not was_modal then
                changes[#changes + 1] = "interface open"
            end
            if H.inv_signature() ~= inv then
                changes[#changes + 1] = "inventory changed"
            end
            if moved.x ~= tile.x or moved.z ~= tile.z or moved.level ~= tile.level then
                changes[#changes + 1] = "moved to " .. H.tile_str(moved)
            end
            local _, _, now = t.player.seq()
            if type(now) == "table" and type(pose) == "table" then
                if now.seq_starts ~= pose.seq_starts and now.seq >= 0 then
                    changes[#changes + 1] = "animation " .. now.seq
                elseif now.anim ~= pose.anim then
                    changes[#changes + 1] = "pose " .. now.anim
                end
            end
            -- The click's own settle names a page or a modal it opened even
            -- when the step after it closed again.
            local opened = tostring(cd):match("(modal [%w_]+)") or tostring(cd):match("(page [%w_]+%->[%w_]+)")
            if opened ~= nil then
                changes[#changes + 1] = opened
            end
            local dull = false
            for i = 1, #said do
                if said[i]:find("Nothing interesting happens", 1, true) or said[i]:find("not implemented", 1, true)
                    or said[i]:find("not available yet", 1, true) then
                    dull = true
                end
            end
            t.check(tag, #changes > 0 and not dull,
                (#changes > 0 and table.concat(changes, "; ") or "no answer at all")
                .. " [click: " .. tostring(cr) .. "]")
            H.close_all()
            -- An op that took the player out of the house (a portal, a
            -- teleport) is answered; walk back in for the next one.
            if not H.inside() then
                H.enter(tag .. ".reenter", false)
            elseif H.tile().level ~= SPOT.level then
                t.note(tag .. ": the op moved the player to level " .. H.tile().level)
            end
        end


        -- The door hotspot on one wall of grid square (gx, gz): the two
        -- middle tiles of that wall.
        local SIDES = {
            east = function(i) return 7, 2 + i end, west = function(i) return 0, 2 + i end,
            north = function(i) return 2 + i, 7 end, south = function(i) return 2 + i, 0 end,
        }
        function H.side_door(gx, gz, level, side)
            local base = H.base()
            for i = 1, 2 do
                local lx, lz = SIDES[side](i)
                local at = { base.x + gx * 8 + lx, base.z + gz * 8 + lz, level }
                for j = 1, #DOOR_HOTSPOTS do
                    local r, d = t.world.loc_near(DOOR_HOTSPOTS[j], 32, { at = at })
                    if r == "ok" then
                        return { sym = DOOR_HOTSPOTS[j], x = d.tile_x, z = d.tile_z, level = d.level }
                    end
                end
            end
            return nil
        end
        -- Add `button` through the `side` doorway of (gx, gz, level).
        function H.add_room_side(tag, gx, gz, level, side, button, name, cost)
            local door = H.side_door(gx, gz, level, side)
            if door == nil then
                t.check(tag .. ".door", false, "no door hotspot on the " .. side .. " wall of " .. gx .. "," .. gz .. " L" .. level)
                return false
            end
            local coins = H.count("coins")
            t.exec(tag .. ".door", t.player.click_loc, door.sym, 5, { at = { door.x, door.z, door.level } })
            if t.expect(tag .. ".panel", t.ui.await_open("poh_add_room", 6)) ~= "ok" then
                H.close_all()
                return false
            end
            t.exec(tag .. ".pick", H.press, "poh_add_room:" .. button)
            if not H.await_choice(tag .. ".confirm_page") then
                H.close_all()
                return false
            end
            t.exec(tag .. ".confirm", H.choose, "Build the " .. name)
            t.ticks(4)
            return t.check(tag .. ".paid", coins - H.count("coins") == cost,
                "paid " .. (coins - H.count("coins")) .. " want " .. cost .. "; " .. H.last_messages(2))
        end
        function H.remove_room_side(tag, gx, gz, level, side, name)
            local door = H.side_door(gx, gz, level, side)
            if not t.check(tag .. ".door", door ~= nil, side .. " doorway of " .. gx .. "," .. gz .. " L" .. level) then
                return false
            end
            t.exec(tag .. ".click", t.player.click_loc, door.sym, 5, { at = { door.x, door.z, door.level } })
            if not H.await_choice(tag .. ".page") then
                H.close_all()
                return false
            end
            t.exec(tag .. ".confirm", H.choose, "Remove the " .. name)
            t.ticks(4)
            return t.check(tag .. ".removed", H.last_messages(3):find("has been removed", 1, true) ~= nil, H.last_messages(3))
        end
        -- Build `sym`'s `row` in grid square (gx, gz, level) through the panel.
        function H.build_at(tag, sym, gx, gz, level, row, mats)
            local r, spot = H.in_square(sym, gx, gz, level)
            if not t.check(tag .. ".hotspot", r == "ok", sym .. ": " .. tostring(spot)) then
                return false
            end
            H.reset_inv()
            H.give(mats)
            t.exec(tag .. ".click", t.player.click_loc, sym, 5, { at = { spot.tile_x, spot.tile_z, spot.level } })
            if t.expect(tag .. ".panel", t.ui.await_open("poh_furniture_creation", 6)) ~= "ok" then
                H.close_all()
                return false
            end
            t.exec(tag .. ".pick", H.press, string.format("poh_furniture_creation:%02d", row))
            return t.expect(tag .. ".built", t.msg.await("You build", 10))
        end

        -- ============================================================ run
        t.cheat("::pohbuy", false)
        if not H.enter("house.enter", true) then
            t.blocked("could not enter the house in Building Mode")
            return
        end
        SPOT = { gx = 5, gz = 4, level = GROUND }

        -- ---- A Skill Hall, its staircase, and the hall above it ----------
        if not H.add_room_east("hall", 4, 4, GROUND, "skillhall", "Skill hall", 15000) then
            t.blocked("the Skill hall could not be added")
            return
        end
        local r, spot = H.in_square("poh_hall1_1_stairs_up", 5, 4, GROUND)
        t.check("stairs.hotspot", r == "ok", tostring(spot))
        H.reset_inv()
        H.give({ { "plank_oak", 10 }, { "steel_bar", 4 } })
        t.exec("stairs.click", t.player.click_loc, "poh_hall1_1_stairs_up", 5, { at = { spot.tile_x, spot.tile_z, spot.level } })
        H.await_choice("stairs.direction_page")
        t.exec("stairs.up", H.choose, "Build stairs going up")
        H.await_choice("stairs.kind_page")
        local _, top = H.messages_since(-1)
        t.exec("stairs.oak", H.choose, "Oak staircase")
        t.ticks(1)
        local said = table.concat(H.messages_since(top), " / ")
        t.check("stairs.built", said:find("You build the furniture", 1, true) ~= nil, said)
        local sr, sd = t.world.loc_near("poh_stairs_3", 24)
        t.check("stairs.placed", sr == "ok", tostring(sr))

        -- Climbing a staircase with nothing above builds the matching hall.
        H.give({ { "plank_oak", 10 }, { "steel_bar", 4 } })
        local coins = H.count("coins")
        t.exec("upstairs.climb", t.player.click_loc, "poh_stairs_3", 1)
        H.await_choice("upstairs.kind_page")
        t.exec("upstairs.kind", H.choose, "Build a Skill Hall above")
        H.await_choice("upstairs.confirm_page")
        t.exec("upstairs.confirm", H.choose, "Build the Skill hall and matching stairs")
        t.ticks(4)
        t.check("upstairs.level", H.tile().level == 2, "at " .. H.tile_str(H.tile()) .. "; " .. H.last_messages(2))
        t.check("upstairs.paid", coins - H.count("coins") == 15000, "paid " .. (coins - H.count("coins")))
        local tr = t.world.loc_near("poh_stairstop_3", 24)
        t.check("upstairs.landing", tr == "ok", "the matching top landing")

        -- Upstairs, a room must stand over another: east of the hall is over
        -- nothing; west is over the Parlour.
        local before = H.count("coins")
        local door = H.east_door(5, 4, 2)
        if t.check("upstairs.east_door", door ~= nil, "east doorway of the upper hall") then
            t.exec("upstairs.east.click", t.player.click_loc, door.sym, 5, { at = { door.x, door.z, door.level } })
            t.ui.await_open("poh_add_room", 6)
            H.press("poh_add_room:bedroom")
            t.ticks(2)
            t.check("upstairs.east.refused", H.last_messages(3):find("supported by a room below", 1, true) ~= nil
                and H.count("coins") == before, H.last_messages(3))
            H.close_all()
        end
        local ok_west = H.add_room_side("upstairs.west", 5, 4, 2, "west", "bedroom", "Bedroom", 10000)
        if ok_west then
            H.remove_room_side("upstairs.west.remove", 5, 4, 2, "west", "Bedroom")
        end

        -- Down, and Remove-room on the staircase takes the hall above away.
        local lr = t.world.loc_near("poh_stairstop_3", 24)
        t.exec("down.climb", t.player.click_loc, "poh_stairstop_3", 1)
        t.ticks(3)
        t.check("down.level", H.tile().level == GROUND, "at " .. H.tile_str(H.tile()))
        t.exec("removeroom.click", t.player.click_loc, "poh_stairs_3", 4)
        if H.await_choice("removeroom.page") then
            t.exec("removeroom.confirm", H.choose, "Remove the Skill hall")
            t.ticks(4)
            t.check("removeroom.done", H.last_messages(3):find("has been removed", 1, true) ~= nil, H.last_messages(3))
        end

        -- The hall goes too: its staircase first, then the room.
        local hr = t.world.loc_near("poh_stairs_3", 24)
        t.exec("hallgone.stairs", t.player.click_loc, "poh_stairs_3", 5)
        if H.await_choice("hallgone.stairs_page") then
            t.exec("hallgone.stairs_confirm", H.choose, "Really remove it?")
        end
        H.home("hallgone")
        H.remove_room_side("hallgone.room", 4, 4, GROUND, "east", "Skill hall")

        -- ---- A Throne Room over an Oubliette ------------------------------
        -- The Parlour's one doorway is east (cache door_locations), so the
        -- Throne Room stands at (5,4,L1). Below it: the stairs room under the
        -- Garden (4,3,L0), a Junction east of that (5,3,L0) -- doorways on
        -- every wall -- and the Oubliette north of the Junction (5,4,L0).
        H.home("throne")
        if not H.add_room_side("throne", 4, 4, GROUND, "east", "throneroom", "Throne room", 150000) then
            t.blocked("the Throne room could not be added")
            return
        end
        if not H.add_below_garden("below", "Dungeon stairs room", "Dungeon stairs", 7500) then
            t.blocked("no basement under the Garden")
            return
        end
        if not H.add_room_side("junction", 4, 3, 0, "east", "dungeon_cross", "Dungeon cross", 7500)
            or not H.add_room_side("oubliette", 5, 3, 0, "north", "oubliette", "Oubliette", 150000) then
            t.blocked("the Oubliette could not be placed under the Throne room")
            return
        end
        -- The ladder in the Oubliette builds the trapdoor above with it.
        local tier = { { "plank_oak", 10 } }
        if H.build_at("ladder", "poh_oubliette_5", 5, 4, 0, 1, tier) then
            t.check("ladder.placed", t.world.loc_near("poh_dungeon_ladder_oak", 24) == "ok", "the oak ladder below")
            H.set_building("use.off", false)
            t.exec("use.climb", t.player.click_loc, "poh_dungeon_ladder_oak", 1)
            t.ticks(3)
            t.check("use.up", H.tile().level == GROUND, "the ladder leads to the Throne room: at " .. H.tile_str(H.tile()))
            local tdr = t.world.loc_near("poh_trapdoor_oak_7", 24)
            t.check("use.trapdoor", tdr == "ok", "the trapdoor above the ladder")
            t.exec("use.trapdoor_op", t.player.click_loc, "poh_trapdoor_oak_7", 1)
            t.ticks(3)
            t.check("use.trapdoor_open", t.world.loc_near("poh_trapdoor_oak_open_7", 24) == "ok",
                "Open leaves the trapdoor open; " .. H.last_messages(2))
            t.exec("use.go_down", t.player.click_loc, "poh_trapdoor_oak_open_7", 1)
            t.ticks(3)
            t.check("use.down", H.tile().level == 0, "Go-down lands in the Oubliette: at " .. H.tile_str(H.tile()))
            t.exec("use.climb_back", t.player.click_loc, "poh_dungeon_ladder_oak", 1)
            t.ticks(3)
            t.check("use.back_up", H.tile().level == GROUND, "at " .. H.tile_str(H.tile()))

            -- Remove-room on the trapdoor takes the Oubliette (and its ladder) away.
            H.set_building("unlink.on", true)
            local trap = "poh_trapdoor_oak_7"
            if t.world.loc_near(trap, 24) ~= "ok" then
                trap = "poh_trapdoor_oak_open_7"
            end
            t.exec("unlink.click", t.player.click_loc, trap, 4)
            if H.await_choice("unlink.page") then
                t.exec("unlink.confirm", H.choose, "Remove the Oubliette")
                t.ticks(4)
                t.check("unlink.removed", H.last_messages(3):find("has been removed", 1, true) ~= nil, H.last_messages(3))
            end
        end
        t.finish(0)
    end,
}
