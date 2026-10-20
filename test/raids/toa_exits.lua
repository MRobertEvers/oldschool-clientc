-- toa_exits: every ToA exit abandons the raid (RSProx index 4288 [693..703],
-- Zebak's exit, rev 241, solo):
--   * Leave on the exit -> the mesbox "You are about to abandon the raid. If you
--     do this, you will not be able to return to your current run.", then the
--     menu "Abandon the raid?" / "Yes, abandon the raid." / "No, I want to stay."
--   * No -> still in the raid.
--   * Yes (Zebak's, the recorded one) -> "You abandon the raid and leave the Tombs
--     of Amascut." and the lobby (3358..3360, 9113..9114).
-- The exits: the Nexus, Scabaras, Het and Apmeken (`toa_door_exit`), Crondis
-- (`toa_crondis_exit`), Zebak (`toa_zebak_exit`), the Wardens' first room
-- (`toa_door_exit_wardens`). Content: minigame_toa/scripts/toa_nexus.rs2.
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
-- `::toastate`'s active= (t.var.server reads 0 for varp 6910 here).
local function active(t)
    t.cheat("::toastate", false)
    t.ticks(1)
    for _, l in ipairs(t.raid.cox_messages(10)) do
        local a = l:match("^toastate active=(%-?%d+)")
        if a then return tonumber(a) end
    end
    return -1
end
local function page(t)
    local _, text = t.chat.text()
    return tostring(text)
end

-- Into room `n`, Leave on its exit, check the box and the menu, and answer.
local function try_exit(t, name, room, exit, answer)
    t.cheat("::toa " .. room, false)
    t.ticks(4)
    local r, d = t.player.click_loc(exit, 1)
    t.expect(name .. ".click", r == "ok" and "ok" or "fail", tostring(r) .. " " .. tostring(d))
    local box = page(t)
    t.expect(name .. ".box", (box:find("You are about to", 1, true)
        and box:find("abandon the raid", 1, true)
        and box:find("return to your current run.", 1, true)) and "ok" or "fail", box:sub(1, 200))
    t.chat.continue_()
    t.ticks(1)
    t.expect(name .. ".choose", t.chat.choose(answer))
    t.ticks(1)
end

return {
    id = "toa_exits",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = { "::clearinv", "::setlevel hitpoints 99" },
    run = function(t)
        local stays = {
            { "nexus", 1, "toa_door_exit" },
            { "crondis", 2, "toa_crondis_exit" },
            { "scabaras", 4, "toa_door_exit" },
            { "het", 6, "toa_door_exit" },
            { "apmeken", 8, "toa_door_exit" },
            { "wardens", 10, "toa_door_exit_wardens" },
        }
        for _, s in ipairs(stays) do
            try_exit(t, s[1], s[2], s[3], "No, I want to stay.")
            local a = active(t)
            t.expect(s[1] .. ".stays", a == 1 and "ok" or "fail", "toastate active=" .. a)
        end

        try_exit(t, "zebak", 3, "toa_zebak_exit", "Yes, abandon the raid.")
        t.ticks(6)
        local _, tile = t.world.tile()
        local in_lobby = tile and tile.level == 0 and tile.x >= 3358 and tile.x <= 3360
            and tile.z >= 9113 and tile.z <= 9114
        t.expect("zebak.abandon", (said(t, "You abandon the raid and leave the Tombs of Amascut.") and in_lobby)
            and "ok" or "fail", string.format("at %s,%s", tostring(tile and tile.x), tostring(tile and tile.z)))
        local a = active(t)
        t.expect("zebak.left", a ~= 1 and "ok" or "fail", "toastate active=" .. a)
    end,
}
