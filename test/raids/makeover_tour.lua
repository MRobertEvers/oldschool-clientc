-- makeover_tour: the makeover interface (516 `makeover`) from every source
-- that offers it in this tree, driven through the clicks a player makes.
--
--   hairdresser  Talk-to -> "I'd like a haircut please." -> a card, a colour,
--                Confirm: the hair kit and colour change, no coins taken
--                (free since 9 November 2022, OSRS Wiki "Hairdresser").
--                Shave (op5) the same on the facial hair tab. A hat refuses.
--   house        a Bedroom with a Shoe box and an Oak dresser:
--                  Shoe box Change-clothes -> the shoes tab; the style is
--                  refused (the box changes the colour only), the colour is
--                  applied.
--                  Oak dresser Haircut -> the hair tab, a new style applied.
--                  Preen answers with the dresser pose.
--
-- The appearance itself has no reader here; what the makeover wrote is read
-- back from the character-design panel's varps it keeps in step
-- (varp7131_design_kit_hair & co., player/configs/player_design.varp).
--
--   TORIRS_STDERR_UNBUFFERED=1 ./src/build_<you>_opt/torirsserver --scriptrun \
--       test/raids/makeover_tour.lua --bots 1 --name mk --ticks 20000 \
--       --session $S/mk --fixture test/raids/fixtures/fresh_lumbridge.ini

local HAIRDRESSER_SIDE = "0_46_52_3_50"       -- 2947,3378, beside the hairdresser
local RIMMINGTON_PORTAL_SIDE = "0_46_50_9_24" -- 2953,3224
local DOOR_HOTSPOTS = { "poh_hotspot_doorl_rimmington", "poh_hotspot_doorr_rimmington" }

