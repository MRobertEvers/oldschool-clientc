-- tob_xarpus_stare_probe: Xarpus' phase 3 retaliation under observation, solo
-- Normal, ::god on. The stock solver plays phases 1 and 2; the moment he
-- stands up `::tobxarpusarm` puts him under his screech, and from the screech
-- this file's stare takes over: on every other turn it walks INTO the quadrant
-- he faces and swings (bait), on the rest it swings from one he does not face.
-- For the first half of the stare it swings on turn ticks and turn+4, so every
-- hit lands between turns; for the second half on turn+3 and turn+7, so every
-- other hit lands ON a turn tick, against a quadrant nobody could see yet.
--
-- The oracle is the server's own tick log, not the client's view: the turn is
-- the `npc_face` row, the swing the whip-attack `player_anim` row on W, the
-- swinger's tile the `player_tile` row of W (one a tick, the tile at its end),
-- and a retaliation is a `hit_player` naming his npc slot with `raw` >= 40 - no
-- pool (<= 16) or stomp (<= 9) reaches that. A melee hit lands on W+1, in the
-- npc phase, after his `[ai_timer]` has made that tick's turn. For every swing
-- landing after the first turn, a retaliation must land on W+1 exactly when the
-- swinger's tile is in the quadrant he faces on W+1 (tob_xarpus.rs2
-- ~tob_xarpus_retaliate, ~tob_xarpus_in_quadrant, boundary inclusive); before
-- the first turn nothing is retaliated.
--
--   python3 tools/raid_gate/run.py tob_xarpus_stare_probe --no-build --no-publish
local WORN = {
    "game_pest_melee_helm", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves",
    "abyssal_tentacle", "dragon_parryingdagger", "zenyte_amulet_enchanted", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv", "::god 1" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::clearinv",
    "::give br_4dosepotionofsaradomin 4", "::give br_4dose2restore 4", "::give br_4dose2combat 2",
}) do kit[#kit + 1] = line end

local MIN_RETALIATION = 40
local WANT_TURNS = 32
local LATE_RHYTHM_FROM = 16

local function run(t)
    local R = QD_ROOT.raid
    local G = R._xarp_geo
    t.check("xstare.ticklog", t.ticklog.start() == "ok", "")
    local r, d = t.raid.enter("tob", "xarpus", { mode = "normal" })
    t.check("xstare.enter", r == "ok", tostring(d))
    t.cheat("::synctimers", false)
    local sr, sd = t.cheat("::tobsyncroom")
    t.check("xstare.sync", sr == "ok", tostring(sd))

    -- Phase 3 as soon as he stands up.
    local orig_step = R._xarp_step
    local arm_sent = false
    R._xarp_step = function(S, F)
        local res = orig_step(S, F)
        if F.form == "combat" and not arm_sent then
            arm_sent = true
            QD_ROOT.cheat("::tobxarpusarm", false)
        end
        return res
    end

    local P = { goal_for = nil, goal = nil }
    local function stare(S, F, B, armed)
        if S.turns ~= P.goal_for then
            P.goal_for = S.turns
            local bait = S.faced ~= nil and (S.turns % 2 == 1)
            local best, bd = nil, nil
            for x = B.x + 31, B.x + 37 do
                for z = B.z + 32, B.z + 38 do
                    if G.beside5(B, x, z) and not F.pools[x .. "," .. z] and not G.on_centre_line(B, x, z) then
                        local inside = S.faced ~= nil and G.in_quadrant(B, x, z, S.faced)
                        if inside == bait then
                            local dd = QD_ROOT.raid._tob_cheb(F.me.x, F.me.z, x, z)
                            if bd == nil or dd < bd then best, bd = { x = x, z = z }, dd end
                        end
                    end
                end
            end
            P.goal = best
        end
        local goal = P.goal or F.me
        if F.me.x ~= goal.x or F.me.z ~= goal.z then
            return { mode = "walk", x = goal.x, z = goal.z }
        end
        local rhythm = (S.turns >= LATE_RHYTHM_FROM) and 3 or 0
        local swing_tick = ((F.tick + 1 - S.Q) % 4 == rhythm)
        if S.turns < WANT_TURNS and swing_tick and armed and F.boss and F.form == "combat" then
            return { mode = "attack", npc = F.boss }
        end
        if S.order and S.order.mode == "attack" then
            return { mode = "walk", x = F.me.x, z = F.me.z }
        end
        return nil
    end

    local result, detail, S = R._xarp_run("xarpus_stare_probe", stare, { max_ticks = 1400 })
    R._xarp_step = orig_step
    t.note("solve: " .. tostring(result) .. " " .. tostring(detail))
    t.check("xstare.armed", arm_sent, "never saw him stand up")
    t.check("xstare.state", S ~= nil and S.base ~= nil, "no room base")
    local B = S.base

    local ids = R._tob_symbols("xarpus_stare_probe", {
        { "boss", "npc", "tob_xarpus_combat" }, { "whip", "seq", "slayer_abyssal_whip_attack" },
    })
    local function rows(kind)
        local _, out = t.ticklog.rows({ kind = kind })
        t.ticks(1)
        return out or {}
    end

    local Q = nil
    for _, s in ipairs(rows("npc_say")) do
        if s.type == ids.boss and Q == nil and tostring(s.text):find("Screeeech") then Q = s.tick end
    end
    t.check("xstare.screech", Q ~= nil, "no screech in the tick log")
    if Q == nil then
        t.finish(1)
        return
    end

    local turns = {}
    local boss_slot = nil
    for _, f in ipairs(rows("npc_face")) do
        if f.type == ids.boss and f.tick > Q then
            turns[#turns + 1] = { tick = f.tick, q = G.quadrant(B, f.x, f.z) }
            boss_slot = f.slot
        end
    end
    local tile_by_tick = {}
    for _, p in ipairs(rows("player_tile")) do tile_by_tick[p.tick] = p end
    local function facing_at(tick)
        local cur = nil
        for _, tn in ipairs(turns) do
            if tn.tick <= tick then cur = tn end
        end
        return cur
    end
    local retaliated = {}
    local amounts = {}
    for _, h in ipairs(rows("hit_player")) do
        if h.raw ~= nil and h.raw >= MIN_RETALIATION and h.npc_slot == boss_slot then
            retaliated[h.tick] = (retaliated[h.tick] or 0) + 1
            amounts[#amounts + 1] = h.raw
        end
    end
    local landed = 0
    for _, h in ipairs(rows("hit_npc")) do
        if h.type == ids.boss and h.tick > Q and (h.damage or 0) > 0 then landed = landed + 1 end
    end
    local said = 0
    local _, msgs = t.msg.last(100)
    for _, m in ipairs(msgs or {}) do
        if tostring(m.text):find("Xarpus is staring in your direction and retaliates with a powerful attack!", 1, true) then
            said = said + 1
        end
    end

    -- [between|on_turn][in|out] -> { swings, retaliated }
    local tally = { between = { inside = { 0, 0 }, outside = { 0, 0 } }, on_turn = { inside = { 0, 0 }, outside = { 0, 0 } } }
    local n_free, n_swings = 0, 0
    local wrong = {}
    local matched = {}
    for _, a in ipairs(rows("player_anim")) do
        if a.seq == ids.whip and a.tick > Q then
            n_swings = n_swings + 1
            local W, L = a.tick, a.tick + 1
            local here = tile_by_tick[W]
            local face = facing_at(L)
            local got = retaliated[L] ~= nil
            if got then matched[L] = true end
            if face == nil then
                n_free = n_free + 1
                if got then wrong[#wrong + 1] = "t" .. W .. " landed before the first turn: retaliated" end
            else
                local when = (face.tick == L) and "on_turn" or "between"
                local inside = G.in_quadrant(B, here.x, here.z, face.q)
                local cell = tally[when][inside and "inside" or "outside"]
                cell[1] = cell[1] + 1
                if got then cell[2] = cell[2] + 1 end
                if inside ~= got then
                    wrong[#wrong + 1] = string.format("swing t%d from %d,%d landing t%d (%s) faced %s: %s",
                        W, here.x, here.z, L, when, face.q, got and "retaliated" or "NOT retaliated")
                end
            end
        end
    end
    for tick, _ in pairs(retaliated) do
        if not matched[tick] then wrong[#wrong + 1] = "retaliation at t" .. tick .. " with no swing landing then" end
    end
    local lo, hi = nil, nil
    for _, v in ipairs(amounts) do
        if lo == nil or v < lo then lo = v end
        if hi == nil or v > hi then hi = v end
    end
    local function cell(c) return c[1] .. " (" .. c[2] .. " retaliated)" end
    local summary = string.format(
        "Q=%s turns=%d swings=%d; landing between turns: in gaze %s, out %s; landing ON a turn: in gaze %s, out %s; "
            .. "before the first turn %d; retaliations %d, raw %s..%s, message %d; hits landed on him %d",
        tostring(Q), #turns, n_swings, cell(tally.between.inside), cell(tally.between.outside),
        cell(tally.on_turn.inside), cell(tally.on_turn.outside), n_free, #amounts, tostring(lo), tostring(hi),
        said, landed)
    t.note(summary)
    for _, w in ipairs(wrong) do t.note("WRONG " .. w) end
    t.check("xstare.turns", #turns >= 4, summary)
    t.check("xstare.between_seen", tally.between.inside[1] >= 3 and tally.between.outside[1] >= 3, summary)
    t.check("xstare.on_turn_seen", tally.on_turn.inside[1] + tally.on_turn.outside[1] >= 3, summary)
    t.check("xstare.oracle", #wrong == 0, table.concat(wrong, "; "))
    t.check("xstare.amount", lo ~= nil and lo >= 50 and hi <= 105, summary)
    -- (the client's message ring holds 100 lines, so the early ones may be gone)
    t.check("xstare.message", said >= 1 and said <= #amounts, summary)
    t.finish(0)
end

return {
    id = "tob_xarpus_stare_probe",
    fixture = "fresh_lumbridge.ini",
    party = 1,
    max_frames = 200000,
    setup = kit,
    run = run,
}
