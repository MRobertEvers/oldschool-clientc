-- _vzp3_plan_check: the raider planner (api_drive.plan, collision_plan in C)
-- against the client's own pathfinder, on the open floor at Lumbridge: every
-- two-step run's middle tile must be the route's first step (a web on the
-- middle tile sticks, so the planner's model of the server's path must be
-- the server's).  One bot, no raid.
--
--   ./src/build_heldop_opt/torirsserver --scriptrun test/raids/_vzp3_plan_check.lua --bots 1 \
--       --session /tmp/vzpc --fixture tests/raids/fixtures/fresh_lumbridge.ini
local function run(t)
    t.ticks(3)
    local n, bad = t.raid.vzp3_plan_check()
    t.check("plan.middle_tiles", n >= 12 and #bad == 0,
        string.format("%d offsets compared, %d disagree%s", n, #bad, #bad > 0 and (": " .. table.concat(bad, "; ")) or ""))
    t.finish(0)
end

return {
    id = "_vzp3_plan_check",
    fixture = "fresh_lumbridge.ini",
    party = 1,
    max_frames = 2000,
    setup = {},
    run = run,
}