return {
    id = "makeover_tour",
    fixture = "fresh_lumbridge.ini",
    frames = 200000,
    setup = {
        "::clearinv",
        "::maxstats",
        "::give coins 100000",
    },

    run = function(t)
        local H = {}

        function H.tile()
            local r, tl = t.world.tile()
            if r ~= "ok" or tl == nil then
                return { x = -1, z = -1, level = -1 }
            end
            return tl
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
        function H.modal()
            local r, live = t.ui.is_modal()
            return r == "ok" and live == true
        end
        function H.last_messages(n)
            local _, last = t.msg.last(n or 3)
            local text = {}
            for i = 1, #(last or {}) do
                text[#text + 1] = tostring(last[i].text)
            end
            return table.concat(text, " / ")
        end
        -- What the makeover last wrote (::makeoverlook says the design varps,
        -- which are server-only).
        function H.look(field)
            local _, prior = t.msg.last(1)
            local since = (prior and prior[1] and prior[1].serial) or -1
            t.cheat("::makeoverlook", false)
            for _ = 1, 5 do
                local _, last = t.msg.last(4)
                for i = #(last or {}), 1, -1 do
                    local text = tostring(last[i].text)
                    if (last[i].serial or 0) > since and text:find("makeover look:", 1, true) then
                        return tonumber(text:match(" " .. field .. "=(%-?%d+)")) or -1
                    end
                end
                t.ticks(1)
            end
            return -1
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
        function H.close_all()
            for _ = 1, 4 do
                if t.chat.kind() == "none" and not H.modal() then
                    return
                end
                t.key("escape")
                t.ticks(1)
            end
        end

        -- ------------------------------------------------ the interface
        -- Card N of the open tab is child 5N of item_area, swatch i child 2i
        -- of colours, and Apply / Confirm the text at sub 9.
        function H.card(tag, row)
            t.exec(tag .. ".card", H.press, "makeover:item_area", row * 5)
            t.ticks(1)
        end
        function H.colour(tag, index)
            t.exec(tag .. ".colour", H.press, "makeover:colours", index * 2)
            t.ticks(1)
        end
        function H.confirm(tag)
            t.exec(tag .. ".confirm", H.press, "makeover:select_and_close", 9)
            t.expect(tag .. ".closed", t.ui.await_close("makeover", 4))
        end
        function H.opened(tag, tab)
            local r = t.expect(tag .. ".open", t.ui.await_open("makeover", 6))
            if r ~= "ok" then
                return false
            end
            local have = H.server("varb4130_makeover_interface_type")
            return t.check(tag .. ".tab", have == tab, "tab " .. tostring(have) .. " want " .. tab)
        end
        function H.tab(tag, tab, want)
            t.exec(tag, H.press, "makeover:burger_menu_frame", 10 + 2 * tab)
            t.ticks(2)
            local have = H.server("varb4130_makeover_interface_type")
            t.check(tag .. ".tab", have == want and H.modal(), "tab " .. tostring(have) .. " want " .. want
                .. "; " .. H.last_messages(1))
        end
        -- A row other than the one worn (the card the server highlighted).
        function H.other_row(want)
            local worn = H.server("varp3788_makeover_hair")
            if worn == want * 5 then
                return want + 1
            end
            return want
        end

        -- =============================================== the hairdresser
        t.cheat("::tele " .. HAIRDRESSER_SIDE, false)
        t.ticks(3)
        local coins = H.count("coins")
        local hair_before = H.look("hair")

        t.exec("hair.talk", t.player.talk_to, "hairdresser", 1)
        t.exec("hair.dialogue", t.chat.play, {
            "npc:Good afternoon. In need of a haircut are we?",
            "choose:I'd like a haircut please.",
            "player:I'd like a haircut please.",
            "npc:Please select the hairstyle you would like",
        })
        if H.opened("hair", 0) then
            -- The burger menu's tabs (text child 10 + 2k): legs and back.
            H.tab("hair.to_legs", 5, 5)
            H.tab("hair.to_hair", 0, 0)
            H.card("hair", H.other_row(3))
            H.colour("hair", 5)
            t.check("hair.selected", H.server("varp3789_makeover_colour") == 5,
                "colour varp " .. H.server("varp3789_makeover_colour"))
            H.confirm("hair")
        end
        local hair_after = H.look("hair")
        t.check("hair.kit_changed", hair_after ~= hair_before,
            "design_kit_hair " .. hair_before .. " -> " .. hair_after)
        t.check("hair.colour", H.look("col_hair") == 5,
            "design_col_hair " .. H.look("col_hair"))
        t.check("hair.free", H.count("coins") == coins, "coins " .. coins .. " -> " .. H.count("coins"))

        -- The right-click Shave goes straight to the facial hair tab.
        local jaw_before = H.look("jaw")
        t.exec("shave.click", t.player.talk_to, "hairdresser", 5)
        if H.opened("shave", 1) then
            H.card("shave", H.other_row(2))
            H.confirm("shave")
        end
        local jaw_after = H.look("jaw")
        t.check("shave.kit_changed", jaw_after ~= jaw_before,
            "design_kit_jaw " .. jaw_before .. " -> " .. jaw_after)
        t.check("shave.free", H.count("coins") == coins, "coins " .. coins .. " -> " .. H.count("coins"))

        -- A hat on: refused before the interface (Transcript:Hairdresser).
        t.cheat("::give bronze_med_helm 1", false)
        t.ticks(1)
        t.exec("hat.wear", t.player.equip, "bronze_med_helm")
        t.ticks(2)
        t.exec("hat.click", t.player.talk_to, "hairdresser", 4)
        t.ticks(3)
        t.check("hat.refused", not H.modal() and t.chat.kind() ~= "none",
            "chat " .. tostring(t.chat.kind()) .. ", modal " .. tostring(H.modal()))
        H.close_all()
        t.cheat("::clearinv", false)
        t.cheat("::give coins 100000", false)

        -- ================================================ the house
        function H.base()
            local tl = H.tile()
            if tl.x < 6400 then
                return nil
            end
            return { x = (tl.x // 64) * 64, z = (tl.z // 64) * 64 }
        end
        function H.in_square(sym, gx, gz, level)
            local base = H.base()
            if base == nil then
                return "not_visible", "not inside a house"
            end
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
        function H.building()
            return H.server("varb2176_poh_building_mode") == 1
        end
        function H.set_building(tag, on)
            if H.building() == on then
                return true
            end
            t.ui.tab("settings")
            H.press("settings_side:houseoptions")
            if t.ui.await_open("poh_options", 5) ~= "ok" then
                t.check(tag .. ".options", false, "House Options did not open")
                return false
            end
            t.exec(tag, H.press, "poh_options:" .. (on and "build_mode_on" or "build_mode_off"))
            t.ticks(4)
            return t.check(tag .. ".mode", H.building() == on, "Building Mode " .. tostring(H.building()))
        end

        t.cheat("::pohbuy", false)
        t.cheat("::tele " .. RIMMINGTON_PORTAL_SIDE, false)
        t.ticks(3)
        t.player.click_loc("poh_rimmington_portal", 3)
        t.ticks(4)
        if not t.check("house.enter", H.inside(), "at " .. H.tile().x .. "," .. H.tile().z) then
            t.blocked("could not enter the house in Building Mode")
            return
        end

        -- A Bedroom east of the starter Parlour (4,4) on the ground floor.
        local GROUND = 1
        local door = nil
        do
            local base = H.base()
            for dz = 3, 4 do
                local at = { base.x + 4 * 8 + 7, base.z + 4 * 8 + dz, GROUND }
                for i = 1, #DOOR_HOTSPOTS do
                    local r, d = t.world.loc_near(DOOR_HOTSPOTS[i], 32, { at = at })
                    if r == "ok" and door == nil then
                        door = { sym = DOOR_HOTSPOTS[i], x = d.tile_x, z = d.tile_z, level = d.level }
                    end
                end
            end
        end
        if not t.check("room.door", door ~= nil, "east doorway of the Parlour") then
            t.blocked("no doorway for the bedroom")
            return
        end
        t.exec("room.door", t.player.click_loc, door.sym, 5, { at = { door.x, door.z, door.level } })
        t.expect("room.panel", t.ui.await_open("poh_add_room", 6))
        t.exec("room.pick", H.press, "poh_add_room:bedroom")
        if H.await_choice("room.confirm_page") then
            t.exec("room.confirm", H.choose, "Build the Bedroom")
        end
        t.ticks(4)

        -- Build one piece on a bedroom hotspot from the creation panel.
        function H.build(tag, hotspot, row, mats)
            local r, spot = H.in_square(hotspot, 5, 4, GROUND)
            if not t.check(tag .. ".hotspot", r == "ok", tostring(spot)) then
                return false
            end
            t.cheat("::give hammer 1", false)
            t.cheat("::give poh_saw 1", false)
            for i = 1, #mats do
                t.cheat("::give " .. mats[i][1] .. " " .. mats[i][2], false)
            end
            t.ticks(1)
            t.exec(tag .. ".click", t.player.click_loc, hotspot, 5, { at = { spot.tile_x, spot.tile_z, spot.level } })
            if t.expect(tag .. ".panel", t.ui.await_open("poh_furniture_creation", 6)) ~= "ok" then
                H.close_all()
                return false
            end
            t.exec(tag .. ".pick", H.press, string.format("poh_furniture_creation:%02d", row))
            return t.expect(tag .. ".built", t.msg.await("You build the furniture", 10)) == "ok"
        end
        H.build("shoebox", "poh_bedroom_2", 1, { { "woodplank", 2 }, { "nails", 2 } })
        H.build("dresser", "poh_bedroom_3", 3, { { "plank_oak", 2 }, { "molten_glass", 1 } })
        H.set_building("building.off", false)

        function H.loc(sym)
            local r, d = H.in_square(sym, 5, 4, GROUND)
            t.check(sym .. ".present", r == "ok", tostring(r == "ok" and (d.tile_x .. "," .. d.tile_z) or d))
            if r ~= "ok" then
                return nil
            end
            return { d.tile_x, d.tile_z, d.level }
        end

        -- Shoe box: the shoes tab, colour only.
        local box = H.loc("poh_wardrobe_1")
        if box ~= nil then
            local col_before = H.look("col_feet")
            local kit_before = H.look("feet")
            t.exec("shoebox.click", t.player.click_loc, "poh_wardrobe_1", 1, { at = box })
            if H.opened("shoebox", 6) then
                local sel = H.server("varp3788_makeover_hair")
                H.card("shoebox", 1)
                t.check("shoebox.style_refused", H.server("varp3788_makeover_hair") == sel
                    and H.last_messages(1):find("can't change that style", 1, true) ~= nil,
                    "selection " .. sel .. " -> " .. H.server("varp3788_makeover_hair") .. "; " .. H.last_messages(1))
                -- A Shoe box offers no wristwear tab.
                H.tab("shoebox.to_hands", 7, 6)
                t.check("shoebox.hands_refused", H.last_messages(1):find("can't change that here", 1, true) ~= nil,
                    H.last_messages(1))
                local want = (col_before == 4) and 3 or 4
                H.colour("shoebox", want)
                H.confirm("shoebox")
                t.check("shoebox.colour", H.look("col_feet") == want,
                    "design_col_feet " .. col_before .. " -> " .. H.look("col_feet"))
                t.check("shoebox.kit_kept", H.look("feet") == kit_before,
                    "design_kit_feet " .. kit_before .. " -> " .. H.look("feet"))
            end
            H.close_all()
        end

        -- Oak dresser: Haircut opens the hair tab; Preen poses.
        local dresser = H.loc("poh_mirror_3")
        if dresser ~= nil then
            local before = H.look("hair")
            t.exec("dresser.haircut", t.player.click_loc, "poh_mirror_3", 2, { at = dresser })
            if H.opened("dresser", 0) then
                H.card("dresser", H.other_row(7))
                H.confirm("dresser")
            end
            local after = H.look("hair")
            t.check("dresser.kit_changed", after ~= before, "design_kit_hair " .. before .. " -> " .. after)
            H.close_all()

            t.exec("dresser.shave", t.player.click_loc, "poh_mirror_3", 3, { at = dresser })
            H.opened("dresser_shave", 1)
            H.close_all()

            local _, _, pose = t.player.seq()
            -- The answer is the pose alone, which click_loc's settle does not
            -- count as one: judged on the seq instead.
            t.player.click_loc("poh_mirror_3", 1, { at = dresser })
            t.ticks(2)
            local _, _, now = t.player.seq()
            t.check("dresser.preen_anim", type(now) == "table" and type(pose) == "table"
                and now.seq_starts ~= pose.seq_starts and now.seq >= 0,
                "seq " .. tostring(type(now) == "table" and now.seq or "?") .. "; " .. H.last_messages(1))
        end
        -- ::pohbuy takes 1000 and the Bedroom 10000; nothing else may.
        t.check("house.free", H.count("coins") == 100000 - 1000 - 10000, "coins " .. H.count("coins"))
        t.finish(0)
    end,
}
