-- box_trap_chinchompa: grey chinchompas on the Isle of Souls, as RSProx index 1855 and
-- 3980 recorded box traps (skill_hunter/scripts/box_trap.rs2's header):
--   - the lay: the box on the floor on the click, the empty trap 3 ticks on, the
--     player left standing on it [937..940];
--   - a chinchompa walks to a side, tries the trap 2 ticks after its last step,
--     and the trap shuts (trapping, then full 2 ticks later) or fails;
--   - Check on a full trap: the catch and the box into the pack two ticks on;
--     Reset on a failed one: the trap dismantled and laid again.
--
--   ./src/build_opt/torirsserver --scriptrun test/hunter/box_trap_chinchompa.lua --bots 1 \
--       --name boxchin0 --ticks 3000 --session /tmp/boxchin --fixture test/quests/fixtures/fresh_lumbridge.ini

local function count(t, obj) local _, n = t.inv.count(obj) return tonumber(n) or 0 end

local function notes(t, label)
    local ok, rows = t.chat.lines(0)
    if ok == "ok" and type(rows) == "table" then
        for i = math.max(1, #rows - 5), #rows do t.note(label .. ": " .. tostring(rows[i].text)) end
    end
end

local function tile(t)
    local res, v = t.world.tile()
    if res == "ok" and type(v) == "table" then return v end
    return nil
end

return {
    id = "box_trap_chinchompa",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", "::setlevel hunter 99", "::questmark quest_eaglespeak", "::give hunting_box_trap 2",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("goto", t.player.goto_tile, 2128, 2952, 0)
        t.ticks(3)

        t.ticklog.mark("lay")
        t.exec("lay.op", t.player.inv_op, "hunting_box_trap", 1)
        t.ticks(1)
        notes(t, "lay")
        t.check("lay.box_down", count(t, "hunting_box_trap") == 1, "the box left the pack on the click")
        -- The 3-tick landing is checked on the tick log (box_trap.rs2 header):
        -- t.exec returns a couple of ticks after the server took the click.
        t.ticks(2)
        t.check("lay.trap", t.world.loc_near("hunting_boxtrap_empty", 0) == "ok", "the empty trap stands")
        local v = tile(t)
        t.check("lay.stayed", v ~= nil and v.x == 2128 and v.z == 2952,
            "the player stays on the trap, read " .. (v and (v.x .. "," .. v.z) or "?"))
        -- Off the tile: a trap with a player on it catches nothing.
        t.exec("step", t.player.goto_tile, 2130, 2952, 0)

        local caught, failed = false, false
        for round = 1, 8 do
            if caught then break end
            local state = nil
            for _ = 1, 250 do
                if t.world.loc_near("hunting_boxtrap_full_chinchompa", 4) == "ok" then state = "full" break end
                if t.world.loc_near("hunting_boxtrap_failed", 4) == "ok" then state = "failed" break end
                t.ticks(1)
            end
            t.check("wait." .. round, state ~= nil, "the trap resolved: " .. tostring(state))
            if state == nil then break end
            if state == "full" then
                local chins = count(t, "chinchompa_captured")
                t.ticklog.mark("check." .. round)
                t.exec("check." .. round, t.player.click_loc, "hunting_boxtrap_full_chinchompa", 1)
                t.ticks(4)
                t.check("check.catch", count(t, "chinchompa_captured") == chins + 1, "one chinchompa taken")
                t.check("check.box", count(t, "hunting_box_trap") == 2, "the box back in the pack")
                caught = true
            else
                failed = true
                t.ticklog.mark("reset." .. round)
                t.exec("reset." .. round, t.player.click_loc, "hunting_boxtrap_failed", 2)
                t.ticks(8)
                t.check("reset.relaid", t.world.loc_near("hunting_boxtrap_empty", 4) == "ok", "Reset laid the trap again")
                t.exec("step." .. round, t.player.goto_tile, 2130, 2952, 0)
            end
        end
        t.check("caught", caught, "a chinchompa was caught (failed rounds: " .. tostring(failed) .. ")")

        -- Reset: a second trap, then Reset on whatever it becomes. The trap is
        -- dismantled and, 3 ticks on, laid again from the pack where it stood.
        t.exec("goto2", t.player.goto_tile, 2128, 2952, 0)
        t.ticks(2)
        t.exec("lay2", t.player.inv_op, "hunting_box_trap", 1)
        t.ticks(4)
        t.exec("step2", t.player.goto_tile, 2130, 2952, 0)
        local sym = nil
        for _ = 1, 250 do
            if t.world.loc_near("hunting_boxtrap_full_chinchompa", 4) == "ok" then sym = "hunting_boxtrap_full_chinchompa" break end
            if t.world.loc_near("hunting_boxtrap_failed", 4) == "ok" then sym = "hunting_boxtrap_failed" break end
            t.ticks(1)
        end
        t.check("reset.wait", sym ~= nil, "the second trap resolved: " .. tostring(sym))
        if sym ~= nil then
            t.ticklog.mark("reset")
            t.exec("reset", t.player.click_loc, sym, 2)
            t.ticks(10)
            t.check("reset.relaid", t.world.loc_near("hunting_boxtrap_empty", 1) == "ok", "Reset laid the trap again")
            notes(t, "reset")
        end
    end,
}
