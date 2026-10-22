-- tob_practice: `::tob`'s practice mode. A death in a room `::tob` built is no
-- raid death: no cage, nothing counted, and the room is built again (a new
-- instance, the barrier uncrossed) with the raider at its start. `::tobmode`,
-- the harness's landing, is not practice: its death is the cage's.
--
--   ./src/build_opt/torirsserver --scriptrun test/raids/tob_practice.lua --bots 1 \
--       --name tob0 --ticks 9000 --session /tmp/tobp --fixture test/raids/fixtures/fresh_lumbridge.ini
-- `::tobstate`'s fields (handle, room, started), read through the driver's
-- own parser: t.raid.state asks the active varp first, which a scriptrun
-- cannot read (transmit=no).
local last_state = ""
local function state(t)
    local r, s, line = t.raid._read_state("tob")
    if r ~= "ok" then
        last_state = tostring(r) .. ": " .. tostring(s)
        return nil
    end
    last_state = tostring(line)
    return s
end

return {
    id = "tob_practice",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticks(2)
        t.cheat("::tob 1", false)
        local r = t.msg.await("Practice mode: a death puts you back", 10)
        t.check("practice.on", r == "ok", "no practice line after ::tob 1")
        t.ticks(4)
        t.cheat("::tobgo")
        t.ticks(3)
        local before = state(t)
        t.check("practice.started", before ~= nil and before.started == 1, "the Maiden's room did not start: " .. last_state)

        t.cheat("::die", false)
        r = t.msg.await("Practice mode: the room starts again", 15)
        t.check("practice.death_line", r == "ok", "no practice death line")
        -- The rebuild is queued for the tick after the restore.
        t.ticks(4)
        local after = state(t)
        t.check("practice.rebuilt", after ~= nil and before ~= nil and after.handle ~= before.handle,
            "the room was not built again: " .. last_state)
        t.check("practice.unstarted", after ~= nil and after.room == 1 and after.started == 0,
            "after the death: " .. last_state)
        t.check("practice.uncounted", t.msg.expect("Death count") ~= "ok", "the death was counted")

        -- `::tobmode` is not practice: the same death is the cage's.
        t.cheat("::tobout")
        t.ticks(2)
        t.cheat("::tobmode 1 1", false)
        t.ticks(6)
        t.cheat("::tobgo")
        t.ticks(3)
        t.cheat("::die", false)
        r = t.msg.await("Death count", 15)
        t.check("tobmode.caged", r == "ok", "::tobmode's death was not the cage's")
        t.ticks(4)
        t.cheat("::tobout")
        t.expect("tob_practice.run", "ok", "done")
    end,
}
