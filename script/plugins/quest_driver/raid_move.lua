-- quest-driver / raid_move: ONE MOVEMENT DECISION A TICK (raid seam54
-- move_arbiter, 2026-10-07).
--
-- WHY.  The owner, 2026-10-07, after a day of the Verzik P3 plan patching one
-- machine's walk with another's: "If you were to model the verzik fight as a
-- solver ... 1. Measure 2. Decide 3. Act".  Before this every P3 machine
-- (tornado guard, ball pair, web detour, enrage close-in) wrote intent.walk
-- itself and the LAST writer won, so a ball partner dodging its tornado broke
-- the pair, the pair's step walked into a tornado, and each fix was one more
-- side channel (guard_tight_tick, ball_forbid, guard_goal).  The svb ball of
-- t814: both partners moved on almost every tick of the flight and landed 2
-- apart.
--
-- WHAT IT IS.  The machines stop moving the player.  They state what the tile
-- I stand on at the end of the tick must not be (HARD: a tornado's step, a
-- web, a projectile's landing tile, outside the ball pair's 3x3 at the
-- landing) and what it should be near (SOFT: the machine's objective, the
-- weapon's reach, a margin from a tornado).  solve() scores every tile one
-- tick's run can reach and takes the cheapest.  A hard constraint is a
-- penalty, not a filter: when every tile breaks one, the least harmful wins
-- instead of nobody moving.
--
-- PURE.  No QD read: the plan builds the query from its measurement and the
-- harness (test/raids/move_solver_test.lua, plain `lua`) builds the same
-- query from a ticklog, so a tile choice is tested in milliseconds instead of
-- a survey.

local M = {}

M.HARD = 1000

function M.cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- The tile a run of `step` tiles toward (tx, tz) ends on: diagonal first,
-- then straight, the way the server's pathing walks an open floor.
function M.toward(x, z, tx, tz, step)
    for _ = 1, step do
        if x < tx then x = x + 1 elseif x > tx then x = x - 1 end
        if z < tz then z = z + 1 elseif z > tz then z = z - 1 end
    end
    return x, z
end

-- Can a tornado be ON (x, z) at the end of the tick my click moves me there?
-- Content (~tob_verzik_tornado_tick): each tick it first hits if it stands on
-- its raider's tile, else walks one tile toward the raider's CURRENT tile.  A
-- click on t moves me on t + 1, so it has two steps before I land: toward
-- where I am (`me`), then toward wherever its next step finds me (`mid`, or
-- the tile itself).  Any of those ending on (x, z) is a touch.
function M.tornado_reaches(tornadoes, me, mid, x, z)
    for _, e in ipairs(tornadoes) do
        local ax, az = M.toward(e.x, e.z, me.x, me.z, 1)
        local a2x, a2z = M.toward(ax, az, mid.x, mid.z, 1)
        local b2x, b2z = M.toward(ax, az, x, z, 1)
        if (e.x == x and e.z == z) or (ax == x and az == z) or (a2x == x and a2z == z) or (b2x == x and b2z == z) then
            return true
        end
    end
    return false
end

-- The cost of standing on (x, z) at the end of the tick.
local function score(q, x, z)
    local total, broke = 0, nil
    for _, h in ipairs(q.hard) do
        if h.bad(x, z) then
            total = total + (h.pen or M.HARD)
            broke = (broke and (broke .. "+") or "") .. h.name
        end
    end
    for _, s in ipairs(q.soft) do
        total = total + s.w * s.cost(x, z)
    end
    total = total + (q.stay_w or 1) * M.cheb(q.me.x, q.me.z, x, z)
    return total, broke
end

-- q = { me = {x, z}, step = 2, ok = function(x, z) -> walkable,
--       hard = { { name, bad = function(x, z) -> bool, pen = number } },
--       soft = { { name, w = number, cost = function(x, z) -> number } },
--       stay_w = number }
-- Returns { x, z, cost, broke = "name+name" or nil, moved = bool }.
function M.solve(q)
    assert(q, "raid_move.solve: q")
    assert(q.me, "raid_move.solve: q.me")
    assert(q.ok, "raid_move.solve: q.ok")
    assert(q.hard, "raid_move.solve: q.hard")
    assert(q.soft, "raid_move.solve: q.soft")
    local step = q.step or 2
    local best = nil
    for dx = -step, step do
        for dz = -step, step do
            local x, z = q.me.x + dx, q.me.z + dz
            if (dx == 0 and dz == 0) or q.ok(x, z) then
                local c, broke = score(q, x, z)
                local moved = M.cheb(q.me.x, q.me.z, x, z)
                if best == nil or c < best.cost or (c == best.cost and moved < best.dist) then
                    best = { x = x, z = z, cost = c, broke = broke, dist = moved }
                end
            end
        end
    end
    best.moved = best.dist > 0
    return best
end

-- The cost of a named tile, for a caller asking "is following my objective
-- as good as the best?" (a far objective is left to the server's pathing).
function M.cost_at(q, x, z)
    return score(q, x, z)
end

-- Loaded two ways: by `require` (the bot agent, tools/raid_agent/run.lua),
-- which wants the table back, and as one part of the quest driver's single
-- concatenated chunk (src/plugin/torirs_plugin_drive.c), where a bare
-- trailing `return` ends the chunk early ("<eof> expected", the driver fails
-- to load). A return inside a block is legal anywhere.
if QD ~= nil and QD.raid ~= nil then
    QD.raid.move = M
else
    return M
end
