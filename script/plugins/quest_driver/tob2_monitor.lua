-- quest-driver / tob2_monitor: PREDICTED vs MEASURED, every tick (ToB v2).
--
-- TOB2_DESIGN.md "MONITOR". Called at the start of each tick, before the
-- decision, it compares what the executive expected for this tick
-- (S.expect, written by QD.T2.exec.emit a tick ago) with what F measures:
--
--   move     path[1]'s tile                      my server tile
--   swing    path[1].swing                       my attack animation started this tick
--   refused  a click accepted                    the drive verb's answer (noted at the send)
--   threat   max damage landing this tick        hp lost this tick
--   stall    movement                            unmoved with a walk under way
--   held     (PROBE) a held op's swing delay     the gap to my next swing
--   gap      a check every tick                  a tick with no expectation
--
-- Counts by rule go in the summary; the first MON_TRACE mismatches per rule
-- go to S.mon.lines with the tick (and the room trace). The monitor never
-- changes behaviour -- except that it is where my own swing is measured,
-- which the executive's swing clock reads (S.x2.swing_last / swing_next).
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.monitor = QD.T2.monitor or {}

QD.T2.MON_RULES = { "move", "swing", "refused", "threat", "stall", "held", "gap" }

function QD.T2.monitor.init(S)
    assert(S)
    local M = { rules = {}, lines = {}, seq_starts = nil, swings = 0, swing_seqs = nil, held_open = nil,
                not_swing = {} }
    -- NOT A SWING: every food and potion plays human_eat on the eater
    -- (consume scripts' anim(human_eat, 0)); counted as a swing it reset
    -- the weapon clock on every sip (v2p2 seat 5 t139-142: a combat potion
    -- read as a hammer swing pushed the next swing 4 ticks late)
    local er, eat_seq = api_drive.symbol("seq", "human_eat")
    assert(er == "ok", "tob2 monitor: no seq named human_eat")
    M.not_swing[eat_seq] = true
    for _, r in ipairs(QD.T2.MON_RULES) do M.rules[r] = { n = 0, checked = 0, traced = 0 } end
    S.mon = M
end

-- A mismatch for `rule` at tick `t`.
function QD.T2.monitor.note(S, rule, t, text)
    local M = S.mon
    assert(M, "tob2: QD.T2.monitor.init was not called")
    local R = M.rules[rule]
    assert(R, "tob2 monitor: no rule " .. tostring(rule))
    R.n = R.n + 1
    if rule == "refused" then R.checked = R.checked + 1 end
    if R.traced < QD.T2.V.MON_TRACE then
        R.traced = R.traced + 1
        local line = "mon " .. rule .. " t" .. tostring(t) .. " " .. text
        M.lines[#M.lines + 1] = line
        QD.raid._tob_trace(S, t, line)
    end
end

-- A swing animation? With the room's list (S.mon.swing_seqs = { [seq] =
-- true }), only those; without one, any start of my own animation.
function QD.T2.monitor.is_swing(S, seq)
    local set = S.mon.swing_seqs
    if set == nil then return seq ~= nil and seq >= 0 and not S.mon.not_swing[seq] end
    return set[seq] == true
end

-- My own animation starts since the last tick (players()' me row: seq,
-- seq_tick, seq_starts, seq_history). -> did a swing start this tick
function QD.T2.monitor.my_swing(S, F)
    local M = S.mon
    local pr, players = api_drive.players()
    if pr ~= "ok" then return false end
    local me = nil
    for _, p in ipairs(players) do if p.me then me = p end end
    if me == nil or me.seq_starts == nil then return false end
    local swung = false
    if M.seq_starts ~= nil and me.seq_starts > M.seq_starts then
        if me.seq_history then
            for _, h in ipairs(me.seq_history) do
                if h.n > M.seq_starts and QD.T2.monitor.is_swing(S, h.seq) then swung = true end
            end
        elseif QD.T2.monitor.is_swing(S, me.seq) then
            swung = true
        end
    end
    M.seq_starts = me.seq_starts
    F.my_seq, F.my_seq_tick = me.seq, me.seq_tick
    return swung
end

