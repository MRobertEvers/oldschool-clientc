-- Walks across region borders among npcs (pm open finding E2): with
--   TORIRS_CONTENT_TEST_RACE=1 TORIRS_CONTENT_TEST_BACKGROUND=1
--   TORIRS_IO_DEFER_FRAMES=32 TORIRS_QUEUE_CONTRACT_TRACE=1
-- the REBUILDs run as background jobs, npc bodies land while a job gathers,
-- and each commit reads back the element such a body was mounted on. The
-- walk rows grade the route (fences), not the race.
local function said(r, d) return tostring(r) .. " " .. tostring(d) end
local north, south = { { 3212, 3240 }, { 3222, 3245 } }, {}
for z = 3250, 3340, 10 do north[#north + 1] = { 3232, z } end
for i = #north - 1, 1, -1 do south[#south + 1] = north[i] end
south[#south + 1] = { 3206, 3233 }
return {
    id = "walk_background_rebuilds",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {},
    run = function(t)
        t.ticks(3)
        for n = 1, 2 do
            local r, d = t.player.walk_route(north)
            t.check("north." .. n, r == "ok", said(r, d))
            r, d = t.player.walk_route(south)
            t.check("south." .. n, r == "ok", said(r, d))
        end
        t.ticks(3)
        t.finish()
    end,
}
