-- display_pickups: a stand-in pickup lies alone on its tile and its Take gives
-- the real item (gen_spawns.py OBJ_SPAWN_EXCLUSIONS, the stacked-spawn rows;
-- player/scripts/pickup_display.rs2 and brain_door.rs2 in OSRS-Content).
--   Lumbridge farm 3232,3296: the axe-in-logs loc, no bronze_axe obj on it.
--   Lumbridge Swamp 3244,3159: the fishing-net loc, no net obj; its Take gives
--     a net every time (RSProx index 1842 [13823..13831]).
--   Trouble Brewing 3804,3010: the Brewin' guide dummy alone; its Take gives
--     the guide and the dummy stays (index 1833 [434]).
--   Great Brain Robbery deck 3801,2870,1: the deck keg alone; its Take gives
--     the inventory keg.
--   Deep Wilderness Dungeon 3018,10353: the fake wine alone; its Take gives a
--     Wine of Zamorak.
local function go(t, name, x, z, level)
    t.cheat(string.format("::goto %d %d %d", x, z, level))
    t.ticks(4)
    local _, tile = t.world.tile()
    t.check(name .. ".at", tile ~= nil and tile.x == x and tile.z == z,
            tile and (tile.x .. "," .. tile.z) or "no tile")
end

local function absent(t, name, sym, x, z)
    local r, row = t.world.obj_near(sym, 3)
    local on = r == "ok" and row.tile_x == x and row.tile_z == z
    t.check(name, not on, on and (sym .. " on " .. x .. "," .. z) or tostring(r))
end

local function count(t, sym)
    local _, n = t.inv.count(sym)
    return n or 0
end

return {
    id = "display_pickups",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {},
    run = function(t)
        go(t, "axe", 3232, 3295, 0)
        absent(t, "axe.no_obj", "bronze_axe", 3232, 3296)
        local r, row = t.world.loc_near("log_withaxe", 3)
        t.check("axe.loc", r == "ok", tostring(r))

        go(t, "net", 3244, 3160, 0)
        absent(t, "net.no_obj", "net", 3244, 3159)
        local before = count(t, "net")
        for i = 1, 2 do
            local cr, cd = t.player.click_loc("net", 1, { at = { 3244, 3159 } })
            t.ticks(4)
            t.check("net.take" .. i, count(t, "net") == before + i,
                    string.format("click_loc -> %s (%s); nets %d", tostring(cr), tostring(cd), count(t, "net")))
        end

        go(t, "guide", 3804, 3011, 0)
        absent(t, "guide.no_obj", "game_brew_instruction_book", 3804, 3010)
        local gr, gd = t.player.click_obj("game_brew_instruction_book_dummy", 3)
        t.ticks(3)
        t.check("guide.take", count(t, "game_brew_instruction_book") == 1,
                string.format("click_obj -> %s (%s)", tostring(gr), tostring(gd)))
        local dr = t.world.obj_near("game_brew_instruction_book_dummy", 3)
        t.check("guide.dummy_stays", dr == "ok", tostring(dr))
        t.check("guide.no_dummy_in_pack", count(t, "game_brew_instruction_book_dummy") == 0, "")

        go(t, "keg", 3802, 2870, 1)
        absent(t, "keg.no_obj", "brain_inv_gun_powder_barrel", 3801, 2870)
        local kr, kd = t.player.click_obj("brain_deck_gun_powder_barrel", 1)
        t.ticks(3)
        t.check("keg.take", count(t, "brain_inv_gun_powder_barrel") == 1,
                string.format("click_obj -> %s (%s)", tostring(kr), tostring(kd)))

        go(t, "wine", 3019, 10353, 0)
        absent(t, "wine.no_obj", "wine_of_zamorak", 3018, 10353)
        local wr, wd = t.player.click_obj("fake_wine_of_zamorak", 3)
        t.ticks(3)
        t.check("wine.take", count(t, "wine_of_zamorak") == 1 and count(t, "fake_wine_of_zamorak") == 0,
                string.format("click_obj -> %s (%s)", tostring(wr), tostring(wd)))
    end,
}
