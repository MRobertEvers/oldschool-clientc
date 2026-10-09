-- _vzp3_plan_probe: the planner given a tornado diagonally adjacent (the
-- live leader at 6432,94 with its tornado at 6431,93 stayed and was touched,
-- 2026-10-09): the answer must step away, and standing must be lethal.
local function run(t)
    t.ticks(3)
    local _, me = t.raid.vzp3_plan_probe({ now = 0, h = 1, beam = 4 })  -- a no-op plan to learn my tile
    local x, z = me.path[1].x, me.path[1].z
    local r, plan = t.raid.vzp3_plan_probe({ now = 100, h = 12, beam = 32,
        chasers = { { x = x - 1, z = z - 1, tier = "lethal" } },
        pulls = { { x = x + 3, z = z + 3, size = 1, weight = 1.5, t0 = 100, t1 = 112 } } })
    local p1 = plan.path[1]
    t.check("probe.steps_away", r == "ok" and (p1.x ~= x or p1.z ~= z) and plan.lethal == 0,
        string.format("from %d,%d chaser %d,%d -> %d,%d lethal %s why %s", x, z, x - 1, z - 1, p1.x, p1.z, tostring(plan.lethal), tostring(plan.why)))
    -- and standing must be lethal: a plan forced to stay (no run, h=1, every move forbidden)
    local forbid = {}
    for dx = -2, 2 do for dz = -2, 2 do if dx ~= 0 or dz ~= 0 then forbid[#forbid + 1] = { x = x + dx, z = z + dz, t0 = 101, t1 = 101, tier = "lethal" } end end end
    local r2, plan2 = t.raid.vzp3_plan_probe({ now = 100, h = 1, beam = 8, chasers = { { x = x - 1, z = z - 1, tier = "lethal" } }, forbid = forbid })
    t.check("probe.stay_is_lethal", r2 == "ok" and plan2.lethal > 0, string.format("lethal %s why %s", tostring(plan2.lethal), tostring(plan2.why)))
    t.finish(0)
end
return { id = "_vzp3_plan_probe", fixture = "fresh_lumbridge.ini", party = 1, max_frames = 2000, setup = {}, run = run }
