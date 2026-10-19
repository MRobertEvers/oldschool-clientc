-- _cox_room_probe: which layout a room test's raid n holds (scratch probe for
-- the RSProx audit, tools/cox/rsprox_room_audit.py): the room's variant, turn
-- and world origin, as the client's instance zones say. The run name is
-- "<room>_<n>", the room as QD.COX.ROOM names it and n the number its own test
-- takes from its run name (the same ::coxtest raid).
--   ./src/build_opt/torirsserver --scriptrun test/raids/_cox_room_probe.lua --bots 1 \
--       --name tekton_5 --ticks 200 --session /tmp/rp --fixture test/raids/fixtures/fresh_lumbridge.ini

local function run(t)
    local user = string.gsub(t.session._user() or "", "_p%d+$", "")
    local room, n = string.match(user, "^([a-z_]+)_(%d+)$")
    t.check("probe.name", room ~= nil, user)
    if room == nil then t.finish(1) return end
    local er, at = t.raid.cox_test_enter(room, tonumber(n))
    t.check("probe.enter", er == "ok", tostring(er))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(3)
    local found = nil
    for level = 0, 3 do
        local r = t.COX.room_at(at.cx, at.cz, level)
        if r ~= nil and r.name == room then found = r end
    end
    t.check("probe.room", found ~= nil, found and string.format("room %s n %s v %d t %d at %d,%d,%d",
        found.name, n, found.variant, found.turn, found.ox, found.oz, found.level) or "not found")
    t.finish(found and 0 or 1)
end

return { id = "_cox_room_probe", fixture = "fresh_lumbridge.ini", max_frames = 20000,
    setup = { "::clearinv" }, run = run }
