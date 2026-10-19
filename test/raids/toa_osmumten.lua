-- toa_osmumten: Osmumten and the Nexus around him, solo.
--   * The raid's Nexus has its five doors as the template draws them.
--   * Akkha killed: Talk-to on his Osmumten says the after-boss line and takes the raider back to the
--     Nexus, landing in front of the Path of Het's door, which has collapsed (`_closed`).
--   * The Wardens' room: the crystal crosses it without starting it; Osmumten's Begin starts it (solo:
--     no ready-up lines) and he leaves a couple of ticks later.
--   * The vault: the first Talk-to is the whole story (`itt_met_osmumten` to 2), then the leave menu;
--     Yes takes the raider out of the Tombs to the lobby.
local function now(t) local _, k = t.tick() return k or -1 end
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function server(t, name)
    local _, v = t.var.server(name)
    return tonumber(v) or -1
end
local function started(t)
    t.cheat("::toastate", false)
    t.ticks(1)
    for _, l in ipairs(t.raid.cox_messages(20)) do
        local s = l:match("^toastate.-started=(%-?%d+)")
        if s then return tonumber(s) end
    end
    return nil
end
local function near(t, loc, radius)
    local r = t.world.loc_near(loc, radius)
    return r == "ok"
end
local function page(t)
    local _, text = t.chat.text()
    return tostring(text)
end

return {
    id = "toa_osmumten",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = { "::clearinv", "::setlevel hitpoints 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 1", false)
        t.ticks(4)
        local doors = { "toa_nexus_crondis_door", "toa_nexus_scabaras_door", "toa_nexus_het_door",
            "toa_nexus_apmeken_door", "toa_nexus_wardens_door" }
        local seen = 0
        for _, d in ipairs(doors) do
            if near(t, d, 40) then seen = seen + 1 end
        end
        t.expect("nexus.doors", seen == 5 and "ok" or "fail", string.format("%d of 5 doors", seen))

        -- Akkha down, then Osmumten's Talk-to
        t.cheat("::toa 7", false)
        t.ticks(3)
        t.cheat("::toago", false)
        t.ticks(3)
        keep_alive(t)
        t.cheat("::toabossdone", false)
        t.ticks(2)
        t.expect("akkha.complete", said(t, "Challenge complete: Akkha") and "ok" or "fail")
        t.ticks(2)
        t.expect("osmumten.talk", t.player.talk_to("toa_osmumten_akkha", 1))
        local line = page(t)
        t.expect("osmumten.line", line:find("We are done here", 1, true) and "ok" or "fail", line)
        t.chat.continue_()
        t.ticks(10)
        t.expect("nexus.landing", near(t, "toa_nexus_het_door_closed", 5) and "ok" or "fail",
            "within 5 tiles of the collapsed Het door")
        t.expect("nexus.het_collapsed", (near(t, "toa_nexus_het_door_closed", 40) and not near(t, "toa_nexus_het_door", 40))
            and "ok" or "fail")
        t.expect("nexus.others_open", (near(t, "toa_nexus_crondis_door", 40) and near(t, "toa_nexus_wardens_door", 40))
            and "ok" or "fail")

        -- The Wardens: the crystal crosses, Begin starts
        t.cheat("::toa 10", false)
        t.ticks(4)
        keep_alive(t)
        t.player.click_loc("wardens_teleport", 1)
        t.ticks(5)
        keep_alive(t)
        t.expect("wardens.crystal_waits", started(t) == 0 and "ok" or "fail", "started=" .. tostring(started(t)))
        local from = now(t)
        t.expect("wardens.begin", t.player.talk_to("toa_osmumten_wardens", 3))
        t.ticks(5)
        keep_alive(t)
        t.expect("wardens.started", (said(t, "Challenge started: The Wardens.") and started(t) == 1) and "ok" or "fail")
        t.expect("wardens.no_ready_lines", said(t, "party members are now ready") and "fail" or "ok")
        local gone = false
        local _, rows = t.ticklog.rows({ kind = "npc_free" })
        for _, row in ipairs(rows or {}) do
            if row.tick >= from and row.type == 11834 then gone = true end
        end
        t.expect("wardens.osmumten_leaves", gone and "ok" or "fail")

        -- The vault: the story, then out
        t.cheat("::toa 12", false)
        t.ticks(4)
        -- he stands 26 tiles south of the vault's arrival tile, local (32,23) against (31,49)
        local ex, ez
        t.cheat("::toastate", false)
        t.ticks(1)
        for _, l in ipairs(t.raid.cox_messages(20)) do
            local x, z = l:match("^toastate.-entry=(%-?%d+),(%-?%d+)")
            if x and not ex then ex, ez = tonumber(x), tonumber(z) end
        end
        if ex then t.player.walk_to(ex + 1, ez - 22, 2) end
        t.ticks(2)
        t.expect("vault.talk", t.player.talk_to("toa_osmumten_vault", 1))
        local pages = {}
        for _ = 1, 8 do
            pages[#pages + 1] = page(t)
            t.chat.continue_()
            t.ticks(1)
        end
        local story = table.concat(pages, " / ")
        t.expect("vault.story", (story:find("As you approach the spirit", 1, true)
            and story:find("I take it you're Osmumten?", 1, true)
            and story:find("We shall see if it is enough.", 1, true)) and "ok" or "fail", story:sub(1, 300))
        t.expect("vault.met", server(t, "varb14464_itt_met_osmumten") == 2 and "ok" or "fail",
            "itt_met_osmumten " .. server(t, "varb14464_itt_met_osmumten"))
        t.expect("vault.choose", t.chat.choose("Yes."))
        t.ticks(6)
        local _, tile = t.world.tile()
        t.expect("vault.leave", (said(t, "You leave the Tombs of Amascut.") and tile and tile.x == 3360 and tile.z == 9114)
            and "ok" or "fail", string.format("at %s,%s", tostring(tile and tile.x), tostring(tile and tile.z)))
    end,
}
