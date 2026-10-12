-- quest-driver / tob_xarpus_hm: XARPUS, HARD MODE, five raiders.
--
--   t.raid.xarpus_hm_solve(opts) -> result, detail, record
--
-- The Normal solver (tob_xarpus.lua: the exhumeds, the step-back stack, the
-- Dawnbringer) with Hard Mode's stare (wiki Theatre_of_Blood/Hard_Mode
-- :353-357, docs/minigames/theater_of_blood/HMT_STRATEGIES.md):
--
--   the ring   "Xarpus will spit poison around the edges of the arena,
--              covering up to two tiles' worth": real pools, which the
--              shared measure reads as pools; the stack's tour skips any tile
--              that is one, and nothing else walks the outer two rings.
--   the stare  "Xarpus will immediately focus on the quadrant he was attacked
--              from, overriding his natural rotation. The team should spread
--              out on all four quadrants and attack on different ticks ... the
--              quadrant with two players should ... attack on the same tick
--              together." Every seat holds a quadrant (SW, SE, NE, NW, the fifth
--              sharing SW) and swings on the shared 4-tick rhythm only when he
--              is not facing it: each swing turns him to its quadrant, so the
--              next rhythm tick is everyone else's.
-- ==========================================================================

QD.XARP_HM = {
    QUAD = { [1] = "SW", [2] = "SE", [3] = "NE", [4] = "NW", [5] = "SW" },
}

-- P3, HARD MODE: my quadrant's nearest side tile; a swing on the rhythm tick
-- only when he faces another quadrant. -> the order
function QD.raid._xarp_hm_stare(S, F, B, armed)
    local G = QD.raid._xarp_geo
    local q = QD.XARP_HM.QUAD[S.role] or "SW"
    local order = nil
    local t = S.stare_tile
    if t == nil or not G.beside5(B, t.x, t.z) or F.pools[t.x .. "," .. t.z] then
        local best, bd = nil, nil
        for x = B.x + 31, B.x + 37 do
            for z = B.z + 32, B.z + 38 do
                if G.beside5(B, x, z) and not F.pools[x .. "," .. z] and not G.on_centre_line(B, x, z)
                    and G.quadrant(B, x, z) == q then
                    local d = QD.raid._tob_cheb(F.me.x, F.me.z, x, z)
                    if bd == nil or d < bd or (d == bd and (x < best.x or (x == best.x and z < best.z))) then
                        best, bd = { x = x, z = z }, d
                    end
                end
            end
        end
        S.stare_tile = best
        if best then QD.raid._tob_trace(S, F.tick, "my quadrant " .. q .. " at " .. (best.x - B.x) .. "," .. (best.z - B.z)) end
    end
    local goal = S.stare_tile or F.me
    local swing_tick = ((F.tick + 1 - S.Q) % 4 == 0)
    local safe = S.faced == nil or not G.in_quadrant(B, F.me.x, F.me.z, S.faced)
    if F.me.x ~= goal.x or F.me.z ~= goal.z then
        order = { mode = "walk", x = goal.x, z = goal.z }
    elseif swing_tick and safe and armed and F.boss and F.form == "combat" then
        order = { mode = "attack", npc = F.boss }
    elseif S.order and S.order.mode == "attack" then
        -- no swing off the rhythm, nor into his face
        order = { mode = "walk", x = F.me.x, z = F.me.z }
    end
    return order
end

function QD.raid.xarpus_hm_solve(opts)
    assert(QD.raid.tob_mode == "hard", "xarpus_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.raid._xarp_run("xarpus_hm_solve", QD.raid._xarp_hm_stare, opts)
end