function QD.T2.monitor.check(S, F)
    assert(S)
    assert(F)
    local M = S.mon
    assert(M, "tob2: QD.T2.monitor.init was not called")
    local X = S.x2
    local d = F.tick
    local E = S.expect
    -- my swing, measured, and STAMPED on the server's clock -- the
    -- executive's weapon clock. A swing seen now went out either in tick d's
    -- player phase (clock d) or in the input phase between d-1 and d (clock
    -- d-1): the latter only for a press sent at d-1 that met a ready weapon
    -- (W <= d-1) without walking (a press out of reach walks in tick d and
    -- swings in its phase). The tick log's player_anim rows say so (v2p2,
    -- consecutive same-weapon swings: an input-phase swing at log tick d is
    -- followed by the next at d + speed, 12 of 12 whip, 16 of 16 blowpipe);
    -- stamped at d it put the clock a tick late and the plan predicted every
    -- swing after a fresh press one tick late ("unpredicted" at d+1, then
    -- "predicted, none" at d+2).
    F.my_swing = QD.T2.monitor.my_swing(S, F)
    if F.my_swing then
        M.swings = M.swings + 1
        local stamp = d
        -- (and only when I was in reach of the target's k = 0 footprint at
        -- the press: E.still, the executive's reach test)
        if E and E.tick == d and E.pressed and F.me.x == E.from.x and F.me.z == E.from.z
            and (E.w == nil or E.w <= d - 1) and E.still ~= false then
            stamp = d - 1
        end
        X.swing_last = stamp
        if X.swing_speed ~= nil then X.swing_next = stamp + X.swing_speed end
        F.my_swing_stamp = stamp
    end
    -- the held rule: a swing must not come before the clock the held op left
    -- (food: +FOOD_ATTACK_DELAY on a running delay, else nothing)
    if M.held_open and F.my_swing then
        local h = M.held_open
        local R = M.rules.held
        R.checked = R.checked + 1
        if h.w ~= nil and F.my_swing_stamp < h.w then
            QD.T2.monitor.note(S, "held", d, h.kind .. " at t" .. h.t .. ": swing at clock " .. F.my_swing_stamp
                .. " < W " .. h.w .. " (was " .. tostring(h.w0) .. ")")
        elseif R.traced < QD.T2.V.MON_TRACE then
            -- the observation itself (not a mismatch)
            R.traced = R.traced + 1
            M.lines[#M.lines + 1] = "mon held-probe t" .. d .. " " .. h.kind .. " at t" .. h.t .. " swing clock "
                .. F.my_swing_stamp .. " W " .. tostring(h.w0) .. "->" .. tostring(h.w)
        end
        M.held_open = nil
    end
    if E == nil then return end
    if E.tick ~= d then
        M.rules.gap.checked = M.rules.gap.checked + 1
        QD.T2.monitor.note(S, "gap", d, "expected t" .. tostring(E.tick))
        S.expect = nil
        return
    end
    M.rules.gap.checked = M.rules.gap.checked + 1
    -- move: path[1]'s tile vs my server tile
    local R = M.rules.move
    if E.mode == "loc" then
        -- a loc op: the server walks me toward the loc (the plan's step is
        -- not the prediction); `stall` checks the approach below
    else
        R.checked = R.checked + 1
        if F.me.x ~= E.x or F.me.z ~= E.z then
            QD.T2.monitor.note(S, "move", d, string.format("%s %d,%d>%d,%d got %d,%d", tostring(E.act),
                E.from.x, E.from.z, E.x, E.z, F.me.x, F.me.z))
        end
    end
    -- swing
    if E.swing or F.my_swing then
        R = M.rules.swing
        R.checked = R.checked + 1
        if E.swing ~= F.my_swing then
            QD.T2.monitor.note(S, "swing", d, (E.swing and "predicted, none" or "unpredicted swing")
                .. " (" .. tostring(E.mode) .. ", seq " .. tostring(F.my_seq) .. ")")
        end
    end
    -- threat: hp lost this tick vs the max damage predicted to land on it
    R = M.rules.threat
    R.checked = R.checked + 1
    local lost = E.hp - F.hp + (E.heal or 0)
    if lost > E.land_max + 0.5 then
        QD.T2.monitor.note(S, "threat", d, string.format("lost %d > max %.1f (exp %.1f) at %d,%d", lost,
            E.land_max, E.land_exp, E.from.x, E.from.z))
    end
    -- stall (a loc op): short of the loc and no nearer to it
    if E.loc then
        R = M.rules.stall
        R.checked = R.checked + 1
        local d0 = QD.raid._tob_cheb(E.from.x, E.from.z, E.loc.x, E.loc.z)
        local d1 = QD.raid._tob_cheb(F.me.x, F.me.z, E.loc.x, E.loc.z)
        if d0 > (E.loc.near or 1) and d1 >= d0 then
            QD.T2.monitor.note(S, "stall", d, "loc op at " .. E.loc.x .. "," .. E.loc.z .. ": " .. d0 .. " -> " .. d1)
        end
    end
    -- stall: a walk under way, and I did not move
    if E.walking then
        R = M.rules.stall
        R.checked = R.checked + 1
        if F.me.x == E.from.x and F.me.z == E.from.z then
            QD.T2.monitor.note(S, "stall", d, "walk to " .. E.x .. "," .. E.z .. " unmoved at " .. F.me.x .. "," .. F.me.z)
        end
    end
    -- refused: counted at the send (QD.T2.exec), the accepted ones here
    M.rules.refused.checked = M.rules.refused.checked + (E.sent or 0) - (E.refused or 0)
    -- a held op sent a tick ago opens the probe
    if E.held then M.held_open = { t = E.held.t, kind = E.held.kind, w0 = E.held.w0, w = E.held.w } end
end

-- "mon move 3/120 swing 1/40 ..." (mismatches / checks per rule)
function QD.T2.monitor.summary(S)
    local M = S.mon
    if M == nil then return "mon -" end
    local out = {}
    for _, r in ipairs(QD.T2.MON_RULES) do
        local R = M.rules[r]
        out[#out + 1] = r .. " " .. R.n .. "/" .. R.checked
    end
    return "mon " .. table.concat(out, " ") .. " swings " .. M.swings
        .. (M.swing_seqs == nil and " (any-seq)" or "") .. "; " .. table.concat(M.lines, " | ")
end
